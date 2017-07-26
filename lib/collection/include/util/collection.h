#ifndef __UTIL_COLLECTION_H__
#define __UTIL_COLLECTION_H__

#include <util/base.h>

typedef struct _Iterator Iterator;
typedef struct _Iterator {
	bool		(*init)(void* context, void* collection);
	bool		(*has_next)(void* context);
	void*		(*next)(void* context);
	void*		(*remove)(void* context);
} Iterator;

typedef struct _Collection Collection;

#define COLLECTIONOPS_PROPS	\
	bool		(*is_empty)(void* this);	\
	bool		(*contains)(void* this, void* element);	\
	bool		(*add)(void* this, void* element);	\
	bool		(*remove)(void* this, void* element);

typedef struct _CollectionOps {
	COLLECTIONOPS_PROPS
} CollectionOps;

#define COLLECTION_PROPS	\
	BASE_PROPS	\
	COLLECTIONOPS_PROPS	\
	size_t		size;	\
	Iterator*	iter;

typedef struct _Collection {
	COLLECTION_PROPS
} Collection;

Collection* collection_create(DataType type, PoolType pool, size_t size);

#define CONTEXT_INIT(collection)					\
	({								\
		typeof(*(collection)->context) context;			\
		&context;						\
	})

#define ITER_INIT(element, collection, context)				\
	({								\
		(collection)->iter->init((context), (collection));	\
		if((collection)->iter->has_next(context)) 		\
			(element) = (collection)->iter->next((context));\
		else							\
			(element) = NULL;				\
		(collection)->iter;					\
	})

/*
 * Nested loop is required to define loop scope variables such as 'context'
 * and 'iter' at the same time. Another macros CONTEXT_INIT and ITER_INIT
 * are also required to return expression value during initializing local
 * variable. Ref: https://gcc.gnu.org/onlinedocs/gcc/Statement-Exprs.html
 **/
#define for_each(element, collection)						\
	for(typeof((collection)->context) context				\
			= CONTEXT_INIT((collection));				\
		context != NULL; context = NULL)				\
	for(Iterator* iter = ITER_INIT((element), (collection), context);	\
		(element);							\
		(element) = iter->has_next(context) ?				\
		iter->next(context): NULL)

#endif /* __UTIL_COLLECTION_H__ */
