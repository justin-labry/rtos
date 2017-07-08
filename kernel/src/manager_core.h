#ifndef __MANAGER_CORE_H__
#define __MANAGER_CORE_H__

#include <stdint.h>
#include <stdbool.h>

#include <control/rpc.h>

/**
 * @file
 * Platform dependant RPC server side stubs
 *   - Open rpc server
 *   - Close rpc server
 *   - Accept new rpc client
 *   - Provide read, write and close implementation to accepted client
 */

/**
 * Platform-dependant core metadata
 */
typedef struct _ManagerCore {
	/**
	 * Transport layer port
	 */
	uint16_t	port;

	/**
	 * Private data
	 *   - Single Kernel with lwip stack: `struct pcb` object. @see lwip/tcp.h
	 *   - Multi Kernel with linux stack: `Connection` object. @see tools/pn/src/manager_core.c
	 */
	void*		data;

	/**
	 * List of connected clients
	 */
	List*		clients;
} ManagerCore;

/**
 * Initialize manager core
 *
 * @param accept Set of the handler function to be applied when a new client rpc is created
 * @return zero for success, nonzero for failure
 */
int manager_core_init(int (*accept)(RPC* rpc));

/**
 * Open manager core server
 *
 * @param port server port number (tcp port)
 * @return ManagerCore for success, NULL for failure
 */
ManagerCore* manager_core_server_open(uint16_t port);

/**
 * Close manager core server
 *
 * @param manager_core core data
 * @return true for success, false for failure
 */
bool manager_core_server_close(ManagerCore* manager_core);

#endif /*__MANAGER_CORE_H__*/
