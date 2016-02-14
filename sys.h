#ifndef SYS_H

#include <stddef.h>

struct sys_mmap_file {
	void* ptr;
	size_t sz;
};

int sys_mmap_file_ro(struct sys_mmap_file*, const char* path);
void sys_munmap_file(struct sys_mmap_file*);

#define SYS_H
#endif
