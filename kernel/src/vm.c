#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <malloc.h>
#include <util/event.h>
#include <errno.h>
#include <util/map.h>
#include <util/ring.h>
#include <util/cmd.h>
#include <net/md5.h>
#include <net/interface.h>
#include <timer.h>
#include <fcntl.h>
#include "file.h"
#include <vnic.h>
#include "icc.h"
#include "vm.h"
#include "apic.h"
#include "icc.h"
#include "gmalloc.h"
#include "stdio.h"
#include "shared.h"
#include "mmap.h"
#include "driver/nicdev.h"
#include "shell.h"
#include "page.h"

static uint32_t	last_vmid = 1;
// FIXME: change to static
Map*	vms;

// Core status
typedef struct {
	VMStatus		status;		// VM_STATUS_XXX
	int			error_code;
	int			return_code;

	VM*			vm;

	char*			stdin;
	volatile size_t*	stdin_head;
	volatile size_t*	stdin_tail;
	size_t			stdin_size;

	char*			stdout;
	volatile size_t*	stdout_head;
	volatile size_t*	stdout_tail;
	size_t			stdout_size;

	char*			stderr;
	volatile size_t*	stderr_head;
	volatile size_t*	stderr_tail;
	size_t			stderr_size;
} Core;

static Core cores[MP_MAX_CORE_COUNT];

static VM_STDIO_CALLBACK stdio_callback;

static int cmd_md5(int argc, char** argv, void(*callback)(char* result, int exit_status));
static int cmd_create(int argc, char** argv, void(*callback)(char* result, int exit_status));
static int cmd_vm_destroy(int argc, char** argv, void(*callback)(char* result, int exit_status));
static int cmd_vm_list(int argc, char** argv, void(*callback)(char* result, int exit_status));
static int cmd_upload(int argc, char** argv, void(*callback)(char* result, int exit_status));
static int cmd_status_set(int argc, char** argv, void(*callback)(char* result, int exit_status));
static int cmd_status_get(int argc, char** argv, void(*callback)(char* result, int exit_status));
static int cmd_stdio(int argc, char** argv, void(*callback)(char* result, int exit_status));
static int cmd_vnic(int argc, char** argv, void(*callback)(char* result, int exit_status));
static int cmd_interface(int argc, char** argv, void(*callback)(char* result, int exit_status));
static Command commands[] = {
	{
		.name = "create",
		.desc = "Create VM",
		.args = "[-c core_count:u8] [-m memory_size:u32] [-s storage_size:u32] "
			"[-n [mac:u64],[dev:str],[ibuf:u32],[obuf:u32],[iband:u64],[oband:u64],[hpad:u16],[tpad:u16],[pool:u32] ] "
			"[-a args:str] -> vmid ",
		.func = cmd_create
	},
	{
		.name = "destroy",
		.desc = "Destroy VM",
		.args = "vmid:u32 -> bool",
		.func = cmd_vm_destroy
	},
	{
		.name = "list",
		.desc = "Print a list of VM IDs",
		.args = " -> u64[]",
		.func = cmd_vm_list
	},
	{
		.name = "upload",
		.desc = "Upload file",
		.args = "vmid:u32 path:str -> bool",
		.func = cmd_upload
	},
	{
		.name = "md5",
		.desc = "MD5 storage",
		.args = "vmid:u32 [size:u64] -> str",
		.func = cmd_md5
	},
	{
		.name = "start",
		.desc = "Start VM",
		.args = "vmid:u32 -> bool",
		.func = cmd_status_set
	},
	{
		.name = "pause",
		.desc = "Pause VM",
		.args = "vmid:u32 -> bool",
		.func = cmd_status_set
	},
	{
		.name = "resume",
		.desc = "Resume VM",
		.args = "vmid:u32 -> bool",
		.func = cmd_status_set
	},
	{
		.name = "stop",
		.desc = "Stop VM",
		.args = "vmid:u32 -> bool",
		.func = cmd_status_set
	},
	{
		.name = "status",
		.desc = "Get VM's status",
		.args = "vmid:u32 -> str{start|pause|stop|invalid}",
		.func = cmd_status_get
	},
	{
		.name = "stdin",
		.desc = "Write stdin to vm",
		.args = "vmid:u32 thread_id:u8 msg:str -> bool",
		.func = cmd_stdio
	},
	{
		.name = "vnic",
		.desc = "List of virtual network interface",
		.func = cmd_vnic
	},
	{
		.name = "interface",
		.desc = "List of virtual network interface",
		.func = cmd_interface
	},
};

static void icc_started(ICC_Message* msg) {
	Core* core = &cores[msg->apic_id];
	VM* vm = core->vm;

	if(msg->result == 0) {
		core->error_code = 0;

		core->stdin = (char*)((uint64_t)msg->data.started.stdin - PHYSICAL_OFFSET);
		core->stdin_head = (size_t*)((uint64_t)msg->data.started.stdin_head - PHYSICAL_OFFSET);
		core->stdin_tail = (size_t*)((uint64_t)msg->data.started.stdin_tail - PHYSICAL_OFFSET);
		core->stdin_size = msg->data.started.stdin_size;

		core->stdout = (char*)((uint64_t)msg->data.started.stdout - PHYSICAL_OFFSET);
		core->stdout_head = (size_t*)((uint64_t)msg->data.started.stdout_head - PHYSICAL_OFFSET);
		core->stdout_tail = (size_t*)((uint64_t)msg->data.started.stdout_tail - PHYSICAL_OFFSET);
		core->stdout_size = msg->data.started.stdout_size;

		core->stderr = (char*)((uint64_t)msg->data.started.stderr - PHYSICAL_OFFSET);
		core->stderr_head = (size_t*)((uint64_t)msg->data.started.stderr_head - PHYSICAL_OFFSET);
		core->stderr_tail = (size_t*)((uint64_t)msg->data.started.stderr_tail - PHYSICAL_OFFSET);
		core->stderr_size = msg->data.started.stderr_size;

		core->status = VM_STATUS_START;

		printf("Execution succeed on core[%d].\n", mp_apic_id_to_processor_id(msg->apic_id));

	} else {
		core->error_code = msg->result;

		core->status = VM_STATUS_PAUSE;

		printf("Execution FAILED on core[%d]: Error code 0x%x.\n", mp_apic_id_to_processor_id(msg->apic_id), msg->result);

	}

	icc_free(msg);

	int error_code = 0;
	for(int i = 0; i < vm->core_size; i++) {
		core = &cores[vm->cores[i]];

		if(core->status == VM_STATUS_START) {
			continue;
		}

		if(core->error_code != 0) {
			error_code = core->error_code;
			continue;
		}

		return;
	}

	vm->status = error_code == 0 ? VM_STATUS_START : VM_STATUS_STOP;

	event_trigger_fire(EVENT_VM_STARTED, vm, NULL, NULL);

	if(error_code != 0) {
		for(int i = 0; i < vm->core_size; i++) {
			if(cores[vm->cores[i]].status == VM_STATUS_START) {
				ICC_Message* msg2 = icc_alloc(ICC_TYPE_STOP);
				icc_send(msg2, vm->cores[i]);
			}
		}
	}

	if(error_code == 0) {
		printf("VM started on cores[");
		for(int i = 0; i < vm->core_size; i++) {
			printf("%d", mp_apic_id_to_processor_id(vm->cores[i]));
			if(i + 1 < vm->core_size) {
				printf(", ");
			}
		}
		printf("]\n");
	} else {
		printf("VM started with error(s) on cores[");
		for(int i = 0; i < vm->core_size; i++) {
			printf("%d", mp_apic_id_to_processor_id(vm->cores[i]));
			error_code = cores[vm->cores[i]].error_code;
			if(error_code != 0)
				printf("(0x%x)", error_code);

			if(i + 1 < vm->core_size) {
				printf(", ");
			}
		}
		printf("]\n");
	}
}

static void icc_paused(ICC_Message* msg) {
	VM* vm = cores[msg->apic_id].vm;

	cores[msg->apic_id].status = VM_STATUS_PAUSE;

	printf("Execution paused on core[%d].\n", mp_apic_id_to_processor_id(msg->apic_id));

	icc_free(msg);
	for(int i = 0; i < vm->core_size; i++) {
		if(cores[vm->cores[i]].status != VM_STATUS_PAUSE)
			return;
	}

	vm->status = VM_STATUS_PAUSE;

	event_trigger_fire(EVENT_VM_PAUSED, vm, NULL, NULL);

	printf("VM paused on cores[");
	for(int i = 0; i < vm->core_size; i++) {
		printf("%d", mp_apic_id_to_processor_id(vm->cores[i]));
		if(i + 1 < vm->core_size) {
			printf(", ");
		}
	}
	printf("]\n");
}

static void icc_resumed(ICC_Message* msg) {
	if(msg->result == -1000) {	// VM is not strated yet
		ICC_Message* msg2 = icc_alloc(ICC_TYPE_RESUME);
		icc_send(msg2, msg->apic_id);
		icc_free(msg);
		return;
	}

	VM* vm = cores[msg->apic_id].vm;

	cores[msg->apic_id].status = VM_STATUS_START;

	printf("Execution resumed on core[%d].\n", mp_apic_id_to_processor_id(msg->apic_id));

	icc_free(msg);

	for(int i = 0; i < vm->core_size; i++) {
		if(cores[vm->cores[i]].status != VM_STATUS_START)
			return;
	}

	vm->status = VM_STATUS_START;

	event_trigger_fire(EVENT_VM_RESUMED, vm, NULL, NULL);

	printf("VM resumed on cores[");
	for(int i = 0; i < vm->core_size; i++) {
		printf("%d", mp_apic_id_to_processor_id(vm->cores[i]));
		if(i + 1 < vm->core_size) {
			printf(", ");
		}
	}
	printf("]\n");
}

static void icc_stopped(ICC_Message* msg) {
	if(msg->result == -1000) {	// VM is not strated yet
		ICC_Message* msg2 = icc_alloc(ICC_TYPE_STOP);
		icc_send(msg2, msg->apic_id);
		icc_free(msg);
		// Resend stop icc
		return;
	}

	VM* vm = cores[msg->apic_id].vm;

	cores[msg->apic_id].status = VM_STATUS_PAUSE;
	cores[msg->apic_id].error_code = msg->result;
	cores[msg->apic_id].return_code = msg->data.stopped.return_code;
	cores[msg->apic_id].stdin = NULL;
	cores[msg->apic_id].stdout = NULL;
	cores[msg->apic_id].stderr = NULL;

	printf("Execution completed on core[%d].\n", mp_apic_id_to_processor_id(msg->apic_id));

	icc_free(msg);

	for(int i = 0; i < vm->core_size; i++) {
		if(cores[vm->cores[i]].status != VM_STATUS_PAUSE) {
			return;
		}
	}

	vm->status = VM_STATUS_STOP;

	event_trigger_fire(EVENT_VM_STOPPED, vm, NULL, NULL);

	printf("VM stopped on cores[");
	for(int i = 0; i < vm->core_size; i++) {
		printf("%d(%d/%d)", mp_apic_id_to_processor_id(vm->cores[i]), cores[vm->cores[i]].error_code, cores[vm->cores[i]].return_code);
		if(i + 1 < vm->core_size) {
			printf(", ");
		}
	}
	printf("]\n");
}

static bool vm_delete(VM* vm, int core) {
	bool is_destroy = true;

	if(core == -1) {
		for(int i = 0; i < vm->core_size; i++) {
			if(vm->cores[i]) {
				cores[vm->cores[i]].status = VM_STATUS_STOP;
			}
		}
	} else {
		for(int i = 0; i < vm->core_size; i++) {
			if(vm->cores[i] == core) {
				cores[vm->cores[i]].status = VM_STATUS_STOP;
			}

			if(!cores[vm->cores[i]].status == VM_STATUS_STOP)
				is_destroy = false;
		}
	}

	if(is_destroy) {
		if(vm->memory.blocks) {
			for(uint32_t i = 0; i < vm->memory.count; i++) {
				if(vm->memory.blocks[i]) {
					bfree(vm->memory.blocks[i]);
				}
			}

			gfree(vm->memory.blocks);
		}

		if(vm->storage.blocks) {
			for(uint32_t i = 0; i < vm->storage.count; i++) {
				if(vm->storage.blocks[i]) {
					bfree(vm->storage.blocks[i]);
				}
			}

			gfree(vm->storage.blocks);
		}

		if(vm->nics) {
			#ifdef PACKETNGIN_SINGLE
			int dispatcher_destroy_vnic(void* vnic) { return 0; }
			#else
			extern int dispatcher_destroy_vnic(void* vnic);
			#endif
			for(int i = 0; i < vm->nic_count; i++) {
				if(vm->nics[i]) {
					NICDevice* nic_dev = nicdev_get(vm->nics[i]->parent);
					nicdev_unregister_vnic(nic_dev, vm->nics[i]->id);
					dispatcher_destroy_vnic(vm->nics[i]);
					bfree(vm->nics[i]->nic);
					vnic_free_id(vm->nics[i]->id);
					gfree(vm->nics[i]);
				}
			}

			gfree(vm->nics);
		}

		if(vm->argv) {
			gfree(vm->argv);
		}

		gfree(vm);
	}

	return is_destroy;
}

static void stdio_dump(int coreno, int fd, char* buffer, volatile size_t* head, volatile size_t* tail, size_t size) {
	if(*head == *tail)
		return;

	char header[] = "[ Core 00 ]";
	header[7] += coreno / 10;
	header[8] += coreno % 10;

	printf("\n%s ", header);
	int length = *tail - *head;
	for(int i = 0; i < length; i++) {
		if(buffer[i] == '\0') {
			if(i != length - 1)
				printf("%s ", header);
		}
		putchar(buffer[i]);
	}

	*head = *tail;
}

static bool vm_loop(void* context) {
	// Standard I/O/E processing
	int get_thread_id(VM* vm, int core) {
		for(int i = 0; i < vm->core_size; i++) {
			if(vm->cores[i] == core)
				return i;
		}

		return -1;
	}

	for(int i = 1; i < MP_MAX_CORE_COUNT; i++) {
		Core* core = &cores[i];
		if(core->status != VM_STATUS_PAUSE && core->status != VM_STATUS_START) continue;
		int thread_id = get_thread_id(core->vm, i);
		if(thread_id == -1)
			continue;

		if(core->stdout != NULL && *core->stdout_head != *core->stdout_tail) {
			stdio_callback(core->vm->id, thread_id, 1, core->stdout, core->stdout_head, core->stdout_tail, core->stdout_size);
		}

		if(core->stderr != NULL && *core->stderr_head != *core->stderr_tail) {
			stdio_callback(core->vm->id, thread_id, 2, core->stderr, core->stderr_head, core->stderr_tail, core->stderr_size);
		}
	}

	for(int i = 1; i < MP_MAX_CORE_COUNT; i++) {
		if(cores[i].status == VM_STATUS_INVALID)
			continue;

		char* buffer = (char*)MP_CORE(VIRTUAL_TO_PHYSICAL(__stdout_ptr), i);
		volatile size_t* head = (size_t*)MP_CORE(VIRTUAL_TO_PHYSICAL(__stdout_head_ptr), i);
		volatile size_t* tail = (size_t*)MP_CORE(VIRTUAL_TO_PHYSICAL(__stdout_tail_ptr), i);
		size_t size = *(size_t*)MP_CORE(VIRTUAL_TO_PHYSICAL(__stdout_size_ptr), i);

		while(*head != *tail) {
			stdio_dump(mp_apic_id_to_processor_id(i), 1, buffer, head, tail, size);
		}
	}

	return true;
}

int vm_init() {
	vms = map_create(4, map_uint64_hash, map_uint64_equals, NULL);
	if(!vms) return -1;

	icc_register(ICC_TYPE_STARTED, icc_started);
	icc_register(ICC_TYPE_PAUSED, icc_paused);
	icc_register(ICC_TYPE_RESUMED, icc_resumed);
	icc_register(ICC_TYPE_STOPPED, icc_stopped);

	// Core 0 is occupied by RPC manager
	cores[0].status = VM_STATUS_START;

	uint8_t* core_map = mp_processor_map();
	for(int i = 1; i < MP_MAX_CORE_COUNT; i++) {
		if(core_map[i] == MP_CORE_INVALID)
			cores[i].status = VM_STATUS_INVALID;	// Disable the core
	}

	event_idle_add(vm_loop, NULL);

	cmd_register(commands, sizeof(commands) / sizeof(commands[0]));

	return 0;
}

static VM* vm_get(uint32_t vmid) {
	return map_get(vms, (void*)(uint64_t)vmid);
}

static bool vm_put(VM* vm) {
	return map_put(vms, (void*)(uint64_t)vm->id, vm);
}

static VM* vm_remove(uint32_t vmid) {
	return map_remove(vms, (void*)(uint64_t)vmid);
}

uint32_t vm_create(VMSpec* vm_spec) {
	VM* vm = gmalloc(sizeof(VM));
	if(!vm) {
		errno = EALLOCMEM;
		return 0;
	}
	memset(vm, 0, sizeof(VM));

	while(true) {
		uint32_t vmid = last_vmid++;

		if(vmid != 0 && !map_contains(vms, (void*)(uint64_t)vmid)) {
			vm->id = vmid;
			break;
		}
	}

	// Allocate args
	if(vm_spec->argc) {
		vm->argc = vm_spec->argc;
		int argv_len = sizeof(char*) * vm_spec->argc;
		for(int i = 0; i < vm_spec->argc; i++) {
			argv_len += strlen(vm_spec->argv[i]) + 1;
		}

		vm->argv = gmalloc(argv_len);
		if(!vm->argv) {
			errno = EALLOCMEM;
			goto fail;
		}
		memset(vm->argv, 0, argv_len);

		char* args = (void*)vm->argv + sizeof(char*) * vm_spec->argc;
		for(int i = 0; i < vm_spec->argc; i++) {
			vm->argv[i] = args;
			int len = strlen(vm_spec->argv[i]) + 1;
			memcpy(vm->argv[i], vm_spec->argv[i], len);
			args += len;
		}
	}

	// Allocate core
	vm->core_size = vm_spec->core_size;
	if(!vm->core_size) {
		errno = EUNDERMIN;
		goto fail;
	}

	int j = 0;
	for(int i = 1; i < MP_MAX_CORE_COUNT; i++) {
		if(cores[i].status == VM_STATUS_STOP) {
			vm->cores[j++] = i;
			cores[i].status = VM_STATUS_PAUSE;
			cores[i].vm = vm;

			if(j >= vm->core_size)
				break;
		}
	}

	if(j < vm->core_size) {
		errno = EALLOCTHREAD;
		goto fail;
	}

	// Allocate memory
	uint32_t memory_size = memory_size = (vm_spec->memory_size + (VM_MEMORY_SIZE_ALIGN - 1)) & ~(VM_MEMORY_SIZE_ALIGN - 1);
	if(memory_size > VM_MAX_MEMORY_SIZE) {
		errno = EOVERMAX;
		goto fail;
	}

	if(memory_size < VM_MIN_MEMORY_SIZE) {
		errno = EUNDERMIN;
		goto fail;
	}

	vm->memory.count = memory_size / VM_MEMORY_SIZE_ALIGN;
	vm->memory.blocks = gmalloc(vm->memory.count * sizeof(void*));
	memset(vm->memory.blocks, 0x0, vm->memory.count * sizeof(void*));
	for(uint32_t i = 0; i < vm->memory.count; i++) {
		vm->memory.blocks[i] = bmalloc(1);
		if(!vm->memory.blocks[i]) {
			errno = EALLOCMEM;
			goto fail;
		}
	}

	// Allocate storage
	uint32_t storage_size = (vm_spec->storage_size + (VM_STORAGE_SIZE_ALIGN - 1)) & ~(VM_STORAGE_SIZE_ALIGN - 1);
	if(storage_size > VM_MAX_STORAGE_SIZE) {
		errno = EOVERMAX;
		goto fail;
	}

	if(storage_size < VM_MIN_STORAGE_SIZE) {
		errno = EUNDERMIN;
		goto fail;
	}

	vm->storage.count = storage_size / VM_STORAGE_SIZE_ALIGN;
	vm->storage.blocks = gmalloc(vm->storage.count * sizeof(void*));
	memset(vm->storage.blocks, 0x0, vm->storage.count * sizeof(void*));
	for(uint32_t i = 0; i < vm->storage.count; i++) {
		vm->storage.blocks[i] = bmalloc(1);
		if(!vm->storage.blocks[i]) {
			errno = EALLOCMEM;
			goto fail;
		}
	}

	// Allocate VNICs
	NICSpec* nics = vm_spec->nics;
	vm->nic_count = vm_spec->nic_count;
	if(vm->nic_count > VM_MAX_NIC_COUNT) {
		errno = EOVERMAX;
		goto fail;
	}

	// VNIC Create
	if(vm->nic_count) {
		vm->nics = gmalloc(sizeof(VNIC) * vm->nic_count);
		if(!vm->nics) {
			errno = EALLOCMEM;
			goto fail;
		}

		memset(vm->nics, 0, sizeof(VNIC) * vm->nic_count);
		for(int i = 0; i < vm->nic_count; i++) {
			NICDevice* nic_dev = strlen(nics[i].parent) ? nicdev_get(nics[i].parent) : nicdev_get_by_idx(0);
			if(!nic_dev) {
				errno = ENICDEV;
				goto fail;
			}

			uint64_t mac = nics[i].mac;
			if(mac & ~0xffffffffffffL) {
				errno = EVNICMAC;
				goto fail;
			}

			if(mac == 0) {
				do {
					mac = timer_frequency() & 0x0fffffffffffL;
					mac |= 0x03L << 40;	// Locally administrered & Unicast
					mac ^= 0x01L << 40;	// Locally administrered
				} while(nicdev_get_vnic_mac(nic_dev, mac) != NULL);
			} else if(nicdev_get_vnic_mac(nic_dev, mac) != NULL) {
				errno = EVNICMAC;
				goto fail;
			}

			if(!nics[i].pool_size) {
				errno = EALLOCMEM;
				goto fail;
			}

			nics[i].pool_size = nics[i].pool_size ? (nics[i].pool_size + VNIC_POOL_SIZE_ALIGN - 1) & ~(VNIC_POOL_SIZE_ALIGN - 1) : NIC_DEFAULT_POOL_SIZE;
			if(nics[i].pool_size > VNIC_MAX_POOL_SIZE) {
				errno = EOVERMAX;
				goto fail;
			}

			uint64_t attrs[] = {
				VNIC_MAC, mac,
				VNIC_DEV, (uint64_t)nic_dev->name,
				VNIC_BUDGET, nics[i].budget,
				VNIC_POOL_SIZE, nics[i].pool_size,
				VNIC_RX_BANDWIDTH, nics[i].rx_bandwidth,
				VNIC_TX_BANDWIDTH, nics[i].tx_bandwidth,
				VNIC_PADDING_HEAD, nics[i].padding_head,
				VNIC_PADDING_TAIL, nics[i].padding_tail,
				VNIC_RX_QUEUE_SIZE, nics[i].rx_buffer_size,
				VNIC_TX_QUEUE_SIZE, nics[i].tx_buffer_size,
				VNIC_NONE
			};

			VNIC* vnic = gmalloc(sizeof(VNIC));
			if(!vnic) {
				errno = EALLOCMEM;
				goto fail;
			}

			vnic->id = vnic_alloc_id();
			char name_buf[32];
			sprintf(name_buf, "v%deth%d", vm->id, i);
			strncpy(vnic->name, name_buf, _IFNAMSIZ);
			vnic->nic_size = nics[i].pool_size;
			vnic->nic = bmalloc(nics[i].pool_size / 0x200000);
			if(!vnic->nic) {
				errno = EALLOCMEM;
				goto fail;
			}

			if(!vnic_init(vnic, attrs)) {
				errno = EVNICINIT;
				goto fail;
			}

			#ifdef PACKETNGIN_SINGLE
			int dispatcher_create_vnic(void* vnic) { return 0; }
			#else
			extern int dispatcher_create_vnic(void* vnic);
			#endif
			if(dispatcher_create_vnic(vnic) < 0) {
				errno = EVNICINIT;
				goto fail;
			}

			vm->nics[i] = vnic;

			nicdev_register_vnic(nic_dev, vnic);
		}
	}

	if(!vm_put(vm)) {
		errno = EADDVM;
		goto fail;
	}

	//Copy to VMSpec
	vm_spec->id = vm->id;
	vm_get_spec(vm_spec);

	return vm->id;

fail:
	vm_delete(vm, -1);
	last_vmid--;

	return 0;
}

bool vm_destroy(uint32_t vmid) {
	VM* vm = vm_get(vmid);
	if(!vm) {
		errno = EVMID;
		return false;
	}
	if(vm->status != VM_STATUS_STOP) {
		errno = ESTATUS;
		return false;
	}
	for(int i = 0; i < vm->core_size; i++) {
		if(cores[vm->cores[i]].status == VM_STATUS_START) {
			errno = ESTATUS;
			return false;
		}
	}

	printf("Manager: Delete vm[%d] on cores [", vmid);
	for(int i = 0; i < vm->core_size; i++) {
		printf("%d", mp_apic_id_to_processor_id(vm->cores[i]));
		if(i + 1 < vm->core_size)
			printf(", ");
	}
	printf("]\n");

	vm_remove(vmid);
	vm_delete(vm, -1);

	return true;
}

bool vm_get_spec(VMSpec* vm_spec) {
	VM* vm = vm_get(vm_spec->id);
	if(!vm) {
		errno = EVMID;
		return false;
	}

	vm_spec->id = vm->id;
	vm_spec->core_size = vm->core_size;
	vm_spec->memory_size = vm->memory.count * VM_MEMORY_SIZE_ALIGN;
	vm_spec->storage_size = vm->storage.count * VM_STORAGE_SIZE_ALIGN;
	
	vm_spec->nic_count = vm->nic_count;
	for(int i = 0; i < vm_spec->nic_count; i++) {
		VNIC* vnic = vm->nics[i];
		NICSpec* nicspec = &vm_spec->nics[i];
		strncpy(nicspec->name, vnic->name, MAX_NIC_NAME_LEN);
		nicspec->mac = vnic->mac;
		strcpy(nicspec->parent, vnic->parent);
		nicspec->budget = vnic->budget;
		nicspec->rx_buffer_size = vnic->rx.size;
		nicspec->tx_buffer_size = vnic->tx.size;
		nicspec->padding_head = vnic->padding_head;
		nicspec->padding_tail = vnic->padding_tail;
		nicspec->rx_bandwidth = vnic->rx_bandwidth;
		nicspec->tx_bandwidth = vnic->tx_bandwidth;
		nicspec->pool_size = vnic->nic_size;
	}

	//TODO: Add arguments
	return true;
}

bool vm_contains(uint32_t vmid) {
	return map_contains(vms, (void*)(uint64_t)vmid);
}

int vm_count() {
	return map_size(vms);
}

int vm_list(uint32_t* vmids, int size) {
	int i = 0;

	MapIterator iter;
	map_iterator_init(&iter, vms);
	while(i < size && map_iterator_has_next(&iter)) {
		MapEntry* entry = map_iterator_next(&iter);
		vmids[i++] = (uint32_t)(uint64_t)entry->key;
	}

	return i;
}

int vm_processors(uint32_t vmid, uint16_t* processors) {
	VM* vm = vm_get(vmid);
	if(!vm) {
		errno = EVMID;
		return -1;
	}

	*processors = 0;
	for(int i = 0; i < vm->core_size; i++) {
		*processors |= 1 << vm->cores[i];
	}

	return vm->core_size;
}

typedef struct {
	VM_STATUS_CALLBACK	callback;
	void*			context;
	int			status;
} CallbackInfo;

static bool status_changed(uint64_t event_type, void* event, void* context) {
	VM* vm = event;
	CallbackInfo* info = context;

	bool result = vm->status == info->status || (vm->status == VM_STATUS_START && info->status == VM_STATUS_RESUME);
	info->callback(result, info->context);

	char* op = "";
	switch(info->status) {
		case VM_STATUS_START: op = "start"; break;
		case VM_STATUS_PAUSE: op = "pause"; break;
		case VM_STATUS_RESUME: op = "resume"; break;
		case VM_STATUS_STOP: op = "stop"; break;
	}

	printf("Manager: VM[%d] %s %s on cores [", vm->id, op, result ? "succeed" : "failed");
	for(int i = 0; i < vm->core_size; i++) {
		printf("%d", mp_apic_id_to_processor_id(vm->cores[i]));
		if(i + 1 < vm->core_size)
			printf(", ");
	}
	printf("]\n");

	free(info);

	return false;
}

bool vm_status_set(uint32_t vmid, int status, VM_STATUS_CALLBACK callback, void* context) {
	VM* vm = vm_get(vmid);
	if(!vm) {
		errno = EVMID;
		goto failure;
	}

	int icc_type = 0;
	uint64_t event_type = 0;
	switch(status) {
		case VM_STATUS_START:
			if(vm->status != VM_STATUS_STOP) {
				errno = ESTATUS;
				goto failure;
			}
			event_type = EVENT_VM_STARTED;
			icc_type = ICC_TYPE_START;
			break;
		case VM_STATUS_PAUSE:
			if(vm->status != VM_STATUS_START) {
				errno = ESTATUS;
				goto failure;
			}
			event_type = EVENT_VM_PAUSED;
			icc_type = ICC_TYPE_PAUSE;
			break;
		case VM_STATUS_RESUME:
			if(vm->status != VM_STATUS_PAUSE) {
				errno = ESTATUS;
				goto failure;
			}
			event_type = EVENT_VM_RESUMED;
			icc_type = ICC_TYPE_RESUME;
			break;
		case VM_STATUS_STOP:
			if(vm->status != VM_STATUS_PAUSE && vm->status != VM_STATUS_START) {
				errno = ESTATUS;
				goto failure;
			}
			event_type = EVENT_VM_STOPPED;
			icc_type = ICC_TYPE_STOP;
			break;
	}

	// Lazy clean up
	if(status == VM_STATUS_START) {
		for(size_t i = 0; i < vm->memory.count; i++)
			memset(vm->memory.blocks[i], 0, VM_MEMORY_SIZE_ALIGN);
	}

	CallbackInfo* info = calloc(1, sizeof(CallbackInfo));
	if(!info) {
		errno = EALLOCMEM;
		goto failure;
	}
	info->callback = callback;
	info->context = context;
	info->status = status;

	event_trigger_add(event_type, status_changed, info);

	for(int i = 0; i < vm->core_size; i++) {
		if(status == VM_STATUS_PAUSE) {
			apic_write64(APIC_REG_ICR, ((uint64_t)vm->cores[i] << 56) |
						APIC_DSH_NONE |
						APIC_TM_EDGE |
						APIC_LV_DEASSERT |
						APIC_DM_PHYSICAL |
						APIC_DMODE_FIXED |
						49);
		} else {
			cores[vm->cores[i]].error_code = 0;
			ICC_Message* msg = icc_alloc(icc_type);
			if(status == VM_STATUS_START) {
				msg->data.start.vm = vm;
			}
			icc_send(msg, vm->cores[i]);
		}
	}

	return true;

failure:
	if(callback)
		callback(false, context);
	return false;
}

VMStatus vm_status_get(uint32_t vmid) {
	VM* vm = vm_get(vmid);
	if(!vm) {
		errno = EVMID;
		return -1;
	}

	return vm->status;
}

ssize_t vm_storage_read(uint32_t vmid, void** buf, size_t offset, size_t size) {
	VM* vm = vm_get(vmid);
	if(!vm) {
		errno = EVMID;
		return -1;
	}

	if(offset > vm->storage.count * VM_STORAGE_SIZE_ALIGN) {
		*buf = NULL;
		errno = EOVERMAX;
		return 0;
	}

	int index = offset / VM_STORAGE_SIZE_ALIGN;
	offset %= VM_STORAGE_SIZE_ALIGN;
	*buf = vm->storage.blocks[index] + offset;

	if(offset + size > VM_STORAGE_SIZE_ALIGN)
		return VM_STORAGE_SIZE_ALIGN - offset;
	else
		return size;
}

ssize_t vm_storage_write(uint32_t vmid, void* buf, size_t offset, size_t size) {
	VM* vm = vm_get(vmid);
	if(!vm) {
		errno = EVMID;
		return -1;
	}

	if(vm->status != VM_STATUS_STOP) {
		errno = ESTATUS;
		return -1;
	}

	if(!size) return 0;

	//Checking buffer size
	if((uint64_t)offset + size > (uint64_t)vm->storage.count * VM_STORAGE_SIZE_ALIGN) {
		errno = EOVERMAX;
		return -1;
	}

	size_t _size = size;
	uint32_t index = offset / VM_STORAGE_SIZE_ALIGN;
	size_t _offset = offset % VM_STORAGE_SIZE_ALIGN;
	for(; index < vm->storage.count && _size; index++) {
		if(_offset + _size > VM_STORAGE_SIZE_ALIGN) {
			size_t write_size = VM_STORAGE_SIZE_ALIGN - _offset;
			memcpy(vm->storage.blocks[index] + _offset, buf, write_size);
			_size -= write_size;
			buf += write_size;
		} else {
			memcpy(vm->storage.blocks[index] + _offset, buf, _size);
			_size = 0;
		}

		_offset = 0;
	}

	vm->used_size = offset + size;

	return size;
}

ssize_t vm_storage_clear(uint32_t vmid) {
	VM* vm = vm_get(vmid);
	if(!vm) {
		errno = EVMID;
		return -1;
	}

	ssize_t size = 0;
	for(uint32_t i = 0; i < vm->storage.count; i++) {
		memset(vm->storage.blocks[i], 0x0, VM_STORAGE_SIZE_ALIGN);
		size += VM_STORAGE_SIZE_ALIGN;
	}

	return size;
}

bool vm_storage_md5(uint32_t vmid, uint32_t size, uint32_t digest[4]) {
	VM* vm = vm_get(vmid);
	if(!vm) {
		errno = EVMID;
		return false;
	}

	if(size == 0) size = vm->used_size;

	uint32_t block_count = size / VM_MEMORY_SIZE_ALIGN;
	if(vm->storage.count < block_count) {
		errno = EOVERMAX;
		return false;
	}

	md5_blocks(vm->storage.blocks, vm->storage.count, VM_STORAGE_SIZE_ALIGN, size, digest);

	return true;
}

ssize_t vm_stdio(uint32_t vmid, int thread_id, int fd, const char* str, size_t size) {
	VM* vm = vm_get(vmid);
	if(!vm) {
		errno = EVMID;
		return -1;
	}

	if(thread_id < 0 || thread_id >= vm->core_size) {
		errno = ETHREADID;
		return -1;
	}

	Core* core = &cores[vm->cores[thread_id]];
	if(vm->status != VM_STATUS_START) {
		errno = ESTATUS;
		return -1;
	}

	switch(fd) {
		case 0:
			if(core->stdin) {
				size_t write_size = ring_write(core->stdin, *core->stdin_head, core->stdin_tail, core->stdin_size, str, size);
				if(write_size < size) errno = EOVERMAX;

				return write_size;
			} else {
				errno = EOVERMAX;
				return -1;
			}
		case 1:
			if(core->stdout) {
				size_t write_size = ring_write(core->stdout, *core->stdout_head, core->stdout_tail, core->stdout_size, str, size);
				if(write_size < size) errno = EOVERMAX;

				return write_size;
			} else {
				errno = EOVERMAX;
				return -1;
			}
		case 2:
			if(core->stderr) {
				size_t write_size = ring_write(core->stderr, *core->stderr_head, core->stderr_tail, core->stderr_size, str, size);
				if(write_size < size) errno = EOVERMAX;

				return write_size;
			} else {
				errno = EOVERMAX;
				return -1;
			}
		default:
			errno = EOVERMAX;
			return -1;
	}
}

void vm_stdio_handler(VM_STDIO_CALLBACK callback) {
	stdio_callback = callback;
}

void print_vm_error(const char* msg) {
	switch(errno) {
		case EVMID: 
			printf("VM Error: The VM ID is invalid");
			break;
		case ESTATUS:
			printf("VM Error: The VM status is invalid");
			break;
		case EALLOCMEM:
			printf("VM Error: Memory allocation failure");
			break;
		case EALLOCTHREAD:
			printf("VM Error: Thread allocation failure");
			break;
		case EOVERMAX:
			printf("VM Error: Beyond the maximum limit");
			break;
		case EUNDERMIN:
			printf("VM Error: Does not meet minimum requirements");
			break;
		case ENICDEV:
			printf("VM Error: Can't found NIC device");
			break;
		case EVNICMAC:
			printf("VM Error: MAC address is invalid");
			break;
		case EVNICINIT:
			printf("VM Error: VNIC initialization failed");
			break;
		case EADDVM:
			printf("VM Error: Failed to add VM");
			break;
		case ETHREADID:
			printf("VM Error: Thread ID is wrong");
			break;
	}

	if(msg) printf(": %s\n", msg);
	else printf("\n");
}
////

static void print_nic_spec(NICSpec* nic_spec, char* indent) {
	printf("%s%s:\n", indent ? : "", nic_spec->name);
	printf("%s    Parent: %s\n", indent ? : "", nic_spec->parent);
	printf("%s    Mac: ", indent? : "");
	for(int j = 5; j >= 0; j--) {
		printf("%02lx", (nic_spec->mac >> (j * 8)) & 0xff);
		if(j - 1 >= 0)
			printf(":");
		else
			printf(" ");
	}
	printf("\n");
	printf("%s    RXBandwidth: %ldMbps\n", indent ? : "", nic_spec->rx_bandwidth / 1000000);
	printf("%s    Rxsize: %ld\n", indent ? : "", nic_spec->rx_buffer_size);
	printf("%s    TXBandwidth: %ldMbps\n", indent ? : "", nic_spec->tx_bandwidth / 1000000);
	printf("%s    TxSize: %ld\n", indent ? : "", nic_spec->tx_buffer_size);
	printf("%s    HeaderPadding: %ld\n", indent ? : "", nic_spec->padding_head);
	printf("%s    TailPadding: %ld\n", indent ? : "",  nic_spec->padding_tail);
	printf("%s    PoolSize: %ldMbs\n", indent ? : "",  nic_spec->pool_size / (1024 * 1024));
	// 	printf("%12sRX packets:%d dropped:%d\n", "", vnic->input_packets, vnic->input_drop_packets);
	// 	printf("%12sTX packets:%d dropped:%d\n", "", vnic->output_packets, vnic->output_drop_packets);
	// 
	// 	printf("%12sRX bytes:%lu ", "", vnic->input_bytes);
	// 	print_byte_size(vnic->input_bytes);
	// 	printf("  TX bytes:%lu ", vnic->output_bytes);
	// 	print_byte_size(vnic->output_bytes);
	// 	printf("\n");
	// 	printf("%12sHead Padding:%d Tail Padding:%d\n", "",vnic->padding_head, vnic->padding_tail);
}

static void print_vm_spec(VMSpec* vm_spec) {
	printf("VM[%d]:\n", vm_spec->id);
	printf("    Processors: ", "");
	uint16_t processors;
	int count = vm_processors(vm_spec->id, &processors);
	printf("[");
	for(int i = 0; i < 16 && count; i++) {
		if(processors & 1) {
			printf("%d", mp_apic_id_to_processor_id(i));
			if(--count)
				printf(", ");
		}
		processors >>= 1;
	}
	printf("]\n");

	printf("    Memory: %dMbs\n", vm_spec->memory_size  / 0x100000);
	printf("    Storage: %dMbs\n", vm_spec->storage_size  / 0x100000);

	if(vm_spec->nic_count) {
		printf("    NICS:\n");
		for(int i = 0; i < vm_spec->nic_count; i++) {
			NICSpec* nic_spec = &vm_spec->nics[i];
			print_nic_spec(nic_spec, "        ");
		}
	}

	if(vm_spec->argc) {
		printf("    Args: [");
		for(int i = 0; i < vm_spec->argc; i++) {
			printf("%s", vm_spec->argv[i]);

			if(i + 1 < vm_spec->argc)
				printf(", ");
		}
		printf("]\n");
	}
}

static int cmd_md5(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	if(argc != 3 && argc !=2) return CMD_WRONG_NUMBER_OF_ARGS;

	if(!is_uint32(argv[1])) return CMD_WRONG_TYPE_OF_ARGS;

	uint32_t vmid = parse_uint32(argv[1]);

	if(argc == 3 && !is_uint64(argv[2])) return CMD_WRONG_TYPE_OF_ARGS;

	uint64_t size = argc == 3 ? parse_uint64(argv[2]) : 0;
	uint32_t md5sum[4];

	bool ret = vm_storage_md5(vmid, size, md5sum);
	if(!ret) {
		print_vm_error("");
		return CMD_ERROR;
	} else {
		char* p = (char*)cmd_result;
		for(int i = 0; i < 16; i++, p += 2) {
			sprintf(p, "%02x", ((uint8_t*)md5sum)[i]);
		}
		*p = '\0';
	}
	printf("%s\n", cmd_result);

	if(ret) callback(cmd_result, 0);

	return CMD_SUCCESS;
}

static int cmd_create(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	VMSpec vm = {0};
	vm.core_size = 1;
	vm.memory_size = 0x1000000;		/* 16MB */
	vm.storage_size = 0x1000000;		/* 16MB */

	NICSpec nics[VM_MAX_NIC_COUNT] = {0};
	vm.nics = nics;

	for(int i = 1; i < argc; i++) {
		if(strcmp(argv[i], "-c") == 0) {
			NEXT_ARGUMENTS();

			if(!is_uint8(argv[i])) return CMD_WRONG_TYPE_OF_ARGS;
			vm.core_size = parse_uint8(argv[i]);
		} else if(strcmp(argv[i], "-m") == 0) {
			NEXT_ARGUMENTS();

			if(!is_uint32(argv[i])) return CMD_WRONG_TYPE_OF_ARGS;
			vm.memory_size = parse_uint32(argv[i]);
		} else if(strcmp(argv[i], "-s") == 0) {
			NEXT_ARGUMENTS();

			if(!is_uint32(argv[i])) return CMD_WRONG_TYPE_OF_ARGS;
			vm.storage_size = parse_uint32(argv[i]);
		} else if(strcmp(argv[i], "-n") == 0) {
			NICSpec* nic = &(vm.nics[vm.nic_count++]);

			nic->mac = 0; //Random
			nic->budget = NIC_DEFAULT_BUDGET_SIZE;
			nic->rx_buffer_size = NIC_DEFAULT_BUFFER_SIZE;
			nic->tx_buffer_size = NIC_DEFAULT_BUFFER_SIZE;
			nic->rx_bandwidth = NIC_DEFAULT_BANDWIDTH;
			nic->tx_bandwidth = NIC_DEFAULT_BANDWIDTH;
			nic->padding_head = NIC_DEFAULT_PADDING_SIZE;
			nic->padding_tail = NIC_DEFAULT_PADDING_SIZE;
			nic->pool_size = NIC_DEFAULT_POOL_SIZE;

			NEXT_ARGUMENTS();
			char* next;
			char* token = strtok_r(argv[i], ",", &next);
			while(token) {
				char* value;
				token = strtok_r(token, "=", &value);
				if(!strcmp(token, "mac")) {
					if(!is_uint64(value)) return CMD_WRONG_TYPE_OF_ARGS;
					nic->mac = strtoull(value, NULL, 16);
				} else if(!strcmp(token, "dev")) {
					strncpy(nic->parent, value, MAX_NIC_NAME_LEN - 1);
				} else if(!strcmp(token, "budget")) {
					if(!is_uint16(value)) return CMD_WRONG_TYPE_OF_ARGS;
					nic->budget = parse_uint16(value);
				} else if(!strcmp(token, "ibuf")) {
					if(!is_uint32(value)) return CMD_WRONG_TYPE_OF_ARGS;
					nic->rx_buffer_size = parse_uint32(value);
				} else if(!strcmp(token, "obuf")) {
					if(!is_uint32(value)) return CMD_WRONG_TYPE_OF_ARGS;
					nic->tx_buffer_size = parse_uint32(value);
				} else if(!strcmp(token, "iband")) {
					if(!is_uint64(value)) return CMD_WRONG_TYPE_OF_ARGS;
					nic->rx_bandwidth = parse_uint64(value);
				} else if(!strcmp(token, "oband")) {
					if(!is_uint64(value)) return CMD_WRONG_TYPE_OF_ARGS;
					nic->tx_bandwidth = parse_uint64(value);
				} else if(!strcmp(token, "hpad")) {
					if(!is_uint16(value)) return CMD_WRONG_TYPE_OF_ARGS;
					nic->padding_head = parse_uint16(value);
				} else if(!strcmp(token, "tpad")) {
					if(!is_uint16(value)) return CMD_WRONG_TYPE_OF_ARGS;
					nic->padding_tail = parse_uint16(value);
				} else if(!strcmp(token, "pool")) {
					if(!is_uint32(value)) return CMD_WRONG_TYPE_OF_ARGS;
					nic->pool_size = parse_uint32(value);
				} else {
					i--;
					break;
				}

				token = strtok_r(next, ",", &next);
			}
		} else if(strcmp(argv[i], "-a") == 0) {
			NEXT_ARGUMENTS();

			vm.argv[vm.argc++] = argv[i];
		} else return CMD_WRONG_TYPE_OF_ARGS;
	}

	uint32_t vmid = vm_create(&vm);
	if(vmid == 0) {
		print_vm_error("");
		return CMD_ERROR;
	} else
		print_vm_spec(&vm);

	free(vm.argv);
	return CMD_SUCCESS;
}

static int cmd_vm_destroy(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	if(argc != 2) return CMD_WRONG_NUMBER_OF_ARGS;
	if(!is_uint32(argv[1])) return CMD_WRONG_TYPE_OF_ARGS;
	uint32_t vmid = parse_uint32(argv[1]);

	bool ret = vm_destroy(vmid);

	if(ret) {
		printf("Vm destroy success\n");
		return CMD_SUCCESS;
	} else {
		printf("Vm destroy fail\n");
		print_vm_error("");
		return CMD_ERROR;
	}
}

static int cmd_vm_list(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	uint32_t vmids[VM_MAX_VM_COUNT];
	int len = vm_list(vmids, VM_MAX_VM_COUNT);

	char* p = cmd_result;

	if(len <= 0) {
		printf("Empty\n");
		return CMD_SUCCESS;
	}

	for(int i = 0; i < len; i++) {
		p += sprintf(p, "%lu", vmids[i]) - 1;
		if(i + 1 < len) {
			*p++ = ' ';
		} else {
			*p++ = '\0';
		}
	}

	printf("%s\n", cmd_result);
	callback(cmd_result, 0);
	return CMD_SUCCESS;
}

static int cmd_upload(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	if(argc != 3) return CMD_WRONG_NUMBER_OF_ARGS;

	if(!is_uint32(argv[1])) return CMD_WRONG_TYPE_OF_ARGS;

	uint32_t vmid = parse_uint32(argv[1]);
	struct stat file_stat;
	stat(argv[2], &file_stat); 
	if(S_ISDIR(file_stat.st_mode)) {
		printf("Error: %s is not regular file\n");
		return CMD_ERROR;
	}

	int fd = open(argv[2], O_RDONLY);
	if(fd < 0) {
		printf("Cannot open file: %s\n", argv[2]);
		return CMD_ERROR;
	}

	int offset = 0;
	int len;
	char buf[4096];
	while((len = read(fd, buf, 4096)) > 0) {
		if(vm_storage_write(vmid, buf, offset, len) != len) {
			print_vm_error("");
			close(fd);
			return CMD_ERROR;
		}

		offset += len;
	}

	printf("Upload success : %s to %d\n", argv[2], vmid);
	close(fd);

	return CMD_SUCCESS;
}

static void status_setted(bool result, void* context) {
	void (*callback)(char* result, int exit_status) = context;
	callback(result ? "true" : "false", 0);
}

static int cmd_status_set(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	if(argc !=2) return CMD_WRONG_NUMBER_OF_ARGS;

	if(!is_uint32(argv[1])) return CMD_WRONG_TYPE_OF_ARGS;
	uint32_t vmid = parse_uint32(argv[1]);

	int status = 0;
	if(strcmp(argv[0], "start") == 0) {
		printf("Start VM...\n");
		status = VM_STATUS_START;
	} else if(strcmp(argv[0], "pause") == 0) {
		printf("Pause VM...\n");
		status = VM_STATUS_PAUSE;
	} else if(strcmp(argv[0], "resume") == 0) {
		printf("Resume VM...\n");
		status = VM_STATUS_RESUME;
	} else if(strcmp(argv[0], "stop") == 0) {
		printf("Stop VM...\n");
		status = VM_STATUS_STOP;
	} else return CMD_WRONG_TYPE_OF_ARGS;

	if(!vm_status_set(vmid, status, status_setted, callback)) {
		print_vm_error("");
		return CMD_ERROR;
	}

	return CMD_SUCCESS;
}

static int cmd_status_get(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	void print_vm_status(int status) {
		switch(status) {
			case VM_STATUS_START:
				printf("start\n");
				callback("start", 0);
				break;
			case VM_STATUS_PAUSE:
				printf("pause\n");
				callback("pause", 0);
				break;
			case VM_STATUS_STOP:
				printf("stop\n");
				callback("stop", 0);
				break;
			default:
				printf("invalid\n");
				callback("invalid", -1);
				break;
		}
	}

	if(argc != 2) return CMD_WRONG_NUMBER_OF_ARGS;
	if(!is_uint32(argv[1])) return CMD_WRONG_TYPE_OF_ARGS;

	uint32_t vmid = parse_uint32(argv[1]);

	VMSpec vm_spec = {0};
	NICSpec nics[VM_MAX_NIC_COUNT] = {0};
	vm_spec.nics = nics;
	vm_spec.id = vmid;

	if(!vm_get_spec(&vm_spec)) {
		print_vm_error("");
		return CMD_ERROR;
	}
	print_vm_spec(&vm_spec);

	VMStatus status = vm_status_get(vmid);
	printf("Status: ");
	print_vm_status(status);

	return CMD_SUCCESS;
}

static int cmd_stdio(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	if(argc < 3) return CMD_WRONG_NUMBER_OF_ARGS;

	if(!is_uint32(argv[1]) || !is_uint8(argv[2])) return CMD_WRONG_TYPE_OF_ARGS;

	uint32_t vmid = parse_uint32(argv[1]);
	uint8_t thread_id = parse_uint8(argv[2]);
	for(int i = 3; i < argc; i++) {
		ssize_t len = vm_stdio(vmid, thread_id, 0, argv[i], strlen(argv[i]) + 1);
		if(len <= 0) {
			print_vm_error("");
			return CMD_ERROR;
		}
	}

	return CMD_SUCCESS;
}

static void print_byte_size(uint64_t byte_size) {
	uint64_t size = 1;
	for(int i = 0; i < 5; i++) {
		if((byte_size / size) < 1000) {
			printf("(%.1f ", (float)byte_size / size);
			switch(i) {
				case 0:
					printf("B)");
					break;
				case 1:
					printf("KB)");
					break;
				case 2:
					printf("MB)");
					break;
				case 3:
					printf("GB)");
					break;
				case 4:
					printf("TB)");
					break;
			}
			return;
		}

		size *= 1000;
	}
}

static void print_interface(VNIC* vnic) {
	IPv4InterfaceTable* table = interface_table_get(vnic->nic);
	if(!table)
		return;

	int offset = 1;
	for(int interface_index = 0; interface_index < IPV4_INTERFACE_MAX_COUNT; interface_index++, offset <<= 1) {
		IPv4Interface* interface = NULL;
		if(table->bitmap & offset)
			 interface = &table->interfaces[interface_index];
		else if(interface_index)
			continue;

		printf("%12s:%d", vnic->name, interface_index);
		printf("HWaddr %02x:%02x:%02x:%02x:%02x:%02x  ",
				(vnic->mac >> 40) & 0xff,
				(vnic->mac >> 32) & 0xff,
				(vnic->mac >> 24) & 0xff,
				(vnic->mac >> 16) & 0xff,
				(vnic->mac >> 8) & 0xff,
				(vnic->mac >> 0) & 0xff);

		printf("Parent %s\n", vnic->parent);
		printf("\n");

		if(interface) {
			uint32_t ip = interface->address;
			printf("%12sinet addr:%d.%d.%d.%d  ", "", (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, (ip >> 0) & 0xff);
			uint32_t mask = interface->netmask;
			printf("\tMask:%d.%d.%d.%d\n", (mask >> 24) & 0xff, (mask >> 16) & 0xff, (mask >> 8) & 0xff, (mask >> 0) & 0xff);
			//uint32_t gw = interface->gateway;
			//printf("%12sGateway:%d.%d.%d.%d\n", "", (gw >> 24) & 0xff, (gw >> 16) & 0xff, (gw >> 8) & 0xff, (gw >> 0) & 0xff);
		}

// 		if(interface_index == 0)
// 			print_nic_spec_metadata(vnic);
		printf("\n");
	}
}

static int cmd_vnic(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	uint32_t ids[16];
	int size = vm_list(ids, 16);

	bool is_empty = true;
	for(int i = 0 ; i < size; i++) {
		VMSpec vm_spec = {0};
		NICSpec nics[VM_MAX_NIC_COUNT] = {0};
		vm_spec.nics = nics;
		vm_spec.id = ids[i];

		if(!vm_get_spec(&vm_spec)) continue;

		for(int i = 0; i < vm_spec.nic_count; i++) {
			NICSpec* nic_spec = &nics[i];

			print_nic_spec(nic_spec, NULL);
			is_empty = false;
		}
		printf("\n");
	}
	if(is_empty) printf("Empty\n");

	return CMD_SUCCESS;
}

static bool parse_vnic_interface(char* name, uint16_t* vmid, uint16_t* vnic_index, uint16_t* interface_index) {
	if(strncmp(name, "v", 1)) return false;

	char* next;
	char* _vmid = strtok_r(name + 1, "eth", &next);
	if(!_vmid) return false;

	if(!is_uint8(_vmid)) return false;

	*vmid = parse_uint8(_vmid);

	char* _interface_index;
	char* _vnic_index = strtok_r(next, ":", &_interface_index);
	if(!_vnic_index) return false;

	if(!is_uint8(_vnic_index)) return false;

	*vnic_index = parse_uint8(_vnic_index);

	if(_interface_index) {
		if(!is_uint8(_interface_index)) return false;

		*interface_index = parse_uint8(_interface_index) + 1;
	} else *interface_index = 0;

	return true;
}

static bool parse_addr(char* argv, uint32_t* address) {
	char* next = NULL;
	uint32_t temp;
	temp = strtol(argv, &next, 0);
	if(temp > 0xff) return false;

	*address = (temp & 0xff) << 24;
	if(next == argv) return false;

	argv = next;
	if(*argv != '.') return false;

	argv++;
	temp = strtol(argv, &next, 0);
	if(temp > 0xff) return false;

	*address |= (temp & 0xff) << 16;
	if(next == argv) return false;

	argv = next;
	if(*argv != '.') return false;

	argv++;
	temp = strtol(argv, &next, 0);
	if(temp > 0xff) return false;

	*address |= (temp & 0xff) << 8;
	if(next == argv) return false;

	argv = next;
	if(*argv != '.') return false;

	argv++;
	temp = strtol(argv, &next, 0);
	if(temp > 0xff) return false;

	*address |= temp & 0xff;
	if(next == argv) return false;

	argv = next;
	if(*argv != '\0') return false;

	return true;
}

static int cmd_interface(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	if(argc == 1) {
		uint32_t ids[16];
		int size = vm_list(ids, 16);

		for(int i = 0 ; i < size; i++) {
			VMSpec vm_spec = {0};
			NICSpec nics[VM_MAX_NIC_COUNT] = {0};
			vm_spec.nics = nics;
			vm_spec.id = ids[i];

			if(!vm_get_spec(&vm_spec))
				continue;

			for(int i = 0; i < vm_spec.nic_count; i++) {
// 				NICSpec* nic_spec = &nics[i];
// 
// 				print_nic_spec(nic_spec);
// 				print_nic_spec_metadata(nic_spec);
			}
			printf("\n");
		}

		return 0;
	} else {
		uint16_t vmid;
		uint16_t vnic_index;
		uint16_t interface_index;
		// TODO use vnic's name
		if(!parse_vnic_interface(argv[1], &vmid, &vnic_index, &interface_index)) return -1;

		VM* vm = map_get(vms, (void*)(uint64_t)vmid);
		if(!vm) return -2;

		if(vnic_index > vm->nic_count) return -2;

		VNIC* vnic = vm->nics[vnic_index];

		if(!vnic) return -2;

		IPv4InterfaceTable* table = interface_table_get(vnic->nic);
		if(!table) return -3;

		IPv4Interface* interface = NULL;
		uint16_t offset = 1;
		for(int i = 0; i < interface_index; i++)
			offset <<= 1;

		table->bitmap |= offset;
		interface = &table->interfaces[interface_index];

		if(argc > 2) {
			uint32_t address;
			if(is_uint32(argv[2])) {
				address = parse_uint32(argv[2]);
			} else if(!parse_addr(argv[2], &address)) {
				printf("Address wrong\n");
				return 0;
			}
			if(address == 0) { //disable
				table->bitmap ^= offset;
				return 0;
			}

			interface->address = address;
		}
		if(argc > 3) {
			uint32_t netmask;
			if(is_uint32(argv[3])) {
				netmask = parse_uint32(argv[3]);
			} else if(!parse_addr(argv[3], &netmask)) {
				printf("Address wrong\n");
				return 0;
			}
			interface->netmask = netmask;
		}
		if(argc > 4) {
			uint32_t gateway;
			if(is_uint32(argv[4])) {
				gateway = parse_uint32(argv[4]);
			} else if(!parse_addr(argv[4], &gateway)) {
				printf("Address wrong\n");
				return 0;
			}
			interface->gateway = gateway;
		}
	}

	return 0;
}
