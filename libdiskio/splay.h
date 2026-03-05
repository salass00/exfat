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

#ifndef SPLAY_H
#define SPLAY_H

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

struct Splay {
	struct Splay *parent;
	struct Splay *left;
	struct Splay *right;
	CONST_APTR    key;
};

typedef int (*SplayCmpFunc)(CONST_APTR key1, CONST_APTR key2);

void InsertSplay(struct Splay **root, SplayCmpFunc cmpfunc, struct Splay *sn, CONST_APTR key);
struct Splay *FindSplay(struct Splay **root, SplayCmpFunc cmpfunc, CONST_APTR key);
struct Splay *FirstSplay(struct Splay **root);
struct Splay *LastSplay(struct Splay **root);
struct Splay *PrevSplay(struct Splay *sn);
struct Splay *NextSplay(struct Splay *sn);
void RemoveSplay(struct Splay **root, struct Splay *sn);

#endif

