#ifndef DEBUGF_H
#define DEBUGF_H

#include <stdarg.h>

int debugf(const char *fmt, ...);
int vdebugf(const char *fmt, va_list args);

#endif

