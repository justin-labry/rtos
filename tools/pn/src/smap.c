#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "popcorn.h"
#include "smap.h"

int smap_init() {
	int mem_fd = open_mem(O_RDWR);
	if(mem_fd == -1)
		return -1;

	struct boot_params* boot_params = map_boot_param(mem_fd);
	if(!boot_params)
		return -2;

	smap_count = boot_params->e820_entries;
	memcpy(smap, boot_params->e820_map, sizeof(boot_params->e820_map));
	unmap_boot_param(boot_params);
	close_mem(mem_fd);

	printf("\tSystem Memory Map\n");
	smap_dump();

	return 0;
}

void smap_dump() {
	for(uint8_t i = 0 ; i < smap_count; i++) {
		SMAP* entry = &smap[i];
		char* type;
		switch(entry->type) {
			case SMAP_TYPE_MEMORY:
				type = "Memory";
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
				type = "Unknown";
		}
		printf("\t\t0x%016lx - 0x%016lx: %s(%d)\n", entry->base, entry->base + entry->length, type, entry->type);
	}
}

static void smap_update(uint64_t start, uint64_t end, uint32_t type) {
	SMAP _smap[SMAP_MAX];
	uint8_t _smap_count = 0;

	for(uint8_t i = 0 ; i < smap_count; i++) {
		if(smap[i].base < end && smap[i].base + smap[i].length >= end) { 
			if(start <= smap[i].base) { // Head cut
				if(smap[i].base + smap[i].length - end) { // Length is not zero
					_smap[_smap_count].base = end;
					_smap[_smap_count].length = smap[i].base + smap[i].length - end;
					_smap[_smap_count++].type = smap[i].type;
				}
			} else if(start > smap[i].base) { // Include
				_smap[_smap_count].base = smap[i].base;
				_smap[_smap_count].length = start - smap[i].base;
				_smap[_smap_count++].type = smap[i].type;

				if(smap[i].base + smap[i].length - end) {
					_smap[_smap_count].base = end;
					_smap[_smap_count].length = smap[i].base + smap[i].length - end;
					_smap[_smap_count++].type = smap[i].type;
				}
			}
		} else if(smap[i].base <= start && smap[i].base + smap[i].length > start) { // Tail Cut
			if(start - smap[i].base) {
				_smap[_smap_count].base = smap[i].base;
				_smap[_smap_count].length = start - smap[i].base;
				_smap[_smap_count++].type = smap[i].type;
			}
		} else if(smap[i].base > start && smap[i].base + smap[i].length < end) { // Inner
			continue;
		} else { // Out
			_smap[_smap_count].base = smap[i].base;
			_smap[_smap_count].length = smap[i].length;
			_smap[_smap_count++].type = smap[i].type;
		}
	}

	for(uint8_t i = 0; i < _smap_count; i++) {
		if(_smap[i].base > start) {
			// Shift
			memmove(&_smap[i + 1], &_smap[i], sizeof(SMAP) * (_smap_count - i));
			_smap_count++;

			_smap[i].base = start;
			_smap[i].length = end - start;
			_smap[i].type = type;
			goto next;
		}
	}

	_smap[_smap_count].base = start;
	_smap[_smap_count].length = end - start;
	_smap[_smap_count++].type = type;

next:

	for(int i = 0; i < _smap_count - 1; i++) {
		if((_smap[i].type == _smap[i + 1].type) && ((_smap[i].base + _smap[i].length) == _smap[i + 1].base)) {
			_smap[i].length = _smap[i].length + _smap[i + 1].length;
			memmove(&_smap[i + 1], &_smap[i + 2], sizeof(SMAP) * (_smap_count - 2 - i));
			// Left shift
			_smap_count--;
			i--;
		}
	}

	memcpy(smap, _smap, sizeof(SMAP) * SMAP_MAX);
	smap_count = _smap_count;

	char* _type;
	switch(type) {
		case SMAP_TYPE_MEMORY:
			_type = "Memory";
			break;
		case SMAP_TYPE_RESERVED:
			_type = "Reserved";
			break;
		case SMAP_TYPE_ACPI:
			_type = "ACPI";
			break;
		case SMAP_TYPE_NVS:
			_type = "NVS";
			break;
		case SMAP_TYPE_UNUSABLE:
			_type = "Disabled";
			break;
		default:
			_type = "Unknown";
	}
	printf("\tSMAP update: 0x%016lx - 0x%016lx: %s(%d)\n", start, end, _type, type);
}

static uint64_t calculate_byte(char* str) {
	char* end;
	unsigned long byte = strtol(str, &end, 10);
	if(!end)
		return -1;

	if(!strcmp(end, "G")) {
		byte *= 1024 * 1024 * 1024;
	} else if(!strcmp(end, "g")) {
		byte *= 1024 * 1024 * 1024;
	} else if(!strcmp(end, "M")) {
		byte *= 1024 * 1024;
	} else if(!strcmp(end, "m")) {
		byte *= 1024 * 1024;
	} else if(!strcmp(end, "K")) {
		byte *= 1024;
	} else if(!strcmp(end, "k")) {
		byte *= 1024;
	}

	return byte;
}

int smap_update_memmap(char* str) {
	char* _start;
	char* _length;

       	if(strchr(str, '@')) {
		_length = strtok_r(str, "@", &_start);
		uint64_t start = calculate_byte(_start);
		uint64_t end = start + calculate_byte(_length);
		smap_update(start, end, SMAP_TYPE_MEMORY);
	} else if(strchr(str, '#')) {
		_length = strtok_r(str, "#", &_start);
		uint64_t start = calculate_byte(_start);
		uint64_t end = start + calculate_byte(_length);
		smap_update(start, end, SMAP_TYPE_ACPI);
	} else if(strchr(str, '$')) {
		_length = strtok_r(str, "$", &_start);
		uint64_t start = calculate_byte(_start);
		uint64_t end = start + calculate_byte(_length);
		smap_update(start, end, SMAP_TYPE_RESERVED);
	} else if(strchr(str, '!')) {
		_length = strtok_r(str, "!", &_start);
		uint64_t start = calculate_byte(_start);
		uint64_t end = start + calculate_byte(_length);
		smap_update(start, end, SMAP_TYPE_RESERVED);
	} else
		return -1;

	return 0;
}

int smap_update_mem(char* str) {
	char* end;
	uint64_t mem = strtol(str, &end, 10);
	if(!end)
		return -1;

	if(!strcmp(end, "G")) {
		mem *= 1024 * 1024 * 1024;
	} else if(!strcmp(end, "M")) {
		mem *= 1024 * 1024;
	} else if(!strcmp(end, "K")) {
		mem *= 1024;
	}
	smap_update(mem, UINTPTR_MAX, SMAP_TYPE_RESERVED);

	return 0;
}

