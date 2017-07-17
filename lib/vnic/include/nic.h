#ifndef __NIC_H__
#define __NIC_H__

#include <packet.h>
#ifndef MODULE
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#else
#include <linux/types.h>
#endif

#define MAX_NIC_NAME_LEN	16

// ROUNDUP(0, 8) -> 0 : ROUNDUP(3, 8) -> 8 : ROUNDUP(8,8) -> 8
#define ROUNDUP(x, y)	((((x) + (y) - 1) / (y)) * (y))

#define NIC_F_INHERITMAC		((uint64_t)1 << 1)
#define NIC_F_NOARP				((uint64_t)1 << 2)
#define NIC_F_PROMISC			((uint64_t)1 << 3)
#define NIC_F_BROADCAST			((uint64_t)1 << 4)
#define NIC_F_MULTICAST			((uint64_t)1 << 5)
#define NIC_F_MULTIQUEUE		((uint64_t)1 << 6)

#define NIC_MAX_COUNT		64
#define NIC_MAX_LINKS		8
#define NIC_CHUNK_SIZE		64
#define NIC_MAX_SIZE		(16 * 1024 * 1024)	// 16MB
#define NIC_HEADER_SIZE		(64 * 1024)		// 64KB

#define NIC_MAGIC_HEADER	0x0A38E56586468C01LL	// PacketNgin vNIC 01(version)

/**
 * @file
 * Network Interface Controller (NIC) host API
 */

// Host API

typedef struct _NICQueue {
	uint32_t	base;			///< Base offset
	uint32_t	head;			///< Queue head
	uint32_t	tail;			///< Queue tail
	uint32_t	size;			///< Maximum number of packets this queue can have
	volatile uint8_t rlock;		///< Read lock
	volatile uint8_t wlock;		///< Write lock
} NICQueue;

/**
 * Bitmap Pool
 */
typedef struct _NICPool {
	uint32_t	bitmap;		///< Bitmap
	uint32_t	count;		///< Chunk count
	uint32_t	pool;		///< Bitmap pool
	uint32_t	index;
	uint32_t	used;		///< Number of active chunks
	volatile uint8_t lock;	///< Write lock
} NICPool;

/**
 * NIC Memory Map
 *
 * NIC_MAGIC_HEADER (8 bytes)
 * Metadata (bandwidth, pdding, queue, pool)
 * Config
 * Fast path rx queue
 * Fast path tx queue
 * Slow path rx queue
 * Slow path tx queue
 * Packet pool bitmap
 * Packet payload pool
 */
typedef struct _NIC {
	// 2MBs aligned
	uint64_t	magic;			///< A magic value to verify that the NIC object was found correctly
	uint32_t	id;			///< NIC unique ID (unique ID in RTOS; copied from VNIC)
	uint64_t	mac;			///< MAC Address
	uint16_t	vlan_proto; 		///< VLAN Protocol
	uint16_t	vlan_tci;   		///< VLAN TCI
	uint64_t	flags;			///< Flags

	uint64_t	rx_bandwidth;		///< Rx bandwith limit (bps)
	uint64_t	tx_bandwidth;		///< Tx bandwith limit (bps)

	uint64_t	input_bytes;		///< Total input bytes (read only)
	uint64_t	input_packets;		///< Total input packets (read only)
	uint64_t	input_drop_bytes;	///< Total dropped input bytes (read only)
	uint64_t	input_drop_packets;	///< Total dropped input packets (read only)
	uint64_t	output_bytes;		///< Total output bytes (read only)
	uint64_t	output_packets;		///< Total output packets (read only)
	uint64_t	output_drop_bytes;	///< Total dropped output bytes (read only)
	uint64_t	output_drop_packets;	///< Total dropped output packets (read only)

	uint16_t	padding_head;
	uint16_t	padding_tail;

	NICQueue	rx;
	NICQueue	tx;

	NICQueue	srx;
	NICQueue	stx;

	NICPool		pool;

	uint32_t	config;
	uint8_t		config_head[0];
	uint8_t		config_tail[0] __attribute__((__aligned__(NIC_HEADER_SIZE)));

	// rx queue (8 bytes aligned)
	// tx queue (8 bytes aligned)
	// slow rx queue (8 bytes aligned)
	// slow tx queue (8 bytes aligned)
	// pool bitmap (8 bytes aligned)
	// pool (NIC_CHUNK_SIZE(64) bytges aligned)
} __attribute__((packed)) NIC;

NIC* nic_find_by_packet(Packet* packet);
int nic_count();
NIC* nic_get(int index);
NIC* nic_get_by_id(uint32_t id);

Packet* nic_alloc(NIC* nic, uint16_t size);
bool nic_free(Packet* packet);

bool queue_push(NIC* nic, NICQueue* queue, Packet* packet);
void* queue_pop(NIC* nic, NICQueue* queue);
uint32_t queue_size(NICQueue* queue);
bool queue_available(NICQueue* queue);
bool queue_empty(NICQueue* queue);

bool nic_has_rx(NIC* nic);
Packet* nic_rx(NIC* nic);
uint32_t nic_rx_size(NIC* nic);

bool nic_has_srx(NIC* nic);
Packet* nic_srx(NIC* nic);
uint32_t nic_srx_size(NIC* nic);

bool nic_tx(NIC* nic, Packet* packet);
bool nic_try_tx(NIC* nic, Packet* packet);
bool nic_tx_dup(NIC* nic, Packet* packet);
bool nic_has_tx(NIC* nic);
uint32_t nic_tx_size(NIC* nic);

bool nic_stx(NIC* nic, Packet* packet);
bool nic_try_stx(NIC* nic, Packet* packet);
bool nic_stx_dup(NIC* nic, Packet* packet);
bool nic_has_stx(NIC* nic);
uint32_t nic_stx_size(NIC* nic);

size_t nic_pool_used(NIC* nic);
size_t nic_pool_free(NIC* nic);
size_t nic_pool_total(NIC* nic);

int32_t nic_config_alloc(NIC* nic, char* name, uint16_t size);
void nic_config_free(NIC* nic, uint16_t key);
int32_t nic_config_key(NIC* nic, char* name);
void* nic_config_get(NIC* nic, uint16_t key);
uint16_t nic_config_size(NIC* nic, uint16_t key);
uint32_t nic_config_available(NIC* nic);
uint32_t nic_config_total(NIC* nic);

/**
 * Initialize NIC memory map
 *
 * NIC metadata (magic, mac, ...)
 * Fast path rx queue
 * Fast path tx queue
 * Slow path rx queue
 * Slow path tx queue
 * Malloc bitmap
 * Malloc pool
 *
 * @param base 2MBs aligned
 * @param size multiples of 2MBs
 * @return 0 succeed
 * @return 1 the base address is not aligned in 2MBs
 * @return 2 the size is not rounded up 2MBs
 */

#endif /* __NIC_H__ */
