/**
 * Copyright (c) 2014-2016 Fredrik Wikstrom <fredrik@a500.org>
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
#include <stdint.h>

#define PERCENT_PROTECTED      30
#define PERCENT_DIRTY          30

#define HIGH_THRESHOLD_PERCENT 60
#define LOW_THRESHOLD_PERCENT  30

struct BlockCache *InitBlockCache(struct DiskIO *dio) {
	struct BlockCache *bc;
	UQUAD disk_cache_size;
	ULONG mem_cache_size;
	ULONG min_cache_size;

	DEBUGF("InitBlockCache(%#p)\n", dio);

	bc = AllocPooled(dio->mempool, sizeof(*bc));
	if (bc == NULL)
		return NULL;

	bzero(bc, sizeof(*bc));

	bc->dio_handle          = dio;
	bc->sector_size         = dio->sector_size;
	bc->sector_shift        = dio->sector_shift;
	bc->write_cache_enabled = dio->write_cache_enabled;

	NEWLIST(&bc->probation_list);
	NEWLIST(&bc->protected_list);
	NEWLIST(&bc->dirty_list);

	InitSemaphore(&bc->cache_semaphore);

	bc->mem_handler.is_Node.ln_Type = NT_INTERRUPT;
	bc->mem_handler.is_Node.ln_Pri  = 50;
	bc->mem_handler.is_Node.ln_Name = (char *)dio->devname;

	bc->mem_handler.is_Data = bc;
	bc->mem_handler.is_Code = (void (*)())DiskIOMemHandler;

	bc->mempool = CreatePool(MEMF_PUBLIC, 4096, 1024);
	if (bc->mempool == NULL)
		goto cleanup;

	/* 1% of total disk space */
	disk_cache_size = (dio->total_sectors + 50) / 100;

	/* 10% of total memory */
	mem_cache_size = ((AvailMem(MEMF_FAST | MEMF_TOTAL) >> bc->sector_shift) + 5) / 10;

	/* Minimum size is 1MB */
	min_cache_size = (1024UL * 1024UL) >> bc->sector_shift;

	if ((UQUAD)mem_cache_size < disk_cache_size)
		bc->max_cache_nodes = mem_cache_size;
	else
		bc->max_cache_nodes = (ULONG)disk_cache_size;

	if (bc->max_cache_nodes < min_cache_size)
		bc->max_cache_nodes = min_cache_size;

	bc->max_protected_nodes = ((UQUAD)bc->max_cache_nodes * PERCENT_PROTECTED + 50) / 100;
	bc->max_dirty_nodes     = ((UQUAD)bc->max_cache_nodes * PERCENT_DIRTY     + 50) / 100;

	bc->high_threshold = ((UQUAD)bc->max_dirty_nodes * HIGH_THRESHOLD_PERCENT + 50) / 100;
	bc->low_threshold  = ((UQUAD)bc->max_dirty_nodes * LOW_THRESHOLD_PERCENT  + 50) / 100;

	if (bc->write_cache_enabled) {
		ULONG max_buffer_size;

		max_buffer_size = (64UL * 1024UL) >> bc->sector_shift;

		bc->write_buffer_size = bc->max_dirty_nodes;

		if (bc->write_buffer_size > max_buffer_size)
			bc->write_buffer_size = max_buffer_size;

		bc->write_buffer = AllocPooled(bc->mempool, bc->write_buffer_size << bc->sector_shift);
		if (bc->write_buffer == NULL)
			goto cleanup;
	}

	AddMemHandler(&bc->mem_handler);

	return bc;

cleanup:
	DEBUGF("InitBlockCache failed\n");

	CleanupBlockCache(bc);

	return NULL;
}

void CleanupBlockCache(struct BlockCache *bc) {
	DEBUGF("CleanupBlockCache(%#p)\n", bc);

	if (bc != NULL) {
		struct DiskIO *dio = bc->dio_handle;

		RemMemHandler(&bc->mem_handler);

		DeletePool(bc->mempool);

		FreePooled(dio->mempool, bc, sizeof(*bc));
	}
}

static ULONG BlockChecksum(const ULONG *data, ULONG bytes) {
	ULONG next_sum, sum = 0;
	ULONG longs = bytes / sizeof(ULONG);

	DEBUGF("BlockChecksum(%#p, %u)\n", data, bytes);

	while (longs--) {
		next_sum = sum + *data++;
		if (next_sum < sum) next_sum++;
		sum = next_sum;
	}

	return sum;
}

static int CacheTreeCompareFunc(CONST_APTR key1, CONST_APTR key2) {
	UQUAD sector1 = *(const UQUAD *)key1;
	UQUAD sector2 = *(const UQUAD *)key2;

	if (sector1 > sector2)
		return 1;
	else if (sector1 < sector2)
		return -1;
	else
		return 0;
}

static void InsertSector(struct Splay **root, struct BlockCacheNode *bcn) {
	InsertSplay(root, CacheTreeCompareFunc, &bcn->splay, &bcn->sector);
}

static struct BlockCacheNode *FindSector(struct Splay **root, UQUAD sector) {
	struct Splay *sn;

	sn = FindSplay(root, CacheTreeCompareFunc, &sector);

	return (sn != NULL) ? BCNFROMSPLAY(sn) : NULL;
}

void ExpungeCacheNode(struct BlockCache *bc, struct BlockCacheNode *bcn) {
	if (bcn != NULL) {
		Remove((struct Node *)&bcn->node);
		RemoveSplay(&bc->cache_tree, &bcn->splay);

		switch (bcn->type) {

			case BCN_PROBATION:
				break;

			case BCN_PROTECTED:
				bc->num_protected_nodes--;
				break;

			case BCN_DIRTY:
				bc->num_dirty_nodes--;
				break;

		}
		bc->num_cache_nodes--;

		FreePooled(bc->mempool, bcn->data, bc->sector_size);
		FreePooled(bc->mempool, bcn, sizeof(struct BlockCacheNode));
	}
}

static void MoveMinList(struct MinList *dst, struct MinList *src) {
	if (!IsMinListEmpty(src)) {
		struct MinNode *head = src->mlh_Head;
		struct MinNode *tail = src->mlh_TailPred;

		dst->mlh_TailPred->mln_Succ = head;
		head->mln_Pred = dst->mlh_TailPred;

		dst->mlh_TailPred = tail;
		tail->mln_Succ = (struct MinNode *)&dst->mlh_Tail;

		NEWLIST(src);
	}
}

static int RangeTreeNeighborFunc(CONST_APTR key1, CONST_APTR key2) {
	UQUAD sector = *(const UQUAD *)key1;
	const struct BlockRange *range = key2;
	UQUAD first = range->first;
	UQUAD last  = range->last;

	if (first > 0) first--;
	if (last < UINT64_MAX) last++;

	if (sector > last)
		return 1;
	else if (sector < first)
		return -1;
	else
		return 0;
}

static int RangeTreeCompareFunc(CONST_APTR key1, CONST_APTR key2) {
	const struct BlockRange *range1 = key1;
	const struct BlockRange *range2 = key2;

	if (range1->first > range2->last)
		return 1;
	else if (range1->last < range2->first)
		return -1;
	else
		return 0;
}

static struct BlockRangeNode *GetBlockRange(struct BlockCache *bc, UQUAD sector) {
	struct Splay *sn;
	struct BlockRangeNode *brn;

	sn = FindSplay(&bc->range_tree, RangeTreeNeighborFunc, &sector);
	if (sn != NULL) {
		brn = BRNFROMSPLAY(sn);
		if (sector < brn->range.first)
			brn->range.first = sector;
		else
			brn->range.last = sector;
	} else {
		brn = AllocPooled(bc->mempool, sizeof(struct BlockRangeNode));
		if (brn != NULL) {
			brn->range.first = sector;
			brn->range.last  = sector;

			NEWLIST(&brn->list);

			InsertSplay(&bc->range_tree, RangeTreeCompareFunc, &brn->splay, &brn->range);
			AddHead((struct List *)&bc->dirty_list, (struct Node *)&brn->node);
		}
	}

	return brn;
}

static void ExpungeBlockRange(struct BlockCache *bc, struct BlockRangeNode *brn) {
	if (brn != NULL) {
		Remove((struct Node *)&brn->node);
		RemoveSplay(&bc->range_tree, &brn->splay);

		FreePooled(bc->mempool, brn, sizeof(struct BlockRangeNode));
	}
}

static struct BlockRangeNode *MergeBlockRanges(struct BlockCache *bc, struct BlockRangeNode *brn1, struct BlockRangeNode *brn2) {
	struct MinNode *node, *succ;
	struct BlockCacheNode *bcn;

	brn1->range.last = brn2->range.last;

	/* Need to update the range_node pointers. */
	for (node = brn2->list.mlh_Head; (succ = node->mln_Succ) != NULL; node = succ) {
		bcn = BCNFROMNODE(node);
		bcn->range_node = brn1;
	}

	MoveMinList(&brn1->list, &brn2->list);

	ExpungeBlockRange(bc, brn2);

	return brn1;
}

static struct BlockRangeNode *AddToBlockRange(struct BlockCache *bc, struct BlockRangeNode *brn, struct BlockCacheNode *bcn) {
	struct Splay *sn;
	struct BlockRangeNode *brn2;

	if (bcn->sector == brn->range.first) {
		AddHead((struct List *)&brn->list, (struct Node *)&bcn->node);

		sn = PrevSplay(&brn->splay);
		if (sn != NULL) {
			brn2 = BRNFROMSPLAY(sn);
			if ((brn2->range.last + 1) == brn->range.first)
				brn = MergeBlockRanges(bc, brn2, brn);
		}
	} else {
		AddTail((struct List *)&brn->list, (struct Node *)&bcn->node);

		sn = NextSplay(&brn->splay);
		if (sn != NULL) {
			brn2 = BRNFROMSPLAY(sn);
			if ((brn->range.last + 1) == brn2->range.first)
				brn = MergeBlockRanges(bc, brn, brn2);
		}
	}

	return brn;
}

static struct BlockCacheNode *AddSector(struct BlockCache *bc, UQUAD sector, BOOL dirty) {
	struct BlockCacheNode *bcn;
	struct BlockRangeNode *brn;
	APTR data;

	bcn = AllocPooled(bc->mempool, sizeof(struct BlockCacheNode));
	data = AllocPooled(bc->mempool, bc->sector_size);
	if (bcn != NULL && data != NULL) {
		if (dirty) brn = GetBlockRange(bc, sector);

		if (dirty == FALSE || brn != NULL) {
			bcn->sector     = sector;
			bcn->data       = data;
			bcn->range_node = NULL;
			bcn->checksum   = (ULONG)-1;

			if (dirty == FALSE) {
				bcn->type = BCN_PROBATION;
				AddHead((struct List *)&bc->probation_list, (struct Node *)&bcn->node);
			} else {
				bcn->type       = BCN_DIRTY;
				bcn->range_node = AddToBlockRange(bc, brn, bcn);
				bc->num_dirty_nodes++;
			}

			InsertSector(&bc->cache_tree, bcn);
			bc->num_cache_nodes++;

			if (bc->num_cache_nodes > bc->max_cache_nodes)
				ExpungeCacheNode(bc, BCNFROMNODE(bc->probation_list.mlh_TailPred));

			return bcn;
		}
	}

	if (data != NULL) FreePooled(bc->mempool, data, bc->sector_size);
	if (bcn != NULL) FreePooled(bc->mempool, bcn, sizeof(struct BlockCacheNode));

	return NULL;
}

static void CacheHit(struct BlockCache *bc, struct BlockCacheNode *bcn) {
	struct BlockRangeNode *brn;

	switch (bcn->type) {

		case BCN_PROBATION:
			Remove((struct Node *)&bcn->node);

			bcn->type = BCN_PROTECTED;
			AddHead((struct List *)&bc->protected_list, (struct Node *)&bcn->node);
			bc->num_protected_nodes++;

			if (bc->num_protected_nodes > bc->max_protected_nodes) {
				bcn = BCNFROMNODE(bc->protected_list.mlh_TailPred);
				Remove((struct Node *)&bcn->node);
				bc->num_protected_nodes--;

				bcn->type = BCN_PROBATION;
				AddHead((struct List *)&bc->probation_list, (struct Node *)&bcn->node);
			}
			break;

		case BCN_PROTECTED:
			if (bc->protected_list.mlh_Head != &bcn->node) {
				Remove((struct Node *)&bcn->node);
				AddHead((struct List *)&bc->protected_list, (struct Node *)&bcn->node);
			}
			break;

		case BCN_DIRTY:
			brn = bcn->range_node;
			if (bc->dirty_list.mlh_Head != &brn->node) {
				Remove((struct Node *)&brn->node);
				AddHead((struct List *)&bc->dirty_list, (struct Node *)&brn->node);
			}
			break;

	}
}

static struct BlockRangeNode *SplitBlockRange(struct BlockCache *bc, struct BlockRangeNode *brn1, struct BlockCacheNode *bcn) {
	struct BlockRangeNode *brn2;
	struct MinNode *node, *succ;
	struct BlockCacheNode *bcn2;

	brn2 = AllocPooled(bc->mempool, sizeof(struct BlockRangeNode));
	if (brn2 != NULL) {
		brn1->range.last  = bcn->sector;

		brn2->range.first = bcn->sector + 1;
		brn2->range.last  = brn1->range.last;

		NEWLIST(&brn2->list);

		/* Need to update the range_node pointers. */
		for (node = bcn->node.mln_Succ; (succ = node->mln_Succ) != NULL; node = succ) {
			bcn2 = BCNFROMNODE(node);
			bcn2->range_node = brn2;
		}

		/* Connect the tail nodes to the new list */
		brn2->list.mlh_Head = bcn->node.mln_Succ;
		brn2->list.mlh_Head->mln_Pred = brn2->list.mlh_TailPred;
		brn2->list.mlh_TailPred = brn1->list.mlh_TailPred;
		brn2->list.mlh_TailPred->mln_Succ = (struct MinNode *)&brn2->list.mlh_Tail;

		/* And disconnect them from the old list */
		brn1->list.mlh_TailPred = &bcn->node;
		bcn->node.mln_Succ = (struct MinNode *)&brn1->list.mlh_Tail;

		InsertSplay(&bc->range_tree, RangeTreeCompareFunc, &brn2->splay, &brn2->range);
		Insert((struct List *)&bc->dirty_list, (struct Node *)&brn2->node, (struct Node *)&brn1->node);
	}

	return brn2;
}

static BOOL ClearDirty(struct BlockCache *bc, struct BlockCacheNode *bcn) {
	struct BlockRangeNode *brn;

	if (bcn->type != BCN_DIRTY)
		return FALSE;

	brn = bcn->range_node;
	if (bcn->sector == brn->range.first)
		brn->range.first++;
	else if (bcn->sector == brn->range.last)
		brn->range.last--;
	else if (!SplitBlockRange(bc, brn, bcn))
		return FALSE;

	Remove((struct Node *)&bcn->node);
	bc->num_dirty_nodes--;

	bcn->type     = BCN_PROBATION;
	bcn->checksum = BlockChecksum(bcn->data, bc->sector_size);

	AddHead((struct List *)&bc->probation_list, (struct Node *)&bcn->node);

	/* Remove block range if empty. */
	if (IsMinListEmpty(&brn->list))
		ExpungeBlockRange(bc, brn);

	return TRUE;
}

static BOOL SetDirty(struct BlockCache *bc, struct BlockCacheNode *bcn) {
	struct BlockRangeNode *brn;

	if (bcn->type == BCN_DIRTY)
		return FALSE;

	brn = GetBlockRange(bc, bcn->sector);
	if (brn == NULL)
		return FALSE;

	Remove((struct Node *)&bcn->node);
	switch (bcn->type) {

		case BCN_PROBATION:
			break;

		case BCN_PROTECTED:
			bc->num_protected_nodes--;
			break;

	}

	bcn->type     = BCN_DIRTY;
	bcn->checksum = (ULONG)-1;

	bcn->range_node = AddToBlockRange(bc, brn, bcn);
	bc->num_dirty_nodes++;

	return TRUE;
}

BOOL ReadCacheNode(struct BlockCache *bc, UQUAD sector, APTR buffer, ULONG flags) {
	struct BlockCacheNode *bcn;
	BOOL result = FALSE;

	DEBUGF("ReadCacheNode(%#p, %llu, %#p, 0x%08x)\n", bc, sector, buffer, flags);

	ObtainSemaphore(&bc->cache_semaphore);

	bcn = FindSector(&bc->cache_tree, sector);
	if (bcn != NULL) {
		if (bcn->type == BCN_DIRTY) {
			result = TRUE;

			if (buffer != NULL)
				CopyMem(bcn->data, buffer, bc->sector_size);

			CacheHit(bc, bcn);
		} else if ((flags & RCN_DIRTY_ONLY) == 0) {
			if (BlockChecksum(bcn->data, bc->sector_size) == bcn->checksum) {
				result = TRUE;

				if (buffer != NULL)
					CopyMem(bcn->data, buffer, bc->sector_size);

				CacheHit(bc, bcn);
			} else {
				/* Throw away corrupted cache data */
				ExpungeCacheNode(bc, bcn);
			}
		}
	}

	ReleaseSemaphore(&bc->cache_semaphore);

	return result;
}

BOOL StoreCacheNode(struct BlockCache *bc, UQUAD sector, CONST_APTR buffer, ULONG flags) {
	struct BlockCacheNode *bcn;
	BOOL result = FALSE;

	DEBUGF("StoreCacheNode(%#p, %llu, %#p, 0x%08x)\n", bc, sector, buffer, flags);

	ObtainSemaphore(&bc->cache_semaphore);

	bcn = FindSector(&bc->cache_tree, sector);
	if (bcn != NULL) {
		result = TRUE;

		CopyMem((APTR)buffer, bcn->data, bc->sector_size);

		if (bcn->type != BCN_DIRTY)
			bcn->checksum = BlockChecksum(bcn->data, bc->sector_size);
		else if ((flags & SCN_CLEAR_DIRTY) != 0)
			ClearDirty(bc, bcn);

		CacheHit(bc, bcn);
	} else {
		if ((flags & SCN_UPDATE_ONLY) == 0) {
			bcn = AddSector(bc, sector, FALSE);
			if (bcn != NULL) {
				result = TRUE;

				CopyMem(buffer, bcn->data, bc->sector_size);

				bcn->checksum = BlockChecksum(bcn->data, bc->sector_size);
			}
		}
	}

	ReleaseSemaphore(&bc->cache_semaphore);

	return result;
}

BOOL WriteCacheNode(struct BlockCache *bc, UQUAD sector, CONST_APTR buffer, ULONG flags) {
	struct BlockCacheNode *bcn;
	BOOL result = FALSE;

	DEBUGF("WriteCacheNode(%#p, %llu, %#p, 0x%08x)\n", bc, sector, buffer, flags);

	if (bc->write_cache_enabled == FALSE)
		return FALSE;

	ObtainSemaphore(&bc->cache_semaphore);

	bcn = FindSector(&bc->cache_tree, sector);
	if (bcn != NULL) {
		if (bcn->type != BCN_DIRTY && bc->num_dirty_nodes < bc->max_dirty_nodes)
			SetDirty(bc, bcn);

		if (bcn->type == BCN_DIRTY) {
			result = TRUE;

			CopyMem((CONST_APTR)buffer, bcn->data, bc->sector_size);
		}

		CacheHit(bc, bcn);
	} else {
		if (bc->num_dirty_nodes < bc->max_dirty_nodes) {
			bcn = AddSector(bc, sector, TRUE);
			if (bcn != NULL) {
				result = TRUE;

				CopyMem(buffer, bcn->data, bc->sector_size);

				bcn->checksum = (ULONG)-1;
			}
		}
	}

	ReleaseSemaphore(&bc->cache_semaphore);

	return result;
}

BOOL FlushDirtyNodes(struct BlockCache *bc, ULONG max_dirty_nodes) {
	struct MinNode *node, *pred, *succ;
	struct BlockRangeNode *brn;
	struct BlockCacheNode *bcn;
	UQUAD sector;
	ULONG sectors, done, todo;
	ULONG errors = 0;
	APTR buffer;
	LONG res;

	DEBUGF("FlushDirtyNodes(%#p, %u)\n", bc, max_dirty_nodes);

	if (bc->write_cache_enabled == FALSE)
		return TRUE;

	if (IsMinListEmpty(&bc->dirty_list))
		return TRUE;

	for (node = bc->dirty_list.mlh_TailPred; (pred = node->mln_Pred) != NULL; node = pred) {
		brn = BRNFROMNODE(node);

		sector = brn->range.first;
		node   = brn->list.mlh_Head;
		done   = 0;
		todo   = brn->range.last - brn->range.first + 1;

		do {
			buffer  = bc->write_buffer;
			sectors = 0;

			while (sectors < bc->write_buffer_size && (succ = node->mln_Succ) != NULL) {
				bcn = BCNFROMNODE(node);
				CopyMem(bcn->data, buffer, bc->sector_size);
				buffer += bc->sector_size;
				sectors++;
				node = succ;
			}

			res = DeviceWriteBlocks(bc->dio_handle, sector, bc->write_buffer, sectors);
			if (res == DIO_SUCCESS) {
				sector += sectors;
				done += sectors;
			} else {
				errors++;
			}

			if (max_dirty_nodes && (bc->num_dirty_nodes - done) <= max_dirty_nodes)
				break;
		} while (res == DIO_SUCCESS && done < todo);

		if (done > 0) {
			ObtainSemaphore(&bc->cache_semaphore);

			node    = brn->list.mlh_Head;
			sectors = 0;

			while (sectors < done && (succ = node->mln_Succ) != NULL) {
				ClearDirty(bc, BCNFROMNODE(node));
				sectors++;
				node = succ;
			}

			ReleaseSemaphore(&bc->cache_semaphore);

			if (max_dirty_nodes && bc->num_dirty_nodes <= max_dirty_nodes)
				break;
		}
	}

	return (errors == 0) ? TRUE : FALSE;
}

