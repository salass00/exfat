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

void DIO_Update(struct DiskIO *dio) {
	DEBUGF("DIO_Update(%#p)\n", dio);

	struct IOExtTD *iotd = dio->diskiotd;

#ifndef DISABLE_BLOCK_CACHE
	if (dio->block_cache != NULL) {
		CleanupBlockCache(dio->block_cache);
		dio->block_cache = NULL;
	}
#endif

	if (dio->rw_buffer != NULL) {
		FreePooled(dio->mempool, dio->rw_buffer, RW_BUFFER_SIZE << dio->sector_shift);
		dio->rw_buffer = NULL;
	}

	dio->disk_present = FALSE;
	dio->disk_ok = FALSE;
	dio->write_protected = FALSE;
	dio->disk_id = 0;
	dio->disk_size = 0;
	dio->read_cmd = CMD_INVALID;
	dio->write_cmd = CMD_INVALID;

	if (dio->use_full_disk) {
		ClearSectorSize(dio);
		dio->partition_start = 0;
		dio->partition_size = 0;
		dio->total_sectors = 0;
	}

	DEBUGF("TD_CHANGESTATE\n");
	iotd->iotd_Req.io_Command = TD_CHANGESTATE;
	if (DoIO((struct IORequest *)iotd) == 0) {
		dio->disk_present = iotd->iotd_Req.io_Actual ? FALSE : TRUE;
		DEBUGF("disk %s\n", dio->disk_present ? "found" : "not present");
	}

	DEBUGF("TD_CHANGENUM\n");
	iotd->iotd_Req.io_Command = TD_CHANGENUM;
	if (DoIO((struct IORequest *)iotd) == 0) {
		dio->disk_id = iotd->iotd_Req.io_Actual;
		DEBUGF("disk id: %lu\n", dio->disk_id);
	}

	DEBUGF("TD_PROTSTATUS\n");
	iotd->iotd_Req.io_Command = TD_PROTSTATUS;
	if (DoIO((struct IORequest *)iotd) == 0) {
		dio->write_protected = iotd->iotd_Req.io_Actual ? TRUE : FALSE;
		DEBUGF("disk is %s\n", dio->write_protected ? "write protected" : "write enabled");
	}

	if (dio->disk_present) {
		struct DriveGeometry dg;

		DEBUGF("TD_GETGEOMETRY\n");
		iotd->iotd_Req.io_Command = TD_GETGEOMETRY;
		iotd->iotd_Req.io_Data = &dg;
		iotd->iotd_Req.io_Length = sizeof(dg);
		if (DoIO((struct IORequest *)iotd) == 0) {
			UQUAD sector_size = dg.dg_SectorSize;
			UQUAD cylinder_size = (UQUAD)dg.dg_CylSectors * sector_size;
			dio->disk_size = (UQUAD)dg.dg_Cylinders * (UQUAD)cylinder_size;
			DEBUGF("disk size: %llu cylinder size: %llu sector size: %llu\n",
				dio->disk_size, cylinder_size, sector_size);
			if (dio->use_full_disk) {
				SetSectorSize(dio, sector_size);
				dio->partition_start = 0;
				dio->partition_size = dio->disk_size;
				dio->total_sectors = dio->partition_size / sector_size;
				dio->disk_ok = TRUE;
				DEBUGF("partiton start: %llu partition size: %llu cylinder size: %llu sector size: %lu total sectors: %llu\n",
					dio->partition_start, dio->partition_size, cylinder_size, dio->sector_size, dio->total_sectors);
			} else {
				if (dio->sector_size >= dg.dg_SectorSize &&
					(dio->sector_size % dg.dg_SectorSize) == 0 &&
					dio->partition_start < dio->disk_size &&
					(dio->partition_start + dio->partition_size) <= dio->disk_size)
				{
					dio->disk_ok = TRUE;
				}
			}
		}
	}

	if (dio->disk_ok) {
		dio->rw_buffer = AllocPooled(dio->mempool, RW_BUFFER_SIZE << dio->sector_shift);
		if (dio->rw_buffer == NULL)
			dio->disk_ok = FALSE;
#ifndef DISABLE_BLOCK_CACHE
		else if (!dio->no_cache) {
			dio->block_cache = InitBlockCache(dio);

			if (dio->block_cache == NULL) {
				FreePooled(dio->mempool, dio->rw_buffer, RW_BUFFER_SIZE << dio->sector_shift);
				dio->rw_buffer = NULL;
				dio->disk_ok = FALSE;
			}
		}
#endif

		if (((dio->partition_start + dio->partition_size - 1) >> 32) != 0) {
			if (dio->cmd_support & CMDSF_NSD_ETD64) {
				DEBUGF("Using NSD ETD64 command set\n");
				dio->read_cmd = NSCMD_ETD_READ64;
				dio->write_cmd = NSCMD_ETD_WRITE64;
			} else if (dio->cmd_support & CMDSF_NSD_TD64) {
				DEBUGF("Using NSD TD64 command set\n");
				dio->read_cmd = NSCMD_TD_READ64;
				dio->write_cmd = NSCMD_TD_WRITE64;
			} else if (dio->cmd_support & CMDSF_TD64) {
				DEBUGF("Using unofficial TD64 command set\n");
				dio->read_cmd = TD_READ64;
				dio->write_cmd = TD_WRITE64;
			} else {
				DEBUGF("No supported 64-bit command set found\n");
				dio->disk_ok = FALSE;
			}
		} else {
			if (dio->cmd_support & CMDSF_ETD32) {
				DEBUGF("Using regular ETD command set\n");
				dio->read_cmd = ETD_READ;
				dio->write_cmd = ETD_WRITE;
			} else if (dio->cmd_support & CMDSF_TD32) {
				DEBUGF("Using regular CMD command set\n");
				dio->read_cmd = CMD_READ;
				dio->write_cmd = CMD_WRITE;
			} else {
				DEBUGF("No supported 32-bit command set found\n");
				dio->disk_ok = FALSE;
			}
		}
	}
}

