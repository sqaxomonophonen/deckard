#ifdef UNITTEST

#define PARANOID

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>

#include "a.h"

void arghf(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");

	fprintf(stderr, "BACKTRACE BEGIN\n");
	void* buffer[256];
	int sz = backtrace(buffer, 256);
	backtrace_symbols_fd(buffer, sz, fileno(stderr));
	fprintf(stderr, "BACKTRACE END\n");

	abort();
}

static int ut_allocations;
static int ut_frees;

void* mem_alloc(size_t sz)
{
	void* p = malloc(sz);
	AN(p);
	ut_allocations++;
	return p;
}

void* mem_calloc(size_t sz)
{
	void* p = calloc(1, sz);
	AN(p);
	ut_allocations++;
	return p;
}

void* mem_realloc(void* p, size_t sz)
{
	p = realloc(p, sz);
	AN(p);
	ut_allocations++;
	return p;
}

void mem_free(void* p)
{
	free(p);
	ut_frees++;
}

#define TEST(fn) \
	do { \
		ut_allocations = ut_frees = 0; \
		fprintf(stderr, #fn " ... "); \
		fn(); \
		fprintf(stderr, "ok\n"); \
		ASSERT(ut_allocations == ut_frees); \
	} while (0);


void run_tests();
int main(int argc, char** argv)
{
	run_tests();
	return EXIT_SUCCESS;
}

#endif
