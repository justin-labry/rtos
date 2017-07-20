#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "bootparam.h"
#include "popcorn.h"

#ifndef PAGE_SIZE
  #define PAGE_SIZE 0x1000
#endif

#define FILE_MEM "/dev/mem"
#define SETUP_HDR_SIGN  "HdrS"
#define SETUP_HDR_MINVER 0x0202

#define __NR_get_boot_params_addr 327

static inline unsigned long get_boot_params_addr () {
	return syscall(__NR_get_boot_params_addr);
}

int open_mem(int flag) {
	return open(FILE_MEM, flag);
}

void close_mem(int fd) {
	close(fd);
}	

struct boot_params* map_boot_param(int mem_fd) {
	if(mem_fd == -1) {
		printf("Error: Memory not opend\n");
		return NULL;
	}

	unsigned long boot_params_phys_addr = get_boot_params_addr();
	if((boot_params_phys_addr == 0) || (unsigned long)boot_params_phys_addr == (unsigned long)~0) {
		printf("Error calling get_boot_params_addr syscall (%ld)\n",
				(unsigned long)boot_params_phys_addr);
		return false;
	}

	void* virt_addr = mmap(NULL, 2 * sizeof(struct boot_params),
			PROT_WRITE, MAP_SHARED,
			mem_fd, (unsigned long)boot_params_phys_addr & ~(PAGE_SIZE - 1)); //ALIGN boot_params_phys_page
	unsigned long boot_params_offset = (unsigned long)boot_params_phys_addr & (PAGE_SIZE -1);

	return virt_addr + boot_params_offset;
}

void unmap_boot_param(struct boot_params* boot_param) {
	unsigned long boot_params_phys_addr = get_boot_params_addr();
	if((boot_params_phys_addr == 0) || (unsigned long)boot_params_phys_addr == (unsigned long)~0) {
		printf("Error calling get_boot_params_addr syscall (%ld)\n",
				(unsigned long)boot_params_phys_addr);
		return;
	}

	unsigned long boot_params_offset = (unsigned long)boot_params_phys_addr & (PAGE_SIZE -1);

	munmap(boot_param - boot_params_offset, 2 * sizeof(struct boot_params));
}

static struct setup_header* get_setup_header(struct boot_params* boot_param) {
	struct setup_header* setup_header = &boot_param->hdr;
	if(memcmp(&setup_header->header, SETUP_HDR_SIGN, sizeof(__u32))) {
		printf("Error invalid setup_header signature field %.4s (%.4s)\n",
				(char*)&setup_header->header, SETUP_HDR_SIGN);
		return NULL;
	}
	if(setup_header->version < (__u16)SETUP_HDR_MINVER) {
		printf("Error setup_header version too old 0x%x (0x%x)\n",
				(unsigned int)setup_header->version, (unsigned int)SETUP_HDR_MINVER);
		return NULL;
	}

	return setup_header;
}	

int save_cmd_line(char* buf, int buf_len) {
	if(!buf || !buf_len)
		return 0;

	int mem_fd = open_mem(O_RDWR);
	struct boot_params* boot_params = map_boot_param(mem_fd);
	if(!boot_params) {
		close_mem(mem_fd);
		return 0;
	}

	struct setup_header* setup_header = get_setup_header(boot_params);
	if(!setup_header) {
		unmap_boot_param(boot_params);
		close_mem(mem_fd);
		return 0;
	}

	int cmd_line_size = setup_header->cmdline_size;
	if(cmd_line_size < buf_len) {
		unmap_boot_param(boot_params);
		close_mem(mem_fd);
		return 0;
	}

	unsigned long cmd_line_phys = setup_header->cmd_line_ptr;
	if(!cmd_line_phys) {
		unmap_boot_param(boot_params);
		close_mem(mem_fd);
		return 0;
	}

	unsigned long cmd_line_offset = cmd_line_phys & (PAGE_SIZE - 1);
	char* virt_page = mmap(NULL, cmd_line_size + cmd_line_offset,
			PROT_WRITE | PROT_READ, MAP_SHARED, mem_fd, cmd_line_phys & ~(PAGE_SIZE - 1));
	if(!virt_page) {
		unmap_boot_param(boot_params);
		close_mem(mem_fd);
		return 0;
	}

	char* virt_cmd_line = virt_page + cmd_line_offset;
	memcpy(virt_cmd_line, buf, buf_len);

	unmap_boot_param(boot_params);
	munmap(virt_page, cmd_line_size + cmd_line_offset);
	close_mem(mem_fd);

	return buf_len;
}

int load_cmd_line(char* buf, int buf_len) {
	if(!buf || !buf_len)
		return 0;

	int mem_fd = open_mem(O_RDONLY);
	struct boot_params* boot_params = map_boot_param(mem_fd);
	if(!boot_params) {
		close_mem(mem_fd);
		return 0;
	}

	struct setup_header* setup_header = get_setup_header(boot_params);
	if(!setup_header) {
		unmap_boot_param(boot_params);
		close_mem(mem_fd);
		return 0;
	}

	int cmd_line_size = setup_header->cmdline_size;
	if(cmd_line_size > buf_len) {
		unmap_boot_param(boot_params);
		close_mem(mem_fd);
		return 0;
	}

	unsigned long cmd_line_phys = setup_header->cmd_line_ptr;
	if(!cmd_line_phys) {
		unmap_boot_param(boot_params);
		close_mem(mem_fd);
		return 0;
	}

	unsigned long cmd_line_offset = cmd_line_phys & (PAGE_SIZE - 1);
	char* virt_page = mmap(NULL, cmd_line_size + cmd_line_offset,
			PROT_READ, MAP_SHARED, mem_fd, cmd_line_phys & ~(PAGE_SIZE - 1));
	if(!virt_page) {
		unmap_boot_param(boot_params);
		close_mem(mem_fd);
		return 0;
	}

	char* virt_cmd_line = virt_page + cmd_line_offset;
	sprintf(buf, "%s", virt_cmd_line);

	unmap_boot_param(boot_params);
	munmap(virt_page, cmd_line_size + cmd_line_offset);
	close_mem(mem_fd);

	return cmd_line_offset;
}	
