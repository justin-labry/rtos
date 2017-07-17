#include <stdio.h>
#include <string.h>
// PacketNign kernel header
#include <gmalloc.h>
#include <port.h>
#include <pci.h>
#include <driver/nicdev.h>
#include <vnic.h>
#include <timer.h>
#include <net/vlan.h>
#include <net/ether.h>
// Virtio driver header
#include "virtio.h"
#include "virtio_config.h"
#include "virtio_ring.h"
#include "virtio_net.h"

#define VNET_HDR_LEN		12
#define MAX_DEVICE_COUNT	8
#define PAGE_SIZE		4096
#define MAX_BUF_SIZE		1526 // MTU + VNET_HDR_LEN

#define BUDGET_SIZE		64

//extern int printf (const char *__restrict __format, ...);

typedef struct {
	/* Common structure */
	uint64_t features;
	uint32_t ioaddr;
	uint8_t status;

	/* Specified structure */
	  /* VirtI/O over PCI */
	PCI_Device* dev;
	  /* VirtI/O network device */
	VirtIONetConfig config;
} VirtIODevice;

typedef struct {
	VirtIODevice vdev;
	VirtQueue *rvq, *svq, *cvq;
	NICDevice* priv;
} VirtNetPriv;

/* Pseudo header used by add_buf for transmit */
// TODO: Partial checksum, GSO needs to be implemented. At that time, real virtio header replaces it
static void* pseudo_vnet_hdr;

/* Check whether device used avail buffer */
static inline bool hasUsedIdx(VirtQueue* vq) {
	return vq->vring.used->idx != vq->last_used_idx;
}

/* Let other side know about buffer changed */
static void kick(VirtQueue* vq) {
	// Data in guest OS need to be set before we update avail ring index
	asm volatile("sfence" ::: "memory"); // wmb()

	vq->vring.avail->idx += vq->num_added;
	vq->num_added = 0;

	// Need to update avail index before notify otherside
	asm volatile("mfence" ::: "memory"); // mb()

	// We force to notify here in that VRING_USED_F_NO_NOTIFY is simply an optimzation
	port_out16(vq->ioaddr + VIRTIO_PCI_QUEUE_NOTIFY, vq->index);
}

/* Add available buffer which host OS can use */
static int add_buf(VirtQueue* vq, void* buffer, uint32_t len) {
	uint32_t head = vq->free_head;
	Vring* vr = &vq->vring;

	switch(vq->index) {
		case VIRTIO_RX_QUEUE_IDX : 
			vq->free_head = (vq->free_head + 1) % vr->num;

			vr->desc[head].flags = VRING_DESC_F_WRITE;
			vr->desc[head].addr = (uint64_t)buffer;
			vr->desc[head].len = len;

			vq->num_free--;

			break;

		case VIRTIO_TX_QUEUE_IDX : 
			vq->free_head = (vq->free_head + 2) % vr->num;

			// Add real packet next
			Packet* packet = (Packet*)buffer;
			vr->desc[head + 1].addr = (uint64_t)(packet->buffer + packet->start);
			vr->desc[head + 1].len = len;

			vq->num_free--;

			break;

		case VIRTIO_CTRL_QUEUE_IDX : 
			vq->free_head = (vq->free_head + 3) % vr->num;

			vr->desc[head].flags = VRING_DESC_F_NEXT;
			vr->desc[head].addr = (uint64_t)buffer;
			vr->desc[head].len = 2;

			uint8_t* ptr = (uint8_t*)buffer;

			vr->desc[head + 1].flags = VRING_DESC_F_NEXT;
			vr->desc[head + 1].addr = (uint64_t)&ptr[2];
			vr->desc[head + 1].len = 1;

			vr->desc[head + 2].flags = VRING_DESC_F_WRITE;
			vr->desc[head + 2].addr = (uint64_t)&ptr[3];
			vr->desc[head + 2].len = 1;

			break;

		default : 
			return -1;
	}

	// Put descriptor head index in avail ring
	int avail = (vr->avail->idx + vq->num_added++) % vr->num;
	vr->avail->ring[avail] = head;

	// Set data buffer for later use
	vq->data[avail] = buffer;

	// Must notify other side of new buffers after add_buf by kick
	return 0;
}

/* Get used buffer which host OS used */
static void* get_buf(VirtQueue* vq, uint32_t* len) {
	if(!hasUsedIdx(vq)) {
		return NULL;
	}

	// Data in host OS should be exposed before guest OS reads
	asm volatile("lfence" ::: "memory"); //rmb();

	if(len)
		*len = vq->vring.used->ring[vq->last_used_idx % vq->vring.num].len;

	int used = vq->last_used_idx % vq->vring.num;

	// Queue needs to trace the number of free descriptors for buffer overflow
	vq->num_free++;
	vq->last_used_idx++;

	return vq->data[used];
}

/* Get virtio configuration */
static void get_config(uint32_t ioaddr, uint32_t offset, void *buf, uint32_t len) {
	void* ioaddr_offset = (void*)(uint64_t)(ioaddr + 20 + offset);

	uint8_t *ptr = buf;
	for (uint32_t i = 0; i < len; i++) 
		ptr[i] = port_in8((uint16_t)(uint64_t)ioaddr_offset + i);
}

/* Check whether virtio device has specific features */
static bool device_has_feature(const VirtIODevice* vdev, uint32_t fbit) {
	if(fbit > 32) {
		printf("Feature bit needs to be under 32\n");
		return 0;
	}

	return 1UL & (vdev->features >> fbit);
}

/* Synchronize and determine features with host */
static int synchronize_features(VirtIODevice* vdev) {
	uint64_t device_features;
	uint64_t driver_features;

	// Figure out what features device supports
	device_features = port_in32(vdev->ioaddr + VIRTIO_PCI_HOST_FEATURES);	

	// Features supported by both device and driver
	uint32_t fbit;
	driver_features = 0;
	int count = sizeof(feature_table) / sizeof(uint32_t);

	for(int i = 0; i < count; i++) {
		fbit = feature_table[i];
		if(fbit > 32) {
			printf("We only support 32 feature bits\n");
			return -1;
		}
		driver_features |= (1ULL << fbit);
	}
	driver_features &= device_features;

	// Finally determine features that we are going to use 
	port_out32(vdev->ioaddr + VIRTIO_PCI_GUEST_FEATURES, driver_features);
	vdev->features |= driver_features;

	return 0;
}

/* Add status to virtio configuration status space */
static void add_status(VirtIODevice* vdev, uint8_t status) {
	vdev->status |= status;
	port_out8(vdev->ioaddr + VIRTIO_PCI_STATUS, vdev->status);
}

/* Prepare pseudo header in the empty send buffers */
static int prepare_send_buf(VirtQueue* vq, uint32_t num) {
	// Add virtio_net_hdr - virtio_net_hdr is always 0, so we just use pseudo header
	Vring* vr = &vq->vring;
	for(uint32_t i = 0; i < num / 2; i++) {
		vr->desc[2 * i].addr = (uint64_t)pseudo_vnet_hdr;
		vr->desc[2 * i].flags = VRING_DESC_F_NEXT;
		vr->desc[2 * i].len = VNET_HDR_LEN;
	}

	return 0;
}

/* Prepare in the empty receive buffers */
static int prepare_recv_buf(VirtQueue* vq, uint32_t num) {
	void* buffer[num]; 
	//int size = PAGE_ALIGN(vring_size(num, VIRTIO_PCI_VRING_ALIGN)); // check
	int size = (vring_size(num, VIRTIO_PCI_VRING_ALIGN) + PAGE_SIZE) & ~PAGE_SIZE;

	for(uint32_t i = 0; i < num; i++) {
		buffer[i] = (void*)((uint64_t)vq->vring.desc + size + PAGE_SIZE * i);
		if(!buffer[i]) {
			return -1;
		}

		if(add_buf(vq, buffer[i], MAX_BUF_SIZE))
			return -2;

	}

	// Notify otherside of new buffer
	kick(vq);

	return 0;
}

/* Initializing function for virtqueues */
static int init_vq(VirtIODevice* vdev, uint32_t index, VirtQueue* vqs[]) {
	// Select the queue we're interested in
	port_out16(vdev->ioaddr + VIRTIO_PCI_QUEUE_SEL, index);

	// Check if queue is either not available or already active
	int num = port_in32(vdev->ioaddr + VIRTIO_PCI_QUEUE_NUM);
	if (!num || port_in32(vdev->ioaddr + VIRTIO_PCI_QUEUE_PFN))
		return -1;

	//int size = PAGE_ALIGN(vring_size(num, VIRTIO_PCI_VRING_ALIGN)); // check
	int size = (vring_size(num, VIRTIO_PCI_VRING_ALIGN) + PAGE_SIZE) & ~PAGE_SIZE;

	// Alloc and initialize virtqueue 
	vqs[index] = gmalloc(sizeof(VirtQueue) + sizeof(void*) * num /* For token data */);
	memset(vqs[index], 0, sizeof(VirtQueue) + sizeof(void*) * num); 
	vqs[index]->size = num;
	vqs[index]->last_used_idx = 0;
	vqs[index]->num_added = 0;
	vqs[index]->index = index;
	vqs[index]->ioaddr = vdev->ioaddr;

	// Assign vring memory space. It must be aligned by page size (4096)
	if(size > 0x200000 /* 2MB */) {
		printf("VirtQueue size is over 2MB\n");
		return -2;
	}

	void* queue = bmalloc(1);
	if(!queue) {
		printf("Queue bmalloc failed\n");
		return -3;
	}
	memset(queue, 0x0, 0x200000); 

	// Activate the queue
	port_out32(vdev->ioaddr + VIRTIO_PCI_QUEUE_PFN, (uint32_t)(uint64_t)queue >> VIRTIO_PCI_QUEUE_ADDR_SHIFT);

	// Create the vring 
	vring_init(&vqs[index]->vring, num, queue, VIRTIO_PCI_VRING_ALIGN);

	// We don't have interrupt handler. Tell otherside not to interrupt us
	vqs[index]->vring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;

	// Put everything in free lists 
	vqs[index]->num_free = num;
	vqs[index]->free_head = 0;

	for(int i = 0; i < num; i++) {
		vqs[index]->vring.desc[i].next = i + 1;
	}

	return 0;
}

/* Probing function for PCI device */
static int virtio_pci_probe(VirtIODevice* vdev) {
	// Read I/O address
	vdev->ioaddr = pci_read32(vdev->dev, PCI_BASE_ADDRESS_0) & ~3;

	if(!vdev->ioaddr) 
		return -1;

	// Enable device
	pci_enable(vdev->dev);

	// Reset device 
	add_status(vdev, 0);

	// OS notice device from now
	add_status(vdev, VIRTIO_CONFIG_S_ACKNOWLEDGE);

	// OS notice driver from now
	add_status(vdev, VIRTIO_CONFIG_S_DRIVER);

	return 0;
}

/* Probing function for VirtI/O network device */
static int virtnet_probe(VirtNetPriv* priv) {
	VirtIODevice* vdev = &priv->vdev;
	// VirtI/O network device has three queues (receive, transmit, control)
	VirtQueue* vqs[3];

	// Confiuration may specify what MAC to use. Otherwise set designated MAC
	get_config(vdev->ioaddr, 0, vdev->config.mac, ETH_ALEN);
	if(!vdev->ioaddr) {
		memcpy(vdev->config.mac, "\0GURUM", ETH_ALEN);
	}

	// Get link status 
	get_config(vdev->ioaddr, 6, &vdev->config.status, 2);
	
	// Check whether we supports control queue 
	int nvqs = device_has_feature(&priv->vdev, VIRTIO_NET_F_CTRL_VQ) ? 3 : 2;

	// Set for queues
	for(int i = 0; i < nvqs; i++) 
		if(init_vq(vdev, i, vqs))
			return -1;

	priv->rvq = vqs[0];
	priv->svq = vqs[1];
	if(nvqs == 3)
		priv->cvq = vqs[2];

	// Make pseudo header for transmit
	pseudo_vnet_hdr = gmalloc(sizeof(VirtIONetHDR));
	bzero(pseudo_vnet_hdr, sizeof(VirtIONetHDR));

	// Prepare receive buffers in advance  
	if(prepare_recv_buf(priv->rvq, priv->rvq->size))
		return -2;
	
	// Prepare pseudo header in send buffers in advance  
	if(prepare_send_buf(priv->svq, priv->rvq->size))
		return -3;

	// Device is alive at this point
	add_status(vdev, VIRTIO_CONFIG_S_DRIVER_OK);

	return 0;
}

/* Send control command to VirtI/O network device */
static bool virtnet_send_command(VirtNetPriv* priv, uint8_t class, uint8_t cmd, void* data) {
	VirtIONetCtrlPacket* ctrl = gmalloc(sizeof(VirtIONetCtrlPacket));
	memset(ctrl, 0, sizeof(VirtIONetCtrlPacket));

	// Controlling RX mode is only available now 
	if(class != VIRTIO_NET_CTRL_RX) {
		printf("[%02d] Class command not supported\n", class);
		return false;
	}
	ctrl->class = class;
	ctrl->cmd = cmd;
	ctrl->ack = ~0;

	// TODO: Other classes need to be implemented. e.g VLAN, MAC Filtering...
	ctrl->cmd_specific_data = *(uint8_t*)data;

	if(add_buf(priv->cvq, ctrl, 4))
		return false;

	// Notify otherside of new buffer
	kick(priv->cvq);

	// Wait for a sec till host send ACK 
	timer_mwait(100);

	VirtIONetCtrlPacket* ctrl_ack = (VirtIONetCtrlPacket*)get_buf(priv->cvq, NULL);

	if(!ctrl_ack) {
		printf("Send command failed\n");
		gfree(ctrl);
		return false;
	}

	if(ctrl_ack->ack != VIRTIO_NET_OK) {
		printf("Command set failed\n");
		gfree(ctrl);
		return false;
	}
	
	gfree(ctrl);
	return true;
}

/* Function for packet receive */
static int virtnet_receive(VirtNetPriv* priv, void* buf, uint32_t len) {
	// Instead of calling add_buf, we just notify that buffer index is updated 
	VirtQueue* vq = priv->rvq;
	vq->num_added++; 
	VirtIONetPacket* vp = (VirtIONetPacket*)buf;
	NICDevice* nicdev = priv->priv;

	Ether* ether = (Ether*)vp->data;
	if(ether->type == endian16(ETHER_TYPE_8021Q)) {
		VLAN* vlan = (VLAN*)ether->payload;
		for(nicdev = nicdev->next; nicdev; nicdev = nicdev->next) {
			if(nicdev->vlan_tci != vlan->tci & 0xff0f)
				continue;

			memmove((uint8_t*)ether + 4 , ether, ETHER_LEN - 2);
			ether = (uint8_t*)ether + 4;
			len -= 4;
			break;
		}
	}

	if(nicdev)
		nicdev_rx(nicdev, ether, len - VNET_HDR_LEN);

	return 0;
}

/* Function for packet send */
static int virtnet_send(VirtNetPriv* priv, Packet* packet) {
	// Check whether free descriptor exists to prevent buffer overflow 
	VirtQueue* vq = priv->svq;
	if(vq->num_free == 0) {
		nic_free(packet);

		return -1;
	}

	// Add new buffer and try to send 
	int len = packet->end - packet->start;
	add_buf(vq, packet, len);

	return 0;
}

static bool poll(NICDevice* nicdev) {
	uint32_t len;
	int received = 0;
	void* buf;

 	VirtNetPriv* priv = nicdev->priv;
	VirtQueue* vq = priv->rvq;
	while((BUDGET_SIZE > received) && (buf = get_buf(vq, &len))) {
		virtnet_receive(priv, buf, len);
		received++;
	}
 
	if(vq->num_free > vq->size / 2) {
		kick(vq);
		vq->num_free = 0;
	}
 
	return true;
}

static bool process(Packet* packet, void* context) {
	NICDevice* nicdev = context;
	if(nicdev->vlan_proto == ETHER_TYPE_8021Q) { //Vlan Tagging
		Ether* ether = (Ether*)(packet->buffer + packet->start);
		if(packet->start < 4) {
			return false;
		}
		memmove((uint8_t*)ether - 4, ether, ETHER_LEN - 2);
		packet->start -= 4;
		ether = (void*)(packet->buffer + packet->start);
		ether->type = endian16(ETHER_TYPE_8021Q);
		VLAN* vlan = (VLAN*)ether->payload;
		vlan->tci = nicdev->vlan_tci;
	}
	return virtnet_send(nicdev->priv, packet) == 0 ? true : false;
}

static bool virtio_xmit(NICDevice* nicdev, Packet* packet) {
 	VirtNetPriv* priv = nicdev->priv;
	VirtQueue* vq;
 
 	// Free used buffer
 	void* buf;
 	while((buf = get_buf(priv->svq, NULL))) {
 		nic_free(buf);
 	}
 
 	// TX
	if(process(packet, nicdev)) {
		vq = priv->svq;
		kick(vq);
	}

	return true;
}

static bool virtio_tx(NICDevice* nicdev) {
 	VirtNetPriv* priv = nicdev->priv;
	VirtQueue* vq;
 
 	// Free used buffer
 	void* buf;
 	while((buf = get_buf(priv->svq, NULL))) {
 		nic_free(buf);
 	}
 
 	// TX
 	int nicdev_tx(NICDevice* dev,
 			bool (*process)(Packet* packet, void* context), void* context);
 	int count = nicdev_tx(nicdev, process, nicdev);
	if(count) {
		vq = priv->svq;
		kick(vq);
	}

	return true;
}


int init(void* device, void* data) {
	int err;

	VirtNetPriv* priv = gmalloc(sizeof(VirtNetPriv));
	memset(priv, 0, sizeof(VirtNetPriv));
	priv->vdev.dev = (PCI_Device*)device;

	// VirtI/O deivce PCI probing
	err = virtio_pci_probe(&priv->vdev);
	if(err) {
		printf("virtio_pci_probe failed: %d\n", err); 
		goto error;
	}

	// Feature synchronizing with host OS
	err = synchronize_features(&priv->vdev);
	if(err) {
		printf("synchronize_features failed: %d\n", err);
		goto error;
	}

	// VirtI/O driver probing 
	err = virtnet_probe(priv);
	if(err) {
		printf("virtnet_probe failed: %d\n", err);
		goto error;
	}

	printf("VirtI/O device initialized\n");

	// Set promiscuos mode
	uint8_t promisc = 1; // 1 means ON for the command
	if(virtnet_send_command(priv, VIRTIO_NET_CTRL_RX, VIRTIO_NET_CTRL_RX_PROMISC, &promisc))
		printf("Promiscuos mode ON\n");
	else
		printf("Promiscous mode OFF\n");

	//Register NICDevice
	NICDevice* nicdev = gmalloc(sizeof(NICDevice));
	if(!nicdev)
		goto error;
	memset(nicdev, 0, sizeof(NICDevice));

	extern int nicdevs_count;
	sprintf(nicdev->name, "eth%d", nicdevs_count);

	nicdev->mac = 0;
	for(int i = 0; i < ETH_ALEN; i++) {
		nicdev->mac |= (uint64_t)priv->vdev.config.mac[i] << (ETH_ALEN - i - 1) * 8;
	}

	extern NICDriver device_driver;
	nicdev->driver = (void*)&device_driver;
	nicdev->priv = priv;
	priv->priv = nicdev;

	extern int nicdev_register(NICDevice* dev);
	//TODO check return value
	nicdev_register(nicdev);

	event_busy_add(poll, nicdev);
	event_busy_add(virtio_tx, nicdev);

	return 0;
error: 
	if(priv)
		gfree(priv);

	return -1;
}

void destroy(int id) {
	// Destory pseudo header 
// 	gfree(pseudo_vnet_hdr);
// 
// 	// Destroy virtqueues
// 	bfree(priv[id]->rvq->vring.desc);
// 	gfree(priv[id]->rvq);
// 
// 	bfree(priv[id]->svq->vring.desc);
// 	gfree(priv[id]->svq);
// 	
// 	if(device_has_feature(&priv[id]->vdev, VIRTIO_NET_F_CTRL_VQ)) {
// 		bfree(priv[id]->cvq->vring.desc);
// 		gfree(priv[id]->rvq);
// 	}
// 
// 	// Destroy private structure
// 	gfree(priv[id]);
}

void get_status(int id, NICStatus* status) {
}

bool set_status(int id, NICStatus* status) {
	return false;
}

void get_info(int id, NICInfo* info) {
	uint64_t mac = 0;

// 	for (int i = 0; i < ETH_ALEN; i++) {
// 		mac |= (uint64_t)priv[id]->vdev.config.mac[i] << (ETH_ALEN - i - 1) * 8;
// 	}

	info->mac = mac;
}

DeviceType device_type = DEVICE_TYPE_NIC;

static PCI_ID pci_ids[] = {
	PCI_DEVICE(0x1af4, 0x1000, "virtio", NULL), 
	{ 0 }
};

bool pci_device_probe(PCI_Device* pci, char** name, void** data) {
	for(int i = 0; pci_ids[i].vendor_id != 0; i++) {
		PCI_ID* id = &pci_ids[i];

		if(pci->vendor_id == id->vendor_id && pci->device_id == id->device_id) {
			*name = id->name;
			*data = id->data;

			return 1;
		}
	}

	return 0;
}

//TODO need vlan filter table
bool virtnet_vlan_rx_add_vid(NICDevice* nicdev, uint16_t vid) {
	return true;
}

bool virtnet_vlan_rx_kill_vid(NICDevice* nicdev, uint16_t vid) {
	return true;
}

NICDriver device_driver = {
	.init = init,
	.destroy = destroy,
	.xmit = virtio_xmit,
	.tx_poll = virtio_tx,
	.poll = poll,
	.get_status = get_status,
	.set_status = set_status,
	.get_info = get_info,

	.add_vid = virtnet_vlan_rx_add_vid,
	.remove_vid = virtnet_vlan_rx_kill_vid
};
