#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <util/list.h>

#include "ramdisk.h"
#include "../file.h"

#define RAMDISK_SECTOR_SIZE	512
typedef struct {
	void*		memory;
	size_t		size;		// TODO: bytes(current)? sector? cluster?
} RamDisk;

// e.g. -addr 0x123456 -size 0x1234
static bool parse(char* argv, uint64_t* address, size_t* size) {
	if(strncmp(argv, "-addr ", 6))
		return false;

	char* next;
	*address = strtoull(argv + 6, &next, 0);
	if(next == argv + 6)
		return false;

	if(strncmp(next, " -size ", 7))
		return false;
	
	*size = strtol(next + 7, 0, 0);	
	return true;
}

static int ramdisk_init(DiskDriver* driver, const char* cmdline, DiskDriver** disks) {
	RamDisk* ramdisk = malloc(sizeof(RamDisk));
	if(!ramdisk) {
		printf("%s : malloc error\n", __FUNCTION__);
		return -1;
	}

	uint64_t address;
	size_t size;
	// Parsing the string
	if(!parse((char*)cmdline, &address, &size)) {
		printf("%s : parsing error\n", __FUNCTION__);
		free(ramdisk);
		return -2;
	}

	ramdisk->memory = (void*)address;
	ramdisk->size = size;

	disks[0] = malloc(sizeof(DiskDriver));
	if(!disks[0]) {
		free(ramdisk);
		return -3;
	}

	memcpy(disks[0], driver, sizeof(DiskDriver));
	disks[0]->number = 0;
	disks[0]->priv = ramdisk;
	driver->priv = ramdisk;

	return 1;
}

static int ramdisk_read(DiskDriver* driver, uint32_t lba, int sector_count, uint8_t* buf) {
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
	RamDisk* ramdisk = driver->priv;
	if(!ramdisk)
		return -1;

	uint32_t offset = lba * RAMDISK_SECTOR_SIZE;
	size_t read_size = sector_count * RAMDISK_SECTOR_SIZE;
	size_t copy_size = MIN(read_size, ramdisk->size - offset);

	memcpy(buf, (uint8_t*)ramdisk->memory + offset, copy_size);

	return copy_size;
}

static int ramdisk_write(DiskDriver* driver, uint32_t lba, int sector_count, uint8_t* buf) {
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
	RamDisk* ramdisk = driver->priv;
	if(!ramdisk)
		return -1;

	uint32_t offset = lba * RAMDISK_SECTOR_SIZE;
	size_t write_size = sector_count * RAMDISK_SECTOR_SIZE;
	size_t copy_size = MIN(write_size, ramdisk->size - offset);

	memcpy((uint8_t*)ramdisk->memory + offset, buf, copy_size);

	return copy_size;
}

static int ramdisk_read_async(DiskDriver* driver, List* blocks, int sector_count, void(*callback)(List* blocks, int count, void* context), void* context) {
	RamDisk* ramdisk = driver->priv;
	if(!ramdisk)
		return -1;

	int count = 0;
	ListIterator iter;
	list_iterator_init(&iter, blocks);
	while(list_iterator_has_next(&iter)) {
		BufferBlock* block = list_iterator_next(&iter);
		uint32_t offset = block->sector * RAMDISK_SECTOR_SIZE;
		size_t read_size = sector_count * RAMDISK_SECTOR_SIZE;
		size_t copy_size = MIN(read_size, ramdisk->size - offset);

		memcpy(block->buffer, (uint8_t*)ramdisk->memory + offset, copy_size);
		count++;
	}

	callback(blocks, count, context);

	return 0;
}

static int ramdisk_write_async(DiskDriver* driver, List* blocks, int sector_count, void(*callback)(List* blocks, int count, void* context), void* context) {
	RamDisk* ramdisk = driver->priv;
	if(!ramdisk)
		return -1;

	int count = 0;
	ListIterator iter;
	list_iterator_init(&iter, blocks);
	while(list_iterator_has_next(&iter)) {
		BufferBlock* block = list_iterator_next(&iter);
		uint32_t offset = block->sector * RAMDISK_SECTOR_SIZE;
		size_t write_size = sector_count * RAMDISK_SECTOR_SIZE;
		size_t copy_size = MIN(write_size, ramdisk->size - offset);

		memcpy((uint8_t*)ramdisk->memory + offset, block->buffer, copy_size);
		count++;
	}

	callback(blocks, count, context);

	return 0;
}

DiskDriver ramdisk_driver = {
	.type = DISK_TYPE_RAMDISK,

	.init = ramdisk_init,
	.read = ramdisk_read,
	.write = ramdisk_write, 
	.read_async = ramdisk_read_async,
	.write_async = ramdisk_write_async,
};
