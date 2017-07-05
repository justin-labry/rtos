#include <stdio.h>
#include <string.h>
#include <tlsf.h>
#include <_malloc.h>
#include <util/list.h>
#include <elf.h>
#include <errno.h>
#include <timer.h>
#include <fio.h>
#include <file.h>
#include "page.h"
//#include "vfio.h"
#include "task.h"
#include "mp.h"

#include "loader.h"

#define SHARED_SIZE         64 * 1024   //64KBytes
typedef struct {
	uint8_t		barrior_lock;
	uint32_t	barrior;
	uint8_t		shared[64 * 1024];
} SharedBlock;

static bool check_header(void* addr);
static uint32_t load(VM* vm, void** malloc_pool, void** gmalloc_pool);
static bool load_args(VM* vm, void* malloc_pool, uint32_t task_id);
static void load_symbols(VM* vm, uint32_t task_id);
static bool relocate(VM* vm, void* malloc_pool, void* gmalloc_pool, uint32_t task_id);

// TODO: Change void* addr to Block*
uint32_t loader_load(VM* vm) {
	errno = 0;

	// TODO: map storage to memory
	if(!check_header(vm->storage.blocks[0]))
		return (uint32_t)-1;

	void* malloc_pool = NULL;
	void* gmalloc_pool = NULL;
	uint32_t id = load(vm, &malloc_pool, &gmalloc_pool);
	if(id == (uint32_t)-1)
		return (uint32_t)-2;

	load_symbols(vm, id);

	if(!relocate(vm, malloc_pool, gmalloc_pool, id))
		return (uint32_t)-3;

	if(!load_args(vm, malloc_pool, id))
		printf("Loader: WARN: Cannot load argc and argv\n");

	return id;
}

static bool check_header(void* addr) {
	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)addr;

	if(memcmp(ehdr->e_ident, ELFMAG, 4) != 0) {
		errno = 0x11;	// Illegal ELF64 magic
		return false;
	}

	if(ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
		errno = 0x12;	// Not supported ELF type
		return false;
	}

	if(ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
		errno = 0x13;	// Not supported ELF data
		return false;
	}

	if(ehdr->e_ident[EI_VERSION] != 1) {
		errno = 0x14;	// Not supported ELF version
		return false;
	}

	if(ehdr->e_ident[EI_OSABI] != ELFOSABI_SYSV) {
		errno = 0x15;	// Not supported ELF O/S ABI
		return false;
	}

	if(ehdr->e_ident[EI_ABIVERSION] != 0) {
		errno = 0x16;	// Not supported ELF ABI version
		return false;
	}

	if(ehdr->e_type != ET_EXEC) {
		errno = 0x17;	// Illegal ELF type
		return false;
	}

	return true;
}

static uint32_t load(VM* vm, void** malloc_pool, void** gmalloc_pool) {
	int thread_id = 0;
	for(int i = 0; i < MP_MAX_CORE_COUNT; i++) {
		if(vm->cores[i] == mp_apic_id()) {
			thread_id = i;
			break;
		}
	}

	int thread_count = vm->core_size;

	uint32_t id = task_create();

	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)vm->storage.blocks[0];
	Elf64_Phdr* phdr = (Elf64_Phdr*)(vm->storage.blocks[0] + ehdr->e_phoff);

	uint32_t code_blocks = 0;
	uint32_t data_blocks = 0;
	uint32_t stack_blocks = 1;	// TODO: make it configurable

	// Analyze
	for(uint16_t i = 0; i < ehdr->e_phnum; i++) {
		if(phdr[i].p_type == PT_LOAD) {
			uint64_t size = (phdr[i].p_vaddr + phdr[i].p_memsz - (phdr[i].p_vaddr & ~(0x200000 - 1)) + (0x200000 - 1)) / 0x200000;

			if(phdr[i].p_flags & PF_W) {
				data_blocks += size;
			} else {
				code_blocks += size;
			}
		}
	}

	// Check memory size
	if(vm->memory.count < code_blocks + (data_blocks + stack_blocks) * thread_count) {
		errno = 0x21;	// Not enough memory to allocate
		goto failed;
	}

	uint64_t idx = 0;
	uint32_t count = 0;

	// Load
	for(uint16_t i = 0; i < ehdr->e_phnum; i++) {
		if(phdr[i].p_type == PT_LOAD) {
			uint64_t vaddr = phdr[i].p_vaddr & ~(0x200000 - 1);
			uint64_t size = (phdr[i].p_vaddr + phdr[i].p_memsz - (phdr[i].p_vaddr & ~(0x200000 - 1)) + (0x200000 - 1)) / 0x200000;

			if(thread_id == 0) {
				for(idx = (vaddr >> 21); idx < ((vaddr >> 21) + size); idx++) {
					void* paddr = vm->memory.blocks[count++];
					task_mmap(id, idx << 21, (uint64_t)paddr, true, phdr[i].p_flags & PF_W, phdr[i].p_flags & PF_X, phdr[i].p_flags & PF_W ? "Data" : "Code");
				}

				task_refresh_mmap();

				memcpy((void*)phdr[i].p_vaddr, vm->storage.blocks[0] + phdr[i].p_offset, phdr[i].p_filesz);

				// malloc block
				if(phdr[i].p_flags & PF_W) {
					uint64_t vend = (phdr[i].p_vaddr + phdr[i].p_memsz + 15) & ~15;
					uint64_t pend = vaddr + size * 0x200000;

					if(vend + 4096 < pend) {
						if(*malloc_pool == NULL) {
							*malloc_pool = (void*)vend;
							init_memory_pool(pend - vend, *malloc_pool, 0);
						} else {
							add_new_area((void*)vend, pend - vend, *malloc_pool);
						}
					}
				}
			} else {
				for(idx = (vaddr >> 21); idx < ((vaddr >> 21) + size); idx++) {
					void* paddr = vm->memory.blocks[(phdr[i].p_flags & PF_W) ? (data_blocks + stack_blocks) * thread_id + count++ : count++];
					task_mmap(id, idx << 21, (uint64_t)paddr, true, phdr[i].p_flags & PF_W, phdr[i].p_flags & PF_X, phdr[i].p_flags & PF_W ? "Data" : "Code");
				}

				task_refresh_mmap();

				if(phdr[i].p_flags & PF_W) {
					memcpy((void*)phdr[i].p_vaddr, vm->storage.blocks[0] + phdr[i].p_offset, phdr[i].p_filesz);
				}

				// malloc block
				if(phdr[i].p_flags & PF_W) {
					uint64_t vend = (phdr[i].p_vaddr + phdr[i].p_memsz + 15) & ~15;
					uint64_t pend = vaddr + size * 0x200000;

					if(vend + 4096 < pend) {
						if(*malloc_pool == NULL) {
							*malloc_pool = (void*)vend;
							init_memory_pool(pend - vend, *malloc_pool, 0);
						} else {
							add_new_area((void*)vend, pend - vend, *malloc_pool);
						}
					}
				}
			}
		}
	}

	// Stack
	// TODO: Set stack size to VM
	uint32_t stack_size = 0x200000;
	idx++;

	while(stack_size > 0) {
		void* paddr = vm->memory.blocks[thread_id == 0 ? count++ : (data_blocks + stack_blocks) * thread_id + count++];
		task_mmap(id, idx << 21, (uint64_t)paddr, true, true, false, "Stack");

		idx++;
		stack_size -= 0x200000;
	}

	task_stack(id, idx << 21);

	// Global heap
	count = code_blocks + (data_blocks + stack_blocks) * thread_count;
	if(count < vm->memory.count) {
		idx++;

		uint64_t vaddr = idx << 21;
		uint64_t size = (vm->memory.count - count) * 0x200000;

		while(count < vm->memory.count) {
			void* paddr = vm->memory.blocks[count++];
			task_mmap(id, idx << 21, (uint64_t)paddr, true, true, false, "Global Heap");

			idx++;
		}

		task_refresh_mmap();

		*gmalloc_pool = (void*)vaddr + sizeof(SharedBlock);
		if(thread_id == 0) {
			bzero((void*)vaddr, sizeof(SharedBlock));
			init_memory_pool(size - sizeof(SharedBlock), *gmalloc_pool, 1);
		}
	} else {
		task_refresh_mmap();
	}

	// entry
	task_entry(id, ehdr->e_entry);

	return id;

failed:
	if(id != (uint32_t)-1)
		task_destroy(id);

	return (uint32_t)-1;
}

static bool load_args(VM* vm, void* malloc_pool, uint32_t task_id) {
	// TODO: Map storage to memory
	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)vm->storage.blocks[0];
	Elf64_Phdr* phdr = (Elf64_Phdr*)(vm->storage.blocks[0] + ehdr->e_phoff);

	size_t len = sizeof(char*) * vm->argc;
	for(int i = 0; i < vm->argc; i++)
		len += strlen(vm->argv[i]) + 1;

	void* vaddr = NULL;

	// First try: Find read only area and insert it
	for(uint16_t i = 0; i < ehdr->e_phnum; i++) {
		if(phdr[i].p_type == PT_LOAD && !(phdr[i].p_flags & PF_W)) {
			// Check tail first
			vaddr = (void*)(phdr[i].p_vaddr + phdr[i].p_memsz);
			uint64_t size = (((uint64_t)vaddr + (0x200000 - 1)) & ~(0x200000 - 1)) - (uint64_t)vaddr;

			if(size >= len) {
				goto dump;
			}

			// Check head next
			vaddr = (void*)(phdr[i].p_vaddr & ~(0x200000 - 1));
			size = phdr[i].p_vaddr - (uint64_t)vaddr;

			if(size >= len) {
				goto dump;
			}
		}
	}

	// Second try: Push to malloc area
	if(malloc_pool) {
		vaddr = __malloc(len, malloc_pool);
		goto dump;
	}

	errno = 0x41;	// Not enough memory to allocate argv
	return false;

dump:
	;
	char** argv2 = vaddr;
	vaddr += sizeof(char*) * vm->argc;

	for(int i = 0; i < vm->argc; i++) {
		int len = strlen(vm->argv[i]) + 1;
		memcpy((void*)vaddr, vm->argv[i], len);
		argv2[i] = vaddr;
		vaddr += len;
	}

	task_arguments(task_id, vm->argc, (uint64_t)argv2);

	return true;
}

static void load_symbols(VM* vm, uint32_t task_id) {
	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)vm->storage.blocks[0];
	Elf64_Shdr* shdr = (Elf64_Shdr*)(vm->storage.blocks[0] + ehdr->e_shoff);
	Elf64_Half strndx = ehdr->e_shstrndx;

	char* shname(Elf64_Word offset) {
		return vm->storage.blocks[0] + shdr[strndx].sh_offset + offset;
	}

	int get_index(char* name) {
		for(int i = 0; i < SYM_END; i++) {
			if(strcmp(task_symbols[i], name) == 0)
				return i;
		}

		return -1;
	}

	char* strings = NULL;
	Elf64_Sym* symbols = NULL;
	uint32_t symbols_size = 0;
	// Find symbol header
	for(uint16_t i = 0; i < ehdr->e_shnum; i++) {
		if(shdr[i].sh_type == SHT_SYMTAB) {
			symbols = vm->storage.blocks[0] + shdr[i].sh_offset;
			symbols_size = shdr[i].sh_size / shdr[i].sh_entsize;
		} else if(shdr[i].sh_type == SHT_STRTAB && strcmp(shname(shdr[i].sh_name), ".strtab") == 0) {
			strings = vm->storage.blocks[0] + shdr[i].sh_offset;
		}
	}

	if(symbols) {
		for(uint32_t i = 0; i < symbols_size; i++) {
			int index = get_index(strings + symbols[i].st_name);
			if(index >= 0) {
				task_symbol(task_id, index, symbols[i].st_value);
			}
		}
	}
}

static bool relocate(VM* vm, void* malloc_pool, void* gmalloc_pool, uint32_t task_id) {
	int thread_id = 0;
	for(int i = 0; i < MP_MAX_CORE_COUNT; i++) {
		if(vm->cores[i] == mp_apic_id()) {
			thread_id = i;
			break;
		}
	}

	int thread_count = vm->core_size;

	SharedBlock* shared_block = (void*)((uint64_t)gmalloc_pool & ~(0x200000L - 1));

	if(task_addr(task_id, SYM_MALLOC_POOL)) {
		*(uint64_t*)task_addr(task_id, SYM_MALLOC_POOL) = (uint64_t)malloc_pool;

		void* __stdin = __malloc(4096, malloc_pool);
		if(__stdin) {
			*(uint64_t*)task_addr(task_id, SYM_STDIN) = (uint64_t)__stdin;
			*(size_t*)task_addr(task_id, SYM_STDIN_SIZE) = 4096;
		} else {
			errno = 0x31;
			return false;
		}

		void* __stdout = __malloc(4096, malloc_pool);
		if(__stdout) {
			*(uint64_t*)task_addr(task_id, SYM_STDOUT) = (uint64_t)__stdout;
			*(size_t*)task_addr(task_id, SYM_STDOUT_SIZE) = 4096;
		} else {
			errno = 0x32;
			return false;
		}

		void* __stderr = __malloc(4096, malloc_pool);
		if(__stderr) {
			*(uint64_t*)task_addr(task_id, SYM_STDERR) = (uint64_t)__stderr;
			*(size_t*)task_addr(task_id, SYM_STDERR_SIZE) = 4096;
		} else {
			errno = 0x33;
			return false;
		}

		// TODO: implement FIO
		// FIO allocation : Only one FIO is needed in a VM
/*
 *                FIO* user_fio;
 *                if(!vm->fio) {
 *                        user_fio = fio_create(gmalloc_pool);
 *
 *                        vm->fio = __malloc(sizeof(VFIO), gmalloc_pool);
 *                        vm->fio->input_buffer = __malloc(sizeof(FIFO), gmalloc_pool);
 *                        vm->fio->output_buffer = __malloc(sizeof(FIFO), gmalloc_pool);
 *
 *                        vm->fio->input_buffer->head = 0;
 *                        vm->fio->input_buffer->tail = 0;
 *                        vm->fio->input_buffer->size = FIO_INPUT_BUFFER_SIZE;
 *                        vm->fio->input_buffer->array = (void**)TRANSLATE_TO_PHYSICAL((uint64_t)user_fio->input_buffer->array);
 *
 *                        vm->fio->output_buffer->head = 0;
 *                        vm->fio->output_buffer->tail = 0;
 *                        vm->fio->output_buffer->size = FIO_OUTPUT_BUFFER_SIZE;
 *                        vm->fio->output_buffer->array = (void**)TRANSLATE_TO_PHYSICAL((uint64_t)user_fio->output_buffer->array);
 *
 *                        vm->fio->input_addr = (FIFO*)TRANSLATE_TO_PHYSICAL((uint64_t)user_fio->input_buffer);
 *                        vm->fio->output_addr = (FIFO*)TRANSLATE_TO_PHYSICAL((uint64_t)user_fio->output_buffer);
 *                        vm->fio->input_buffer = (FIFO*)TRANSLATE_TO_PHYSICAL((uint64_t)vm->fio->input_buffer);
 *                        vm->fio->output_buffer = (FIFO*)TRANSLATE_TO_PHYSICAL((uint64_t)vm->fio->output_buffer);
 *                        vm->fio->user_fio = (FIO*)TRANSLATE_TO_PHYSICAL((uint64_t)user_fio);
 *                        vm->fio = (VFIO*)TRANSLATE_TO_PHYSICAL((uint64_t)vm->fio);
 *                }
 *
 *                if(user_fio) {
 *                        *(uint64_t*)task_addr(task_id, SYM_FIO) = (uint64_t)user_fio;
 *                } else {
 *                        errno = 0x31;
 *                        return false;
 *                }
 */
	}

	if(task_addr(task_id, SYM_GMALLOC_POOL)) {
		*(uint64_t*)task_addr(task_id, SYM_GMALLOC_POOL) = (uint64_t)gmalloc_pool;
	}

	if(task_addr(task_id, SYM_THREAD_ID)) {
		*(int*)task_addr(task_id, SYM_THREAD_ID) = thread_id;
		*(int*)task_addr(task_id, SYM_THREAD_COUNT) = thread_count;
	}

	if(task_addr(task_id, SYM_BARRIOR)) {
		*(uint8_t**)task_addr(task_id, SYM_BARRIOR_LOCK) = &shared_block->barrior_lock;
		*(uint32_t**)task_addr(task_id, SYM_BARRIOR) = &shared_block->barrior;
	}

	if(task_addr(task_id, SYM_SHARED)) {
		*(uint64_t*)task_addr(task_id, SYM_SHARED) = (uint64_t)shared_block->shared;
	}

	if(task_addr(task_id, SYM_TIMER_FREQUENCY)) {
		*(uint64_t*)task_addr(task_id, SYM_TIMER_FREQUENCY) = TIMER_FREQUENCY_PER_SEC;
	}

	if(task_addr(task_id, SYM_TIMER_MS)) {
		*(uint64_t*)task_addr(task_id, SYM_TIMER_MS) = __timer_ms;
	}

	if(task_addr(task_id, SYM_TIMER_US)) {
		*(uint64_t*)task_addr(task_id, SYM_TIMER_US) = __timer_us;
	}

	if(task_addr(task_id, SYM_TIMER_NS)) {
		*(uint64_t*)task_addr(task_id, SYM_TIMER_NS) = __timer_ns;
	}

	return true;
}
