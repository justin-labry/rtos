#ifndef __STDIO_H__
#define __STDIO_H__

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <util/types.h>

#define STDIO_NORMAL  0x07
#define STDIO_PASS    0x02
#define STDIO_FAIL    (0x04 | 0x08)

extern volatile size_t __stdin_head;
extern volatile size_t __stdin_tail;
extern size_t __stdin_size;

extern char __stdout[];
extern volatile size_t __stdout_head;
extern volatile size_t __stdout_tail;
extern size_t __stdout_size;

extern char* __stdout_ptr;
extern volatile size_t* __stdout_head_ptr;
extern volatile size_t* __stdout_tail_ptr;
extern size_t* __stdout_size_ptr;

extern char __stderr[];
extern volatile size_t __stderr_head;
extern volatile size_t __stderr_tail;
extern size_t __stderr_size;
	
void stdio_init(uint8_t apic_id, void* buffer, size_t size);

void stdio_scancode(int code);
int stdio_getchar();
int stdio_putchar(const char ch);

int printf(const char* format, ...);

#endif /* __STDIO_H__ */
