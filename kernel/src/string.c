#include <_string.h>
//#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <malloc.h>

#ifndef __MODE_32_BIT__
#include "apic.h"
#include "asm.h"

static bool is_sse_support() {
	uint32_t eax = 0x1;
	uint32_t edx = 0x0;
	asm volatile("cpuid"
		: "=d"(edx)
		: "a"(eax), "b"(0), "c"(0), "d"(0));

	if(edx & (1 << 25)) {//SSE support
		return true;
	} else
		return false;
}

static bool is_sse_allow() {
	uint64_t val = read_cr0();

	if(val & (1 << 3)) //cr0 ts flag check
		return false;

	APIC_Handler handler = apic_register(7, NULL);
	if(!handler) {
		return false;
	}
	apic_register(7, handler); //re regist

	return true;
}
#endif

void* __strcpy_chk(char* dest, const char* src) {
	return __memcpy(dest, src, __strlen(src) + 1);
}

void* __memcpy_chk(void* dest, const void* src, size_t size, size_t bos) {
	return __memcpy(dest, src, size);
}

static void* memset_check(void* s, int c, size_t n);
static void*(*memset_func)(void*, int, size_t) = memset_check;
static void* memset_check(void* s, int c, size_t n) {
#ifndef __MODE_32_BIT__
	if(is_sse_support()) {
		if(is_sse_allow()) {
			memset_func = __memset_sse;
		} else {
			return __memset(s, c, n);
		}
	} else {
		memset_func = __memset;
	}
#else
	memset_func = __memset;
#endif
	return memset_func(s, c, n);
}

void *memset(void *s, int c, size_t n) {
	return memset_func(s, c, n);
}

static void* memcpy_check(void* dest, const void* src, size_t n);
static void*(*memcpy_func)(void*, const void*, size_t) = memcpy_check;
static void* memcpy_check(void* dest, const void* src, size_t n) {
#ifndef __MODE_32_BIT__
	if(is_sse_support()) {
		if(is_sse_allow()) {
			memcpy_func = __memcpy_sse;
		} else {
			return __memcpy(dest, src, n);
		}
	} else {
		memcpy_func = __memcpy;
	}
#else
	memcpy_func = __memcpy;
#endif
	return memcpy_func(dest, src, n);
}

void *memcpy(void *dest, const void *src, size_t n) {
	return memcpy_func(dest, src, n);
}

static void* memmove_check(void* dest, const void* src, size_t len);
static void*(*memmove_func)(void*, const void*, size_t) = memmove_check;
static void* memmove_check(void* dest, const void* src, size_t len) {
#ifndef __MODE_32_BIT__
	if(is_sse_support()) {
		if(is_sse_allow()) {
			memmove_func = __memmove_sse;
		} else {
			return __memmove(dest, src, len);
		}
	} else {
		memmove_func = __memmove;
	}
#else
	memmove_func = __memmove;
#endif
	return memmove_func(dest, src, len);
}

void *memmove(void *dest, const void *src, size_t len) {
	return memmove_func(dest, src, len);
}

static int memcmp_check(const void* v1, const void* v2, size_t size);
static int(*memcmp_func)(const void*, const void*, size_t) = memcmp_check;
static int memcmp_check(const void* v1, const void* v2, size_t size) {
#ifndef __MODE_32_BIT__
	if(is_sse_support()) {
		if(is_sse_allow()) {
			memcmp_func = __memcmp_sse;
		} else {
			return __memcmp(v1, v2, size);
		}
	} else {
		memcmp_func = __memcmp;
	}
#else
	memcmp_func = __memcmp;
#endif
	return memcmp_func(v1, v2, size);
}

int memcmp(const void* v1, const void* v2, size_t size) {
	return memcmp_func(v1, v2, size);
}

void bzero(void* dest, size_t size) {
	__bzero(dest, size);
}

size_t strlen(const char* s) {
	return __strlen(s);
}

char* strstr(const char* haystack, const char* needle) {
	return __strstr(haystack, needle);
}

char* strchr(const char* s, int c) {
	return __strchr(s, c);
}

char* strrchr(const char* s, int c) {
	return __strrchr(s, c);
}

int strcmp(const char *s, const char *d) {
	return __strcmp(s, d);
}

int strncmp(const char* s, const char* d, size_t size) {
	return __strncmp(s, d, size);
}

char* strdup(const char* source) {
	return __strdup(source);
}

long int strtol(const char *nptr, char **endptr, int base) {
	return __strtol(nptr, endptr, base);
}

long long int strtoll(const char *nptr, char **endptr, int base) {
	return __strtoll(nptr, endptr, base);
}

unsigned long int strtoul(const char *nptr, char **endptr, int base) {
	return __strtoul(nptr, endptr, base);
}

unsigned long long int strtoull(const char *nptr, char **endptr, int base) {
	return __strtoull(nptr, endptr, base);
}

char* strcpy(char *dest, const char *src) {
	return __memcpy(dest, src, strlen(src) + 1);
}

char* strncpy(char *dest, const char *src, size_t len) {
	return __memcpy(dest, src, len);
}

char* strtok(char* argv, const char* delim) {
	char* ptr = strstr(argv, delim);
	if(!ptr)
		return NULL;
	memset(ptr, 0, strlen(delim));

	return argv;
}

char* strtok_r(char* argv, const char* delim, char** next) {
	char* res = strtok(argv, delim);
	if(!res) {
		*next = NULL;
		return argv;
	}

	*next = res + strlen(res) + strlen(delim);
	return res;
}
