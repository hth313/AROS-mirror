
/*
    Copyright � 1995-2009, The AROS Development Team. All rights reserved.
    $Id$

    Desc: Common startup code
    Lang: english
*/
#define DEBUG 0

#include <aros/config.h>
#include <setjmp.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <aros/asmcall.h>
#include <aros/debug.h>
#include <aros/symbolsets.h>
#include <aros/startup.h>

#include "etask.h"

THIS_PROGRAM_HANDLES_SYMBOLSETS

/* pass these values to the command line handling function */
char *__argstr;
ULONG __argsize;

/* the command line handling functions will pass these values back to us */
char **__argv;
int  __argc;

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct WBStartup *WBenchMsg;

extern int main(int argc, char ** argv);
int (*__main_function_ptr)(int argc, char ** argv) __attribute__((__weak__)) = main;

DEFINESET(CTORS);
DEFINESET(DTORS);
DEFINESET(INIT);
DEFINESET(EXIT);
DEFINESET(PROGRAM_ENTRIES);

/* if the programmer hasn't defined a symbol with the name __nocommandline
   then the code to handle the commandline will be included from the autoinit.lib
*/
extern int __nocommandline;
asm(".set __importcommandline, __nocommandline");

/* if the programmer hasn't defined a symbol with the name __nostdiowin
   then the code to open a window for stdio will be included from the autoinit.lib
*/
extern int __nostdiowin;
asm(".set __importstdiowin, __nostdiowin");

static void __startup_entries_init(void);

struct aros_startup __aros_startup;

/* Guarantee that __startup_entry is placed at the beginning of the binary */
AROS_UFP3(LONG, __startup_entry,
    AROS_UFHA(char *,argstr,A0),
    AROS_UFHA(ULONG,argsize,D0),
    AROS_UFHA(struct ExecBase *,sysbase,A6)
) __attribute__((section(".aros.startup")));

#warning TODO: reset and initialize the FPU
#warning TODO: resident startup
AROS_UFH3(LONG, __startup_entry,
    AROS_UFHA(char *,argstr,A0),
    AROS_UFHA(ULONG,argsize,D0),
    AROS_UFHA(struct ExecBase *,sysbase,A6)
)
{
    AROS_USERFUNC_INIT

    struct Process *myproc;
    
    SysBase = sysbase;

    D(bug("Entering __startup_entry(\"%s\", %d, %x)\n", argstr, argsize, SysBase));

    /*
        No one program will be able to do anything useful without the dos.library,
        so we open it here instead of using the automatic opening system
    */
    DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 39);
    if (!DOSBase) return RETURN_FAIL;

    __argstr  = argstr;
    __argsize = argsize;

    myproc = (struct Process *)FindTask(NULL);
    GetIntETask(myproc)->iet_startup = &__aros_startup;
    __aros_startup.as_startup_error = RETURN_FAIL;

    __startup_entries_init();
    __startup_entries_next();

    CloseLibrary((struct Library *)DOSBase);

    D(bug("Leaving __startup_entry\n"));

    return __aros_startup.as_startup_error;

    AROS_USERFUNC_EXIT
} /* entry */


static void __startup_fromwb(void)
{
    struct Process *myproc;
    BPTR win = NULL;
    BPTR old_in, old_out, old_err;

    D(bug("Entering __startup_fromwb()\n"));

    myproc = (struct Process *)FindTask(NULL);

    /* Do we have a CLI structure? */
    if (!myproc->pr_CLI)
    {
	/* Workbench startup. Get WBenchMsg and pass it to main() */

	WaitPort(&myproc->pr_MsgPort);
	WBenchMsg = (struct WBStartup *)GetMsg(&myproc->pr_MsgPort);
	__argv = (char **) WBenchMsg;
        __argc = 0;

	D(bug("[startup] Started from Workbench\n"));
    }

    __startup_entries_next();

    /* Reply startup message to Workbench */
    if (WBenchMsg)
    {
        Forbid(); /* make sure we're not UnLoadseg()ed before we're really done */
        ReplyMsg((struct Message *) WBenchMsg);
    }

    D(bug("Leaving __startup_fromwb\n"));
}


static void __startup_initexit(void)
{
    D(bug("Entering __startup_initexit\n"));

    if (set_open_libraries())
    {
        if
	(
	    setjmp(__aros_startup.as_startup_jmp_buf) == 0 &&
            set_call_funcs(SETNAME(INIT), 1, 1)
	)
	{
            /* ctors/dtors get called in inverse order than init funcs */
            set_call_funcs(SETNAME(CTORS), -1, 0);

            __startup_entries_next();

            set_call_funcs(SETNAME(DTORS), 1, 0);
        }
        set_call_funcs(SETNAME(EXIT), -1, 0);
    }
    set_close_libraries();
    
    D(bug("Leaving __startup_initexit\n"));
}


static void __startup_main(void)
{
    D(bug("Entering __startup_main\n"));

    /* Invoke the main function. A weak symbol is used as function name so that
       it can be overridden (for *nix stuff, for instance).  */
    __aros_startup.as_startup_error = (*__main_function_ptr) (__argc, __argv);

    D(bug("Leaving __startup_main\n"));
}

ADD2SET(__startup_fromwb, program_entries, -50);
ADD2SET(__startup_initexit, program_entries, 126);
ADD2SET(__startup_main, program_entries, 127);


static int __startup_entry_pos;

void __startup_entries_init(void)
{
    __startup_entry_pos = 1;
}

void __startup_entries_next(void)
{
    void (*entry_func)(void);
 
    entry_func = SETNAME(PROGRAM_ENTRIES)[__startup_entry_pos];
    if (entry_func)
    {
        __startup_entry_pos++;
        entry_func();
    }
}


/*
    Stub function for GCC __main().

    The __main() function is originally used for C++ style constructors
    and destructors in C. This replacement does nothing and gets rid of
    linker-errors about references to __main().
*/
#ifdef AROS_NEEDS___MAIN
void __main(void)
{
    /* Do nothing. */
}
#endif
