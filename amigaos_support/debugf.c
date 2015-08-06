#include <debugf.h>
#include <stdarg.h>
#include <stdio.h>
#include <proto/arossupport.h>

int vdebugf(const char *fmt, va_list args) {
	char buffer[256];

	int retval = vsnprintf(buffer, sizeof(buffer), fmt, args);
	kprintf("%s", buffer);

	return retval;
}

int debugf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int retval = vdebugf(fmt, ap);
	va_end(ap);
	return retval;
}

