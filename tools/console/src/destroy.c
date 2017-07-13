#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <util/types.h>
#include <control/rpc.h>

#include "../include/rpc_console.h"

static RPC* rpc;

static void help() {
#define ANSI_UNDERLINED_PRE  "\033[4m"
#define ANSI_UNDERLINED_POST "\033[0m"

#define UNDERLINE(OPTION) ANSI_UNDERLINED_PRE #OPTION ANSI_UNDERLINED_POST

	printf("Usage: destroy " UNDERLINE(VM ID) "\n");
}

bool callback_vm_destroy(bool result, void* context) {
	if(result)
		printf("true\n");
	else
		printf("false\n");

	rpc_disconnect(rpc);
	return false;
}

static int vm_destroy(int argc, char** argv) {
	if(argc < 2) {
		help();
		return -1;
	}

	if(!is_uint32(argv[1])) {
		help();
		return -2;
	}

	uint32_t vmid = parse_uint32(argv[1]);
	rpc_vm_destroy(rpc, vmid, callback_vm_destroy, NULL);

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
	if((rc = vm_destroy(argc, argv))) {
		printf("Failed to destroy VM. Error code : %d\n", rc);
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

