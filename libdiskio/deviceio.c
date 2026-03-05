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

LONG DeviceReadBlocks(struct DiskIO *dio, UQUAD block, APTR buffer, ULONG blocks) {
	UQUAD offset;
	ULONG bytes;
	struct IOExtTD *iotd;
	int error;
	LONG res = DIO_SUCCESS;

	DEBUGF("DeviceReadBlocks(%#p, %llu, %#p, %u)\n", dio, block, buffer, blocks);

	if (block >= dio->total_sectors || (block + (UQUAD)blocks) > dio->total_sectors)
		return DIO_ERROR_OUTOFBOUNDS;

	if (blocks == 0)
		return DIO_SUCCESS;

	iotd = dio->diskiotd;
	offset = dio->partition_start + (block << dio->sector_shift);
	bytes = blocks << dio->sector_shift;

	iotd->iotd_Req.io_Command = dio->read_cmd;
	iotd->iotd_Req.io_Data = (APTR)buffer;
	iotd->iotd_Req.io_Actual = offset >> 32;
	iotd->iotd_Req.io_Offset = offset;
	iotd->iotd_Req.io_Length = bytes;
	iotd->iotd_Count = dio->disk_id;

	error = DoIO((struct IORequest *)iotd);
	if (error != 0) {
		DEBUGF("DeviceReadBlocks failed - io error %d\n", error);
		res = DIO_ERROR_UNSPECIFIED;
	}

	return res;
}

LONG DeviceWriteBlocks(struct DiskIO *dio, UQUAD block, CONST_APTR buffer, ULONG blocks) {
	UQUAD offset;
	ULONG bytes;
	struct IOExtTD *iotd;
	int error;
	LONG res = DIO_SUCCESS;

	DEBUGF("DeviceWriteBlocks(%#p, %llu, %#p, %u)\n", dio, block, buffer, (unsigned)blocks);

	if (block >= dio->total_sectors || (block + (UQUAD)blocks) > dio->total_sectors)
		return DIO_ERROR_OUTOFBOUNDS;

	if (blocks == 0)
		return DIO_SUCCESS;

	iotd = dio->diskiotd;
	offset = dio->partition_start + (block << dio->sector_shift);
	bytes = blocks << dio->sector_shift;

	iotd->iotd_Req.io_Command = dio->write_cmd;
	iotd->iotd_Req.io_Data = (APTR)buffer;
	iotd->iotd_Req.io_Actual = offset >> 32;
	iotd->iotd_Req.io_Offset = offset;
	iotd->iotd_Req.io_Length = bytes;
	iotd->iotd_Count = dio->disk_id;

	error = DoIO((struct IORequest *)iotd);
	if (error != 0) {
		DEBUGF("DeviceWriteBlocks failed - io error %d\n", error);
		res = DIO_ERROR_UNSPECIFIED;
	}

	dio->doupdate = TRUE;

	return res;
}

LONG DeviceUpdate(struct DiskIO *dio) {
	if (dio->doupdate) {
		dio->doupdate = FALSE;

		if (dio->update_cmd != CMD_INVALID) {
			struct IOExtTD *iotd = dio->diskiotd;
			int error;

			iotd->iotd_Req.io_Command = dio->update_cmd;
			iotd->iotd_Count          = dio->disk_id;
			error = DoIO((struct IORequest *)iotd);

			if (error != 0) {
				DEBUGF("DeviceUpdate() failed - io error %d\n", error);
				return DIO_ERROR_UNSPECIFIED;
			}
		}
	}

	return DIO_SUCCESS;
}

