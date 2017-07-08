#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

#include <_malloc.h>
#include <control/rpc.h>
#include <util/event.h>
#include "manager.h"
#include "manager_core.h"

typedef struct _Connection {
	int	sock;
	struct	sockaddr_in addr;
	uint64_t last_response;
} Connection;
#define RESPONSE_TIMEOUT 1500 // 1.5sec

static int (*on_accept)(RPC* rpc);
static ManagerCore* manager_core;
static uint64_t current;

/*RPC Client*/
/*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*/
static int client_read(RPC* rpc, void* buf, int size) {
	Connection* conn = (Connection*)rpc->data;

	ssize_t len = recv(conn->sock, buf, size, MSG_DONTWAIT);
	if(len <= 0)
		return (errno == EAGAIN || errno == 0) ? 0 : -1;

	conn->last_response = current;
	return len;
}

static int client_write(RPC* rpc, void* buf, int size) {
	Connection* conn = (Connection*)rpc->data;

	ssize_t len = send(conn->sock, buf, size, MSG_DONTWAIT | MSG_NOSIGNAL);
	if(len <= 0)
		return (errno == EAGAIN || errno == 0) ? 0 : -1;

	conn->last_response = current;
	return len;
}

static void client_close(RPC* rpc) {
	Connection* data = (Connection*)rpc->data;
	if(data->sock == -1) return;

	close(data->sock);
	data->sock = -1;
}

static bool client_process(void* event_context) {
	struct timeval t;
	if(gettimeofday(&t, NULL) == 0)
		current = t.tv_sec * 1000 + t.tv_usec / 1000;

	ListIterator iter;
	list_iterator_init(&iter, manager_core->clients);
	while(list_iterator_has_next(&iter)) {
		RPC* rpc = list_iterator_next(&iter);
		Connection* conn = (Connection*)rpc->data;

		rpc_loop(rpc);

		if(current - conn->last_response >= RESPONSE_TIMEOUT) {
			list_iterator_remove(&iter);
			client_close(rpc);
			free(rpc);
		}
	}

	return true;
}

static bool client_rpc_compare(void* a, void* b) {
	RPC* a_rpc = a;
	RPC* b_rpc = b;
	Connection* a_conn = (Connection*)a_rpc->data;
	Connection* b_conn = (Connection*)b_rpc->data;
	return a_conn->sock == b_conn->sock;
}

/*RPC Server*/
/*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*/
static Connection* server_create(int port) {
	int error = 0;
	int server = -1;
	struct sockaddr_in serveraddr = {};

	server = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(server == -1) goto failure;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(port);

	error = bind(server, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
	if(error) goto failure;

	error = listen(server, 10);
	if(error) goto failure;

	error = fcntl(server, F_SETFL, fcntl(server, F_GETFL, 0) | O_NONBLOCK);
	if(error) goto failure;

	Connection* conn = calloc(1, sizeof(Connection));
	if(!conn) goto failure;
	conn->sock = server;
	conn->addr = serveraddr;

	return conn;

failure:
	if(server != -1)
		close(server);
	return NULL;
}

static bool server_accept(void* event_context) {
	Connection* conn = manager_core->data;
	struct sockaddr_in client_addr;
	socklen_t client_addrlen;
	int client = -1;
	RPC* client_rpc = NULL;

	// Create client socket
	client = accept(conn->sock, (struct sockaddr*)&client_addr, &client_addrlen);
	if(client == -1) return true;

	// Create client rpc
	client_rpc = calloc(1, sizeof(RPC) + sizeof(Connection));
	if(!client_rpc) goto failure;
	client_rpc->read = client_read;
	client_rpc->write = client_write;
	client_rpc->close = client_close;
	Connection* data = (Connection*)client_rpc->data;
	data->sock = client;
	data->addr = client_addr;
	data->last_response = current;
	on_accept(client_rpc);

	// Push rpc object if not exists
	bool exists = list_index_of(manager_core->clients, client_rpc, client_rpc_compare) != -1;
	if(exists) goto failure;
	list_add(manager_core->clients, client_rpc);

	return true;

failure:
	if(client != -1)
		close(client);
	if(client_rpc)
		free(client_rpc);
	return true;
}

/*RPC Core Interface*/
/*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*/
int manager_core_init(int (*_accept)(RPC* rpc)) {
	on_accept = _accept;
	return 0;
}

ManagerCore* manager_core_server_open(uint16_t port) {
	if(manager_core) return manager_core;

	// Create manager core and server
	manager_core = calloc(1, sizeof(ManagerCore));
	if(!manager_core) goto failure;
	Connection* server_conn = server_create(port);
	if(!server_conn) goto failure;

	manager_core->port = port;
	manager_core->data = server_conn;
	manager_core->clients = list_create(NULL);

	// Then regsiter server side rpc event loop
	event_busy_add(server_accept, NULL);
	event_idle_add(client_process, NULL);

	return manager_core;

failure:
	if(manager_core) {
		if(manager_core->clients)
			list_destroy(manager_core->clients);
		free(manager_core);
	}
	free(server_conn);
	return NULL;
}

bool manager_core_server_close(ManagerCore* manager_core) {
	if(!manager_core) return false;

	Connection* conn = manager_core->data;
	close(conn->sock);
	free(conn);
	free(manager_core), manager_core = NULL;

	return true;
}
