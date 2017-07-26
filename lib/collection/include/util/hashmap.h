#ifndef __UTIL_HASH_MAP_H__
#define __UTIL_HASH_MAP_H__

#include <util/map.h>
#include <util/linkedlist.h>

typedef struct _HashMap {
	MAP_PROPS

	LinkedList**		table;
	size_t			capacity;
	size_t			threshold;
} HashMap;

HashMap* hashmap_create(DataType type, PoolType pool, size_t initial_capacity);
void hashmap_destroy(HashMap* this);

#endif /* __UTIL_HASH_MAP_H__ */
