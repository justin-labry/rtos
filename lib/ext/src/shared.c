#include <stdint.h>
#include <string.h>
#include <shared.h>
#include <stdbool.h>

void* __shared;

#define __shared_tail (__shared + (64 * 1024))

void* shared_register(char* key, size_t size) {
	int len = strlen(key) + 1;
	if(len > 255) return NULL;

	uint32_t req = 1 + ((len + sizeof(uint32_t) - 1) / sizeof(uint32_t)) + (((uint32_t)size + sizeof(uint32_t) - 1) / sizeof(uint32_t)); // heder + round(key) + round(blocks)

	// Check key is already allocated
	for(uint32_t* p = (uint32_t*)__shared; p < (uint32_t*)(__shared_tail); ) {
		uint16_t len2 = *p >> 16;
		uint16_t count2 = *p & 0xffff;

		if(*p == 0) {
			p++;
		} else {
			if(len2 == len && strncmp(key, (const char*)(p + 1), len - 1) == 0) return NULL;

			p += count2;
		}
	}

	// Check available space
	for(uint32_t* p = (uint32_t*)__shared; p < (uint32_t*)(__shared_tail); ) {
		if(*p == 0) {
			bool found = true;
			for(uint32_t i = 0; i < req; i++) {
				if(p >= (uint32_t*)__shared_tail) return NULL;

				if(p[i] != 0) {
					p += i - 1;
					found = false;
					break;
				}
			}

			if(found) {
				*p = (uint32_t)len << 16 | req;
				memcpy(p + 1, key, len);
				return p + 1 + ((len + sizeof(uint32_t) - 1) / sizeof(uint32_t));
			}

			p++;
		} else {
			p += *p & 0xffff;
		}
	}

	return NULL;
}

void* shared_get(char* key) {
	int len = strlen(key) + 1;
	if(len > 255)
		return NULL;

	for(uint32_t* p = (uint32_t*)__shared; p < (uint32_t*)__shared_tail; ) {
		if(*p == 0) {
			p++;
		} else {
			uint16_t len2 = *p >> 16;
			uint16_t count2 = *p & 0xffff;

			if(len2 == len && strncmp(key, (const char*)(p + 1), len - 1) == 0) {
				return p + 1 + ((len + sizeof(uint32_t) - 1) / sizeof(uint32_t));
			}

			p += count2;
		}
	}

	return NULL;
}

bool shared_unregister(char* key) {
	int len = strlen(key) + 1;
	if(len > 255)
		return false;

	for(uint32_t* p = (uint32_t*)__shared; p < (uint32_t*)__shared_tail; ) {
		if(*p == 0) {
			p++;
		} else {
			uint16_t len2 = *p >> 16;
			uint16_t count2 = *p & 0xffff;

			if(len2 == len && strncmp(key, (const char*)(p + 1), len - 1) == 0) {
				uint16_t count = (uint16_t)*p & 0xffff;

				for(int i = count - 1; i >= 0; i--) {
					p[i] = 0;
				}
				return true;
			}

			p += count2;
		}
	}

	return false;
}
