#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>	// For "basename" function
#include <util/types.h>
#include <control/rpc.h>

#include "../include/rpc_console.h"

static RPC* rpc;

static void help(char* command) {
#define ANSI_UNDERLINED_PRE  "\033[4m"
#define ANSI_UNDERLINED_POST "\033[0m"

#define UNDERLINE(OPTION) ANSI_UNDERLINED_PRE #OPTION ANSI_UNDERLINED_POST

	printf("Usage: %s " UNDERLINE(VM ID) "\n", command);
}

static bool callback_status_set(bool result, void* context) {
	void(*callback)(char* result, int exit_status) = context;

	if(result) {
		printf("true\n");
	} else {
		printf("false\n");
	}

	rpc_disconnect(rpc);
	return false;
}

static int status_set(int argc, char** argv) {
	if(argc < 2) {
		help(argv[0]);
		return -1;
	}

	if(!is_uint32(argv[1])) {
		help(argv[0]);
		return -2;
	}

	uint32_t vmid = parse_uint32(argv[1]);
	VMStatus vmstatus;

	if(strcmp(argv[0], "start") == 0)
		vmstatus = VM_STATUS_START;
	else if(strcmp(argv[0], "pause") == 0)
		vmstatus = VM_STATUS_PAUSE;
	else if(strcmp(argv[0], "resume") == 0)
		vmstatus = VM_STATUS_RESUME;
	else if(strcmp(argv[0], "stop") == 0)
		vmstatus = VM_STATUS_STOP;
	else 
		return -3;

	rpc_status_set(rpc, vmid, vmstatus, callback_status_set, NULL);

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

	// We should have basename for argv[0] for command parsing
	argv[0] = basename(argv[0]);
	int rc;
	if((rc = status_set(argc, argv))) {
		printf("Failed to %s VM. Error code : %d\n", argv[0], rc);
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

