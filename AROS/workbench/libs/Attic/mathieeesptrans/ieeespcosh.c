/*
    (C) 1995-97 AROS - The Amiga Replacement OS
    $Id$

    Desc:
    Lang: english
*/


#include <libraries/mathieeesp.h>
#include <aros/libcall.h>
#include <proto/mathieeesingbas.h>
#include <proto/mathieeesingtrans.h>
#include <proto/exec.h>
#include <exec/types.h>
#include "mathieeesingtrans_intern.h"


/*****************************************************************************

    NAME */

      AROS_LH1(LONG, IEEESPCosh,

/*  SYNOPSIS */

      AROS_LHA(LONG, y, D0),

/*  LOCATION */

      struct Library *, MathIeeeSingTransBase, 11, Mathieeesingtrans)

/*  FUNCTION

      Calculate the hyperbolic cosine of the IEEE single precision number

    INPUTS

      y - IEEE single precision floating point number

    RESULT

      IEEE single precision floating point number


      flags:
	zero	 : result is zero
	negative : 0 (not possible)
	overflow : result too big for ffp-number

    NOTES

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS
      cosh(x) = (1/2)*( e^x + e^(-x) )

      cosh( |x| >= 9  ) = (1/2) * (e^x);

    HISTORY

******************************************************************************/

{
LONG Res;
  /* cosh(-x) = cosh(x) */
  y &= ( IEEESPMantisse_Mask + IEEESPExponent_Mask );

  if ( IEEESP_Pinfty == y)
  {
    SetSR(Overflow_Bit, Zero_Bit | Negative_Bit | Overflow_Bit);
    return y;
  }

  Res = IEEESPExp(y);

  if ( IEEESP_Pinfty == Res )
  {
    SetSR(Overflow_Bit, Zero_Bit | Negative_Bit | Overflow_Bit);
    return 0x7f000000; /* Res; */
  }

  if ( y < 0x41100000  )
    Res = IEEESPAdd(Res, IEEESPDiv(one, Res));

  /* Res = Res / 2 */
  Res -= 0x00800000;

  if ( 0 == Res || Res < 0 )
  {
    SetSR(Zero_Bit, Zero_Bit | Negative_Bit | Overflow_Bit);
    return 0;
  }
  return Res;
} /* SPCosh */
