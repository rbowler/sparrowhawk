/* FLOAT.C      (c) Copyright Peter Kuschnerus, 2000                 */
/*              ESA/390 Floatingpoint Instructions                   */

/*-------------------------------------------------------------------*/
/* This module implements the ESA/390 Floatingpoint Instructions     */
/* described in the manual                                           */
/* SA22-7201-04 ESA/390 Principles of Operation.                     */
/*-------------------------------------------------------------------*/

#include "hercules.h"


/*-------------------------------------------------------------------*/
/* Structure definition for internal short floatingpoint format      */
/*-------------------------------------------------------------------*/
typedef struct _SHORT_FLOAT {
        S32     short_fract;            /* Fraction                  */
        char    expo;                   /* Exponent + 64             */
        BYTE    sign;                   /* Sign                      */
} SHORT_FLOAT;

/*-------------------------------------------------------------------*/
/* Structure definition for internal long floatingpoint format       */
/*-------------------------------------------------------------------*/
typedef struct _LONG_FLOAT {
        S64     long_fract;             /* Fraction                  */
        char    expo;                   /* Exponent + 64             */
        BYTE    sign;                   /* Sign                      */
} LONG_FLOAT;

/*-------------------------------------------------------------------*/
/* Structure definition for internal extended floatingpoint format   */
/*-------------------------------------------------------------------*/
typedef struct _EXTENDED_FLOAT {
        S64     ms_fract;               /* Most significant fraction */
        S64     ls_fract;               /* Least significant fraction*/
        char    expo;                   /* Exponent + 64             */
        BYTE    sign;                   /* Sign                      */
} EXTENDED_FLOAT;

#define POS     0                       /* Positive value of sign    */
#define NEG     1                       /* Negative value of sign    */


/*-------------------------------------------------------------------*/
/* Add signed 128 bit integer                                        */
/*                                                                   */
/* Input:                                                            */
/*      msa     most significant 64 bit of operand a                 */
/*      lsa     least significant 64 bit of operand a                */
/*      msb     most significant 64 bit of operand b                 */
/*      lsb     least significant 64 bit of operand b                */
/*-------------------------------------------------------------------*/
static void add_S128 ( S64 *msa, S64 *lsa, S64 msb, S64 lsb )
{
S64 a[4];
S64 b[4];
int c;
int i;

    /* split to 32 bit parts */
    a[0] = *msa >> 32;
    a[1] = *msa & 0x00000000FFFFFFFF;
    a[2] = *lsa >> 32;
    a[3] = *lsa & 0x00000000FFFFFFFF;
    b[0] = msb >> 32;
    b[1] = msb & 0x00000000FFFFFFFF;
    b[2] = lsb >> 32;
    b[3] = lsb & 0x00000000FFFFFFFF;

    i = 3;
    c = 0;
    while (i >= 0) {
        a[i] = a[i] + b[i] + c;
        c = a[i] >> 32;
        a[i] &= 0x00000000FFFFFFFF;
        i--;
    }

    /* back to operand a */
    *msa = (a[0] << 32) | a[1];
    *lsa = (a[2] << 32) | a[3];

} /* end function add_S128 */


/*-------------------------------------------------------------------*/
/* Subtract signed 128 bit integer                                   */
/*                                                                   */
/* Input:                                                            */
/*      msa     most significant 64 bit of operand a                 */
/*      lsa     least significant 64 bit of operand a                */
/*      msb     most significant 64 bit of operand b                 */
/*      lsb     least significant 64 bit of operand b                */
/*-------------------------------------------------------------------*/
static void sub_S128 ( S64 *msa, S64 *lsa, S64 msb, S64 lsb )
{
S64 a[4];
S64 b[4];
int c;
int i;

    /* split to 32 bit parts */
    a[0] = *msa >> 32;
    a[1] = *msa & 0x00000000FFFFFFFF;
    a[2] = *lsa >> 32;
    a[3] = *lsa & 0x00000000FFFFFFFF;
    b[0] = msb >> 32;
    b[1] = msb & 0x00000000FFFFFFFF;
    b[2] = lsb >> 32;
    b[3] = lsb & 0x00000000FFFFFFFF;

    i = 3;
    c = 0;
    while (i >= 0) {
        a[i] = a[i] - b[i] + c;
        c = a[i] >> 32;
        a[i] &= 0x00000000FFFFFFFF;
        i--;
    }

    /* back to operand a */
    *msa = (a[0] << 32) | a[1];
    *lsa = (a[2] << 32) | a[3];

} /* end function sub_S128 */


/*-------------------------------------------------------------------*/
/* Negate signed 128 bit integer                                     */
/*                                                                   */
/* Input:                                                            */
/*      ms      most significant 64 bit of operand                   */
/*      ls      least significant 64 bit of operand                  */
/*-------------------------------------------------------------------*/
static void neg_S128 ( S64 *ms, S64 *ls )
{
S64 a[4];
S64 b[4];
int c;
int i;

    /* a = zero */
    a[0] = 0;
    a[1] = 0;
    a[2] = 0;
    a[3] = 0;

    /* split to 32 bit parts */
    b[0] = *ms >> 32;
    b[1] = *ms & 0x00000000FFFFFFFF;
    b[2] = *ls >> 32;
    b[3] = *ls & 0x00000000FFFFFFFF;

    i = 3;
    c = 0;
    while (i >= 0) {
        a[i] = a[i] - b[i] + c;
        c = a[i] >> 32;
        a[i] &= 0x00000000FFFFFFFF;
        i--;
    }

    /* back to operand */
    *ms = (a[0] << 32) | a[1];
    *ls = (a[2] << 32) | a[3];

} /* end function neg_S128 */


/*-------------------------------------------------------------------*/
/* Get short float from register                                     */
/*                                                                   */
/* Input:                                                            */
/*      fl      Internal float format to be converted to             */
/*      fpr     Register to be converted from                        */
/*-------------------------------------------------------------------*/
static void get_sf ( SHORT_FLOAT *fl, U32 *fpr )
{
    fl->sign = *fpr >> 31;
    fl->expo = (*fpr >> 24) & 0x7F;
    fl->short_fract = *fpr & 0x00FFFFFF;

} /* end function get_sf */


/*-------------------------------------------------------------------*/
/* Store short float to register                                     */
/*                                                                   */
/* Input:                                                            */
/*      fl      Internal float format to be converted from           */
/*      fpr     Register to be converted to                          */
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
/*      fl      Internal float format to be converted to             */
/*      fpr     Register to be converted from                        */
/*-------------------------------------------------------------------*/
static void get_lf ( LONG_FLOAT *fl, U32 *fpr )
{
    fl->sign = *fpr >> 31;
    fl->expo = (*fpr >> 24) & 0x7F;
    fl->long_fract = ((U64)(fpr[0] & 0x00FFFFFF) << 32)
                   | fpr[1];

} /* end function get_lf */


/*-------------------------------------------------------------------*/
/* Store long float to register                                      */
/*                                                                   */
/* Input:                                                            */
/*      fl      Internal float format to be converted from           */
/*      fpr     Register to be converted to                          */
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
/*      fl      Internal float format to be converted to             */
/*      fpr     Register to be converted from                        */
/*-------------------------------------------------------------------*/
static void get_ef ( EXTENDED_FLOAT *fl, U32 *fpr )
{
    fl->sign = *fpr >> 31;
    fl->expo = (*fpr >> 24) & 0x7F;
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
/*      fl      Internal float format to be converted from           */
/*      fpr     Register to be converted to                          */
/*-------------------------------------------------------------------*/
static void store_ef ( EXTENDED_FLOAT *fl, U32 *fpr )
{
char ls_sign;

    ls_sign = (fl->expo - 14) & 0x7F;
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
    fl->expo = (value >> 24) & 0x7F;

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
    fl->expo = (value >> 56) & 0x7F;

    /* Extract fraction from low-order 7 bytes */
    fl->long_fract = value & 0x00FFFFFFFFFFFFFFULL;

} /* end function vfetch_lf */



/*-------------------------------------------------------------------*/
/* Normalize short float                                             */
/* The fraction is expected to be non zero                           */
/* If zero an enless loop is caused !!!!!!                           */
/*                                                                   */
/* Input:                                                            */
/*      fl      Internal float                                       */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void normal_sf ( SHORT_FLOAT *fl, REGS *regs )
{
    while ((fl->short_fract & 0x00F00000) == 0) {
        fl->short_fract <<= 4;
        (fl->expo)--;
    }

    if (fl->expo < 0) {
        if (regs->psw.eumask) {
            fl->expo &= 0x7F;
            program_check (regs, PGM_EXPONENT_UNDERFLOW_EXCEPTION);
        } else {
            /* set true 0 */

            fl->short_fract = 0;
            fl->sign = POS;
            fl->expo = 0;
        }
    }
} /* end function normal_sf */


/*-------------------------------------------------------------------*/
/* Normalize long float                                              */
/* The fraction is expected to be non zero                           */
/* If zero an enless loop is caused !!!!!!                           */
/*                                                                   */
/* Input:                                                            */
/*      fl      Internal float                                       */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void normal_lf ( LONG_FLOAT *fl, REGS *regs )
{
    while ((fl->long_fract & 0x00F0000000000000LL) == 0) {
        fl->long_fract <<= 4;
        (fl->expo)--;
    }

    if (fl->expo < 0) {
        if (regs->psw.eumask) {
            fl->expo &= 0x7F;
            program_check (regs, PGM_EXPONENT_UNDERFLOW_EXCEPTION);
        } else {
            /* set true 0 */

            fl->long_fract = 0;
            fl->sign = POS;
            fl->expo = 0;
        }
    }
} /* end function normal_lf */


/*-------------------------------------------------------------------*/
/* Normalize extended float                                          */
/* The fraction is expected to be non zero                           */
/* If zero an enless loop is caused !!!!!!                           */
/*                                                                   */
/* Input:                                                            */
/*      fl      Internal float                                       */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void normal_ef ( EXTENDED_FLOAT *fl, REGS *regs )
{
    while (fl->ms_fract == 0) {
        fl->ms_fract = fl->ls_fract >> 16;
        fl->ls_fract <<= 48;
        fl->expo -= 12;
    }

    while ((fl->ms_fract & 0x0000F00000000000LL) == 0) {
        if (fl->ls_fract) {
            fl->ms_fract = (fl->ms_fract << 4)
                         | (fl->ls_fract >> 60);
            fl->ls_fract <<= 4;
        } else {
            fl->ms_fract <<= 4;
        }
        (fl->expo)--;
    }

    if (fl->expo < 0) {
        if (regs->psw.eumask) {
            fl->expo &= 0x7F;
            program_check (regs, PGM_EXPONENT_UNDERFLOW_EXCEPTION);
        } else {
            /* set true 0 */

            fl->ms_fract = 0;
            fl->ls_fract = 0;
            fl->sign = POS;
            fl->expo = 0;
        }
    }
} /* end function normal_ef */


/*-------------------------------------------------------------------*/
/* Handle significance of short float                                */
/* The fraction is expected to be zero                               */
/*                                                                   */
/* Input:                                                            */
/*      fl      Internal float                                       */
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
/* Handle significance of long float                                 */
/* The fraction is expected to be zero                               */
/*                                                                   */
/* Input:                                                            */
/*      fl      Internal float                                       */
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
/* Handle significance of extended float                             */
/* The fraction is expected to be zero                               */
/*                                                                   */
/* Input:                                                            */
/*      fl      Internal float                                       */
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
/*      fl      Internal float                                       */
/*      add_fl  Float to be added                                    */
/*      sub     Operator: add or subtract                            */
/*      norm    Normalize if true                                    */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void add_sf ( SHORT_FLOAT *fl, SHORT_FLOAT *add_fl, BYTE sub, BYTE normal, REGS *regs )
{
BYTE shift;

    if (add_fl->short_fract) {  /* add_fl not 0 */
        if (fl->short_fract) {  /* fl not 0 */
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

                            fl->sign = sub ? (! add_fl->sign) : add_fl->sign;
                            fl->short_fract = add_fl->short_fract;

                            if (normal) {
                                normal_sf (fl, regs);
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

                            if (normal) {
                                normal_sf (fl, regs);
                            }
                            return;
                        }
                    }
                    /* guard digit */
                    fl->short_fract <<= 4;
                }
            }
            /* compute with guard digit */

            if (fl->sign)
                /* 2s complement fract */
                fl->short_fract = (-(fl->short_fract));

            if (sub)
                /* subtract */
                fl->short_fract -= add_fl->short_fract;
            else
                /* add */
                fl->short_fract += add_fl->short_fract;

            /* reestablish sign */
            if (fl->short_fract < 0) {
                fl->short_fract = (-(fl->short_fract));
                fl->sign = NEG;
            } else
                fl->sign = POS;

            /* handle overflow with guard digit */
            if (fl->short_fract & 0xF0000000) {
                fl->short_fract >>= 8;

                if (fl->expo < 127)
                    (fl->expo)++;
                else {
                    fl->expo = 0;
                    program_check (regs, PGM_EXPONENT_OVERFLOW_EXCEPTION);
                }
            } else {

                if (normal) {
                    /* normalize with guard digit */
                    if (fl->short_fract) {
                        /* not 0 */

                        if (fl->short_fract & 0x0F000000) {
                            /* not normalize, just guard digit */
                            fl->short_fract >>= 4;
                        } else {
                            normal_sf (fl, regs);
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
            fl->sign = sub ? (! add_fl->sign) : add_fl->sign;
            fl->short_fract = add_fl->short_fract;
        }
    } else {                        /* add_fl 0 */
        if (fl->short_fract == 0) { /* fl 0 */
            /* both 0 */

            significance_sf (fl, regs);
            return;
        }
    }
    if (normal) {
        normal_sf (fl, regs);
    }
} /* end function add_sf */


/*-------------------------------------------------------------------*/
/* Add long float                                                    */
/*                                                                   */
/* Input:                                                            */
/*      fl      Internal float                                       */
/*      add_fl  Float to be added                                    */
/*      sub     Operator: add or subtract                            */
/*      norm    Normalize if true                                    */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void add_lf ( LONG_FLOAT *fl, LONG_FLOAT *add_fl, BYTE sub, BYTE normal, REGS *regs )
{
BYTE shift;

    if (add_fl->long_fract) {   /* add_fl not 0 */
        if (fl->long_fract) {   /* fl not 0 */
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

                            fl->sign = sub ? (! add_fl->sign) : add_fl->sign;
                            fl->long_fract = add_fl->long_fract;

                            if (normal) {
                                normal_lf (fl, regs);
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

                            if (normal) {
                                normal_lf (fl, regs);
                            }
                            return;
                        }
                    }
                    /* guard digit */
                    fl->long_fract <<= 4;
                }
            }
            /* compute with guard digit */

            if (fl->sign)
                /* 2s complement fract */
                fl->long_fract = (-(fl->long_fract));

            if (sub)
                /* subtract */
                fl->long_fract -= add_fl->long_fract;
            else
                /* add */
                fl->long_fract += add_fl->long_fract;

            /* reestablish sign */
            if (fl->long_fract < 0) {
                fl->long_fract = (-(fl->long_fract));
                fl->sign = NEG;
            } else
                fl->sign = POS;

            /* handle overflow with guard digit */
            if (fl->long_fract & 0xF000000000000000LL) {
                fl->long_fract >>= 8;

                if (fl->expo < 127)
                    (fl->expo)++;
                else {
                    fl->expo = 0;
                    program_check (regs, PGM_EXPONENT_OVERFLOW_EXCEPTION);
                }
            } else {

                if (normal) {
                    /* normalize with guard digit */
                    if (fl->long_fract) {
                        /* not 0 */

                        if (fl->long_fract & 0x0F00000000000000LL) {
                            /* not normalize, just guard digit */
                            fl->long_fract >>= 4;
                        } else {
                            normal_lf (fl, regs);
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
            fl->sign = sub ? (! add_fl->sign) : add_fl->sign;
            fl->long_fract = add_fl->long_fract;
        }
    } else {                       /* add_fl 0 */
        if (fl->long_fract == 0) { /* fl 0 */
            /* both 0 */

            significance_lf (fl, regs);
            return;
        }
    }
    if (normal) {
        normal_lf (fl, regs);
    }
} /* end function add_lf */


/*-------------------------------------------------------------------*/
/* Add extended float normalized                                     */
/*                                                                   */
/* Input:                                                            */
/*      fl      Internal float                                       */
/*      add_fl  Float to be added                                    */
/*      sub     Operator: add or subtract                            */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void add_ef ( EXTENDED_FLOAT *fl, EXTENDED_FLOAT *add_fl, BYTE sub, REGS *regs )
{
BYTE shift;

    if (add_fl->ms_fract || add_fl->ls_fract) { /* not 0 */
        if (fl->ms_fract || fl->ls_fract) {     /* not 0 */
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

                            fl->sign = sub ? (! add_fl->sign) : add_fl->sign;
                            fl->ms_fract = add_fl->ms_fract;
                            fl->ls_fract = add_fl->ls_fract;

                            normal_ef (fl, regs);
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

                            fl->sign = sub ? (! add_fl->sign) : add_fl->sign;
                            fl->ms_fract = add_fl->ms_fract;
                            fl->ls_fract = add_fl->ls_fract;

                            normal_ef (fl, regs);
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

                            normal_ef (fl, regs);
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

                            normal_ef (fl, regs);
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

            if (fl->sign)
                /* 2s complement fract */
                neg_S128 (&(fl->ms_fract), &(fl->ls_fract));

            if (sub)
                /* subtract */
                sub_S128 (&(fl->ms_fract),
                          &(fl->ls_fract),
                          add_fl->ms_fract,
                          add_fl->ls_fract);
            else
                /* add */
                add_S128 (&(fl->ms_fract),
                          &(fl->ls_fract),
                          add_fl->ms_fract,
                          add_fl->ls_fract);

            /* reestablish sign */
            if (fl->ms_fract < 0) {
                neg_S128 (&(fl->ms_fract), &(fl->ls_fract));
                fl->sign = NEG;
            } else
                fl->sign = POS;

            /* handle overflow with guard digit */
            if (fl->ms_fract & 0x00F0000000000000LL) {
                fl->ls_fract = (fl->ms_fract << 56)
                         | (fl->ls_fract >> 8);
                fl->ms_fract >>= 8;

                if (fl->expo < 127)
                    (fl->expo)++;
                else {
                    fl->expo = 0;
                    program_check (regs, PGM_EXPONENT_OVERFLOW_EXCEPTION);
                }
            } else {
                /* normalize with guard digit */
                if (fl->ms_fract || fl->ls_fract) {
                    /* not 0 */

                    if (fl->ms_fract & 0x000F000000000000LL) {
                        /* not normalize, just guard digit */
                        fl->ls_fract = (fl->ms_fract << 60)
                                     | (fl->ls_fract >> 4);
                        fl->ms_fract >>= 4;
                    } else {
                        normal_ef (fl, regs);
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
            fl->sign = sub ? (! add_fl->sign) : add_fl->sign;
            fl->ms_fract = add_fl->ms_fract;
            fl->ls_fract = add_fl->ls_fract;
        }
    } else {                                              /* add_fl 0*/
        if ((fl->ms_fract == 0) && (fl->ls_fract == 0)) { /* fl 0 */
            /* both 0 */

            significance_ef (fl, regs);
            return;
        }
    }
    normal_ef (fl, regs);

} /* end function add_ef */


/*-------------------------------------------------------------------*/
/* Compare short float                                               */
/*                                                                   */
/* Input:                                                            */
/*      fl      Internal float                                       */
/*      cmp_fl  Float to be compared                                 */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void cmp_sf ( SHORT_FLOAT *fl, SHORT_FLOAT *cmp_fl, REGS *regs )
{
BYTE shift;

    if (cmp_fl->short_fract) {  /* cmp_fl not 0 */
        if (fl->short_fract) {  /* fl not 0 */
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
                            } else
                                regs->psw.cc = 0;

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
                            } else
                                regs->psw.cc = 0;

                            return;
                        }
                    }
                    /* guard digit */
                    fl->short_fract <<= 4;
                }
            }
            /* compute with guard digit */

            if (fl->sign)
                /* 2s complement fract */
                fl->short_fract = (-(fl->short_fract));

            if (cmp_fl->sign)
                /* add */
                fl->short_fract += cmp_fl->short_fract;
            else
                /* subtract */
                fl->short_fract -= cmp_fl->short_fract;

            /* reestablish sign */
            if (fl->short_fract < 0) {
                fl->short_fract = (-(fl->short_fract));
                fl->sign = NEG;
            } else
                fl->sign = POS;

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
            } else
                regs->psw.cc = 0;

            return;
        } else { /* fl 0, cmp_fl not 0 */
            /* Set condition code */
            regs->psw.cc = cmp_fl->sign ? 2 : 1;

            return;
        }
    } else {                        /* cmp_fl 0 */
        /* Set condition code */
        if (fl->short_fract) {
            regs->psw.cc = fl->sign ? 1 : 2;
        } else
            regs->psw.cc = 0;

        return;
    }
} /* end function cmp_sf */


/*-------------------------------------------------------------------*/
/* Compare long float                                                */
/*                                                                   */
/* Input:                                                            */
/*      fl      Internal float                                       */
/*      cmp_fl  Float to be compared                                 */
/*      regs    CPU register context                                 */
/*-------------------------------------------------------------------*/
static void cmp_lf ( LONG_FLOAT *fl, LONG_FLOAT *cmp_fl, REGS *regs )
{
BYTE shift;

    if (cmp_fl->long_fract) {   /* cmp_fl not 0 */
        if (fl->long_fract) {   /* fl not 0 */
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
                            } else
                                regs->psw.cc = 0;

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
                            } else
                                regs->psw.cc = 0;

                            return;
                        }
                    }
                    /* guard digit */
                    fl->long_fract <<= 4;
                }
            }
            /* compute with guard digit */

            if (fl->sign)
                /* 2s complement fract */
                fl->long_fract = (-(fl->long_fract));

            if (cmp_fl->sign)
                /* add */
                fl->long_fract += cmp_fl->long_fract;
            else
                /* subtract */
                fl->long_fract -= cmp_fl->long_fract;

            /* reestablish sign */
            if (fl->long_fract < 0) {
                fl->long_fract = (-(fl->long_fract));
                fl->sign = NEG;
            } else
                fl->sign = POS;

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
            } else
                regs->psw.cc = 0;

            return;
        } else { /* fl 0, cmp_fl not 0 */
            /* Set condition code */
            regs->psw.cc = cmp_fl->sign ? 2 : 1;

            return;
        }
    } else {                        /* cmp_fl 0 */
        /* Set condition code */
        if (fl->long_fract) {
            regs->psw.cc = fl->sign ? 1 : 2;
        } else
            regs->psw.cc = 0;

        return;
    }
} /* end function cmp_lf */


void halve_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;

    /* Get register content */
    get_lf (&fl, regs->fpr + r2);

    /* Halve the value */
    fl.long_fract >>= 1;
    if (fl.long_fract)
        normal_lf (&fl, regs);

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


void round_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;

    /* Get register content */
    get_lf (&fl, regs->fpr + r2);

    /* Rounding */
    fl.long_fract += (regs->fpr[r2 + 2] >> 23) & 1;

    /* Handle overflow */
    if (fl.long_fract & 0x0F00000000000000LL) {
        fl.long_fract >>= 4;

        if (fl.expo < 127)
            (fl.expo)++;
        else {
            fl.expo = 0;
            program_check (regs, PGM_EXPONENT_OVERFLOW_EXCEPTION);
        }
    }

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


void multiply_float_ext_reg (int r1, int r2, REGS *regs)
{

/* not yet implemented */

}


void multiply_float_long_to_ext_reg (int r1, int r2, REGS *regs)
{

/* not yet implemented */

}


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


void add_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT add_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&add_fl, regs->fpr + r2);

    /* Add long with normalization */
    add_lf (&fl, &add_fl, add_fl.sign, 1, regs);

    /* Set condition code */
    if (fl.long_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


void subtract_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT sub_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&sub_fl, regs->fpr + r2);

    /* Subtract long with normalization */
    add_lf (&fl, &sub_fl, ! sub_fl.sign, 1, regs);

    /* Set condition code */
    if (fl.long_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


void multiply_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT mul_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&mul_fl, regs->fpr + r2);

/* not yet implemented */

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


void divide_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT div_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&div_fl, regs->fpr + r2);

/* not yet implemented */

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


void add_unnormal_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT add_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&add_fl, regs->fpr + r2);

    /* Add long without normalization */
    add_lf (&fl, &add_fl, add_fl.sign, 0, regs);

    /* Set condition code */
    if (fl.long_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


void subtract_unnormal_float_long_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT sub_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    get_lf (&sub_fl, regs->fpr + r2);

    /* Subtract long without normalization */
    add_lf (&fl, &sub_fl, ! sub_fl.sign, 0, regs);

    /* Set condition code */
    if (fl.long_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


void halve_float_short_reg (int r1, int r2, REGS *regs)
{
SHORT_FLOAT fl;

    /* Get register content */
    get_sf (&fl, regs->fpr + r2);

    /* Halve the value */
    fl.short_fract >>= 1;
    if (fl.short_fract)
        normal_sf (&fl, regs);

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


void round_float_short_reg (int r1, int r2, REGS *regs)
{
LONG_FLOAT from_fl;
SHORT_FLOAT to_fl;

    /* Get register content */
    get_lf (&from_fl, regs->fpr + r2);

    /* Rounding */
    to_fl.short_fract = (from_fl.long_fract + 0x0000000080000000LL) >> 32;
    to_fl.sign = from_fl.sign;
    to_fl.expo = from_fl.expo;

    /* Handle overflow */
    if (to_fl.short_fract & 0x0F000000) {
        to_fl.short_fract >>= 4;

        if (to_fl.expo < 127)
            (to_fl.expo)++;
        else {
            to_fl.expo = 0;
            program_check (regs, PGM_EXPONENT_OVERFLOW_EXCEPTION);
        }
    }

    /* To register */
    store_sf (&to_fl, regs->fpr + r1);
}


void add_float_ext_reg (int r1, int r2, REGS *regs)
{
EXTENDED_FLOAT fl;
EXTENDED_FLOAT add_fl;

    /* Get the operands */
    get_ef (&fl, regs->fpr + r1);
    get_ef (&add_fl, regs->fpr + r2);

    /* Add extended */
    add_ef (&fl, &add_fl, add_fl.sign, regs);

    /* Set condition code */
    if (fl.ms_fract || fl.ls_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_ef (&fl, regs->fpr + r1);
}


void subtract_float_ext_reg (int r1, int r2, REGS *regs)
{
EXTENDED_FLOAT fl;
EXTENDED_FLOAT sub_fl;

    /* Get the operands */
    get_ef (&fl, regs->fpr + r1);
    get_ef (&sub_fl, regs->fpr + r2);

    /* Subtract extended */
    add_ef (&fl, &sub_fl, ! sub_fl.sign, regs);

    /* Set condition code */
    if (fl.ms_fract || fl.ls_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_ef (&fl, regs->fpr + r1);
}


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


void add_float_short_reg (int r1, int r2, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT add_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&add_fl, regs->fpr + r2);

    /* Add short with normalization */
    add_sf (&fl, &add_fl, add_fl.sign, 1, regs);

    /* Set condition code */
    if (fl.short_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


void subtract_float_short_reg (int r1, int r2, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT sub_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&sub_fl, regs->fpr + r2);

    /* Subtract short with normalization */
    add_sf (&fl, &sub_fl, ! sub_fl.sign, 1, regs);

    /* Set condition code */
    if (fl.short_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


void multiply_float_short_to_long_reg (int r1, int r2, REGS *regs)
{

/* Not yet implemented */

}


void divide_float_short_reg (int r1, int r2, REGS *regs)
{

/* Not yet implemented */

}


void add_unnormal_float_short_reg (int r1, int r2, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT add_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&add_fl, regs->fpr + r2);

    /* Add short without normalization */
    add_sf (&fl, &add_fl, add_fl.sign, 0, regs);

    /* Set condition code */
    if (fl.short_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


void subtract_unnormal_float_short_reg (int r1, int r2, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT sub_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    get_sf (&sub_fl, regs->fpr + r2);

    /* Subtract short without normalization */
    add_sf (&fl, &sub_fl, sub_fl.sign, 0, regs);

    /* Set condition code */
    if (fl.short_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


void multiply_float_long_to_ext (int r1, U32 addr, int arn, REGS *regs)
{

/* not yet implemented */

}


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


void add_float_long (int r1, U32 addr, int arn, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT add_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&add_fl, addr, arn, regs );

    /* Add long with normalization */
    add_lf (&fl, &add_fl, add_fl.sign, 1, regs);

    /* Set condition code */
    if (fl.long_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


void subtract_float_long (int r1, U32 addr, int arn, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT sub_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&sub_fl, addr, arn, regs );

    /* Subtract long with normalization */
    add_lf (&fl, &sub_fl, ! sub_fl.sign, 1, regs);

    /* Set condition code */
    if (fl.long_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


void multiply_float_long (int r1, U32 addr, int arn, REGS *regs)
{

/* not yet implemented */

}


void divide_float_long (int r1, U32 addr, int arn, REGS *regs)
{

/* not yet implemented */

}


void add_unnormal_float_long (int r1, U32 addr, int arn, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT add_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&add_fl, addr, arn, regs );

    /* Add long without normalization */
    add_lf (&fl, &add_fl, add_fl.sign, 0, regs);

    /* Set condition code */
    if (fl.long_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


void subtract_unnormal_float_long (int r1, U32 addr, int arn, REGS *regs)
{
LONG_FLOAT fl;
LONG_FLOAT sub_fl;

    /* Get the operands */
    get_lf (&fl, regs->fpr + r1);
    vfetch_lf (&sub_fl, addr, arn, regs );

    /* Subtract long without normalization */
    add_lf (&fl, &sub_fl, ! sub_fl.sign, 0, regs);

    /* Set condition code */
    if (fl.long_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_lf (&fl, regs->fpr + r1);
}


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


void add_float_short (int r1, U32 addr, int arn, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT add_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&add_fl, addr, arn, regs );

    /* Add long with normalization */
    add_sf (&fl, &add_fl, add_fl.sign, 1, regs);

    /* Set condition code */
    if (fl.short_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


void subtract_float_short (int r1, U32 addr, int arn, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT sub_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&sub_fl, addr, arn, regs );

    /* Add long with normalization */
    add_sf (&fl, &sub_fl, ! sub_fl.sign, 1, regs);

    /* Set condition code */
    if (fl.short_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


void multiply_float_short_to_long (int r1, U32 addr, int arn, REGS *regs)
{

/* not yet implemented */

}


void divide_float_short (int r1, U32 addr, int arn, REGS *regs)
{

/* not yet implemented */

}


void add_unnormal_float_short (int r1, U32 addr, int arn, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT add_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&add_fl, addr, arn, regs );

    /* Add long without normalization */
    add_sf (&fl, &add_fl, add_fl.sign, 0, regs);

    /* Set condition code */
    if (fl.short_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


void subtract_unnormal_float_short (int r1, U32 addr, int arn, REGS *regs)
{
SHORT_FLOAT fl;
SHORT_FLOAT sub_fl;

    /* Get the operands */
    get_sf (&fl, regs->fpr + r1);
    vfetch_sf (&sub_fl, addr, arn, regs );

    /* Add long without normalization */
    add_sf (&fl, &sub_fl, ! sub_fl.sign, 0, regs);

    /* Set condition code */
    if (fl.short_fract) {
        regs->psw.cc = fl.sign ? 1 : 2;
    } else
        regs->psw.cc = 0;

    /* Back to register */
    store_sf (&fl, regs->fpr + r1);
}


void divide_float_ext_reg (int r1, int r2, REGS *regs)
{

/* not yet implemented */

}


/* end of float.c */
