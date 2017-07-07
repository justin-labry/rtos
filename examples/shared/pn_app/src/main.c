#include <stdio.h>
#include <string.h>
#include <thread.h>
#include <shared.h>

void ginit(int argc, char** argv) {
}

void init(int argc, char** argv) {
}

void destroy() {
}

void gdestroy() {
}

int main(int argc, char** argv) {
	printf("Thread %d booting\n", thread_id());
	if(thread_id() == 0) {
		ginit(argc, argv);
	}

	thread_barrior();

	init(argc, argv);

	thread_barrior();

	extern void* __shared;
	extern void* __gmalloc_pool;
	printf("shared: %p\n", __shared);
	printf("gmalloc: %p\n", __gmalloc_pool);

	char* data = shared_register("data", 128);
	strcpy(data, "Shared Memory Test Application\n");

	while(1);

	thread_barrior();

	destroy();

	thread_barrior();

	if(thread_id() == 0) {
		gdestroy(argc, argv);
	}

	return 0;
}
