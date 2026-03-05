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

#include "diskio_internal.h"

#ifdef __AROS__
AROS_UFH5(int, DiskIOMemHandler,
	AROS_UFHA(APTR, data, A1),
	AROS_UFHA(APTR, code, A5),
	AROS_UFHA(struct ExecBase *, SysBase, A6),
	AROS_UFHA(APTR, mask, D1),
	AROS_UFHA(APTR, custom, A0))
{
	AROS_USERFUNC_INIT
#else
/*SAVEDS*/ ASM int DiskIOMemHandler(
	REG(a6, struct ExecBase *SysBase),
	REG(a0, APTR custom),
	REG(a1, APTR data))
{
#endif
	struct MemHandlerData *memh = custom;
	struct BlockCache *bc = data;
	struct MinNode *node, *pred;
	struct MinList *list;
	ULONG freed, goal;
	int i;

	DEBUGF("DiskIOMemHandler(%#p, %#p, %#p)\n", SysBase, memh, bc);

	if (bc->cache_busy)
		return MEM_DID_NOTHING;

	bc->cache_busy = TRUE;

	freed = 0;
	goal = memh->memh_RequestSize;
	for (i = 0; i < 2; i++) {
		switch (i) {
			case 0:
				list = &bc->probation_list;
				break;
			default:
				list = &bc->protected_list;
				break;
		}

		for (node = list->mlh_TailPred; (pred = node->mln_Pred) != NULL; node = pred) {
			ExpungeCacheNode(bc, BCNFROMNODE(node));
			freed += bc->sector_size;
		}

		if (freed >= goal)
			break;
	}

	bc->cache_busy = FALSE;

	if (freed == 0)
		return MEM_DID_NOTHING;
	else if (i < 2)
		return MEM_TRY_AGAIN;
	else
		return MEM_ALL_DONE;

#ifdef __AROS__
	AROS_USERFUNC_EXIT
#endif
}

