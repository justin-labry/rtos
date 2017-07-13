#include <stdio.h>
#include <util/cmd.h>

#include "driver/nicdev.h"

static int cmd_nic(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	int nicdev_count = nicdev_get_count();
	if(!nicdev_count) {
		printf("Empty\n");
		return 0;
	}

	for(int i = 0 ; i < nicdev_count; i++) {
		NICDevice* nicdev = nicdev_get_by_idx(i);
		if(!nicdev) break;

		printf("%s:\n", nicdev->name);
		printf("    HWaddr: %02x:%02x:%02x:%02x:%02x:%02x\n",
				(nicdev->mac >> 40) & 0xff,
				(nicdev->mac >> 32) & 0xff,
				(nicdev->mac >> 24) & 0xff,
				(nicdev->mac >> 16) & 0xff,
				(nicdev->mac >> 8) & 0xff,
				(nicdev->mac >> 0) & 0xff);
	}

	return 0;
}

static Command commands[] = {
	{
		.name = "nic",
		.desc = "Print a list of network interface",
		.func = cmd_nic
	},
};

int nicutil_init() {
	cmd_register(commands, sizeof(commands) / sizeof(commands[0]));
	return 0;
}
