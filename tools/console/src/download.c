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

	// TODO: Why size is needed?
	printf("Usage: download " UNDERLINE(VM ID) " " UNDERLINE(FILE) " [SIZE]\n");
}

typedef struct {
	char		path[256];
	int		fd;
	uint64_t	file_size;
	uint32_t	offset;
	uint64_t	current_time;
} FileInfo;

static int callback_storage_download(uint32_t offset, void* buf, int32_t size, void* context) {
	FileInfo* file_info = context;

	if(size < 0) {
		printf("Storage Download Error\n");
		close(file_info->fd);
		printf("false\n");
		remove(file_info->path);
		free(file_info);

		rpc_disconnect(rpc);
		return 0;
	}

	if(file_info->offset != offset) {
		lseek(file_info->fd, offset, SEEK_SET);
		file_info->offset = offset;
	}

	size = write(file_info->fd, buf, size);
	file_info->offset += size;

	if(size < 0) {
		printf("Write Error!\n");
		close(file_info->fd);
		printf("false\n");
		remove(file_info->path);
		free(file_info);

		rpc_disconnect(rpc);
		return 0;
	}

	if(size == 0) {
		printf("\nStorage Download Completed\n");
		printf("Total Size : %d Byte\n", file_info->offset);
		close(file_info->fd);

		struct timeval tv;
		gettimeofday(&tv, NULL);
		uint64_t current = tv.tv_sec * 1000 * 1000 + tv.tv_usec;
		printf("time = %0.3f ms\n", (float)(current - file_info->current_time) / (float)1000);

		printf("true\n");
		free(file_info);

		rpc_disconnect(rpc);

		return 0;
	}

	printf(".");
	fflush(stdout);

	return size;
}

static int download(int argc, char** argv) {
	if(argc < 3) {
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

	uint32_t vmid = parse_uint32(argv[1]);

	FileInfo* file_info = (FileInfo*)malloc(sizeof(FileInfo));;
	if(file_info == NULL) {
		return -4;
	}
	memset(file_info, 0, sizeof(FileInfo));

	file_info->fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0755);

	if(file_info->fd < 0) {
		free(file_info);
		return -5;
	}

	strcpy(file_info->path, argv[2]);
	lseek(file_info->fd, 0, SEEK_SET);
	uint64_t download_size = 0;
	if(argc == 4) {
		if(!is_uint32(argv[3]))
			return -6;

		download_size = parse_uint32(argv[3]);
	} else {
		download_size = 0;
	}

	file_info->offset = 0;

	struct timeval tv;
	gettimeofday(&tv, NULL);
	file_info->current_time = tv.tv_sec * 1000 * 1000 + tv.tv_usec;

	rpc_storage_download(rpc, vmid, download_size, callback_storage_download, file_info);

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
	if((rc = download(argc, argv))) {
		printf("Failed to download file. Error code : %d\n", rc);
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

