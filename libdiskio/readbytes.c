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

int DIO_ReadBytes(struct DiskIO *dio, UQUAD offset, APTR buffer, ULONG bytes)
{
	DEBUGF("DIO_ReadBytes(%#p, %llu, %#p, %lu)\n", dio, offset, buffer, bytes);

	if (dio == NULL || dio->disk_ok == FALSE) return DIO_ERROR_UNSPECIFIED;

	APTR sector_buffer = dio->rw_buffer;
	UQUAD block = offset >> dio->sector_shift;
	ULONG boffs = offset & dio->sector_mask;
	ULONG blocks, blen;
	int res = DIO_SUCCESS;

	do {
#ifdef MAX_READ_AHEAD
		if (!dio->no_cache) {
			blocks = (boffs + bytes + dio->sector_mask) >> dio->sector_shift;
			while (blocks < MAX_READ_AHEAD && (block + blocks) < dio->total_sectors &&
				!BlockCacheRetrieve(dio->block_cache, block + blocks, NULL, FALSE))
			{
				blocks++;
			}
			blen = blocks << dio->sector_shift;
			if (blocks <= MAX_READ_AHEAD) {
				res = ReadBlocksCached(dio, block, sector_buffer, blocks);
				if (res) break;
				CopyMem(sector_buffer + boffs, buffer, bytes);
				return DIO_SUCCESS;
			}
		}
#endif

		if (boffs) {
			blen = MIN(dio->sector_size - boffs, bytes);
			res = ReadBlocksCached(dio, block, sector_buffer, 1);
			if (res) break;
			CopyMem(sector_buffer + boffs, buffer, blen);
			buffer += blen;
			bytes -= blen;
			block++;
		}

		if (bytes >= dio->sector_size) {
			blocks = bytes >> dio->sector_shift;
			blen = blocks << dio->sector_shift;
			res = ReadBlocksCached(dio, block, buffer, blocks);
			if (res) break;
			buffer += blen;
			bytes -= blen;
			block += blocks;
		}

		if (bytes) {
			res = ReadBlocksCached(dio, block, sector_buffer, 1);
			if (res) break;
			CopyMem(sector_buffer, buffer, bytes);
		}
	} while (0);

	if (res) DEBUGF("DIO_ReadBytes failed - io error %d\n", res);

	return res;
}

