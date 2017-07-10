#ifndef ___STRING_H__
#define ___STRING_H__

#include <stddef.h>

void *__memset(void *s, int c, size_t n);
void *__memset_sse(void *dst, int value, size_t len);
void *__memcpy(void *dest, const void *src, size_t n);
void *__memcpy_sse(void *dest, const void *src, size_t n);
void * __memmove(void *dest, const void *src, size_t len );
void * __memmove_sse(void *dest, const void *src, size_t len );
int __memcmp(const void* v1, const void* v2, size_t size);
int __memcmp_sse(const void* v1, const void* v2, size_t size);
void __bzero(void* dest, size_t size);
size_t __strlen(const char* s);
char* __strstr(const char* haystack, const char* needle);
char* __strchr(const char* s, int c);
char* __strrchr(const char* s, int c);
int __strcmp(const char *s, const char *d);
int __strncmp(const char* s, const char* d, size_t size);
char* __strdup(const char* source);
long int __strtol(const char *nptr, char **endptr, int base);
long long int __strtoll(const char *nptr, char **endptr, int base);
long int __strtoul(const char *nptr, char **endptr, int base);
long long int __strtoull(const char *nptr, char **endptr, int base);

#endif /* ___STRING_H__ */
