#include <stdio.h>
#include <string.h>
#include <proto/arossupport.h>

static int debugputs(const char *str) {
	kprintf("%s", str);
	return strlen(str);
}

int puts(const char *str) {
	return debugputs(str);
}

int fputs(const char *str, FILE *s) {
	return debugputs(str);
}

