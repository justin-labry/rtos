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

#define VMSPEC_MAX_NIC_COUNT	64
#define VMSPEC_MAX_ARGC		256
#define VMSPEC_MAX_ARGV		4096

#define NICSPEC_DEVICE_MAC	((uint64_t)1 << 48)

typedef struct {
	char		name[MAX_NIC_NAME_LEN];
	uint64_t	mac;
	char		parent[MAX_NIC_NAME_LEN];
	uint16_t	budget;
	uint32_t	rx_buffer_size;
	uint32_t	tx_buffer_size;
	uint8_t		padding_head;
	uint8_t		padding_tail;
	uint64_t	rx_bandwidth;
	uint64_t	tx_bandwidth;
	uint32_t	pool_size;
} NICSpec;

typedef struct {
	uint32_t	id;

	uint32_t	core_size;
	uint32_t	memory_size;
	uint32_t	storage_size;

	uint16_t	nic_count;
	NICSpec*	nics;

	uint16_t	argc;
	char*		argv[VMSPEC_MAX_ARGC];
} VMSpec;

#endif /* __CONTROL_VMSPEC_H__ */
