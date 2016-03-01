#ifndef _A_H_

#include <stdint.h>

/* a for assert, argh, abort, abandon ye all hope, and so on */

// general assertions
#ifdef UNITTEST
#define ARGHF ut_arghf
#else
#define ARGHF arghf
#endif

void ARGHF(const char* fmt, ...) __attribute__((noreturn)) __attribute__((format (printf, 1, 2)));

#define ASSERT(cond) \
	do { \
		if (!(cond)) { \
			ARGHF("ASSERT(%s) failed in %s() in %s:%d\n", #cond, __func__, __FILE__, __LINE__); \
		} \
	} while (0)


#define WRONG(msg) do { ASSERT(0 == (uintptr_t)msg); } while(0)
#define AN(expr) do { ASSERT((expr) != 0); } while(0)
#define AZ(expr) do { ASSERT((expr) == 0); } while(0)

// GL
#define CHKGL \
	do { \
		GLenum CHKGL_error = glGetError(); \
		if (CHKGL_error != GL_NO_ERROR) { \
			ARGHF("OPENGL ERROR %d in %s:%d\n", CHKGL_error, __FILE__, __LINE__); \
		} \
	} while (0)

#ifdef PARANOID
#define PARANOID_ASSERT ASSERT
#define PARANOID_WRONG WRONG
#else
#define PARANOID_ASSERT(x)
#define PARANOID_WRONG(x)
#endif

#define _A_H_
#endif
