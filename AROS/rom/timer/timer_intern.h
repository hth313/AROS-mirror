#ifndef _TIMER_INTERN_H
#define _TIMER_INTERN_H

/*
    Copyright � 1995-2011, The AROS Development Team. All rights reserved.
    $Id$

    Desc: Internal information about the timer.device and HIDD's
    Lang: english
*/

#include <exec/execbase.h>
#include <exec/lists.h>
#include <exec/interrupts.h>
#include <exec/io.h>
#include <exec/devices.h>
#include <devices/timer.h>
#include <dos/bptr.h>
#include <aros/asmcall.h>

/*
 * First two of these correspond to UNIT_MICROHZ and UNIT_VBLANK.
 * This is important.
 */
#define TL_MICROHZ	0
#define TL_VBLANK	1
#define TL_WAITVBL	2
#define NUM_LISTS	3

struct TimerBase
{
    /* Required by the system */
    struct Device	 tb_Device;		/* Our base						*/
    APTR		 tb_KernelBase;		/* kernel.resource base					*/

    /* Time counters */
    struct timeval	 tb_CurrentTime;	/* Absolute system time. Can be set by TR_SETSYSTIME.	*/
    struct timeval	 tb_Elapsed;		/* Relative system time. Used for measuring intervals.	*/

    /* This is required for hardware-specific code */
    LONG		 tb_TimerIRQNum;	/* Timer IRQ number					*/
    APTR		 tb_TimerIRQHandle;	/* Timer IRQ handle					*/
    struct Interrupt	 tb_VBlankInt;		/* Used by older implementations, needs to be removed	*/
    struct timeval	 tb_VBlankTime;		/* Software-emulated periodic timer interval		*/

    /* Request queues */
    struct MinList	 tb_Lists[NUM_LISTS];

    /* EClock counter */
    UQUAD                tb_ticks_total;	/* Effective EClock value				*/
    ULONG                tb_ticks_sec;		/* Fraction of second for CurrentTime in ticks		*/
    ULONG                tb_ticks_elapsed;	/* Fraction of second for Elapsed in ticks		*/
    ULONG                tb_prev_tick;		/* Hardware-specific					*/
    ULONG		 tb_eclock_rate;	/* EClock frequency					*/

#ifdef USE_VBLANK_EMU
    struct timerequest   tb_vblank_timerequest; /* VBlank emulation request				*/
#endif
};

#define GetTimerBase(tb)	((struct TimerBase *)(tb))
#define GetDevice(tb)		((struct Device *)(tb))

BOOL common_BeginIO(struct timerequest *timereq, struct TimerBase *TimerBase);
void handleMicroHZ(struct TimerBase *TimerBase, struct ExecBase *SysBase);
void handleVBlank(struct TimerBase *TimerBase, struct ExecBase *SysBase);
void EClockUpdate(struct TimerBase *TimerBase);
void EClockSet(struct TimerBase *TimerBase);

#endif /* _TIMER_INTERN_H */
