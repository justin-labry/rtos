#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <shared.h>
#include <stdbool.h>
#include <lock.h>

void* __shared;

typedef struct _RegisterHead {
	uint8_t volatile lock;
} RegisterHead __attribute__((__aligned__(8)));

#define register_head __shared
#define register_buffer_start (__shared + 8)
#define register_buffer_end (__shared + (64 * 1024))

void* shared_register(char* key, size_t size) {
	int len = strlen(key) + 1;
	if(len > 255) return NULL;

	uint32_t req = 1 + ((len + sizeof(uint32_t) - 1) / sizeof(uint32_t)) + (((uint32_t)size + sizeof(uint32_t) - 1) / sizeof(uint32_t)); // heder + round(key) + round(blocks)


	RegisterHead* head = register_head;
	lock_lock(&head->lock);
	// Check key is already allocated
	for(uint32_t* p = (uint32_t*)register_buffer_start; p < (uint32_t*)(register_buffer_end); ) {
		uint16_t len2 = *p >> 16;
		uint16_t count2 = *p & 0xffff;

		if(*p == 0) {
			p++;
		} else {
			if(len2 == len && strncmp(key, (const char*)(p + 1), len - 1) == 0) {
				lock_unlock(&head->lock);
				return NULL;
			}

			p += count2;
		}
	}

	// Check available space
	for(uint32_t* p = (uint32_t*)register_buffer_start; p < (uint32_t*)(register_buffer_end); ) {
		if(*p == 0) {
			bool found = true;
			for(uint32_t i = 0; i < req; i++) {
				if(p >= (uint32_t*)register_buffer_end) {
					lock_unlock(&head->lock);
					return NULL;
				}

				if(p[i] != 0) {
					p += i - 1;
					found = false;
					break;
				}
			}

			if(found) {
				*p = (uint32_t)len << 16 | req;
				memcpy(p + 1, key, len);
				lock_unlock(&head->lock);
				return p + 1 + ((len + sizeof(uint32_t) - 1) / sizeof(uint32_t));
			}

			p++;
		} else {
			p += *p & 0xffff;
		}
	}

	lock_unlock(&head->lock);
	return NULL;
}

void* shared_get(char* key) {
	int len = strlen(key) + 1;
	if(len > 255) return NULL;

	RegisterHead* head = register_head;
	lock_lock(&head->lock);
	for(uint32_t* p = (uint32_t*)register_buffer_start; p < (uint32_t*)register_buffer_end; ) {
		if(*p == 0) {
			p++;
		} else {
			uint16_t len2 = *p >> 16;
			uint16_t count2 = *p & 0xffff;

			if(len2 == len && strncmp(key, (const char*)(p + 1), len - 1) == 0) {
				lock_unlock(&head->lock);
				return p + 1 + ((len + sizeof(uint32_t) - 1) / sizeof(uint32_t));
			}

			p += count2;
		}
	}

	lock_unlock(&head->lock);
	return NULL;
}

bool shared_unregister(char* key) {
	int len = strlen(key) + 1;
	if(len > 255) return false;

	RegisterHead* head = register_head;
	lock_lock(&head->lock);
	for(uint32_t* p = (uint32_t*)register_buffer_start; p < (uint32_t*)register_buffer_end; ) {
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

				lock_unlock(&head->lock);
				return true;
			}

			p += count2;
		}
	}

	lock_unlock(&head->lock);
	return false;
}
