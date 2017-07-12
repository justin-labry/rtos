#include <stdio.h>
#include <thread.h>
#include <nic.h>

/**
  * Slowpath Simple Application.
  * Must set slowpath for vnic.
 */

void ginit(int argc, char** argv) {
}

void init(int argc, char** argv) {
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
				Packet* packet = nic_rx(ni);
				nic_stx(ni, packet);
			}
			if(nic_has_srx(ni)) {
				Packet* packet = nic_srx(ni);
				nic_tx(ni, packet);
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
