#include <stdio.h>
#include <string.h>

#include "net/ether.h"
#include "util/cmd.h"
#include "driver/nicdev.h"
#include "packet_dump.h"
#include "rtc.h"

#define DUMP_ON				1 << 0
#define DUMP_VERBOSE_INFO	1 << 2
#define DUMP_VVERBOSE_INFO	1 << 3
#define DUMP_PACKET			1 << 4

static uint8_t packet_debug_switch;

static bool packet_dump(void* buf, size_t size, void* context) {
	if(unlikely(!!(packet_debug_switch & DUMP_ON))) {

		uint32_t time = rtc_time();
		printf("%s\n", context);

		char* eth_type;
		Ether* eth = buf;
		switch(endian16(eth->type)) {
			case ETHER_TYPE_IPv4:
				eth_type = "IPv4";
				break;
			case ETHER_TYPE_ARP:
				eth_type = "ARP";
				break;
			case ETHER_TYPE_IPv6:
				eth_type = "IPv6";
				break;
			case ETHER_TYPE_LLDP:
				eth_type = "LLDP";
				break;
			case ETHER_TYPE_8021Q:
				eth_type = "8021Q";
				break;
			case ETHER_TYPE_8021AD:
				eth_type = "8021AD";
				break;
			case ETHER_TYPE_QINQ1:
				eth_type = "QINQ1";
				break;
			case ETHER_TYPE_QINQ2:
				eth_type = "QINQ2";
				break;
			case ETHER_TYPE_QINQ3:
				eth_type = "QINQ3";
				break;
			default:
				eth_type = "Unknown";
				break;
		}
		printf("%02d:%02d:%02d %s length: %d\n", RTC_HOUR(time), RTC_MINUTE(time), RTC_SECOND(time), eth_type, size);

		if(packet_debug_switch & (DUMP_VERBOSE_INFO | DUMP_VVERBOSE_INFO)) {
			uint64_t dmac = endian48(eth->dmac);
			uint64_t smac = endian48(eth->smac);
			printf("%02x:%02x:%02x:%02x:%02x:%02x %02x:%02x:%02x:%02x:%02x:%02x\n",
					(dmac >> 40) & 0xff, (dmac >> 32) & 0xff, (dmac >> 24) & 0xff,
					(dmac >> 16) & 0xff, (dmac >> 8) & 0xff, (dmac >> 0) & 0xff,
					(smac >> 40) & 0xff, (smac >> 32) & 0xff, (smac >> 24) & 0xff,
					(smac >> 16) & 0xff, (smac >> 8) & 0xff, (smac >> 0) & 0xff);
		}
		if(packet_debug_switch & DUMP_VVERBOSE_INFO) {
			//TODO
		}

		if(packet_debug_switch & DUMP_PACKET) {
			uint16_t* data = buf;
			int i = 0;
			while((void*)data < buf + size) {
				printf("\t0x%04x:\t", i);
				i += 16;
				if((void*)(data + 8) <= buf + size) {
					printf("%04x %04x %04x %04x %04x %04x %04x %04x\n"
							, endian16(*data), endian16(*(data + 1)), endian16(*(data + 2)), endian16(*(data + 3)), endian16(*(data + 4)), endian16(*(data + 5)), endian16(*(data + 6)), endian16(*(data + 7)));
					data += 8;
					continue;
				}

				while((void*)(data + 1) <= buf + size) {
					printf("%04x ", endian16(*data++));
				}

				if((void*)data < buf + size) {
					printf("%02x ", *(uint8_t*)data & 0xff);
					data = (uint16_t*)((uint8_t*)data + 1);
				}
				printf("\n");
			}
			printf("\n");
		}
	}

	return true;
}

static bool is_init;

static int cmd_dump(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	if(!is_init) {
		printf("Packet Dumper is not initialized\n");
		return 0;
	}

	uint8_t temp = 0;
	for(int i = 1; i < argc; i++) {
		if(!strcmp(argv[i], "-v")) {
			temp |= DUMP_VERBOSE_INFO | DUMP_ON;
		} else if(!strcmp(argv[i], "-vv")) {
			temp |= DUMP_VVERBOSE_INFO | DUMP_ON;
		} else if(!strcmp(argv[i], "-d")) {
			temp |= DUMP_PACKET | DUMP_ON;
		} else return CMD_WRONG_TYPE_OF_ARGS;
	}

	if(!temp)
		packet_debug_switch = packet_debug_switch ? 0 : DUMP_ON;
	else
		packet_debug_switch = temp;

	return 0;
}

static Command commands[] = {
	{
		.name = "dump",
		.desc = "Packet dump",
		.func = cmd_dump
	}
};

int packet_dump_init() {
	if(nicdev_process_register(packet_dump, "RX Packet", NICDEV_RX_PROCESS)) goto error;

	if(nicdev_process_register(packet_dump, "TX Packet", NICDEV_TX_PROCESS)) goto error;

// 	if(nicdev_process_register(packet_dump, "SRX Packet", NICDEV_SRX_PROCESS)) goto error;
// 
// 	if(nicdev_process_register(packet_dump, "STX Packet", NICDEV_STX_PROCESS)) goto error;

	if(cmd_register(commands, sizeof(commands) / sizeof(commands[0]))) goto error;

	is_init = true;

	return 0;

error:
	nicdev_process_unregister(NICDEV_RX_PROCESS);
	nicdev_process_unregister(NICDEV_TX_PROCESS);
// 	nicdev_process_unregister(NICDEV_SRX_PROCESS);
// 	nicdev_process_unregister(NICDEV_STX_PROCESS);

	return -1;
}
