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

#ifndef DISKIO_H
#define DISKIO_H

#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif

#ifdef __AROS__
#include <aros/preprocessor/variadic/cast2iptr.hpp>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__AROS__) && !defined(AROS_TYPES_DEFINED)
#define AROS_TYPES_DEFINED
typedef ULONG              IPTR;
typedef LONG               SIPTR;
typedef unsigned long long UQUAD;
typedef signed long long   QUAD;
#endif

struct DiskIO; /* This is a library private structure, do not touch! */

/* Tags for DIO_Setup() */
#define DIOS_Dummy          (TAG_USER)
#define DIOS_Cache          (DIOS_Dummy + 1) /* (BOOL) Enable/disable I/O cache */
#define DIOS_WriteCache     (DIOS_Dummy + 2) /* (BOOL) Enable/disable caching of write operations */
#define DIOS_Inhibit        (DIOS_Dummy + 3) /* (BOOL) Inhibit filesystem */
#define DIOS_DOSType        (DIOS_Dummy + 4) /* (uint32) Check that filesystem reports the correct dostype */
#define DIOS_DOSTypeMask    (DIOS_Dummy + 5) /* (uint32) Which bits of dostype to check and which to ignore */
#define DIOS_Error          (DIOS_Dummy + 6) /* (int32 *) Error code if Setup() failed */
#define DIOS_ReadOnly       (DIOS_Dummy + 7) /* (BOOL) Enable/disable read-only mode */

/* Tags for DIO_Query() */
#define DIOQ_Dummy          (TAG_USER)
#define DIOQ_DiskPresent    (DIOQ_Dummy + 1) /* (uint32) Is a disk present? */
#define DIOQ_WriteProtected (DIOQ_Dummy + 2) /* (uint32) Is it write protected? */
#define DIOQ_DiskValid      (DIOQ_Dummy + 3) /* (uint32) Is it usable? */
#define DIOQ_TotalSectors   (DIOQ_Dummy + 4) /* (uint64) Total sectors of disk/partition */
#define DIOQ_BytesPerSector (DIOQ_Dummy + 5) /* (uint32) Sector size of disk/partition */
#define DIOQ_SectorShift    (DIOQ_Dummy + 6) /* (uint32) */
#define DIOQ_SectorMask     (DIOQ_Dummy + 7) /* (uint32) */
#define DIOQ_TotalBytes     (DIOQ_Dummy + 8) /* (uint64) Total size of disk/partition */
#define DIOQ_DOSDevName     (DIOQ_Dummy + 9) /* (CONST_STRPTR) Name of DOS device ("USB0:", "DH1:") */

enum {
	DIO_SUCCESS = 0,       /* Success */
	DIO_ERROR_UNSPECIFIED, /* General catch-all error code */
	DIO_ERROR_NOMEM,       /* Not enough memory */
	DIO_ERROR_GETFSD,      /* GetDiskFileSystemData() failed */
	DIO_ERROR_DOSTYPE,     /* Didn't pass dostype check */
	DIO_ERROR_INHIBIT,     /* Inhibit() failed */
	DIO_ERROR_OPENDEVICE,  /* OpenDevice() failed */
	DIO_ERROR_NSDQUERY,    /* NSCMD_DEVICEQUERY failed or not of type NSDEVTYPE_TRACKDISK */
	DIO_ERROR_OUTOFBOUNDS, /* Out of bounds read or write access */
	DIO_ERROR_READONLY     /* Tried to write to a read-only handle */
};

typedef void (*DiskChangeHandlerFunc)(APTR udata);

struct DiskIO *DIO_Setup(CONST_STRPTR name, const struct TagItem *tags);
void DIO_Cleanup(struct DiskIO *dio);
void DIO_Update(struct DiskIO *dio);
void DIO_Query(struct DiskIO *dio, const struct TagItem *tags);
int DIO_ReadBytes(struct DiskIO *dio, UQUAD offset, APTR buffer, ULONG bytes);
int DIO_WriteBytes(struct DiskIO *dio, UQUAD offset, CONST_APTR buffer, ULONG bytes);
int DIO_FlushIOCache(struct DiskIO *dio);

#ifdef __AROS__
#define DIO_SetupTags(dio, ...) \
({ \
	DIO_Setup((dio), (struct TagItem *)(IPTR []){ AROS_PP_VARIADIC_CAST2IPTR(__VA_ARGS__) }); \
})
#define DIO_QueryTags(dio, ...) \
({ \
	DIO_Query((dio), (struct TagItem *)(IPTR []){ AROS_PP_VARIADIC_CAST2IPTR(__VA_ARGS__) }); \
})
#else
struct DiskIO *DIO_SetupTags(CONST_STRPTR name, Tag tag1, ...);
void DIO_QueryTags(struct DiskIO *dio, Tag tag1, ...);
#endif

#ifdef __cplusplus
}
#endif

#endif

