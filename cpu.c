/* CPU.C        (c) Copyright Roger Bowler, 1994-2000                */
/*              ESA/390 CPU Emulator                                 */

/*-------------------------------------------------------------------*/
/* This module implements the CPU instruction execution function of  */
/* the S/370 and ESA/390 architectures, as described in the manuals  */
/* GA22-7000-03 System/370 Principles of Operation                   */
/* SA22-7201-06 ESA/390 Principles of Operation                      */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      Floating point instructions by Peter Kuschnerus              */
/*      Nullification corrections by Jan Jaeger                      */
/*      Corrections to shift instructions by Jay Maynard, Jan Jaeger */
/*      Set priority by Reed H. Petty from an idea by Steve Gay      */
/*      Bad frame support by Jan Jaeger                              */
/*      STCPS and SCHM instructions by Jan Jaeger                    */
/*      Corrections to program check by Jan Jaeger                   */
/*      Branch tracing by Jan Jaeger                                 */
/*      Light optimization on critical path by Valery Pogonchenko    */
/*      STCRW and CSP instructions by Jan Jaeger                     */
/*-------------------------------------------------------------------*/

#include "hercules.h"

//#define MODULE_TRACE
//#define INSTRUCTION_COUNTING
//#define SVC_TRACE

/*-------------------------------------------------------------------*/
/* Add two signed fullwords giving a signed fullword result          */
/* and return condition code                                         */
/*-------------------------------------------------------------------*/
static inline int
add_signed ( U32 *result, U32 op1, U32 op2 )
{
U64     r;
U32     x;
int     carry_in, carry_out;
int     cc;

    r = (U64)op1 + op2;
    x = (U32)r;
    carry_in = ((op1 & 0x7FFFFFFF) + (op2 & 0x7FFFFFFF)) >> 31;
    carry_out = r >> 32;
    *result = x;
    cc = (carry_out != carry_in)? 3 :
        (x == 0)? 0 : ((S32)x < 0)? 1 : 2;
    return cc;
} /* end function add_signed */

/*-------------------------------------------------------------------*/
/* Subtract two signed fullwords giving a signed fullword result     */
/* and return condition code                                         */
/*-------------------------------------------------------------------*/
static inline int
sub_signed ( U32 *result, U32 op1, U32 op2 )
{
U64     r;
U32     x;
int     carry_in, carry_out;
int     cc;

    r = (U64)op1 + ~op2 + 1;
    x = (U32)r;
    carry_in = ((op1 & 0x7FFFFFFF) + (~op2 & 0x7FFFFFFF) + 1) >> 31;
    carry_out = r >> 32;
    *result = x;
    cc = (carry_out != carry_in)? 3 :
        (x == 0)? 0 : ((S32)x < 0)? 1 : 2;
    return cc;
} /* end function sub_signed */

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
} /* end function add_logical */

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
} /* end function sub_logical */

/*-------------------------------------------------------------------*/
/* Multiply two signed fullwords giving a signed doubleword result   */
/*-------------------------------------------------------------------*/
static inline void
mul_signed ( U32 *resulthi, U32 *resultlo, U32 op1, U32 op2 )
{
S64     r;

    r = (S64)(S32)op1 * (S32)op2;
    *resulthi = (U64)r >> 32;
    *resultlo = (U64)r & 0xFFFFFFFF;
} /* end function mul_signed */

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
} /* end function div_signed */

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
    if (psw->wait && (sysblk.insttrace || sysblk.inststep))
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
void program_check (REGS *regs, int code)
{
PSA    *psa;                            /* -> Prefixed storage area  */
REGS   *realregs;                       /* True regs structure       */

    /* program_check() may be called with a shadow copy of the
       regs structure, realregs is the pointer to the real structure
       which must be used when loading/storing the psw, or backing up
       the instruction address in case of nullification */
    realregs = sysblk.regs + regs->cpuad;

#if MAX_CPU_ENGINES > 1
    /* Unlock the main storage lock if held */
    if (realregs->mainlock)
        RELEASE_MAINLOCK(realregs);
#endif /*MAX_CPU_ENGINES > 1*/

    /* Set the main storage reference and change bits */
    STORAGE_KEY(regs->pxr) |= (STORKEY_REF | STORKEY_CHANGE);

    /* Back up the PSW for exceptions which cause nullification,
       unless the exception occurred during instruction fetch */
    if ((code == PGM_PAGE_TRANSLATION_EXCEPTION
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
        && regs->instvalid)
    {
        realregs->psw.ia -= realregs->psw.ilc;
        realregs->psw.ia &= ADDRESS_MAXWRAP(realregs);
    }

    /* Store the interrupt code in the PSW */
    regs->psw.intcode = code;

    /* Trace the program check */
    if (sysblk.insttrace || sysblk.inststep
        || (code != PGM_PAGE_TRANSLATION_EXCEPTION
            && code != PGM_SEGMENT_TRANSLATION_EXCEPTION
#ifdef NO_PROTECTION_EXCEPTION_TRACE
            && code != PGM_PROTECTION_EXCEPTION
#endif /*NO_PROTECTION_EXCEPTION_TRACE*/
#ifdef NO_BINARY_FP_OPERATION_EXCEPTION_TRACE
            && (!(code == PGM_OPERATION_EXCEPTION
                && regs->inst[0] == 0xB3))
#endif /*NO_BINARY_FP_OPERATION_EXCEPTION_TRACE*/
            && code != PGM_SPACE_SWITCH_EVENT
            && code != PGM_STACK_FULL_EXCEPTION
            && code != PGM_TRACE_TABLE_EXCEPTION
            && code != PGM_MONITOR_EVENT))
    {
        logmsg ("Program check CODE=%4.4X ILC=%d ",
                code, regs->psw.ilc);
        display_inst (regs, regs->instvalid ? regs->inst : NULL);
    }

    /* Point to PSA in main storage */
    psa = (PSA*)(sysblk.mainstor + regs->pxr);

    /* For ECMODE, store extended interrupt information in PSA */
    if ( regs->psw.ecmode )
    {
        /* Store the program interrupt code at PSA+X'8C' */
        psa->pgmint[0] = 0;
        psa->pgmint[1] = regs->psw.ilc;
        psa->pgmint[2] = code >> 8;
        psa->pgmint[3] = code & 0xFF;

        /* Store the access register number at PSA+160 */
        if (code == PGM_PAGE_TRANSLATION_EXCEPTION
            || code == PGM_SEGMENT_TRANSLATION_EXCEPTION
            || code == PGM_ALEN_TRANSLATION_EXCEPTION
            || code == PGM_ALE_SEQUENCE_EXCEPTION
            || code == PGM_ASTE_VALIDITY_EXCEPTION
            || code == PGM_ASTE_SEQUENCE_EXCEPTION
            || code == PGM_EXTENDED_AUTHORITY_EXCEPTION
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
            || code == PGM_PROTECTION_EXCEPTION
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
           )
            psa->excarid = regs->excarid;

        /* Store the translation exception address at PSA+144 */
        if (code == PGM_PAGE_TRANSLATION_EXCEPTION
            || code == PGM_SEGMENT_TRANSLATION_EXCEPTION
            || code == PGM_AFX_TRANSLATION_EXCEPTION
            || code == PGM_ASX_TRANSLATION_EXCEPTION
            || code == PGM_PRIMARY_AUTHORITY_EXCEPTION
            || code == PGM_SECONDARY_AUTHORITY_EXCEPTION
            || code == PGM_SPACE_SWITCH_EVENT
            || code == PGM_LX_TRANSLATION_EXCEPTION
            || code == PGM_EX_TRANSLATION_EXCEPTION
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
            || code == PGM_PROTECTION_EXCEPTION
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
           )
        {
            psa->tea[0] = (regs->tea & 0xFF000000) >> 24;
            psa->tea[1] = (regs->tea & 0xFF0000) >> 16;
            psa->tea[2] = (regs->tea & 0xFF00) >> 8;
            psa->tea[3] = regs->tea & 0xFF;
        }
    }

    /* Store current PSW at PSA+X'28' */
    store_psw (&(realregs->psw), psa->pgmold);

    /* Load new PSW from PSA+X'68' */
    if ( load_psw (&(realregs->psw), psa->pgmnew) )
    {
        logmsg ("Invalid program-check new PSW: "
                "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
                psa->pgmnew[0], psa->pgmnew[1], psa->pgmnew[2],
                psa->pgmnew[3], psa->pgmnew[4], psa->pgmnew[5],
                psa->pgmnew[6], psa->pgmnew[7]);
        realregs->cpustate = CPUSTATE_STOPPED;
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

    /* Set the main storage reference and change bits */
    STORAGE_KEY(regs->pxr) |= (STORKEY_REF | STORKEY_CHANGE);

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
int     r1, r2, b1 = 0, b2 = 0;         /* Values of R fields        */
U32     newia;                          /* New instruction address   */
int     ilc;                            /* Instruction length code   */
int     m;                              /* Condition code mask       */
U32     n, n1, n2;                      /* 32-bit operand values     */
int     private;                        /* 1=Private address space   */
int     protect;                        /* 1=ALE or page protection  */
int     stid;                           /* Segment table indication  */
int     cc;                             /* Condition code            */
BYTE    obyte, sbyte, dbyte;            /* Byte work areas           */
DWORD   dword;                          /* Doubleword work area      */
U64     dreg;                           /* Double register work area */
int     d, h, i, j;                     /* Integer work areas        */
int     divide_overflow;                /* 1=divide overflow         */
int     effective_addr = 0;             /* Effective address         */
int     effective_addr2 = 0;            /* Effective address         */
PSA    *psa;                            /* -> prefixed storage area  */
int     rc;                             /* Return code               */
DEVBLK *dev;                            /* -> device block for SIO   */
U32     ccwaddr;                        /* CCW address for start I/O */
U32     ioparm;                         /* I/O interruption parameter*/
U16     xcode;                          /* Exception code            */
#if defined(FEATURE_IMMEDIATE_AND_RELATIVE)
U16     h1, h2, h3;                     /* 16-bit operand values     */
#endif /*FEATURE_IMMEDIATE_AND_RELATIVE*/
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
    r2 = ibyte & 0x0F;
#define r3 r2 /* alternate name for second register number */
#define x2 r2 /* alternate name for second register number */

    /* Determine the instruction length */
    ilc = (opcode < 0x40) ? 2 : (opcode < 0xC0) ? 4 : 6;

    /* For 4 and 6 byte instructions, calculate effective addresses */
    if (ilc > 2)
    {
        /* Calculate the first effective address */
        b1 = inst[2] >> 4;
        effective_addr = ((inst[2] & 0x0F) << 8) | inst[3];
        if (b1 != 0)
        {
            effective_addr += regs->gpr[b1];
            effective_addr &= ADDRESS_MAXWRAP(regs);
        }

        /* Apply indexing for RX instructions (note: indexing for
           the LRA instruction is handled within the case statement,
           to save one comparison here on the critical path) */
        if (opcode <= 0x7F && x2 != 0)
        {
            effective_addr += regs->gpr[x2];
            effective_addr &= ADDRESS_MAXWRAP(regs);
        }

        /* Calculate the 2nd effective address for SS instructions */
        if (ilc > 4)
        {
            b2 = inst[4] >> 4;
            effective_addr2 = ((inst[4] & 0x0F) << 8) | inst[5];
            if (b2 != 0)
            {
                effective_addr2 += regs->gpr[b2];
                effective_addr2 &= ADDRESS_MAXWRAP(regs);
            }
        }

    } /* end if(ilc>2) */

    /* If this instruction was not the subject of an execute,
       update the PSW instruction length and address */
    if (execflag == 0)
    {
        regs->psw.ilc = ilc;
        regs->psw.ia += ilc;
        regs->psw.ia &= ADDRESS_MAXWRAP(regs);
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
                program_check (regs, PGM_SPACE_SWITCH_EVENT);
            }

            break;

        case 0x02:
        /*-----------------------------------------------------------*/
        /* 0102: UPT - Update Tree                               [E] */
        /*-----------------------------------------------------------*/

            /* Update Tree and set condition code */
            regs->psw.cc = update_tree (regs);

            break;

#ifdef FEATURE_EXTENDED_TOD_CLOCK
        case 0x07:
        /*-----------------------------------------------------------*/
        /* 0107: SCKPF - Set Clock Programmable Field            [E] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if register 0 bits 0-15 are not zeroes */
            if ( regs->gpr[0] & 0xFFFF0000 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Set TOD programmable register from register 0 */
            regs->todpr = regs->gpr[0] & 0x0000FFFF;

            break;
#endif /*FEATURE_EXTENDED_TOD_CLOCK*/

        default:
        /*-----------------------------------------------------------*/
        /* 01xx: Invalid operation                                   */
        /*-----------------------------------------------------------*/
            program_check (regs, PGM_OPERATION_EXCEPTION);
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

#ifdef FEATURE_TRACING
        /* Add a branch trace entry to the trace table */
        if ((regs->cr[12] & CR12_BRTRACE) && (r2 != 0))
            regs->cr[12] = trace_br (regs->psw.amode,
                                        regs->gpr[r2], regs);
#endif /*FEATURE_TRACING*/

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
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Program check if R2 bits 28-31 are not zeroes */
        if ( regs->gpr[r2] & 0x0000000F )
        {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Load 2K block address from R2 register */
        n = regs->gpr[r2] & 0x00FFF800;

        /* Convert real address to absolute address */
        n = APPLY_PREFIXING (n, regs->pxr);

        /* Addressing exception if block is outside main storage */
        if ( n >= sysblk.mainsize )
        {
            program_check (regs, PGM_ADDRESSING_EXCEPTION);
            goto terminate;
        }

        /* Update the storage key from R1 register bits 24-30 */
        STORAGE_KEY(n) &= STORKEY_BADFRM;
        STORAGE_KEY(n) |= regs->gpr[r1] & ~(STORKEY_BADFRM);

//      /*debug*/logmsg("SSK storage block %8.8X key %2.2X\n",
//      /*debug*/       regs->gpr[r2], regs->gpr[r1] & 0xFE);

        break;

    case 0x09:
    /*---------------------------------------------------------------*/
    /* ISK      Insert Storage Key                              [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Program check if R2 bits 28-31 are not zeroes */
        if ( regs->gpr[r2] & 0x0000000F )
        {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Load 2K block address from R2 register */
        n = regs->gpr[r2] & 0x00FFF800;

        /* Convert real address to absolute address */
        n = APPLY_PREFIXING (n, regs->pxr);

        /* Addressing exception if block is outside main storage */
        if ( n >= sysblk.mainsize )
        {
            program_check (regs, PGM_ADDRESSING_EXCEPTION);
            goto terminate;
        }

        /* Insert the storage key into R1 register bits 24-31 */
        regs->gpr[r1] &= 0xFFFFFF00;
        regs->gpr[r1] |= (STORAGE_KEY(n) & 0xFE);

        /* In BC mode, clear bits 29-31 of R1 register */
        if ( regs->psw.ecmode == 0 )
            regs->gpr[r1] &= 0xFFFFFFF8;

//      /*debug*/logmsg("ISK storage block %8.8X key %2.2X\n",
//                      regs->gpr[r2], regs->gpr[r1] & 0xFE);

        break;
#endif /*FEATURE_BASIC_STORAGE_KEYS*/

    case 0x0A:
    /*---------------------------------------------------------------*/
    /* SVC      Supervisor Call                                 [RR] */
    /*---------------------------------------------------------------*/

        /* Set the main storage reference and change bits */
        STORAGE_KEY(regs->pxr) |= (STORKEY_REF | STORKEY_CHANGE);

        /* Use the I-byte to set the SVC interruption code */
        regs->psw.intcode = ibyte;

#ifdef SVC_TRACE
        /* Trace BLDL/FIND */
        if (ibyte == 18)
        {
            BYTE memname[8];
            n = regs->gpr[0];
            if ((regs->gpr[1] & 0x80000000) == 0) n += 4;
            n &= ADDRESS_MAXWRAP(regs);
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
            n &= ADDRESS_MAXWRAP(regs);
            n = vfetch4 (n, 0, regs);
            n &= ADDRESS_MAXWRAP(regs);
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
            n &= ADDRESS_MAXWRAP(regs);
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
            program_check (regs, rc);
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

#ifdef FEATURE_TRACING
        /* Add a branch trace entry to the trace table */
        if ((regs->cr[12] & CR12_BRTRACE) && (r2 != 0))
            regs->cr[12] = trace_br (regs->gpr[r2] & 0x80000000,
                                        regs->gpr[r2], regs);
#endif /*FEATURE_TRACING*/

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

#ifdef FEATURE_TRACING
        /* Add a branch trace entry to the trace table */
        if ((regs->cr[12] & CR12_BRTRACE) && (r2 != 0))
            regs->cr[12] = trace_br (regs->psw.amode,
                                        regs->gpr[r2], regs);
#endif /*FEATURE_TRACING*/

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
                program_check (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
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
                program_check (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
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
            program_check (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
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
            program_check (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
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
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
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
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
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
            program_check (regs, PGM_FIXED_POINT_DIVIDE_EXCEPTION);
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

#ifdef FEATURE_HEXADECIMAL_FLOATING_POINT
    case 0x20:
    /*---------------------------------------------------------------*/
    /* LPDR     Load Positive Floating Point Long Register      [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Copy register contents, clear the sign bit */
        regs->fpr[r1] = regs->fpr[r2] & 0x7FFFFFFF;
        regs->fpr[r1+1] = regs->fpr[r2+1];

        /* Set condition code */
        regs->psw.cc =
            ((regs->fpr[r1] & 0x00FFFFFF) || regs->fpr[r1+1]) ? 2 : 0;

        break;

    case 0x21:
    /*---------------------------------------------------------------*/
    /* LNDR     Load Negative Floating Point Long Register      [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Copy register contents, set the sign bit */
        regs->fpr[r1] = regs->fpr[r2] | 0x80000000;
        regs->fpr[r1+1] = regs->fpr[r2+1];

        /* Set condition code */
        regs->psw.cc =
            ((regs->fpr[r1] & 0x00FFFFFF) || regs->fpr[r1+1]) ? 1 : 0;

        break;

    case 0x22:
    /*---------------------------------------------------------------*/
    /* LTDR     Load and Test Floating Point Long Register      [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Copy register contents */
        regs->fpr[r1] = regs->fpr[r2];
        regs->fpr[r1+1] = regs->fpr[r2+1];

        /* Set condition code */
        if ((regs->fpr[r1] & 0x00FFFFFF) || regs->fpr[r1+1]) {
            regs->psw.cc = (regs->fpr[r1] & 0x80000000) ? 1 : 2;
        } else
            regs->psw.cc = 0;

        break;

    case 0x23:
    /*---------------------------------------------------------------*/
    /* LCDR     Load Complement Floating Point Long Register    [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Copy register contents, invert sign bit */
        regs->fpr[r1] = regs->fpr[r2] ^ 0x80000000;
        regs->fpr[r1+1] = regs->fpr[r2+1];

        /* Set condition code */
        if ((regs->fpr[r1] & 0x00FFFFFF) || regs->fpr[r1+1]) {
            regs->psw.cc = (regs->fpr[r1] & 0x80000000) ? 1 : 2;
        } else
            regs->psw.cc = 0;

        break;

    case 0x24:
    /*---------------------------------------------------------------*/
    /* HDR      Halve Floating Point Long Register              [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        halve_float_long_reg (r1, r2, regs);

        break;

    case 0x25:
    /*---------------------------------------------------------------*/
    /* LRDR     Load Rounded Floating Point Long Register       [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        /* or if R2 is not 0 or 4 */
        if (( r1 & 9) || (r2 & 11)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        round_float_long_reg (r1, r2, regs);

        break;

    case 0x26:
    /*---------------------------------------------------------------*/
    /* MXR      Multiply Floating Point Extended Register       [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0 or 4 */
        if (( r1 & 11) || (r2 & 11)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        multiply_float_ext_reg (r1, r2, regs);

        break;

    case 0x27:
    /*---------------------------------------------------------------*/
    /* MXDR     Multiply Floating Point Long to Extended Reg.   [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0 or 4 */
        /* or if R2 is not 0, 2, 4, or 6 */
        if (( r1 & 11) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        multiply_float_long_to_ext_reg (r1, r2, regs);

        break;

    case 0x28:
    /*---------------------------------------------------------------*/
    /* LDR      Load Floating Point Long Register               [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Copy register contents */
        regs->fpr[r1] = regs->fpr[r2];
        regs->fpr[r1+1] = regs->fpr[r2+1];

        break;

    case 0x29:
    /*---------------------------------------------------------------*/
    /* CDR      Compare Floating Point Long Register            [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        compare_float_long_reg (r1, r2, regs);

        break;

    case 0x2A:
    /*---------------------------------------------------------------*/
    /* ADR      Add Floating Point Long Register                [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        add_float_long_reg (r1, r2, regs);

        break;

    case 0x2B:
    /*---------------------------------------------------------------*/
    /* SDR      Subtract Floating Point Long Register           [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        subtract_float_long_reg (r1, r2, regs);

        break;

    case 0x2C:
    /*---------------------------------------------------------------*/
    /* MDR      Multiply Floating Point Long Register           [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        multiply_float_long_reg (r1, r2, regs);

        break;

    case 0x2D:
    /*---------------------------------------------------------------*/
    /* DDR      Divide Floating Point Long Register             [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        divide_float_long_reg (r1, r2, regs);

        break;

    case 0x2E:
    /*---------------------------------------------------------------*/
    /* AWR      Add Unnormalized Floating Point Long Register   [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        add_unnormal_float_long_reg (r1, r2, regs);

        break;

    case 0x2F:
    /*---------------------------------------------------------------*/
    /* SWR      Subtract Unnormalized Floating Point Long Reg.  [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        subtract_unnormal_float_long_reg (r1, r2, regs);

        break;

    case 0x30:
    /*---------------------------------------------------------------*/
    /* LPER     Load Positive Floating Point Short Register     [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Copy register contents, clear sign bit */
        regs->fpr[r1] = regs->fpr[r2] & 0x7FFFFFFF;

        /* Set condition code */
        regs->psw.cc = (regs->fpr[r1] & 0x00FFFFFF) ? 2 : 0;

        break;

    case 0x31:
    /*---------------------------------------------------------------*/
    /* LNER     Load Negative Floating Point Short Register     [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Copy register contents, set sign bit */
        regs->fpr[r1] = regs->fpr[r2] | 0x80000000;

        /* Set condition code */
        regs->psw.cc = (regs->fpr[r1] & 0x00FFFFFF) ? 1 : 0;

        break;

    case 0x32:
    /*---------------------------------------------------------------*/
    /* LTER     Load and Test Floating Point Short Register     [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Copy register contents */
        regs->fpr[r1] = regs->fpr[r2];

        /* Set condition code */
        if (regs->fpr[r1] & 0x00FFFFFF) {
            regs->psw.cc = (regs->fpr[r1] & 0x80000000) ? 1 : 2;
        } else
            regs->psw.cc = 0;

        break;

    case 0x33:
    /*---------------------------------------------------------------*/
    /* LCER     Load Complement Floating Point Short Register   [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Copy register contents, invert sign bit */
        regs->fpr[r1] = regs->fpr[r2] ^ 0x80000000;

        /* Set condition code */
        if (regs->fpr[r1] & 0x00FFFFFF) {
            regs->psw.cc = (regs->fpr[r1] & 0x80000000) ? 1 : 2;
        } else
            regs->psw.cc = 0;

        break;

    case 0x34:
    /*---------------------------------------------------------------*/
    /* HER      Halve Floating Point Short Register             [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        halve_float_short_reg (r1, r2, regs);

        break;

    case 0x35:
    /*---------------------------------------------------------------*/
    /* LRER     Load Rounded Floating Point Short Register      [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        round_float_short_reg (r1, r2, regs);

        break;

    case 0x36:
    /*---------------------------------------------------------------*/
    /* AXR      Add Floating Point Extended Register            [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0 or 4 */
        if (( r1 & 11) || (r2 & 11)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        add_float_ext_reg (r1, r2, regs);

        break;

    case 0x37:
    /*---------------------------------------------------------------*/
    /* SXR      Subtract Floating Point Extended Register       [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0 or 4 */
        if (( r1 & 11) || (r2 & 11)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        subtract_float_ext_reg (r1, r2, regs);

        break;

    case 0x38:
    /*---------------------------------------------------------------*/
    /* LER      Load Floating Point Short Register              [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Copy register content */
        regs->fpr[r1] = regs->fpr[r2];

        break;

    case 0x39:
    /*---------------------------------------------------------------*/
    /* CER      Compare Floating Point Short Register           [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        compare_float_short_reg (r1, r2, regs);

        break;

    case 0x3A:
    /*---------------------------------------------------------------*/
    /* AER      Add Floating Point Short Register               [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        add_float_short_reg (r1, r2, regs);

        break;

    case 0x3B:
    /*---------------------------------------------------------------*/
    /* SER      Subtract Floating Point Short Register          [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        subtract_float_short_reg (r1, r2, regs);

        break;

    case 0x3C:
    /*---------------------------------------------------------------*/
    /* MER      Multiply Short to Long Floating Point Register  [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        multiply_float_short_to_long_reg (r1, r2, regs);

        break;

    case 0x3D:
    /*---------------------------------------------------------------*/
    /* DER      Divide Floating Point Short Register            [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        divide_float_short_reg (r1, r2, regs);

        break;

    case 0x3E:
    /*---------------------------------------------------------------*/
    /* AUR      Add Unnormalized Floating Point Short Register  [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        add_unnormal_float_short_reg (r1, r2, regs);

        break;

    case 0x3F:
    /*---------------------------------------------------------------*/
    /* SUR      Subtract Unnormalized Floating Point Short Reg. [RR] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 or R2 is not 0, 2, 4, or 6 */
        if (( r1 & 9) || (r2 & 9)) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        subtract_unnormal_float_short_reg (r1, r2, regs);

        break;
#endif /*FEATURE_HEXADECIMAL_FLOATING_POINT*/

    case 0x40:
    /*---------------------------------------------------------------*/
    /* STH      Store Halfword                                  [RX] */
    /*---------------------------------------------------------------*/

        /* Store rightmost 2 bytes of R1 register at operand address */
        vstore2 ( regs->gpr[r1] & 0xFFFF, effective_addr, b1, regs );

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
        vstoreb ( regs->gpr[r1] & 0xFF, effective_addr, b1, regs );

        break;

    case 0x43:
    /*---------------------------------------------------------------*/
    /* IC       Insert Character                                [RX] */
    /*---------------------------------------------------------------*/

        /* Load rightmost byte of R1 register from operand address */
        n = regs->gpr[r1] & 0xFFFFFF00;
        regs->gpr[r1] = n | vfetchb ( effective_addr, b1, regs );

        break;

    case 0x44:
    /*---------------------------------------------------------------*/
    /* EX       Execute                                         [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if operand is not on a halfword boundary */
        if ( effective_addr & 0x00000001 )
        {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Fetch target instruction from operand address */
        instfetch (dword, effective_addr, regs);

        /* Program check if recursive execute */
        if ( dword[0] == 0x44 )
        {
            program_check (regs, PGM_EXECUTE_EXCEPTION);
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
        regs->gpr[r1] = vfetch2 ( effective_addr, b1, regs );

        /* Propagate sign bit to leftmost 2 bytes of register */
        if ( regs->gpr[r1] > 0x7FFF )
            regs->gpr[r1] |= 0xFFFF0000;

        break;

    case 0x49:
    /*---------------------------------------------------------------*/
    /* CH       Compare Halfword                                [RX] */
    /*---------------------------------------------------------------*/

        /* Load rightmost 2 bytes of comparand from operand address */
        n = vfetch2 ( effective_addr, b1, regs );

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
        n = vfetch2 ( effective_addr, b1, regs );

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
            program_check (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
            goto terminate;
        }

        break;

    case 0x4B:
    /*---------------------------------------------------------------*/
    /* SH       Subtract Halfword                               [RX] */
    /*---------------------------------------------------------------*/

        /* Load 2 bytes from operand address */
        n = vfetch2 ( effective_addr, b1, regs );

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
            program_check (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
            goto terminate;
        }

        break;

    case 0x4C:
    /*---------------------------------------------------------------*/
    /* MH       Multiply Halfword                               [RX] */
    /*---------------------------------------------------------------*/

        /* Load 2 bytes from operand address */
        n = vfetch2 ( effective_addr, b1, regs );

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
        convert_to_decimal (r1, effective_addr, b1, regs);

        break;

    case 0x4F:
    /*---------------------------------------------------------------*/
    /* CVB      Convert to Binary                               [RX] */
    /*---------------------------------------------------------------*/

        /* Convert packed decimal storage operand into R1 register */
        convert_to_binary (r1, effective_addr, b1, regs);

        break;

    case 0x50:
    /*---------------------------------------------------------------*/
    /* ST       Store                                           [RX] */
    /*---------------------------------------------------------------*/

        /* Store register contents at operand address */
        vstore4 ( regs->gpr[r1], effective_addr, b1, regs );

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
        n = vfetch4 ( effective_addr, b1, regs );

        /* AND second operand with first and set condition code */
        regs->psw.cc = ( regs->gpr[r1] &= n ) ? 1 : 0;

        break;

    case 0x55:
    /*---------------------------------------------------------------*/
    /* CL       Compare Logical                                 [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, b1, regs );

        /* Compare unsigned operands and set condition code */
        regs->psw.cc = regs->gpr[r1] < n ? 1 :
                         regs->gpr[r1] > n ? 2 : 0;

        break;

    case 0x56:
    /*---------------------------------------------------------------*/
    /* O        Or                                              [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, b1, regs );

        /* OR second operand with first and set condition code */
        regs->psw.cc = ( regs->gpr[r1] |= n ) ? 1 : 0;

        break;

    case 0x57:
    /*---------------------------------------------------------------*/
    /* X        Exclusive Or                                    [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, b1, regs );

        /* XOR second operand with first and set condition code */
        regs->psw.cc = ( regs->gpr[r1] ^= n ) ? 1 : 0;

        break;

    case 0x58:
    /*---------------------------------------------------------------*/
    /* L        Load                                            [RX] */
    /*---------------------------------------------------------------*/

        /* Load R1 register from second operand */
        regs->gpr[r1] = vfetch4 ( effective_addr, b1, regs );

        break;

    case 0x59:
    /*---------------------------------------------------------------*/
    /* C        Compare                                         [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, b1, regs );

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
        n = vfetch4 ( effective_addr, b1, regs );

        /* Add signed operands and set condition code */
        regs->psw.cc =
                add_signed (&(regs->gpr[r1]),
                        regs->gpr[r1],
                        n);

        /* Program check if fixed-point overflow */
        if ( regs->psw.cc == 3 && regs->psw.fomask )
        {
            program_check (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
            goto terminate;
        }

        break;

    case 0x5B:
    /*---------------------------------------------------------------*/
    /* S        Subtract                                        [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, b1, regs );

        /* Subtract signed operands and set condition code */
        regs->psw.cc =
                sub_signed (&(regs->gpr[r1]),
                        regs->gpr[r1],
                        n);

        /* Program check if fixed-point overflow */
        if ( regs->psw.cc == 3 && regs->psw.fomask )
        {
            program_check (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
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
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, b1, regs );

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
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, b1, regs );

        /* Divide r1::r1+1 by n, remainder in r1, quotient in r1+1 */
        divide_overflow =
            div_signed (&(regs->gpr[r1]), &(regs->gpr[r1+1]),
                        regs->gpr[r1],
                        regs->gpr[r1+1],
                        n);

        /* Program check if overflow */
        if ( divide_overflow )
        {
            program_check (regs, PGM_FIXED_POINT_DIVIDE_EXCEPTION);
            goto terminate;
        }

        break;

    case 0x5E:
    /*---------------------------------------------------------------*/
    /* AL       Add Logical                                     [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, b1, regs );

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
        n = vfetch4 ( effective_addr, b1, regs );

        /* Subtract unsigned operands and set condition code */
        regs->psw.cc =
                sub_logical (&(regs->gpr[r1]),
                        regs->gpr[r1],
                        n);

        break;

#ifdef FEATURE_HEXADECIMAL_FLOATING_POINT
    case 0x60:
    /*---------------------------------------------------------------*/
    /* STD      Store Floating Point Long                       [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Store register contents at operand address */
        dreg = ((U64)regs->fpr[r1] << 32) | regs->fpr[r1+1];
        vstore8 (dreg, effective_addr, b1, regs);

        break;

    case 0x67:
    /*---------------------------------------------------------------*/
    /* MXD      Multiply Floating Point Long to Extended        [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0 or 4 */
        if ( r1 & 11) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        multiply_float_long_to_ext (r1, effective_addr, b1, regs);

        break;

    case 0x68:
    /*---------------------------------------------------------------*/
    /* LD       Load Floating Point Long                        [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Fetch value from operand address */
        dreg = vfetch8 (effective_addr, b1, regs );

        /* Update register contents */
        regs->fpr[r1] = dreg >> 32;
        regs->fpr[r1+1] = dreg;

        break;

    case 0x69:
    /*---------------------------------------------------------------*/
    /* CD       Compare Floating Point Long                     [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        compare_float_long (r1, effective_addr, b1, regs);

        break;

    case 0x6A:
    /*---------------------------------------------------------------*/
    /* AD       Add Floating Point Long                         [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        add_float_long (r1, effective_addr, b1, regs);

        break;

    case 0x6B:
    /*---------------------------------------------------------------*/
    /* SD       Subtract Floating Point Long                    [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        subtract_float_long (r1, effective_addr, b1, regs);

        break;

    case 0x6C:
    /*---------------------------------------------------------------*/
    /* MD       Multiply Floating Point Long                    [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        multiply_float_long (r1, effective_addr, b1, regs);

        break;

    case 0x6D:
    /*---------------------------------------------------------------*/
    /* DD       Divide Floating Point Long                      [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        divide_float_long (r1, effective_addr, b1, regs);

        break;

    case 0x6E:
    /*---------------------------------------------------------------*/
    /* AW       Add Unnormalized Floating Point Long            [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        add_unnormal_float_long (r1, effective_addr, b1, regs);

        break;

    case 0x6F:
    /*---------------------------------------------------------------*/
    /* SW       Subtract Unnormalized Floating Point Long       [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        subtract_unnormal_float_long (r1, effective_addr, b1, regs);

        break;

    case 0x70:
    /*---------------------------------------------------------------*/
    /* STE      Store Floating Point Short                      [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Store register contents at operand address */
        vstore4 (regs->fpr[r1], effective_addr, b1, regs);

        break;
#endif /*FEATURE_HEXADECIMAL_FLOATING_POINT*/

#ifdef FEATURE_IMMEDIATE_AND_RELATIVE
    case 0x71:
    /*---------------------------------------------------------------*/
    /* MS       Multiply Single                                 [RX] */
    /*---------------------------------------------------------------*/

        /* Load second operand from operand address */
        n = vfetch4 ( effective_addr, b1, regs );

        /* Multiply signed operands ignoring overflow */
        (S32)regs->gpr[r1] *= (S32)n;

        break;
#endif /*FEATURE_IMMEDIATE_AND_RELATIVE*/

#ifdef FEATURE_HEXADECIMAL_FLOATING_POINT
    case 0x78:
    /*---------------------------------------------------------------*/
    /* LE       Load Floating Point Short                       [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Update first 32 bits of register from operand address */
        regs->fpr[r1] = vfetch4 (effective_addr, b1, regs);

        break;

    case 0x79:
    /*---------------------------------------------------------------*/
    /* CE       Compare Floating Point Short                    [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        compare_float_short (r1, effective_addr, b1, regs);

        break;

    case 0x7A:
    /*---------------------------------------------------------------*/
    /* AE       Add Floating Point Short                        [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        add_float_short (r1, effective_addr, b1, regs);

        break;

    case 0x7B:
    /*---------------------------------------------------------------*/
    /* SE       Subtract Floating Point Short                   [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        subtract_float_short (r1, effective_addr, b1, regs);

        break;

    case 0x7C:
    /*---------------------------------------------------------------*/
    /* ME       Multiply Floating Point Short to Long           [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        multiply_float_short_to_long (r1, effective_addr, b1, regs);

        break;

    case 0x7D:
    /*---------------------------------------------------------------*/
    /* DE       Divide Floating Point Short                     [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        divide_float_short (r1, effective_addr, b1, regs);

        break;

    case 0x7E:
    /*---------------------------------------------------------------*/
    /* AU       Add Unnormalized Floating Point Short           [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        add_unnormal_float_short (r1, effective_addr, b1, regs);

        break;

    case 0x7F:
    /*---------------------------------------------------------------*/
    /* SU       Subtract Unnormalized Floating Point Short      [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if R1 is not 0, 2, 4, or 6 */
        if ( r1 & 9 ) {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        subtract_unnormal_float_short (r1, effective_addr, b1, regs);

        break;
#endif /*FEATURE_HEXADECIMAL_FLOATING_POINT*/

    case 0x80:
    /*---------------------------------------------------------------*/
    /* SSM      Set System Mask                                  [S] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Special operation exception if SSM-suppression is active */
        if ( regs->cr[0] & CR0_SSM_SUPP )
        {
            program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Load new system mask value from operand address */
        regs->psw.sysmask = vfetchb ( effective_addr, b1, regs );

        /* For ECMODE, bits 0 and 2-4 of system mask must be zero */
        if (regs->psw.ecmode && (regs->psw.sysmask & 0xB8) != 0)
        {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
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
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Program check if operand is not on a doubleword boundary */
        if ( effective_addr & 0x00000007 )
        {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Perform serialization and checkpoint synchronization */
        perform_serialization ();
        perform_chkpt_sync ();

        /* Fetch new PSW from operand address */
        vfetchc ( dword, 7, effective_addr, b1, regs );

        /* Load updated PSW */
        rc = load_psw ( &(regs->psw), dword );
        if ( rc )
        {
            program_check (regs, rc);
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

        /* Process diagnose instruction */
        diagnose_call (effective_addr, r1, r2, regs);

        /* Perform serialization and checkpoint-synchronization */
        perform_serialization ();
        perform_chkpt_sync ();

        break;

#ifdef FEATURE_IMMEDIATE_AND_RELATIVE
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
#endif /*FEATURE_IMMEDIATE_AND_RELATIVE*/

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
        regs->gpr[r1] = n > 31 ? 0 : regs->gpr[r1] >> n;

        break;

    case 0x89:
    /*---------------------------------------------------------------*/
    /* SLL      Shift Left Logical                              [RS] */
    /*---------------------------------------------------------------*/

        /* Use rightmost six bits of operand address as shift count */
        n = effective_addr & 0x3F;

        /* Shift the R1 register */
        regs->gpr[r1] = n > 31 ? 0 : regs->gpr[r1] << n;

        break;

    case 0x8A:
    /*---------------------------------------------------------------*/
    /* SRA      Shift Right Arithmetic                          [RS] */
    /*---------------------------------------------------------------*/

        /* Use rightmost six bits of operand address as shift count */
        n = effective_addr & 0x3F;

        /* Shift the signed value of the R1 register */
        (S32)regs->gpr[r1] = n > 30 ?
                        ((S32)regs->gpr[r1] < 0 ? -1 : 0) :
                        (S32)regs->gpr[r1] >> n;

        /* Set the condition code */
        regs->psw.cc = (S32)regs->gpr[r1] > 0 ? 2 :
                       (S32)regs->gpr[r1] < 0 ? 1 : 0;

        break;

    case 0x8B:
    /*---------------------------------------------------------------*/
    /* SLA      Shift Left Arithmetic                           [RS] */
    /*---------------------------------------------------------------*/

        /* Use rightmost six bits of operand address as shift count */
        n = effective_addr & 0x3F;

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
            {
                program_check (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
                goto terminate;
            }
            break;
        }

        /* Set the condition code */
        regs->psw.cc = (S32)regs->gpr[r1] > 0 ? 2 :
                       (S32)regs->gpr[r1] < 0 ? 1 : 0;

        break;

    case 0x8C:
    /*---------------------------------------------------------------*/
    /* SRDL     Shift Right Double Logical                      [RS] */
    /*---------------------------------------------------------------*/

        /* Specification exception if R1 is odd */
        if ( r1 & 1 )
        {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
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
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
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
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Use rightmost six bits of operand address as shift count */
        n = effective_addr & 0x3F;

        /* Shift the R1 and R1+1 registers */
        dreg = (U64)regs->gpr[r1] << 32 | regs->gpr[r1+1];
        dreg = (U64)((S64)dreg >> n);
        regs->gpr[r1] = dreg >> 32;
        regs->gpr[r1+1] = dreg & 0xFFFFFFFF;

        /* Set the condition code */
        regs->psw.cc = (S64)dreg > 0 ? 2 : (S64)dreg < 0 ? 1 : 0;

        break;

    case 0x8F:
    /*---------------------------------------------------------------*/
    /* SLDA     Shift Left Double Arithmetic                    [RS] */
    /*---------------------------------------------------------------*/

        /* Specification exception if R1 is odd */
        if ( r1 & 1 )
        {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Use rightmost six bits of operand address as shift count */
        n = effective_addr & 0x3F;

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
        if (m) regs->gpr[r1] |= 0x80000000;
        regs->gpr[r1+1] = dreg & 0xFFFFFFFF;

        /* Condition code 3 and program check if overflow occurred */
        if (j)
        {
            regs->psw.cc = 3;
            if ( regs->psw.fomask )
            {
                program_check (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
                goto terminate;
            }
            break;
        }

        /* Set the condition code */
        regs->psw.cc = (S64)dreg > 0 ? 2 : (S64)dreg < 0 ? 1 : 0;

        break;

    case 0x90:
    /*---------------------------------------------------------------*/
    /* STM      Store Multiple                                  [RS] */
    /*---------------------------------------------------------------*/

        /* Copy register contents into work area */
        for ( n = r1, d = 0; ; )
        {
            /* Copy contents of one register to work area */
            cwork1[d++] = (regs->gpr[n] & 0xFF000000) >> 24;
            cwork1[d++] = (regs->gpr[n] & 0xFF0000) >> 16;
            cwork1[d++] = (regs->gpr[n] & 0xFF00) >> 8;
            cwork1[d++] = regs->gpr[n] & 0xFF;

            /* Instruction is complete when r3 register is done */
            if ( n == r3 ) break;

            /* Update register number, wrapping from 15 to 0 */
            n++; n &= 15;
        }

        /* Store register contents at operand address */
        vstorec ( cwork1, d-1, effective_addr, b1, regs );

        break;

    case 0x91:
    /*---------------------------------------------------------------*/
    /* TM       Test under Mask                                 [SI] */
    /*---------------------------------------------------------------*/

        /* Fetch byte from operand address */
        obyte = vfetchb ( effective_addr, b1, regs );

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
        vstoreb ( ibyte, effective_addr, b1, regs );

        break;

    case 0x93:
    /*---------------------------------------------------------------*/
    /* TS       Test and Set                                     [S] */
    /*---------------------------------------------------------------*/

        /* Perform serialization before starting operation */
        perform_serialization ();

        /* Obtain main-storage access lock */
        OBTAIN_MAINLOCK(regs);

        /* Fetch byte from operand address */
        obyte = vfetchb ( effective_addr, b1, regs );

        /* Set all bits of operand to ones */
        vstoreb ( 0xFF, effective_addr, b1, regs );

        /* Release main-storage access lock */
        RELEASE_MAINLOCK(regs);

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
        obyte = vfetchb ( effective_addr, b1, regs );

        /* AND with immediate operand */
        obyte &= ibyte;

        /* Store result at operand address */
        vstoreb ( obyte, effective_addr, b1, regs );

        /* Set condition code */
        regs->psw.cc = obyte ? 1 : 0;

        break;

    case 0x95:
    /*---------------------------------------------------------------*/
    /* CLI      Compare Logical Immediate                       [SI] */
    /*---------------------------------------------------------------*/

        /* Fetch byte from operand address */
        obyte = vfetchb ( effective_addr, b1, regs );

        /* Compare with immediate operand and set condition code */
        regs->psw.cc = (obyte < ibyte) ? 1 :
                      (obyte > ibyte) ? 2 : 0;

        break;

    case 0x96:
    /*---------------------------------------------------------------*/
    /* OI       Or Immediate                                    [SI] */
    /*---------------------------------------------------------------*/

        /* Fetch byte from operand address */
        obyte = vfetchb ( effective_addr, b1, regs );

        /* OR with immediate operand */
        obyte |= ibyte;

        /* Store result at operand address */
        vstoreb ( obyte, effective_addr, b1, regs );

        /* Set condition code */
        regs->psw.cc = obyte ? 1 : 0;

        break;

    case 0x97:
    /*---------------------------------------------------------------*/
    /* XI       Exclusive Or Immediate                          [SI] */
    /*---------------------------------------------------------------*/

        /* Fetch byte from operand address */
        obyte = vfetchb ( effective_addr, b1, regs );

        /* XOR with immediate operand */
        obyte ^= ibyte;

        /* Store result at operand address */
        vstoreb ( obyte, effective_addr, b1, regs );

        /* Set condition code */
        regs->psw.cc = obyte ? 1 : 0;

        break;

    case 0x98:
    /*---------------------------------------------------------------*/
    /* LM       Load Multiple                                   [RS] */
    /*---------------------------------------------------------------*/

        /* Calculate the number of bytes to be loaded */
        d = (((r3 < r1) ? r3 + 16 - r1 : r3 - r1) + 1) * 4;

        /* Fetch new register contents from operand address */
        vfetchc ( cwork1, d-1, effective_addr, b1, regs );

        /* Load registers from work area */
        for ( n = r1, d = 0; ; )
        {
            /* Load one register from work area */
            regs->gpr[n] = (cwork1[d] << 24) | (cwork1[d+1] << 16)
                            | (cwork1[d+2] << 8) | cwork1[d+3];
            d += 4;

            /* Instruction is complete when r3 register is done */
            if ( n == r3 ) break;

            /* Update register number, wrapping from 15 to 0 */
            n++; n &= 15;
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
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Program check if operand is not on a fullword boundary */
        if ( effective_addr & 0x00000003 )
        {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Exit if explicit tracing (control reg 12 bit 31) is off */
        if ( (regs->cr[12] & CR12_EXTRACE) == 0 )
            break;

        /* Fetch the trace operand */
        n2 = vfetch4 ( effective_addr, b1, regs );

        /* Exit if bit zero of the trace operand is one */
        if ( (n2 & 0x80000000) )
            break;

        /* Perform serialization and checkpoint-synchronization */
        perform_serialization ();
        perform_chkpt_sync ();

        /* Add a new trace table entry and update CR12 */
        trace_tr (n2, r1, r3, regs);

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
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Calculate the number of bytes to be loaded */
        d = (((r3 < r1) ? r3 + 16 - r1 : r3 - r1) + 1) * 4;

        /* Fetch new access register contents from operand address */
        vfetchc ( cwork1, d-1, effective_addr, b1, regs );

        /* Load access registers from work area */
        for ( n = r1, d = 0; ; )
        {
            /* Load one access register from work area */
            regs->ar[n] = (cwork1[d] << 24) | (cwork1[d+1] << 16)
                        | (cwork1[d+2] << 8) | cwork1[d+3];
            d += 4;

            /* Instruction is complete when r3 register is done */
            if ( n == r3 ) break;

            /* Update register number, wrapping from 15 to 0 */
            n++; n &= 15;
        }

        break;

    case 0x9B:
    /*---------------------------------------------------------------*/
    /* STAM     Store Access Multiple                           [RS] */
    /*---------------------------------------------------------------*/

        /* Program check if operand is not on a fullword boundary */
        if ( effective_addr & 0x00000003 )
        {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Copy access registers into work area */
        for ( n = r1, d = 0; ; )
        {
            /* Copy contents of one access register to work area */
            cwork1[d++] = (regs->ar[n] & 0xFF000000) >> 24;
            cwork1[d++] = (regs->ar[n] & 0xFF0000) >> 16;
            cwork1[d++] = (regs->ar[n] & 0xFF00) >> 8;
            cwork1[d++] = regs->ar[n] & 0xFF;

            /* Instruction is complete when r3 register is done */
            if ( n == r3 ) break;

            /* Update register number, wrapping from 15 to 0 */
            n++; n &= 15;
        }

        /* Store access register contents at operand address */
        vstorec ( cwork1, d-1, effective_addr, b1, regs );

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
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
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
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
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
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Test for pending interrupt and set condition code */
        regs->psw.cc = test_channel (regs, effective_addr & 0xFF00);

        break;
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_IMMEDIATE_AND_RELATIVE
    case 0xA7:
    /*---------------------------------------------------------------*/
    /* Register Immediate instructions (opcode A7xx)                 */
    /*---------------------------------------------------------------*/

        /* Bits 12-15 of instruction determine the operation code */
        switch (r3) {

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
                program_check (regs, PGM_FIXED_POINT_OVERFLOW_EXCEPTION);
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

        default:
        /*-----------------------------------------------------------*/
        /* A7xx: Invalid operation                                   */
        /*-----------------------------------------------------------*/
            program_check (regs, PGM_OPERATION_EXCEPTION);
            goto terminate;

        } /* end switch(r3) */

        break;
#endif /*FEATURE_IMMEDIATE_AND_RELATIVE*/

#ifdef FEATURE_COMPARE_AND_MOVE_EXTENDED
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
#endif /*FEATURE_COMPARE_AND_MOVE_EXTENDED*/

    case 0xAC:
    /*---------------------------------------------------------------*/
    /* STNSM    Store Then And System Mask                      [SI] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Store current system mask value into storage operand */
        vstoreb ( regs->psw.sysmask, effective_addr, b1, regs );

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
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Store current system mask value into storage operand */
        vstoreb ( regs->psw.sysmask, effective_addr, b1, regs );

        /* OR system mask with immediate operand */
        regs->psw.sysmask |= ibyte;

        /* For ECMODE, bits 0 and 2-4 of system mask must be zero */
        if (regs->psw.ecmode && (regs->psw.sysmask & 0xB8) != 0)
        {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
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
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Perform serialization before starting operation */
        perform_serialization ();

        /* Signal processor and set condition code */
        regs->psw.cc =
            signal_processor (r1, r3, effective_addr, regs);

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
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
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
        program_check (regs, PGM_MONITOR_EVENT);

        break;

    case 0xB1:
    /*---------------------------------------------------------------*/
    /* LRA      Load Real Address                               [RX] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Apply indexing to the effective address */
        if (x2 != 0)
        {
            effective_addr += regs->gpr[x2];
            effective_addr &= ADDRESS_MAXWRAP(regs);
        }

        /* Translate the effective address to a real address */
        cc = translate_addr (effective_addr, b1, regs, ACCTYPE_LRA,
                &n, &xcode, &private, &protect, &stid, NULL, NULL);

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

        /* Extract RRE R1/R2 fields from instruction byte 3 */
        r1 = inst[3] >> 4;
        r2 = inst[3] & 0x0F;

        /* The immediate byte determines the instruction opcode */
        switch ( ibyte ) {

        case 0x02:
        /*-----------------------------------------------------------*/
        /* B202: STIDP - Store CPU ID                            [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on doubleword boundary */
            if ( effective_addr & 0x00000007 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Store CPU ID at operand address */
            vstore8 ( sysblk.cpuid, effective_addr, b1, regs );

            break;

#ifdef FEATURE_S370_CHANNEL
        case 0x03:
        /*-----------------------------------------------------------*/
        /* B203: STIDC - Store Channel ID                        [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Store Channel ID and set condition code */
            regs->psw.cc =
                store_channel_id (regs, effective_addr & 0xFF00);

            break;
#endif /*FEATURE_S370_CHANNEL*/

        case 0x04:
        /*-----------------------------------------------------------*/
        /* B204: SCK - Set Clock                                 [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on doubleword boundary */
            if ( effective_addr & 0x00000007 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Fetch new TOD clock value from operand address */
            dreg = vfetch8 ( effective_addr, b1, regs);

            /* Obtain the TOD clock update lock */
            obtain_lock (&sysblk.todlock);

            /* Compute the new TOD clock offset in microseconds */
            sysblk.todoffset += (dreg >> 12) - (sysblk.todclk >> 4);

            /* Set the new TOD clock value */
            sysblk.todclk = (dreg & 0xFFFFFFFFFFFFF000ULL) >> 8;

            /* Reset the TOD clock uniqueness value */
            sysblk.toduniq = 0;

            /* Release the TOD clock update lock */
            release_lock (&sysblk.todlock);

//          /*debug*/logmsg("Set TOD clock=%16.16llX\n", dreg);

            /* Return condition code zero */
            regs->psw.cc = 0;

            break;

        case 0x05:
        /*-----------------------------------------------------------*/
        /* B205: STCK - Store Clock                              [S] */
        /*-----------------------------------------------------------*/

            /* Perform serialization before fetching clock */
            perform_serialization ();

            /* Obtain the TOD clock update lock */
            obtain_lock (&sysblk.todlock);

            /* Retrieve the TOD clock value and shift out the epoch */
            dreg = sysblk.todclk << 8;

            /* Insert the uniqueness value in bits 52-63 */
            dreg |= (sysblk.toduniq & 0xFFF);

            /* Increment the TOD clock uniqueness value */
            sysblk.toduniq++;

            /* Release the TOD clock update lock */
            release_lock (&sysblk.todlock);

//          /*debug*/logmsg("Store TOD clock=%16.16llX\n", dreg);

            /* Store TOD clock value at operand address */
            vstore8 ( dreg, effective_addr, b1, regs );

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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on doubleword boundary */
            if ( effective_addr & 0x00000007 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Fetch clock comparator value from operand location */
            dreg = vfetch8 ( effective_addr, b1, regs )
                        & 0xFFFFFFFFFFFFF000ULL;

//          /*debug*/logmsg("Set clock comparator=%16.16llX\n", dreg);

            /* Obtain the TOD clock update lock */
            obtain_lock (&sysblk.todlock);

            /* Update the clock comparator and set epoch to zero */
            regs->clkc = dreg >> 8;

            /* Release the TOD clock update lock */
            release_lock (&sysblk.todlock);

            break;

        case 0x07:
        /*-----------------------------------------------------------*/
        /* B207: STCKC - Store Clock Comparator                  [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on doubleword boundary */
            if ( effective_addr & 0x00000007 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Obtain the TOD clock update lock */
            obtain_lock (&sysblk.todlock);

            /* Save clock comparator value and shift out the epoch */
            dreg = regs->clkc << 8;

            /* Release the TOD clock update lock */
            release_lock (&sysblk.todlock);

            /* Store clock comparator value at operand location */
            vstore8 ( dreg, effective_addr, b1, regs );

//          /*debug*/logmsg("Store clock comparator=%16.16llX\n", dreg);

            break;

        case 0x08:
        /*-----------------------------------------------------------*/
        /* B208: SPT - Set CPU Timer                             [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on doubleword boundary */
            if ( effective_addr & 0x00000007 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Fetch the CPU timer value from operand location */
            dreg = vfetch8 ( effective_addr, b1, regs )
                        & 0xFFFFFFFFFFFFF000ULL;

            /* Obtain the TOD clock update lock */
            obtain_lock (&sysblk.todlock);

            /* Update the CPU timer */
            regs->ptimer = dreg;

            /* Release the TOD clock update lock */
            release_lock (&sysblk.todlock);

//          /*debug*/logmsg("Set CPU timer=%16.16llX\n", dreg);

            break;

        case 0x09:
        /*-----------------------------------------------------------*/
        /* B209: STPT - Store CPU Timer                          [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on doubleword boundary */
            if ( effective_addr & 0x00000007 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Obtain the TOD clock update lock */
            obtain_lock (&sysblk.todlock);

            /* Save the CPU timer value */
            dreg = regs->ptimer;

            /* Release the TOD clock update lock */
            release_lock (&sysblk.todlock);

            /* Store CPU timer value at operand location */
            vstore8 ( dreg, effective_addr, b1, regs );

//          /*debug*/logmsg("Store CPU timer=%16.16llX\n", dreg);

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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on fullword boundary */
            if ( effective_addr & 0x00000003 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Perform serialization before fetching the operand */
            perform_serialization ();

            /* Load new prefix value from operand address */
            n = vfetch4 ( effective_addr, b1, regs );

            /* Isolate bits 1-19 of new prefix value */
            n &= 0x7FFFF000;

            /* Program check if prefix is invalid absolute address */
            if ( n >= sysblk.mainsize )
            {
                program_check (regs, PGM_ADDRESSING_EXCEPTION);
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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on fullword boundary */
            if ( effective_addr & 0x00000003 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Store prefix register at operand address */
            vstore4 ( regs->pxr, effective_addr, b1, regs );

            break;

        case 0x12:
        /*-----------------------------------------------------------*/
        /* B212: STAP - Store CPU Address                        [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on halfword boundary */
            if ( effective_addr & 0x00000001 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Store CPU address at operand address */
            vstore2 ( regs->cpuad, effective_addr, b1, regs );

            break;

#ifdef FEATURE_BASIC_STORAGE_KEYS
        case 0x13:
        /*-----------------------------------------------------------*/
        /* B213: RRB - Reset Reference Bit                       [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load 2K block real address from operand address */
            n = effective_addr & 0x00FFF800;

            /* Convert real address to absolute address */
            n = APPLY_PREFIXING (n, regs->pxr);

            /* Addressing exception if block is outside main storage */
            if ( n >= sysblk.mainsize )
            {
                program_check (regs, PGM_ADDRESSING_EXCEPTION);
                goto terminate;
            }

            /* Set the condition code according to the original state
               of the reference and change bits in the storage key */
            regs->psw.cc =
               ((STORAGE_KEY(n) & STORKEY_REF) ? 2 : 0)
               | ((STORAGE_KEY(n) & STORKEY_CHANGE) ? 1 : 0);

            /* Reset the reference bit in the storage key */
            STORAGE_KEY(n) &= ~(STORKEY_REF);

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
                program_check (regs, PGM_SPACE_SWITCH_EVENT);
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
                program_check (regs, PGM_SPACE_SWITCH_EVENT);

            break;
#endif /*FEATURE_DUAL_ADDRESS_SPACE*/

        case 0x1A:
        /*-----------------------------------------------------------*/
        /* B21A: CFC - Compare and Form Codeword                 [S] */
        /*-----------------------------------------------------------*/

            /* Compare and form codeword and set condition code */
            regs->psw.cc =
                compare_and_form_codeword (regs, effective_addr);

            break;

        case 0x20:
        /*-----------------------------------------------------------*/
        /* B220: SERVC - Service Call                          [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* R1 is SCLP command word */
            n1 = regs->gpr[r1];

            /* R2 is real address of service call control block */
            n2 = regs->gpr[r2];

            /* Call service processor and set condition code */
            regs->psw.cc = service_call ( n1, n2, regs );

            break;

        case 0x21:
        /*-----------------------------------------------------------*/
        /* B221: IPTE - Invalidate Page Table Entry            [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Perform serialization before operation */
            perform_serialization ();

            /* Invalidate page table entry */
            invalidate_pte (ibyte, r1, r2, regs);

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
                program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Privileged operation exception if in problem state
               and the extraction-authority control bit is zero */
            if ( regs->psw.prob
                 && (regs->cr[0] & CR0_EXT_AUTH) == 0 )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load virtual storage address from R2 register */
            effective_addr = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

            /* Translate virtual address to real address */
            if (translate_addr (effective_addr, r2, regs, ACCTYPE_IVSK,
                &n, &xcode, &private, &protect, &stid, NULL, NULL))
            {
                program_check (regs, xcode);
                goto terminate;
            }

            /* Convert real address to absolute address */
            n = APPLY_PREFIXING (n, regs->pxr);

            /* Addressing exception if block is outside main storage */
            if ( n >= sysblk.mainsize )
            {
                program_check (regs, PGM_ADDRESSING_EXCEPTION);
                goto terminate;
            }

            /* Insert the storage key into R1 register bits 24-31 */
            regs->gpr[r1] &= 0xFFFFFF00;
            regs->gpr[r1] |= (STORAGE_KEY(n) & 0xFE);

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
                program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Privileged operation exception if in problem state
               and the extraction-authority control bit is zero */
            if ( regs->psw.prob
                 && (regs->cr[0] & CR0_EXT_AUTH) == 0 )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
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
                program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Privileged operation exception if in problem state
               and the extraction-authority control bit is zero */
            if ( regs->psw.prob
                 && (regs->cr[0] & CR0_EXT_AUTH) == 0 )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
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

            /* Set new PKM, PASN, SASN, PSTD, SSTD, amode,
               instruction address, and problem state bit */
            rc = program_transfer (r1, r2, regs);

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Generate space switch event if required */
            if (rc) {
                program_check (regs, PGM_SPACE_SWITCH_EVENT);
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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load 4K block address from R2 register */
            n = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

            /* Convert real address to absolute address */
            n = APPLY_PREFIXING (n, regs->pxr);

            /* Addressing exception if block is outside main storage */
            if ( n >= sysblk.mainsize )
            {
                program_check (regs, PGM_ADDRESSING_EXCEPTION);
                goto terminate;
            }

            /* Insert the storage key into R1 register bits 24-31 */
            regs->gpr[r1] &= 0xFFFFFF00;
            regs->gpr[r1] |= (STORAGE_KEY(n) & 0xFE);

            break;

        case 0x2A:
        /*-----------------------------------------------------------*/
        /* B22A: RRBE - Reset Reference Bit Extended           [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load 4K block address from R2 register */
            n = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

            /* Convert real address to absolute address */
            n = APPLY_PREFIXING (n, regs->pxr);

            /* Addressing exception if block is outside main storage */
            if ( n >= sysblk.mainsize )
            {
                program_check (regs, PGM_ADDRESSING_EXCEPTION);
                goto terminate;
            }

            /* Set the condition code according to the original state
               of the reference and change bits in the storage key */
            regs->psw.cc =
               ((STORAGE_KEY(n) & STORKEY_REF) ? 2 : 0)
               | ((STORAGE_KEY(n) & STORKEY_CHANGE) ? 1 : 0);

            /* Reset the reference bit in the storage key */
            STORAGE_KEY(n) &= ~(STORKEY_REF);

            break;

        case 0x2B:
        /*-----------------------------------------------------------*/
        /* B22B: SSKE - Set Storage Key Extended               [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load 4K block address from R2 register */
            n = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Convert real address to absolute address */
            n = APPLY_PREFIXING (n, regs->pxr);

            /* Addressing exception if block is outside main storage */
            if ( n >= sysblk.mainsize )
            {
                program_check (regs, PGM_ADDRESSING_EXCEPTION);
                goto terminate;
            }

            /* Update the storage key from R1 register bits 24-30 */
            STORAGE_KEY(n) &= STORKEY_BADFRM;
            STORAGE_KEY(n) |= regs->gpr[r1] & ~(STORKEY_BADFRM);

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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load 4K block address from R2 register */
            n = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);
            n &= 0xFFFFF000;

            /* Perform serialization */
            perform_serialization ();

            /* Addressing exception if block is outside main storage */
            if ( n >= sysblk.mainsize )
            {
                program_check (regs, PGM_ADDRESSING_EXCEPTION);
                goto terminate;
            }

            /* Protection exception if low-address protection is set */
            if ( n == 0 && (regs->cr[0] & CR0_LOW_PROT) )
            {
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
                regs->tea = (n & TEA_EFFADDR);
                regs->excarid = 0;
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
                program_check (regs, PGM_PROTECTION_EXCEPTION);
                goto terminate;
            }

            /* Convert real address to absolute address */
            n = APPLY_PREFIXING (n, regs->pxr);

            /* Clear the 4K block to zeroes */
            memset (sysblk.mainstor + n, 0x00, 4096);

            /* Set condition code 0 if storage usable, 1 if unusable */
            if (STORAGE_KEY(n) & STORKEY_BADFRM)
                regs->psw.cc = 1;
            else
                regs->psw.cc = 0;

            /* Perform serialization */
            perform_serialization ();

            /* Clear general register 0 */
            regs->gpr[0] = 0;

            break;

#ifdef FEATURE_HEXADECIMAL_FLOATING_POINT
        case 0x2D:
        /*-----------------------------------------------------------*/
        /* B22D: DXR - Divide Float Extended Register          [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if R1 or R2 is not 0 or 4 */
            if (( r1 & 11 || r2 & 11)) {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            divide_float_ext_reg (r1, r2, regs);

            break;
#endif /*FEATURE_HEXADECIMAL_FLOATING_POINT*/

#ifdef FEATURE_EXPANDED_STORAGE
        case 0x2E:
        /*-----------------------------------------------------------*/
        /* B22E: PGIN - Page in from expanded storage          [RRE] */
        /*-----------------------------------------------------------*/

            /* Copy page from expanded storage and set cond code */
            regs->psw.cc = page_in (r1, r2, regs);

            break;

        case 0x2F:
        /*-----------------------------------------------------------*/
        /* B22F: PGOUT - Page out to expanded storage          [RRE] */
        /*-----------------------------------------------------------*/

            /* Copy page to expanded storage and set cond code */
            regs->psw.cc = page_out (r1, r2, regs);

            break;
#endif /*FEATURE_EXPANDED_STORAGE*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
        case 0x30:
        /*-----------------------------------------------------------*/
        /* B230: CSCH - Clear Subchannel                         [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if reg 1 bits 0-15 not X'0001' */
            if ( (regs->gpr[1] >> 16) != 0x0001 )
            {
                program_check (regs, PGM_OPERAND_EXCEPTION);
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

        case 0x31:
        /*-----------------------------------------------------------*/
        /* B231: HSCH - Halt Subchannel                          [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if reg 1 bits 0-15 not X'0001' */
            if ( (regs->gpr[1] >> 16) != 0x0001 )
            {
                program_check (regs, PGM_OPERAND_EXCEPTION);
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

            /* Perform halt subchannel and set condition code */
            regs->psw.cc = halt_subchan (regs, dev);

            break;

        case 0x32:
        /*-----------------------------------------------------------*/
        /* B232: MSCH - Modify Subchannel                        [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on fullword boundary */
            if ( effective_addr & 0x00000003 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Fetch the updated path management control word */
            vfetchc ( &pmcw, sizeof(PMCW)-1, effective_addr,
                        b1, regs );

            /* Program check if reserved bits are not zero */
            if (pmcw.flag4 & PMCW4_RESV
                || (pmcw.flag5 & PMCW5_LM) == PMCW5_LM_RESV
                || pmcw.flag24 != 0 || pmcw.flag25 != 0
                || pmcw.flag26 != 0 || (pmcw.flag27 & PMCW27_RESV))
            {
                program_check (regs, PGM_OPERAND_EXCEPTION);
                goto terminate;
            }

            /* Program check if reg 1 bits 0-15 not X'0001' */
            if ( (regs->gpr[1] >> 16) != 0x0001 )
            {
                program_check (regs, PGM_OPERAND_EXCEPTION);
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

            /* If the subchannel is invalid then return cc0 */
            if (!(dev->pmcw.flag5 & PMCW5_V))
            {
                regs->psw.cc = 0;
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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on fullword boundary */
            if ( effective_addr & 0x00000003 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Fetch the operation request block */
            vfetchc ( &orb, sizeof(ORB)-1, effective_addr,
                        b1, regs );

            /* Program check if reserved bits are not zero */
            if (orb.flag4 & ORB4_RESV
                || orb.flag5 & ORB5_RESV
                || orb.flag7 & ORB7_RESV
                || orb.ccwaddr[0] & 0x80)
            {
                program_check (regs, PGM_OPERAND_EXCEPTION);
                goto terminate;
            }

            /* Program check if incorrect length suppression */
            if (orb.flag7 & ORB7_L)
            {
                program_check (regs, PGM_OPERAND_EXCEPTION);
                goto terminate;
            }

            /* Program check if reg 1 bits 0-15 not X'0001' */
            if ( (regs->gpr[1] >> 16) != 0x0001 )
            {
                program_check (regs, PGM_OPERAND_EXCEPTION);
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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if reg 1 bits 0-15 not X'0001' */
            if ( (regs->gpr[1] >> 16) != 0x0001 )
            {
                program_check (regs, PGM_OPERAND_EXCEPTION);
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
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
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
                        b1, regs );

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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on fullword boundary */
            if ( effective_addr & 0x00000003 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if reg 1 bits 0-15 not X'0001' */
            if ( (regs->gpr[1] >> 16) != 0x0001 )
            {
                program_check (regs, PGM_OPERAND_EXCEPTION);
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
            vstorec ( &irb, sizeof(IRB)-1, effective_addr, b1, regs );

            break;

        case 0x36:
        /*-----------------------------------------------------------*/
        /* B236: TPI - Test Pending Interruption                 [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on fullword boundary */
            if ( effective_addr & 0x00000003 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Perform serialization and checkpoint-synchronization */
            perform_serialization ();
            perform_chkpt_sync ();

            /* Obtain the interrupt lock */
            obtain_lock (&sysblk.intlock);

            /* Test and clear pending interrupt, set condition code */
            regs->psw.cc =
                present_io_interrupt (regs, &ioid, &ioparm, NULL);

            /* Release the interrupt lock */
            release_lock (&sysblk.intlock);

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
                vstore8 ( dreg, effective_addr, b1, regs );
            }

            break;

        case 0x38:
        /*-----------------------------------------------------------*/
        /* B238: RSCH - Resume Subchannel                        [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if reg 1 bits 0-15 not X'0001' */
            if ( (regs->gpr[1] >> 16) != 0x0001 )
            {
                program_check (regs, PGM_OPERAND_EXCEPTION);
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

        case 0x39:
        /*-----------------------------------------------------------*/
        /* B239: STCRW - Store Channel Report Word               [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on a fullword boundary */
            if ( effective_addr & 0x00000003 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Obtain any pending channel report */
            n = channel_report();

            /* Store channel report word at operand address */
            vstore4 ( n, effective_addr, b1, regs );

            /* Indicate if channel report or zeros were stored */
            regs->psw.cc = (n == 0) ? 1 : 0;

            break;

        case 0x3A:
        /*-----------------------------------------------------------*/
        /* B23A: STCPS - Store Channel Path Status               [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on 32 byte boundary */
            if ( effective_addr & 0x0000001F )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /*INCOMPLETE, SET TO ALL ZEROS*/
            memset(cwork1,0x00,32);

            /* Store channel path status word at operand address */
            vstorec ( cwork1, 32-1, effective_addr, b1, regs );

            break;

        case 0x3C:
        /*-----------------------------------------------------------*/
        /* B23C: SCHM - Set Channel Monitor                      [S] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Reserved bits in gpr1 must be zero */
            if (regs->gpr[1] & CHM_GPR1_RESV)
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if M bit one and gpr2 address not on
               a 32 byte boundary or highorder bit set */
            if ((regs->gpr[1] & CHM_GPR1_M)
             && (regs->gpr[2] & CHM_GPR2_RESV))
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Set the measurement block origin address */
            if (regs->gpr[1] & CHM_GPR1_M)
            {
                sysblk.mbo = regs->gpr[2] & CHM_GPR2_MBO;
                sysblk.mbk = (regs->gpr[1] & CHM_GPR1_MBK) >> 24;
                sysblk.mbm = 1;
            }
            else
                sysblk.mbm = 0;

            sysblk.mbd = regs->gpr[1] & CHM_GPR1_D;

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

#ifdef FEATURE_TRACING
            /* Form the branch trace entry */
            if((regs->cr[12] & CR12_BRTRACE) && (r2 != 0))
                n = trace_br(regs->psw.amode, regs->gpr[r2], regs);
#endif /*FEATURE_TRACING*/

            /* Form the linkage stack entry */
            form_stack_entry (LSED_UET_BAKR, n1, n2, regs);

#ifdef FEATURE_TRACING
            /* Update CR12 to reflect the new branch trace entry */
            if((regs->cr[12] & CR12_BRTRACE) && (r2 != 0))
                regs->cr[12] = n;
#endif /*FEATURE_TRACING*/

            /* Execute the branch unless R2 specifies register 0 */
            if ( r2 != 0 )
            {
                newia = regs->gpr[r2];
                goto setia;
            } /* end if(r2!=0) */

            break;
#endif /*FEATURE_LINKAGE_STACK*/

#ifdef FEATURE_CHECKSUM_INSTRUCTION
        case 0x41:
        /*-----------------------------------------------------------*/
        /* B241: CKSM - Checksum                               [RRE] */
        /*-----------------------------------------------------------*/

            /* Compute checksum and set condition code */
            regs->psw.cc =
                compute_checksum (r1, r2, regs);

            break;
#endif /*FEATURE_CHECKSUM_INSTRUCTION*/

        case 0x46:
        /*-----------------------------------------------------------*/
        /* B246: STURA - Store Using Real Address              [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* R2 register contains operand real storage address */
            n = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

            /* Program check if operand not on fullword boundary */
            if ( n & 0x00000003 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* R2 register contains operand real storage address */
            n = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

            /* Program check if operand not on fullword boundary */
            if ( n & 0x00000003 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
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
                program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
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

#ifdef FEATURE_BROADCASTED_PURGING
        case 0x50:
        /*-----------------------------------------------------------*/
        /* B250: CSP - Compare and Swap and Purge              [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if r1 is odd */
            if ( r1 & 1 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Perform serialization before starting operation */
            perform_serialization ();

            /* Obtain main-storage access lock */
            OBTAIN_MAINLOCK(regs);

            /* Obtain 2nd operand address from r2 */
            n2 = regs->gpr[r2] & 0x7FFFFFFC & ADDRESS_MAXWRAP(regs);

            /* Load second operand from operand address  */
            n = vfetch4 ( n2, r2, regs );

            /* Compare operand with R1 register contents */
            if ( regs->gpr[r1] == n )
            {
                /* If equal, store R1+1 at operand location and set cc=0 */
                vstore4 ( regs->gpr[r1+1], n2, r2, regs );
                regs->psw.cc = 0;

                /* Purge the TLB if bit 31 of r2 register is set */
                if (regs->gpr[r2] & 0x00000001)
                {
#if MAX_CPU_ENGINES == 1
                    purge_tlb(regs);
#else /*!MAX_CPU_ENGINES == 1*/
                    issue_broadcast_request(&sysblk.brdcstptlb);
#endif /*!MAX_CPU_ENGINES == 1*/
                }

                /* Purge the ALB if bit 30 of r2 register is set */
                if (regs->gpr[r2] & 0x00000002)
                {
#if MAX_CPU_ENGINES == 1
                    purge_alb(regs);
#else /*!MAX_CPU_ENGINES == 1*/
                    issue_broadcast_request(&sysblk.brdcstpalb);
#endif /*!MAX_CPU_ENGINES == 1*/
                }

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
            perform_serialization ();

            break;
#endif /*FEATURE_BROADCASTED_PURGING*/

        case 0x52:
        /*-----------------------------------------------------------*/
        /* B252: MSR - Multiply Single Register                [RRE] */
        /*-----------------------------------------------------------*/

            /* Multiply signed registers ignoring overflow */
            (S32)regs->gpr[r1] *= (S32)regs->gpr[r2];

            break;

#ifdef FEATURE_MOVE_PAGE_FACILITY_2
        case 0x54:
        /*-----------------------------------------------------------*/
        /* B254: MVPG - Move Page                              [RRE] */
        /*-----------------------------------------------------------*/

            /* Perform move page and set condition code */
            regs->psw.cc = move_page (r1, r2, regs);

            break;
#endif /*FEATURE_MOVE_PAGE_FACILITY_2*/

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

#ifdef FEATURE_MOVE_PAGE_FACILITY_2
        case 0x59:
        /*-----------------------------------------------------------*/
        /* B259: IESBE - Invalidate Expanded Storage Blk Entry [RRE] */
        /*-----------------------------------------------------------*/

            /* Program check if in problem state */
            if ( regs->psw.prob )
            {
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Perform serialization before operation */
            perform_serialization ();

            /* Update page table entry interlocked */
            OBTAIN_MAINLOCK(regs);

            /* Invalidate page table entry */
            invalidate_pte (ibyte, r1, r2, regs);

            /* Release mainstore interlock */
            RELEASE_MAINLOCK(regs);

            /* Perform serialization after operation */
            perform_serialization ();

            break;
#endif /*FEATURE_MOVE_PAGE_FACILITY_2*/

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

#if 0
        case 0x65:
        /*-----------------------------------------------------------*/
        /* B265: ???? - Coupling Facility                      [???] */
        /*-----------------------------------------------------------*/

            /* Set condition code 3 indicating no CF attached */
            regs->psw.cc = 3;

            break;
#endif

#ifdef FEATURE_EXTENDED_TOD_CLOCK
        case 0x78:
        /*-----------------------------------------------------------*/
        /* B278: STCKE - Store Clock Extended                    [S] */
        /*-----------------------------------------------------------*/

            /* Perform serialization before fetching clock */
            perform_serialization ();

            /* Obtain the TOD clock update lock */
            obtain_lock (&sysblk.todlock);

            /* Retrieve the TOD epoch, clock bits 0-51, and 4 zeroes */
            dreg = sysblk.todclk;

            /* Load and increment the TOD clock uniqueness value */
            n = sysblk.toduniq++;

            /* Release the TOD clock update lock */
            release_lock (&sysblk.todlock);

            /* Check that all 16 bytes of the operand are accessible */
            validate_operand (effective_addr, b1, 15,
                                ACCTYPE_WRITE, regs);

//          /*debug*/logmsg("Store TOD clock extended: +0=%16.16llX\n",
//          /*debug*/       dreg);

            /* Insert bits 20-23 of the TOD uniqueness value */
            dreg |= (U64)((n & 0x00000F00) >> 8);

            /* Store the 8 bit TOD epoch, clock bits 0-51, and bits
               20-23 of the TOD uniqueness value at operand address */
            vstore8 ( dreg, effective_addr, b1, regs );

            /* Build second doubleword of operand using bits 24-31
               of the TOD clock uniqueness value, followed by a
               40-bit non-zero value, followed by the 16 bit TOD
               programmable field from the TOD programmable register */
            dreg = ((U64)(n & 0xFF) << 56)
                 | 0x0000000000FF0000ULL
                 | (U64)(regs->todpr & 0xFFFF);

//          /*debug*/logmsg("Store TOD clock extended: +8=%16.16llX\n",
//          /*debug*/       dreg);

            /* Store second doubleword value at operand+8 */
            effective_addr += 8;
            effective_addr &= ADDRESS_MAXWRAP(regs);
            vstore8 ( dreg, effective_addr + 8, b1, regs );

            /* Perform serialization after storing clock */
            perform_serialization ();

            /* Set condition code zero */
            regs->psw.cc = 0;

            break;
#endif /*FEATURE_EXTENDED_TOD_CLOCK*/

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
                program_check (regs, PGM_SPACE_SWITCH_EVENT);

            break;
#endif /*FEATURE_DUAL_ADDRESS_SPACE*/

        case 0xF0:
        /*-----------------------------------------------------------*/
        /* B2F0: IUCV - Inter User Communications Vehicle        [S] */
        /*-----------------------------------------------------------*/

            /* Set condition code to indicate IUCV not available */
            regs->psw.cc = 3;

            break;

        default:
        /*-----------------------------------------------------------*/
        /* B2xx: Invalid operation                                   */
        /*-----------------------------------------------------------*/
            program_check (regs, PGM_OPERATION_EXCEPTION);
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
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Program check if operand is not on a fullword boundary */
        if ( effective_addr & 0x00000003 )
        {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Copy control registers into work area */
        for ( n = r1, d = 0; ; )
        {
            /* Copy contents of one control register to work area */
            cwork1[d++] = (regs->cr[n] & 0xFF000000) >> 24;
            cwork1[d++] = (regs->cr[n] & 0xFF0000) >> 16;
            cwork1[d++] = (regs->cr[n] & 0xFF00) >> 8;
            cwork1[d++] = regs->cr[n] & 0xFF;

            /* Instruction is complete when r3 register is done */
            if ( n == r3 ) break;

            /* Update register number, wrapping from 15 to 0 */
            n++; n &= 15;
        }

        /* Store control register contents at operand address */
        vstorec ( cwork1, d-1, effective_addr, b1, regs );

        break;

    case 0xB7:
    /*---------------------------------------------------------------*/
    /* LCTL     Load Control                                    [RS] */
    /*---------------------------------------------------------------*/

        /* Program check if in problem state */
        if ( regs->psw.prob )
        {
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Program check if operand is not on a fullword boundary */
        if ( effective_addr & 0x00000003 )
        {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Calculate the number of bytes to be loaded */
        d = (((r3 < r1) ? r3 + 16 - r1 : r3 - r1) + 1) * 4;

        /* Fetch new control register contents from operand address */
        vfetchc ( cwork1, d-1, effective_addr, b1, regs );

        /* Load control registers from work area */
        for ( n = r1, d = 0; ; )
        {
            /* Load one control register from work area */
            regs->cr[n] = (cwork1[d] << 24) | (cwork1[d+1] << 16)
                        | (cwork1[d+2] << 8) | cwork1[d+3];
            d += 4;

            /* Instruction is complete when r3 register is done */
            if ( n == r3 ) break;

            /* Update register number, wrapping from 15 to 0 */
            n++; n &= 15;
        }

        break;

    case 0xBA:
    /*---------------------------------------------------------------*/
    /* CS       Compare and Swap                                [RS] */
    /*---------------------------------------------------------------*/

        /* Program check if operand is not on a fullword boundary */
        if ( effective_addr & 0x00000003 )
        {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Perform serialization before starting operation */
        perform_serialization ();

        /* Obtain main-storage access lock */
        OBTAIN_MAINLOCK(regs);

        /* Load second operand from operand address  */
        n = vfetch4 ( effective_addr, b1, regs );

        /* Compare operand with R1 register contents */
        if ( regs->gpr[r1] == n )
        {
            /* If equal, store R3 at operand location and set cc=0 */
            vstore4 ( regs->gpr[r3], effective_addr, b1, regs );
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
        perform_serialization ();

        break;

    case 0xBB:
    /*---------------------------------------------------------------*/
    /* CDS      Compare Double and Swap                         [RS] */
    /*---------------------------------------------------------------*/

        /* Program check if either R1 or R3 is odd */
        if ( ( r1 & 1 ) || ( r3 & 1 ) )
        {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Program check if operand is not on a doubleword boundary */
        if ( effective_addr & 0x00000007 )
        {
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
            goto terminate;
        }

        /* Perform serialization before starting operation */
        perform_serialization ();

        /* Obtain main-storage access lock */
        OBTAIN_MAINLOCK(regs);

        /* Load second operand from operand address  */
        n1 = vfetch4 ( effective_addr, b1, regs );
        n2 = vfetch4 ( effective_addr + 4, b1, regs );

        /* Compare doubleword operand with R1:R1+1 register contents */
        if ( regs->gpr[r1] == n1 && regs->gpr[r1+1] == n2 )
        {
            /* If equal, store R3:R3+1 at operand location and set cc=0 */
            vstore4 ( regs->gpr[r3], effective_addr, b1, regs );
            vstore4 ( regs->gpr[r3+1], effective_addr + 4, b1, regs );
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
        perform_serialization ();

        break;

    case 0xBD:
    /*---------------------------------------------------------------*/
    /* CLM      Compare Logical Characters under Mask           [RS] */
    /*---------------------------------------------------------------*/

        /* Clear the condition code */
        cc = 0;

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
                sbyte = vfetchb ( effective_addr++, b1, regs );

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

        break;

    case 0xBE:
    /*---------------------------------------------------------------*/
    /* STCM     Store Characters under Mask                     [RS] */
    /*---------------------------------------------------------------*/

        /* Load value from register */
        n = regs->gpr[r1];

        /* Copy characters from register to work area */
        for ( i = 0, j = 0; i < 4; i++ )
        {
            /* Test mask bit corresponding to this character */
            if ( r3 & 0x08 )
            {
                /* Copy character from register to work area */
                cwork1[j++] = n >> 24;
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
            validate_operand (effective_addr, b1, 0,
                                ACCTYPE_WRITE, regs);
            break;
        }

        /* Store result at operand location */
        vstorec ( cwork1, j-1, effective_addr, b1, regs );

        break;

    case 0xBF:
    /*---------------------------------------------------------------*/
    /* ICM      Insert Characters under Mask                    [RS] */
    /*---------------------------------------------------------------*/

        /* Clear condition code */
        cc = 0;

        /* If the mask is all zero, we must nevertheless load one
           byte from the storage operand, because POP requires us
           to recognize an access exception on the first byte */
        if ( r3 == 0 )
        {
            sbyte = vfetchb ( effective_addr, b1, regs );
            regs->psw.cc = 0;
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
                sbyte = vfetchb ( effective_addr, b1, regs );

                /* If this is the first byte fetched then test the
                   high-order bit to determine the condition code */
                if ( (r3 & 0xF0) == 0 )
                    h = (sbyte & 0x80) ? 1 : 2;

                /* If byte is non-zero then set the condition code */
                if ( sbyte != 0 )
                     cc = h;

                /* Insert the byte into the register */
                dreg &= 0xFFFFFFFF00FFFFFFULL;
                dreg |= (U32)sbyte << 24;

                /* Increment the operand address */
                effective_addr++;
                effective_addr &= ADDRESS_MAXWRAP(regs);
            }

            /* Shift mask and register for next byte */
            r3 <<= 1;
            dreg <<= 8;

        } /* end for(i) */

        /* Load the register with the updated value */
        regs->gpr[r1] = dreg >> 32;

        /* Set condition code */
        regs->psw.cc = cc;

        break;

    case 0xD1:
    /*---------------------------------------------------------------*/
    /* MVN      Move Numerics                                   [SS] */
    /*---------------------------------------------------------------*/

        /* Copy low digits of source bytes to destination bytes */
        ss_operation (opcode, effective_addr, b1,
                effective_addr2, b2, ibyte, regs);
        break;

    case 0xD2:
    /*---------------------------------------------------------------*/
    /* MVC      Move Characters                                 [SS] */
    /*---------------------------------------------------------------*/

        /* Move characters using current addressing mode and key */
        move_chars (effective_addr, b1, regs->psw.pkey,
                effective_addr2, b2, regs->psw.pkey, ibyte, regs);
        break;

    case 0xD3:
    /*---------------------------------------------------------------*/
    /* MVZ      Move Zones                                      [SS] */
    /*---------------------------------------------------------------*/

        /* Copy high digits of source bytes to destination bytes */
        ss_operation (opcode, effective_addr, b1,
                effective_addr2, b2, ibyte, regs);
        break;

    case 0xD4:
    /*---------------------------------------------------------------*/
    /* NC       And Characters                                  [SS] */
    /*---------------------------------------------------------------*/

        /* Perform AND operation and set condition code */
        regs->psw.cc = ss_operation (opcode, effective_addr, b1,
                                effective_addr2, b2, ibyte, regs);
        break;

    case 0xD5:
    /*---------------------------------------------------------------*/
    /* CLC      Compare Logical Characters                      [SS] */
    /*---------------------------------------------------------------*/

        /* Fetch first and second operands into work areas */
        vfetchc ( cwork1, ibyte, effective_addr, b1, regs );
        vfetchc ( cwork2, ibyte, effective_addr2, b2, regs );

        /* Compare first operand with second operand */
        rc = memcmp (cwork1, cwork2, ibyte+1);

        /* Set the condition code */
        regs->psw.cc = (rc == 0) ? 0 : (rc < 0) ? 1 : 2;

        break;

    case 0xD6:
    /*---------------------------------------------------------------*/
    /* OC       Or Characters                                   [SS] */
    /*---------------------------------------------------------------*/

        /* Perform OR operation and set condition code */
        regs->psw.cc = ss_operation (opcode, effective_addr, b1,
                                effective_addr2, b2, ibyte, regs);
        break;

    case 0xD7:
    /*---------------------------------------------------------------*/
    /* XC       Exclusive Or Characters                         [SS] */
    /*---------------------------------------------------------------*/

        /* Perform XOR operation and set condition code */
        regs->psw.cc = ss_operation (opcode, effective_addr, b1,
                                effective_addr2, b2, ibyte, regs);
        break;

    case 0xD9:
    /*---------------------------------------------------------------*/
    /* MVCK     Move with Key                                   [SS] */
    /*---------------------------------------------------------------*/

        /* Load true length from R1 register */
        n = regs->gpr[r1];

        /* If the true length does not exceed 256, set condition code
           zero, otherwise set cc=3 and use effective length of 256 */
        if (n <= 256)
            cc = 0;
        else {
            cc = 3;
            n = 256;
        }

        /* Load source key from R3 register bits 24-27 */
        j = regs->gpr[r3] & 0xF0;

        /* Program check if in problem state and key mask in
           CR3 bits 0-15 is not 1 for the specified key */
        if ( regs->psw.prob
            && ((regs->cr[3] << (j >> 4)) & 0x80000000) == 0 )
        {
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Move characters using source key for second operand */
        if (n > 0)
            move_chars (effective_addr, b1, regs->psw.pkey,
                        effective_addr2, b2, j, n-1, regs);

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
            program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Load true length from R1 register */
        n = regs->gpr[r1];

        /* If the true length does not exceed 256, set condition code
           zero, otherwise set cc=3 and use effective length of 256 */
        if (n <= 256)
            cc = 0;
        else {
            cc = 3;
            n = 256;
        }

        /* Load secondary space key from R3 register bits 24-27 */
        j = regs->gpr[r3] & 0xF0;

        /* Program check if in problem state and key mask in
           CR3 bits 0-15 is not 1 for the specified key */
        if ( regs->psw.prob
            && ((regs->cr[3] << (j >> 4)) & 0x80000000) == 0 )
        {
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Move characters from secondary address space to primary
           address space using secondary key for second operand */
        if (n > 0)
            move_chars (effective_addr, USE_PRIMARY_SPACE,
                        regs->psw.pkey,
                        effective_addr2, USE_SECONDARY_SPACE,
                        j, n-1, regs);

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
            program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Load true length from R1 register */
        n = regs->gpr[r1];

        /* If the true length does not exceed 256, set condition code
           zero, otherwise set cc=3 and use effective length of 256 */
        if (n <= 256)
            cc = 0;
        else {
            cc = 3;
            n = 256;
        }

        /* Load secondary space key from R3 register bits 24-27 */
        j = regs->gpr[r3] & 0xF0;

        /* Program check if in problem state and key mask in
           CR3 bits 0-15 is not 1 for the specified key */
        if ( regs->psw.prob
            && ((regs->cr[3] << (j >> 4)) & 0x80000000) == 0 )
        {
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            goto terminate;
        }

        /* Move characters from primary address space to secondary
           address space using secondary key for first operand */
        if (n > 0)
            move_chars (effective_addr, USE_SECONDARY_SPACE, j,
                        effective_addr2, USE_PRIMARY_SPACE,
                        regs->psw.pkey, n-1, regs);

        /* Set condition code */
        regs->psw.cc = cc;

        break;

    case 0xDC:
    /*---------------------------------------------------------------*/
    /* TR       Translate                                       [SS] */
    /*---------------------------------------------------------------*/

        /* Validate the first operand for write access */
        validate_operand (effective_addr, b1, ibyte,
                        ACCTYPE_WRITE, regs);

        /* Fetch first operand into work area */
        vfetchc ( cwork1, ibyte, effective_addr, b1, regs );

        /* Determine the second operand range by scanning the
           first operand to find the bytes with the highest
           and lowest values */
        for ( i = 0, d = 255, h = 0; i <= ibyte; i++ )
        {
            if (cwork1[i] < d) d = cwork1[i];
            if (cwork1[i] > h) h = cwork1[i];
        }

        /* Validate the referenced portion of the second operand */
        n = (effective_addr2 + d) & ADDRESS_MAXWRAP(regs);
        validate_operand (n, b2, h-d, ACCTYPE_READ, regs);

        /* Process first operand from left to right, refetching
           second operand and storing the result byte by byte
           to ensure correct handling of overlapping operands */
        for ( i = 0; i <= ibyte; i++ )
        {
            /* Fetch byte from second operand */
            n = (effective_addr2 + cwork1[i]) & ADDRESS_MAXWRAP(regs);
            sbyte = vfetchb ( n, b2, regs );

            /* Store result at first operand address */
            vstoreb ( sbyte, effective_addr, b1, regs );

            /* Increment first operand address */
            effective_addr++;
            effective_addr &= ADDRESS_MAXWRAP(regs);

        } /* end for(i) */

        break;

    case 0xDD:
    /*---------------------------------------------------------------*/
    /* TRT      Translate and Test                              [SS] */
    /*---------------------------------------------------------------*/

        /* Clear condition code */
        cc = 0;

        /* Process first operand from left to right */
        for ( i = 0; i <= ibyte; i++ )
        {
            /* Fetch argument byte from first operand */
            dbyte = vfetchb ( effective_addr, b1, regs );

            /* Fetch function byte from second operand */
            sbyte = vfetchb ( effective_addr2 + dbyte, b2, regs );

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
                cc = (i == ibyte) ? 2 : 1;

                /* Terminate the operation at this point */
                break;

            } /* end if(sbyte) */

            /* Increment first operand address */
            effective_addr++;
            effective_addr &= ADDRESS_MAXWRAP(regs);

        } /* end for(i) */

        /* Update the condition code */
        regs->psw.cc = cc;

        break;

    case 0xDE:
    /*---------------------------------------------------------------*/
    /* EDIT     Edit                                            [SS] */
    /*---------------------------------------------------------------*/

        /* Edit packed decimal value and set condition code */
        regs->psw.cc =
            edit_packed (0, effective_addr, ibyte, b1,
                        effective_addr2, b2, regs);

        break;

    case 0xDF:
    /*---------------------------------------------------------------*/
    /* EDMK     Edit and Mark                                   [SS] */
    /*---------------------------------------------------------------*/

        /* Edit and mark packed decimal value and set condition code */
        regs->psw.cc =
            edit_packed (1, effective_addr, ibyte, b1,
                        effective_addr2, b2, regs);

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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Special operation exception if ASN translation control
               (bit 12 of control register 14) is zero */
            if ( (regs->cr[14] & CR14_ASN_TRAN) == 0 )
            {
                program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Program check if operand not on doubleword boundary */
            if ( effective_addr & 0x00000007 )
            {
                program_check (regs, PGM_SPECIFICATION_EXCEPTION);
                goto terminate;
            }

            /* Fetch PKM, SASN, AX, and PASN from first operand */
            dreg = vfetch8 ( effective_addr, b1, regs );
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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Load access key from operand 2 address bits 24-27 */
            dbyte = effective_addr2 & 0xF0;

            /* Test protection and set condition code */
            regs->psw.cc =
                test_prot (effective_addr, b1, regs, dbyte);

            break;

#ifdef FEATURE_MVS_ASSIST
        case 0x04:
        /*-----------------------------------------------------------*/
        /* E504: Obtain Local Lock                             [SSE] */
        /*-----------------------------------------------------------*/

            /* Perform serialization before starting operation */
            perform_serialization ();

            /* Call MVS assist to obtain lock */
            obtain_local_lock (effective_addr, b1,
                                effective_addr2, b2, regs);

            /* Perform serialization after completing operation */
            perform_serialization ();

            break;

        case 0x05:
        /*-----------------------------------------------------------*/
        /* E505: Release Local Lock                            [SSE] */
        /*-----------------------------------------------------------*/

            /* Call MVS assist to release lock */
            release_local_lock (effective_addr, b1,
                                effective_addr2, b2, regs);

            break;

        case 0x06:
        /*-----------------------------------------------------------*/
        /* E506: Obtain CMS Lock                               [SSE] */
        /*-----------------------------------------------------------*/

            /* Perform serialization before starting operation */
            perform_serialization ();

            /* Call MVS assist to obtain lock */
            obtain_cms_lock (effective_addr, b1,
                                effective_addr2, b2, regs);

            /* Perform serialization after completing operation */
            perform_serialization ();

            break;

        case 0x07:
        /*-----------------------------------------------------------*/
        /* E507: Release CMS Lock                              [SSE] */
        /*-----------------------------------------------------------*/

            /* Call MVS assist to release lock */
            release_cms_lock (effective_addr, b1,
                                effective_addr2, b2, regs);

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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Move characters using source key for second operand */
            move_chars (effective_addr, b1, regs->psw.pkey,
                        effective_addr2, b2, j, h, regs);

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
                program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
                goto terminate;
            }

            /* Move characters using destination key for operand 1 */
            move_chars (effective_addr, b1, j,
                        effective_addr2, b2, regs->psw.pkey,
                        h, regs);

            break;

        default:
        /*-----------------------------------------------------------*/
        /* E5xx: Invalid operation                                   */
        /*-----------------------------------------------------------*/
            program_check (regs, PGM_OPERATION_EXCEPTION);
            goto terminate;

        } /* end switch(ibyte) */
        break;

    case 0xE8:
    /*---------------------------------------------------------------*/
    /* MVCIN    Move Characters Inverse                         [SS] */
    /*---------------------------------------------------------------*/

        /* Validate the operands for addressing and protection */
        validate_operand (effective_addr, b1, ibyte,
                        ACCTYPE_WRITE, regs);
        n = (effective_addr2 - ibyte) & ADDRESS_MAXWRAP(regs);
        validate_operand (n, b2, ibyte, ACCTYPE_READ, regs);

        /* Process the destination operand from left to right,
           and the source operand from right to left */
        for ( i = 0; i <= ibyte; i++ )
        {
            /* Fetch a byte from the source operand */
            sbyte = vfetchb ( effective_addr2, b2, regs );

            /* Store the byte in the destination operand */
            vstoreb ( sbyte, effective_addr, b1, regs );

            /* Increment destination operand address */
            effective_addr++;
            effective_addr &= ADDRESS_MAXWRAP(regs);

            /* Decrement source operand address */
            effective_addr2--;
            effective_addr2 &= ADDRESS_MAXWRAP(regs);

        } /* end for(i) */

        break;

    case 0xF0:
    /*---------------------------------------------------------------*/
    /* SRP      Shift and Round Decimal                         [SS] */
    /*---------------------------------------------------------------*/

        /* Shift packed decimal operand and set condition code */
        regs->psw.cc =
            shift_and_round_packed (effective_addr, r1, b1, regs,
                                r2, effective_addr2 );

        break;

    case 0xF1:
    /*---------------------------------------------------------------*/
    /* MVO      Move with Offset                                [SS] */
    /*---------------------------------------------------------------*/

        /* Move shifted second operand to first operand */
        move_with_offset (effective_addr, r1, b1,
                        effective_addr2, r2, b2, regs);

        break;

    case 0xF2:
    /*---------------------------------------------------------------*/
    /* PACK     Pack                                            [SS] */
    /*---------------------------------------------------------------*/

        /* Pack second operand into first operand */
        zoned_to_packed (effective_addr, r1, b1,
                        effective_addr2, r2, b2, regs);

        break;

    case 0xF3:
    /*---------------------------------------------------------------*/
    /* UNPK     Unpack                                          [SS] */
    /*---------------------------------------------------------------*/

        /* Unpack second operand into first operand */
        packed_to_zoned (effective_addr, r1, b1,
                        effective_addr2, r2, b2, regs);

        break;

    case 0xF8:
    /*---------------------------------------------------------------*/
    /* ZAP      Zero and Add Decimal                            [SS] */
    /*---------------------------------------------------------------*/

        /* Copy packed decimal operand and set condition code */
        regs->psw.cc =
            zero_and_add_packed (effective_addr, r1, b1,
                                effective_addr2, r2, b2, regs );

        break;

    case 0xF9:
    /*---------------------------------------------------------------*/
    /* CP       Compare Decimal                                 [SS] */
    /*---------------------------------------------------------------*/

        /* Compare packed decimal operands and set condition code */
        regs->psw.cc =
            compare_packed (effective_addr, r1, b1,
                                effective_addr2, r2, b2, regs );

        break;

    case 0xFA:
    /*---------------------------------------------------------------*/
    /* AP       Add Decimal                                     [SS] */
    /*---------------------------------------------------------------*/

        /* Add packed decimal operands and set condition code */
        regs->psw.cc =
            add_packed (effective_addr, r1, b1,
                                effective_addr2, r2, b2, regs );

        break;

    case 0xFB:
    /*---------------------------------------------------------------*/
    /* SP       Subtract Decimal                                [SS] */
    /*---------------------------------------------------------------*/

        /* Subtract packed decimal operands and set condition code */
        regs->psw.cc =
            subtract_packed (effective_addr, r1, b1,
                                effective_addr2, r2, b2, regs );

        break;

    case 0xFC:
    /*---------------------------------------------------------------*/
    /* MP       Multiply Decimal                                [SS] */
    /*---------------------------------------------------------------*/

        /* Multiply packed decimal operands */
        multiply_packed (effective_addr, r1, b1,
                                effective_addr2, r2, b2, regs );

        break;

    case 0xFD:
    /*---------------------------------------------------------------*/
    /* DP       Divide Decimal                                  [SS] */
    /*---------------------------------------------------------------*/

        /* Divide packed decimal operands */
        divide_packed (effective_addr, r1, b1,
                                effective_addr2, r2, b2, regs );

        break;

    setia:
    /*---------------------------------------------------------------*/
    /* Set PSW instruction address from newia                        */
    /*---------------------------------------------------------------*/
        regs->psw.ia = newia & ADDRESS_MAXWRAP(regs);
        break;

    /*---------------------------------------------------------------*/
    /* Invalid instruction operation code                            */
    /*---------------------------------------------------------------*/
    default:
        program_check (regs, PGM_OPERATION_EXCEPTION);
    terminate:
        break;
    } /* end switch(opcode) */

} /* end function execute_instruction */

/*-------------------------------------------------------------------*/
/* Perform I/O interrupt if pending                                  */
/* Note: The caller MUST hold the interrupt lock (sysblk.intlock)    */
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

    /* Set the main storage reference and change bits */
    STORAGE_KEY(regs->pxr) |= (STORKEY_REF | STORKEY_CHANGE);

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
        program_check (regs, rc);
    }

} /* end function perform_io_interrupt */

/*-------------------------------------------------------------------*/
/* Perform Machine Check interrupt if pending                        */
/* Note: The caller MUST hold the interrupt lock (sysblk.intlock)    */
/*-------------------------------------------------------------------*/
static void perform_mck_interrupt (REGS *regs)
{
int     rc;                             /* Return code               */
PSA    *psa;                            /* -> Prefixed storage area  */
U64     mcic;                           /* Mach.check interrupt code */
U32     xdmg;                           /* External damage code      */
U32     fsta;                           /* Failing storage address   */

    /* Test and clear pending machine check interrupt */
    rc = present_mck_interrupt (regs, &mcic, &xdmg, &fsta);

    /* Exit if no machine check was presented */
    if (rc == 0) return;

    /* Set the main storage reference and change bits */
    STORAGE_KEY(regs->pxr) |= (STORKEY_REF | STORKEY_CHANGE);

    /* Point to the PSA in main storage */
    psa = (PSA*)(sysblk.mainstor + regs->pxr);

    /* Store registers in machine check save area */
    store_status(regs, regs->pxr);

    /* Set the extended logout area to zeros */
    memset(psa->storepsw, 0, 16);

    /* Store the machine check interrupt code at PSA+232 */
    psa->mckint[0] = mcic >> 56;
    psa->mckint[1] = (mcic >> 48) & 0xFF;
    psa->mckint[2] = (mcic >> 40) & 0xFF;
    psa->mckint[3] = (mcic >> 32) & 0xFF;
    psa->mckint[4] = (mcic >> 24) & 0xFF;
    psa->mckint[5] = (mcic >> 16) & 0xFF;
    psa->mckint[6] = (mcic >> 8) & 0xFF;
    psa->mckint[7] = mcic & 0xFF;

    /* Trace the machine check interrupt */
    if (sysblk.insttrace || sysblk.inststep)
        logmsg ("Machine Check code=%2.2X%2.2X%2.2X%2.2X."
                "%2.2X%2.2X%2.2X%2.2X\n",
                psa->mckint[0], psa->mckint[1],
                psa->mckint[2], psa->mckint[3],
                psa->mckint[4], psa->mckint[5],
                psa->mckint[6], psa->mckint[7]);

    /* Store the external damage code at PSA+244 */
    psa->xdmgcode[0] = xdmg > 24;
    psa->xdmgcode[1] = (xdmg > 16) & 0xFF;
    psa->xdmgcode[2] = (xdmg > 8) & 0xFF;
    psa->xdmgcode[3] = xdmg & 0xFF;

    /* Store the failing storage address at PSA+248 */
    psa->mcstorad[0] = fsta > 24;
    psa->mcstorad[1] = (fsta > 16) & 0xFF;
    psa->mcstorad[2] = (fsta > 8) & 0xFF;
    psa->mcstorad[3] = fsta & 0xFF;

    /* Store current PSW at PSA+X'30' */
    store_psw ( &(regs->psw), psa->mckold );

    /* Load new PSW from PSA+X'70' */
    rc = load_psw ( &(regs->psw), psa->mcknew );
    if ( rc )
    {
        program_check (regs, rc);
    }

} /* end function perform_mck_interrupt */

/*-------------------------------------------------------------------*/
/* CPU instruction execution thread                                  */
/*-------------------------------------------------------------------*/
void *cpu_thread (REGS *regs)
{
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
#define CPU_PRIORITY    15              /* CPU thread priority       */

    /* Display thread started message on control panel */
    logmsg ("HHC620I CPU%d thread started: tid=%8.8lX, pid=%d, "
            "priority=%d\n",
            regs->cpuad, thread_id(), getpid(),
            getpriority(PRIO_PROCESS,0));

    /* Set CPU thread priority */
    if (setpriority(PRIO_PROCESS, 0, CPU_PRIORITY))
        logmsg ("HHC621I CPU thread set priority failed: %s\n",
                strerror(errno));

    logmsg ("HHC622I CPU%d priority adjusted to %d\n",
            regs->cpuad, getpriority(PRIO_PROCESS,0));

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

        /* Test for interrupts if it appears that one may be pending */
        if ((sysblk.mckpending && regs->psw.mach)
            || ((sysblk.extpending || regs->cpuint)
                && (regs->psw.sysmask & PSW_EXTMASK))
            || regs->restart
#ifndef FEATURE_BCMODE
            || (sysblk.iopending && (regs->psw.sysmask & PSW_IOMASK))
#else /*FEATURE_BCMODE*/
            ||  (sysblk.iopending &&
              (regs->psw.sysmask & (regs->psw.ecmode ? PSW_IOMASK : 0xFE)))
#endif /*FEATURE_BCMODE*/
#if MAX_CPU_ENGINES > 1
            || (sysblk.broadcast != regs->broadcast)
#endif /*MAX_CPU_ENGINES > 1*/
            || regs->psw.wait
            || regs->cpustate != CPUSTATE_STARTED)
        {
            /* Obtain the interrupt lock */
            obtain_lock (&sysblk.intlock);

#if MAX_CPU_ENGINES > 1
            /* Perform broadcasted purge of ALB and TLB if requested */
            if (sysblk.broadcast != regs->broadcast)
                perform_broadcast_request(regs);
#endif /*MAX_CPU_ENGINES > 1*/

            /* If a machine check is pending and we are enabled for
               machine checks then take the interrupt */
            if (sysblk.mckpending && regs->psw.mach)
                perform_mck_interrupt (regs);

            /* If enabled for external interrupts, invite the
               service processor to present a pending interrupt */
            if (regs->psw.sysmask & PSW_EXTMASK)
                perform_external_interrupt (regs);

            /* If an I/O interrupt is pending, and this CPU is
               enabled for I/O interrupts, invite the channel
               subsystem to present a pending interrupt */
            if (sysblk.iopending &&
#ifdef FEATURE_BCMODE
                (regs->psw.sysmask &
                    (regs->psw.ecmode ? PSW_IOMASK : 0xFE))
#else /*!FEATURE_BCMODE*/
                (regs->psw.sysmask & PSW_IOMASK)
#endif /*!FEATURE_BCMODE*/
               )
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
                /* Store status at absolute location 0 if requested */
                if (regs->storstat)
                {
                    regs->storstat = 0;
                    store_status (regs, 0);
                }

                /* Wait for start command from panel */
                wait_condition (&sysblk.intcond, &sysblk.intlock);
                release_lock (&sysblk.intlock);
                continue;
            }

            /* Test for disabled wait PSW */
            if (regs->psw.wait &&
#ifdef FEATURE_BCMODE
            (regs->psw.sysmask &
                (regs->psw.ecmode ? (PSW_IOMASK | PSW_EXTMASK) : 0xFF))
                == 0)
#else /*!FEATURE_BCMODE*/
            (regs->psw.sysmask & (PSW_IOMASK | PSW_EXTMASK)) == 0)
#endif /*!FEATURE_BCMODE*/
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

        } /* end if(interrupt) */

        /* Clear the instruction validity flag in case an access
           error occurs while attempting to fetch next instruction */
        regs->instvalid = 0;

        /* Fetch the next sequential instruction */
        instfetch (regs->inst, regs->psw.ia, regs);

        /* Set the instruction validity flag */
        regs->instvalid = 1;

        /* Count instruction usage */
        regs->instcount++;

#ifdef INSTRUCTION_COUNTING
        /* Find instruction counter for this opcode */
        switch (regs->inst[0]) {
        case 0x01: picta = instcount.op01; icidx = regs->inst[1]; break;
        case 0xA4: picta = instcount.opA4; icidx = regs->inst[1]; break;
        case 0xA5: picta = instcount.opA5; icidx = regs->inst[1]; break;
        case 0xA6: picta = instcount.opA6; icidx = regs->inst[1]; break;
        case 0xA7: picta = instcount.opA7; icidx = regs->inst[1] & 0x0F;
                   break;
        case 0xB2: picta = instcount.opB2; icidx = regs->inst[1]; break;
        case 0xB3: picta = instcount.opB3; icidx = regs->inst[1]; break;
        case 0xE4: picta = instcount.opE4; icidx = regs->inst[1]; break;
        case 0xE5: picta = instcount.opE5; icidx = regs->inst[1]; break;
        case 0xE6: picta = instcount.opE6; icidx = regs->inst[1]; break;
        case 0xED: picta = instcount.opED; icidx = regs->inst[1]; break;
        default: picta = instcount.general; icidx = regs->inst[0];
        } /* end switch */

        /* Test for first usage of this opcode */
        if (picta[icidx] == 0 && regs->instcount >= 256) {
            if (picta == instcount.general)
                logmsg ("First use of instruction %2.2X\n",
                        regs->inst[0]);
            else
                logmsg ("First use of instruction %2.2X%2.2X\n",
                        regs->inst[0], icidx);
            tracethis = 1;
//          if (regs->inst[0] == 0x90 || regs->inst[0] == 0x98
//              || regs->inst[0] == 0x9A || regs->inst[0] == 0x9B
//              || regs->inst[0] == 0xB6 || regs->inst[0] == 0xB7
//              || regs->inst[0] == 0xD1 || regs->inst[0] == 0xD3
//              || regs->inst[0] == 0xD4 || regs->inst[0] == 0xD6
//              || regs->inst[0] == 0xD7 || regs->inst[0] == 0xDC)
//              sysblk.inststep = 1;
        }

        /* Count instruction usage by opcode */
        picta[icidx]++;
//      if (regs->instcount % 1000000 == 0)
//          logmsg ("%llu instructions executed\n", regs->instcount);
#endif /*INSTRUCTION_COUNTING*/

        /* Turn on trace for specific instructions */
//      if (regs->inst[0] == 0xB2 && regs->inst[1] == 0x50) tracethis = 1; /*CSP*/
//      if (regs->inst[0] == 0xB2 && regs->inst[1] == 0x2E) tracethis = 1; /*PGIN*/
//      if (regs->inst[0] == 0xB2 && regs->inst[1] == 0x2F) tracethis = 1; /*PGOUT*/
//      if (regs->inst[0] == 0xB2 && regs->inst[1] == 0x54) tracethis = 1; /*MVPG*/
//      if (regs->inst[0] == 0xB2 && regs->inst[1] == 0x20) sysblk.inststep = 1; /*SERVC*/
//      if (regs->inst[0] == 0xB2 && regs->inst[1] == 0x25) sysblk.inststep = 1; /*SSAR*/
//      if (regs->inst[0] == 0xB2 && regs->inst[1] == 0x40) sysblk.inststep = 1; /*BAKR*/
//      if (regs->inst[0] == 0xB2 && regs->inst[1] == 0x18) sysblk.inststep = 1; /*PC*/
//      if (regs->inst[0] == 0xB2 && regs->inst[1] == 0x28) sysblk.inststep = 1; /*PT*/
//      if (regs->inst[0] == 0xB2 && regs->inst[1] == 0x47) sysblk.inststep = 1; /*MSTA*/
//      if (regs->inst[0] == 0xB2 && regs->inst[1] == 0x49) sysblk.inststep = 1; /*EREG*/
//      if (regs->inst[0] == 0xB2 && regs->inst[1] == 0x4A) sysblk.inststep = 1; /*ESTA*/
//      if (regs->inst[0] == 0xB2 && regs->inst[1] == 0xF0) sysblk.inststep = 1; /*IUCV*/
//      if (regs->inst[0] == 0x01 && regs->inst[1] == 0x01) sysblk.inststep = 1; /*PR*/
//      if (regs->inst[0] == 0xE5) sysblk.inststep = 1; /*LASP & MVS assists*/
//      if (regs->inst[0] == 0xFC) sysblk.inststep = 1; /*MP*/
//      if (regs->inst[0] == 0xFD) tracethis = 1; /*DP*/
//      if (regs->inst[0] == 0x83 && regs->inst[2] == 0x02 && regs->inst[3] == 0x14) sysblk.inststep = 1; /*Diagnose 214*/

        /* Test for breakpoint */
        shouldbreak = sysblk.instbreak
                        && (regs->psw.ia == sysblk.breakaddr);

        /* Display the instruction */
        if (sysblk.insttrace || sysblk.inststep || shouldbreak
            || tracethis || stepthis)
        {
            display_inst (regs, regs->inst);
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
        execute_instruction (regs->inst, 0, regs);
    }

    return NULL;
} /* end function cpu_thread */
