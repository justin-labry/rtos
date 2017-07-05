#include <tlsf.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <util/map.h>

#define PNDSHM_NAME 	"/pndshm"
#define PNDSHM_SIZE 	0x1000000 //16Mbyte
#define PNDSHM_OFFSET	0xff000000

typedef struct _PNDShareData {
	uint64_t    PHYSICAL_OFFSET;
	Map*        vms;
	ssize_t     pool_size;
	uint8_t     padding[0] __attribute__((__aligned__(4096)));
	uint8_t     pool[0];
} __attribute__ ((packed)) PNDShareData;

static PNDShareData* pnd_share_data;

int malloc_init() {
	int fd = shm_open(PNDSHM_NAME, O_CREAT | O_RDWR , 0666);
	if(fd < 0) return -1;

	if(ftruncate(fd, PNDSHM_SIZE) < 0) return -2;

	pnd_share_data = mmap((void*)PNDSHM_OFFSET, PNDSHM_SIZE,
			PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if(pnd_share_data == MAP_FAILED) return -3;

	memset(pnd_share_data, 0, PNDSHM_SIZE);
	pnd_share_data->pool_size = PNDSHM_SIZE - sizeof(PNDShareData);

	extern void* __malloc_pool;	// Defined in malloc.c from libcore
	__malloc_pool = (void*)pnd_share_data->pool;
	printf("%d\n", init_memory_pool(pnd_share_data->pool_size, __malloc_pool, 0));

	return 0;
}

int shmem_init() {
	extern uint64_t PHYSICAL_OFFSET;
	extern Map* vms;

	pnd_share_data->PHYSICAL_OFFSET = PHYSICAL_OFFSET;
	pnd_share_data->vms = vms;

	return 0;
}
