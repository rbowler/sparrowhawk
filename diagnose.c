/* DIAGNOSE.C   (c) Copyright Roger Bowler, 2000-2001                */
/*              ESA/390 Diagnose Functions                           */

/*-------------------------------------------------------------------*/
/* This module implements miscellaneous diagnose functions           */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      Hercules-specific diagnose calls by Jay Maynard.             */
/*      Set/reset bad frame indicator call by Jan Jaeger.            */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Internal macro definitions                                        */
/*-------------------------------------------------------------------*/
#define SPACE   ((BYTE)' ')

/*-------------------------------------------------------------------*/
/* Diagnose instruction                                              */
/*-------------------------------------------------------------------*/
void diagnose_call (U32 code, int r1, int r2, REGS *regs)
{
#ifdef FEATURE_HERCULES_DIAGCALLS
U32             n;                      /* 32-bit operand value      */
#endif /*FEATURE_HERCULES_DIAGCALLS*/

    switch(code) {


#if defined(FEATURE_HYPERVISOR) || defined(FEATURE_EMULATE_VM)
    case 0x044:
    /*---------------------------------------------------------------*/
    /* Diagnose 044: Voluntary Time Slice End                        */
    /*---------------------------------------------------------------*/
        scpend_call();
        break;
#endif


#ifdef FEATURE_MSSF_CALL
    case 0x080:
    /*---------------------------------------------------------------*/
    /* Diagnose 080: MSSF Call                                       */
    /*---------------------------------------------------------------*/
        regs->psw.cc = mssf_call (r1, r2, regs);
        break;
#endif /*FEATURE_MSSF_CALL*/


#if defined(FEATURE_HYPERVISOR)
    case 0x204:
    /*---------------------------------------------------------------*/
    /* Diagnose 204: LPAR RMF Interface                              */
    /*---------------------------------------------------------------*/
        diag204_call (r1, r2, regs);
        regs->psw.cc = 0;
        break;
#endif /*defined(FEATURE_HYPERVISOR)*/

#if 0
    case 0x21C:
    /*---------------------------------------------------------------*/
    /* Diagnose 21C: ????                                            */
    /*---------------------------------------------------------------*/
        /*INCOMPLETE*/
        regs->psw.cc = 0;
        break;
#endif

#ifdef FEATURE_EMULATE_VM
    case 0x000:
    /*---------------------------------------------------------------*/
    /* Diagnose 000: Store Extended Identification Code              */
    /*---------------------------------------------------------------*/
        extid_call (r1, r2, regs);
        break;

    case 0x008:
    /*---------------------------------------------------------------*/
    /* Diagnose 008: Virtual Console Function                        */
    /*---------------------------------------------------------------*/
        /* Process CP command and set condition code */
        regs->psw.cc = cpcmd_call (r1, r2, regs);
        break;

    case 0x00C:
    /*---------------------------------------------------------------*/
    /* Diagnose 00C: Pseudo Timer                                    */
    /*---------------------------------------------------------------*/
        pseudo_timer (code, r1, r2, regs);
        break;

    case 0x024:
    /*---------------------------------------------------------------*/
    /* Diagnose 024: Device Type and Features                        */
    /*---------------------------------------------------------------*/
        regs->psw.cc = diag_devtype (r1, r2, regs);
        break;

    case 0x05C:
    /*---------------------------------------------------------------*/
    /* Diagnose 05C: Error Message Editing                           */
    /*---------------------------------------------------------------*/
        /* This function is implemented as a no-operation */
        regs->psw.cc = 0;
        break;

    case 0x060:
    /*---------------------------------------------------------------*/
    /* Diagnose 060: Virtual Machine Storage Size                    */
    /*---------------------------------------------------------------*/
        /* Load main storage size in bytes into R1 register */
        regs->gpr[r1] = regs->mainsize;
        break;

    case 0x064:
    /*---------------------------------------------------------------*/
    /* Diagnose 064: Named Saved Segment Manipulation                */
    /*---------------------------------------------------------------*/
        /* Return code 44 cond code 2 means segment does not exist */
        regs->gpr[r2] = 44;
        regs->psw.cc = 2;
        break;

    case 0x0A4:
    /*---------------------------------------------------------------*/
    /* Diagnose 0A4: Synchronous I/O (Standard CMS Blocksize)        */
    /*---------------------------------------------------------------*/
        regs->psw.cc = syncblk_io (r1, r2, regs);
//      logmsg ("Diagnose X\'0A4\': CC=%d, R15=%8.8X\n",      /*debug*/
//              regs->psw.cc, regs->gpr[15]);                 /*debug*/
        break;

    case 0x0A8:
    /*---------------------------------------------------------------*/
    /* Diagnose 0A8: Synchronous General I/O                         */
    /*---------------------------------------------------------------*/
        regs->psw.cc = syncgen_io (r1, r2, regs);
//      logmsg ("Diagnose X\'0A8\': CC=%d, R15=%8.8X\n",      /*debug*/
//              regs->psw.cc, regs->gpr[15]);                 /*debug*/
        break;

    case 0x0B0:
    /*---------------------------------------------------------------*/
    /* Diagnose 0B0: Access Re-IPL Data                              */
    /*---------------------------------------------------------------*/
        access_reipl_data (r1, r2, regs);
        break;

    case 0x0DC:
    /*---------------------------------------------------------------*/
    /* Diagnose 0DC: Control Application Monitor Record Collection   */
    /*---------------------------------------------------------------*/
        /* This function is implemented as a no-operation */
        regs->gpr[r2] = 0;
        regs->psw.cc = 0;
        break;

    case 0x214:
    /*---------------------------------------------------------------*/
    /* Diagnose 214: Pending Page Release                            */
    /*---------------------------------------------------------------*/
        regs->psw.cc = diag_ppagerel (r1, r2, regs);
        break;

    case 0x23C:
    /*---------------------------------------------------------------*/
    /* Diagnose 23C: Address Space Services                          */
    /*---------------------------------------------------------------*/
        /* This function is implemented as a no-operation */
        regs->gpr[r2] = 0;
        break;

    case 0x264:
    /*---------------------------------------------------------------*/
    /* Diagnose 264: CP Communication                                */
    /*---------------------------------------------------------------*/
        /* This function is implemented as a no-operation */
        regs->psw.cc = 0;
        break;

    case 0x270:
    /*---------------------------------------------------------------*/
    /* Diagnose 270: Pseudo Timer Extended                           */
    /*---------------------------------------------------------------*/
        pseudo_timer (code, r1, r2, regs);
        break;

    case 0x274:
    /*---------------------------------------------------------------*/
    /* Diagnose 274: Set Timezone Interrupt Flag                     */
    /*---------------------------------------------------------------*/
        /* This function is implemented as a no-operation */
        regs->psw.cc = 0;
        break;
#endif /*FEATURE_EMULATE_VM*/

#ifdef FEATURE_HERCULES_DIAGCALLS
    case 0xF00:
    /*---------------------------------------------------------------*/
    /* Diagnose F00: Hercules normal mode                            */
    /*---------------------------------------------------------------*/
        sysblk.inststep = 0;
        set_doinst();
        break;

    case 0xF04:
    /*---------------------------------------------------------------*/
    /* Diagnose F04: Hercules single step mode                       */
    /*---------------------------------------------------------------*/
        sysblk.inststep = 1;
        set_doinst();
        break;

    case 0xF08:
    /*---------------------------------------------------------------*/
    /* Diagnose F08: Hercules get instruction counter                */
    /*---------------------------------------------------------------*/
        regs->gpr[r1] = (U32)regs->instcount;
//        logmsg("Diagnose F08 %llu %4x\n", regs->instcount, regs->psw.ia);
#ifdef INSTSTAT
        {
        BYTE i;
        for (i=; i < 0xff; i++)
        {
            logmsg("Inst %x %llu\n", i, regs->instcountx[i]);
            regs->instcountx[i] = 0;
        }
        }
#endif

#ifdef IBUF_STAT
        logmsg("instcount %llu\n", regs->instcount);
        logmsg("ibufrecompile %llu\n", regs->ibufrecompile);
        logmsg("ibufrecompilestorage %llu\n", regs->ibufrecompilestorage);
        logmsg("ibufrecompiledisk %llu\n", regs->ibufrecompiledisk);
        logmsg("ibufinterpret %llu\n", regs->ibufinterpret);
        logmsg("ibufcodechange %llu\n", regs->ibufcodechange);
        logmsg("ibufexeinst %llu\n", regs->ibufexeinst);
        logmsg("ibufget %llu\n", regs->ibufget);
        logmsg("ibufassign %llu\n", regs->ibufassign);
        logmsg("ibufexeassign %llu\n", regs->ibufexeassign);
        logmsg("ibufinvalidate %llu\n", regs->ibufinvalidate);
        logmsg("ibufinvalidatex %llu\n", regs->ibufinvalidatex);
#endif
        break;

    case 0xF0C:
    /*---------------------------------------------------------------*/
    /* Diagnose F0C: Set/reset bad frame indicator                   */
    /*---------------------------------------------------------------*/
        /* Load 4K block address from R2 register */
        n = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

        /* Convert real address to absolute address */
        n = APPLY_PREFIXING (n, regs->pxr);

        /* Addressing exception if block is outside main storage */
        if ( n >= regs->mainsize )
        {
            program_interrupt (regs, PGM_ADDRESSING_EXCEPTION);
            break;
        }

        /* Update the storage key from R1 register bit 31 */
        STORAGE_KEY(n) &= ~(STORKEY_BADFRM);
        STORAGE_KEY(n) |= regs->gpr[r1] & STORKEY_BADFRM;

        break;

    case 0xF10:
    /*---------------------------------------------------------------*/
    /* Diagnose F10: Hercules CPU stop                               */
    /*---------------------------------------------------------------*/
        regs->cpustate = CPUSTATE_STOPPING;
        break;
#endif /*FEATURE_HERCULES_DIAGCALLS*/

    default:
    /*---------------------------------------------------------------*/
    /* Diagnose xxx: Invalid function code                           */
    /*---------------------------------------------------------------*/
        program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);
        return;

    } /* end switch(code) */

    return;

} /* end function diagnose_call */
