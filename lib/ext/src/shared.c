#include <shared.h>
#include <stdbool.h>

void** __shared;

void shared_set(void* ptr){
	*__shared = ptr;
}

void* shared_get(){
	return *__shared;
}

bool shared_table_create(size_t size) {
    if(*__shared)
        return false;

    *__shared = gmalloc(size);
    if(!*__shared)
        return false;

    return true;
}

void* shared_table_register(char* key, size_t size) {
    void* addr;
    //find key
    //addr = base + index;
    return addr;
}

void* shared_table_get(char* key) {
    void* addr;
    return addr;
}

bool shared_table_unregister(char* key) {
    return true;
}
