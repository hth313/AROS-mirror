#ifndef __VFORK_H
#define __VFORK_H

/*
    Copyright � 2008-2009, The AROS Development Team. All rights reserved.
    $Id$
*/

#include <exec/types.h>
#include <dos/bptr.h>
#include <exec/exec.h>
#include <setjmp.h>
#include <sys/types.h>
#include <aros/startup.h>

struct vfork_data
{
    struct vfork_data *prev;
    jmp_buf vfork_jump;
    
    struct Task *parent;
    jmp_buf startup_jmp_buf;

    ULONG child_id;
    BYTE parent_signal;
    APTR parent_acpd_fd_mempool;
    void *parent_acpd_fd_array;
    int parent_acpd_numslots;
    APTR parent_mempool;
    int parent_cd_changed;
    BPTR parent_cd_lock;
    BPTR parent_curdir;
    struct arosc_privdata *ppriv;

    struct Task *child;
    struct arosc_privdata *cpriv;
    int child_executed;
    int child_errno;
    BYTE child_signal;

    const char *exec_filename;
    char *const *exec_argv;
    char *const *exec_envp;
    APTR exec_id;
};

pid_t __vfork(jmp_buf env);
void vfork_longjmp (jmp_buf env, int val);

#endif /* __VFORK_H */
