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

void ClearSectorSize(struct DiskIO *dio) {
	dio->sector_size = 0;
	dio->sector_shift = 0;
	dio->sector_mask = 0;
}

void SetSectorSize(struct DiskIO *dio, ULONG sector_size) {
	ULONG shift = 0;
	UQUAD size = 1;

	dio->sector_size = sector_size;
	dio->sector_shift = 0;
	dio->sector_mask = 0;

	while (size < sector_size) {
		shift++;
		size <<= 1;
	}

	if (size == sector_size) {
		dio->sector_shift = shift;
		dio->sector_mask = size - 1;
	} else {
		DEBUGF("SetSectorSize - sector size (%u) is not a power of 2!\n", (unsigned)sector_size);
	}
}

LONG ReadBlocksUncached(struct DiskIO *dio, UQUAD block, APTR buffer, ULONG blocks) {
	DEBUGF("ReadBlocksUncached(%#p, %llu, %#p, %u)\n", dio, block, buffer, (unsigned)blocks);

	UQUAD offset;
	ULONG bytes;
	struct IOExtTD *iotd;
	LONG res = DIO_SUCCESS;

	if (block >= dio->total_sectors || (block + (UQUAD)blocks) > dio->total_sectors) {
		DEBUGF("ReadBlocksUncached failed - out of bounds access\n");
		return DIO_ERROR_OUTOFBOUNDS;
	}

	if (blocks == 0) {
		DEBUGF("ReadBlocksUncached - no blocks to read\n");
		return DIO_SUCCESS;
	}

	iotd = dio->diskiotd;
	offset = dio->partition_start + (block << dio->sector_shift);
	bytes = blocks << dio->sector_shift;

	iotd->iotd_Req.io_Command = dio->read_cmd;
	iotd->iotd_Req.io_Data = (APTR)buffer;
	iotd->iotd_Req.io_Actual = offset >> 32;
	iotd->iotd_Req.io_Offset = offset;
	iotd->iotd_Req.io_Length = bytes;
	iotd->iotd_Count = dio->disk_id;

	if (DoIO((struct IORequest *)iotd) != 0) {
		res = DIO_ERROR_UNSPECIFIED;
		DEBUGF("ReadBlocksUncached failed - io error %d\n", (int)iotd->iotd_Req.io_Error);
	}

	return res;
}

LONG WriteBlocksUncached(struct DiskIO *dio, UQUAD block, CONST_APTR buffer, ULONG blocks) {
	DEBUGF("WriteBlocksUncached(%#p, %llu, %#p, %u)\n", dio, block, buffer, (unsigned)blocks);

	UQUAD offset;
	ULONG bytes;
	struct IOExtTD *iotd;
	LONG res = DIO_SUCCESS;

	if (block >= dio->total_sectors || (block + (UQUAD)blocks) > dio->total_sectors) {
		DEBUGF("WriteBlocksUncached failed - out of bounds access\n");
		return DIO_ERROR_OUTOFBOUNDS;
	}

	if (blocks == 0) {
		DEBUGF("WriteBlocksUncached - no blocks to write\n");
		return DIO_SUCCESS;
	}

	iotd = dio->diskiotd;
	offset = dio->partition_start + (block << dio->sector_shift);
	bytes = blocks << dio->sector_shift;

	iotd->iotd_Req.io_Command = dio->write_cmd;
	iotd->iotd_Req.io_Data = (APTR)buffer;
	iotd->iotd_Req.io_Actual = offset >> 32;
	iotd->iotd_Req.io_Offset = offset;
	iotd->iotd_Req.io_Length = bytes;
	iotd->iotd_Count = dio->disk_id;

	if (DoIO((struct IORequest *)iotd) != 0) {
		res = DIO_ERROR_UNSPECIFIED;
		DEBUGF("WriteBlocksUncached failed - io error %d\n", (int)iotd->iotd_Req.io_Error);
	}

	dio->doupdate = TRUE;

	return res;
}

#ifndef DISABLE_BLOCK_CACHE

LONG ReadBlocksCached(struct DiskIO *dio, UQUAD block, APTR buffer, ULONG blocks) {
	DEBUGF("ReadBlocksCached(%#p, %llu, %#p, %u)\n", dio, block, buffer, (unsigned)blocks);

	struct BlockCache *bc = dio->block_cache;
	LONG res;

	if (bc == NULL)
		return ReadBlocksUncached(dio, block, buffer, blocks);

	if (block >= dio->total_sectors || (block + (UQUAD)blocks) > dio->total_sectors) {
		DEBUGF("ReadBlocksCached failed - out of bounds access\n");
		return DIO_ERROR_OUTOFBOUNDS;
	}

	if (blocks == 0) {
		DEBUGF("ReadBlocksCached - no blocks to read\n");
		return DIO_SUCCESS;
	}

#ifdef MAX_CACHED_READ
	if (blocks > MAX_CACHED_READ) {
		res = ReadBlocksUncached(dio, block, buffer, blocks);
		if (res) return res;

		do {
			BlockCacheRetrieve(bc, block++, buffer, TRUE);
			buffer += dio->sector_size;
		} while (--blocks);

		return DIO_SUCCESS;
	} else
#endif
	{
		ULONG uncached = 0;

		do {
			if (blocks > 0 && !BlockCacheRetrieve(bc, block, buffer, FALSE))
				uncached++;
			else if (uncached) {
				UQUAD blk = block - uncached;
				APTR buf = buffer - (uncached << dio->sector_shift);
				res = ReadBlocksUncached(dio, blk, buf, uncached);
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

LONG WriteBlocksCached(struct DiskIO *dio, UQUAD block, CONST_APTR buffer, ULONG blocks) {
	DEBUGF("WriteBlocksCached(%#p, %llu, %#p, %u)\n", dio, block, buffer, (unsigned)blocks);

	struct BlockCache *bc = dio->block_cache;
	BOOL big_write = FALSE;
	LONG res;

	if (bc == NULL)
		return WriteBlocksUncached(dio, block, buffer, blocks);

	if (block >= dio->total_sectors || (block + (UQUAD)blocks) > dio->total_sectors) {
		DEBUGF("WriteBlocksCached failed - out of bounds access\n");
		return DIO_ERROR_OUTOFBOUNDS;
	}

	if (blocks == 0) {
		DEBUGF("WriteBlocksCached - no blocks to write\n");
		return DIO_SUCCESS;
	}

#ifdef MAX_CACHED_WRITE
	if (blocks > MAX_CACHED_WRITE)
		big_write = TRUE;
#endif
	if (dio->no_write_cache || big_write) {
		res = WriteBlocksUncached(dio, block, buffer, blocks);
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
				res = WriteBlocksUncached(dio, blk, buf, uncached);
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

#endif

