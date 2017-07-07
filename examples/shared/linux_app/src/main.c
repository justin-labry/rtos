#include <stdio.h>
#include <startup.h>
#include <shared.h>

int main(int argc, char** argv) {
	printf("PacketNgin Control Plane EXample Application\n");

	startup(argc, argv);
	extern void* __shared;
	extern void* __gmalloc_pool;
	printf("%p\n", __shared);
	printf("%p\n", __gmalloc_pool);

	char* data = shared_get("data");
	printf("%s\n", data);

	return 0;
}
