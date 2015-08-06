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

void DIO_Query(struct DiskIO *dio, const struct TagItem *tags) {
	DEBUGF("DIO_Query(%#p, %#p)\n", dio, tags);

	const struct TagItem *tag;
	ULONG *data;

	while ((tag = NextTagItem((struct TagItem **)&tags)) != NULL) {
		data = (ULONG *)tag->ti_Data;

		switch (tag->ti_Tag) {

			case DIOQ_DiskPresent:
				*data = dio->disk_present;
				break;

			case DIOQ_WriteProtected:
				*data = dio->write_protected;
				break;

			case DIOQ_DiskValid:
				*data = dio->disk_ok;
				break;

			case DIOQ_TotalSectors:
				*(UQUAD *)data = dio->total_sectors;
				break;

			case DIOQ_TotalBytes:
				*(UQUAD *)data = dio->partition_size;
				break;

			case DIOQ_BytesPerSector:
				*data = dio->sector_size;
				break;

			case DIOQ_SectorShift:
				*data = dio->sector_shift;
				break;

			case DIOQ_SectorMask:
				*data = dio->sector_mask;
				break;

			case DIOQ_DOSDevName:
				*(CONST_STRPTR *)data = dio->devname;
				break;

		}
	}
}

