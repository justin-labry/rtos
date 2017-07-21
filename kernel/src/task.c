#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <util/list.h>
#include "apic.h"
#include "asm.h"
#include "malloc.h"
#include "gmalloc.h"
#include "vnic.h"
#include "page.h"

#include "task.h"

#define CTX_GS		0
#define CTX_FS		1
#define CTX_ES		2
#define CTX_DS		3
#define CTX_R15		4
#define CTX_R14		5
#define CTX_R13		6
#define CTX_R12		7
#define CTX_R11		8
#define CTX_R10		9
#define CTX_R9		10
#define CTX_R8		11
#define CTX_RSI		12
#define CTX_RDI		13
#define CTX_RDX		14
#define CTX_RCX		15
#define CTX_RBX		16
#define CTX_RAX		17
#define CTX_RBP		18
// Below is interupt return stack
#define CTX_RIP		19
#define CTX_CS		20
#define CTX_RFLAGS	21
#define CTX_RSP		22
#define CTX_SS		23

typedef struct {
	uint8_t		type;
	void*		data;
} Resource;

char* task_symbols[] = {
	"__nic_count",
	"__nics",
	"__dpis",
	"__app_status",
	"__malloc_pool",
	"__stdin",
	"__stdin_head",
	"__stdin_tail",
	"__stdin_size",
	"__stdout",
	"__stdout_head",
	"__stdout_tail",
	"__stdout_size",
	"__stderr",
	"__stderr_head",
	"__stderr_tail",
	"__stderr_size",
	"__gmalloc_pool",
	"__thread_id",
	"__thread_count",
	"__barrior_lock",
	"__barrior",
	"__shared",
	"__fio",
	"__timer_frequency",
	"__timer_ms",
	"__timer_us",
	"__timer_ns",
};

typedef struct {
	uint64_t	fpu[64];
	uint64_t	cpu[24];

	bool		is_fpu_inited;

	List*		mmap;
	List*		resources;
	uint64_t	symbols[SYM_END];
	uint64_t	stack;

	uint8_t		padding[0] __attribute__((aligned(16)));
} Task;

static Task tasks[2] __attribute__ ((aligned(16)));

void finit();
void fxsave(void* context);
void fxrstor(void* context);
void ts_set();
void ts_clear();

static uint32_t current_task;
static uint32_t last_fpu_task = (uint32_t)-1;


static void device_not_available_handler(uint64_t vector, uint64_t error_code) {
	ts_clear();

	if(last_fpu_task != (uint32_t)-1) {
		fxsave(tasks[last_fpu_task].fpu);
	}

	Task* task = &tasks[current_task];
	if(task->is_fpu_inited) {
		fxrstor(task->fpu);
	} else {
		finit();
		task->is_fpu_inited = true;
	}

	last_fpu_task = current_task;
}

void task_init() {
	ts_clear();
	finit();
	tasks[0].is_fpu_inited = true;

	apic_register(7, device_not_available_handler);
}

uint32_t task_create() {
	uint32_t id = 1;

	bzero(&tasks[id], sizeof(Task));

	tasks[id].cpu[CTX_CS] = 0x20 | 0x03;	// Code segment, PL=3
	tasks[id].cpu[CTX_DS] = 0x18 | 0x03;	// Data segment, PL=3
	tasks[id].cpu[CTX_ES] = 0x18 | 0x03;	// Data segment, PL=3
	tasks[id].cpu[CTX_FS] = 0x18 | 0x03;	// Data segment, PL=3
	tasks[id].cpu[CTX_GS] = 0x18 | 0x03;	// Data segment, PL=3
	tasks[id].cpu[CTX_SS] = 0x18 | 0x03;	// Data segment, PL=3

	tasks[id].cpu[CTX_RFLAGS] = 0x0200;	// Enable interrupt

	tasks[id].mmap = list_create(NULL);
	tasks[id].resources = list_create(NULL);

	return id;
}

void task_entry(uint32_t id, uint64_t entry) {
	tasks[id].cpu[CTX_RIP] = entry;
}

void task_stack(uint32_t id, uint64_t stack) {
	tasks[id].stack = stack;
	((uint64_t*)(void*)stack)[-1] = 0x00;
	tasks[id].cpu[CTX_RSP] = stack - 8;
 	tasks[id].cpu[CTX_RBP] = stack;
}

void task_arguments(uint32_t id, uint64_t argc, uint64_t argv) {
	tasks[id].cpu[CTX_RDI] = argc;
	tasks[id].cpu[CTX_RSI] = argv;
}

void task_symbol(uint32_t id, uint32_t symbol, uint64_t vaddr) {
	tasks[id].symbols[symbol] = vaddr;
}

void* task_addr(uint32_t id, uint32_t symbol) {
	return (void*)tasks[id].symbols[symbol];
}

extern uint64_t PHYSICAL_OFFSET;

void task_mmap(uint32_t id, uint64_t vaddr, uint64_t paddr, bool is_user, bool is_writable, bool is_executable, char* desc) {
	list_add(tasks[id].mmap, (void*)vaddr);

	uint64_t idx = vaddr >> 21;
	PAGE_L4U[idx].base = (paddr >> 21) + (PHYSICAL_OFFSET >> 21);
	PAGE_L4U[idx].us = !!is_user;
	PAGE_L4U[idx].rw = !!is_writable;
	PAGE_L4U[idx].exb = !is_executable;

	printf("Task: virtual memory map: %dMB -> %dMB %c%c%c %s\n", idx * 2, PAGE_L4U[idx].base * 2,
		PAGE_L4U[idx].us ? 'r' : '-',
		PAGE_L4U[idx].rw ? 'w' : '-',
		PAGE_L4U[idx].exb ? '-' : 'x',
		desc);
}

void task_refresh_mmap() {
	refresh_cr3();
}

void task_resource(uint32_t id, uint8_t type, void* data) {
	Resource* resource = malloc(sizeof(Resource));
	resource->type = type;
	resource->data = data;
	list_add(tasks[id].resources, resource);

	switch(type) {
		case RESOURCE_NI:
			;
			bool is_first = true;
			VNIC* vnic = (VNIC*)data;

			int count = vnic->nic_size / 0x200000;
			uint64_t vaddr = (uint64_t)vnic->nic;

			while(count--) {
				uint64_t idx = vaddr >> 21;
				PAGE_L4U[idx].base = idx + (PHYSICAL_OFFSET >> 21);
				PAGE_L4U[idx].us = 1;
				PAGE_L4U[idx].rw = 1;
				PAGE_L4U[idx].exb = 1;

				if(is_first) {
					printf("Task: virtual memory map : %dMB -> %dMB %c%c%c %s[%02x:%02x:%02x:%02x:%02x:%02x]\n",
						idx * 2, PAGE_L4U[idx].base * 2,
						PAGE_L4U[idx].us ? 'r' : '-',
						PAGE_L4U[idx].rw ? 'w' : '-',
						PAGE_L4U[idx].exb ? '-' : 'x',
						"VNIC",
						(vnic->mac >> 40) & 0xff,
						(vnic->mac >> 32) & 0xff,
						(vnic->mac >> 24) & 0xff,
						(vnic->mac >> 16) & 0xff,
						(vnic->mac >> 8) & 0xff,
						(vnic->mac >> 0) & 0xff);

					is_first = false;
				} else {
					printf("Task: virtual memory map : %dMB -> %dMB %c%c%c %s\n",
						idx * 2, PAGE_L4U[idx].base * 2,
						PAGE_L4U[idx].us ? 'r' : '-',
						PAGE_L4U[idx].rw ? 'w' : '-',
						PAGE_L4U[idx].exb ? '-' : 'x',
						"VNIC");
				}

				vaddr += 0x200000;
			}
			task_refresh_mmap();
			break;
	}
}

void task_destroy(uint32_t id) {
	//printf("task destroy %p\n", tasks[id].mmap);
	// Restore memory map
	if(tasks[id].mmap) {
		//printf("task destroy %d\n", list_size(tasks[id].mmap));
		while(list_size(tasks[id].mmap) > 0) {
			uint64_t vaddr = (uint64_t)list_remove_first(tasks[id].mmap);
			uint64_t idx = vaddr >> 21;

			//bfree((void*)((uint64_t)PAGE_L4U[idx].base << 21));

			PAGE_L4U[idx].base = idx + (PHYSICAL_OFFSET >> 21);
			PAGE_L4U[idx].us = 0;
			PAGE_L4U[idx].rw = 1;
			PAGE_L4U[idx].exb = 1;

// 			printf("Task: virtual memory map: %dMB -> %dMB %c%c%c\n", idx * 2, PAGE_L4U[idx].base * 2,
// 					PAGE_L4U[idx].us ? 'r' : '-',
// 					PAGE_L4U[idx].rw ? 'w' : '-',
// 					PAGE_L4U[idx].exb ? '-' : 'x');
		}

		list_destroy(tasks[id].mmap);
		tasks[id].mmap = NULL;
	}

	// Restore resource
/*
 *        while(list_size(tasks[id].resources) > 0) {
 *                Resource* resource = list_remove_first(tasks[id].resources);
 *                ListIterator iter;
 *
 *                switch(resource->type) {
 *                        case RESOURCE_NI:
 *                                list_iterator_init(&iter, ((VNIC*)resource->data)->pools);
 *                                while(list_iterator_has_next(&iter)) {
 *                                        uint64_t vaddr = (uint64_t)list_iterator_next(&iter);
 *                                        uint64_t idx = vaddr >> 21;
 *
 *                                        PAGE_L4U[idx].base = idx + (PHYSICAL_OFFSET >> 21);;
 *                                        PAGE_L4U[idx].us = 0;
 *                                        PAGE_L4U[idx].rw = 1;
 *                                        PAGE_L4U[idx].exb = 0;
 *
 *                                        printf("Task: virtual memory map: %dMB -> %dMB %c%c%c %s\n", idx * 2, PAGE_L4U[idx].base * 2,
 *                                                PAGE_L4U[idx].us ? 'r' : '-',
 *                                                PAGE_L4U[idx].rw ? 'w' : '-',
 *                                                PAGE_L4U[idx].exb ? '-' : 'x',
 *                                                "VNIC");
 *                                }
 *                                break;
 *                }
 *                free(resource);
 *        }
 *
 */
	if(tasks[id].resources) {
		list_destroy(tasks[id].resources);
		tasks[id].resources = NULL;
	}

	refresh_cr3();

	if(id == last_fpu_task)
		last_fpu_task = (uint32_t)-1;

// 	printf("%d %d\n", id, current_task);
// 	if(id == current_task) {
// 		current_task = (uint32_t)-1;
// 
// 		task_switch(0);
// 	}
}

inline uint32_t task_id() {
	return current_task;
}

void task_switch(uint32_t id) {
	//printf("task_switch\n");
	if(id == (uint32_t)-1) {
		id = 0;
	}

	uint32_t old_task = current_task;
	current_task = id;

/*
 *        printf("Task %d to %d\n", old_task, current_task);
 *        printf("Last FPU %d\n", last_fpu_task);
 *
 *        printf("Task Old \n");
 *        task_dump(old_task);
 *        printf("Task New \n");
 *        task_dump(current_task);
 *
 */
	//printf("current: %d old: %d\n", current_task, old_task);
	if(current_task == last_fpu_task) {
		ts_clear();
	} else {
		ts_set();
	}

	void _context_switch(void* prev, void* next);
	_context_switch(old_task == (uint32_t)-1 ? NULL : tasks[old_task].cpu, tasks[current_task].cpu);
}

bool task_is_active(uint32_t id) {
	return tasks[id].mmap != NULL;
}

uint64_t task_get_stack(uint32_t id) {
	return tasks[id].stack;
}

void task_dump(uint32_t id) {
	#define CPU(i) tasks[id].cpu[i]

	printf("gs: %08x\tfs: %08x, es: %08x, ds: %08x\n", CPU(0), CPU(1), CPU(2), CPU(3));
	printf("r15: %08x\tr14: %08x, r13: %08x, r12: %08x\n", CPU(4), CPU(5), CPU(6), CPU(7));
	printf("r11: %08x\tr10: %08x, r9: %08x, r8: %08x\n", CPU(8), CPU(9), CPU(10), CPU(11));
	printf("rsi: %08x\trdi: %08x, rdx: %08x, rcx: %08x\n", CPU(12), CPU(13), CPU(14), CPU(15));
	printf("rbx: %08x\trax: %08x, rbp: %08x, rip: %08x\n", CPU(16), CPU(17), CPU(18), CPU(19));
	printf("cs: %08x\trflags: %08x, rsp: %08x, ss: %08x\n", CPU(20), CPU(21), CPU(22), CPU(23));
}
