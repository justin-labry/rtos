#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "util/types.h"

bool is_uint8(const char* val) {
	char* end = NULL;
	errno = 0;
	long int v = strtoul(val, &end, 0);

	if(end == NULL || *end != '\0') return false;
	if(v > UINT8_MAX) return false;
	if(errno == ERANGE) return false;

	return true;
}

uint8_t parse_uint8(const char* val) {
	return strtoul(val, NULL, 0);
}

bool is_uint16(const char* val) {
	char* end = NULL;
	errno = 0;
	unsigned long int v = strtoul(val, &end, 0);

	if(end == NULL || *end != '\0') return false;
	if(v > UINT16_MAX) return false;
	if(errno == ERANGE) return false;

	return true;
}

uint16_t parse_uint16(const char* val) {
	return strtoul(val, NULL, 0);
}

bool is_uint32(const char* val) {
	char* end = NULL;
	errno = 0;
	unsigned long int v = strtoul(val, &end, 0);

	if(end == NULL || *end != '\0') return false;
	if(errno == ERANGE) return false;

	return true;
}

uint32_t parse_uint32(const char* val) {
	return strtoul(val, NULL, 0);
}

bool is_uint64(const char* val) {
	char* end = NULL;
	errno = 0;
	unsigned long long int v = strtoull(val, &end, 0);

	if(end == NULL || *end != '\0') return false;
	if(errno == ERANGE) return false;
	
	return true;
}

uint64_t parse_uint64(const char* val) {
	return strtoull(val, NULL, 0);
}
