/*
    Copyright � 2002-2003, The AROS Development Team. All rights reserved.
    $Id$
*/

#ifndef _MUI_CLASSES_SEMAPHORE_H
#define _MUI_CLASSES_SEMAPHORE_H

#define MUIC_Semaphore "Semaphore.mui"

/* Semaphore methods */
#define MUIM_Semaphore_Attempt       (MUIB_MUI|0x00426ce2) /* MUI: V11 */
#define MUIM_Semaphore_AttemptShared (MUIB_MUI|0x00422551) /* MUI: V11 */
#define MUIM_Semaphore_Obtain        (MUIB_MUI|0x004276f0) /* MUI: V11 */
#define MUIM_Semaphore_ObtainShared  (MUIB_MUI|0x0042ea02) /* MUI: V11 */
#define MUIM_Semaphore_Release       (MUIB_MUI|0x00421f2d) /* MUI: V11 */
struct MUIP_Semaphore_Attempt        {ULONG MethodID;};
struct MUIP_Semaphore_AttemptShared  {ULONG MethodID;};
struct MUIP_Semaphore_Obtain         {ULONG MethodID;};
struct MUIP_Semaphore_ObtainShared   {ULONG MethodID;};
struct MUIP_Semaphore_Release        {ULONG MethodID;};

extern const struct __MUIBuiltinClass _MUI_Semaphore_desc; /* PRIV */

#endif
