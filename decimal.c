/* DECIMAL.C    (c) Copyright Roger Bowler, 1991-1999                */
/*              ESA/390 Packed Decimal Routines                      */

/*-------------------------------------------------------------------*/
/* This module contains packed decimal subroutines for the           */
/* Hercules ESA/390 emulator.                                        */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Internal macro definitions                                        */
/*-------------------------------------------------------------------*/
#define MAX_DECIMAL_LENGTH      16
#define MAX_DECIMAL_DIGITS      (((MAX_DECIMAL_LENGTH)*2)-1)

/*-------------------------------------------------------------------*/
/* Load a packed decimal storage operand into a decimal byte string  */
/*                                                                   */
/* Input:                                                            */
/*      addr    Logical address of packed decimal storage operand    */
/*      len     Length minus one of storage operand (range 0-15)     */
/*      arn     Access register number associated with operand       */
/*      regs    CPU register context                                 */
/* Output:                                                           */
/*      result  Points to a 31-byte area into which the decimal      */
/*              digits are loaded.  One decimal digit is loaded      */
/*              into the low-order 4 bits of each byte, and the      */
/*              result is padded to the left with high-order zeroes  */
/*              if the storage operand contains less than 31 digits. */
/*      count   Points to an integer to receive the number of        */
/*              digits in the result excluding leading zeroes.       */
/*              This field is set to zero if the result is all zero. */
/*      sign    Points to an integer which will be set to -1 if the  */
/*              result is negative, 0 if the result is zero, or      */
/*              +1 if the result is positive.                        */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, or if the operand causes a data exception         */
/*      because of invalid decimal digits or sign.                   */
/*-------------------------------------------------------------------*/
static void load_decimal (U32 addr, int len, int arn, REGS *regs,
                        BYTE *result, int *count, int *sign)
{
int     h;                              /* Hexadecimal digit         */
int     i, j;                           /* Array subscripts          */
int     n;                              /* Significant digit counter */
BYTE    pack[MAX_DECIMAL_LENGTH];       /* Packed decimal work area  */

    /* Fetch the operand into a work area */
    memset (pack, 0, sizeof(pack));
    vfetchc (pack+sizeof(pack)-len-1, len, addr, arn, regs);

    /* Unpack digits into result */
    for (i=0, j=0, n=0; i < MAX_DECIMAL_DIGITS; i++)
    {
        /* Load source digit */
        if (i & 1)
            h = pack[j++] & 0x0F;
        else
            h = pack[j] >> 4;

        /* Check for valid numeric */
        if (h > 9) {
            program_check (PGM_DATA_EXCEPTION);
            return;
        }

        /* Count significant digits */
        if (n > 0 || h != 0)
            n++;

        /* Store decimal digit in result */
        result[i] = h;

    } /* end for */

    /* Check for valid sign */
    h = pack[MAX_DECIMAL_LENGTH-1] & 0x0F;
    if ( h < 0x0A ) {
        program_check (PGM_DATA_EXCEPTION);
        return;
    }

    /* Set number of significant digits */
    *count = n;

    /* Set sign of result */
    *sign = (n == 0) ? 0 : (h == 0x0B || h == 0x0D) ? -1 : 1;

} /* end function load_decimal */

/*-------------------------------------------------------------------*/
/* Store decimal byte string into packed decimal storage operand     */
/*                                                                   */
/* Input:                                                            */
/*      addr    Logical address of packed decimal storage operand    */
/*      len     Length minus one of storage operand (range 0-15)     */
/*      arn     Access register number associated with operand       */
/*      regs    CPU register context                                 */
/*      dec     A 31-byte area containing the decimal digits to be   */
/*              stored.  Each byte contains one decimal digit in     */
/*              the low-order 4 bits of the byte.                    */
/*      count   The number of significant digits.                    */
/*      sign    -1 if the digits are negative, 0 if the digits are   */
/*              all zero, +1 if the digits are positive.             */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or protection exception.  */
/*-------------------------------------------------------------------*/
static void store_decimal (U32 addr, int len, int arn, REGS *regs,
                        BYTE *dec, int count, int sign)
{
int     i, j;                           /* Array subscripts          */
int     n;                              /* Significant digit counter */
BYTE    pack[MAX_DECIMAL_LENGTH];       /* Packed decimal work area  */

    /* Pack digits into packed decimal work area */
    for (i=0, j=0, n=0; i < MAX_DECIMAL_DIGITS; i++)
    {
        if (i & 1)
            pack[j++] |= dec[i];
        else
            pack[j] = dec[i] << 4;
    } /* end for */

    /* Pack the sign into low-order digit of work area */
    pack[MAX_DECIMAL_LENGTH-1] |= (sign < 0 ? 0x0D : 0x0C);

    /* Store the result at the operand location */
    vstorec (pack+sizeof(pack)-len-1, len, addr, arn, regs);

} /* end function store_decimal */

/*-------------------------------------------------------------------*/
/* Add two decimal byte strings as unsigned decimal numbers          */
/*                                                                   */
/* Input:                                                            */
/*      dec1    A 31-byte area containing the decimal digits of      */
/*              the first operand.  Each byte contains one decimal   */
/*              digit in the low-order 4 bits of the byte.           */
/*      dec2    A 31-byte area containing the decimal digits of      */
/*              the second operand.  Each byte contains one decimal  */
/*              digit in the low-order 4 bits of the byte.           */
/* Output:                                                           */
/*      result  Points to a 31-byte area to contain the result       */
/*              digits. One decimal digit is placed in the low-order */
/*              4 bits of each byte.                                 */
/*      count   Points to an integer to receive the number of        */
/*              digits in the result excluding leading zeroes.       */
/*              This field is set to zero if the result is all zero, */
/*              or to MAX_DECIMAL_DIGITS+1 if overflow occurred.     */
/*-------------------------------------------------------------------*/
static void add_decimal (BYTE *dec1, BYTE *dec2,
                        BYTE *result, int *count)
{
int     d;                              /* Decimal digit             */
int     i;                              /* Array subscript           */
int     n = 0;                          /* Significant digit counter */
int     carry = 0;                      /* Carry indicator           */

    /* Add digits from right to left */
    for (i = MAX_DECIMAL_DIGITS - 1; i >= 0; i--)
    {
        /* Add digits from first and second operands */
        d = dec1[i] + dec2[i] + carry;

        /* Check for carry into next digit */
        if (d > 9) {
            d -= 10;
            carry = 1;
        } else {
            carry = 0;
        }

        /* Check for significant digit */
        if (d != 0)
            n = MAX_DECIMAL_DIGITS - i;

        /* Store digit in result */
        result[i] = d;

    } /* end for */

    /* Check for carry out of leftmost digit */
    if (carry)
        n = MAX_DECIMAL_DIGITS + 1;

    /* Return significant digit counter */
    *count = n;

} /* end function add_decimal */

/*-------------------------------------------------------------------*/
/* Subtract two decimal byte strings as unsigned decimal numbers     */
/*                                                                   */
/* Input:                                                            */
/*      dec1    A 31-byte area containing the decimal digits of      */
/*              the first operand.  Each byte contains one decimal   */
/*              digit in the low-order 4 bits of the byte.           */
/*      dec2    A 31-byte area containing the decimal digits of      */
/*              the second operand.  Each byte contains one decimal  */
/*              digit in the low-order 4 bits of the byte.           */
/* Output:                                                           */
/*      result  Points to a 31-byte area to contain the result       */
/*              digits. One decimal digit is placed in the low-order */
/*              4 bits of each byte.                                 */
/*      count   Points to an integer to receive the number of        */
/*              digits in the result excluding leading zeroes.       */
/*              This field is set to zero if the result is all zero. */
/*      sign    -1 if the result is negative (operand 2 > operand 1) */
/*              0 if result is zero (operand 2 = operand 1)          */
/*              +1 if the result is positive (operand 2 < operand 1) */
/*-------------------------------------------------------------------*/
static void subtract_decimal (BYTE *dec1, BYTE *dec2,
                        BYTE *result, int *count, int *sign)
{
int     d;                              /* Decimal digit             */
int     i;                              /* Array subscript           */
int     n = 0;                          /* Significant digit counter */
int     borrow = 0;                     /* Borrow indicator          */
int     rc;                             /* Return code               */
BYTE   *higher;                         /* -> Higher value operand   */
BYTE   *lower;                          /* -> Lower value operand    */

    /* Compare digits to find which operand has higher numeric value */
    rc = memcmp (dec1, dec2, MAX_DECIMAL_DIGITS);

    /* Return zero result if both operands are equal */
    if (rc == 0) {
        memset (result, 0, MAX_DECIMAL_DIGITS);
        *count = 0;
        *sign = 0;
        return;
    }

    /* Point to higher and lower value operands and set sign */
    if (rc > 0) {
        higher = dec1;
        lower = dec2;
       *sign = +1;
    } else {
        lower = dec1;
        higher = dec2;
       *sign = -1;
    }

    /* Subtract digits from right to left */
    for (i = MAX_DECIMAL_DIGITS - 1; i >= 0; i--)
    {
        /* Subtract lower operand digit from higher operand digit */
        d = higher[i] - lower[i] - borrow;

        /* Check for borrow from next digit */
        if (d < 0) {
            d += 10;
            borrow = 1;
        } else {
            borrow = 0;
        }

        /* Check for significant digit */
        if (d != 0)
            n = MAX_DECIMAL_DIGITS - i;

        /* Store digit in result */
        result[i] = d;

    } /* end for */

    /* Return significant digit counter */
    *count = n;

} /* end function subtract_decimal */

/*-------------------------------------------------------------------*/
/* Compare two packed decimal operands                               */
/*                                                                   */
/* Input:                                                            */
/*      addr1   Logical address of packed decimal storage operand 1  */
/*      len1    Length minus one of storage operand 1 (range 0-15)   */
/*      arn1    Access register number associated with operand 1     */
/*      addr2   Logical address of packed decimal storage operand 2  */
/*      len2    Length minus one of storage operand 2 (range 0-15)   */
/*      arn2    Access register number associated with operand 2     */
/*      regs    CPU register context                                 */
/* Output:                                                           */
/*      The return value is the condition code:                      */
/*      0=operands are equal, 1=1st operand low, 2=1st operand high  */
/*                                                                   */
/*      A program check may be generated if either logical address   */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, or if either operand causes a data exception      */
/*      because of invalid decimal digits or sign.                   */
/*-------------------------------------------------------------------*/
int compare_packed (U32 addr1, int len1, int arn1,
                        U32 addr2, int len2, int arn2, REGS *regs)
{
BYTE    work1[MAX_DECIMAL_DIGITS];      /* Work area for operand 1   */
BYTE    work2[MAX_DECIMAL_DIGITS];      /* Work area for operand 2   */
int     count1, count2;                 /* Significant digit counters*/
int     sign1, sign2;                   /* Sign of each operand      */
int     rc;                             /* Return code               */

    /* Load operands into work areas */
    load_decimal (addr1, len1, arn1, regs, work1, &count1, &sign1);
    load_decimal (addr2, len2, arn2, regs, work2, &count2, &sign2);

    /* Result is equal if both operands are zero */
    if (sign1 == 0 && sign2 == 0)
        return 0;

    /* Result is low if operand 1 is -ve and operand 2 is +ve */
    if (sign1 < 0 && sign2 > 0)
        return 1;

    /* Result is high if operand 1 is +ve and operand 2 is -ve */
    if (sign1 > 0 && sign2 < 0)
        return 2;

    /* If signs are equal then compare the digits */
    rc = memcmp (work1, work2, MAX_DECIMAL_DIGITS);

    /* Return low or high (depending on sign) if digits are unequal */
    if (rc < 0) return (sign1 > 0) ? 1 : 2;
    if (rc > 0) return (sign1 > 0) ? 2 : 1;

    /* Return equal if all digits and signs are equal */
    return 0;

} /* end function compare_packed */

/*-------------------------------------------------------------------*/
/* Add two packed decimal operands                                   */
/*                                                                   */
/* Input:                                                            */
/*      addr1   Logical address of packed decimal storage operand 1  */
/*      len1    Length minus one of storage operand 1 (range 0-15)   */
/*      arn1    Access register number associated with operand 1     */
/*      addr2   Logical address of packed decimal storage operand 2  */
/*      len2    Length minus one of storage operand 2 (range 0-15)   */
/*      arn2    Access register number associated with operand 2     */
/*      regs    CPU register context                                 */
/* Output:                                                           */
/*      The return value is the condition code:                      */
/*      0=result zero, 1=result -ve, 2=result +ve, 3=overflow        */
/*                                                                   */
/*      A program check may be generated if either logical address   */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, or if either operand causes a data exception      */
/*      because of invalid decimal digits or sign, or if the         */
/*      first operand is store protected.  Depending on the PSW      */
/*      program mask, decimal overflow may cause a program check.    */
/*-------------------------------------------------------------------*/
int add_packed (U32 addr1, int len1, int arn1,
                        U32 addr2, int len2, int arn2, REGS *regs)
{
int     cc;                             /* Condition code            */
BYTE    work1[MAX_DECIMAL_DIGITS];      /* Work area for operand 1   */
BYTE    work2[MAX_DECIMAL_DIGITS];      /* Work area for operand 2   */
BYTE    work3[MAX_DECIMAL_DIGITS];      /* Work area for result      */
int     count1, count2, count3;         /* Significant digit counters*/
int     sign1, sign2, sign3;            /* Sign of operands & result */

    /* Load operands into work areas */
    load_decimal (addr1, len1, arn1, regs, work1, &count1, &sign1);
    load_decimal (addr2, len2, arn2, regs, work2, &count2, &sign2);

    /* Add or subtract operand values */
    if (sign2 == 0)
    {
        /* If second operand is zero then result is first operand */
        memcpy (work3, work1, MAX_DECIMAL_DIGITS);
        count3 = count1;
        sign3 = sign1;
    }
    else if (sign1 == 0)
    {
        /* If first operand is zero then result is second operand */
        memcpy (work3, work2, MAX_DECIMAL_DIGITS);
        count3 = count2;
        sign3 = sign2;
    }
    else if (sign1 == sign2)
    {
        /* If signs are equal then add operands */
        add_decimal (work1, work2, work3, &count3);
        sign3 = sign1;
    }
    else
    {
        /* If signs are opposite then subtract operands */
        subtract_decimal (work1, work2, work3, &count3, &sign3);
        if (sign1 < 0) sign3 = -sign3;
    }

    /* Set condition code */
    cc = (count3 == 0) ? 0 : (sign3 < 1) ? 1 : 2;

    /* Overflow if result exceeds first operand length */
    if (count3 > (len1+1) * 2 - 1)
        cc = 3;

    /* Store result into first operand location */
    store_decimal (addr1, len1, arn1, regs, work3, count3, sign3);

    /* Program check if overflow and PSW program mask is set */
    if (cc == 3 && regs->psw.domask)
    {
        program_check (PGM_DECIMAL_OVERFLOW_EXCEPTION);
    }

    /* Return condition code */
    return cc;

} /* end function add_packed */

/*-------------------------------------------------------------------*/
/* Subtract two packed decimal operands                              */
/*                                                                   */
/* Input:                                                            */
/*      addr1   Logical address of packed decimal storage operand 1  */
/*      len1    Length minus one of storage operand 1 (range 0-15)   */
/*      arn1    Access register number associated with operand 1     */
/*      addr2   Logical address of packed decimal storage operand 2  */
/*      len2    Length minus one of storage operand 2 (range 0-15)   */
/*      arn2    Access register number associated with operand 2     */
/*      regs    CPU register context                                 */
/* Output:                                                           */
/*      The return value is the condition code:                      */
/*      0=result zero, 1=result -ve, 2=result +ve, 3=overflow        */
/*                                                                   */
/*      A program check may be generated if either logical address   */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, or if either operand causes a data exception      */
/*      because of invalid decimal digits or sign, or if the         */
/*      first operand is store protected.  Depending on the PSW      */
/*      program mask, decimal overflow may cause a program check.    */
/*-------------------------------------------------------------------*/
int subtract_packed (U32 addr1, int len1, int arn1,
                        U32 addr2, int len2, int arn2, REGS *regs)
{
int     cc;                             /* Condition code            */
BYTE    work1[MAX_DECIMAL_DIGITS];      /* Work area for operand 1   */
BYTE    work2[MAX_DECIMAL_DIGITS];      /* Work area for operand 2   */
BYTE    work3[MAX_DECIMAL_DIGITS];      /* Work area for result      */
int     count1, count2, count3;         /* Significant digit counters*/
int     sign1, sign2, sign3;            /* Sign of operands & result */

    /* Load operands into work areas */
    load_decimal (addr1, len1, arn1, regs, work1, &count1, &sign1);
    load_decimal (addr2, len2, arn2, regs, work2, &count2, &sign2);

    /* Add or subtract operand values */
    if (sign2 == 0)
    {
        /* If second operand is zero then result is first operand */
        memcpy (work3, work1, MAX_DECIMAL_DIGITS);
        count3 = count1;
        sign3 = sign1;
    }
    else if (sign1 == 0)
    {
        /* If first operand is zero then result is -second operand */
        memcpy (work3, work2, MAX_DECIMAL_DIGITS);
        count3 = count2;
        sign3 = -sign2;
    }
    else if (sign1 != sign2)
    {
        /* If signs are opposite then add operands */
        add_decimal (work1, work2, work3, &count3);
        sign3 = sign1;
    }
    else
    {
        /* If signs are equal then subtract operands */
        subtract_decimal (work1, work2, work3, &count3, &sign3);
        if (sign1 < 0) sign3 = -sign3;
    }

    /* Set condition code */
    cc = (count3 == 0) ? 0 : (sign3 < 1) ? 1 : 2;

    /* Overflow if result exceeds first operand length */
    if (count3 > (len1+1) * 2 - 1)
        cc = 3;

    /* Store result into first operand location */
    store_decimal (addr1, len1, arn1, regs, work3, count3, sign3);

    /* Program check if overflow and PSW program mask is set */
    if (cc == 3 && regs->psw.domask)
    {
        program_check (PGM_DECIMAL_OVERFLOW_EXCEPTION);
    }

    /* Return condition code */
    return cc;

} /* end function subtract_packed */

/*-------------------------------------------------------------------*/
/* Copy packed decimal operands                                      */
/*                                                                   */
/* Input:                                                            */
/*      addr1   Logical address of packed decimal storage operand 1  */
/*      len1    Length minus one of storage operand 1 (range 0-15)   */
/*      arn1    Access register number associated with operand 1     */
/*      addr2   Logical address of packed decimal storage operand 2  */
/*      len2    Length minus one of storage operand 2 (range 0-15)   */
/*      arn2    Access register number associated with operand 2     */
/*      regs    CPU register context                                 */
/* Output:                                                           */
/*      The return value is the condition code:                      */
/*      0=result zero, 1=result -ve, 2=result +ve, 3=overflow        */
/*                                                                   */
/*      A program check may be generated if either logical address   */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, or if either operand causes a data exception      */
/*      because of invalid decimal digits or sign, or if the         */
/*      first operand is store protected.  Depending on the PSW      */
/*      program mask, decimal overflow may cause a program check.    */
/*-------------------------------------------------------------------*/
int zero_and_add_packed (U32 addr1, int len1, int arn1,
                        U32 addr2, int len2, int arn2, REGS *regs)
{
int     cc;                             /* Condition code            */
BYTE    work[MAX_DECIMAL_DIGITS];       /* Work area for operand     */
int     count;                          /* Significant digit counter */
int     sign;                           /* Sign                      */

    /* Load second operand into work area */
    load_decimal (addr2, len2, arn2, regs, work, &count, &sign);

    /* Set condition code */
    cc = (count == 0) ? 0 : (sign < 1) ? 1 : 2;

    /* Overflow if result exceeds first operand length */
    if (count > (len1+1) * 2 - 1)
        cc = 3;

    /* Store result into first operand location */
    store_decimal (addr1, len1, arn1, regs, work, count, sign);

    /* Program check if overflow and PSW program mask is set */
    if (cc == 3 && regs->psw.domask)
    {
        program_check (PGM_DECIMAL_OVERFLOW_EXCEPTION);
    }

    /* Return condition code */
    return cc;

} /* end function zero_and_add_packed */

