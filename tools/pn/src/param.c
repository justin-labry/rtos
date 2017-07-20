#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/mman.h>
#include <malloc.h>

#include "smap.h"

#define MAX_COMMAND_LINE	2048

int cpu_start;
int cpu_end;

static int parse_present_mask(char* present_mask) {
	char* next = present_mask;
	char* end = NULL;
	char* start = strtok_r(next, "-", &end);

	if(start) {
		cpu_start = strtol(start, NULL, 10);
		cpu_end = cpu_start;
	} else
		return -1;

	if(*end) {
		cpu_end = strtol(end, NULL, 10);
		if(cpu_start > cpu_end)
			return -2;
	}

	return 0;
}

static int parse_args(char* args) {
	int fd = open(args, O_RDONLY);
	if(fd < 0) {
		perror("\tFailed to open arguments file\n");
		return -1;
	}

	off_t args_size = lseek(fd, 0, SEEK_END);
	char* addr = mmap(NULL, args_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if(addr == MAP_FAILED) {
		close(fd);
		return -2;
	}
	//char* boot_command_line = calloc(1, args_size + 1);
	if(MAX_COMMAND_LINE < args_size + 1) return -2;

	char boot_command_line[MAX_COMMAND_LINE] = {0};
	memcpy(boot_command_line, addr, args_size);
	munmap(addr, args_size);
	close(fd);

	char* next = strtok(boot_command_line, "\n");
	char* str;
	bool smap_fixed = false;
	while(next && (str = strtok_r(next, " ", &next))) {
		char* value = NULL;
		char* key = strtok_r(str, "=", &value);

		if(!strcmp(key, "present_mask")) {
			int ret = parse_present_mask(value);
			if(ret)
				goto error;
		} else if(!strcmp(key, "memmap")) {
			int ret = smap_update_memmap(value);
			if(ret)
				goto error;
			smap_fixed = true;
		} else if(!strcmp(key, "mem")){
			int ret = smap_update_mem(value);
			if(ret)
				goto error;
			smap_fixed = true;
		}
	}

	if(smap_fixed) {
		printf("\tSystem Memory Map Updated\n");
		smap_dump();
	}

	printf("\tCPU Usage: %d - %d\n", cpu_start, cpu_end);

	return 0;

error:
	printf("\tFailed to parse arguments\n");

	return -2;
}

static void help() {
	puts("Usage: pn -i KERNEL_IMAGE_PATH -a KERNEL_ARG_PATH -s KERNEL_START_ADDR");
	puts("       pn --image KERNEL_IMAGE_PATH --args KERNEL_ARG_PATH --startaddr KERNEL_START_ADDR");
}

long kernel_start_address;	// Kernel Start address
char kernel_elf[256];
char kernel_args[256];

int param_parse(int argc, char** argv) {
	static struct option options[] = {
		{ "image", required_argument, 0, 'i' },
		{ "args", required_argument, 0, 'a' },
		{ "startaddr", required_argument, 0, 's' },
		{ 0, 0, 0, 0 }
	};

	int opt;
	int index = 0;
	while((opt = getopt_long(argc, argv, "i:a:s:", 
					options, &index)) != -1) {
		switch(opt) {
			case 'i':
				strncpy(kernel_elf, optarg, 256);
				break;
			case 'a':
				strncpy(kernel_args, optarg, 256);
				break;
			case 's':
				;
				char* next;
				kernel_start_address = strtol(optarg, &next, 10);
				if(*next == 'G' || *next == 'g') {
					kernel_start_address *= 1024 * 1024 * 1024;
				} else if(*next == 'M' || *next == 'm') {
					kernel_start_address *= 1024 * 1024;
				} else if(*next == 'K' || *next == 'k') {
					kernel_start_address *= 1024;
				} else {
					printf("\tFailed to parse kernel start address : %s\n", optarg);
					return -1;
				}
				break;
			default:
				help();
				exit(-1);
		}
	}
	if(kernel_start_address == 0 || *kernel_elf == '\0' || *kernel_args == '\0') {
		help();
		exit(-1);
	}

	printf("\tPacketNgin Image:\t%s\n", kernel_elf);
	printf("\tArgument File:\t\t%s\n", kernel_args);
	printf("\tKernel Start address:\t0x%p\n", kernel_start_address);

	int ret = parse_args(kernel_args);
	if(ret) {
		printf("\tFailed to parse kernel arguments : %d\n", ret);
		return ret;
	}

	return 0;
}
