/*
    (C) 1995-96 AROS - The Amiga Research OS
    $Id$

    Desc: Init of workbench.library
    Lang: english
*/

#define AROS_ALMOST_COMPATIBLE
#include "lowlevel_intern.h"
#include "libdefs.h"

#ifdef SysBase
#   undef SysBase
#endif
#ifdef ExecBase
#   undef ExecBase
#endif

/* Customize libheader.c */
#define LC_SYSBASE_FIELD(lib)	(((LIBBASETYPEPTR)(lib))->ll_SysBase)
#define LC_SEGLIST_FIELD(lib)   (((LIBBASETYPEPTR)(lib))->ll_SegList)
#define LC_RESIDENTNAME		LowLevel_resident
#define LC_RESIDENTFLAGS	RTF_AUTOINIT|RTF_COLDSTART
#define LC_RESIDENTPRI		-120
#define LC_LIBBASESIZE		sizeof(LIBBASETYPE)
#define LC_LIBHEADERTYPEPTR	LIBBASETYPEPTR
#define LC_LIB_FIELD(lib)       (((LIBBASETYPEPTR)(lib))->LibNode)

#define LC_NO_OPENLIB
#define LC_NO_CLOSELIB
#define LC_STATIC_INITLIB

#include <libcore/libheader.c>

struct ExecBase   * SysBase; /* global variable */
struct LocaleBase * LocaleBase;

ULONG SAVEDS LC_BUILDNAME(L_InitLib) (LC_LIBHEADERTYPEPTR lh)
{
    SysBase = lh->ll_SysBase;

    LocaleBase = (struct LocaleBase *)OpenLibrary("locale.library",39);
    if (!LocaleBase)
        return FALSE;

    return TRUE;
} /* L_InitLib */

void SAVEDS L_ExpungeLib (LC_LIBHEADERTYPEPTR lh)
{
    if (LocaleBase)
        CloseLibrary(LocaleBase);
}
