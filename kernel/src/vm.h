#ifndef __VM_H__
#define __VM_H__

#include <sys/types.h>
#include <control/vmspec.h>

//#include "vfio.h"
#include "mp.h"
#include "vnic.h"

#define EVENT_VM_STARTED	0x0200000000000001
#define EVENT_VM_PAUSED	    	0x0200000000000002
#define EVENT_VM_RESUMED	0x0200000000000003
#define EVENT_VM_STOPPED	0x0200000000000004

#define VM_MEMORY_SIZE_ALIGN	0x200000
#define VM_STORAGE_SIZE_ALIGN	0x200000
#define VNIC_POOL_SIZE_ALIGN	0x200000

#define MAX_VM_COUNT		128

typedef struct {
	uint32_t	count;
	void**		blocks;	// gmalloc(array), bmalloc(content)
} Block;

#define VM_MAX_VM_COUNT	128
#define VM_MIN_MEMORY_SIZE	0x800000		//8Mb
#define VM_MAX_MEMORY_SIZE	0x8000000		//128Mb
#define VM_MIN_STORAGE_SIZE	0x200000		//2Mb
#define VM_MAX_STORAGE_SIZE	0x8000000		//128Mb
#define VM_MAX_NIC_COUNT	NIC_MAX_COUNT
#define VNIC_MAX_POOL_SIZE	0x8000000		//128Mb

#define NIC_DEFAULT_NICDEV		"eth0"
#define NIC_DEFAULT_POOL_SIZE		0x200000	// 4Mb
#define NIC_DEFAULT_BUDGET_SIZE		32
#define NIC_DEFAULT_PADDING_SIZE	32
#define NIC_DEFAULT_BUFFER_SIZE		1024
#define NIC_DEFAULT_BANDWIDTH		1000000000	// 1Gbps

typedef enum _VMError {
	EVMID = 1,
	ESTATUS,
	EALLOCMEM,
	EALLOCTHREAD,
	EOVERMAX,
	EUNDERMIN,
	ENICDEV,
	EVNICMAC,
	EVNICINIT,
	EADDVM,
	ETHREADID,
} VMError;

/**
 * Virtual Machine
 */
typedef struct _VM {
	uint32_t	id;				///< VM identifier
	int		core_size;			///< Number of cores
	uint8_t		cores[MP_MAX_CORE_COUNT];	///< Set of core id
	Block		memory;				///< Total Memeory size
	Block		storage;			///< Total Block size
	uint64_t	used_size;			///< Application image size
	int		nic_count;			///< Number of NICs
	VNIC**		nics;				///< NICs (gmalloc)
	//	VFIO*		fio;
	int		argc;				///< Number of arguments
	char**		argv;				///< Arguments (gmalloc)

	VMStatus	status;				///< VM status
} VM;

/**
 * Initilaize Module
 */
int vm_init();

/**
 * Create VM
 *
 * @param vm_spec Properties of new VM
 *
 * @return id for success, 0 for failure
 */
uint32_t vm_create(VMSpec* vm_spec);

/**
 * Get VM Spec
 *
 * @param vm_spec for get properties of new VM
 *
 * @return true for success, false for failure
 */
bool vm_get_spec(VMSpec* vm_spec);

/**
 * Destroy VM
 *
 * @param vmid id
 *
 * @return true for success, false for failure
 */
bool vm_destroy(uint32_t vmid);

/**
 * Get a number of vm
 *
 * @return number of vm
 */
int vm_count();

/**
 * Check if the VM exists.
 *
 * @param vmid id
 *
 * @return true if there is a vm
 */
bool vm_contains(uint32_t vmid);

/**
 * Gets a list of all VM IDs
 *
 * @param vmids result array
 * @param size size of result array
 *
 * @return number of vm id
 */
int vm_list(uint32_t* vmids, int size);

typedef void(*VM_STATUS_CALLBACK)(bool, void*);

/**
 * Asynchronously Set the state of the VM
 *
 * @param vmid id
 * @param status new state of vm
 * @param callback status callback
 * @param context callback context
 */
bool vm_status_set(uint32_t vmid, int status, VM_STATUS_CALLBACK callback, void* context);

/**
 * Get the status of the VM
 *
 * @param vmid id
 *
 * @return vm's status
 */
VMStatus vm_status_get(uint32_t vmid);

/**
 * Get VM Processors
 *
 * @param vmid id
 * @param processor bit mask
 *
 * @return processor count
 */
int vm_processors(uint32_t vmid, uint16_t* processors);

ssize_t vm_storage_read(uint32_t vmid, void** buf, size_t offset, size_t size);
ssize_t vm_storage_write(uint32_t vmid, void* buf, size_t offset, size_t size);
ssize_t vm_storage_clear(uint32_t vmid);
bool vm_storage_md5(uint32_t vmid, uint32_t size, uint32_t digest[4]);
ssize_t vm_stdio(uint32_t vmid, int thread_id, int fd, const char* str, size_t size);
typedef void(*VM_STDIO_CALLBACK)(uint32_t vmid, int thread_id, int fd, char* buffer, volatile size_t* head, volatile size_t* tail, size_t size);
void vm_stdio_handler(VM_STDIO_CALLBACK callback);

#endif /* __VM_H__ */
