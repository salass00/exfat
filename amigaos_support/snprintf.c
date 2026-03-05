/**
 * Copyright (c) 2015-2026 Fredrik Wikstrom <fredrik@a500.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <exec/types.h>
#include <string.h>
#include <stdarg.h>

#define min(x,y) ((x)<(y)?(x):(y))
#define max(x,y) ((x)>(y)?(x):(y))

#ifndef __AROS__
struct putchdata {
	char *buffer;
	size_t size;
	size_t count;
};

void putchproc(char ch, struct putchdata *putchdata) {
	if (putchdata->size != 0) {
		if (--putchdata->size != 0)
			*putchdata->buffer++ = ch;
		else
			*putchdata->buffer++ = '\0';
	}

	if (ch != '\0')
		putchdata->count++;
}

static void reverse(char *str, size_t len) {
	char *start = str;
	char *end = str + len - 1;
	char tmp;

	while (start < end) {
		tmp = *end;
		*end-- = *start;
		*start++ = tmp;
	}
}


static size_t itoa(unsigned num, char *dst, unsigned base,
	char issigned, char addplus, char uppercase)
{
	char a = uppercase ? 'A' : 'a';
	char negative = FALSE;
	char *d = dst;
	size_t len;

	if (num == 0) {
		*d++ = '0';
		return d - dst;
	}

	if (issigned && (int)num < 0 && base == 10) {
		negative = TRUE;
		num = -num;
	}

	while (num != 0) {
		unsigned rem = num % base;
		num /= base;
		*d++ = (rem > 9) ? (rem - 10 + a) : (rem + '0');
	}

	if (negative)
		*d++ = '-';
	else if (addplus)
		*d++ = '+';

	len = d - dst;
	reverse(dst, len);
	return len;
}

static size_t lltoa(unsigned long long num, char *dst, unsigned base,
	char issigned, char addplus, char uppercase)
{
	char a = uppercase ? 'A' : 'a';
	char negative = FALSE;
	char *d = dst;
	size_t len;

	if (num == 0) {
		*d++ = '0';
		return d - dst;
	}

	if (issigned && (signed long long)num < 0 && base == 10) {
		negative = TRUE;
		num = -num;
	}

	while (num != 0) {
		unsigned rem = num % base;
		num /= base;
		*d++ = (rem > 9) ? (rem - 10 + a) : (rem + '0');
	}

	if (negative)
		*d++ = '-';
	else if (addplus)
		*d++ = '+';

	len = d - dst;
	reverse(dst, len);
	return len;
}

int my_vsnprintf(char *buffer, size_t size, const char *fmt, va_list arg) {
	struct putchdata putchdata;
	char ch;

	putchdata.buffer = buffer;
	putchdata.size   = size;
	putchdata.count  = 0;

	while ((ch = *fmt++) != '\0') {
		if (ch != '%') {
			putchproc(ch, &putchdata);
		} else {
			char left = FALSE;
			char addplus = FALSE;
			char alternate = FALSE;
			char lead = ' ';
			size_t width = 0;
			size_t limit = 0;
			char longlong = FALSE;
			char uppercase;
			char tmp[128];
			const char *src;
			size_t len;

			if ((ch = *fmt++) == '\0') return putchdata.count;

			while (TRUE) {
				if (ch == '-')
					left = TRUE;
				else if (ch == '+')
					addplus = TRUE;
				else if (ch == '#')
					alternate = TRUE;
				else if (ch == '0')
					lead = '0';
				else
					break;
				if ((ch = *fmt++) == '\0') return putchdata.count;
			}

			while (ch >= '0' && ch <= '9') {
				width = 10 * width + (ch - '0');
				if ((ch = *fmt++) == '\0') return putchdata.count;
			}

			if (ch == '.') {
				if ((ch = *fmt++) == '\0') return putchdata.count;

				while (ch >= '0' && ch <= '9') {
					limit = 10 * limit + (ch - '0');
					if ((ch = *fmt++) == '\0') return putchdata.count;
				}
			}

			if (ch == 'l' || ch == 'h') {
				if ((ch = *fmt++) == '\0') return putchdata.count;
				if (ch == 'l') {
					longlong = TRUE;
					if ((ch = *fmt++) == '\0') return putchdata.count;
				}
			}

			switch (ch) {
			case '%':
				putchproc('%', &putchdata);
				break;
			case 'D':
			case 'd':
				uppercase = (ch == 'D') ? TRUE : FALSE;
				if (longlong)
					len = lltoa(va_arg(arg, long long), tmp, 10, TRUE, addplus, uppercase);
				else
					len = itoa(va_arg(arg, int), tmp, 10, TRUE, addplus, uppercase);

				src = tmp;
				if (width > len)
					width -= len;
				else
					width = 0;

				if (!left)
					while (width--)
						putchproc(lead, &putchdata);

				while (len--)
					putchproc(*src++, &putchdata);

				if (left)
					while (width--)
						putchproc(' ', &putchdata);
				break;
			case 'U':
			case 'u':
				uppercase = (ch == 'X') ? TRUE : FALSE;
				if (longlong)
					len = lltoa(va_arg(arg, long long), tmp, 10, FALSE, addplus, uppercase);
				else
					len = itoa(va_arg(arg, int), tmp, 10, FALSE, addplus, uppercase);

				src = tmp;
				if (width > len)
					width -= len;
				else
					width = 0;

				if (!left)
					while (width--)
						putchproc(lead, &putchdata);

				while (len--)
					putchproc(*src++, &putchdata);

				if (left)
					while (width--)
						putchproc(' ', &putchdata);
				break;
			case 'X':
			case 'x':
				uppercase = (ch == 'X') ? TRUE : FALSE;
				if (longlong)
					len = lltoa(va_arg(arg, long long), tmp, 16, FALSE, addplus, uppercase);
				else
					len = itoa(va_arg(arg, int), tmp, 16, FALSE, addplus, uppercase);

				src = tmp;
				if (width > len)
					width -= len;
				else
					width = 0;

				if (!left)
					while (width--)
						putchproc(lead, &putchdata);

				while (len--)
					putchproc(*src++, &putchdata);

				if (left)
					while (width--)
						putchproc(' ', &putchdata);
				break;
			case 'P':
			case 'p':
				uppercase = (ch == 'P') ? TRUE : FALSE;
				if (longlong)
					len = lltoa(va_arg(arg, long long), tmp, 16, FALSE, FALSE, uppercase);
				else
					len = itoa(va_arg(arg, int), tmp, 16, FALSE, FALSE, uppercase);

				src = tmp;
				width = 8;
				lead = '0';
				if (width > len)
					width -= len;
				else
					width = 0;

				if (alternate && tmp[0] != '0') {
					putchproc('0', &putchdata);
					putchproc('x', &putchdata);
				}

				while (width--)
					putchproc(lead, &putchdata);

				while (len--)
					putchproc(*src++, &putchdata);
				break;
			case 'S':
			case 's':
				src = va_arg(arg, const char *);
				if (src == NULL)
					src = "(null)";

				len = strlen(src);

				if (limit != 0)
					len = min(len, limit);

				if (width > len)
					width -= len;
				else
					width = 0;

				if (!left)
					while (width--)
						putchproc(' ', &putchdata);

				while (len--)
					putchproc(*src++, &putchdata);

				if (left)
					while (width--)
						putchproc(' ', &putchdata);
				break;
			}
		}
	}
	putchproc('\0', &putchdata);

	return putchdata.count;
}

int my_snprintf(char *buffer, size_t size, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int retval = my_vsnprintf(buffer, size, fmt, ap);
	va_end(ap);
	return retval;
}
#endif

