/* FLOAT.C      (c) Copyright Peter Kuschnerus, 2000                 */
/*              ESA/390 Hex Floatingpoint Instructions               */

/*-------------------------------------------------------------------*/
/* This module implements the ESA/390 Hex Floatingpoint Instructions */
/* described in the manual ESA/390 Principles of Operation.          */
/*-------------------------------------------------------------------*/

#include "hercules.h"


/*-------------------------------------------------------------------*/
/* Structure definition for internal short floatingpoint format      */
/*-------------------------------------------------------------------*/
typedef struct _SHORT_FLOAT {
	U32	short_fract;		/* Fraction                  */
	short	expo;			/* Exponent + 64             */
	BYTE	sign;			/* Sign			     */
} SHORT_FLOAT;

/*-------------------------------------------------------------------*/
/* Structure definition for internal long floatingpoint format       */
/*-------------------------------------------------------------------*/
typedef struct _LONG_FLOAT {
	U64	long_fract;		/* Fraction                  */
	short	expo;			/* Exponent + 64             */
	BYTE	sign;			/* Sign			     */
} LONG_FLOAT;

/*-------------------------------------------------------------------*/
/* Structure definition for internal extended floatingpoint format   */
/*-------------------------------------------------------------------*/
typedef struct _EXTENDED_FLOAT {
	U64	ms_fract;		/* Most significant fraction */
	U64	ls_fract;		/* Least significant fraction*/
	short	expo;			/* Exponent + 64             */
	BYTE	sign;			/* Sign			     */
} EXTENDED_FLOAT;

#define	POS	0			/* Positive value of sign    */
#define NEG	1			/* Negative value of sign    */
#define UNNORMAL 0			/* Without normalisation     */
#define NORMAL	1			/* With normalisation	     */


/*-------------------------------------------------------------------*/
/* Static functions                                                  */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Add 128 bit integer                                               */
/* The result is placed in operand a                                 */
/*                                                                   */
/* Input:                                                            */
/*	msa 	most significant 64 bit of operand a                 */
/*	lsa 	least significant 64 bit of operand a                */
/*	msb 	most significant 64 bit of operand b                 */
/*	lsb 	least significant 64 bit of operand b                */
/*-------------------------------------------------------------------*/
static void add_U128 ( U64 *msa, U64 *lsa, U64 msb, U64 lsb )
{
U64 wk;
U32 sum;
U32 c;

    wk = (*lsa & 0x00000000FFFFFFFFULL) + (lsb & 0x00000000FFFFFFFFULL);
    sum = wk;
    c = wk >> 32;
    wk = (*lsa >> 32) + (lsb >> 32) + c;
    c = wk >> 32;
    *lsa = (wk << 32) | sum;
    *msa = *msa + msb + c;

} /* end function add_U128 */


/*-------------------------------------------------------------------*/
/* Subtract 128 bit integer                                          */
/* The operand b is subtracted from operand a                        */
/* The result is placed in operand a                                 */
/*                                                                   */
/* Input:                                                            */
/*	msa 	most significant 64 bit of operand a                 */
/*	lsa 	least significant 64 bit of operand a                */
/*	msb 	most significant 64 bit of operand b                 */
/*	lsb 	least significant 64 bit of operand b                */
/*-------------------------------------------------------------------*/
static void sub_U128 ( U64 *msa, U64 *lsa, U64 msb, U64 lsb )
{
S64 wk;
U32 sum;
int c;

    wk = (*lsa & 0x00000000FFFFFFFFULL) - (lsb & 0x00000000FFFFFFFFULL);
    sum = wk;
    c = wk >> 32;
    wk = (*lsa >> 32) - (lsb >> 32) + c;
    c = wk >> 32;
    *lsa = (wk << 32) | sum;
    *msa = *msa - msb + c;

} /* end function sub_U128 */


/*-------------------------------------------------------------------*/
/* Subtract 128 bit integer reverse                                  */
/* The operand a is subtracted from operand b                        */
/* The result is placed in operand a                                 */
/*                                                                   */
/* Input:                                                            */
/*	msa 	most significant 64 bit of operand a                 */
/*	lsa 	least significant 64 bit of operand a                */
/*	msb 	most significant 64 bit of operand b                 */
/*	lsb 	least significant 64 bit of operand b                */
/*-------------------------------------------------------------------*/
static void sub_reverse_U128 ( U64 *msa, U64 *lsa, U64 msb, U64 lsb )
{
S64 wk;
U32 sum;
int c;

    wk = (lsb & 0x00000000FFFFFFFFULL) - (*lsa & 0x00000000FFFFFFFFULL);
    sum = wk;
    c = wk >> 32;
    wk = (lsb >> 32) - (*lsa >> 32) + c;
    c = wk >> 32;
    *lsa = (wk << 32) | sum;
    *msa = msb - *msa + c;

} /* end function sub_reverse_U128 */


/*-------------------------------------------------------------------*/
/* Get short float from register                                     */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float format to be converted to	     */
/*	fpr	Register to be converted from			     */
/*-------------------------------------------------------------------*/
static void get_sf ( SHORT_FLOAT *fl, U32 *fpr )
{
    fl->sign = *fpr >> 31;
    fl->expo = (*fpr >> 24) & 0x007F;
    fl->short_fract = *fpr & 0x00FFFFFF;

} /* end function get_sf */


/*-------------------------------------------------------------------*/
/* Store short float to register                                     */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float format to be converted from	     */
/*	fpr	Register to be converted to			     */
/*-------------------------------------------------------------------*/
static void store_sf ( SHORT_FLOAT *fl, U32 *fpr )
{
    *fpr = ((U32)fl->sign << 31) 
	 | ((U32)fl->expo << 24) 
	 | (fl->short_fract);

} /* end function store_sf */


/*-------------------------------------------------------------------*/
/* Get long float from register                                      */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float format to be converted to	     */
/*	fpr	Register to be converted from			     */
/*-------------------------------------------------------------------*/
static void get_lf ( LONG_FLOAT *fl, U32 *fpr )
{
    fl->sign = *fpr >> 31;
    fl->expo = (*fpr >> 24) & 0x007F;
    fl->long_fract = ((U64)(fpr[0] & 0x00FFFFFF) << 32) 
		   | fpr[1];

} /* end function get_lf */


/*-------------------------------------------------------------------*/
/* Store long float to register                                      */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float format to be converted from	     */
/*	fpr	Register to be converted to			     */
/*-------------------------------------------------------------------*/
static void store_lf ( LONG_FLOAT *fl, U32 *fpr )
{
    fpr[0] = ((U32)fl->sign << 31) 
	   | ((U32)fl->expo << 24) 
	   | (fl->long_fract >> 32);
    fpr[1] = fl->long_fract;

} /* end function store_lf */


/*-------------------------------------------------------------------*/
/* Get extended float from register                                  */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float format to be converted to	     */
/*	fpr	Register to be converted from			     */
/*-------------------------------------------------------------------*/
static void get_ef ( EXTENDED_FLOAT *fl, U32 *fpr )
{
    fl->sign = *fpr >> 31;
    fl->expo = (*fpr >> 24) & 0x007F;
    fl->ms_fract = ((U64)(fpr[0] & 0x00FFFFFF) << 24) 
		 | (fpr[1] >> 8);
    fl->ls_fract = (((U64)fpr[1]) << 56) 
		 | (((U64)(fpr[2] & 0x00FFFFFF)) << 32) 
		 | fpr[3];

} /* end function get_ef */


/*-------------------------------------------------------------------*/
/* Store extended float to register                                  */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float format to be converted from	     */
/*	fpr	Register to be converted to			     */
/*-------------------------------------------------------------------*/
static void store_ef ( EXTENDED_FLOAT *fl, U32 *fpr )
{
char ls_sign;

    ls_sign = (fl->expo - 14) & 0x007F;
    fpr[0] = ((U32)fl->sign << 31) 
	   | ((U32)fl->expo << 24) 
	   | (fl->ms_fract >> 24);
    fpr[1] = (fl->ms_fract << 8) 
	   | (fl->ls_fract >> 56);
    fpr[2] = ((U32)fl->sign << 31) 
	   | ((U32)ls_sign << 24) 
	   | ((fl->ls_fract >> 32) & 0x00FFFFFF);
    fpr[3] = fl->ls_fract;

} /* end function store_ef */


/*-------------------------------------------------------------------*/
/* Fetch a short floatingpoint operand from virtual storage          */
/*                                                                   */
/* Input:                                                            */
/*      fl      Internal float format                                */
/*      addr    Logical address of leftmost byte of operand          */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
static void vfetch_sf ( SHORT_FLOAT *fl, U32 addr, int arn, REGS *regs )
{
U32     value;                          /* Operand value             */

    /* Fetch 4 bytes from operand address */
    value = vfetch4 (addr, arn, regs);

    /* Extract sign and exponent from high-order byte */
    fl->sign = value >> 31;
    fl->expo = (value >> 24) & 0x007F;

    /* Extract fraction from low-order 3 bytes */
    fl->short_fract = value & 0x00FFFFFF;

} /* end function vfetch_sf */


/*-------------------------------------------------------------------*/
/* Fetch a long floatingpoint operand from virtual storage           */
/*                                                                   */
/* Input:                                                            */
/*      fl      Internal float format                                */
/*      addr    Logical address of leftmost byte of operand          */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
static void vfetch_lf ( LONG_FLOAT *fl, U32 addr, int arn, REGS *regs )
{
U64     value;                          /* Operand value             */

    /* Fetch 8 bytes from operand address */
    value = vfetch8 (addr, arn, regs);

    /* Extract sign and exponent from high-order byte */
    fl->sign = value >> 63;
    fl->expo = (value >> 56) & 0x007F;

    /* Extract fraction from low-order 7 bytes */
    fl->long_fract = value & 0x00FFFFFFFFFFFFFFULL;

} /* end function vfetch_lf */


/*-------------------------------------------------------------------*/
/* Normalize short float                                             */
/* The fraction is expected to be non zero                           */
/* If zero an endles loop is caused !!!!!!                           */ 
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*-------------------------------------------------------------------*/
static void normal_sf ( SHORT_FLOAT *fl )
{
    while ((fl->short_fract & 0x00F00000) == 0) {
	fl->short_fract <<= 4;
	(fl->expo)--;
    }

} /* end function normal_sf */


/*-------------------------------------------------------------------*/
/* Normalize long float                                              */
/* The fraction is expected to be non zero                           */
/* If zero an endles loop is caused !!!!!!                           */ 
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*-------------------------------------------------------------------*/
static void normal_lf ( LONG_FLOAT *fl )
{
    while ((fl->long_fract & 0x00F0000000000000ULL) == 0) {
	fl->long_fract <<= 4;
	(fl->expo)--;
    }

} /* end function normal_lf */


/*-------------------------------------------------------------------*/
/* Normalize extended float                                          */
/* The fraction is expected to be non zero                           */
/* If zero an endles loop is caused !!!!!!                           */ 
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*-------------------------------------------------------------------*/
static void normal_ef ( EXTENDED_FLOAT *fl )
{
    while (fl->ms_fract == 0) {
	fl->ms_fract = fl->ls_fract >> 16;
	fl->ls_fract <<= 48;
	fl->expo -= 12;
    }

    while ((fl->ms_fract & 0x0000F00000000000ULL) == 0) {
	if (fl->ls_fract) {
	    fl->ms_fract = (fl->ms_fract << 4) 
			 | (fl->ls_fract >> 60);
	    fl->ls_fract <<= 4;
	} else {
	    fl->ms_fract <<= 4;
	}
	(fl->expo)--;
    }

} /* end function normal_ef */


/*-------------------------------------------------------------------*/
/* Overflow of short float                                           */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void overflow_sf ( SHORT_FLOAT *fl, REGS *regs )
{
    if (fl->expo > 127) {
	fl->expo &= 0x007F;
	program_check (regs, PGM_EXPONENT_OVERFLOW_EXCEPTION);
    }
    
} /* end function overflow_sf */


/*-------------------------------------------------------------------*/
/* Overflow of long float                                            */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void overflow_lf ( LONG_FLOAT *fl, REGS *regs )
{
    if (fl->expo > 127) {
	fl->expo &= 0x007F;
	program_check (regs, PGM_EXPONENT_OVERFLOW_EXCEPTION);
    }
    
} /* end function overflow_lf */


/*-------------------------------------------------------------------*/
/* Overflow of extended float                                        */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void overflow_ef ( EXTENDED_FLOAT *fl, REGS *regs )
{
    if (fl->expo > 127) {
	fl->expo &= 0x007F;
	program_check (regs, PGM_EXPONENT_OVERFLOW_EXCEPTION);
    }
    
} /* end function overflow_ef */


/*-------------------------------------------------------------------*/
/* Underflow of short float                                          */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void underflow_sf ( SHORT_FLOAT *fl, REGS *regs )
{
    if (fl->expo < 0) {
	if (regs->psw.eumask) {
	    fl->expo &= 0x007F;
	    program_check (regs, PGM_EXPONENT_UNDERFLOW_EXCEPTION);
	} else {
	    /* set true 0 */

	    fl->short_fract = 0;
	    fl->sign = POS;
	    fl->expo = 0;
	}
    }

} /* end function underflow_sf */


/*-------------------------------------------------------------------*/
/* Underflow of long float                                           */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void underflow_lf ( LONG_FLOAT *fl, REGS *regs )
{
    if (fl->expo < 0) {
	if (regs->psw.eumask) {
	    fl->expo &= 0x007F;
	    program_check (regs, PGM_EXPONENT_UNDERFLOW_EXCEPTION);
	} else {
	    /* set true 0 */

	    fl->long_fract = 0;
	    fl->sign = POS;
	    fl->expo = 0;
	}
    }

} /* end function underflow_lf */


/*-------------------------------------------------------------------*/
/* Underflow of extended float                                       */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void underflow_ef ( EXTENDED_FLOAT *fl, REGS *regs )
{
    if (fl->expo < 0) {
	if (regs->psw.eumask) {
	    fl->expo &= 0x007F;
	    program_check (regs, PGM_EXPONENT_UNDERFLOW_EXCEPTION);
	} else {
	    /* set true 0 */

	    fl->ms_fract = 0;
	    fl->ls_fract = 0;
	    fl->sign = POS;
	    fl->expo = 0;
	}
    }

} /* end function underflow_ef */


/*-------------------------------------------------------------------*/
/* Significance of short float                                       */
/* The fraction is expected to be zero                               */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void significance_sf ( SHORT_FLOAT *fl, REGS *regs )
{
    fl->sign = POS;
    if (regs->psw.sgmask) {
        program_check (regs, PGM_SIGNIFICANCE_EXCEPTION);
    } else {
        /* true 0 */

        fl->expo = 0;
    }

} /* end function significance_sf */


/*-------------------------------------------------------------------*/
/* Significance of long float                                        */
/* The fraction is expected to be zero                               */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void significance_lf ( LONG_FLOAT *fl, REGS *regs )
{
    fl->sign = POS;
    if (regs->psw.sgmask) {
        program_check (regs, PGM_SIGNIFICANCE_EXCEPTION);
    } else {
        /* true 0 */

        fl->expo = 0;
    }

} /* end function significance_lf */


/*-------------------------------------------------------------------*/
/* Significance of extended float                                    */
/* The fraction is expected to be zero                               */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void significance_ef ( EXTENDED_FLOAT *fl, REGS *regs )
{
    fl->sign = POS;
    if (regs->psw.sgmask) {
        program_check (regs, PGM_SIGNIFICANCE_EXCEPTION);
    } else {
        /* true 0 */

        fl->expo = 0;
    }

} /* end function significance_ef */


/*-------------------------------------------------------------------*/
/* Add short float                                                   */
/*                                                                   */
/* Input:                                                            */
/*      fl	Float						     */
/*      add_fl	Float to be added				     */
/*	normal	Normalize if true				     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void add_sf ( SHORT_FLOAT *fl, SHORT_FLOAT *add_fl, BYTE normal, 
		REGS *regs )
{
BYTE shift;

    if (add_fl->short_fract) {	/* add_fl not 0 */
	if (fl->short_fract) {	/* fl not 0 */
	    /* both not 0 */

	    if (fl->expo == add_fl->expo) {
		/* expo equal */

		/* both guard digits */
	    	fl->short_fract <<= 4;
	    	add_fl->short_fract <<= 4;
	    } else {
		/* expo not equal, denormalize */

		if (fl->expo < add_fl->expo) {
		    /* shift minus guard digit */
		    shift = add_fl->expo - fl->expo - 1;
		    fl->expo = add_fl->expo;

		    if (shift) {
		    	if (shift >= 6 || ((fl->short_fract >>= (shift * 4)) == 0)) {	
			    /* 0, copy summand */

			    fl->sign = add_fl->sign;
			    fl->short_fract = add_fl->short_fract;

			    if (normal == NORMAL) {
				normal_sf (fl);
				underflow_sf (fl, regs);
			    }
			    return;
			}
		    }
		    /* guard digit */
	    	    add_fl->short_fract <<= 4;
		} else {
		    /* shift minus guard digit */
		    shift = fl->expo - add_fl->expo - 1;

		    if (shift) {
		    	if (shift >= 6 || ((add_fl->short_fract >>= (shift * 4)) == 0)) {
			    /* 0, nothing to add */

			    if (normal == NORMAL) {
				normal_sf (fl);
				underflow_sf (fl, regs);
			    }
			    return;
			}
		    }
		    /* guard digit */
	    	    fl->short_fract <<= 4;
		}
	    }

	    /* compute with guard digit */
	    if (fl->sign == add_fl->sign) {
		fl->short_fract += add_fl->short_fract;
	    } else {
		if (fl->short_fract == add_fl->short_fract) {
		    /* true 0 */

		    fl->short_fract = 0;
		    significance_sf (fl, regs);
		    return;

		} else if (fl->short_fract > add_fl->short_fract) {
		    fl->short_fract -= add_fl->short_fract;
		} else {
		    fl->short_fract = add_fl->short_fract - fl->short_fract;
		    fl->sign = add_fl->sign;
		}
	    }

	    /* handle overflow with guard digit */
	    if (fl->short_fract & 0xF0000000) {
		fl->short_fract >>= 8;
		(fl->expo)++;
		overflow_sf (fl, regs);
	    } else {

	 	if (normal == NORMAL) {
		    /* normalize with guard digit */
	    	    if (fl->short_fract) {
		        /* not 0 */

		        if (fl->short_fract & 0x0F000000) {
			    /* not normalize, just guard digit */
		            fl->short_fract >>= 4;
		        } else {
			    (fl->expo)--;
			    normal_sf (fl);
			    underflow_sf (fl, regs);
		        }
		    } else {
		        /* true 0 */

		 	significance_sf (fl, regs);
		    }
	        } else {
		    /* not normalize, just guard digit */
		    fl->short_fract >>= 4;
	    	    if (fl->short_fract == 0) {
		 	significance_sf (fl, regs);
		    }
	        }
	    }
	    return;
	} else { /* fl 0, add_fl not 0 */
	    /* copy summand */

	    fl->expo = add_fl->expo;
	    fl->sign = add_fl->sign;
	    fl->short_fract = add_fl->short_fract;
	}
    } else { 			    /* add_fl 0 */
	if (fl->short_fract == 0) { /* fl 0 */
	    /* both 0 */

	    significance_sf (fl, regs);
	    return;
	}
    }
    if (normal == NORMAL) {
	normal_sf (fl);
	underflow_sf (fl, regs);
    }

} /* end function add_sf */


/*-------------------------------------------------------------------*/
/* Add long float                                                    */
/*                                                                   */
/* Input:                                                            */
/*      fl	Float						     */
/*      add_fl	Float to be added				     */
/*	normal	Normalize if true				     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void add_lf ( LONG_FLOAT *fl, LONG_FLOAT *add_fl, BYTE normal, 
		REGS *regs )
{
BYTE shift;

    if (add_fl->long_fract) {	/* add_fl not 0 */
	if (fl->long_fract) {	/* fl not 0 */
	    /* both not 0 */

	    if (fl->expo == add_fl->expo) {	
                /* expo equal */

		/* both guard digits */
                fl->long_fract <<= 4;
                add_fl->long_fract <<= 4;
            } else {
		/* expo not equal, denormalize */

		if (fl->expo < add_fl->expo) {
		    /* shift minus guard digit */
		    shift = add_fl->expo - fl->expo - 1;
		    fl->expo = add_fl->expo;

		    if (shift) {
		    	if (shift >= 14 || ((fl->long_fract >>= (shift * 4)) == 0)) {	
			    /* 0, copy summand */

			    fl->sign = add_fl->sign;
			    fl->long_fract = add_fl->long_fract;

			    if (normal == NORMAL) {
				normal_lf (fl);
				underflow_lf (fl, regs);
			    }
			    return;
			}
		    }
		    /* guard digit */
                    add_fl->long_fract <<= 4;
		} else {
		    /* shift minus guard digit */
		    shift = fl->expo - add_fl->expo - 1;

		    if (shift) {
		    	if (shift >= 14 || ((add_fl->long_fract >>= (shift * 4)) == 0)) {
			    /* 0, nothing to add */

			    if (normal == NORMAL) {
				normal_lf (fl);
				underflow_lf (fl, regs);
			    }
			    return;
			}
		    }
		    /* guard digit */
                    fl->long_fract <<= 4;
		}
	    }

	    /* compute with guard digit */
	    if (fl->sign == add_fl->sign) {
		fl->long_fract += add_fl->long_fract;
	    } else {
		if (fl->long_fract == add_fl->long_fract) {
		    /* true 0 */

		    fl->long_fract = 0;
		    significance_lf (fl, regs);
		    return;

		} else if (fl->long_fract > add_fl->long_fract) {
		    fl->long_fract -= add_fl->long_fract;
		} else {
		    fl->long_fract = add_fl->long_fract - fl->long_fract;
		    fl->sign = add_fl->sign;
		}
	    }

	    /* handle overflow with guard digit */
            if (fl->long_fract & 0xF000000000000000ULL) {
                fl->long_fract >>= 8;
                (fl->expo)++;
		overflow_lf (fl, regs);
            } else {

                if (normal == NORMAL) {
                    /* normalize with guard digit */
                    if (fl->long_fract) {
                        /* not 0 */

                        if (fl->long_fract & 0x0F00000000000000ULL) {
                            /* not normalize, just guard digit */
                            fl->long_fract >>= 4;
                        } else {
			    (fl->expo)--;
			    normal_lf (fl);
			    underflow_lf (fl, regs);
                        }
                    } else {
                        /* true 0 */

                        significance_lf (fl, regs);
                    }
                } else {
                    /* not normalize, just guard digit */
                    fl->long_fract >>= 4;
                    if (fl->long_fract == 0) {
                        significance_lf (fl, regs);
                    }
                }
            }
	    return;
	} else { /* fl 0, add_fl not 0 */
	    /* copy summand */

	    fl->expo = add_fl->expo;
	    fl->sign = add_fl->sign;
	    fl->long_fract = add_fl->long_fract;
	}
    } else {                       /* add_fl 0 */
        if (fl->long_fract == 0) { /* fl 0 */
            /* both 0 */

            significance_lf (fl, regs);
            return;
        }
    }
    if (normal == NORMAL) {
	normal_lf (fl);
	underflow_lf (fl, regs);
    }

} /* end function add_lf */


/*-------------------------------------------------------------------*/
/* Add extended float normalized                                     */
/*                                                                   */
/* Input:                                                            */
/*      fl	Float						     */
/*      add_fl	Float to be added				     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void add_ef ( EXTENDED_FLOAT *fl, EXTENDED_FLOAT *add_fl, REGS *regs )
{
BYTE shift;

    if (add_fl->ms_fract || add_fl->ls_fract) {	/* not 0 */
	if (fl->ms_fract || fl->ls_fract) {	/* not 0 */
	    /* both not 0 */

	    if (fl->expo == add_fl->expo) {	
                /* expo equal */

		/* both guard digits */
		fl->ms_fract = (fl->ms_fract << 4) 
			     | (fl->ls_fract >> 60);
                fl->ls_fract <<= 4;
		add_fl->ms_fract = (add_fl->ms_fract << 4) 
				 | (add_fl->ls_fract >> 60);
                add_fl->ls_fract <<= 4;
            } else {
		/* expo not equal, denormalize */

		if (fl->expo < add_fl->expo) {
		    /* shift minus guard digit */
		    shift = add_fl->expo - fl->expo - 1;
		    fl->expo = add_fl->expo;

		    if (shift) {
		    	if (shift >= 28) {
			    /* 0, copy summand */

			    fl->sign = add_fl->sign;
			    fl->ms_fract = add_fl->ms_fract;
			    fl->ls_fract = add_fl->ls_fract;

			    normal_ef (fl);
			    underflow_ef (fl, regs);
			    return;
			} else if (shift >= 16) {
			    fl->ls_fract = fl->ms_fract;
			    if (shift > 16) {
				fl->ls_fract >>= (shift - 16) * 4;
			    }
			    fl->ms_fract = 0;
			} else {
			    shift *= 4;
			    fl->ls_fract = fl->ms_fract << (64 - shift) 
					 | fl->ls_fract >> shift;
			    fl->ms_fract >>= shift;
			}

			if ((fl->ms_fract == 0) && (fl->ls_fract == 0)) {	
			    /* 0, copy summand */

			    fl->sign = add_fl->sign;
			    fl->ms_fract = add_fl->ms_fract;
			    fl->ls_fract = add_fl->ls_fract;

			    normal_ef (fl);
			    underflow_ef (fl, regs);
			    return;
			}
		    }
		    /* guard digit */
		    add_fl->ms_fract = (add_fl->ms_fract << 4) 
				     | (add_fl->ls_fract >> 60);
                    add_fl->ls_fract <<= 4;
		} else {
		    /* shift minus guard digit */
		    shift = fl->expo - add_fl->expo - 1;

		    if (shift) {
		    	if (shift >= 28) {
			    /* 0, nothing to add */

			    normal_ef (fl);
			    underflow_ef (fl, regs);
			    return;
			} else if (shift >= 16) {
			    add_fl->ls_fract = add_fl->ms_fract;
			    if (shift > 16) {
				add_fl->ls_fract >>= (shift - 16) * 4;
			    }
			    add_fl->ms_fract = 0;
			} else {
			    shift *= 4;
			    add_fl->ls_fract = add_fl->ms_fract << (64 - shift) 
					     | add_fl->ls_fract >> shift;
			    add_fl->ms_fract >>= shift;
			}

			if ((add_fl->ms_fract == 0) && (add_fl->ls_fract == 0)) {	
			    /* 0, nothing to add */

			    normal_ef (fl);
			    underflow_ef (fl, regs);
			    return;
			}
		    }
		    /* guard digit */
		    fl->ms_fract = (fl->ms_fract << 4) 
				 | (fl->ls_fract >> 60);
                    fl->ls_fract <<= 4;
		}
	    }

	    /* compute with guard digit */
	    if (fl->sign == add_fl->sign) {
		add_U128 (&(fl->ms_fract), 
			  &(fl->ls_fract), 
			  add_fl->ms_fract, 
			  add_fl->ls_fract);
	    } else {
		if ((fl->ms_fract == add_fl->ms_fract) 
		    && (fl->ls_fract == add_fl->ls_fract)) {
		    /* true 0 */

		    fl->ms_fract = 0;
		    fl->ls_fract = 0;
		    significance_ef (fl, regs);
		    return;

		} else if ((fl->ms_fract > add_fl->ms_fract) 
			|| ((fl->ms_fract == add_fl->ms_fract) 
			 && (fl->ls_fract > add_fl->ls_fract))) {
		    sub_U128 (&(fl->ms_fract), 
			  &(fl->ls_fract), 
			  add_fl->ms_fract, 
			  add_fl->ls_fract);
		} else {
		    sub_reverse_U128 (&(fl->ms_fract), 
			  &(fl->ls_fract), 
			  add_fl->ms_fract, 
			  add_fl->ls_fract);
		    fl->sign = add_fl->sign;
		}
	    }

	    /* handle overflow with guard digit */
	    if (fl->ms_fract & 0x00F0000000000000ULL) {
		fl->ls_fract = (fl->ms_fract << 56) 
			 | (fl->ls_fract >> 8);
		fl->ms_fract >>= 8;
                (fl->expo)++;
		overflow_ef (fl, regs);
            } else {
		/* normalize with guard digit */
		if (fl->ms_fract || fl->ls_fract) {
		    /* not 0 */

		    if (fl->ms_fract & 0x000F000000000000ULL) {
			/* not normalize, just guard digit */
		 	fl->ls_fract = (fl->ms_fract << 60) 
				     | (fl->ls_fract >> 4);
			fl->ms_fract >>= 4;
		    } else {
			(fl->expo)--;
			normal_ef (fl);
			underflow_ef (fl, regs);
		    }
		} else {
		    /* true 0 */

		    significance_ef (fl, regs);
		}
	    }
	    return;
	} else { /* fl 0, add_fl not 0 */
	    /* copy summand */

	    fl->expo = add_fl->expo;
	    fl->sign = add_fl->sign;
	    fl->ms_fract = add_fl->ms_fract;
	    fl->ls_fract = add_fl->ls_fract;
	}
    } else {						  /* add_fl 0*/
	if ((fl->ms_fract == 0) && (fl->ls_fract == 0)) { /* fl 0 */
	    /* both 0 */

	    significance_ef (fl, regs);
	    return;
	}
    }
    normal_ef (fl);
    underflow_ef (fl, regs);

} /* end function add_ef */


/*-------------------------------------------------------------------*/
/* Compare short float                                               */
/*                                                                   */
/* Input:                                                            */
/*      fl	Float						     */
/*      cmp_fl	Float to be compared				     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void cmp_sf ( SHORT_FLOAT *fl, SHORT_FLOAT *cmp_fl, REGS *regs )
{
BYTE shift;

    if (cmp_fl->short_fract) {	/* cmp_fl not 0 */
	if (fl->short_fract) {	/* fl not 0 */
	    /* both not 0 */

	    if (fl->expo == cmp_fl->expo) {
		/* expo equal */

		/* both guard digits */
	    	fl->short_fract <<= 4;
	    	cmp_fl->short_fract <<= 4;
	    } else {
		/* expo not equal, denormalize */

		if (fl->expo < cmp_fl->expo) {
		    /* shift minus guard digit */
		    shift = cmp_fl->expo - fl->expo - 1;

		    if (shift) {
		    	if (shift >= 6 || ((fl->short_fract >>= (shift * 4)) == 0)) {	
			    /* Set condition code */
			    if (cmp_fl->short_fract) {
			        regs->psw.cc = cmp_fl->sign ? 2 : 1;
			    } else {
			    	regs->psw.cc = 0;
			    }

			    return;
			}
		    }
		    /* guard digit */
	    	    cmp_fl->short_fract <<= 4;
		} else {
		    /* shift minus guard digit */
		    shift = fl->expo - cmp_fl->expo - 1;

		    if (shift) {
		    	if (shift >= 6 || ((cmp_fl->short_fract >>= (shift * 4)) == 0)) {
			    /* Set condition code */
			    if (fl->short_fract) {
			        regs->psw.cc = fl->sign ? 1 : 2;
			    } else {
			    	regs->psw.cc = 0;
			    }

			    return;
			}
		    }
		    /* guard digit */
	    	    fl->short_fract <<= 4;
		}
	    }

	    /* compute with guard digit */
	    if (fl->sign != cmp_fl->sign) {
		fl->short_fract += cmp_fl->short_fract;
	    } else if (fl->short_fract >= cmp_fl->short_fract) {
		fl->short_fract -= cmp_fl->short_fract;
	    } else {
		fl->short_fract = cmp_fl->short_fract - fl->short_fract;
		fl->sign = ! (cmp_fl->sign);
	    }

	    /* handle overflow with guard digit */
	    if (fl->short_fract & 0xF0000000) {
		fl->short_fract >>= 8;
	    } else {
		/* guard digit */
		fl->short_fract >>= 4;
	    }

	    /* Set condition code */
	    if (fl->short_fract) {
	        regs->psw.cc = fl->sign ? 1 : 2;
	    } else {
	    	regs->psw.cc = 0;
	    }

	    return;
	} else { /* fl 0, cmp_fl not 0 */
	    /* Set condition code */
	    regs->psw.cc = cmp_fl->sign ? 2 : 1;

	    return;
	}
    } else { 			    /* cmp_fl 0 */
	/* Set condition code */
	if (fl->short_fract) {
	    regs->psw.cc = fl->sign ? 1 : 2;
	} else {
	    regs->psw.cc = 0;
	}

	return;
    }

} /* end function cmp_sf */


/*-------------------------------------------------------------------*/
/* Compare long float                                                */
/*                                                                   */
/* Input:                                                            */
/*      fl	Float						     */
/*      cmp_fl	Float to be compared				     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void cmp_lf ( LONG_FLOAT *fl, LONG_FLOAT *cmp_fl, REGS *regs )
{
BYTE shift;

    if (cmp_fl->long_fract) {	/* cmp_fl not 0 */
	if (fl->long_fract) {	/* fl not 0 */
	    /* both not 0 */

	    if (fl->expo == cmp_fl->expo) {
		/* expo equal */

		/* both guard digits */
	    	fl->long_fract <<= 4;
	    	cmp_fl->long_fract <<= 4;
	    } else {
		/* expo not equal, denormalize */

		if (fl->expo < cmp_fl->expo) {
		    /* shift minus guard digit */
		    shift = cmp_fl->expo - fl->expo - 1;

		    if (shift) {
		    	if (shift >= 6 || ((fl->long_fract >>= (shift * 4)) == 0)) {	
			    /* Set condition code */
			    if (cmp_fl->long_fract) {
			        regs->psw.cc = cmp_fl->sign ? 2 : 1;
			    } else {
			    	regs->psw.cc = 0;
			    }

			    return;
			}
		    }
		    /* guard digit */
	    	    cmp_fl->long_fract <<= 4;
		} else {
		    /* shift minus guard digit */
		    shift = fl->expo - cmp_fl->expo - 1;

		    if (shift) {
		    	if (shift >= 6 || ((cmp_fl->long_fract >>= (shift * 4)) == 0)) {
			    /* Set condition code */
			    if (fl->long_fract) {
			        regs->psw.cc = fl->sign ? 1 : 2;
			    } else {
			    	regs->psw.cc = 0;
			    }

			    return;
			}
		    }
		    /* guard digit */
	    	    fl->long_fract <<= 4;
		}
	    }

	    /* compute with guard digit */
	    if (fl->sign != cmp_fl->sign) {
		fl->long_fract += cmp_fl->long_fract;
	    } else if (fl->long_fract >= cmp_fl->long_fract) {
		fl->long_fract -= cmp_fl->long_fract;
	    } else {
		fl->long_fract = cmp_fl->long_fract - fl->long_fract;
		fl->sign = ! (cmp_fl->sign);
	    }

	    /* handle overflow with guard digit */
	    if (fl->long_fract & 0xF0000000) {
		fl->long_fract >>= 8;
	    } else {
		/* guard digit */
		fl->long_fract >>= 4;
	    }

	    /* Set condition code */
	    if (fl->long_fract) {
	        regs->psw.cc = fl->sign ? 1 : 2;
	    } else {
	    	regs->psw.cc = 0;
	    }

	    return;
	} else { /* fl 0, cmp_fl not 0 */
	    /* Set condition code */
	    regs->psw.cc = cmp_fl->sign ? 2 : 1;

	    return;
	}
    } else { 			    /* cmp_fl 0 */
	/* Set condition code */
	if (fl->long_fract) {
	    regs->psw.cc = fl->sign ? 1 : 2;
	} else {
	    regs->psw.cc = 0;
	}

	return;
    }

} /* end function cmp_lf */


/*-------------------------------------------------------------------*/
/* Multiply short float to long float                                */
/*                                                                   */
/* Input:                                                            */
/*      fl	Multiplicand short float			     */
/*      mul_fl	Multiplicator short float			     */
/*      result_fl	Result long float			     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void mul_sf_to_lf ( SHORT_FLOAT *fl, SHORT_FLOAT *mul_fl, 
		LONG_FLOAT *result_fl, REGS *regs )
{
    if (fl->short_fract && mul_fl->short_fract) {
	/* normalize operands */
	normal_sf ( fl );
	normal_sf ( mul_fl );

	/* multiply fracts */
	result_fl->long_fract = (U64) fl->short_fract * mul_fl->short_fract;

	/* normalize result and compute expo */
	if (result_fl->long_fract & 0x0000F00000000000ULL) {
	    result_fl->long_fract <<= 8;
	    result_fl->expo = fl->expo + mul_fl->expo - 64;
	} else {
	    result_fl->long_fract <<= 12;
	    result_fl->expo = fl->expo + mul_fl->expo - 65;
	}

	/* handle overflow and underflow */
	overflow_lf (result_fl, regs);
	underflow_lf (result_fl, regs);

	/* determine sign */
	result_fl->sign = (fl->sign == mul_fl->sign) ? POS : NEG;
    } else {
    	/* true 0 */

    	result_fl->sign = POS;
    	result_fl->expo = 0;
    	result_fl->long_fract = 0;
    }
} /* end function mul_sf_to_lf */


/*-------------------------------------------------------------------*/
/* Multiply long float to extended float                             */
/*                                                                   */
/* Input:                                                            */
/*      fl	Multiplicand long float				     */
/*      mul_fl	Multiplicator long float			     */
/*      result_fl	Result extended float			     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void mul_lf_to_ef ( LONG_FLOAT *fl, LONG_FLOAT *mul_fl, 
		EXTENDED_FLOAT *result_fl, REGS *regs )
{
U64 wk;

    if (fl->long_fract && mul_fl->long_fract) {
	/* normalize operands */
	normal_lf ( fl );
	normal_lf ( mul_fl );

	/* multiply fracts by sum of partial multiplications */
	wk = (fl->long_fract & 0x00000000FFFFFFFFULL) * (mul_fl->long_fract & 0x00000000FFFFFFFFULL);
	result_fl->ls_fract = wk & 0x00000000FFFFFFFFULL;

	wk >>= 32;
	wk += (fl->long_fract & 0x00000000FFFFFFFFULL) * (mul_fl->long_fract >> 32);
	wk += (fl->long_fract >> 32) * (mul_fl->long_fract & 0x00000000FFFFFFFFULL);
	result_fl->ls_fract |= wk << 32;

	result_fl->ms_fract = (wk >> 32) + ((fl->long_fract >> 32) * (mul_fl->long_fract >> 32));

	/* normalize result and compute expo */
	if (result_fl->ms_fract & 0x0000F00000000000ULL) {
	    result_fl->expo = fl->expo + mul_fl->expo - 64;
	} else {
	    result_fl->ms_fract = (result_fl->ms_fract << 4) | (result_fl->ls_fract >> 60);
	    result_fl->ls_fract <<= 4;
	    result_fl->expo = fl->expo + mul_fl->expo - 65;
	}

	/* handle overflow and underflow */
	overflow_ef (result_fl, regs);
	underflow_ef (result_fl, regs);

	/* determine sign */
	result_fl->sign = (fl->sign == mul_fl->sign) ? POS : NEG;
    } else {
    	/* true 0 */

    	result_fl->sign = POS;
    	result_fl->expo = 0;
    	result_fl->ms_fract = 0;
    	result_fl->ls_fract = 0;
    }
} /* end function mul_lf_to_ef */


/*-------------------------------------------------------------------*/
/* Multiply long float			                             */
/*                                                                   */
/* Input:                                                            */
/*      fl	Multiplicand long float				     */
/*      mul_fl	Multiplicator long float			     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void mul_lf ( LONG_FLOAT *fl, LONG_FLOAT *mul_fl, REGS *regs )
{
U64 wk;
U32 v;

    if (fl->long_fract && mul_fl->long_fract) {
	/* normalize operands */
	normal_lf ( fl );
	normal_lf ( mul_fl );

	/* multiply fracts by sum of partial multiplications */
	wk = ((fl->long_fract & 0x00000000FFFFFFFFULL) * (mul_fl->long_fract & 0x00000000FFFFFFFFULL)) >> 32;

	wk += (fl->long_fract & 0x00000000FFFFFFFFULL) * (mul_fl->long_fract >> 32);
	wk += (fl->long_fract >> 32) * (mul_fl->long_fract & 0x00000000FFFFFFFFULL);
	v = wk;

	fl->long_fract = (wk >> 32) + ((fl->long_fract >> 32) * (mul_fl->long_fract >> 32));

	/* normalize result and compute expo */
	if (fl->long_fract & 0x0000F00000000000ULL) {
	    fl->long_fract = (fl->long_fract << 8) | (v >> 24);
	    fl->expo = fl->expo + mul_fl->expo - 64;
	} else {
	    fl->long_fract = (fl->long_fract << 12) | (v >> 20);
	    fl->expo = fl->expo + mul_fl->expo - 65;
	}

	/* handle overflow and underflow */
	overflow_lf (fl, regs);
	underflow_lf (fl, regs);

	/* determine sign */
	fl->sign = (fl->sign == mul_fl->sign) ? POS : NEG;
    } else {
    	/* true 0 */

    	fl->sign = POS;
    	fl->expo = 0;
    	fl->long_fract = 0;
    }
} /* end function mul_lf */


/*-------------------------------------------------------------------*/
/* Multiply extended float		                             */
/*                                                                   */
/* Input:                                                            */
/*      fl	Multiplicand extended float			     */
/*      mul_fl	Multiplicator extended float			     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void mul_ef ( EXTENDED_FLOAT *fl, EXTENDED_FLOAT *mul_fl, REGS *regs )
{
U64 wk;
U32 v;

    if ((fl->ms_fract || fl->ls_fract)
     && (mul_fl->ms_fract || mul_fl->ls_fract)) {
	/* normalize operands */
	normal_ef ( fl );
	normal_ef ( mul_fl );

	/* multiply fracts by sum of partial multiplications */
	wk = ((fl->ls_fract & 0x00000000FFFFFFFFULL) * (mul_fl->ls_fract & 0x00000000FFFFFFFFULL)) >> 32;

	wk += (fl->ls_fract & 0x00000000FFFFFFFFULL) * (mul_fl->ls_fract >> 32);
	wk += (fl->ls_fract >> 32) * (mul_fl->ls_fract & 0x00000000FFFFFFFFULL);

	wk >>= 32;
	wk += (fl->ls_fract & 0x00000000FFFFFFFFULL) * (mul_fl->ms_fract & 0x00000000FFFFFFFFULL);
	wk += (fl->ls_fract >> 32) * (mul_fl->ls_fract >> 32);
	wk += (fl->ms_fract & 0x00000000FFFFFFFFULL) * (mul_fl->ls_fract & 0x00000000FFFFFFFFULL);

	wk >>= 32;
	wk += (fl->ls_fract & 0x00000000FFFFFFFFULL) * (mul_fl->ms_fract >> 32);
	wk += (fl->ls_fract >> 32) * (mul_fl->ms_fract & 0x00000000FFFFFFFFULL);
	wk += (fl->ms_fract & 0x00000000FFFFFFFFULL) * (mul_fl->ls_fract >> 32);
	wk += (fl->ms_fract >> 32) * (mul_fl->ls_fract & 0x00000000FFFFFFFFULL);
	v = wk;

	wk >>= 32;
	wk += (fl->ls_fract >> 32) * (mul_fl->ms_fract >> 32);
	wk += (fl->ms_fract & 0x00000000FFFFFFFFULL) * (mul_fl->ms_fract & 0x00000000FFFFFFFFULL);
	wk += (fl->ms_fract >> 32) * (mul_fl->ls_fract >> 32);
	fl->ls_fract = wk & 0x00000000FFFFFFFFULL;

	wk >>= 32;
	wk += (fl->ms_fract & 0x00000000FFFFFFFFULL) * (mul_fl->ms_fract >> 32);
	wk += (fl->ms_fract >> 32) * (mul_fl->ms_fract & 0x00000000FFFFFFFFULL);
	fl->ls_fract |= wk << 32;

	fl->ms_fract = (wk >> 32) + ((fl->ms_fract >> 32) * (mul_fl->ms_fract >> 32));

	/* normalize result and compute expo */
	if (fl->ms_fract & 0x00000000F0000000ULL) {
	    fl->ms_fract = (fl->ms_fract << 16) | (fl->ls_fract >> 48);
	    fl->ls_fract = (fl->ls_fract << 16) | (v >> 16);
	    fl->expo = fl->expo + mul_fl->expo - 64;
	} else {
	    fl->ms_fract = (fl->ms_fract << 20) | (fl->ls_fract >> 44);
	    fl->ls_fract = (fl->ls_fract << 20) | (v >> 12);
	    fl->expo = fl->expo + mul_fl->expo - 65;
	}

	/* handle overflow and underflow */
	overflow_ef (fl, regs);
	underflow_ef (fl, regs);

	/* determine sign */
	fl->sign = (fl->sign == mul_fl->sign) ? POS : NEG;
    } else {
    	/* true 0 */

    	fl->sign = POS;
    	fl->expo = 0;
    	fl->ms_fract = 0;
    	fl->ls_fract = 0;
    }
} /* end function mul_ef */


/*-------------------------------------------------------------------*/
/* Divide short float			                             */
/*                                                                   */
/* Input:                                                            */
/*      fl	Dividend short float				     */
/*      mul_fl	Divisor short float				     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void div_sf ( SHORT_FLOAT *fl, SHORT_FLOAT *div_fl, REGS *regs )
{
U64 wk;

    if (div_fl->short_fract) {
	if (fl->short_fract) {
	    /* normalize operands */
	    normal_sf ( fl );
	    normal_sf ( div_fl );

	    /* position fracts and compute expo */
	    if (fl->short_fract < div_fl->short_fract) {
		wk = (U64) fl->short_fract << 24;
		fl->expo = fl->expo - div_fl->expo + 64;
	    } else {
		wk = (U64) fl->short_fract << 20;
		fl->expo = fl->expo - div_fl->expo + 65;
	    }
	    /* divide fractions */
	    fl->short_fract = wk / div_fl->short_fract;
		
	    /* handle overflow and underflow */
	    overflow_sf (fl, regs);
	    underflow_sf (fl, regs);

	    /* determine sign */
	    fl->sign = (fl->sign == div_fl->sign) ? POS : NEG;
	} else {
    	    /* fraction of dividend 0, true 0 */

    	    fl->sign = POS;
    	    fl->expo = 0;
    	    fl->short_fract = 0;
	}
    } else {
	/* divisor 0 */

        program_check (regs, PGM_FLOATING_POINT_DIVIDE_EXCEPTION);
    }
} /* end function div_sf */


/*-------------------------------------------------------------------*/
/* Divide long float			                             */
/*                                                                   */
/* Input:                                                            */
/*      fl	Dividend long float				     */
/*      mul_fl	Divisor long float				     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void div_lf ( LONG_FLOAT *fl, LONG_FLOAT *div_fl, REGS *regs )
{
U64 wk;
U64 wk2;
int i;

    if (div_fl->long_fract) {
	if (fl->long_fract) {
	    /* normalize operands */
	    normal_lf ( fl );
	    normal_lf ( div_fl );

	    /* position fracts and compute expo */
	    if (fl->long_fract < div_fl->long_fract) {
		fl->expo = fl->expo - div_fl->expo + 64;
	    } else {
		fl->expo = fl->expo - div_fl->expo + 65;
		div_fl->long_fract <<= 4;
	    }

	    /* partial divide first hex digit */
	    wk2 = fl->long_fract / div_fl->long_fract;
	    wk = (fl->long_fract % div_fl->long_fract) << 4;

	    /* partial divide middle hex digits */
	    i = 13;
	    while (i--) {
		wk2 = (wk2 << 4) | (wk / div_fl->long_fract);
		wk = (wk % div_fl->long_fract) << 4;
	    }

	    /* partial divide last hex digit */
	    fl->long_fract = (wk2 << 4) | (wk / div_fl->long_fract);

	    /* handle overflow and underflow */
	    overflow_lf (fl, regs);
	    underflow_lf (fl, regs);

	    /* determine sign */
	    fl->sign = (fl->sign == div_fl->sign) ? POS : NEG;
	} else {
    	    /* fraction of dividend 0, true 0 */

    	    fl->sign = POS;
    	    fl->expo = 0;
    	    fl->long_fract = 0;
	}
    } else {
	/* divisor 0 */

        program_check (regs, PGM_FLOATING_POINT_DIVIDE_EXCEPTION);
    }
} /* end function div_lf */


/*-------------------------------------------------------------------*/
/* Divide extended float		                             */
/*                                                                   */
/* Input:                                                            */
/*      fl	Dividend extended float				     */
/*      mul_fl	Divisor extended float				     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void div_ef ( EXTENDED_FLOAT *fl, EXTENDED_FLOAT *div_fl, REGS *regs )
{
U64 wkm;
U64 wkl;
int i;

    if (div_fl->ms_fract || div_fl->ls_fract) {
	if (fl->ms_fract || fl->ls_fract) {
	    /* normalize operands */
	    normal_ef ( fl );
	    normal_ef ( div_fl );

	    /* position fracts and compute expo */
	    if ((fl->ms_fract < div_fl->ms_fract)
	     || ((fl->ms_fract == div_fl->ms_fract) && (fl->ls_fract < div_fl->ls_fract))) {
		fl->expo = fl->expo - div_fl->expo + 64;
	    } else {
		fl->expo = fl->expo - div_fl->expo + 65;
		div_fl->ms_fract = (div_fl->ms_fract << 4) | (div_fl->ls_fract >> 60);
		div_fl->ls_fract <<= 4;
	    }

	    /* divide fractions */

	    /* the first binary digit */
	    wkm = fl->ms_fract;
	    wkl = fl->ls_fract;
	    sub_U128 (&wkm, &wkl, div_fl->ms_fract, div_fl->ls_fract);
	    wkm = (wkm << 1) | (wkl >> 63);
	    wkl <<= 1;
	    fl->ms_fract = 0;
	    if (((S64)wkm) >= 0) {
		fl->ls_fract = 1;
		sub_U128 (&wkm, &wkl, div_fl->ms_fract, div_fl->ls_fract);
	    } else {
		fl->ls_fract = 0;
		add_U128 (&wkm, &wkl, div_fl->ms_fract, div_fl->ls_fract);
	    }

	    /* the middle binary digits */
	    i = 111;
	    while (i--) {
		wkm = (wkm << 1) | (wkl >> 63);
		wkl <<= 1;

		fl->ms_fract = (fl->ms_fract << 1) | (fl->ls_fract >> 63);
		fl->ls_fract <<= 1;
		if (((S64)wkm) >= 0) {
		    fl->ls_fract |= 1;
		    sub_U128 (&wkm, &wkl, div_fl->ms_fract, div_fl->ls_fract);
		} else {
		    add_U128 (&wkm, &wkl, div_fl->ms_fract, div_fl->ls_fract);
		}
	    }

	    /* the last binary digit */
	    fl->ms_fract = (fl->ms_fract << 1) | (fl->ls_fract >> 63);
	    fl->ls_fract <<= 1;
	    if (((S64)wkm) >= 0) {
		fl->ls_fract |= 1;
	    }

	    /* handle overflow and underflow */
	    overflow_ef (fl, regs);
	    underflow_ef (fl, regs);

	    /* determine sign */
	    fl->sign = (fl->sign == div_fl->sign) ? POS : NEG;
	} else {
    	    /* fraction of dividend 0, true 0 */

    	    fl->sign = POS;
    	    fl->expo = 0;
    	    fl->ms_fract = 0;
    	    fl->ls_fract = 0;
	}
    } else {
	/* divisor 0 */

        program_check (regs, PGM_FLOATING_POINT_DIVIDE_EXCEPTION);
    }
} /* end function div_ef */


/*-------------------------------------------------------------------*/
/* Extern functions                                                  */
/*-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* Halve float long register	                                     */
/*                                                                   */
/* Input:                                                            */
/*      r1	Result long float register			     */
/*      r2	Input long float register			     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void halve_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;

    /* Get register content */
    get_lf (&fl, regs->fpr + r2);

    /* Halve the value */
    fl.long_fract >>= 1;
    if (fl.long_fract) {
	normal_lf (&fl);
	underflow_lf (&fl, regs);
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Round float long register	                                     */
/*                                                                   */
/* Input:                                                            */
/*      r1	Result long float register			     */
/*      r2	Input long float register			     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void round_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;

    /* Get register content */
    get_lf (&fl, regs->fpr + r2);

    /* Rounding */
    fl.long_fract += (regs->fpr[r2 + 2] >> 23) & 1;

    /* Handle overflow */
    if (fl.long_fract & 0x0F00000000000000ULL) {
	fl.long_fract >>= 4;
	(fl.expo)++;
	overflow_lf (&fl, regs);
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Multiply float extended register                                  */
/*                                                                   */
/* Input:                                                            */
/*      r1	Multiplicand and result extended float register      */
/*      r2	Multiplicator extended float register		     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void multiply_float_ext_reg (int r1, int r2, REGS *regs)
{
EXTENDED_FLOAT fl;
EXTENDED_FLOAT mul_fl;

    /* Get the operands */
    get_ef (&fl, regs->fpr + r1);
    get_ef (&mul_fl, regs->fpr + r2);

    /* multiply extended */
    mul_ef (&fl, &mul_fl, regs);

    /* Back to register */
    store_ef (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Multiply float long to extended register                          */
/*                                                                   */
/* Input:                                                            */
/*      r1	Multiplicand long and result extended float register */
/*      r2	Multiplicator long float register		     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void multiply_float_long_to_ext_reg (int r1, int r2, REGS *regs)
{ 
LONG_FLOAT fl;
LONG_FLOAT mul_fl;
EXTENDED_FLOAT result_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&mul_fl, regs->fpr + r2);

    /* multiply long to extended */
    mul_lf_to_ef (&fl, &mul_fl, &result_fl, regs);

    /* Back to register */
    store_ef (&result_fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Compare float long to register                                    */
/*                                                                   */
/* Input:                                                            */
/*      r1	Long float register                                  */
/*      r2	Long float register		                     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void compare_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT cmp_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&cmp_fl, regs->fpr + r2);

    /* Compare long */
    cmp_lf (&fl, &cmp_fl, regs);
}


/*-------------------------------------------------------------------*/
/* Add float long to register                                        */
/*                                                                   */
/* Input:                                                            */
/*      r1	Long float register                                  */
/*      r2	Long float register		                     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void add_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT add_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&add_fl, regs->fpr + r2);

    /* Add long with normalization */
    add_lf (&fl, &add_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Subtract float long to register                                   */
/*                                                                   */
/* Input:                                                            */
/*      r1	Long float register                                  */
/*      r2	Long float register		                     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void subtract_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT sub_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&sub_fl, regs->fpr + r2);

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add long with normalization */
    add_lf (&fl, &sub_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Multiply float long register                                      */
/*                                                                   */
/* Input:                                                            */
/*      r1	Multiplicand and result long float register          */
/*      r2	Multiplicator long float register		     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void multiply_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT mul_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&mul_fl, regs->fpr + r2);

    /* multiply long */
    mul_lf (&fl, &mul_fl, regs);

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Divide float long register                                        */
/*                                                                   */
/* Input:                                                            */
/*      r1	Dividend and result long float register              */
/*      r2	Divisor long float register		             */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void divide_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT div_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&div_fl, regs->fpr + r2);

    /* divide long */
    div_lf (&fl, &div_fl, regs);

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Add unnormalized float long register                              */
/*                                                                   */
/* Input:                                                            */
/*      r1	Long float register                                  */
/*      r2	Long float register		                     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void add_unnormal_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT add_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&add_fl, regs->fpr + r2);

    /* Add long without normalization */
    add_lf (&fl, &add_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Subtract unnormalized float long register                         */
/*                                                                   */
/* Input:                                                            */
/*      r1	Long float register                                  */
/*      r2	Long float register		                     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void subtract_unnormal_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT sub_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&sub_fl, regs->fpr + r2);

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add long without normalization */
    add_lf (&fl, &sub_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Halve float short register                                        */
/*                                                                   */
/* Input:                                                            */
/*      r1	Result short float register                          */
/*      r2	Input short float register		             */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void halve_float_short_reg (int r1, int r2, REGS *regs)
{
SHORT_FLOAT fl;

    /* Get register content */
    get_sf (&fl, regs->fpr + r2);

    /* Halve the value */
    fl.short_fract >>= 1;
    if (fl.short_fract) {
	normal_sf (&fl);
	underflow_sf (&fl, regs);
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Round float short register                                        */
/*                                                                   */
/* Input:                                                            */
/*      r1	Result short float register                          */
/*      r2	Input short float register		             */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void round_float_short_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT from_fl;
SHORT_FLOAT to_fl;

    /* Get register content */
    get_lf (&from_fl, regs->fpr + r2);

    /* Rounding */
    to_fl.short_fract = (from_fl.long_fract + 0x0000000080000000ULL) >> 32;
    to_fl.sign = from_fl.sign;
    to_fl.expo = from_fl.expo;

    /* Handle overflow */
    if (to_fl.short_fract & 0x0F000000) {
	to_fl.short_fract >>= 4;
	(to_fl.expo)++;
	overflow_sf (&to_fl, regs);
    }

    /* To register */
    store_sf (&to_fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Add float extended register                                       */
/*                                                                   */
/* Input:                                                            */
/*      r1	Extended float register                              */
/*      r2	Extended float register		                     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void add_float_ext_reg (int r1, int r2, REGS *regs)
{
EXTENDED_FLOAT fl;
EXTENDED_FLOAT add_fl;

    /* Get the operands */
    get_ef (&fl, regs->fpr + r1);
    get_ef (&add_fl, regs->fpr + r2);

    /* Add extended */
    add_ef (&fl, &add_fl, regs);

    /* Set condition code */
    if (fl.ms_fract || fl.ls_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_ef (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Subtract float extended register                                  */
/*                                                                   */
/* Input:                                                            */
/*      r1	Extended float register                              */
/*      r2	Extended float register		                     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void subtract_float_ext_reg (int r1, int r2, REGS *regs)
{
EXTENDED_FLOAT fl;
EXTENDED_FLOAT sub_fl;

    /* Get the operands */
    get_ef (&fl, regs->fpr + r1);
    get_ef (&sub_fl, regs->fpr + r2);

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add extended */
    add_ef (&fl, &sub_fl, regs);

    /* Set condition code */
    if (fl.ms_fract || fl.ls_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_ef (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Compare float short register                                      */
/*                                                                   */
/* Input:                                                            */
/*      r1	Short float register                                 */
/*      r2	Short float register		                     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void compare_float_short_reg (int r1, int r2, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT cmp_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&cmp_fl, regs->fpr + r2);

    /* Compare short */
    cmp_sf (&fl, &cmp_fl, regs);
}


/*-------------------------------------------------------------------*/
/* Add float short register                                          */
/*                                                                   */
/* Input:                                                            */
/*      r1	Short float register                                 */
/*      r2	Short float register		                     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void add_float_short_reg (int r1, int r2, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT add_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&add_fl, regs->fpr + r2);

    /* Add short with normalization */
    add_sf (&fl, &add_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Subtract float short register                                     */
/*                                                                   */
/* Input:                                                            */
/*      r1	Short float register                                 */
/*      r2	Short float register		                     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void subtract_float_short_reg (int r1, int r2, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT sub_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&sub_fl, regs->fpr + r2);

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Subtract short with normalization */
    add_sf (&fl, &sub_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Multiply float short to long register                             */
/*                                                                   */
/* Input:                                                            */
/*      r1	Multiplicand short and result long float register    */
/*      r2	Multiplicator short float register		     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void multiply_float_short_to_long_reg (int r1, int r2, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT mul_fl;
LONG_FLOAT result_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&mul_fl, regs->fpr + r2);

    /* multiply short to long */
    mul_sf_to_lf (&fl, &mul_fl, &result_fl, regs);

    /* Back to register */
    store_lf (&result_fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Divide float short register                                       */
/*                                                                   */
/* Input:                                                            */
/*      r1	Dividend and result short float register             */
/*      r2	Divisor short float register		             */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void divide_float_short_reg (int r1, int r2, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT div_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&div_fl, regs->fpr + r2);

    /* divide short */
    div_sf (&fl, &div_fl, regs);

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Add unnormalized float short register                             */
/*                                                                   */
/* Input:                                                            */
/*      r1	Short float register                                 */
/*      r2	Short float register		                     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void add_unnormal_float_short_reg (int r1, int r2, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT add_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&add_fl, regs->fpr + r2);

    /* Add short without normalization */
    add_sf (&fl, &add_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Subtract unnormalized float short register                        */
/*                                                                   */
/* Input:                                                            */
/*      r1	Short float register                                 */
/*      r2	Short float register		                     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void subtract_unnormal_float_short_reg (int r1, int r2, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT sub_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&sub_fl, regs->fpr + r2);

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add short without normalization */
    add_sf (&fl, &sub_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Multiply float long to extended                                   */
/*                                                                   */
/* Input:                                                            */
/*      r1	Multiplicand long and result extended float register */
/*      addr	Multiplicator long float			     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void multiply_float_long_to_ext (int r1, U32 addr, int arn, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT mul_fl;
EXTENDED_FLOAT result_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&mul_fl, addr, arn, regs );

    /* multiply long to extended */
    mul_lf_to_ef (&fl, &mul_fl, &result_fl, regs);

    /* Back to register */
    store_ef (&result_fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Compare float long                                                */
/*                                                                   */
/* Input:                                                            */
/*      r1	Long float register                                  */
/*      addr	Long float			                     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void compare_float_long (int r1, U32 addr, int arn, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT cmp_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&cmp_fl, addr, arn, regs );

    /* Compare long */
    cmp_lf (&fl, &cmp_fl, regs);
}


/*-------------------------------------------------------------------*/
/* Add float long                                                    */
/*                                                                   */
/* Input:                                                            */
/*      r1	Long float register                                  */
/*      addr	Long float			                     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void add_float_long (int r1, U32 addr, int arn, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT add_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&add_fl, addr, arn, regs );

    /* Add long with normalization */
    add_lf (&fl, &add_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Subtract float long                                               */
/*                                                                   */
/* Input:                                                            */
/*      r1	Long float register                                  */
/*      addr	Long float			                     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void subtract_float_long (int r1, U32 addr, int arn, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT sub_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&sub_fl, addr, arn, regs );

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add long with normalization */
    add_lf (&fl, &sub_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Multiply float long                                               */
/*                                                                   */
/* Input:                                                            */
/*      r1	Multiplicand and result long float register          */
/*      addr	Multiplicator long float			     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void multiply_float_long (int r1, U32 addr, int arn, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT mul_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&mul_fl, addr, arn, regs );

    /* multiply long */
    mul_lf (&fl, &mul_fl, regs);

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Divide float long                                                 */
/*                                                                   */
/* Input:                                                            */
/*      r1	Dividend and result long float register              */
/*      addr	Divisor long float				     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void divide_float_long (int r1, U32 addr, int arn, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT div_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&div_fl, addr, arn, regs );

    /* divide long */
    div_lf (&fl, &div_fl, regs);

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Add unnormalized float long                                       */
/*                                                                   */
/* Input:                                                            */
/*      r1	Long float register                                  */
/*      addr	Long float			                     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void add_unnormal_float_long (int r1, U32 addr, int arn, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT add_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&add_fl, addr, arn, regs );

    /* Add long without normalization */
    add_lf (&fl, &add_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Subtract unnormalized float long                                  */
/*                                                                   */
/* Input:                                                            */
/*      r1	Long float register                                  */
/*      addr	Long float			                     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void subtract_unnormal_float_long (int r1, U32 addr, int arn, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT sub_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&sub_fl, addr, arn, regs );

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add long without normalization */
    add_lf (&fl, &sub_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Compare float short                                               */
/*                                                                   */
/* Input:                                                            */
/*      r1	Short float register                                 */
/*      addr	Short float			                     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void compare_float_short (int r1, U32 addr, int arn, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT cmp_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&cmp_fl, addr, arn, regs );

    /* Compare long */
    cmp_sf (&fl, &cmp_fl, regs);
}


/*-------------------------------------------------------------------*/
/* Add float short                                                   */
/*                                                                   */
/* Input:                                                            */
/*      r1	Short float register                                 */
/*      addr	Short float			                     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void add_float_short (int r1, U32 addr, int arn, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT add_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&add_fl, addr, arn, regs );

    /* Add short with normalization */
    add_sf (&fl, &add_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Subtract float short                                              */
/*                                                                   */
/* Input:                                                            */
/*      r1	Short float register                                 */
/*      addr	Short float			                     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void subtract_float_short (int r1, U32 addr, int arn, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT sub_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&sub_fl, addr, arn, regs );

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add short with normalization */
    add_sf (&fl, &sub_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Multiply float short to long                                      */
/*                                                                   */
/* Input:                                                            */
/*      r1	Multiplicand short and result long float register    */
/*      addr	Multiplicator short float			     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void multiply_float_short_to_long (int r1, U32 addr, int arn, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT mul_fl;
LONG_FLOAT result_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&mul_fl, addr, arn, regs );

    /* multiply short to long */
    mul_sf_to_lf (&fl, &mul_fl, &result_fl, regs);

    /* Back to register */
    store_lf (&result_fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Divide float short                                                */
/*                                                                   */
/* Input:                                                            */
/*      r1	Dividend and result short float register             */
/*      addr	Multiplicator short float			     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void divide_float_short (int r1, U32 addr, int arn, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT div_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&div_fl, addr, arn, regs );

    /* divide short */
    div_sf (&fl, &div_fl, regs);

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Add unnormalized float short                                      */
/*                                                                   */
/* Input:                                                            */
/*      r1	Short float register                                 */
/*      addr	Short float			                     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void add_unnormal_float_short (int r1, U32 addr, int arn, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT add_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&add_fl, addr, arn, regs );

    /* Add short without normalization */
    add_sf (&fl, &add_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Subtract unnormalized float short                                 */
/*                                                                   */
/* Input:                                                            */
/*      r1	Short float register                                 */
/*      addr	Short float			                     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void subtract_unnormal_float_short (int r1, U32 addr, int arn, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT sub_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&sub_fl, addr, arn, regs );

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add short without normalization */
    add_sf (&fl, &sub_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


/*-------------------------------------------------------------------*/
/* Divide float extended register                                    */
/*                                                                   */
/* Input:                                                            */
/*      r1	Dividend and result extended float register          */
/*      r2	Multiplicator extended float register		     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
void divide_float_ext_reg (int r1, int r2, REGS *regs)
{
EXTENDED_FLOAT fl;
EXTENDED_FLOAT div_fl;

    /* Get the operands */
    get_ef (&fl, regs->fpr + r1);
    get_ef (&div_fl, regs->fpr + r2);

    /* divide extended */
    div_ef (&fl, &div_fl, regs);

    /* Back to register */
    store_ef (&fl, regs->fpr + r1);
}


/* end of float.c */
