/*
    (C) 1995-97 AROS - The Amiga Replacement OS
    $Id$

    Desc: Filenote CLI command
    Lang: english
*/

/*****************************************************************************

    NAME

        Filenote

    SYNOPSIS

        FILE/A,COMMENT,ALL/S,QUIET/S

    LOCATION

        Workbench:c

    FUNCTION

        Add a comment to a file or directory.

        Filenote allows a recursive scan of all directories adding comments
        to each file/directory it finds that matches the file pattern
        specified.

    INPUTS

        FILE    - Always has to be specified. Can be either a filename with
                  a full path or a file pattern that is to be matched.

        COMMENT - The ASCII string that is to be added as a comment to the
                  file(s)/dir(s) specified.

                  To provide a comment that has embedded quotation marks,
                  precede each quote with an asterisk.

                    i.e. Filenote FILE=RAM:test.txt COMMENT=*"hello*"

        ALL     - Boolean switch. If specified, Filenote scans the directories
                  that match the pattern specified, recursively.

        QUIET   - Boolean switch. If specified, no diagnostic text will be
                  displayed to stdout.

    RESULT

        Standard DOS return codes.

    NOTES

        Output from AROS' Filenote is more neat and structured than the
        standard Filenote command.

        Does not yet support multi-assigns.

    EXAMPLE

        Filenote ram: hello all

            Recurses through each directory in RAM: adding "hello" as a
            filenote to each file/directory.

    BUGS

    SEE ALSO

        SetComment()

    INTERNALS

    HISTORY

        27-Jul-1997     laguest     Initial inclusion into the AROS tree

******************************************************************************/

#include <proto/arossupport.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <dos/dos.h>
#include <dos/dosasl.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <exec/memory.h>
#include <utility/utility.h>

#include <ctype.h>

#define ARG_TEMPLATE    "FILE/A,COMMENT,ALL/S,QUIET/S"
#define ARG_FILE        0
#define ARG_COMMENT     1
#define ARG_ALL         2
#define ARG_QUIET       3
#define TOTAL_ARGS      4

/* To define whether a command line switch was set or not.
 */
#define NOT_SET         0

#define MAX_PATH_LEN    512

static const char version[] = "$VER: Filenote 41.0 (20.04.1997)\n";

struct UtilityBase *UtilityBase;

int Do_Filenote(struct AnchorPath *, STRPTR, STRPTR, LONG, LONG);

int main(int argc, char *argv[])
{
    struct RDArgs     * rda;
    struct AnchorPath * apath;
    IPTR              * args[TOTAL_ARGS] = { NULL, NULL, NULL, NULL};
    int                 Return_Value;

    Return_Value = RETURN_OK;

    UtilityBase = (struct UtilityBase *)OpenLibrary("utility.library", 39);
    if (UtilityBase != NULL)
    {
        apath = AllocVec(sizeof(struct AnchorPath) + MAX_PATH_LEN,
                         MEMF_ANY | MEMF_CLEAR);
        if (apath)
        {
            /* Make sure DOS knows the buffer size.
             */
            apath->ap_Strlen = MAX_PATH_LEN;

            rda = ReadArgs(ARG_TEMPLATE, (LONG *)args, NULL);
            if (rda)
            {
                Return_Value = Do_Filenote(apath,
                                           (STRPTR)args[ARG_FILE],
                                           (STRPTR)args[ARG_COMMENT],
                                           (LONG)args[ARG_ALL],
                                           (LONG)args[ARG_QUIET]
                );
                FreeArgs(rda);
            }
            else
            {
                PrintFault(IoErr(), "Filenote");

                Return_Value = RETURN_FAIL;
            }
        }
        else
        {
            Return_Value = RETURN_FAIL;
        }

        FreeVec(apath);

        CloseLibrary((struct Library *)UtilityBase);
    }
    else
    {
        VPrintf("Need \'utility.library\' version 39 or above\n", NULL);
        Return_Value = RETURN_FAIL;
    }

    return (Return_Value);

} /* main */


/* Defines whether MatchFirst(), etc has matched a file.
 */
#define MATCHED_FILE    0

void PrintFileName(struct AnchorPath *, LONG);
int SafeSetFileComment(struct AnchorPath *, char *);

int Do_Filenote(struct AnchorPath *a,
                STRPTR File,
                STRPTR Comment,
                LONG All,
                LONG Quiet)
{
    LONG  Result,
          TabValue;
    int   Return_Value;

    Return_Value = RETURN_OK;
    TabValue     = 0L;

    /* Looks to see if the user has given a volume name as a pattern
     * without the all switch set. If so, we fail.
     */

    if
    (
        IsDosEntryA((char *)File, LDF_VOLUMES | LDF_DEVICES) == TRUE
    &&
        All == NOT_SET
    )
    {
        VPrintf("Note failed\n", NULL);
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);

        Return_Value = RETURN_FAIL;
    }
    else
    {
        if (Return_Value != RETURN_FAIL)
        {
            Result = MatchFirst((STRPTR)File, a);

            if (Result == MATCHED_FILE)
            {
                do
                {
                    if (All == NOT_SET)
                    {
                        Return_Value = SafeSetFileComment(a, Comment);

                        if (Quiet == NOT_SET)
                        {
                            PrintFileName(a, TabValue);
                        }
                    }
                    else
                    {
                        /* Allow a recursive scan.
                         */

                        if (a->ap_Info.fib_DirEntryType > 0)
                        {
                            /* Enter directory.
                             */
                            if (!(a->ap_Flags & APF_DIDDIR))
                            {
                                a->ap_Flags |= APF_DODIR;

                                Return_Value = SafeSetFileComment(a, Comment);

                                if (Quiet == NOT_SET)
                                {
                                    PrintFileName(a, TabValue);
                                }
                                TabValue++;
                            }
                            else
                            {
                                /* Leave directory.
                                 */
                                a->ap_Flags &= ~APF_DIDDIR;
                                TabValue--;
                            }
                        }
                        else
                        {
                            Return_Value = SafeSetFileComment(a, Comment);

                            if (Quiet == NOT_SET)
                            {
                                PrintFileName(a, TabValue);
                            }
                        }
                    }
                }
                while
                (
                    ((Result = MatchNext(a)) == MATCHED_FILE)
                &&
                    Return_Value != RETURN_FAIL
                );
            }
            else
            {
                PrintFault(IoErr(), "Filenote");
                Return_Value = RETURN_FAIL;
            }

            MatchEnd(a);
        }
    }

    return (Return_Value);

} /* Do_Filenote */


void PrintFileName(struct AnchorPath *a, LONG t)
{
    int i;
    IPTR args[2];

    args[0] = (IPTR)FilePart(&a->ap_Buf[0]);
    args[1] = (IPTR)NULL;

    VPrintf("   ", NULL);

    for (i = 0; i != t; i++)
    {
        VPrintf("  ", NULL);
    }

    VPrintf("%s", args);

    if (a->ap_Info.fib_DirEntryType > 0)
    {
        VPrintf(" (dir)", NULL);
    }

    VPrintf("...Done\n", NULL);

} /* PrintFileName */


int SafeSetFileComment(struct AnchorPath *a, char *c)
{
    int Return_Value;

    Return_Value = RETURN_OK;

    if (!SetComment(a->ap_Buf, c))
    {
        Return_Value = RETURN_WARN;
    }

    return(Return_Value);

} /* SafeSetFileComment */
