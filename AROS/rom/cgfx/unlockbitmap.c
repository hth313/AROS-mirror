/*
    (C) 1997 AROS - The Amiga Research OS
    $Id$

    Desc:
    Lang: english
*/
#include "cybergraphics_intern.h"

/*****************************************************************************

    NAME */
#include <clib/cybergraphics_protos.h>

	AROS_LH1(void, UnLockBitMap,

/*  SYNOPSIS */
	AROS_LHA(APTR, Handle, A0),

/*  LOCATION */
	struct Library *, CyberGfxBase, 29, Cybergraphics)

/*  FUNCTION

    INPUTS

    RESULT

    NOTES

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

    HISTORY
	27-11-96    digulla automatically created from
			    cybergraphics_lib.fd and clib/cybergraphics_protos.h

*****************************************************************************/
{
    AROS_LIBFUNC_INIT
    AROS_LIBBASE_EXT_DECL(struct Library *,CyberGfxBase)
    
    driver_UnLockBitMap(Handle, CyberGfxBase);

    AROS_LIBFUNC_EXIT
} /* UnLockBitMap */
