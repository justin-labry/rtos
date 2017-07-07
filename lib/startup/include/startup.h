#ifndef __STARTUP_H__
#define __STARTUP_H__

#include <stdbool.h>

void vm_info(int vmid);
int vm_status(int vmid);

void _startup(int vmid);
void startup(int argc, char** argv);
void shutdown();

#endif /*__STARTUP_H__*/
