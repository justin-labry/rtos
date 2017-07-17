#include <string.h>
#include <lock.h>
#include <vnic.h>
#include <nic.h>

#define ID_BUFFER_SIZE (128 * MAX_VNIC_COUNT / 8)
static uint8_t id_map[ID_BUFFER_SIZE];

static uint64_t TIMER_FREQUENCY_PER_SEC;

void vnic__init_timer(uint64_t freq_per_sec) {
	TIMER_FREQUENCY_PER_SEC = freq_per_sec;
}

inline uint64_t timer_frequency() {
	uint64_t t;
	uint32_t* p = (uint32_t*)&t;
	asm volatile("rdtsc" : "=a"(p[0]), "=d"(p[1]));
	return t;
}

int vnic_alloc_id() {
	uint8_t bit;
	for(size_t bucket = 0; bucket < ID_BUFFER_SIZE; bucket++) {
		// If all slots in the bucket are not in use
		if(~id_map[bucket] & 0xff) {
			bit = 0x1;
			for(size_t slot = 0; slot < 8; slot++) {
				if(!(id_map[bucket] & bit)) {
					id_map[bucket] |= bit;
					return bucket * 8 + slot;
				}
				bit <<= 1;
			}
		}
	}
	return -1;
}

void vnic_free_id(int id) {
	int bucket = id / 8;
	uint8_t slot = 1 << (id % 8);

	// Mark slot in use and reliably remove it
	id_map[bucket] |= slot;
	id_map[bucket] ^= slot;
}

static uint64_t get_value(uint64_t* attrs, uint64_t key) {
	int i = 0;
	while(attrs[i * 2] != VNIC_NONE) {
		if(attrs[i * 2] == key)
			return attrs[i * 2 + 1];
		i++;
	}
	return (uint64_t)-1;
}

static VNICError has_mandatory(uint64_t* attrs) {
	uint8_t used[VNIC__MAND_END] = {0,};

	for(int i = 0; attrs[i] != VNIC_NONE; i += 2) {
		VNICAttributes key = attrs[i];
		if(key > VNIC__MAND_STRT && key < VNIC__MAND_END) {
			if(used[key])
				return VNIC_ERROR_ATTRIBUTE_CONFLICT;
			else
				used[key] = 1;
		}
	}

	int nkey = 0;
	for(int key = VNIC__MAND_STRT + 1; key < VNIC__MAND_END; ++key)
		nkey += used[key];

	return nkey == (VNIC__MAND_END - VNIC__MAND_STRT - 1) ?
		VNIC_ERROR_NOERROR : VNIC_ERROR_ATTRIBUTE_MISSING;
}

static VNICError nic_init(void* base, uint64_t* attrs) {
	if(has_mandatory(attrs) != VNIC_ERROR_NOERROR)
		return has_mandatory(attrs);
	if((uintptr_t)base == 0 || (uintptr_t)base % 0x200000 != 0)
		return VNIC_ERROR_INVALID_BASE;
	if(get_value(attrs, VNIC_POOL_SIZE) % 0x200000 != 0)
		return VNIC_ERROR_INVALID_POOLSIZE;

	int index = sizeof(NIC);

	NIC* nic = base;
	nic->magic = NIC_MAGIC_HEADER;
	nic->id = 0;
	nic->mac = get_value(attrs, VNIC_MAC);
	nic->flags = get_value(attrs, VNIC_FLAGS);
	nic->rx_bandwidth = get_value(attrs, VNIC_RX_BANDWIDTH);
	nic->tx_bandwidth = get_value(attrs, VNIC_TX_BANDWIDTH);
	nic->padding_head = get_value(attrs, VNIC_PADDING_HEAD);
	nic->padding_tail = get_value(attrs, VNIC_PADDING_TAIL);

	nic->rx.base = index;
	nic->rx.head = 0;
	nic->rx.tail = 0;
	nic->rx.size = get_value(attrs, VNIC_RX_QUEUE_SIZE);
	nic->rx.rlock = 0;
	nic->rx.wlock = 0;

	index += nic->rx.size * sizeof(uint64_t);
	index = ROUNDUP(index, 8);
	nic->tx.base = index;
	nic->tx.head = 0;
	nic->tx.tail = 0;
	nic->tx.size = get_value(attrs, VNIC_TX_QUEUE_SIZE);
	nic->tx.rlock = 0;
	nic->tx.wlock = 0;

	index += nic->tx.size * sizeof(uint64_t);
	index = ROUNDUP(index, 8);
	nic->srx.base = index;
	nic->srx.head = 0;
	nic->srx.tail = 0;
	nic->srx.size = get_value(attrs, VNIC_SLOW_RX_QUEUE_SIZE);
	nic->srx.rlock = 0;
	nic->srx.wlock = 0;

	index += nic->srx.size * sizeof(uint64_t);
	index = ROUNDUP(index, 8);
	nic->stx.base = index;
	nic->stx.head = 0;
	nic->stx.tail = 0;
	nic->stx.size = get_value(attrs, VNIC_SLOW_TX_QUEUE_SIZE);
	nic->stx.rlock = 0;
	nic->stx.wlock = 0;
	index += nic->stx.size * sizeof(uint64_t);
	index = ROUNDUP(index, 8);

	uint64_t poolsize = get_value(attrs, VNIC_POOL_SIZE);
	#define BITMAP_SIZE (poolsize - index) / NIC_CHUNK_SIZE;

	nic->pool.bitmap = index;
	index += BITMAP_SIZE;
	index = ROUNDUP(index, NIC_CHUNK_SIZE);
	if(index + NIC_CHUNK_SIZE > poolsize) return VNIC_ERROR_NO_MEMORY;

	nic->pool.pool = index;
	nic->pool.count = BITMAP_SIZE;
	nic->pool.index = 0;
	nic->pool.used = 0;
	nic->pool.lock = 0;

	nic->config = 0;

	memset(nic->config_head, 0, (size_t)((uintptr_t)nic->config_tail - (uintptr_t)nic->config_head));
	memset(base + nic->pool.bitmap, 0, nic->pool.count);

	return VNIC_ERROR_NOERROR;
}

bool vnic_init(VNIC* vnic, uint64_t* attrs) {
	if(nic_init(vnic->nic, attrs) != VNIC_ERROR_NOERROR)
		return false;

	strncpy(vnic->parent, (char*)get_value(attrs, VNIC_DEV), MAX_NIC_NAME_LEN);
	vnic->nic->id = vnic->id;
	vnic->budget = get_value(attrs, VNIC_BUDGET) ? : 32;
	vnic->magic = vnic->nic->magic;
	vnic->mac = vnic->nic->mac;
	vnic->flags = vnic->nic->flags;
	vnic->pool.bitmap = vnic->nic->pool.bitmap;
	vnic->pool.count = vnic->nic->pool.count;
	vnic->pool.pool = vnic->nic->pool.pool;

	vnic->rx_bandwidth = vnic->nic->rx_bandwidth;
	vnic->tx_bandwidth = vnic->nic->tx_bandwidth;
	vnic->padding_head = vnic->nic->padding_head;
	vnic->padding_tail = vnic->nic->padding_tail;

	vnic->rx = vnic->nic->rx;
	vnic->tx = vnic->nic->tx;
	vnic->srx = vnic->nic->srx;
	vnic->stx = vnic->nic->stx;

	vnic->rx_closed = vnic->tx_closed = timer_frequency();
	vnic->rx_wait = TIMER_FREQUENCY_PER_SEC * 8 / vnic->rx_bandwidth;
	vnic->rx_wait_grace = TIMER_FREQUENCY_PER_SEC / 100;
	vnic->tx_wait = TIMER_FREQUENCY_PER_SEC * 8 / vnic->tx_bandwidth;
	vnic->tx_wait_grace = TIMER_FREQUENCY_PER_SEC / 1000;

	return true;
}

VNICError vnic_update(VNIC* vnic, uint64_t* attrs) {
	// TODO impl
	return VNIC_ERROR_UNSUPPORTED;
}

Packet* vnic_alloc(VNIC* vnic, size_t size) {
	if(!lock_trylock(&vnic->nic->pool.lock))
		return NULL;

	uint8_t* bitmap = (void*)vnic->nic + vnic->pool.bitmap;
	uint32_t count = vnic->pool.count;
	void* pool = (void*)vnic->nic + vnic->pool.pool;

	uint32_t packet_size = sizeof(Packet) + vnic->padding_head + size + vnic->padding_tail;
	uint8_t req = (ROUNDUP(packet_size, NIC_CHUNK_SIZE)) / NIC_CHUNK_SIZE;
	uint32_t index = vnic->nic->pool.index;

	// Find tail
	uint32_t idx = 0;
	for(idx = index; idx <= count - req; idx++) {
		for(uint32_t j = 0; j < req; j++) {
			uint32_t pos = idx + j; // base + i
			if(bitmap[pos] == 0) {
				continue;
			} else {
				// another packet uses it. jump to the end of packet chunk
				idx += j + bitmap[pos];
				goto next;
			}
		}

		goto found;
next:
		idx -= 1; // for loop increases idx for next loop
	}

	// Find head
	for(idx = 0; idx < index - req; idx++) {
		for(uint32_t j = 0; j < req; j++) {
			if(bitmap[idx + j] == 0) {
				continue;
			} else {
				idx += j + bitmap[idx + j];
				goto notfound;
			}
		}

		goto found;
	}

notfound:
	// Not found
	lock_unlock(&vnic->nic->pool.lock);
	return NULL;

found:
	vnic->nic->pool.index = idx + req;
	for(uint32_t k = 0; k < req; k++) {
		bitmap[idx + k] = req - k;
	}

	vnic->nic->pool.used += req;

	lock_unlock(&vnic->nic->pool.lock);

	Packet* packet = pool + (idx * NIC_CHUNK_SIZE);
	packet->time = 0;
	packet->start = 0;
	packet->end = 0;
	packet->size = (req * NIC_CHUNK_SIZE) - sizeof(Packet);

	return packet;
}

bool vnic_free(VNIC* vnic, Packet* packet) {
	NIC* nic = nic_find_by_packet(packet);
	if(!nic || vnic->nic->id != nic->id)
		return false;

	uint8_t* bitmap = (void*)vnic->nic + vnic->pool.bitmap;
	uint32_t count = vnic->pool.count;
	void* pool = (void*)vnic + vnic->pool.pool;

	uint32_t idx = ((uintptr_t)packet - (uintptr_t)pool) / NIC_CHUNK_SIZE;
	if(idx >= count)
		return false;

	uint8_t req = bitmap[idx];
	if(idx + req > count)
		return false;

	for(uint32_t i = idx + req - 1; i >= idx; i--)
		bitmap[i] = 0;

	lock_lock(&vnic->nic->pool.lock);
	vnic->nic->pool.used -= req;
	lock_unlock(&vnic->nic->pool.lock);

	return true;
}

VNICError vnic_rx(VNIC* vnic, uint8_t* buf1, size_t size1, uint8_t* buf2, size_t size2) {
	const uint64_t t = timer_frequency();
	const size_t size = size1 + size2;
	if(vnic->rx_closed - vnic->rx_wait_grace > t) // TODO
		return -1;

	if(!lock_trylock(&vnic->nic->rx.wlock))
		return VNIC_ERROR_RESOURCE_NOT_AVAILABLE;

	vnic->rx.head = vnic->nic->rx.head;
	if(queue_available(&vnic->rx)) {
		Packet* packet = vnic_alloc(vnic, size);
		if(!packet) {
			lock_unlock(&vnic->nic->rx.wlock);
			goto drop;
		}

		memcpy(packet->buffer + packet->start, buf1, size1);
		if(size2)
			memcpy(packet->buffer + packet->start + size1, buf2, size2);
		packet->end = packet->start + size;

		if(queue_push(vnic->nic, &vnic->rx, packet)) {
			vnic->nic->rx.tail = vnic->rx.tail;
			lock_unlock(&vnic->nic->rx.wlock);

			if(vnic->rx_closed > t)
				vnic->rx_closed += vnic->rx_wait * size;
			else
				vnic->rx_closed = t + vnic->rx_wait * size;

			vnic->input_packets += 1;
			vnic->input_bytes += size;
			return VNIC_ERROR_NOERROR;
		} else {
			lock_unlock(&vnic->nic->rx.wlock);
			nic_free(packet);
			goto drop;
		}
	} else {
		lock_unlock(&vnic->nic->rx.wlock);
		goto drop;
	}

drop:
	vnic->input_drop_packets += 1;
	vnic->input_drop_bytes += size;
	return VNIC_ERROR_RESOURCE_NOT_AVAILABLE;
}

VNICError vnic_rx2(VNIC* vnic, Packet* packet) {
	// For VNICs belonging to the same VM: exchanging is done by putting packets in the queue
	// For VNICs not in the same VM: packets are replicated for exchange
	uint64_t t = timer_frequency();
	if(vnic->rx_closed - vnic->rx_wait_grace > t)
		goto drop;
	if(!lock_trylock(&vnic->nic->rx.wlock))
		goto drop;

	vnic->rx.head = vnic->nic->rx.head;
	if(queue_push(vnic->nic, &vnic->rx, packet)) {
		vnic->nic->rx.tail = vnic->rx.tail;
		lock_unlock(&vnic->nic->rx.wlock);

		if(vnic->rx_closed > t)
			vnic->rx_closed += vnic->rx_wait * (packet->end - packet->start);
		else
			vnic->rx_closed = t + vnic->rx_wait * (packet->end - packet->start);

		vnic->input_packets += 1;
		vnic->input_bytes += packet->end - packet->start;
		return VNIC_ERROR_NOERROR;
	} else {
		lock_unlock(&vnic->nic->rx.wlock);
		nic_free(packet);
		goto drop;
	}

drop:
	vnic->input_drop_packets += 1;
	vnic->input_drop_bytes += packet->end - packet->start;
	return VNIC_ERROR_RESOURCE_NOT_AVAILABLE;
}

bool vnic_has_srx(VNIC* vnic) {
	vnic->srx.head = vnic->nic->srx.head;
	return queue_available(&vnic->srx);
}

bool vnic_srx(VNIC* vnic, uint8_t* buf1, size_t size1, uint8_t* buf2, size_t size2) {
	if(!lock_trylock(&vnic->nic->srx.wlock))
		return false;

	size_t size = size1 + size2;
	vnic->srx.head = vnic->nic->srx.head;
	if(queue_available(&vnic->srx)) {
		Packet* packet = vnic_alloc(vnic, size);
		if(packet == NULL) {
			lock_unlock(&vnic->nic->srx.wlock);
			return false;
		}

		memcpy(packet->buffer + packet->start, buf1, size1);
		memcpy(packet->buffer + packet->start + size1, buf2, size2);

		packet->end = packet->start + size;

		if(queue_push(vnic->nic, &vnic->srx, packet)) {
			vnic->nic->srx.tail = vnic->srx.tail;
			lock_unlock(&vnic->nic->srx.wlock);
			return true;
		} else {
			lock_unlock(&vnic->nic->srx.wlock);
			nic_free(packet);
			return false;
		}
	} else {
		lock_unlock(&vnic->nic->srx.wlock);
		return false;
	}
}

bool vnic_srx2(VNIC* vnic, Packet* packet) {
	if(!lock_trylock(&vnic->nic->srx.wlock))
		return false;
	vnic->srx.head = vnic->nic->srx.head;
	if(queue_push(vnic->nic, &vnic->srx, packet)) {
		vnic->nic->srx.tail = vnic->srx.tail;
		lock_unlock(&vnic->nic->srx.wlock);
		return true;
	} else {
		lock_unlock(&vnic->nic->srx.wlock);
		nic_free(packet);
		return false;
	}
}

bool vnic_has_tx(VNIC* vnic) {
	vnic->tx.tail = vnic->nic->tx.tail;
	return !queue_empty(&vnic->tx);
}

VNICError vnic_tx(VNIC* vnic, bool (*transmitter)(Packet*, void*), void* transmitter_context) {
	if(!vnic_has_tx(vnic))
		return VNIC_ERROR_RESOURCE_NOT_AVAILABLE;

	uint64_t t = timer_frequency();
	if(vnic->tx_closed - vnic->tx_wait_grace > t)
		return VNIC_ERROR_RESOURCE_NOT_AVAILABLE;

	if(!lock_trylock(&vnic->nic->tx.rlock))
		return VNIC_ERROR_RESOURCE_NOT_AVAILABLE;

	bool transmitted	= false;

	vnic->tx.tail		= vnic->nic->tx.tail;
	Packet* packet		= queue_pop(vnic->nic, &vnic->tx);
	vnic->nic->tx.head	= vnic->tx.head;

	if(packet) {
		uint64_t packet_size = packet->end - packet->start;
		if(vnic->tx_closed > t)
			vnic->tx_closed += vnic->tx_wait * packet_size;
		else
			vnic->tx_closed = t + vnic->tx_wait * packet_size;

		transmitted = transmitter(packet, transmitter_context);
		if(transmitted) {
			vnic->output_packets += 1;
			vnic->output_bytes += packet_size;
		} else {
			vnic->output_drop_packets += 1;
			vnic->output_drop_bytes += packet_size;
		}
	}

	lock_unlock(&vnic->nic->tx.rlock);
	return transmitted ? VNIC_ERROR_NOERROR : VNIC_ERROR_OPERATION_FAILED;
}

bool vnic_has_stx(VNIC* vnic) {
	vnic->stx.tail = vnic->nic->stx.tail;
	return !queue_empty(&vnic->stx);
}

VNICError vnic_stx(VNIC* vnic, bool (*transmitter)(Packet*, void*), void* transmitter_context) {
	if(!vnic_has_stx(vnic))
		return VNIC_ERROR_RESOURCE_NOT_AVAILABLE;

	if(!lock_trylock(&vnic->nic->stx.rlock))
		return VNIC_ERROR_RESOURCE_NOT_AVAILABLE;

	bool transmitted	= false;

	vnic->stx.tail		= vnic->nic->stx.tail;
	Packet* packet		= queue_pop(vnic->nic, &vnic->stx);
	vnic->nic->stx.head	= vnic->stx.head;

	if(packet) {
		uint64_t packet_size = packet->end - packet->start;

		transmitted = transmitter(packet, transmitter_context);
		if(transmitted) {
			vnic->output_packets += 1;
			vnic->output_bytes += packet_size;
		} else {
			vnic->output_drop_packets += 1;
			vnic->output_drop_bytes += packet_size;
		}
	}

	lock_unlock(&vnic->nic->stx.rlock);
	return transmitted ? VNIC_ERROR_NOERROR : VNIC_ERROR_OPERATION_FAILED;
}
