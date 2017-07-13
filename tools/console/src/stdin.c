#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <util/types.h>
#include <control/rpc.h>

#include "../include/rpc_console.h"

static RPC* rpc;

static void help() {
	printf("Usage: stdin [VM ID] [THREAD ID] [MESSAGE]\n");
}

static bool callback_stdin(uint16_t written, void* context) {
	if(written == 0)
		printf("false\n");
	else
		printf("true\n");

	rpc_disconnect(rpc);
	return false;
}

static int vm_stdin(int argc, char** argv) {
	if(argc < 4) {
		help();
		return -1;
	}

	if(!is_uint32(argv[1])) {
		help();
		return -2;
	}

	if(!is_uint8(argv[2])) {
		help();
		return -3;
	}

	uint32_t vmid = parse_uint32(argv[1]);
	uint8_t thread_id = parse_uint8(argv[2]);
	uint16_t length = strlen(argv[3]);
	argv[3][length++] = '\0';

	rpc_stdio(rpc, vmid, thread_id, 0, argv[3], length, callback_stdin, NULL);

	return 0;
}

int main(int argc, char *argv[]) {
	rpc_init();
	RPCSession* session = rpc_session();
	if(!session) {
		printf("RPC server not connected\n");
		return ERROR_RPC_DISCONNECTED;
	}

	rpc = rpc_connect(session->host, session->port, 3, true);
	if(rpc == NULL) {
		printf("Failed to connect RPC server\n");
		return ERROR_RPC_DISCONNECTED;
	}
	
	int rc;
	if((rc = vm_stdin(argc, argv))) {
		printf("Failed to stdin VM. Error code : %d\n", rc);
		rpc_disconnect(rpc);
		return ERROR_CMD_EXECUTE;
	}

	while(1) {
		if(rpc_connected(rpc)) {
			rpc_loop(rpc);
		} else {
			free(rpc);
			break;
		}
	}
}

