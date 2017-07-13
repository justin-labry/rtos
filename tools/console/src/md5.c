#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <getopt.h>
#include <sys/time.h>
#include <util/types.h>
#include <control/rpc.h>
#include <control/vmspec.h>

#include "../include/rpc_console.h"

static int request_md5(int argc, char* argv[]);
static bool response_md5(bool result, uint32_t* md5, void* context);

static RPC* rpc;

static void help() {
	printf("Usage: md5 vmid [size]\n");
}

static int request_md5(int argc, char* argv[]) {
	int vmid = 0;
	size_t storage_size = 0;

	if(argc == 2)
		vmid = atoi(argv[1]);
	else if(argc == 3) {
		vmid = atoi(argv[1]);
		storage_size = atoi(argv[2]);
	} else {
		help();
		exit(1);
	}

	// Request MD5 value & Register Response callback handler
	rpc_storage_md5(rpc, vmid, storage_size, response_md5, NULL);
	return 0;
}

static bool response_md5(bool result, uint32_t* md5, void* context) {
	// vm_md5_callback() implementation
	if(!result || !md5)
		printf("Failed to get VM storage md5 checksum\n");
	else {
		// Print MD5 checksum
		uint8_t* md5_raw = (uint8_t*)md5;
		for(int i = 0; i < 16; ++i)
			printf("%02x", md5_raw[i]);
		puts("");
	}

	rpc_disconnect(rpc);
	return false;
}

int main(int argc, char *argv[]) {
	rpc_init();
	RPCSession* session = rpc_session();
	if(!session) {
		printf("RPC server not connected\n");
		return ERROR_RPC_DISCONNECTED;
	}

	rpc = rpc_connect(session->host, session->port, 3, true);
	if(!rpc) {
		printf("Failed to connect RPC server\n");
		return ERROR_RPC_DISCONNECTED;
	}

	int rc;
	if((rc = request_md5(argc, argv))) {
		printf("Failed to get VM storage md5 checksum. Error code : %d\n", rc);
		rpc_disconnect(rpc);
		return ERROR_CMD_EXECUTE;
	}

	while(1) {
		// Wait for response
		if(rpc_connected(rpc)) {
			rpc_loop(rpc);
		} else {
			free(rpc);
			break;
		}
	}

	return 0;
}

