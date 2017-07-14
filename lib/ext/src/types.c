#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

#include "_string.h"
#include "util/types.h"

bool is_uint8(const char* val) {
	if(!val || val[0] == '-') return false;

	char* end = NULL;
	errno = 0;
	long int v = __strtoul(val, &end, 0);

	if(end == NULL || *end != '\0') return false;
	if(v > UINT8_MAX) return false;
	if(errno == ERANGE) return false;

	return true;
}

uint8_t parse_uint8(const char* val) {
	return __strtoul(val, NULL, 0);
}

bool is_uint16(const char* val) {
	if(!val || val[0] == '-') return false;

	char* end = NULL;
	errno = 0;
	uint32_t v = __strtoul(val, &end, 0);

	if(end == NULL || *end != '\0') return false;
	if(v > UINT16_MAX) return false;
	if(errno == ERANGE) return false;

	return true;
}

uint16_t parse_uint16(const char* val) {
	return __strtoul(val, NULL, 0);
}

bool is_uint32(const char* val) {
	if(!val || val[0] == '-') return false;

	char* end = NULL;
	errno = 0;
	__strtoul(val, &end, 0);

	if(end == NULL || *end != '\0') return false;
	if(errno == ERANGE) return false;

	return true;
}

uint32_t parse_uint32(const char* val) {
	return __strtoul(val, NULL, 0);
}

bool is_uint64(const char* val) {
	if(!val || val[0] == '-') return false;

	char* end = NULL;
	errno = 0;
	__strtoull(val, &end, 0);

	if(end == NULL || *end != '\0') return false;
	if(errno == ERANGE) return false;

	return true;
}

uint64_t parse_uint64(const char* val) {
	return __strtoull(val, NULL, 0);
}
