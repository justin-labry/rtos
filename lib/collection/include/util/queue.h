#ifndef __UTIL_QUEUE_H__
#define __UTIL_QUEUE_H__

#include <util/collection.h>

typedef struct _Queue Queue;

#define QUEUEOPS_PROPS	\
	bool	(*enqueue)(void* this, void* element);	\
	void*	(*dequeue)(void* this);	\
	void*	(*get)(void* this, int index);	\
	void*	(*peek)(void* this);

typedef struct _QueOps {
	QUEUEOPS_PROPS
} QueueOps;

#define QUEUE_PROPS	\
	COLLECTION_PROPS	\
	QUEUEOPS_PROPS

typedef struct _Queue {
	QUEUE_PROPS
} Queue;

Queue* queue_create(DataType type, PoolType pool, size_t size);
void queue_destroy(Queue* this);

#endif /* __UTIL_QUEUE_H__ */
