#include <stdio.h>
#include <stdint.h>
#include <tlsf.h>
#include "mmap.h"
#include "gmalloc.h"
#include "shared.h"
#include <util/list.h>
#include <malloc.h>
#include "smap.h"
#include "idt.h"
#include "e820.h"
#include "pnkc.h"

uint32_t bmalloc_count;

typedef struct _BmallocPool {
	bool		used;
	bool		head;
	int		count;
	uint64_t	pool;
} BmallocPool;

BmallocPool* bmalloc_pool;
void* gmalloc_pool;

int gmalloc_init() {
	/* Gmalloc pool area : IDT_END_ADDR */
	uint64_t start = VIRTUAL_TO_PHYSICAL(IDT_END_ADDR);
	uint64_t end = VIRTUAL_TO_PHYSICAL(DESC_TABLE_AREA_END);

	init_memory_pool(end - start, (void*)start, 0);

	gmalloc_pool = (void*)start;

	// TODO: Check array size
	Block reserved[3 + MP_MAX_CORE_COUNT + 1 + 1];
	int reserved_count = 0;

	reserved[reserved_count].start = VIRTUAL_TO_PHYSICAL(BIOS_AREA_START);
	reserved[reserved_count].end = VIRTUAL_TO_PHYSICAL(BIOS_AREA_END);
	reserved_count++;

	reserved[reserved_count].start = VIRTUAL_TO_PHYSICAL(DESC_TABLE_AREA_START);
	reserved[reserved_count].end = VIRTUAL_TO_PHYSICAL(DESC_TABLE_AREA_END);
	reserved_count++;

	reserved[reserved_count].start = VIRTUAL_TO_PHYSICAL(KERNEL_TEXT_AREA_START);
	reserved[reserved_count].end = VIRTUAL_TO_PHYSICAL(KERNEL_TEXT_AREA_END);
	reserved_count++;

	printf("\tReserved AP Space\n");
	uint8_t* core_map = mp_processor_map();
	for(int i = 0; i < MP_MAX_CORE_COUNT; i++) {
 		if(core_map[i] == MP_CORE_INVALID)
 			continue;

		reserved[reserved_count].start = VIRTUAL_TO_PHYSICAL(KERNEL_DATA_AREA_START)
			+ KERNEL_DATA_AREA_SIZE * i;
		reserved[reserved_count].end = VIRTUAL_TO_PHYSICAL(KERNEL_DATA_AREA_START)
			+ KERNEL_DATA_AREA_SIZE * (i + 1);
		reserved_count++;
	}

	//TODO fix here
	//Multi Kernel don't use pnkc
 	PNKC* pnkc = (PNKC*)(0x200000 - sizeof(PNKC));
 	reserved[reserved_count].start = (uint64_t)pnkc;
 	reserved[reserved_count].end = (uint64_t)pnkc + sizeof(PNKC);
 	reserved_count++;

	//FIXME
	reserved[reserved_count].start = VIRTUAL_TO_PHYSICAL(RAMDISK_START);
	reserved[reserved_count].end = VIRTUAL_TO_PHYSICAL(RAMDISK_START + (pnkc->initrd_end - pnkc->initrd_start));
	reserved_count++;

	// Relocate to physical to determine physical memory map
	void relocate(Block* blocks, int count) {
		for(int i = 0; i < count; i++) {
			Block* r = &reserved[i];
			r->start += PHYSICAL_OFFSET;
			r->end += PHYSICAL_OFFSET;
		}
	}
	relocate(reserved, reserved_count);

	for(int i = 0; i < reserved_count; i++) {
		Block* r = &reserved[i];
		printf("\t\tReserved[%02d] : %p ~ %p\n", i, r->start, r->end);
	}

	List* blocks = e820_get_mem_blocks();
	// Remove reserved blocks
	for(size_t i = 0; i < list_size(blocks); i++) {
		Block* b = list_get(blocks, i);

		for(int j = 0; j < reserved_count; j++) {
			Block* r = &reserved[j];

			if(r->end <= b->start || r->start >= b->end) {		// Out of bounds
				continue;
			} else if(r->start <= b->start && r->end >= b->end) {	// Including exception
				list_remove(blocks, i);
				i--;
				break;
			} else if(r->start <= b->start && r->end < b->end) {	// Head cut
				b->start = r->end;
			} else if(r->start < b->end && r->end >= b->end) {	// Tail cut
				b->end = r->start;
			} else {						// Body cut
				Block* b2 = malloc(sizeof(Block));
				if(!b2)
					return -1;
				b2->start = r->end;
				b2->end = b->end;
				list_add(blocks, b2);

				b->end = r->start;
			}
		}
	}

	printf("\tExtend gmalloc pool\n");
	ListIterator iter;
	list_iterator_init(&iter, blocks);
	while(list_iterator_has_next(&iter)) {
		Block* b = list_iterator_next(&iter);

		// Relocate to virtual to acually set malloc pool
		b->start -= PHYSICAL_OFFSET;
		b->end -= PHYSICAL_OFFSET;

		uintptr_t start = (b->start + 0x200000 - 1) & ~((uintptr_t)0x200000 - 1);
		uintptr_t end = b->end & ~((uintptr_t)0x200000 - 1);

		if(start >= end) {
			printf("\t\t0x%016lx - 0x%016lx\n", b->start, b->end - b->start);
			add_new_area((void*)b->start, b->end - b->start, gmalloc_pool);
			b->start = b->end = 0;
		} else {
			if(start > b->start) {
				printf("\t\t0x%016lx - 0x%016lx\n", b->start, start - b->start);
				add_new_area((void*)b->start, start - b->start, gmalloc_pool);
				b->start = start;
			}

			if(end < b->end) {
				printf("\t\t0x%016lx - 0x%016lx\n", end, b->end - end);
				add_new_area((void*)end, b->end - end, gmalloc_pool);
				b->end = end;
			}
		}

		if(b->start < b->end) {
			bmalloc_count += (end - start) / 0x200000;
		} else {
			free(b);
			list_iterator_remove(&iter);
		}
	}

	printf("\tExtend bmalloc pool\n");
	Block* pop() {
		uintptr_t last_start = UINTPTR_MAX;
		int last_index = -1;

		int i = 0;
		list_iterator_init(&iter, blocks);
		while(list_iterator_has_next(&iter)) {
			Block* b = list_iterator_next(&iter);
			if(b->start < last_start) {
				last_start = b->start;
				last_index = i;
			}

			i++;
		}

		if(last_index >= 0)
			return list_remove(blocks, last_index);
		else
			return NULL;
	}

	bmalloc_pool = malloc(sizeof(BmallocPool) * bmalloc_count);
	if(!bmalloc_pool)
		return -2;

	uint32_t bmalloc_index = 0;
	Block* block = pop();
	while(block) {
		uintptr_t start = block->start;
		uintptr_t end = block->end;
		printf("\t\t0x%016lx - 0x%016lx\n", start, end);

		while(start < end) {
			bmalloc_pool[bmalloc_index++].pool = start;
			start += 0x200000;
		}

		free(block);

		block = pop();
	}

	list_destroy(blocks);

	void print_pool(char* message, size_t total) {
		size_t mb = total / 1024 / 1024;
		size_t kb = total / 1024 - mb * 1024;
		printf("%s: %d.%dMB ", message, mb, kb);
	}

	printf("\tMemory pool: ");
	print_pool("global", gmalloc_total());
	print_pool("block", bmalloc_total());
	printf("\n");

	return 0;
}

inline size_t gmalloc_total() {
	return get_total_size(gmalloc_pool);
}

inline size_t gmalloc_used() {
	return get_used_size(gmalloc_pool);
}

inline void* gmalloc(size_t size) {
	do {
		void* ptr = malloc_ex(size, gmalloc_pool);
		if(ptr) {
			return ptr;
		}

		void* block = bmalloc(1);
		if(!block) {
			// TODO: print to stderr
			printf("ERROR: Not enough block memory!!!\n");
			while(1);
			return NULL;
		}

		add_new_area(block, 0x200000, gmalloc_pool);
	} while(1);
}

inline void gfree(void* ptr) {
	free_ex(ptr, gmalloc_pool);
}

inline void* grealloc(void* ptr, size_t size) {
	return realloc_ex(ptr, size, gmalloc_pool);
}

inline void* gcalloc(uint32_t nmemb, size_t size) {
	return calloc_ex(nmemb, size, gmalloc_pool);
}

static inline bool is_linear(BmallocPool* bmalloc_pool, int count) {
	for(int i = 0; i < count; i++)
		if(bmalloc_pool[i].used)
			return false;

	return true;
}

static inline void set_used(BmallocPool* bmalloc_pool, int count) {
	bmalloc_pool[0].head = true;
	bmalloc_pool[0].count = count;

	for(int i = 0; i < count; i++)
		bmalloc_pool[i].used = true;
}

static inline void clear_used(BmallocPool* bmalloc_pool) {
	for(int i = 0; i < bmalloc_pool[0].count; i++)
		bmalloc_pool[i].used = false;

	bmalloc_pool[0].head = false;
	bmalloc_pool[0].count = 0;
}

void* bmalloc(int count) {
	for(size_t i = 0; i < bmalloc_count; i++) {
		if(!bmalloc_pool[i].used) {
			if(is_linear(&bmalloc_pool[i], count)) {
				set_used(&bmalloc_pool[i], count);
				return (void*)bmalloc_pool[i].pool;
			}

			i += count;
		}
	}

	printf("Not enough block memory!!!");
	while(1);

	return NULL;
}

void bfree(void* ptr) {
	for(size_t i = 0; i < bmalloc_count; i++) {
		if((void*)bmalloc_pool[i].pool == ptr) {
			if(bmalloc_pool[i].head)
				clear_used(&bmalloc_pool[i]);
			break;
		}
	}
}

size_t bmalloc_total() {
	return bmalloc_count * 0x200000;
}

size_t bmalloc_used() {
	size_t size = 0;
	for(size_t i = 0; i < bmalloc_count; i++) {
		if(bmalloc_pool[i].used) {
			size += 0x200000;
		}
	}

	return size;
}
