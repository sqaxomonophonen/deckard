#include <stdio.h>
#include <stdarg.h>

#include "log.h"

void warnf(const char* fmt, ...) {
	fprintf(stderr, "WARNING: ");
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
}

