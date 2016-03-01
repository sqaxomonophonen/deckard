#ifdef UNITTEST

#define PARANOID

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>
#include <setjmp.h>

#include "a.h"

/* set ut_assert inside test to expect an assertion starting with this string
 * (automatically cleared before TEST()). when catching an assert, the
 * following ASSERT(ut_allocations == ut_frees) is NOT performed, meaning you
 * can memory leak a bit.. don't do it too much :) */
static char* ut_assert;

static int ut_allocations;
static int ut_frees;

static jmp_buf ut_jmp_buf;

void ut_arghf(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);

	if (ut_assert) {
		char msg[4096];
		vsnprintf(msg, sizeof(msg), fmt, args);
		if (strstr(msg, ut_assert) == msg) {
			longjmp(ut_jmp_buf, 1); // no return
		}
	}

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

void pre_test();
void post_test();
void run_tests();

#define TEST(fn) \
	do { \
		ut_allocations = ut_frees = 0; \
		ut_assert = NULL; \
		int r = setjmp(ut_jmp_buf); \
		if (r == 0) { \
			fprintf(stderr, #fn " ... "); \
			pre_test(); \
			fn(); \
			post_test(); \
			fprintf(stderr, "ok\n"); \
			ASSERT(ut_allocations == ut_frees); \
			AZ(ut_assert); \
		} else { \
			fprintf(stderr, "aok\n"); \
		} \
	} while (0);


int main(int argc, char** argv)
{
	run_tests();
	return EXIT_SUCCESS;
}

#endif
