/* GENERAL.C    (c) Copyright Roger Bowler, 1994-2000                */
/*              ESA/390 CPU Emulator                                 */

/*              (c) Copyright Peter Kuschnerus, 1999 (UPT & CFC)     */

/* Interpretive Execution - (c) Copyright Jan Jaeger, 1999-2000      */

/*-------------------------------------------------------------------*/
/* This module implements all general instructions of the            */
/* S/370 and ESA/390 architectures, as described in the manuals      */
/* GA22-7000-03 System/370 Principles of Operation                   */
/* SA22-7201-06 ESA/390 Principles of Operation                      */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      Corrections to shift instructions by Jay Maynard, Jan Jaeger */
/*      Branch tracing by Jan Jaeger                                 */
/*      Instruction decode by macros - Jan Jaeger                    */
/*      Prevent TOD from going backwards in time - Jan Jaeger        */
/*      Fix STCKE instruction - Bernard van der Helm                 */
/*      Instruction decode rework - Jan Jaeger                       */
/*      Make STCK update the TOD clock - Jay Maynard                 */
/*      Fix address wraparound in MVO - Jan Jaeger                   */
/*      PLO instruction - Jan Jaeger                                 */
/*      Modifications for Interpretive Execution (SIE) by Jan Jaeger */
/*      Extended translation feature - Roger Bowler                  */
/*-------------------------------------------------------------------*/

#include "hercules.h"

#include "opcode.h"

#include "inline.h"

#ifdef IBUF
#include "ibuf.h"
#endif

/*-------------------------------------------------------------------*/
/* 1A   AR    - Add Register                                    [RR] */
/*-------------------------------------------------------------------*/
void zz_add_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    /* Add signed operands and set condition code */
    regs->psw.cc =
            add_signed (&(regs->gpr[r1]),
                    regs->gpr[r1],
                    regs->gpr[r2]);

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        program_interrupt (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
}

        
/*-------------------------------------------------------------------*/
/* 5A   A     - Add                                             [RX] */
/*-------------------------------------------------------------------*/
void zz_add (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = vfetch4 ( effective_addr2, b2, regs );

    /* Add signed operands and set condition code */
    regs->psw.cc =
            add_signed (&(regs->gpr[r1]),
                    regs->gpr[r1],
                    n);

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        program_interrupt (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);

}

/*-------------------------------------------------------------------*/
/* 4A   AH    - Add Halfword                                    [RX] */
/*-------------------------------------------------------------------*/
void zz_add_halfword (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load 2 bytes from operand address */
    n = vfetch2 ( effective_addr2, b2, regs );

    /* Propagate sign bit to leftmost 2 bytes of operand */
    if ( n > 0x7FFF )
        n |= 0xFFFF0000;

    /* Add signed operands and set condition code */
    regs->psw.cc =
            add_signed (&(regs->gpr[r1]),
                    regs->gpr[r1],
                    n);

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        program_interrupt (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);

}


/*-------------------------------------------------------------------*/
/* A7xA AHI   - Add Halfword Immediate                          [RI] */
/*-------------------------------------------------------------------*/
void zz_add_halfword_immediate (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U32     n2;                             /* 16-bit converted to 32    */

    RI(inst, execflag, regs, r1, opcd, n2);

    /* Propagate sign bit to form 32-bit signed operand */
    if ( n2 > 0x7FFF )
        n2 |= 0xFFFF0000;

    /* Add signed operands and set condition code */
    regs->psw.cc =
            add_signed (&(regs->gpr[r1]),
                    regs->gpr[r1],
                    n2);

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        program_interrupt (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);

}


/*-------------------------------------------------------------------*/
/* 1E   ALR   - Add Logical Register                            [RR] */
/*-------------------------------------------------------------------*/
void zz_add_logical_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    /* Add signed operands and set condition code */
    regs->psw.cc =
            add_logical (&(regs->gpr[r1]),
                    regs->gpr[r1],
                    regs->gpr[r2]);
}


/*-------------------------------------------------------------------*/
/* 5E   AL    - Add Logical                                     [RX] */
/*-------------------------------------------------------------------*/
void zz_add_logical (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = vfetch4 ( effective_addr2, b2, regs );

    /* Add signed operands and set condition code */
    regs->psw.cc =
            add_logical (&(regs->gpr[r1]),
                    regs->gpr[r1],
                    n);
}


/*-------------------------------------------------------------------*/
/* 14   NR    - And Register                                    [RR] */
/*-------------------------------------------------------------------*/
void zz_and_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    /* AND second operand with first and set condition code */
    regs->psw.cc = ( regs->gpr[r1] &= regs->gpr[r2] ) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* 54   N     - And                                             [RX] */
/*-------------------------------------------------------------------*/
void zz_and (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = vfetch4 ( effective_addr2, b2, regs );

    /* AND second operand with first and set condition code */
    regs->psw.cc = ( regs->gpr[r1] &= n ) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* 94   NI    - And Immediate                                   [SI] */
/*-------------------------------------------------------------------*/
void zz_and_immediate (BYTE inst[], int execflag, REGS *regs)
{
BYTE    i2;                             /* Immediate byte of opcode  */
int     b1;                             /* Base of effective addr    */
U32     effective_addr1;                /* Effective address         */
BYTE    rbyte;                          /* Result byte               */

    SI(inst, execflag, regs, i2, b1, effective_addr1);

    /* Fetch byte from operand address */
    rbyte = vfetchb ( effective_addr1, b1, regs );

    /* AND with immediate operand */
    rbyte &= i2;

    /* Store result at operand address */
    vstoreb ( rbyte, effective_addr1, b1, regs );

    /* Set condition code */
    regs->psw.cc = rbyte ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* D4   NC    - And Character                                   [SS] */
/*-------------------------------------------------------------------*/
void zz_and_character (BYTE inst[], int execflag, REGS *regs)
{
BYTE    l;                              /* Length byte               */
int     b1, b2;                         /* Base register numbers     */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
U32     abs1, abs2;                     /* Absolute addresses        */
U32     npv1, npv2;                     /* Next page virtual addrs   */
U32     npa1 = 0, npa2 = 0;             /* Next page absolute addrs  */
int     i;                              /* Loop counter              */
int     cc = 0;                         /* Condition code            */
BYTE    byte1, byte2;                   /* Operand bytes             */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */
#ifdef IBUF
U32     habs1;
BYTE    l1;
#endif

    SS_L(inst, execflag, regs, l, b1, effective_addr1,
                                  b2, effective_addr2);

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Translate addresses of leftmost operand bytes */
    abs1 = logical_to_abs (effective_addr1, b1, regs, ACCTYPE_WRITE, akey);
    abs2 = logical_to_abs (effective_addr2, b2, regs, ACCTYPE_READ, akey);

    /* Calculate page addresses of rightmost operand bytes */
    npv1 = (effective_addr1 + l) & ADDRESS_MAXWRAP(regs);
    npv1 &= STORAGE_KEY_PAGEMASK;
    npv2 = (effective_addr2 + l) & ADDRESS_MAXWRAP(regs);
    npv2 &= STORAGE_KEY_PAGEMASK;

    /* Translate next page addresses if page boundary crossed */
    if (npv1 != (effective_addr1 & STORAGE_KEY_PAGEMASK))
        npa1 = logical_to_abs (npv1, b1, regs, ACCTYPE_WRITE, akey);
    if (npv2 != (effective_addr2 & STORAGE_KEY_PAGEMASK))
        npa2 = logical_to_abs (npv2, b2, regs, ACCTYPE_READ, akey);

#ifdef IBUF
    habs1 = abs1;
#endif

    /* Process operands from left to right */
    for ( i = 0; i <= l; i++ )
    {
        /* Fetch a byte from each operand */
        byte1 = sysblk.mainstor[abs1];
        byte2 = sysblk.mainstor[abs2];

        /* Copy low digit of operand 2 to operand 1 */
        byte1 &=  byte2;

        /* Set condition code 1 if result is non-zero */
        if (byte1 != 0) cc = 1;

        /* Store the result in the destination operand */
        sysblk.mainstor[abs1] = byte1;

        /* Increment first operand address */
        effective_addr1++;
        effective_addr1 &= ADDRESS_MAXWRAP(regs);
        abs1++;

        /* Adjust absolute address if page boundary crossed */
        if ((effective_addr1 & STORAGE_KEY_BYTEMASK) == 0x000)
            abs1 = npa1;

        /* Increment second operand address */
        effective_addr2++;
        effective_addr2 &= ADDRESS_MAXWRAP(regs);
        abs2++;

        /* Adjust absolute address if page boundary crossed */
        if ((effective_addr2 & STORAGE_KEY_BYTEMASK) == 0x000)
            abs2 = npa2;

    } /* end for(i) */

    regs->psw.cc = cc;

#ifdef IBUF
    if (!npa1)
        FRAG_INVALIDATE(habs1, l)
    else
    {
        l1 = (habs1 & STORAGE_KEY_PAGEMASK) +
              STORAGE_KEY_PAGESIZE - habs1;
        FRAG_INVALIDATE(habs1, l1);
        FRAG_INVALIDATE(npa1, l - l1);
    }
#endif

}

/*-------------------------------------------------------------------*/
/* 05   BALR  - Branch and Link Register                        [RR] */
/*-------------------------------------------------------------------*/
void zz_branch_and_link_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     newia;                          /* New instruction address   */

    RR(inst, execflag, regs, r1, r2);

    /* Compute the branch address from the R2 operand */
    newia = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

#if defined(FEATURE_TRACING)
    /* Add a branch trace entry to the trace table */
    if ((regs->cr[12] & CR12_BRTRACE) && (r2 != 0))
        regs->cr[12] = trace_br (regs->psw.amode,
                                    regs->gpr[r2], regs);
#endif /*defined(FEATURE_TRACING)*/

    /* Save the link information in the R1 operand */
    regs->gpr[r1] =
        ( regs->psw.amode ) ?
            0x80000000 | regs->psw.ia :
            (regs->psw.ilc << 29) | (regs->psw.cc << 28)
            | (regs->psw.fomask << 27)
            | (regs->psw.domask << 26)
            | (regs->psw.eumask << 25)
            | (regs->psw.sgmask << 24)
            | regs->psw.ia;

    /* Execute the branch unless R2 specifies register 0 */
    if ( r2 != 0 )
    {
        regs->psw.ia = newia;
        GET_FRAGENTRY(regs);
    }
}


/*-------------------------------------------------------------------*/
/* 45   BAL   - Branch and Link                                 [RX] */
/*-------------------------------------------------------------------*/
void zz_branch_and_link (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Save the link information in the R1 operand */
    regs->gpr[r1] =
        ( regs->psw.amode ) ?
            0x80000000 | regs->psw.ia :
            (regs->psw.ilc << 29) | (regs->psw.cc << 28)
            | (regs->psw.fomask << 27)
            | (regs->psw.domask << 26)
            | (regs->psw.eumask << 25)
            | (regs->psw.sgmask << 24)
            | regs->psw.ia;

    regs->psw.ia = effective_addr2;
    GET_FRAGENTRY(regs);

}


/*-------------------------------------------------------------------*/
/* 0D   BASR  - Branch and Save Register                        [RR] */
/*-------------------------------------------------------------------*/
void zz_branch_and_save_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     newia;                          /* New instruction address   */

    RR(inst, execflag, regs, r1, r2);

    /* Compute the branch address from the R2 operand */
    newia = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

#if defined(FEATURE_TRACING)
    /* Add a branch trace entry to the trace table */
    if ((regs->cr[12] & CR12_BRTRACE) && (r2 != 0))
        regs->cr[12] = trace_br (regs->psw.amode,
                                    regs->gpr[r2], regs);
#endif /*defined(FEATURE_TRACING)*/

    /* Save the link information in the R1 operand */
    if ( regs->psw.amode )
        regs->gpr[r1] = 0x80000000 | regs->psw.ia;
    else
        regs->gpr[r1] = regs->psw.ia & 0x00FFFFFF;

    /* Execute the branch unless R2 specifies register 0 */
    if ( r2 != 0 )
    {
        regs->psw.ia = newia;
        GET_FRAGENTRY(regs);
    }
}


/*-------------------------------------------------------------------*/
/* 4D   BAS   - Branch and Save                                 [RX] */
/*-------------------------------------------------------------------*/
void zz_branch_and_save (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Save the link information in the R1 register */
    if ( regs->psw.amode )
        regs->gpr[r1] = 0x80000000 | regs->psw.ia;
    else
        regs->gpr[r1] = regs->psw.ia & 0x00FFFFFF;

    regs->psw.ia = effective_addr2;
    GET_FRAGENTRY(regs);
}

/*-------------------------------------------------------------------*/
/* 0C   BASSM - Branch and Save and Set Mode                    [RR] */
/*-------------------------------------------------------------------*/
void zz_branch_and_save_and_set_mode (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     newia;                          /* New instruction address   */

    RR(inst, execflag, regs, r1, r2);

    SIE_MODE_XA(regs);

    /* Compute the branch address from the R2 operand */
    newia = regs->gpr[r2];

#if defined(FEATURE_TRACING)
    /* Add a branch trace entry to the trace table */
    if ((regs->cr[12] & CR12_BRTRACE) && (r2 != 0))
        regs->cr[12] = trace_br (regs->gpr[r2] & 0x80000000,
                                    regs->gpr[r2], regs);
#endif /*defined(FEATURE_TRACING)*/

    /* Save the link information in the R1 operand */
    if ( regs->psw.amode )
        regs->gpr[r1] = 0x80000000 | regs->psw.ia;
    else
        regs->gpr[r1] = regs->psw.ia & 0x00FFFFFF;

    /* Set mode and branch to address specified by R2 operand */
    if ( r2 != 0 )
    {
        if ( newia & 0x80000000 )
        {
            regs->psw.amode = 1;
            regs->psw.ia = newia & 0x7FFFFFFF;
            GET_FRAGENTRY(regs);
        }
        else
        {
            regs->psw.amode = 0;
            regs->psw.ia = newia & 0x00FFFFFF;
            GET_FRAGENTRY(regs);
        }
    }
}


/*-------------------------------------------------------------------*/
/* 0B   BSM   - Branch and Set Mode                             [RR] */
/*-------------------------------------------------------------------*/
void zz_branch_and_set_mode (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     newia;                          /* New instruction address   */

    RR(inst, execflag, regs, r1, r2);

    SIE_MODE_XA(regs);

    /* Compute the branch address from the R2 operand */
    newia = regs->gpr[r2];

    /* Insert addressing mode into bit 0 of R1 operand */
    if ( r1 != 0 )
    {
        regs->gpr[r1] &= 0x7FFFFFFF;
        if ( regs->psw.amode )
            regs->gpr[r1] |= 0x80000000;
    }

    /* Set mode and branch to address specified by R2 operand */
    if ( r2 != 0 )
    {
        if ( newia & 0x80000000 )
        {
            regs->psw.amode = 1;
            regs->psw.ia = newia & 0x7FFFFFFF;
            GET_FRAGENTRY(regs);
        }
        else
        {
            regs->psw.amode = 0;
            regs->psw.ia = newia & 0x00FFFFFF;
            GET_FRAGENTRY(regs);
        }
    }
}


/*-------------------------------------------------------------------*/
/* 07   BCR   - Branch on Condition Register                    [RR] */
/*-------------------------------------------------------------------*/
void zz_branch_on_condition_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     newia;                          /* New instruction address   */
int     m;                              /* Condition code mask       */

    RR(inst, execflag, regs, r1, r2);

    /* Compute the branch address from the R2 operand */
    newia = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Generate a bit mask from the condition code value */
    m = ( regs->psw.cc == 0 ) ? 0x08
      : ( regs->psw.cc == 1 ) ? 0x04
      : ( regs->psw.cc == 2 ) ? 0x02 : 0x01;

    /* Branch if R1 mask bit is set and R2 is not register 0 */
    if ( (r1 & m) != 0 && r2 != 0 )
    {
        regs->psw.ia = newia;
        GET_FRAGENTRY(regs);
    }
    else
        /* Perform serialization and checkpoint synchronization if
           the mask is all ones and the register is all zeroes */
        if ( r1 == 0x0F && r2 == 0 )
        {
            PERFORM_SERIALIZATION (regs);
            PERFORM_CHKPT_SYNC (regs);
        }

}


/*-------------------------------------------------------------------*/
/* 47   BC    - Branch on Condition                             [RX] */
/*-------------------------------------------------------------------*/
void zz_branch_on_condition (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
int     m;                              /* Condition code mask       */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Generate a bit mask from the condition code value */
    m = ( regs->psw.cc == 0 ) ? 0x08
      : ( regs->psw.cc == 1 ) ? 0x04
      : ( regs->psw.cc == 2 ) ? 0x02 : 0x01;

    /* Branch to operand address if r1 mask bit is set */
    if ( (r1 & m) != 0 )
    {
        regs->psw.ia = effective_addr2;
        GET_FRAGENTRY(regs);
    }

}


/*-------------------------------------------------------------------*/
/* 06   BCTR  - Branch on Count Register                        [RR] */
/*-------------------------------------------------------------------*/
void zz_branch_on_count_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     newia;                          /* New instruction address   */

    RR(inst, execflag, regs, r1, r2);

    /* Compute the branch address from the R2 operand */
    newia = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Subtract 1 from the R1 operand and branch if result
           is non-zero and R2 operand is not register zero */
    if ( --(regs->gpr[r1]) && r2 != 0 )
    {
        regs->psw.ia = newia;
        GET_FRAGENTRY(regs);
    }
   
}


/*-------------------------------------------------------------------*/
/* 46   BCT   - Branch on Count                                 [RX] */
/*-------------------------------------------------------------------*/
void zz_branch_on_count (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Subtract 1 from the R1 operand and branch if non-zero */
    if ( --(regs->gpr[r1]) )
    {
        regs->psw.ia = effective_addr2;
        GET_FRAGENTRY(regs);
    }

}


/*-------------------------------------------------------------------*/
/* 86   BXH   - Branch on Index High                            [RS] */
/*-------------------------------------------------------------------*/
void zz_branch_on_index_high (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
S32     i, j;                           /* Integer work areas        */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Load the increment value from the R3 register */
    i = (S32)regs->gpr[r3];

    /* Load compare value from R3 (if R3 odd), or R3+1 (if even) */
    j = (r3 & 1) ? (S32)regs->gpr[r3] : (S32)regs->gpr[r3+1];

    /* Add the increment value to the R1 register */
    (S32)regs->gpr[r1] += i;

    /* Branch if result compares high */
    if ( (S32)regs->gpr[r1] > j )
    {
        regs->psw.ia = effective_addr2;
        GET_FRAGENTRY(regs);
    }

}


/*-------------------------------------------------------------------*/
/* 87   BXLE  - Branch on Index Low or Equal                    [RS] */
/*-------------------------------------------------------------------*/
void zz_branch_on_index_low_or_equal (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
S32     i, j;                           /* Integer work areas        */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Load the increment value from the R3 register */
    i = regs->gpr[r3];

    /* Load compare value from R3 (if R3 odd), or R3+1 (if even) */
    j = (r3 & 1) ? (S32)regs->gpr[r3] : (S32)regs->gpr[r3+1];

    /* Add the increment value to the R1 register */
    (S32)regs->gpr[r1] += i;

    /* Branch if result compares low or equal */
    if ( (S32)regs->gpr[r1] <= j )
    {
        regs->psw.ia = effective_addr2;
        GET_FRAGENTRY(regs);
    }

}


/*-------------------------------------------------------------------*/
/* A7x4 BRC   - Branch Relative on Condition                    [RI] */
/*-------------------------------------------------------------------*/
void zz_branch_relative_on_condition (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */
int     j;                              /* Work                      */

    RI(inst, execflag, regs, r1, opcd, i2);

    /* Generate a bit mask from the condition code value */
    j = ( regs->psw.cc == 0 ) ? 0x08
      : ( regs->psw.cc == 1 ) ? 0x04
      : ( regs->psw.cc == 2 ) ? 0x02 : 0x01;

    /* Branch if R1 mask bit is set */
    if ( (r1 & j) != 0 )
    {
        /* Calculate the relative branch address */
        regs->psw.ia = (regs->psw.ia - 4 + 2*(S16)i2)
                                              & ADDRESS_MAXWRAP(regs);
        GET_FRAGENTRY(regs);
    }
}


/*-------------------------------------------------------------------*/
/* A7x5 BRAS  - Branch Relative And Save                        [RI] */
/*-------------------------------------------------------------------*/
void zz_branch_relative_and_save (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);


    /* Save the link information in the R1 operand */
    if ( regs->psw.amode )
        regs->gpr[r1] = 0x80000000 | regs->psw.ia;
    else
        regs->gpr[r1] = regs->psw.ia & 0x00FFFFFF;

    /* Calculate the relative branch address */
    regs->psw.ia = (regs->psw.ia - 4 + 2*(S16)i2)
                                              & ADDRESS_MAXWRAP(regs);
    GET_FRAGENTRY(regs);
}


/*-------------------------------------------------------------------*/
/* A7x6 BRCT  - Branch Relative on Count                        [RI] */
/*-------------------------------------------------------------------*/
void zz_branch_relative_on_count (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    /* Subtract 1 from the R1 operand and branch if non-zero */
    if ( --(regs->gpr[r1]) )
    {
        regs->psw.ia = (regs->psw.ia - 4 + 2*(S16)i2)
                                              & ADDRESS_MAXWRAP(regs);
        GET_FRAGENTRY(regs);
    }
}


/*-------------------------------------------------------------------*/
/* 84   BRXH  - Branch Relative on Index High                  [RSI] */
/*-------------------------------------------------------------------*/
void zz_branch_relative_on_index_high (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
U32     i2;                             /* 16-bit operand            */
int     i,j;                            /* Integer workareas         */

    RI(inst, execflag, regs, r1, r3, i2);

    /* Load the increment value from the R3 register */
    i = (S32)regs->gpr[r3];

    /* Load compare value from R3 (if R3 odd), or R3+1 (if even) */
    j = (r3 & 1) ? (S32)regs->gpr[r3] : (S32)regs->gpr[r3+1];

    /* Add the increment value to the R1 register */
    (S32)regs->gpr[r1] += i;

    /* Branch if result compares high */
    if ( (S32)regs->gpr[r1] > j )
    {
        regs->psw.ia = (regs->psw.ia - 4 + 2*(S16)i2)
                                              & ADDRESS_MAXWRAP(regs);
        GET_FRAGENTRY(regs);
    }

}


/*-------------------------------------------------------------------*/
/* 85   BRXLE - Branch Relative on Index Low or Equal          [RSI] */
/*-------------------------------------------------------------------*/
void zz_branch_relative_on_index_low_or_equal (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
U32     i2;                             /* 16-bit operand            */
int     i,j;                            /* Integer workareas         */

    RI(inst, execflag, regs, r1, r3, i2);

    /* Load the increment value from the R3 register */
    i = (S32)regs->gpr[r3];

    /* Load compare value from R3 (if R3 odd), or R3+1 (if even) */
    j = (r3 & 1) ? (S32)regs->gpr[r3] : (S32)regs->gpr[r3+1];

    /* Add the increment value to the R1 register */
    (S32)regs->gpr[r1] += i;

    /* Branch if result compares low or equal */
    if ( (S32)regs->gpr[r1] <= j )
    {
        regs->psw.ia = (regs->psw.ia - 4 + 2*(S16)i2)
                                              & ADDRESS_MAXWRAP(regs);
        GET_FRAGENTRY(regs);
    }

}


/*-------------------------------------------------------------------*/
/* B241 CKSM  - Checksum                                       [RRE] */
/*-------------------------------------------------------------------*/
void zz_checksum (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     addr2;                          /* Address of second operand */
U32     len;                            /* Operand length            */
int     i, j;                           /* Loop counters             */
int     cc = 0;                         /* Condition code            */
U32     n;                              /* Word loaded from operand  */
U64     dreg;                           /* Checksum accumulator      */

    RRE(inst, execflag, regs, r1, r2);

    ODD_CHECK(r2, regs);

    /* Obtain the second operand address and length from R2, R2+1 */
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);
    len = regs->gpr[r2+1];

    /* Initialize the checksum from the first operand register */
    dreg = regs->gpr[r1];

    /* Process each fullword of second operand */
    for ( i = 0; len > 0 ; i++ )
    {
        /* If 1024 words have been processed, exit with cc=3 */
        if ( i >= 1024 )
        {
            cc = 3;
            break;
        }

        /* Fetch fullword from second operand */
        if (len >= 4)
        {
            n = vfetch4 ( addr2, r2, regs );
            addr2 += 4;
            addr2 &= ADDRESS_MAXWRAP(regs);
            len -= 4;
        }
        else
        {
            /* Fetch final 1, 2, or 3 bytes and pad with zeroes */
            for (j = 0, n = 0; j < 4; j++)
            {
                n <<= 8;
                if (len > 0)
                {
                    n |= vfetchb ( addr2, r2, regs );
                    addr2++;
                    addr2 &= ADDRESS_MAXWRAP(regs);
                    len--;
                }
            } /* end for(j) */
        }

        /* Accumulate the fullword into the checksum */
        dreg += n;

        /* Carry 32 bit overflow into bit 31 */
        if (dreg > 0xFFFFFFFFULL)
        {
            dreg &= 0xFFFFFFFFULL;
            dreg++;
        }
    } /* end for(i) */

    /* Load the updated checksum into the R1 register */
    regs->gpr[r1] = dreg;

    /* Update the operand address and length registers */
    regs->gpr[r2] = addr2;
    regs->gpr[r2+1] = len;

    /* Set condition code 0 or 3 */
    regs->psw.cc = cc;

}


/*-------------------------------------------------------------------*/
/* 19   CR    - Compare Register                                [RR] */
/*-------------------------------------------------------------------*/
void zz_compare_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    /* Compare signed operands and set condition code */
    regs->psw.cc =
                (S32)regs->gpr[r1] < (S32)regs->gpr[r2] ? 1 :
                (S32)regs->gpr[r1] > (S32)regs->gpr[r2] ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* 59   C     - Compare                                         [RX] */
/*-------------------------------------------------------------------*/
void zz_compare (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = vfetch4 ( effective_addr2, b2, regs );

    /* Compare signed operands and set condition code */
    regs->psw.cc =
            (S32)regs->gpr[r1] < (S32)n ? 1 :
            (S32)regs->gpr[r1] > (S32)n ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* B21A CFC   - Compare and Form Codeword                        [S] */
/*              (c) Copyright Peter Kuschnerus, 1999                 */
/*-------------------------------------------------------------------*/
void zz_compare_and_form_codeword (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
int     cc;                             /* Condition code            */
int     ar1 = 1;                        /* Access register number    */
U32     addr1, addr3;                   /* Operand addresses         */
U32     work;                           /* 32-bit workarea           */
U16     index_limit;                    /* Index limit               */
U16     index;                          /* Index                     */
U16     temp_hw;                        /* TEMPHW                    */
U16     op1, op3;                       /* 16-bit operand values     */
BYTE    operand_control;                /* Operand control bit       */

    S(inst, execflag, regs, b2, effective_addr2);

    /* Check GR1, GR2, GR3 even */
    if ( regs->gpr[1] & 1 || regs->gpr[2] & 1 || regs->gpr[3] & 1 )
        program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Get index limit and operand-control bit */
    index_limit = effective_addr2 & 0x7FFE;
    operand_control = effective_addr2 & 1;

    /* Loop until break */
    for (;;)
    {
        /* Check index limit */
        index = regs->gpr[2];
        if ( index > index_limit )
        {
            regs->gpr[2] = regs->gpr[3] | 0x80000000;
            regs->psw.cc = 0;
            return;
        }

        /* fetch 1st operand */
        addr1 = (regs->gpr[1] + index) & ADDRESS_MAXWRAP(regs);
        op1 = vfetch2 ( addr1, ar1, regs );

        /* fetch 3rd operand */
        addr3 = (regs->gpr[3] + index) & ADDRESS_MAXWRAP(regs);
        op3 = vfetch2 ( addr3, ar1, regs );

        regs->gpr[2] += 2;

        /* Compare oprerands */
        if ( op1 > op3 )
        {
            if ( operand_control )
            {
                temp_hw = op3;
                cc = 1;
            }
            else
            {
                temp_hw = ~op1;

                /* Exchange GR1 and GR3 */
                work = regs->gpr[1];
                regs->gpr[1] = regs->gpr[3];
                regs->gpr[3] = work;

                cc = 2;
            }

            /* end of loop */
            break;
        }
        else if ( op1 < op3 )
        {
            if ( operand_control )
            {
                temp_hw = op1;

                /* Exchange GR1 and GR3 */
                work = regs->gpr[1];
                regs->gpr[1] = regs->gpr[3];
                regs->gpr[3] = work;

                cc = 2;
            }
            else
            {
                temp_hw = ~op3;
                cc = 1;
            }

            /* end of loop */
            break;
        }
        /* if equal continue */
    }

    regs->gpr[2] = (regs->gpr[2] << 16) | temp_hw;
    regs->psw.cc = cc;
}


/*-------------------------------------------------------------------*/
/* BA   CS    - Compare and Swap                                [RS] */
/*-------------------------------------------------------------------*/
void zz_compare_and_swap (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
U32     n;                              /* 32-bit operand value      */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    FW_CHECK(effective_addr2, regs);

    /* Perform serialization before starting operation */
    PERFORM_SERIALIZATION (regs);

    /* Obtain main-storage access lock */
    OBTAIN_MAINLOCK(regs);

    /* Load second operand from operand address  */
    n = vfetch4 ( effective_addr2, b2, regs );

    /* Compare operand with R1 register contents */
    if ( regs->gpr[r1] == n )
    {
        /* If equal, store R3 at operand location and set cc=0 */
        vstore4 ( regs->gpr[r3], effective_addr2, b2, regs );
        regs->psw.cc = 0;
    }
    else
    {
        /* If unequal, load R1 from operand and set cc=1 */
        regs->gpr[r1] = n;
        regs->psw.cc = 1;
    }

    /* Release main-storage access lock */
    RELEASE_MAINLOCK(regs);

    /* Perform serialization after completing operation */
    PERFORM_SERIALIZATION (regs);

#if MAX_CPU_ENGINES > 1
    /* It this is a failed compare and swap
       and there is more then 1 CPU in the configuration
       and there is no broadcast synchronization in progress
       then call the hypervisor to end this timeslice,
       this to prevent this virtual CPU monopolizing
       the physical CPU on a spinlock */
    if(regs->psw.cc && sysblk.numcpu > 1 && sysblk.brdcstncpu == 0)
        usleep(1L);
#endif MAX_CPU_ENGINES > 1

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    if(regs->sie_state && (regs->siebk->ic[0] & SIE_IC0_CS1))
        longjmp(regs->progjmp, SIE_INTERCEPT_INST);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

}

/*-------------------------------------------------------------------*/
/* BB   CDS   - Compare Double and Swap                         [RS] */
/*-------------------------------------------------------------------*/
void zz_compare_double_and_swap (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
U32     n1, n2;                         /* 32-bit operand values     */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    ODD2_CHECK(r1, r3, regs);
   
    DW_CHECK(effective_addr2, regs);

    /* Perform serialization before starting operation */
    PERFORM_SERIALIZATION (regs);

    /* Obtain main-storage access lock */
    OBTAIN_MAINLOCK(regs);

    /* Load second operand from operand address  */
    n1 = vfetch4 ( effective_addr2, b2, regs );
    n2 = vfetch4 ( effective_addr2 + 4, b2, regs );

    /* Compare doubleword operand with R1:R1+1 register contents */
    if ( regs->gpr[r1] == n1 && regs->gpr[r1+1] == n2 )
    {
        /* If equal, store R3:R3+1 at operand location and set cc=0 */
        vstore4 ( regs->gpr[r3], effective_addr2, b2, regs );
        vstore4 ( regs->gpr[r3+1], effective_addr2 + 4, b2, regs );
        regs->psw.cc = 0;
    }
    else
    {
        /* If unequal, load R1:R1+1 from operand and set cc=1 */
        regs->gpr[r1] = n1;
        regs->gpr[r1+1] = n2;
        regs->psw.cc = 1;
    }

    /* Release main-storage access lock */
    RELEASE_MAINLOCK(regs);

    /* Perform serialization after completing operation */
    PERFORM_SERIALIZATION (regs);

#if MAX_CPU_ENGINES > 1
    /* It this is a failed compare and swap
       and there is more then 1 CPU in the configuration
       and there is no broadcast synchronization in progress
       then call the hypervisor to end this timeslice,
       this to prevent this virtual CPU monopolizing
       the physical CPU on a spinlock */
    if(regs->psw.cc && sysblk.numcpu > 1 && sysblk.brdcstncpu == 0)
        usleep(1L);
#endif MAX_CPU_ENGINES > 1

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    if(regs->sie_state && (regs->siebk->ic[0] & SIE_IC0_CDS1))
        longjmp(regs->progjmp, SIE_INTERCEPT_INST);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

}


/*-------------------------------------------------------------------*/
/* 49   CH    - Compare Halfword                                [RX] */
/*-------------------------------------------------------------------*/
void zz_compare_halfword (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load rightmost 2 bytes of comparand from operand address */
    n = vfetch2 ( effective_addr2, b2, regs );

    /* Propagate sign bit to leftmost 2 bytes of comparand */
    if ( n > 0x7FFF )
        n |= 0xFFFF0000;

    /* Compare signed operands and set condition code */
    regs->psw.cc =
            (S32)regs->gpr[r1] < (S32)n ? 1 :
            (S32)regs->gpr[r1] > (S32)n ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* A7xE CHI   - Compare Halfword Immediate                      [RI] */
/*-------------------------------------------------------------------*/
void zz_compare_halfword_immediate (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U32     i2;                             /* 16-bit operand            */

    RI(inst, execflag, regs, r1, opcd, i2);

    /* Compare signed operands and set condition code */
    regs->psw.cc =
            (S32)regs->gpr[r1] < (S16)i2 ? 1 :
            (S32)regs->gpr[r1] > (S16)i2 ? 2 : 0;

}


/*-------------------------------------------------------------------*/
/* 15   CLR   - Compare Logical Register                        [RR] */
/*-------------------------------------------------------------------*/
void zz_compare_logical_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    /* Compare unsigned operands and set condition code */
    regs->psw.cc = regs->gpr[r1] < regs->gpr[r2] ? 1 :
                   regs->gpr[r1] > regs->gpr[r2] ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* 55   CL    - Compare Logical                                 [RX] */
/*-------------------------------------------------------------------*/
void zz_compare_logical (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = vfetch4 ( effective_addr2, b2, regs );

    /* Compare unsigned operands and set condition code */
    regs->psw.cc = regs->gpr[r1] < n ? 1 :
                   regs->gpr[r1] > n ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* 95   CLI   - Compare Logical Immediate                       [SI] */
/*-------------------------------------------------------------------*/
void zz_compare_logical_immediate (BYTE inst[], int execflag, REGS *regs)
{
BYTE    i2;                             /* Immediate byte            */
int     b1;                             /* Base of effective addr    */
U32     effective_addr1;                /* Effective address         */
BYTE    cbyte;                          /* Compare byte              */

    SI(inst, execflag, regs, i2, b1, effective_addr1);

    /* Fetch byte from operand address */
    cbyte = vfetchb ( effective_addr1, b1, regs );

    /* Compare with immediate operand and set condition code */
    regs->psw.cc = (cbyte < i2) ? 1 :
                   (cbyte > i2) ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* D5   CLC   - Compare Logical Character                       [SS] */
/*-------------------------------------------------------------------*/
void zz_compare_logical_character (BYTE inst[], int execflag, REGS *regs)
{
BYTE    l;                              /* Lenght byte               */
int     b1, b2;                         /* Base registers            */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
int     rc;                             /* Return code               */
BYTE    cwork1[256];                    /* Character work areas      */
BYTE    cwork2[256];                    /* Character work areas      */

    SS_L(inst, execflag, regs, l, b1, effective_addr1,
                                  b2, effective_addr2);

    /* Fetch first and second operands into work areas */
    vfetchc ( cwork1, l, effective_addr1, b1, regs );
    vfetchc ( cwork2, l, effective_addr2, b2, regs );

    /* Compare first operand with second operand */
    rc = memcmp (cwork1, cwork2, l + 1);

    /* Set the condition code */
    regs->psw.cc = (rc == 0) ? 0 : (rc < 0) ? 1 : 2;
}


/*-------------------------------------------------------------------*/
/* BD   CLM   - Compare Logical Characters under Mask           [RS] */
/*-------------------------------------------------------------------*/
void zz_compare_logical_characters_under_mask (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
U32     n;                              /* 32-bit operand values     */
int     cc = 0;                         /* Condition code            */
BYTE    sbyte,
        dbyte;                          /* Byte work areas           */
int     i;                              /* Integer work areas        */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Load value from register */
    n = regs->gpr[r1];

    /* Compare characters in register with operand characters */
    for ( i = 0; i < 4; i++ )
    {
        /* Test mask bit corresponding to this character */
        if ( r3 & 0x08 )
        {
            /* Fetch character from register and operand */
            dbyte = n >> 24;
            sbyte = vfetchb ( effective_addr2++, b2, regs );

            /* Compare bytes, set condition code if unequal */
            if ( dbyte != sbyte )
            {
                cc = (dbyte < sbyte) ? 1 : 2;
                break;
            } /* end if */
        }

        /* Shift mask and register for next byte */
        r3 <<= 1;
        n <<= 8;

    } /* end for(i) */

    /* Update the condition code */
    regs->psw.cc = cc;
}


/*-------------------------------------------------------------------*/
/* 0F   CLCL  - Compare Logical Long                            [RR] */
/*-------------------------------------------------------------------*/
void zz_compare_logical_long (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
int     cc = 0;                         /* Condition code            */
U32     addr1, addr2;                   /* Operand addresses         */
U32     len1, len2;                     /* Operand lengths           */
BYTE    byte1, byte2;                   /* Operand bytes             */
BYTE    pad;                            /* Padding byte              */

    RR(inst, execflag, regs, r1, r2);

    ODD2_CHECK(r1, r2, regs);

    /* Determine the destination and source addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Load padding byte from bits 0-7 of R2+1 register */
    pad = regs->gpr[r2+1] >> 24;

    /* Load operand lengths from bits 8-31 of R1+1 and R2+1 */
    len1 = regs->gpr[r1+1] & 0x00FFFFFF;
    len2 = regs->gpr[r2+1] & 0x00FFFFFF;

    /* Process operands from left to right */
    while (len1 > 0 || len2 > 0)
    {
        /* Fetch a byte from each operand, or use padding byte */
        byte1 = (len1 > 0) ? vfetchb (addr1, r1, regs) : pad;
        byte2 = (len2 > 0) ? vfetchb (addr2, r2, regs) : pad;

        /* Compare operand bytes, set condition code if unequal */
        if (byte1 != byte2)
        {
            cc = (byte1 < byte2) ? 1 : 2;
            break;
        } /* end if */

        /* Update the first operand address and length */
        if (len1 > 0)
        {
            addr1++;
            addr1 &= ADDRESS_MAXWRAP(regs);
            len1--;
        }

        /* Update the second operand address and length */
        if (len2 > 0)
        {
            addr2++;
            addr2 &= ADDRESS_MAXWRAP(regs);
            len2--;
        }

        /* The instruction can be interrupted when a CPU determined
           number of bytes have been processed.  The instruction
           address will be backed up, and the instruction will
           be re-executed.  This is consistent with operation
           under a hypervisor such as LPAR or VM.                *JJ */
        if ((len1 + len2 > 255) && !((addr1 - len2) & 0xFFF))
        {
            regs->psw.ia -= regs->psw.ilc;
            regs->psw.ia &= ADDRESS_MAXWRAP(regs);
            GET_FRAGENTRY(regs);
            break;
        }

    } /* end while(len1||len2) */

    /* Update the registers */
    regs->gpr[r1] = addr1;
    regs->gpr[r1+1] &= 0xFF000000;
    regs->gpr[r1+1] |= len1;
    regs->gpr[r2] = addr2;
    regs->gpr[r2+1] &= 0xFF000000;
    regs->gpr[r2+1] |= len2;

    regs->psw.cc = cc;

}


/*-------------------------------------------------------------------*/
/* A9   CLCLE - Compare Logical Long Extended                   [RS] */
/*-------------------------------------------------------------------*/
void zz_compare_logical_long_extended (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
int     i;                              /* Loop counter              */
int     cc = 0;                         /* Condition code            */
U32     addr1, addr2;                   /* Operand addresses         */
U32     len1, len2;                     /* Operand lengths           */
BYTE    byte1, byte2;                   /* Operand bytes             */
BYTE    pad;                            /* Padding byte              */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    ODD2_CHECK(r1, r3, regs);

    /* Load padding byte from bits 24-31 of effective address */
    pad = effective_addr2 & 0xFF;

    /* Determine the destination and source addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r3] & ADDRESS_MAXWRAP(regs);

    /* Load operand lengths from bits 0-31 of R1+1 and R3+1 */
    len1 = regs->gpr[r1+1];
    len2 = regs->gpr[r3+1];

    /* Process operands from left to right */
    for (i = 0; len1 > 0 || len2 > 0 ; i++)
    {
        /* If 4096 bytes have been compared, exit with cc=3 */
        if (i >= 4096)
        {
            cc = 3;
            break;
        }

        /* Fetch a byte from each operand, or use padding byte */
        byte1 = (len1 > 0) ? vfetchb (addr1, r1, regs) : pad;
        byte2 = (len2 > 0) ? vfetchb (addr2, r3, regs) : pad;

        /* Compare operand bytes, set condition code if unequal */
        if (byte1 != byte2)
        {
            cc = (byte1 < byte2) ? 1 : 2;
            break;
        } /* end if */

        /* Update the first operand address and length */
        if (len1 > 0)
        {
            addr1++;
            addr1 &= ADDRESS_MAXWRAP(regs);
            len1--;
        }

        /* Update the second operand address and length */
        if (len2 > 0)
        {
            addr2++;
            addr2 &= ADDRESS_MAXWRAP(regs);
            len2--;
        }

    } /* end for(i) */

    /* Update the registers */
    regs->gpr[r1] = addr1;
    regs->gpr[r1+1] = len1;
    regs->gpr[r3] = addr2;
    regs->gpr[r3+1] = len2;

    regs->psw.cc = cc;

}


/*-------------------------------------------------------------------*/
/* B25D CLST  - Compare Logical String                         [RRE] */
/*-------------------------------------------------------------------*/
void zz_compare_logical_string (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
int     i;                              /* Loop counter              */
int     cc;                             /* Condition code            */
U32     addr1, addr2;                   /* Operand addresses         */
BYTE    byte1, byte2;                   /* Operand bytes             */
BYTE    termchar;                       /* Terminating character     */

    RRE(inst, execflag, regs, r1, r2);

    /* Program check if bits 0-23 of register 0 not zero */
    if ((regs->gpr[0] & 0xFFFFFF00) != 0)
        program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Load string terminating character from register 0 bits 24-31 */
    termchar = regs->gpr[0] & 0xFF;

    /* Determine the operand addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Initialize the condition code to 3 */
    cc = 3;

    /* Compare up to 4096 bytes until terminating character */
    for (i = 0; i < 4096; i++)
    {
        /* Fetch a byte from each operand */
        byte1 = vfetchb ( addr1, r1, regs );
        byte2 = vfetchb ( addr2, r2, regs );

        /* If both bytes are the terminating character then the
           strings are equal so return condition code 0
           and leave the R1 and R2 registers unchanged */
        if (byte1 == termchar && byte2 == termchar)
        {
            regs->psw.cc = 0;
            return;
        }

        /* If first operand byte is the terminating character,
           or if the first operand byte is lower than the
           second operand byte, then return condition code 1 */
        if (byte1 == termchar || byte1 < byte2)
        {
            cc = 1;
            break;
        }

        /* If second operand byte is the terminating character,
           or if the first operand byte is higher than the
           second operand byte, then return condition code 2 */
        if (byte2 == termchar || byte1 > byte2)
        {
            cc = 2;
            break;
        }

        /* Increment operand addresses */
        addr1++;
        addr1 &= ADDRESS_MAXWRAP(regs);
        addr2++;
        addr2 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */

    /* Set R1 and R2 to point to current character of each operand */
    regs->gpr[r1] = addr1;
    regs->gpr[r2] = addr2;

    /* Set condition code */
    regs->psw.cc =  cc;

}


/*-------------------------------------------------------------------*/
/* B257 CUSE  - Compare Until Substring Equal                  [RRE] */
/*-------------------------------------------------------------------*/
void zz_compare_until_substring_equal (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
int     i;                              /* Loop counter              */
int     cc = 0;                         /* Condition code            */
U32     addr1, addr2;                   /* Operand addresses         */
S32     len1, len2;                     /* Operand lengths           */
BYTE    byte1, byte2;                   /* Operand bytes             */
BYTE    pad;                            /* Padding byte              */
BYTE    sublen;                         /* Substring length          */
BYTE    equlen = 0;                     /* Equal byte counter        */
U32     eqaddr1, eqaddr2;               /* Address of equal substring*/
S32     remlen1, remlen2;               /* Lengths remaining         */

    RRE(inst, execflag, regs, r1, r2);

    ODD2_CHECK(r1, r2, regs);

    /* Load substring length from bits 24-31 of register 0 */
    sublen = regs->gpr[0] & 0xFF;

    /* Load padding byte from bits 24-31 of register 1 */
    pad = regs->gpr[1] & 0xFF;

    /* Determine the destination and source addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Load signed operand lengths from R1+1 and R2+1 */
    len1 = (S32)(regs->gpr[r1+1]);
    len2 = (S32)(regs->gpr[r2+1]);

    /* Initialize equal string addresses and lengths */
    eqaddr1 = addr1;
    eqaddr2 = addr2;
    remlen1 = len1;
    remlen2 = len2;
    /* If substring length is zero, exit with condition code 0 */
    if (sublen == 0)
    {
        regs->psw.cc = 0;
        return;
    }

    /* If both operand lengths are zero, exit with condition code 2 */
    if (len1 == 0 && len2 == 0)
    {
        regs->psw.cc = 2;
        return;
    }

    /* Process operands from left to right */
    for (i = 0; len1 > 0 || len2 > 0 ; i++)
    {
        /* If equal byte count has reached substring length
           exit with condition code zero */
        if (equlen == sublen)
        {
            cc = 0;
            break;
        }

        /* If 4096 bytes have been compared, and the last bytes
           compared were unequal, exit with condition code 3 */
        if (equlen == 0 && i >= 4096)
        {
            cc = 3;
            break;
        }

        /* Fetch byte from first operand, or use padding byte */
        if (len1 > 0)
            byte1 = vfetchb ( addr1, r1, regs );
        else
            byte1 = pad;

        /* Fetch byte from second operand, or use padding byte */
        if (len2 > 0)
            byte2 = vfetchb ( addr2, r2, regs );
        else
            byte2 = pad;

        /* Test if bytes compare equal */
        if (byte1 == byte2)
        {
            /* If this is the first equal byte, save the start of
               substring addresses and remaining lengths */
            if (equlen == 0)
            {
                eqaddr1 = addr1;
                eqaddr2 = addr2;
                remlen1 = len1;
                remlen2 = len2;
            }

            /* Count the number of equal bytes */
            equlen++;

            /* Set condition code 1 */
            cc = 1;
        }
        else
        {
            /* Reset equal byte count and set condition code 2 */
            equlen = 0;
            cc = 2;
        }

        /* Update the first operand address and length */
        if (len1 > 0)
        {
            addr1++;
            addr1 &= ADDRESS_MAXWRAP(regs);
            len1--;
        }

        /* Update the second operand address and length */
        if (len2 > 0)
        {
            addr2++;
            addr2 &= ADDRESS_MAXWRAP(regs);
            len2--;
        }

    } /* end for(i) */

    /* Update the registers */
    if (cc < 2)
    {
        /* Update R1 and R2 to point to the equal substring */
        regs->gpr[r1] = eqaddr1;
        regs->gpr[r2] = eqaddr2;

        /* Set R1+1 and R2+1 to length remaining in each
           operand after the start of the substring */
        regs->gpr[r1+1] = (U32)remlen1;
        regs->gpr[r2+1] = (U32)remlen2;
    }
    else
    {
        /* Update R1 and R2 to point to next bytes to compare */
        regs->gpr[r1] = addr1;
        regs->gpr[r2] = addr2;

        /* Set R1+1 and R2+1 to remaining operand lengths */
        regs->gpr[r1+1] = (U32)len1;
        regs->gpr[r2+1] = (U32)len2;
    }

    /* Set condition code */
    regs->psw.cc =  cc;

}


#ifdef FEATURE_EXTENDED_TRANSLATION
/*-------------------------------------------------------------------*/
/* B2A6 CUUTF - Convert Unicode to UTF-8                       [RRE] */
/*-------------------------------------------------------------------*/
void zz_convert_unicode_to_utf8 (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Register numbers          */
int     i;                              /* Loop counter              */
int     cc = 0;                         /* Condition code            */
U32     addr1, addr2;                   /* Operand addresses         */
U32     len1, len2;                     /* Operand lengths           */
U32     naddr2, nlen2;                  /* Next operand 2 addr/length*/
U16     uvwxy;                          /* Unicode work area         */
U16     unicode1;                       /* Unicode character         */
U16     unicode2;                       /* Unicode low surrogate     */
int     n;                              /* Number of UTF-8 bytes - 1 */
BYTE    utf[4];                         /* UTF-8 bytes               */

    RRE(inst, execflag, regs, r1, r2);

    ODD2_CHECK(r1, r2, regs);

    /* Determine the destination and source addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Load operand lengths from bits 0-31 of R1+1 and R2+1 */
    len1 = regs->gpr[r1+1];
    len2 = regs->gpr[r2+1];

    /* Process operands from left to right */
    for (i = 0; len1 > 0 || len2 > 0; i++)
    {
        /* If 4096 characters have been converted, exit with cc=3 */
        if (i >= 4096)
        {
            cc = 3;
            break;
        }

        /* Exit if fewer than 2 bytes remain in source operand */
        if (len2 < 2) break;

        /* Fetch two bytes from source operand */
        unicode1 = vfetch2 ( addr2, r2, regs );
        naddr2 = addr2 + 2;
        naddr2 &= ADDRESS_MAXWRAP(regs);
        nlen2 = len2 - 2;

        /* Convert Unicode to UTF-8 */
        if (unicode1 < 0x0080)
        {
            /* Convert Unicode 0000-007F to one UTF-8 byte */
            utf[0] = (BYTE)unicode1;
            n = 0;
        }
        else if (unicode1 < 0x0800)
        {
            /* Convert Unicode 0080-07FF to two UTF-8 bytes */
            utf[0] = (BYTE)0xC0 | (BYTE)(unicode1 >> 6);
            utf[1] = (BYTE)0x80 | (BYTE)(unicode1 & 0x003F);
            n = 1;
        }
        else if (unicode1 < 0xD800 || unicode1 >= 0xDC00)
        {
            /* Convert Unicode 0800-D7FF or DC00-FFFF
               to three UTF-8 bytes */
            utf[0] = (BYTE)0xE0 | (BYTE)(unicode1 >> 12);
            utf[1] = (BYTE)0x80 | (BYTE)((unicode1 & 0x0FC0) >> 6);
            utf[2] = (BYTE)0x80 | (BYTE)(unicode1 & 0x003F);
            n = 2;
        }
        else
        {
            /* Exit if fewer than 2 bytes remain in source operand */
            if (nlen2 < 2) break;

            /* Fetch the Unicode low surrogate */
            unicode2 = vfetch2 ( naddr2, r2, regs );
            naddr2 += 2;
            naddr2 &= ADDRESS_MAXWRAP(regs);
            nlen2 -= 2;

            /* Convert Unicode surrogate pair to four UTF-8 bytes */
            uvwxy = ((unicode1 & 0x03C0) >> 6) + 1;
            utf[0] = (BYTE)0xF0 | (BYTE)(uvwxy >> 2);
            utf[1] = (BYTE)0x80 | (BYTE)((uvwxy & 0x0003) << 4)
                        | (BYTE)((unicode1 & 0x003C) >> 2);
            utf[2] = (BYTE)0x80 | (BYTE)((unicode1 & 0x0003) << 4)
                        | (BYTE)((unicode2 & 0x03C0) >> 6);
            utf[3] = (BYTE)0x80 | (BYTE)(unicode2 & 0x003F);
            n = 3;
        }

        /* Exit cc=1 if too few bytes remain in destination operand */
        if (len1 <= n)
        {
            cc = 1;
            break;
        }

        /* Store the result bytes in the destination operand */
        vstorec ( utf, n, addr1, r1, regs );
        addr1 += n + 1;
        addr1 &= ADDRESS_MAXWRAP(regs);
        len1 -= n + 1;

        /* Update operand 2 address and length */
        addr2 = naddr2;
        len2 = nlen2;

        /* Update the registers */
        regs->gpr[r1] = addr1;
        regs->gpr[r1+1] = len1;
        regs->gpr[r2] = addr2;
        regs->gpr[r2+1] = len2;

    } /* end for(i) */

    regs->psw.cc = cc;

} /* end zz_convert_unicode_to_utf8 */


/*-------------------------------------------------------------------*/
/* B2A7 CUTFU - Convert UTF-8 to Unicode                       [RRE] */
/*-------------------------------------------------------------------*/
void zz_convert_utf8_to_unicode (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Register numbers          */
int     i;                              /* Loop counter              */
int     cc = 0;                         /* Condition code            */
U32     addr1, addr2;                   /* Operand addresses         */
U32     len1, len2;                     /* Operand lengths           */
int     pair;                           /* 1=Store Unicode pair      */
U16     uvwxy;                          /* Unicode work area         */
U16     unicode1;                       /* Unicode character         */
U16     unicode2;                       /* Unicode low surrogate     */
int     n;                              /* Number of UTF-8 bytes - 1 */
BYTE    utf[4];                         /* UTF-8 bytes               */

    RRE(inst, execflag, regs, r1, r2);

    ODD2_CHECK(r1, r2, regs);

    /* Determine the destination and source addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Load operand lengths from bits 0-31 of R1+1 and R2+1 */
    len1 = regs->gpr[r1+1];
    len2 = regs->gpr[r2+1];

    /* Process operands from left to right */
    for (i = 0; len1 > 0 || len2 > 0; i++)
    {
        /* If 4096 characters have been converted, exit with cc=3 */
        if (i >= 4096)
        {
            cc = 3;
            break;
        }

        /* Fetch first UTF-8 byte from source operand */
        utf[0] = vfetchb ( addr2, r2, regs );

        /* Convert UTF-8 to Unicode */
        if (utf[0] < (BYTE)0x80)
        {
            /* Convert 00-7F to Unicode 0000-007F */
            n = 0;
            unicode1 = utf[0];
            pair = 0;
        }
        else if ((utf[0] & 0xE0) == 0xC0)
        {
            /* Exit if fewer than 2 bytes remain in source operand */
            n = 1;
            if (len2 <= n) break;

            /* Fetch two UTF-8 bytes from source operand */
            vfetchc ( utf, n, addr2, r2, regs );

            /* Convert C0xx-DFxx to Unicode */
            unicode1 = ((U16)(utf[0] & 0x1F) << 6)
                        | ((U16)(utf[1] & 0x3F));
            pair = 0;
        }
        else if ((utf[0] & 0xF0) == 0xE0)
        {
            /* Exit if fewer than 3 bytes remain in source operand */
            n = 2;
            if (len2 <= n) break;

            /* Fetch three UTF-8 bytes from source operand */
            vfetchc ( utf, n, addr2, r2, regs );

            /* Convert E0xxxx-EFxxxx to Unicode */
            unicode1 = ((U16)(utf[0]) << 12)
                        | ((U16)(utf[1] & 0x3F) << 6)
                        | ((U16)(utf[2] & 0x3F));
            pair = 0;
        }
        else if ((utf[0] & 0xF8) == 0xF0)
        {
            /* Exit if fewer than 4 bytes remain in source operand */
            n = 3;
            if (len2 <= n) break;

            /* Fetch four UTF-8 bytes from source operand */
            vfetchc ( utf, n, addr2, r2, regs );

            /* Convert F0xxxxxx-F7xxxxxx to Unicode surrogate pair */
            uvwxy = (((U16)(utf[0] & 0x07) << 2)
                        | ((U16)(utf[1] & 0x30) >> 4)) - 1;
            unicode1 = 0xD800 | (uvwxy << 6) | ((U16)(utf[1] & 0x0F) << 2)
                        | ((U16)(utf[2] & 0x30) >> 4);
            unicode2 = 0xDC00 | ((U16)(utf[2] & 0x0F) << 6)
                        | ((U16)(utf[3] & 0x3F));
            pair = 1;
        }
        else
        {
            /* Invalid UTF-8 byte 80-BF or F8-FF, exit with cc=2 */
            cc = 2;
            break;
        }

        /* Store the result bytes in the destination operand */
        if (pair)
        {
            /* Exit if fewer than 4 bytes remain in destination */
            if (len1 < 4)
            {
                cc = 1;
                break;
            }

            /* Store Unicode surrogate pair in destination */
            vstore4 ( ((U32)unicode1 << 16) | (U32)unicode2,
                        addr1, r1, regs );
            addr1 += 4;
            addr1 &= ADDRESS_MAXWRAP(regs);
            len1 -= 4;
        }
        else
        {
            /* Exit if fewer than 2 bytes remain in destination */
            if (len1 < 2)
            {
                cc = 1;
                break;
            }

            /* Store Unicode character in destination */
            vstore2 ( unicode1, addr1, r1, regs );
            addr1 += 2;
            addr1 &= ADDRESS_MAXWRAP(regs);
            len1 -= 2;
        }

        /* Update operand 2 address and length */
        addr2 += n + 1;
        addr2 &= ADDRESS_MAXWRAP(regs);
        len2 -= n + 1;

        /* Update the registers */
        regs->gpr[r1] = addr1;
        regs->gpr[r1+1] = len1;
        regs->gpr[r2] = addr2;
        regs->gpr[r2+1] = len2;

    } /* end for(i) */

    regs->psw.cc = cc;

} /* end zz_convert_utf8_to_unicode */
#endif /*FEATURE_EXTENDED_TRANSLATION*/


/*-------------------------------------------------------------------*/
/* 4F   CVB   - Convert to Binary                               [RX] */
/*-------------------------------------------------------------------*/
void zz_convert_to_binary (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U64     dreg;                           /* 64-bit result accumulator */
int     i;                              /* Loop counter              */
int     h, d;                           /* Decimal digits            */
BYTE    sbyte;                          /* Source operand byte       */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Initialize binary result */
    dreg = 0;

    /* Convert digits to binary */
    for (i = 0; i < 8; i++)
    {
        /* Load next byte of operand */
        sbyte = vfetchb ( effective_addr2, b2, regs );

        /* Isolate high-order and low-order digits */
        h = (sbyte & 0xF0) >> 4;
        d = sbyte & 0x0F;

        /* Check for valid high-order digit */
        if (h > 9)
            program_interrupt (regs, PGM_DATA_EXCEPTION);

        /* Accumulate high-order digit into result */
        dreg *= 10;
        dreg += h;

        /* Check for valid low-order digit or sign */
        if (i < 7)
        {
            /* Check for valid low-order digit */
            if (d > 9)
                program_interrupt (regs, PGM_DATA_EXCEPTION);

            /* Accumulate low-order digit into result */
            dreg *= 10;
            dreg += d;
        }
        else
        {
            /* Check for valid sign */
            if (d < 10)
                program_interrupt (regs, PGM_DATA_EXCEPTION);
        }

        /* Increment operand address */
        effective_addr2++;

    } /* end for(i) */

    /* Result is negative if sign is X'B' or X'D' */
    if (d == 0x0B || d == 0x0D)
    {
        (S64)dreg = -((S64)dreg);
    }

    /* Store low-order 32 bits of result into R1 register */
    regs->gpr[r1] = dreg & 0xFFFFFFFF;

    /* Program check if overflow */
    if ((S64)dreg < -2147483648LL || (S64)dreg > 2147483647LL)
        program_interrupt (regs, PGM_FIXED_POINT_DIVIDE_EXCEPTION);

}


/*-------------------------------------------------------------------*/
/* 4E   CVD   - Convert to Decimal                              [RX] */
/*-------------------------------------------------------------------*/
void zz_convert_to_decimal (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U64     dreg;                           /* 64-bit result accumulator */
int     i;                              /* Loop counter              */
int     d;                              /* Decimal digit             */
U32     n;                              /* Absolute value to convert */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load absolute value and generate sign */
    if (regs->gpr[r1] < 0x80000000)
    {
        /* Value is positive */
        n = regs->gpr[r1];
        dreg = 0x0C;
    }
    else if (regs->gpr[r1] > 0x80000000 )
    {
        /* Value is negative */
        n = -((S32)(regs->gpr[r1]));
        dreg = 0x0D;
    }
    else
    {
        /* Special case when R1 is maximum negative value */
        n = 0;
        dreg = 0x2147483648DULL;
    }

    /* Generate decimal digits */
    for (i = 4; n != 0; i += 4)
    {
        d = n % 10;
        n /= 10;
        dreg |= (U64)d << i;
    }

    /* Store packed decimal result at operand address */
    vstore8 ( dreg, effective_addr2, b2, regs );

}


/*-------------------------------------------------------------------*/
/* B24D CPYA  - Copy Access                                    [RRE] */
/*-------------------------------------------------------------------*/
void zz_copy_access (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Copy R2 access register to R1 access register */
    regs->ar[r1] = regs->ar[r2];

}


/*-------------------------------------------------------------------*/
/* 1D   DR    - Divide Register                                 [RR] */
/*-------------------------------------------------------------------*/
void zz_divide_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Values of R fields        */
int     r2;                             /* Values of R fields        */
int     divide_overflow;                /* 1=divide overflow         */

    RR(inst, execflag, regs, r1, r2);

    ODD_CHECK(r1, regs);

    /* Divide r1::r1+1 by r2, remainder in r1, quotient in r1+1 */
    divide_overflow =
        div_signed (&(regs->gpr[r1]),&(regs->gpr[r1+1]),
                    regs->gpr[r1],
                    regs->gpr[r1+1],
                    regs->gpr[r2]);

    /* Program check if overflow */
    if ( divide_overflow )
        program_interrupt (regs, PGM_FIXED_POINT_DIVIDE_EXCEPTION);
}


/*-------------------------------------------------------------------*/
/* 5D   D     - Divide                                          [RX] */
/*-------------------------------------------------------------------*/
void zz_divide (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */
int     divide_overflow;                /* 1=divide overflow         */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    ODD_CHECK(r1, regs);

    /* Load second operand from operand address */
    n = vfetch4 ( effective_addr2, b2, regs );

    /* Divide r1::r1+1 by n, remainder in r1, quotient in r1+1 */
    divide_overflow =
        div_signed (&(regs->gpr[r1]), &(regs->gpr[r1+1]),
                    regs->gpr[r1],
                    regs->gpr[r1+1],
                    n);

    /* Program check if overflow */
    if ( divide_overflow )
            program_interrupt (regs, PGM_FIXED_POINT_DIVIDE_EXCEPTION);

}


/*-------------------------------------------------------------------*/
/* 17   XR    - Exclusive Or Register                           [RR] */
/*-------------------------------------------------------------------*/
void zz_exclusive_or_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    /* XOR second operand with first and set condition code */
    regs->psw.cc = ( regs->gpr[r1] ^= regs->gpr[r2] ) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* 57   X     - Exclusive Or                                    [RX] */
/*-------------------------------------------------------------------*/
void zz_exclusive_or (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = vfetch4 ( effective_addr2, b2, regs );

    /* XOR second operand with first and set condition code */
    regs->psw.cc = ( regs->gpr[r1] ^= n ) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* 97   XI    - Exclusive Or Immediate                          [SI] */
/*-------------------------------------------------------------------*/
void zz_exclusive_or_immediate (BYTE inst[], int execflag, REGS *regs)
{
BYTE    i2;                             /* Immediate operand         */
int     b1;                             /* Base of effective addr    */
U32     effective_addr1;                /* Effective address         */
BYTE    rbyte;                          /* Result byte               */

    SI(inst, execflag, regs, i2, b1, effective_addr1);

    /* Fetch byte from operand address */
    rbyte = vfetchb ( effective_addr1, b1, regs );

    /* XOR with immediate operand */
    rbyte ^= i2;

    /* Store result at operand address */
    vstoreb ( rbyte, effective_addr1, b1, regs );

    /* Set condition code */
    regs->psw.cc = rbyte ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* D7   XC    - Exclusive Or Character                          [SS] */
/*-------------------------------------------------------------------*/
void zz_exclusive_or_character (BYTE inst[], int execflag, REGS *regs)
{
BYTE    l;                              /* Length byte               */
int     b1, b2;                         /* Base register numbers     */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
U32     abs1, abs2;                     /* Absolute addresses        */
U32     npv1, npv2;                     /* Next page virtual addrs   */
U32     npa1 = 0, npa2 = 0;             /* Next page absolute addrs  */
int     i;                              /* Loop counter              */
int     cc = 0;                         /* Condition code            */
BYTE    byte1, byte2;                   /* Operand bytes             */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */
#ifdef IBUF
U32     habs1;
BYTE    l1;
#endif

    SS_L(inst, execflag, regs, l, b1, effective_addr1,
                                  b2, effective_addr2);

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Translate addresses of leftmost operand bytes */
    abs1 = logical_to_abs (effective_addr1, b1, regs, ACCTYPE_WRITE, akey);
    abs2 = logical_to_abs (effective_addr2, b2, regs, ACCTYPE_READ, akey);

    /* Calculate page addresses of rightmost operand bytes */
    npv1 = (effective_addr1 + l) & ADDRESS_MAXWRAP(regs);
    npv1 &= STORAGE_KEY_PAGEMASK;
    npv2 = (effective_addr2 + l) & ADDRESS_MAXWRAP(regs);
    npv2 &= STORAGE_KEY_PAGEMASK;

    /* Translate next page addresses if page boundary crossed */
    if (npv1 != (effective_addr1 & STORAGE_KEY_PAGEMASK))
        npa1 = logical_to_abs (npv1, b1, regs, ACCTYPE_WRITE, akey);
    if (npv2 != (effective_addr2 & STORAGE_KEY_PAGEMASK))
        npa2 = logical_to_abs (npv2, b2, regs, ACCTYPE_READ, akey);

#ifdef IBUF
    habs1 = abs1;
#endif

    /* Process operands from left to right */
    for ( i = 0; i <= l; i++ )
    {
        /* Fetch a byte from each operand */
        byte1 = sysblk.mainstor[abs1];
        byte2 = sysblk.mainstor[abs2];

        /* XOR operand 1 with operand 2 */
        byte1 ^= byte2;

        /* Set condition code 1 if result is non-zero */
        if (byte1 != 0) cc = 1;

        /* Store the result in the destination operand */
        sysblk.mainstor[abs1] = byte1;

        /* Increment first operand address */
        effective_addr1++;
        effective_addr1 &= ADDRESS_MAXWRAP(regs);
        abs1++;

        /* Adjust absolute address if page boundary crossed */
        if ((effective_addr1 & STORAGE_KEY_BYTEMASK) == 0x000)
            abs1 = npa1;

        /* Increment second operand address */
        effective_addr2++;
        effective_addr2 &= ADDRESS_MAXWRAP(regs);
        abs2++;

        /* Adjust absolute address if page boundary crossed */
        if ((effective_addr2 & STORAGE_KEY_BYTEMASK) == 0x000)
            abs2 = npa2;

    } /* end for(i) */

    /* Set condition code */
    regs->psw.cc = cc;

#ifdef IBUF
    if (!npa1)
        FRAG_INVALIDATE(habs1, l)
    else
    {
        l1 = (habs1 & STORAGE_KEY_PAGEMASK) +
              STORAGE_KEY_PAGESIZE - habs1;
        FRAG_INVALIDATE(habs1, l1);
        FRAG_INVALIDATE(npa1, l - l1);
    }
#endif
}


/*-------------------------------------------------------------------*/
/* 44   EX    - Execute                                         [RX] */
/*-------------------------------------------------------------------*/
void zz_execute (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    ODD_CHECK(effective_addr2, regs);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    /* Ensure that the instruction field is zero, such that
       zeros are stored in the interception parm field, if
       the interrupt is intercepted */
    memset(regs->exinst, 0, 2);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Fetch target instruction from operand address */
    regs->iaabs = 0;
    instfetch (regs->exinst, effective_addr2, regs);
    regs->iaabs = 0;

    /* Program check if recursive execute */
    if ( regs->exinst[0] == 0x44 )
        program_interrupt (regs, PGM_EXECUTE_EXCEPTION);

    /* Or 2nd byte of instruction with low-order byte of R1 */
    if ( r1 != 0 )
    {
        regs->exinst[1] |= (regs->gpr[r1] & 0xFF);
        FRAG_INVALIDATE(effective_addr2, 1);
    }

    /* Execute the target instruction */
#ifdef IBUF
    regs->actentry = NULL;
#endif
    EXECUTE_INSTRUCTION (regs->exinst, 1, regs);
    LASTPAGE_INVALIDATE(regs);
    REASSIGN_FRAG(regs);

}

/*-------------------------------------------------------------------*/
/* B24F EAR   - Extract Access Register                        [RRE] */
/*-------------------------------------------------------------------*/
void zz_extract_access_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Copy R2 access register to R1 general register */
    regs->gpr[r1] = regs->ar[r2];
}


/*-------------------------------------------------------------------*/
/* 43   IC    - Insert Character                                [RX] */
/*-------------------------------------------------------------------*/
void zz_insert_character (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
BYTE    obyte;                          /* Operand byte              */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* fetch the byte to be inserted */
    obyte = vfetchb ( effective_addr2, b2, regs );

    /* Load rightmost byte of R1 register from operand address */
    regs->gpr[r1] = (regs->gpr[r1] & 0xFFFFFF00) | obyte;
}


/*-------------------------------------------------------------------*/
/* BF   ICM   - Insert Characters under Mask                    [RS] */
/*-------------------------------------------------------------------*/
void zz_insert_characters_under_mask (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
int     cc = 0;                         /* Condition code            */
BYTE    tbyte;                          /* Byte work areas           */
int     h, i;                           /* Integer work areas        */
U64     dreg;                           /* Double register work area */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* If the mask is all zero, we must nevertheless load one
       byte from the storage operand, because POP requires us
       to recognize an access exception on the first byte */
    if ( r3 == 0 )
    {
        tbyte = vfetchb ( effective_addr2, b2, regs );
        regs->psw.cc = 0;
        return;
    }

    /* Load existing register value into 64-bit work area */
    dreg = regs->gpr[r1];

    /* Insert characters into register from operand address */
    for ( i = 0, h = 0; i < 4; i++ )
    {
        /* Test mask bit corresponding to this character */
        if ( r3 & 0x08 )
        {
            /* Fetch the source byte from the operand */
            tbyte = vfetchb ( effective_addr2, b2, regs );

            /* If this is the first byte fetched then test the
               high-order bit to determine the condition code */
            if ( (r3 & 0xF0) == 0 )
                h = (tbyte & 0x80) ? 1 : 2;

            /* If byte is non-zero then set the condition code */
            if ( tbyte != 0 )
                 cc = h;

            /* Insert the byte into the register */
            dreg &= 0xFFFFFFFF00FFFFFFULL;
            dreg |= (U32)tbyte << 24;

            /* Increment the operand address */
            effective_addr2++;
            effective_addr2 &= ADDRESS_MAXWRAP(regs);
        }

        /* Shift mask and register for next byte */
        r3 <<= 1;
        dreg <<= 8;

    } /* end for(i) */

    /* Load the register with the updated value */
    regs->gpr[r1] = dreg >> 32;

    /* Set condition code */
    regs->psw.cc = cc;
}


/*-------------------------------------------------------------------*/
/* B222 IPM   - Insert Program Mask                            [RRE] */
/*-------------------------------------------------------------------*/
void zz_insert_program_mask (BYTE inst[], int execflag, REGS *regs)
{
int     r1, unused;                     /* Value of R field          */

    RRE(inst, execflag, regs, r1, unused);

    /* Insert condition code in R1 bits 2-3, program mask
       in R1 bits 4-7, and set R1 bits 0-1 to zero */
    regs->gpr[r1] &= 0x00FFFFFF;
    regs->gpr[r1] |=
            (regs->psw.cc << 28)
            | (regs->psw.fomask << 27)
            | (regs->psw.domask << 26)
            | (regs->psw.eumask << 25)
            | (regs->psw.sgmask << 24);
}

/*-------------------------------------------------------------------*/
/* 58   L     - Load                                            [RX] */
/*-------------------------------------------------------------------*/
void zz_load (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load R1 register from second operand */
    regs->gpr[r1] = vfetch4 ( effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* 18   LR    - Load Register                                   [RR] */
/*-------------------------------------------------------------------*/
void zz_load_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    /* Copy second operand to first operand */
    regs->gpr[r1] = regs->gpr[r2];
}


/*-------------------------------------------------------------------*/
/* 9A   LAM   - Load Access Multiple                            [RS] */
/*-------------------------------------------------------------------*/
void zz_load_access_multiple (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
int     n, d;                           /* Integer work areas        */
BYTE    rwork[64];                      /* Register work area        */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    FW_CHECK(effective_addr2, regs);

    /* Calculate the number of bytes to be loaded */
    d = (((r3 < r1) ? r3 + 16 - r1 : r3 - r1) + 1) * 4;

    /* Fetch new access register contents from operand address */
    vfetchc ( rwork, d-1, effective_addr2, b2, regs );

    /* Load access registers from work area */
    for ( n = r1, d = 0; ; )
    {
        /* Load one access register from work area */
        regs->ar[n] = (rwork[d] << 24) | (rwork[d+1] << 16)
                    | (rwork[d+2] << 8) | rwork[d+3];
        d += 4;

        /* Instruction is complete when r3 register is done */
        if ( n == r3 ) break;

        /* Update register number, wrapping from 15 to 0 */
        n++; n &= 15;
    }

}


/*-------------------------------------------------------------------*/
/* 41   LA    - Load Address                                    [RX] */
/*-------------------------------------------------------------------*/
void zz_load_address (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load operand address into register */
    regs->gpr[r1] = effective_addr2;
}


/*-------------------------------------------------------------------*/
/* 51   LAE   - Load Address Extended                           [RX] */
/*-------------------------------------------------------------------*/
void zz_load_address_extended (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load operand address into register */
    regs->gpr[r1] = effective_addr2;

    /* Load corresponding value into access register */
    if ( PRIMARY_SPACE_MODE(&(regs->psw)) )
        regs->ar[r1] = ALET_PRIMARY;
    else if ( SECONDARY_SPACE_MODE(&(regs->psw)) )
        regs->ar[r1] = ALET_SECONDARY;
    else if ( HOME_SPACE_MODE(&(regs->psw)) )
        regs->ar[r1] = ALET_HOME;
    else /* ACCESS_REGISTER_MODE(&(regs->psw)) */
        regs->ar[r1] = (b2 == 0) ? 0 : regs->ar[b2];
}


/*-------------------------------------------------------------------*/
/* 12   LTR   - Load and Test Register                          [RR] */
/*-------------------------------------------------------------------*/
void zz_load_and_test_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    /* Copy second operand and set condition code */
    regs->gpr[r1] = regs->gpr[r2];

    regs->psw.cc = (S32)regs->gpr[r1] < 0 ? 1 :
                   (S32)regs->gpr[r1] > 0 ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* 13   LCR   - Load Complement Register                        [RR] */
/*-------------------------------------------------------------------*/
void zz_load_complement_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    /* Condition code 3 and program check if overflow */
    if ( regs->gpr[r2] == 0x80000000 )
    {
        regs->gpr[r1] = regs->gpr[r2];
        regs->psw.cc = 3;
        if ( regs->psw.fomask )
            program_interrupt (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
        return;
    }

    /* Load complement of second operand and set condition code */
    (S32)regs->gpr[r1] = -((S32)regs->gpr[r2]);

    regs->psw.cc = (S32)regs->gpr[r1] < 0 ? 1 :
                   (S32)regs->gpr[r1] > 0 ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* 48   LH    - Load Halfword                                   [RX] */
/*-------------------------------------------------------------------*/
void zz_load_halfword (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load rightmost 2 bytes of register from operand address */
    regs->gpr[r1] = vfetch2 ( effective_addr2, b2, regs );

    /* Propagate sign bit to leftmost 2 bytes of register */
    if ( regs->gpr[r1] > 0x7FFF )
        regs->gpr[r1] |= 0xFFFF0000;
}

 
/*-------------------------------------------------------------------*/
/* A7x8 LHI   - Load Halfword Immediate                         [RI] */
/*-------------------------------------------------------------------*/
void zz_load_halfword_immediate (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    /* Load operand into register */
    (S32)regs->gpr[r1] = (S16)i2;

}


/*-------------------------------------------------------------------*/
/* 98   LM    - Load Multiple                                   [RS] */
/*-------------------------------------------------------------------*/
void zz_load_multiple (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
int     i, d;                           /* Integer work areas        */
BYTE    rwork[64];                      /* Character work areas      */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Calculate the number of bytes to be loaded */
    d = (((r3 < r1) ? r3 + 16 - r1 : r3 - r1) + 1) * 4;

    /* Fetch new register contents from operand address */
    vfetchc ( rwork, d-1, effective_addr2, b2, regs );

    /* Load registers from work area */
    for ( i = r1, d = 0; ; )
    {
        /* Load one register from work area */
        regs->gpr[i] = (rwork[d] << 24) | (rwork[d+1] << 16)
                     | (rwork[d+2] << 8) | rwork[d+3];
        d += 4;

        /* Instruction is complete when r3 register is done */
        if ( i == r3 ) break;

        /* Update register number, wrapping from 15 to 0 */
        i++; i &= 15;
    }
}


/*-------------------------------------------------------------------*/
/* 11   LNR   - Load Negative Register                          [RR] */
/*-------------------------------------------------------------------*/
void zz_load_negative_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    /* Load negative value of second operand and set cc */
    (S32)regs->gpr[r1] = (S32)regs->gpr[r2] > 0 ?
                            -((S32)regs->gpr[r2]) :
                            (S32)regs->gpr[r2];

    regs->psw.cc = (S32)regs->gpr[r1] == 0 ? 0 : 1;
}


/*-------------------------------------------------------------------*/
/* 10   LPR   - Load Positive Register                          [RR] */
/*-------------------------------------------------------------------*/
void zz_load_positive_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    /* Condition code 3 and program check if overflow */
    if ( regs->gpr[r2] == 0x80000000 )
    {
        regs->gpr[r1] = regs->gpr[r2];
        regs->psw.cc = 3;
        if ( regs->psw.fomask )
            program_interrupt (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
        return;
    }

    /* Load positive value of second operand and set cc */
    (S32)regs->gpr[r1] = (S32)regs->gpr[r2] < 0 ?
                            -((S32)regs->gpr[r2]) :
                            (S32)regs->gpr[r2];

    regs->psw.cc = (S32)regs->gpr[r1] == 0 ? 0 : 2;
}


/*-------------------------------------------------------------------*/
/* AF   MC    - Monitor Call                                    [SI] */
/*-------------------------------------------------------------------*/
void zz_monitor_call (BYTE inst[], int execflag, REGS *regs)
{
BYTE    i2;                             /* Monitor class             */
int     b1;                             /* Base of effective addr    */
U32     effective_addr1;                /* Effective address         */
U32     n;                              /* Work                      */

    SI(inst, execflag, regs, i2, b1, effective_addr1);

    /* Program check if monitor class exceeds 15 */
    if ( i2 > 0x0F )
        program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Ignore if monitor mask in control register 8 is zero */
    n = (regs->cr[8] & CR8_MCMASK) << i2;
    if ((n & 0x00008000) == 0)
        return;

    regs->monclass = i2;
    regs->moncode = effective_addr1;

    /* Generate a monitor event program interruption */
    program_interrupt (regs, PGM_MONITOR_EVENT);

}


/*-------------------------------------------------------------------*/
/* 92   MVI   - Move Immediate                                  [SI] */
/*-------------------------------------------------------------------*/
void zz_move_immediate (BYTE inst[], int execflag, REGS *regs)
{
BYTE    i2;                             /* Immediate operand         */
int     b1;                             /* Base of effective addr    */
U32     effective_addr1;                /* Effective address         */

    SI(inst, execflag, regs, i2, b1, effective_addr1);

    /* Store immediate operand at operand address */
    vstoreb ( i2, effective_addr1, b1, regs );
}


/*-------------------------------------------------------------------*/
/* D2   MVC   - Move Character                                  [SS] */
/*-------------------------------------------------------------------*/
void zz_move_character (BYTE inst[], int execflag, REGS *regs)
{
BYTE    l;                              /* Length byte               */
int     b1, b2;                         /* Values of base fields     */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */

    SS_L(inst, execflag, regs, l, b1, effective_addr1,
                                  b2, effective_addr2);

    /* Move characters using current addressing mode and key */
    move_chars (effective_addr1, b1, regs->psw.pkey,
                effective_addr2, b2, regs->psw.pkey, l, regs);
}


/*-------------------------------------------------------------------*/
/* E8   MVCIN - Move Inverse                                    [SS] */
/*-------------------------------------------------------------------*/
void zz_move_inverse (BYTE inst[], int execflag, REGS *regs)
{
BYTE    l;                              /* Lenght byte               */
int     b1, b2;                         /* Base registers            */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
U32     n;                              /* 32-bit operand values     */
BYTE    tbyte;                          /* Byte work areas           */
int     i;                              /* Integer work areas        */

    SS_L(inst, execflag, regs, l, b1, effective_addr1,
                                  b2, effective_addr2);

    /* Validate the operands for addressing and protection */
    validate_operand (effective_addr1, b1, l, ACCTYPE_WRITE, regs);
    n = (effective_addr2 - l) & ADDRESS_MAXWRAP(regs);
    validate_operand (n, b2, l, ACCTYPE_READ, regs);

    /* Process the destination operand from left to right,
       and the source operand from right to left */
    for ( i = 0; i <= l; i++ )
    {
        /* Fetch a byte from the source operand */
        tbyte = vfetchb ( effective_addr2, b2, regs );

        /* Store the byte in the destination operand */
        vstoreb ( tbyte, effective_addr1, b1, regs );

        /* Increment destination operand address */
        effective_addr1++;
        effective_addr1 &= ADDRESS_MAXWRAP(regs);

        /* Decrement source operand address */
        effective_addr2--;
        effective_addr2 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */
}


/*-------------------------------------------------------------------*/
/* 0E   MVCL  - Move Long                                       [RR] */
/*-------------------------------------------------------------------*/
void zz_move_long (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
int     cc;                             /* Condition code            */
U32     addr1, addr2;                   /* Operand addresses         */
U32     len1, len2;                     /* Operand lengths           */
U32     n;                              /* Work area                 */
BYTE    obyte;                          /* Operand byte              */
BYTE    pad;                            /* Padding byte              */

    RR(inst, execflag, regs, r1, r2);

    ODD2_CHECK(r1, r2, regs);

    /* Determine the destination and source addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Load padding byte from bits 0-7 of R2+1 register */
    pad = regs->gpr[r2+1] >> 24;

    /* Load operand lengths from bits 8-31 of R1+1 and R2+1 */
    len1 = regs->gpr[r1+1] & 0x00FFFFFF;
    len2 = regs->gpr[r2+1] & 0x00FFFFFF;

    /* Test for destructive overlap */
    if ( len2 > 1 && len1 > 1
        && (!ACCESS_REGISTER_MODE(&(regs->psw))
            || (r1 == 0 ? 0 : regs->ar[r1])
               != (r2 == 0 ? 0 : regs->ar[r2])))
    {
        n = addr2 + ((len2 < len1) ? len2 : len1) - 1;
        n &= ADDRESS_MAXWRAP(regs);
        if ((n > addr2
                && (addr1 > addr2 && addr1 <= n))
          || (n <= addr2
                && (addr1 > addr2 || addr1 <= n)))
        {
            regs->gpr[r1] = addr1;
            regs->gpr[r2] = addr2;
            regs->psw.cc =  3;
            logmsg ("MVCL destructive overlap\n");
            logmsg ("R%2.2d=%8.8X  R%2.2d=%8.8X  "
                    "R%2.2d=%8.8X  R%2.2d=%8.8X\n",
                    r1, regs->gpr[r1], r1+1, regs->gpr[r1+1],
                    r2, regs->gpr[r2], r2+1, regs->gpr[r2+1]);
            return;
        }
    }
    
    /* Set the condition code according to the lengths */
    cc = (len1 < len2) ? 1 : (len1 > len2) ? 2 : 0;

    /* Process operands from left to right */
    while (len1 > 0)
    {
        /* Fetch byte from source operand, or use padding byte */
        if (len2 > 0)
        {
            obyte = vfetchb ( addr2, r2, regs );
            addr2++;
            addr2 &= ADDRESS_MAXWRAP(regs);
            len2--;
        }   
        else   
            obyte = pad;
        
        /* Store the byte in the destination operand */
        vstoreb ( obyte, addr1, r1, regs );
        addr1++;
        addr1 &= ADDRESS_MAXWRAP(regs);
        len1--;

        /* Update the registers */
        regs->gpr[r1] = addr1;
        regs->gpr[r1+1] &= 0xFF000000;
        regs->gpr[r1+1] |= len1;
        regs->gpr[r2] = addr2;
        regs->gpr[r2+1] &= 0xFF000000;
        regs->gpr[r2+1] |= len2;

        /* The instruction can be interrupted when a CPU determined
           number of bytes have been processed.  The instruction
           address will be backed up, and the instruction will
           be re-executed.  This is consistent with operation
           under a hypervisor such as LPAR or VM.                *JJ */
        if ((len1 > 255) && !(addr1 & 0xFFF))
        {
            regs->psw.ia -= regs->psw.ilc;
            regs->psw.ia &= ADDRESS_MAXWRAP(regs);
            GET_FRAGENTRY(regs);
            break;
        }

    } /* end while(len1) */

    regs->psw.cc = cc;

}


/*-------------------------------------------------------------------*/
/* A8   MVCLE - Move Long Extended                              [RS] */
/*-------------------------------------------------------------------*/
void zz_move_long_extended (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
int     i;                              /* Loop counter              */
int     cc;                             /* Condition code            */
U32     addr1, addr2;                   /* Operand addresses         */
U32     len1, len2;                     /* Operand lengths           */
BYTE    obyte;                          /* Operand byte              */
BYTE    pad;                            /* Padding byte              */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    ODD2_CHECK(r1, r3, regs);

    /* Load padding byte from bits 24-31 of effective address */
    pad = effective_addr2 & 0xFF;

    /* Determine the destination and source addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r3] & ADDRESS_MAXWRAP(regs);

    /* Load operand lengths from bits 0-31 of R1+1 and R3+1 */
    len1 = regs->gpr[r1+1];
    len2 = regs->gpr[r3+1];

    /* Set the condition code according to the lengths */
    cc = (len1 < len2) ? 1 : (len1 > len2) ? 2 : 0;

    /* Process operands from left to right */
    for (i = 0; len1 > 0 || len2 > 0 ; i++)
    {
        /* If 4096 bytes have been moved, exit with cc=3 */
        if (i >= 4096)
        {
            cc = 3;
            break;
        }

        /* Fetch byte from source operand, or use padding byte */
        if (len2 > 0)
        {
            obyte = vfetchb ( addr2, r3, regs );
            addr2++;
            addr2 &= ADDRESS_MAXWRAP(regs);
            len2--;
        }
        else
            obyte = pad;

        /* Store the byte in the destination operand */
        vstoreb ( obyte, addr1, r1, regs );
        addr1++;
        addr1 &= ADDRESS_MAXWRAP(regs);
        len1--;

        /* Update the registers */
        regs->gpr[r1] = addr1;
        regs->gpr[r1+1] = len1;
        regs->gpr[r3] = addr2;
        regs->gpr[r3+1] = len2;

    } /* end for(i) */

    regs->psw.cc = cc;

}


/*-------------------------------------------------------------------*/
/* D1   MVN   - Move Numerics                                   [SS] */
/*-------------------------------------------------------------------*/
void zz_move_numerics (BYTE inst[], int execflag, REGS *regs)
{
BYTE    l;                              /* Length byte               */
int     b1, b2;                         /* Base register numbers     */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
U32     abs1, abs2;                     /* Absolute addresses        */
U32     npv1, npv2;                     /* Next page virtual addrs   */
U32     npa1 = 0, npa2 = 0;             /* Next page absolute addrs  */
int     i;                              /* Loop counter              */
BYTE    byte1, byte2;                   /* Operand bytes             */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */
#ifdef IBUF
U32     habs1;
BYTE    l1;
#endif

    SS_L(inst, execflag, regs, l, b1, effective_addr1,
                                  b2, effective_addr2);

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Translate addresses of leftmost operand bytes */
    abs1 = logical_to_abs (effective_addr1, b1, regs, ACCTYPE_WRITE, akey);
    abs2 = logical_to_abs (effective_addr2, b2, regs, ACCTYPE_READ, akey);

    /* Calculate page addresses of rightmost operand bytes */
    npv1 = (effective_addr1 + l) & ADDRESS_MAXWRAP(regs);
    npv1 &= STORAGE_KEY_PAGEMASK;
    npv2 = (effective_addr2 + l) & ADDRESS_MAXWRAP(regs);
    npv2 &= STORAGE_KEY_PAGEMASK;

    /* Translate next page addresses if page boundary crossed */
    if (npv1 != (effective_addr1 & STORAGE_KEY_PAGEMASK))
        npa1 = logical_to_abs (npv1, b1, regs, ACCTYPE_WRITE, akey);
    if (npv2 != (effective_addr2 & STORAGE_KEY_PAGEMASK))
        npa2 = logical_to_abs (npv2, b2, regs, ACCTYPE_READ, akey);

#ifdef IBUF
    habs1 = abs1;
#endif

    /* Process operands from left to right */
    for ( i = 0; i <= l; i++ )
    {
        /* Fetch a byte from each operand */
        byte1 = sysblk.mainstor[abs1];
        byte2 = sysblk.mainstor[abs2];

        /* Copy low digit of operand 2 to operand 1 */
        byte1 = (byte1 & 0xF0) | (byte2 & 0x0F);

        /* Store the result in the destination operand */
        sysblk.mainstor[abs1] = byte1;

        /* Increment first operand address */
        effective_addr1++;
        effective_addr1 &= ADDRESS_MAXWRAP(regs);
        abs1++;

        /* Adjust absolute address if page boundary crossed */
        if ((effective_addr1 & STORAGE_KEY_BYTEMASK) == 0x000)
            abs1 = npa1;

        /* Increment second operand address */
        effective_addr2++;
        effective_addr2 &= ADDRESS_MAXWRAP(regs);
        abs2++;

        /* Adjust absolute address if page boundary crossed */
        if ((effective_addr2 & STORAGE_KEY_BYTEMASK) == 0x000)
            abs2 = npa2;

    } /* end for(i) */

#ifdef IBUF
    if (!npa1)
       FRAG_INVALIDATE(habs1, l)
    else
    {
        l1 = (habs1 & STORAGE_KEY_PAGEMASK) +
              STORAGE_KEY_PAGESIZE - habs1;
        FRAG_INVALIDATE(habs1, l1);
        FRAG_INVALIDATE(npa1, l - l1);
    }
#endif

}


/*-------------------------------------------------------------------*/
/* B255 MVST  - Move String                                    [RRE] */
/*-------------------------------------------------------------------*/
void zz_move_string (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
int     i;                              /* Loop counter              */
U32     addr1, addr2;                   /* Operand addresses         */
BYTE    sbyte;                          /* String character          */
BYTE    termchar;                       /* Terminating character     */

    RRE(inst, execflag, regs, r1, r2);

    /* Program check if bits 0-23 of register 0 not zero */
    if ((regs->gpr[0] & 0xFFFFFF00) != 0)
        program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Load string terminating character from register 0 bits 24-31 */
    termchar = regs->gpr[0] & 0xFF;

    /* Determine the destination and source addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Move up to 4096 bytes until terminating character */
    for (i = 0; i < 4096; i++)
    {
        /* Fetch a byte from the source operand */
        sbyte = vfetchb ( addr2, r2, regs );

        /* Store the byte in the destination operand */
        vstoreb ( sbyte, addr1, r1, regs );

        /* Check if string terminating character was moved */
        if (sbyte == termchar)
        {
            /* Set r1 to point to terminating character */
            regs->gpr[r1] = addr1;

            /* Set condition code 1 */
            regs->psw.cc = 1;
            return;
        }

        /* Increment operand addresses */
        addr1++;
        addr1 &= ADDRESS_MAXWRAP(regs);
        addr2++;
        addr2 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */

    /* Set R1 and R2 to point to next character of each operand */
    regs->gpr[r1] = addr1;
    regs->gpr[r2] = addr2;

    /* Set condition code 3 */
    regs->psw.cc = 3;

}


/*-------------------------------------------------------------------*/
/* F1   MVO   - Move with Offset                                [SS] */
/*-------------------------------------------------------------------*/
void zz_move_with_offset (BYTE inst[], int execflag, REGS *regs)
{
int     l1, l2;                         /* Lenght values             */
int     b1, b2;                         /* Values of base registers  */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
int     i, j;                           /* Loop counters             */
BYTE    sbyte;                          /* Source operand byte       */
BYTE    dbyte;                          /* Destination operand byte  */

    SS(inst, execflag, regs, l1, l2, b1, effective_addr1,
                                     b2, effective_addr2);

    /* Validate the operands for addressing and protection */
    validate_operand (effective_addr1, b1, l1, ACCTYPE_WRITE, regs);
    validate_operand (effective_addr2, b2, l2, ACCTYPE_READ, regs);

    /* Fetch the rightmost byte from the source operand */
    effective_addr2 += l2;
    effective_addr2 &= ADDRESS_MAXWRAP(regs);
    sbyte = vfetchb ( effective_addr2--, b2, regs );

    /* Fetch the rightmost byte from the destination operand */
    effective_addr1 += l1;
    effective_addr1 &= ADDRESS_MAXWRAP(regs);
    dbyte = vfetchb ( effective_addr1, b1, regs );

    /* Move low digit of source byte to high digit of destination */
    dbyte &= 0x0F;
    dbyte |= sbyte << 4;
    vstoreb ( dbyte, effective_addr1--, b1, regs );

    /* Process remaining bytes from right to left */
    for (i = l1, j = l2; i > 0; i--)
    {
        /* Move previous high digit to destination low digit */
        dbyte = sbyte >> 4;

        /* Fetch next byte from second operand */
        if ( j-- > 0 ) {
            effective_addr2 &= ADDRESS_MAXWRAP(regs);
            sbyte = vfetchb ( effective_addr2--, b2, regs );
        }
        else
            sbyte = 0x00;

        /* Move low digit to destination high digit */
        dbyte |= sbyte << 4;
        effective_addr1 &= ADDRESS_MAXWRAP(regs);
        vstoreb ( dbyte, effective_addr1--, b1, regs );

    } /* end for(i) */

}


/*-------------------------------------------------------------------*/
/* D3   MVZ   - Move Zones                                      [SS] */
/*-------------------------------------------------------------------*/
void zz_move_zones (BYTE inst[], int execflag, REGS *regs)
{
BYTE    l;                              /* Length byte               */
int     b1, b2;                         /* Base register numbers     */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
U32     abs1, abs2;                     /* Absolute addresses        */
U32     npv1, npv2;                     /* Next page virtual addrs   */
U32     npa1 = 0, npa2 = 0;             /* Next page absolute addrs  */
int     i;                              /* Loop counter              */
BYTE    byte1, byte2;                   /* Operand bytes             */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */
#ifdef IBUF
U32     habs1;
BYTE    l1;
#endif

    SS_L(inst, execflag, regs, l, b1, effective_addr1,
                                  b2, effective_addr2);

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Translate addresses of leftmost operand bytes */
    abs1 = logical_to_abs (effective_addr1, b1, regs, ACCTYPE_WRITE, akey);
    abs2 = logical_to_abs (effective_addr2, b2, regs, ACCTYPE_READ, akey);

    /* Calculate page addresses of rightmost operand bytes */
    npv1 = (effective_addr1 + l) & ADDRESS_MAXWRAP(regs);
    npv1 &= STORAGE_KEY_PAGEMASK;
    npv2 = (effective_addr2 + l) & ADDRESS_MAXWRAP(regs);
    npv2 &= STORAGE_KEY_PAGEMASK;

    /* Translate next page addresses if page boundary crossed */
    if (npv1 != (effective_addr1 & STORAGE_KEY_PAGEMASK))
        npa1 = logical_to_abs (npv1, b1, regs, ACCTYPE_WRITE, akey);
    if (npv2 != (effective_addr2 & STORAGE_KEY_PAGEMASK))
        npa2 = logical_to_abs (npv2, b2, regs, ACCTYPE_READ, akey);

#ifdef IBUF
    habs1 = abs1;
#endif

    /* Process operands from left to right */
    for ( i = 0; i <= l; i++ )
    {
        /* Fetch a byte from each operand */
        byte1 = sysblk.mainstor[abs1];
        byte2 = sysblk.mainstor[abs2];

        /* Copy high digit of operand 2 to operand 1 */
        byte1 = (byte1 & 0x0F) | (byte2 & 0xF0);

        /* Store the result in the destination operand */
        sysblk.mainstor[abs1] = byte1;

        /* Increment first operand address */
        effective_addr1++;
        effective_addr1 &= ADDRESS_MAXWRAP(regs);
        abs1++;

        /* Adjust absolute address if page boundary crossed */
        if ((effective_addr1 & STORAGE_KEY_BYTEMASK) == 0x000)
            abs1 = npa1;

        /* Increment second operand address */
        effective_addr2++;
        effective_addr2 &= ADDRESS_MAXWRAP(regs);
        abs2++;

        /* Adjust absolute address if page boundary crossed */
        if ((effective_addr2 & STORAGE_KEY_BYTEMASK) == 0x000)
            abs2 = npa2;

    } /* end for(i) */

#ifdef IBUF
    if (!npa1)
       FRAG_INVALIDATE(habs1, l)
    else
    {
        l1 = (habs1 & STORAGE_KEY_PAGEMASK) +
              STORAGE_KEY_PAGESIZE - habs1;
        FRAG_INVALIDATE(habs1, l1);
        FRAG_INVALIDATE(npa1, l - l1);
    }
#endif

}


/*-------------------------------------------------------------------*/
/* 1C   MR    - Multiply Register                               [RR] */
/*-------------------------------------------------------------------*/
void zz_multiply_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    ODD_CHECK(r1, regs);

    /* Multiply r1+1 by r2 and place result in r1 and r1+1 */
    mul_signed (&(regs->gpr[r1]),&(regs->gpr[r1+1]),
                    regs->gpr[r1+1],
                    regs->gpr[r2]);
}


/*-------------------------------------------------------------------*/
/* 5C   M     - Multiply                                        [RX] */
/*-------------------------------------------------------------------*/
void zz_multiply (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    ODD_CHECK(r1, regs);

    /* Load second operand from operand address */
    n = vfetch4 ( effective_addr2, b2, regs );

    /* Multiply r1+1 by n and place result in r1 and r1+1 */
    mul_signed (&(regs->gpr[r1]), &(regs->gpr[r1+1]),
                    regs->gpr[r1+1],
                    n);

}


/*-------------------------------------------------------------------*/
/* 4C   MH    - Multiply Halfword                               [RX] */
/*-------------------------------------------------------------------*/
void zz_multiply_halfword (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load 2 bytes from operand address */
    n = vfetch2 ( effective_addr2, b2, regs );

    /* Propagate sign bit to leftmost 2 bytes of operand */
    if ( n > 0x7FFF )
        n |= 0xFFFF0000;

    /* Multiply R1 register by n, ignore leftmost 32 bits of
       result, and place rightmost 32 bits in R1 register */
    mul_signed (&n, &(regs->gpr[r1]), regs->gpr[r1], n);

}


/*-------------------------------------------------------------------*/
/* A7xC MHI   - Multiply Halfword Immediate                     [RI] */
/*-------------------------------------------------------------------*/
void zz_multiply_halfword_immediate (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand            */

    RI(inst, execflag, regs, r1, opcd, i2);

    /* Multiply register by operand ignoring overflow  */
    (S32)regs->gpr[r1] *= (S16)i2;

}


/*-------------------------------------------------------------------*/
/* B252 MSR   - Multiply Single Register                       [RRE] */
/*-------------------------------------------------------------------*/
void zz_multiply_single_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Multiply signed registers ignoring overflow */
    (S32)regs->gpr[r1] *= (S32)regs->gpr[r2];

}


/*-------------------------------------------------------------------*/
/* 71   MS    - Multiply Single                                 [RX] */
/*-------------------------------------------------------------------*/
void zz_multiply_single (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = vfetch4 ( effective_addr2, b2, regs );

    /* Multiply signed operands ignoring overflow */
    (S32)regs->gpr[r1] *= (S32)n;

}


/*-------------------------------------------------------------------*/
/* 16   OR    - Or Register                                     [RR] */
/*-------------------------------------------------------------------*/
void zz_or_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    /* OR second operand with first and set condition code */
    regs->psw.cc = ( regs->gpr[r1] |= regs->gpr[r2] ) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* 56   O     - Or                                              [RX] */
/*-------------------------------------------------------------------*/
void zz_or (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = vfetch4 ( effective_addr2, b2, regs );

    /* OR second operand with first and set condition code */
    regs->psw.cc = ( regs->gpr[r1] |= n ) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* 96   OI    - Or Immediate                                    [SI] */
/*-------------------------------------------------------------------*/
void zz_or_immediate (BYTE inst[], int execflag, REGS *regs)
{
BYTE    i2;                             /* Immediate operand byte    */
int     b1;                             /* Base of effective addr    */
U32     effective_addr1;                /* Effective address         */
BYTE    rbyte;                          /* Result byte               */

    SI(inst, execflag, regs, i2, b1, effective_addr1);

    /* Fetch byte from operand address */
    rbyte = vfetchb ( effective_addr1, b1, regs );

    /* OR with immediate operand */
    rbyte |= i2;

    /* Store result at operand address */
    vstoreb ( rbyte, effective_addr1, b1, regs );

    /* Set condition code */
    regs->psw.cc = rbyte ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* D6   OC    - Or Characters                                   [SS] */
/*-------------------------------------------------------------------*/
void zz_or_character (BYTE inst[], int execflag, REGS *regs)
{
BYTE    l;                              /* Length byte               */
int     b1, b2;                         /* Base register numbers     */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
U32     abs1, abs2;                     /* Absolute addresses        */
U32     npv1, npv2;                     /* Next page virtual addrs   */
U32     npa1 = 0, npa2 = 0;             /* Next page absolute addrs  */
int     i;                              /* Loop counter              */
int     cc = 0;                         /* Condition code            */
BYTE    byte1, byte2;                   /* Operand bytes             */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */
#ifdef IBUF
U32     habs1;
BYTE    l1;
#endif

    SS_L(inst, execflag, regs, l, b1, effective_addr1,
                                  b2, effective_addr2);

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Translate addresses of leftmost operand bytes */
    abs1 = logical_to_abs (effective_addr1, b1, regs, ACCTYPE_WRITE, akey);
    abs2 = logical_to_abs (effective_addr2, b2, regs, ACCTYPE_READ, akey);

    /* Calculate page addresses of rightmost operand bytes */
    npv1 = (effective_addr1 + l) & ADDRESS_MAXWRAP(regs);
    npv1 &= STORAGE_KEY_PAGEMASK;
    npv2 = (effective_addr2 + l) & ADDRESS_MAXWRAP(regs);
    npv2 &= STORAGE_KEY_PAGEMASK;

    /* Translate next page addresses if page boundary crossed */
    if (npv1 != (effective_addr1 & STORAGE_KEY_PAGEMASK))
        npa1 = logical_to_abs (npv1, b1, regs, ACCTYPE_WRITE, akey);
    if (npv2 != (effective_addr2 & STORAGE_KEY_PAGEMASK))
        npa2 = logical_to_abs (npv2, b2, regs, ACCTYPE_READ, akey);

#ifdef IBUF
    habs1 = abs1;
#endif

    /* Process operands from left to right */
    for ( i = 0; i <= l; i++ )
    {
        /* Fetch a byte from each operand */
        byte1 = sysblk.mainstor[abs1];
        byte2 = sysblk.mainstor[abs2];

        /* OR operand 1 with operand 2 */
        byte1 |= byte2;

        /* Set condition code 1 if result is non-zero */
        if (byte1 != 0) cc = 1;

        /* Store the result in the destination operand */
        sysblk.mainstor[abs1] = byte1;

        /* Increment first operand address */
        effective_addr1++;
        effective_addr1 &= ADDRESS_MAXWRAP(regs);
        abs1++;

        /* Adjust absolute address if page boundary crossed */
        if ((effective_addr1 & STORAGE_KEY_BYTEMASK) == 0x000)
            abs1 = npa1;

        /* Increment second operand address */
        effective_addr2++;
        effective_addr2 &= ADDRESS_MAXWRAP(regs);
        abs2++;

        /* Adjust absolute address if page boundary crossed */
        if ((effective_addr2 & STORAGE_KEY_BYTEMASK) == 0x000)
            abs2 = npa2;

    } /* end for(i) */

    /* Set condition code */
    regs->psw.cc = cc;

#ifdef IBUF
    if (!npa1)
       FRAG_INVALIDATE(habs1, l)
    else
    {
        l1 = (habs1 & STORAGE_KEY_PAGEMASK) +
              STORAGE_KEY_PAGESIZE - habs1;
        FRAG_INVALIDATE(habs1, l1);
        FRAG_INVALIDATE(npa1, l - l1);
    }
#endif

}


/*-------------------------------------------------------------------*/
/* F2   PACK  - Pack                                            [SS] */
/*-------------------------------------------------------------------*/
void zz_pack (BYTE inst[], int execflag, REGS *regs)
{
int     l1, l2;                         /* Lenght values             */
int     b1, b2;                         /* Values of base registers  */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
int     i, j;                           /* Loop counters             */
BYTE    sbyte;                          /* Source operand byte       */
BYTE    dbyte;                          /* Destination operand byte  */

    SS(inst, execflag, regs, l1, l2, b1, effective_addr1,
                                     b2, effective_addr2);

    /* Validate the operands for addressing and protection */
    validate_operand (effective_addr1, b1, l1, ACCTYPE_WRITE, regs);
    validate_operand (effective_addr2, b2, l2, ACCTYPE_READ, regs);

    /* Exchange the digits in the rightmost byte */
    effective_addr1 += l1;
    effective_addr2 += l2;
    sbyte = vfetchb ( effective_addr2, b2, regs );
    dbyte = (sbyte << 4) | (sbyte >> 4);
    vstoreb ( dbyte, effective_addr1, b1, regs );

    /* Process remaining bytes from right to left */
    for (i = l1, j = l2; i > 0; i--)
    {
        /* Fetch source bytes from second operand */
        if (j-- > 0)
        {
            sbyte = vfetchb ( --effective_addr2, b2, regs );
            dbyte = sbyte & 0x0F;

            if (j-- > 0)
            {
                effective_addr2 &= ADDRESS_MAXWRAP(regs);
                sbyte = vfetchb ( --effective_addr2, b2, regs );
                dbyte |= sbyte << 4;
            }
        }
        else
        {
            dbyte = 0;
        }

        /* Store packed digits at first operand address */
        vstoreb ( dbyte, --effective_addr1, b1, regs );

        /* Wraparound according to addressing mode */
        effective_addr1 &= ADDRESS_MAXWRAP(regs);
        effective_addr2 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */

}


/*-------------------------------------------------------------------*/
/* EE   PLO   - Perform Locked Operation                        [SS] */
/*-------------------------------------------------------------------*/
void zz_perform_locked_operation (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Lenght values             */
int     b2, b4;                         /* Values of base registers  */
U32     effective_addr2,
        effective_addr4;                /* Effective addresses       */

    SS(inst, execflag, regs, r1, r3, b2, effective_addr2,
                                     b4, effective_addr4);

    if(regs->gpr[0] & PLO_GPR0_RESV)
        program_interrupt(regs, PGM_SPECIFICATION_EXCEPTION);

    if(regs->gpr[0] & PLO_GPR0_T)
        switch(regs->gpr[0] & PLO_GPR0_FC) 
    {
        case PLO_CL:
        case PLO_CLG:
        case PLO_CS:
        case PLO_CSG:
        case PLO_DCS:
        case PLO_DCSG:
        case PLO_CSST:
        case PLO_CSSTG:
        case PLO_CSDST:
        case PLO_CSDSTG:
        case PLO_CSTST:
        case PLO_CSTSTG:

            /* Indicate function supported */
            regs->psw.cc = 0;   
            break;

        default:
            /* indicate function not supported */
            regs->psw.cc = 3;
            break;
    }
    else
    {
        /* gpr1/ar1 indentify the program lock token, which is used
           to select a lock from the model dependent number of locks 
           in the configuration.  We simply use 1 lock which is the 
           main storage access lock which is also used by CS, CDS 
           and TS.                                               *JJ */
        OBTAIN_MAINLOCK(regs);

        switch(regs->gpr[0] & PLO_GPR0_FC) 
        {
            case PLO_CL:
                {
                U32 op2,
                    op4;

                    FW_CHECK(effective_addr2, regs);
                    FW_CHECK(effective_addr4, regs);

                    /* Load second operand from operand address  */
                    op2 = VFETCH4 ( effective_addr2, b2, regs );

                    if(regs->gpr[r1] == op2)
                    {
                        
                        op4 = VFETCH4 ( effective_addr4, b4, regs );
                        regs->gpr[r3] = op4;
                        regs->psw.cc = 0;
                    }
                    else
                    {
                        regs->gpr[r1] = op2;
                        regs->psw.cc = 1;
                    }

                }
                break;


            case PLO_CLG:
                {
                U64 op1c,
                    op2,
                    op4;
                U32 op4alet = 0,
                    op4addr;
                
                    DW_CHECK(effective_addr4, regs);
                    DW_CHECK(effective_addr2, regs);

                    /* load second operand */
                    op2 = VFETCH8(effective_addr2, b2, regs);

                    /* load 1st op. compare value */
                    op1c = VFETCH8(effective_addr4 + 8, b4, regs);

                    if(op1c == op2)
                    {
                        /* When in ar mode, ar3 is used to access the 
                           operand. The alet is fetched from the pl */
                        if(ACCESS_REGISTER_MODE(&(regs->psw)))
                        {
                            op4alet = VFETCH4(effective_addr4 + 68, b4, regs);
                            if(op4alet)
                            {
                                if(r3 == 0)
                                    program_interrupt(regs, PGM_SPECIFICATION_EXCEPTION);
                                regs->ar[r3] = op4alet;
                            }
                        }

                        /* Load address of operand 4 */
                        op4addr = VFETCH4(effective_addr4 + 76, b4, regs);
                        op4addr &= ADDRESS_MAXWRAP(regs);
                        DW_CHECK(op4addr, regs);

                        /* Load operand 4, using ar3 when in ar mode */
                        op4 = VFETCH8(op4addr, op4alet ? r3 : 0, regs);

                        /* replace the 3rd operand with the 4th operand */
                        VSTORE8(op4, effective_addr4 + 40, b4, regs);

                        regs->psw.cc = 0;
                    }
                    else
                    {
                        /* replace the first op compare value with 2nd op */
                        VSTORE8(op2, effective_addr4 + 8, b4, regs);

                        regs->psw.cc = 1;
                    }
                }
                break;
    

            case PLO_CS:
                {
                U32 op2;
        
                    ODD_CHECK(r1, regs);
                    FW_CHECK(effective_addr2, regs);

                    /* Load second operand from operand address  */
                    op2 = VFETCH4 ( effective_addr2, b2, regs );

                    /* Compare operand with R1 register contents */
                    if ( regs->gpr[r1] == op2 )
                    {
                        /* If equal, store R1+1 at operand loc and set cc=0 */
                        VSTORE4 ( regs->gpr[r1+1], effective_addr2, b2, regs );

                        regs->psw.cc = 0;
                    }
                    else
                    {
                        /* If unequal, load R1 from operand and set cc=1 */
                        regs->gpr[r1] = op2;

                        regs->psw.cc = 1;
                    }

                }
                break;


            case PLO_CSG:
                {
                U64 op1c, 
                    op1r,
                    op2;

                    DW_CHECK(effective_addr4, regs);
                    DW_CHECK(effective_addr2, regs);

                    /* Load first op compare value */
                    op1c = VFETCH8(effective_addr4 + 8, b4, regs);

                    /* Load 2nd operand */
                    op2 = VFETCH8(effective_addr2, b2, regs);

                    if(op1c == op2)
                    {
                        /* Load 1st op replacement value */
                        op1r = VFETCH8(effective_addr4 + 24, b4, regs);

                        /* Store at 2nd operand location */
                        VSTORE8(op1r, effective_addr2, b2, regs);

                        regs->psw.cc = 0;
                    }
                    else
                    {
                        /* Replace 1st op comp value by 2nd op */
                        VSTORE8(op2, effective_addr4 + 8, b4, regs);

                        regs->psw.cc = 1;
                    }
                }
                break;


            case PLO_DCS:
                {
                U32 op2,
                    op4;
        
                    ODD2_CHECK(r1, r3, regs);
                    FW_CHECK(effective_addr2, regs);
                    FW_CHECK(effective_addr4, regs);

                    /* Load second operands from operand addresses  */
                    op2 = VFETCH4 ( effective_addr2, b2, regs );

                    if(regs->gpr[r1] != op2)
                    {
                        regs->gpr[r1] = op2;

                        regs->psw.cc = 1;
                    }
                    else
                    {
                        op4 = VFETCH4 ( effective_addr4, b4, regs );

                        /* Compare operand with register contents */
                        if (regs->gpr[r3] != op4)
                        {
                            /* If unequal, load r3 from op and set cc=2 */
                            regs->gpr[r3] = op4;
                            regs->psw.cc = 2;
                        }
                        else
                        {
                            /* Verify access to 2nd operand */
                            validate_operand (effective_addr2, b2, 4-1,
                                ACCTYPE_WRITE, regs);

                            /* If equal, store replacement and set cc=0 */
                            VSTORE4 ( regs->gpr[r3+1], effective_addr4, b4, regs );
                            VSTORE4 ( regs->gpr[r1+1], effective_addr2, b2, regs );

                            regs->psw.cc = 0;
                        }
                    }

                }
                break;


            case PLO_DCSG:
                {
                U64 op1c,
                    op1r,
                    op2,
                    op3c,
                    op3r,
                    op4;
                U32 op4alet = 0,
                    op4addr;

                    DW_CHECK(effective_addr2, regs);
                    DW_CHECK(effective_addr4, regs);

                    /* load 1st op compare value from the pl */
                    op1c = VFETCH8(effective_addr4 + 8, b4, regs);

                    /* load 2nd operand */
                    op2 = VFETCH8(effective_addr2, b2, regs);

                    if(op1c != op2)
                    {
                        /* replace the 1st op compare value with 2nd op */
                        VSTORE8(op2, effective_addr4 + 8, b4, regs);

                        regs->psw.cc = 1;
                    }
                    else
                    {
                        /* Load 3rd op compare value */
                        op3c = VFETCH8(effective_addr4 + 40, b4, regs);

                        /* When in ar mode, ar3 is used to access the 
                           operand. The alet is fetched from the pl */
                        if(ACCESS_REGISTER_MODE(&(regs->psw)))
                        {
                            op4alet = VFETCH4(effective_addr4 + 68, b4, regs);
                            if(op4alet)
                            {
                                if(r3 == 0)
                                    program_interrupt(regs, PGM_SPECIFICATION_EXCEPTION);
                                regs->ar[r3] = op4alet;
                            }
                        }

                        /* Load address of operand 4 */
                        op4addr = VFETCH4(effective_addr4 + 76, b4, regs);
                        op4addr &= ADDRESS_MAXWRAP(regs);
                        DW_CHECK(op4addr, regs);

                        /* Load operand 4, using ar3 when in ar mode */
                        op4 = VFETCH8(op4addr, op4alet ? r3 : 0, regs);

                        if(op3c != op4)
                        {
                            VSTORE8(op4, effective_addr4 + 40, b4, regs);
                            regs->psw.cc = 2;
                        }
                        else
                        {
                            /* load replacement values */
                            op1r = VFETCH8(effective_addr4 + 24, b4, regs);
                            op3r = VFETCH8(effective_addr4 + 56, b4, regs);

                            /* Verify access to 2nd operand */
                            validate_operand (effective_addr2, b2, 8-1,
                                ACCTYPE_WRITE, regs);

                            /* Store 3rd op replacement at 4th op */
                            VSTORE8(op3r, op4addr, op4alet ? r3 : 0, regs);

                            /* Store 1st op replacement at 2nd op */
                            VSTORE8(op1r, effective_addr2, b2, regs);

                            regs->psw.cc = 0;
                        }
                    }

                }
                break;


            case PLO_CSST:
                {
                U32 op2;

                    ODD_CHECK(r1, regs);
                    FW_CHECK(effective_addr2, regs);
                    FW_CHECK(effective_addr4, regs);

                    /* Load second operand from operand address  */
                    op2 = VFETCH4 ( effective_addr2, b2, regs );

                    /* Compare operand with register contents */
                    if ( regs->gpr[r1] == op2)
                    {
                        /* Verify access to 2nd operand */
                        validate_operand (effective_addr2, b2, 4-1,
                            ACCTYPE_WRITE, regs);

                        /* If equal, store replacement and set cc=0 */
                        VSTORE4 ( regs->gpr[r3], effective_addr4, b4, regs );
                        VSTORE4 ( regs->gpr[r1+1], effective_addr2, b2, regs );

                        regs->psw.cc = 0;
                    }
                    else
                    {
                        regs->gpr[r1] = op2;

                        regs->psw.cc = 1;
                    }

                }
                break;


            case PLO_CSSTG:
                {
                U64 op1c,
                    op1r,
                    op2,
                    op3;
                U32 op4alet = 0,
                    op4addr;

                    DW_CHECK(effective_addr2, regs);
                    DW_CHECK(effective_addr4, regs);

                    op1c = VFETCH8(effective_addr4 + 8, b4, regs);
                    op2 = VFETCH8(effective_addr2, b2, regs);
                    
                    if(op1c == op2)
                    {
                        op1r = VFETCH8(effective_addr4 + 24, b4, regs);
                        op3 = VFETCH8(effective_addr4 + 56, b4, regs);
                        
                        /* Verify access to 2nd operand */
                        validate_operand (effective_addr2, b2, 8-1,
                            ACCTYPE_WRITE, regs);

                        /* When in ar mode, ar3 is used to access the 
                           operand. The alet is fetched from the pl */
                        if(ACCESS_REGISTER_MODE(&(regs->psw)))
                        {
                            op4alet = VFETCH4(effective_addr4 + 68, b4, regs);
                            if(op4alet)
                            {
                                if(r3 == 0)
                                    program_interrupt(regs, PGM_SPECIFICATION_EXCEPTION);
                                regs->ar[r3] = op4alet;
                            }
                        }

                        /* Load address of operand 4 */
                        op4addr = VFETCH4(effective_addr4 + 76, b4, regs);
                        op4addr &= ADDRESS_MAXWRAP(regs);
                        DW_CHECK(op4addr, regs);

                        VSTORE8(op3, op4addr, op4alet ? r3 : 0, regs);
                        VSTORE8(op1r, effective_addr2, b2, regs);

                        regs->psw.cc = 0;
                    }
                    else
                    {
                        /* Store 2nd op at 1st op comare value */
                        VSTORE8(op2, effective_addr4 + 8, b4, regs);
                        regs->psw.cc = 1;
                    }

                }
                break;


            case PLO_CSDST:
                {
                U32 op2,
                    op3,
                    op4alet = 0,
                    op4addr,
                    op5,
                    op6alet = 0,
                    op6addr;

                    ODD_CHECK(r1, regs);
                    FW_CHECK(effective_addr2, regs);
                    FW_CHECK(effective_addr4, regs);

                    op2 = VFETCH4(effective_addr2, b2, regs);

                    if(regs->gpr[r1] == op2)
                    {
                        op3 = VFETCH4(effective_addr4 + 60, b4, regs);
                        op5 = VFETCH4(effective_addr4 + 92, b4, regs);

                        /* Verify access to 2nd operand */
                        validate_operand (effective_addr2, b2, 4-1,
                            ACCTYPE_WRITE, regs);

                        /* When in ar mode, ar3 is used to access the 
                           operand. The alet is fetched from the pl */
                        if(ACCESS_REGISTER_MODE(&(regs->psw)))
                        {
                            op4alet = VFETCH4(effective_addr4 + 68, b4, regs);
                            op6alet = VFETCH4(effective_addr4 + 100, b4, regs);
                            if(op4alet || op6alet)
                            {
                                if(r3 == 0)
                                    program_interrupt(regs, PGM_SPECIFICATION_EXCEPTION);
                                regs->ar[r3] = op4alet;
                            }
                        }

                        /* Load address of operand 4 */
                        op4addr = VFETCH4(effective_addr4 + 76, b4, regs);
                        op4addr &= ADDRESS_MAXWRAP(regs);
                        FW_CHECK(op4addr, regs);

                        /* Load address of operand 6 */
                        op6addr = VFETCH4(effective_addr4 + 108, b4, regs);
                        op6addr &= ADDRESS_MAXWRAP(regs);
                        FW_CHECK(op6addr, regs);

                        /* Verify access to 4th operand */
                        validate_operand (op4addr, op4alet ? r3 : 0, 4-1,
                            ACCTYPE_WRITE, regs);

                        /* Store 5th op at 6th op */
                        if(op6alet)
                            regs->ar[r3] = op6alet;
                        VSTORE4(op5, op6addr, op6alet ? r3 : 0, regs);

                        /* Store 3th op at 4th op */
                        if(op4alet)
                            regs->ar[r3] = op4alet;
                        VSTORE4(op3, op4addr, op4alet ? r3 : 0, regs);

                        /* Store 1st op at 2nd op */
                        VSTORE4(regs->gpr[r1+1], effective_addr2, b2, regs);

                        regs->psw.cc = 0;
                    }
                    else
                    {
                        regs->gpr[r1] = op2;
                        regs->psw.cc = 1;
                    }
                }
                break;


            case PLO_CSDSTG:
                {
                U64 op1c,
                    op1r,
                    op2,
                    op3,
                    op5;
                U32 op4alet = 0,
                    op4addr,
                    op6alet = 0,
                    op6addr;

                    DW_CHECK(effective_addr2, regs);
                    DW_CHECK(effective_addr4, regs);

                    op1c = VFETCH8(effective_addr4 + 8, b4, regs);
                    op2 = VFETCH8(effective_addr2, b2, regs);

                    if(op1c == op2)
                    {
                        op1r = VFETCH8(effective_addr4 + 24, b4, regs);
                        op3 = VFETCH8(effective_addr4 + 56, b4, regs);
                        op5 = VFETCH8(effective_addr4 + 88, b4, regs);

                        /* Verify access to 2nd operand */
                        validate_operand (effective_addr2, b2, 8-1,
                            ACCTYPE_WRITE, regs);

                        /* When in ar mode, ar3 is used to access the 
                           operand. The alet is fetched from the pl */
                        if(ACCESS_REGISTER_MODE(&(regs->psw)))
                        {
                            op4alet = VFETCH4(effective_addr4 + 68, b4, regs);
                            op6alet = VFETCH4(effective_addr4 + 100, b4, regs);
                            if(op4alet || op6alet)
                            {
                                if(r3 == 0)
                                    program_interrupt(regs, PGM_SPECIFICATION_EXCEPTION);
                                regs->ar[r3] = op4alet;
                            }
                        }

                        /* Load address of operand 4 */
                        op4addr = VFETCH4(effective_addr4 + 76, b4, regs);
                        op4addr &= ADDRESS_MAXWRAP(regs);
                        DW_CHECK(op4addr, regs);

                        /* Load address of operand 6 */
                        op6addr = VFETCH4(effective_addr4 + 108, b4, regs);
                        op6addr &= ADDRESS_MAXWRAP(regs);
                        DW_CHECK(op6addr, regs);

                        /* Verify access to 4th operand */
                        validate_operand (op4addr, op4alet ? r3 : 0, 8-1,
                            ACCTYPE_WRITE, regs);

                        /* Store 5th op at 6th op */
                        if(op6alet)
                            regs->ar[r3] = op6alet;
                        VSTORE8(op5, op6addr, op6alet ? r3 : 0, regs);

                        /* Store 3th op at 4th op */
                        if(op4alet)
                            regs->ar[r3] = op4alet;
                        VSTORE8(op3, op4addr, op4alet ? r3 : 0, regs);

                        /* Store 1st op replacement at 2nd op */
                        VSTORE8(op1r, effective_addr2, b2, regs);

                        regs->psw.cc = 0;
                    }
                    else
                    {
                        VSTORE8(op2, effective_addr4 + 8, b4, regs);
                        regs->psw.cc = 1;
                    }
                }
                break;


            case PLO_CSTST:
                {
                U32 op2,
                    op3,
                    op4alet = 0,
                    op4addr,
                    op5,
                    op6alet = 0,
                    op6addr,
                    op7,
                    op8alet = 0,
                    op8addr;

                    ODD_CHECK(r1, regs);
                    FW_CHECK(effective_addr2, regs);
                    FW_CHECK(effective_addr4, regs);

                    op2 = VFETCH4(effective_addr2, b2, regs);

                    if(regs->gpr[r1] == op2)
                    {
                        op3 = VFETCH4(effective_addr4 + 60, b4, regs);
                        op5 = VFETCH4(effective_addr4 + 92, b4, regs);
                        op7 = VFETCH4(effective_addr4 + 124, b4, regs);

                        /* Verify access to 2nd operand */
                        validate_operand (effective_addr2, b2, 4-1,
                            ACCTYPE_WRITE, regs);

                        /* When in ar mode, ar3 is used to access the 
                           operand. The alet is fetched from the pl */
                        if(ACCESS_REGISTER_MODE(&(regs->psw)))
                        {
                            op4alet = VFETCH4(effective_addr4 + 68, b4, regs);
                            op6alet = VFETCH4(effective_addr4 + 100, b4, regs);
                            op8alet = VFETCH4(effective_addr4 + 132, b4, regs);
                            if(op4alet || op6alet || op8alet)
                            {
                                if(r3 == 0)
                                    program_interrupt(regs, PGM_SPECIFICATION_EXCEPTION);
                                regs->ar[r3] = op4alet;
                            }
                        }

                        /* Load address of operand 4 */
                        op4addr = VFETCH4(effective_addr4 + 76, b4, regs);
                        op4addr &= ADDRESS_MAXWRAP(regs);
                        FW_CHECK(op4addr, regs);

                        /* Load address of operand 6 */
                        op6addr = VFETCH4(effective_addr4 + 108, b4, regs);
                        op6addr &= ADDRESS_MAXWRAP(regs);
                        FW_CHECK(op6addr, regs);

                        /* Load address of operand 8 */
                        op8addr = VFETCH4(effective_addr4 + 140, b4, regs);
                        op8addr &= ADDRESS_MAXWRAP(regs);
                        FW_CHECK(op8addr, regs);

                        /* Verify access to 4th operand */
                        validate_operand (op4addr, op4alet ? r3 : 0, 4-1,
                            ACCTYPE_WRITE, regs);

                        /* Verify access to 6th operand */
                        if(op6alet)
                            regs->ar[r3] = op6alet;
                        validate_operand (op6addr, op6alet ? r3 : 0, 4-1,
                            ACCTYPE_WRITE, regs);

                        /* Store 7th op at 8th op */
                        if(op8alet)
                            regs->ar[r3] = op8alet;
                        VSTORE4(op7, op8addr, op8alet ? r3 : 0, regs);

                        /* Store 5th op at 6th op */
                        if(op6alet)
                            regs->ar[r3] = op6alet;
                        VSTORE4(op5, op6addr, op6alet ? r3 : 0, regs);

                        /* Store 3rd op at 4th op */
                        if(op4alet)
                            regs->ar[r3] = op4alet;
                        VSTORE4(op3, op4addr, op4alet ? r3 : 0, regs);

                        /* Store 1st op replacement at 2nd op */
                        VSTORE4(regs->gpr[r1+1], effective_addr2, b2, regs);

                        regs->psw.cc = 0;
                    }
                    else
                    {
                        regs->gpr[r1] = op2;
                        regs->psw.cc = 1;
                    }
                }
                break;


            case PLO_CSTSTG:
                {
                U64 op1c,
                    op1r,
                    op2,
                    op3,
                    op5,
                    op7;
                U32 op4alet = 0,
                    op4addr,
                    op6alet = 0,
                    op6addr,
                    op8alet = 0,
                    op8addr;

                    DW_CHECK(effective_addr2, regs);
                    DW_CHECK(effective_addr4, regs);

                    op1c = VFETCH8(effective_addr4 + 8, b4, regs);
                    op2 = VFETCH8(effective_addr2, b2, regs);

                    if(op1c == op2)
                    {
                        op1r = VFETCH8(effective_addr4 + 24, b4, regs);
                        op3 = VFETCH8(effective_addr4 + 56, b4, regs);
                        op5 = VFETCH8(effective_addr4 + 88, b4, regs);
                        op7 = VFETCH8(effective_addr4 + 120, b4, regs);

                        /* Verify access to 2nd operand */
                        validate_operand (effective_addr2, b2, 8-1,
                            ACCTYPE_WRITE, regs);

                        /* When in ar mode, ar3 is used to access the 
                           operand. The alet is fetched from the pl */
                        if(ACCESS_REGISTER_MODE(&(regs->psw)))
                        {
                            op4alet = VFETCH4(effective_addr4 + 68, b4, regs);
                            op6alet = VFETCH4(effective_addr4 + 100, b4, regs);
                            op8alet = VFETCH4(effective_addr4 + 132, b4, regs);
                            if(op4alet || op6alet || op8alet)
                            {
                                if(r3 == 0)
                                    program_interrupt(regs, PGM_SPECIFICATION_EXCEPTION);
                                regs->ar[r3] = op4alet;
                            }
                        }

                        /* Load address of operand 4 */
                        op4addr = VFETCH4(effective_addr4 + 76, b4, regs);
                        op4addr &= ADDRESS_MAXWRAP(regs);
                        DW_CHECK(op4addr, regs);

                        /* Load address of operand 6 */
                        op6addr = VFETCH4(effective_addr4 + 108, b4, regs);
                        op6addr &= ADDRESS_MAXWRAP(regs);
                        DW_CHECK(op6addr, regs);

                        /* Load address of operand 8 */
                        op8addr = VFETCH4(effective_addr4 + 140, b4, regs);
                        op8addr &= ADDRESS_MAXWRAP(regs);
                        DW_CHECK(op8addr, regs);

                        /* Verify access to 4th operand */
                        validate_operand (op4addr, op4alet ? r3 : 0, 8-1,
                            ACCTYPE_WRITE, regs);

                        /* Verify access to 6th operand */
                        if(op6alet)
                            regs->ar[r3] = op6alet;
                        validate_operand (op6addr, op6alet ? r3 : 0, 8-1,
                            ACCTYPE_WRITE, regs);

                        /* Store 7th op at 8th op */
                        if(op8alet)
                            regs->ar[r3] = op8alet;
                        VSTORE8(op7, op8addr, op8alet ? r3 : 0, regs);

                        /* Store 5th op at 6th op */
                        if(op6alet)
                            regs->ar[r3] = op6alet;
                        VSTORE8(op5, op6addr, op6alet ? r3 : 0, regs);
                
                        /* Store 3th op at 4th op */
                        if(op4alet)
                            regs->ar[r3] = op4alet;
                        VSTORE8(op3, op4addr, op4alet ? r3 : 0, regs);

                        /* Store 1st op replacement value at 2nd op */
                        VSTORE8(op1r, effective_addr2, b2, regs);

                        regs->psw.cc = 0;
                    }
                    else
                    {
                        VSTORE8(op2, effective_addr4 + 8, b4, regs);
                        regs->psw.cc = 1;
                    }
                }
                break;

            default:
                program_interrupt(regs, PGM_SPECIFICATION_EXCEPTION);
                
        }

        /* Release main-storage access lock */
        RELEASE_MAINLOCK(regs);

#if MAX_CPU_ENGINES > 1
        /* It this is a failed locked operation
           and there is more then 1 CPU in the configuration
           and there is no broadcast synchronization in progress
           then call the hypervisor to end this timeslice,
           this to prevent this virtual CPU monopolizing
           the physical CPU on a spinlock */
        if(regs->psw.cc && sysblk.numcpu > 1
            && sysblk.brdcstncpu == 0)
            usleep(1L);
#endif MAX_CPU_ENGINES > 1

    }
}


/*-------------------------------------------------------------------*/
/* B25E SRST  - Search String                                  [RRE] */
/*-------------------------------------------------------------------*/
void zz_search_string (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
int     i;                              /* Loop counter              */
U32     addr1, addr2;                   /* End/start addresses       */
BYTE    sbyte;                          /* String character          */
BYTE    termchar;                       /* Terminating character     */

    RRE(inst, execflag, regs, r1, r2);

    /* Program check if bits 0-23 of register 0 not zero */
    if ((regs->gpr[0] & 0xFFFFFF00) != 0)
        program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Load string terminating character from register 0 bits 24-31 */
    termchar = regs->gpr[0] & 0xFF;

    /* Determine the operand end and start addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Search up to 4096 bytes until end of operand */
    for (i = 0; i < 4096; i++)
    {
        /* If operand end address has been reached, return condition
           code 2 and leave the R1 and R2 registers unchanged */
        if (addr2 == addr1)
        {
            regs->psw.cc = 2;
            return;
        }

        /* Fetch a byte from the operand */
        sbyte = vfetchb ( addr2, r2, regs );

        /* If the terminating character was found, return condition
           code 1 and load the address of the character into R1 */
        if (sbyte == termchar)
        {
            regs->gpr[r1] = addr2;
            regs->psw.cc = 1;
            return;
        }

        /* Increment operand address */
        addr2++;
        addr2 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */

    /* Set R2 to point to next character of operand */
    regs->gpr[r2] = addr2;

    /* Return condition code 3 */
    regs->psw.cc = 3;

}


/*-------------------------------------------------------------------*/
/* B24E SAR   - Set Access Register                            [RRE] */
/*-------------------------------------------------------------------*/
void zz_set_access_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Copy R2 general register to R1 access register */
    regs->ar[r1] = regs->gpr[r2];
}


/*-------------------------------------------------------------------*/
/* 04   SPM   - Set Program Mask                                [RR] */
/*-------------------------------------------------------------------*/
void zz_set_program_mask (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    /* Set condition code from bits 2-3 of R1 register */
    regs->psw.cc = ( regs->gpr[r1] & 0x30000000 ) >> 28;

    /* Set program mask from bits 4-7 of R1 register */
    regs->psw.fomask = ( regs->gpr[r1] & 0x08000000 ) >> 27;
    regs->psw.domask = ( regs->gpr[r1] & 0x04000000 ) >> 26;
    regs->psw.eumask = ( regs->gpr[r1] & 0x02000000 ) >> 25;
    regs->psw.sgmask = ( regs->gpr[r1] & 0x01000000 ) >> 24;
}


/*-------------------------------------------------------------------*/
/* 8F   SLDA  - Shift Left Double                               [RS] */
/*-------------------------------------------------------------------*/
void zz_shift_left_double (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
U32     n;                              /* 32-bit operand values     */
U64     dreg;                           /* Double register work area */
int     h, i, j, m;                     /* Integer work areas        */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    ODD_CHECK(r1, regs);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Load the signed value from the R1 and R1+1 registers */
    dreg = (U64)regs->gpr[r1] << 32 | regs->gpr[r1+1];
    m = ((S64)dreg < 0) ? 1 : 0;

    /* Shift the numeric portion of the value */
    for (i = 0, j = 0; i < n; i++)
    {
        /* Shift bits 1-63 left one bit position */
        dreg <<= 1;

        /* Overflow if bit shifted out is unlike the sign bit */
        h = ((S64)dreg < 0) ? 1 : 0;
        if (h != m)
            j = 1;
    }

    /* Load updated value into the R1 and R1+1 registers */
    regs->gpr[r1] = (dreg >> 32) & 0x7FFFFFFF;
    if (m)
        regs->gpr[r1] |= 0x80000000;
    regs->gpr[r1+1] = dreg & 0xFFFFFFFF;

    /* Condition code 3 and program check if overflow occurred */
    if (j)
    {
        regs->psw.cc = 3;
        if ( regs->psw.fomask )
            program_interrupt (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
        return;
    }

    /* Set the condition code */
    regs->psw.cc = (S64)dreg > 0 ? 2 : (S64)dreg < 0 ? 1 : 0;

}


/*-------------------------------------------------------------------*/
/* 8D   SLDL  - Shift Left Double Logical                       [RS] */
/*-------------------------------------------------------------------*/
void zz_shift_left_double_logical (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
U32     n;                              /* 32-bit operand values     */
U64     dreg;                           /* Double register work area */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    ODD_CHECK(r1, regs);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Shift the R1 and R1+1 registers */
    dreg = (U64)regs->gpr[r1] << 32 | regs->gpr[r1+1];
    dreg <<= n;
    regs->gpr[r1] = dreg >> 32;
    regs->gpr[r1+1] = dreg & 0xFFFFFFFF;

}


/*-------------------------------------------------------------------*/
/* 8B   SLA   - Shift Left Single                               [RS] */
/*-------------------------------------------------------------------*/
void zz_shift_left_single (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
U32     n, n1, n2;                      /* 32-bit operand values     */
int     i, j;                           /* Integer work areas        */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Load the numeric and sign portions from the R1 register */
    n1 = regs->gpr[r1] & 0x7FFFFFFF;
    n2 = regs->gpr[r1] & 0x80000000;

    /* Shift the numeric portion left n positions */
    for (i = 0, j = 0; i < n; i++)
    {
        /* Shift bits 1-31 left one bit position */
        n1 <<= 1;

        /* Overflow if bit shifted out is unlike the sign bit */
        if ((n1 & 0x80000000) != n2)
            j = 1;
    }

    /* Load the updated value into the R1 register */
    regs->gpr[r1] = (n1 & 0x7FFFFFFF) | n2;

    /* Condition code 3 and program check if overflow occurred */
    if (j)
    {
        regs->psw.cc = 3;
        if ( regs->psw.fomask )
            program_interrupt (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
        return;
    }

    /* Set the condition code */
    regs->psw.cc = (S32)regs->gpr[r1] > 0 ? 2 :
                   (S32)regs->gpr[r1] < 0 ? 1 : 0;

}


/*-------------------------------------------------------------------*/
/* 89   SLL   - Shift Left Single Logical                       [RS] */
/*-------------------------------------------------------------------*/
void zz_shift_left_single_logical (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
U32     n;                              /* Integer work areas        */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Shift the R1 register */
    regs->gpr[r1] = n > 31 ? 0 : regs->gpr[r1] << n;
}


/*-------------------------------------------------------------------*/
/* 8E   SRDA  - Shift Right Double                              [RS] */
/*-------------------------------------------------------------------*/
void zz_shift_right_double (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
U32     n;                              /* 32-bit operand values     */
U64     dreg;                           /* Double register work area */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    ODD_CHECK(r1, regs);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Shift the R1 and R1+1 registers */
    dreg = (U64)regs->gpr[r1] << 32 | regs->gpr[r1+1];
    dreg = (U64)((S64)dreg >> n);
    regs->gpr[r1] = dreg >> 32;
    regs->gpr[r1+1] = dreg & 0xFFFFFFFF;

    /* Set the condition code */
    regs->psw.cc = (S64)dreg > 0 ? 2 : (S64)dreg < 0 ? 1 : 0;

}


/*-------------------------------------------------------------------*/
/* 8C   SRDL  - Shift Right Double Logical                      [RS] */
/*-------------------------------------------------------------------*/
void zz_shift_right_double_logical (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
U32     n;                              /* 32-bit operand values     */
U64     dreg;                           /* Double register work area */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    ODD_CHECK(r1, regs);

        /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Shift the R1 and R1+1 registers */
    dreg = (U64)regs->gpr[r1] << 32 | regs->gpr[r1+1];
    dreg >>= n;
    regs->gpr[r1] = dreg >> 32;
    regs->gpr[r1+1] = dreg & 0xFFFFFFFF;

}


/*-------------------------------------------------------------------*/
/* 8A   SRA   - Shift Right single                              [RS] */
/*-------------------------------------------------------------------*/
void zz_shift_right_single (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
U32     n;                              /* Integer work areas        */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Shift the signed value of the R1 register */
    (S32)regs->gpr[r1] = n > 30 ?
                    ((S32)regs->gpr[r1] < 0 ? -1 : 0) :
                    (S32)regs->gpr[r1] >> n;

    /* Set the condition code */
    regs->psw.cc = (S32)regs->gpr[r1] > 0 ? 2 :
                   (S32)regs->gpr[r1] < 0 ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* 88   SRL   - Shift Right Single Logical                      [RS] */
/*-------------------------------------------------------------------*/
void zz_shift_right_single_logical (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
U32     n;                              /* Integer work areas        */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Shift the R1 register */
    regs->gpr[r1] = n > 31 ? 0 : regs->gpr[r1] >> n;
}


/*-------------------------------------------------------------------*/
/* 50   ST    - Store                                           [RX] */
/*-------------------------------------------------------------------*/
void zz_store (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Store register contents at operand address */
    vstore4 ( regs->gpr[r1], effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* 9B   STAM  - Store Access Multiple                           [RS] */
/*-------------------------------------------------------------------*/
void zz_store_access_multiple (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
int     n, d;                           /* Integer work area         */
BYTE    rwork[64];                      /* Register work area        */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    FW_CHECK(effective_addr2, regs);

    /* Copy access registers into work area */
    for ( n = r1, d = 0; ; )
    {
        /* Copy contents of one access register to work area */
        rwork[d++] = (regs->ar[n] & 0xFF000000) >> 24;
        rwork[d++] = (regs->ar[n] & 0xFF0000) >> 16;
        rwork[d++] = (regs->ar[n] & 0xFF00) >> 8;
        rwork[d++] = regs->ar[n] & 0xFF;

        /* Instruction is complete when r3 register is done */
        if ( n == r3 ) break;

        /* Update register number, wrapping from 15 to 0 */
        n++; n &= 15;
    }

    /* Store access register contents at operand address */
    vstorec ( rwork, d-1, effective_addr2, b2, regs );

}


/*-------------------------------------------------------------------*/
/* 42   STC   - Store Character                                 [RX] */
/*-------------------------------------------------------------------*/
void zz_store_character (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Store rightmost byte of R1 register at operand address */
    vstoreb ( regs->gpr[r1] & 0xFF, effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* BE   STCM  - Store Characters under Mask                     [RS] */
/*-------------------------------------------------------------------*/
void zz_store_characters_under_mask (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
U32     n;                              /* 32-bit operand values     */
int     i, j;                           /* Integer work areas        */
BYTE    cwork[4];                       /* Character work areas      */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Load value from register */
    n = regs->gpr[r1];

    /* Copy characters from register to work area */
    for ( i = 0, j = 0; i < 4; i++ )
    {
        /* Test mask bit corresponding to this character */
        if ( r3 & 0x08 )
        {
            /* Copy character from register to work area */
            cwork[j++] = n >> 24;
        }

        /* Shift mask and register for next byte */
        r3 <<= 1;
        n <<= 8;

    } /* end for(i) */

    /* If the mask is all zero, we nevertheless access one byte
       from the storage operand, because POP states that an
       access exception may be recognized on the first byte */
    if (j == 0)
    {
// /*debug*/logmsg ("Model dependent STCM use\n");
        validate_operand (effective_addr2, b2, 0, ACCTYPE_WRITE, regs);
        return;
    }

    /* Store result at operand location */
    vstorec ( cwork, j-1, effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* B205 STCK  - Store Clock                                      [S] */
/*-------------------------------------------------------------------*/
void zz_store_clock (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U64     dreg;                           /* Double word work area     */

    S(inst, execflag, regs, b2, effective_addr2);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    if(regs->sie_state && (regs->siebk->ic[2] & SIE_IC2_STCK))
        longjmp(regs->progjmp, SIE_INTERCEPT_INST);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Perform serialization before fetching clock */
    PERFORM_SERIALIZATION (regs);

    /* Update the TOD clock value */
    update_TOD_clock();

    /* Obtain the TOD clock update lock just in case the timer thread
       grabbed it while we weren't looking */
    obtain_lock (&sysblk.todlock);

    /* Retrieve the TOD clock value and shift out the epoch */
    dreg = ((sysblk.todclk + regs->todoffset) << 8) | regs->cpuad;

    /* Release the TOD clock update lock */
    release_lock (&sysblk.todlock);

// /*debug*/logmsg("Store TOD clock=%16.16llX\n", dreg);

    /* Store TOD clock value at operand address */
    vstore8 ( dreg, effective_addr2, b2, regs );

    /* Perform serialization after storing clock */
    PERFORM_SERIALIZATION (regs);

    /* Set condition code zero */
    regs->psw.cc = 0;

}


/*-------------------------------------------------------------------*/
/* B278 STCKE - Store Clock Extended                             [S] */
/*-------------------------------------------------------------------*/
void zz_store_clock_extended (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U64     dreg;                           /* Double word work area     */

    S(inst, execflag, regs, b2, effective_addr2);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    if(regs->sie_state && (regs->siebk->ic[2] & SIE_IC2_STCK))
        longjmp(regs->progjmp, SIE_INTERCEPT_INST);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Perform serialization before fetching clock */
    PERFORM_SERIALIZATION (regs);

    /* Update the TOD clock value */
    update_TOD_clock();

    /* Obtain the TOD clock update lock just in case the timer thread
       grabbed it while we weren't looking */
    obtain_lock (&sysblk.todlock);

    /* Retrieve the TOD epoch, clock bits 0-51, and 4 zeroes */
    dreg = (sysblk.todclk + regs->todoffset);

    /* Release the TOD clock update lock */
    release_lock (&sysblk.todlock);

    /* Check that all 16 bytes of the operand are accessible */
    validate_operand (effective_addr2, b2, 15, ACCTYPE_WRITE, regs);

//  /*debug*/logmsg("Store TOD clock extended: +0=%16.16llX\n",
//  /*debug*/       dreg);

    /* Store the 8 bit TOD epoch, clock bits 0-51, and bits
       20-23 of the TOD uniqueness value at operand address */
    vstore8 ( dreg, effective_addr2, b2, regs );

//  /*debug*/logmsg("Store TOD clock extended: +8=%16.16llX\n",
//  /*debug*/       dreg);

    /* Store second doubleword value at operand+8 */
    effective_addr2 += 8;
    effective_addr2 &= ADDRESS_MAXWRAP(regs);

    /* Store nonzero value in pos 72 to 111 */
    dreg = (dreg << 21) | 0x00100000 | (regs->cpuad << 16) | regs->todpr;

    vstore8 ( dreg, effective_addr2, b2, regs );

    /* Perform serialization after storing clock */
    PERFORM_SERIALIZATION (regs);

    /* Set condition code zero */
    regs->psw.cc = 0;
}


/*-------------------------------------------------------------------*/
/* 40   STH   - Store Halfword                                  [RX] */
/*-------------------------------------------------------------------*/
void zz_store_halfword (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Store rightmost 2 bytes of R1 register at operand address */
    vstore2 ( regs->gpr[r1] & 0xFFFF, effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* 90   STM   - Store Multiple                                  [RS] */
/*-------------------------------------------------------------------*/
void zz_store_multiple (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
int     i, d;                           /* Integer work area         */
BYTE    rwork[64];                      /* Register work area        */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Copy register contents into work area */
    for ( i = r1, d = 0; ; )
    {
        /* Copy contents of one register to work area */
        rwork[d++] = (regs->gpr[i] & 0xFF000000) >> 24;
        rwork[d++] = (regs->gpr[i] & 0xFF0000) >> 16;
        rwork[d++] = (regs->gpr[i] & 0xFF00) >> 8;
        rwork[d++] = regs->gpr[i] & 0xFF;

        /* Instruction is complete when r3 register is done */
        if ( i == r3 ) break;

        /* Update register number, wrapping from 15 to 0 */
        i++; i &= 15;
    }

    /* Store register contents at operand address */
    vstorec ( rwork, d-1, effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* 1B   SR    - Subtract Register                               [RR] */
/*-------------------------------------------------------------------*/
void zz_subtract_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    /* Subtract signed operands and set condition code */
    regs->psw.cc =
            sub_signed (&(regs->gpr[r1]),
                    regs->gpr[r1],
                    regs->gpr[r2]);

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        program_interrupt (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
}


/*-------------------------------------------------------------------*/
/* 5B   S     - Subtract                                        [RX] */
/*-------------------------------------------------------------------*/
void zz_subtract (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = vfetch4 ( effective_addr2, b2, regs );

    /* Subtract signed operands and set condition code */
    regs->psw.cc =
            sub_signed (&(regs->gpr[r1]),
                    regs->gpr[r1],
                    n);

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        program_interrupt (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);

}


/*-------------------------------------------------------------------*/
/* 4B   SH    - Subtract Halfword                               [RX] */
/*-------------------------------------------------------------------*/
void zz_subtract_halfword (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load 2 bytes from operand address */
    n = vfetch2 ( effective_addr2, b2, regs );

    /* Propagate sign bit to leftmost 2 bytes of operand */
    if ( n > 0x7FFF )
        n |= 0xFFFF0000;

    /* Subtract signed operands and set condition code */
    regs->psw.cc =
            sub_signed (&(regs->gpr[r1]),
                    regs->gpr[r1],
                    n);

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        program_interrupt (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);

}


/*-------------------------------------------------------------------*/
/* 1F   SLR   - Subtract Logical Register                       [RR] */
/*-------------------------------------------------------------------*/
void zz_subtract_logical_register (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RR(inst, execflag, regs, r1, r2);

    /* Subtract unsigned operands and set condition code */
    regs->psw.cc =
            sub_logical (&(regs->gpr[r1]),
                    regs->gpr[r1],
                    regs->gpr[r2]);
}


/*---------------------------------------------------------------*/
/* 5F   SL    - Subtract Logical                                [RX] */
/*---------------------------------------------------------------*/
void zz_subtract_logical (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = vfetch4 ( effective_addr2, b2, regs );

    /* Subtract unsigned operands and set condition code */
    regs->psw.cc =
            sub_logical (&(regs->gpr[r1]),
                    regs->gpr[r1],
                    n);
}


/*-------------------------------------------------------------------*/
/* 0A   SVC   - Supervisor Call                                 [RR] */
/*-------------------------------------------------------------------*/
void zz_supervisor_call (BYTE inst[], int execflag, REGS *regs)
{
BYTE    i;                              /* Instruction byte 1        */
PSA    *psa;                            /* -> prefixed storage area  */
U32     pxr;                            /* prefix                    */
int     rc;                             /* Return code               */

    RR_SVC(inst, execflag, regs, i);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    if(regs->sie_state &&
      ( (regs->siebk->svc_ctl[0] & SIE_SVC0_ALL)
        || ( (regs->siebk->svc_ctl[0] & SIE_SVC0_1N) && 
              regs->siebk->svc_ctl[1] == i)
        || ( (regs->siebk->svc_ctl[0] & SIE_SVC0_2N) && 
              regs->siebk->svc_ctl[2] == i)
        || ( (regs->siebk->svc_ctl[0] & SIE_SVC0_3N) && 
              regs->siebk->svc_ctl[3] == i) ))
        longjmp(regs->progjmp, SIE_INTERCEPT_INST);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    pxr = regs->pxr;
    SIE_TRANSLATE(&pxr, ACCTYPE_WRITE, regs);

    /* Set the main storage reference and change bits */
    STORAGE_KEY(pxr) |= (STORKEY_REF | STORKEY_CHANGE);

    /* Use the I-byte to set the SVC interruption code */
    regs->psw.intcode = i;

    /* Point to PSA in main storage */
    psa = (PSA*)(sysblk.mainstor + pxr);

    /* For ECMODE, store SVC interrupt code at PSA+X'88' */
    if ( regs->psw.ecmode )
    {
        psa->svcint[0] = 0;
        psa->svcint[1] = regs->psw.ilc;
        psa->svcint[2] = 0;
        psa->svcint[3] = i;
        FRAG_INVALIDATE(pxr, 512);
    }

    /* Store current PSW at PSA+X'20' */
    store_psw ( regs, psa->svcold );

    /* Load new PSW from PSA+X'60' */
    rc = load_psw ( regs, psa->svcnew );
    obtain_lock(&sysblk.intlock);
    set_doint(regs);
    release_lock(&sysblk.intlock);
    if ( rc )
        program_interrupt (regs, rc);

    /* Perform serialization and checkpoint synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    LASTPAGE_INVALIDATE(regs);
    REASSIGN_FRAG(regs);

}


/*-------------------------------------------------------------------*/
/* 93   TS    - Test and Set                                     [S] */
/*-------------------------------------------------------------------*/
void zz_test_and_set (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
BYTE    obyte;                          /* Operand byte              */

    S(inst, execflag, regs, b2, effective_addr2);

    /* Perform serialization before starting operation */
    PERFORM_SERIALIZATION (regs);

    /* Obtain main-storage access lock */
    OBTAIN_MAINLOCK(regs);

    /* Fetch byte from operand address */
    obyte = vfetchb ( effective_addr2, b2, regs );

    /* Set all bits of operand to ones */
    vstoreb ( 0xFF, effective_addr2, b2, regs );

    /* Release main-storage access lock */
    RELEASE_MAINLOCK(regs);

    /* Set condition code from leftmost bit of operand byte */
    regs->psw.cc = obyte >> 7;

    /* Perform serialization after completing operation */
    PERFORM_SERIALIZATION (regs);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    if(regs->sie_state && (regs->siebk->ic[0] & SIE_IC0_TS1))
        longjmp(regs->progjmp, SIE_INTERCEPT_INST);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

}


/*-------------------------------------------------------------------*/
/* 91   TM    - Test under Mask                                 [SI] */
/*-------------------------------------------------------------------*/
void zz_test_under_mask (BYTE inst[], int execflag, REGS *regs)
{
BYTE    i2;                             /* Immediate operand         */
int     b1;                             /* Base of effective addr    */
U32     effective_addr1;                /* Effective address         */
BYTE    tbyte;                          /* Work byte                 */

    SI(inst, execflag, regs, i2, b1, effective_addr1);

    /* Fetch byte from operand address */
    tbyte = vfetchb ( effective_addr1, b1, regs );

    /* AND with immediate operand mask */
    tbyte &= i2;

    /* Set condition code according to result */
    regs->psw.cc =
            ( tbyte == 0 ) ? 0 :            /* result all zeroes */
            ((tbyte^i2) == 0) ? 3 :         /* result all ones   */
            1 ;                             /* result mixed      */
}


/*-------------------------------------------------------------------*/
/* A7x0 TMH   - Test under Mask High                            [RI] */
/*-------------------------------------------------------------------*/
void zz_test_under_mask_high (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */
U16     h1;                             /* 16-bit operand values     */
U16     h2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    /* AND register bits 0-15 with immediate operand */
    h1 = i2 & (regs->gpr[r1] >> 16);

    /* Isolate leftmost bit of immediate operand */
    for ( h2 = 0x8000; h2 != 0 && (h2 & i2) == 0; h2 >>= 1 );

    /* Set condition code according to result */
    regs->psw.cc =
            ( h1 == 0 ) ? 0 :           /* result all zeroes */
            ((h1 ^ i2) == 0) ? 3 :      /* result all ones   */
            ((h1 & h2) == 0) ? 1 :      /* leftmost bit zero */
            2;                          /* leftmost bit one  */
}


/*-------------------------------------------------------------------*/
/* A7x1 TML   - Test under Mask Low                             [RI] */
/*-------------------------------------------------------------------*/
void zz_test_under_mask_low (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */
U16     h1;                             /* 16-bit operand values     */
U16     h2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    /* AND register bits 16-31 with immediate operand */
    h1 = i2 & (regs->gpr[r1] & 0xFFFF);

    /* Isolate leftmost bit of immediate operand */
    for ( h2 = 0x8000; h2 != 0 && (h2 & i2) == 0; h2 >>= 1 );

    /* Set condition code according to result */
    regs->psw.cc =
            ( h1 == 0 ) ? 0 :           /* result all zeroes */
            ((h1 ^ i2) == 0) ? 3 :      /* result all ones   */
            ((h1 & h2) == 0) ? 1 :      /* leftmost bit zero */
            2;                          /* leftmost bit one  */

}


/*-------------------------------------------------------------------*/
/* DC   TR    - Translate                                       [SS] */
/*-------------------------------------------------------------------*/
void zz_translate (BYTE inst[], int execflag, REGS *regs)
{
int     l;                              /* Lenght byte               */
int     b1, b2;                         /* Values of base field      */
U32     effective_addr1,
        effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */
BYTE    sbyte;                          /* Byte work areas           */
int     d;                              /* Integer work areas        */
int     h;                              /* Integer work areas        */
int     i;                              /* Integer work areas        */
BYTE    cwork[256];                     /* Character work areas      */

    SS_L(inst, execflag, regs, l, b1, effective_addr1,
                                  b2, effective_addr2);

    /* Validate the first operand for write access */
    validate_operand (effective_addr1, b1, l, ACCTYPE_WRITE, regs);

    /* Fetch first operand into work area */
    vfetchc ( cwork, l, effective_addr1, b1, regs );

    /* Determine the second operand range by scanning the
       first operand to find the bytes with the highest
       and lowest values */
    for ( i = 0, d = 255, h = 0; i <= l; i++ )
    {
        if (cwork[i] < d) d = cwork[i];
        if (cwork[i] > h) h = cwork[i];
    }

    /* Validate the referenced portion of the second operand */
    n = (effective_addr2 + d) & ADDRESS_MAXWRAP(regs);
    validate_operand (n, b2, h-d, ACCTYPE_READ, regs);

    /* Process first operand from left to right, refetching
       second operand and storing the result byte by byte
       to ensure correct handling of overlapping operands */
    for ( i = 0; i <= l; i++ )
    {
        /* Fetch byte from second operand */
        n = (effective_addr2 + cwork[i]) & ADDRESS_MAXWRAP(regs);
        sbyte = vfetchb ( n, b2, regs );

        /* Store result at first operand address */
        vstoreb ( sbyte, effective_addr1, b1, regs );

        /* Increment first operand address */
        effective_addr1++;
        effective_addr1 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */
}


/*-------------------------------------------------------------------*/
/* DD   TRT   - Translate and Test                              [SS] */
/*-------------------------------------------------------------------*/
void zz_translate_and_test (BYTE inst[], int execflag, REGS *regs)
{
int     l;                              /* Lenght byte               */
int     b1, b2;                         /* Values of base field      */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
int     cc = 0;                         /* Condition code            */
BYTE    sbyte;                          /* Byte work areas           */
BYTE    dbyte;                          /* Byte work areas           */
int     i;                              /* Integer work areas        */

    SS_L(inst, execflag, regs, l, b1, effective_addr1,
                                  b2, effective_addr2);

    /* Process first operand from left to right */
    for ( i = 0; i <= l; i++ )
    {
        /* Fetch argument byte from first operand */
        dbyte = vfetchb ( effective_addr1, b1, regs );

        /* Fetch function byte from second operand */
        sbyte = vfetchb ( effective_addr2 + dbyte, b2, regs );

        /* Test for non-zero function byte */
        if (sbyte != 0) {

            /* Store address of argument byte in register 1 */
            if ( regs->psw.amode )
            {
                regs->gpr[1] = effective_addr1;
            } else {
                regs->gpr[1] &= 0xFF000000;
                regs->gpr[1] |= effective_addr1;
            }

            /* Store function byte in low-order byte of reg.2 */
            regs->gpr[2] &= 0xFFFFFF00;
            regs->gpr[2] |= sbyte;

            /* Set condition code 2 if argument byte was last byte
               of first operand, otherwise set condition code 1 */
            cc = (i == l) ? 2 : 1;

            /* Terminate the operation at this point */
            break;

        } /* end if(sbyte) */

        /* Increment first operand address */
        effective_addr1++;
        effective_addr1 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */

    /* Update the condition code */
    regs->psw.cc = cc;
}


#ifdef FEATURE_EXTENDED_TRANSLATION
/*-------------------------------------------------------------------*/
/* B2A5 TRE - Translate Extended                               [RRE] */
/*-------------------------------------------------------------------*/
void zz_translate_extended (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
int     i;                              /* Loop counter              */
int     cc = 0;                         /* Condition code            */
U32     addr1, addr2;                   /* Operand addresses         */
U32     len1;                           /* Operand length            */
BYTE    byte1, byte2;                   /* Operand bytes             */
BYTE    tbyte;                          /* Test byte                 */
BYTE    trtab[256];                     /* Translate table           */

    RRE(inst, execflag, regs, r1, r2);

    ODD_CHECK(r1, regs);

    /* Load the test byte from bits 24-31 of register 0 */
    tbyte = regs->gpr[0] & 0xFF;

    /* Load the operand addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Load first operand length from R1+1 */
    len1 = regs->gpr[r1+1];

    /* Fetch second operand into work area.
       [7.5.101] Access exceptions for all 256 bytes of the second
       operand may be recognized, even if not all bytes are used */
    vfetchc ( trtab, 255, addr2, r2, regs );

    /* Process first operand from left to right */
    for (i = 0; len1 > 0; i++)
    {
        /* If 4096 bytes have been compared, exit with condition code 3 */
        if (i >= 4096)
        {
            cc = 3;
            break;
        }

        /* Fetch byte from first operand */
        byte1 = vfetchb ( addr1, r1, regs );

        /* If equal to test byte, exit with condition code 1 */
        if (byte1 == tbyte)
        {
            cc = 1;
            break;
        }

        /* Load indicated byte from translate table */
        byte2 = trtab[byte1];

        /* Store result at first operand address */
        vstoreb ( byte2, addr1, r1, regs );
        addr1++;
        addr1 &= ADDRESS_MAXWRAP(regs);
        len1--;

        /* Update the registers */
        regs->gpr[r1] = addr1;
        regs->gpr[r1+1] = len1;

    } /* end for(i) */

    /* Set condition code */
    regs->psw.cc =  cc;

} /* end zz_translate_extended */
#endif /*FEATURE_EXTENDED_TRANSLATION*/


/*-------------------------------------------------------------------*/
/* F3   UNPK  - Unpack                                          [SS] */
/*-------------------------------------------------------------------*/
void zz_unpack (BYTE inst[], int execflag, REGS *regs)
{
int     l1, l2;                         /* Register numbers          */
int     b1, b2;                         /* Base registers            */
U32     effective_addr1,
        effective_addr2;                /* Effective addressES       */
int     i, j;                           /* Loop counters             */
BYTE    sbyte;                          /* Source operand byte       */
BYTE    rbyte;                          /* Right result byte of pair */
BYTE    lbyte;                          /* Left result byte of pair  */

    SS(inst, execflag, regs, l1, l2, b1, effective_addr1,
                                     b2, effective_addr2);

    /* Validate the operands for addressing and protection */
    validate_operand (effective_addr1, b1, l1, ACCTYPE_WRITE, regs);
    validate_operand (effective_addr2, b2, l2, ACCTYPE_READ, regs);

    /* Exchange the digits in the rightmost byte */
    effective_addr1 += l1;
    effective_addr2 += l2;
    sbyte = vfetchb ( effective_addr2, b2, regs );
    rbyte = (sbyte << 4) | (sbyte >> 4);
    vstoreb ( rbyte, effective_addr1, b1, regs );

    /* Process remaining bytes from right to left */
    for (i = l1, j = l2; i > 0; i--)
    {
        /* Fetch source byte from second operand */
        if (j-- > 0)
        {
            sbyte = vfetchb ( --effective_addr2, b2, regs );
            rbyte = (sbyte & 0x0F) | 0xF0;
            lbyte = (sbyte >> 4) | 0xF0;
        }
        else
        {
            rbyte = 0xF0;
            lbyte = 0xF0;
        }

        /* Store unpacked bytes at first operand address */
        vstoreb ( rbyte, --effective_addr1, b1, regs );
        if (--i > 0)
        {
            effective_addr1 &= ADDRESS_MAXWRAP(regs);
            vstoreb ( lbyte, --effective_addr1, b1, regs );
        }

        /* Wraparound according to addressing mode */
        effective_addr1 &= ADDRESS_MAXWRAP(regs);
        effective_addr2 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */

}


/*-------------------------------------------------------------------*/
/* 0102 UPT   - Update Tree                                      [E] */
/*              (c) Copyright Peter Kuschnerus, 1999                 */
/*-------------------------------------------------------------------*/
void zz_update_tree (BYTE inst[], int execflag, REGS *regs)
{
U32     tempword1;                      /* TEMPWORD1                 */
U32     tempword2;                      /* TEMPWORD2                 */
U32     tempword3;                      /* TEMPWORD3                 */
U32     tempaddress;                    /* TEMPADDRESS               */
int     cc;                             /* Condition code            */
int     ar1 = 4;                        /* Access register number    */

    E(inst, execflag, regs);

    /* Check GR4, GR5 doubleword alligned */
    if ( regs->gpr[4] & 0x00000007 || regs->gpr[5] & 0x00000007 )
        program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Loop until break */
    for (;;)
    {
        tempword1 = (regs->gpr[5] >> 1) & 0xFFFFFFF8;
        if ( tempword1 == 0 )
        {
            regs->gpr[5] = 0;
            cc = 1;
            break;
        }

        regs->gpr[5] = tempword1;

        /* Check bit 0 of GR0 */
        if ( ((U32) regs->gpr[0]) < 0 )
        {
            cc = 3;
            break;
        }

        tempaddress = regs->gpr[4] + tempword1;

        /* Fetch doubleword from tempaddress to tempword2 and tempword3 */
        tempaddress &= ADDRESS_MAXWRAP(regs);
        tempword2 = vfetch4 ( tempaddress, ar1, regs );
        tempword3 = vfetch4 ( tempaddress + 4, ar1, regs );

        if ( regs->gpr[0] == tempword2 )
        {
            /* Load GR2 and GR3 from tempword2 and tempword3 */
            regs->gpr[2] = tempword2;
            regs->gpr[3] = tempword3;

            cc = 0;
            break;
        }

        if ( regs->gpr[0] < tempword2 )
        {
            /* Store doubleword from GR0 and GR1 to tempaddress */
            vstore4 ( regs->gpr[0], tempaddress, ar1, regs );
            vstore4 ( regs->gpr[1], tempaddress + 4, ar1, regs );

            /* Load GR0 and GR1 from tempword2 and tempword3 */
            regs->gpr[0] = tempword2;
            regs->gpr[1] = tempword3;
        }
    }

    regs->psw.cc = cc;
}



