/* CPU.C        (c) Copyright Roger Bowler, 1994-2000                */
/*              ESA/390 CPU Emulator                                 */

/* Interpretive Execution - (c) Copyright Jan Jaeger, 1999-2000      */
/* z/Architecture support - (c) Copyright Jan Jaeger, 1999-2000      */

/*-------------------------------------------------------------------*/
/* This module implements the CPU instruction execution function of  */
/* the S/370 and ESA/390 architectures, as described in the manuals  */
/* GA22-7000-03 System/370 Principles of Operation                   */
/* SA22-7201-06 ESA/390 Principles of Operation                      */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      Nullification corrections by Jan Jaeger                      */
/*      Set priority by Reed H. Petty from an idea by Steve Gay      */
/*      Corrections to program check by Jan Jaeger                   */
/*      Light optimization on critical path by Valery Pogonchenko    */
/*      OSTAILOR parameter by Jay Maynard                            */
/*      CPU timer and clock comparator interrupt improvements by     */
/*          Jan Jaeger, after a suggestion by Willem Konynenberg     */
/*      Instruction decode rework - Jan Jaeger                       */
/*      Modifications for Interpretive Execution (SIE) by Jan Jaeger */
/*      Basic FP extensions support - Peter Kuschnerus           v209*/
/*-------------------------------------------------------------------*/

#include "hercules.h"

#include "opcode.h"

#include "inline.h"

/*-------------------------------------------------------------------*/
/* Store current PSW at a specified address in main storage          */
/*-------------------------------------------------------------------*/
void ARCH_DEP(store_psw) (REGS *regs, BYTE *addr)
{
    addr[0] = regs->psw.sysmask;
    addr[1] = (regs->psw.pkey & 0xF0) | (regs->psw.ecmode << 3)
                | (regs->psw.mach << 2) | (regs->psw.wait << 1)
                | regs->psw.prob;

    if ( regs->psw.ecmode ) {
        addr[2] = (regs->psw.space << 7) | (regs->psw.armode << 6)
                    | (regs->psw.cc << 4)
                    | (regs->psw.fomask << 3) | (regs->psw.domask << 2)
                    | (regs->psw.eumask << 1) | regs->psw.sgmask;
        addr[3] = 0;
        STORE_FW(addr + 4,regs->psw.IA); addr[4] |= regs->psw.amode << 7;
    } else {
#if defined(FEATURE_ESAME)
        addr[2] = (regs->psw.space << 7) | (regs->psw.armode << 6)
                    | (regs->psw.cc << 4)
                    | (regs->psw.fomask << 3) | (regs->psw.domask << 2)
                    | (regs->psw.eumask << 1) | regs->psw.sgmask;
        addr[3] = regs->psw.amode64;
        addr[4] = (regs->psw.amode << 7);
        addr[5] = 0;
        addr[6] = 0;
        addr[7] = 0;
        STORE_DW(addr + 8,regs->psw.IA);
#else /*!defined(FEATURE_ESAME)*/
        STORE_HW(addr + 2,regs->psw.intcode);
        STORE_FW(addr + 4,regs->psw.IA);
        addr[4] = (regs->psw.ilc << 5) | (regs->psw.cc << 4)
                    | (regs->psw.fomask << 3) | (regs->psw.domask << 2)
                    | (regs->psw.eumask << 1) | regs->psw.sgmask;
#endif /*!defined(FEATURE_ESAME)*/
    }
} /* end function ARCH_DEP(store_psw) */

/*-------------------------------------------------------------------*/
/* Load current PSW from a specified address in main storage         */
/* Returns 0 if valid, 0x0006 if specification exception             */
/*-------------------------------------------------------------------*/
int ARCH_DEP(load_psw) (REGS *regs, BYTE *addr)
{
    INVALIDATE_AIA(regs);

    INVALIDATE_AEA_ALL(regs);

    regs->psw.sysmask = addr[0];
    regs->psw.pkey = addr[1] & 0xF0;
    regs->psw.ecmode = (addr[1] & 0x08) >> 3;
    regs->psw.mach = (addr[1] & 0x04) >> 2;
    regs->psw.wait = (addr[1] & 0x02) >> 1;
    regs->psw.prob = addr[1] & 0x01;

#if !defined(FEATURE_ESAME)
    if ( regs->psw.ecmode ) {
#endif /*!defined(FEATURE_ESAME)*/

        /* Processing for EC mode PSW */
        regs->psw.space = (addr[2] & 0x80) >> 7;
        regs->psw.armode = (addr[2] & 0x40) >> 6;
        regs->psw.intcode = 0;
        regs->psw.ilc = 0;
        regs->psw.cc = (addr[2] & 0x30) >> 4;
        regs->psw.fomask = (addr[2] & 0x08) >> 3;
        regs->psw.domask = (addr[2] & 0x04) >> 2;
        regs->psw.eumask = (addr[2] & 0x02) >> 1;
        regs->psw.sgmask = addr[2] & 0x01;

        regs->psw.amode = (addr[4] & 0x80) >> 7;

#if defined(FEATURE_ESAME)
        if( ESAMEMODE(&regs->psw) )
        {
        FETCH_DW(regs->psw.IA, addr + 8);
        if( addr[3] & 0x01 )
            regs->psw.amode64 = regs->psw.amode = 1;
//      else
//          regs->psw.IA &= 0x7FFFFFFF;
        }
        else
#endif /*!defined(FEATURE_ESAME)*/
        {
            FETCH_FW(regs->psw.IA, addr + 4);
            regs->psw.IA &= 0x7FFFFFFF;
            regs->psw.amode64 = 0;
        }

        /* Bits 0 and 2-4 of system mask must be zero */
        if ((addr[0] & 0xB8) != 0)
            return PGM_SPECIFICATION_EXCEPTION;

#if defined(FEATURE_ESAME)
        regs->psw.notesame = 0;

        /* Bits 24-30 must be zero */
        if( addr[3] & 0xFE )
#else /*!defined(FEATURE_ESAME)*/
        /* Bits 24-31 must be zero */
        if ( addr[3] )
#endif /*!defined(FEATURE_ESAME)*/
            return PGM_SPECIFICATION_EXCEPTION;

#ifndef FEATURE_DUAL_ADDRESS_SPACE
        /* If DAS feature not installed then bit 16 must be zero */
        if (regs->psw.space)
            return PGM_SPECIFICATION_EXCEPTION;
#endif /*!FEATURE_DUAL_ADDRESS_SPACE*/

#ifndef FEATURE_ACCESS_REGISTERS
        /* If not ESA/370 or ESA/390 then bit 17 must be zero */
        if (regs->psw.armode)
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

#if !defined(FEATURE_ESAME)
    } else {

#if defined(FEATURE_BCMODE)
        /* Processing for S/370 BC mode PSW */
        regs->psw.space = 0;
        regs->psw.armode = 0;
        regs->psw.intcode = (addr[2] << 8) | addr[3];
        regs->psw.ilc = (addr[4] >> 6) * 2;
        regs->psw.cc = (addr[4] & 0x30) >> 4;
        regs->psw.fomask = (addr[4] & 0x08) >> 3;
        regs->psw.domask = (addr[4] & 0x04) >> 2;
        regs->psw.eumask = (addr[4] & 0x02) >> 1;
        regs->psw.sgmask = addr[4] & 0x01;
        regs->psw.amode = 0;
        FETCH_FW(regs->psw.IA, addr + 4);
        regs->psw.IA &= 0x00FFFFFF;
#else /*!FEATURE_BCMODE*/
        /* BC mode is not valid for 370-XA, ESA/370, or ESA/390 */
        return PGM_SPECIFICATION_EXCEPTION;
#endif /*!FEATURE_BCMODE*/

    }
#endif /*!defined(FEATURE_ESAME)*/

#if defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)
    /* Bits 5 and 16 must be zero in XC mode */
    if( regs->sie_state && (regs->siebk->mx & SIE_MX_XC)
      && ( (regs->psw.sysmask & PSW_DATMODE) || regs->psw.space) )
        return PGM_SPECIFICATION_EXCEPTION;
#endif /*defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)*/

    /* Check for wait state PSW */
    if (regs->psw.wait && (sysblk.insttrace || sysblk.inststep))
    {
        logmsg ("Wait state PSW loaded: ");
        display_psw (regs);
    }

    return 0;
} /* end function ARCH_DEP(load_psw) */

/*-------------------------------------------------------------------*/
/* Load program interrupt new PSW                                    */
/*-------------------------------------------------------------------*/
void ARCH_DEP(program_interrupt) (REGS *regs, int code)
{
PSA    *psa;                            /* -> Prefixed storage area  */
REGS   *realregs;                       /* True regs structure       */
RADR    px;                             /* host real address of pfx  */
#if defined(_FEATURE_SIE)
int     nointercept;                    /* True for virtual pgmint   */
#endif /*defined(_FEATURE_SIE)*/
#if defined(OPTION_FOOTPRINT_BUFFER)
U32     n;
#endif /*defined(OPTION_FOOTPRINT_BUFFER)*/

static char *pgmintname[] = {
        /* 01 */        "Operation exception",
        /* 02 */        "Privileged-operation exception",
        /* 03 */        "Execute exception",
        /* 04 */        "Protection exception",
        /* 05 */        "Addressing exception",
        /* 06 */        "Specification exception",
        /* 07 */        "Data exception",
        /* 08 */        "Fixed-point-overflow exception",
        /* 09 */        "Fixed-point-divide exception",
        /* 0A */        "Decimal-overflow exception",
        /* 0B */        "Decimal-divide exception",
        /* 0C */        "HFP-exponent-overflow exception",
        /* 0D */        "HFP-exponent-underflow exception",
        /* 0E */        "HFP-significance exception",
        /* 0F */        "HFP-floating-point-divide exception",
        /* 10 */        "Segment-translation exception",
        /* 11 */        "Page-translation exception",
        /* 12 */        "Translation-specification exception",
        /* 13 */        "Special-operation exception",
        /* 14 */        "Pseudo-page-fault exception",
        /* 15 */        "Operand exception",
        /* 16 */        "Trace-table exception",
        /* 17 */        "ASN-translation exception",
        /* 18 */        "Page access exception",
        /* 19 */        "Vector operation exception",
        /* 1A */        "Page state exception",
        /* 1B */        "Page transition exception",
        /* 1C */        "Space-switch event",
        /* 1D */        "Square-root exception",
        /* 1E */        "Unnormalized-operand exception",
        /* 1F */        "PC-translation specification exception",
        /* 20 */        "AFX-translation exception",
        /* 21 */        "ASX-translation exception",
        /* 22 */        "LX-translation exception",
        /* 23 */        "EX-translation exception",
        /* 24 */        "Primary-authority exception",
        /* 25 */        "Secondary-authority exception",
        /* 26 */        "Page-fault-assist exception",
        /* 27 */        "Control-switch exception",
        /* 28 */        "ALET-specification exception",
        /* 29 */        "ALEN-translation exception",
        /* 2A */        "ALE-sequence exception",
        /* 2B */        "ASTE-validity exception",
        /* 2C */        "ASTE-sequence exception",
        /* 2D */        "Extended-authority exception",
        /* 2E */        "Unassigned exception",
        /* 2F */        "Unassigned exception",
        /* 30 */        "Stack-full exception",
        /* 31 */        "Stack-empty exception",
        /* 32 */        "Stack-specification exception",
        /* 33 */        "Stack-type exception",
        /* 34 */        "Stack-operation exception",
        /* 35 */        "Unassigned exception",
        /* 36 */        "Unassigned exception",
        /* 37 */        "Unassigned exception",
        /* 38 */        "ASCE-type exception",
        /* 39 */        "Region-first-translation exception",
        /* 3A */        "Region-second-translation exception",
        /* 3B */        "Region-third-translation exception",
        /* 3C */        "Unassigned exception",
        /* 3D */        "Unassigned exception",
        /* 3E */        "Unassigned exception",
        /* 3F */        "Unassigned exception",
        /* 40 */        "Monitor event" };

    /* program_interrupt() may be called with a shadow copy of the
       regs structure, realregs is the pointer to the real structure
       which must be used when loading/storing the psw, or backing up
       the instruction address in case of nullification */
#if defined(_FEATURE_SIE)
        realregs = regs->sie_state ? sysblk.sie_regs + regs->cpuad
                                   : sysblk.regs + regs->cpuad;
#else /*!defined(_FEATURE_SIE)*/
    realregs = sysblk.regs + regs->cpuad;
#endif /*!defined(_FEATURE_SIE)*/

#if MAX_CPU_ENGINES > 1
    /* Unlock the main storage lock if held */
    if (realregs->mainlock)
        RELEASE_MAINLOCK(realregs);
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    if(realregs->sie_active && realregs->guestregs->mainlock)
        RELEASE_MAINLOCK(realregs->guestregs);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
#endif /*MAX_CPU_ENGINES > 1*/

    /* Perform serialization and checkpoint synchronization */
    PERFORM_SERIALIZATION (realregs);
    PERFORM_CHKPT_SYNC (realregs);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    /* Host protection and addressing exceptions must be
       reflected to the guest */
    if(realregs->sie_active &&
        (code == PGM_PROTECTION_EXCEPTION
      || code == PGM_ADDRESSING_EXCEPTION
#if defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)
      || code == PGM_ALET_SPECIFICATION_EXCEPTION
      || code == PGM_ALEN_TRANSLATION_EXCEPTION
#endif /*defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)*/
        ) )
    {
// /*DEBUG*/ logmsg("program_int() passing to guest code=%4.4X\n",code);
        realregs->guestregs->TEA = realregs->TEA;
        realregs->guestregs->excarid = realregs->excarid;
        (realregs->guestregs->sie_guestpi) (realregs->guestregs, code);
    }
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

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
      || code == PGM_STACK_OPERATION_EXCEPTION
      || code == PGM_VECTOR_OPERATION_EXCEPTION)
      && realregs->instvalid)
    {
        realregs->psw.IA -= realregs->psw.ilc;
        realregs->psw.IA &= ADDRESS_MAXWRAP(realregs);
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
        /* When in SIE mode the guest instruction causing this
           host exception must also be nullified */
        if(realregs->sie_active && realregs->guestregs->instvalid)
        {
            realregs->guestregs->psw.IA -= realregs->guestregs->psw.ilc;
            realregs->guestregs->psw.IA &= ADDRESS_MAXWRAP(realregs->guestregs);
        }
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
    }

    /* Store the interrupt code in the PSW */
    realregs->psw.intcode = code;

    /* Trace the program check */
    if(sysblk.insttrace || sysblk.inststep
        || sysblk.pgminttr & ((U64)1 << ((code - 1) & 0x3F)))
    {
#if defined(OPTION_FOOTPRINT_BUFFER)
        if(!(sysblk.insttrace || sysblk.inststep))
            for(n = sysblk.footprptr[realregs->cpuad] + 1 ; n != sysblk.footprptr[realregs->cpuad]; n++, n &= OPTION_FOOTPRINT_BUFFER - 1)
                display_inst (&sysblk.footprregs[realregs->cpuad][n], sysblk.footprregs[realregs->cpuad][n].inst);
#endif /*defined(OPTION_FOOTPRINT_BUFFER)*/
#if defined(_FEATURE_SIE)
        if(realregs->sie_state)
            logmsg("SIE: ");
#endif /*defined(_FEATURE_SIE)*/
// /*DEBUG*/  logmsg (MSTRING(_GEN_ARCH) " ");
        logmsg ("CPU%4.4X: %s CODE=%4.4X ILC=%d\n", realregs->cpuad,
                pgmintname[ (code - 1) & 0x3F], code, realregs->psw.ilc);
        display_inst (realregs, realregs->instvalid ? realregs->inst : NULL);
    }

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    /* If this is a host exception in SIE state then leave SIE */
    if(realregs->sie_active)
        ARCH_DEP(sie_exit) (realregs, SIE_HOST_INTERRUPT);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Absolute address of prefix page */
    px = realregs->PX;

    /* If under SIE translate to host absolute */
    SIE_TRANSLATE(&px, ACCTYPE_WRITE, realregs);

#if defined(_FEATURE_SIE)
    if(!regs->sie_state ||
      /* Interception is mandatory for the following exceptions */
      (  code != PGM_PROTECTION_EXCEPTION
      && code != PGM_ADDRESSING_EXCEPTION
      && code != PGM_PAGE_TRANSLATION_EXCEPTION
      && code != PGM_SEGMENT_TRANSLATION_EXCEPTION
#ifdef FEATURE_VECTOR_FACILITY
      && code != PGM_VECTOR_OPERATION_EXCEPTION
#endif /*FEATURE_VECTOR_FACILITY*/
#if defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)
      && !( regs->sie_state
        && (regs->siebk->mx & SIE_MX_XC)
        && code == PGM_ALEN_TRANSLATION_EXCEPTION )
#endif /*defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)*/
      /* And conditional for the following exceptions */
      && !(code == PGM_OPERATION_EXCEPTION
        && (regs->siebk->ic[0] & SIE_IC0_OPEREX))
      && !(code == PGM_PRIVILEGED_OPERATION_EXCEPTION
        && (regs->siebk->ic[0] & SIE_IC0_PRIVOP))
      /* Or all exceptions if requested as such */
      && !(regs->siebk->ic[0] & SIE_IC0_PGMALL) )
    )
    {
#endif /*defined(_FEATURE_SIE)*/
        /* Set the main storage reference and change bits */
        STORAGE_KEY(px) |= (STORKEY_REF | STORKEY_CHANGE);

        /* Point to PSA in main storage */
        psa = (void*)(sysblk.mainstor + px);
#if defined(_FEATURE_SIE)
        nointercept = 1;
    }
    else
    {
        /* Set the main storage reference and change bits */
        STORAGE_KEY(regs->sie_state) |= (STORKEY_REF | STORKEY_CHANGE);

        /* This is a guest interruption interception so point to
           the interruption parm area in the state descriptor
           rather then the PSA */
        psa = (void*)(sysblk.mainstor + regs->sie_state + SIE_IP_PSA_OFFSET);
        nointercept = 0;
    }
#endif /*defined(_FEATURE_SIE)*/

#if !defined(FEATURE_ESAME)
    /* For ECMODE, store extended interrupt information in PSA */
    if ( realregs->psw.ecmode )
#endif /*!defined(FEATURE_ESAME)*/
    {
#if defined(FEATURE_ESAME)
        psa->arch = 1;
#endif /*defined(FEATURE_ESAME)*/

        /* Store the program interrupt code at PSA+X'8C' */
        psa->pgmint[0] = 0;
        psa->pgmint[1] = realregs->psw.ilc;
        STORE_HW(psa->pgmint + 2, code);

        /* Store the access register number at PSA+160 */
        if ( code == PGM_PAGE_TRANSLATION_EXCEPTION
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
        if ( code == PGM_PAGE_TRANSLATION_EXCEPTION
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
#ifdef FEATURE_BASIC_FP_EXTENSIONS
          || code == PGM_DATA_EXCEPTION
#endif /*FEATURE_BASIC_FP_EXTENSIONS*/
           )
        {
#if defined(FEATURE_ESAME)
            STORE_DW(psa->tea, regs->TEA);
#else
            STORE_FW(psa->tea, regs->TEA);
#endif
        }

        /* Store the monitor class and event code */
        if (code == PGM_MONITOR_EVENT)
        {
            STORE_HW(psa->monclass, regs->monclass);

#if defined(FEATURE_ESAME)
            STORE_DW(psa->moncode, regs->MONCODE);
#else
            /* Store the monitor code at PSA+156 */
            STORE_FW(psa->moncode, regs->MONCODE);
#endif
        }
    }

#if defined(_FEATURE_SIE)
    if(nointercept)
    {
#endif /*defined(_FEATURE_SIE)*/

        /* Store current PSW at PSA+X'28' */
        ARCH_DEP(store_psw) (realregs, psa->pgmold);

        /* Load new PSW from PSA+X'68' */
        if ( ARCH_DEP(load_psw) (realregs, psa->pgmnew) )
        {
#if defined(_FEATURE_SIE)
            if(realregs->sie_state)
                logmsg("SIE: ");
// /*DEBUG*/  logmsg (MSTRING(_GEN_ARCH) " ");
#endif /*defined(_FEATURE_SIE)*/
            logmsg ("CPU%4.4X: Invalid program interrupt new PSW: ",regs->cpuad);
            display_psw (regs);
            realregs->cpustate = CPUSTATE_STOPPING;
#if defined(_FEATURE_SIE)
            if(realregs->sie_state)
                longjmp(realregs->progjmp, SIE_INTERCEPT_VALIDITY);
#endif /*defined(_FEATURE_SIE)*/
        }

        longjmp(realregs->progjmp, SIE_NO_INTERCEPT);

#if defined(_FEATURE_SIE)
    }

    longjmp (realregs->progjmp, code);
#endif /*defined(_FEATURE_SIE)*/

} /* end function ARCH_DEP(program_interrupt) */

/*-------------------------------------------------------------------*/
/* Load restart new PSW                                              */
/*-------------------------------------------------------------------*/
static void ARCH_DEP(restart_interrupt) (REGS *regs)
{
int     rc;                             /* Return code               */
PSA    *psa;                            /* -> Prefixed storage area  */

    /* Set the main storage reference and change bits */
    STORAGE_KEY(regs->PX) |= (STORKEY_REF | STORKEY_CHANGE);

    /* Zeroize the interrupt code in the PSW */
    regs->psw.intcode = 0;

    /* Point to PSA in main storage */
    psa = (PSA*)(sysblk.mainstor + regs->PX);

    /* Store current PSW at PSA+X'8' */
    ARCH_DEP(store_psw) (regs, psa->iplccw1);

    /* Load new PSW from PSA+X'0' */
    rc = ARCH_DEP(load_psw) (regs, psa->iplpsw);

    release_lock(&sysblk.intlock);

    if ( rc )
    {
        logmsg ("CPU%4.4X: Invalid restart new PSW: ",regs->cpuad);
        display_psw (regs);
        ARCH_DEP(program_interrupt)(regs, rc);
    }
    else
        regs->cpustate = CPUSTATE_STARTED;

    longjmp (regs->progjmp, SIE_INTERCEPT_RESTART);
} /* end function restart_interrupt */


/*-------------------------------------------------------------------*/
/* Perform I/O interrupt if pending                                  */
/* Note: The caller MUST hold the interrupt lock (sysblk.intlock)    */
/*-------------------------------------------------------------------*/
static void ARCH_DEP(perform_io_interrupt) (REGS *regs)
{
int     rc;                             /* Return code               */
PSA    *psa;                            /* -> Prefixed storage area  */
U32     ioparm;                         /* I/O interruption parameter*/
U32     ioid;                           /* I/O interruption address  */
DWORD   csw;                            /* CSW for S/370 channels    */

    /* Test and clear pending I/O interrupt */
    rc = ARCH_DEP(present_io_interrupt) (regs, &ioid, &ioparm, csw);

    /* Exit if no interrupt was presented */
    if (rc == 0) return;

    /* Set the main storage reference and change bits */
    STORAGE_KEY(regs->PX) |= (STORKEY_REF | STORKEY_CHANGE);

    /* Point to the PSA in main storage */
    psa = (void*)(sysblk.mainstor + regs->PX);

#ifdef FEATURE_S370_CHANNEL
    /* Store the channel status word at PSA+X'40' */
    memcpy (psa->csw, csw, 8);

    /* Set the interrupt code to the I/O device address */
    regs->psw.intcode = ioid;

    /* For ECMODE, store the I/O device address at PSA+X'B8' */
    if (regs->psw.ecmode)
        STORE_FW(psa->ioid, ioid);

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
    STORE_FW(psa->ioid, ioid);

    /* Store the I/O interruption parameter at PSA+X'BC' */
    STORE_FW(psa->ioparm, ioparm);

    /* Trace the I/O interrupt */
    if (sysblk.insttrace || sysblk.inststep)
        logmsg ("I/O interrupt code=%8.8X parm=%8.8X\n", ioid, ioparm);
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Store current PSW at PSA+X'38' */
    ARCH_DEP(store_psw) ( regs, psa->iopold );

    /* Load new PSW from PSA+X'78' */
    rc = ARCH_DEP(load_psw) ( regs, psa->iopnew );

    release_lock(&sysblk.intlock);

    if ( rc )
    {
        logmsg ("CPU%4.4X: Invalid I/O new PSW: ",regs->cpuad);
        display_psw (regs);
        ARCH_DEP(program_interrupt) (regs, rc);
    }

    longjmp (regs->progjmp, SIE_INTERCEPT_IOREQ);
} /* end function perform_io_interrupt */

/*-------------------------------------------------------------------*/
/* Perform Machine Check interrupt if pending                        */
/* Note: The caller MUST hold the interrupt lock (sysblk.intlock)    */
/*-------------------------------------------------------------------*/
static void ARCH_DEP(perform_mck_interrupt) (REGS *regs)
{
int     rc;                             /* Return code               */
PSA    *psa;                            /* -> Prefixed storage area  */
U64     mcic;                           /* Mach.check interrupt code */
U32     xdmg;                           /* External damage code      */
RADR    fsta;                           /* Failing storage address   */

    /* Test and clear pending machine check interrupt */
    rc = ARCH_DEP(present_mck_interrupt) (regs, &mcic, &xdmg, &fsta);

    /* Exit if no machine check was presented */
    if (rc == 0) return;

    /* Set the main storage reference and change bits */
    STORAGE_KEY(regs->PX) |= (STORKEY_REF | STORKEY_CHANGE);

    /* Point to the PSA in main storage */
    psa = (void*)(sysblk.mainstor + regs->PX);

    /* Store registers in machine check save area */
    ARCH_DEP(store_status) (regs, regs->PX);

#if !defined(FEATURE_ESAME)
// ZZ
    /* Set the extended logout area to zeros */
    memset(psa->storepsw, 0, 16);
#endif

    /* Store the machine check interrupt code at PSA+232 */
    STORE_DW(psa->mckint, mcic);

    /* Trace the machine check interrupt */
    if (sysblk.insttrace || sysblk.inststep)
        logmsg ("Machine Check code=%16.16llu\n", mcic);

    /* Store the external damage code at PSA+244 */
    STORE_FW(psa->xdmgcode, xdmg);

#if defined(FEATURE_ESAME)
    /* Store the failing storage address at PSA+248 */
    STORE_DW(psa->mcstorad, fsta);
#else /*!defined(FEATURE_ESAME)*/
    /* Store the failing storage address at PSA+248 */
    STORE_FW(psa->mcstorad, fsta);
#endif /*!defined(FEATURE_ESAME)*/

    /* Store current PSW at PSA+X'30' */
    ARCH_DEP(store_psw) ( regs, psa->mckold );

    /* Load new PSW from PSA+X'70' */
    rc = ARCH_DEP(load_psw) ( regs, psa->mcknew );

    release_lock(&sysblk.intlock);

    if ( rc )
    {
        logmsg ("CPU%4.4X: Invalid machine check new PSW: ",regs->cpuad);
        display_psw (regs);
        ARCH_DEP(program_interrupt) (regs, rc);
    }

    longjmp (regs->progjmp, SIE_INTERCEPT_MCK);
} /* end function perform_mck_interrupt */

/*-------------------------------------------------------------------*/
/* CPU instruction execution thread                                  */
/*-------------------------------------------------------------------*/
#if !defined(_GEN_ARCH)
void s370_run_cpu (REGS *regs);
void s390_run_cpu (REGS *regs);
void z900_run_cpu (REGS *regs);
// void z964_run_cpu (REGS *regs);
static void (* run_cpu[GEN_MAXARCH]) (REGS *regs) =
                { s370_run_cpu, s390_run_cpu, z900_run_cpu }; /* z964_run_cpu */
static char *arch_name[GEN_MAXARCH] =
                { "S/370", "ESA/390", "ESAME" }; /* "ESAME 64V" */

void *cpu_thread (REGS *regs)
{
#define CPU_PRIORITY    15              /* CPU thread priority       */

#ifndef WIN32
    /* Set CPU thread priority */
    if (setpriority(PRIO_PROCESS, 0, CPU_PRIORITY))
        logmsg ("HHC621I CPU thread set priority failed: %s\n",
                strerror(errno));

    /* Display thread started message on control panel */
    logmsg ("HHC620I CPU%4.4X thread started: tid=%8.8lX, pid=%d, "
            "priority=%d\n",
            regs->cpuad, thread_id(), getpid(),
            getpriority(PRIO_PROCESS,0));
#endif

    logmsg ("HHC630I CPU%4.4X Architecture Mode %s\n",regs->cpuad,
                                arch_name[regs->arch_mode]);

#ifdef FEATURE_VECTOR_FACILITY
    if (regs->vf->online)
        logmsg ("HHC625I CPU%4.4X Vector Facility online\n",
                regs->cpuad);
#endif /*FEATURE_VECTOR_FACILITY*/

    /* Add this CPU to the configuration. Also ajust
       the number of CPU's to perform synchronisation as the
       synchonization process relies on the number of CPU's
       in the configuration to accurate */
    obtain_lock(&sysblk.intlock);
    if(regs->cpustate != CPUSTATE_STARTING)
    {
        logmsg("HHC623I CPU%4.4X thread already started\n",
            regs->cpuad);
        release_lock(&sysblk.intlock);
        return NULL;
    }
    sysblk.numcpu++;
#if MAX_CPU_ENGINES > 1
    if (sysblk.brdcstncpu != 0)
        sysblk.brdcstncpu++;
#endif /*MAX_CPU_ENGINES > 1*/

    /* Perform initial cpu reset */
    initial_cpu_reset (regs);

    /* release the intlock */
    release_lock(&sysblk.intlock);

    /* Establish longjmp destination to switch architecture mode */
    setjmp(regs->archjmp);

    /* Switch from architecture mode if appropriate */
    if(sysblk.arch_mode != regs->arch_mode)
    {
        regs->arch_mode = sysblk.arch_mode;
        logmsg ("HHC631I CPU%4.4X Architecture Mode set to %s\n",
                                    regs->cpuad,
                                    arch_name[regs->arch_mode]);
    }

    /* Execute the program in specified mode */
    run_cpu[regs->arch_mode] (regs);

    /* Clear all regs */
    initial_cpu_reset (regs);

    /* Display thread ended message on control panel */
    logmsg ("HHC624I CPU%4.4X thread ended: tid=%8.8lX, pid=%d\n",
            regs->cpuad, thread_id(), getpid());

    /* Thread exit */
    regs->cputid = 0;

    return NULL;

}


#endif /*!defined(_GEN_ARCH)*/


void ARCH_DEP(run_cpu) (REGS *regs)
{
int     tracethis;                      /* Trace this instruction    */
int     stepthis;                       /* Stop on this instruction  */
int     shouldbreak;                    /* 1=Stop at breakpoint      */

    /* Establish longjmp destination for program check */
    setjmp(regs->progjmp);

    while (1)
    {
        /* Reset instruction trace indicators */
        tracethis = 0;
        stepthis = 0;

        /* Test for interrupts if it appears that one may be pending */
        if ((sysblk.mckpending && regs->psw.mach)
            || ((regs->psw.sysmask & PSW_EXTMASK)
              && (sysblk.extpending || regs->cpuint
#if !defined(OPTION_NO_LINUX_INTERRUPT_PATCH)
               || (regs->ptpend && (regs->CR(0) & CR0_XM_PTIMER))
               || (regs->ckpend && (regs->CR(0) & CR0_XM_CLKC))
#endif
                ))
            || regs->restart
#ifndef FEATURE_BCMODE
            || (sysblk.iopending && (regs->psw.sysmask & PSW_IOMASK))
#else /*FEATURE_BCMODE*/
            ||  (sysblk.iopending &&
              (regs->psw.sysmask & (regs->psw.ecmode ? PSW_IOMASK : 0xFE)))
#endif /*FEATURE_BCMODE*/
            || regs->psw.wait
#if MAX_CPU_ENGINES > 1
            || sysblk.brdcstncpu != 0
#endif /*MAX_CPU_ENGINES > 1*/
            || regs->cpustate != CPUSTATE_STARTED)
        {
            /* Obtain the interrupt lock */
            obtain_lock (&sysblk.intlock);

#if MAX_CPU_ENGINES > 1
            /* Perform broadcasted purge of ALB and TLB if requested
               synchronize_broadcast() must be called until there are
               no more broadcast pending because synchronize_broadcast()
               releases and reacquires the mainlock. */
            while (sysblk.brdcstncpu != 0)
                synchronize_broadcast(regs, NULL);
#endif /*MAX_CPU_ENGINES > 1*/

            /* Take interrupts if CPU is not stopped */
            if (regs->cpustate == CPUSTATE_STARTED)
            {
                /* If a machine check is pending and we are enabled for
                   machine checks then take the interrupt */
                if (sysblk.mckpending && regs->psw.mach)
                {
                    PERFORM_SERIALIZATION (regs);
                    PERFORM_CHKPT_SYNC (regs);
                    ARCH_DEP (perform_mck_interrupt) (regs);
                }

                /* If enabled for external interrupts, invite the
                   service processor to present a pending interrupt */
                if (regs->psw.sysmask & PSW_EXTMASK)
                {
                    PERFORM_SERIALIZATION (regs);
                    PERFORM_CHKPT_SYNC (regs);
                    ARCH_DEP (perform_external_interrupt) (regs);
                }

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
                {
                    PERFORM_SERIALIZATION (regs);
                    PERFORM_CHKPT_SYNC (regs);
                    ARCH_DEP (perform_io_interrupt) (regs);
                }

            } /*if(cpustate == CPU_STARTED)*/

            /* If CPU is stopping, change status to stopped */
            if (regs->cpustate == CPUSTATE_STOPPING)
            {
                /* Change CPU status to stopped */
                regs->cpustate = CPUSTATE_STOPPED;

                if (!regs->cpuonline)
                {
                    /* Remove this CPU from the configuration. Only do this
                       when no synchronization is in progress as the
                       synchonization process relies on the number of CPU's
                       in the configuration to accurate. The first thing
                       we do during interrupt processing is synchronize
                       the broadcast functions so we are safe to manipulate
                       the number of CPU's in the configuration.  */

                    sysblk.numcpu--;

#ifdef FEATURE_VECTOR_FACILITY
                    /* Mark Vector Facility offline */
                    regs->vf->online = 0;
#endif /*FEATURE_VECTOR_FACILITY*/

                    release_lock(&sysblk.intlock);

                    /* Thread exit */
                    return;
                }

                /* If initial CPU reset pending then perform reset */
                if (regs->sigpireset)
                {
                    PERFORM_SERIALIZATION (regs);
                    PERFORM_CHKPT_SYNC (regs);
                    ARCH_DEP (initial_cpu_reset) (regs);
                }

                /* If a CPU reset is pending then perform the reset */
                if (regs->sigpreset)
                {
                    PERFORM_SERIALIZATION (regs);
                    PERFORM_CHKPT_SYNC (regs);
                    ARCH_DEP(cpu_reset) (regs);
                }

                /* Store status at absolute location 0 if requested */
                if (regs->storstat)
                {
                    regs->storstat = 0;
                    ARCH_DEP(store_status) (regs, 0);
                }
            } /* end if(cpustate == STOPPING) */

            /* Perform restart interrupt if pending */
            if (regs->restart)
            {
                PERFORM_SERIALIZATION (regs);
                PERFORM_CHKPT_SYNC (regs);
                regs->restart = 0;
                ARCH_DEP(restart_interrupt) (regs);
            } /* end if(restart) */

            /* This is where a stopped CPU will wait */
            if (regs->cpustate == CPUSTATE_STOPPED)
            {
                /* Wait until there is work to do */
                while (regs->cpustate == CPUSTATE_STOPPED)
                {
                    wait_condition (&sysblk.intcond, &sysblk.intlock);
                }
                release_lock (&sysblk.intlock);
                /* If the architecture mode has changed we must adapt */
                if(sysblk.arch_mode != regs->arch_mode)
                    longjmp(regs->archjmp,0);
                continue;
            } /* end if(cpustate == STOPPED) */

            /* Test for wait state */
            if (regs->psw.wait)
            {
                /* Test for disabled wait PSW and issue message */
#ifdef FEATURE_BCMODE
                if( (regs->psw.sysmask &
                    (regs->psw.ecmode ?
                        (PSW_IOMASK | PSW_EXTMASK) : 0xFF)) == 0)
#else /*!FEATURE_BCMODE*/
                if( (regs->psw.sysmask & (PSW_IOMASK | PSW_EXTMASK)) == 0)
#endif /*!FEATURE_BCMODE*/
                {
                    logmsg ("CPU%4.4X: Disabled wait state\n",regs->cpuad);
                    display_psw (regs);
                    regs->cpustate = CPUSTATE_STOPPING;
                }

                INVALIDATE_AIA(regs);

                INVALIDATE_AEA_ALL(regs);

                /* Wait for I/O, external or restart interrupt */
                wait_condition (&sysblk.intcond, &sysblk.intlock);
                release_lock (&sysblk.intlock);
                continue;
            } /* end if(wait) */

            /* Release the interrupt lock */
            release_lock (&sysblk.intlock);

        } /* end if(interrupt) */

        /* Clear the instruction validity flag in case an access
           error occurs while attempting to fetch next instruction */
        regs->instvalid = 0;

        /* Fetch the next sequential instruction */
        INSTRUCTION_FETCH(regs->inst, regs->psw.IA, regs);

        /* Set the instruction validity flag */
        regs->instvalid = 1;

        /* Count instruction usage */
        regs->instcount++;

        /* Turn on trace for specific instructions */
//      if (regs->inst[0] == 0xB2 && regs->inst[1] == 0x50) tracethis = 1; /*CSP*/
//      if (regs->inst[0] == 0xB2 && regs->inst[1] == 0x14) sysblk.inststep = 1; /*SIE*/

        /* Test for breakpoint */
        shouldbreak = sysblk.instbreak
                        && (regs->psw.IA == sysblk.breakaddr);

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
        EXECUTE_INSTRUCTION (regs->inst, 0, regs);
    }

} /* end function cpu_thread */


#if !defined(_GEN_ARCH)

// #define  _GEN_ARCH 964
// #include "cpu.c"

// #undef   _GEN_ARCH
#define  _GEN_ARCH 390
#include "cpu.c"

#undef   _GEN_ARCH
#define  _GEN_ARCH 370
#include "cpu.c"


void store_psw (REGS *regs, BYTE *addr)
{
    switch(regs->arch_mode) {
        case ARCH_370:
            s370_store_psw(regs, addr);
            break;
        case ARCH_390:
            s390_store_psw(regs, addr);
            break;
        case ARCH_900:
            z900_store_psw(regs, addr);
            break;
//         case ARCH_964:
//             z964_store_psw(regs, addr);
//             break;
    }
}


/*-------------------------------------------------------------------*/
/* Display program status word                                       */
/*-------------------------------------------------------------------*/
void display_psw (REGS *regs)
{
QWORD   qword;                            /* quadword work area      */

    memset(qword, 0, sizeof(qword));

    if( regs->arch_mode < ARCH_900 )
    {
        store_psw (regs, qword);
        logmsg ("PSW=%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
                qword[0], qword[1], qword[2], qword[3],
                qword[4], qword[5], qword[6], qword[7]);
    }
    else
    {
        store_psw (regs, qword);
        logmsg ("PSW=%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X "
                "%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X\n",
                qword[0], qword[1], qword[2], qword[3],
                qword[4], qword[5], qword[6], qword[7],
                qword[8], qword[9], qword[10], qword[11],
                qword[12], qword[13], qword[14], qword[15]);
    }


} /* end function display_psw */

#endif /*!defined(_GEN_ARCH)*/
