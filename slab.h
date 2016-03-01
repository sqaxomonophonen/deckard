#ifndef SLAB_H

static inline int slab_sz_log2(size_t sz)
{
	sz--;
	int x = 1;
	while (sz >>= 1) x++;
	return x;
}

int slab_can_alloc_log2(int sz_log2);
void* slab_alloc_log2(int sz_log2);
void* slab_calloc_log2(int sz_log2);

static inline int slab_can_alloc(size_t sz)
{
	return slab_can_alloc_log2(slab_sz_log2(sz));
}

static inline void* slab_alloc(size_t sz)
{
	return slab_alloc_log2(slab_sz_log2(sz));
}

static inline void* slab_calloc(size_t sz)
{
	return slab_calloc_log2(slab_sz_log2(sz));
}

void slab_free(void* p);

#define SLAB_H
#endif
