#ifndef __CP_H__
#define __CP_H__

#include <stdbool.h>

void _cp_load(int vmid, int try, int sleep);
void cp_load(int argc, char** argv);
void cp_dump_vm(int vmid);
int cp_vm_status(int vmid);

#endif /*__CP_H__*/
