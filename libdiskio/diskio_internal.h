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

#ifdef __GNUC__
typedef struct { } LABEL;
#else
typedef char LABEL;
#endif

struct BlockCache {
	struct MinNode         node;
	struct DiskIO         *dio_handle;
	APTR                   mempool;
	ULONG                  sector_size;
	ULONG                  sector_shift;
	APTR                   write_buffer;
	ULONG                  write_buffer_size;
	struct SignalSemaphore cache_semaphore;
	struct MinList         probation_list;
	struct MinList         protected_list;
	struct MinList         dirty_list;
	struct Splay          *cache_tree;
	struct Splay          *range_tree;
	ULONG                  num_protected_nodes;
	ULONG                  num_dirty_nodes;
	ULONG                  num_cache_nodes;
	ULONG                  max_protected_nodes;
	ULONG                  max_dirty_nodes;
	ULONG                  max_cache_nodes;
	ULONG                  high_threshold;
	ULONG                  low_threshold;
	struct Interrupt       mem_handler;
	BOOL                   write_cache_enabled;
};

struct BlockRange {
	UQUAD first;
	UQUAD last;
};

struct BlockRangeNode {
	struct Splay      splay;
	struct MinNode    node;
	struct BlockRange range;
	struct MinList    list;
};

#define BRNFROMSPLAY(s) container_of(s, struct BlockRangeNode, splay)
#define BRNFROMNODE(n)  container_of(n, struct BlockRangeNode, node)

struct BlockCacheNode {
	struct Splay           splay;
	struct MinNode         node;
	UQUAD                  sector;
	APTR                   data;
	struct BlockRangeNode *range_node;
	UBYTE                  type;
	UBYTE                  pad[3];
	ULONG                  checksum;
};

#define BCNFROMSPLAY(s) container_of(s, struct BlockCacheNode, splay)
#define BCNFROMNODE(n)  container_of(n, struct BlockCacheNode, node)

enum {
	BCN_PROBATION = 1,
	BCN_PROTECTED,
	BCN_DIRTY
};

struct DiskIO {
	/* These fields are initialised on Setup() only. */
	APTR               mempool;
	struct MsgPort    *diskmp;
	struct IOExtTD    *diskiotd;
	UWORD              cmd_support;
	UWORD              update_cmd;
	BOOL               cache_enabled;
	BOOL               write_cache_enabled;
	BOOL               inhibit;
	BOOL               uninhibit;
	BOOL               doupdate;
	BOOL               read_only;
	BOOL               use_full_disk;

	/* If use_full_disk is TRUE the following are initialised by Setup() only,
     * otherwise they are initialised by Update(). */
	LABEL              SETUP_DATA_START;
	ULONG              sector_size;
	ULONG              sector_shift;
	ULONG              sector_mask;
	UWORD              read_cmd;
	UWORD              write_cmd;
	UQUAD              partition_start;
	UQUAD              partition_size;
	UQUAD              total_sectors;
	LABEL              SETUP_DATA_END;

	/* The following fields are always (re)initialised on Update(). */
	LABEL              UPDATE_DATA_START;
	APTR               read_buffer;
	ULONG              read_buffer_size;
	ULONG              max_cached_read;
	ULONG              max_cached_write;
	struct BlockCache *block_cache;
	ULONG              disk_id;
	UQUAD              disk_size;
	BOOL               disk_present;
	BOOL               disk_ok;
	BOOL               write_protected;
	LABEL              UPDATE_DATA_END;

	/* Buffer used to store the DOS device name */
	TEXT               devname[256];
};

#define SETUP_DATA_START(dio) ((APTR)&(dio)->SETUP_DATA_START)
#define SETUP_DATA_END(dio)   ((APTR)&(dio)->SETUP_DATA_END)
#define SETUP_DATA_SIZE(dio)  ((IPTR)SETUP_DATA_END(dio) - (IPTR)SETUP_DATA_START(dio))

#define UPDATE_DATA_START(dio) ((APTR)&(dio)->UPDATE_DATA_START)
#define UPDATE_DATA_END(dio)   ((APTR)&(dio)->UPDATE_DATA_END)
#define UPDATE_DATA_SIZE(dio)  ((IPTR)UPDATE_DATA_END(dio) - (IPTR)UPDATE_DATA_START(dio))

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
BOOL IsValidSectorSize(ULONG x);
void SetSectorSize(struct DiskIO *dio, ULONG sector_size);
void CleanupDisk(struct DiskIO *dio, BOOL cleanup);
BOOL Internal_Update(struct DiskIO *dio, BOOL setup);

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
void ExpungeCacheNode(struct BlockCache *bc, struct BlockCacheNode *bcn);
BOOL ReadCacheNode(struct BlockCache *bc, UQUAD sector, APTR buffer, ULONG flags);
BOOL StoreCacheNode(struct BlockCache *bc, UQUAD sector, CONST_APTR buffer, ULONG flags);
BOOL WriteCacheNode(struct BlockCache *bc, UQUAD sector, CONST_APTR buffer, ULONG flags);
BOOL FlushDirtyNodes(struct BlockCache *bc, ULONG max_dirty_nodes);

/* memhandler.c */
#ifdef __AROS__
AROS_UFP5(int, DiskIOMemHandler,
	AROS_UFPA(APTR, data, A1),
	AROS_UFPA(APTR, code, A5),
	AROS_UFPA(struct ExecBase *, SysBase, A6),
	AROS_UFPA(APTR, mask, D1),
	AROS_UFPA(APTR, custom, A0));
#else
SAVEDS ASM int DiskIOMemHandler(
	REG(a6, struct ExecBase *SysBase),
	REG(a0, APTR custom),
	REG(a1, APTR data));
#endif

#endif

