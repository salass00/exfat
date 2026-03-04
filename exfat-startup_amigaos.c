/**
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

#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/filesysbox.h>
#include <SDI/SDI_compiler.h>

#ifdef __AROS__
#include "exfat-handler_rev.h"
#else
#include "exFATFileSystem_rev.h"
#endif

#ifdef __AROS__
const char *EXEC_NAME = "exfat-handler";
#else
const char *EXEC_NAME = "exFATFileSystem";
#endif

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct UtilityBase *UtilityBase;
#ifdef __AROS__
struct Library *StdlibBase;
struct Library *CrtBase;
#endif
struct Library *FileSysBoxBase;

#ifndef ZERO
#define ZERO MKBADDR(NULL)
#endif

//#define DEBUG
#ifdef DEBUG
# include <debugf.h>
# define DEBUGF(str,args...) debugf(str, ## args)
#else
# define DEBUGF(str,args...)
#endif

extern int setup_malloc(void);
extern int cleanup_malloc(void);

extern int exfat_main(struct Message *msg);

static const TEXT vstring[];
static const TEXT dosName[];
static const TEXT utilityName[];
#ifdef __AROS__
static const TEXT stdlibName[];
static const TEXT crtName[];
#endif
static const TEXT filesysboxName[];

#ifdef __AROS__
__startup AROS_UFH3(int, startup,
	AROS_UFHA(STRPTR, argstr, A0),
	AROS_UFHA(ULONG, arglen, D0),
	AROS_UFHA(struct ExecBase *, sysbase, A6)
)
{
	AROS_USERFUNC_INIT
#else
int startup(void)
{
#endif
	struct Process   *me;
	struct Message   *msg;
	struct DosPacket *pkt = NULL;
	int               rc = RETURN_ERROR;
	struct MsgPort   *port = NULL;

#ifdef __AROS__
	SysBase = sysbase;
#else
	SysBase = *(struct ExecBase **)4;
#endif

	if (!setup_malloc())
	{
		goto cleanup;
	}

	DOSBase = (struct DosLibrary *)OpenLibrary(dosName, 39);
	if (DOSBase == NULL)
	{
		goto cleanup;
	}

	UtilityBase = (struct UtilityBase *)OpenLibrary(utilityName, 39);
	if (UtilityBase == NULL)
	{
		goto cleanup;
	}

#ifdef __AROS__
	StdlibBase = OpenLibrary(stdlibName, 1);
	if (StdlibBase == NULL)
	{
		goto cleanup;
	}
	CrtBase = OpenLibrary(crtName, 2);
	if (CrtBase == NULL)
	{
		goto cleanup;
	}
#endif

	me = (struct Process *)FindTask(NULL);
	if (me->pr_CLI != 0)
	{
		PutStr(vstring);
		rc = RETURN_OK;
		goto cleanup;
	}

	port = &me->pr_MsgPort;
	WaitPort(port);
	msg = GetMsg(port);
	if (msg == NULL) goto cleanup;

	if (msg->mn_Node.ln_Name == NULL)
	{
		rc = RETURN_FAIL;
		Forbid();
		ReplyMsg(msg);
		goto cleanup;
	}

	pkt = (struct DosPacket *)msg->mn_Node.ln_Name;

	FileSysBoxBase = OpenLibrary(filesysboxName, 53);
	if (FileSysBoxBase == NULL)
	{
		goto cleanup;
	}

	if (exfat_main(msg) == 0)
	{
		rc = RETURN_OK;
	}

	/* Set to NULL so we don't reply the packet twice */
	pkt = NULL;

cleanup:

	if (FileSysBoxBase != NULL)
	{
		CloseLibrary(FileSysBoxBase);
		FileSysBoxBase = NULL;
	}

	if (pkt != NULL)
	{
		ReplyPkt(pkt, DOSFALSE, ERROR_INVALID_RESIDENT_LIBRARY);
		pkt = NULL;
	}

#ifdef __AROS__
	if (CrtBase != NULL)
	{
		CloseLibrary(CrtBase);
		CrtBase = NULL;
	}
	if (StdlibBase != NULL)
	{
		CloseLibrary(StdlibBase);
		StdlibBase = NULL;
	}
#endif

	if (UtilityBase != NULL)
	{
		CloseLibrary((struct Library *)UtilityBase);
		DOSBase = NULL;
	}

	if (DOSBase != NULL)
	{
		CloseLibrary((struct Library *)DOSBase);
		DOSBase = NULL;
	}

	cleanup_malloc();

	return rc;

#ifdef __AROS__
	AROS_USERFUNC_EXIT
#endif
}

static const TEXT USED verstag[] = VERSTAG;
static const TEXT vstring[] = VSTRING;
static const TEXT dosName[] = "dos.library";
static const TEXT utilityName[] = "utility.library";
#ifdef __AROS__
static const TEXT stdlibName[] = "stdlib.library";
static const TEXT crtName[] = "crt.library";
#endif
static const TEXT filesysboxName[] = "filesysbox.library";

