#ifndef __UTIL_LINKED_LIST_H__
#define __UTIL_LINKED_LIST_H__

#include <util/list.h>
#include <util/deque.h>

typedef struct _ListNode ListNode;
typedef struct _ListNode {
	ListNode*	prev;
	ListNode*	next;
	void*		data;
} ListNode;

typedef struct _LinkedList LinkedList;

#define LINKEDLISTOPTS_PROPS	\
	void		(*rotate)(LinkedList* this);

typedef struct _LinkedListOps {
	LINKEDLISTOPTS_PROPS
} LinkedListOps;

typedef struct _LinkedListIterContext {
	LinkedList*	list;
	ListNode*	prev;
	ListNode*	node;
} LinkedListIterContext;

typedef struct _LinkedList {
	LIST_PROPS
	DEQUEOPS_PROPS
	LINKEDLISTOPTS_PROPS

	ListNode*	head;
	ListNode*	tail;
	LinkedListIterContext*	context;
} LinkedList;

LinkedList* linkedlist_create(DataType type, PoolType pool);
void linkedlist_destroy(LinkedList* this);

#endif /* __UTIL_LINKED_LIST_H__ */
