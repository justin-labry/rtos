#include <stdio.h>
#include <util/cmd.h>

#include "driver/disk.h"
#include "driver/fs.h"

static int cmd_mount(int argc, char** argv, void(*callback)(char* result, int exit_status));
static Command commands[] = {
	{
		.name = "mount",
		.desc = "Mount file system",
		.args = "-t fs_type:str{bfs|ext2|fat} device:str path:str -> bool",
		.func = cmd_mount
	},
};

int mount_init() {
	cmd_register(commands, sizeof(commands) / sizeof(commands[0]));
	return 0;
}

static int cmd_mount(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	if(argc < 5) {
		printf("Argument is not enough\n");
		return -1;
	}

	uint8_t type;
	uint8_t disk;
	uint8_t number;
	uint8_t partition;

	if(strcmp(argv[1], "-t") != 0) {
		printf("Argument is wrong\n");
		return -2;
	}

	if(strcmp(argv[2], "bfs") == 0 || strcmp(argv[2], "BFS") == 0) {
		type = FS_TYPE_BFS;
	} else if(strcmp(argv[2], "ext2") == 0 || strcmp(argv[2], "EXT2") == 0) {
		type = FS_TYPE_EXT2;
	} else if(strcmp(argv[2], "fat") == 0 || strcmp(argv[2], "FAT") == 0) {
		type = FS_TYPE_FAT;
	} else {
		printf("%s type is not supported\n", argv[2]);
		return -3;
	}

	if(argv[3][0] == 'v') {
		disk = DISK_TYPE_VIRTIO_BLK;
	} else if(argv[3][0] == 's') {
		disk = DISK_TYPE_USB;
	} else if(argv[3][0] == 'h') {
		disk = DISK_TYPE_PATA;
	} else {
		printf("%c type is not supported\n", argv[3][0]);
		return -3;
	}

	number = argv[3][2] - 'a';
	if(number > 8) {
		printf("Disk number cannot exceed %d\n", DISK_AVAIL_DEVICES);
		return -4;
	}

	partition = argv[3][3] - '1';
	if(partition > 3) {
		printf("Partition number cannot exceed 4\n");
		return -5;
	}

	fs_mount(disk << 16 | number, partition, type, argv[4]);

	return -2;
}

