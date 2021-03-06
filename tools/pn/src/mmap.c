#include <unistd.h>
#include <stdint.h>

uint64_t PHYSICAL_OFFSET;

char* DESC_TABLE_AREA_START;
char* DESC_TABLE_AREA_END;

char* GDTR_ADDR;
char* GDT_ADDR;
char* GDT_END_ADDR;
char* TSS_ADDR;

char* IDTR_ADDR;
char* IDT_ADDR;
char* IDT_END_ADDR;

 /*
  * Kernel text area (4M ~ 6M)
  **/
char* KERNEL_TEXT_AREA_START;
char* KERNEL_TEXT_AREA_END;
#define KERNEL_TEXT_AREA_SIZE		(KERNEL_TEXT_AREA_END - KERNEL_TEXT_AREA_START)

/*
 * Kernel data area (6M ~ 8M)
 **/
char* KERNEL_DATA_AREA_START;
char* KERNEL_DATA_AREA_END;
#define KERNEL_DATA_AREA_SIZE		(KERNEL_DATA_AREA_END - KERNEL_DATA_AREA_START)

char* KERNEL_DATA_START;
char* KERNEL_DATA_END;

char* VGA_BUFFER_START;
char* VGA_BUFFER_END;

char* USER_INTR_STACK_START;
char* USER_INTR_STACK_END;

char* KERNEL_INTR_STACK_START;
char* KERNEL_INTR_STACK_END;

char* KERNEL_STACK_START;
char* KERNEL_STACK_END;

char* PAGE_TABLE_START;
char* PAGE_TABLE_END;

char* LOCAL_MALLOC_START;
char* LOCAL_MALLOC_END;

char* SHARED_ADDR;

char* __stdout_ptr;
volatile size_t* __stdout_head_ptr;
volatile size_t* __stdout_tail_ptr;
size_t* __stdout_size_ptr;
