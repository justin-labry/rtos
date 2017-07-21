#include <stdio.h>
#include <string.h>
#include <util/event.h>
#include <util/fifo.h>
#include <lock.h>
#include <_malloc.h>
#include <timer.h>
#include "asm.h"
#include "apic.h"
#include "mp.h"
#include "mmap.h"
#include "page.h"
#include "shared.h"
#include "gmalloc.h"
#include "task.h"

#include "icc.h"

static uint32_t icc_id;

#define ICC_EVENTS_COUNT	64
typedef void (*ICC_Handler)(ICC_Message*);
static ICC_Handler icc_events[ICC_EVENTS_COUNT];

static bool icc_event(void* context) {
	uint8_t apic_id = mp_apic_id();
	Shared* shared = (Shared*)SHARED_ADDR;
	FIFO* icc_queue = shared->icc_queues[apic_id].icc_queue;
	if(!icc_queue) return true;

	if(fifo_empty(icc_queue)) return true;

	lock_lock(&shared->icc_queues[apic_id].icc_queue_lock);
	ICC_Message* icc_msg = fifo_pop(icc_queue);
	lock_unlock(&shared->icc_queues[apic_id].icc_queue_lock);

	if(!icc_msg) return true;

	//printf("icc_event() %d %d\n", icc_msg->type, task_id());
	if(icc_msg->type >= ICC_EVENTS_COUNT) {
		icc_free(icc_msg);
		return true;
	}

	if(!icc_events[icc_msg->type]) {
		icc_free(icc_msg);
		return true;
	}

	icc_events[icc_msg->type](icc_msg); //event call

	return true;
}

static void icc(uint64_t vector, uint64_t err) {
	uint8_t apic_id = mp_apic_id();
	Shared* shared = (Shared*)SHARED_ADDR;
	FIFO* icc_queue = shared->icc_queues[apic_id].icc_queue;
	ICC_Message* icc_msg = fifo_peek(icc_queue, 0);

	apic_eoi();

	if(icc_msg == NULL) return;
	//printf("icc() %d %d\n", icc_msg->type, task_id());

	if(task_id()) { //user context
		icc_event(NULL);
	}
}

int icc_init() {
	uint8_t processor_count = mp_processor_count();
	extern void* gmalloc_pool;
	uint8_t apic_id = mp_apic_id();
	Shared* shared = (Shared*)SHARED_ADDR;

	if(apic_id == 0) {
		int icc_max = processor_count * processor_count;
		shared->icc_pool = fifo_create(icc_max, gmalloc_pool);

		for(int i = 0; i < icc_max; i++) {
			ICC_Message* icc_message = __malloc(sizeof(ICC_Message), shared->icc_pool->pool);
			fifo_push(shared->icc_pool, icc_message);
		}

		shared->icc_queues = __malloc(MP_MAX_CORE_COUNT * sizeof(ICC), gmalloc_pool);

		lock_init(&shared->icc_lock_alloc);

		uint8_t* core_map = mp_processor_map();
		for(int i = 0; i < MP_MAX_CORE_COUNT; i++) {
			if(core_map[i] != MP_CORE_INVALID) {
				shared->icc_queues[i].icc_queue = fifo_create(processor_count, gmalloc_pool);
				lock_init(&shared->icc_queues[i].icc_queue_lock);
			}
		}
	}

	event_busy_add(icc_event, NULL);
	apic_register(48, icc);

	return 0;
}

ICC_Message* icc_alloc(uint8_t type) {
	Shared* shared = (Shared*)SHARED_ADDR;

	lock_lock(&shared->icc_lock_alloc);
	ICC_Message* icc_message = fifo_pop(shared->icc_pool);
	lock_unlock(&shared->icc_lock_alloc);

	icc_message->id = icc_id++;
	icc_message->type = type;
	icc_message->apic_id = mp_apic_id();
	icc_message->result = 0;

	return icc_message;
}

void icc_free(ICC_Message* msg) {
	Shared* shared = (Shared*)SHARED_ADDR;

	lock_lock(&shared->icc_lock_free);
	fifo_push(shared->icc_pool, msg);
	lock_unlock(&shared->icc_lock_free);
}

uint32_t icc_send(ICC_Message* msg, uint8_t apic_id) {
	Shared* shared = (Shared*)SHARED_ADDR;
	uint32_t _icc_id = msg->id;

	lock_lock(&shared->icc_queues[apic_id].icc_queue_lock);
	fifo_push(shared->icc_queues[apic_id].icc_queue, msg);
	lock_unlock(&shared->icc_queues[apic_id].icc_queue_lock);

	apic_write64(APIC_REG_ICR, ((uint64_t)(apic_id) << 56) |
			APIC_DSH_NONE | 
			APIC_TM_EDGE | 
			APIC_LV_DEASSERT | 
			APIC_DM_PHYSICAL | 
			APIC_DMODE_FIXED |
			(msg->type == ICC_TYPE_PAUSE ? 49 : 48));

	// TODO: Remove wait
	timer_mwait(100);

	return _icc_id;
}

void icc_register(uint8_t type, void(*event)(ICC_Message*)) {
	icc_events[type] = event;
}
