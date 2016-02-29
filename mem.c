#include <stdlib.h>

#include "mem.h"
#include "a.h"

void* mem_alloc(size_t sz)
{
	void* p = malloc(sz);
	AN(p);
	return p;
}

void* mem_calloc(size_t sz)
{
	void* p = calloc(1, sz);
	AN(p);
	return p;
}

void* mem_realloc(void* p, size_t sz)
{
	p = realloc(p, sz);
	AN(p);
	return p;
}

void mem_free(void* p)
{
	free(p);
}
