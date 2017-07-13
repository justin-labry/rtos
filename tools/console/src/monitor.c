#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <util/types.h>
#include <control/rpc.h>

#include "../include/rpc_console.h"

static RPC* rpc;

static void help() {
#define ANSI_UNDERLINED_PRE  "\033[4m"
#define ANSI_UNDERLINED_POST "\033[0m"

#define UNDERLINE(OPTION) ANSI_UNDERLINED_PRE #OPTION ANSI_UNDERLINED_POST

	printf("Usage: monitor [VM ID] [THREAD ID]\n");
}

static void stdio_handler(RPC* rpc, uint32_t id, uint8_t thread_id, int fd, char* str, uint16_t size, void* context, void(*callback)(RPC* rpc, uint16_t size)) {
	static uint32_t current_id = -1;
	static uint8_t current_thread_id = -1;

	if(!(current_id == id && current_thread_id == thread_id)) {
		current_id = id;
		current_thread_id = thread_id;
		printf( "***** vmid=%d thread=%d standard %s *****\n", current_id, current_thread_id, fd == 1 ? "Output" : "Error");
		fflush(stdout);
	}

	size = write(1, str, size);
	fflush(stdout);

	callback(rpc, size);
}

static int vm_monitor(int argc, char** argv) {
	// TODO: Selectively monitor by VM, Thread
/*
 *        uint32_t vmid;
 *        uint32_t thread_id;
 *
 *        if(argc == 2) {
 *                if(!is_uint32(argv[1])) {
 *                        help();
 *                        return -1;
 *                }
 *                vmid = parse_uint32(argv[1]);
 *        } else if (argc == 3) {
 *                if(!is_uint32(argv[1])) {
 *                        help();
 *                        return -1;
 *                }
 *
 *                if(!is_uint32(argv[2])) {
 *                        help();
 *                        return -2;
 *                }
 *                vmid = parse_uint32(argv[1]);
 *                thread_id = parse_uint32(argv[2]);
 *        }
 */

	rpc_stdio_handler(rpc, stdio_handler, NULL);

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
	if((rc = vm_monitor(argc, argv))) {
		printf("Failed to monitor VM. Error code : %d\n", rc);
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

