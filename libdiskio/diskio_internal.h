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

#ifndef DISKIO_INTERNAL_H
#define DISKIO_INTERNAL_H

#include "diskio.h"
#include <exec/errors.h>
#include <exec/interrupts.h>
#include <devices/trackdisk.h>
#include <devices/newstyle.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/filesysbox.h>
#include <string.h>
#include <stdarg.h>
#include "splay.h"

//#define DEBUG
#define DISABLE_DOSTYPE_CHECK

#ifndef NEWLIST
#define NEWLIST(list) \
	do { \
		((struct List *)(list))->lh_Head = (struct Node *)&((struct List *)(list))->lh_Tail; \
		((struct List *)(list))->lh_Tail = NULL; \
		((struct List *)(list))->lh_TailPred = (struct Node *)&((struct List *)(list))->lh_Head; \
	} while (0)
#endif

#ifndef IsMinListEmpty
#define IsMinListEmpty(list) IsListEmpty((struct List *)list)
#endif

#ifndef TD_READ64
#define TD_READ64	24
#define TD_WRITE64	25
#define TD_SEEK64	26
#define TD_FORMAT64	27
#endif

/* debugf.c */
int debugf(const char *fmt, ...);
int vdebugf(const char *fmt, va_list args);

#ifdef DEBUG
#define DEBUGF(...) debugf(__VA_ARGS__)
#else
#define DEBUGF(...)
#endif

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

#ifdef __GNUC__
#define container_of(ptr, type, member) ({ \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type, member) );})
#else
#define container_of(ptr, type, member) ( (type *)( (char *)(ptr) - offsetof(type, member) ) )
#endif

#define MAX_CACHE_NODES  4096
#define MAX_DIRTY_NODES  1024
#define RW_BUFFER_SIZE   128
#define MAX_READ_AHEAD   128
#define MAX_CACHED_READ  128
#define MAX_CACHED_WRITE 128

struct BlockCache {
	struct MinNode         node;
	struct DiskIO         *dio_handle;
	APTR                   mempool;
	ULONG                  sector_size;
	struct SignalSemaphore cache_semaphore;
	struct MinList         clean_list;
	struct MinList         dirty_list;
	struct Splay          *cache_tree;
	ULONG                  num_clean_nodes;
	ULONG                  num_dirty_nodes;
	ULONG                  num_cache_nodes;
	APTR                   rw_buffer;
	struct Interrupt       mem_handler;
};

struct BlockCacheNode {
	struct Splay   splay;
	struct MinNode node;
	UQUAD          sector;
	UBYTE          dirty;
	UBYTE          pad[3];
	APTR           data;
	ULONG          checksum;
};

#define BCNFROMSPLAY(s) container_of(s, struct BlockCacheNode, splay)
#define BCNFROMNODE(n)  container_of(n, struct BlockCacheNode, node)

struct DiskIO {
	APTR               mempool;
	struct MsgPort    *diskmp;
	struct IOExtTD    *diskiotd;
	struct Device     *disk_device;
	UWORD              cmd_support;
	BOOL               use_full_disk;
	ULONG              sector_size;
	ULONG              sector_shift;
	ULONG              sector_mask;
	UQUAD              partition_start;
	UQUAD              partition_size;
	UQUAD              total_sectors;
	BOOL               disk_present;
	BOOL               disk_ok;
	BOOL               write_protected;
	ULONG              disk_id;
	UQUAD              disk_size;
	UWORD              read_cmd;
	UWORD              write_cmd;
	UWORD              update_cmd;
	BOOL               no_cache;
	BOOL               no_write_cache;
	struct BlockCache *block_cache;
	APTR               rw_buffer;
	TEXT               devname[256];
	BOOL               inhibit;
	BOOL               uninhibit;
	BOOL               doupdate;
	BOOL               read_only;
};

#define CMDSF_TD32       (1 << 0)
#define CMDSF_ETD32      (1 << 1)
#define CMDSF_TD64       (1 << 2)
#define CMDSF_NSD_TD64   (1 << 3)
#define CMDSF_NSD_ETD64  (1 << 4)
#define CMDSF_CMD_UPDATE (1 << 5)
#define CMDSF_ETD_UPDATE (1 << 6)

/* Flags for ReadCacheNode() */
#define RCN_DIRTY_ONLY   (1 << 0)

/* Flags for StoreCacheNode() */
#define SCN_UPDATE_ONLY  (1 << 0)
#define SCN_CLEAR_DIRTY  (1 << 1)

/* update.c */
void SetSectorSize(struct DiskIO *dio, ULONG sector_size);

/* deviceio.c */
LONG DeviceReadBlocks(struct DiskIO *dio, UQUAD block, APTR buffer, ULONG blocks);
LONG DeviceWriteBlocks(struct DiskIO *dio, UQUAD block, CONST_APTR buffer, ULONG blocks);
LONG DeviceUpdate(struct DiskIO *dio);

/* cachedio.c */
LONG CachedReadBlocks(struct DiskIO *dio, UQUAD block, APTR buffer, ULONG blocks);
LONG CachedWriteBlocks(struct DiskIO *dio, UQUAD block, CONST_APTR buffer, ULONG blocks);

/* blockcache.c */
struct BlockCache *InitBlockCache(struct DiskIO *dio);
void CleanupBlockCache(struct BlockCache *bc);
BOOL ReadCacheNode(struct BlockCache *bc, UQUAD sector, APTR buffer, ULONG flags);
BOOL StoreCacheNode(struct BlockCache *bc, UQUAD sector, CONST_APTR buffer, ULONG flags);
BOOL WriteCacheNode(struct BlockCache *bc, UQUAD sector, CONST_APTR buffer, ULONG flags);
BOOL FlushDirtyNodes(struct BlockCache *bc);

/* mergesort.c */
void SortCacheNodes(struct MinList *list);

#endif

