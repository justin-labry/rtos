#ifndef __UTIL_HASH_SET_H__
#define __UTIL_HASH_SET_H__

#include <util/set.h>
#include <util/hashmap.h>

#define HASHSETOPS_PROPS	\
	void*	(*get)(void* this, void* key);

typedef struct _HashSetOps {
	HASHSETOPS_PROPS
} HashSetOps;

typedef struct _HashSetIterContext {
	HashMap*	_map;
	MapIterContext	_context;
} HashSetIterContext;

typedef struct _HashSet {
	SET_PROPS
	HASHSETOPS_PROPS

	HashMap*		map;
	HashSetIterContext*	context;
} HashSet;

HashSet* hashset_create(DataType type, PoolType pool, size_t initial_capacity);
void hashset_destroy(HashSet* this);

#endif /* __UTIL_HASH_SET_H__ */
