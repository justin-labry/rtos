#ifndef __SHARED_H__
#define __SHARED_H__
#include <stdbool.h>
#include <stdlib.h>

/**
 * @file
 * Set or get shared pointer between threads.
 */

/**
 * Set shared pointer
 *
 * @param ptr the pointer from global memory area
 *
 * @see gmalloc.h
 */
void shared_set(void* ptr);

/**
 * Get shared pointer
 *
 * @return shared pointer or NULL if it's not setted yet
 */
void* shared_get();

/**
 * Init Shared Table
 *
 * @param shared table size
 * @return is success
 */
bool shared_table_create(size_t size);

/**
 * Register Data
 * 
 * @param key data key
 * @param size data size
 * @return data address
 */
void* shared_table_register(char* key, size_t size);

/**
 * Get Address
 *
 * @param key data key
 * @return data address
 */
void* shared_table_get(char* key);

/**
 * Unregister Data
 *
 * @param key data key
 * @return is success
 */
bool shared_table_unregister(char* key);

#endif /* __SHARED_H__ */
