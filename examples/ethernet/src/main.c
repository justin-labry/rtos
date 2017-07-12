#include <stdio.h>
#include <thread.h>
#include <nic.h>
#include <net/ether.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/checksum.h>
#include <net/udp.h>

void ginit(int argc, char** argv) {
}

void init(int argc, char** argv) {
}

static uint32_t address = 0xc0a8640a;	// 192.168.100.10

void process(NIC* ni) {
	Packet* packet = nic_rx(ni);
	if(!packet)
		return;

	Ether* ether = (Ether*)(packet->buffer + packet->start);
	printf("dmac: %02x:%02x:%02x:%02x:%02x:%02x ",
			(ether->dmac >> 0) & 0xff, (ether->dmac >> 8) & 0xff,
			(ether->dmac >> 16) & 0xff, (ether->dmac >> 24) & 0xff,
			(ether->dmac >> 32) & 0xff, (ether->dmac >> 40) & 0xff);
	printf("smac: %02x:%02x:%02x:%02x:%02x:%02x ",
			(ether->smac >> 0) & 0xff, (ether->smac >> 8) & 0xff,
			(ether->smac >> 16) & 0xff, (ether->smac >> 24) & 0xff,
			(ether->smac >> 32) & 0xff, (ether->smac >> 40) & 0xff);
	printf("type: %04x ", endian16(ether->type));
	printf("payload: %d\n", (packet->end - packet->start) - ETHER_LEN);

	if(endian16(ether->type) == ETHER_TYPE_ARP) {
		ARP* arp = (ARP*)ether->payload;
		if(endian16(arp->operation) == 1 && endian32(arp->tpa) == address) {
			ether->dmac = ether->smac;
			ether->smac = endian48(ni->mac);
			arp->operation = endian16(2);
			arp->tha = arp->sha;
			arp->tpa = arp->spa;
			arp->sha = ether->smac;
			arp->spa = endian32(address);

			nic_tx(ni, packet);
			packet = NULL;
		}
	} else if(endian16(ether->type) == ETHER_TYPE_IPv4) {
		IP* ip = (IP*)ether->payload;
		if(ip->protocol == IP_PROTOCOL_ICMP && endian32(ip->destination) == address) {
			ICMP* icmp = (ICMP*)ip->body;
			icmp->type = 0;
			icmp->checksum = 0;
			icmp->checksum = endian16(checksum(icmp, packet->end - packet->start - ETHER_LEN - IP_LEN));

			ip->destination = ip->source;
			ip->source = endian32(address);
			ip->ttl = endian8(64);
			ip->checksum = 0;
			ip->checksum = endian16(checksum(ip, ip->ihl * 4));

			ether->dmac = ether->smac;
			ether->smac = endian48(ni->mac);

			nic_tx(ni, packet);
			packet = NULL;
		} else if(ip->protocol == IP_PROTOCOL_UDP) {
			UDP* udp = (UDP*)ip->body;
			if(endian16(udp->destination) == 7) {
				uint16_t t = udp->destination;
				udp->destination = udp->source;
				udp->source = t;
				udp->checksum = 0;

				uint32_t t2 = ip->destination;
				ip->destination = ip->source;
				ip->source = t2;
				ip->ttl = 0x40;
				ip->checksum = 0;
				ip->checksum = endian16(checksum(ip, ip->ihl * 4));

				uint64_t t3 = ether->dmac;
				ether->dmac = ether->smac;
				ether->smac = t3;

				nic_tx(ni, packet);
				packet = NULL;
			}
		}
	}

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
