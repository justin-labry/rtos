#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <_malloc.h>

extern void* __malloc_pool;

/**
 * @file
 * Memory allocator wrapper
 *
 * Note1
 * these functions should be compiled as an weak symbol
 * to use the same interface on packetngin and linux
 *
 * Note2
 * If you are linking libext with a Linux application,
 * the `-lc` option must be at the front of the library list
 */

void* __attribute__((weak)) malloc(size_t size) {
	return __malloc(size, __malloc_pool);
}

void __attribute__((weak)) free(void *ptr) {
	__free(ptr, __malloc_pool);
}

void* __attribute__((weak)) realloc(void *ptr, size_t new_size) {
	return __realloc(ptr, new_size, __malloc_pool);
}

void* __attribute__((weak)) calloc(size_t nelem, size_t elem_size) {
	return __calloc(nelem, elem_size, __malloc_pool);
}

