#include <stdio.h>
#include <stdarg.h>
#include <debugf.h>

int vprintf(const char *fmt, va_list args) {
	return vdebugf(fmt, args);
}

int printf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int retval = vdebugf(fmt, ap);
	va_end(ap);
	return retval;
}

int vfprintf(FILE *s, const char *fmt, va_list args) {
	return vdebugf(fmt, args);
}

int fprintf(FILE *s, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int retval = vdebugf(fmt, ap);
	va_end(ap);
	return retval;
}

