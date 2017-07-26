#ifndef __UTIL_List_H__
#define __UTIL_List_H__

#include <util/collection.h>

typedef struct _List List;

#define LISTOPS_PROPS	\
	void*	(*get)(void* this, size_t index);	\
	void*	(*set)(void* this, size_t index, void* element);	\
	void*	(*remove_at)(void* this, size_t index);	\
	bool	(*add_at)(void* this, size_t index, void* element);	\
	int	(*index_of)(void* this, void* element);

typedef struct _ListOps {
	LISTOPS_PROPS
} ListOps;

#define LIST_PROPS	\
	COLLECTION_PROPS	\
	LISTOPS_PROPS

typedef struct _List {
	LIST_PROPS
} List;

List* list_create(DataType type, PoolType pool, size_t size);
void list_destroy(List* this);

#endif /* __UTIL_List_H__ */
