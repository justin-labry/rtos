#ifndef __DISK_H__
#define __DISK_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <util/list.h>

#define DISK_MAX_DRIVERS	16

#define DISK_TYPE_NULL 		0x00
#define DISK_TYPE_PATA		0x01
#define DISK_TYPE_SATA		0x02
#define DISK_TYPE_USB		0x03
#define DISK_TYPE_VIRTIO_BLK	0x04
#define DISK_TYPE_RAMDISK	0x05

#define DISK_AVAIL_DEVICES	8 	/* Available disks for each driver */
#define DISK_AVAIL_PARTITIONS	4 	/* Available partitions for each disk */

typedef struct {
	uint8_t			status;
	uint8_t			first_chs_addr[3];
	uint8_t			partition_type;
	uint8_t			last_chs_addr[3];
	uint32_t		first_lba;
	uint32_t		num_of_sec;
} __attribute__((packed)) PartEntry;

typedef struct {
	uint8_t			code[446];
	PartEntry		part_entry[DISK_AVAIL_PARTITIONS];
	uint8_t			boot_signature[2];
} __attribute__((packed)) BootSector;

typedef struct _DiskDriver DiskDriver;

typedef struct _DiskDriver {
	// Driver information
	uint16_t type;
	uint16_t number;
	BootSector* boot_sector;

	/* Low level file operation - disk specific */
	int	(*init)(DiskDriver* driver, const char* cmdline, DiskDriver** disks);
	int	(*read)(DiskDriver* driver, uint32_t lba, int sector_count, uint8_t* buf);
	int	(*read_async)(DiskDriver* driver, List* blocks, int sector_count, void(*callback)(List* blocks, int count, void* context), void* context);
	int	(*write_async)(DiskDriver* driver, List* blocks, int sector_count, void(*callback)(List* blocks, int count, void* context), void* context);
	int	(*write)(DiskDriver* driver, uint32_t lba, int sector_count, uint8_t* buf);

	// Probe information
	void* 	priv;
} DiskDriver;

int disk_init();
bool disk_register(DiskDriver* driver, const char* cmdline);
size_t disk_count();
int disk_ids(uint32_t* ids, int size);
DiskDriver* disk_get(uint32_t id);

#endif /* __DISK_H__ */
