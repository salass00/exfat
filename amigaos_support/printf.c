#include <stdio.h>
#include <stdarg.h>
#include <proto/arossupport.h>

int vprintf(const char *fmt, va_list args) {
	return vkprintf(fmt, args);
}

int printf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int retval = vkprintf(fmt, ap);
	va_end(ap);
	return retval;
}

int vfprintf(FILE *s, const char *fmt, va_list args) {
	return vkprintf(fmt, args);
}

int fprintf(FILE *s, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int retval = vkprintf(fmt, ap);
	va_end(ap);
	return retval;
}

