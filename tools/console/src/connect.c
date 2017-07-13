#include <stdio.h>
#include <stdlib.h>
#include <util/types.h>
#include "../include/rpc_console.h"

#define DEFAULT_HOST		"192.168.100.254"
#define DEFAULT_PORT		1111
#define DEFAULT_TIMEOUT		3

static RPC* rpc;

static void help() {
	printf("Usage: connect [IP] [PORT]\n");
}

static int connect(int argc, char** argv) {
	char* host = DEFAULT_HOST;
	int port = DEFAULT_PORT;
	int timeout = DEFAULT_TIMEOUT;

	if(argc >= 2)
		host = argv[1];

	if(argc >= 3 && is_uint16(argv[2]))
		port = parse_uint16(argv[2]);

	rpc = rpc_connect(host, port, timeout, false);
	if(rpc == NULL)
		return -1;

	return 0;
}

int main(int argc, char *argv[]) {
	int rc;
	rpc_init();
	if((rc = connect(argc, argv))) {
		printf("Failed to connect RPC server. Error code : %d\n", rc);
		return ERROR_RPC_DISCONNECTED;
	}

	while(1) {
		if(rpc_connected(rpc)) {
			rpc_loop(rpc);
		} else {
			free(rpc);
			break;
		}
	}

	return 0;
}
