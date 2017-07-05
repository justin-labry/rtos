#include <stdio.h>
#include <pncp.h>

//Must set vmid
int vmid = 1;

int main(int argc, char** argv) {
	printf("PacketNgin Control Plane EXample Application\n");

	cp_load(argc, argv);
	extern void* __shared;
	extern void* __gmalloc_pool;
	printf("%p\n", __shared);
	printf("%p\n", __gmalloc_pool);
	printf("%s\n", (char*)__shared);

	return 0;
}
