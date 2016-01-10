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

int DIO_WriteBytes(struct DiskIO *dio, UQUAD offset, CONST_APTR buffer, ULONG bytes) {
	DEBUGF("DIO_WriteBytes(%#p, %llu, %#p, %lu)\n", dio, offset, buffer, bytes);

	if (dio == NULL || dio->disk_ok == FALSE) return DIO_ERROR_UNSPECIFIED;

	if (dio->read_only) return DIO_ERROR_READONLY;

	APTR sector_buffer = NULL;
	UQUAD block = offset >> dio->sector_shift;
	ULONG boffs = offset & dio->sector_mask;
	int res = DIO_SUCCESS;

	do {
		if (boffs) {
			ULONG blen = MIN(dio->sector_size - boffs, bytes);
			if ((sector_buffer = AllocPooled(dio->mempool, dio->sector_size)) == NULL)
				return TDERR_NoMem;
			res = ReadBlocksCached(dio, block, sector_buffer, 1);
			if (res) break;
			CopyMem((APTR)buffer, sector_buffer + boffs, blen);
			res = WriteBlocksCached(dio, block, sector_buffer, 1);
			if (res) break;
			buffer += blen;
			bytes -= blen;
			block++;
		}

		if (bytes >= dio->sector_size) {
			ULONG blocks = bytes >> dio->sector_shift;
			ULONG blen = blocks << dio->sector_shift;
			res = WriteBlocksCached(dio, block, buffer, blocks);
			if (res) break;
			buffer += blen;
			bytes -= blen;
			block += blocks;
		}

		if (bytes) {
			if (sector_buffer == NULL && (sector_buffer = AllocPooled(dio->mempool, dio->sector_size)) == NULL)
				return TDERR_NoMem;
			res = ReadBlocksCached(dio, block, sector_buffer, 1);
			if (res) break;
			CopyMem((APTR)buffer, sector_buffer, bytes);
			res = WriteBlocksCached(dio, block, sector_buffer, 1);
			if (res) break;
		}
	} while (0);

	FreePooled(dio->mempool, sector_buffer, dio->sector_size);

	if (res) DEBUGF("DIO_WriteBytes failed - io error %d\n", res);

	return res;
}

