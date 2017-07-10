#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <malloc.h>

#include <util/event.h>
#include <util/map.h>
#include "io_mux.h"

static fd_set fds;
static int max_fd;
static struct timeval tv; //Busy Poll
static Map* io_mux_table;

bool io_mux_poll(void* context) {
	fd_set read_fds;
	read_fds = fds;

	int retval = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

	if(retval == -1) {
		perror("IO Mux Error\n");
	} else if(retval) {
		MapIterator iter;
		map_iterator_init(&iter, io_mux_table);
		while(map_iterator_has_next(&iter)) {
			MapEntry* entry = map_iterator_next(&iter);
			IOMultiplexer* io_mux = entry->data;

			//Read Handle
			if(FD_ISSET(io_mux->fd, &read_fds)) {
				if(io_mux->read_handler && io_mux->read_handler(io_mux->fd, io_mux->context) < 0) {
					perror("IO Mux Read Error\n");
				}
			}
		}
	}

	//Write Event
	MapIterator iter;
	map_iterator_init(&iter, io_mux_table);
	while(map_iterator_has_next(&iter)) {
		MapEntry* entry = map_iterator_next(&iter);
		IOMultiplexer* io_mux = entry->data;

		if(io_mux->write_event && io_mux->write_event(io_mux->fd, io_mux->context) < 0) {
			perror("IO Mux Read Error\n");
		}
	}

	return true;
}

bool io_mux_init() {
	FD_ZERO(&fds);
	io_mux_table = map_create(32, NULL, NULL, NULL);
	if(!io_mux_table) return false;

	event_busy_add(io_mux_poll, NULL);
	return true;
}

bool io_mux_add(IOMultiplexer* io_mux, uint64_t key) {
	if(!io_mux) return false;

	if(!io_mux_table) return false;

	if(!map_put(io_mux_table, (void*)key, io_mux)) return false;

	FD_SET(io_mux->fd, &fds);
	max_fd = max_fd < io_mux->fd ? io_mux->fd : max_fd;

	return true;
}

IOMultiplexer* io_mux_remove(uint64_t key) {
	if(!io_mux_table) return NULL;

	IOMultiplexer* io_mux = map_remove(io_mux_table, (void*)key);
	FD_CLR(io_mux->fd, &fds);

	return io_mux;
}
