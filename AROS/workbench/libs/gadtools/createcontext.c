/*
    (C) 1997 AROS - The Amiga Research OS
    $Id$

    Desc:
    Lang: english
*/
#include <proto/intuition.h>
#include <intuition/classusr.h>
#include <utility/tagitem.h>
#include <libraries/gadtools.h>
#include "gadtools_intern.h"

/*********************************************************************

    NAME */
#include <proto/gadtools.h>
#include <intuition/intuition.h>

        AROS_LH1(struct Gadget *, CreateContext,

/*  SYNOPSIS */
	AROS_LHA(struct Gadget **, glistpointer, A0),

/*  LOCATION */
	struct Library *, GadToolsBase, 19, GadTools)

/*  FUNCTION
        Creates a virtual first gadget to which all following gadgets must be
        linked.

    INPUTS
        glistpointer - pointer to a pointer to the virtual gadget. Use the
                       gadget pointer as NewWindow->FirstGadget or in
                       AddGList(). The gadget pointer must be initialized
                       to NULL before CreateContext() is called.

    RESULT
        A point to the first gadget or NULL, if there was an error.

    NOTES

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

    HISTORY

***************************************************************************/
{
    AROS_LIBFUNC_INIT
    AROS_LIBBASE_EXT_DECL(struct GadToolsBase_intern *,GadToolsBase)

    struct GT_ContextGadget *rc;
    
    rc = (struct GT_ContextGadget *)AllocMem(sizeof(struct GT_ContextGadget),MEMF_PUBLIC | MEMF_CLEAR);
    if (rc)
    {
    	rc->gad.Flags = GFLG_GADGHNONE | GFLG_DISABLED;
	rc->gad.GadgetType = GTYP_GADTOOLS;
    }
    
    *glistpointer = (struct Gadget *)rc;
    
    return (struct Gadget *)rc;

    AROS_LIBFUNC_EXIT
} /* CreateContext */
