#ifndef __UTIL_ARRAY_LIST_H__
#define __UTIL_ARRAY_LIST_H__

#include <util/list.h>

typedef struct _ArrayList ArrayList;

#define ARRAYLISTOPS_PROPS	\
	bool		(*is_available)(ArrayList* this);

typedef struct _ArrayListOps {
	ARRAYLISTOPS_PROPS
} ArrayListOps;

typedef struct _ArrayListIterContext {
	ArrayList*	list;
	size_t		index;
} ArrayListIterContext;

typedef struct _ArrayList {
	LIST_PROPS
	ARRAYLISTOPS_PROPS

	size_t		capacity;
	void**		array;
	ArrayListIterContext*	context;
} ArrayList;

ArrayList* arraylist_create(DataType type, PoolType pool, size_t initial_capacity);
void arraylist_destroy(ArrayList* this);

#endif /* __UTIL_ARRAY_LIST_H__ */
