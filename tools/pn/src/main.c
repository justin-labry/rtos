#include "stdio.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
//#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <linux/limits.h>

#include <util/event.h>
#include <timer.h>
#include "mmap.h"

#include "version.h"
#include "apic.h"
#include "icc.h"
#include "mapping.h"
#include "shared.h"
#include "driver/console.h"
#include "gmalloc.h"
#include "vm.h"
#include "manager.h"
#include "shell.h"
#include "malloc.h"
#include "vnic.h"
#include "amp.h"
#include "cpu.h"
#include "gdt.h"
#include "idt.h"
#include "smap.h"
#include "stdio.h"
#include "elf.h"
#include "device.h"
#include "popcorn.h"
#include "netob.h"
#include "dispatcher.h"
#include "driver/nicdev.h"
#include "param.h"
#include "symbols.h"
#include "io_mux.h"
#include "shmem.h"

#include "ver.h"

static int _timer_init(char* cpu_brand) {
	uint64_t _frequency;
	if(strstr(cpu_brand, "Intel") != NULL && strstr(cpu_brand, "@ ") != NULL) {
		int number = 0;
		int is_dot_found = 0;
		int dot = 0;

		const char* ch = strstr(cpu_brand, "@ ");
		ch += 2;
		while((*ch >= '0' && *ch <= '9') || *ch == '.') {
			if(*ch == '.') {
				is_dot_found = 1;
			} else {
				number *= 10;
				number += *ch - '0';

				dot += is_dot_found;
			}

			ch++;
		}

		uint64_t frequency = 0;
		if(strncmp(ch, "THz", 3) == 0) {
		} else if(strncmp(ch, "GHz", 3) == 0) {
			frequency = 1000000000L;
		} else if(strncmp(ch, "MHz", 3) == 0) {
			frequency = 1000000L;
		} else if(strncmp(ch, "KHz", 3) == 0) {
			frequency = 1000L;
		} else if(strncmp(ch, "Hz", 3) == 0) {
			frequency = 1L;
		}

		while(dot > 0) {
			frequency /= 10;
			dot--;
		}

		_frequency = frequency * number;
	} else {
		// Get timer frequency using bogomips (ref http://www.clifton.nl/bogo-faq.html)
		FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
		if(cpuinfo) {
			char buf[8192];
			while(!feof(cpuinfo)) {
				char* line = fgets(buf, sizeof(buf), cpuinfo);
				if(!line) break;

				// example: "bogomips	: 8015.88"
				if(strstr(line, "bogomips")) {
					while(!isdigit(*line)) line++;
					uint64_t bogomips = atof(line);
					bogomips *= 1000 * 1000 / 2;

					_frequency = bogomips;
					break;
				}
			}
			fclose(cpuinfo);
		} else {
			// Worst case: there is no way to determine timer frequency
			uint64_t time_tsc1 = timer_frequency();
			sleep(1);
			uint64_t time_tsc0 = timer_frequency();

			_frequency = time_tsc0 - time_tsc1;
		}
	}

	extern uint64_t TIMER_FREQUENCY_PER_SEC;
	extern uint64_t __timer_ms;
	extern uint64_t __timer_us;
	extern uint64_t __timer_ns;

	void* timer_symbols[4][2] = {
		{ &TIMER_FREQUENCY_PER_SEC, "TIMER_FREQUENCY_PER_SEC"},
		{ &__timer_ms, "__timer_ms"},
		{ &__timer_us, "__timer_us"},
		{ &__timer_ns, "__timer_ns"},
	};

	printf("\tFreqeuncy : %lx\n", _frequency);
	for(int i = 0; i < 4; i++) {
		unsigned long symbol_addr = elf_get_symbol(timer_symbols[i][1]);
		if(!symbol_addr) {
			printf("\tCan't Get Symbol Address: \"%s\"", timer_symbols[i][1]);
			return -1;
		}

		uint64_t *time_val = (uint64_t *)VIRTUAL_TO_PHYSICAL(symbol_addr);
		*time_val = _frequency;
		*(uint64_t*)timer_symbols[i][0] = _frequency;

		_frequency /= 1000;
	}

	return 0;
}

static int ensure_single_instance() {
	int lockfile = open("/tmp/pnd.lock", O_RDONLY | O_CREAT, 0444);
	if(lockfile == -1) return 1;

	return flock(lockfile, LOCK_EX | LOCK_NB);
}

int main(int argc, char** argv) {
	int ret;

	ret = ensure_single_instance();
	if(ret) goto error;

	//uint64_t vga_buffer = (uint64_t)VGA_BUFFER_START;
	console_init();
	char vga_buffer[64 * 1024];
	stdio_init(0, (void*)vga_buffer, 64 * 1024);

	printf("PacketNgin %s\n", VERSION);

	printf("Permission Check...\n");
	if(geteuid()) goto error;

	printf("\nIntializing System Memory Map\n");
	if(smap_init()) goto error;

	printf("\nParsing parameter...\n");
	if(param_parse(argc, argv)) goto error;

	printf("\nInitializing PacketNgin kernel module...\n");
	if(dispatcher_init()) goto error;

	printf("\nInitializing memory mapping...\n");
	if(mapping_memory()) goto error;

	printf("\nCopying kernel image...\n");
	if(elf_copy(kernel_elf, kernel_start_address)) goto error;

	printf("\nInitializing malloc area...\n");
	if(malloc_init()) goto error;

	printf("\nInitializing timer...\n");
	if(_timer_init(cpu_brand)) goto error;

	printf("\nInitializing shared memory area...\n");
	if(shared_init()) goto error;

	printf("\nWake-Up Multi processor...\n");
	if(amp_init(kernel_start_address)) goto error;

	mp_sync();	// Barrier #0
	/**
	 * Step 1. Kernel main
	 */
	printf("\nInitializing mulitiprocessing...\n");
	if(mp_init()) goto error;

	printf("\nInitializing cpu...\n");
	if(cpu_init()) goto error;

	printf("\nInitializing gmalloc area...\n");
	if(gmalloc_init()) goto error;

	mp_sync(); // Barrier #1

	printf("\nInitilizing GDT...\n");
	gdt_init();

	printf("\nInitilizing TSS...\n");
	tss_init();

	printf("\nInitilizing IDT...\n");
	idt_init();

	mp_sync(); // Barrier #2

	printf("\nInitailizing local APIC...\n");
	if(mapping_apic()) goto error;

	printf("\nInitializing events...\n");
	if(event_init() == false) goto error;

	printf("\nInitializing I/O Multiplexer...\n");
	if(io_mux_init() < 0) goto error;

	printf("\nInitializing inter-core communications...\n");
	if(icc_init()) goto error;

	printf("\nInitializing VM manager...\n");
	if(vm_init()) goto error;

	printf("\nInitializing linux network interface observer...\n");
	if(netob_init()) goto error;

	printf("\nInitializing RPC manager...\n");
	if(manager_init()) goto error;

	printf("\nInitializing shell...\n");
	if(shell_init()) goto error;

	printf("\nInitializing Version... \n");
	if(ver_init()) {
		printf("Can't initialize Version\n");
	}

	shmem_init();
	mp_sync(); // Barrier #3

	while(1) event_loop();

	return 0;

error:
	printf("\nPacketNgin initialization error occured.\n");
	printf("Terminated...\n");
	return -1;
}
