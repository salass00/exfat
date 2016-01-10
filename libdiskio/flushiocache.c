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

int DIO_FlushIOCache(struct DiskIO *dio) {
	DEBUGF("DIO_FlushIOCache(%#p)\n", dio);

	if (dio == NULL || dio->disk_ok == FALSE) return DIO_ERROR_UNSPECIFIED;

#ifndef DISABLE_BLOCK_CACHE
	if (dio->block_cache != NULL &&
		dio->no_write_cache == FALSE &&
		dio->read_only == FALSE &&
		BlockCacheFlush(dio->block_cache) == FALSE)
	{
		DEBUGF("DIO_FlushIOCache failed\n");
		return DIO_ERROR_UNSPECIFIED;
	}
#endif

	if (dio->doupdate) {
		dio->doupdate = FALSE;

		if (dio->update_cmd != CMD_INVALID) {
			struct IOExtTD *iotd = dio->diskiotd;
			int error;

			iotd->iotd_Req.io_Command = dio->update_cmd;
			iotd->iotd_Count = dio->disk_id;
			error = DoIO((struct IORequest *)iotd);

			if (error != 0) {
				DEBUGF("Update command (%u) failed - %d\n",
					dio->update_cmd, error);
				return DIO_ERROR_UNSPECIFIED;
			}
		}
	}

	return DIO_SUCCESS;
}

