#include <string.h>
#include <errno.h>
#include <stdlib.h>

// PacketNgin
#include <util/cmd.h>
#include <util/types.h>
#include <util/map.h>
#include <util/event.h>
#include <timer.h>
#include <net/interface.h>
#include <net/ether.h>
#include <net/arp.h>
#include <nic.h>
#include <vnic.h>
#include "gmalloc.h"
#include "manager.h"
#include "manager_core.h"
#include "driver/nicdev.h"
#include "vm.h"

// lwip
#include <netif/etharp.h>
#include <lwip/init.h>
#include <lwip/ip.h>
#include <lwip/tcp.h>
#include <lwip/netif.h>
#include <arch/driver.h>

typedef struct _Manager {
	int	vnic_count;
	VNIC*	vnics[MAX_VNIC_COUNT];	// gmalloc, ni_create
	List* 	netifs;
	List*	servers;
	List* 	actives;
} Manager;

extern NIC* __nics[MAX_VNIC_COUNT];
extern int __nic_count;

ManagerCore* manager_core;
Manager manager;
static struct netif* netif;
static List* rx_process_list; // TODO netif includes fix rx_process_list
static err_t (*core_accept)(RPC* rpc);

// LwIP TCP callbacks
typedef struct {
	struct tcp_pcb*	pcb;
	List*			pbufs;
	int				poll_count;
} RPCData;

typedef struct _ARPingContext {
	NIC* nic;
	uint32_t addr;
	uint32_t count;
	uint64_t time;
} ARPingContext;

typedef struct _ProcessContext {
	bool (*process)(Packet* packet, void* context);
	void* context;
} ProcessContext;

/* struct netif* manager_create_netif(NIC* nic, uint32_t ip, uint32_t netmask, uint32_t gateway, bool is_default); */
/* bool manager_destroy_netif(struct netif* netif); */

/* uint32_t manager_netif_get_ip(); */
/* void manager_netif_set_ip(struct netif* netif, uint32_t ip); */

/* uint32_t manager_netif_get_port(); */
/* void manager_netif_set_port(uint16_t port); */

/* uint32_t manager_netif_get_gateway(); */
/* void manager_netif_set_gateway(uint32_t gw); */

/* uint32_t manager_netif_get_netmask(); */
/* void manager_netif_set_netmask(uint32_t nm); */

/* void manager_netif_set_interface(); */

static int pcb_read(RPC* rpc, void* buf, int size);
static int pcb_write(RPC* rpc, void* buf, int size);
static void pcb_close(RPC* rpc);

static err_t manager_recv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err);
static void manager_err(void* arg, err_t err);
static err_t manager_poll(void* arg, struct tcp_pcb* pcb);

static void print_byte_size(uint64_t byte_size) {
	uint64_t size = 1;
	for(int i = 0; i < 5; i++) {
		if((byte_size / size) < 1000) {
			printf("(%.1f ", (float)byte_size / size);
			switch(i) {
				case 0:
					printf("B)");
					break;
				case 1:
					printf("KB)");
					break;
				case 2:
					printf("MB)");
					break;
				case 3:
					printf("GB)");
					break;
				case 4:
					printf("TB)");
					break;
			}
			return;
		}

		size *= 1000;
	}
}

static bool parse_vnic_interface(char* name, uint16_t* vmid, uint16_t* vnic_index, uint16_t* interface_index) {
	if(strncmp(name, "v", 1)) return false;

	char* next;
	char* _vmid = strtok_r(name + 1, "eth", &next);
	if(!_vmid) return false;

	if(!is_uint8(_vmid)) return false;

	*vmid = parse_uint8(_vmid);

	char* _interface_index;
	char* _vnic_index = strtok_r(next, ":", &_interface_index);
	if(!_vnic_index) return false;

	if(!is_uint8(_vnic_index)) return false;

	*vnic_index = parse_uint8(_vnic_index);

	if(_interface_index) {
		if(!is_uint8(_interface_index)) return false;

		*interface_index = parse_uint8(_interface_index) + 1;
	} else *interface_index = 0;

	return true;
}

static bool parse_addr(char* argv, uint32_t* address) {
	char* next = NULL;
	uint32_t temp;
	temp = strtol(argv, &next, 0);
	if(temp > 0xff) return false;

	*address = (temp & 0xff) << 24;
	if(next == argv) return false;

	argv = next;
	if(*argv != '.') return false;

	argv++;
	temp = strtol(argv, &next, 0);
	if(temp > 0xff) return false;

	*address |= (temp & 0xff) << 16;
	if(next == argv) return false;

	argv = next;
	if(*argv != '.') return false;

	argv++;
	temp = strtol(argv, &next, 0);
	if(temp > 0xff) return false;

	*address |= (temp & 0xff) << 8;
	if(next == argv) return false;

	argv = next;
	if(*argv != '.') return false;

	argv++;
	temp = strtol(argv, &next, 0);
	if(temp > 0xff) return false;

	*address |= temp & 0xff;
	if(next == argv) return false;

	argv = next;
	if(*argv != '\0') return false;

	return true;
}

static void rpc_free(RPC* rpc) {
	RPCData* data = (RPCData*)rpc->data;

	ListIterator iter;
	list_iterator_init(&iter, data->pbufs);
	while(list_iterator_has_next(&iter)) {
		struct pbuf* pbuf = list_iterator_next(&iter);
		list_iterator_remove(&iter);
		pbuf_free(pbuf);
	}
	list_destroy(data->pbufs);
	list_remove_data(manager.actives, rpc);
	free(rpc);

	list_remove_data(manager_core->clients, rpc);
}

static VNIC* manager_create_vnic(char* nicdev_name) {
	if(manager.vnic_count > MAX_VNIC_COUNT) return NULL;

	NICDevice* nicdev = nicdev_get(nicdev_name);
	if(!nicdev) {
		printf("\tCannot create manager\n");
		return NULL;
	}

	uint64_t attrs[] = {
		//VNIC_MAC, nicdev->mac,
		VNIC_MAC, nicdev->mac,
		VNIC_DEV, (uint64_t)nicdev->name,
		VNIC_BUDGET, 32,
		VNIC_POOL_SIZE, 0x200000,
		VNIC_RX_BANDWIDTH, 1000000000L,
		VNIC_TX_BANDWIDTH, 1000000000L,
		VNIC_RX_QUEUE_SIZE, 1024,
		VNIC_TX_QUEUE_SIZE, 1024,
		VNIC_PADDING_HEAD, 32,
		VNIC_PADDING_TAIL, 32,
		VNIC_SLOW_RX_QUEUE_SIZE, 1024,
		VNIC_SLOW_TX_QUEUE_SIZE, 1024,
		VNIC_NONE
	};

	VNIC* vnic = gmalloc(sizeof(VNIC));
	if(!vnic) goto fail;

	memset(vnic, 0, sizeof(VNIC));

	vnic->nic = bmalloc(1);
	if(!vnic->nic) goto fail;

	vnic->id = vnic_alloc_id();
	vnic->nic_size = 0x200000;
	memset(vnic->nic, 0, 0x200000);

	if(!vnic_init(vnic, attrs)) goto fail;

	int vnic_id = nicdev_register_vnic(nicdev, vnic);
	if(vnic_id < 0) return NULL;

	for(int i = 0; i < MAX_VNIC_COUNT; i++) {
		if(manager.vnics[i]) continue;

		manager.vnics[i] = vnic;
		manager.vnic_count++;
		__nics[i] = vnic->nic;
		__nic_count++;
		break;
	}

	return vnic;

fail:
	if(vnic) gfree(vnic);

	if(vnic && vnic->nic) bfree(vnic->nic);

	return NULL;
}

static bool manager_destroy_vnic(VNIC* vnic) {
	for(int i = 0; i < MAX_VNIC_COUNT; i++) {
		if(manager.vnics[i] != vnic) continue;

		manager.vnics[i] = NULL;
		manager.vnic_count--;
		__nics[i] = NULL;
		__nic_count++;
		break;
	}

	NICDevice* nic_dev = nicdev_get(vnic->parent);
	if(!nic_dev) return false;

	if(!nicdev_unregister_vnic(nic_dev, vnic->id)) return false;

	bfree(vnic->nic);
	gfree(vnic);

	return true;
}

static bool arping_process(Packet* packet, void* context) {
	ARPingContext* arping_context = context;

	Ether* ether = (Ether*)(packet->buffer + packet->start);
	if(endian16(ether->type) != ETHER_TYPE_ARP) return true;

	ARP* arp = (ARP*)ether->payload;
	switch(endian16(arp->operation)) {
		case 2: // Reply
			;
			uint64_t smac = endian48(arp->sha);
			uint32_t sip = endian32(arp->spa);

			if(arping_context->addr == sip) {
				uint32_t time = timer_ns() - arping_context->time;
				uint32_t ms = time / 1000;
				uint32_t ns = time - ms * 1000;

				printf("Reply from %d.%d.%d.%d [%02x:%02x:%02x:%02x:%02x:%02x] %d.%dms\n",
						(sip >> 24) & 0xff,
						(sip >> 16) & 0xff,
						(sip >> 8) & 0xff,
						(sip >> 0) & 0xff,
						(smac >> 40) & 0xff,
						(smac >> 32) & 0xff,
						(smac >> 24) & 0xff,
						(smac >> 16) & 0xff,
						(smac >> 8) & 0xff,
						(smac >> 0) & 0xff,
						ms, ns);
			}
			break;
	}

	if(!arping_context->count) {
		free(arping_context);
		return false;
	}

	return true;
}

static bool arping(void* context) {
	ARPingContext* arping_context = context;
	arping_context->time = timer_ns();
	arp_request(arping_context->nic, arping_context->addr);
	arping_context->count--;

	return arping_context->count ? true : false;
}

static bool rx_process_add_process(ProcessContext* context) {
	return list_add(rx_process_list, context);
}

static bool manager_arping(NIC* nic, uint32_t addr, uint32_t count) {
	ARPingContext* arping_context = calloc(1, sizeof(ARPingContext));
	if(!arping_context) return false;

	arping_context->nic = nic;
	arping_context->addr = addr;
	arping_context->count = count;
	uint64_t event_id = event_timer_add(arping, arping_context, 0, 1000000);
	if(!event_id) {
		free(arping_context);
		return false;
	}

	ProcessContext* process_context = calloc(1, sizeof(ProcessContext));
	if(!process_context) {
		free(arping_context);
		event_timer_remove(event_id);
		return false;
	}

	process_context->process = arping_process;
	process_context->context = arping_context;
	if(!rx_process_add_process(process_context)) {
		free(arping_context);
		event_timer_remove(event_id);
		free(process_context);
	}
	return true;
}

static Packet* rx_process(Packet* packet) {
	ListIterator iter;
	list_iterator_init(&iter, rx_process_list);
	while(list_iterator_has_next(&iter)) {
		ProcessContext* context = list_iterator_next(&iter);
		if(!context->process(packet, context->context))
			list_iterator_remove(&iter);
	}

	return packet;
}

static Packet* tx_process(Packet* packet) {
	return packet;
}

struct netif* manager_create_netif(NIC* nic, uint32_t ip, uint32_t netmask, uint32_t gateway, bool is_default) {
	IPv4Interface* interface = interface_alloc(nic, ip, netmask, gateway, is_default);
	if(!interface) return false;

	struct netif* netif = calloc(1, sizeof(struct netif));
	struct netif_private* private = calloc(1, sizeof(struct netif_private));

	private->nic = nic;
	private->rx_process = rx_process;
	private->tx_process = tx_process;

	struct ip_addr ip2, netmask2, gateway2;
	IP4_ADDR(&ip2, (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, (ip >> 0) & 0xff);
	IP4_ADDR(&netmask2, (netmask >> 24) & 0xff, (netmask >> 16) & 0xff, (netmask >> 8) & 0xff, (netmask >> 0) & 0xff);
	IP4_ADDR(&gateway2, (gateway >> 24) & 0xff, (gateway >> 16) & 0xff, (gateway >> 8) & 0xff, (gateway >> 0) & 0xff);

	netif_add(netif, &ip2, &netmask2, &gateway2, private, lwip_driver_init, ethernet_input);

	if(is_default) netif_set_default(netif);

	netif_set_up(netif);
	list_add(manager.netifs, netif);

	return netif;
}

static err_t manager_accept(void* arg, struct tcp_pcb* pcb, err_t err) {
	struct tcp_pcb_listen* server = arg;
	tcp_accepted(server);
	//printf("Accepted: %p\n", pcb);

	RPC* rpc = calloc(1, sizeof(RPC) + sizeof(RPCData));
	rpc->read = pcb_read;
	rpc->write = pcb_write;
	rpc->close = pcb_close;

	core_accept(rpc);
	RPCData* data = (RPCData*)rpc->data;
	data->pcb = pcb;
	data->pbufs = list_create(NULL);

	tcp_arg(pcb, rpc);
	tcp_recv(pcb, manager_recv);
	tcp_err(pcb, manager_err);
	tcp_poll(pcb, manager_poll, 2);

	list_add(manager_core->clients, rpc);

	return ERR_OK;
}

static void manager_close(struct tcp_pcb* pcb, RPC* rpc, bool is_force) {
	printf("Close connection: %p\n", pcb);
	tcp_arg(pcb, NULL);
	tcp_sent(pcb, NULL);
	tcp_recv(pcb, NULL);
	tcp_err(pcb, NULL);
	tcp_poll(pcb, NULL, 0);

	if(rpc) {
		rpc_free(rpc);
		list_remove_data(manager_core->clients, rpc);
	}

	if(is_force) tcp_abort(pcb);
	else if(tcp_close(pcb) != ERR_OK) printf("\tCannot close pcb: %p %p\n", pcb, rpc);
}

inline void manager_netif_set_ip(struct netif* netif, uint32_t ip) {
	struct ip_addr ip2;
	IP4_ADDR(&ip2, (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, (ip >> 0) & 0xff);
	netif_set_ipaddr(netif, &ip2);
}

inline void manager_netif_set_gateway(struct netif* netif, uint32_t gateway) {
	struct ip_addr gateway2;
	IP4_ADDR(&gateway2, (gateway >> 24) & 0xff, (gateway >> 16) & 0xff, (gateway >> 8) & 0xff, (gateway >> 0) & 0xff);
	netif_set_gw(netif, &gateway2);
}

inline void manager_netif_set_netmask(struct netif* netif, uint32_t netmask) {
	struct ip_addr netmask2;
	IP4_ADDR(&netmask2, (netmask >> 24) & 0xff, (netmask >> 16) & 0xff, (netmask >> 8) & 0xff, (netmask >> 0) & 0xff);
	netif_set_netmask(netif, &netmask2);
}

static struct tcp_pcb* manager_netif_server_open(struct netif* netif, uint16_t port) {
	struct ip_addr ip_addr = netif->ip_addr;

	struct tcp_pcb* tcp_pcb = tcp_new();
	err_t err = tcp_bind(tcp_pcb, &ip_addr, port);
	if(err != ERR_OK) return false;

	tcp_pcb = tcp_listen(tcp_pcb);
	tcp_arg(tcp_pcb, tcp_pcb);

	printf("\tManager started: %d.%d.%d.%d:%d\n", ip_addr.addr & 0xff, (ip_addr.addr >> 8) & 0xff,
		(ip_addr.addr >> 16) & 0xff, (ip_addr.addr >> 24) & 0xff, port);

	tcp_accept(tcp_pcb, manager_accept);

	return tcp_pcb;
}

static bool manager_netif_server_close(struct tcp_pcb* tcp_pcb) {
	ListIterator iter;
	list_iterator_init(&iter, manager_core->clients);
	while(list_iterator_has_next(&iter)) {
		RPC* rpc = list_iterator_next(&iter);
		RPCData* data = (RPCData*)rpc->data;
		manager_close(data->pcb, rpc, true);
	}

	if(tcp_close(tcp_pcb) != ERR_OK) {
		printf("\tCannot close manager server: %p", tcp_pcb);

		return false;
	}

	return true;
}

static bool manager_core_loop(void* context) {
	ListIterator iter;
	list_iterator_init(&iter, manager.netifs);
	while(list_iterator_has_next(&iter)) {
		struct netif* netif = list_iterator_next(&iter);
		NIC* nic = ((struct netif_private*)netif->state)->nic;
		if(!nic_has_rx(nic)) continue;

		Packet* packet = nic_rx(nic);
		if(packet) lwip_nic_poll(netif, packet);
	}

	if(!list_is_empty(manager.actives)) {
		ListIterator iter;
		list_iterator_init(&iter, manager.actives);
		while(list_iterator_has_next(&iter)) {
			RPC* rpc = list_iterator_next(&iter);
			if(rpc_is_active(rpc)) {
				rpc_loop(rpc);
			} else {
				list_iterator_remove(&iter);
			}
		}
	}

	return true;
}


static bool manager_core_timer(void* context) {
	lwip_nic_timer();

	return true;
}

static int pcb_read(RPC* rpc, void* buf, int size) {
	RPCData* data = (RPCData*)rpc->data;

	int idx = 0;
	ListIterator iter;
	list_iterator_init(&iter, data->pbufs);
	while(list_iterator_has_next(&iter)) {
		struct pbuf* pbuf = list_iterator_next(&iter);
		if(idx + pbuf->len <= size) {
			memcpy(buf + idx, pbuf->payload, pbuf->len);
			idx += pbuf->len;
			size -= pbuf->len;
			pbuf_free(pbuf);
			list_iterator_remove(&iter);
		} else break;
	}

	return idx;
}

static int pcb_write(RPC* rpc, void* buf, int size) {
	RPCData* data = (RPCData*)rpc->data;

	int len = tcp_sndbuf(data->pcb);
	len = len > size ? size : len;

	if(tcp_write(data->pcb, buf, len, TCP_WRITE_FLAG_COPY) != ERR_OK) {
		errno = -1;
		return -1;
	}

	if(tcp_output(data->pcb) != ERR_OK) {
		errno = -2;
		return -1;
	}

	return len;
}

static void pcb_close(RPC* rpc) {
	RPCData* data = (RPCData*)rpc->data;
	manager_close(data->pcb, rpc, false);
}

static err_t manager_recv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
	RPC* rpc = arg;

	if(p == NULL) {	// Remote host closed the connection
		manager_close(pcb, rpc, false);
	} else if(err != ERR_OK) {
		pbuf_free(p);
	} else {
		RPCData* data = (RPCData*)rpc->data;
		list_add(data->pbufs, p);

		rpc_loop(rpc);
		tcp_recved(pcb, p->len);
		if(rpc_is_active(rpc)) {
			if(list_index_of(manager.actives, rpc, NULL) < 0) {
				list_add(manager.actives, rpc);
			}
		}
	}

	return ERR_OK;
}

static void manager_err(void* arg, err_t err) {
	RPC* rpc = arg;
	if(rpc != NULL) {
		RPCData* data = (RPCData*)rpc->data;
		printf("Error closed: %d %p\n", err, data->pcb);
		rpc_free(rpc);
	}
}

static err_t manager_poll(void* arg, struct tcp_pcb* pcb) {
	RPC* rpc = arg;

	if(rpc == NULL) {
		manager_close(pcb, NULL, true);
	} else {
		RPCData* data = (RPCData*)rpc->data;
		data->poll_count++;

		if(rpc->ver == 0 && data->poll_count++ >= 4) {	// 2 seconds
			printf("Close connection: not receiving hello in 2 secs.\n");
			manager_close(pcb, rpc, false);
		}
	}

	return ERR_OK;
}
// 
// static int cmd_manager_core(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
// 	//TODO error handle
// 	for(int i = 1; i < argc; i++) {
// 		if(!strcmp("create", argv[i])) {
// 			NEXT_ARGUMENTS();
// 
// 			VNIC* vnic = manager_create_vnic(argv[i]);
// 			if(!vnic) printf("fail\n");
// 
// 			return 0;
// 		} else if(!strcmp("destroy", argv[i])) {
// 			NEXT_ARGUMENTS();
// 
// 			uint16_t vmid;
// 			uint16_t vnic_index;
// 			uint16_t interface_index;
// 			if(!parse_vnic_interface(argv[i], &vmid, &vnic_index, &interface_index)) return -i;
// 
// 			if(vmid) return -i;
// 
// 			VNIC* vnic = manager.vnics[vnic_index];
// 			if(!vnic) return -i;
// 
// 			if(!manager_destroy_vnic(vnic))
// 				printf("fail\n");
// 
// 			return 0;
// 		} else if(!strcmp("show", argv[i])) {
// 			for(int i = 0; i < MAX_VNIC_COUNT; i++) {
// 				VNIC* vnic = manager.vnics[i];	// gmalloc, ni_create
// 				if(!vnic) continue;
// 
// 				print_interface(vnic, 0, i);
// 			}
// 
// 			return 0;
// 		}
// 	}
// 
// 	return 0;
// }

static int cmd_vlan(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	if(argc < 3) {
		printf("Wrong number of arguments\n");
		return -1;
	}

	for(int i = 1; i < argc; i++) {
		if(!strcmp(argv[i], "add")) {
			NEXT_ARGUMENTS();
			NICDevice* nicdev = nicdev_get(argv[i]);
			if(!nicdev) {
				printf("Cannot Found Device!\n");
				return -2;
			}

			NEXT_ARGUMENTS();
			if(!is_uint16(argv[i])) {
				printf("VLAN ID Wrong!\n");
				return -3;
			}
			uint16_t vid = parse_uint16(argv[i]);

			if(vid == 0 || vid > 4096) {
				printf("VLAN ID Wrong!\n");
				return -3;
			}
			NICDevice* vlan_nicdev = nicdev_add_vlan(nicdev, vid);
			if(!vlan_nicdev)
				return -3;

			printf("Create VLAN NIC: %s\n", vlan_nicdev->name);
		} else if(!strcmp(argv[i], "remove")) {
			NEXT_ARGUMENTS();
			NICDevice* nicdev = nicdev_get(argv[i]);
			if(!nicdev) {
				printf("Cannot Found Device!\n");
				return -2;
			}
			if(!nicdev_remove_vlan(nicdev)) {
				printf("Cannot Remove VLAN NIC!\n");
			}
		} else if(!strcmp(argv[i], "set")) {
		} else return -1;
	}

	return 0;
}

static int cmd_arping(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	if(argc < 2) {
		return CMD_STATUS_WRONG_NUMBER;
	}

	NIC* nic = NULL;
	uint32_t addr = 0;
	uint32_t count = 1;

	if(!parse_addr(argv[1], &addr)) {
		printf("Address Wrong\n");
		return -1;
	}

	for(int i = 2; i < argc; i++) {
		if(!strcmp(argv[i], "-c")) {
			NEXT_ARGUMENTS();

			if(!is_uint32(argv[i])) return -i;

			count = parse_uint32(argv[i]);
		} else if(!strcmp(argv[i], "-I")) {
			NEXT_ARGUMENTS();

			uint16_t vmid;
			uint16_t vnic_index;
			uint16_t interface_index;
			if(!parse_vnic_interface(argv[i], &vmid, &vnic_index, &interface_index)) return -i;

			if(vmid) return -i;

			VNIC* vnic = manager.vnics[vnic_index];
			if(!vnic) return -i;

			nic = vnic->nic;
		}
	}

	if(!nic) return -1;

	manager_arping(nic, addr, count);

	return 0;
}

static Command commands[] = {
// 	{
// 		.name = "manager_core",
// 		.desc = "Set ip, port, netmask, gateway, nic of manager",
// 		.func = cmd_manager_core
// 	},
	{
		.name = "vlan",
		.desc = "Add or remove vlan",
		.args = "add nicdev_name vlan_id:u16\n"
			"remove device_name",
		.func = cmd_vlan
	},
	{
		.name = "arping",
		.desc = "ARP ping to the host.",
		.args = "address \"-I\" nic_name [\"-c\" count:u32]",
		.func = cmd_arping
	},
};

int manager_core_init(err_t (*_accept)(RPC* rpc)) {
	lwip_init();

	rx_process_list = list_create(NULL);
	manager.netifs = list_create(NULL); //lwip
	if(!manager.netifs) return -1;

	manager.servers = list_create(NULL); //lwip server pcb
	if(!manager.servers) return -1;

	manager.actives = list_create(NULL);
	if(!manager.actives) return -1;

	VNIC* vnic = manager_create_vnic((char*)MANAGER_DEFAULT_NICDEV);
	if(!vnic) return -2;

	//TODO: add manager init script
	netif = manager_create_netif(vnic->nic, MANAGER_DEFAULT_IP, MANAGER_DEFAULT_NETMASK, MANAGER_DEFAULT_GW, true);
	if(!netif) return -3;

	if(!event_idle_add(manager_core_loop, NULL)) return -5;

	if(!event_timer_add(manager_core_timer, NULL, 100000, 100000)) return -6;

	cmd_register(commands, sizeof(commands) / sizeof(commands[0]));

	core_accept = _accept;

	return 0;
}

ManagerCore* manager_core_server_open(uint16_t port) {
	struct tcp_pcb* tcp_pcb =  manager_netif_server_open(netif, port);
	if(!tcp_pcb) return NULL;

	manager_core = calloc(1, sizeof(ManagerCore));
	manager_core->port = port;
	manager_core->data = tcp_pcb;
	manager_core->clients = list_create(NULL);

	return manager_core;
}

bool manager_core_server_close(ManagerCore* manager_core) {
	return manager_netif_server_close((struct tcp_pcb*)manager_core->data);
}
