#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "unittest.h"

#include "a.h"
#include "mem.h"

#include "slab.h"

#define ITEM_SIZE_MALLOC_THRESHOLD_MIN_LOG2 (4)
#define ITEM_SIZE_MALLOC_THRESHOLD_MAX_LOG2 (16)
#define NUM_SIZES (ITEM_SIZE_MALLOC_THRESHOLD_MAX_LOG2 - ITEM_SIZE_MALLOC_THRESHOLD_MIN_LOG2)
#define MAX_SLABS_PER_ITEM_SIZE (256)
#define SLAB_SPACE_LOG2 (20)
#define MAX_ALLOCS_PER_SLAB_LOG2 (SLAB_SPACE_LOG2 - ITEM_SIZE_MALLOC_THRESHOLD_MIN_LOG2)
#if MAX_ALLOCS_PER_SLAB_LOG2 > 16
#error "MAX_ALLOCS_PER_SLAB_LOG2 cannot exceed 16 (otherwise freelist_t must support it)"
#endif
typedef uint16_t freelist_t;
#define MAX_PTR_RANGES (NUM_SIZES * MAX_SLABS_PER_ITEM_SIZE)
#define MAX_FALLBACK_ALLOCATIONS (1<<16)

struct slab {
	void* begin;
	int n_allocated;
	freelist_t* freelist;
};

struct slab_size {
	int last_used;
	int n_active_slabs;
	struct slab slabs[MAX_SLABS_PER_ITEM_SIZE];
};

struct ptr_range {
	void* p0;
	int slab_size_index;
	int slab_index;
};

struct fallback_allocation {
	void* ptr;
	int sz_log2;
};

static struct slab_size slab_sizes[NUM_SIZES];

static int n_ptr_ranges;
static struct ptr_range ptr_ranges[MAX_PTR_RANGES];

static int n_fallback_allocations;
static struct fallback_allocation fallback_allocations[MAX_FALLBACK_ALLOCATIONS];

static int in_ptr_range(void* p, int i)
{
	struct ptr_range* pr = &ptr_ranges[i];
	return p >= pr->p0 && p < (void*)((uint8_t*)pr->p0 + (1 << SLAB_SPACE_LOG2));
}

static int ptr_range_index_bin_search(void* p)
{
	int low = 0;
	int high = n_ptr_ranges - 1;
	while (low < high) {
		int mid = (low + high) >> 1;
		if (ptr_ranges[mid].p0 < p) {
			low = mid + 1;
		} else {
			high = mid;
		}
	}

	if (low != high) return -1;
	return low;
}

static struct ptr_range* ptr_range_bin_search(void* p)
{
	int pi = ptr_range_index_bin_search(p);
	for (int i = 0; i < 2; i++) {
		if (pi < 0) return NULL;
		if (in_ptr_range(p, pi)) return &ptr_ranges[pi];
		pi--;
	}
	return NULL;
}

static int fallback_allocation_near_bin_search(void* p)
{
	int low = 0;
	int high = n_fallback_allocations - 1;
	while (low < high) {
		int mid = (low + high) >> 1;
		if (fallback_allocations[mid].ptr < p) {
			low = mid + 1;
		} else {
			high = mid;
		}
	}
	if (low != high) return -1;
	return low;
}

static int fallback_allocation_exact_bin_search(void* p)
{
	int i = fallback_allocation_near_bin_search(p);
	if (i == -1) return -1;
	if (fallback_allocations[i].ptr != p) return -1;
	return i;
}

static inline int get_slab_size_index_for_sz_log2(int sz_log2)
{
	int i = sz_log2 - ITEM_SIZE_MALLOC_THRESHOLD_MIN_LOG2;
	ASSERT(i >= 0);
	ASSERT(i < NUM_SIZES);
	return i;
}

static inline int get_sz_log2_from_slab_size_index(int slab_size_index)
{
	return slab_size_index + ITEM_SIZE_MALLOC_THRESHOLD_MIN_LOG2;
}

static void* fallback_alloc(int sz_log2)
{
	void* p = mem_alloc(1 << sz_log2);
	AN(p);

	int i = fallback_allocation_near_bin_search(p);
	struct fallback_allocation* fa;
	if (i == -1) {
		AZ(n_fallback_allocations);
		fa = &fallback_allocations[0];
	} else {
		if (fallback_allocations[i].ptr < p) i++;
		int to_move = n_fallback_allocations - i;
		if (to_move > 0) {
			memmove(&fallback_allocations[i+1], &fallback_allocations[i], to_move * sizeof(*fallback_allocations));
		}
		fa = &fallback_allocations[i];
	}

	fa->ptr = p;
	fa->sz_log2 = sz_log2;
	n_fallback_allocations++;

	return p;
}

static void fallback_free(void* p)
{
	int idx = fallback_allocation_exact_bin_search(p);
	ASSERT(idx != -1);
	struct fallback_allocation* fa = &fallback_allocations[idx];
	ASSERT(fa->ptr == p);

	n_fallback_allocations--;

	int to_move = n_fallback_allocations - idx;
	if (to_move > 0) {
		memmove(&fallback_allocations[idx], &fallback_allocations[idx+1], to_move * sizeof(*fallback_allocations));
	}

	mem_free(p);
}

static inline int get_max_allocations_log2(int sz_log2)
{
	return SLAB_SPACE_LOG2 - sz_log2;
}

static inline int get_max_allocations(int sz_log2)
{
	return 1 << get_max_allocations_log2(sz_log2);
}

static void* try_alloc(struct slab_size* ss, int slab_index, int sz_log2)
{
	ASSERT(slab_index >= 0);
	ASSERT(slab_index < MAX_SLABS_PER_ITEM_SIZE);
	struct slab* slab = &ss->slabs[slab_index];

	if (slab->begin == NULL) return NULL;

	ASSERT(sz_log2 >= ITEM_SIZE_MALLOC_THRESHOLD_MIN_LOG2);
	ASSERT(sz_log2 <= ITEM_SIZE_MALLOC_THRESHOLD_MAX_LOG2);

	int max_allocs_log2 = get_max_allocations_log2(sz_log2);
	ASSERT(max_allocs_log2 >= (SLAB_SPACE_LOG2 - ITEM_SIZE_MALLOC_THRESHOLD_MAX_LOG2));
	ASSERT(max_allocs_log2 <= MAX_ALLOCS_PER_SLAB_LOG2);
	int max_allocs = 1 << max_allocs_log2;

	if (slab->n_allocated == max_allocs) return NULL;

	ASSERT(slab->n_allocated >= 0);
	ASSERT(slab->n_allocated < max_allocs);

	ss->last_used = slab_index;
	return (uint8_t*)slab->begin + slab->freelist[slab->n_allocated++] * (1 << sz_log2);
}

static int new_slab(int slab_size_index)
{
	struct slab_size* ss = &slab_sizes[slab_size_index];
	if (ss->n_active_slabs == MAX_SLABS_PER_ITEM_SIZE) return -1;
	ASSERT(ss->n_active_slabs < MAX_SLABS_PER_ITEM_SIZE);
	ASSERT(ss->n_active_slabs >= 0);

	struct slab* slab = &ss->slabs[ss->n_active_slabs];
	AZ(slab->begin);
	AZ(slab->n_allocated);
	AZ(slab->freelist);

	ASSERT(n_ptr_ranges < MAX_PTR_RANGES);

	slab->begin = mem_alloc(1 << SLAB_SPACE_LOG2);
	AN(slab->begin);

	int max_allocations = get_max_allocations(get_sz_log2_from_slab_size_index(slab_size_index));
	slab->freelist = mem_alloc(sizeof(*slab->freelist) * max_allocations);
	AN(slab->freelist);
	for (int i = 0; i < max_allocations; i++) slab->freelist[i] = i;

	struct ptr_range* pr;
	int pridx = ptr_range_index_bin_search(slab->begin);
	if (pridx == -1) {
		AZ(n_ptr_ranges);
		pr = &ptr_ranges[0];
	} else {
		if (ptr_ranges[pridx].p0 < slab->begin) pridx++;
		int to_move = n_ptr_ranges - pridx;
		if (to_move > 0) {
			memmove(&ptr_ranges[pridx+1], &ptr_ranges[pridx], to_move * sizeof(*ptr_ranges));
		}
		pr = &ptr_ranges[pridx];
	}

	pr->p0 = slab->begin;
	pr->slab_size_index = slab_size_index;
	pr->slab_index = ss->n_active_slabs;

	n_ptr_ranges++;

	return ss->n_active_slabs++;
}

static int sz_log2_min(int sz_log2)
{
	if (sz_log2 < ITEM_SIZE_MALLOC_THRESHOLD_MIN_LOG2) sz_log2 = ITEM_SIZE_MALLOC_THRESHOLD_MIN_LOG2;
	return sz_log2;
}

#ifdef PARANOID
static int freelist_index_count(struct slab* slab, int sz_log2, int allocation_index)
{
	int max_allocations = get_max_allocations(sz_log2);
	int count = 0;
	for (int i = slab->n_allocated; i < max_allocations; i++) if (slab->freelist[i] == allocation_index) count++;
	return count;
}
#endif

void* slab_alloc_log2(int sz_log2)
{
	if (sz_log2 > ITEM_SIZE_MALLOC_THRESHOLD_MAX_LOG2) {
		return fallback_alloc(sz_log2);
	} else {
		sz_log2 = sz_log2_min(sz_log2);
		int slab_size_index = get_slab_size_index_for_sz_log2(sz_log2);
		struct slab_size* ss = &slab_sizes[slab_size_index ];
		ASSERT(ss->last_used >= 0);
		ASSERT(ss->last_used < MAX_SLABS_PER_ITEM_SIZE);
		void* p = try_alloc(ss, ss->last_used, sz_log2);
		if (p != NULL) return p;
		for (int i = 0; i < ss->n_active_slabs; i++) {
			void* p = try_alloc(ss, i, sz_log2);
			if (p != NULL) return p;
		}
		int new_index = new_slab(slab_size_index);
		if (new_index == -1) {
			return fallback_alloc(sz_log2);
		} else {
			void* p = try_alloc(ss, new_index, sz_log2);
			AN(p);
			return p;
		}
	}
}

void* slab_calloc_log2(int sz_log2)
{
	void* p = slab_alloc_log2(sz_log2);
	memset(p, 0, 1 << sz_log2);
	return p;
}

void* slab_realloc_log2(void* old_ptr, int new_sz_log2)
{
	struct ptr_range* pr = ptr_range_bin_search(old_ptr);
	int old_sz_log2;
	if (pr == NULL) {
		int faidx = fallback_allocation_exact_bin_search(old_ptr);
		ASSERT(faidx >= 0);
		struct fallback_allocation* fa = &fallback_allocations[faidx];
		ASSERT(fa->ptr == old_ptr);
		old_sz_log2 = fa->sz_log2;
		if (new_sz_log2 == old_sz_log2) return old_ptr;
	} else {
		old_sz_log2 = get_sz_log2_from_slab_size_index(pr->slab_size_index);
		if (new_sz_log2 == old_sz_log2) return old_ptr;
	}

	void* new_ptr = slab_alloc_log2(new_sz_log2);
	int to_copy_log2 = new_sz_log2 > old_sz_log2 ? old_sz_log2 : new_sz_log2;
	memcpy(new_ptr, old_ptr, 1 << to_copy_log2);
	slab_free(old_ptr);
	return new_ptr;
}

void slab_free(void* p)
{
	struct ptr_range* pr = ptr_range_bin_search(p);
	if (pr == NULL) {
		fallback_free(p);
		return;
	}

	struct slab* slab = &slab_sizes[pr->slab_size_index].slabs[pr->slab_index];
	ASSERT(slab->n_allocated > 0);

	int allocation_offset = (uint8_t*)p - (uint8_t*)slab->begin;
	int sz_log2 = get_sz_log2_from_slab_size_index(pr->slab_size_index);
	int allocation_index = allocation_offset >> sz_log2;
	ASSERT((allocation_index << sz_log2) == allocation_offset);

	slab->freelist[--slab->n_allocated] = allocation_index;

	PARANOID_ASSERT(freelist_index_count(slab, sz_log2, allocation_index) == 1); // assertion is O(n)-expensive hence paranoid
}



#ifdef UNITTEST

static void count_active_slabs_and_allocated(int* n_active_slabs, int* n_allocated)
{
	if (n_active_slabs) *n_active_slabs = 0;
	if (n_allocated) *n_allocated = 0;
	for (int i = 0; i < NUM_SIZES; i++) {
		struct slab_size* ss = &slab_sizes[i];
		if (n_active_slabs) *n_active_slabs += ss->n_active_slabs;
		for (int j = 0; j < ss->n_active_slabs; j++) {
			if (n_allocated) *n_allocated += ss->slabs[j].n_allocated;
		}
	}
}

static void test_functional()
{
	ASSERT(slab_sz_log2(1) == 1);
	ASSERT(slab_sz_log2(2) == 1);
	ASSERT(slab_sz_log2(3) == 2);
	ASSERT(slab_sz_log2(16) == 4);
	ASSERT(slab_sz_log2(20) == 5);
	ASSERT(slab_sz_log2(30) == 5);
	ASSERT(slab_sz_log2(32) == 5);
	ASSERT(slab_sz_log2(40) == 6);

	ASSERT(get_slab_size_index_for_sz_log2(4) == 0);
	ASSERT(get_slab_size_index_for_sz_log2(5) == 1);
	ASSERT(get_slab_size_index_for_sz_log2(6) == 2);

	ASSERT(get_sz_log2_from_slab_size_index(0) == 4);
	ASSERT(get_sz_log2_from_slab_size_index(1) == 5);
	ASSERT(get_sz_log2_from_slab_size_index(2) == 6);

	ASSERT(get_max_allocations_log2(4) == 16);
	ASSERT(get_max_allocations_log2(5) == 15);
	ASSERT(get_max_allocations_log2(6) == 14);

	ASSERT(get_max_allocations(4) == (1 <<16));
	ASSERT(get_max_allocations(5) == (1 <<15));
	ASSERT(get_max_allocations(6) == (1 <<14));

	ASSERT(sz_log2_min(0) == 4);
	ASSERT(sz_log2_min(2) == 4);
	ASSERT(sz_log2_min(4) == 4);
	ASSERT(sz_log2_min(5) == 5);
}


static void test_simple_allocations()
{
	void* p = slab_alloc(10);
	AN(p);
	slab_free(p);

	void* p2 = slab_alloc(10);
	ASSERT(p == p2);
	slab_free(p2);

	void* p3 = slab_alloc(40);
	ASSERT(p != p3);
	slab_free(p3);

	int n_active_slabs, n_allocated;
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_active_slabs == 2);
	AZ(n_allocated);

	ASSERT(ut_allocations == 4); // 2 per slab
	ASSERT(ut_frees == 0);
}

static void test_using_multiple_slabs_per_size()
{
	int sz_log2 = 5;
	int n = get_max_allocations(sz_log2);
	for (int i = 0; i < n; i++) slab_alloc(1 << sz_log2);

	int n_active_slabs, n_allocated;
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_active_slabs == 1);
	ASSERT(n_allocated == n);

	slab_alloc(1 << sz_log2);
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_active_slabs == 2);
	ASSERT(n_allocated == n+1);
}

static void test_freeing_stuff()
{
	int sz_log2 = 10;
	int sz = 1 << sz_log2;
	int n = get_max_allocations(sz_log2);
	void* ptrs[n];
	for (int i = 0; i < n; i++) {
		ptrs[i] = slab_alloc(sz);
	}

	for (int i = 0; i < n; i++) {
		if (i&1) slab_free(ptrs[i]);
	}

	int n_active_slabs, n_allocated;
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_active_slabs == 1);
	ASSERT(n_allocated == n/2);

	for (int i = 0; i < n; i++) {
		if (i&1) slab_alloc(sz);
	}

	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_active_slabs == 1);
	ASSERT(n_allocated == n);

	slab_alloc(sz);
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_active_slabs == 2);
	ASSERT(n_allocated == n+1);
}

static void test_freeing_multiple_sizes()
{
	const int szn = 5;
	const int n = 10;
	void* ptrs[szn*n];
	int o = 0;
	for (int i = 0; i < n; i++) {
		for (int sz_log2 = 6; sz_log2 < 6+szn; sz_log2++) {
			ptrs[o++] = slab_alloc(1 << sz_log2);
		}
	}

	int n_active_slabs, n_allocated;
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_active_slabs == szn);
	ASSERT(n_allocated == o);

	for (int i = 0; i < o; i++) slab_free(ptrs[i]);
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_active_slabs == szn);
	ASSERT(n_allocated == 0);
}

static void fail_to_free_invalid_ptr()
{
	uint8_t* p = slab_alloc(32);
	ut_assert = "ASSERT((allocation_index << sz_log2) == allocation_offset) failed in slab_free";
	slab_free(p + 1);
}

static void test_malloc_fallback()
{
	slab_alloc(20);
	ASSERT(ut_allocations == 2);
	ASSERT(ut_frees == 0);

	void* p = slab_alloc(65536 + 1); // fallback allocation threshold plus one
	ASSERT(n_fallback_allocations == 1);
	ASSERT(ut_allocations == 3);
	ASSERT(ut_frees == 0);

	slab_free(p);
	ASSERT(n_fallback_allocations == 0);
	ASSERT(ut_allocations == 3);
	ASSERT(ut_frees == 1);
}

static void test_multiple_with_malloc_fallback()
{
	int n = 64;
	void* ptrs[n];
	for (int i = 0; i < n; i++) {
		ptrs[i] = slab_alloc(65536+1);
		ASSERT(n_fallback_allocations == i+1);
		ASSERT(ut_allocations == i+1);
	}

	for (int i = 0; i < n; i++) {
		if (i&1) {
			slab_free(ptrs[i]);
		}
	}
	ASSERT(n_fallback_allocations == n/2);
	ASSERT(ut_allocations == n);
	ASSERT(ut_frees == n/2);

	for (int i = 0; i < n; i++) {
		if (i&1) {
			ptrs[i] = slab_alloc((1<<18));
		}
	}
	ASSERT(n_fallback_allocations == n);
	ASSERT(ut_allocations == n*3/2);

	for (int i = 0; i < n; i++) {
		slab_free(ptrs[i]);
	}
	ASSERT(n_fallback_allocations == 0);
	ASSERT(ut_allocations == n*3/2);
	ASSERT(ut_frees == n*3/2);

}

static void test_slab_realloc()
{
	int n_active_slabs, n_allocated;

	// initial allocation
	uint32_t* p0 = slab_alloc(20);
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_active_slabs == 1);
	ASSERT(n_allocated == 1);
	ASSERT(ut_allocations == 2);

	*p0 = 0xdeadbeef; // tracer

	// reallocating to new slab
	uint32_t* p1 = slab_realloc(p0, 40);
	ASSERT(p0 != p1);
	ASSERT(*p1 == 0xdeadbeef);
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_active_slabs == 2);
	ASSERT(n_allocated == 1);
	ASSERT(ut_allocations == 4);

	// reallocating to old slab; assuming it gets same ptr as initial
	// allocation
	uint32_t* p2 = slab_realloc(p0, 19);
	ASSERT(*p2 == 0xdeadbeef);
	ASSERT(p2 != p1);
	ASSERT(p2 == p0);
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_active_slabs == 2);
	ASSERT(n_allocated == 1);
	ASSERT(ut_allocations == 4);

	// no-op reallocation
	uint32_t* p3 = slab_realloc(p0, 18);
	ASSERT(*p3 == 0xdeadbeef);
	ASSERT(p3 == p2);
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_active_slabs == 2);
	ASSERT(n_allocated == 1);
	ASSERT(ut_allocations == 4);
}

static void test_fallback_realloc()
{
	int n_active_slabs, n_allocated;

	uint32_t* p0 = slab_alloc(100);
	ASSERT(ut_frees == 0);
	ASSERT(ut_allocations == 2);
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_active_slabs == 1);
	ASSERT(n_allocated == 1);
	ASSERT(n_fallback_allocations == 0);

	*p0 = 0xdeadbeef;

	uint32_t* p1 = slab_realloc(p0, 100000);
	ASSERT(p1 != p0);
	ASSERT(ut_frees == 0);
	ASSERT(ut_allocations == 3);
	ASSERT(*p1 == 0xdeadbeef);
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_allocated == 0);
	ASSERT(n_active_slabs == 1);
	ASSERT(n_fallback_allocations == 1);

	uint32_t* p2 = slab_realloc(p1, 200000);
	ASSERT(p2 != p1);
	ASSERT(ut_frees == 1);
	ASSERT(ut_allocations == 4);
	ASSERT(*p2 == 0xdeadbeef);
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_allocated == 0);
	ASSERT(n_active_slabs == 1);
	ASSERT(n_fallback_allocations == 1);

	uint32_t* p3 =  slab_realloc(p2, 1000);
	ASSERT(p3 != p2);
	ASSERT(ut_frees == 2);
	ASSERT(ut_allocations == 6);
	ASSERT(*p3 == 0xdeadbeef);
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	ASSERT(n_allocated == 1);
	ASSERT(n_active_slabs == 2);
	ASSERT(n_fallback_allocations == 0);
}

void pre_test()
{
	memset(slab_sizes, 0, sizeof(slab_sizes));
	n_ptr_ranges = 0;
	memset(ptr_ranges, 0, sizeof(ptr_ranges));
	n_fallback_allocations = 0;
	memset(fallback_allocations, 0, sizeof(fallback_allocations));
}

void post_test()
{
	for (int i = 0; i < NUM_SIZES; i++) {
		struct slab_size* ss = &slab_sizes[i];
		ss->last_used = 0;
		if (!ss->n_active_slabs) continue;
		for (int j = 0; j < ss->n_active_slabs; j++) {
			struct slab* s = &ss->slabs[j];
			mem_free(s->begin);
			mem_free(s->freelist);
			memset(s, 0, sizeof(*s));
		}
		memset(ss, 0, sizeof(*ss));
	}

	int n_active_slabs, n_allocated;
	count_active_slabs_and_allocated(&n_active_slabs, &n_allocated);
	AZ(n_active_slabs);
	AZ(n_allocated);

	n_ptr_ranges = 0;

	for (int i = 0; i < n_fallback_allocations; i++) {
		mem_free(fallback_allocations[i].ptr);
	}
	n_fallback_allocations = 0;

}

void run_tests()
{
	TEST(test_functional);
	TEST(test_simple_allocations);
	TEST(test_using_multiple_slabs_per_size);
	TEST(test_freeing_stuff);
	TEST(test_freeing_multiple_sizes);
	TEST(fail_to_free_invalid_ptr);
	TEST(test_malloc_fallback);
	TEST(test_multiple_with_malloc_fallback);
	TEST(test_slab_realloc);
	TEST(test_fallback_realloc);
}

#endif
