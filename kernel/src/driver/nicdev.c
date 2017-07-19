#include "nicdev.h"

#define ETHER_TYPE_IPv4		0x0800		///< Ether type of IPv4
#define ETHER_TYPE_ARP		0x0806		///< Ether type of ARP
#define ETHER_TYPE_IPv6		0x86dd		///< Ether type of IPv6
#define ETHER_TYPE_LLDP		0x88cc		///< Ether type of LLDP
#define ETHER_TYPE_8021Q	0x8100		///< Ether type of 802.1q
#define ETHER_TYPE_8021AD	0x88a8		///< Ether type of 802.1ad
#define ETHER_TYPE_QINQ1	0x9100		///< Ether type of QinQ
#define ETHER_TYPE_QINQ2	0x9200		///< Ether type of QinQ
#define ETHER_TYPE_QINQ3	0x9300		///< Ether type of QinQ

#define endian16(v)		__builtin_bswap16((v))	///< Change endianness for 48 bits
#define endian48(v)		(__builtin_bswap64((v)) >> 16)	///< Change endianness for 48 bits

#define ETHER_BROADCAST		0xffffffffffff ///< MAC address is broadcast
#define ETHER_MULTICAST		((uint64_t)1 << 40)	///< MAC address is multicast
#define ID_BUFFER_SIZE		(MAX_NIC_DEVICE_COUNT * 8)


static bool (*rx_process)(void* _data, size_t size, void* context);
static void* rx_process_context;
static bool (*tx_process)(void* _data, size_t size, void* context);
static void* tx_process_context;
static bool (*srx_process)(void* _data, size_t size, void* context);
static void* srx_process_context;
static bool (*stx_process)(void* _data, size_t size, void* context);
static void* stx_process_context;

int nicdev_process_register(bool (*process)(void*, size_t, void*), void* context, NICDEV_PROCESS_TYPE type) {
	switch(type) {
		case NICDEV_RX_PROCESS:
			rx_process = process;
			rx_process_context = context;
			return 0;
		case NICDEV_TX_PROCESS:
			tx_process = process;
			tx_process_context = context;
			return 0;
		case NICDEV_SRX_PROCESS:
			srx_process = process;
			srx_process_context = context;
			return 0;
		case NICDEV_STX_PROCESS:
			stx_process = process;
			stx_process_context = context;
			return 0;
		default:
			return -1;
	}
}

int nicdev_process_unregister(NICDEV_PROCESS_TYPE type) {
	return nicdev_process_register(NULL, NULL, type);
}

typedef struct _Ether {
	uint64_t dmac: 48;			///< Destination address (endian48)
	uint64_t smac: 48;			///< Destination address (endian48)
	uint16_t type;				///< Ether type (endian16)
	uint8_t payload[0];			///< Ehternet payload
} __attribute__ ((packed)) Ether;

NICDevice* nicdevs[MAX_NIC_DEVICE_COUNT]; //key string
int nicdevs_count;

//for linux driver
//linux driver can't include glib header
extern int strncmp (const char *__s1, const char *__s2, size_t __n);
static NICDevice* nicdev_get0(NICDevice* nicdev, const char* name) {
	if(!strncmp(nicdev->name, name, MAX_NIC_NAME_LEN))
		return nicdev;
	else if(nicdev->next)
		return nicdev_get0(nicdev->next, name);

	return NULL;
}

NICDevice* nicdev_get(const char* name) {
	for(int i = 0; i < nicdevs_count; i++) {
		NICDevice* nicdev = nicdev_get0(nicdevs[i], name);
		if(nicdev) return nicdev;
	}

	return NULL;
}

int nicdev_register(NICDevice* nicdev) {
	if(nicdevs_count >= MAX_NIC_DEVICE_COUNT) return -1;

	if(nicdev_get(nicdev->name)) return -2;

	nicdevs[nicdevs_count++] = nicdev;

	return 0;
}

NICDevice* nicdev_unregister(const char* name) {
	for(int i = 0; i < nicdevs_count; i++) {
		if(!strncmp(nicdevs[i]->name, name, MAX_NIC_NAME_LEN)) {
			NICDevice* nicdev = nicdevs[i];
			nicdevs[i] = NULL;
			nicdevs_count--;
			nicdevs[i] = nicdevs[nicdevs_count];

			return nicdev;
		}
	}

	return NULL;
}

NICDevice* nicdev_get_default() {
	return nicdevs[0];
}

extern NIC* __nics[NIC_MAX_COUNT];
extern int __nic_count;

int nicdev_register_vnic(NICDevice* nicdev, VNIC* vnic) {
	if(nicdev->vnics_count >= MAX_VNIC_COUNT) return -1;

	for(int i = 0; i < nicdev->vnics_count; i++) {
		if(nicdev->vnics[i]->mac == vnic->mac) return -1;
	}

	nicdev->vnics[nicdev->vnics_count++] = vnic;
	vnic->vlan_proto = nicdev->vlan_proto;
	vnic->vlan_tci = nicdev->vlan_tci;

	__nics[__nic_count++] = vnic->nic;

	return vnic->id;
}

VNIC* nicdev_unregister_vnic(NICDevice* nicdev, uint32_t id) {
	for(int i = 0; i < nicdev->vnics_count; i++) {
		if(nicdev->vnics[i]->id == id) {
			VNIC* vnic = nicdev->vnics[i];
			nicdev->vnics[i] = NULL;
			nicdev->vnics[i] = nicdev->vnics[nicdev->vnics_count - 1];
			nicdev->vnics_count--;

			for(int j = 0; j < __nic_count; j++) {
				if(__nics[j] == vnic->nic) {
					__nics[j] = NULL;
					__nics[j] = __nics[__nic_count - 1];
					__nic_count--;
				}
			}

			return vnic;
		}
	}

	return NULL;
}

VNIC* nicdev_get_vnic(NICDevice* nicdev, uint32_t id) {
	if(!nicdev) return NULL;

	for(int i = 0; i < nicdev->vnics_count; i++) {
		if(nicdev->vnics[i]->id == id) return nicdev->vnics[i];
	}

	return NULL;
}

VNIC* nicdev_get_vnic_mac(NICDevice* nicdev, uint64_t mac) {
	if(!nicdev) return NULL;

	for(int i = 0; i < nicdev->vnics_count; i++) {
		if(nicdev->vnics[i]->mac == mac) return nicdev->vnics[i];
	}

	return NULL;
}

extern int strcmp(const char *s1, const char *s2);
VNIC* nicdev_get_vnic_name(NICDevice* nicdev, char* name) {
	if(!nicdev || !name) return NULL;

	for(int i = 0; i < nicdev->vnics_count; i++) {
		VNIC* vnic = nicdev->vnics[i];

		if(strcmp(vnic->name, name) == 0) return vnic;
	}

	return NULL;
}

VNIC* nicdev_update_vnic(NICDevice* nicdev, VNIC* src_vnic) {
	VNIC* dst_vnic = nicdev_get_vnic(nicdev, src_vnic->id);
	if(!dst_vnic) return NULL;

	if(dst_vnic->mac != src_vnic->mac) {
		if(nicdev_get_vnic_mac(nicdev, src_vnic->mac)) return NULL;

		dst_vnic->mac = src_vnic->mac;
	}

	//TODO fix bandwidth
	dst_vnic->rx_bandwidth = dst_vnic->nic->rx_bandwidth = src_vnic->rx_bandwidth;
	dst_vnic->tx_bandwidth = dst_vnic->nic->tx_bandwidth = src_vnic->tx_bandwidth;
	dst_vnic->padding_head = dst_vnic->nic->padding_head = src_vnic->padding_head;
	dst_vnic->padding_tail = dst_vnic->nic->padding_tail = src_vnic->padding_tail;

	return dst_vnic;
}


/**
 * rx process 
 */

int nicdev_rx(NICDevice* dev, void* data, size_t size) {
	return nicdev_rx0(dev, data, size, NULL, 0);
}

int nicdev_rx0(NICDevice* nicdev, void* data, size_t size,
		void* data_optional, size_t size_optional) {
	Ether* eth = data;
	if(size + size_optional < sizeof(Ether)) return NICDEV_PROCESS_PASS;

	if(unlikely(!!rx_process)) rx_process(data, size, rx_process_context);

	bool is_complete = false;
	uint64_t dmac = endian48(eth->dmac);
	for(int i = 0; i < nicdev->vnics_count; i++) {
		VNIC* vnic = nicdev->vnics[i];
		if(vnic->mac == dmac) {
			is_complete = true;
			goto recv;
		}

		if(vnic->flags & NIC_F_PROMISC) goto recv;

		if(dmac & ETHER_MULTICAST) {
			if(vnic->flags & NIC_F_MULTICAST) goto recv;
			if(dmac == ETHER_BROADCAST && vnic->flags & NIC_F_BROADCAST) goto recv;
		}

		continue;
recv:
		vnic_rx(vnic, (uint8_t*)eth, size, data_optional, size_optional);
	}
	if(is_complete) return NICDEV_PROCESS_COMPLETE;

	return NICDEV_PROCESS_PASS;
}

int nicdev_srx(VNIC* vnic, void* data, size_t size) {
	return nicdev_srx0(vnic, data, size, NULL, 0);
}

int nicdev_srx0(VNIC* vnic, void* data, size_t size,
		void* data_optional, size_t size_optional) {
	Ether* eth = data;

	if(size + size_optional < sizeof(Ether)) return NICDEV_PROCESS_PASS;

	if(unlikely(!!srx_process)) srx_process(data, size, srx_process_context);

	uint64_t smac = endian48(eth->smac);
	if(smac == vnic->mac) {
		vnic_srx(vnic, (uint8_t*)eth, size, data_optional, size_optional);
		return NICDEV_PROCESS_PASS;
	}

	return NICDEV_PROCESS_PASS;
}

typedef struct _TransmitContext{
	bool (*process)(Packet* packet, void* context);
	void* context;
} TransmitContext;

static bool transmitter(Packet* packet, void* context) {
	if(!packet) return false;

	if(unlikely(!!tx_process)) tx_process(packet->buffer + packet->start, packet->end - packet->start, tx_process_context);

	TransmitContext* transmitter_context = context;

	if(!transmitter_context->process(packet, transmitter_context->context)) return false;

	return true;
}

/**
 * @param dev NIC device
 * @param process function to process packets in NIC device
 * @param context context to be passed to process function
 *
 * @return number of packets proccessed
 */
//Task = budget
int nicdev_tx(NICDevice* nicdev,
		bool (*process)(Packet* packet, void* context), void* context) {
	TransmitContext transmitter_context = {
		.process = process,
		.context = context};

	int count = 0;
	for(; nicdev->round < MAX_VNIC_COUNT; nicdev->round++) {
		VNIC* vnic = nicdev->vnics[nicdev->round];
		if(!vnic) {
			nicdev->round = 0;
			break;
		}

		int budget = vnic->budget;
		while(budget--) {
			VNICError ret = vnic_tx(vnic, transmitter, &transmitter_context);

			if(ret == VNIC_ERROR_OPERATION_FAILED) // Transmiitter Error
				return count;
			else if(ret == VNIC_ERROR_RESOURCE_NOT_AVAILABLE) // There no Packet, Check next vnic
				break;

			count++;
		}
	}

	return count;
}

static bool stransmitter(Packet* packet, void* context) {
	if(!packet) return false;

	if(unlikely(!!stx_process)) tx_process(packet->buffer + packet->start, packet->end - packet->start, stx_process_context);

	TransmitContext* transmitter_context = context;

	if(!transmitter_context->process(packet, transmitter_context->context)) return false;

	return true;
}
/**
 * @param dev NIC device
 * @param process function to process packets in NIC device
 * @param context context to be passed to process function
 *
 * @return number of packets proccessed
 */
//Task = budget
int nicdev_stx(VNIC *vnic,
			   bool (*process)(Packet *packet, void *context), void *context) {
	TransmitContext transmitter_context = {
		.process = process,
		.context = context};

	VNICError ret = vnic_stx(vnic, stransmitter, &transmitter_context);

	if (ret == VNIC_ERROR_OPERATION_FAILED)
		return 0;
	else if (ret == VNIC_ERROR_RESOURCE_NOT_AVAILABLE) // There no Packet, Check next vnic
		return 0;

	return 0;
}

void nicdev_free(Packet* packet) {
	// TODO
	for(int i = 0; i < MAX_VNIC_COUNT; ++i) {
	}
}
