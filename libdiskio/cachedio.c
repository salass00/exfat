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

LONG CachedReadBlocks(struct DiskIO *dio, UQUAD block, APTR buffer, ULONG blocks) {
	struct BlockCache *bc = dio->block_cache;
	LONG res;

	DEBUGF("CachedReadBlocks(%#p, %llu, %#p, %u)\n", dio, block, buffer, blocks);

	if (bc == NULL)
		return DeviceReadBlocks(dio, block, buffer, blocks);

	if (block >= dio->total_sectors || (block + (UQUAD)blocks) > dio->total_sectors)
		return DIO_ERROR_OUTOFBOUNDS;

	if (blocks == 0)
		return DIO_SUCCESS;

	if (MAX_CACHED_READ && blocks > MAX_CACHED_READ) {
		res = DeviceReadBlocks(dio, block, buffer, blocks);
		if (res) return res;

		do {
			BlockCacheRetrieve(bc, block++, buffer, TRUE);
			buffer += dio->sector_size;
		} while (--blocks);

		return DIO_SUCCESS;
	} else {
		ULONG uncached = 0;

		do {
			if (blocks > 0 && !BlockCacheRetrieve(bc, block, buffer, FALSE))
				uncached++;
			else if (uncached) {
				UQUAD blk = block - uncached;
				APTR buf = buffer - (uncached << dio->sector_shift);
				res = DeviceReadBlocks(dio, blk, buf, uncached);
				if (res) return res;
				do {
					BlockCacheStore(bc, blk++, buf, FALSE);
					buf += dio->sector_size;
				} while (--uncached);
			}
			block++;
			buffer += dio->sector_size;
		} while (blocks--);

		return DIO_SUCCESS;
	}
}

LONG CachedWriteBlocks(struct DiskIO *dio, UQUAD block, CONST_APTR buffer, ULONG blocks) {
	struct BlockCache *bc = dio->block_cache;
	BOOL big_write = FALSE;
	LONG res;

	DEBUGF("CachedWriteBlocks(%#p, %llu, %#p, %u)\n", dio, block, buffer, (unsigned)blocks);

	if (bc == NULL)
		return DeviceWriteBlocks(dio, block, buffer, blocks);

	if (block >= dio->total_sectors || (block + (UQUAD)blocks) > dio->total_sectors)
		return DIO_ERROR_OUTOFBOUNDS;

	if (blocks == 0)
		return DIO_SUCCESS;

	if (MAX_CACHED_WRITE && blocks > MAX_CACHED_WRITE)
		big_write = TRUE;

	if (dio->no_write_cache || big_write) {
		res = DeviceWriteBlocks(dio, block, buffer, blocks);
		if (res) return res;

		do {
			BlockCacheStore(bc, block++, buffer, big_write);
			buffer += dio->sector_size;
		} while (--blocks);

		return DIO_SUCCESS;
	} else {
		ULONG uncached = 0;

		ObtainSemaphore(&bc->cache_semaphore);
		if ((bc->num_dirty_nodes + blocks) > MAX_DIRTY_NODES) {
			BlockCacheFlush(bc);
		}
		ReleaseSemaphore(&bc->cache_semaphore);

		do {
			if (blocks > 0 && !BlockCacheWrite(bc, block, buffer))
				uncached++;
			else if (uncached) {
				UQUAD blk = block - uncached;
				CONST_APTR buf = buffer - (uncached << dio->sector_shift);
				res = DeviceWriteBlocks(dio, blk, buf, uncached);
				if (res) return res;
				do {
					BlockCacheStore(bc, blk++, buf, FALSE);
					buf += dio->sector_size;
				} while (--uncached);
			}
			block++;
			buffer += dio->sector_size;
		} while (blocks--);

		return DIO_SUCCESS;
	}
}

