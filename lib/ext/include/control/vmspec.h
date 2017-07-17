#ifndef __CONTROL_VMSPEC_H__
#define __CONTROL_VMSPEC_H__

#include <stdint.h>
#include <nic.h>

 /**
   +-O create()
   |
   |                   start()
   |  +------------------------------------------+
   |  |                                          |
+--+--+---+  stop()  +---------+   pause()  +----v----+
|         <----------+         <------------+         |
| STOP    |          | PAUSE   |            | START   |
|         |          |         +------------>         |
+----^----+          +---------+  resume()  +----+----+
   | |                                           |
   | +-------------------------------------------+
   |                   stop()
   |
   +-X destroy()

                      on error
                     +---------+
                     |         |
                     | INVALID |
                     |         |
                     +---------+
 */
typedef enum {
	VM_STATUS_STOP		= 0,
	VM_STATUS_PAUSE		= 1,
	VM_STATUS_START		= 2,
	VM_STATUS_RESUME	= 3,
	VM_STATUS_INVALID	= -1,
} VMStatus;

#define NICSPEC_F_INHERITMAC		NIC_F_INHERITMAC
#define NICSPEC_F_NOARP				NIC_F_NOARP
#define NICSPEC_F_PROMISC			NIC_F_PROMISC
#define NICSPEC_F_BROADCAST			NIC_F_BROADCAST
#define NICSPEC_F_MULTICAST			NIC_F_MULTICAST
#define NICSPEC_F_MULTIQUEUE		NIC_F_MULTIQUEUE

#define NICSPEC_DEFAULT_MAC				0
#define NICSPEC_DEFAULT_BUDGET_SIZE		32
#define NICSPEC_DEFAULT_FLAGS			NICSPEC_F_MULTICAST | NICSPEC_F_BROADCAST
#define NICSPEC_DEFAULT_BUFFER_SIZE		1024
#define NICSPEC_DEFAULT_BANDWIDTH		1000000000	// 1Gbps
#define NICSPEC_DEFAULT_PADDING_SIZE	32
#define NICSPEC_DEFAULT_POOL_SIZE		0x200000	// 2Mb

typedef struct {
	char		name[MAX_NIC_NAME_LEN];
	uint64_t	mac;
	char		parent[MAX_NIC_NAME_LEN];
	uint16_t	budget;
	uint64_t	flags;
	uint32_t	rx_buffer_size;
	uint32_t	tx_buffer_size;
	uint8_t		padding_head;
	uint8_t		padding_tail;
	uint64_t	rx_bandwidth;
	uint64_t	tx_bandwidth;
	uint32_t	pool_size;

	uint64_t	rx_bytes;
	uint64_t	rx_packets;
	uint64_t	rx_drop_bytes;
	uint64_t	rx_drop_packets;
	uint64_t	tx_bytes;
	uint64_t	tx_packets;
	uint64_t	tx_drop_bytes;
	uint64_t	tx_drop_packets;
} NICSpec;

#define VMSPEC_MAX_NIC_COUNT	16
#define VMSPEC_MAX_ARGC			256
#define VMSPEC_MAX_ARGV			4096

typedef struct {
	uint32_t	id;

	uint32_t	core_size;
	uint32_t	memory_size;
	uint32_t	storage_size;

	uint16_t	nic_count;
	NICSpec		nics[VMSPEC_MAX_NIC_COUNT];

	uint16_t	argc;
	char*		argv[VMSPEC_MAX_ARGC];
} VMSpec;

#endif /* __CONTROL_VMSPEC_H__ */