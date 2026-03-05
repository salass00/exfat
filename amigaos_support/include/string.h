#ifndef STRING_H
#define STRING_H

#include_next <string.h>

#ifndef __AROS__
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
#endif

#endif

