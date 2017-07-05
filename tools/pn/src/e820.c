#include <stdio.h>
#include <malloc.h>

#include "e820.h"
#include "smap.h"

List* e820_get_mem_blocks() {
	int count = smap_count;
	List* blocks = list_create(NULL);
	if(!blocks) return NULL;

	printf("\tSystem memory map\n");
	for(int i = 0; i < count; i++) {
		SMAP* entry = &smap[i];
		char* type;
		switch(entry->type) {
			case SMAP_TYPE_MEMORY:
				;
				type = "Memory";
				Block* block = malloc(sizeof(Block));
				if(!block) return NULL;

				block->start = entry->base;
				block->end = entry->base + entry->length;
				list_add(blocks, block);
				break;
			case SMAP_TYPE_RESERVED:
				type = "Reserved";
				break;
			case SMAP_TYPE_ACPI:
				type = "ACPI";
				break;
			case SMAP_TYPE_NVS:
				type = "NVS";
				break;
			case SMAP_TYPE_UNUSABLE:
				type = "Disabled";
				break;
			default:
				break;
				type = "Unknown";
		}
		printf("\t\t0x%016lx - 0x%016lx: %s(%d)\n", entry->base, entry->base + entry->length, type, entry->type);
	}
	return blocks;
}
