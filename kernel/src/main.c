// Standard C
#include <stdio.h>
#include <string.h>
#include <errno.h>

// Core
#include <util/cmd.h>
#include <util/event.h>

// Kernel
#include "asm.h"
#include "timer.h"
#include "malloc.h"
#include "gmalloc.h"
#include "page.h"
#include "stdio.h"
#include "port.h"
#include "amp.h"
#include "mp.h"
#include "cpu.h"
#include "gdt.h"
#include "idt.h"
#include "acpi.h"
#include "pci.h"
#include "apic.h"
#include "ioapic.h"
#include "task.h"
#include "icc.h"
#include "icc_ap.h"
#include "symbols.h"
#include "file.h"
#include "module.h"
#include "device.h"
#include "vnic.h"
#include "manager.h"
#include "shell.h"
#include "loader.h"
#include "vfio.h"
#include "shared.h"
#include "pnkc.h"
#include "mmap.h"
#include "version.h"

//
#include "rtc.h"
#include "packet_dump.h"
#include "ver.h"
#include "mount.h"
//

// Drivers
#include "driver/pata.h"
#include "driver/virtio_blk.h"
#include "driver/ramdisk.h"
#include "driver/usb/libpayload.h"
#include "driver/nicdev.h"
#include "driver/fs.h"
#include "driver/bfs.h"
#include "driver/console.h"

static void ap_timer_init() {
	extern uint64_t TIMER_FREQUENCY_PER_SEC;
	extern uint64_t __timer_ms;
	extern uint64_t __timer_us;
	extern uint64_t __timer_ns;

	*(uint64_t*)&TIMER_FREQUENCY_PER_SEC = *(uint64_t*)VIRTUAL_TO_PHYSICAL((uint64_t)&TIMER_FREQUENCY_PER_SEC);
	__timer_ms = *(uint64_t*)VIRTUAL_TO_PHYSICAL((uint64_t)&__timer_ms);
	__timer_us = *(uint64_t*)VIRTUAL_TO_PHYSICAL((uint64_t)&__timer_us);
	__timer_ns = *(uint64_t*)VIRTUAL_TO_PHYSICAL((uint64_t)&__timer_ns);
}

static bool idle_monitor_event(void* data) {
	static uint8_t trigger;

	monitor(&trigger);
	mwait(1, 0x21);

	return true;
}

static bool idle_hlt_event(void* data) {
	hlt();

	return true;
}

static void fixup_page_table(uint64_t offset) {
	uint8_t apic_id = amp_get_apic_id();
	uint64_t base = VIRTUAL_TO_PHYSICAL(PAGE_TABLE_START) + apic_id * 0x200000 + offset;
	PageTable* l4u = (PageTable*)(base + PAGE_TABLE_SIZE * PAGE_L4U_INDEX);

	for(int i = 0; i < PAGE_L4U_SIZE * PAGE_ENTRY_COUNT; i++) {
		if(i == 0)
			continue;

		l4u[i].base = i + (offset >> 21);
		l4u[i].p = 1;
		l4u[i].us = 0;
		l4u[i].rw = 1;
		l4u[i].ps = 1;
	}

	// Local APIC address (0xfee00000: 0x7F7(PFN))
	l4u[0x7f7].base = 0x7f7;

	uint64_t pml4 = VIRTUAL_TO_PHYSICAL(PAGE_TABLE_START) + apic_id * 0x200000 + offset;
	asm volatile("movq %0, %%cr3" : : "r"(pml4));
}

void main() {
	if(PHYSICAL_OFFSET) fixup_page_table(PHYSICAL_OFFSET);

	shared_init();
	amp_init((uint64_t)NULL);
	mp_sync();	// Barrier #0
	mp_init();

	uint64_t apic_id = mp_apic_id();

	extern uint64_t PHYSICAL_OFFSET;
	console_init();

	uint64_t vga_buffer = (uint64_t)VGA_BUFFER_START;
	stdio_init(apic_id, (void*)vga_buffer,  VGA_BUFFER_END - VGA_BUFFER_START);

	printf("\nInitializing malloc area...\n");
	if(malloc_init()) goto error;


	mp_sync();	// Barrier #1
	if(apic_id == 0) {
		printf("PacketNgin %s\n", VERSION);
		printf("\x1b""32mOK""\x1b""0m\n");

		printf("\nInitializing cpu...\n");
		if(cpu_init()) goto error;

		printf("\nInitializing gmalloc area...\n");
		if(gmalloc_init()) goto error;

		timer_init();
		vnic__init_timer(TIMER_FREQUENCY_PER_SEC);

		printf("\nInitilizing GDT...\n");
		gdt_init();

		printf("\nInitilizing TSS...\n");
		tss_init();

		printf("\nInitilizing IDT...\n");
		idt_init();

		mp_sync();	// Barrier #2

		printf("\nLoading GDT...\n");
		gdt_load();

		printf("\nLoading TSS...\n");
		tss_load();

		printf("\nLoading IDT...\n");
		idt_load();

		printf("\nInitializing APICs...\n");
		apic_activate();

		printf("\nInitializing PCI...\n");
		pci_init();

		printf("\nInitailizing local APIC...\n");
		apic_init();

		printf("\nInitializing I/O APIC...\n");
		ioapic_init();
		apic_enable();

		printf("\nInitializing Multi-tasking...\n");
		task_init();

		printf("\nInitializing events...\n");
		if(event_init() == false) goto error;

		printf("\nInitializing inter-core communications...\n");
		if(icc_init()) goto error;

		printf("\nInitializing USB controller driver...\n");
		if(usb_initialize()) goto error;

		printf("\nInitializing disk drivers...\n");
		if(disk_init()) goto error;

//FIXME: Move to another file
		if(!disk_register(&pata_driver, NULL)) {
			printf("\tPATA driver registration FAILED!\n");
			goto error;
		}

		if (!disk_register(&usb_msc_driver, NULL)) {
			printf("\tUSB MSC driver registration FAILED!\n");
			goto error;
		}

		if (!disk_register(&virtio_blk_driver, NULL)) {
			printf("\tVIRTIO BLOCK driver registration FAILED!\n");
			goto error;
		}

		PNKC* pnkc = (PNKC*)(0x200000 - sizeof(PNKC));
		char cmdline[64];
		sprintf(cmdline, "-addr 0x%lx -size 0x%lx", RAMDISK_START, pnkc->initrd_end - pnkc->initrd_start);
		if(!disk_register(&ramdisk_driver, cmdline)) {
			printf("\tRAM disk driver registration FAILED!\n");

			goto error;
		}

		printf("\nInitializing file system...\n");
		fs_init();
		fs_register(&bfs_driver);
		fs_mount(DISK_TYPE_RAMDISK << 16 | 0x00, 0,  FS_TYPE_BFS, "/boot");
//

		printf("\nInitializing modules...\n");
		module_init();

		printf("\nInitializing device drivers...\n");
		device_module_init();

		printf("\nInitializing VM manager...\n");
		if(vm_init()) goto error;

		printf("\nInitializing RPC manager...\n");
		if(manager_init()) goto error;

		printf("\nInitializing shell...\n");
		if(shell_init()) goto error;

		printf("\nInitializing Utilities... \n");
		printf("\nInitializing Packet Dumper... \n");
		if(packet_dump_init()) {
			printf("Can't initialize Packet Dumper\n");
		}

		printf("\nInitializing Real Time Clock... \n");
		if(rtc_init()) {
			printf("Can't initialize Real Time Clock\n");
		}

		printf("\nInitializing Mount... \n");
		if(mount_init()) {
			printf("Can't initialize Mount\n");
		}

		printf("\nInitializing Version... \n");
		if(ver_init()) {
			printf("Can't initialize Version\n");
		}
	} else {
		mp_sync();	// Barrier #2
		ap_timer_init();

		gdt_load();
		tss_load();
		idt_load();

		apic_init();
		apic_enable();

		task_init();
		event_init();
		icc_init();
		icc_ap_init();

		if(cpu_has_feature(CPU_FEATURE_MONITOR_MWAIT) && cpu_has_feature(CPU_FEATURE_MWAIT_INTERRUPT))
			event_idle_add(idle_monitor_event, NULL);
		else
			event_idle_add(idle_hlt_event, NULL);
	}

	mp_sync(); // Barrier #3

	// 	if(apic_id == 0) {
	// 	        while(exec("/boot/init.psh") > 0)
	// 	                event_loop();
	// 	}


	while(1) event_loop();

error:
	printf("\nPacketNgin initialization error occured.\n");
	printf("\tTerminated...\n");

	while(1) hlt();
}
