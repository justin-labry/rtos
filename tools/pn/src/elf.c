#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <elf.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "elf.h"
#include "symbols.h"
#include "pnkc.h"
#include "page.h"

static void copy(void* dst, void* src, size_t size, uint16_t apic_id) {
	memcpy(dst, src, size);
}

static void clean(void* dst, size_t size, uint16_t apic_id) {
	memset(dst, 0, size);
}

static bool copy_kernel(PNKC* pnkc) {
 	uint16_t apic_id = 0;
	void* pos = pnkc;
	pos += sizeof(PNKC);

	if(pnkc->magic != PNKC_MAGIC) return false;

	// Copy .text
	clean((void*)0x200000, 0x400000, apic_id);
	copy((void*)0x200000 + pnkc->text_offset, pos, pnkc->text_size, apic_id);
	pos += pnkc->text_size;

	// Copy .rodata
	copy((void*)0x200000 + pnkc->rodata_offset, pos, pnkc->rodata_size, apic_id);
	pos += pnkc->rodata_size;

	// Copy smap
	copy((void*)0x200000 + pnkc->smap_offset, pos, pnkc->smap_size, apic_id);
	pos += pnkc->smap_size;

	// Copy .data
	clean((void*)0x600000, 0x200000, apic_id);
	copy((void*)0x400000 + pnkc->data_offset, pos, pnkc->data_size, apic_id);
	pos += pnkc->data_size;

	// Write PNKC
	copy((void*)(0x200000 /* Kernel entry end */ - sizeof(PNKC)), pnkc, sizeof(PNKC), apic_id);

	return true;
}

static int kernel_symbols_init() {
	void* mmap_symbols[][2] = {
		{ &DESC_TABLE_AREA_START, "DESC_TABLE_AREA_START"},
		{ &DESC_TABLE_AREA_END, "DESC_TABLE_AREA_END"},

		{ &GDTR_ADDR, "GDTR_ADDR" },
		{ &GDT_ADDR, "GDT_ADDR" },
		{ &GDT_END_ADDR, "GDT_END_ADDR" },
		{ &TSS_ADDR, "TSS_ADDR" },

		{ &IDTR_ADDR, "IDTR_ADDR" },
		{ &IDT_ADDR, "IDT_ADDR" },
		{ &IDT_END_ADDR, "IDT_END_ADDR" },

		{ &KERNEL_TEXT_AREA_START, "KERNEL_TEXT_AREA_START" },
		{ &KERNEL_TEXT_AREA_END, "KERNEL_TEXT_AREA_END" },

		{ &KERNEL_DATA_AREA_START, "KERNEL_DATA_AREA_START" },
		{ &KERNEL_DATA_AREA_END, "KERNEL_DATA_AREA_END" },

		{ &KERNEL_DATA_START, "KERNEL_DATA_START" },
		{ &KERNEL_DATA_END, "KERNEL_DATA_END" },

		{ &VGA_BUFFER_START, "VGA_BUFFER_START" },
		{ &VGA_BUFFER_END, "VGA_BUFFER_END" },

		{ &USER_INTR_STACK_START, "USER_INTR_STACK_START" },
		{ &USER_INTR_STACK_END, "USER_INTR_STACK_END" },

		{ &KERNEL_INTR_STACK_START, "KERNEL_INTR_STACK_START" },
		{ &KERNEL_INTR_STACK_END, "KERNEL_INTR_STACK_END" },

		{ &KERNEL_STACK_START, "KERNEL_STACK_START" },
		{ &KERNEL_STACK_END, "KERNEL_STACK_END" },

		{ &PAGE_TABLE_START, "PAGE_TABLE_START" },
		{ &PAGE_TABLE_END, "PAGE_TABLE_END" },

		{ &LOCAL_MALLOC_START, "LOCAL_MALLOC_START" },
		{ &LOCAL_MALLOC_END, "LOCAL_MALLOC_END" },

		{ &SHARED_ADDR, "SHARED_ADDR" },

		{ &__stdout_ptr, "__stdout" },
		{ &__stdout_head_ptr, "__stdout_head" },
		{ &__stdout_tail_ptr, "__stdout_tail" },
		{ &__stdout_size_ptr, "__stdout_size" },
	};

	int count = sizeof(mmap_symbols) / (sizeof(char*) * 2);
	for(int i = 0; i < count; i++) {
		uint64_t symbol_addr = elf_get_symbol(mmap_symbols[i][1]);
		if(!symbol_addr) {
			printf("\tCan't Get Symbol Address: \"%s\"", mmap_symbols[i][1]);
			return -1;
		}
		char** addr = (char**)mmap_symbols[i][0];

		// PND needs to access shread area
		if(!strcmp("SHARED_ADDR", mmap_symbols[i][1]) || !strncmp("LOCAL_MALLOC", mmap_symbols[i][1], strlen("LOCAL_MALLOC"))) *addr = (void*)VIRTUAL_TO_PHYSICAL(symbol_addr);
		else *addr = (char*)symbol_addr;

		printf("\tSymbol \"%s\" : %p %p\n", mmap_symbols[i][1], symbol_addr, *addr);
	}

	return 0;
}

int elf_copy(char* elf_file, unsigned long kernel_start_address) {
	int fd = open(elf_file, O_RDONLY);
	if(fd < 0) {
		printf("\tCannot open file: %s\n", elf_file);
		return -1;
	}

	off_t elf_size = lseek(fd, 0, SEEK_END);
	PNKC* pnkc = (PNKC*)mmap(NULL, elf_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if(pnkc == MAP_FAILED) {
		close(fd);
		return -2;
	}

	copy_kernel(pnkc);
	munmap(pnkc, elf_size);
	close(fd);

	printf("\tInitiliazing Kernel symbols...\n");
	if(kernel_symbols_init()) return -3;

	return 0;
}

uint64_t elf_get_symbol(char* sym_name) {
	Symbol* symbol = symbols_get(sym_name);
	if(!symbol)
		return 0;

	return (uint64_t)symbol->address;
}
