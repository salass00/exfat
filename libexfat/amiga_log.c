/*
	log.c (02.09.09)
	exFAT file system implementation library.

	Free exFAT implementation.
	Copyright (C) 2010-2013  Andrew Nayenko

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "exfat.h"
#include <stdarg.h>
#include <debugf.h>
#include <proto/exec.h>

int exfat_errors;

/*
 * This message means an internal bug in exFAT implementation.
 */
void exfat_bug(const char* format, ...)
{
	va_list ap;

	debugf("BUG: ");
	va_start(ap, format);
	vdebugf(format, ap);
	va_end(ap);
	debugf(".\n");

	while (1) Wait(0);
}

/*
 * This message means an error in exFAT file system.
 */
void exfat_error(const char* format, ...)
{
	va_list ap;

	exfat_errors++;

	debugf("ERROR: ");
	va_start(ap, format);
	vdebugf(format, ap);
	va_end(ap);
	debugf(".\n");
}

/*
 * This message means that there is something unexpected in exFAT file system
 * that can be a potential problem.
 */
void exfat_warn(const char* format, ...)
{
	va_list ap;

	debugf("WARN: ");
	va_start(ap, format);
	vdebugf(format, ap);
	va_end(ap);
	debugf(".\n");
}

/*
 * Just debug message. Disabled by default.
 */
void exfat_debug(const char* format, ...)
{
	va_list ap;

	debugf("DEBUG: ");
	va_start(ap, format);
	vdebugf(format, ap);
	va_end(ap);
	debugf(".\n");
}
