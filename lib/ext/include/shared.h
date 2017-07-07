#ifndef __SHARED_H__
#define __SHARED_H__
#include <stdbool.h>
#include <stdlib.h>

/**
 * @file
 * Set or get shared pointer between threads.
 */

/**
 * Register Data
 * 
 * @param key data key
 * @param size data size
 * @return data address
 */
void* shared_register(char* key, size_t size);

/**
 * Get Address
 *
 * @param key data key
 * @return data address
 */
void* shared_get(char* key);

/**
 * Unregister Data
 *
 * @param key data key
 * @return is success
 */
bool shared_unregister(char* key);

#endif /* __SHARED_H__ */
