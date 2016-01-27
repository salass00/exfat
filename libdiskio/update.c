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

static inline BOOL IsPowerOfTwo(ULONG x) {
	if (x == (x & -x))
		return TRUE;
	else
		return FALSE;
}

BOOL IsValidSectorSize(ULONG x) {
	if (x >= 256 && IsPowerOfTwo(x))
		return TRUE;
	else
		return FALSE;
}

static inline ULONG FirstSetBit(ULONG x) {
#ifdef __PPC__
	__asm__ ("cntlzw %0,%1" : "=r" (x) : "r" (x));
	return 31 - x;
#else
	ULONG i;
	for (i = 0; i < 32; i++) {
		if ((x & (1UL << i)) != 0) break;
	}
	return i;
#endif
}

void SetSectorSize(struct DiskIO *dio, ULONG sector_size) {
	/* Assumes that sector_size is a power of two. */
	dio->sector_size  = sector_size;
	dio->sector_shift = FirstSetBit(sector_size);
	dio->sector_mask  = sector_size - 1;
}

static BOOL PickCommandSet(struct DiskIO *dio) {
	UQUAD last_byte = dio->partition_start + dio->partition_size - 1;
	BOOL success = TRUE;

	if ((last_byte >> 32) != 0) {
		if (dio->cmd_support & CMDSF_NSD_ETD64) {
			DEBUGF("Using NSD ETD64 command set\n");
			dio->read_cmd  = NSCMD_ETD_READ64;
			dio->write_cmd = NSCMD_ETD_WRITE64;
		} else if (dio->cmd_support & CMDSF_NSD_TD64) {
			DEBUGF("Using NSD TD64 command set\n");
			dio->read_cmd  = NSCMD_TD_READ64;
			dio->write_cmd = NSCMD_TD_WRITE64;
		} else if (dio->cmd_support & CMDSF_TD64) {
			DEBUGF("Using unofficial TD64 command set\n");
			dio->read_cmd  = TD_READ64;
			dio->write_cmd = TD_WRITE64;
		} else {
			DEBUGF("No supported 64-bit command set found\n");
			success = FALSE;
		}
	} else {
		if (dio->cmd_support & CMDSF_ETD32) {
			DEBUGF("Using ETD command set\n");
			dio->read_cmd  = ETD_READ;
			dio->write_cmd = ETD_WRITE;
		} else if (dio->cmd_support & CMDSF_TD32) {
			DEBUGF("Using CMD command set\n");
			dio->read_cmd  = CMD_READ;
			dio->write_cmd = CMD_WRITE;
		} else {
			DEBUGF("No supported 32-bit command set found\n");
			success = FALSE;
		}
	}

	return success;
}

static BOOL SetupDisk(struct DiskIO *dio, BOOL setup) {
	struct IOExtTD *iotd = dio->diskiotd;
	BOOL success = FALSE;

	if (setup) {
		if (!PickCommandSet(dio)) goto out;
	}

	iotd->iotd_Req.io_Command = TD_CHANGESTATE;
	if (DoIO((struct IORequest *)iotd) == 0) {
		if (iotd->iotd_Req.io_Actual == 0)
			dio->disk_present = TRUE;
	}

	iotd->iotd_Req.io_Command = TD_CHANGENUM;
	if (DoIO((struct IORequest *)iotd) == 0) {
		dio->disk_id = iotd->iotd_Req.io_Actual;
	}

	iotd->iotd_Req.io_Command = TD_PROTSTATUS;
	if (DoIO((struct IORequest *)iotd) == 0) {
		if (iotd->iotd_Req.io_Actual)
			dio->write_protected = TRUE;
	}

	if (dio->disk_present) {
		struct DriveGeometry dg;

		iotd->iotd_Req.io_Command = TD_GETGEOMETRY;
		iotd->iotd_Req.io_Data    = &dg;
		iotd->iotd_Req.io_Length  = sizeof(dg);
		if (DoIO((struct IORequest *)iotd) == 0) {
			dio->disk_size = (UQUAD)dg.dg_Cylinders * (UQUAD)dg.dg_CylSectors * (UQUAD)dg.dg_SectorSize;

			if (!IsValidSectorSize(dg.dg_SectorSize))
				goto out;

			if (dio->use_full_disk) {
				SetSectorSize(dio, dg.dg_SectorSize);

				dio->partition_start = 0;
				dio->partition_size  = dio->disk_size;
				dio->total_sectors   = dio->partition_size >> dio->sector_shift;

				if (!PickCommandSet(dio)) goto out;
			} else {
				/* Make sure that the partition can fit on this disk */
				if (dio->sector_size < dg.dg_SectorSize) goto out;
				if ((dio->sector_size & (dg.dg_SectorSize - 1)) != 0) goto out;
				if (dio->partition_start >= dio->disk_size) goto out;
				if ((dio->partition_start + dio->partition_size) > dio->disk_size) goto out;
			}
		} else goto out;

		dio->rw_buffer = AllocPooled(dio->mempool, RW_BUFFER_SIZE << dio->sector_shift);
		if (dio->rw_buffer == NULL) goto out;

		if (dio->cache_enabled) {
			dio->block_cache = InitBlockCache(dio);
			if (dio->block_cache == NULL) goto out;
		}

		dio->disk_ok = TRUE;
	}

	success = TRUE;

out:
	return success;
}

void CleanupDisk(struct DiskIO *dio, BOOL cleanup) {
	if (dio->block_cache != NULL) CleanupBlockCache(dio->block_cache);

	if (dio->rw_buffer != NULL) FreePooled(dio->mempool, dio->rw_buffer, RW_BUFFER_SIZE << dio->sector_shift);

	if (!cleanup) {
		APTR  data_start;
		ULONG data_size;

		if (dio->use_full_disk) {
			data_start = UPDATE_DATA_START(dio);
			data_size  = UPDATE_DATA_SIZE(dio);
		} else {
			data_start = SETUP_DATA_START(dio);
			data_size  = SETUP_DATA_SIZE(dio) + UPDATE_DATA_SIZE(dio);
		}

		bzero(data_start, data_size);
	}
}

BOOL Internal_Update(struct DiskIO *dio, BOOL setup) {
	BOOL success;

	if (!setup) {
		CleanupDisk(dio, FALSE);
	}

	success = SetupDisk(dio, setup);
	if (!success) {
		/* CleanupDisk() clears all fields but we want to keep these,
         * so we must save and restore them. */
		ULONG disk_id         = dio->disk_id;
		UQUAD disk_size       = dio->disk_size;
		BOOL  disk_present    = dio->disk_present;
		BOOL  write_protected = dio->write_protected;

		CleanupDisk(dio, FALSE);

		dio->disk_id         = disk_id;
		dio->disk_size       = disk_size;
		dio->disk_present    = disk_present;
		dio->write_protected = write_protected;
	}

	return success;
}

void DIO_Update(struct DiskIO *dio) {
	DEBUGF("DIO_Update(%#p)\n", dio);

	if (dio != NULL) {
		Internal_Update(dio, FALSE);
	}
}

