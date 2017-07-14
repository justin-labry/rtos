#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <sys/time.h>

#include <util/types.h>
#include <control/rpc.h>
#include <control/vmspec.h>

#include "../include/rpc_console.h"

static RPC* rpc;

static void help() {
	printf("Usage: create"
			"[-c core_count] [-m memory_size] [-s storage_size] [-a arguments]"
			"[-n,[mac=mac_hex_value],[dev=interface_name],[ibuf=ibuf_size],[obuf=obuf_size],[iband=iband_size],[oband=oband_size],[hpad=hpad_size],[tpad=tpad_size],[pool=pool_hex_size]\n");
}

static bool callback_vm_create(uint32_t id, void* context) {
	if(id == 0)
		printf("Fail\n");
	else
		printf("%d\n", id);

	rpc_disconnect(rpc);
	return false;
}

static int vm_create(int argc, char* argv[]) {
	// Default value
	VMSpec vm = {};
	vm.core_size = 1;
	vm.memory_size = 0x1000000;	// 16MB
	vm.storage_size = 0x1000000;	// 16MB

	NICSpec nics[VMSPEC_MAX_NIC_COUNT] = {};
	vm.nics = nics;

	// Main options
	static struct option options[] = {
		{ "core", required_argument, 0, 'c' },
		{ "memory", required_argument, 0, 'm' },
		{ "storage", required_argument, 0, 's' },
		{ "nic", optional_argument, 0, 'n' },
		{ "args", required_argument, 0, 'a' },
		{ 0, 0, 0, 0 }
	};

	int opt;
	int index = 0;
	while((opt = getopt_long(argc, argv, "c:m:s:n::a:", options, &index)) != -1) {
		switch(opt) {
			case 'c' :
				if(!is_uint32(optarg)) goto failure;
				vm.core_size = strtol(optarg, NULL, 0);
				break;
			case 'm' :
				if(!is_uint32(optarg)) goto failure;
				vm.memory_size = strtol(optarg, NULL, 16);
				break;
			case 's' :
				if(!is_uint32(optarg)) goto failure;
				vm.storage_size = strtol(optarg, NULL, 16);
				break;
			case 'n' :;
				// Suboptions for NIC
				enum {
					EMPTY, MAC, DEV, IBUF, OBUF, IBAND, OBAND, HPAD, TPAD, POOL, SLOWPATH,
				};

				const char* token[] = {
					[EMPTY] = "",
					[MAC]   = "mac",
					[DEV]   = "dev",
					[IBUF]	= "ibuf",
					[OBUF]	= "obuf",
					[IBAND]	= "iband",
					[OBAND]	= "oband",
					[HPAD]	= "hpad",
					[TPAD]	= "tpad",
					[POOL]	= "pool",
					NULL,
				};

				// Default NIC configuration
				NICSpec* nic = &vm.nics[vm.nic_count++];
				nic->mac = 0;
				nic->parent[0] = '\0';
				nic->rx_buffer_size = 1024;
				nic->tx_buffer_size = 1024;
				nic->rx_bandwidth = 1000000000; /* 1 GB */
				nic->tx_bandwidth = 1000000000; /* 1 GB */
				nic->padding_head = 32;
				nic->padding_tail = 32;
				nic->pool_size = 0x400000; /* 4 MB */

				char* subopts = optarg;
				while(optarg && *subopts != '\0') {
					char* value = NULL;
					int subopt_index = getsubopt(&subopts, (char* const*)token, &value);
					if(subopt_index == -1) goto failure;

					switch(subopt_index) {
						case EMPTY:
							break;
						case MAC:
							if(!is_uint64(value)) goto failure;
							nic->mac = strtoll(value, NULL, 16);
							break;
						case DEV:
							strncpy(nic->parent, value, MAX_NIC_NAME_LEN);
							break;
						case IBUF:
							if(!is_uint32(value)) goto failure;
							nic->rx_buffer_size = strtoul(value, NULL, 0);
							break;
						case OBUF:
							if(!is_uint32(value)) goto failure;
							nic->tx_buffer_size = strtoul(value, NULL, 0);
							break;
						case IBAND:
							if(!is_uint64(value)) goto failure;
							nic->rx_bandwidth = strtoull(value, NULL, 0);
							break;
						case OBAND:
							if(!is_uint64(value)) goto failure;
							nic->tx_bandwidth = strtoull(value, NULL, 0);
							break;
						case HPAD:
							if(!is_uint8(value)) goto failure;
							nic->padding_head = strtoul(value, NULL, 0);
							break;
						case TPAD:
							if(!is_uint8(value)) goto failure;
							nic->padding_tail = strtoul(value, NULL, 0);
							break;
						case POOL:
							if(!is_uint32(value)) goto failure;
							nic->pool_size = strtoul(value, NULL, 16);
							break;
						default:
							goto failure;
							break;
					}
				}
				break;
			case 'a' :
				vm.argv[vm.argc++] = strdup(optarg);
				if(errno == ENOMEM) goto failure;
				break;

			default:
				goto failure;
		}
	}

	rpc_vm_create(rpc, &vm, callback_vm_create, NULL);

	return 0;

failure:
	printf("Malformed input were given\n");
	help();
	exit(EXIT_FAILURE);
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
	if((rc = vm_create(argc, argv))) {
		printf("Failed to create VM. Error code : %d\n", rc);
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

