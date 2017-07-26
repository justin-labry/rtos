#ifndef __UTIL_SET_H__
#define __UTIL_SET_H__

#include <util/collection.h>
#include <util/linkedlist.h>

typedef struct _SetEntry {
	void* data;
} SetEntry;

#define SET_PROPS	\
	COLLECTION_PROPS	\
	size_t		capacity;

typedef struct _Set {
	SET_PROPS
} Set;

Set* set_create(DataType type, PoolType pool, size_t size);
void set_destroy(Set* this);

#endif /* __UTIL_SET_H__ */
