/*
    Copyright � 2000-2010, The AROS Development Team. All rights reserved.
    $Id$

    Desc:
    Lang: English
*/

#include <sys/errno.h>
#include <sys/socket.h>

#include "bsdsocket_intern.h"

/*****************************************************************************

    NAME */

        AROS_LH6(int, recvfrom,

/*  SYNOPSIS */
        AROS_LHA(int,               s,       D0),
        AROS_LHA(void *,            buf,     A0),
        AROS_LHA(int,               len,     D1),
        AROS_LHA(int,               flags,   D2),
        AROS_LHA(struct sockaddr *, from,    A1),
        AROS_LHA(int *,             fromlen, A2),

/*  LOCATION */
        struct bsdsocketBase *, SocketBase, 12, BSDSocket)

/*  FUNCTION

    INPUTS

    RESULT

    NOTES

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

    HISTORY

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    return bsd_recvfrom(s, buf, len, flags, from, fromlen);

    AROS_LIBFUNC_EXIT

} /* recvfrom */
