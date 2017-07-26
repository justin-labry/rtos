#ifndef __UTIL_CACHE_H__
#define __UTIL_CACHE_H__

#include <util/hashmap.h>
#include <util/linkedlist.h>

typedef struct _LinkedListIterContext CacheIterContext;

typedef struct _Cache {
	MAP_PROPS

	size_t			capacity;
	HashMap*		map;
	LinkedList*		list;

	Iterator*		iter;
	CacheIterContext*	context;
} Cache;

Cache* cache_create(DataType type, PoolType pool, size_t initial_capacity);
void cache_destroy(Cache* this);

#endif /* __UTIL_CACHE_H__ */
