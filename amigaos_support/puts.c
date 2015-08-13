#include <stdio.h>
#include <string.h>
#include <clib/debug_protos.h>

int puts(const char *str) {
	KPutStr((CONST_STRPTR)str);
	return strlen(str);
}

int fputs(const char *str, FILE *s) {
	return puts(str);
}

