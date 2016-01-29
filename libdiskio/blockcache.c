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

#define PERCENT_DIRTY 30

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

	NEWLIST(&bc->clean_list);
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

	bc->max_dirty_nodes = ((UQUAD)bc->max_cache_nodes * PERCENT_DIRTY + 50) / 100;

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

void ExpungeCacheNode(struct BlockCache *bc, struct BlockCacheNode *bcn) {
	if (bcn != NULL) {
		Remove((struct Node *)&bcn->node);
		RemoveSplay(&bc->cache_tree, &bcn->splay);

		if (bcn->dirty) bc->num_dirty_nodes--;

		bc->num_cache_nodes--;

		FreePooled(bc->mempool, bcn->data, bc->sector_size);
		FreePooled(bc->mempool, bcn, sizeof(struct BlockCacheNode));
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

static void CacheHit(struct BlockCache *bc, struct BlockCacheNode *bcn) {
	if (bcn->dirty) {
		if (bc->dirty_list.mlh_Head != &bcn->node) {
			Remove((struct Node *)&bcn->node);
			AddHead((struct List *)&bc->dirty_list, (struct Node *)&bcn->node);
		}
	} else {
		if (bc->clean_list.mlh_Head != &bcn->node) {
			Remove((struct Node *)&bcn->node);
			AddHead((struct List *)&bc->clean_list, (struct Node *)&bcn->node);
		}
	}
}

static BOOL ClearDirty(struct BlockCache *bc, struct BlockCacheNode *bcn) {
	if (bcn->dirty == FALSE)
		return FALSE;

	Remove((struct Node *)&bcn->node);
	bc->num_dirty_nodes--;

	bcn->dirty    = FALSE;
	bcn->checksum = BlockChecksum(bcn->data, bc->sector_size);

	AddHead((struct List *)&bc->clean_list, (struct Node *)&bcn->node);

	return TRUE;
}

static BOOL SetDirty(struct BlockCache *bc, struct BlockCacheNode *bcn) {
	if (bcn->dirty)
		return FALSE;

	Remove((struct Node *)&bcn->node);

	bcn->dirty    = TRUE;
	bcn->checksum = (ULONG)-1;

	AddHead((struct List *)&bc->dirty_list, (struct Node *)&bcn->node);
	bc->num_dirty_nodes++;

	return TRUE;
}

BOOL ReadCacheNode(struct BlockCache *bc, UQUAD sector, APTR buffer, ULONG flags) {
	struct Splay *sn;
	struct BlockCacheNode *bcn;
	BOOL result = FALSE;

	DEBUGF("ReadCacheNode(%#p, %llu, %#p, 0x%08x)\n", bc, sector, buffer, flags);

	ObtainSemaphore(&bc->cache_semaphore);

	sn = FindSplay(&bc->cache_tree, CacheTreeCompareFunc, &sector);
	if (sn != NULL) {
		bcn = BCNFROMSPLAY(sn);

		if (bcn->dirty) {
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
	struct Splay *sn;
	struct BlockCacheNode *bcn;
	BOOL result = FALSE;

	DEBUGF("StoreCacheNode(%#p, %llu, %#p, 0x%08x)\n", bc, sector, buffer, flags);

	ObtainSemaphore(&bc->cache_semaphore);

	sn = FindSplay(&bc->cache_tree, CacheTreeCompareFunc, &sector);
	if (sn != NULL) {
		result = TRUE;

		bcn = BCNFROMSPLAY(sn);

		CopyMem((APTR)buffer, bcn->data, bc->sector_size);

		if (bcn->dirty == FALSE)
			bcn->checksum = BlockChecksum(bcn->data, bc->sector_size);
		else if ((flags & SCN_CLEAR_DIRTY) != 0)
			ClearDirty(bc, bcn);

		CacheHit(bc, bcn);
	} else if ((flags & SCN_UPDATE_ONLY) == 0) {
		bcn = NULL;

		if (bc->num_cache_nodes < bc->max_cache_nodes) {
			bcn = AllocPooled(bc->mempool, sizeof(struct BlockCacheNode));
			if (bcn != NULL) {
				bcn->data = AllocPooled(bc->mempool, bc->sector_size);
				if (bcn->data == NULL) {
					FreePooled(bc->mempool, bcn, sizeof(struct BlockCacheNode));
					bcn = NULL;
				}
			}
		}

		if (bcn == NULL) {
			struct MinNode *node;
			node = (struct MinNode *)RemTail((struct List *)&bc->clean_list);
			if (node != NULL) {
				bcn = BCNFROMNODE(node);

				RemoveSplay(&bc->cache_tree, &bcn->splay);

				bc->num_cache_nodes--;
			}
		}

		if (bcn != NULL) {
			result = TRUE;

			CopyMem(buffer, bcn->data, bc->sector_size);

			bcn->sector   = sector;
			bcn->dirty    = FALSE;
			bcn->checksum = BlockChecksum(bcn->data, bc->sector_size);

			InsertSplay(&bc->cache_tree, CacheTreeCompareFunc, &bcn->splay, &bcn->sector);
			AddHead((struct List *)&bc->clean_list, (struct Node *)&bcn->node);

			bc->num_cache_nodes++;
		}
	}

	ReleaseSemaphore(&bc->cache_semaphore);

	return result;
}

BOOL WriteCacheNode(struct BlockCache *bc, UQUAD sector, CONST_APTR buffer, ULONG flags) {
	struct Splay *sn;
	struct BlockCacheNode *bcn;
	BOOL result = FALSE;

	DEBUGF("WriteCacheNode(%#p, %llu, %#p, 0x%08x)\n", bc, sector, buffer, flags);

	if (bc->write_cache_enabled == FALSE)
		return FALSE;

	ObtainSemaphore(&bc->cache_semaphore);

	sn = FindSplay(&bc->cache_tree, CacheTreeCompareFunc, &sector);
	if (sn != NULL) {
		bcn = BCNFROMSPLAY(sn);

		if (bcn->dirty == FALSE && bc->num_dirty_nodes < bc->max_dirty_nodes)
			SetDirty(bc, bcn);

		if (bcn->dirty) {
			result = TRUE;

			CopyMem((CONST_APTR)buffer, bcn->data, bc->sector_size);
		}

		CacheHit(bc, bcn);
	} else if (bc->num_dirty_nodes < bc->max_dirty_nodes) {
		bcn = NULL;

		if (bc->num_cache_nodes < bc->max_cache_nodes) {
			bcn = AllocPooled(bc->mempool, sizeof(struct BlockCacheNode));
			if (bcn != NULL) {
				bcn->data = AllocPooled(bc->mempool, bc->sector_size);
				if (bcn->data == NULL) {
					FreePooled(bc->mempool, bcn, sizeof(struct BlockCacheNode));
					bcn = NULL;
				}
			}
		}

		if (bcn == NULL) {
			struct MinNode *node;
			node = (struct MinNode *)RemTail((struct List *)&bc->clean_list);
			if (node != NULL) {
				bcn = BCNFROMNODE(node);

				RemoveSplay(&bc->cache_tree, &bcn->splay);

				bc->num_cache_nodes--;
			}
		}

		if (bcn != NULL) {
			result = TRUE;

			CopyMem(buffer, bcn->data, bc->sector_size);

			bcn->sector   = sector;
			bcn->dirty    = TRUE;
			bcn->checksum = -1;

			InsertSplay(&bc->cache_tree, CacheTreeCompareFunc, &bcn->splay, &bcn->sector);
			AddHead((struct List *)&bc->dirty_list, (struct Node *)&bcn->node);

			bc->num_cache_nodes++;
			bc->num_dirty_nodes++;
		}
	}

	ReleaseSemaphore(&bc->cache_semaphore);

	return result;
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

BOOL FlushDirtyNodes(struct BlockCache *bc) {
	struct DiskIO *dio = bc->dio_handle;
	struct MinNode *node, *succ;
	struct BlockCacheNode *bcn;
	struct MinList write_list;
	struct MinList failed_write_list;
	UQUAD sector;
	ULONG sectors;
	ULONG errors = 0;
	APTR buffer;

	DEBUGF("FlushDirtyNodes(%#p)\n", bc);

	if (bc->write_cache_enabled == FALSE)
		return TRUE;

	if (IsMinListEmpty(&bc->dirty_list))
		return TRUE;

	ObtainSemaphore(&bc->cache_semaphore);

	SortCacheNodes(&bc->dirty_list);

	NEWLIST(&write_list);
	NEWLIST(&failed_write_list);

	sectors = 0;
	sector = -1;
	buffer = NULL;
	do {
		node = (struct MinNode *)RemHead((struct List *)&bc->dirty_list);
		if (node != NULL)
			bcn = BCNFROMNODE(node);
		else
			bcn = NULL;

		if (sectors > 0 && (bcn == NULL || sectors == bc->write_buffer_size ||
			(sector + sectors) != bcn->sector))
		{
			LONG res;

			res = CachedWriteBlocks(dio, sector, bc->write_buffer, sectors);
			if (res == DIO_SUCCESS) {
				for (node = write_list.mlh_Head; (succ = node->mln_Succ) != NULL; node = succ)
					ClearDirty(bc, BCNFROMNODE(node));
			} else {
				MoveMinList(&failed_write_list, &write_list);
				errors++;
			}

			sectors = 0;
		}

		if (bcn != NULL) {
			AddTail((struct List *)&write_list, (struct Node *)&bcn->node);

			if (sectors++ == 0) {
				sector = bcn->sector;
				buffer = bc->write_buffer;
			}

			CopyMem(bcn->data, buffer, bc->sector_size);
			buffer += bc->sector_size;
		}
	} while (bcn != NULL);
	MoveMinList(&bc->dirty_list, &failed_write_list);

	ReleaseSemaphore(&bc->cache_semaphore);

	return (errors == 0) ? TRUE : FALSE;
}

