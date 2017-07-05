#ifndef __MMAP_H__
#define __MMAP_H__

/*
 * This header describes PacketNgin memory map. (docs/mmap.md)
 * All of these addresses are virtual address. Address mapping is
 * described in PacketNgin linker script (kernel/build/elf_x86_64.ld)
 **/

#include <stdint.h>

extern uint64_t PHYSICAL_OFFSET;

/*
 * BIOS area (0M ~ 2M)
 **/
#define BIOS_AREA_START		0x0
#define BIOS_AREA_END		0x200000 /* 2 MB */

#define BDA_AREA_START		0x0400
#define BDA_AREA_END		0x04FF

/*
 * Description table area (2M ~ 3M)
 **/
extern char* DESC_TABLE_AREA_START;
extern char* DESC_TABLE_AREA_END;

extern char* GDTR_ADDR;
extern char* GDT_ADDR;
extern char* GDT_END_ADDR;
extern char* TSS_ADDR;

extern char* IDTR_ADDR;
extern char* IDT_ADDR;
extern char* IDT_END_ADDR;

 /*
  * Kernel text area (4M ~ 6M)
  **/
extern char* KERNEL_TEXT_AREA_START;
extern char* KERNEL_TEXT_AREA_END;
#define KERNEL_TEXT_AREA_SIZE		(KERNEL_TEXT_AREA_END - KERNEL_TEXT_AREA_START)

/*
 * Kernel data area (6M ~ 8M)
 **/
extern char* KERNEL_DATA_AREA_START;
extern char* KERNEL_DATA_AREA_END;
#define KERNEL_DATA_AREA_SIZE		(KERNEL_DATA_AREA_END - KERNEL_DATA_AREA_START)

extern char* KERNEL_DATA_START;
extern char* KERNEL_DATA_END;

extern char* VGA_BUFFER_START;
extern char* VGA_BUFFER_END;

extern char* USER_INTR_STACK_START;
extern char* USER_INTR_STACK_END;

extern char* KERNEL_INTR_STACK_START;
extern char* KERNEL_INTR_STACK_END;

extern char* KERNEL_STACK_START;
extern char* KERNEL_STACK_END;

extern char* PAGE_TABLE_START;
extern char* PAGE_TABLE_END;

extern char* LOCAL_MALLOC_START;
extern char* LOCAL_MALLOC_END;

extern char* SHARED_ADDR;

/*
 * Kernel data area (8M ~ 38M)
 *
 * Kernel data area above (4M ~ 6M) is duplicated here for 15 cores.
 * PacketNgin support maximum 16 cores.
 **/

/*
 * Ramdisk area (38M ~ sizeof(initrd.img))
 **/
#define RAMDISK_START               (KERNEL_TEXT_AREA_START + KERNEL_TEXT_AREA_SIZE * 16)

#endif /* __MMAP_H__ */
