/* DECIMAL.C    (c) Copyright Roger Bowler, 1991-1999                */
/*              ESA/390 Packed Decimal Routines                      */

/*-------------------------------------------------------------------*/
/* This module contains packed decimal subroutines for ESA/390.      */
/*                                                                   */
/* Acknowledgements:                                                 */
/*      The lowest level string-math functions are modelled on       */
/*      algorithms in D.E.Knuth's 'The Art of Computer Programming   */
/*      Vol.2', and on C.E.Burton's algorithms in DDJ #89.           */
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
/*      sign    Points to an integer which will be set to -1 if a    */
/*              negative sign was loaded from the operand, or +1 if  */
/*              a positive sign was loaded from the operand.         */
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

    /* Fetch the packed decimal operand into work area */
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

    /* Set sign of operand */
    *sign = (h == 0x0B || h == 0x0D) ? -1 : 1;

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
/*      sign    -1 if a negative sign is to be stored, or +1 if a    */
/*              positive sign is to be stored.                       */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or protection exception.  */
/*-------------------------------------------------------------------*/
static void store_decimal (U32 addr, int len, int arn, REGS *regs,
                        BYTE *dec, int sign)
{
int     i, j;                           /* Array subscripts          */
BYTE    pack[MAX_DECIMAL_LENGTH];       /* Packed decimal work area  */

    /* Pack digits into packed decimal work area */
    for (i=0, j=0; i < MAX_DECIMAL_DIGITS; i++)
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
/*      sign    -1 if the result is negative (operand2 > operand1)   */
/*              +1 if the result is positive (operand2 <= operand1)  */
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

    /* Return positive zero result if both operands are equal */
    if (rc == 0) {
        memset (result, 0, MAX_DECIMAL_DIGITS);
        *count = 0;
        *sign = +1;
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
/* Divide two decimal byte strings as unsigned decimal numbers       */
/*                                                                   */
/* Input:                                                            */
/*      dec1    A 31-byte area containing the decimal digits of      */
/*              the dividend.  Each byte contains one decimal        */
/*              digit in the low-order 4 bits of the byte.           */
/*      count1  The number of significant digits in the dividend.    */
/*      dec2    A 31-byte area containing the decimal digits of      */
/*              the divisor.  Each byte contains one decimal         */
/*              digit in the low-order 4 bits of the byte.           */
/*      count2  The number of significant digits in the divisor.     */
/* Output:                                                           */
/*      quot    Points to a 31-byte area to contain the quotient     */
/*              digits. One decimal digit is placed in the low-order */
/*              4 bits of each byte.                                 */
/*      rem     Points to a 31-byte area to contain the remainder    */
/*              digits. One decimal digit is placed in the low-order */
/*              4 bits of each byte.                                 */
/* Restrictions:                                                     */
/*      It is assumed that the caller has already verified that      */
/*      divide overflow cannot occur, that the divisor is not zero,  */
/*      and that the dividend has at least one high order zero.      */
/*-------------------------------------------------------------------*/
static void divide_decimal (BYTE *dec1, int count1, BYTE *dec2,
                        int count2, BYTE *quot, BYTE *rem)
{
BYTE   *num1;                           /* -> dividend digits        */
BYTE   *num2;                           /* -> divisor digits         */
int     div, flag, scale;               /* Work areas for algorithm  */
int     index, index1, index2;          /* Work areas for algorithm  */
int     indexq, indexr, temp1, temp2;   /* Work areas for algorithm  */
int     temp3, temp4, temp5, qtest;     /* Work areas for algorithm  */

    /* Clear the result fields */
    memset (quot, 0, MAX_DECIMAL_DIGITS);
    memset (rem, 0, MAX_DECIMAL_DIGITS);

    /* If dividend is zero then return zero quotient and remainder */
    if (count1 == 0)
        return;

    /* If dividend is less than divisor then return zero quotient
       and set remainder equal to dividend */
    if (memcmp(dec1, dec2, MAX_DECIMAL_DIGITS) < 0)
    {
        strcpy(rem, dec1);
        return;
    }

    /* Adjust dividend digit count to give one leading zero */
    count1++;

    /* Point to significant digits of dividend with leading zero */
    num1 = dec1 + MAX_DECIMAL_DIGITS - count1;

    /* Point to significant digits of divisor */
    num2 = dec2 + MAX_DECIMAL_DIGITS - count2;

    scale = 10 / (num2[0] + 1);
    if (scale > 1)
    {
        for (index1 = count1-1, flag = 0; index1 >= 0; index1--)
        {
            div = flag + scale*num1[index1];
            num1[index1] = div % 10;
            flag = div / 10;
        } /* end for(index1) */

        for (index2 = count2-1, flag = 0; index2 >= 0; index2--)
        {
            div = flag + scale*num2[index2];
            num2[index2] = div % 10;
            flag = div / 10;
        } /* end for(index2) */

    } /* end if(scale>1) */

    for (index1 = 0; index1 < count1-count2; index1++)
    {
        if (num2[0] == num1[index1])
            qtest = 9;
        else
        {
            temp2 = (index1+1 < count1) ? num1[index1+1] : 0;
            qtest = (10*num1[index1] + temp2) / num2[0];
        }
        temp2 = num1[index1];
        temp4 = num2[0];
        temp1 = (count2 >= 2) ? num2[1] : 0;
        if (index1+1 < count1)
        {
            temp3 = num1[index1+1];
            temp5 = (index1+2 < count1) ? num1[index1+2] : 0;
        }
        else
        {
            temp3 = 0;
            temp5 = 0;
        }
        while (qtest*temp1 > (10*(10*temp2 + temp3
                            - qtest*temp4) + temp5))
            --qtest;

        for (index = index1+count2, index2 = count2-1, flag = 0;
                index >= index1; index--, index2--)
        {
            if (index2 >= 0)
                flag -= qtest*num2[index2];
            div = flag + num1[index];
            if (div < 0)
            {
                flag = div / 10;
                div %= 10;
                if (div < 0)
                {
                    div += 10;
                    --flag;
                }
            }
            else
                flag = 0;
            num1[index] = div;
        } /* end for(index) */

        indexq = MAX_DECIMAL_DIGITS - (count1-count2) + index1;
        if (flag != 0)
        {
            quot[indexq] = qtest - 1;
            for (index = index1+count2, index2 = count2-1, flag = 0;
                    index >= index1; index--, index2--)
            {
                if (index2 >= 0)
                    flag += num2[index2];
                div = flag + num1[index];
                if (div > 9)
                {
                    div -= 10;
                    flag = 1;
                }
                else
                    flag = 0;
                num1[index] = div;
            } /* end for(index) */
        }
        else
            quot[indexq] = qtest;
    } /* end for(index1) */

    for (index1 = count1-count2,
            indexr = MAX_DECIMAL_DIGITS-count2, flag = 0;
            index1 < count1; index1++, indexr++)
    {
        div = num1[index1] + 10*flag;
        rem[indexr] = div / scale;
        flag = div % scale;
    } /* end for(index1) */

    for (index2 = 0, flag = 0; index2 < count2; index2++)
    {
        div = num2[index2] + 10*flag;
        num2[index2] = div / scale;
        flag = div % scale;
    } /* end for(index2) */

} /* end function divide_decimal */

/*-------------------------------------------------------------------*/
/* Shift and round packed decimal operand                            */
/*                                                                   */
/* Input:                                                            */
/*      addr    Logical address of packed decimal storage operand    */
/*      len     Length minus one of storage operand (range 0-15)     */
/*      arn     Access register number associated with operand       */
/*      regs    CPU register context                                 */
/*      round   Rounding digit                                       */
/*      shift   Shift count (six-bit signed integer)                 */
/* Output:                                                           */
/*      The return value is the condition code:                      */
/*      0=result zero, 1=result -ve, 2=result +ve, 3=overflow        */
/*                                                                   */
/*      A program check may be generated if the operand address      */
/*      causes an addressing, translation, or protection exception,  */
/*      or if the storage operand contains invalid decimal digits    */
/*      or sign, or if the rounding digit is not a decimal digit.    */
/*      Depending on the PSW program mask, a decimal overflow        */
/*      condition during shifting may cause a program check.         */
/*-------------------------------------------------------------------*/
int shift_and_round_packed (U32 addr, int len, int arn, REGS *regs,
                        BYTE round, BYTE shift)
{
int     cc;                             /* Condition code            */
BYTE    dec[MAX_DECIMAL_DIGITS];        /* Work area for operand     */
int     count;                          /* Significant digit counter */
int     sign;                           /* Sign of operand/result    */
int     i, j;                           /* Array subscripts          */
int     d;                              /* Decimal digit             */
int     carry;                          /* Carry indicator           */

    /* Load operand into work area */
    load_decimal (addr, len, arn, regs, dec, &count, &sign);

    /* Program check if rounding digit is invalid */
    if (round > 9)
    {
        program_check (PGM_DATA_EXCEPTION);
        return 3;
    }

    /* Isolate low-order six bits of shift count */
    shift &= 0x3F;

    /* Shift count 0-31 means shift left, 32-63 means shift right */
    if (shift < 32)
    {
        /* Set condition code according to operand sign */
        cc = (count == 0) ? 0 : (sign < 0) ? 1 : 2;

        /* Set cc=3 if non-zero digits will be lost on left shift */
        for (j=0; j < shift; j++)
        {
            if (dec[j] != 0)
                cc = 3;
        }

        /* Shift operand left */
        for (i=0, j=shift; i < MAX_DECIMAL_DIGITS; i++, j++)
            dec[i] = (j < MAX_DECIMAL_DIGITS) ? dec[j] : 0;
    }
    else
    {
        /* Calculate number of digits (1-32) to shift right */
        shift = 64 - shift;

        /* Add the rounding digit to the leftmost of the digits
           to be shifted out and propagate the carry to the left */
        carry = (dec[MAX_DECIMAL_DIGITS - shift] + round) / 10;

        /* Shift operand right */
        for (i=MAX_DECIMAL_DIGITS-1, j=MAX_DECIMAL_DIGITS-1-shift;
                i >= 0; i--, j--)
        {
            d = (j >= 0) ? dec[j] : 0;
            d += carry;
            carry = d / 10;
            d %= 10;
            dec[i] = d;
            if (d != 0)
                count = MAX_DECIMAL_DIGITS - i;
        }

        /* Set condition code according to operand sign */
        cc = (count == 0) ? 0 : (sign < 0) ? 1 : 2;
    }

    /* Make sign positive if result is zero */
    if (cc == 0)
        sign = +1;

    /* Store result into operand location */
    store_decimal (addr, len, arn, regs, dec, sign);

    /* Program check if overflow and PSW program mask is set */
    if (cc == 3 && regs->psw.domask)
    {
        program_check (PGM_DECIMAL_OVERFLOW_EXCEPTION);
    }

    /* Return condition code */
    return cc;

} /* end function shift_and_round_packed */

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
BYTE    dec[MAX_DECIMAL_DIGITS];        /* Work area for operand     */
int     count;                          /* Significant digit counter */
int     sign;                           /* Sign                      */

    /* Load second operand into work area */
    load_decimal (addr2, len2, arn2, regs, dec, &count, &sign);

    /* Set condition code */
    cc = (count == 0) ? 0 : (sign < 1) ? 1 : 2;

    /* Overflow if result exceeds first operand length */
    if (count > (len1+1) * 2 - 1)
        cc = 3;

    /* Set positive sign if result is zero */
    if (count == 0)
        sign = +1;

    /* Store result into first operand location */
    store_decimal (addr1, len1, arn1, regs, dec, sign);

    /* Program check if overflow and PSW program mask is set */
    if (cc == 3 && regs->psw.domask)
    {
        program_check (PGM_DECIMAL_OVERFLOW_EXCEPTION);
    }

    /* Return condition code */
    return cc;

} /* end function zero_and_add_packed */

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
BYTE    dec1[MAX_DECIMAL_DIGITS];       /* Work area for operand 1   */
BYTE    dec2[MAX_DECIMAL_DIGITS];       /* Work area for operand 2   */
int     count1, count2;                 /* Significant digit counters*/
int     sign1, sign2;                   /* Sign of each operand      */
int     rc;                             /* Return code               */

    /* Load operands into work areas */
    load_decimal (addr1, len1, arn1, regs, dec1, &count1, &sign1);
    load_decimal (addr2, len2, arn2, regs, dec2, &count2, &sign2);

    /* Result is equal if both operands are zero */
    if (count1 == 0 && count2 == 0)
        return 0;

    /* Result is low if operand 1 is -ve and operand 2 is +ve */
    if (sign1 < 0 && sign2 > 0)
        return 1;

    /* Result is high if operand 1 is +ve and operand 2 is -ve */
    if (sign1 > 0 && sign2 < 0)
        return 2;

    /* If signs are equal then compare the digits */
    rc = memcmp (dec1, dec2, MAX_DECIMAL_DIGITS);

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
BYTE    dec1[MAX_DECIMAL_DIGITS];       /* Work area for operand 1   */
BYTE    dec2[MAX_DECIMAL_DIGITS];       /* Work area for operand 2   */
BYTE    dec3[MAX_DECIMAL_DIGITS];       /* Work area for result      */
int     count1, count2, count3;         /* Significant digit counters*/
int     sign1, sign2, sign3;            /* Sign of operands & result */

    /* Load operands into work areas */
    load_decimal (addr1, len1, arn1, regs, dec1, &count1, &sign1);
    load_decimal (addr2, len2, arn2, regs, dec2, &count2, &sign2);

    /* Add or subtract operand values */
    if (count2 == 0)
    {
        /* If second operand is zero then result is first operand */
        memcpy (dec3, dec1, MAX_DECIMAL_DIGITS);
        count3 = count1;
        sign3 = sign1;
    }
    else if (count1 == 0)
    {
        /* If first operand is zero then result is second operand */
        memcpy (dec3, dec2, MAX_DECIMAL_DIGITS);
        count3 = count2;
        sign3 = sign2;
    }
    else if (sign1 == sign2)
    {
        /* If signs are equal then add operands */
        add_decimal (dec1, dec2, dec3, &count3);
        sign3 = sign1;
    }
    else
    {
        /* If signs are opposite then subtract operands */
        subtract_decimal (dec1, dec2, dec3, &count3, &sign3);
        if (sign1 < 0) sign3 = -sign3;
    }

    /* Set condition code */
    cc = (count3 == 0) ? 0 : (sign3 < 1) ? 1 : 2;

    /* Overflow if result exceeds first operand length */
    if (count3 > (len1+1) * 2 - 1)
        cc = 3;

    /* Set positive sign if result is zero */
    if (count3 == 0)
        sign3 = 1;

    /* Store result into first operand location */
    store_decimal (addr1, len1, arn1, regs, dec3, sign3);

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
BYTE    dec1[MAX_DECIMAL_DIGITS];       /* Work area for operand 1   */
BYTE    dec2[MAX_DECIMAL_DIGITS];       /* Work area for operand 2   */
BYTE    dec3[MAX_DECIMAL_DIGITS];       /* Work area for result      */
int     count1, count2, count3;         /* Significant digit counters*/
int     sign1, sign2, sign3;            /* Sign of operands & result */

    /* Load operands into work areas */
    load_decimal (addr1, len1, arn1, regs, dec1, &count1, &sign1);
    load_decimal (addr2, len2, arn2, regs, dec2, &count2, &sign2);

    /* Add or subtract operand values */
    if (count2 == 0)
    {
        /* If second operand is zero then result is first operand */
        memcpy (dec3, dec1, MAX_DECIMAL_DIGITS);
        count3 = count1;
        sign3 = sign1;
    }
    else if (count1 == 0)
    {
        /* If first operand is zero then result is -second operand */
        memcpy (dec3, dec2, MAX_DECIMAL_DIGITS);
        count3 = count2;
        sign3 = -sign2;
    }
    else if (sign1 != sign2)
    {
        /* If signs are opposite then add operands */
        add_decimal (dec1, dec2, dec3, &count3);
        sign3 = sign1;
    }
    else
    {
        /* If signs are equal then subtract operands */
        subtract_decimal (dec1, dec2, dec3, &count3, &sign3);
        if (sign1 < 0) sign3 = -sign3;
    }

    /* Set condition code */
    cc = (count3 == 0) ? 0 : (sign3 < 1) ? 1 : 2;

    /* Overflow if result exceeds first operand length */
    if (count3 > (len1+1) * 2 - 1)
        cc = 3;

    /* Set positive sign if result is zero */
    if (count3 == 0)
        sign3 = 1;

    /* Store result into first operand location */
    store_decimal (addr1, len1, arn1, regs, dec3, sign3);

    /* Program check if overflow and PSW program mask is set */
    if (cc == 3 && regs->psw.domask)
    {
        program_check (PGM_DECIMAL_OVERFLOW_EXCEPTION);
    }

    /* Return condition code */
    return cc;

} /* end function subtract_packed */

/*-------------------------------------------------------------------*/
/* Multiply two packed decimal operands                              */
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
/*      The result is stored in operand 1.                           */
/*      The condition code remains unchanged.                        */
/*                                                                   */
/*      A program check may be generated if either logical address   */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, or if either operand causes a data exception      */
/*      because of invalid decimal digits or sign, or if the         */
/*      first operand is store protected, or if the operand lengths  */
/*      or values are such that overflow could potentially occur.    */
/*-------------------------------------------------------------------*/
void multiply_packed (U32 addr1, int len1, int arn1,
                        U32 addr2, int len2, int arn2, REGS *regs)
{
BYTE    dec1[MAX_DECIMAL_DIGITS];       /* Work area for operand 1   */
BYTE    dec2[MAX_DECIMAL_DIGITS];       /* Work area for operand 2   */
BYTE    dec3[MAX_DECIMAL_DIGITS];       /* Work area for result      */
int     count1, count2;                 /* Significant digit counters*/
int     sign1, sign2, sign3;            /* Sign of operands & result */
int     d;                              /* Decimal digit             */
int     i1, i2, i3;                     /* Array subscripts          */
int     carry;                          /* Carry indicator           */

    /* Load operands into work areas */
    load_decimal (addr1, len1, arn1, regs, dec1, &count1, &sign1);
    load_decimal (addr2, len2, arn2, regs, dec2, &count2, &sign2);

    /* Program check if the second operand length exceeds 15 digits
       or is equal to or greater than the first operand length */
    if (len2 > 7 || len2 >= len1)
    {
        program_check (PGM_SPECIFICATION_EXCEPTION);
        return;
    }

    /* Program check if the number of bytes in the second operand
       is less than the number of bytes of high-order zeroes in the
       first operand; this ensures that overflow cannot occur */
    if (len2 > len1 - (count1/2 + 1))
    {
        program_check (PGM_DATA_EXCEPTION);
        return;
    }

    /* Clear the result field */
    memset (dec3, 0, MAX_DECIMAL_DIGITS);

    /* Perform decimal multiplication */
    for (i2 = MAX_DECIMAL_DIGITS-1; i2 >= 0; i2--)
    {
        if (dec2[i2] != 0)
        {
            for (i1 = MAX_DECIMAL_DIGITS - 1, i3 = i2, carry = 0;
                        i1 >= 0; i1--, i3--)
            {
                d = carry + dec1[i1]*dec2[i2] + dec3[i3];
                dec3[i3] = d % 10;
                carry = d / 10;
            }
        }
    } /* end for(i2) */

    /* Result is positive if operand signs are equal, and negative
       if operand signs are opposite, even if result is zero */
    sign3 = (sign1 == sign2) ? 1 : -1;

    /* Store result into first operand location */
    store_decimal (addr1, len1, arn1, regs, dec3, sign3);

} /* end function multiply_packed */

/*-------------------------------------------------------------------*/
/* Divide two packed decimal operands                                */
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
/*      The remainder is placed in the rightmost bytes of operand 1  */
/*      and has a length equal to the length of the second operand.  */
/*      The quotient is placed in the leftmost bytes of operand 1    */
/*      and has a length equal to the length of the first operand    */
/*      minus the length of the second operand.                      */
/*      The condition code remains unchanged.                        */
/*                                                                   */
/*      A program check may be generated if either logical address   */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, or if either operand causes a data exception      */
/*      because of invalid decimal digits or sign, or if the         */
/*      first operand is store protected, or if the operand lengths  */
/*      or values are such that overflow could potentially occur.    */
/*-------------------------------------------------------------------*/
void divide_packed (U32 addr1, int len1, int arn1,
                        U32 addr2, int len2, int arn2, REGS *regs)
{
BYTE    dec1[MAX_DECIMAL_DIGITS];       /* Operand 1 (dividend)      */
BYTE    dec2[MAX_DECIMAL_DIGITS];       /* Operand 2 (divisor)       */
BYTE    quot[MAX_DECIMAL_DIGITS];       /* Quotient                  */
BYTE    rem[MAX_DECIMAL_DIGITS];        /* Remainder                 */
int     count1, count2;                 /* Significant digit counters*/
int     sign1, sign2;                   /* Sign of operands          */
int     signq, signr;                   /* Sign of quotient/remainder*/

    /* Load operands into work areas */
    load_decimal (addr1, len1, arn1, regs, dec1, &count1, &sign1);
    load_decimal (addr2, len2, arn2, regs, dec2, &count2, &sign2);

    /* Program check if the second operand length exceeds 15 digits
       or is equal to or greater than the first operand length */
    if (len2 > 7 || len2 >= len1)
    {
        program_check (PGM_SPECIFICATION_EXCEPTION);
        return;
    }

    /* Program check if second operand value is zero */
    if (count2 == 0)
    {
        program_check (PGM_DECIMAL_DIVIDE_EXCEPTION);
        return;
    }

    /* Perform trial comparison to determine potential overflow.
       The leftmost digit of the divisor is aligned one digit to
       the right of the leftmost dividend digit.  When the divisor,
       so aligned, is less than or equal to the dividend, ignoring
       signs, a divide exception is indicated.  As a result of this
       comparison, it is also certain that the leftmost digit of the
       dividend must be zero, and that the divisor cannot be zero */
    if (memcmp(dec2 + (MAX_DECIMAL_DIGITS - len2*2 - 2),
                dec1 + (MAX_DECIMAL_DIGITS - len1*2 - 1),
                len2*2 + 2) <= 0)
    {
        program_check (PGM_DECIMAL_DIVIDE_EXCEPTION);
        return;
    }

    /* Perform decimal division */
    divide_decimal (dec1, count1, dec2, count2, quot, rem);

    /* Quotient is positive if operand signs are equal, and negative
       if operand signs are opposite, even if quotient is zero */
    signq = (sign1 == sign2) ? 1 : -1;

    /* Remainder sign is same as dividend, even if remainder is zero */
    signr = sign1;

    /* Store remainder into entire first operand location.  The entire
       field will be filled in order to check for store protection.
       Subsequently the quotient will be stored in the leftmost bytes
       of the first operand location, overwriting high order zeroes */
    store_decimal (addr1, len1, arn1, regs, rem, signr);

    /* Store quotient in leftmost bytes of first operand location */
    store_decimal (addr1, len1-len2-1, arn1, regs, quot, signq);

} /* end function divide_packed */

