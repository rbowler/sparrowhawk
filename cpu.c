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
/*      Nullification corrections by Jan Jaeger                      */
/*      Set priority by Reed H. Petty from an idea by Steve Gay      */
/*      Corrections to program check by Jan Jaeger                   */
/*      Light optimization on critical path by Valery Pogonchenko    */
/*      OSTAILOR parameter by Jay Maynard                            */
/*      CPU timer and clock comparator interrupt improvements by     */
/*          Jan Jaeger, after a suggestion by Willem Koynenberg      */
/*      Instruction decode rework - Jan Jaeger                       */
/*-------------------------------------------------------------------*/

#include "hercules.h"

#include "opcode.h"

//#define MODULE_TRACE
//#define INSTRUCTION_COUNTING
//#define SVC_TRACE

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
#if defined(FOOTPRINT_BUFFER)
U32     n;
#endif /*defined(FOOTPRINT_BUFFER)*/

static char *pgmintname[] = { 
        /* 01 */        "Operation exception",
        /* 02 */        "Priviledged-operation exception",
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
        /* 14 */        "Unassigned exception",
        /* 15 */        "Operand exception",
        /* 16 */        "Trace-table exception",
        /* 17 */        "ASN-translation exception",
        /* 18 */        "Unassigned exception",
        /* 19 */        "Vector operation",
        /* 1A */        "Unassigned exception",
        /* 1B */        "Unassigned exception",
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
        /* 27 */        "Unassigned exception",
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
        /* 38 */        "Unassigned exception",   
        /* 39 */        "Unassigned exception",  
        /* 3A */        "Unassigned exception",
        /* 3B */        "Unassigned exception",
        /* 3C */        "Unassigned exception",
        /* 3D */        "Unassigned exception",
        /* 3E */        "Unassigned exception",
        /* 3F */        "Unassigned exception",
        /* 40 */        "Monitor event" };

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

    /* Perform serialization and checkpoint synchronization */
    PERFORM_SERIALIZATION (realregs);
    PERFORM_CHKPT_SYNC (realregs);

    /* Set the main storage reference and change bits */
    STORAGE_KEY(realregs->pxr) |= (STORKEY_REF | STORKEY_CHANGE);

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
        realregs->psw.ia -= realregs->psw.ilc;
        realregs->psw.ia &= ADDRESS_MAXWRAP(realregs);
    }

    /* Store the interrupt code in the PSW */
    realregs->psw.intcode = code;

    /* Trace the program check */
    if (sysblk.insttrace || sysblk.inststep
        || sysblk.pgminttr & ((U64)1 << ((code - 1) & 0x3F)) )
    {
#if defined(FOOTPRINT_BUFFER)
        if(!(sysblk.insttrace || sysblk.inststep))
            for(n = sysblk.footprptr[realregs->cpuad] + 1 ; n != sysblk.footprptr[realregs->cpuad]; n++, n &= FOOTPRINT_BUFFER - 1)
                display_inst (&sysblk.footprregs[realregs->cpuad][n], sysblk.footprregs[realregs->cpuad][n].inst);
#endif /*defined(FOOTPRINT_BUFFER)*/
        logmsg ("CPU%4.4X: %s CODE=%4.4X ILC=%d\n", realregs->cpuad,
                pgmintname[ (code - 1) & 0x3F], code, realregs->psw.ilc);
        display_inst (realregs, realregs->instvalid ? realregs->inst : NULL);
    }

    /* Point to PSA in main storage */
    psa = (PSA*)(sysblk.mainstor + realregs->pxr);

    /* For ECMODE, store extended interrupt information in PSA */
    if ( realregs->psw.ecmode )
    {
        /* Store the program interrupt code at PSA+X'8C' */
        psa->pgmint[0] = 0;
        psa->pgmint[1] = realregs->psw.ilc;
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
        logmsg ("CPU%4.4X: Invalid program-check new PSW: "
                "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
                regs->cpuad,
                psa->pgmnew[0], psa->pgmnew[1], psa->pgmnew[2],
                psa->pgmnew[3], psa->pgmnew[4], psa->pgmnew[5],
                psa->pgmnew[6], psa->pgmnew[7]);
        realregs->cpustate = CPUSTATE_STOPPED;
    }

    /* Return directly to cpu_thread function */
    longjmp (realregs->progjmp, code);

} /* end function program_check */

/*-------------------------------------------------------------------*/
/* Load restart new PSW                                              */
/*-------------------------------------------------------------------*/
static void restart_interrupt (REGS *regs)
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

    release_lock(&sysblk.intlock);

    if ( rc )
    {
        logmsg ("CPU%4.4X: Invalid restart new PSW: "
                "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
                regs->cpuad,
                psa->iplpsw[0], psa->iplpsw[1], psa->iplpsw[2],
                psa->iplpsw[3], psa->iplpsw[4], psa->iplpsw[5],
                psa->iplpsw[6], psa->iplpsw[7]);
        program_check(regs, rc);
    }
    else
        regs->cpustate = CPUSTATE_STARTED;

    longjmp (regs->progjmp, 0);
} /* end function restart_interrupt */


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

    release_lock(&sysblk.intlock);

    if ( rc )
    {
        logmsg ("CPU%4.4X: Invalid I/O new PSW: "
                "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
                regs->cpuad,
                psa->iopnew[0], psa->iopnew[1], psa->iopnew[2],
                psa->iopnew[3], psa->iopnew[4], psa->iopnew[5],
                psa->iopnew[6], psa->iopnew[7]);
        program_check (regs, rc);
    }

    longjmp (regs->progjmp, 0);
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

    release_lock(&sysblk.intlock);

    if ( rc )
    {
        logmsg ("CPU%4.4X: Invalid machine check new PSW: "
                "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
                regs->cpuad,
                psa->mcknew[0], psa->mcknew[1], psa->mcknew[2],
                psa->mcknew[3], psa->mcknew[4], psa->mcknew[5],
                psa->mcknew[6], psa->mcknew[7]);
        program_check (regs, rc);
    }

    longjmp (regs->progjmp, 0);
} /* end function perform_mck_interrupt */

/*-------------------------------------------------------------------*/
/* CPU instruction execution thread                                  */
/*-------------------------------------------------------------------*/
void *cpu_thread (REGS *regs)
{
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
    logmsg ("HHC620I CPU%4.4X thread started: tid=%8.8lX, pid=%d, "
            "priority=%d\n",
            regs->cpuad, thread_id(), getpid(),
            getpriority(PRIO_PROCESS,0));

    /* Set CPU thread priority */
    if (setpriority(PRIO_PROCESS, 0, CPU_PRIORITY))
        logmsg ("HHC621I CPU thread set priority failed: %s\n",
                strerror(errno));
  
    logmsg ("HHC622I CPU%4.4X priority adjusted to %d\n",
            regs->cpuad, getpriority(PRIO_PROCESS,0));
  
#ifdef FEATURE_VECTOR_FACILITY
    if (regs->vf.online)
        logmsg ("HHC625I CPU%4.4X Vector Facility online\n",
                regs->cpuad);
#endif /*FEATURE_VECTOR_FACILITY*/

#ifdef INSTRUCTION_COUNTING
    /* Clear instruction counters */
    memset (&instcount, 0, sizeof(instcount));
#endif /*INSTRUCTION_COUNTING*/

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
    initial_cpu_reset(regs);

    /* release the intlock */
    release_lock(&sysblk.intlock);

    /* Establish longjmp destination for program check */
    setjmp(regs->progjmp);

    /* Clear the disabled wait state flag */
    diswait = 0;

    /* Execute the program */
    while (1)
    {
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
                    perform_mck_interrupt (regs);
                }

                /* If enabled for external interrupts, invite the
                   service processor to present a pending interrupt */
                if (regs->psw.sysmask & PSW_EXTMASK)
                {
                    PERFORM_SERIALIZATION (regs);
                    PERFORM_CHKPT_SYNC (regs);
                    perform_external_interrupt (regs);
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
                    perform_io_interrupt (regs);
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
                    regs->vf.online = 0;
#endif /*FEATURE_VECTOR_FACILITY*/
    
                    /* Clear all regs */
                    initial_cpu_reset(regs);

                    /* Display thread ended message on control panel */
                    logmsg ("HHC624I CPU%4.4X thread ended: tid=%8.8lX, pid=%d\n",
                            regs->cpuad, thread_id(), getpid());

                    release_lock(&sysblk.intlock);

                    /* Thread exit */
                    regs->cputid = 0;
                    return NULL;
                }

                /* If initial CPU reset pending then perform reset */
                if (regs->sigpireset)
                {
                    PERFORM_SERIALIZATION (regs);
                    PERFORM_CHKPT_SYNC (regs);
                    initial_cpu_reset(regs);
                }

                /* If a CPU reset is pending then perform the reset */
                if (regs->sigpreset)
                {
                    PERFORM_SERIALIZATION (regs);
                    PERFORM_CHKPT_SYNC (regs);
                    cpu_reset(regs);
                }

                /* Store status at absolute location 0 if requested */
                if (regs->storstat)
                {
                    regs->storstat = 0;
                    store_status (regs, 0);
                }
            } /* end if(cpustate == STOPPING) */

            /* Perform restart interrupt if pending */
            if (regs->restart)
            {
                PERFORM_SERIALIZATION (regs);
                PERFORM_CHKPT_SYNC (regs);
                regs->restart = 0;
                restart_interrupt (regs);
            } /* end if(restart) */

            /* This is where a stopped CPU will wait */
            if (regs->cpustate == CPUSTATE_STOPPED)
            {
                /* Wait until there is work to do */
                while (regs->cpustate == CPUSTATE_STOPPED)
                    wait_condition (&sysblk.intcond, &sysblk.intlock);
                release_lock (&sysblk.intlock);
                continue;
            } /* end if(cpustate == STOPPED) */

            /* Test for wait state */
            if (regs->psw.wait)
            {
                /* Test for disabled wait PSW and issue message */
                if ( diswait == 0 &&
#ifdef FEATURE_BCMODE
                (regs->psw.sysmask &
                    (regs->psw.ecmode ?
                        (PSW_IOMASK | PSW_EXTMASK) : 0xFF)) == 0)
#else /*!FEATURE_BCMODE*/
                (regs->psw.sysmask & (PSW_IOMASK | PSW_EXTMASK)) == 0)
#endif /*!FEATURE_BCMODE*/
                {
                    logmsg ("CPU%4.4X: Disabled wait state code %8.8X\n",
                            regs->cpuad,
                            regs->psw.ia | (regs->psw.amode << 31));
#ifdef INSTRUCTION_COUNTING
                    logmsg ("%llu instructions executed\n",
                            regs->instcount);
#endif /*INSTRUCTION_COUNTING*/
                    diswait = 1;
                }

                /* Wait for I/O, external or restart interrupt */
                wait_condition (&sysblk.intcond, &sysblk.intlock);
                release_lock (&sysblk.intlock);
                continue;
            } /* end if(wait) */

            /* Reset disabled wait state indicator */
            diswait = 0;

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

#if 0
        if (regs->inst[0] == 0xA4  /* Trace the Vector Facility */
         || regs->inst[0] == 0xA5
         || regs->inst[0] == 0xA6
         || regs->inst[0] == 0xE4) tracethis = 1;
#endif
// if (regs->inst[0] == 0x0A && regs->inst[1] == 0x38 && regs->gpr[1] == 0x7F7731D0) sysblk.inststep = 1;

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
//      if(regs->inst[0] < 0x80)
            EXECUTE_INSTRUCTION (regs->inst, 0, regs);
//      else
//          execute_instruction (regs->inst, 0, regs);
    }

    return NULL;
} /* end function cpu_thread */
