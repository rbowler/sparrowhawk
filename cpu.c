/* CPU.C        (c) Copyright Roger Bowler, 1994-1999                */
/*              ESA/390 CPU Emulator                                 */

/*-------------------------------------------------------------------*/
/* This module implements the CPU instruction execution function of  */
/* the S/370 and ESA/390 architectures, as described in the manuals  */
/* GA22-7000-03 System/370 Principles of Operation                   */
/* SA22-7201-04 ESA/390 Principles of Operation                      */
/*-------------------------------------------------------------------*/

#include "hercules.h"

#undef  MODULE_TRACE
#undef  INSTRUCTION_COUNTING
#undef  SVC_TRACE

/*-------------------------------------------------------------------*/
/* Add two signed fullwords giving a signed fullword result          */
/* and return condition code                                         */
/*-------------------------------------------------------------------*/
static inline int
add_signed ( U32 *result, U32 op1, U32 op2 )
{
S64     r;
int     cc;

    r = (S64)(S32)op1 + (S64)(S32)op2;
    *result = (U32)r;
    cc = (r < -2147483648LL || r > 2147483647LL)? 3 :
        (r == 0)? 0 : (r < 0)? 1 : 2;
    return cc;
}

/*-------------------------------------------------------------------*/
/* Subtract two signed fullwords giving a signed fullword result     */
/* and return condition code                                         */
/*-------------------------------------------------------------------*/
static inline int
sub_signed ( U32 *result, U32 op1, U32 op2 )
{
S64     r;
int     cc;

    r = (S64)(S32)op1 - (S64)(S32)op2;
    *result = (U32)r;
    cc = (r < -2147483648LL || r > 2147483647LL)? 3 :
        (r == 0)? 0 : (r < 0)? 1 : 2;
    return cc;
}

/*-------------------------------------------------------------------*/
/* Add two unsigned fullwords giving an unsigned fullword result     */
/* and return condition code                                         */
/*-------------------------------------------------------------------*/
static inline int
add_logical ( U32 *result, U32 op1, U32 op2 )
{
U64     r;
int     cc;

    r = (U64)op1 + (U64)op2;
    *result = (U32)r;
    if ((r >> 32) == 0) cc = ((U32)r == 0)? 0 : 1;
    else cc = ((U32)r == 0)? 2 : 3;
    return cc;
}

/*-------------------------------------------------------------------*/
/* Subtract two unsigned fullwords giving an unsigned fullword       */
/* and return condition code                                         */
/*-------------------------------------------------------------------*/
static inline int
sub_logical ( U32 *result, U32 op1, U32 op2 )
{
U64     r;
int     cc;

    r = (U64)op1 + ~((U32)op2) + 1;
    *result = (U32)r;
    cc = ((U32)r == 0) ? 2 : ((r >> 32) == 0) ? 1 : 3;
    return cc;
}

/*-------------------------------------------------------------------*/
/* Multiply two signed fullwords giving a signed doubleword result   */
/*-------------------------------------------------------------------*/
static inline void
mul_signed ( U32 *resulthi, U32 *resultlo, U32 op1, U32 op2 )
{
S64     r;

    r = (S64)op1 * (S32)op2;
    *resulthi = (U64)r >> 32;
    *resultlo = (U64)r & 0xFFFFFFFF;
}

/*-------------------------------------------------------------------*/
/* Divide a signed doubleword dividend by a signed fullword divisor  */
/* giving a signed fullword remainder and a signed fullword quotient.*/
/* Returns 0 if successful, 1 if divide overflow.                    */
/*-------------------------------------------------------------------*/
static inline int
div_signed ( U32 *remainder, U32 *quotient, U32 dividendhi,
           U32 dividendlo, U32 divisor )
{
U64     dividend;
S64     quot, rem;

    if (divisor == 0) return 1;
    dividend = (U64)dividendhi << 32 | dividendlo;
    quot = (S64)dividend / (S32)divisor;
    rem = (S64)dividend % (S32)divisor;
    if (quot < -2147483648LL || quot > 2147483647LL) return 1;
    *quotient = (U32)quot;
    *remainder = (U32)rem;
    return 0;
}

/*-------------------------------------------------------------------*/
/* Perform serialization                                             */
/*-------------------------------------------------------------------*/
static inline void perform_serialization (void)
{
} /* end function perform_serialization */

/*-------------------------------------------------------------------*/
/* Perform checkpoint synchronization                                */
/*-------------------------------------------------------------------*/
static inline void perform_chkpt_sync (void)
{
} /* end function perform_chkpt_sync */

/*-------------------------------------------------------------------*/
/* Store current PSW at a specified address in main storage          */
/*-------------------------------------------------------------------*/
void store_psw (PSW *psw, BYTE *addr)
{
    addr[0] = psw->sysmask;
    addr[1] = (psw->pkey & 0xF0) | (psw->ecmode << 3)
                | (psw->mach << 2) | (psw->wait << 1) | psw->prob;

    if ( psw->ecmode ) {
        addr[2] = (psw->space << 7) | (psw->armode << 6)
                    | (psw->cc << 4)
                    | (psw->fomask << 3) | (psw->domask << 2)
                    | (psw->eumask << 1) | psw->sgmask;
        addr[3] = 0;
        addr[4] = (psw->amode << 7) | ((psw->ia & 0x7F000000) >> 24);
        addr[5] = (psw->ia & 0xFF0000) >> 16;
        addr[6] = (psw->ia & 0xFF00) >> 8;
        addr[7] = psw->ia & 0xFF;
    } else {
        addr[2] = psw->intcode >> 8;
        addr[3] = psw->intcode & 0xFF;
        addr[4] = (psw->ilc << 5) | (psw->cc << 4)
                    | (psw->fomask << 3) | (psw->domask << 2)
                    | (psw->eumask << 1) | psw->sgmask;
        addr[5] = (psw->ia & 0xFF0000) >> 16;
        addr[6] = (psw->ia & 0xFF00) >> 8;
        addr[7] = psw->ia & 0xFF;
    }
} /* end function store_psw */

/*-------------------------------------------------------------------*/
/* Load current PSW from a specified address in main storage         */
/* Returns 0 if valid, 0x0006 if specification exception             */
/*-------------------------------------------------------------------*/
int load_psw (PSW *psw, BYTE *addr)
{
    psw->sysmask = addr[0];
    psw->pkey = addr[1] & 0xF0;
    psw->ecmode = (addr[1] & 0x08) >> 3;
    psw->mach = (addr[1] & 0x04) >> 2;
    psw->wait = (addr[1] & 0x02) >> 1;
    psw->prob = addr[1] & 0x01;

    if ( psw->ecmode ) {

        /* Processing for EC mode PSW */
        psw->space = (addr[2] & 0x80) >> 7;
        psw->armode = (addr[2] & 0x40) >> 6;
        psw->intcode = 0;
        psw->ilc = 0;
        psw->cc = (addr[2] & 0x30) >> 4;
        psw->fomask = (addr[2] & 0x08) >> 3;
        psw->domask = (addr[2] & 0x04) >> 2;
        psw->eumask = (addr[2] & 0x02) >> 1;
        psw->sgmask = addr[2] & 0x01;
        psw->amode = (addr[4] & 0x80) >> 7;
        psw->ia = ((addr[4] & 0x7F) << 24)
                | (addr[5] << 16) | (addr[6] << 8) | addr[7];

        /* Bits 0 and 2-4 of system mask must be zero */
        if ((addr[0] & 0xB8) != 0)
            return PGM_SPECIFICATION_EXCEPTION;

        /* Bits 24-31 must be zero */
        if (addr[3] != 0)
            return PGM_SPECIFICATION_EXCEPTION;

#ifndef FEATURE_DUAL_ADDRESS_SPACE
        /* If DAS feature not installed then bit 16 must be zero */
        if (psw->space)
            return PGM_SPECIFICATION_EXCEPTION;
#endif /*!FEATURE_DUAL_ADDRESS_SPACE*/

#ifndef FEATURE_ACCESS_REGISTERS
        /* If not ESA/370 or ESA/390 then bit 17 must be zero */
        if (psw->armode)
            return PGM_SPECIFICATION_EXCEPTION;
#endif /*!FEATURE_ACCESS_REGISTERS*/

#ifdef FEATURE_BIMODAL_ADDRESSING
        /* For 370-XA, ESA/370, and ESA/390,
           if amode=24, bits 33-39 must be zero */
        if (addr[4] > 0x00 && addr[4] < 0x80)
            return PGM_SPECIFICATION_EXCEPTION;
#else /*!FEATURE_BIMODAL_ADDRESSING*/
        /* For S/370, bits 32-39 must be zero */
        if (addr[4] != 0x00)
            return PGM_SPECIFICATION_EXCEPTION;
#endif /*!FEATURE_BIMODAL_ADDRESSING*/

    } else {

#ifdef FEATURE_BCMODE
        /* Processing for S/370 BC mode PSW */
        psw->space = 0;
        psw->armode = 0;
        psw->intcode = (addr[2] << 8) | addr[3];
        psw->ilc = (addr[4] >> 6) * 2;
        psw->cc = (addr[4] & 0x30) >> 4;
        psw->fomask = (addr[4] & 0x08) >> 3;
        psw->domask = (addr[4] & 0x04) >> 2;
        psw->eumask = (addr[4] & 0x02) >> 1;
        psw->sgmask = addr[4] & 0x01;
        psw->amode = 0;
        psw->ia = (addr[5] << 16) | (addr[6] << 8) | addr[7];
#else /*!FEATURE_BCMODE*/
        /* BC mode is not valid for 370-XA, ESA/370, or ESA/390 */
        return PGM_SPECIFICATION_EXCEPTION;
#endif /*!FEATURE_BCMODE*/

    }

    /* Check for wait state PSW */
    if (psw->wait && (sysblk.insttrace || sysblk.inststep
//      || psw->ia != 0
        ))
    {
        logmsg ("Wait state PSW loaded: "
                "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
                addr[0], addr[1], addr[2], addr[3],
                addr[4], addr[5], addr[6], addr[7]);
    }

    return 0;
} /* end function load_psw */

/*-------------------------------------------------------------------*/
/* Load program check new PSW                                        */
/*-------------------------------------------------------------------*/
void program_check (int code)
{
PSA    *psa;                            /* -> Prefixed storage area  */
int     rc;                             /* Return code               */
DWORD   dword;                          /* Doubleword work area      */
REGS   *regs = &(sysblk.regs[0]);

    /* Back up the PSW for exceptions which cause nullification */
    if (code == PGM_PAGE_TRANSLATION_EXCEPTION
        || code == PGM_SEGMENT_TRANSLATION_EXCEPTION
        || code == PGM_TRACE_TABLE_EXCEPTION
        || code == PGM_AFX_TRANSLATION_EXCEPTION
        || code == PGM_ASX_TRANSLATION_EXCEPTION
        || code == PGM_LX_TRANSLATION_EXCEPTION
        || code == PGM_EX_TRANSLATION_EXCEPTION
        || code == PGM_PRIMARY_AUTHORITY_EXCEPTION
        || code == PGM_SECONDARY_AUTHORITY_EXCEPTION
        || code == PGM_ALEN_TRANSLATION_EXCEPTION
        || code == PGM_ALE_SEQUENCE_EXCEPTION
        || code == PGM_ASTE_VALIDITY_EXCEPTION
        || code == PGM_ASTE_SEQUENCE_EXCEPTION
        || code == PGM_EXTENDED_AUTHORITY_EXCEPTION
        || code == PGM_STACK_FULL_EXCEPTION
        || code == PGM_STACK_EMPTY_EXCEPTION
        || code == PGM_STACK_SPECIFICATION_EXCEPTION
        || code == PGM_STACK_TYPE_EXCEPTION
        || code == PGM_STACK_OPERATION_EXCEPTION)
    {
        regs->psw.ia -= regs->psw.ilc;
        regs->psw.ia &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
        regs->psw.ilc = 0;
    }

    /* Store the interrupt code in the PSW */
    regs->psw.intcode = code;

    /* Trace the program check */
    if (code != PGM_TRACE_TABLE_EXCEPTION)
    {
        logmsg ("Program check CODE=%4.4X ILC=%d ",
                code, regs->psw.ilc);
        instfetch (dword, regs->psw.ia - regs->psw.ilc, regs);
        display_inst (regs, dword);
//      if (code != PGM_PAGE_TRANSLATION_EXCEPTION
//          && code != PGM_SEGMENT_TRANSLATION_EXCEPTION)
//          regs->cpustate = CPUSTATE_STOPPING;
    }

    /* Point to PSA in main storage */
    psa = (PSA*)(sysblk.mainstor + regs->pxr);

    /* For ECMODE, store program interrupt code at PSA+X'8C' */
    if ( regs->psw.ecmode )
    {
        psa->pgmint[0] = 0;
        psa->pgmint[1] = regs->psw.ilc;
        psa->pgmint[2] = code >> 8;
        psa->pgmint[3] = code & 0xFF;
    }

    /* Store current PSW at PSA+X'28' */
    store_psw (&(regs->psw), psa->pgmold);

    /* Load new PSW from PSA+X'68' */
    rc = load_psw (&(regs->psw), psa->pgmnew);
    if ( rc )
    {
        logmsg ("Invalid program-check new PSW: "
                "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
                psa->pgmnew[0], psa->pgmnew[1], psa->pgmnew[2],
                psa->pgmnew[3], psa->pgmnew[4], psa->pgmnew[5],
                psa->pgmnew[6], psa->pgmnew[7]);
        regs->cpustate = CPUSTATE_STOPPED;
    }

    /* Return directly to cpu_thread function */
    longjmp (regs->progjmp, code);

} /* end function program_check */

/*-------------------------------------------------------------------*/
/* Load restart new PSW                                              */
/*-------------------------------------------------------------------*/
static int psw_restart (REGS *regs)
{
int     rc;                             /* Return code               */
PSA    *psa;                            /* -> Prefixed storage area  */

    /* Zeroize the interrupt code in the PSW */
    regs->psw.intcode = 0;

    /* Point to PSA in main storage */
    psa = (PSA*)(sysblk.mainstor + regs->pxr);

    /* Store current PSW at PSA+X'8' */
    store_psw (&(regs->psw), psa->iplccw1);

    /* Load new PSW from PSA+X'0' */
    rc = load_psw (&(regs->psw), psa->iplpsw);
    if ( rc )
    {
        logmsg ("Invalid restart new PSW: "
                "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
                psa->iplpsw[0], psa->iplpsw[1], psa->iplpsw[2],
                psa->iplpsw[3], psa->iplpsw[4], psa->iplpsw[5],
                psa->iplpsw[6], psa->iplpsw[7]);
        return rc;
    }

    return 0;
} /* end function psw_restart */

/*-------------------------------------------------------------------*/
/* Execute an instruction                                            */
/*-------------------------------------------------------------------*/
static void execute_instruction (BYTE inst[], int execflag, REGS *regs)
{
BYTE    opcode;                         /* Instruction byte 0        */
BYTE    ibyte;                          /* Instruction byte 1        */
int     r1, r2, r3, b1, b2, x2;         /* Values of R fields        */
U32     newia;                          /* New instruction address   */
int     ilc;                            /* Instruction length code   */
int     m;                              /* Condition code mask       */
U32     n, n1, n2;                      /* 32-bit operand values     */
int     private;                        /* 1=Private address space   */
int     protect;                        /* 1=ALE or page protection  */
int     cc;                             /* Condition code            */
BYTE    obyte, sbyte, dbyte;            /* Byte work areas           */
DWORD   dword;                          /* Doubleword work area      */
U64     dreg;                           /* Double register work area */
int     d, h, i, j;                     /* Integer work areas        */
int     divide_overflow;                /* 1=divide overflow         */
int     effective_addr = 0;             /* Effective address         */
int     effective_addr2 = 0;            /* Effective address         */
int     ar1, ar2;                       /* Access register numbers   */
PSA    *psa;                            /* -> prefixed storage area  */
int     rc;                             /* Return code               */
DEVBLK *dev;                            /* -> device block for SIO   */
U32     ccwaddr;                        /* CCW address for start I/O */
U32     ioparm;                         /* I/O interruption parameter*/
U16     xcode;                          /* Exception code            */
#if defined(FEATURE_HALFWORD_IMMEDIATE) \
    || defined(FEATURE_RELATIVE_BRANCH)
U16     h1, h2, h3;                     /* 16-bit operand values     */
#endif /*FEATURE_HALFWORD_IMMEDIATE || FEATURE_RELATIVE_BRANCH*/
#ifdef FEATURE_DUAL_ADDRESS_SPACE
U32     asteo;                          /* Real address of ASTE      */
U32     aste[16];                       /* ASN second table entry    */
U16     ax;                             /* Authorization index       */
U16     pkm;                            /* PSW key mask              */
U16     pasn;                           /* Primary ASN               */
U16     sasn;                           /* Secondary ASN             */
#endif /*FEATURE_DUAL_ADDRESS_SPACE*/
#ifdef FEATURE_S370_CHANNEL
BYTE    ccwkey;                         /* Bits 0-3=key, 4=7=zeroes  */
#endif /*FEATURE_S370_CHANNEL*/
#ifdef FEATURE_CHANNEL_SUBSYSTEM
U32     ioid;                           /* I/O interruption address  */
PMCW    pmcw;                           /* Path management ctl word  */
ORB     orb;                            /* Operation request block   */
SCHIB   schib;                          /* Subchannel information blk*/
IRB     irb;                            /* Interruption response blk */
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/
BYTE    cwork1[256], cwork2[256];       /* Character work areas      */
#ifdef MODULE_TRACE
static BYTE module[8];                  /* Module name               */
#endif /*MODULE_TRACE*/

    /* Extract the opcode and R1/R2/I fields */
    opcode = inst[0];
    ibyte = inst[1];
    r1 = ibyte >> 4;
    r2 = r3 = x2 = ibyte & 0x0F;
    b1 = 0;
    ar1 = ar2 = 0;

    /* For RRE instructions, extract R1/R2 fields from byte 3 */
    if (opcode == 0xB2) {
        r1 = inst[3] >> 4;
        r2 = inst[3] & 0x0F;
    }

    /* Determine the instruction length */
    ilc = (opcode < 0x40) ? 2 : (opcode < 0xC0) ? 4 : 6;

    /* Calculate the effective address */
    if (ilc > 2) {
        b1 = inst[2] >> 4;
        effective_addr = ((inst[2] & 0x0F) << 8) | inst[3];
        if (b1 != 0)
        {
            effective_addr += regs->gpr[b1];
            effective_addr &=
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
        }
        ar1 = b1;
    }

    /* Apply indexing for RX instructions */
    if ((opcode >= 0x40 && opcode <= 0x7F) || opcode == 0xB1) {
        if (x2 != 0)
        {
            effective_addr += regs->gpr[x2];
            effective_addr &=
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
        }
    }

    /* Calculate the 2nd effective address for SS instructions */
    if (ilc > 4) {
        b2 = inst[4] >> 4;
        effective_addr2 = ((inst[4] & 0x0F) << 8) | inst[5];
        if (b2 != 0)
        {
            effective_addr2 += regs->gpr[b2];
            effective_addr2 &=
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
        }
        ar2 = b2;
    }

    /* If this instruction was not the subject of an execute,
       update the PSW instruction length and address */
    if (execflag == 0)
    {
        regs->psw.ilc = ilc;
        regs->psw.ia += ilc;
        regs->psw.ia &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    }

    switch (opcode) {

    case 0x01:
    /*---------------------------------------------------------------*/
    /* Extended instructions (opcode 01xx)                           */
    /*---------------------------------------------------------------*/

        /* The immediate byte determines the instruction opcode */
        switch ( ibyte ) {

        case 0x01:
        /*-----------------------------------------------------------*/
        /* 0101: PR - Program Return                             [E] */
        /*-----------------------------------------------------------*/

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Perform unstacking process */
            rc = program_return (regs);

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Generate space switch event if required */
            if (rc) {
                program_check (PGM_SPACE_SWITCH_EVENT);
            }

            break;

        default:
        /*-----------------------------------------------------------*/
        /* 01xx: Invalid operation                                   */
        /*-----------------------------------------------------------*/
            program_check (PGM_OPERATION_EXCEPTION);
            goto terminate;

        } /* end switch(ibyte) */
        break;

    case 0x04:
    /*---------------------------------------------------------------*/
    /* SPM      Set Program Mask                                [RR] */
    /*---------------------------------------------------------------*/

        /* Set condition code from bits 2-3 of R1 register */
        regs->psw.cc = ( regs->gpr[r1] & 0x30000000 ) >> 28;

        /* Set program mask from bits 4-7 of R1 register */
        regs->psw.fomask = ( regs->gpr[r1] & 0x08000000 ) >> 27;
        regs->psw.domask = ( regs->gpr[r1] & 0x04000000 ) >> 26;
        regs->psw.eumask = ( regs->gpr[r1] & 0x02000000 ) >> 25;
        regs->psw.sgmask = ( regs->gpr[r1] & 0x01000000 ) >> 24;

        break;

    case 0x05:
    /*---------------------------------------------------------------*/
    /* BALR     Branch and Link Register                        [RR] */
    /*---------------------------------------------------------------*/

        /* Compute the branch address from the R2 operand */
        newia = regs->gpr[r2];

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
            goto setia;
        } /* end if(r2!=0) */

        break;

    case 0x06:
    /*---------------------------------------------------------------*/
    /* BCTR     Branch on Count Register                        [RR] */
    /*---------------------------------------------------------------*/

        /* Compute the branch address from the R2 operand */
        newia = regs->gpr[r2];

        /* Subtract 1 from the R1 operand and branch if result
           is non-zero and R2 operand is not register zero */
        if ( --(regs->gpr[r1]) && r2 != 0 )
            goto setia;

        break;

    case 0x07:
    /*---------------------------------------------------------------*/
    /* BCR      Branch on Condition Register                    [RR] */
    /*---------------------------------------------------------------*/

        /* Compute the branch address from the R2 operand */
        newia = regs->gpr[r2];

        /* Generate a bit mask from the condition code value */
        m = ( regs->psw.cc == 0 ) ? 0x08
          : ( regs->psw.cc == 1 ) ? 0x04
          : ( regs->psw.cc == 2 ) ? 0x02 : 0x01;

        /* Branch if R1 mask bit is set and R2 is not register 0 */
        if ( (r1 & m) != 0 && r2 != 0 )
            goto setia;

        /* Perform serialization and checkpoint synchronization if
           the mask is all ones and the register is all zeroes */
        if ( r1 == 0x0F && r2 == 0 )
        {
            perform_serialization ();
            perform_chkpt_sync ();
        }

        break;

#ifdef FEATURE_BASIC_STORAGE_KEYS
    case 0x08:
    /*---------------------------------------------------------------*/
    /* SSK      Set Storage Key                                 [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Program check if R2 bits 28-31 are not zeroes */
        if ( regs->gpr[r2] & 0x0000000F )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Load 2K block address from R2 register */
        n = regs->gpr[r2] & 0x00FFF800;

        /* Convert real address to absolute address */
        n = APPLY_PREFIXING (n, regs->pxr);

        /* Addressing exception if block is outside main storage */
        if ( n >= sysblk.mainsize )
        {
            program_check (PGM_ADDRESSING_EXCEPTION);
            goto terminate;
        }

        /* Update the storage key from R1 register bits 24-30 */
        n >>= 12;
        sysblk.storkeys[n] = regs->gpr[r1] & 0xFE;

//      /*debug*/logmsg("SSK storage block %8.8X key %2.2X\n",
//                      regs->gpr[r2], regs->gpr[r1] & 0xFE);

        break;

    case 0x09:
    /*---------------------------------------------------------------*/
    /* ISK      Insert Storage Key                              [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Program check if R2 bits 28-31 are not zeroes */
        if ( regs->gpr[r2] & 0x0000000F )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Load 2K block address from R2 register */
        n = regs->gpr[r2] & 0x00FFF800;

        /* Convert real address to absolute address */
        n = APPLY_PREFIXING (n, regs->pxr);

        /* Addressing exception if block is outside main storage */
        if ( n >= sysblk.mainsize )
        {
            program_check (PGM_ADDRESSING_EXCEPTION);
            goto terminate;
        }

        /* Insert the storage key into R1 register bits 24-31 */
        n >>= 12;
        regs->gpr[r1] &= 0xFFFFFF00;
        regs->gpr[r1] |= sysblk.storkeys[n];

        /* In BC mode, clear bits 29-31 of R1 register */
        if ( regs->psw.ecmode == 0 )
            regs->gpr[r1] &= 0xFFFFFFF8;

        /*debug*/logmsg("ISK storage block %8.8X key %2.2X\n",
                        regs->gpr[r2], regs->gpr[r1] & 0xFE);

        break;
#endif /*FEATURE_BASIC_STORAGE_KEYS*/

    case 0x0A:
    /*---------------------------------------------------------------*/
    /* SVC      Supervisor Call                                 [RR] */
    /*---------------------------------------------------------------*/

        /* Use the I-byte to set the SVC interruption code */
        regs->psw.intcode = ibyte;

#ifdef SVC_TRACE
        /* Trace BLDL/FIND */
        if (ibyte == 18)
        {
            BYTE memname[8];
            n = regs->gpr[0];
            if ((regs->gpr[1] & 0x80000000) == 0) n += 4;
            n &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
            vfetchc (memname, 7, n, 0, regs);
            for (i=0; i < 8; i++)
                memname[i] = ebcdic_to_ascii[memname[i]];
            display_inst (regs, inst);
            logmsg ("SVC %u (%s):%8.8s\n", ibyte,
                (regs->gpr[1] & 0x80000000) ? "FIND" : "BLDL",
                memname);
        }

    #if 0
        /* Trace LINK and XCTL module name */
        if (ibyte == 6 || ibyte == 7)
        {
            BYTE epname[8];
            n = regs->gpr[15];
            n &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
            n = vfetch4 (n, 0, regs);
            n &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
            vfetchc (epname, 7, n, 0, regs);
            for (i=0; i < 8; i++)
                epname[i] = ebcdic_to_ascii[epname[i]];
            display_inst (regs, inst);
            logmsg ("SVC %u:%8.8s\n", ibyte, epname);
        }

        /* Trace WTO and SVC34 */
        if (ibyte == 34 || ibyte == 35)
        {
            BYTE message[256];
            n = regs->gpr[1];
            n &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
            n = vfetch2 (n, 0, regs);
            if (n > 130) n = 130;
            if (n > 0)
                vfetchc (message, n-1, regs->gpr[1], 0, regs);
            for (i=4; i < n; i++)
                message[i] = ebcdic_to_ascii[message[i]];
            message[i] = '\0';
            display_inst (regs, inst);
            logmsg ("SVC %u:%s\n", ibyte, message+4);
        }
    #endif

        /* Stop on selected SVC numbers */
        if (ibyte == 13)
        {
            display_inst (regs, inst);
            regs->cpustate = CPUSTATE_STOPPING;
        }
#endif /*SVC_TRACE*/

        /* Point to PSA in main storage */
        psa = (PSA*)(sysblk.mainstor + regs->pxr);

        /* For ECMODE, store SVC interrupt code at PSA+X'88' */
        if ( regs->psw.ecmode )
        {
            psa->svcint[0] = 0;
            psa->svcint[1] = regs->psw.ilc;
            psa->svcint[2] = 0;
            psa->svcint[3] = ibyte;
        }

        /* Store current PSW at PSA+X'20' */
        store_psw ( &(regs->psw), psa->svcold );

        /* Load new PSW from PSA+X'60' */
        rc = load_psw ( &(regs->psw), psa->svcnew );
        if ( rc )
        {
            program_check (rc);
            goto terminate;
        }

        /* Perform serialization and checkpoint synchronization */
        perform_serialization ();
        perform_chkpt_sync ();

        break;

#ifdef FEATURE_BIMODAL_ADDRESSING
    case 0x0B:
    /*---------------------------------------------------------------*/
    /* BSM      Branch and Set Mode                             [RR] */
    /*---------------------------------------------------------------*/

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
            }
            else
            {
                regs->psw.amode = 0;
                regs->psw.ia = newia & 0x00FFFFFF;
            }
        }

        break;

    case 0x0C:
    /*---------------------------------------------------------------*/
    /* BASSM    Branch and Save and Set Mode                    [RR] */
    /*---------------------------------------------------------------*/

        /* Compute the branch address from the R2 operand */
        newia = regs->gpr[r2];

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
            }
            else
            {
                regs->psw.amode = 0;
                regs->psw.ia = newia & 0x00FFFFFF;
            }
        }

        break;
#endif /*FEATURE_BIMODAL_ADDRESSING*/

    case 0x0D:
    /*---------------------------------------------------------------*/
    /* BASR     Branch and Save Register                        [RR] */
    /*---------------------------------------------------------------*/

        /* Compute the branch address from the R2 operand */
        newia = regs->gpr[r2];

        /* Save the link information in the R1 operand */
        if ( regs->psw.amode )
            regs->gpr[r1] = 0x80000000 | regs->psw.ia;
        else
            regs->gpr[r1] = regs->psw.ia & 0x00FFFFFF;

        /* Execute the branch unless R2 specifies register 0 */
        if ( r2 != 0 )
        {
            goto setia;
        } /* end if(r2!=0) */

        break;

    case 0x0E:
    /*---------------------------------------------------------------*/
    /* MVCL     Move Long                                       [RR] */
    /*---------------------------------------------------------------*/

        /* Perform move long and set condition code */
        regs->psw.cc = move_long (r1, r2, regs);

        break;

    case 0x0F:
    /*---------------------------------------------------------------*/
    /* CLCL     Compare Logical Long                            [RR] */
    /*---------------------------------------------------------------*/

        /* Perform compare long and set condition code */
        regs->psw.cc = compare_long (r1, r2, regs);

        break;

    case 0x10:
    /*---------------------------------------------------------------*/
    /* LPR      Load Positive Register                          [RR] */
    /*---------------------------------------------------------------*/

        /* Condition code 3 and program check if overflow */
        if ( regs->gpr[r2] == 0x80000000 )
        {
            regs->gpr[r1] = regs->gpr[r2];
            regs->psw.cc = 3;
            if ( regs->psw.fomask )
            {
                program_check (PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
                goto terminate;
            }
            break;
        }

        /* Load positive value of second operand and set cc */
        (S32)regs->gpr[r1] = (S32)regs->gpr[r2] < 0 ?
                                -((S32)regs->gpr[r2]) :
                                (S32)regs->gpr[r2];

        regs->psw.cc = (S32)regs->gpr[r1] == 0 ? 0 : 2;

        break;

    case 0x11:
    /*---------------------------------------------------------------*/
    /* LNR      Load Negative Register                          [RR] */
    /*---------------------------------------------------------------*/

        /* Load negative value of second operand and set cc */
        (S32)regs->gpr[r1] = (S32)regs->gpr[r2] > 0 ?
                                -((S32)regs->gpr[r2]) :
                                (S32)regs->gpr[r2];

        regs->psw.cc = (S32)regs->gpr[r1] == 0 ? 0 : 1;

        break;

    case 0x12:
    /*---------------------------------------------------------------*/
    /* LTR      Load and Test Register                          [RR] */
    /*---------------------------------------------------------------*/

        /* Copy second operand and set condition code */
        regs->gpr[r1] = regs->gpr[r2];

        regs->psw.cc = (S32)regs->gpr[r1] < 0 ? 1 :
                      (S32)regs->gpr[r1] > 0 ? 2 : 0;

        break;

    case 0x13:
    /*---------------------------------------------------------------*/
    /* LCR      Load Complement Register                        [RR] */
    /*---------------------------------------------------------------*/

        /* Condition code 3 and program check if overflow */
        if ( regs->gpr[r2] == 0x80000000 )
        {
            regs->gpr[r1] = regs->gpr[r2];
            regs->psw.cc = 3;
            if ( regs->psw.fomask )
            {
                program_check (PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
                goto terminate;
            }
            break;
        }

        /* Load complement of second operand and set condition code */
        (S32)regs->gpr[r1] = -((S32)regs->gpr[r2]);

        regs->psw.cc = (S32)regs->gpr[r1] < 0 ? 1 :
                      (S32)regs->gpr[r1] > 0 ? 2 : 0;

        break;

    case 0x14:
    /*---------------------------------------------------------------*/
    /* NR       And Register                                    [RR] */
    /*---------------------------------------------------------------*/

        /* AND second operand with first and set condition code */
        regs->psw.cc = ( regs->gpr[r1] &= regs->gpr[r2] ) ? 1 : 0;

        break;

    case 0x15:
    /*---------------------------------------------------------------*/
    /* CLR      Compare Logical Register                        [RR] */
    /*---------------------------------------------------------------*/

        /* Compare unsigned operands and set condition code */
        regs->psw.cc = regs->gpr[r1] < regs->gpr[r2] ? 1 :
                      regs->gpr[r1] > regs->gpr[r2] ? 2 : 0;

        break;

    case 0x16:
    /*---------------------------------------------------------------*/
    /* OR       Or Register                                     [RR] */
    /*---------------------------------------------------------------*/

        /* OR second operand with first and set condition code */
        regs->psw.cc = ( regs->gpr[r1] |= regs->gpr[r2] ) ? 1 : 0;

        break;

    case 0x17:
    /*---------------------------------------------------------------*/
    /* XR       Exclusive Or Register                           [RR] */
    /*---------------------------------------------------------------*/

        /* XOR second operand with first and set condition code */
        regs->psw.cc = ( regs->gpr[r1] ^= regs->gpr[r2] ) ? 1 : 0;

        break;

    case 0x18:
    /*---------------------------------------------------------------*/
    /* LR       Load Register                                   [RR] */
    /*---------------------------------------------------------------*/

        /* Copy second operand to first operand */
        regs->gpr[r1] = regs->gpr[r2];

        break;

    case 0x19:
    /*---------------------------------------------------------------*/
    /* CR       Compare Register                                [RR] */
    /*---------------------------------------------------------------*/

        /* Compare signed operands and set condition code */
        regs->psw.cc =
                (S32)regs->gpr[r1] < (S32)regs->gpr[r2] ? 1 :
                (S32)regs->gpr[r1] > (S32)regs->gpr[r2] ? 2 : 0;

        break;

    case 0x1A:
    /*---------------------------------------------------------------*/
    /* AR       Add Register                                    [RR] */
    /*---------------------------------------------------------------*/

        /* Add signed operands and set condition code */
        regs->psw.cc =
                add_signed (&(regs->gpr[r1]),
                        regs->gpr[r1],
                        regs->gpr[r2]);

        /* Program check if fixed-point overflow */
        if ( regs->psw.cc == 3 && regs->psw.fomask )
        {
            program_check (PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
            goto terminate;
        }

        break;

    case 0x1B:
    /*---------------------------------------------------------------*/
    /* SR       Subtract Register                               [RR] */
    /*---------------------------------------------------------------*/

        /* Subtract signed operands and set condition code */
        regs->psw.cc =
                sub_signed (&(regs->gpr[r1]),
                        regs->gpr[r1],
                        regs->gpr[r2]);

        /* Program check if fixed-point overflow */
        if ( regs->psw.cc == 3 && regs->psw.fomask )
        {
            program_check (PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
            goto terminate;
        }

        break;

    case 0x1C:
    /*---------------------------------------------------------------*/
    /* MR       Multiply Register                               [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is odd */
        if ( r1 & 1 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Multiply r1+1 by r2 and place result in r1 and r1+1 */
        mul_signed (&(regs->gpr[r1]),&(regs->gpr[r1+1]),
                        regs->gpr[r1+1],
                        regs->gpr[r2]);

        break;

    case 0x1D:
    /*---------------------------------------------------------------*/
    /* DR       Divide Register                                 [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is odd */
        if ( r1 & 1 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Divide r1::r1+1 by r2, remainder in r1, quotient in r1+1 */
        divide_overflow =
            div_signed (&(regs->gpr[r1]),&(regs->gpr[r1+1]),
                        regs->gpr[r1],
                        regs->gpr[r1+1],
                        regs->gpr[r2]);

        /* Program check if overflow */
        if ( divide_overflow )
        {
            program_check (PGM_FIXED_POINT_DIVIDE_EXCEPTION);
            goto terminate;
        }

        break;

    case 0x1E:
    /*---------------------------------------------------------------*/
    /* ALR      Add Logical Register                            [RR] */
    /*---------------------------------------------------------------*/

        /* Add signed operands and set condition code */
        regs->psw.cc =
                add_logical (&(regs->gpr[r1]),
                        regs->gpr[r1],
                        regs->gpr[r2]);

        break;

    case 0x1F:
    /*---------------------------------------------------------------*/
    /* SLR      Subtract Logical Register                       [RR] */
    /*---------------------------------------------------------------*/

        /* Subtract unsigned operands and set condition code */
        regs->psw.cc =
                sub_logical (&(regs->gpr[r1]),
                        regs->gpr[r1],
                        regs->gpr[r2]);

        break;

    case 0x28:
    /*---------------------------------------------------------------*/
    /* LDR      Load Floating Point Long Register               [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if ( r1 & 1 || r1 > 6 || r2 & 1 || r2 > 6)
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Copy register contents */
        regs->fpr[r1] = regs->fpr[r2];
        regs->fpr[r1+1] = regs->fpr[r2+1];

        break;

    case 0x35:
    /*---------------------------------------------------------------*/
    /* LRER     Load Rounded Floating Point Short Register      [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if ( r1 & 1 || r1 > 6 || r2 & 1 || r2 > 6)
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Copy and round register contents */
        regs->fpr[r1] = regs->fpr[r2];
        /*INCOMPLETE*/

        break;

    case 0x38:
    /*---------------------------------------------------------------*/
    /* LER      Load Floating Point Short Register              [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if ( r1 & 1 || r1 > 6 || r2 & 1 || r2 > 6)
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Copy register contents */
        regs->fpr[r1] = regs->fpr[r2];

        break;

    case 0x40:
    /*---------------------------------------------------------------*/
    /* STH      Store Halfword                                  [RX] */
    /*---------------------------------------------------------------*/

        /* Store rightmost 2 bytes of R1 register at operand address */
        vstore2 ( regs->gpr[r1] & 0xFFFF, effective_addr, ar1, regs );

        break;

    case 0x41:
    /*---------------------------------------------------------------*/
    /* LA       Load Address                                    [RX] */
    /*---------------------------------------------------------------*/

        /* Load operand address into register */
        regs->gpr[r1] = effective_addr;

        break;

    case 0x42:
    /*---------------------------------------------------------------*/
    /* STC      Store Character                                 [RX] */
    /*---------------------------------------------------------------*/

        /* Store rightmost byte of R1 register at operand address */
        vstoreb ( regs->gpr[r1] & 0xFF, effective_addr, ar1, regs );

        break;

    case 0x43:
    /*---------------------------------------------------------------*/
    /* IC       Insert Character                                [RX] */
    /*---------------------------------------------------------------*/

        /* Load rightmost byte of R1 register from operand address */
        regs->gpr[r1] &= 0xFFFFFF00;
        regs->gpr[r1] |= vfetchb ( effective_addr, ar1, regs );

        break;

    case 0x44:
    /*---------------------------------------------------------------*/
    /* EX       Execute                                         [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if operand is not on a halfword boundary */
        if ( effective_addr & 0x00000001 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Fetch target instruction from operand address */
        instfetch (dword, effective_addr, regs);

        /* Program check if recursive execute */
        if ( dword[0] == 0x44 )
        {
            program_check (PGM_EXECUTE_EXCEPTION);
            goto terminate;
        }

        /* Or 2nd byte of instruction with low-order byte of R1 */
        if ( r1 != 0 )
            dword[1] |= (regs->gpr[r1] & 0xFF);

        /* Execute the target instruction */
        execute_instruction (dword, 1, regs);

        break;

    case 0x45:
    /*---------------------------------------------------------------*/
    /* BAL      Branch and Link                                 [RX] */
    /*---------------------------------------------------------------*/

        /* Use the second operand address as the branch address */
        newia = effective_addr;

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

        /* Execute the branch */
        goto setia;

        break;

    case 0x46:
    /*---------------------------------------------------------------*/
    /* BCT      Branch on Count                                 [RX] */
    /*---------------------------------------------------------------*/

        /* Use the operand address as the branch address */
        newia = effective_addr;

        /* Subtract 1 from the R1 operand and branch if non-zero */
        if ( --(regs->gpr[r1]) )
            goto setia;

        break;

    case 0x47:
    /*---------------------------------------------------------------*/
    /* BC       Branch on Condition                             [RX] */
    /*---------------------------------------------------------------*/

        /* Use the operand address as the branch address */
        newia = effective_addr;

#ifdef MODULE_TRACE
        /* Test for module entry */
        if (inst[1] == 0xF0 && inst[2] == 0xF0
            && inst[3] == ((vfetchb(regs->psw.ia, 0, regs) + 6) & 0xFE))
        {
            vfetchc (module, 7, regs->psw.ia + 1, 0, regs);
            for (i=0; i < 8; i++)
                module[i] = ebcdic_to_ascii[module[i]];
            logmsg ("Entering %8.8s at %8.8X\n",
                    module, regs->psw.ia - 4);
//          if (memcmp(module, "????????", 8) == 0) sysblk.inststep = 1;
        }
#endif /*MODULE_TRACE*/

        /* Generate a bit mask from the condition code value */
        m = ( regs->psw.cc == 0 ) ? 0x08
          : ( regs->psw.cc == 1 ) ? 0x04
          : ( regs->psw.cc == 2 ) ? 0x02 : 0x01;

        /* Branch if R1 mask bit is set */
        if ( (r1 & m) != 0 )
            goto setia;

        break;

    case 0x48:
    /*---------------------------------------------------------------*/
    /* LH       Load Halfword                                   [RX] */
    /*---------------------------------------------------------------*/

        /* Load rightmost 2 bytes of register from operand address */
        regs->gpr[r1] = vfetch2 ( effective_addr, ar1, regs );

        /* Propagate sign bit to leftmost 2 bytes of register */
        if ( regs->gpr[r1] > 0x7FFF )
            regs->gpr[r1] |= 0xFFFF0000;

        break;

    case 0x49:
    /*---------------------------------------------------------------*/
    /* CH       Compare Halfword                                [RX] */
    /*---------------------------------------------------------------*/

        /* Load rightmost 2 bytes of comparand from operand address */
        n = vfetch2 ( effective_addr, ar1, regs );

        /* Propagate sign bit to leftmost 2 bytes of comparand */
        if ( n > 0x7FFF )
            n |= 0xFFFF0000;

        /* Compare signed operands and set condition code */
        regs->psw.cc =
                (S32)regs->gpr[r1] < (S32)n ? 1 :
                (S32)regs->gpr[r1] > (S32)n ? 2 : 0;

        break;

    case 0x4A:
    /*---------------------------------------------------------------*/
    /* AH       Add Halfword                                    [RX] */
    /*---------------------------------------------------------------*/

        /* Load 2 bytes from operand address */
        n = vfetch2 ( effective_addr, ar1, regs );

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
        {
            program_check (PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
            goto terminate;
        }

        break;

    case 0x4B:
    /*---------------------------------------------------------------*/
    /* SH       Subtract Halfword                               [RX] */
    /*---------------------------------------------------------------*/

        /* Load 2 bytes from operand address */
        n = vfetch2 ( effective_addr, ar1, regs );

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
        {
            program_check (PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
            goto terminate;
        }

        break;

    case 0x4C:
    /*---------------------------------------------------------------*/
    /* MH       Multiply Halfword                               [RX] */
    /*---------------------------------------------------------------*/

        /* Load 2 bytes from operand address */
        n = vfetch2 ( effective_addr, ar1, regs );

        /* Propagate sign bit to leftmost 2 bytes of operand */
        if ( n > 0x7FFF )
            n |= 0xFFFF0000;

        /* Multiply R1 register by n, ignore leftmost 32 bits of
           result, and place rightmost 32 bits in R1 register */
        mul_signed (&n, &(regs->gpr[r1]), regs->gpr[r1], n);

        break;

    case 0x4D:
    /*---------------------------------------------------------------*/
    /* BAS      Branch and Save                                 [RX] */
    /*---------------------------------------------------------------*/

        /* Use the second operand address as the branch address */
        newia = effective_addr;

        /* Save the link information in the R1 register */
        if ( regs->psw.amode )
            regs->gpr[r1] = 0x80000000 | regs->psw.ia;
        else
            regs->gpr[r1] = regs->psw.ia & 0x00FFFFFF;

        /* Execute the branch */
        goto setia;

        break;

    case 0x4E:
    /*---------------------------------------------------------------*/
    /* CVD      Convert to Decimal                              [RX] */
    /*---------------------------------------------------------------*/

        /* Convert R1 register to packed decimal */
        convert_to_decimal (r1, effective_addr, ar1, regs);

        break;

    case 0x4F:
    /*---------------------------------------------------------------*/
    /* CVB      Convert to Binary                               [RX] */
    /*---------------------------------------------------------------*/

        /* Convert packed decimal storage operand into R1 register */
        convert_to_binary (r1, effective_addr, ar1, regs);

        break;

    case 0x50:
    /*---------------------------------------------------------------*/
    /* ST       Store                                           [RX] */
    /*---------------------------------------------------------------*/

        /* Store register contents at operand address */
        vstore4 ( regs->gpr[r1], effective_addr, ar1, regs );

        break;

    case 0x51:
    /*---------------------------------------------------------------*/
    /* LAE      Load Address Extended                           [RX] */
    /*---------------------------------------------------------------*/

        /* Load operand address into register */
        regs->gpr[r1] = effective_addr;

        /* Load corresponding value into access register */
        if ( PRIMARY_SPACE_MODE(&(regs->psw)) )
            regs->ar[r1] = ALET_PRIMARY;
        else if ( SECONDARY_SPACE_MODE(&(regs->psw)) )
            regs->ar[r1] = ALET_SECONDARY;
        else if ( HOME_SPACE_MODE(&(regs->psw)) )
            regs->ar[r1] = ALET_HOME;
        else /* ACCESS_REGISTER_MODE(&(regs->psw)) */
            regs->ar[r1] = (b1 == 0)? 0 : regs->ar[b1];

        break;

    case 0x54:
    /*---------------------------------------------------------------*/
    /* N        And                                             [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, ar1, regs );

        /* AND second operand with first and set condition code */
        regs->psw.cc = ( regs->gpr[r1] &= n ) ? 1 : 0;

        break;

    case 0x55:
    /*---------------------------------------------------------------*/
    /* CL       Compare Logical                                 [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, ar1, regs );

        /* Compare unsigned operands and set condition code */
        regs->psw.cc = regs->gpr[r1] < n ? 1 :
                         regs->gpr[r1] > n ? 2 : 0;

        break;

    case 0x56:
    /*---------------------------------------------------------------*/
    /* O        Or                                              [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, ar1, regs );

        /* OR second operand with first and set condition code */
        regs->psw.cc = ( regs->gpr[r1] |= n ) ? 1 : 0;

        break;

    case 0x57:
    /*---------------------------------------------------------------*/
    /* X        Exclusive Or                                    [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, ar1, regs );

        /* XOR second operand with first and set condition code */
        regs->psw.cc = ( regs->gpr[r1] ^= n ) ? 1 : 0;

        break;

    case 0x58:
    /*---------------------------------------------------------------*/
    /* L        Load                                            [RX] */
    /*---------------------------------------------------------------*/

        /* Load R1 register from second operand */
        regs->gpr[r1] = vfetch4 ( effective_addr, ar1, regs );

        break;

    case 0x59:
    /*---------------------------------------------------------------*/
    /* C        Compare                                         [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, ar1, regs );

        /* Compare signed operands and set condition code */
        regs->psw.cc =
                (S32)regs->gpr[r1] < (S32)n ? 1 :
                (S32)regs->gpr[r1] > (S32)n ? 2 : 0;

        break;

    case 0x5A:
    /*---------------------------------------------------------------*/
    /* A        Add                                             [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, ar1, regs );

        /* Add signed operands and set condition code */
        regs->psw.cc =
                add_signed (&(regs->gpr[r1]),
                        regs->gpr[r1],
                        n);

        /* Program check if fixed-point overflow */
        if ( regs->psw.cc == 3 && regs->psw.fomask )
        {
            program_check (PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
            goto terminate;
        }

        break;

    case 0x5B:
    /*---------------------------------------------------------------*/
    /* S        Subtract                                        [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, ar1, regs );

        /* Subtract signed operands and set condition code */
        regs->psw.cc =
                sub_signed (&(regs->gpr[r1]),
                        regs->gpr[r1],
                        n);

        /* Program check if fixed-point overflow */
        if ( regs->psw.cc == 3 && regs->psw.fomask )
        {
            program_check (PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
            goto terminate;
        }

        break;

    case 0x5C:
    /*---------------------------------------------------------------*/
    /* M        Multiply                                        [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is odd */
        if ( r1 & 1 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, ar1, regs );

        /* Multiply r1+1 by n and place result in r1 and r1+1 */
        mul_signed (&(regs->gpr[r1]), &(regs->gpr[r1+1]),
                        regs->gpr[r1+1],
                        n);

        break;

    case 0x5D:
    /*---------------------------------------------------------------*/
    /* D        Divide                                          [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is odd */
        if ( r1 & 1 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, ar1, regs );

        /* Divide r1::r1+1 by n, remainder in r1, quotient in r1+1 */
        divide_overflow =
            div_signed (&(regs->gpr[r1]), &(regs->gpr[r1+1]),
                        regs->gpr[r1],
                        regs->gpr[r1+1],
                        n);

        /* Program check if overflow */
        if ( divide_overflow )
        {
            program_check (PGM_FIXED_POINT_DIVIDE_EXCEPTION);
            goto terminate;
        }

        break;

    case 0x5E:
    /*---------------------------------------------------------------*/
    /* AL       Add Logical                                     [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, ar1, regs );

        /* Add signed operands and set condition code */
        regs->psw.cc =
                add_logical (&(regs->gpr[r1]),
                        regs->gpr[r1],
                        n);

        break;

    case 0x5F:
    /*---------------------------------------------------------------*/
    /* SL       Subtract Logical                                [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, ar1, regs );

        /* Subtract unsigned operands and set condition code */
        regs->psw.cc =
                sub_logical (&(regs->gpr[r1]),
                        regs->gpr[r1],
                        n);

        break;

    case 0x60:
    /*---------------------------------------------------------------*/
    /* STD      Store Floating Point Long                       [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 1 || r1 > 6 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Store register contents at operand address */
        dreg = ((U64)regs->fpr[r1] << 32) | regs->fpr[r1+1];
        vstore8 ( dreg, effective_addr, ar1, regs );

        break;

    case 0x68:
    /*---------------------------------------------------------------*/
    /* LD       Load Floating Point Long                        [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 1 || r1 > 6 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Fetch value from operand address */
        dreg = vfetch8 ( effective_addr, ar1, regs );

        /* Update register contents */
        regs->fpr[r1] = dreg >> 32;
        regs->fpr[r1+1] = dreg & 0xFFFFFFFF;

        break;

    case 0x70:
    /*---------------------------------------------------------------*/
    /* STE      Store Floating Point Short                      [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 1 || r1 > 6 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Store register contents at operand address */
        vstore4 ( regs->fpr[r1], effective_addr, ar1, regs );

        break;

    case 0x71:
    /*---------------------------------------------------------------*/
    /* MS       Multiply Single                                 [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, ar1, regs );

        /* Multiply signed operands ignoring overflow */
        (S32)regs->gpr[r1] *= (S32)n;

        break;

    case 0x78:
    /*---------------------------------------------------------------*/
    /* LE       Load Floating Point Short                       [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 1 || r1 > 6 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Update first 32 bits of register from operand address */
        regs->fpr[r1] = vfetch4 ( effective_addr, ar1, regs );

        break;

    case 0x80:
    /*---------------------------------------------------------------*/
    /* SSM      Set System Mask                                  [S] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Special operation exception if SSM-suppression is active */
        if ( regs->cr[0] & CR0_SSM_SUPP )
        {
            program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Load new system mask value from operand address */
        regs->psw.sysmask = vfetchb ( effective_addr, ar1, regs );

        /* For ECMODE, bits 0 and 2-4 of system mask must be zero */
        if (regs->psw.ecmode && (regs->psw.sysmask & 0xB8) != 0)
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        break;

    case 0x82:
    /*---------------------------------------------------------------*/
    /* LPSW     Load Program Status Word                         [S] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Program check if operand is not on a doubleword boundary */
        if ( effective_addr & 0x00000007 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Perform serialization and checkpoint synchronization */
        perform_serialization ();
        perform_chkpt_sync ();

        /* Fetch new PSW from operand address */
        vfetchc ( dword, 7, effective_addr, ar1, regs );

        /* Load updated PSW */
        rc = load_psw ( &(regs->psw), dword );
        if ( rc )
        {
            program_check (rc);
            goto terminate;
        }

        /* Perform serialization and checkpoint synchronization */
        perform_serialization ();
        perform_chkpt_sync ();

        break;

    case 0x83:
    /*---------------------------------------------------------------*/
    /* --       Diagnose                                             */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        switch(effective_addr) {

        case 0x044:
        /*-----------------------------------------------------------*/
        /* Diagnose 044: SCPEND                                      */
        /*-----------------------------------------------------------*/

            scpend_call();

            break;

#ifdef FEATURE_MSSF_CALL
        case 0x080:
        /*-----------------------------------------------------------*/
        /* Diagnose 080: MSSFCALL                                    */
        /*-----------------------------------------------------------*/

            /* R1 contains the real address of the SPCCB */
            n1 = APPLY_PREFIXING ( regs->gpr[r1], regs->pxr );

            /* R3 contains the service-processor-command word */
            n2 = regs->gpr[r3];

            /* Call MSSF and set condition code */
            regs->psw.cc = mssf_call (n1, n2);

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            break;
#endif /*FEATURE_MSSF_CALL*/

        default:
        /*-----------------------------------------------------------*/
        /* Diagnose xxx: Invalid function code                       */
        /*-----------------------------------------------------------*/
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;

        } /* end switch(effective_addr) */

        break;

#ifdef FEATURE_RELATIVE_BRANCH
    case 0x84:
    /*---------------------------------------------------------------*/
    /* BRXH     Branch Relative on Index High                  [RSI] */
    /*---------------------------------------------------------------*/

        /* Load immediate operand from instruction bytes 2-3 */
        h1 = (inst[2] << 8) | inst[3];

        /* Calculate the relative branch address */
        newia = regs->psw.ia - ilc + 2*(S16)h1;

        /* Load the increment value from the R3 register */
        i = (S32)regs->gpr[r3];

        /* Load compare value from R3 (if R3 odd), or R3+1 (if even) */
        d = (r3 & 1) ? (S32)regs->gpr[r3] : (S32)regs->gpr[r3+1];

        /* Add the increment value to the R1 register */
        (S32)regs->gpr[r1] += i;

        /* Branch if result compares high */
        if ( (S32)regs->gpr[r1] > d )
            goto setia;

        break;

    case 0x85:
    /*---------------------------------------------------------------*/
    /* BRXLE    Branch Relative on Index Low or Equal          [RSI] */
    /*---------------------------------------------------------------*/

        /* Load immediate operand from instruction bytes 2-3 */
        h1 = (inst[2] << 8) | inst[3];

        /* Calculate the relative branch address */
        newia = regs->psw.ia - ilc + 2*(S16)h1;

        /* Load the increment value from the R3 register */
        i = (S32)regs->gpr[r3];

        /* Load compare value from R3 (if R3 odd), or R3+1 (if even) */
        d = (r3 & 1) ? (S32)regs->gpr[r3] : (S32)regs->gpr[r3+1];

        /* Add the increment value to the R1 register */
        (S32)regs->gpr[r1] += i;

        /* Branch if result compares low or equal */
        if ( (S32)regs->gpr[r1] <= d )
            goto setia;

        break;
#endif /*FEATURE_RELATIVE_BRANCH*/

    case 0x86:
    /*---------------------------------------------------------------*/
    /* BXH      Branch on Index High                            [RS] */
    /*---------------------------------------------------------------*/

        /* Compute the branch address from the second operand */
        newia = effective_addr;

        /* Load the increment value from the R3 register */
        i = (S32)regs->gpr[r3];

        /* Load compare value from R3 (if R3 odd), or R3+1 (if even) */
        d = (r3 & 1) ? (S32)regs->gpr[r3] : (S32)regs->gpr[r3+1];

        /* Add the increment value to the R1 register */
        (S32)regs->gpr[r1] += i;

        /* Branch if result compares high */
        if ( (S32)regs->gpr[r1] > d )
            goto setia;

        break;

    case 0x87:
    /*---------------------------------------------------------------*/
    /* BXLE     Branch on Index Low or Equal                    [RS] */
    /*---------------------------------------------------------------*/

        /* Compute the branch address from the second operand */
        newia = effective_addr;

        /* Load the increment value from the R3 register */
        i = regs->gpr[r3];

        /* Load compare value from R3 (if R3 odd), or R3+1 (if even) */
        d = (r3 & 1) ? (S32)regs->gpr[r3] : (S32)regs->gpr[r3+1];

        /* Add the increment value to the R1 register */
        (S32)regs->gpr[r1] += i;

        /* Branch if result compares low or equal */
        if ( (S32)regs->gpr[r1] <= d )
            goto setia;

        break;

    case 0x88:
    /*---------------------------------------------------------------*/
    /* SRL      Shift Right Logical                             [RS] */
    /*---------------------------------------------------------------*/

        /* Use rightmost six bits of operand address as shift count */
        n = effective_addr & 0x3F;

        /* Shift the R1 register */
        regs->gpr[r1] >>= n;

        break;

    case 0x89:
    /*---------------------------------------------------------------*/
    /* SLL      Shift Left Logical                              [RS] */
    /*---------------------------------------------------------------*/

        /* Use rightmost six bits of operand address as shift count */
        n = effective_addr & 0x3F;

        /* Shift the R1 register */
        regs->gpr[r1] <<= n;

        break;

    case 0x8A:
    /*---------------------------------------------------------------*/
    /* SRA      Shift Right Arithmetic                          [RS] */
    /*---------------------------------------------------------------*/

        /* Use rightmost six bits of operand address as shift count */
        n = effective_addr & 0x3F;

        /* Shift the signed value of the R1 register */
        (S32)regs->gpr[r1] >>= n;

        break;

    case 0x8B:
    /*---------------------------------------------------------------*/
    /* SLA      Shift Left Arithmetic                           [RS] */
    /*---------------------------------------------------------------*/

        /* Use rightmost six bits of operand address as shift count */
        n = effective_addr & 0x3F;

        /* Shift the signed value of the R1 register */
        (S32)regs->gpr[r1] <<= n;

        break;

    case 0x8C:
    /*---------------------------------------------------------------*/
    /* SRDL     Shift Right Double Logical                      [RS] */
    /*---------------------------------------------------------------*/

        /* Specification exception if R1 is odd */
        if ( r1 & 1 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Use rightmost six bits of operand address as shift count */
        n = effective_addr & 0x3F;

        /* Shift the R1 and R1+1 registers */
        dreg = (U64)regs->gpr[r1] << 32 | regs->gpr[r1+1];
        dreg >>= n;
        regs->gpr[r1] = dreg >> 32;
        regs->gpr[r1+1] = dreg & 0xFFFFFFFF;

        break;

    case 0x8D:
    /*---------------------------------------------------------------*/
    /* SLDL     Shift Left Double Logical                       [RS] */
    /*---------------------------------------------------------------*/

        /* Specification exception if R1 is odd */
        if ( r1 & 1 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Use rightmost six bits of operand address as shift count */
        n = effective_addr & 0x3F;

        /* Shift the R1 and R1+1 registers */
        dreg = (U64)regs->gpr[r1] << 32 | regs->gpr[r1+1];
        dreg <<= n;
        regs->gpr[r1] = dreg >> 32;
        regs->gpr[r1+1] = dreg & 0xFFFFFFFF;

        break;

    case 0x8E:
    /*---------------------------------------------------------------*/
    /* SRDA     Shift Right Double Arithmetic                   [RS] */
    /*---------------------------------------------------------------*/

        /* Specification exception if R1 is odd */
        if ( r1 & 1 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Use rightmost six bits of operand address as shift count */
        n = effective_addr & 0x3F;

        /* Shift the R1 and R1+1 registers */
        dreg = (U64)regs->gpr[r1] << 32 | regs->gpr[r1+1];
        dreg = (U64)((S64)dreg >> n);
        regs->gpr[r1] = dreg >> 32;
        regs->gpr[r1+1] = dreg & 0xFFFFFFFF;

        break;

    case 0x8F:
    /*---------------------------------------------------------------*/
    /* SLDA     Shift Left Double Arithmetic                    [RS] */
    /*---------------------------------------------------------------*/

        /* Specification exception if R1 is odd */
        if ( r1 & 1 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Use rightmost six bits of operand address as shift count */
        n = effective_addr & 0x3F;

        /* Shift the R1 and R1+1 registers */
        dreg = (U64)regs->gpr[r1] << 32 | regs->gpr[r1+1];
        dreg = (U64)((S64)dreg << n);
        regs->gpr[r1] = dreg >> 32;
        regs->gpr[r1+1] = dreg & 0xFFFFFFFF;

        break;

    case 0x90:
    /*---------------------------------------------------------------*/
    /* STM      Store Multiple                                  [RS] */
    /*---------------------------------------------------------------*/

        for ( n = r1; ; )
        {
            /* Store register contents at operand address */
            vstore4 ( regs->gpr[n], effective_addr, ar1, regs );

            /* Instruction is complete when r3 register is done */
            if ( n == r3 ) break;

            /* Update register number, wrapping from 15 to 0 */
            n++; n &= 15;

            /* Update effective address to next fullword */
            effective_addr += 4;
        }

        break;

    case 0x91:
    /*---------------------------------------------------------------*/
    /* TM       Test under Mask                                 [SI] */
    /*---------------------------------------------------------------*/

        /* Fetch byte from operand address */
        obyte = vfetchb ( effective_addr, ar1, regs );

        /* AND with immediate operand mask */
        obyte &= ibyte;

        /* Set condition code according to result */
        regs->psw.cc =
                ( obyte == 0 ) ? 0 :            /* result all zeroes */
                ((obyte^ibyte) == 0) ? 3 :      /* result all ones   */
                1 ;                             /* result mixed      */

        break;

    case 0x92:
    /*---------------------------------------------------------------*/
    /* MVI      Move Immediate                                  [SI] */
    /*---------------------------------------------------------------*/

        /* Store immediate operand at operand address */
        vstoreb ( ibyte, effective_addr, ar1, regs );

        break;

    case 0x93:
    /*---------------------------------------------------------------*/
    /* TS       Test and Set                                     [S] */
    /*---------------------------------------------------------------*/

        /* Perform serialization before starting operation */
        perform_serialization ();

        /* Obtain main-storage access lock */
        obtain_lock (&sysblk.mainlock);

        /* Fetch byte from operand address */
        obyte = vfetchb ( effective_addr, ar1, regs );

        /* Set all bits of operand to ones */
        vstoreb ( 0xFF, effective_addr, ar1, regs );

        /* Release main-storage access lock */
        release_lock (&sysblk.mainlock);

        /* Set condition code from leftmost bit of operand byte */
        regs->psw.cc = obyte >> 7;

        /* Perform serialization after completing operation */
        perform_serialization ();

        break;

    case 0x94:
    /*---------------------------------------------------------------*/
    /* NI       And Immediate                                   [SI] */
    /*---------------------------------------------------------------*/

        /* Fetch byte from operand address */
        obyte = vfetchb ( effective_addr, ar1, regs );

        /* AND with immediate operand and set condition code */
        regs->psw.cc = ( obyte &= ibyte ) ? 1 : 0;

        /* Store result at operand address */
        vstoreb ( obyte, effective_addr, ar1, regs );

        break;

    case 0x95:
    /*---------------------------------------------------------------*/
    /* CLI      Compare Logical Immediate                       [SI] */
    /*---------------------------------------------------------------*/

        /* Fetch byte from operand address */
        obyte = vfetchb ( effective_addr, ar1, regs );

        /* Compare with immediate operand and set condition code */
        regs->psw.cc = (obyte < ibyte) ? 1 :
                      (obyte > ibyte) ? 2 : 0;

        break;

    case 0x96:
    /*---------------------------------------------------------------*/
    /* OI       Or Immediate                                    [SI] */
    /*---------------------------------------------------------------*/

        /* Fetch byte from operand address */
        obyte = vfetchb ( effective_addr, ar1, regs );

        /* OR with immediate operand and set condition code */
        regs->psw.cc = ( obyte |= ibyte ) ? 1 : 0;

        /* Store result at operand address */
        vstoreb ( obyte, effective_addr, ar1, regs );

        break;

    case 0x97:
    /*---------------------------------------------------------------*/
    /* XI       Exclusive Or Immediate                          [SI] */
    /*---------------------------------------------------------------*/

        /* Fetch byte from operand address */
        obyte = vfetchb ( effective_addr, ar1, regs );

        /* XOR with immediate operand and set condition code */
        regs->psw.cc = ( obyte ^= ibyte ) ? 1 : 0;

        /* Store result at operand address */
        vstoreb ( obyte, effective_addr, ar1, regs );

        break;

    case 0x98:
    /*---------------------------------------------------------------*/
    /* LM       Load Multiple                                   [RS] */
    /*---------------------------------------------------------------*/

        for ( n = r1; ; )
        {
            /* Load register from operand address */
            regs->gpr[n] = vfetch4 ( effective_addr, ar1, regs );

            /* Instruction is complete when r3 register is done */
            if ( n == r3 ) break;

            /* Update register number, wrapping from 15 to 0 */
            n++; n &= 15;

            /* Update effective address to next fullword */
            effective_addr += 4;
        }

        break;

#ifdef FEATURE_TRACING
    case 0x99:
    /*---------------------------------------------------------------*/
    /* TRACE    Trace                                           [RS] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Program check if operand is not on a fullword boundary */
        if ( effective_addr & 0x00000003 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Exit if explicit tracing (control reg 12 bit 31) is off */
        if ( (regs->cr[12] & CR12_EXTRACE) == 0 )
            break;

        /* Fetch the trace operand */
        n2 = vfetch4 ( effective_addr, ar1, regs );

        /* Exit if bit zero of the trace operand is one */
        if ( (n2 & 0x80000000) )
            break;

        /* Perform serialization and checkpoint-synchronization */
        perform_serialization ();
        perform_chkpt_sync ();

        /* Obtain the trace entry address from control register 12 */
        n = regs->cr[12] & CR12_TRACEEA;

        /* Low-address protection program check if trace entry
           address is 0-511 and bit 3 of control register 0 is set */
        if ( n < 512 && (regs->cr[0] & CR0_LOW_PROT) )
        {
            program_check (PGM_PROTECTION_EXCEPTION);
            goto terminate;
        }

        /* Convert trace entry real address to absolute address */
        n = APPLY_PREFIXING (n, regs->pxr);

        /* Program check if trace entry is outside main storage */
        if ( n >= sysblk.mainsize )
        {
            program_check (PGM_ADDRESSING_EXCEPTION);
            goto terminate;
        }

        /* Program check if storing the maximum length trace
           entry (76 bytes) would overflow a 4K page boundary */
        if ( ((n + 76) & 0xFFFFF000) != (n & 0xFFFFF000) )
        {
            program_check (PGM_TRACE_TABLE_EXCEPTION);
            goto terminate;
        }

        /* Calculate the number of registers to be traced, minus 1 */
        i = ( r3 > r1 ) ? r3 + 16 - r1 : r3 - r1;

        /* Obtain the TOD clock update lock */
        obtain_lock (&sysblk.todlock);

        /* Retrieve the TOD clock value */
        dreg = sysblk.todclk;

        /* Release the TOD clock update lock */
        release_lock (&sysblk.todlock);

        /* Set the main storage change and reference bits */
        sysblk.storkeys[n >> 12] |= (STORKEY_REF | STORKEY_CHANGE);

        /* Build the explicit trace entry */
        sysblk.mainstor[n++] = (0x70 | i);
        sysblk.mainstor[n++] = 0x00;
        sysblk.mainstor[n++] = ((dreg >> 40) & 0xFF);
        sysblk.mainstor[n++] = ((dreg >> 32) & 0xFF);
        sysblk.mainstor[n++] = ((dreg >> 24) & 0xFF);
        sysblk.mainstor[n++] = ((dreg >> 16) & 0xFF);
        sysblk.mainstor[n++] = ((dreg >> 8) & 0xFF);
        sysblk.mainstor[n++] = (dreg & 0xFF);
        sysblk.mainstor[n++] = ((n2 >> 24) & 0xFF);
        sysblk.mainstor[n++] = ((n2 >> 16) & 0xFF);
        sysblk.mainstor[n++] = ((n2 >> 8) & 0xFF);
        sysblk.mainstor[n++] = (n2 & 0xFF);

        /* Store general registers r1 through r3 in the trace entry */
        for ( i = r1; ; )
        {
            sysblk.mainstor[n++] = ((regs->gpr[i] >> 24) & 0xFF);
            sysblk.mainstor[n++] = ((regs->gpr[i] >> 16) & 0xFF);
            sysblk.mainstor[n++] = ((regs->gpr[i] >> 8) & 0xFF);
            sysblk.mainstor[n++] = (regs->gpr[i] & 0xFF);

            /* Instruction is complete when r3 register is done */
            if ( i == r3 ) break;

            /* Update register number, wrapping from 15 to 0 */
            i++; i &= 15;
        }

        /* Update trace entry address in control register 12 */
        regs->cr[12] &= ~CR12_TRACEEA;
        regs->cr[12] |= n;

        /* Perform serialization and checkpoint-synchronization */
        perform_serialization ();
        perform_chkpt_sync ();

        break;
#endif /*FEATURE_TRACING*/

#ifdef FEATURE_ACCESS_REGISTERS
    case 0x9A:
    /*---------------------------------------------------------------*/
    /* LAM      Load Access Multiple                            [RS] */
    /*---------------------------------------------------------------*/

        /* Program check if operand is not on a fullword boundary */
        if ( effective_addr & 0x00000003 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        for ( n = r1; ; )
        {
            /* Load access register from operand address */
            regs->ar[n] = vfetch4 ( effective_addr, ar1, regs );

            /* Instruction is complete when r3 register is done */
            if ( n == r3 ) break;

            /* Update register number, wrapping from 15 to 0 */
            n++; n &= 15;

            /* Update effective address to next fullword */
            effective_addr += 4;
        }

        break;

    case 0x9B:
    /*---------------------------------------------------------------*/
    /* STAM     Store Access Multiple                           [RS] */
    /*---------------------------------------------------------------*/

        /* Program check if operand is not on a fullword boundary */
        if ( effective_addr & 0x00000003 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        for ( n = r1; ; )
        {
            /* Store access register contents at operand address */
            vstore4 ( regs->ar[n], effective_addr, ar1, regs );

            /* Instruction is complete when r3 register is done */
            if ( n == r3 ) break;

            /* Update register number, wrapping from 15 to 0 */
            n++; n &= 15;

            /* Update effective address to next fullword */
            effective_addr += 4;
        }

        break;
#endif /*FEATURE_ACCESS_REGISTERS*/

#ifdef FEATURE_S370_CHANNEL
    case 0x9C:
    /*---------------------------------------------------------------*/
    /* 9C00: SIO        Start I/O                                [S] */
    /* 9C01: SIOF       Start I/O Fast Release                   [S] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Locate the device block */
        dev = find_device_by_devnum (effective_addr);

        /* Set condition code 3 if device does not exist */
        if (dev == NULL)
        {
            regs->psw.cc = 3;
            break;
        }

        /* Fetch key and CCW address from the CAW at PSA+X'48' */
        psa = (PSA*)(sysblk.mainstor + regs->pxr);
        ccwkey = psa->caw[0] & 0xF0;
        ccwaddr = (psa->caw[1] << 16) | (psa->caw[2] << 8)
                        | psa->caw[3];
        ioparm = 0;

        /* Start the channel program and set the condition code */
        regs->psw.cc =
            start_io (dev, ioparm, ccwkey, 0, 0, 0, ccwaddr);

        break;

    case 0x9D:
    /*---------------------------------------------------------------*/
    /* 9D00: TIO        Test I/O                                 [S] */
    /* 9D01: CLRIO      Clear I/O                                [S] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Locate the device block */
        dev = find_device_by_devnum (effective_addr);

        /* Set condition code 3 if device does not exist */
        if (dev == NULL)
        {
            regs->psw.cc = 3;
            break;
        }

        /* Test the device and set the condition code */
        regs->psw.cc = test_io (regs, dev, ibyte);

        break;

    case 0x9F:
    /*---------------------------------------------------------------*/
    /* TCH      Test Channel                                     [S] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Test for pending interrupt and set condition code */
        regs->psw.cc = test_channel (regs, effective_addr & 0xFF00);

        break;
#endif /*FEATURE_S370_CHANNEL*/

    case 0xA7:
    /*---------------------------------------------------------------*/
    /* Register Immediate instructions (opcode A7xx)                 */
    /*---------------------------------------------------------------*/

        /* Bits 12-15 of instruction determine the operation code */
        switch (r3) {

#ifdef FEATURE_HALFWORD_IMMEDIATE
        case 0x0:
        /*-----------------------------------------------------------*/
        /* A7x0: TMH - Test under Mask High                     [RI] */
        /*-----------------------------------------------------------*/

            /* Load immediate operand from instruction bytes 2-3 */
            h1 = (inst[2] << 8) | inst[3];

            /* AND register bits 0-15 with immediate operand */
            h2 = h1 & (regs->gpr[r1] >> 16);

            /* Isolate leftmost bit of immediate operand */
            for ( h3 = 0x8000; h3 != 0 && (h3 & h1) == 0; h3 >>= 1 );

            /* Set condition code according to result */
            regs->psw.cc =
                    ( h2 == 0 ) ? 0 :           /* result all zeroes */
                    ((h2 ^ h1) == 0) ? 3 :      /* result all ones   */
                    ((h2 & h3) == 0) ? 1 :      /* leftmost bit zero */
                    2;                          /* leftmost bit one  */

            break;

        case 0x1:
        /*-----------------------------------------------------------*/
        /* A7x1: TML - Test under Mask Low                      [RI] */
        /*-----------------------------------------------------------*/

            /* Load immediate operand from instruction bytes 2-3 */
            h1 = (inst[2] << 8) | inst[3];

            /* AND register bits 16-31 with immediate operand */
            h2 = h1 & (regs->gpr[r1] & 0xFFFF);

            /* Isolate leftmost bit of immediate operand */
            for ( h3 = 0x8000; h3 != 0 && (h3 & h1) == 0; h3 >>= 1 );

            /* Set condition code according to result */
            regs->psw.cc =
                    ( h2 == 0 ) ? 0 :           /* result all zeroes */
                    ((h2 ^ h1) == 0) ? 3 :      /* result all ones   */
                    ((h2 & h3) == 0) ? 1 :      /* leftmost bit zero */
                    2;                          /* leftmost bit one  */

            break;
#endif /*FEATURE_HALFWORD_IMMEDIATE*/

#ifdef FEATURE_RELATIVE_BRANCH
        case 0x4:
        /*-----------------------------------------------------------*/
        /* A7x4: BRC - Branch Relative on Condition             [RI] */
        /*-----------------------------------------------------------*/

            /* Load immediate operand from instruction bytes 2-3 */
            h1 = (inst[2] << 8) | inst[3];

            /* Calculate the relative branch address */
            newia = regs->psw.ia - ilc + 2*(S16)h1;

            /* Generate a bit mask from the condition code value */
            m = ( regs->psw.cc == 0 ) ? 0x08
              : ( regs->psw.cc == 1 ) ? 0x04
              : ( regs->psw.cc == 2 ) ? 0x02 : 0x01;

            /* Branch if R1 mask bit is set */
            if ( (r1 & m) != 0 )
                goto setia;

            break;

        case 0x5:
        /*-----------------------------------------------------------*/
        /* A7x5: BRAS - Branch Relative And Save                [RI] */
        /*-----------------------------------------------------------*/

            /* Load immediate operand from instruction bytes 2-3 */
            h1 = (inst[2] << 8) | inst[3];

            /* Save the link information in the R1 operand */
            if ( regs->psw.amode )
                regs->gpr[r1] = 0x80000000 | regs->psw.ia;
            else
                regs->gpr[r1] = regs->psw.ia & 0x00FFFFFF;

            /* Calculate the relative branch address */
            newia = regs->psw.ia - ilc + 2*(S16)h1;

            goto setia;

        case 0x6:
        /*-----------------------------------------------------------*/
        /* A7x6: BRCT - Branch Relative on Count                [RI] */
        /*-----------------------------------------------------------*/

            /* Load immediate operand from instruction bytes 2-3 */
            h1 = (inst[2] << 8) | inst[3];

            /* Calculate the relative branch address */
            newia = regs->psw.ia - ilc + 2*(S16)h1;

            /* Subtract 1 from the R1 operand and branch if non-zero */
            if ( --(regs->gpr[r1]) )
                goto setia;

            break;
#endif /*FEATURE_RELATIVE_BRANCH*/

#ifdef FEATURE_HALFWORD_IMMEDIATE
        case 0x8:
        /*-----------------------------------------------------------*/
        /* A7x8: LHI - Load Halfword Immediate                  [RI] */
        /*-----------------------------------------------------------*/

            /* Load immediate operand from instruction bytes 2-3 */
            h1 = (inst[2] << 8) | inst[3];

            /* Load operand into register */
            (S32)regs->gpr[r1] = (S16)h1;

            break;

        case 0xA:
        /*-----------------------------------------------------------*/
        /* A7xA: AHI - Add Halfword Immediate                   [RI] */
        /*-----------------------------------------------------------*/

            /* Load immediate operand from instruction bytes 2-3 */
            n = (inst[2] << 8) | inst[3];

            /* Propagate sign bit to form 32-bit signed operand */
            if ( n > 0x7FFF )
                n |= 0xFFFF0000;

            /* Add signed operands and set condition code */
            regs->psw.cc =
                    add_signed (&(regs->gpr[r1]),
                            regs->gpr[r1],
                            n);

            /* Program check if fixed-point overflow */
            if ( regs->psw.cc == 3 && regs->psw.fomask )
            {
                program_check (PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
                goto terminate;
            }

            break;

        case 0xC:
        /*-----------------------------------------------------------*/
        /* A7xC: MHI - Multiply Halfword Immediate              [RI] */
        /*-----------------------------------------------------------*/

            /* Load immediate operand from instruction bytes 2-3 */
            h1 = (inst[2] << 8) | inst[3];

            /* Multiply register by operand ignoring overflow  */
            (S32)regs->gpr[r1] *= (S16)h1;

            break;

        case 0xE:
        /*-----------------------------------------------------------*/
        /* A7xE: CHI - Compare Halfword Immediate               [RI] */
        /*-----------------------------------------------------------*/

            /* Load immediate operand from instruction bytes 2-3 */
            h1 = (inst[2] << 8) | inst[3];

            /* Compare signed operands and set condition code */
            regs->psw.cc =
                    (S32)regs->gpr[r1] < (S16)h1 ? 1 :
                    (S32)regs->gpr[r1] > (S16)h1 ? 2 : 0;

            break;
#endif /*FEATURE_HALFWORD_IMMEDIATE*/

        default:
        /*-----------------------------------------------------------*/
        /* A7xx: Invalid operation                                   */
        /*-----------------------------------------------------------*/
            program_check (PGM_OPERATION_EXCEPTION);
            goto terminate;

        } /* end switch(r3) */

        break;

    case 0xA8:
    /*---------------------------------------------------------------*/
    /* MVCLE    Move Long Extended                              [RS] */
    /*---------------------------------------------------------------*/

        /* Perform move long extended and set condition code */
        regs->psw.cc =
            move_long_extended (r1, r3, effective_addr, regs);

        break;

    case 0xA9:
    /*---------------------------------------------------------------*/
    /* CLCLE    Compare Logical Long Extended                   [RS] */
    /*---------------------------------------------------------------*/

        /* Perform compare long extended and set condition code */
        regs->psw.cc =
            compare_long_extended (r1, r3, effective_addr, regs);

        break;

    case 0xAC:
    /*---------------------------------------------------------------*/
    /* STNSM    Store Then And System Mask                      [SI] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Store current system mask value into storage operand */
        vstoreb ( regs->psw.sysmask, effective_addr, ar1, regs );

        /* AND system mask with immediate operand */
        regs->psw.sysmask &= ibyte;

        break;

    case 0xAD:
    /*---------------------------------------------------------------*/
    /* STOSM    Store Then Or System Mask                       [SI] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Store current system mask value into storage operand */
        vstoreb ( regs->psw.sysmask, effective_addr, ar1, regs );

        /* OR system mask with immediate operand */
        regs->psw.sysmask |= ibyte;

        /* For ECMODE, bits 0 and 2-4 of system mask must be zero */
        if (regs->psw.ecmode && (regs->psw.sysmask & 0xB8) != 0)
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        break;

    case 0xAE:
    /*---------------------------------------------------------------*/
    /* SIGP     Signal Processor                                [RS] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Load the target CPU address from R3 bits 16-31 */
        i = regs->gpr[r3] & 0xFFFF;

        /* Load the order code from operand address bits 24-31 */
        d = effective_addr & 0xFF;

        /* Load the parameter from R1 (if R1 odd), or R1+1 (if even) */
        n = (r1 & 1) ? regs->gpr[r1] : regs->gpr[r1+1];

        /*debug*/logmsg("SIGP CPU %4.4X OPERATION %2.2X PARM %8.8X\n",
                        i, d, n);

        /* Set condition code 3 if target CPU does not exist */
        if (i > 0)
        {
            regs->psw.cc = 3;
            break;
        }

        /* Perform serialization before starting operation */
        perform_serialization ();

        /* Set condition code 2 because SIGP is not implemented yet */
        regs->psw.cc = 2;

        /* Perform serialization after completing operation */
        perform_serialization ();

        break;

    case 0xAF:
    /*---------------------------------------------------------------*/
    /* MC       Monitor Call                                    [SI] */
    /*---------------------------------------------------------------*/

        /* Program check if monitor class exceeds 15 */
        if ( ibyte > 0x0F )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Ignore if monitor mask in control register 8 is zero */
        n = (regs->cr[8] & CR8_MCMASK) << ibyte;
        if ((n & 0x00008000) == 0)
            break;

        /* Clear PSA+148 and store monitor class at PSA+149 */
        psa = (PSA*)(sysblk.mainstor + regs->pxr);
        psa->monclass[0] = 0;
        psa->monclass[1] = ibyte;

        /* Store the monitor code at PSA+156 */
        psa->moncode[0] = effective_addr >> 24;
        psa->moncode[1] = (effective_addr & 0xFF0000) >> 16;
        psa->moncode[2] = (effective_addr & 0xFF00) >> 8;
        psa->moncode[3] = effective_addr & 0xFF;

        /* Generate a monitor event program interruption */
        program_check (PGM_MONITOR_EVENT);

        break;

    case 0xB1:
    /*---------------------------------------------------------------*/
    /* LRA      Load Real Address                               [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Translate the effective address to a real address */
        cc = translate_addr (effective_addr, ar1, regs, ACCTYPE_LRA,
                &n, &xcode, &private, &protect);

        /* If ALET exception, set exception code in R1 bits 16-31
           set high order bit of R1, and set condition code 3 */
        if (cc == 4) {
            regs->gpr[r1] = 0x80000000 | xcode;
            regs->psw.cc = 3;
            break;
        }

        /* Set r1 and condition code as returned by translate_addr */
        regs->gpr[r1] = n;
        regs->psw.cc = cc;

        break;

    case 0xB2:
    /*---------------------------------------------------------------*/
    /* Extended instructions (opcode B2xx)                           */
    /*---------------------------------------------------------------*/

        /* The immediate byte determines the instruction opcode */
        switch ( ibyte ) {

        case 0x02:
        /*-----------------------------------------------------------*/
        /* B202: STIDP - Store CPU ID                            [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on doubleword boundary */
            if ( effective_addr & 0x00000007 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Store CPU ID at operand address */
            vstore8 ( sysblk.cpuid, effective_addr, ar1, regs );

            break;

        case 0x05:
        /*-----------------------------------------------------------*/
        /* B205: STCK - Store Clock                              [S] */
        /*-----------------------------------------------------------*/

            /* Perform serialization before fetching clock */
            perform_serialization ();

            /* Obtain the TOD clock update lock */
            obtain_lock (&sysblk.todlock);

            /* Retrieve the TOD clock value */
            dreg = sysblk.todclk;

            /* Increment bit position 63 to ensure unique values */
            sysblk.todclk++;

            /* Release the TOD clock update lock */
            release_lock (&sysblk.todlock);

//          /*debug*/logmsg("Store TOD clock=%16.16llX\n", dreg);

            /* Store TOD clock value at operand address */
            vstore8 ( dreg, effective_addr, ar1, regs );

            /* Perform serialization after storing clock */
            perform_serialization ();

            /* Set condition code zero */
            regs->psw.cc = 0;

            break;

        case 0x06:
        /*-----------------------------------------------------------*/
        /* B206: SCKC - Set Clock Comparator                     [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on doubleword boundary */
            if ( effective_addr & 0x00000007 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Fetch clock comparator value from operand location */
            regs->clkc = vfetch8 ( effective_addr, ar1, regs )
                        & 0xFFFFFFFFFFFFF000ULL;
//          /*debug*/logmsg("Set clock comparator=%16.16llX\n",
//          /*debug*/       regs->clkc);

            break;

        case 0x07:
        /*-----------------------------------------------------------*/
        /* B207: STCKC - Store Clock Comparator                  [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on doubleword boundary */
            if ( effective_addr & 0x00000007 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Store clock comparator at operand location */
            vstore8 ( regs->clkc, effective_addr, ar1, regs );
//          /*debug*/logmsg("Store clock comparator=%16.16llX\n",
//          /*debug*/       regs->clkc);

            break;

        case 0x08:
        /*-----------------------------------------------------------*/
        /* B208: SPT - Set CPU Timer                             [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on doubleword boundary */
            if ( effective_addr & 0x00000007 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Fetch the CPU timer value from operand location */
            regs->ptimer = vfetch8 ( effective_addr, ar1, regs )
                        & 0xFFFFFFFFFFFFF000ULL;
//          /*debug*/logmsg("Set CPU timer=%16.16llX\n", regs->ptimer);

            break;

        case 0x09:
        /*-----------------------------------------------------------*/
        /* B209: STPT - Store CPU Timer                          [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on doubleword boundary */
            if ( effective_addr & 0x00000007 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Store the CPU timer at operand location */
            vstore8 ( regs->ptimer, effective_addr, ar1, regs );
//          /*debug*/logmsg("Store CPU timer=%16.16llX\n", regs->ptimer);

            break;

        case 0x0A:
        /*-----------------------------------------------------------*/
        /* B20A: SPKA - Set PSW Key from Address                 [S] */
        /*-----------------------------------------------------------*/

            /* Isolate the key from bits 24-27 of effective address */
            n = effective_addr & 0x000000F0;

            /* Privileged operation exception if in problem state
               and the corresponding PSW key mask bit is zero */
            if ( regs->psw.prob
                && ((regs->cr[3] << (n >> 4)) & 0x80000000) == 0 )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Set PSW key */
            regs->psw.pkey = n;

            break;

        case 0x0B:
        /*-----------------------------------------------------------*/
        /* B20B: IPK - Insert PSW Key                            [S] */
        /*-----------------------------------------------------------*/

            /* Privileged operation exception if in problem state
               and the extraction-authority control bit is zero */
            if ( regs->psw.prob
                 && (regs->cr[0] & CR0_EXT_AUTH) == 0 )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Insert PSW key into bits 24-27 of general register 2
               and set bits 28-31 of general register 2 to zero */
            regs->gpr[2] &= 0xFFFFFF00;
            regs->gpr[2] |= (regs->psw.pkey & 0xF0);

            break;

        case 0x0D:
        /*-----------------------------------------------------------*/
        /* B20D: PTLB - Purge TLB                              [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Purge the translation lookaside buffer for this CPU */
            purge_tlb (regs);

            break;

        case 0x10:
        /*-----------------------------------------------------------*/
        /* B210: SPX - Set Prefix                                [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on fullword boundary */
            if ( effective_addr & 0x00000003 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Perform serialization before fetching the operand */
            perform_serialization ();

            /* Load new prefix value from operand address */
            n = vfetch4 ( effective_addr, ar1, regs );

            /* Isolate bits 1-19 of new prefix value */
            n &= 0x7FFFF000;

            /* Program check if prefix is invalid absolute address */
            if ( n >= sysblk.mainsize )
            {
                program_check (PGM_ADDRESSING_EXCEPTION);
                goto terminate;
            }

            /* Load new value into prefix register */
            regs->pxr = n;

            /* Invalidate the ALB and TLB */
            purge_alb (regs);
            purge_tlb (regs);

            /* Perform serialization after completing the operation */
            perform_serialization ();

            break;

        case 0x11:
        /*-----------------------------------------------------------*/
        /* B211: STPX - Store Prefix                             [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on fullword boundary */
            if ( effective_addr & 0x00000003 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Store prefix register at operand address */
            vstore4 ( regs->pxr, effective_addr, ar1, regs );

            break;

        case 0x12:
        /*-----------------------------------------------------------*/
        /* B212: STAP - Store CPU Address                        [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on halfword boundary */
            if ( effective_addr & 0x00000001 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Store CPU address at operand address */
            vstore2 ( regs->cpuad, effective_addr, ar1, regs );

            break;

#ifdef FEATURE_BASIC_STORAGE_KEYS
        case 0x13:
        /*-----------------------------------------------------------*/
        /* B213: RRB - Reset Reference Bit                       [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load 2K block real address from operand address */
            n = effective_addr & 0x00FFF800;

            /* Convert real address to absolute address */
            n = APPLY_PREFIXING (n, regs->pxr);

            /* Addressing exception if block is outside main storage */
            if ( n >= sysblk.mainsize )
            {
                program_check (PGM_ADDRESSING_EXCEPTION);
                goto terminate;
            }

            /* Set the condition code according to the original state
               of the reference and change bits in the storage key */
            n >>= 12;
            regs->psw.cc =
               ((sysblk.storkeys[n] & STORKEY_REF) ? 2 : 0)
               | ((sysblk.storkeys[n] & STORKEY_CHANGE) ? 1 : 0);

            /* Reset the reference bit in the storage key */
            sysblk.storkeys[n] &= ~(STORKEY_REF);

            break;
#endif /*FEATURE_BASIC_STORAGE_KEYS*/

#ifdef FEATURE_DUAL_ADDRESS_SPACE
        case 0x18:
        /*-----------------------------------------------------------*/
        /* B218: PC - Program Call                               [S] */
        /*-----------------------------------------------------------*/

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Perform PC using operand address as PC number */
            rc = program_call (effective_addr, regs);

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Generate space switch event if required */
            if (rc) {
                program_check (PGM_SPACE_SWITCH_EVENT);
            }

            break;

        case 0x19:
        /*-----------------------------------------------------------*/
        /* B219: SAC - Set Address Space Control                 [S] */
        /*-----------------------------------------------------------*/

            /* Isolate bits 20-23 of effective address */
            obyte = (effective_addr & 0x00000F00) >> 8;

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Set the address-space control bits in the PSW */
            rc = set_address_space_control (obyte, regs);

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Generate a space-switch-event if indicated */
            if (rc)
                program_check (PGM_SPACE_SWITCH_EVENT);

            break;
#endif /*FEATURE_DUAL_ADDRESS_SPACE*/

        case 0x20:
        /*-----------------------------------------------------------*/
        /* B220: SERVC - Service Call                          [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* R1 is SCLP command word */
            n1 = regs->gpr[r1];

            /* R2 is real address of service call control block */
            n2 = regs->gpr[r2];
            n2 = APPLY_PREFIXING (n2, regs->pxr);

            /* Call service processor and set condition code */
            regs->psw.cc = service_call ( n1, n2 );

            break;

        case 0x21:
        /*-----------------------------------------------------------*/
        /* B221: IPTE - Invalidate Page Table Entry            [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Perform serialization before operation */
            perform_serialization ();

            /* Program check if translation format is invalid */
            if ((regs->cr[0] & CR0_TRAN_FMT) != CR0_TRAN_ESA390)
            {
                program_check (PGM_TRANSLATION_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Combine the page table origin in the R1 register with
               the page index in the R2 register, ignoring carry, to
               form the 31-bit real address of the page table entry */
            n = (regs->gpr[r1] & SEGTAB_PTO)
                + ((regs->gpr[r2] & 0x000FF000) >> 10);
            n &= 0x7FFFFFFF;

            /* Fetch the page table entry from real storage, subject
               to normal storage protection mechanisms */
            n1 = vfetch4 ( n, USE_REAL_ADDR, regs );

            /* Set the page invalid bit in the page table entry,
               again subject to storage protection mechansims */
            n1 |= PAGETAB_INVALID;
            vstore4 ( n1, n, USE_REAL_ADDR, regs );

            /* Clear the TLB of any entries with matching PFRA */
            invalidate_tlb_entry (n1, regs);

            /* Signal each CPU to perform the same invalidation.
               IPTE must not complete until all CPUs have indicated
               that they have cleared their TLB and have completed
               any storage accesses using the invalidated entries */
            /*INCOMPLETE*/ /* Not yet designed a way of doing this
            without adversely impacting TLB performance */

            /* Perform serialization after operation */
            perform_serialization ();

            break;

        case 0x22:
        /*-----------------------------------------------------------*/
        /* B222: IPM - Insert Program Mask                     [RRE] */
        /*-----------------------------------------------------------*/

            /* Insert condition code in R1 bits 2-3, program mask
               in R1 bits 4-7, and set R1 bits 0-1 to zero */
            regs->gpr[r1] &= 0x00FFFFFF;
            regs->gpr[r1] |=
                    (regs->psw.cc << 28)
                    | (regs->psw.fomask << 27)
                    | (regs->psw.domask << 26)
                    | (regs->psw.eumask << 25)
                    | (regs->psw.sgmask << 24);

            break;

        case 0x23:
        /*-----------------------------------------------------------*/
        /* B223: IVSK - Insert Virtual Storage Key             [RRE] */
        /*-----------------------------------------------------------*/

            /* Special operation exception if DAT is off */
            if ( (regs->psw.sysmask & PSW_DATMODE) == 0 )
            {
                program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Privileged operation exception if in problem state
               and the extraction-authority control bit is zero */
            if ( regs->psw.prob
                 && (regs->cr[0] & CR0_EXT_AUTH) == 0 )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load virtual storage address from R2 register */
            effective_addr = regs->gpr[r2] &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

            /* Translate virtual address to real address */
            if (translate_addr (effective_addr, r2, regs, ACCTYPE_IVSK,
                &n, &xcode, &private, &protect))
            {
                program_check (xcode);
                goto terminate;
            }

            /* Convert real address to absolute address */
            n = APPLY_PREFIXING (n, regs->pxr);

            /* Addressing exception if block is outside main storage */
            if ( n >= sysblk.mainsize )
            {
                program_check (PGM_ADDRESSING_EXCEPTION);
                goto terminate;
            }

            /* Insert the storage key into R1 register bits 24-31 */
            n >>= 12;
            regs->gpr[r1] &= 0xFFFFFF00;
            regs->gpr[r1] |= sysblk.storkeys[n];

            /* Clear bits 29-31 of R1 register */
            regs->gpr[r1] &= 0xFFFFFFF8;

            break;

#ifdef FEATURE_DUAL_ADDRESS_SPACE
        case 0x24:
        /*-----------------------------------------------------------*/
        /* B224: IAC - Insert Address Space Control            [RRE] */
        /*-----------------------------------------------------------*/

            /* Obtain the current address-space mode */
            cc = insert_address_space_control (regs);

            /* Clear bits 16-23 of the general purpose register */
            regs->gpr[r1] &= 0xFFFF00FF;

            /* Insert address-space mode into register bits 22-23 */
            regs->gpr[r1] |= cc << 8;

            /* Set condition code equal to address-space mode */
            regs->psw.cc = cc;

            break;

        case 0x25:
        /*-----------------------------------------------------------*/
        /* B225: SSAR - Set Secondary ASN                      [RRE] */
        /*-----------------------------------------------------------*/

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Load the new ASN from R1 register bits 16-31 */
            sasn = regs->gpr[r1] & CR3_SASN;

            /* Update control registers 3 and 7 with new SASN */
            set_secondary_asn (sasn, regs);

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            break;

        case 0x26:
        /*-----------------------------------------------------------*/
        /* B226: EPAR - Extract Primary ASN                    [RRE] */
        /*-----------------------------------------------------------*/

            /* Special operation exception if DAT is off */
            if ( (regs->psw.sysmask & PSW_DATMODE) == 0 )
            {
                program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Privileged operation exception if in problem state
               and the extraction-authority control bit is zero */
            if ( regs->psw.prob
                 && (regs->cr[0] & CR0_EXT_AUTH) == 0 )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load R1 with PASN from control register 4 bits 16-31 */
            regs->gpr[r1] = regs->cr[4] & CR4_PASN;

            break;

        case 0x27:
        /*-----------------------------------------------------------*/
        /* B227: ESAR - Extract Secondary ASN                  [RRE] */
        /*-----------------------------------------------------------*/

            /* Special operation exception if DAT is off */
            if ( (regs->psw.sysmask & PSW_DATMODE) == 0 )
            {
                program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Privileged operation exception if in problem state
               and the extraction-authority control bit is zero */
            if ( regs->psw.prob
                 && (regs->cr[0] & CR0_EXT_AUTH) == 0 )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load R1 with SASN from control register 3 bits 16-31 */
            regs->gpr[r1] = regs->cr[3] & CR3_SASN;

            break;

        case 0x28:
        /*-----------------------------------------------------------*/
        /* B228: PT - Program Transfer                         [RRE] */
        /*-----------------------------------------------------------*/

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Extract the PSW key mask from R1 register bits 0-15 */
            pkm = regs->gpr[r1] >> 16;

            /* Extract the ASN from R1 register bits 16-31 */
            pasn = regs->gpr[r1] & 0xFFFF;

            /* Extract the amode bit from R2 register bit 0 */
            i = (regs->gpr[r2] & 0x80000000) ? 1 : 0;

            /* Extract the instruction address from R2 bits 1-30 */
            n = regs->gpr[r2] & 0x7FFFFFFE;

            /* Extract the problem state bit from R2 register bit 31 */
            j = regs->gpr[r2] & 0x00000001;

            /* Set new PKM, PASN, SASN, PSTD, SSTD, amode,
               instruction address, and problem state bit */
            rc = program_transfer (pkm, pasn, i, n, j, regs);

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Generate space switch event if required */
            if (rc) {
                program_check (PGM_SPACE_SWITCH_EVENT);
            }

            break;

#endif /*FEATURE_DUAL_ADDRESS_SPACE*/

#ifdef FEATURE_EXTENDED_STORAGE_KEYS
        case 0x29:
        /*-----------------------------------------------------------*/
        /* B229: ISKE - Insert Storage Key Extended            [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load 4K block address from R2 register */
            n = regs->gpr[r2] &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

            /* Convert real address to absolute address */
            n = APPLY_PREFIXING (n, regs->pxr);

            /* Addressing exception if block is outside main storage */
            if ( n >= sysblk.mainsize )
            {
                program_check (PGM_ADDRESSING_EXCEPTION);
                goto terminate;
            }

            /* Insert the storage key into R1 register bits 24-31 */
            n >>= 12;
            regs->gpr[r1] &= 0xFFFFFF00;
            regs->gpr[r1] |= sysblk.storkeys[n];

            break;

        case 0x2A:
        /*-----------------------------------------------------------*/
        /* B22A: RRBE - Reset Reference Bit Extended           [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load 4K block address from R2 register */
            n = regs->gpr[r2] &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

            /* Convert real address to absolute address */
            n = APPLY_PREFIXING (n, regs->pxr);

            /* Addressing exception if block is outside main storage */
            if ( n >= sysblk.mainsize )
            {
                program_check (PGM_ADDRESSING_EXCEPTION);
                goto terminate;
            }

            /* Set the condition code according to the original state
               of the reference and change bits in the storage key */
            n >>= 12;
            regs->psw.cc =
               ((sysblk.storkeys[n] & STORKEY_REF) ? 2 : 0)
               | ((sysblk.storkeys[n] & STORKEY_CHANGE) ? 1 : 0);

            /* Reset the reference bit in the storage key */
            sysblk.storkeys[n] &= ~(STORKEY_REF);

            break;

        case 0x2B:
        /*-----------------------------------------------------------*/
        /* B22B: SSKE - Set Storage Key Extended               [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load 4K block address from R2 register */
            n = regs->gpr[r2] &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Convert real address to absolute address */
            n = APPLY_PREFIXING (n, regs->pxr);

            /* Addressing exception if block is outside main storage */
            if ( n >= sysblk.mainsize )
            {
                program_check (PGM_ADDRESSING_EXCEPTION);
                goto terminate;
            }

            /* Update the storage key from R1 register bits 24-30 */
            n >>= 12;
            sysblk.storkeys[n] = regs->gpr[r1] & 0xFE;

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            break;
#endif /*FEATURE_EXTENDED_STORAGE_KEYS*/

        case 0x2C:
        /*-----------------------------------------------------------*/
        /* B22C: TB - Test Block                               [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load 4K block address from R2 register */
            n = regs->gpr[r2] &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
            n &= 0xFFFFF000;

            /* Perform serialization */
            perform_serialization ();

            /* Convert real address to absolute address */
            n = APPLY_PREFIXING (n, regs->pxr);

            /* Addressing exception if block is outside main storage */
            if ( n >= sysblk.mainsize )
            {
                program_check (PGM_ADDRESSING_EXCEPTION);
                goto terminate;
            }

            /* Protection exception if low-address protection is set */
            if ( n == 0 && (regs->cr[0] & CR0_LOW_PROT) )
            {
                program_check (PGM_PROTECTION_EXCEPTION);
                goto terminate;
            }

            /* Clear the 4K block to zeroes */
            memset (sysblk.mainstor + n, 0x00, 4096);

            /* Set condition code 0 if storage usable, 1 if unusable */
            regs->psw.cc = 0;

            /* Perform serialization */
            perform_serialization ();

            /* Clear general register 0 */
            regs->gpr[0] = 0;

            break;

#ifdef FEATURE_CHANNEL_SUBSYSTEM
        case 0x30:
        /*-----------------------------------------------------------*/
        /* B230: CSCH - Clear Subchannel                         [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if reg 1 bits 0-15 not X'0001' */
            if ( (regs->gpr[1] >> 16) != 0x0001 )
            {
                program_check (PGM_OPERAND_EXCEPTION);
                goto terminate;
            }

            /* Locate the device block for this subchannel */
            dev = find_device_by_subchan (regs->gpr[1] & 0xFFFF);

            /* Condition code 3 if subchannel does not exist,
               is not valid, or is not enabled */
            if (dev == NULL
                || (dev->pmcw.flag5 & PMCW5_V) == 0
                || (dev->pmcw.flag5 & PMCW5_E) == 0)
            {
                regs->psw.cc = 3;
                break;
            }

            /* Perform clear subchannel and set condition code zero */
            clear_subchan (regs, dev);
            regs->psw.cc = 0;

            break;

        case 0x32:
        /*-----------------------------------------------------------*/
        /* B232: MSCH - Modify Subchannel                        [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on fullword boundary */
            if ( effective_addr & 0x00000003 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Fetch the updated path management control word */
            vfetchc ( &pmcw, sizeof(PMCW)-1, effective_addr,
                        ar1, regs );

            /* Program check if reserved bits are not zero */
            if (pmcw.flag4 & PMCW4_RESV
                || (pmcw.flag5 & PMCW5_LM) == PMCW5_LM_RESV
                || pmcw.flag24 != 0 || pmcw.flag25 != 0
                || pmcw.flag26 != 0 || (pmcw.flag27 & PMCW27_RESV))
            {
                program_check (PGM_OPERAND_EXCEPTION);
                goto terminate;
            }

            /* Program check if reg 1 bits 0-15 not X'0001' */
            if ( (regs->gpr[1] >> 16) != 0x0001 )
            {
                program_check (PGM_OPERAND_EXCEPTION);
                goto terminate;
            }

            /* Locate the device block for this subchannel */
            dev = find_device_by_subchan (regs->gpr[1] & 0xFFFF);

            /* Condition code 3 if subchannel does not exist */
            if (dev == NULL)
            {
                regs->psw.cc = 3;
                break;
            }

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Condition code 1 if subchannel is status pending */
            if (dev->scsw.flag3 & SCSW3_SC_PEND)
            {
                regs->psw.cc = 1;
                break;
            }

            /* Condition code 2 if subchannel is busy */
            if (dev->busy || dev->pending)
            {
                regs->psw.cc = 2;
                break;
            }

            /* Update the enabled (E), limit mode (LM),
               and measurement mode (MM), and multipath (D) bits */
            dev->pmcw.flag5 &=
                ~(PMCW5_E | PMCW5_LM | PMCW5_MM | PMCW5_D);
            dev->pmcw.flag5 |= (pmcw.flag5 &
                (PMCW5_E | PMCW5_LM | PMCW5_MM | PMCW5_D));

            /* Update the measurement block index */
            memcpy (dev->pmcw.mbi, pmcw.mbi, sizeof(HWORD));

            /* Update the interruption parameter */
            memcpy (dev->pmcw.intparm, pmcw.intparm, sizeof(FWORD));

            /* Update the interruption subclass (ISC) field */
            dev->pmcw.flag4 &= ~PMCW4_ISC;
            dev->pmcw.flag4 |= (pmcw.flag4 & PMCW4_ISC);

            /* Update the path management (LPM and POM) fields */
            dev->pmcw.lpm = pmcw.lpm;
            dev->pmcw.pom = pmcw.pom;

            /* Update the concurrent sense (S) field */
            dev->pmcw.flag27 &= ~PMCW27_S;
            dev->pmcw.flag27 |= (pmcw.flag27 & PMCW27_S);

            /* Set condition code 0 */
            regs->psw.cc = 0;
            break;

        case 0x33:
        /*-----------------------------------------------------------*/
        /* B233: SSCH - Start Subchannel                         [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on fullword boundary */
            if ( effective_addr & 0x00000003 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Fetch the operation request block */
            vfetchc ( &orb, sizeof(ORB)-1, effective_addr,
                        ar1, regs );

            /* Program check if reserved bits are not zero */
            if (orb.flag4 & ORB4_RESV
                || orb.flag5 & ORB5_RESV
                || orb.flag7 & ORB7_RESV
                || orb.ccwaddr[0] & 0x80)
            {
                program_check (PGM_OPERAND_EXCEPTION);
                goto terminate;
            }

            /* Program check if incorrect length suppression */
            if (orb.flag7 & ORB7_L)
            {
                program_check (PGM_OPERAND_EXCEPTION);
                goto terminate;
            }

            /* Program check if reg 1 bits 0-15 not X'0001' */
            if ( (regs->gpr[1] >> 16) != 0x0001 )
            {
                program_check (PGM_OPERAND_EXCEPTION);
                goto terminate;
            }

            /* Locate the device block for this subchannel */
            dev = find_device_by_subchan (regs->gpr[1] & 0xFFFF);

            /* Condition code 3 if subchannel does not exist,
               is not valid, or is not enabled */
            if (dev == NULL
                || (dev->pmcw.flag5 & PMCW5_V) == 0
                || (dev->pmcw.flag5 & PMCW5_E) == 0)
            {
                regs->psw.cc = 3;
                break;
            }

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Clear the path not operational mask */
            dev->pmcw.pnom = 0;

            /* Extract CCW address and I/O parameter */
            ccwaddr = (orb.ccwaddr[0] << 24) | (orb.ccwaddr[1] << 16)
                        | (orb.ccwaddr[2] << 8) | orb.ccwaddr[3];
            ioparm = (orb.intparm[0] << 24) | (orb.intparm[1] << 16)
                        | (orb.intparm[2] << 8) | orb.intparm[3];

            /* Start the channel program and set the condition code */
            regs->psw.cc =
                start_io (dev, ioparm, orb.flag4, orb.flag5,
                                orb.lpm, orb.flag7, ccwaddr);

            break;

        case 0x34:
        /*-----------------------------------------------------------*/
        /* B234: STSCH - Store Subchannel                        [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if reg 1 bits 0-15 not X'0001' */
            if ( (regs->gpr[1] >> 16) != 0x0001 )
            {
                program_check (PGM_OPERAND_EXCEPTION);
                goto terminate;
            }

            /* Locate the device block for this subchannel */
            dev = find_device_by_subchan (regs->gpr[1] & 0xFFFF);

            /* Set condition code 3 if subchannel does not exist */
            if (dev == NULL)
            {
                regs->psw.cc = 3;
                break;
            }

            /* Program check if operand not on fullword boundary */
            if ( effective_addr & 0x00000003 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Build the subchannel information block */
            schib.pmcw = dev->pmcw;
            schib.scsw = dev->scsw;
            memset (schib.moddep, 0, sizeof(schib.moddep));

            /* Store the subchannel information block */
            vstorec ( &schib, sizeof(SCHIB)-1, effective_addr,
                        ar1, regs );

            /* Set condition code 0 */
            regs->psw.cc = 0;
            break;

        case 0x35:
        /*-----------------------------------------------------------*/
        /* B235: TSCH - Test Subchannel                          [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on fullword boundary */
            if ( effective_addr & 0x00000003 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if reg 1 bits 0-15 not X'0001' */
            if ( (regs->gpr[1] >> 16) != 0x0001 )
            {
                program_check (PGM_OPERAND_EXCEPTION);
                goto terminate;
            }

            /* Locate the device block for this subchannel */
            dev = find_device_by_subchan (regs->gpr[1] & 0xFFFF);

            /* Condition code 3 if subchannel does not exist,
               is not valid, or is not enabled */
            if (dev == NULL
                || (dev->pmcw.flag5 & PMCW5_V) == 0
                || (dev->pmcw.flag5 & PMCW5_E) == 0)
            {
                regs->psw.cc = 3;
                break;
            }

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Test and clear pending status, set condition code */
            regs->psw.cc = test_subchan (regs, dev, &irb);

            /* Store the interruption response block */
            vstorec ( &irb, sizeof(IRB)-1, effective_addr, ar1, regs );

            break;

        case 0x36:
        /*-----------------------------------------------------------*/
        /* B236: TPI - Test Pending Interruption                 [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on fullword boundary */
            if ( effective_addr & 0x00000003 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Test and clear pending interrupt, set condition code */
            regs->psw.cc =
                present_io_interrupt (regs, &ioid, &ioparm, NULL);

            /* Store the SSID word and I/O parameter */
            if ( effective_addr == 0 )
            {
                /* If operand address is zero, store in PSA */
                psa = (PSA*)(sysblk.mainstor + regs->pxr);
                psa->ioid[0] = ioid >> 24;
                psa->ioid[1] = (ioid & 0xFF0000) >> 16;
                psa->ioid[2] = (ioid & 0xFF00) >> 8;
                psa->ioid[3] = ioid & 0xFF;
                psa->ioparm[0] = ioparm >> 24;
                psa->ioparm[1] = (ioparm & 0xFF0000) >> 16;
                psa->ioparm[2] = (ioparm & 0xFF00) >> 8;
                psa->ioparm[3] = ioparm & 0xFF;
            }
            else
            {
                /* Otherwise store at operand location */
                dreg = ((U64)ioid << 32) | ioparm;
                vstore8 ( dreg, effective_addr, ar1, regs );
            }

            break;

        case 0x38:
        /*-----------------------------------------------------------*/
        /* B238: RSCH - Resume Subchannel                        [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if reg 1 bits 0-15 not X'0001' */
            if ( (regs->gpr[1] >> 16) != 0x0001 )
            {
                program_check (PGM_OPERAND_EXCEPTION);
                goto terminate;
            }

            /* Locate the device block for this subchannel */
            dev = find_device_by_subchan (regs->gpr[1] & 0xFFFF);

            /* Condition code 3 if subchannel does not exist,
               is not valid, or is not enabled */
            if (dev == NULL
                || (dev->pmcw.flag5 & PMCW5_V) == 0
                || (dev->pmcw.flag5 & PMCW5_E) == 0)
            {
                regs->psw.cc = 3;
                break;
            }

            /* Perform resume subchannel and set condition code */
            regs->psw.cc = resume_subchan (regs, dev);

            break;
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

#ifdef FEATURE_LINKAGE_STACK
        case 0x40:
        /*-----------------------------------------------------------*/
        /* B240: BAKR - Branch and Stack Register              [RRE] */
        /*-----------------------------------------------------------*/

            /* Obtain the return address and addressing mode from
               the R1 register, or use updated PSW if R1 is zero */
            if ( r1 != 0 )
            {
                n1 = regs->gpr[r1];
                if ( (n1 & 0x80000000) == 0 )
                    n1 &= 0x00FFFFFF;
            }
            else
            {
                n1 = regs->psw.ia;
                if ( regs->psw.amode )
                    n1 |= 0x80000000;
            }

            /* Obtain the branch address from the R2 register, or use
               the updated PSW instruction address if R2 is zero */
            n2 = (r2 != 0) ? regs->gpr[r2] : regs->psw.ia;

            /* Set the addressing mode bit in the branch address */
            if ( regs->psw.amode )
                n2 |= 0x80000000;
            else
                n2 &= 0x00FFFFFF;

            /* Form the linkage stack entry */
            form_stack_entry (LSED_UET_BAKR, n1, n2, regs);

            /* Execute the branch unless R2 specifies register 0 */
            if ( r2 != 0 )
            {
                newia = regs->gpr[r2];
                goto setia;
            } /* end if(r2!=0) */

            break;
#endif /*FEATURE_LINKAGE_STACK*/

        case 0x41:
        /*-----------------------------------------------------------*/
        /* B241: CKSM - Checksum                               [RRE] */
        /*-----------------------------------------------------------*/

            /* Compute checksum and set condition code */
            regs->psw.cc =
                compute_checksum (r1, r2, regs);

            break;

        case 0x46:
        /*-----------------------------------------------------------*/
        /* B246: STURA - Store Using Real Address              [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* R2 register contains operand real storage address */
            n = regs->gpr[r2] &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

            /* Program check if operand not on fullword boundary */
            if ( n & 0x00000003 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Store R1 register at second operand location */
            vstore4 (regs->gpr[r1], n, USE_REAL_ADDR, regs );

            break;

#ifdef FEATURE_LINKAGE_STACK
        case 0x47:
        /*-----------------------------------------------------------*/
        /* B247: MSTA - Modify Stacked State                   [RRE] */
        /*-----------------------------------------------------------*/

            /* Modify the current linkage stack entry */
            modify_stacked_state (r1, regs);

            break;
#endif /*FEATURE_LINKAGE_STACK*/

#ifdef FEATURE_ACCESS_REGISTERS
        case 0x48:
        /*-----------------------------------------------------------*/
        /* B248: PALB - Purge ALB                              [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Purge the ART lookaside buffer for this CPU */
            purge_alb (regs);

            break;
#endif /*FEATURE_ACCESS_REGISTERS*/

#ifdef FEATURE_LINKAGE_STACK
        case 0x49:
        /*-----------------------------------------------------------*/
        /* B249: EREG - Extract Stacked Registers              [RRE] */
        /*-----------------------------------------------------------*/

            /* Extract registers from current linkage stack entry */
            extract_stacked_registers (r1, r2, regs);

            break;

        case 0x4A:
        /*-----------------------------------------------------------*/
        /* B24A: ESTA - Extract Stacked State                  [RRE] */
        /*-----------------------------------------------------------*/

            /* Load the extraction code from R2 register bits 24-31 */
            obyte = regs->gpr[r2] & 0xFF;

            /* Extract state from current linkage stack entry
               and set condition code */
            regs->psw.cc =
                extract_stacked_state (r1, obyte, regs);

            break;
#endif /*FEATURE_LINKAGE_STACK*/

        case 0x4B:
        /*-----------------------------------------------------------*/
        /* B24B: LURA - Load Using Real Address                [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* R2 register contains operand real storage address */
            n = regs->gpr[r2] &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

            /* Program check if operand not on fullword boundary */
            if ( n & 0x00000003 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Load R1 register from second operand */
            regs->gpr[r1] = vfetch4 ( n, USE_REAL_ADDR, regs );

            break;

#ifdef FEATURE_ACCESS_REGISTERS
        case 0x4C:
        /*-----------------------------------------------------------*/
        /* B24C: TAR - Test Access                             [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if ASF control bit is zero */
            if ((regs->cr[0] & CR0_ASF) == 0)
            {
                program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Set condition code 0 if ALET value is 0 */
            if (regs->ar[r1] == ALET_PRIMARY)
            {
                regs->psw.cc = 0;
                break;
            }

            /* Set condition code 3 if ALET value is 1 */
            if (regs->ar[r1] == ALET_SECONDARY)
            {
                regs->psw.cc = 3;
                break;
            }

            /* Perform ALET translation using EAX value from register
               R2 bits 0-15, and set condition code 3 if exception */
            if (translate_alet (regs->ar[r1], (regs->gpr[r2] >> 16),
                                ACCTYPE_TAR, regs,
                                &asteo, aste, &protect));
            {
                regs->psw.cc = 3;
                break;
            }

            /* Set condition code 1 or 2 according to whether
               the ALET designates the DUCT or the PASTE */
            regs->psw.cc = (regs->ar[r1] & ALET_PRI_LIST) ? 2 : 1;

            break;

        case 0x4D:
        /*-----------------------------------------------------------*/
        /* B24D: CPYA - Copy Access Register                   [RRE] */
        /*-----------------------------------------------------------*/

            /* Copy R2 access register to R1 access register */
            regs->ar[r1] = regs->ar[r2];

            break;

        case 0x4E:
        /*-----------------------------------------------------------*/
        /* B24E: SAR - Set Access Register                     [RRE] */
        /*-----------------------------------------------------------*/

            /* Copy R2 general register to R1 access register */
            regs->ar[r1] = regs->gpr[r2];

            break;

        case 0x4F:
        /*-----------------------------------------------------------*/
        /* B24F: EAR - Extract Access Register                 [RRE] */
        /*-----------------------------------------------------------*/

            /* Copy R2 access register to R1 general register */
            regs->gpr[r1] = regs->ar[r2];

            break;
#endif /*FEATURE_ACCESS_REGISTERS*/

        case 0x52:
        /*-----------------------------------------------------------*/
        /* B252: MSR - Multiply Single Register                [RRE] */
        /*-----------------------------------------------------------*/

            /* Multiply signed registers ignoring overflow */
            (S32)regs->gpr[r1] *= (S32)regs->gpr[r2];

            break;

        case 0x55:
        /*-----------------------------------------------------------*/
        /* B255: MVST - Move String                            [RRE] */
        /*-----------------------------------------------------------*/

            /* Perform move string and set condition code */
            regs->psw.cc = move_string (r1, r2, regs);

            break;

        case 0x57:
        /*-----------------------------------------------------------*/
        /* B257: CUSE - Compare Until Substring Equal          [RRE] */
        /*-----------------------------------------------------------*/

            /* Perform compare substring and set condition code */
            regs->psw.cc =
                compare_until_substring_equal (r1, r2, regs);

            break;

#ifdef FEATURE_SUBSPACE_GROUP
        case 0x58:
        /*-----------------------------------------------------------*/
        /* B258: BSG - Branch in Subspace Group                [RRE] */
        /*-----------------------------------------------------------*/

            /* Perform branch in subspace group */
            branch_in_subspace_group (r1, r2, regs);

            break;
#endif /*FEATURE_SUBSPACE_GROUP*/

#ifdef FEATURE_BRANCH_AND_SET_AUTHORITY
        case 0x5A:
        /*-----------------------------------------------------------*/
        /* B25A: BSA - Branch and Set Authority                [RRE] */
        /*-----------------------------------------------------------*/

            /* Enter or leave the reduced authority state */
            branch_and_set_authority (r1, r2, regs);

            break;
#endif /*FEATURE_BRANCH_AND_SET_AUTHORITY*/

        case 0x5D:
        /*-----------------------------------------------------------*/
        /* B25D: CLST - Compare Logical String                 [RRE] */
        /*-----------------------------------------------------------*/

            /* Perform compare string and set condition code */
            regs->psw.cc = compare_string (r1, r2, regs);

            break;

        case 0x5E:
        /*-----------------------------------------------------------*/
        /* B25E: SRST - Search String                          [RRE] */
        /*-----------------------------------------------------------*/

            /* Perform search string and set condition code */
            regs->psw.cc = search_string (r1, r2, regs);

            break;

#ifdef FEATURE_DUAL_ADDRESS_SPACE
        case 0x79:
        /*-----------------------------------------------------------*/
        /* B279: SACF - Set Address Space Control Fast           [S] */
        /*-----------------------------------------------------------*/

            /* Isolate bits 20-23 of effective address */
            obyte = (effective_addr & 0x00000F00) >> 8;

            /* Set the address-space control bits in the PSW */
            rc = set_address_space_control (obyte, regs);

            /* Generate a space-switch-event if indicated */
            if (rc)
                program_check (PGM_SPACE_SWITCH_EVENT);

            break;
#endif /*FEATURE_DUAL_ADDRESS_SPACE*/

        default:
        /*-----------------------------------------------------------*/
        /* B2xx: Invalid operation                                   */
        /*-----------------------------------------------------------*/
            program_check (PGM_OPERATION_EXCEPTION);
            goto terminate;

        } /* end switch(ibyte) */
        break;

    case 0xB6:
    /*---------------------------------------------------------------*/
    /* STCTL    Store Control                                   [RS] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Program check if operand is not on a fullword boundary */
        if ( effective_addr & 0x00000003 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        for ( n = r1; ; )
        {
            /* Store control register contents at operand address */
            vstore4 ( regs->cr[n], effective_addr, ar1, regs );

            /* Instruction is complete when r3 register is done */
            if ( n == r3 ) break;

            /* Update register number, wrapping from 15 to 0 */
            n++; n &= 15;

            /* Update effective address to next fullword */
            effective_addr += 4;
        }

        break;

    case 0xB7:
    /*---------------------------------------------------------------*/
    /* LCTL     Load Control                                    [RS] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Program check if operand is not on a fullword boundary */
        if ( effective_addr & 0x00000003 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        for ( n = r1; ; )
        {
            /* Load control register from operand address */
            regs->cr[n] = vfetch4 ( effective_addr, ar1, regs );

            /* Instruction is complete when r3 register is done */
            if ( n == r3 ) break;

            /* Update register number, wrapping from 15 to 0 */
            n++; n &= 15;

            /* Update effective address to next fullword */
            effective_addr += 4;
        }

        break;

    case 0xBA:
    /*---------------------------------------------------------------*/
    /* CS       Compare and Swap                                [RS] */
    /*---------------------------------------------------------------*/

        /* Program check if operand is not on a fullword boundary */
        if ( effective_addr & 0x00000003 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Perform serialization before starting operation */
        perform_serialization ();

        /* Obtain main-storage access lock */
        obtain_lock (&sysblk.mainlock);

        /* Load second operand from operand address  */
        n = vfetch4 ( effective_addr, ar1, regs );

        /* Compare operand with R1 register contents */
        if ( regs->gpr[r1] == n )
        {
            /* If equal, store R3 at operand location and set cc=0 */
            vstore4 ( regs->gpr[r3], effective_addr, ar1, regs );
            regs->psw.cc = 0;
        }
        else
        {
            /* If unequal, load R1 from operand and set cc=1 */
            regs->gpr[r1] = n;
            regs->psw.cc = 1;
        }

        /* Release main-storage access lock */
        release_lock (&sysblk.mainlock);

        /* Perform serialization after completing operation */
        perform_serialization ();

        break;

    case 0xBB:
    /*---------------------------------------------------------------*/
    /* CDS      Compare Double and Swap                         [RS] */
    /*---------------------------------------------------------------*/

        /* Program check if either R1 or R3 is odd */
        if ( ( r1 & 1 ) || ( r3 & 1 ) )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Program check if operand is not on a doubleword boundary */
        if ( effective_addr & 0x00000007 )
        {
            program_check (PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Perform serialization before starting operation */
        perform_serialization ();

        /* Obtain main-storage access lock */
        obtain_lock (&sysblk.mainlock);

        /* Load second operand from operand address  */
        n = vfetch4 ( effective_addr, ar1, regs );
        d = vfetch4 ( effective_addr + 4, ar1, regs );

        /* Compare doubleword operand with R1:R1+1 register contents */
        if ( regs->gpr[r1] == n && regs->gpr[r1+1] == d )
        {
            /* If equal, store R3:R3+1 at operand location and set cc=0 */
            vstore4 ( regs->gpr[r3], effective_addr, ar1, regs );
            vstore4 ( regs->gpr[r3+1], effective_addr + 4, ar1, regs );
            regs->psw.cc = 0;
        }
        else
        {
            /* If unequal, load R1:R1+1 from operand and set cc=1 */
            regs->gpr[r1] = n;
            regs->gpr[r1+1] = d;
            regs->psw.cc = 1;
        }

        /* Release main-storage access lock */
        release_lock (&sysblk.mainlock);

        /* Perform serialization after completing operation */
        perform_serialization ();

        break;

    case 0xBD:
    /*---------------------------------------------------------------*/
    /* CLM      Compare Logical Characters under Mask           [RS] */
    /*---------------------------------------------------------------*/

        /* Clear the condition code */
        regs->psw.cc = 0;

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
                sbyte = vfetchb ( effective_addr++, ar1, regs );

                /* Compare bytes, set condition code if unequal */
                if ( dbyte != sbyte )
                {
                    regs->psw.cc = (dbyte < sbyte) ? 1 : 2;
                    break;
                } /* end if */
            }

            /* Shift mask and register for next byte */
            r3 <<= 1;
            n <<= 8;

        } /* end for(i) */

        break;

    case 0xBE:
    /*---------------------------------------------------------------*/
    /* STCM     Store Characters under Mask                     [RS] */
    /*---------------------------------------------------------------*/

        /* Load value from register */
        n = regs->gpr[r1];

        /* Store characters from register to operand address */
        for ( i = 0; i < 4; i++ )
        {
            /* Test mask bit corresponding to this character */
            if ( r3 & 0x08 )
            {
                /* Store character from register to operand */
                dbyte = n >> 24;
                vstoreb ( dbyte, effective_addr++, ar1, regs );
            }

            /* Shift mask and register for next byte */
            r3 <<= 1;
            n <<= 8;

        } /* end for(i) */

        break;

    case 0xBF:
    /*---------------------------------------------------------------*/
    /* ICM      Insert Characters under Mask                    [RS] */
    /*---------------------------------------------------------------*/

        /* Clear the condition code */
        regs->psw.cc = 0;

        /* If the mask is all zero, we must nevertheless load one
           byte from the storage operand, because POP requires us
           to recognize an access exception on the first byte */
        if ( r3 == 0 )
        {
            sbyte = vfetchb ( effective_addr, ar1, regs );
            break;
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
                sbyte = vfetchb ( effective_addr, ar1, regs );

                /* If this is the first byte fetched then test the
                   high-order bit to determine the condition code */
                if ( (r3 & 0xF0) == 0 )
                    h = (sbyte & 0x80) ? 1 : 2;

                /* If byte is non-zero then set the condition code */
                if ( sbyte != 0 )
                    regs->psw.cc = h;

                /* Insert the byte into the register */
                dreg &= 0xFFFFFFFF00FFFFFFULL;
                dreg |= (U32)sbyte << 24;

                /* Increment the operand address */
                effective_addr++;
                effective_addr &=
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
            }

            /* Shift mask and register for next byte */
            r3 <<= 1;
            dreg <<= 8;

        } /* end for(i) */

        /* Load the register with the updated value */
        regs->gpr[r1] = dreg >> 32;

        break;

    case 0xD1:
    /*---------------------------------------------------------------*/
    /* MVN      Move Numerics                                   [SS] */
    /*---------------------------------------------------------------*/

        /* Process operands from left to right */
        for ( i = 0; i < ibyte+1; i++ )
        {
            /* Fetch bytes from source and destination operands */
            sbyte = vfetchb ( effective_addr2, ar2, regs );
            dbyte = vfetchb ( effective_addr, ar1, regs );

            /* Copy low digit of source byte to destination byte */
            dbyte = (dbyte & 0xF0) | ( sbyte & 0x0F);

            /* Store result at destination operand address */
            vstoreb ( dbyte, effective_addr, ar1, regs );

            /* Increment operand addresses */
            effective_addr++;
            effective_addr &=
                    (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
            effective_addr2++;
            effective_addr2 &=
                    (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        } /* end for(i) */

        break;

    case 0xD2:
    /*---------------------------------------------------------------*/
    /* MVC      Move Characters                                 [SS] */
    /*---------------------------------------------------------------*/

        /* Move characters using current addressing mode and key */
        move_chars (effective_addr, ar1, regs->psw.pkey,
                effective_addr2, ar2, regs->psw.pkey, ibyte, regs);

        break;

    case 0xD3:
    /*---------------------------------------------------------------*/
    /* MVZ      Move Zones                                      [SS] */
    /*---------------------------------------------------------------*/

        /* Process operands from left to right */
        for ( i = 0; i < ibyte+1; i++ )
        {
            /* Fetch bytes from source and destination operands */
            sbyte = vfetchb ( effective_addr2, ar2, regs );
            dbyte = vfetchb ( effective_addr, ar1, regs );

            /* Copy high digit of source byte to destination byte */
            dbyte = (dbyte & 0x0F) | ( sbyte & 0xF0);

            /* Store result at destination operand address */
            vstoreb ( dbyte, effective_addr, ar1, regs );

            /* Increment operand addresses */
            effective_addr++;
            effective_addr &=
                    (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
            effective_addr2++;
            effective_addr2 &=
                    (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        } /* end for(i) */

        break;

    case 0xD4:
    /*---------------------------------------------------------------*/
    /* NC       And Characters                                  [SS] */
    /*---------------------------------------------------------------*/

        /* Clear the condition code */
        regs->psw.cc = 0;

        /* Process operands from left to right */
        for ( i = 0; i < ibyte+1; i++ )
        {
            /* Fetch bytes from source and destination operands */
            sbyte = vfetchb ( effective_addr2, ar2, regs );
            dbyte = vfetchb ( effective_addr, ar1, regs );

            /* AND operand bytes and set condition code */
            if ( dbyte &= sbyte ) regs->psw.cc = 1;

            /* Store result at destination operand address */
            vstoreb ( dbyte, effective_addr, ar1, regs );

            /* Increment operand addresses */
            effective_addr++;
            effective_addr &=
                    (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
            effective_addr2++;
            effective_addr2 &=
                    (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        } /* end for(i) */

        break;

    case 0xD5:
    /*---------------------------------------------------------------*/
    /* CLC      Compare Logical Characters                      [SS] */
    /*---------------------------------------------------------------*/

        /* Fetch first and second operands into work areas */
        vfetchc ( cwork1, ibyte, effective_addr, ar1, regs );
        vfetchc ( cwork2, ibyte, effective_addr2, ar2, regs );

        /* Compare first operand with second operand */
        rc = memcmp (cwork1, cwork2, ibyte+1);

        /* Set the condition code */
        regs->psw.cc = (rc == 0) ? 0 : (rc < 0) ? 1 : 2;

        break;

    case 0xD6:
    /*---------------------------------------------------------------*/
    /* OC       Or Characters                                   [SS] */
    /*---------------------------------------------------------------*/

        /* Clear the condition code */
        regs->psw.cc = 0;

        /* Process operands from left to right */
        for ( i = 0; i < ibyte+1; i++ )
        {
            /* Fetch bytes from source and destination operands */
            sbyte = vfetchb ( effective_addr2, ar2, regs );
            dbyte = vfetchb ( effective_addr, ar1, regs );

            /* OR operand bytes and set condition code */
            if ( dbyte |= sbyte ) regs->psw.cc = 1;

            /* Store result at destination operand address */
            vstoreb ( dbyte, effective_addr, ar1, regs );

            /* Increment operand addresses */
            effective_addr++;
            effective_addr &=
                    (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
            effective_addr2++;
            effective_addr2 &=
                    (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        } /* end for(i) */

        break;

    case 0xD7:
    /*---------------------------------------------------------------*/
    /* XC       Exclusive Or Characters                         [SS] */
    /*---------------------------------------------------------------*/

        /* Clear the condition code */
        regs->psw.cc = 0;

        /* Process operands from left to right */
        for ( i = 0; i < ibyte+1; i++ )
        {
            /* Fetch bytes from source and destination operands */
            sbyte = vfetchb ( effective_addr2, ar2, regs );
            dbyte = vfetchb ( effective_addr, ar1, regs );

            /* XOR operand bytes and set condition code */
            if ( dbyte ^= sbyte ) regs->psw.cc = 1;

            /* Store result at destination operand address */
            vstoreb ( dbyte, effective_addr, ar1, regs );

            /* Increment operand addresses */
            effective_addr++;
            effective_addr &=
                    (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
            effective_addr2++;
            effective_addr2 &=
                    (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        } /* end for(i) */

        break;

    case 0xD9:
    /*---------------------------------------------------------------*/
    /* MVCK     Move with Key                                   [SS] */
    /*---------------------------------------------------------------*/

        /* Load true length from R1 register */
        h = regs->gpr[r1];

        /* If the true length does not exceed 256, set condition code
           zero, otherwise set cc=3 and use effective length of 256 */
        if (h <= 256)
            cc = 0;
        else {
            cc = 3;
            h = 256;
        }

        /* Load source key from R3 register bits 24-27 */
        j = regs->gpr[r3] & 0xF0;

        /* Program check if in problem state and key mask in
           CR3 bits 0-15 is not 1 for the specified key */
        if ( regs->psw.prob
            && ((regs->cr[3] << (j >> 4)) & 0x80000000) == 0 )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Move characters using source key for second operand */
        if (h > 0)
            move_chars (effective_addr, ar1, regs->psw.pkey,
                        effective_addr2, ar2, j, h-1, regs);

        /* Set condition code */
        regs->psw.cc = cc;

        break;

    case 0xDA:
    /*---------------------------------------------------------------*/
    /* MVCP     Move to Primary                                 [SS] */
    /*---------------------------------------------------------------*/

        /* Program check if secondary space control (CR0 bit 5) is 0,
           or if DAT is off, or if in AR mode or home-space mode */
        if ((regs->cr[0] & CR0_SEC_SPACE) == 0
            || REAL_MODE(&regs->psw)
            || regs->psw.armode)
        {
            program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Load true length from R1 register */
        h = regs->gpr[r1];

        /* If the true length does not exceed 256, set condition code
           zero, otherwise set cc=3 and use effective length of 256 */
        if (h <= 256)
            cc = 0;
        else {
            cc = 3;
            h = 256;
        }

        /* Load secondary space key from R3 register bits 24-27 */
        j = regs->gpr[r3] & 0xF0;

        /* Program check if in problem state and key mask in
           CR3 bits 0-15 is not 1 for the specified key */
        if ( regs->psw.prob
            && ((regs->cr[3] << (j >> 4)) & 0x80000000) == 0 )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Move characters from secondary address space to primary
           address space using secondary key for second operand */
        if (h > 0)
            move_chars (effective_addr, USE_PRIMARY_SPACE,
                        regs->psw.pkey,
                        effective_addr2, USE_SECONDARY_SPACE,
                        j, h-1, regs);

        /* Set condition code */
        regs->psw.cc = cc;

        break;

    case 0xDB:
    /*---------------------------------------------------------------*/
    /* MVCS     Move to Secondary                               [SS] */
    /*---------------------------------------------------------------*/

        /* Program check if secondary space control (CR0 bit 5) is 0,
           or if DAT is off, or if in AR mode or home-space mode */
        if ((regs->cr[0] & CR0_SEC_SPACE) == 0
            || REAL_MODE(&regs->psw)
            || regs->psw.armode)
        {
            program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Load true length from R1 register */
        h = regs->gpr[r1];

        /* If the true length does not exceed 256, set condition code
           zero, otherwise set cc=3 and use effective length of 256 */
        if (h <= 256)
            cc = 0;
        else {
            cc = 3;
            h = 256;
        }

        /* Load secondary space key from R3 register bits 24-27 */
        j = regs->gpr[r3] & 0xF0;

        /* Program check if in problem state and key mask in
           CR3 bits 0-15 is not 1 for the specified key */
        if ( regs->psw.prob
            && ((regs->cr[3] << (j >> 4)) & 0x80000000) == 0 )
        {
            program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Move characters from primary address space to secondary
           address space using secondary key for first operand */
        if (h > 0)
            move_chars (effective_addr, USE_SECONDARY_SPACE, j,
                        effective_addr2, USE_PRIMARY_SPACE,
                        regs->psw.pkey, h-1, regs);

        /* Set condition code */
        regs->psw.cc = cc;

        break;

    case 0xDC:
    /*---------------------------------------------------------------*/
    /* TR       Translate                                       [SS] */
    /*---------------------------------------------------------------*/

        /* Process first operand from left to right */
        for ( i = 0; i < ibyte+1; i++ )
        {
            /* Fetch byte from first operand address */
            dbyte = vfetchb ( effective_addr, ar1, regs );

            /* Fetch byte from translate table */
            sbyte = vfetchb ( effective_addr2 + dbyte, ar2, regs );

            /* Store result at first operand address */
            vstoreb ( sbyte, effective_addr, ar1, regs );

            /* Increment first operand address */
            effective_addr++;
            effective_addr &=
                    (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        } /* end for(i) */

        break;

    case 0xDD:
    /*---------------------------------------------------------------*/
    /* TRT      Translate and Test                              [SS] */
    /*---------------------------------------------------------------*/

        /* Clear the condition code */
        regs->psw.cc = 0;

        /* Process first operand from left to right */
        for ( i = 0; i < ibyte+1; i++ )
        {
            /* Fetch argument byte from first operand */
            dbyte = vfetchb ( effective_addr, ar1, regs );

            /* Fetch function byte from second operand */
            sbyte = vfetchb ( effective_addr2 + dbyte, ar2, regs );

            /* Test for non-zero function byte */
            if (sbyte != 0) {

                /* Store address of argument byte in register 1 */
                if ( regs->psw.amode )
                {
                    regs->gpr[1] = effective_addr;
                } else {
                    regs->gpr[1] &= 0xFF000000;
                    regs->gpr[1] |= effective_addr;
                }

                /* Store function byte in low-order byte of reg.2 */
                regs->gpr[2] &= 0xFFFFFF00;
                regs->gpr[2] |= sbyte;

                /* Set condition code 2 if argument byte was last byte
                   of first operand, otherwise set condition code 1 */
                regs->psw.cc = (i == ibyte) ? 2 : 1;

                /* Terminate the operation at this point */
                break;

            } /* end if(sbyte) */

            /* Increment first operand address */
            effective_addr++;
            effective_addr &=
                    (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        } /* end for(i) */

        break;

    case 0xDE:
    /*---------------------------------------------------------------*/
    /* EDIT     Edit                                            [SS] */
    /*---------------------------------------------------------------*/

        /* Edit packed decimal value and set condition code */
        regs->psw.cc =
            edit_packed (0, effective_addr, ibyte, ar1,
                        effective_addr2, ar2, regs);

        break;

    case 0xDF:
    /*---------------------------------------------------------------*/
    /* EDMK     Edit and Mark                                   [SS] */
    /*---------------------------------------------------------------*/

        /* Edit and mark packed decimal value and set condition code */
        regs->psw.cc =
            edit_packed (1, effective_addr, ibyte, ar1,
                        effective_addr2, ar2, regs);

        break;

    case 0xE5:
    /*---------------------------------------------------------------*/
    /* Extended instructions (opcode E5xx)                           */
    /*---------------------------------------------------------------*/

        /* The immediate byte determines the instruction opcode */
        switch ( ibyte ) {

#ifdef FEATURE_DUAL_ADDRESS_SPACE
        case 0x00:
        /*-----------------------------------------------------------*/
        /* E500: LASP - Load Address Space Parameters          [SSE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Special operation exception if ASN translation control
               (bit 12 of control register 14) is zero */
            if ( (regs->cr[14] & CR14_ASN_TRAN) == 0 )
            {
                program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on doubleword boundary */
            if ( effective_addr & 0x00000007 )
            {
                program_check (PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Fetch PKM, SASN, AX, and PASN from first operand */
            dreg = vfetch8 ( effective_addr, ar1, regs );
            pkm = (dreg & 0xFFFF000000000000ULL) >> 48;
            sasn = (dreg & 0xFFFF00000000ULL) >> 32;
            ax = (dreg & 0xFFFF0000) >> 16;
            pasn = dreg & 0xFFFF;

            /* Load control registers and set condition code */
            regs->psw.cc =
                load_address_space_parameters (pkm, sasn, ax, pasn,
                                effective_addr2, regs);

            break;
#endif /*FEATURE_DUAL_ADDRESS_SPACE*/

        case 0x01:
        /*-----------------------------------------------------------*/
        /* E501: TPROT - Test Protection                       [SSE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load access key from operand 2 address bits 24-27 */
            dbyte = effective_addr2 & 0xF0;

            /* Test protection and set condition code */
            regs->psw.cc =
                test_prot (effective_addr, ar1, regs, dbyte);

            break;

#ifdef FEATURE_MVS_ASSIST
        case 0x04:
        /*-----------------------------------------------------------*/
        /* E504: Obtain Local Lock                             [SSE] */
        /*-----------------------------------------------------------*/

            /* Perform serialization before starting operation */
            perform_serialization ();

            /* Call MVS assist to obtain lock */
            obtain_local_lock (effective_addr, ar1,
                                effective_addr2, ar2, regs);

            /* Perform serialization after completing operation */
            perform_serialization ();

            break;

        case 0x05:
        /*-----------------------------------------------------------*/
        /* E505: Release Local Lock                            [SSE] */
        /*-----------------------------------------------------------*/

            /* Call MVS assist to release lock */
            release_local_lock (effective_addr, ar1,
                                effective_addr2, ar2, regs);

            break;

        case 0x06:
        /*-----------------------------------------------------------*/
        /* E506: Obtain CMS Lock                               [SSE] */
        /*-----------------------------------------------------------*/

            /* Perform serialization before starting operation */
            perform_serialization ();

            /* Call MVS assist to obtain lock */
            obtain_cms_lock (effective_addr, ar1,
                                effective_addr2, ar2, regs);

            /* Perform serialization after completing operation */
            perform_serialization ();

            break;

        case 0x07:
        /*-----------------------------------------------------------*/
        /* E507: Release CMS Lock                              [SSE] */
        /*-----------------------------------------------------------*/

            /* Call MVS assist to release lock */
            release_cms_lock (effective_addr, ar1,
                                effective_addr2, ar2, regs);

            break;
#endif /*FEATURE_MVS_ASSIST*/

        case 0x0E:
        /*-----------------------------------------------------------*/
        /* E50E: MVCSK - Move with Source Key                  [SSE] */
        /*-----------------------------------------------------------*/

            /* Load operand length-1 from register 0 bits 24-31 */
            h = regs->gpr[0] & 0xFF;

            /* Load source key from register 1 bits 24-27 */
            j = regs->gpr[1] & 0xF0;

            /* Program check if in problem state and key mask in
               CR3 bits 0-15 is not 1 for the specified key */
            if ( regs->psw.prob
                && ((regs->cr[3] << (j >> 4)) & 0x80000000) == 0 )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Move characters using source key for second operand */
            move_chars (effective_addr, ar1, regs->psw.pkey,
                        effective_addr2, ar2, j, h, regs);

            break;

        case 0x0F:
        /*-----------------------------------------------------------*/
        /* E50F: MVCDK - Move with Destination Key             [SSE] */
        /*-----------------------------------------------------------*/

            /* Load operand length-1 from register 0 bits 24-31 */
            h = regs->gpr[0] & 0xFF;

            /* Load destination key from register 1 bits 24-27 */
            j = regs->gpr[1] & 0xF0;

            /* Program check if in problem state and key mask in
               CR3 bits 0-15 is not 1 for the specified key */
            if ( regs->psw.prob
                && ((regs->cr[3] << (j >> 4)) & 0x80000000) == 0 )
            {
                program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Move characters using destination key for operand 1 */
            move_chars (effective_addr, ar1, j,
                        effective_addr2, ar2, regs->psw.pkey,
                        h, regs);

            break;

        default:
        /*-----------------------------------------------------------*/
        /* E5xx: Invalid operation                                   */
        /*-----------------------------------------------------------*/
            program_check (PGM_OPERATION_EXCEPTION);
            goto terminate;

        } /* end switch(ibyte) */
        break;

    case 0xE8:
    /*---------------------------------------------------------------*/
    /* MVCIN    Move Characters Inverse                         [SS] */
    /*---------------------------------------------------------------*/

        /* Process the destination operand from left to right,
           and the source operand from right to left */
        effective_addr2 += ibyte;

        for ( i = 0; i < ibyte+1; i++ )
        {
            /* Fetch a byte from the source operand */
            sbyte = vfetchb ( effective_addr2, ar2, regs );

            /* Store the byte in the destination operand */
            vstoreb ( sbyte, effective_addr, ar1, regs );

            /* Increment destination operand address */
            effective_addr++;
            effective_addr &=
                    (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

            /* Decrement source operand address */
            effective_addr2--;
            effective_addr2 &=
                    (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        } /* end for(i) */

        break;

    case 0xF0:
    /*---------------------------------------------------------------*/
    /* SRP      Shift and Round Decimal                         [SS] */
    /*---------------------------------------------------------------*/

        /* Shift packed decimal operand and set condition code */
        regs->psw.cc =
            shift_and_round_packed (effective_addr, r1, ar1, regs,
                                r2, effective_addr2 );

        break;

    case 0xF1:
    /*---------------------------------------------------------------*/
    /* MVO      Move with Offset                                [SS] */
    /*---------------------------------------------------------------*/

        /* Move shifted second operand to first operand */
        move_with_offset (effective_addr, r1, ar1,
                        effective_addr2, r2, ar2, regs);

        break;

    case 0xF2:
    /*---------------------------------------------------------------*/
    /* PACK     Pack                                            [SS] */
    /*---------------------------------------------------------------*/

        /* Pack second operand into first operand */
        zoned_to_packed (effective_addr, r1, ar1,
                        effective_addr2, r2, ar2, regs);

        break;

    case 0xF3:
    /*---------------------------------------------------------------*/
    /* UNPK     Unpack                                          [SS] */
    /*---------------------------------------------------------------*/

        /* Unpack second operand into first operand */
        packed_to_zoned (effective_addr, r1, ar1,
                        effective_addr2, r2, ar2, regs);

        break;

    case 0xF8:
    /*---------------------------------------------------------------*/
    /* ZAP      Zero and Add Decimal                            [SS] */
    /*---------------------------------------------------------------*/

        /* Copy packed decimal operand and set condition code */
        regs->psw.cc =
            zero_and_add_packed (effective_addr, r1, ar1,
                                effective_addr2, r2, ar2, regs );

        break;

    case 0xF9:
    /*---------------------------------------------------------------*/
    /* CP       Compare Decimal                                 [SS] */
    /*---------------------------------------------------------------*/

        /* Compare packed decimal operands and set condition code */
        regs->psw.cc =
            compare_packed (effective_addr, r1, ar1,
                                effective_addr2, r2, ar2, regs );

        break;

    case 0xFA:
    /*---------------------------------------------------------------*/
    /* AP       Add Decimal                                     [SS] */
    /*---------------------------------------------------------------*/

        /* Add packed decimal operands and set condition code */
        regs->psw.cc =
            add_packed (effective_addr, r1, ar1,
                                effective_addr2, r2, ar2, regs );

        break;

    case 0xFB:
    /*---------------------------------------------------------------*/
    /* SP       Subtract Decimal                                [SS] */
    /*---------------------------------------------------------------*/

        /* Subtract packed decimal operands and set condition code */
        regs->psw.cc =
            subtract_packed (effective_addr, r1, ar1,
                                effective_addr2, r2, ar2, regs );

        break;

    case 0xFC:
    /*---------------------------------------------------------------*/
    /* MP       Multiply Decimal                                [SS] */
    /*---------------------------------------------------------------*/

        /* Multiply packed decimal operands */
        multiply_packed (effective_addr, r1, ar1,
                                effective_addr2, r2, ar2, regs );

        break;

    case 0xFD:
    /*---------------------------------------------------------------*/
    /* DP       Divide Decimal                                  [SS] */
    /*---------------------------------------------------------------*/

        /* Divide packed decimal operands */
        divide_packed (effective_addr, r1, ar1,
                                effective_addr2, r2, ar2, regs );

        break;

    setia:
    /*---------------------------------------------------------------*/
    /* Set PSW instruction address from newia                        */
    /*---------------------------------------------------------------*/
        regs->psw.ia = newia &
                (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
        break;

    /*---------------------------------------------------------------*/
    /* Invalid instruction operation code                            */
    /*---------------------------------------------------------------*/
    default:
        program_check (PGM_OPERATION_EXCEPTION);
    terminate:
        break;
    } /* end switch(opcode) */

} /* end function execute_instruction */

/*-------------------------------------------------------------------*/
/* Perform I/O interrupt if pending                                  */
/*-------------------------------------------------------------------*/
static void perform_io_interrupt (REGS *regs)
{
int     rc;                             /* Return code               */
PSA    *psa;                            /* -> Prefixed storage area  */
U32     ioparm;                         /* I/O interruption parameter*/
U32     ioid;                           /* I/O interruption address  */
DWORD   csw;                            /* CSW for S/370 channels    */

    /* Test and clear pending I/O interrupt */
    rc = present_io_interrupt (regs, &ioid, &ioparm, csw);

    /* Exit if no interrupt was presented */
    if (rc == 0) return;

    /* Point to the PSA in main storage */
    psa = (PSA*)(sysblk.mainstor + regs->pxr);

#ifdef FEATURE_S370_CHANNEL
    /* Store the channel status word at PSA+X'40' */
    memcpy (psa->csw, csw, 8);

    /* Set the interrupt code to the I/O device address */
    regs->psw.intcode = ioid;

    /* For ECMODE, store the I/O device address at PSA+X'B8' */
    if (regs->psw.ecmode)
    {
        psa->ioid[0] = 0;
        psa->ioid[1] = 0;
        psa->ioid[2] = ioid >> 8;
        psa->ioid[3] = ioid & 0xFF;
    }

    /* Trace the I/O interrupt */
    if (sysblk.insttrace || sysblk.inststep)
        logmsg ("I/O interrupt code=%4.4X "
                "CSW=%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
                regs->psw.intcode,
                csw[0], csw[1], csw[2], csw[3],
                csw[4], csw[5], csw[6], csw[7]);
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Store X'0001' + subchannel number at PSA+X'B8' */
    psa->ioid[0] = ioid >> 24;
    psa->ioid[1] = (ioid & 0xFF0000) >> 16;
    psa->ioid[2] = (ioid & 0xFF00) >> 8;
    psa->ioid[3] = ioid & 0xFF;

    /* Store the I/O interruption parameter at PSA+X'BC' */
    psa->ioparm[0] = ioparm >> 24;
    psa->ioparm[1] = (ioparm & 0xFF0000) >> 16;
    psa->ioparm[2] = (ioparm & 0xFF00) >> 8;
    psa->ioparm[3] = ioparm & 0xFF;

    /* Trace the I/O interrupt */
    if (sysblk.insttrace || sysblk.inststep)
        logmsg ("I/O interrupt code=%2.2X%2.2X%2.2X%2.2X "
                "parm=%2.2X%2.2X%2.2X%2.2X\n",
                psa->ioid[0], psa->ioid[1],
                psa->ioid[2], psa->ioid[3],
                psa->ioparm[0], psa->ioparm[1],
                psa->ioparm[2], psa->ioparm[3]);
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Store current PSW at PSA+X'38' */
    store_psw ( &(regs->psw), psa->iopold );

    /* Load new PSW from PSA+X'78' */
    rc = load_psw ( &(regs->psw), psa->iopnew );
    if ( rc )
    {
        program_check (rc);
    }

} /* end function perform_io_interrupt */

/*-------------------------------------------------------------------*/
/* CPU instruction execution thread                                  */
/*-------------------------------------------------------------------*/
void *cpu_thread (REGS *regs)
{
DWORD   inst;                           /* Instruction area          */
int     rc;                             /* Return code               */
int     tracethis;                      /* Trace this instruction    */
int     stepthis;                       /* Stop on this instruction  */
int     diswait;                        /* 1=Disabled wait state     */
int     shouldbreak;                    /* 1=Stop at breakpoint      */
#ifdef INSTRUCTION_COUNTING
struct {
    int general[256];                   /* General inst counters     */
    int op01[256];                      /* 01xx instruction counters */
    int opA4[256];                      /* A4xx instruction counters */
    int opA5[256];                      /* A5xx instruction counters */
    int opA6[256];                      /* A6xx instruction counters */
    int opA7[16];                       /* A70x instruction counters */
    int opB2[256];                      /* B2xx instruction counters */
    int opB3[256];                      /* B3xx instruction counters */
    int opE4[256];                      /* E4xx instruction counters */
    int opE5[256];                      /* E5xx instruction counters */
    int opE6[256];                      /* E6xx instruction counters */
    int opED[256];                      /* EDxx instruction counters */
        } instcount;
int    *picta;                          /* -> Inst counter array     */
int     icidx;                          /* Instruction counter index */
#endif /*INSTRUCTION_COUNTING*/

    /* Display thread started message on control panel */
//  logmsg ("HHC620I CPU%d thread started: id=%ld\n",
//          regs->cpuad, thread_id());

#ifdef INSTRUCTION_COUNTING
    /* Clear instruction counters */
    memset (&instcount, 0, sizeof(instcount));
#endif /*INSTRUCTION_COUNTING*/

    /* Establish longjmp destination for program check */
    setjmp(regs->progjmp);

    /* Clear the disabled wait state flag */
    diswait = 0;

    /* Execute the program */
    while (1) {

        /* Reset instruction trace indicators */
        tracethis = 0;
        stepthis = 0;

        /* Obtain the interrupt lock */
        obtain_lock (&sysblk.intlock);

        /* If enabled for external interrupts, invite the
           service processor to present a pending interrupt */
        if (regs->psw.sysmask & PSW_EXTMASK)
            perform_external_interrupt (regs);

        /* If enabled for I/O interrupts, invite the channel
           subsystem to present a pending interrupt */
        if (regs->psw.sysmask &
                (regs->psw.ecmode ? PSW_IOMASK : 0xFE))
            perform_io_interrupt (regs);

        /* Perform restart interrupt if pending */
        if (regs->restart)
        {
            regs->restart = 0;
            rc = psw_restart (regs);
            if (rc == 0) regs->cpustate = CPUSTATE_STARTED;
        }

        /* If CPU is stopping, change status to stopped */
        if (regs->cpustate == CPUSTATE_STOPPING)
            regs->cpustate = CPUSTATE_STOPPED;

        /* Test for stopped state */
        if (regs->cpustate == CPUSTATE_STOPPED)
        {
            /* Wait for start command from panel */
            wait_condition (&sysblk.intcond, &sysblk.intlock);
            release_lock (&sysblk.intlock);
            continue;
        }

        /* Test for disabled wait PSW */
        if (regs->psw.wait && (regs->psw.sysmask &
                (regs->psw.ecmode ? (PSW_IOMASK | PSW_EXTMASK) : 0xFF))
                == 0)
        {
            if (diswait == 0)
            {
                logmsg ("Disabled wait state code %8.8X\n",
                        regs->psw.ia | (regs->psw.amode << 31));
#ifdef INSTRUCTION_COUNTING
                logmsg ("%llu instructions executed\n",
                        regs->instcount);
#endif /*INSTRUCTION_COUNTING*/
                diswait = 1;
            }

            /* Wait for restart command from panel */
            wait_condition (&sysblk.intcond, &sysblk.intlock);
            release_lock (&sysblk.intlock);
            continue;
        }

        /* Reset disabled wait state indicator */
        diswait = 0;

        /* Test for enabled wait state */
        if (regs->psw.wait)
        {
            /* Wait for I/O or external interrupt */
            wait_condition (&sysblk.intcond, &sysblk.intlock);
            release_lock (&sysblk.intlock);
            continue;
        }

        /* Release the interrupt lock */
        release_lock (&sysblk.intlock);

        /* Fetch the next sequential instruction */
        instfetch (inst, regs->psw.ia, regs);

        /* Count instruction usage */
        regs->instcount++;

#ifdef INSTRUCTION_COUNTING
        /* Find instruction counter for this opcode */
        switch (inst[0]) {
        case 0x01: picta = instcount.op01; icidx = inst[1]; break;
        case 0xA4: picta = instcount.opA4; icidx = inst[1]; break;
        case 0xA5: picta = instcount.opA5; icidx = inst[1]; break;
        case 0xA6: picta = instcount.opA6; icidx = inst[1]; break;
        case 0xA7: picta = instcount.opA7; icidx = inst[1] & 0x0F;
                   break;
        case 0xB2: picta = instcount.opB2; icidx = inst[1]; break;
        case 0xB3: picta = instcount.opB3; icidx = inst[1]; break;
        case 0xE4: picta = instcount.opE4; icidx = inst[1]; break;
        case 0xE5: picta = instcount.opE5; icidx = inst[1]; break;
        case 0xE6: picta = instcount.opE6; icidx = inst[1]; break;
        case 0xED: picta = instcount.opED; icidx = inst[1]; break;
        default: picta = instcount.general; icidx = inst[0];
        } /* end switch */

        /* Test for first usage of this opcode */
        if (picta[icidx] == 0 && regs->instcount >= 256) {
            if (picta == instcount.general)
                logmsg ("First use of instruction %2.2X\n",
                        inst[0]);
            else
                logmsg ("First use of instruction %2.2X%2.2X\n",
                        inst[0], icidx);
            tracethis = 1;
        }

        /* Count instruction usage by opcode */
        picta[icidx]++;
        if (regs->instcount % 1000000 == 0)
            logmsg ("%llu instructions executed\n", regs->instcount);
#endif /*INSTRUCTION_COUNTING*/

        /* Turn on trace for specific instructions */
//      if (inst[0] == 0xB2 && inst[1] == 0x20) sysblk.inststep = 1; /*SERVC*/
//      if (inst[0] == 0xB2 && inst[1] == 0x25) sysblk.inststep = 1; /*SSAR*/
//      if (inst[0] == 0xB2 && inst[1] == 0x40) sysblk.inststep = 1; /*BAKR*/
//      if (inst[0] == 0xB2 && inst[1] == 0x18) sysblk.inststep = 1; /*PC*/
//      if (inst[0] == 0xB2 && inst[1] == 0x28) sysblk.inststep = 1; /*PT*/
//      if (inst[0] == 0xB2 && inst[1] == 0x47) sysblk.inststep = 1; /*MSTA*/
//      if (inst[0] == 0xB2 && inst[1] == 0x49) sysblk.inststep = 1; /*EREG*/
//      if (inst[0] == 0xB2 && inst[1] == 0x4A) sysblk.inststep = 1; /*ESTA*/
//      if (inst[0] == 0x01 && inst[1] == 0x01) sysblk.inststep = 1; /*PR*/
//      if (inst[0] == 0xE5) sysblk.inststep = 1; /*LASP & MVS assists*/
        if (inst[0] == 0xFC) sysblk.inststep = 1; /*MP*/
        if (inst[0] == 0xFD) sysblk.inststep = 1; /*DP*/

        /* Test for breakpoint */
        shouldbreak = sysblk.instbreak
                        && (regs->psw.ia == sysblk.breakaddr);

        /* Display the instruction */
        if (sysblk.insttrace || sysblk.inststep || shouldbreak
            || tracethis || stepthis)
        {
            display_inst (regs, inst);
            if (sysblk.inststep || stepthis || shouldbreak)
            {
                /* Put CPU into stopped state */
                regs->cpustate = CPUSTATE_STOPPED;

                /* Wait for start command from panel */
                obtain_lock (&sysblk.intlock);
                while (regs->cpustate == CPUSTATE_STOPPED)
                    wait_condition (&sysblk.intcond, &sysblk.intlock);
                release_lock (&sysblk.intlock);
            }
        }

        /* Execute the instruction */
        execute_instruction (inst, 0, regs);
    }

    return NULL;
} /* end function cpu_thread */
