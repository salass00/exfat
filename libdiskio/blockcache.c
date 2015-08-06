/**
 * Copyright (c) 2015 Fredrik Wikstrom <fredrik@a500.org>
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

#ifndef DISABLE_BLOCK_CACHE

#include "splaytree.h"
#include <SDI/SDI_hook.h>

HOOKPROTO(CacheTreeHookFunc, SIPTR, const UQUAD *key1, const UQUAD *key2) {
	if (*key1 > *key2)
		return 1;
	else if (*key1 < *key2)
		return -1;
	else
		return 0;
}

MakeHook(CacheTreeHook, CacheTreeHookFunc);

#ifdef __AROS__
AROS_UFH5(int, DIO_MemHandler,
	AROS_UFHA(APTR, data, A1),
	AROS_UFHA(APTR, code, A5),
	AROS_UFHA(struct ExecBase *, SysBase, A6),
	AROS_UFHA(APTR, mask, D1),
	AROS_UFHA(APTR, custom, A0))
{
	AROS_USERFUNC_INIT
#else
SAVEDS ASM int DIO_MemHandler(REG(a0, APTR custom), REG(a1, APTR data)) {
#endif
	DEBUGF("DIO_MemHandler(%#p, %#p)\n", custom, data);

	struct BlockCache *bc = data;
	struct BlockCacheNode *cn;
	int cache_nodes_freed = 0;

	if (!AttemptSemaphore(&bc->cache_semaphore))
		return MEM_DID_NOTHING;

	while ((cn = (struct BlockCacheNode *)RemTail((struct List *)&bc->clean_list))) {
		RemoveSplayNode(bc->cache_tree, &cn->sector);
		FreePooled(bc->mempool, cn->data, bc->sector_size);
		FreePooled(bc->mempool, cn, sizeof(*cn));
		bc->num_cache_nodes--;
		bc->num_clean_nodes--;
		cache_nodes_freed++;
	}

	ReleaseSemaphore(&bc->cache_semaphore);

	return cache_nodes_freed ? MEM_ALL_DONE : MEM_DID_NOTHING;

#ifdef __AROS__
	AROS_USERFUNC_EXIT
#endif
}

struct BlockCache *InitBlockCache(struct DiskIO *dio) {
	DEBUGF("InitBlockCache(%#p)\n", dio);

	struct BlockCache *bc = NULL;

	bc = AllocPooled(dio->mempool, sizeof(*bc));
	if (bc == NULL)
		return NULL;

	bzero(bc, sizeof(*bc));

	bc->dio_handle   = dio;
	bc->sector_size  = dio->sector_size;
	bc->rw_buffer    = dio->rw_buffer;
	NEWLIST(&bc->clean_list);
	NEWLIST(&bc->dirty_list);

	InitSemaphore(&bc->cache_semaphore);

	bc->mem_handler.is_Node.ln_Type = NT_INTERRUPT;
	bc->mem_handler.is_Node.ln_Pri  = 50;
	bc->mem_handler.is_Node.ln_Name = (char *)dio->devname;

	bc->mem_handler.is_Data = bc;
	bc->mem_handler.is_Code = (void (*)())DIO_MemHandler;

	bc->mempool = CreatePool(MEMF_PUBLIC,
		dio->sector_size << 4,
		dio->sector_size << 3);
	if (bc->mempool == NULL)
		goto cleanup;

	bc->cache_tree = CreateSplayTree(&CacheTreeHook);
	if (bc->cache_tree == NULL)
		goto cleanup;

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

		DeleteSplayTree(bc->cache_tree);

		FreePooled(dio->mempool, bc, sizeof(*bc));
	}
}

static ULONG BlockChecksum(const ULONG *data, ULONG bytes) {
	DEBUGF("BlockChecksum(%#p, %u)\n", data, (unsigned)bytes);

	ULONG next_sum, sum = 0;
	ULONG longs = bytes / sizeof(*data);
	while (longs--) {
		next_sum = sum + *data++;
		if (next_sum < sum) next_sum++;
		sum = next_sum;
	}
	return sum;
}

#ifdef DEBUG
static BOOL NodeInMinList(struct MinList *list, struct MinNode *node) {
	struct MinNode *n;
	for (n = list->mlh_Head; n->mln_Succ != NULL; n = n->mln_Succ) {
		if (n == node)
			return TRUE;
	}
	return FALSE;
}
#endif

BOOL BlockCacheRetrieve(struct BlockCache *bc, UQUAD sector, APTR buffer, BOOL dirty_only) {
	DEBUGF("BlockCacheRetrieve(%#p, %llu, %#p, %d)\n", bc, sector, buffer, dirty_only);

	BOOL res = FALSE;
	struct BlockCacheNode *cache;

	ObtainSemaphore(&bc->cache_semaphore);

	cache = FindSplayNode(bc->cache_tree, &sector);
	if (cache != NULL) {
#ifdef DEBUG
		if (cache->dirty) {
			if (!NodeInMinList(&bc->dirty_list, &cache->node))
				DEBUGF("Node not in dirty list: %#p\n", cache);
		} else {
			if (!NodeInMinList(&bc->clean_list, &cache->node))
				DEBUGF("Node not in clean list: %#p\n", cache);
		}
#endif
		if (cache->dirty) {
			if (buffer != NULL)
				CopyMem(cache->data, buffer, bc->sector_size);
			if (bc->dirty_list.mlh_Head != &cache->node) {
				Remove((struct Node *)cache);
				AddHead((struct List *)&bc->dirty_list, (struct Node *)cache);
			}
			res = TRUE;
		} else if (!dirty_only) {
			if (BlockChecksum(cache->data, bc->sector_size) == cache->checksum) {
				if (buffer != NULL)
					CopyMem(cache->data, buffer, bc->sector_size);
				if (bc->clean_list.mlh_Head != &cache->node) {
					Remove((struct Node *)cache);
					AddHead((struct List *)&bc->clean_list, (struct Node *)cache);
				}
				res = TRUE;
			} else {
				Remove((struct Node *)cache);
				RemoveSplayNode(bc->cache_tree, &cache->sector);
				FreePooled(bc->mempool, cache->data, bc->sector_size);
				FreePooled(bc->mempool, cache, sizeof(*cache));
				bc->num_clean_nodes--;
				bc->num_cache_nodes--;
			}
		}
	}

	ReleaseSemaphore(&bc->cache_semaphore);

	DEBUGF("BlockCacheRetrieve: %d\n", (int)res);

	return res;
}

BOOL BlockCacheStore(struct BlockCache *bc, UQUAD sector, CONST_APTR buffer, BOOL update_only) {
	DEBUGF("BlockCacheStore(%#p, %llu, %#p, %d)\n", bc, sector, buffer, (int)update_only);

	BOOL res = FALSE;
	struct BlockCacheNode *cache;

	ObtainSemaphore(&bc->cache_semaphore);

	cache = FindSplayNode(bc->cache_tree, &sector);
	if (cache != NULL) {
#ifdef DEBUG
		if (cache->dirty) {
			if (!NodeInMinList(&bc->dirty_list, &cache->node))
				DEBUGF("Node not in dirty list: %#p\n", cache);
		} else {
			if (!NodeInMinList(&bc->clean_list, &cache->node))
				DEBUGF("Node not in clean list: %#p\n", cache);
		}
#endif
		CopyMem(buffer, cache->data, bc->sector_size);
		if (update_only && cache->dirty) {
			cache->dirty = FALSE;
			bc->num_dirty_nodes--;
			bc->num_clean_nodes++;
		}
		if (cache->dirty) {
			if (bc->dirty_list.mlh_Head != &cache->node) {
				Remove((struct Node *)cache);
				AddHead((struct List *)&bc->dirty_list, (struct Node *)cache);
			}
		} else {
			cache->checksum = BlockChecksum(cache->data, bc->sector_size);
			if (bc->clean_list.mlh_Head != &cache->node) {
				Remove((struct Node *)cache);
				AddHead((struct List *)&bc->clean_list, (struct Node *)cache);
			}
		}
		res = TRUE;
	} else if (!update_only) {
		cache = NULL;
		if (bc->num_cache_nodes < MAX_CACHE_NODES) {
			cache = AllocPooled(bc->mempool, sizeof(*cache));
			if (cache != NULL) {
				cache->data = AllocPooled(bc->mempool, bc->sector_size);
				if (cache->data == NULL) {
					FreePooled(bc->mempool, cache, sizeof(*cache));
					cache = NULL;
				}
			}
		}
		if (cache == NULL) {
			cache = (struct BlockCacheNode *)RemTail((struct List *)&bc->clean_list);
			if (cache != NULL) {
				RemoveSplayNode(bc->cache_tree, &cache->sector);
				bc->num_cache_nodes--;
				bc->num_clean_nodes--;
			}
		}
		if (cache != NULL) {
			cache->sector = sector;
			res = InsertSplayNode(bc->cache_tree, &cache->sector, cache);
			if (res != FALSE) {
				CopyMem(buffer, cache->data, bc->sector_size);
				cache->checksum = BlockChecksum(cache->data, bc->sector_size);
				cache->dirty = FALSE;
				AddHead((struct List *)&bc->clean_list, (struct Node *)cache);
				bc->num_cache_nodes++;
				bc->num_clean_nodes++;
			} else {
				FreePooled(bc->mempool, cache->data, bc->sector_size);
				FreePooled(bc->mempool, cache, sizeof(*cache));
			}
		}
	}

	ReleaseSemaphore(&bc->cache_semaphore);

	DEBUGF("BlockCacheStore: %d\n", (int)res);

	return res;
}

BOOL BlockCacheWrite(struct BlockCache *bc, UQUAD sector, CONST_APTR buffer) {
	DEBUGF("BlockCacheWrite(%#p, %llu, %#p)\n", bc, sector, buffer);

	BOOL res = FALSE;
	struct BlockCacheNode *cache;

	ObtainSemaphore(&bc->cache_semaphore);

	cache = FindSplayNode(bc->cache_tree, &sector);
	if (cache != NULL) {
#ifdef DEBUG
		if (cache->dirty) {
			if (!NodeInMinList(&bc->dirty_list, &cache->node))
				DEBUGF("Node not in dirty list: %#p\n", cache);
		} else {
			if (!NodeInMinList(&bc->clean_list, &cache->node))
				DEBUGF("Node not in clean list: %#p\n", cache);
		}
#endif
		CopyMem(buffer, cache->data, bc->sector_size);
		if (!cache->dirty) {
			cache->checksum = -1;
			cache->dirty = TRUE;
			bc->num_clean_nodes--;
			bc->num_dirty_nodes++;
		}
		if (bc->dirty_list.mlh_Head != &cache->node) {
			Remove((struct Node *)cache);
			AddHead((struct List *)&bc->dirty_list, (struct Node *)cache);
		}
		res = TRUE;
	} else if (bc->num_dirty_nodes < MAX_DIRTY_NODES) {
		cache = NULL;
		if (bc->num_cache_nodes < MAX_CACHE_NODES) {
			cache = AllocPooled(bc->mempool, sizeof(*cache));
			if (cache != NULL) {
				cache->data = AllocPooled(bc->mempool, bc->sector_size);
				if (cache->data == NULL) {
					FreePooled(bc->mempool, cache, sizeof(*cache));
					cache = NULL;
				}
			}
		}
		if (cache == NULL) {
			cache = (struct BlockCacheNode *)RemTail((struct List *)&bc->clean_list);
			if (cache != NULL) {
				RemoveSplayNode(bc->cache_tree, &cache->sector);
				bc->num_cache_nodes--;
				bc->num_clean_nodes--;
			}
		}
		if (cache != NULL) {
			cache->sector = sector;
			res = InsertSplayNode(bc->cache_tree, &cache->sector, cache);
			if (res != FALSE) {
				CopyMem(buffer, cache->data, bc->sector_size);
				cache->checksum = -1;
				cache->dirty = TRUE;
				AddHead((struct List *)&bc->dirty_list, (struct Node *)cache);
				bc->num_cache_nodes++;
				bc->num_dirty_nodes++;
			} else {
				FreePooled(bc->mempool, cache->data, bc->sector_size);
				FreePooled(bc->mempool, cache, sizeof(*cache));
			}
		}
	}

	ReleaseSemaphore(&bc->cache_semaphore);

	DEBUGF("BlockCacheWrite: %d\n", (int)res);

	return res;
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

BOOL BlockCacheFlush(struct BlockCache *bc) {
	DEBUGF("BlockCacheFlush(%#p)\n", bc);

	struct DiskIO *dio = bc->dio_handle;
	struct BlockCacheNode *cache, *tcache;
	struct MinList write_list;
	struct MinList failed_write_list;
	UQUAD sector;
	ULONG sectors;
	APTR buffer;
	BOOL res;

	if (IsMinListEmpty(&bc->dirty_list)) {
		DEBUGF("BlockCacheFlush - no dirty blocks in cache\n");
		return TRUE;
	}

	ObtainSemaphore(&bc->cache_semaphore);

	SortBlockCacheNodes(&bc->dirty_list);

	NEWLIST(&write_list);
	NEWLIST(&failed_write_list);

	sectors = 0;
	sector = -1;
	buffer = NULL;
	do {
		cache = (struct BlockCacheNode *)RemHead((struct List *)&bc->dirty_list);
		if (sectors > 0 && (cache == NULL || sectors == RW_BUFFER_SIZE ||
			(sector + sectors) != cache->sector))
		{
			res = WriteBlocksUncached(dio, sector, bc->rw_buffer, sectors);
			if (res == 0) {
				while ((tcache = (struct BlockCacheNode *)RemHead((struct List *)&write_list)) != NULL) {
					tcache->checksum = BlockChecksum(tcache->data, bc->sector_size);
					tcache->dirty = FALSE;
					bc->num_dirty_nodes--;
					bc->num_clean_nodes++;
					AddHead((struct List *)&bc->clean_list, (struct Node *)tcache);
				}
			} else
				MoveMinList(&failed_write_list, &write_list);
			sectors = 0;
		}
		if (cache != NULL) {
			AddTail((struct List *)&write_list, (struct Node *)cache);

			if (sectors++ == 0) {
				sector = cache->sector;
				buffer = bc->rw_buffer;
			}

			CopyMem(cache->data, buffer, bc->sector_size);
			buffer += bc->sector_size;
		}
	} while (cache != NULL);
	MoveMinList(&bc->dirty_list, &failed_write_list);

	res = IsMinListEmpty(&bc->dirty_list);
	DEBUGF("BlockCacheFlush: %d\n", (int)res);

	ReleaseSemaphore(&bc->cache_semaphore);

	return res;
}

#endif

