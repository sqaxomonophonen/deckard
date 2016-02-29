#ifndef SCRATCH_H

#include <stdlib.h>
#include <string.h>

#include "a.h"
#include "log.h"

struct scratch {
	void* mem;
	size_t sz;
	size_t top;
};

static inline void scratch_init(struct scratch* ms, size_t sz)
{
	memset(ms, 0,  sizeof(*ms));
	AN(ms->mem = malloc(sz));
	ms->sz = sz;
}

static inline void* scratch_deref(struct scratch* ms, size_t p)
{
	return (void*)((char*)ms->mem + p);
}

static inline size_t scratch_alloc_align_log2(struct scratch* ms, size_t sz, int align_log2)
{
	size_t begin = ((ms->top + (1 << align_log2) - 1) >> align_log2) << align_log2;
	size_t new_top = begin + sz;
	size_t sz0 = ms->sz;
	while (new_top > ms->sz) ms->sz *= 2;
	if (ms->sz != sz0) {
		warnf("resizing scratch from %zu to %zu bytes", sz0, ms->sz);
		AN(ms->mem = realloc(ms->mem, ms->sz));
	}
	ms->top = new_top;
	return begin;
}

static inline size_t scratch_alloc(struct scratch* ms, size_t sz)
{
	return scratch_alloc_align_log2(ms, sz, 4);
}

static inline void* scratch_alloc_ptr(struct scratch* ms, size_t sz)
{
	return scratch_deref(ms, scratch_alloc_align_log2(ms, sz, 4));
}

static inline size_t scratch_calloc(struct scratch* ms, size_t sz)
{
	size_t p = scratch_alloc(ms, sz);
	memset(scratch_deref(ms, p), 0, sz);
	return p;
}

static inline void* scratch_calloc_ptr(struct scratch* ms, size_t sz)
{
	return scratch_deref(ms, scratch_calloc(ms, sz));
}


/* global scratch for main thread */
extern struct scratch main_thread_scratch;
#define MTS_get_top() main_thread_scratch.top
#define MTS_set_top(p) do { main_thread_scratch.top = p; } while (0)
#define MTS_ENTER(x) size_t _mts_top_ ## x = MTS_get_top()
#define MTS_LEAVE(x) MTS_set_top(_mts_top_ ## x)
#define MTS_deref(p) scratch_deref(&main_thread_scratch, p)
#define MTS_alloc_align_log2(sz, align_log2) scratch_alloc_align_log2(&main_thread_scratch, sz, align_log2)
#define MTS_alloc(sz) scratch_alloc(&main_thread_scratch, sz)
#define MTS_alloc_ptr(sz) scratch_alloc_ptr(&main_thread_scratch, sz)
#define MTS_calloc(sz) scratch_calloc(&main_thread_scratch, sz)
#define MTS_calloc_ptr(sz) scratch_calloc_ptr(&main_thread_scratch, sz)


#define SCRATCH_H
#endif
