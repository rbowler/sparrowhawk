/* FLOAT.C      (c) Copyright Peter Kuschnerus, 2000                 */
/*              ESA/390 Hex Floatingpoint Instructions               */

/*-------------------------------------------------------------------*/
/* This module implements the ESA/390 Hex Floatingpoint Instructions */
/* described in the manual ESA/390 Principles of Operation.          */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Incorporated all floating point instructions from cpu.c in order  */
/* to implement revised instruction decoding.                        */
/*                                               Jan Jaeger 01/07/00 */
/*-------------------------------------------------------------------*/

#include "hercules.h"
#include "opcode.h"

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
/* Static inline functions                                           */
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
static inline void add_U128 ( U64 *msa, U64 *lsa, U64 msb, U64 lsb )
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
static inline void sub_U128 ( U64 *msa, U64 *lsa, U64 msb, U64 lsb )
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
static inline void sub_reverse_U128 ( U64 *msa, U64 *lsa, U64 msb, U64 lsb )
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
static inline void get_sf ( SHORT_FLOAT *fl, U32 *fpr )
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
static inline void store_sf ( SHORT_FLOAT *fl, U32 *fpr )
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
static inline void get_lf ( LONG_FLOAT *fl, U32 *fpr )
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
static inline void store_lf ( LONG_FLOAT *fl, U32 *fpr )
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
static inline void get_ef ( EXTENDED_FLOAT *fl, U32 *fpr )
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
static inline void store_ef ( EXTENDED_FLOAT *fl, U32 *fpr )
{
    fpr[0] = ((U32)fl->sign << 31) 
	   | ((U32)fl->expo << 24) 
	   | (fl->ms_fract >> 24);
    fpr[1] = (fl->ms_fract << 8) 
	   | (fl->ls_fract >> 56);
    fpr[2] = ((U32)fl->sign << 31) 
	   | ((fl->ls_fract >> 32) & 0x00FFFFFF);
    fpr[3] = fl->ls_fract;

    if ( fpr[0] || fpr[1] || fpr[2] || fpr[3] ) {
	fpr[2] |= ((((U32)fl->expo - 14) << 24) & 0x7f000000);
    }

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
static inline void vfetch_sf ( SHORT_FLOAT *fl, U32 addr, int arn, REGS *regs )
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
static inline void vfetch_lf ( LONG_FLOAT *fl, U32 addr, int arn, REGS *regs )
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
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*-------------------------------------------------------------------*/
static inline void normal_sf ( SHORT_FLOAT *fl )
{
    if (fl->short_fract) {
	while ((fl->short_fract & 0x00F00000) == 0) {
	    fl->short_fract <<= 4;
	    (fl->expo)--;
	}
    } else {
	fl->sign = POS;
	fl->expo = 0;
    }

} /* end function normal_sf */


/*-------------------------------------------------------------------*/
/* Normalize long float                                              */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*-------------------------------------------------------------------*/
static inline void normal_lf ( LONG_FLOAT *fl )
{
    if (fl->long_fract) {
	while ((fl->long_fract & 0x00F0000000000000ULL) == 0) {
	    fl->long_fract <<= 4;
	    (fl->expo)--;
	}
    } else {
	fl->sign = POS;
	fl->expo = 0;
    }

} /* end function normal_lf */


/*-------------------------------------------------------------------*/
/* Normalize extended float                                          */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*-------------------------------------------------------------------*/
static inline void normal_ef ( EXTENDED_FLOAT *fl )
{
    if (fl->ms_fract || fl->ls_fract) {
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
    } else {
	fl->sign = POS;
	fl->expo = 0;
    }

} /* end function normal_ef */


/*-------------------------------------------------------------------*/
/* Overflow of short float                                           */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static inline int overflow_sf ( SHORT_FLOAT *fl, REGS *regs )
{
    if (fl->expo > 127) {
	fl->expo &= 0x007F;
	return (PGM_EXPONENT_OVERFLOW_EXCEPTION);
    }
    return (0);
    
} /* end function overflow_sf */


/*-------------------------------------------------------------------*/
/* Overflow of long float                                            */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static inline int overflow_lf ( LONG_FLOAT *fl, REGS *regs )
{
    if (fl->expo > 127) {
	fl->expo &= 0x007F;
	return (PGM_EXPONENT_OVERFLOW_EXCEPTION);
    }
    return (0);
    
} /* end function overflow_lf */


/*-------------------------------------------------------------------*/
/* Overflow of extended float                                        */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static inline int overflow_ef ( EXTENDED_FLOAT *fl, REGS *regs )
{
    if (fl->expo > 127) {
	fl->expo &= 0x007F;
	return (PGM_EXPONENT_OVERFLOW_EXCEPTION);
    }
    return (0);
    
} /* end function overflow_ef */


/*-------------------------------------------------------------------*/
/* Underflow of short float                                          */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static inline int underflow_sf ( SHORT_FLOAT *fl, REGS *regs )
{
    if (fl->expo < 0) {
	if (regs->psw.eumask) {
	    fl->expo &= 0x007F;
	    return (PGM_EXPONENT_UNDERFLOW_EXCEPTION);
	} else {
	    /* set true 0 */

	    fl->short_fract = 0;
	    fl->sign = POS;
	    fl->expo = 0;
	}
    }
    return (0);

} /* end function underflow_sf */


/*-------------------------------------------------------------------*/
/* Underflow of long float                                           */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static inline int underflow_lf ( LONG_FLOAT *fl, REGS *regs )
{
    if (fl->expo < 0) {
	if (regs->psw.eumask) {
	    fl->expo &= 0x007F;
	    return (PGM_EXPONENT_UNDERFLOW_EXCEPTION);
	} else {
	    /* set true 0 */

	    fl->long_fract = 0;
	    fl->sign = POS;
	    fl->expo = 0;
	}
    }
    return (0);

} /* end function underflow_lf */


/*-------------------------------------------------------------------*/
/* Underflow of extended float                                       */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static inline int underflow_ef ( EXTENDED_FLOAT *fl, REGS *regs )
{
    if (fl->expo < 0) {
	if (regs->psw.eumask) {
	    fl->expo &= 0x007F;
	    return (PGM_EXPONENT_UNDERFLOW_EXCEPTION);
	} else {
	    /* set true 0 */

	    fl->ms_fract = 0;
	    fl->ls_fract = 0;
	    fl->sign = POS;
	    fl->expo = 0;
	}
    }
    return (0);

} /* end function underflow_ef */


/*-------------------------------------------------------------------*/
/* Overflow and underflow of short float                             */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static inline int over_under_flow_sf ( SHORT_FLOAT *fl, REGS *regs )
{
    if (fl->expo > 127) {
	fl->expo &= 0x007F;
	return (PGM_EXPONENT_OVERFLOW_EXCEPTION);
    } else {
	if (fl->expo < 0) {
	    if (regs->psw.eumask) {
		fl->expo &= 0x007F;
		return (PGM_EXPONENT_UNDERFLOW_EXCEPTION);
	    } else {
		/* set true 0 */

		fl->short_fract = 0;
		fl->sign = POS;
		fl->expo = 0;
	    }
	}
    }
    return (0);

} /* end function over_under_flow_sf */
    

/*-------------------------------------------------------------------*/
/* Overflow and underflow of long float                              */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static inline int over_under_flow_lf ( LONG_FLOAT *fl, REGS *regs )
{
    if (fl->expo > 127) {
	fl->expo &= 0x007F;
	return (PGM_EXPONENT_OVERFLOW_EXCEPTION);
    } else {
	if (fl->expo < 0) {
	    if (regs->psw.eumask) {
		fl->expo &= 0x007F;
		return (PGM_EXPONENT_UNDERFLOW_EXCEPTION);
	    } else {
		/* set true 0 */

		fl->long_fract = 0;
		fl->sign = POS;
		fl->expo = 0;
	    }
	}
    }
    return (0);
    
} /* end function over_under_flow_lf */


/*-------------------------------------------------------------------*/
/* Overflow and underflow of extended float                          */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static inline int over_under_flow_ef ( EXTENDED_FLOAT *fl, REGS *regs )
{
    if (fl->expo > 127) {
	fl->expo &= 0x007F;
	return (PGM_EXPONENT_OVERFLOW_EXCEPTION);
    } else {
	if (fl->expo < 0) {
	    if (regs->psw.eumask) {
		fl->expo &= 0x007F;
		return (PGM_EXPONENT_UNDERFLOW_EXCEPTION);
	    } else {
		/* set true 0 */

		fl->ms_fract = 0;
		fl->ls_fract = 0;
		fl->sign = POS;
		fl->expo = 0;
	    }
	}
    }
    return (0);
    
} /* end function over_under_flow_ef */


/*-------------------------------------------------------------------*/
/* Significance of short float                                       */
/* The fraction is expected to be zero                               */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static inline int significance_sf ( SHORT_FLOAT *fl, REGS *regs )
{
    fl->sign = POS;
    if (regs->psw.sgmask) {
	return (PGM_SIGNIFICANCE_EXCEPTION);
    }
    /* set true 0 */

    fl->expo = 0;
    return (0);

} /* end function significance_sf */


/*-------------------------------------------------------------------*/
/* Significance of long float                                        */
/* The fraction is expected to be zero                               */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static inline int significance_lf ( LONG_FLOAT *fl, REGS *regs )
{
    fl->sign = POS;
    if (regs->psw.sgmask) {
	return (PGM_SIGNIFICANCE_EXCEPTION);
    }
    /* set true 0 */

    fl->expo = 0;
    return (0);

} /* end function significance_lf */


/*-------------------------------------------------------------------*/
/* Significance of extended float                                    */
/* The fraction is expected to be zero                               */
/*                                                                   */
/* Input:                                                            */
/*      fl	Internal float					     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static inline int significance_ef ( EXTENDED_FLOAT *fl, REGS *regs )
{
    fl->sign = POS;
    if (regs->psw.sgmask) {
	return (PGM_SIGNIFICANCE_EXCEPTION);
    }
    /* set true 0 */

    fl->expo = 0;
    return (0);

} /* end function significance_ef */


/*-------------------------------------------------------------------*/
/* Static functions                                                  */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Add short float                                                   */
/*                                                                   */
/* Input:                                                            */
/*      fl	Float						     */
/*      add_fl	Float to be added				     */
/*	normal	Normalize if true				     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static int add_sf ( SHORT_FLOAT *fl, SHORT_FLOAT *add_fl, BYTE normal, 
		REGS *regs )
{
int	pgm_check;
BYTE	shift;

    pgm_check = 0;
    if (add_fl->short_fract || add_fl->expo) {	/* add_fl not 0 */
	if (fl->short_fract || fl->expo) {	/* fl not 0 */
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

			    if (fl->short_fract == 0) {
	 			pgm_check = significance_sf (fl, regs);
			    } else {
				if (normal == NORMAL) {
				    normal_sf (fl);
				    pgm_check = underflow_sf (fl, regs);
				}
			    }
			    return (pgm_check);
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

			    if (fl->short_fract == 0) {
	 			pgm_check = significance_sf (fl, regs);
			    } else {
				if (normal == NORMAL) {
				    normal_sf (fl);
				    pgm_check = underflow_sf (fl, regs);
				}
			    }
			    return (pgm_check);
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
		    return ( significance_sf (fl, regs) );

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
		pgm_check = overflow_sf (fl, regs);
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
			    pgm_check = underflow_sf (fl, regs);
		        }
		    } else {
		        /* true 0 */

		 	pgm_check = significance_sf (fl, regs);
		    }
	        } else {
		    /* not normalize, just guard digit */
		    fl->short_fract >>= 4;
	    	    if (fl->short_fract == 0) {
		 	pgm_check = significance_sf (fl, regs);
		    }
	        }
	    }
	    return (pgm_check);
	} else { /* fl 0, add_fl not 0 */
	    /* copy summand */

	    fl->expo = add_fl->expo;
	    fl->sign = add_fl->sign;
	    fl->short_fract = add_fl->short_fract;
	}
    } else { 			    /* add_fl 0 */
	if (fl->short_fract == 0) { /* fl 0 */
	    /* both 0 */

	    return ( significance_sf (fl, regs) );
	}
    }
    if (normal == NORMAL) {
	normal_sf (fl);
	pgm_check = underflow_sf (fl, regs);
    }
    return (pgm_check);

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
static int add_lf ( LONG_FLOAT *fl, LONG_FLOAT *add_fl, BYTE normal, 
		REGS *regs )
{
int	pgm_check;
BYTE	shift;

    pgm_check = 0;
    if (add_fl->long_fract || add_fl->expo) {	/* add_fl not 0 */
	if (fl->long_fract || fl->expo) {	/* fl not 0 */
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

			    if (fl->long_fract == 0) {
				pgm_check = significance_lf (fl, regs);
			    } else {
				if (normal == NORMAL) {
				    normal_lf (fl);
				    pgm_check = underflow_lf (fl, regs);
				}
			    }
			    return (pgm_check);
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

			    if (fl->long_fract == 0) {
				pgm_check = significance_lf (fl, regs);
			    } else {
				if (normal == NORMAL) {
				    normal_lf (fl);
				    pgm_check = underflow_lf (fl, regs);
				}
			    }
			    return (pgm_check);
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
		    return ( significance_lf (fl, regs) );

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
		pgm_check = overflow_lf (fl, regs);
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
			    pgm_check = underflow_lf (fl, regs);
                        }
                    } else {
                        /* true 0 */

                        pgm_check = significance_lf (fl, regs);
                    }
                } else {
                    /* not normalize, just guard digit */
                    fl->long_fract >>= 4;
                    if (fl->long_fract == 0) {
                        pgm_check = significance_lf (fl, regs);
                    }
                }
            }
	    return (pgm_check);
	} else { /* fl 0, add_fl not 0 */
	    /* copy summand */

	    fl->expo = add_fl->expo;
	    fl->sign = add_fl->sign;
	    fl->long_fract = add_fl->long_fract;
	}
    } else {                       /* add_fl 0 */
        if (fl->long_fract == 0) { /* fl 0 */
            /* both 0 */

            return ( significance_lf (fl, regs) );
        }
    }
    if (normal == NORMAL) {
	normal_lf (fl);
	pgm_check = underflow_lf (fl, regs);
    }
    return (pgm_check);
	
} /* end function add_lf */


/*-------------------------------------------------------------------*/
/* Add extended float normalized                                     */
/*                                                                   */
/* Input:                                                            */
/*      fl	Float						     */
/*      add_fl	Float to be added				     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static int add_ef ( EXTENDED_FLOAT *fl, EXTENDED_FLOAT *add_fl, REGS *regs )
{
int	pgm_check;
BYTE	shift;

    pgm_check = 0;
    if (add_fl->ms_fract || add_fl->ls_fract || add_fl->expo) {	/* add_fl not 0 */
	if (fl->ms_fract || fl->ls_fract || fl->expo) 	{	/* fl not 0 */
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

			    if ((fl->ms_fract == 0) && (fl->ls_fract == 0)) {	
				pgm_check = significance_ef (fl, regs);
			    } else {
				normal_ef (fl);
				pgm_check =  underflow_ef (fl, regs);
			    }
			    return (pgm_check);
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

			    if ((fl->ms_fract == 0) && (fl->ls_fract == 0)) {	
				pgm_check = significance_ef (fl, regs);
			    } else {
				normal_ef (fl);
				pgm_check = underflow_ef (fl, regs);
			    }
			    return (pgm_check);
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

			    if ((fl->ms_fract == 0) && (fl->ls_fract == 0)) {	
				pgm_check = significance_ef (fl, regs);
			    } else {
				normal_ef (fl);
				pgm_check = underflow_ef (fl, regs);
			    }
			    return (pgm_check);
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

			    if ((fl->ms_fract == 0) && (fl->ls_fract == 0)) {	
				pgm_check = significance_ef (fl, regs);
			    } else {
				normal_ef (fl);
				pgm_check = underflow_ef (fl, regs);
			    }
			    return (pgm_check);
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
		    return ( significance_ef (fl, regs) );

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
		pgm_check = overflow_ef (fl, regs);
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
			pgm_check = underflow_ef (fl, regs);
		    }
		} else {
		    /* true 0 */

		    pgm_check = significance_ef (fl, regs);
		}
	    }
	    return (pgm_check);
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

	    return ( significance_ef (fl, regs) );
	}
    }
    normal_ef (fl);
    return ( underflow_ef (fl, regs) );
  
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
BYTE	shift;

    if (cmp_fl->short_fract || cmp_fl->expo) {	/* cmp_fl not 0 */
	if (fl->short_fract || fl->expo) {	/* fl not 0 */
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
BYTE	shift;

    if (cmp_fl->long_fract || cmp_fl->expo) {	/* cmp_fl not 0 */
	if (fl->long_fract || fl->expo) {	/* fl not 0 */
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
static int mul_sf_to_lf ( SHORT_FLOAT *fl, SHORT_FLOAT *mul_fl, 
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

	/* determine sign */
	result_fl->sign = (fl->sign == mul_fl->sign) ? POS : NEG;

	/* handle overflow and underflow */
	return ( over_under_flow_lf (result_fl, regs) );
    } else {
    	/* set true 0 */

    	result_fl->sign = POS;
    	result_fl->expo = 0;
    	result_fl->long_fract = 0;
	return (0);
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
static int mul_lf_to_ef ( LONG_FLOAT *fl, LONG_FLOAT *mul_fl, 
		EXTENDED_FLOAT *result_fl, REGS *regs )
{
U64	wk;

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

	/* determine sign */
	result_fl->sign = (fl->sign == mul_fl->sign) ? POS : NEG;

	/* handle overflow and underflow */
	return ( over_under_flow_ef (result_fl, regs) );
    } else {
    	/* set true 0 */

    	result_fl->sign = POS;
    	result_fl->expo = 0;
    	result_fl->ms_fract = 0;
    	result_fl->ls_fract = 0;
	return (0);
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
static int mul_lf ( LONG_FLOAT *fl, LONG_FLOAT *mul_fl, REGS *regs )
{
U64	wk;
U32	v;

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

	/* determine sign */
	fl->sign = (fl->sign == mul_fl->sign) ? POS : NEG;

	/* handle overflow and underflow */
	return ( over_under_flow_lf (fl, regs) );
    } else {
    	/* set true 0 */

    	fl->sign = POS;
    	fl->expo = 0;
    	fl->long_fract = 0;
	return (0);
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
static int mul_ef ( EXTENDED_FLOAT *fl, EXTENDED_FLOAT *mul_fl, REGS *regs )
{
U64	wk;
U32	v;

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

	/* determine sign */
	fl->sign = (fl->sign == mul_fl->sign) ? POS : NEG;

	/* handle overflow and underflow */
	return ( over_under_flow_ef (fl, regs) );
    } else {
    	/* set true 0 */

    	fl->sign = POS;
    	fl->expo = 0;
    	fl->ms_fract = 0;
    	fl->ls_fract = 0;
	return (0);
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
static int div_sf ( SHORT_FLOAT *fl, SHORT_FLOAT *div_fl, REGS *regs )
{
U64	wk;

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

	    /* determine sign */
	    fl->sign = (fl->sign == div_fl->sign) ? POS : NEG;
		
	    /* handle overflow and underflow */
	    return ( over_under_flow_sf (fl, regs) );
	} else {
    	    /* fraction of dividend 0, set true 0 */

    	    fl->sign = POS;
    	    fl->expo = 0;
    	    fl->short_fract = 0;
	}
    } else {
	/* divisor 0 */

        program_check (regs, PGM_FLOATING_POINT_DIVIDE_EXCEPTION);
    }
    return (0);

} /* end function div_sf */


/*-------------------------------------------------------------------*/
/* Divide long float			                             */
/*                                                                   */
/* Input:                                                            */
/*      fl	Dividend long float				     */
/*      mul_fl	Divisor long float				     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static int div_lf ( LONG_FLOAT *fl, LONG_FLOAT *div_fl, REGS *regs )
{
U64	wk;
U64	wk2;
int	i;

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

	    /* determine sign */
	    fl->sign = (fl->sign == div_fl->sign) ? POS : NEG;

	    /* handle overflow and underflow */
	    return ( over_under_flow_lf (fl, regs) );
	} else {
    	    /* fraction of dividend 0, set true 0 */

    	    fl->sign = POS;
    	    fl->expo = 0;
    	    fl->long_fract = 0;
	}
    } else {
	/* divisor 0 */

        program_check (regs, PGM_FLOATING_POINT_DIVIDE_EXCEPTION);
    }
    return (0);

} /* end function div_lf */


/*-------------------------------------------------------------------*/
/* Divide extended float		                             */
/*                                                                   */
/* Input:                                                            */
/*      fl	Dividend extended float				     */
/*      mul_fl	Divisor extended float				     */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static int div_ef ( EXTENDED_FLOAT *fl, EXTENDED_FLOAT *div_fl, REGS *regs )
{
U64	wkm;
U64	wkl;
int	i;

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

	    /* determine sign */
	    fl->sign = (fl->sign == div_fl->sign) ? POS : NEG;

	    /* handle overflow and underflow */
	    return ( over_under_flow_ef (fl, regs) );
	} else {
    	    /* fraction of dividend 0, set true 0 */

    	    fl->sign = POS;
    	    fl->expo = 0;
    	    fl->ms_fract = 0;
    	    fl->ls_fract = 0;
	}
    } else {
	/* divisor 0 */

        program_check (regs, PGM_FLOATING_POINT_DIVIDE_EXCEPTION);
    }
    return (0);

} /* end function div_ef */


/*-------------------------------------------------------------------*/
/* Extern functions                                                  */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* 20   LPDR  - Load Positive Floating Point Long Register      [RR] */
/*-------------------------------------------------------------------*/
void zz_load_positive_float_long_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Copy register contents, clear the sign bit */
    regs->fpr[r1] = regs->fpr[r2] & 0x7FFFFFFF;
    regs->fpr[r1+1] = regs->fpr[r2+1];

    /* Set condition code */
    regs->psw.cc =
        ((regs->fpr[r1] & 0x00FFFFFF) || regs->fpr[r1+1]) ? 2 : 0;

}


/*-------------------------------------------------------------------*/
/* 21   LNDR  - Load Negative Floating Point Long Register      [RR] */
/*-------------------------------------------------------------------*/
void zz_load_negative_float_long_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Copy register contents, set the sign bit */
    regs->fpr[r1] = regs->fpr[r2] | 0x80000000;
    regs->fpr[r1+1] = regs->fpr[r2+1];

    /* Set condition code */
    regs->psw.cc =
        ((regs->fpr[r1] & 0x00FFFFFF) || regs->fpr[r1+1]) ? 1 : 0;

}


/*-------------------------------------------------------------------*/
/* 22   LTDR  - Load and Test Floating Point Long Register      [RR] */
/*-------------------------------------------------------------------*/
void zz_load_and_test_float_long_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Copy register contents */
    regs->fpr[r1] = regs->fpr[r2];
    regs->fpr[r1+1] = regs->fpr[r2+1];

    /* Set condition code */
    if ((regs->fpr[r1] & 0x00FFFFFF) || regs->fpr[r1+1]) {
        regs->psw.cc = (regs->fpr[r1] & 0x80000000) ? 1 : 2;
    } else
        regs->psw.cc = 0;

}


/*-------------------------------------------------------------------*/
/* 23   LCDR  - Load Complement Floating Point Long Register    [RR] */
/*-------------------------------------------------------------------*/
void zz_load_complement_float_long_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Copy register contents, invert sign bit */
    regs->fpr[r1] = regs->fpr[r2] ^ 0x80000000;
    regs->fpr[r1+1] = regs->fpr[r2+1];

    /* Set condition code */
    if ((regs->fpr[r1] & 0x00FFFFFF) || regs->fpr[r1+1]) {
        regs->psw.cc = (regs->fpr[r1] & 0x80000000) ? 1 : 2;
    } else
        regs->psw.cc = 0;

}


/*-------------------------------------------------------------------*/
/* 24   HDR   - Halve Floating Point Long Register              [RR] */
/*-------------------------------------------------------------------*/
void zz_halve_float_long_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
LONG_FLOAT fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get register content */
    get_lf (&fl, regs->fpr + r2);

    /* Halve the value */
    if (fl.long_fract & 0x00E0000000000000ULL) {
	fl.long_fract >>= 1;
	pgm_check = 0;
    } else {
	fl.long_fract <<= 3;
	(fl.expo)--;
	normal_lf (&fl);
	pgm_check = underflow_lf (&fl, regs);
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 25   LRDR  - Load Rounded Floating Point Long Register       [RR] */
/*-------------------------------------------------------------------*/
void zz_round_float_long_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
LONG_FLOAT fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);

    /* Program check if R1 is not 0, 2, 4, or 6 */
    /* or if R2 is not 0 or 4 */
    if (( r1 & 9) || (r2 & 11))
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Get register content */
    get_lf (&fl, regs->fpr + r2);

    /* Rounding */
    fl.long_fract += (regs->fpr[r2 + 2] >> 23) & 1;

    /* Handle overflow */
    if (fl.long_fract & 0x0F00000000000000ULL) {
	fl.long_fract >>= 4;
	(fl.expo)++;
	pgm_check = overflow_lf (&fl, regs);
    } else {
	pgm_check = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 26   MXR   - Multiply Floating Point Extended Register       [RR] */
/*-------------------------------------------------------------------*/
void zz_multiply_float_ext_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
EXTENDED_FLOAT fl;
EXTENDED_FLOAT mul_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPODD2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_ef (&fl, regs->fpr + r1);
    get_ef (&mul_fl, regs->fpr + r2);

    /* multiply extended */
    pgm_check = mul_ef (&fl, &mul_fl, regs);

    /* Back to register */
    store_ef (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 27   MXDR  - Multiply Floating Point Long to Extended Reg.   [RR] */
/*-------------------------------------------------------------------*/
void zz_multiply_float_long_to_ext_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
LONG_FLOAT fl;
LONG_FLOAT mul_fl;
EXTENDED_FLOAT result_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);

    /* Program check if R1 is not 0 or 4 */
    /* or if R2 is not 0, 2, 4, or 6 */
    if (( r1 & 11) || (r2 & 9))
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&mul_fl, regs->fpr + r2);

    /* multiply long to extended */
    pgm_check = mul_lf_to_ef (&fl, &mul_fl, &result_fl, regs);

    /* Back to register */
    store_ef (&result_fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 28   LDR   - Load Floating Point Long Register               [RR] */
/*-------------------------------------------------------------------*/
void zz_load_float_long_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Copy register contents */
    regs->fpr[r1] = regs->fpr[r2];
    regs->fpr[r1+1] = regs->fpr[r2+1];

}


/*-------------------------------------------------------------------*/
/* 29   CDR   - Compare Floating Point Long Register            [RR] */
/*-------------------------------------------------------------------*/
void zz_compare_float_long_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
LONG_FLOAT fl;
LONG_FLOAT cmp_fl;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&cmp_fl, regs->fpr + r2);

    /* Compare long */
    cmp_lf (&fl, &cmp_fl, regs);

}


/*-------------------------------------------------------------------*/
/* 2A   ADR   - Add Floating Point Long Register                [RR] */
/*-------------------------------------------------------------------*/
void zz_add_float_long_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
LONG_FLOAT fl;
LONG_FLOAT add_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&add_fl, regs->fpr + r2);

    /* Add long with normalization */
    pgm_check = add_lf (&fl, &add_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 2B   SDR   - Subtract Floating Point Long Register           [RR] */
/*-------------------------------------------------------------------*/
void zz_subtract_float_long_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
LONG_FLOAT fl;
LONG_FLOAT sub_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&sub_fl, regs->fpr + r2);

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add long with normalization */
    pgm_check = add_lf (&fl, &sub_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 2C   MDR   - Multiply Floating Point Long Register           [RR] */
/*-------------------------------------------------------------------*/
void zz_multiply_float_long_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
LONG_FLOAT fl;
LONG_FLOAT mul_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&mul_fl, regs->fpr + r2);

    /* multiply long */
    pgm_check = mul_lf (&fl, &mul_fl, regs);

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 2D   DDR   - Divide Floating Point Long Register             [RR] */
/*-------------------------------------------------------------------*/
void zz_divide_float_long_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
LONG_FLOAT fl;
LONG_FLOAT div_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&div_fl, regs->fpr + r2);

    /* divide long */
    pgm_check = div_lf (&fl, &div_fl, regs);

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 2E   AWR   - Add Unnormalized Floating Point Long Register   [RR] */
/*-------------------------------------------------------------------*/
void zz_add_unnormal_float_long_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
LONG_FLOAT fl;
LONG_FLOAT add_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&add_fl, regs->fpr + r2);

    /* Add long without normalization */
    pgm_check = add_lf (&fl, &add_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 2F   SWR   - Subtract Unnormalized Floating Point Long Reg.  [RR] */
/*-------------------------------------------------------------------*/
void zz_subtract_unnormal_float_long_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
LONG_FLOAT fl;
LONG_FLOAT sub_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&sub_fl, regs->fpr + r2);

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add long without normalization */
    pgm_check = add_lf (&fl, &sub_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 30   LPER  - Load Positive Floating Point Short Register     [RR] */
/*-------------------------------------------------------------------*/
void zz_load_positive_float_short_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Copy register contents, clear sign bit */
    regs->fpr[r1] = regs->fpr[r2] & 0x7FFFFFFF;

    /* Set condition code */
    regs->psw.cc = (regs->fpr[r1] & 0x00FFFFFF) ? 2 : 0;

}


/*-------------------------------------------------------------------*/
/* 31   LNER  - Load Negative Floating Point Short Register     [RR] */
/*-------------------------------------------------------------------*/
void zz_load_negative_float_short_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Copy register contents, set sign bit */
    regs->fpr[r1] = regs->fpr[r2] | 0x80000000;

    /* Set condition code */
    regs->psw.cc = (regs->fpr[r1] & 0x00FFFFFF) ? 1 : 0;

}


/*-------------------------------------------------------------------*/
/* 32   LTER  - Load and Test Floating Point Short Register     [RR] */
/*-------------------------------------------------------------------*/
void zz_load_and_test_float_short_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Copy register contents */
    regs->fpr[r1] = regs->fpr[r2];

    /* Set condition code */
    if (regs->fpr[r1] & 0x00FFFFFF) {
        regs->psw.cc = (regs->fpr[r1] & 0x80000000) ? 1 : 2;
    } else
        regs->psw.cc = 0;

}


/*-------------------------------------------------------------------*/
/* 33   LCER  - Load Complement Floating Point Short Register   [RR] */
/*-------------------------------------------------------------------*/
void zz_load_complement_float_short_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Copy register contents, invert sign bit */
    regs->fpr[r1] = regs->fpr[r2] ^ 0x80000000;

    /* Set condition code */
    if (regs->fpr[r1] & 0x00FFFFFF) {
        regs->psw.cc = (regs->fpr[r1] & 0x80000000) ? 1 : 2;
    } else
        regs->psw.cc = 0;

}


/*-------------------------------------------------------------------*/
/* 34   HER   - Halve Floating Point Short Register             [RR] */
/*-------------------------------------------------------------------*/
void zz_halve_float_short_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
SHORT_FLOAT fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get register content */
    get_sf (&fl, regs->fpr + r2);

    /* Halve the value */
    if (fl.short_fract & 0x00E00000) {
	fl.short_fract >>= 1;
	pgm_check = 0;
    } else {
	fl.short_fract <<= 3;
	(fl.expo)--;
	normal_sf (&fl);
	pgm_check = underflow_sf (&fl, regs);
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 35   LRER  - Load Rounded Floating Point Short Register      [RR] */
/*-------------------------------------------------------------------*/
void zz_round_float_short_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
LONG_FLOAT from_fl;
SHORT_FLOAT to_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

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
	pgm_check = overflow_sf (&to_fl, regs);
    } else {
	pgm_check = 0;
    }

    /* To register */
    store_sf (&to_fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 36   AXR   - Add Floating Point Extended Register            [RR] */
/*-------------------------------------------------------------------*/
void zz_add_float_ext_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
EXTENDED_FLOAT fl;
EXTENDED_FLOAT add_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPODD2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_ef (&fl, regs->fpr + r1);
    get_ef (&add_fl, regs->fpr + r2);

    /* Add extended */
    pgm_check = add_ef (&fl, &add_fl, regs);

    /* Set condition code */
    if (fl.ms_fract || fl.ls_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_ef (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 37   SXR   - Subtract Floating Point Extended Register       [RR] */
/*-------------------------------------------------------------------*/
void zz_subtract_float_ext_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
EXTENDED_FLOAT fl;
EXTENDED_FLOAT sub_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPODD2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_ef (&fl, regs->fpr + r1);
    get_ef (&sub_fl, regs->fpr + r2);

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add extended */
    pgm_check = add_ef (&fl, &sub_fl, regs);

    /* Set condition code */
    if (fl.ms_fract || fl.ls_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_ef (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 38   LER   - Load Floating Point Short Register              [RR] */
/*-------------------------------------------------------------------*/
void zz_load_float_short_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Copy register content */
    regs->fpr[r1] = regs->fpr[r2];

}


/*-------------------------------------------------------------------*/
/* 39   CER   - Compare Floating Point Short Register           [RR] */
/*-------------------------------------------------------------------*/
void zz_compare_float_short_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
SHORT_FLOAT fl;
SHORT_FLOAT cmp_fl;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&cmp_fl, regs->fpr + r2);

    /* Compare short */
    cmp_sf (&fl, &cmp_fl, regs);

}


/*-------------------------------------------------------------------*/
/* 3A   AER   - Add Floating Point Short Register               [RR] */
/*-------------------------------------------------------------------*/
void zz_add_float_short_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
SHORT_FLOAT fl;
SHORT_FLOAT add_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&add_fl, regs->fpr + r2);

    /* Add short with normalization */
    pgm_check = add_sf (&fl, &add_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 3B   SER   - Subtract Floating Point Short Register          [RR] */
/*-------------------------------------------------------------------*/
void zz_subtract_float_short_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
SHORT_FLOAT fl;
SHORT_FLOAT sub_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&sub_fl, regs->fpr + r2);

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Subtract short with normalization */
    pgm_check = add_sf (&fl, &sub_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 3C   MER   - Multiply Short to Long Floating Point Register  [RR] */
/*-------------------------------------------------------------------*/
void zz_multiply_float_short_to_long_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
SHORT_FLOAT fl;
SHORT_FLOAT mul_fl;
LONG_FLOAT result_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&mul_fl, regs->fpr + r2);

    /* multiply short to long */
    pgm_check = mul_sf_to_lf (&fl, &mul_fl, &result_fl, regs);

    /* Back to register */
    store_lf (&result_fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 3D   DER   - Divide Floating Point Short Register            [RR] */
/*-------------------------------------------------------------------*/
void zz_divide_float_short_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
SHORT_FLOAT fl;
SHORT_FLOAT div_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&div_fl, regs->fpr + r2);

    /* divide short */
    pgm_check = div_sf (&fl, &div_fl, regs);

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 3E   AUR   - Add Unnormalized Floating Point Short Register  [RR] */
/*-------------------------------------------------------------------*/
void zz_add_unnormal_float_short_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
SHORT_FLOAT fl;
SHORT_FLOAT add_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&add_fl, regs->fpr + r2);

    /* Add short without normalization */
    pgm_check = add_sf (&fl, &add_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 3F   SUR   - Subtract Unnormalized Floating Point Short Reg. [RR] */
/*-------------------------------------------------------------------*/
void zz_subtract_unnormal_float_short_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
SHORT_FLOAT fl;
SHORT_FLOAT sub_fl;
int	pgm_check;

    RR(inst, execflag, regs, r1, r2);
    HFPREG2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&sub_fl, regs->fpr + r2);

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add short without normalization */
    pgm_check = add_sf (&fl, &sub_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 60   STD   - Store Floating Point Long                       [RX] */
/*-------------------------------------------------------------------*/
void zz_store_float_long (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U64     dreg;                           /* Double word workarea      */

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Store register contents at operand address */
    dreg = ((U64)regs->fpr[r1] << 32) | regs->fpr[r1+1];
    vstore8 (dreg, effective_addr2, b2, regs);

}


/*-------------------------------------------------------------------*/
/* 67   MXD   - Multiply Floating Point Long to Extended        [RX] */
/*-------------------------------------------------------------------*/
void zz_multiply_float_long_to_ext (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
LONG_FLOAT fl;
LONG_FLOAT mul_fl;
EXTENDED_FLOAT result_fl;
int	pgm_check;

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPODD_CHECK(r1, regs);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&mul_fl, effective_addr2, b2, regs );

    /* multiply long to extended */
    pgm_check = mul_lf_to_ef (&fl, &mul_fl, &result_fl, regs);

    /* Back to register */
    store_ef (&result_fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 68   LD    - Load Floating Point Long                        [RX] */
/*-------------------------------------------------------------------*/
void zz_load_float_long (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U64     dreg;                           /* Double word workarea      */

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Fetch value from operand address */
    dreg = vfetch8 (effective_addr2, b2, regs);

    /* Update register contents */
    regs->fpr[r1] = dreg >> 32;
    regs->fpr[r1+1] = dreg;

}


/*-------------------------------------------------------------------*/
/* 69   CD    - Compare Floating Point Long                     [RX] */
/*-------------------------------------------------------------------*/
void zz_compare_float_long (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
LONG_FLOAT fl;
LONG_FLOAT cmp_fl;

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&cmp_fl, effective_addr2, b2, regs );

    /* Compare long */
    cmp_lf (&fl, &cmp_fl, regs);

}


/*-------------------------------------------------------------------*/
/* 6A   AD    - Add Floating Point Long                         [RX] */
/*-------------------------------------------------------------------*/
void zz_add_float_long (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
LONG_FLOAT fl;
LONG_FLOAT add_fl;
int	pgm_check;

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&add_fl, effective_addr2, b2, regs );

    /* Add long with normalization */
    pgm_check = add_lf (&fl, &add_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 6B   SD    - Subtract Floating Point Long                    [RX] */
/*-------------------------------------------------------------------*/
void zz_subtract_float_long (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
LONG_FLOAT fl;
LONG_FLOAT sub_fl;
int	pgm_check;

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&sub_fl, effective_addr2, b2, regs );

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add long with normalization */
    pgm_check = add_lf (&fl, &sub_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 6C   MD    - Multiply Floating Point Long                    [RX] */
/*-------------------------------------------------------------------*/
void zz_multiply_float_long (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
LONG_FLOAT fl;
LONG_FLOAT mul_fl;
int	pgm_check;

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&mul_fl, effective_addr2, b2, regs );

    /* multiply long */
    pgm_check = mul_lf (&fl, &mul_fl, regs);

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 6D   DD    - Divide Floating Point Long                      [RX] */
/*-------------------------------------------------------------------*/
void zz_divide_float_long (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
LONG_FLOAT fl;
LONG_FLOAT div_fl;
int	pgm_check;

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&div_fl, effective_addr2, b2, regs );

    /* divide long */
    pgm_check = div_lf (&fl, &div_fl, regs);

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 6E   AW    - Add Unnormalized Floating Point Long            [RX] */
/*-------------------------------------------------------------------*/
void zz_add_unnormal_float_long (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
LONG_FLOAT fl;
LONG_FLOAT add_fl;
int	pgm_check;

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&add_fl, effective_addr2, b2, regs );

    /* Add long without normalization */
    pgm_check = add_lf (&fl, &add_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 6F   SW    - Subtract Unnormalized Floating Point Long       [RX] */
/*-------------------------------------------------------------------*/
void zz_subtract_unnormal_float_long (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
LONG_FLOAT fl;
LONG_FLOAT sub_fl;
int	pgm_check;

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&sub_fl, effective_addr2, b2, regs );

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add long without normalization */
    pgm_check = add_lf (&fl, &sub_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.long_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 70   STE   - Store Floating Point Short                      [RX] */
/*-------------------------------------------------------------------*/
void zz_store_float_short (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Store register contents at operand address */
    vstore4 (regs->fpr[r1], effective_addr2, b2, regs);

}


/*-------------------------------------------------------------------*/
/* 78   LE    - Load Floating Point Short                       [RX] */
/*-------------------------------------------------------------------*/
void zz_load_float_short (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Update first 32 bits of register from operand address */
    regs->fpr[r1] = vfetch4 (effective_addr2, b2, regs);

}


/*-------------------------------------------------------------------*/
/* 79   CE    - Compare Floating Point Short                    [RX] */
/*-------------------------------------------------------------------*/
void zz_compare_float_short (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
SHORT_FLOAT fl;
SHORT_FLOAT cmp_fl;

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&cmp_fl, effective_addr2, b2, regs );

    /* Compare long */
    cmp_sf (&fl, &cmp_fl, regs);

}


/*-------------------------------------------------------------------*/
/* 7A   AE    - Add Floating Point Short                        [RX] */
/*-------------------------------------------------------------------*/
void zz_add_float_short (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
SHORT_FLOAT fl;
SHORT_FLOAT add_fl;
int	pgm_check;

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&add_fl, effective_addr2, b2, regs );

    /* Add short with normalization */
    pgm_check = add_sf (&fl, &add_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 7B   SE    - Subtract Floating Point Short                   [RX] */
/*-------------------------------------------------------------------*/
void zz_subtract_float_short (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
SHORT_FLOAT fl;
SHORT_FLOAT sub_fl;
int	pgm_check;

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&sub_fl, effective_addr2, b2, regs );

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add short with normalization */
    pgm_check = add_sf (&fl, &sub_fl, NORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 7C   ME    - Multiply Floating Point Short to Long           [RX] */
/*-------------------------------------------------------------------*/
void zz_multiply_float_short_to_long (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
SHORT_FLOAT fl;
SHORT_FLOAT mul_fl;
LONG_FLOAT result_fl;
int	pgm_check;

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&mul_fl, effective_addr2, b2, regs );

    /* multiply short to long */
    pgm_check = mul_sf_to_lf (&fl, &mul_fl, &result_fl, regs);

    /* Back to register */
    store_lf (&result_fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 7D   DE    - Divide Floating Point Short                     [RX] */
/*-------------------------------------------------------------------*/
void zz_divide_float_short (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
SHORT_FLOAT fl;
SHORT_FLOAT div_fl;
int	pgm_check;

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&div_fl, effective_addr2, b2, regs );

    /* divide short */
    pgm_check = div_sf (&fl, &div_fl, regs);

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 7E   AU    - Add Unnormalized Floating Point Short           [RX] */
/*-------------------------------------------------------------------*/
void zz_add_unnormal_float_short (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
SHORT_FLOAT fl;
SHORT_FLOAT add_fl;
int	pgm_check;

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&add_fl, effective_addr2, b2, regs );

    /* Add short without normalization */
    pgm_check = add_sf (&fl, &add_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* 7F   SU    - Subtract Unnormalized Floating Point Short      [RX] */
/*-------------------------------------------------------------------*/
void zz_subtract_unnormal_float_short (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
SHORT_FLOAT fl;
SHORT_FLOAT sub_fl;
int	pgm_check;

    RX(inst, execflag, regs, r1, b2, effective_addr2);
    HFPREG_CHECK(r1, regs);

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&sub_fl, effective_addr2, b2, regs );

    /* Invert the sign of 2nd operand */
    sub_fl.sign = ! (sub_fl.sign);

    /* Add short without normalization */
    pgm_check = add_sf (&fl, &sub_fl, UNNORMAL, regs);

    /* Set condition code */
    if (fl.short_fract) {
	regs->psw.cc = fl.sign ? 1 : 2;
    } else {
	regs->psw.cc = 0;
    }

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}


/*-------------------------------------------------------------------*/
/* B22D DXR   - Divide Float Extended Register                 [RRE] */
/*-------------------------------------------------------------------*/
void zz_divide_float_ext_reg (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
EXTENDED_FLOAT fl;
EXTENDED_FLOAT div_fl;
int	pgm_check;

    RRE(inst, execflag, regs, r1, r2);
    HFPODD2_CHECK(r1, r2, regs);

    /* Get the operands */
    get_ef (&fl, regs->fpr + r1);
    get_ef (&div_fl, regs->fpr + r2);

    /* divide extended */
    pgm_check = div_ef (&fl, &div_fl, regs);

    /* Back to register */
    store_ef (&fl, regs->fpr + r1);

    /* Program check ? */
    if (pgm_check)
        program_check (regs, pgm_check);

}

/* end of float.c */
