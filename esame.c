/* ESAME.C      (c) Copyright Jan Jaeger, 2000-2001                  */
/*              ESAME (z/Architecture) instructions                  */

/*-------------------------------------------------------------------*/
/* This module implements the instructions which exist in ESAME      */
/* mode but not in ESA/390 mode, as described in the manual          */
/* SA22-7832-00 z/Architecture Principles of Operation               */
/*-------------------------------------------------------------------*/

#include "hercules.h"

#include "opcode.h"

#include "inline.h"

#if !defined(_LONG_MATH)
#define _LONG_MATH
/* These routines need to go into inline.h *JJ */
static inline int add_logical_long(U64 *result, U64 op1, U64 op2)
{
    *result = op1 + op2;

    return (*result == 0 ? 0 : 1) | (op1 > *result ? 2 : 0);
}
        

static inline int sub_logical_long(U64 *result, U64 op1, U64 op2)
{
    *result = op1 - op2;

    return (*result == 0 ? 0 : 1) | (op1 < *result ? 0 : 2);
}
        

static inline int add_signed_long(U64 *result, U64 op1, U64 op2)
{
    *result = (S64)op1 + (S64)op2;

    return (((S64)op1 < 0 && (S64)op2 < 0 && (S64)*result >= 0)
      || ((S64)op1 >= 0 && (S64)op2 >= 0 && (S64)*result < 0)) ? 3 :
                                              (S64)*result < 0 ? 1 :
                                              (S64)*result > 0 ? 2 : 0;
}
        

static inline int sub_signed_long(U64 *result, U64 op1, U64 op2)
{
    *result = (S64)op1 - (S64)op2;

    return (((S64)op1 < 0 && (S64)op2 >= 0 && (S64)*result >= 0)
      || ((S64)op1 >= 0 && (S64)op2 < 0 && (S64)*result < 0)) ? 3 :
                                             (S64)*result < 0 ? 1 :
                                             (S64)*result > 0 ? 2 : 0;
}
#endif /*!defined(_LONG_MATH)*/


#if defined(FEATURE_ESAME)
/*-------------------------------------------------------------------*/
/* B988 ALCGR - Add Logical with Carry Long Register           [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(add_logical_carry_long_register)
{
int     r1, r2;                         /* Values of R fields        */
int     carry = 0;

    RRE(inst, execflag, regs, r1, r2);

    /* Add the carry to operand */
    if(regs->psw.cc & 2)
        carry = add_logical_long(&(regs->GR_G(r1)),
                                   regs->GR_G(r1),
                                   1) & 2;

    /* Add unsigned operands and set condition code */
    regs->psw.cc = add_logical_long(&(regs->GR_G(r1)),
                                      regs->GR_G(r1),
                                      regs->GR_G(r2)) | carry;
}


/*-------------------------------------------------------------------*/
/* B989 SLBGR - Subtract Logical with Borrow Long Register     [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_logical_borrow_long_register)
{
int     r1, r2;                         /* Values of R fields        */
int     borrow = 0;

    RRE(inst, execflag, regs, r1, r2);

    /* Subtract the borrow from operand */
    if(!(regs->psw.cc & 2))
        borrow = sub_logical_long(&(regs->GR_G(r1)),
                                    regs->GR_G(r1),
                                    1) & 2;

    /* Subtract unsigned operands and set condition code */
    regs->psw.cc = sub_logical_long(&(regs->GR_G(r1)),
                                      regs->GR_G(r1),
                                      regs->GR_G(r2)) & ~borrow;

}


/*-------------------------------------------------------------------*/
/* E388 ALCG  - Add Logical with Carry Long                    [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(add_logical_carry_long)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U64     n;                              /* 64-bit operand values     */
int     carry = 0;

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );

    /* Add the carry to operand */
    if(regs->psw.cc & 2)
        carry = add_logical_long(&(regs->GR_G(r1)),
                                   regs->GR_G(r1),
                                   1) & 2;

    /* Add unsigned operands and set condition code */
    regs->psw.cc = add_logical_long(&(regs->GR_G(r1)),
                                      regs->GR_G(r1),
                                      n) | carry;
}


/*-------------------------------------------------------------------*/
/* E389 SLBG  - Subtract Logical with Borrow Long              [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_logical_borrow_long)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U64     n;                              /* 64-bit operand values     */
int     borrow = 0;

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );

    /* Subtract the borrow from operand */
    if(!(regs->psw.cc & 2))
        borrow = sub_logical_long(&(regs->GR_G(r1)),
                                    regs->GR_G(r1),
                                    1) & 2;

    /* Subtract unsigned operands and set condition code */
    regs->psw.cc = sub_logical_long(&(regs->GR_G(r1)),
                                      regs->GR_G(r1),
                                      n) & ~borrow;

}


/*-------------------------------------------------------------------*/
/* B998 ALCR  - Add Logical with Carry Register                [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(add_logical_carry_register)
{
int     r1, r2;                         /* Values of R fields        */
int     carry = 0;

    RRE(inst, execflag, regs, r1, r2);

    /* Add the carry to operand */
    if(regs->psw.cc & 2)
        carry = add_logical(&(regs->GR_L(r1)),
                              regs->GR_L(r1),
                              1) & 2;

    /* Add unsigned operands and set condition code */
    regs->psw.cc = add_logical(&(regs->GR_L(r1)),
                                 regs->GR_L(r1),
                                 regs->GR_L(r2)) | carry;
}


/*-------------------------------------------------------------------*/
/* B999 SLBR  - Subtract Logical with Borrow Register          [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_logical_borrow_register)
{
int     r1, r2;                         /* Values of R fields        */
int     borrow = 0;

    RRE(inst, execflag, regs, r1, r2);

    /* Subtract the borrow from operand */
    if(!(regs->psw.cc & 2))
        borrow = sub_logical(&(regs->GR_L(r1)),
                               regs->GR_L(r1),
                               1) & 2;

    /* Subtract unsigned operands and set condition code */
    regs->psw.cc = sub_logical(&(regs->GR_L(r1)),
                                 regs->GR_L(r1),
                                 regs->GR_L(r2)) & ~borrow;

}


/*-------------------------------------------------------------------*/
/* E398 ALC   - Add Logical with Carry                         [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(add_logical_carry)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */
int     carry = 0;

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch4) ( effective_addr2, b2, regs );

    /* Add the carry to operand */
    if(regs->psw.cc & 2)
        carry = add_logical(&(regs->GR_L(r1)),
                              regs->GR_L(r1),
                              1) & 2;

    /* Add unsigned operands and set condition code */
    regs->psw.cc = add_logical(&(regs->GR_L(r1)),
                                 regs->GR_L(r1),
                                 n) | carry;
}


/*-------------------------------------------------------------------*/
/* E399 SLB   - Subtract Logical with Borrow                   [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_logical_borrow)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */
int     borrow = 0;

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch4) ( effective_addr2, b2, regs );

    /* Subtract the borrow from operand */
    if(!(regs->psw.cc & 2))
        borrow = sub_logical(&(regs->GR_L(r1)),
                               regs->GR_L(r1),
                               1) & 2;

    /* Subtract unsigned operands and set condition code */
    regs->psw.cc = sub_logical(&(regs->GR_L(r1)),
                                 regs->GR_L(r1),
                                 n) & ~borrow;

}


/*-------------------------------------------------------------------*/
/* E30D DSG   - Divide Single Long                             [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(divide_single_long)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
S64     n;                              /* 64-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    ODD_CHECK(r1, regs);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );

    if(n == 0
      || (n == -1LL &&
          regs->GR_G(r1) == 0x8000000000000000ULL))
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_DIVIDE_EXCEPTION);

    regs->GR_G(r1) = (S64)regs->GR_G(r1 + 1) % (S64)n;
    regs->GR_G(r1 + 1) = (S64)regs->GR_G(r1 + 1) / (S64)n;

}


/*-------------------------------------------------------------------*/
/* E31D DSGF  - Divide Single Long Fullword                    [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(divide_single_long_fullword)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
S32     n;                              /* 64-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    ODD_CHECK(r1, regs);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch4) ( effective_addr2, b2, regs );

    if(n == 0
      || (n == -1 &&
          regs->GR_G(r1) == 0x8000000000000000ULL))
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_DIVIDE_EXCEPTION);

    regs->GR_G(r1) = (S64)regs->GR_G(r1 + 1) % (S32)n;
    regs->GR_G(r1 + 1) = (S64)regs->GR_G(r1 + 1) / (S32)n;

}


/*-------------------------------------------------------------------*/
/* B90D DSGR  - Divide Single Long Register                    [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(divide_single_long_register)
{
int     r1, r2;                         /* Values of R fields        */
S64     n;

    RRE(inst, execflag, regs, r1, r2);

    ODD_CHECK(r1, regs);

    if(regs->GR_G(r2) == 0
      || ((S64)regs->GR_G(r2) == -1LL &&
          regs->GR_G(r1) == 0x8000000000000000ULL))
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_DIVIDE_EXCEPTION);

    n = (S64)regs->GR_G(r2);

    /* Divide signed registers */
    regs->GR_G(r1) = (S64)regs->GR_G(r1 + 1) % (S64)n;
    regs->GR_G(r1 + 1) = (S64)regs->GR_G(r1 + 1) / (S64)n;

}


/*-------------------------------------------------------------------*/
/* B91D DSGFR - Divide Single Long Fullword Register           [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(divide_single_long_fullword_register)
{
int     r1, r2;                         /* Values of R fields        */
S32     n;

    RRE(inst, execflag, regs, r1, r2);

    ODD_CHECK(r1, regs);

    if(regs->GR_L(r2) == 0
      || ((S32)regs->GR_L(r2) == -1 &&
          regs->GR_G(r1) == 0x8000000000000000ULL))
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_DIVIDE_EXCEPTION);

    n = (S32)regs->GR_L(r2);

    /* Divide signed registers */
    regs->GR_G(r1) = (S64)regs->GR_G(r1 + 1) % (S32)n;
    regs->GR_G(r1 + 1) = (S64)regs->GR_G(r1 + 1) / (S32)n;

}


/*-------------------------------------------------------------------*/
/* E390 LLGC  - Load Logical Character                         [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_logical_character)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    regs->GR_G(r1) = ARCH_DEP(vfetchb) ( effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* E391 LLGH  - Load Logical Halfword                          [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_logical_halfword)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    regs->GR_G(r1) = ARCH_DEP(vfetch2) ( effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* E38E STPQ  - Store Pair to Quadword                         [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(store_pair_to_quadword)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
QWORD   qwork;                          /* Quadword work area        */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    ODD_CHECK(r1, regs);

    /* Store regs in workarea */
    STORE_DW(qwork, regs->GR_G(r1));
    STORE_DW(qwork+8, regs->GR_G(r1+1));

    /* Store R1 and R1+1 registers to second operand
       Provide storage consistancy by means of obtaining 
       the main storage access lock */
    OBTAIN_MAINLOCK(regs);
    ARCH_DEP(vstorec) ( qwork, 16-1, effective_addr2, b2, regs );
    RELEASE_MAINLOCK(regs);
}


/*-------------------------------------------------------------------*/
/* E38F LPQ   - Load Pair from Quadword                        [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_pair_from_quadword)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
QWORD   qwork;                          /* Quadword work area        */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    ODD_CHECK(r1, regs);

    /* Load R1 and R1+1 registers contents from second operand
       Provide storage consistancy by means of obtaining 
       the main storage access lock */
    OBTAIN_MAINLOCK(regs);
    ARCH_DEP(vfetchc) ( qwork, 16-1, effective_addr2, b2, regs );
    RELEASE_MAINLOCK(regs);

    /* Load regs from workarea */
    FETCH_DW(regs->GR_G(r1), qwork);
    FETCH_DW(regs->GR_G(r1+1), qwork+8);
}


/*-------------------------------------------------------------------*/
/* B90E EREGG - Extract Stacked Registers Long                 [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(extract_stacked_registers_long)
{
int     r1, r2;                         /* Values of R fields        */
LSED    lsed;                           /* Linkage stack entry desc. */
VADR    lsea;                           /* Linkage stack entry addr  */

    RRE(inst, execflag, regs, r1, r2);

    SIE_MODE_XC_OPEX(regs);

    /* Find the virtual address of the entry descriptor
       of the current state entry in the linkage stack */
    lsea = ARCH_DEP(locate_stack_entry) (0, &lsed, regs);

    /* Load registers from the stack entry */
    ARCH_DEP(unstack_registers) (1, lsea, r1, r2, regs);

}


/*-------------------------------------------------------------------*/
/* B98D EPSW  - Extract PSW                                    [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(extract_psw)
{
int     r1, r2;                         /* Values of R fields        */
QWORD   currpsw;                        /* Work area for PSW         */

    RRE(inst, execflag, regs, r1, r2);

    /* Store the current PSW in work area */
    ARCH_DEP(store_psw) (regs, currpsw);

    /* Load PSW bits 0-31 into bits 32-63 of the R1 register */
    FETCH_FW(regs->GR_L(r1), currpsw);

    /* If R2 specifies a register other than register zero,
       load PSW bits 32-63 into bits 32-63 of the R2 register */
    if(r2 != 0)
        FETCH_FW(regs->GR_L(r2), currpsw+4);

}


/*-------------------------------------------------------------------*/
/* B99D ESEA  - Extract and Set Extended Authority             [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(extract_and_set_extended_authority)
{
int     r1, unused;                     /* Value of R field          */

    RRE(inst, execflag, regs, r1, unused);

    PRIV_CHECK(regs);

    regs->GR_LHH(r1) = regs->CR_LHH(8);
    regs->CR_LHH(8) = regs->GR_LHL(r1);

    INVALIDATE_AIA(regs);
    INVALIDATE_AEA_ALL(regs);
}


/*-------------------------------------------------------------------*/
/* C0x0 LARL  - Load Address Relative Long                     [RIL] */
/*-------------------------------------------------------------------*/
DEF_INST(load_address_relative_long)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U32     i2;                             /* 32-bit operand values     */

    RIL(inst, execflag, regs, r1, opcd, i2);

    GR_A(r1, regs) = ((!execflag ? (regs->psw.IA - 6) : regs->ET)
                                 + 2*(S32)i2) & ADDRESS_MAXWRAP(regs);
}


/*-------------------------------------------------------------------*/
/* A5x0 IIHH  - Insert Immediate High High                      [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(insert_immediate_high_high)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_HHH(r1) = i2;
}


/*-------------------------------------------------------------------*/
/* A5x1 IIHL  - Insert Immediate High Low                       [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(insert_immediate_high_low)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_HHL(r1) = i2;
}


/*-------------------------------------------------------------------*/
/* A5x2 IILH  - Insert Immediate Low High                       [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(insert_immediate_low_high)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_LHH(r1) = i2;
}


/*-------------------------------------------------------------------*/
/* A5x3 IILL  - Insert Immediate Low Low                        [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(insert_immediate_low_low)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_LHL(r1) = i2;
}


/*-------------------------------------------------------------------*/
/* A5x4 NIHH  - And Immediate High High                         [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(and_immediate_high_high)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_HHH(r1) &= i2;

    /* Set condition code according to result */
    regs->psw.cc = regs->GR_HHH(r1) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* A5x5 NIHL  - And Immediate High Low                          [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(and_immediate_high_low)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_HHL(r1) &= i2;

    /* Set condition code according to result */
    regs->psw.cc = regs->GR_HHL(r1) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* A5x6 NILH  - And Immediate Low High                          [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(and_immediate_low_high)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_LHH(r1) &= i2;

    /* Set condition code according to result */
    regs->psw.cc = regs->GR_LHH(r1) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* A5x7 NILL  - And Immediate Low Low                           [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(and_immediate_low_low)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_LHL(r1) &= i2;

    /* Set condition code according to result */
    regs->psw.cc = regs->GR_LHL(r1) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* A5x8 OIHH  - Or Immediate High High                          [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(or_immediate_high_high)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_HHH(r1) |= i2;

    /* Set condition code according to result */
    regs->psw.cc = regs->GR_HHH(r1) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* A5x9 OIHL  - Or Immediate High Low                           [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(or_immediate_high_low)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_HHL(r1) |= i2;

    /* Set condition code according to result */
    regs->psw.cc = regs->GR_HHL(r1) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* A5xA OILH  - Or Immediate Low High                           [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(or_immediate_low_high)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_LHH(r1) |= i2;

    /* Set condition code according to result */
    regs->psw.cc = regs->GR_LHH(r1) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* A5xB OILL  - Or Immediate Low Low                            [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(or_immediate_low_low)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_LHL(r1) |= i2;

    /* Set condition code according to result */
    regs->psw.cc = regs->GR_LHL(r1) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* A5xC LLIHH - Load Logical Immediate High High                [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(load_logical_immediate_high_high)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_G(r1) = (U64)i2 << 48;
}


/*-------------------------------------------------------------------*/
/* A5xD LLIHL - Load Logical Immediate High Low                 [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(load_logical_immediate_high_low)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_G(r1) = (U64)i2 << 32;
}


/*-------------------------------------------------------------------*/
/* A5xE LLILH - Load Logical Immediate Low High                 [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(load_logical_immediate_low_high)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_G(r1) = (U64)i2 << 16;
}


/*-------------------------------------------------------------------*/
/* A5xF LLILL - Load Logical Immediate Low Low                  [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(load_logical_immediate_low_low)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    regs->GR_G(r1) = (U64)i2;
}


/*-------------------------------------------------------------------*/
/* C0x4 BRCL  - Branch Relative on Condition                   [RIL] */
/*-------------------------------------------------------------------*/
DEF_INST(branch_relative_on_condition_long)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U32     i2;                             /* 32-bit operand values     */

    RIL(inst, execflag, regs, r1, opcd, i2);

    /* Branch if R1 mask bit is set */
    if ((0x08 >> regs->psw.cc) & r1)
        /* Calculate the relative branch address */
        regs->psw.IA = ((!execflag ? (regs->psw.IA - 6) : regs->ET)
                                  + 2*(S32)i2) & ADDRESS_MAXWRAP(regs);
}


/*-------------------------------------------------------------------*/
/* C0x5 BRASL - Branch Relative And Save Long                  [RIL] */
/*-------------------------------------------------------------------*/
DEF_INST(branch_relative_and_save_long)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U32     i2;                             /* 32-bit operand values     */

    RIL(inst, execflag, regs, r1, opcd, i2);

    if(regs->psw.amode64)
        regs->GR_G(r1) = regs->psw.IA;
    else
    if ( regs->psw.amode )
        regs->GR_L(r1) = 0x80000000 | regs->psw.IA;
    else
        regs->GR_L(r1) = regs->psw.IA_LA24;

    /* Set instruction address to the relative branch address */
    regs->psw.IA = ((!execflag ? (regs->psw.IA - 6) : regs->ET)
                                  + 2*(S32)i2) & ADDRESS_MAXWRAP(regs);
}


/*-------------------------------------------------------------------*/
/* EB20 CLMH  - Compare Logical Characters under Mask High     [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(compare_logical_characters_under_mask_high)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U32     n;                              /* 32-bit operand values     */
int     cc = 0;                         /* Condition code            */
BYTE    sbyte,
        dbyte;                          /* Byte work areas           */
int     i;                              /* Integer work areas        */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Load value from register */
    n = regs->GR_H(r1);

    /* Compare characters in register with operand characters */
    for ( i = 0; i < 4; i++ )
    {
        /* Test mask bit corresponding to this character */
        if ( r3 & 0x08 )
        {
            /* Fetch character from register and operand */
            dbyte = n >> 24;
            sbyte = ARCH_DEP(vfetchb) ( effective_addr2++, b2, regs );

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
/* EB2C STCMH - Store Characters under Mask High               [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(store_characters_under_mask_high)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U32     n;                              /* 32-bit operand values     */
int     i, j;                           /* Integer work areas        */
BYTE    cwork[4];                       /* Character work areas      */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Load value from register */
    n = regs->GR_H(r1);

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
        ARCH_DEP(validate_operand) (effective_addr2, b2, 0, ACCTYPE_WRITE, regs);
        return;
    }

    /* Store result at operand location */
    ARCH_DEP(vstorec) ( cwork, j-1, effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* EB80 ICMH  - Insert Characters under Mask High              [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(insert_characters_under_mask_high)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
int     cc = 0;                         /* Condition code            */
BYTE    tbyte;                          /* Byte work areas           */
int     h, i;                           /* Integer work areas        */
U64     dreg;                           /* Double register work area */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* If the mask is all zero, we must nevertheless load one
       byte from the storage operand, because POP requires us
       to recognize an access exception on the first byte */
    if ( r3 == 0 )
    {
        tbyte = ARCH_DEP(vfetchb) ( effective_addr2, b2, regs );
        regs->psw.cc = 0;
        return;
    }

    /* Load existing register value into 64-bit work area */
    dreg = regs->GR_H(r1);

    /* Insert characters into register from operand address */
    for ( i = 0, h = 0; i < 4; i++ )
    {
        /* Test mask bit corresponding to this character */
        if ( r3 & 0x08 )
        {
            /* Fetch the source byte from the operand */
            tbyte = ARCH_DEP(vfetchb) ( effective_addr2, b2, regs );

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
    regs->GR_H(r1) = dreg >> 32;

    /* Set condition code */
    regs->psw.cc = cc;
}


/*-------------------------------------------------------------------*/
/* EC44 BRXHG - Branch Relative on Index High Long             [RIE] */
/*-------------------------------------------------------------------*/
DEF_INST(branch_relative_on_index_high_long)
{
int     r1, r3;                         /* Register numbers          */
U32     i2;                             /* 32-bit operand            */
int     i,j;                            /* Integer workareas         */

    RIE(inst, execflag, regs, r1, r3, i2);

    /* Load the increment value from the R3 register */
    i = (S64)regs->GR_G(r3);

    /* Load compare value from R3 (if R3 odd), or R3+1 (if even) */
    j = (r3 & 1) ? (S64)regs->GR_G(r3) : (S64)regs->GR_G(r3+1);

    /* Add the increment value to the R1 register */
    (S64)regs->GR_G(r1) += i;

    /* Branch if result compares high */
    if ( (S64)regs->GR_G(r1) > j )
        regs->psw.IA = ((!execflag ? (regs->psw.IA - 6) : regs->ET)
                                  + 2*(S32)i2) & ADDRESS_MAXWRAP(regs);

}


/*-------------------------------------------------------------------*/
/* EC45 BRXLG - Branch Relative on Index Low or Equal Long     [RIE] */
/*-------------------------------------------------------------------*/
DEF_INST(branch_relative_on_index_low_or_equal_long)
{
int     r1, r3;                         /* Register numbers          */
U32     i2;                             /* 32-bit operand            */
int     i,j;                            /* Integer workareas         */

    RIE(inst, execflag, regs, r1, r3, i2);

    /* Load the increment value from the R3 register */
    i = (S64)regs->GR_G(r3);

    /* Load compare value from R3 (if R3 odd), or R3+1 (if even) */
    j = (r3 & 1) ? (S64)regs->GR_G(r3) : (S64)regs->GR_G(r3+1);

    /* Add the increment value to the R1 register */
    (S64)regs->GR_G(r1) += i;

    /* Branch if result compares low or equal */
    if ( (S64)regs->GR_G(r1) <= j )
        regs->psw.IA = ((!execflag ? (regs->psw.IA - 6) : regs->ET)
                                  + 2*(S32)i2) & ADDRESS_MAXWRAP(regs);

}


/*-------------------------------------------------------------------*/
/* EB44 BXHG  - Branch on Index High Long                      [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(branch_on_index_high_long)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
S64     i, j;                           /* Integer work areas        */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Load the increment value from the R3 register */
    i = (S64)regs->GR_G(r3);

    /* Load compare value from R3 (if R3 odd), or R3+1 (if even) */
    j = (r3 & 1) ? (S64)regs->GR_G(r3) : (S64)regs->GR_G(r3+1);

    /* Add the increment value to the R1 register */
    (S64)regs->GR_G(r1) += i;

    /* Branch if result compares high */
    if ( (S64)regs->GR_G(r1) > j )
        regs->psw.IA = effective_addr2;

}


/*-------------------------------------------------------------------*/
/* EB45 BXLEG - Branch on Index Low or Equal Long              [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(branch_on_index_low_or_equal_long)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
S64     i, j;                           /* Integer work areas        */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Load the increment value from the R3 register */
    i = regs->GR_G(r3);

    /* Load compare value from R3 (if R3 odd), or R3+1 (if even) */
    j = (r3 & 1) ? (S64)regs->GR_G(r3) : (S64)regs->GR_G(r3+1);

    /* Add the increment value to the R1 register */
    (S64)regs->GR_G(r1) += i;

    /* Branch if result compares low or equal */
    if ( (S64)regs->GR_G(r1) <= j )
        regs->psw.IA = effective_addr2;

}


/*-------------------------------------------------------------------*/
/* EB30 CSG   - Compare and Swap Long                          [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(compare_and_swap_long)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U64     n;                              /* 64-bit operand value      */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    FW_CHECK(effective_addr2, regs);

    /* Perform serialization before starting operation */
    PERFORM_SERIALIZATION (regs);

    /* Obtain main-storage access lock */
    OBTAIN_MAINLOCK(regs);

    /* Load second operand from operand address  */
    n = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );

    /* Compare operand with R1 register contents */
    if ( regs->GR_G(r1) == n )
    {
        /* If equal, store R3 at operand location and set cc=0 */
        ARCH_DEP(vstore8) ( regs->GR_G(r3), effective_addr2, b2, regs );
        regs->psw.cc = 0;
    }
    else
    {
        /* If unequal, load R1 from operand and set cc=1 */
        regs->GR_G(r1) = n;
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

#if defined(_FEATURE_ZSIE)
    if(regs->sie_state && (regs->siebk->ic[0] & SIE_IC0_CS1))
        longjmp(regs->progjmp, SIE_INTERCEPT_INST);
#endif /*defined(_FEATURE_ZSIE)*/

}

/*-------------------------------------------------------------------*/
/* EB3E CDSG  - Compare Double and Swap Long                   [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(compare_double_and_swap_long)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U64     n1, n2;                         /* 64-bit operand values     */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    ODD2_CHECK(r1, r3, regs);

    DW_CHECK(effective_addr2, regs);

    /* Perform serialization before starting operation */
    PERFORM_SERIALIZATION (regs);

    /* Obtain main-storage access lock */
    OBTAIN_MAINLOCK(regs);

    /* Load second operand from operand address  */
    n1 = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );
    n2 = ARCH_DEP(vfetch8) ( effective_addr2 + 8, b2, regs );

    /* Compare doubleword operand with R1:R1+1 register contents */
    if ( regs->GR_G(r1) == n1 && regs->GR_G(r1+1) == n2 )
    {
        /* If equal, store R3:R3+1 at operand location and set cc=0 */
        ARCH_DEP(vstore8) ( regs->GR_G(r3), effective_addr2, b2, regs );
        ARCH_DEP(vstore8) ( regs->GR_G(r3+1), effective_addr2 + 8, b2, regs );
        regs->psw.cc = 0;
    }
    else
    {
        /* If unequal, load R1:R1+1 from operand and set cc=1 */
        regs->GR_G(r1) = n1;
        regs->GR_G(r1+1) = n2;
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

#if defined(_FEATURE_ZSIE)
    if(regs->sie_state && (regs->siebk->ic[0] & SIE_IC0_CDS1))
        longjmp(regs->progjmp, SIE_INTERCEPT_INST);
#endif /*defined(_FEATURE_ZSIE)*/

}


/*-------------------------------------------------------------------*/
/* E346 BCTG  - Branch on Count Long                           [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(branch_on_count_long)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Subtract 1 from the R1 operand and branch if non-zero */
    if ( --(regs->GR_G(r1)) )
        regs->psw.IA = effective_addr2;

}


/*-------------------------------------------------------------------*/
/* B946 BCTGR - Branch on Count Long Register                  [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(branch_on_count_long_register)
{
int     r1, r2;                         /* Values of R fields        */
VADR    newia;                          /* New instruction address   */

    RRE(inst, execflag, regs, r1, r2);

    /* Compute the branch address from the R2 operand */
    newia = regs->GR_G(r2) & ADDRESS_MAXWRAP(regs);

    /* Subtract 1 from the R1 operand and branch if result
           is non-zero and R2 operand is not register zero */
    if ( --(regs->GR_G(r1)) && r2 != 0 )
        regs->psw.IA = newia;

}


/*-------------------------------------------------------------------*/
/* B920 CGR   - Compare Long Register                          [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(compare_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Compare signed operands and set condition code */
    regs->psw.cc =
                (S64)regs->GR_G(r1) < (S64)regs->GR_G(r2) ? 1 :
                (S64)regs->GR_G(r1) > (S64)regs->GR_G(r2) ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* B930 CGFR  - Compare Long Fullword Register                 [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(compare_long_fullword_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Compare signed operands and set condition code */
    regs->psw.cc =
                (S64)regs->GR_G(r1) < (S32)regs->GR_L(r2) ? 1 :
                (S64)regs->GR_G(r1) > (S32)regs->GR_L(r2) ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* E320 CG    - Compare Long                                   [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(compare_long)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U64     n;                              /* 64-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );

    /* Compare signed operands and set condition code */
    regs->psw.cc =
            (S64)regs->GR_G(r1) < (S64)n ? 1 :
            (S64)regs->GR_G(r1) > (S64)n ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* E330 CGF   - Compare Long Fullword                          [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(compare_long_fullword)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
S32     n;                              /* 64-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = (S32)ARCH_DEP(vfetch4) ( effective_addr2, b2, regs );

    /* Compare signed operands and set condition code */
    regs->psw.cc =
            (S64)regs->GR_G(r1) < (S32)n ? 1 :
            (S64)regs->GR_G(r1) > (S32)n ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* E30A ALG   - Add Logical Long                               [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(add_logical_long)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U64     n;                              /* 64-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );

    /* Add unsigned operands and set condition code */
    regs->psw.cc = add_logical_long(&(regs->GR_G(r1)),
                                      regs->GR_G(r1),
                                      n);
}


/*-------------------------------------------------------------------*/
/* E31A ALGF  - Add Logical Long Fullword                      [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(add_logical_long_fullword)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch4) ( effective_addr2, b2, regs );

    /* Add unsigned operands and set condition code */
    regs->psw.cc = add_logical_long(&(regs->GR_G(r1)),
                                      regs->GR_G(r1),
                                      n);
}


/*-------------------------------------------------------------------*/
/* E318 AGF   - Add Long Fullword                              [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(add_long_fullword)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch4) ( effective_addr2, b2, regs );

    /* Add signed operands and set condition code */
    regs->psw.cc = add_signed_long (&(regs->GR_G(r1)),
                                      regs->GR_G(r1),
                                 (S32)n);

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
}


/*-------------------------------------------------------------------*/
/* E308 AG    - Add Long                                       [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(add_long)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U64     n;                              /* 64-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );

    /* Add signed operands and set condition code */
    regs->psw.cc = add_signed_long (&(regs->GR_G(r1)),
                                      regs->GR_G(r1),
                                      n);

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
}


/*-------------------------------------------------------------------*/
/* E30B SLG   - Subtract Logical Long                          [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_logical_long)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U64     n;                              /* 64-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );

    /* Subtract unsigned operands and set condition code */
    regs->psw.cc = sub_logical_long(&(regs->GR_G(r1)),
                                      regs->GR_G(r1),
                                      n);

}


/*-------------------------------------------------------------------*/
/* E31B SLGF  - Subtract Logical Long Fullword                 [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_logical_long_fullword)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch4) ( effective_addr2, b2, regs );

    /* Subtract unsigned operands and set condition code */
    regs->psw.cc = sub_logical_long(&(regs->GR_G(r1)),
                                      regs->GR_G(r1),
                                      n);

}


/*-------------------------------------------------------------------*/
/* E319 SGF   - Subtract Long Fullword                         [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_long_fullword)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch4) ( effective_addr2, b2, regs );

    /* Subtract signed operands and set condition code */
    regs->psw.cc = sub_signed_long(&(regs->GR_G(r1)),
                                     regs->GR_G(r1),
                                (S32)n);

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
}


/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/
/* E309 SG    - Subtract Long                                  [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_long)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U64     n;                              /* 64-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );

    /* Subtract signed operands and set condition code */
    regs->psw.cc = sub_signed_long(&(regs->GR_G(r1)),
                                     regs->GR_G(r1),
                                     n);

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
}


/*-------------------------------------------------------------------*/
/* B909 SGR   - Subtract Long Register                         [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Subtract signed operands and set condition code */
    regs->psw.cc = sub_signed_long(&(regs->GR_G(r1)),
                                     regs->GR_G(r1),
                                     regs->GR_G(r2));

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
}


/*-------------------------------------------------------------------*/
/* B919 SGFR  - Subtract Long Fullword Register                [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_long_fullword_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Subtract signed operands and set condition code */
    regs->psw.cc = sub_signed_long(&(regs->GR_G(r1)),
                                     regs->GR_G(r1),
                                (S32)regs->GR_L(r2));

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
}


/*-------------------------------------------------------------------*/
/* B908 AGR   - Add Long Register                              [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(add_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Add signed operands and set condition code */
    regs->psw.cc = add_signed_long(&(regs->GR_G(r1)),
                                     regs->GR_G(r1),
                                     regs->GR_G(r2));

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
}


/*-------------------------------------------------------------------*/
/* B918 AGFR  - Add Long Fullword Register                     [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(add_long_fullword_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Add signed operands and set condition code */
    regs->psw.cc = add_signed_long(&(regs->GR_G(r1)),
                                     regs->GR_G(r1),
                                (S32)regs->GR_L(r2));

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
}


/*-------------------------------------------------------------------*/
/* B900 LPGR  - Load Positive Long Register                    [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_positive_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Condition code 3 and program check if overflow */
    if ( regs->GR_G(r2) == 0x8000000000000000ULL )
    {
        regs->GR_G(r1) = regs->GR_G(r2);
        regs->psw.cc = 3;
        if ( regs->psw.fomask )
            ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
        return;
    }

    /* Load positive value of second operand and set cc */
    (S64)regs->GR_G(r1) = (S64)regs->GR_G(r2) < 0 ?
                            -((S64)regs->GR_G(r2)) :
                            (S64)regs->GR_G(r2);

    regs->psw.cc = (S64)regs->GR_G(r1) == 0 ? 0 : 2;
}


/*-------------------------------------------------------------------*/
/* B910 LPGFR - Load Positive Long Fullword Register           [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_positive_long_fullword_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Load positive value of second operand and set cc */
    (S64)regs->GR_G(r1) = (S32)regs->GR_L(r2) < 0 ?
                            -((S32)regs->GR_L(r2)) :
                            (S32)regs->GR_L(r2);

    regs->psw.cc = (S64)regs->GR_G(r1) == 0 ? 0 : 2;
}


/*-------------------------------------------------------------------*/
/* B901 LNGR  - Load Negative Long Register                    [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_negative_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Load negative value of second operand and set cc */
    (S64)regs->GR_G(r1) = (S64)regs->GR_G(r2) > 0 ?
                            -((S64)regs->GR_G(r2)) :
                            (S64)regs->GR_G(r2);

    regs->psw.cc = (S64)regs->GR_G(r1) == 0 ? 0 : 1;
}


/*-------------------------------------------------------------------*/
/* B911 LNGFR - Load Negative Long Fullword Register           [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_negative_long_fullword_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Load negative value of second operand and set cc */
    (S64)regs->GR_G(r1) = (S64)regs->GR_L(r2) > 0 ?
                            -((S64)regs->GR_L(r2)) :
                            (S64)regs->GR_L(r2);

    regs->psw.cc = (S64)regs->GR_G(r1) == 0 ? 0 : 1;
}


/*-------------------------------------------------------------------*/
/* B902 LTGR  - Load and Test Long Register                    [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_and_test_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Copy second operand and set condition code */
    regs->GR_G(r1) = regs->GR_G(r2);

    regs->psw.cc = (S64)regs->GR_G(r1) < 0 ? 1 :
                   (S64)regs->GR_G(r1) > 0 ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* B912 LTGFR - Load and Test Long Fullword Register           [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_and_test_long_fullword_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Copy second operand and set condition code */
    (S64)regs->GR_G(r1) = (S32)regs->GR_L(r2);

    regs->psw.cc = (S64)regs->GR_G(r1) < 0 ? 1 :
                   (S64)regs->GR_G(r1) > 0 ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* B903 LCGR  - Load Complement Long Register                  [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_complement_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Condition code 3 and program check if overflow */
    if ( regs->GR_G(r2) == 0x8000000000000000ULL )
    {
        regs->GR_G(r1) = regs->GR_G(r2);
        regs->psw.cc = 3;
        if ( regs->psw.fomask )
            ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
        return;
    }

    /* Load complement of second operand and set condition code */
    (S64)regs->GR_G(r1) = -((S64)regs->GR_G(r2));

    regs->psw.cc = (S64)regs->GR_G(r1) < 0 ? 1 :
                   (S64)regs->GR_G(r1) > 0 ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* B913 LCGFR - Load Complement Long Fullword Register         [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_complement_long_fullword_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Load complement of second operand and set condition code */
    (S64)regs->GR_G(r1) = -((S64)regs->GR_L(r2));

    regs->psw.cc = (S64)regs->GR_G(r1) < 0 ? 1 :
                   (S64)regs->GR_G(r1) > 0 ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* A7x2 TMHH  - Test under Mask High High                       [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(test_under_mask_high_high)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */
U16     h1;                             /* 16-bit operand values     */
U16     h2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    /* AND register bits 0-15 with immediate operand */
    h1 = i2 & regs->GR_HHH(r1);

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
/* A7x3 TMHL  - Test under Mask High Low                        [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(test_under_mask_high_low)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */
U16     h1;                             /* 16-bit operand values     */
U16     h2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    /* AND register bits 16-31 with immediate operand */
    h1 = i2 & regs->GR_HHL(r1);

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
/* A7x7 BRCT  - Branch Relative on Count Long                   [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(branch_relative_on_count_long)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    /* Subtract 1 from the R1 operand and branch if non-zero */
    if ( --(regs->GR_G(r1)) )
        regs->psw.IA = ((!execflag ? (regs->psw.IA - 4) : regs->ET)
                                  + 2*(S16)i2) & ADDRESS_MAXWRAP(regs);
}


/*-------------------------------------------------------------------*/
/* E321 CLG   - Compare Logical long                           [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(compare_logical_long)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U64     n;                              /* 64-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );

    /* Compare unsigned operands and set condition code */
    regs->psw.cc = regs->GR_G(r1) < n ? 1 :
                   regs->GR_G(r1) > n ? 2 : 0;
}

/*-------------------------------------------------------------------*/
/* E331 CLGF  - Compare Logical long fullword                  [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(compare_logical_long_fullword)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch4) ( effective_addr2, b2, regs );

    /* Compare unsigned operands and set condition code */
    regs->psw.cc = regs->GR_G(r1) < n ? 1 :
                   regs->GR_G(r1) > n ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* B931 CLGFR - Compare Logical Long Fullword Register         [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(compare_logical_long_fullword_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Compare unsigned operands and set condition code */
    regs->psw.cc = regs->GR_G(r1) < regs->GR_L(r2) ? 1 :
                   regs->GR_G(r1) > regs->GR_L(r2) ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* B917 LLGTR - Load Logical Long Thirtyone Regsiter           [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_logical_long_thirtyone_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    regs->GR_G(r1) = regs->GR_L(r2) & 0x7FFFFFFF;
}


/*-------------------------------------------------------------------*/
/* B921 CLGR  - Compare Logical Long Register                  [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(compare_logical_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Compare unsigned operands and set condition code */
    regs->psw.cc = regs->GR_G(r1) < regs->GR_G(r2) ? 1 :
                   regs->GR_G(r1) > regs->GR_G(r2) ? 2 : 0;
}


/*-------------------------------------------------------------------*/
/* EB1C RLLG  - Rotate Left Single Logical Long                [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(rotate_left_single_logical_long)     
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U64     n;                              /* Integer work areas        */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Rotate and copy contents of r3 to r1 */
    regs->GR_G(r1) = regs->GR_G(r3) << n
                   | regs->GR_G(r3) >> (64 - n);

}


/*-------------------------------------------------------------------*/
/* EB1D RLL   - Rotate Left Single Logical                     [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(rotate_left_single_logical)     
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U64     n;                              /* Integer work areas        */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Rotate and copy contents of r3 to r1 */
    regs->GR_L(r1) = regs->GR_L(r3) << n
                   | regs->GR_L(r3) >> (64 - n);

}


/*-------------------------------------------------------------------*/
/* EB0D SLLG  - Shift Left Single Logical Long                 [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(shift_left_single_logical_long)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U64     n;                              /* Integer work areas        */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Copy contents of r3 to r1 */
    regs->GR_G(r1) = regs->GR_G(r3) << n;
}


/*-------------------------------------------------------------------*/
/* EB0C SRLG  - Shift Right Single Logical Long                [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(shift_right_single_logical_long)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U64     n;                              /* Integer work areas        */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Copy contents of r3 to r1 */
    regs->GR_G(r1) = regs->GR_G(r3) >> n;
}


/*-------------------------------------------------------------------*/
/* EB0B SLAG  - Shift Left Single Long                         [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(shift_left_single_long)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U64     n, n1, n2;                      /* 64-bit operand values     */
int     i, j;                           /* Integer work areas        */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Copy contents of r3 to r1 */
    regs->GR_G(r1) = regs->GR_G(r3);

    /* Load the numeric and sign portions from the R1 register */
    n1 = regs->GR_G(r1) & 0x7FFFFFFFFFFFFFFFULL;
    n2 = regs->GR_G(r1) & 0x8000000000000000ULL;

    /* Shift the numeric portion left n positions */
    for (i = 0, j = 0; i < n; i++)
    {
        /* Shift bits 1-31 left one bit position */
        n1 <<= 1;

        /* Overflow if bit shifted out is unlike the sign bit */
        if ((n1 & 0x8000000000000000ULL) != n2)
            j = 1;
    }

    /* Load the updated value into the R1 register */
    regs->GR_G(r1) = (n1 & 0x7FFFFFFFFFFFFFFFULL) | n2;

    /* Condition code 3 and program check if overflow occurred */
    if (j)
    {
        regs->psw.cc = 3;
        if ( regs->psw.fomask )
            ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
        return;
    }

    /* Set the condition code */
    regs->psw.cc = (S64)regs->GR_G(r1) > 0 ? 2 :
                   (S64)regs->GR_G(r1) < 0 ? 1 : 0;

}


/*-------------------------------------------------------------------*/
/* EB0A SRAG  - Shift Right single Long                        [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(shift_right_single_long)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
U64     n;                              /* Integer work areas        */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Use rightmost six bits of operand address as shift count */
    n = effective_addr2 & 0x3F;

    /* Copy contents of r3 to r1 */
    regs->GR_G(r1) = regs->GR_G(r3);

    /* Shift the signed value of the R1 register */
    (S64)regs->GR_G(r1) = n > 62 ?
                    ((S64)regs->GR_G(r1) < 0 ? -1 : 0) :
                    (S64)regs->GR_G(r1) >> n;

    /* Set the condition code */
    regs->psw.cc = (S64)regs->GR_G(r1) > 0 ? 2 :
                   (S64)regs->GR_G(r1) < 0 ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* E31C MSGF  - Multiply Single Long Fullword                  [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(multiply_single_long_fullword)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U32     n;                              /* 32-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch4) ( effective_addr2, b2, regs );

    /* Multiply signed operands ignoring overflow */
    (S64)regs->GR_G(r1) *= (S32)n;

}


/*-------------------------------------------------------------------*/
/* E30C MSG   - Multiply Single Long                           [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(multiply_single_long)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U64     n;                              /* 64-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );

    /* Multiply signed operands ignoring overflow */
    (S64)regs->GR_G(r1) *= (S64)n;

}


/*-------------------------------------------------------------------*/
/* B91C MSGFR - Multiply Single Long Fullword Register         [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(multiply_single_long_fullword_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Multiply signed registers ignoring overflow */
    (S64)regs->GR_G(r1) *= (S32)regs->GR_L(r2);

}


/*-------------------------------------------------------------------*/
/* B90C MSGR  - Multiply Single Long Register                  [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(multiply_single_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Multiply signed registers ignoring overflow */
    (S64)regs->GR_G(r1) *= (S64)regs->GR_G(r2);

}


/*-------------------------------------------------------------------*/
/* A7x9 LGHI  - Load Long Halfword Immediate                    [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(load_long_halfword_immediate)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand values     */

    RI(inst, execflag, regs, r1, opcd, i2);

    /* Load operand into register */
    (S64)regs->GR_G(r1) = (S16)i2;

}


/*-------------------------------------------------------------------*/
/* A7xB AGHI  - Add Long Halfword Immediate                     [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(add_long_halfword_immediate)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit immediate op       */
S64     n;                              /* 16-bit converted to long  */

    RI(inst, execflag, regs, r1, opcd, i2);

    /* Propagate sign bit to form 32-bit signed operand */
    n = (S16)i2;

    /* Add signed operands and set condition code */
    regs->psw.cc = add_signed_long(&(regs->GR_G(r1)),
                                     regs->GR_G(r1),
                                     n);

    /* Program check if fixed-point overflow */
    if ( regs->psw.cc == 3 && regs->psw.fomask )
        ARCH_DEP(program_interrupt) (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
}

/*-------------------------------------------------------------------*/
/* A7xD MGHI  - Multiply Long Halfword Immediate                [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(multiply_long_halfword_immediate)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand            */

    RI(inst, execflag, regs, r1, opcd, i2);

    /* Multiply register by operand ignoring overflow  */
    (S64)regs->GR_G(r1) *= (S16)i2;

}


/*-------------------------------------------------------------------*/
/* A7xF CGHI  - Compare Long Halfword Immediate                 [RI] */
/*-------------------------------------------------------------------*/
DEF_INST(compare_long_halfword_immediate)
{
int     r1;                             /* Register number           */
int     opcd;                           /* Opcode                    */
U16     i2;                             /* 16-bit operand            */

    RI(inst, execflag, regs, r1, opcd, i2);

    /* Compare signed operands and set condition code */
    regs->psw.cc =
            (S64)regs->GR_G(r1) < (S16)i2 ? 1 :
            (S64)regs->GR_G(r1) > (S16)i2 ? 2 : 0;

}


/*-------------------------------------------------------------------*/
/* B980 NGR   - And Register Long                              [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(and_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* AND second operand with first and set condition code */
    regs->psw.cc = ( regs->GR_G(r1) &= regs->GR_G(r2) ) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* B981 OGR   - Or Register Long                               [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(or_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* OR second operand with first and set condition code */
    regs->psw.cc = ( regs->GR_G(r1) |= regs->GR_G(r2) ) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* B982 XGR   - Exclusive Or Register Long                     [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(exclusive_or_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* XOR second operand with first and set condition code */
    regs->psw.cc = ( regs->GR_G(r1) ^= regs->GR_G(r2) ) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* E380 NG    - And Long                                       [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(and_long)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U64     n;                              /* 64-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );

    /* AND second operand with first and set condition code */
    regs->psw.cc = ( regs->GR_G(r1) &= n ) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* E381 OG    - Or Long                                        [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(or_long)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U64     n;                              /* 64-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );

    /* OR second operand with first and set condition code */
    regs->psw.cc = ( regs->GR_G(r1) |= n ) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* E382 XG    - Exclusive Or Long                              [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(exclusive_or_long)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U64     n;                              /* 64-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load second operand from operand address */
    n = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );

    /* XOR second operand with first and set condition code */
    regs->psw.cc = ( regs->GR_G(r1) ^= n ) ? 1 : 0;
}


/*-------------------------------------------------------------------*/
/* B904 LGR   - Load Long Register                             [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Copy second operand to first operand */
    regs->GR_G(r1) = regs->GR_G(r2);
}


/*-------------------------------------------------------------------*/
/* B916 LLGFR - Load Logical Long Fullword Register            [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_logical_long_fullword_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Copy second operand to first operand */
    regs->GR_G(r1) = regs->GR_L(r2);
}


/*-------------------------------------------------------------------*/
/* B914 LGFR  - Load Long Fullword Register                    [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_long_fullword_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Copy second operand to first operand */
    (S64)regs->GR_G(r1) = (S32)regs->GR_L(r2);
}


/*-------------------------------------------------------------------*/
/* B90A ALGR  - Add Logical Register Long                      [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(add_logical_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Add unsigned operands and set condition code */
    regs->psw.cc = add_logical_long(&(regs->GR_G(r1)),
                                      regs->GR_G(r1),
                                      regs->GR_G(r2));
}


/*-------------------------------------------------------------------*/
/* B91A ALGFR - Add Logical Long Fullword Register    1         [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(add_logical_long_fullword_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Add signed operands and set condition code */
    regs->psw.cc = add_logical_long(&(regs->GR_G(r1)),
                                      regs->GR_G(r1),
                                      regs->GR_L(r2));
}


/*-------------------------------------------------------------------*/
/* B91B SLGFR - Subtract Logical Long Fullword Register        [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_logical_long_fullword_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Subtract unsigned operands and set condition code */
    regs->psw.cc = sub_logical_long(&(regs->GR_G(r1)),
                                      regs->GR_G(r1),
                                      regs->GR_L(r2));
}


/*-------------------------------------------------------------------*/
/* B90B SLGR  - Subtract Logical Register Long                 [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(subtract_logical_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Subtract unsigned operands and set condition code */
    regs->psw.cc = sub_logical_long(&(regs->GR_G(r1)),
                                      regs->GR_G(r1),
                                      regs->GR_G(r2));
}


/*-------------------------------------------------------------------*/
/* EF   LMD   - Load Multiple Disjoint                          [SS] */
/*-------------------------------------------------------------------*/
DEF_INST(load_multiple_disjoint)
{
int     r1, r3;                         /* Register numbers          */
int     b2, b4;                         /* Base register numbers     */
VADR    effective_addr2;                /* Operand2 address          */
VADR    effective_addr4;                /* Operand4 address          */
int     i, d;                           /* Integer work areas        */
BYTE    rworkh[64], rworkl[64];         /* High and low halves of new
                                           values to be loaded       */

    SS(inst, execflag, regs, r1, r3, b2, effective_addr2,
                                        b4, effective_addr4);

    /* Calculate the number of bytes to be loaded from each operand */
    d = (((r3 < r1) ? r3 + 16 - r1 : r3 - r1) + 1) * 4;

    /* Fetch high order half of new register contents from operand 2 */
    ARCH_DEP(vfetchc) ( rworkh, d-1, effective_addr2, b2, regs );

    /* Fetch low order half of new register contents from operand 4 */
    ARCH_DEP(vfetchc) ( rworkl, d-1, effective_addr4, b4, regs );

    /* Load registers from work areas */
    for ( i = r1, d = 0; ; )
    {
        /* Load both halves of one register from the work areas */
        FETCH_FW(regs->GR_H(i), rworkh + d);
        FETCH_FW(regs->GR_L(i), rworkl + d);
        d += 4;

        /* Instruction is complete when r3 register is done */
        if ( i == r3 ) break;

        /* Update register number, wrapping from 15 to 0 */
        i++; i &= 15;
    }
}


/*-------------------------------------------------------------------*/
/* EB96 LMH   - Load Multiple High                             [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_multiple_high)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
int     i, d;                           /* Integer work areas        */
BYTE    rwork[64];                      /* Character work areas      */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Calculate the number of bytes to be loaded */
    d = (((r3 < r1) ? r3 + 16 - r1 : r3 - r1) + 1) * 4;

    /* Fetch new register contents from operand address */
    ARCH_DEP(vfetchc) ( rwork, d-1, effective_addr2, b2, regs );

    /* Load registers from work area */
    for ( i = r1, d = 0; ; )
    {
        /* Load one register from work area */
        FETCH_FW(regs->GR_H(i), rwork + d); d += 4;

        /* Instruction is complete when r3 register is done */
        if ( i == r3 ) break;

        /* Update register number, wrapping from 15 to 0 */
        i++; i &= 15;
    }
}


/*-------------------------------------------------------------------*/
/* EB04 LMG   - Load Multiple Long                             [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_multiple_long)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
int     i, d;                           /* Integer work areas        */
BYTE    rwork[128];                     /* Register work areas       */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Calculate the number of bytes to be loaded */
    d = (((r3 < r1) ? r3 + 16 - r1 : r3 - r1) + 1) * 8;

    /* Fetch new control register contents from operand address */
    ARCH_DEP(vfetchc) ( rwork, d-1, effective_addr2, b2, regs );

    /* Load control registers from work area */
    for ( i = r1, d = 0; ; )
    {
        /* Load one general register from work area */
        FETCH_DW(regs->GR_G(i), rwork + d); d += 8;

        /* Instruction is complete when r3 register is done */
        if ( i == r3 ) break;

        /* Update register number, wrapping from 15 to 0 */
        i++; i &= 15;
    }
}


/*-------------------------------------------------------------------*/
/* EB25 STCTG - Store Control Long                             [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(store_control_long)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
int     i, d;                           /* Integer work areas        */
BYTE    rwork[128];                      /* Register work areas       */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    PRIV_CHECK(regs);

    FW_CHECK(effective_addr2, regs);

#if defined(_FEATURE_ZSIE)
    if(regs->sie_state && (regs->siebk->ic[1] & SIE_IC1_STCTL))
        longjmp(regs->progjmp, SIE_INTERCEPT_INST);
#endif /*defined(_FEATURE_ZSIE)*/

    /* Copy control registers into work area */
    for ( i = r1, d = 0; ; )
    {
        /* Copy contents of one control register to work area */
        STORE_DW(rwork + d, regs->CR_G(i)); d += 8;

        /* Instruction is complete when r3 register is done */
        if ( i == r3 ) break;

        /* Update register number, wrapping from 15 to 0 */
        i++; i &= 15;
    }

    /* Store control register contents at operand address */
    ARCH_DEP(vstorec) ( rwork, d-1, effective_addr2, b2, regs );

}


/*-------------------------------------------------------------------*/
/* EB2F LCTLG - Load Control Long                              [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_control_long)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
int     i, d;                           /* Integer work areas        */
BYTE    rwork[128];                     /* Register work areas       */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    PRIV_CHECK(regs);

    FW_CHECK(effective_addr2, regs);

#if defined(_FEATURE_ZSIE)
    if(regs->sie_state)
    {
    U32 n;
        for(i = r1, n = 0x80008000 >> r1; ; )
        {
            if(regs->siebk->lctl_ctl[i < 8 ? 0 : 1] & (i < 8) ? n >> 8 : n)
                longjmp(regs->progjmp, SIE_INTERCEPT_INST);

            if ( i == r3 ) break;
            i++; i &= 15;
        }
    }
#endif /*defined(_FEATURE_ZSIE)*/

    /* Calculate the number of bytes to be loaded */
    d = (((r3 < r1) ? r3 + 16 - r1 : r3 - r1) + 1) * 8;

    /* Fetch new control register contents from operand address */
    ARCH_DEP(vfetchc) ( rwork, d-1, effective_addr2, b2, regs );

    INVALIDATE_AIA(regs);

    INVALIDATE_AEA_ALL(regs);

    /* Load control registers from work area */
    for ( i = r1, d = 0; ; )
    {
        /* Load one control register from work area */
        FETCH_DW(regs->CR_G(i), rwork + d); d += 8;

        /* Instruction is complete when r3 register is done */
        if ( i == r3 ) break;

        /* Update register number, wrapping from 15 to 0 */
        i++; i &= 15;
    }
}


/*-------------------------------------------------------------------*/
/* EB24 STMG  - Store Multiple Long                            [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(store_multiple_long)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
int     i, d;                           /* Integer work areas        */
BYTE    rwork[128];                      /* Register work areas       */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Copy control registers into work area */
    for ( i = r1, d = 0; ; )
    {
        /* Copy contents of one control register to work area */
        STORE_DW(rwork + d, regs->GR_G(i)); d += 8;

        /* Instruction is complete when r3 register is done */
        if ( i == r3 ) break;

        /* Update register number, wrapping from 15 to 0 */
        i++; i &= 15;
    }

    /* Store control register contents at operand address */
    ARCH_DEP(vstorec) ( rwork, d-1, effective_addr2, b2, regs );

}


/*-------------------------------------------------------------------*/
/* EB26 STMH  - Store Multiple High                            [RSE] */
/*-------------------------------------------------------------------*/
DEF_INST(store_multiple_high)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
VADR    effective_addr2;                /* effective address         */
int     i, d;                           /* Integer work area         */
BYTE    rwork[64];                      /* Register work area        */

    RSE(inst, execflag, regs, r1, r3, b2, effective_addr2);

    /* Copy register contents into work area */
    for ( i = r1, d = 0; ; )
    {
        /* Copy contents of one register to work area */
        STORE_FW(rwork + d, regs->GR_H(i)); d += 4;

        /* Instruction is complete when r3 register is done */
        if ( i == r3 ) break;

        /* Update register number, wrapping from 15 to 0 */
        i++; i &= 15;
    }

    /* Store register contents at operand address */
    ARCH_DEP(vstorec) ( rwork, d-1, effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* B905 LURAG - Load Using Real Address Long                   [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_using_real_address_long)
{
int     r1, r2;                         /* Values of R fields        */
RADR    n;                              /* Unsigned work             */

    RRE(inst, execflag, regs, r1, r2);

    PRIV_CHECK(regs);

    /* R2 register contains operand real storage address */
    n = regs->GR_G(r2) & ADDRESS_MAXWRAP(regs);

    /* Program check if operand not on fullword boundary */
    FW_CHECK(n, regs);

    /* Load R1 register from second operand */
    regs->GR_G(r1) = ARCH_DEP(vfetch8) ( n, USE_REAL_ADDR, regs );

}


/*-------------------------------------------------------------------*/
/* B925 STURG - Store Using Real Address Long                  [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(store_using_real_address_long)
{
int     r1, r2;                         /* Values of R fields        */
RADR    n;                              /* Unsigned work             */

    RRE(inst, execflag, regs, r1, r2);

    PRIV_CHECK(regs);

    /* R2 register contains operand real storage address */
    n = regs->GR_G(r2) & ADDRESS_MAXWRAP(regs);

    /* Program check if operand not on fullword boundary */
    FW_CHECK(n, regs);

    /* Store R1 register at second operand location */
    ARCH_DEP(vstore8) (regs->GR_G(r1), n, USE_REAL_ADDR, regs );

}


/*-------------------------------------------------------------------*/
/* 010B TAM   - Test Addressing Mode                             [E] */
/*-------------------------------------------------------------------*/
DEF_INST(test_addressing_mode)
{
    E(inst, execflag, regs);

    regs->psw.cc = (regs->psw.amode64 << 1) | regs->psw.amode;
}


/*-------------------------------------------------------------------*/
/* 010C SAM24 - Set Addressing Mode 24                           [E] */
/*-------------------------------------------------------------------*/
DEF_INST(set_addressing_mode_24)
{
VADR    ia;                             /* Unupdated instruction addr*/

    E(inst, execflag, regs);

    /* Calculate the unupdated instruction address */
    ia = (regs->psw.IA - regs->psw.ilc) & ADDRESS_MAXWRAP(regs);

    /* Program check if instruction is located above 16MB */
    if (ia > 0xFFFFFFULL)
        ARCH_DEP(program_interrupt) (regs, PGM_SPECIFICATION_EXCEPTION);

    regs->psw.amode = regs->psw.amode64 = 0;
}


/*-------------------------------------------------------------------*/
/* 010D SAM31 - Set Addressing Mode 31                           [E] */
/*-------------------------------------------------------------------*/
DEF_INST(set_addressing_mode_31)
{
VADR    ia;                             /* Unupdated instruction addr*/

    E(inst, execflag, regs);

    /* Calculate the unupdated instruction address */
    ia = (regs->psw.IA - regs->psw.ilc) & ADDRESS_MAXWRAP(regs);

    /* Program check if instruction is located above 2GB */
    if (ia > 0x7FFFFFFFULL)
        ARCH_DEP(program_interrupt) (regs, PGM_SPECIFICATION_EXCEPTION);

    regs->psw.amode = 1; regs->psw.amode64 = 0;
}


/*-------------------------------------------------------------------*/
/* 010E SAM64 - Set Addressing Mode 64                           [E] */
/*-------------------------------------------------------------------*/
DEF_INST(set_addressing_mode_64)
{
    E(inst, execflag, regs);

    regs->psw.amode = regs->psw.amode64 = 1;
}


/*-------------------------------------------------------------------*/
/* E324 STG   - Store Long                                     [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(store_long)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Store register contents at operand address */
    ARCH_DEP(vstore8) ( regs->GR_G(r1), effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* E502 STRAG - Store Real Address                             [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(store_real_address)
{
int     b1, b2;                         /* Values of base registers  */
VADR    effective_addr1,
        effective_addr2;                /* Effective addresses       */
U16     xcode;                          /* Exception code            */
int     private;                        /* 1=Private address space   */
int     protect;                        /* 1=ALE or page protection  */
int     stid;                           /* Segment table indication  */
RADR    n;                              /* 64-bit operand values     */

    SSE(inst, execflag, regs, b1, effective_addr1, b2, effective_addr2);

    PRIV_CHECK(regs);

    DW_CHECK(effective_addr1, regs);

    /* Translate virtual address to real address */
    if (ARCH_DEP(translate_addr) (effective_addr2, b2, regs, ACCTYPE_STRAG,
        &n, &xcode, &private, &protect, &stid, NULL, NULL))
        ARCH_DEP(program_interrupt) (regs, xcode);

    /* Store register contents at operand address */
    ARCH_DEP(vstore8) ( n, effective_addr1, b1, regs );
}


/*-------------------------------------------------------------------*/
/* E304 LG    - Load Long                                      [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_long)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load R1 register from second operand */
    regs->GR_G(r1) = ARCH_DEP(vfetch8) ( effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* E314 LGF   - Load Long Fullword                             [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_long_fullword)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load R1 register from second operand */
    (S64)regs->GR_G(r1) = (S32)ARCH_DEP(vfetch4) ( effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* E316 LLGF  - Load Logical Long Fullword                     [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_logical_long_fullword)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load R1 register from second operand */
    regs->GR_G(r1) = ARCH_DEP(vfetch4) ( effective_addr2, b2, regs );
}


/*-------------------------------------------------------------------*/
/* E317 LLGT  - Load Logical Long Thirtyone                    [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_logical_long_thirtyone)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load R1 register from second operand */
    regs->GR_G(r1) = ARCH_DEP(vfetch4) ( effective_addr2, b2, regs )
                                                        & 0x7FFFFFFF;
}


/*-------------------------------------------------------------------*/
/* B2B2 LPSWE - Load PSW Extended                                [S] */
/*-------------------------------------------------------------------*/
DEF_INST(load_program_status_word_extended)
{
int     b2;                             /* Base of effective addr    */
U64     effective_addr2;                /* Effective address         */
QWORD   qword;
int     rc;

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    DW_CHECK(effective_addr2, regs);

#if defined(_FEATURE_ZSIE)
    if(regs->sie_state && (regs->siebk->ic[1] & SIE_IC1_LPSW))
        longjmp(regs->progjmp, SIE_INTERCEPT_INST);
#endif /*defined(_FEATURE_ZSIE)*/

    /* Perform serialization and checkpoint synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    /* Fetch new PSW from operand address */
    ARCH_DEP(vfetchc) ( qword, 16-1, effective_addr2, b2, regs );

    /* Load updated PSW */
    rc = ARCH_DEP(load_psw) ( regs, qword );
    if ( rc )
        ARCH_DEP(program_interrupt) (regs, rc);

    /* Perform serialization and checkpoint synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

}


/*-------------------------------------------------------------------*/
/* E303 LRAG  - Load Real Address Long                         [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_real_address_long)
{
int     r1;                             /* Register number           */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
U16     xcode;                          /* Exception code            */
int     private;                        /* 1=Private address space   */
int     protect;                        /* 1=ALE or page protection  */
int     stid;                           /* Segment table indication  */
int     cc;                             /* Condition code            */
RADR    n;                              /* 64-bit operand values     */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    SIE_MODE_XC_OPEX(regs);

    PRIV_CHECK(regs);

    /* Translate the effective address to a real address */
    cc = ARCH_DEP(translate_addr) (effective_addr2, b2, regs, ACCTYPE_LRA,
            &n, &xcode, &private, &protect, &stid, NULL, NULL);

    /* If ALET exception, set exception code in R1 bits 16-31
       set high order bit of R1, and set condition code 3 */
    if (cc == 4) {
        regs->GR_G(r1) = 0x8000000000000000ULL | xcode;
        regs->psw.cc = 3;
    }
    else
    {
        /* Set r1 and condition code as returned by translate_addr */
        regs->GR_G(r1) = n;
        regs->psw.cc = cc;
    }

}


#endif /*defined(FEATURE_ESAME)*/


#if defined(FEATURE_ESAME_INSTALLED) || defined(FEATURE_ESAME)
/*-------------------------------------------------------------------*/
/* B2B1 STFL  - Store Facilities List                            [S] */
/*-------------------------------------------------------------------*/
DEF_INST(store_facilities_list)
{
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */
PSA    *psa;                            /* -> Prefixed storage area  */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Set the main storage reference and change bits */
    STORAGE_KEY(regs->PX) |= (STORKEY_REF | STORKEY_CHANGE);

    /* Point to PSA in main storage */
    psa = (void*)(sysblk.mainstor + regs->PX);

    psa->stfl[0] = 0
#if defined(FEATURE_ESAME_N3_ESA390)
                 | 0x80
#endif /*defined(FEATURE_ESAME_N3_ESA390)*/
#if defined(FEATURE_ESAME_INSTALLED) || defined(FEATURE_ESAME)
                 | (sysblk.arch_z900 ? 0x40 : 0)
#endif /*defined(FEATURE_ESAME_INSTALLED) || defined(FEATURE_ESAME)*/
#if defined(FEATURE_ESAME)
                 | 0x20
#endif /*defined(FEATURE_ESAME)*/
                 ;
    psa->stfl[1] = 0;
    psa->stfl[2] = 0;
    psa->stfl[3] = 0;

}
#endif /*defined(FEATURE_ESAME_INSTALLED) || defined(FEATURE_ESAME)*/


#if defined(FEATURE_LOAD_REVERSED)
#if defined(FEATURE_ESAME)
/*-------------------------------------------------------------------*/
/* B90F LRVGR - Load Reversed Long Register                    [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_reversed_long_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Copy second operand to first operand */
    regs->GR_G(r1) = bswap_64(regs->GR_G(r2));
}
#endif /*defined(FEATURE_ESAME)*/


/*-------------------------------------------------------------------*/
/* B91F LRVR  - Load Reversed Register                         [RRE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_reversed_register)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Copy second operand to first operand */
    regs->GR_L(r1) = bswap_32(regs->GR_L(r2));
}


#if defined(FEATURE_ESAME)
/*-------------------------------------------------------------------*/
/* E30F LRVG  - Load Reversed Long                             [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_reversed_long)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load R1 register from second operand */
    regs->GR_G(r1) = bswap_64(ARCH_DEP(vfetch8) ( effective_addr2, b2, regs ));
}
#endif /*defined(FEATURE_ESAME)*/


/*-------------------------------------------------------------------*/
/* E31E LRV   - Load Reversed                                  [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_reversed)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load R1 register from second operand */
    regs->GR_L(r1) = bswap_32(ARCH_DEP(vfetch4) ( effective_addr2, b2, regs ));
}


#if defined(FEATURE_ESAME)
/*-------------------------------------------------------------------*/
/* E31F LRVH  - Load Reversed Half                             [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(load_reversed_half)
{
int     r1;                             /* Value of R field          */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Load R1 register from second operand */
    (S16)regs->GR_LHL(r1) = (S16)bswap_16(ARCH_DEP(vfetch2) ( effective_addr2, b2, regs ));
}
#endif /*defined(FEATURE_ESAME)*/


#if defined(FEATURE_ESAME)
/*-------------------------------------------------------------------*/
/* E32F STRVG - Store Reversed Long                            [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(store_reversed_long)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Store register contents at operand address */
    ARCH_DEP(vstore8) ( bswap_64(regs->GR_G(r1)), effective_addr2, b2, regs );
}
#endif /*defined(FEATURE_ESAME)*/


/*-------------------------------------------------------------------*/
/* E33E STRV  - Store Reversed                                 [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(store_reversed)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Store register contents at operand address */
    ARCH_DEP(vstore4) ( bswap_32(regs->GR_L(r1)), effective_addr2, b2, regs );
}


#if defined(FEATURE_ESAME)
/*-------------------------------------------------------------------*/
/* E33F STRVH - Store Reversed Half                            [RXE] */
/*-------------------------------------------------------------------*/
DEF_INST(store_reversed_half)
{
int     r1;                             /* Values of R fields        */
int     b2;                             /* Base of effective addr    */
VADR    effective_addr2;                /* Effective address         */

    RXE(inst, execflag, regs, r1, b2, effective_addr2);

    /* Store register contents at operand address */
    ARCH_DEP(vstore2) ( bswap_16(regs->GR_LHL(r1)), effective_addr2, b2, regs );
}
#endif /*defined(FEATURE_ESAME)*/
#endif /*defined(FEATURE_LOAD_REVERSED)*/


#if !defined(_GEN_ARCH)

// #define  _GEN_ARCH 964
// #include "esame.c"

// #undef   _GEN_ARCH
#define  _GEN_ARCH 390
#include "esame.c"

#undef   _GEN_ARCH
#define  _GEN_ARCH 370
#include "esame.c"

#endif /*!defined(_GEN_ARCH)*/
