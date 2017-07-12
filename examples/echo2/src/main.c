#include <stdio.h>
#include <thread.h>
#include <nic.h>
#include <net/interface.h>
#include <net/ether.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/checksum.h>
#include <net/udp.h>

void ginit(int argc, char** argv) {
}

static uint32_t address = 0xc0a86464;	// 192.168.100.100
static uint32_t netmask = 0xffffff00;	// 255.255.255.0
static uint32_t gateway = 0xc0a864fe;	// 192.168.100.254

void init(int argc, char** argv) {
	uint32_t count = nic_count();
	for(int i = 0; i < count; i++) {
		NIC* nic = nic_get(i);
		interface_alloc(nic, address, netmask, gateway, false);
	}
}

void process(NIC* ni) {
	Packet* packet = nic_rx(ni);
	if(!packet)
		return;

	if(arp_process(ni, packet))
		return;

	if(icmp_process(ni, packet))
		return;

	if(packet)
		nic_free(packet);
}

void destroy() {
}

void gdestroy() {
}

int main(int argc, char** argv) {
	printf("Thread %d booting\n", thread_id());
	if(thread_id() == 0) {
		ginit(argc, argv);
	}

	thread_barrior();

	init(argc, argv);

	thread_barrior();

	uint32_t i = 0;
	while(1) {
 		uint32_t count = nic_count();
 		if(count > 0) {
 			i = (i + 1) % count;
 
 			NIC* ni = nic_get(i);
 			if(nic_has_rx(ni)) {
 				process(ni);
 			}
 		}
	}

	thread_barrior();

	destroy();

	thread_barrior();

	if(thread_id() == 0) {
		gdestroy();
	}

	return 0;
}
