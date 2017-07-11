#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <util/types.h>
#include <control/rpc.h>

#include "rpc.h"

static RPC* rpc;

static void help() {
#define ANSI_UNDERLINED_PRE  "\033[4m"
#define ANSI_UNDERLINED_POST "\033[0m"

#define UNDERLINE(OPTION) ANSI_UNDERLINED_PRE #OPTION ANSI_UNDERLINED_POST

	printf("Usage: status " UNDERLINE(VM ID) "\n");
}

static bool callback_status_get(VMStatus status, void* context) {
	if(status == VM_STATUS_STOP)
		printf("stop\n");
	else if(status == VM_STATUS_PAUSE)
		printf("pause\n");
	else if(status == VM_STATUS_START)
		printf("start\n");
	else if(status == VM_STATUS_RESUME)
		printf("resume\n");
	else if(status == VM_STATUS_INVALID)
		printf("invalid\n");

	rpc_disconnect(rpc);
	return false;
}

static int status_get(int argc, char** argv) {
	if(argc < 2) {
		help();
		return -1;
	}

	if(!is_uint32(argv[1])) {
		help();
		return -2;
	}

	uint32_t vmid = parse_uint32(argv[1]);
	rpc_status_get(rpc, vmid, callback_status_get, NULL);

	return 0;
}

void* __attribute__((weak)) malloc(size_t size);
int main(int argc, char *argv[]) {
	malloc(1024);
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
	if((rc = status_get(argc, argv))) {
		printf("Failed to get VM status. Error code : %d\n", rc);
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

