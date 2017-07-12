#include <stdio.h>
#include <byteswap.h>

#include <util/ring.h>
#include <util/cmd.h>

#include "pnkc.h"
#include "port.h"
#include "device.h"
#include "string.h"
#include "driver/console.h"
#include "driver/stdin.h"
#include "driver/stdout.h"

#include "stdio.h"

#define BUFFER_SIZE	4096

char __stdin[BUFFER_SIZE];
volatile size_t __stdin_head;
volatile size_t __stdin_tail;
size_t __stdin_size = BUFFER_SIZE;

char __stdout[BUFFER_SIZE];
volatile size_t __stdout_head;
volatile size_t __stdout_tail;
size_t __stdout_size = BUFFER_SIZE;

char* __stdout_ptr = __stdout;
volatile size_t* __stdout_head_ptr = &__stdout_head;
volatile size_t* __stdout_tail_ptr = &__stdout_tail;
size_t* __stdout_size_ptr = &__stdout_size;

char __stderr[BUFFER_SIZE];
volatile size_t __stderr_head;
volatile size_t __stderr_tail;
size_t __stderr_size = BUFFER_SIZE;

Device* device_stdin;
Device* device_stdout;
Device* device_stderr;

#define write1(_buf, _len) ((CharOut*)device_stdout->driver)->write(device_stdout->id, _buf, _len)
#define write2(_buf, _len) ((CharOut*)device_stderr->driver)->write(device_stderr->id, _buf, _len)
#define set_cursor1(_rows, _cols) ((CharOut*)device_stdout->driver)->set_cursor(device_stdout->id, _rows, _cols)
#define get_cursor1(_rows, _cols) ((CharOut*)device_stdout->driver)->get_cursor(device_stdout->id, _rows, _cols)

#define DEFAULT_ATTRIBUTE	0x07

static int clear(int argc, char** argv, void(*callback)(char* result, int exit_status));
static Command commands[] = {
	{
		.name = "clear",
		.desc = "Clear screen.",
		.func = clear
	},
};

void stdio_init(uint8_t apic_id, void* buffer, size_t size) {
	if(apic_id == 0) {
		CharOutInit data = { .buf = buffer, .len = size, .is_capture = apic_id == 0, .is_render = apic_id == 0 };
		device_stdin = device_register(console_in_type, &console_in_driver, NULL, NULL);
		device_stderr = device_stdout = device_register(console_out_type, &console_out_driver, NULL, &data);
		cmd_register(commands, sizeof(commands) / sizeof(commands[0]));
	} else {
		// TODO: char in redirector
		device_stdin = device_register(stdin_type, &stdin_driver, NULL, NULL);
		device_stdout = device_register(stdout_type, &stdout_driver, NULL, "stdout");
		device_stderr = device_register(stdout_type, &stdout_driver, NULL, "stderr");

//		device_stderr = device_stdout = device_register(vga_type, &vga_driver, NULL, &data);
	}
}

/*
void stdio_render(uint8_t apic_id) {
	Device* device = device_stdout;
	CharOut* driver = device->driver;
	driver->set_render(device->id, true);
	driver->scroll(device->id, 0);
}
*/

void stdio_dump(int coreno, int fd, char* buffer, volatile size_t* head, volatile size_t* tail, size_t size) {
#define HEX(v)	(((v) & 0x0f) > 9 ? ((v) & 0x0f) - 10 + 'a' : ((v) & 0x0f) + '0')

	if(*head == *tail)
		return;

	char header[10] = "Core 01> ";
	header[5] = HEX(coreno >> 4);
	header[6] = HEX(coreno >> 0);
	header[7] = '>';

	int header_len = strlen(header);
	int body_len = 80 - header_len;

	char* strchrn(const char* s, const char* e, int c) {
		char* ch = (char*)s;

		while(*ch != c && *ch != '\0' && ch < e)
			ch++;

		return ch < e && *ch == c ? ch: NULL;
	}

	char* dump_lines(char* h, char* e) {
		char* t = strchrn(h, e, '\n');
		while(t) {
			// Skip newline and null charater
			t += 2;
			while(t - h > body_len) {
				write1(header, header_len);
				write1(h, body_len);

				h += body_len;
			}

			write1(header, header_len);
			write1(h, t - h);

			if(t >= e)
				return NULL;

			h = t;
			t = strchrn(h, e, '\n');
		}

		while(e - h > body_len) {
			write1(header, header_len);
			write1(h, body_len);

			h += body_len;
		}

		return h < e ? h : NULL;
	}

	char* h = buffer + *head;
	char* e = buffer + *tail;
	if(*head > *tail) {
		h = dump_lines(h, buffer + size);
		if(h) {
			int len1 = buffer + size - h;
			write1(header, header_len);
			write1(h, len1);

			size_t len2 = body_len - len1;
			if(len2 > *tail)
				len2 = *tail;

			char* t = strchrn(h, buffer + len2, '\n');
			if(t) {
				t++;
				write1(h, t - h);
				h = t;
			} else {
				write1(buffer, len2);
				write1("\n", 1);
				h = buffer + len2;
			}
		} else {
			h = buffer;
		}
	}

	h = dump_lines(h, e);
	if(h) {
		//write1(header, header_len);
		write1(h, e - h);
		//write1("\n", 1);
	}

	*head = *tail;
}

// Ref: http://www.powerindex.net/U_convt/ascii/ascii.htm
static const char mapping[][2] = {
	{ 0, 0 },	// 0x00
	{ 0x1b, 0x1b },
	{ '1', '!' },
	{ '2', '@' },
	{ '3', '#' },
	{ '4', '$' },
	{ '5', '%' },
	{ '6', '^' },
	{ '7', '&' },
	{ '8', '*' },
	{ '9', '(' },
	{ '0', ')' },
	{ '-', '_' },
	{ '=', '+' },
	{ '\b', '\b' },	// Back space

	{ '\t', '\t' },	// Tap
	{ 'q', 'Q' },	// Scan code: 0x10
	{ 'w', 'W' },
	{ 'e', 'E' },
	{ 'r', 'R' },
	{ 't', 'T' },
	{ 'y', 'Y' },
	{ 'u', 'U' },
	{ 'i', 'I' },
	{ 'o', 'O' },
	{ 'p', 'P' },
	{ '[', '{' },
	{ ']', '}' },
	{ '\n', '\n' },	// Carriage Return

	{ 0, 0 },	// Left ctrl
	{ 'a', 'A' },
	{ 's', 'S' },
	{ 'd', 'D' },	// Scan code: 0x20
	{ 'f', 'F' },
	{ 'g', 'G' },
	{ 'h', 'H' },
	{ 'j', 'J' },
	{ 'k', 'K' },
	{ 'l', 'L' },
	{ ';', ':' },
	{ '\'', '"' },
	{ '`', '~' },

	{ 0, 0 },	// Left shift
	{ '\\', '|' },
	{ 'z', 'Z' },
	{ 'x', 'X' },
	{ 'c', 'C' },
	{ 'v', 'V' },
	{ 'b', 'B' },	// Scan code: 0x30
	{ 'n', 'N' },
	{ 'm', 'M' },
	{ ',', '<' },
	{ '.', '>' },
	{ '/', '?' },
	{ 0, 0 },	// Right shift
	{ '*', '*' },

	{ 0, 0 },	// Left alt
	{ ' ', ' ' },
	{ 0, 0 },	// Caps lock
	{ 0, 0 },	// F1
	{ 0, 0 },	// F2
	{ 0, 0 },	// F3
	{ 0, 0 },	// F4
	{ 0, 0 },	// F5
	{ 0, 0 },	// F6, Scan code: 0x40
	{ 0, 0 },	// F7
	{ 0, 0 },	// F8
	{ 0, 0 },	// F9
	{ 0, 0 },	// F10

	{ 0, 0 },	// Num lock
	{ 0, 0 },	// Scroll lock
	{ 0, '7' },	// Home
	{ 0, '8' },	// Up
	{ 0, '9' },	// Page Up
	{ '-', '-'},
	{ 0, '4' },	// Left
	{ 0, '5' },	// Center
	{ 0, '6' },	// Right
	{ '+', '+' },
	{ 0, '1' },	// End
	{ 0, '2' },	// Down, Scan code: 0x50
	{ 0, '3' },	// Page down
	{ 0, '0' },	// Insert
	{ 0x7f, '.' },	// Delete
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },	// F11
	{ 0, 0 },	// F12
	{ 0, 0 },	// 
	{ 0, 0 },	// Scan code: 0x60
};

static int shift;
static int ctrl;
static int alt;
static bool capslock;
static bool numlock;
static bool scrolllock;
static int extention;

// int _IO_putc(int ch, struct _IO_FILE* fp) {
// 	uint8_t c = ch;
// 	if(fp == stdout) {
// 		return write1((void*)&c, 1);
// 	} else if(fp == stderr) {
// 		write2((void*)&c, 1);
// 	}
// 
// 	return -1;
// }

static void led() {
	port_out8(0x60, 
			(((uint8_t)capslock) << 2) |
			(((uint8_t)numlock) << 1) |
			(((uint8_t)scrolllock) << 0));
}

void stdio_scancode(int code) {
	// Parse control code
	if(extention == 0) {
		switch(code) {
			case 0x1d:		// left ctrl press
				ctrl++;
				return;
			case 0x2a:		// left shift press
				shift++;
				return;
			case 0x36: 		// right shift press
				shift++;
				return;
			case 0x38:		// left alt press
				alt++;
				return;
			case 0x3a:
				capslock = !capslock;
				led();
				return;
			case 0x45:
				numlock = !numlock;
				led();
				return;
			case 0x46:
				scrolllock = !scrolllock;
				led();
				return;
			case 0x9d:		// ctrl release
				ctrl--;
				return;
			case 0xaa:		// left shift release
				shift--;
				return;
			case 0xb6: 		// right shift release
				shift--;
				return;
			case 0xb8:		// alt release
				alt--;
				return;
			case 0xe0:
				extention = 1;
				return;
			case 0xe1:
				extention = 2;
				return;
		}
	}

	// Parse char scancode
#define PUT()		ring_write(__stdin, __stdin_head, &__stdin_tail, __stdin_size, &ch, 1)

	// ASCII escape: http://ascii-table.com/ansi-escape-sequences-vt-100.php
	bool is_cap = false;
	char ch;
	if(extention > 0) {
		switch(code) {
			case 0x47:	// home
				ch = 0x1b;	// ESC
				PUT();
				ch = 'O';
				PUT();
				ch = 'H';
				PUT();
				break;
			case 0x49:	// page up
				ch = 0x1b;	// ESC
				PUT();
				ch = '[';
				PUT();
				ch = 0x35;
				PUT();
				ch = '~';
				PUT();
				break;
			case 0x4f:	// end
				ch = 0x1b;	// ESC
				PUT();
				ch = 'O';
				PUT();
				ch = 'F';
				PUT();
				break;
			case 0x48:	// arrow up
				ch = 0x1b;	// ESC
				PUT();
				ch = 'O';
				PUT();
				ch = 'A';
				PUT();
				break;
			case 0x50:	// arrow down
				ch = 0x1b;	// ESC
				PUT();
				ch = 'O';
				PUT();
				ch = 'B';
				PUT();
				break;
			case 0x51:	// page down
				ch = 0x1b;	// ESC
				PUT();
				ch = '[';
				PUT();
				ch = 0x36;
				PUT();
				ch = '~';
				PUT();
				break;
		}

		extention--;
	} else if(code <= 0x60) {
		ch = mapping[code][0];
		if(code <= 0x58 && ch == 0)
			return;

		if(ch >= 'a' && ch <= 'z') {
			if(shift > 0)
				is_cap = !is_cap;

			if(capslock)
				is_cap = !is_cap;

			if(is_cap)
				ch = mapping[code][1];
		} else {
			if(code >= 0x47) {
				if(numlock)
					ch = mapping[code][1];
			} else if(shift > 0) {
				ch = mapping[code][1];
			}
		}

		if(ctrl > 0) {
			if(ch >= 0x40 && ch < 0x60)
				ch -= 0x40;
		}

		PUT();
	}

	// TODO: make numlock mapping table
}

int stdio_getchar() {
	char ch;
	if(ring_read(__stdin, &__stdin_head, __stdin_tail, __stdin_size, &ch, 1) > 0)
		return (int)ch;
	else
		return -1;
}

int stdio_putchar(const char ch) {
	return ring_write(__stdin, __stdin_head, &__stdin_tail, __stdin_size, &ch, 1);
}

int vsprintf(char *str, const char *format, va_list va) {
	char* str0 = str;

	char fill;
	int width;
	int width2;

	void print_string(char* str2) {
		char* str0 = str;
		while(*str2 != 0) {
			*str++ = *str2++;
		}

		while(str - str0 < width) {
			*str++ = fill;
		}
	}

	void print_integer(int value, char a, int base) {
		if(value < 0) {
			*str++ = '-';
			value = -value;
		}

		char* str0 = str;
		do {
			int mod = value % base;
			if(mod >= 10)
				*str++ = a + mod - 10;
			else
				*str++ = '0' + mod;

			value /= base;
		} while(value != 0);

		while(str - str0 < width) {
			*str++ = fill;
		}

		int half = (str - str0) / 2;
		for(int i = 0; i < half; i++) {
			char tmp = str0[i];
			str0[i] = str[-i - 1];
			str[-i - 1] = tmp;
		}
	}

	void print_long(long value, char a, int base) {
		if(value < 0) {
			*str++ = '-';
			value = -value;
		}

		char* str0 = str;
		do {
			int mod = value % base;
			if(mod >= 10)
				*str++ = a + mod - 10;
			else
				*str++ = '0' + mod;

			value /= base;
		} while(value != 0);

		while(str - str0 < width) {
			*str++ = fill;
		}

		int half = (str - str0) / 2;
		for(int i = 0; i < half; i++) {
			char tmp = str0[i];
			str0[i] = str[-i - 1];
			str[-i - 1] = tmp;
		}
	}

	void print_unsigned_integer(unsigned int value, char a, int base) {
 		char* str0 = str;
 		do {
 			int mod = value % base;
 			if(mod >= 10)
 				*str++ = a + mod - 10;
 			else
 				*str++ = '0' + mod;
 
 			value /= base;
 		} while(value != 0);
 
 		while(str - str0 < width) {
 			*str++ = fill;
 		}
 
 		int half = (str - str0) / 2;
 		for(int i = 0; i < half; i++) {
 			char tmp = str0[i];
 			str0[i] = str[-i - 1];
 			str[-i - 1] = tmp;
 		}
	}

	void print_unsigned_long(unsigned long value, char a, int base) {
 		char* str0 = str;
 		do {
 			int mod = value % base;
 			if(mod >= 10)
 				*str++ = a + mod - 10;
 			else
 				*str++ = '0' + mod;
 
 			value /= base;
 		} while(value != 0);
 
 		while(str - str0 < width) {
 			*str++ = fill;
 		}
 
 		int half = (str - str0) / 2;
 		for(int i = 0; i < half; i++) {
 			char tmp = str0[i];
 			str0[i] = str[-i - 1];
 			str[-i - 1] = tmp;
 		}
	}

	void print_double(double value) {
		long l = (long)value;
		print_long(l, 0, 10);

		*str++ = '.';

		if(width2 == 0)
			width2 = 6;

		for(int i = 0; i < width2; i++) {
			value = (value - l) * 10;
			if(value == 0.0) {
				*str++ = '0';
				break;
			}

			l = (long)value;
			*str++ = (char)l + '0';
		}
	}

	while(1) {
		switch(*format) {
			case '%':
				format++;
				fill = ' ';
				width = 0;
				width2 = 0;
				if(*format >= '0' && *format <= '9') {
					if(*format == '0') {
						fill = '0';
					}

					while(*format >= '0' && *format <= '9') {
						width = width * 10 + *format++ - '0';
					}
				} else if(*format == '#') {
					format++;
					switch(*format) {
						case 'x':
							*str++ = '0';
							*str++ = 'x';
							break;
						case 'o':
							*str++ = '0';
							break;
					}
				}

				if(*format == '.') {
					format++;

					while(*format >= '0' && *format <= '9') {
						width2 = width2 * 10 + *format++ - '0';
					}
				}

				switch(*format) {
					case '%':
						*str++ = *format++;
						break;
					case 'c':
						*str++ = va_arg(va, int);
						format++;
						break;
					case 's':
						print_string(va_arg(va, char*));
						format++;
						break;
					case 'd':
					case 'i':
						print_integer(va_arg(va, int), 0, 10);
						format++;
						break;
					case 'u':
						print_unsigned_integer(va_arg(va, unsigned int), 0, 10);
						format++;
						break;
					case 'l':
						format++;
						switch(*format) {
							case 'u':
								print_unsigned_long(va_arg(va, unsigned long), 0, 10);
								format++;
								break;
							case 'd':
								print_long(va_arg(va, unsigned long), 0, 10);
								format++;
								break;
							case 'x':
								print_unsigned_long(va_arg(va, unsigned long), 'a', 16);
								format++;
								break;
							case 'X':
								print_unsigned_long(va_arg(va, unsigned long), 'A', 16);
								format++;
								break;
							case 'o':
								print_unsigned_long(va_arg(va, unsigned long), 0, 8);
								format++;
								break;
							case 'O':
								print_unsigned_long(va_arg(va, unsigned long), 0, 8);
								format++;
								break;
							default:
								*str++ = 0;
								goto done;
						}
						break;
					case 'p':
						width = 16;
						fill = '0';
						print_unsigned_long(va_arg(va, unsigned long), 'a', 16);
						format++;
						break;
					case 'x':
						print_unsigned_integer(va_arg(va, unsigned int), 'a', 16);
						format++;
						break;
					case 'X':
						print_unsigned_integer(va_arg(va, unsigned int), 'A', 16);
						format++;
						break;
					case 'o':
						print_unsigned_integer(va_arg(va, unsigned int), 0, 8);
						format++;
						break;
					case 'O':
						print_unsigned_integer(va_arg(va, unsigned int), 0, 8);
						format++;
						break;
					case 'f':
						print_double(va_arg(va, double));
						format++;
						break;
					default:
						*str++ = 0;
						// Unknown format
						goto done;
				}

				break;
			case 0:
				*str++ = *format++;
				goto done;
			default:
				*str++ = *format++;
		}
	}
done:

	return str - str0;
}

int sprintf(char *str, const char *format, ...) {
	va_list va;
	va_start(va, format);
	int len = vsprintf(str, format, va);
	va_end(va);

	return len;
}

//int __sprintf_chk(char *__restrict __s, int flag, char *str, const char *format, ...) {
int __sprintf_chk (char *__restrict str, int __flag, size_t __slen,
			  const char *__restrict format, ...) {
	va_list va;
	va_start(va, format);
	int len = vsprintf(str, format, va);
	va_end(va);

	return len;
}

int printf(const char* format, ...) {
	char buf[4096];

	va_list va;
	va_start(va, format);
	int len = vsprintf(buf, format, va);
	va_end(va);

	return write1(buf, len);
}

int __printf_chk(int flag, const char *format, ...) {
	char buf[4096];

	va_list va;
	va_start(va, format);
	int len = vsprintf(buf, format, va);
	va_end(va);

	return write1(buf, len);
}

int fprintf(FILE* stream, const char* format, ...) {
	char buf[4096];

	va_list va;
	va_start(va, format);
	int len = vsprintf(buf, format, va);
	va_end(va);

	return write1(buf, len);
}

int __fprintf_chk(FILE* stream, int flag, const char* format, ...) {
	char buf[4096];

	va_list va;
	va_start(va, format);
	int len = vsprintf(buf, format, va);
	va_end(va);

	return write1(buf, len);
}

void exit(int status) {
	printf("Exit is called: %d\n", status);
	while(1) asm("hlt");
}

int putchar(int c) {
	char ch = c;
	return write1(&ch, 1);
}

ssize_t write0(int fd, const void* buf, size_t count) {
	if(fd == 1)
		return write1(buf, count);

	return -1;
}

uint32_t htonl(uint32_t hostlong) {
	return bswap_32(hostlong);
}

uint16_t htons(uint16_t hostshort) {
	return bswap_16(hostshort);
}

uint32_t ntohl(uint32_t netlong) {
	return bswap_32(netlong);
}

uint16_t ntohs(uint16_t netshort) {
	return bswap_16(netshort);
}

static int clear(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	printf("\f");

	return 0;
}
