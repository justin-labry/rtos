#include <stdio.h>
#include <string.h>
#include <util/cmd.h>

#include "driver/disk.h"
#include "driver/fs.h"

static int cmd_mount(int argc, char** argv, void(*callback)(char* result, int exit_status));
static Command commands[] = {
	{
		.name = "mount",
		.desc = "Mount file system",
		.args = "-t fs_type:str{bfs|ext2|fat} posix_device_name:str path:str -> bool",
		.func = cmd_mount
	},
};

int mount_init() {
	cmd_register(commands, sizeof(commands) / sizeof(commands[0]));
	return 0;
}

static int cmd_mount(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	if(argc < 5) {
		printf("argument is not enough\n");
		return -1;
	}

	uint8_t type;
	uint8_t disk;
	uint8_t number;
	uint8_t partition;

	if(strcmp(argv[1], "-t") != 0) {
		printf("invalid option name %s were given\n", argv[1]);
		return -2;
	}

	if(strcmp(argv[2], "bfs") == 0 || strcmp(argv[2], "BFS") == 0) {
		type = FS_TYPE_BFS;
	} else if(strcmp(argv[2], "ext2") == 0 || strcmp(argv[2], "EXT2") == 0) {
		type = FS_TYPE_EXT2;
	} else if(strcmp(argv[2], "fat") == 0 || strcmp(argv[2], "FAT") == 0) {
		type = FS_TYPE_FAT;
	} else {
		printf("filesystem %s is not supported\n", argv[2]);
		return -3;
	}

	if(strlen(argv[3]) != 3) {
		printf("malformed device name %s were given\n", argv[3]);
		return -4;
	}

	if(argv[3][0] == 'v') {
		disk = DISK_TYPE_VIRTIO_BLK;
	} else if(argv[3][0] == 's') {
		disk = DISK_TYPE_USB;
	} else if(argv[3][0] == 'h') {
		disk = DISK_TYPE_PATA;
	} else {
		printf("device type %c is not supported. supported type is [v, s, h]\n", argv[3][0]);
		return -5;
	}

	number = argv[3][1] - 'a';
	if(number > DISK_AVAIL_DEVICES) {
		printf("disk number cannot exceed %d\n", DISK_AVAIL_DEVICES);
		return -6;
	}

	partition = argv[3][2] - '0';
	if(partition > DISK_AVAIL_PARTITIONS) {
		printf("partition number cannot exceed %d\n", DISK_AVAIL_PARTITIONS);
		return -7;
	}

	fs_mount(disk << 16 | number, partition, type, argv[4]);

	return -2;
}

