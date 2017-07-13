#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <util/types.h>
#include <control/rpc.h>

#include "../include/rpc_console.h"

static RPC* rpc;

static void help() {
#define ANSI_UNDERLINED_PRE  "\033[4m"
#define ANSI_UNDERLINED_POST "\033[0m"

#define UNDERLINE(OPTION) ANSI_UNDERLINED_PRE #OPTION ANSI_UNDERLINED_POST
	printf("Usage: upload " UNDERLINE(VM ID) " " UNDERLINE(FILE) " [SIZE]\n");
}

typedef struct {
	char		path[256];
	int		fd;
	uint64_t	file_size;
	uint32_t	offset;
	uint64_t	current_time;
} FileInfo;

static int32_t callback_storage_upload(uint32_t offset, void** buf, int32_t size, void* context) {
	FileInfo* file_info = context;
	static uint8_t ubuf[2048];

	if(size < 0) {
		printf("Storage Upload Error\n");
		fflush(stdout);

		close(file_info->fd);
		free(file_info);

		printf("false\n");
		rpc_disconnect(rpc);
		return 0;
	}

	if(size == 0) {
		printf("\nStorage Upload Completed\n");
		fflush(stdout);

		struct timeval tv;
		gettimeofday(&tv, NULL);
		uint64_t current = tv.tv_sec * 1000 * 1000 + tv.tv_usec;
		printf("time = %0.3f ms\n", (float)(current - file_info->current_time) / (float)1000);

		close(file_info->fd);
		free(file_info);

		printf("true\n");
		rpc_disconnect(rpc);
		return 0;
	}

	if(offset == 0)
		printf("Total Upload Size : %ld Byte\n", file_info->file_size);

	if(file_info->offset != offset) {
		lseek(file_info->fd, offset, SEEK_SET);
		file_info->offset = offset;
	}

	size = read(file_info->fd, ubuf, size > (int32_t)(file_info->file_size - file_info->offset) ? (int32_t)(file_info->file_size - file_info->offset) : size);

	if(size < 0) {
		printf("\nFile Read Error\n");
		fflush(stdout);
		buf = NULL;

		rpc_disconnect(rpc);
		return -1;
	}

	file_info->offset += size;
	*buf = ubuf;

	printf(".");
	fflush(stdout);

	return size;
}

static int upload(int argc, char** argv) {
	if(argc < 3 || argc > 4) {
		help();
		return -1;
	}

	if(!is_uint32(argv[1])) {
		help();
		return -2;
	}

	if(strlen(argv[2]) >= 256) {
		help();
		return -3;
	}

	if(argc == 4) {
		if(!is_uint32(argv[3])) {
			help();
			return -4;
		}
	}

	uint32_t vmid = parse_uint32(argv[1]);

	FileInfo* file_info = malloc(sizeof(FileInfo));
	if(file_info == NULL) {
		return -5;
	}
	memset(file_info, 0, sizeof(FileInfo));

	file_info->fd = open(argv[2], O_RDONLY);
	if(file_info->fd < 0) {
		free(file_info);

		return -6;
	}

	file_info->file_size = lseek(file_info->fd, 0, SEEK_END);
	lseek(file_info->fd, 0, SEEK_SET);
	file_info->offset = 0;

	if(argc == 4) {
		uint32_t size = parse_uint32(argv[3]);
		if(size > file_info->file_size) {
			printf("File size is smaller than paramter\n");
			return -7;
		}
		file_info->file_size = size;
	}

	struct timeval tv;
	gettimeofday(&tv, NULL);
	file_info->current_time = tv.tv_sec * 1000 * 1000 + tv.tv_usec;

	rpc_storage_upload(rpc, vmid, callback_storage_upload, file_info);

	return 0;
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
	if((rc = upload(argc, argv))) {
		printf("Failed to upload file. Error code : %d\n", rc);
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
}

