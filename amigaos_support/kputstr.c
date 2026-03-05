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

#include <clib/debug_protos.h>

extern struct Library *SysBase;

#if !defined(__AROS__) && !defined(NODEBUG)
void KPutStr(CONST_STRPTR str) {
	TEXT ch;
	register struct Library *_a6 __asm__("a6") = SysBase;
	while ((ch = *str++) != '\0') {
		register char _d0 __asm__("d0") = ch;
		__asm__("jsr -516(a6)"
			:
			: "d" (_d0), "a" (_a6)
			: "d0", "d1", "a0", "a1", "cc"
		);
	}
}
#endif

