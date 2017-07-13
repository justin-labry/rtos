#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <control/rpc.h>

#include "../include/rpc_console.h"

static RPC* rpc;

static void help() {
	printf("Usage: list \n");
}

static bool callback_vm_list(uint32_t* ids, uint16_t count, void* context) {
	for(int i = 0 ; i < count; i++)
		printf("%d ", ids[i]);

	printf("\n");
	rpc_disconnect(rpc);
	return false;
}

static int vm_list(int argc, char** argv) {

	rpc_vm_list(rpc, callback_vm_list, NULL);
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
	
	if(vm_list(argc, argv)) {
		printf("Failed to destroy VM\n");
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
		
	return 0;
}

