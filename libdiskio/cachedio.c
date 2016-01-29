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

	if (dio->max_cached_read && blocks > dio->max_cached_read) {
		res = DeviceReadBlocks(dio, block, buffer, blocks);
		if (res == DIO_SUCCESS) {
			do {
				ReadCacheNode(bc, block++, buffer, RCN_DIRTY_ONLY);
				buffer += dio->sector_size;
			} while (--blocks);
		}

		return res;
	} else {
		ULONG uncached = 0;

		do {
			if (blocks > 0 && ReadCacheNode(bc, block, buffer, 0) == FALSE)
				uncached++;
			else if (uncached) {
				UQUAD blk = block - uncached;
				APTR buf = buffer - (uncached << dio->sector_shift);

				res = DeviceReadBlocks(dio, blk, buf, uncached);
				if (res == DIO_SUCCESS) {
					do {
						StoreCacheNode(bc, blk++, buf, 0);
						buf += dio->sector_size;
					} while (--uncached);
				} else
					return res;
			}
			block++;
			buffer += dio->sector_size;
		} while (blocks--);

		return DIO_SUCCESS;
	}
}

LONG CachedWriteBlocks(struct DiskIO *dio, UQUAD block, CONST_APTR buffer, ULONG blocks) {
	struct BlockCache *bc = dio->block_cache;
	BOOL bigwrite = FALSE;
	LONG res;

	DEBUGF("CachedWriteBlocks(%#p, %llu, %#p, %u)\n", dio, block, buffer, (unsigned)blocks);

	if (bc == NULL)
		return DeviceWriteBlocks(dio, block, buffer, blocks);

	if (block >= dio->total_sectors || (block + (UQUAD)blocks) > dio->total_sectors)
		return DIO_ERROR_OUTOFBOUNDS;

	if (blocks == 0)
		return DIO_SUCCESS;

	if (dio->max_cached_write && blocks > dio->max_cached_write)
		bigwrite = TRUE;

	if (dio->write_cache_enabled == FALSE || bigwrite) {
		ULONG scn_flags = SCN_CLEAR_DIRTY;

		if (bigwrite)
			scn_flags |= SCN_UPDATE_ONLY;

		res = DeviceWriteBlocks(dio, block, buffer, blocks);
		if (res == DIO_SUCCESS) {
			do {
				StoreCacheNode(bc, block++, buffer, scn_flags);
				buffer += dio->sector_size;
			} while (--blocks);
		}

		return res;
	} else {
		ULONG uncached = 0;

		if ((bc->num_dirty_nodes + blocks) >= bc->max_dirty_nodes)
			FlushDirtyNodes(bc, bc->low_threshold);

		do {
			if (blocks > 0 && WriteCacheNode(bc, block, buffer, 0) == FALSE)
				uncached++;
			else if (uncached) {
				UQUAD blk = block - uncached;
				CONST_APTR buf = buffer - (uncached << dio->sector_shift);

				res = DeviceWriteBlocks(dio, blk, buf, uncached);
				if (res == DIO_SUCCESS) {
					do {
						StoreCacheNode(bc, blk++, buf, SCN_CLEAR_DIRTY);
						buf += dio->sector_size;
					} while (--uncached);
				} else
					return res;
			}
			block++;
			buffer += dio->sector_size;
		} while (blocks--);

		return DIO_SUCCESS;
	}
}

