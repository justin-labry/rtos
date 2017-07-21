#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <malloc.h>
#include <sys/socket.h>
#include <net/ether.h>
#include <vnic.h>

#include "driver/nicdev.h"
#include "slowpath.h"
#include "io_mux.h"

#define MAX_PACKET_SIZE	2048
#define ALIGN	16

// Linux Kernel -> PacketNgin NetApp
static int slowpath_read_handler(int fd, void* context) {
	char buffer[MAX_PACKET_SIZE];
	VNIC* vnic = context;

	int size = read(fd, buffer, MAX_PACKET_SIZE);
	if(size > 0) {
		//TODO Check MTU

		nicdev_srx(vnic, buffer, size);
	}


	return 0;
}

static bool packet_process(Packet* packet, void* context) {
	int fd = (int)(uint64_t)context;
	if(packet) {
		while(packet->end - packet->start) {
			packet->start += write(fd, packet->buffer + packet->start, packet->end - packet->start);
		}
		return true;
	}

	return false;
}

// PacketNgin NetApp -> Linux Kernel
static int slowpath_write_event(int fd, void* context) {
	VNIC* vnic = context;
	nicdev_stx(vnic, packet_process, (void*)(uint64_t)fd);

	return 0;
}

int slowpath_up(VNIC* vnic) {
	int fd = socket(PF_INET, SOCK_DGRAM, 0);
	if(fd < 0)
		fd = socket(PF_PACKET, SOCK_DGRAM, 0);
	if(fd < 0)
		fd = socket(PF_INET6, SOCK_DGRAM, 0);
	if(fd < 0)
		return -1;

	struct ifreq ifr;
	strncpy(ifr.ifr_name, vnic->name, IFNAMSIZ);
	int err = ioctl(fd, SIOCGIFFLAGS, &ifr);
	if(err < 0) {
		perror("ioctl(SIOCGIFFLAGS)");
		close(fd);
		return -1;
	}
	ifr.ifr_flags |= IFF_UP;
	err = ioctl(fd, SIOCSIFFLAGS, &ifr);
	if(err < 0) {
		perror("ioctl(SIOCSIFFLAGS)");
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}

int slowpath_create(VNIC* vnic) {
	int fd = open("/dev/net/tun", O_RDWR);
	if(fd < 0) return -1;

	// Create New TAP Interface
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(struct ifreq));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	strncpy(ifr.ifr_name, vnic->name, IFNAMSIZ);
	int err = ioctl(fd, TUNSETIFF, (void*)&ifr);
	if(err < 0) goto error;

	//Set HW Address
	err = ioctl(fd, SIOCGIFHWADDR, &ifr); // Get Current HW Address
	if(err < 0) goto error;

	uint64_t* mac = (uint64_t*)ifr.ifr_hwaddr.sa_data;
	*mac = endian48(vnic->mac);

	err = ioctl(fd, SIOCSIFHWADDR, &ifr); //Set New HW Address
	if(err < 0) goto error;

	// Regist IO Multiplexer Event

	IOMultiplexer* io_mux = NULL;
	io_mux = calloc(1, sizeof(IOMultiplexer));
	if(!io_mux) goto error;
	io_mux->fd = fd;
	io_mux->context = vnic;
	io_mux->read_handler = slowpath_read_handler;
	io_mux->write_event = slowpath_write_event;

	if(!io_mux_add(io_mux, (uint64_t)vnic)) goto error;

	// FIXME: Check return value

	return 0;

error:
	//TODO add error handling
	if(io_mux) free(io_mux);

	ioctl(fd, TUNSETPERSIST, 0);
	close(fd);

	return -1;
}

int slowpath_destroy(VNIC* vnic) {
	IOMultiplexer* io_mux = io_mux_remove((uint64_t)vnic);

	//Close Tap interface
	if(io_mux) {
		ioctl(io_mux->fd, TUNSETPERSIST, 0);
		close(io_mux->fd);

		free(io_mux);
	}

	return 0;
}

bool slowpath_init() {
	return true;
}

int slowpath_interface_add(VNIC* vnic) {
	// TODO: Fill me!!!
	return 0;
}

int slowpath_interface_remove(VNIC* vnic) {
	// TODO: Fill me!!!
	return 0;
}
