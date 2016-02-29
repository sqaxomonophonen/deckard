#ifndef MEM_H

void* mem_alloc(size_t sz);
void* mem_calloc(size_t sz);
void* mem_realloc(void* p, size_t sz);
void mem_free(void*);

#define MEM_H
#endif
