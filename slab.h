#ifndef SLAB_H

static inline int slab_sz_log2(size_t sz)
{
	sz--;
	int x = 1;
	while (sz >>= 1) x++;
	return x;
}

void* slab_alloc_log2(int sz_log2);
void* slab_calloc_log2(int sz_log2);
void* slab_realloc_log2(void* p, int sz_log2);

static inline void* slab_alloc(size_t sz)
{
	return slab_alloc_log2(slab_sz_log2(sz));
}

static inline void* slab_calloc(size_t sz)
{
	return slab_calloc_log2(slab_sz_log2(sz));
}

static inline void* slab_realloc(void* p, size_t sz)
{
	return slab_realloc_log2(p, slab_sz_log2(sz));
}

void slab_free(void* p);

#define SLAB_H
#endif