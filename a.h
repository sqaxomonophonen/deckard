#ifndef _A_H_

#include <stdint.h>

/* a for assert, argh, abort, abandon ye all hope, and so on */

void arghf(const char* fmt, ...) __attribute__((noreturn)) __attribute__((format (printf, 1, 2)));

// general assertions
#define ASSERT(cond) \
	do { \
		if (!(cond)) { \
			arghf("ASSERT(%s) failed in %s() in %s:%d\n", #cond, __func__, __FILE__, __LINE__); \
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
			arghf("OPENGL ERROR %d in %s:%d\n", CHKGL_error, __FILE__, __LINE__); \
		} \
	} while (0)

// paranoid (TODO disable in production)
#define PARANOID_ASSERT ASSERT
#define PARANOID_WRONG WRONG

#define _A_H_
#endif
