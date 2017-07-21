#include <stdio.h>
#include <unistd.h>
#include <syscall.h>
#include "mmap.h"
#include "page.h"
#include "shared.h"

#define __NR_multikernel_boot 326

extern int cpu_start;
extern int cpu_end;
int processor_count;

static void wakeup_ap(long kernel_start_address) {
	if(!cpu_end)
		return;

	Shared* shared = (Shared*)SHARED_ADDR;
	printf("\tBooting APs : %p\n", (void*)(uintptr_t)kernel_start_address);
	for(int cpu = cpu_start; cpu < cpu_end + 1; cpu++) {
		printf("\tWakeup %d\n", cpu);
		int apicid = syscall(__NR_multikernel_boot, cpu, kernel_start_address);
		if(apicid < 0) {
			continue;
		}
		sleep(1);
		shared->mp_processors[apicid] = cpu;
		processor_count++;
		printf("\t\tAPIC ID: %d CPU ID: %d\n", apicid, cpu);
	}
	printf("Done\n");
}

int amp_init(long kernel_start_address) {
	processor_count++;
	Shared* shared = (Shared*)SHARED_ADDR;
	shared->mp_processors[0] = 0;
	/**
	 * Multikernel AP not yet wake-up.
	 */
	wakeup_ap(kernel_start_address);

	return 0;
}

uint8_t amp_get_apic_id() {
	return 0;
}
