/* CONTROL.C    (c) Copyright Roger Bowler, 1994-2000                */
/*              ESA/390 CPU Emulator                                 */

/*-------------------------------------------------------------------*/
/* This module implements all control instructions of the            */
/* S/370 and ESA/390 architectures, as described in the manuals      */
/* GA22-7000-03 System/370 Principles of Operation                   */
/* SA22-7201-06 ESA/390 Principles of Operation                      */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      Bad frame support by Jan Jaeger                              */
/*      Branch tracing by Jan Jaeger                                 */
/*      CSP instructions by Jan Jaeger                               */
/*      Instruction decode by macros - Jan Jaeger                    */
/*      Prevent TOD from going backwards in time - Jan Jaeger        */
/*      Instruction decode rework - Jan Jaeger                       */
/*-------------------------------------------------------------------*/

#include "hercules.h"

#include "opcode.h"

#include "inline.h"

/*-------------------------------------------------------------------*/
/* B25A BSA   - Branch and Set Authority                       [RRE] */
/*-------------------------------------------------------------------*/
void zz_branch_and_set_authority (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     ducto;                          /* DUCT origin               */
U32     duct8;                          /* DUCT word 8               */
U32     duct9;                          /* DUCT word 9               */
BYTE    key;                            /* New PSW key               */
#ifdef FEATURE_TRACING
U32     newcr12 = 0;                    /* CR12 upon completion      */
#endif /*FEATURE_TRACING*/

    RRE(inst, execflag, regs, r1, r2);

    /* Special operation exception if CR0 bit 15 is zero */
    if ((regs->cr[0] & CR0_ASF) == 0)
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

#ifdef FEATURE_TRACING
    /* Perform tracing */
    if ((regs->cr[12] & CR12_BRTRACE) && (r2 != 0))
        newcr12 = trace_br (regs->gpr[r2] & 0x80000000,
                                regs->gpr[r2], regs);
#endif /*FEATURE_TRACING*/

    /* Load real address of dispatchable unit control table */
    ducto = regs->cr[2] & CR2_DUCTO;

    /* Apply low-address protection to stores into the DUCT */
    if (ducto < 512 && (regs->cr[0] & CR0_LOW_PROT))
    {
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
        regs->tea = (ducto & TEA_EFFADDR);
        regs->excarid = 0;
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
        program_check (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Convert DUCT real address to absolute address */
    ducto = APPLY_PREFIXING (ducto, regs->pxr);

    /* Program check if DUCT origin address is invalid */
    if (ducto >= sysblk.mainsize)
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Load DUCT words 8 and 9 */
    duct8 = fetch_fullword_absolute (ducto+32);
    duct9 = fetch_fullword_absolute (ducto+36);

    /* Perform base authority or reduced authority operation */
    if ((duct9 & DUCT9_RA) == 0)
    {
        /* In base authority state R2 cannot specify register zero */
        if (r2 == 0)
            program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

        /* Obtain the new PSW key from R1 register bits 24-27 */
        key = regs->gpr[r1] & 0x000000F0;

        /* Privileged operation exception if in problem state and
           current PSW key mask does not permit new key value */
        if (regs->psw.prob
            && ((regs->cr[3] << (key >> 4)) & 0x80000000) == 0 )
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);

        /* Save current PSW amode and instruction address */
        duct8 = regs->psw.ia & DUCT8_IA;
        if (regs->psw.amode) duct8 |= DUCT8_AMODE;

        /* Save current PSW key mask, PSW key, and problem state */
        duct9 = (regs->cr[3] & CR3_KEYMASK) | regs->psw.pkey;
        if (regs->psw.prob) duct9 |= DUCT9_PROB;

        /* Set the reduced authority bit */
        duct9 |= DUCT9_RA;

        /* Store the updated values in DUCT words 8 and 9 */
        store_fullword_absolute (duct8, ducto+32);
        store_fullword_absolute (duct9, ducto+36);

        /* Load new PSW key and PSW key mask from R1 register */
        regs->psw.pkey = key;
        regs->cr[3] &= ~CR3_KEYMASK;
        regs->cr[3] |= regs->gpr[r1] & CR3_KEYMASK;

        /* Set the problem state bit in the current PSW */
        regs->psw.prob = 1;

        /* Set PSW instruction address and amode from R2 register */
        if (regs->gpr[r2] & 0x80000000)
        {
            regs->psw.amode = 1;
            regs->psw.ia = regs->gpr[r2] & 0x7FFFFFFF;
        }
        else
        {
            regs->psw.amode = 0;
            regs->psw.ia = regs->gpr[r2] & 0x00FFFFFF;
        }

    } /* end if(BSA-ba) */
    else
    { /* BSA-ra */

        /* In reduced authority state R2 must specify register zero */
        if (r2 != 0)
            program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

        /* If R1 is non-zero, save the current PSW addressing mode
           and instruction address in the R1 register */
        if (r1 != 0)
        {
            regs->gpr[r1] = regs->psw.ia;
            if (regs->psw.amode) regs->gpr[r1] |= 0x80000000;
        }

        /* Restore PSW amode and instruction address from the DUCT */
        regs->psw.ia = duct8 & DUCT8_IA;
        regs->psw.amode = (duct8 & DUCT8_AMODE) ? 1 : 0;

        /* Restore the PSW key mask from the DUCT */
        regs->cr[3] &= ~CR3_KEYMASK;
        regs->cr[3] |= duct9 & DUCT9_PKM;

        /* Restore the PSW key from the DUCT */
        regs->psw.pkey = duct9 & DUCT9_KEY;

        /* Restore the problem state bit from the DUCT */
        regs->psw.prob = (duct9 & DUCT9_PROB) ? 1 : 0;

        /* Reset the reduced authority bit in the DUCT */
        duct9 &= ~DUCT9_RA;
        store_fullword_absolute (duct9, ducto+36);

        /* Specification exception if the PSW is now invalid */
        if ((regs->psw.ia & 1)
            || (regs->psw.amode == 0 && regs->psw.ia > 0x00FFFFFF))
        {
            regs->psw.ilc = 0;
            program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        }

    } /* end if(BSA-ra) */

#ifdef FEATURE_TRACING
    /* Update trace table address if branch tracing is on */
    if ((regs->cr[12] & CR12_BRTRACE) && (r2 != 0))
        regs->cr[12] = newcr12;
#endif /*FEATURE_TRACING*/

}


/*-------------------------------------------------------------------*/
/* B258 BSG   - Branch in Subspace Group                       [RRE] */
/*-------------------------------------------------------------------*/
void zz_branch_in_subspace_group (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     alet;                           /* Destination subspace ALET */
U32     dasteo;                         /* Destination ASTE origin   */
U32     daste[16];                      /* ASN second table entry    */
U32     ducto;                          /* DUCT origin               */
U32     duct0;                          /* DUCT word 0               */
U32     duct1;                          /* DUCT word 1               */
U32     duct3;                          /* DUCT word 3               */
U32     abs;                            /* Absolute address          */
U32     newia;                          /* New instruction address   */
int     protect = 0;                    /* 1=ALE protection detected
                                           by ART (ignored by BSG)   */
U16     xcode;                          /* Exception code            */
#ifdef FEATURE_TRACING
U32     newcr12 = 0;                    /* CR12 upon completion      */
#endif /*FEATURE_TRACING*/

    RRE(inst, execflag, regs, r1, r2);

    /* Special operation exception if DAT is off or CR0 bit 15 is 0 */
    if (REAL_MODE(&(regs->psw))
        || (regs->cr[0] & CR0_ASF) == 0)
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

#ifdef FEATURE_TRACING
    /* Perform tracing */
    if (regs->cr[12] & CR12_ASNTRACE)
        newcr12 = trace_bsg ((r2 == 0) ? 0 : regs->ar[r2],
                                regs->gpr[r2], regs);
    else
        if (regs->cr[12] & CR12_BRTRACE)
            newcr12 = trace_br (regs->gpr[r2] & 0x80000000,
                                regs->gpr[r2], regs);
#endif /*FEATURE_TRACING*/

    /* Load real address of dispatchable unit control table */
    ducto = regs->cr[2] & CR2_DUCTO;

    /* Apply low-address protection to stores into the DUCT */
    if (ducto < 512 && (regs->cr[0] & CR0_LOW_PROT))
    {
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
        regs->tea = (ducto & TEA_EFFADDR);
        regs->excarid = 0;
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
        program_check (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Convert DUCT real address to absolute address */
    ducto = APPLY_PREFIXING (ducto, regs->pxr);

    /* Program check if DUCT origin address is invalid */
    if (ducto >= sysblk.mainsize)
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Fetch DUCT words 0, 1, and 3 from absolute storage
       (note: the DUCT cannot cross a page boundary) */
    duct0 = fetch_fullword_absolute (ducto);
    duct1 = fetch_fullword_absolute (ducto+4);
    duct3 = fetch_fullword_absolute (ducto+12);

    /* Special operation exception if the current primary ASTE origin
       is not the same as the base ASTE for the dispatchable unit */
    if ((regs->cr[5] & CR5_PASTEO) != (duct0 & DUCT0_BASTEO))
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    /* Obtain the destination ALET from the R2 access register,
       except that register zero means destination ALET is zero */
    alet = (r2 == 0) ? 0 : regs->ar[r2];

    /* Perform special ALET translation to obtain destination ASTE */
    switch (alet) {

    case ALET_PRIMARY: /* Branch to base space */

        /* Load the base space ASTE origin from the DUCT */
        dasteo = duct0 & DUCT0_BASTEO;

        /* Convert the ASTE origin to an absolute address */
        abs = APPLY_PREFIXING (dasteo, regs->pxr);

        /* Program check if ASTE origin address is invalid */
        if (abs >= sysblk.mainsize)
            program_check (regs, PGM_ADDRESSING_EXCEPTION);

        /* Fetch destination ASTE word 2 from absolute storage
           (note: the ASTE cannot cross a page boundary) */
        daste[2] = fetch_fullword_absolute (abs+8);

        break;

    case ALET_SECONDARY: /* Branch to last-used subspace */

        /* Load the subspace ASTE origin from the DUCT */
        dasteo = duct1 & DUCT1_SSASTEO;

        /* Special operation exception if SSASTEO is zero */
        if (dasteo == 0)
            program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

        /* Convert the ASTE origin to an absolute address */
        abs = APPLY_PREFIXING (dasteo, regs->pxr);

        /* Program check if ASTE origin address is invalid */
        if (abs >= sysblk.mainsize)
            program_check (regs, PGM_ADDRESSING_EXCEPTION);

        /* Fetch subspace ASTE words 0, 2, and 5 from absolute
           storage (note: the ASTE cannot cross a page boundary) */
        daste[0] = fetch_fullword_absolute (abs);
        daste[2] = fetch_fullword_absolute (abs+8);
        daste[5] = fetch_fullword_absolute (abs+20);

        /* ASTE validity exception if ASTE invalid bit is one */
        if (daste[0] & ASTE0_INVALID)
            program_check (regs, PGM_ASTE_VALIDITY_EXCEPTION);

        /* ASTE sequence exception if the subspace ASTE sequence
           number does not match the sequence number in the DUCT */
        if ((daste[5] & ASTE5_ASTESN) != (duct3 & DUCT3_SSASTESN))
            program_check (regs, PGM_ASTE_SEQUENCE_EXCEPTION);

        break;

    default: /* ALET not 0 or 1 */

        /* Perform special ART to obtain destination ASTE */
        xcode = translate_alet (alet, 0, ACCTYPE_BSG, regs,
                                &dasteo, daste, &protect);

        /* Program check if ALET translation error */
        if (xcode != 0)
            program_check (regs, xcode);

        /* Special operation exception if the destination ASTE
           is the base space of a different subspace group */
        if (dasteo != (duct0 & DUCT0_BASTEO)
                && ((daste[2] & STD_GROUP) == 0
                    || (daste[0] & ASTE0_BASE) == 0))
            program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    } /* end switch(alet) */

    /* Update the primary STD from word 2 of the destination ASTE */
    if (dasteo == (duct0 & DUCT0_BASTEO))
    {
        /* When the destination ASTE is the base space,
           replace the primary STD by the STD in the ASTE*/
        regs->cr[1] = daste[2];
    }
    else
    {
        /* When the destination ASTE is a subspace, replace
           bits 1-23 and 25-31 of the primary STD with the
           corresponding bits from the STD in the ASTE */
        regs->cr[1] &= (STD_SSEVENT | STD_SAEVENT);
        regs->cr[1] |= (daste[2] & ~(STD_SSEVENT | STD_SAEVENT));
    }

    /* Compute the branch address from the R2 operand */
    newia = regs->gpr[r2];

    /* If R1 is non-zero, save the current PSW addressing mode
       and instruction address in the R1 register */
    if (r1 != 0)
    {
        regs->gpr[r1] = regs->psw.ia;
        if (regs->psw.amode) regs->gpr[r1] |= 0x80000000;
    }

    /* Set mode and branch to address specified by R2 operand */
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

    /* Set SSTD equal to PSTD */
    regs->cr[7] = regs->cr[1];

    /* Set SASN equal to PASN */
    regs->cr[3] &= ~CR3_SASN;
    regs->cr[3] |= (regs->cr[4] & CR4_PASN);

    /* Reset the subspace fields in the DUCT */
    if (dasteo == (duct0 & DUCT0_BASTEO))
    {
        /* When the destination ASTE is the base space,
           reset the subspace active bit in the DUCT */
        duct1 &= ~DUCT1_SA;
        store_fullword_absolute (duct1, ducto+4);
    }
    else if (alet == ALET_SECONDARY)
    {
        /* When the destination ASTE specifies a subspace by means
           of ALET 1, set the subspace active bit in the DUCT */
        duct1 |= DUCT1_SA;
        store_fullword_absolute (duct1, ducto+4);
    }
    else
    {
        /* When the destination ASTE specifies a subspace by means
           of an ALET other than ALET 1, set the subspace active
           bit and store the subspace ASTE origin in the DUCT */
        duct1 = DUCT1_SA | dasteo;
        store_fullword_absolute (duct1, ducto+4);

        /* Set the subspace ASTE sequence number in the DUCT
           equal to the destination ASTE sequence number */
        duct3 = daste[5];
        store_fullword_absolute (duct3, ducto+12);
    }

#ifdef FEATURE_TRACING
    /* Update trace table address if ASN tracing or branch tracing */
    if (regs->cr[12] & (CR12_ASNTRACE | CR12_BRTRACE))
        regs->cr[12] = newcr12;
#endif /*FEATURE_TRACING*/

}


/*-------------------------------------------------------------------*/
/* B240 BAKR  - Branch and Stack Register                      [RRE] */
/*-------------------------------------------------------------------*/
void zz_branch_and_stack (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     n1, n2;                         /* Fullword workareas        */
#ifdef FEATURE_TRACING
U32     n = 0;                          /* Fullword workareas        */
#endif /*FEATURE_TRACING*/

    RRE(inst, execflag, regs, r1, r2);

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
    form_stack_entry (LSED_UET_BAKR, n1, n2, 0, regs);

#ifdef FEATURE_TRACING
    /* Update CR12 to reflect the new branch trace entry */
    if((regs->cr[12] & CR12_BRTRACE) && (r2 != 0))
        regs->cr[12] = n;
#endif /*FEATURE_TRACING*/

    /* Execute the branch unless R2 specifies register 0 */
    if ( r2 != 0 )
        regs->psw.ia = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

}


#if defined(FEATURE_BROADCASTED_PURGING)
/*-------------------------------------------------------------------*/
/* B250 CSP   - Compare and Swap and Purge                     [RRE] */
/*-------------------------------------------------------------------*/
void zz_compare_and_swap_and_purge (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     n1, n2;                         /* 32 Bit work               */

    RRE(inst, execflag, regs, r1, r2);

    PRIV_CHECK(regs);

    ODD_CHECK(r1, regs);

    /* Perform serialization before starting operation */
    PERFORM_SERIALIZATION (regs);

    /* Obtain main-storage access lock */
    OBTAIN_MAINLOCK(regs);

    /* Obtain 2nd operand address from r2 */
    n2 = regs->gpr[r2] & 0x7FFFFFFC & ADDRESS_MAXWRAP(regs);

    /* Load second operand from operand address  */
    n1 = vfetch4 ( n2, r2, regs );

    /* Compare operand with R1 register contents */
    if ( regs->gpr[r1] == n1 )
    {
        /* If equal, store R1+1 at operand location and set cc=0 */
        vstore4 ( regs->gpr[r1+1], n2, r2, regs );
        regs->psw.cc = 0;

        /* Release main-storage access lock
           synchronize_broadcast() must not be called
           with the mainlock held as this can cause
           a deadly embrace with other CPU's */
        RELEASE_MAINLOCK(regs);

        /* Purge the TLB if bit 31 of r2 register is set */
        if (regs->gpr[r2] & 0x00000001)
        {
#if MAX_CPU_ENGINES == 1
            purge_tlb(regs);
#else /*!MAX_CPU_ENGINES == 1*/
            synchronize_broadcast(regs, &sysblk.brdcstptlb);
#endif /*!MAX_CPU_ENGINES == 1*/
        }

        /* Purge the ALB if bit 30 of r2 register is set */
        if (regs->gpr[r2] & 0x00000002)
        {
#if MAX_CPU_ENGINES == 1
            purge_alb(regs);
#else /*!MAX_CPU_ENGINES == 1*/
            synchronize_broadcast(regs, &sysblk.brdcstpalb);
#endif /*!MAX_CPU_ENGINES == 1*/
        }

    }
    else
    {
        /* If unequal, load R1 from operand and set cc=1 */
        regs->gpr[r1] = n1;
        regs->psw.cc = 1;

        /* Release main-storage access lock */
        RELEASE_MAINLOCK(regs);
    }

    /* Perform serialization after completing operation */
    PERFORM_SERIALIZATION (regs);

}
#endif /*defined(FEATURE_BROADCASTED_PURGING)*/


/*-------------------------------------------------------------------*/
/* 83         - Diagnose                                             */
/*-------------------------------------------------------------------*/
void zz_diagnose (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

//  PRIV_CHECK(regs);

    /* Process diagnose instruction */
    diagnose_call (effective_addr2, r1, r3, regs);

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);
}


/*-------------------------------------------------------------------*/
/* B226 EPAR  - Extract Primary ASN                            [RRE] */
/*-------------------------------------------------------------------*/
void zz_extract_primary_asn (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Special operation exception if DAT is off */
    if ( (regs->psw.sysmask & PSW_DATMODE) == 0 )
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    /* Privileged operation exception if in problem state
       and the extraction-authority control bit is zero */
    if ( regs->psw.prob
         && (regs->cr[0] & CR0_EXT_AUTH) == 0 )
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);

    /* Load R1 with PASN from control register 4 bits 16-31 */
    regs->gpr[r1] = regs->cr[4] & CR4_PASN;

}


/*-------------------------------------------------------------------*/
/* B227 ESAR  - Extract Secondary ASN                          [RRE] */
/*-------------------------------------------------------------------*/
void zz_extract_secondary_asn (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Special operation exception if DAT is off */
    if ( (regs->psw.sysmask & PSW_DATMODE) == 0 )
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    /* Privileged operation exception if in problem state
       and the extraction-authority control bit is zero */
    if ( regs->psw.prob
         && (regs->cr[0] & CR0_EXT_AUTH) == 0 )
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);

    /* Load R1 with SASN from control register 3 bits 16-31 */
    regs->gpr[r1] = regs->cr[3] & CR3_SASN;

}


/*-------------------------------------------------------------------*/
/* B249 EREG  - Extract Stacked Registers                      [RRE] */
/*-------------------------------------------------------------------*/
void zz_extract_stacked_registers (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
LSED    lsed;                           /* Linkage stack entry desc. */
U32     lsea;                           /* Linkage stack entry addr  */

    RRE(inst, execflag, regs, r1, r2);

    /* Find the virtual address of the entry descriptor
       of the current state entry in the linkage stack */
    lsea = locate_stack_entry (0, &lsed, regs);

    /* Load registers from the stack entry */
    unstack_registers (lsea, r1, r2, regs);

}


/*-------------------------------------------------------------------*/
/* B24A ESTA  - Extract Stacked State                          [RRE] */
/*-------------------------------------------------------------------*/
void zz_extract_stacked_state (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
BYTE    code;                           /* Extraction code           */
LSED    lsed;                           /* Linkage stack entry desc. */
U32     lsea;                           /* Linkage stack entry addr  */
U32     abs;                            /* Absolute address          */

    RRE(inst, execflag, regs, r1, r2);

    /* Load the extraction code from R2 register bits 24-31 */
    code = regs->gpr[r2] & 0xFF;

    /* Program check if r1 is odd, or if extraction code is invalid */
    if ((r1 & 1) || code > 3)
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Find the virtual address of the entry descriptor
       of the current state entry in the linkage stack */
    lsea = locate_stack_entry (0, &lsed, regs);

    /* Point back to byte 128 of the state entry */
    lsea -= LSSE_SIZE - sizeof(LSED);
    lsea += 128;

    /* Point to byte 128, 136, 144, or 152 depending on the code */
    lsea += code * 8;
    lsea &= 0x7FFFFFFF;

    /* Load the general register pair from the state entry */
    abs = abs_stack_addr (lsea, regs, ACCTYPE_READ);
    regs->gpr[r1] = (sysblk.mainstor[abs] << 24)
                        | (sysblk.mainstor[abs+1] << 16)
                        | (sysblk.mainstor[abs+2] << 8)
                        | sysblk.mainstor[abs+3];
    regs->gpr[r1+1] = (sysblk.mainstor[abs+4] << 24)
                        | (sysblk.mainstor[abs+5] << 16)
                        | (sysblk.mainstor[abs+6] << 8)
                        | sysblk.mainstor[abs+7];

    /* Set condition code depending on entry type */
    regs->psw.cc =  ((lsed.uet & LSED_UET_ET) == LSED_UET_PC) ? 1 : 0;

}


/*-------------------------------------------------------------------*/
/* B224 IAC   - Insert Address Space Control                   [RRE] */
/*-------------------------------------------------------------------*/
void zz_insert_address_space_control (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    /* Special operation exception if DAT is off */
    if (REAL_MODE(&(regs->psw)))
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    /* Privileged operation exception if in problem state
       and the extraction-authority control bit is zero */
    if ( regs->psw.prob
         && (regs->cr[0] & CR0_EXT_AUTH) == 0 )
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);

    /* Extract the address-space control bits from the PSW */
    regs->psw.cc = (regs->psw.armode << 1) | (regs->psw.space);

    /* Clear bits 16-23 of the general purpose register */
    regs->gpr[r1] &= 0xFFFF00FF;

    /* Insert address-space mode into register bits 22-23 */
    regs->gpr[r1] |= regs->psw.cc << 8;

}


/*-------------------------------------------------------------------*/
/* B20B IPK   - Insert PSW Key                                   [S] */
/*-------------------------------------------------------------------*/
void zz_insert_psw_key (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    S(inst, execflag, regs, b2, effective_addr2);

    /* Privileged operation exception if in problem state
       and the extraction-authority control bit is zero */
    if ( regs->psw.prob
         && (regs->cr[0] & CR0_EXT_AUTH) == 0 )
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);

    /* Insert PSW key into bits 24-27 of general register 2
       and set bits 28-31 of general register 2 to zero */
    regs->gpr[2] &= 0xFFFFFF00;
    regs->gpr[2] |= (regs->psw.pkey & 0xF0);

}


/*-------------------------------------------------------------------*/
/* 09   ISK   - Insert Storage Key                              [RR] */
/*-------------------------------------------------------------------*/
void zz_insert_storage_key (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     n;                              /* Absolute storage addr     */

    RR(inst, execflag, regs, r1, r2);

    PRIV_CHECK(regs);

    /* Program check if R2 bits 28-31 are not zeroes */
    if ( regs->gpr[r2] & 0x0000000F )
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Load 2K block address from R2 register */
    n = regs->gpr[r2] & 0x00FFF800;

    /* Convert real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Addressing exception if block is outside main storage */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Insert the storage key into R1 register bits 24-31 */
    regs->gpr[r1] &= 0xFFFFFF00;
    regs->gpr[r1] |= (STORAGE_KEY(n) & 0xFE);

    /* In BC mode, clear bits 29-31 of R1 register */
    if ( regs->psw.ecmode == 0 )
        regs->gpr[r1] &= 0xFFFFFFF8;

//  /*debug*/logmsg("ISK storage block %8.8X key %2.2X\n",
//                  regs->gpr[r2], regs->gpr[r1] & 0xFE);

}


/*-------------------------------------------------------------------*/
/* B229 ISKE  - Insert Storage Key Extended                    [RRE] */
/*-------------------------------------------------------------------*/
void zz_insert_storage_key_extended (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     n;                              /* Workarea                  */

    RRE(inst, execflag, regs, r1, r2);

    PRIV_CHECK(regs);

    /* Load 4K block address from R2 register */
    n = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Convert real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Addressing exception if block is outside main storage */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Insert the storage key into R1 register bits 24-31 */
    regs->gpr[r1] &= 0xFFFFFF00;
    regs->gpr[r1] |= (STORAGE_KEY(n) & 0xFE);

}


/*-------------------------------------------------------------------*/
/* B223 IVSK  - Insert Virtual Storage Key                     [RRE] */
/*-------------------------------------------------------------------*/
void zz_insert_virtual_storage_key (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     effective_addr;                 /* Virtual storage addr      */
U16     xcode;                          /* Exception code            */
int     private;                        /* 1=Private address space   */
int     protect;                        /* 1=ALE or page protection  */
int     stid;                           /* Segment table indication  */
U32     n;                              /* 32-bit operand values     */

    RRE(inst, execflag, regs, r1, r2);

    /* Special operation exception if DAT is off */
    if ( (regs->psw.sysmask & PSW_DATMODE) == 0 )
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    /* Privileged operation exception if in problem state
       and the extraction-authority control bit is zero */
    if ( regs->psw.prob
         && (regs->cr[0] & CR0_EXT_AUTH) == 0 )
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);

    /* Load virtual storage address from R2 register */
    effective_addr = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Translate virtual address to real address */
    if (translate_addr (effective_addr, r2, regs, ACCTYPE_IVSK,
        &n, &xcode, &private, &protect, &stid, NULL, NULL))
        program_check (regs, xcode);

    /* Convert real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Addressing exception if block is outside main storage */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Insert the storage key into R1 register bits 24-31 */
    regs->gpr[r1] &= 0xFFFFFF00;
    regs->gpr[r1] |= (STORAGE_KEY(n) & 0xFE);

    /* Clear bits 29-31 of R1 register */
    regs->gpr[r1] &= 0xFFFFFFF8;

}


/*-------------------------------------------------------------------*/
/* B221 IPTE  - Invalidate Page Table Entry                    [RRE] */
/*-------------------------------------------------------------------*/
void zz_invalidate_page_table_entry (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */

    RRE(inst, execflag, regs, r1, r2);

    PRIV_CHECK(regs);

    /* Perform serialization before operation */
    PERFORM_SERIALIZATION (regs);

    /* Invalidate page table entry */
    invalidate_pte (inst[1], r1, r2, regs);

    /* Perform serialization after operation */
    PERFORM_SERIALIZATION (regs);

}


/*-------------------------------------------------------------------*/
/* E500 LASP  - Load Address Space Parameters                  [SSE] */
/*-------------------------------------------------------------------*/
void zz_load_address_space_parameters (BYTE inst[], int execflag, REGS *regs)
{
int     b1, b2;                         /* Values of base field      */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
U64     dreg;
U16     pkm_d;
U16     sasn_d;
U16     ax_d;
U16     pasn_d;
U32     aste[16];                       /* ASN second table entry    */
U32     pstd;                           /* Primary STD               */
U32     sstd;                           /* Secondary STD             */
U32     ltd;                            /* Linkage table descriptor  */
U32     pasteo;                         /* Primary ASTE origin       */
U32     sasteo;                         /* Secondary ASTE origin     */
U16     ax;                             /* Authorisation index       */
#ifdef FEATURE_SUBSPACE_GROUP
U16     xcode;                          /* Exception code            */
#endif /*FEATURE_SUBSPACE_GROUP*/

    SSE(inst, execflag, regs, b1, effective_addr1, b2, effective_addr2); 

    PRIV_CHECK(regs);

    /* Special operation exception if ASN translation control
       (bit 12 of control register 14) is zero */
    if ( (regs->cr[14] & CR14_ASN_TRAN) == 0 )
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    DW_CHECK(effective_addr1, regs);

    /* Fetch PKM, SASN, AX, and PASN from first operand */
    dreg = vfetch8 ( effective_addr1, b1, regs );
    pkm_d = (dreg & 0xFFFF000000000000ULL) >> 48;
    sasn_d = (dreg & 0xFFFF00000000ULL) >> 32;
    ax_d = (dreg & 0xFFFF0000) >> 16;
    pasn_d = dreg & 0xFFFF;

    /* PASN translation */

    /* Perform PASN translation if PASN not equal to current
       PASN, or if LASP function bit 29 is set */
    if ((effective_addr2 & 0x00000004)
        || pasn_d != (regs->cr[4] & CR4_PASN))
    {
        /* Translate PASN and return condition code 1 if
           AFX- or ASX-translation exception condition */
        if (translate_asn (pasn_d, regs, &pasteo, aste))
        {
            regs->psw.cc = 1;
            return;
        }

        /* Obtain new PSTD and LTD from ASTE */
        pstd = aste[2];
        ltd = aste[3];
        ax = (aste[1] & ASTE1_AX) >> 16;

#ifdef FEATURE_SUBSPACE_GROUP
        /* Perform subspace replacement on new PSTD */
        pstd = subspace_replace (pstd, pasteo, &xcode, regs);

        /* Return with condition code 1 if ASTE exception was recognized */
        if (xcode != 0)
        {
            regs->psw.cc = 1;
            return;
        }
#endif /*FEATURE_SUBSPACE_GROUP*/

        /* Return with condition code 3 if either current STD
           or new STD indicates a space switch event */
        if ((regs->cr[1] & STD_SSEVENT)
            || (aste[2] & STD_SSEVENT))
        {
            regs->psw.cc = 3;
            return;
        }

    }
    else
    {
        /* Load current PSTD and LTD or PASTEO */
        pstd = regs->cr[1];
        ltd = regs->cr[5];
        pasteo = regs->cr[5];
        ax = (regs->cr[4] & CR4_AX) >> 16;
    }

    /* If bit 30 of the LASP function bits is zero,
       use the current AX instead of the AX specified
       in the first operand */
    if ((effective_addr2 & 0x00000002))
        ax = ax_d;

    /* SASN translation */

    /* If new SASN = new PASN then set new SSTD = new PSTD */
    if (sasn_d == pasn_d)
    {
        sstd = pstd;

    }
    else
    {
        /* If new SASN = current SASN, and bit 29 of the LASP
       function bits is 0, and bit 31 of the LASP function bits
       is 1, use current SSTD in control register 7 */
        if (!(effective_addr2 & 0x00000004)
            && (effective_addr2 & 0x00000001)
            && (sasn_d == (regs->cr[3] & CR3_SASN)))
        {
            sstd = regs->cr[7];
        }
        else
        {
            /* Translate SASN and return condition code 2 if
               AFX- or ASX-translation exception condition */
            if (translate_asn (sasn_d, regs, &sasteo, aste))
            {
                regs->psw.cc = 2;
                return;
            }

            /* Obtain new SSTD from secondary ASTE */
            sstd = aste[2];

#ifdef FEATURE_SUBSPACE_GROUP
            /* Perform subspace replacement on new SSTD */
            sstd = subspace_replace (sstd, sasteo, &xcode, regs);

            /* Return condition code 2 if ASTE exception was recognized */
            if (xcode != 0)
            {
                regs->psw.cc = 2;
                return;
            }
#endif /*FEATURE_SUBSPACE_GROUP*/

            /* Perform SASN authorization if bit 31 of the
               LASP function bits is 0 */
            if (!(effective_addr2 & 0x00000001))
            {
                /* Condition code 2 if SASN authorization fails */
                if (authorize_asn (ax, aste, ATE_SECONDARY, regs))
                {
                    regs->psw.cc = 2;
                    return;
                }

            } /* end if(SASN authorization) */

        } /* end if(SASN translation) */

    } /* end if(SASN = PASN) */

    /* Perform control-register loading */
    regs->cr[1] = pstd;
    regs->cr[3] = (pkm_d << 16) | sasn_d;
    regs->cr[4] = (ax << 16) | pasn_d;
    regs->cr[5] = (regs->cr[0] & CR0_ASF) ? pasteo : ltd;
    regs->cr[7] = sstd;

    /* Return condition code zero */
    regs->psw.cc = 0;

} 


/*-------------------------------------------------------------------*/
/* B7   LCTL  - Load Control                                    [RS] */
/*-------------------------------------------------------------------*/
void zz_load_control (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
int     i, d;                           /* Integer work areas        */
BYTE    rwork[64];                      /* Register work areas       */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    PRIV_CHECK(regs);

    FW_CHECK(effective_addr2, regs);

    /* Calculate the number of bytes to be loaded */
    d = (((r3 < r1) ? r3 + 16 - r1 : r3 - r1) + 1) * 4;

    /* Fetch new control register contents from operand address */
    vfetchc ( rwork, d-1, effective_addr2, b2, regs );

    /* Load control registers from work area */
    for ( i = r1, d = 0; ; )
    {
        /* Load one control register from work area */
        regs->cr[i] = (rwork[d] << 24) | (rwork[d+1] << 16)
                    | (rwork[d+2] << 8) | rwork[d+3];
        d += 4;

        /* Instruction is complete when r3 register is done */
        if ( i == r3 ) break;

        /* Update register number, wrapping from 15 to 0 */
        i++; i &= 15;
    }
}



/*-------------------------------------------------------------------*/
/* 82   LPSW  - Load Program Status Word                         [S] */
/*-------------------------------------------------------------------*/
void zz_load_psw (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
DWORD   dword;
int     rc;                             

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    DW_CHECK(effective_addr2, regs);

    /* Perform serialization and checkpoint synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    /* Fetch new PSW from operand address */
    vfetchc ( dword, 7, effective_addr2, b2, regs );

    /* Load updated PSW */
    rc = load_psw ( &(regs->psw), dword );
    if ( rc )
        program_check (regs, rc);

    /* Perform serialization and checkpoint synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

}


/*-------------------------------------------------------------------*/
/* B1   LRA   - Load Real Address                               [RX] */
/*-------------------------------------------------------------------*/
void zz_load_real_address (BYTE inst[], int execflag, REGS *regs)
{
int     r1;                             /* Register number           */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U16     xcode;                          /* Exception code            */
int     private;                        /* 1=Private address space   */
int     protect;                        /* 1=ALE or page protection  */
int     stid;                           /* Segment table indication  */
int     cc;                             /* Condition code            */
U32     n;                              /* 32-bit operand values     */

    RX(inst, execflag, regs, r1, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Translate the effective address to a real address */
    cc = translate_addr (effective_addr2, b2, regs, ACCTYPE_LRA,
            &n, &xcode, &private, &protect, &stid, NULL, NULL);

    /* If ALET exception, set exception code in R1 bits 16-31
       set high order bit of R1, and set condition code 3 */
    if (cc == 4) {
        regs->gpr[r1] = 0x80000000 | xcode;
        regs->psw.cc = 3;
    }
    else
    {
        /* Set r1 and condition code as returned by translate_addr */
        regs->gpr[r1] = n;
        regs->psw.cc = cc;
    }

}


/*-------------------------------------------------------------------*/
/* B24B LURA  - Load Using Real Address                        [RRE] */
/*-------------------------------------------------------------------*/
void zz_load_using_real_address (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     n;                              /* Unsigned work             */

    RRE(inst, execflag, regs, r1, r2);

    PRIV_CHECK(regs);

    /* R2 register contains operand real storage address */
    n = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Program check if operand not on fullword boundary */
    FW_CHECK(n, regs);

    /* Load R1 register from second operand */
    regs->gpr[r1] = vfetch4 ( n, USE_REAL_ADDR, regs );

}


/*-------------------------------------------------------------------*/
/* B247 MSTA  - Modify Stacked State                           [RRE] */
/*-------------------------------------------------------------------*/
void zz_modify_stacked_state (BYTE inst[], int execflag, REGS *regs)
{
int     r1, unused;                     /* Values of R fields        */
LSED    lsed;                           /* Linkage stack entry desc. */
U32     lsea;                           /* Linkage stack entry addr  */
U32     abs;                            /* Absolute address          */

    RRE(inst, execflag, regs, r1, unused);

    ODD_CHECK(r1, regs);

    /* Find the virtual address of the entry descriptor
       of the current state entry in the linkage stack */
    lsea = locate_stack_entry (0, &lsed, regs);

    /* Point back to byte 152 of the state entry */
    lsea -= LSSE_SIZE - sizeof(LSED);
    lsea += 152;
    lsea &= 0x7FFFFFFF;

    /* Store the general register pair into the state entry */
    abs = abs_stack_addr (lsea, regs, ACCTYPE_WRITE);
    sysblk.mainstor[abs] = (regs->gpr[r1] >> 24) & 0xFF;
    sysblk.mainstor[abs+1] = (regs->gpr[r1] >> 16) & 0xFF;
    sysblk.mainstor[abs+2] = (regs->gpr[r1] >> 8) & 0xFF;
    sysblk.mainstor[abs+3] = regs->gpr[r1] & 0xFF;
    sysblk.mainstor[abs+4] = (regs->gpr[r1+1] >> 24) & 0xFF;
    sysblk.mainstor[abs+5] = (regs->gpr[r1+1] >> 16) & 0xFF;
    sysblk.mainstor[abs+6] = (regs->gpr[r1+1] >> 8) & 0xFF;
    sysblk.mainstor[abs+7] = regs->gpr[r1+1] & 0xFF;

}


/*-------------------------------------------------------------------*/
/* DA   MVCP  - Move to Primary                                 [SS] */
/*-------------------------------------------------------------------*/
void zz_move_to_primary (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b1, b2;                         /* Values of base registers  */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
int     cc;                             /* Condition code            */
int     j;                              /* Integer workarea          */
U32     n;                              /* Unsigned workarea         */

    SS(inst, execflag, regs, r1, r3, b1, effective_addr1,
                                     b2, effective_addr2);


    /* Program check if secondary space control (CR0 bit 5) is 0,
       or if DAT is off, or if in AR mode or home-space mode */
    if ((regs->cr[0] & CR0_SEC_SPACE) == 0
        || REAL_MODE(&regs->psw)
        || regs->psw.armode)
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

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
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);

    /* Move characters from secondary address space to primary
       address space using secondary key for second operand */
    if (n > 0)
        move_chars (effective_addr1, USE_PRIMARY_SPACE,
                    regs->psw.pkey,
                    effective_addr2, USE_SECONDARY_SPACE,
                    j, n-1, regs);

    /* Set condition code */
    regs->psw.cc = cc;

}


/*-------------------------------------------------------------------*/
/* DB   MVCS  - Move to Secondary                               [SS] */
/*-------------------------------------------------------------------*/
void zz_move_to_secondary (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b1, b2;                         /* Values of base registers  */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
int     cc;                             /* Condition code            */
int     j;                              /* Integer workarea          */
U32     n;                              /* Unsigned workarea         */

    SS(inst, execflag, regs, r1, r3, b1, effective_addr1,
                                     b2, effective_addr2);

    /* Program check if secondary space control (CR0 bit 5) is 0,
       or if DAT is off, or if in AR mode or home-space mode */
    if ((regs->cr[0] & CR0_SEC_SPACE) == 0
        || REAL_MODE(&regs->psw)
        || regs->psw.armode)
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

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
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);

    /* Move characters from primary address space to secondary
       address space using secondary key for first operand */
    if (n > 0)
        move_chars (effective_addr1, USE_SECONDARY_SPACE, j,
                    effective_addr2, USE_PRIMARY_SPACE,
                    regs->psw.pkey, n-1, regs);

    /* Set condition code */
    regs->psw.cc = cc;

}


/*-------------------------------------------------------------------*/
/* E50F MVCDK - Move with Destination Key                      [SSE] */
/*-------------------------------------------------------------------*/
void zz_move_with_destination_key (BYTE inst[], int execflag, REGS *regs)
{
int     b1, b2;                         /* Values of base registers  */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
int     i, j;                           /* Integer workarea          */

    SSE(inst, execflag, regs, b1, effective_addr1, b2, effective_addr2); 

    /* Load operand length-1 from register 0 bits 24-31 */
    i = regs->gpr[0] & 0xFF;

    /* Load destination key from register 1 bits 24-27 */
    j = regs->gpr[1] & 0xF0;

    /* Program check if in problem state and key mask in
       CR3 bits 0-15 is not 1 for the specified key */
    if ( regs->psw.prob
        && ((regs->cr[3] << (j >> 4)) & 0x80000000) == 0 )
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);

    /* Move characters using destination key for operand 1 */
    move_chars (effective_addr1, b1, j,
                effective_addr2, b2, regs->psw.pkey,
                i, regs);

}


/*-------------------------------------------------------------------*/
/* D9   MVCK  - Move with Key                                   [SS] */
/*-------------------------------------------------------------------*/
void zz_move_with_key (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b1, b2;                         /* Values of base registers  */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
int     cc;                             /* Condition code            */
int     j;                              /* Integer workarea          */
U32     n;                              /* Unsigned workarea         */

    SS(inst, execflag, regs, r1, r3, b1, effective_addr1,
                                     b2, effective_addr2);

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
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);

    /* Move characters using source key for second operand */
    if (n > 0)
        move_chars (effective_addr1, b1, regs->psw.pkey,
                    effective_addr2, b2, j, n-1, regs);

    /* Set condition code */
    regs->psw.cc = cc;

}


/*-------------------------------------------------------------------*/
/* E50E MVCSK - Move with Source Key                           [SSE] */
/*-------------------------------------------------------------------*/
void zz_move_with_source_key (BYTE inst[], int execflag, REGS *regs)
{
int     b1, b2;                         /* Values of base registers  */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
int     i, j;                           /* Integer workarea          */

    SSE(inst, execflag, regs, b1, effective_addr1, b2, effective_addr2); 

    /* Load operand length-1 from register 0 bits 24-31 */
    i = regs->gpr[0] & 0xFF;

    /* Load source key from register 1 bits 24-27 */
    j = regs->gpr[1] & 0xF0;

    /* Program check if in problem state and key mask in
       CR3 bits 0-15 is not 1 for the specified key */
    if ( regs->psw.prob
        && ((regs->cr[3] << (j >> 4)) & 0x80000000) == 0 )
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);

    /* Move characters using source key for second operand */
    move_chars (effective_addr1, b1, regs->psw.pkey,
                effective_addr2, b2, j, i, regs);

}


/*-------------------------------------------------------------------*/
/* B218 PC    - Program Call                                     [S] */
/*-------------------------------------------------------------------*/
void zz_program_call (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     abs;                            /* Absolute address          */
U32     ltd;                            /* Linkage table designation */
U32     pstd;                           /* Primary STD               */
U32     pasteo;                         /* Primary ASTE origin       */
U32     lto;                            /* Linkage table origin      */
int     ltl;                            /* Linkage table length      */
U32     lte;                            /* Linkage table entry       */
U32     eto;                            /* Entry table origin        */
int     etl;                            /* Entry table length        */
U32     ete[8];                         /* Entry table entry         */
int     numwords;                       /* ETE size (4 or 8 words)   */
int     i;                              /* Array subscript           */
int     ssevent = 0;                    /* 1=space switch event      */
U32     retn;                           /* Return address and amode  */
U32     aste[16];                       /* ASN second table entry    */
U16     xcode;                          /* Exception code            */
U16     pasn;                           /* Primary ASN               */
U32     csi = 0;                        /* Called Space ID           */
#ifdef FEATURE_TRACING
U32     newcr12 = 0;                    /* CR12 upon completion      */
#endif /*FEATURE_TRACING*/

    S(inst, execflag, regs, b2, effective_addr2);

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    /* Special operation exception if DAT is off, or if
       in secondary space mode or home space mode */
    if (REAL_MODE(&(regs->psw)) || regs->psw.space == 1)
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    /* [5.5.3.1] Load the linkage table designation */
    if ((regs->cr[0] & CR0_ASF) == 0)
    {
        /* Obtain the LTD from control register 5 */
        ltd = regs->cr[5];
    }
    else
    {
        /* Obtain the primary ASTE origin from control register 5 */
        pasteo = regs->cr[5] & CR5_PASTEO;
        pasteo = APPLY_PREFIXING (pasteo, regs->pxr);

        /* Program check if PASTE is outside main storage */
        if (pasteo >= sysblk.mainsize)
            program_check (regs, PGM_ADDRESSING_EXCEPTION);

        /* Fetch LTD from PASTE word 3 */
        ltd = fetch_fullword_absolute(pasteo+12);
    }

#ifdef FEATURE_TRACING
    /* Form trace entry if ASN tracing is active */
    if (regs->cr[12] & CR12_ASNTRACE)
        newcr12 = trace_pc (effective_addr2, regs);
#endif /*FEATURE_TRACING*/

    /* Special operation exception if subsystem linkage
       control bit in linkage table designation is zero */
    if ((ltd & LTD_SSLINK) == 0)
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    /* [5.5.3.2] Linkage table lookup */

    /* Extract the linkage table origin and length from the LTD */
    lto = ltd & LTD_LTO;
    ltl = ltd & LTD_LTL;

    /* Program check if linkage index is outside the linkage table */
    if (ltl < ((effective_addr2 & PC_LX) >> 13))
    {
        regs->tea = effective_addr2;
        program_check (regs, PGM_LX_TRANSLATION_EXCEPTION);
    }

    /* Calculate the address of the linkage table entry */
    lto += (effective_addr2 & PC_LX) >> 6;
    lto &= 0x7FFFFFFF;

    /* Program check if linkage table entry is outside real storage */
    if (lto >= sysblk.mainsize)
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Fetch linkage table entry from real storage.  All bytes
       must be fetched concurrently as observed by other CPUs */
    lto = APPLY_PREFIXING (lto, regs->pxr);
    lte = fetch_fullword_absolute(lto);

    /* Program check if linkage entry invalid bit is set */
    if (lte & LTE_INVALID)
    {
        regs->tea = effective_addr2;
        program_check (regs, PGM_LX_TRANSLATION_EXCEPTION);
    }

    /* [5.5.3.3] Entry table lookup */

    /* Extract the entry table origin and length from the LTE */
    eto = lte & LTE_ETO;
    etl = lte & LTE_ETL;

    /* Program check if entry index is outside the entry table */
    if (etl < ((effective_addr2 & PC_EX) >> 2))
    {
        regs->tea = effective_addr2;
        program_check (regs, PGM_EX_TRANSLATION_EXCEPTION);
    }

    /* Calculate the starting address of the entry table entry */
    eto += (effective_addr2 & PC_EX) << ((regs->cr[0] & CR0_ASF) ? 5 : 4);
    eto &= 0x7FFFFFFF;

    /* Determine the size of the entry table entry */
    numwords = (regs->cr[0] & CR0_ASF) ? 8 : 4;

    /* Fetch the 4- or 8-word entry table entry from real
       storage.  Each fullword of the ETE must be fetched
       concurrently as observed by other CPUs */
    for (i = 0; i < numwords; i++)
    {
        /* Program check if address is outside main storage */
        abs = APPLY_PREFIXING (eto, regs->pxr);
        if (abs >= sysblk.mainsize)
            program_check (regs, PGM_ADDRESSING_EXCEPTION);

        /* Fetch one word of the entry table entry */
        ete[i] = fetch_fullword_absolute (abs);
        eto += 4;
        eto &= 0x7FFFFFFF;
    }

    /* Clear remaining words if fewer than 8 words were loaded */
    while (i < 8) ete[i++] = 0;

    /* Program check if basic program call in AR mode */
    if ((ete[4] & ETE4_T) == 0 && regs->psw.armode)
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    /* Program check if addressing mode is zero and the
       entry instruction address is not a 24-bit address */
    if ((ete[1] & ETE1_AMODE) == 0
        && (ete[1] & ETE1_EIA) > 0x00FFFFFF)
        program_check (regs, PGM_PC_TRANSLATION_SPECIFICATION_EXCEPTION);

    /* Program check if in problem state and the PKM in control
       register 3 produces zero when ANDed with the AKM in the ETE */
    if (regs->psw.prob
        && ((regs->cr[3] & CR3_KEYMASK) & (ete[0] & ETE0_AKM)) == 0)
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);

    /* Obtain the new primary ASN from the entry table */
    pasn = ete[0] & ETE0_ASN;

    /* Perform ASN translation if ASN is non-zero */
    if (pasn != 0)
    {
        /* Program check if ASN translation control is zero */
        if ((regs->cr[14] & CR14_ASN_TRAN) == 0)
            program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

        /* Perform ASN translation to obtain ASTE */
        xcode = translate_asn (pasn, regs, &pasteo, aste);

        /* Program check if ASN translation exception */
        if (xcode != 0)
            program_check (regs, xcode);

        /* Obtain the new PSTD from the ASTE */
        pstd = aste[2];

#ifdef FEATURE_SUBSPACE_GROUP
        /* Perform subspace replacement on new PSTD */
        pstd = subspace_replace (pstd, pasteo, NULL, regs);
#endif /*FEATURE_SUBSPACE_GROUP*/

    } /* end if(PC-ss) */
    else
    { /* PC-cp */

        /* For PC to current primary, load current primary STD */
        pstd = regs->cr[1];

    } /* end if(PC-cp) */

    /* Perform basic or stacking program call */
    if ((ete[4] & ETE4_T) == 0)
    {
        /* For basic PC, load linkage info into general register 14 */
        regs->gpr[14] = regs->psw.ia | regs->psw.prob;
        if (regs->psw.amode) regs->gpr[14] |= 0x80000000;

        /* Update the PSW from the entry table */
        regs->psw.amode = (ete[1] & ETE1_AMODE) ? 1 : 0;
        regs->psw.ia = ete[1] & ETE1_EIA;
        regs->psw.prob = (ete[1] & ETE1_PROB) ? 1 : 0;

        /* Load the current PKM and PASN into general register 3 */
        regs->gpr[3] = (regs->cr[3] & CR3_KEYMASK)
                        | (regs->cr[4] & CR4_PASN);

        /* OR the EKM into the current PKM */
        regs->cr[3] |= (ete[3] & ETE3_EKM);

        /* Load the entry parameter into general register 4 */
        regs->gpr[4] = ete[2];

    } /* end if(basic PC) */
    else
    { /* stacking PC */

#ifdef FEATURE_CALLED_SPACE_IDENTIFICATION
        /* Set the called space identification */
        csi = (pasn == 0) ? 0 : pasn << 16 | (aste[5] & 0x0000FFFF);
#endif /*FEATURE_CALLED_SPACE_IDENTIFICATION*/

        /* Perform the stacking process */
        retn = regs->psw.ia;
        if (regs->psw.amode) retn |= 0x80000000;
        form_stack_entry (LSED_UET_PC, retn, effective_addr2, csi, regs);

        /* Update the PSW from the entry table */
        regs->psw.amode = (ete[1] & ETE1_AMODE) ? 1 : 0;
        regs->psw.ia = ete[1] & ETE1_EIA;
        regs->psw.prob = (ete[1] & ETE1_PROB) ? 1 : 0;

        /* Replace the PSW key by the entry key if the K bit is set */
        if (ete[4] & ETE4_K)
            regs->psw.pkey = (ete[4] & ETE4_EK) >> 16;

        /* Replace the PSW key mask by the EKM if the M bit is set,
           otherwise OR the EKM into the current PSW key mask */
        if (ete[4] & ETE4_M)
            regs->cr[3] &= ~CR3_KEYMASK;
        regs->cr[3] |= (ete[3] & ETE3_EKM);

        /* Replace the EAX key by the EEAX if the E bit is set */
        if (ete[4] & ETE4_E)
        {
            regs->cr[8] &= ~CR8_EAX;
            regs->cr[8] |= (ete[4] & ETE4_EEAX) << 16;
        }

        /* Set the access mode according to the C bit */
        regs->psw.armode = (ete[4] & ETE4_C) ? 1 : 0;

        /* Load the entry parameter into general register 4 */
        regs->gpr[4] = ete[2];

    } /* end if(stacking PC) */

    /* If new ASN is zero, perform program call to current primary */
    if (pasn == 0)
    {
        /* Set SASN equal to PASN */
        regs->cr[3] &= ~CR3_SASN;
        regs->cr[3] |= (regs->cr[4] & CR4_PASN);

        /* Set SSTD equal to PSTD */
        regs->cr[7] = regs->cr[1];

    } /* end if(PC-cp) */
    else
    { /* Program call with space switching */

        /* Set SASN and SSTD equal to current PASN and PSTD */
        regs->cr[3] &= ~CR3_SASN;
        regs->cr[3] |= (regs->cr[4] & CR4_PASN);
        regs->cr[7] = regs->cr[1];

        /* Set flag if either the current or new PSTD indicates
           a space switch event, or if PER mode is set */
        if ((regs->cr[1] & STD_SSEVENT) || (aste[2] & STD_SSEVENT)
            || (regs->psw.sysmask & PSW_PERMODE))
        {
            /* [6.5.2.34] Set the translation exception address equal
               to the old primary ASN, with the high-order bit set if
               the old primary space-switch-event control bit is one */
            regs->tea = regs->cr[4] & CR4_PASN;
            if (regs->cr[1] & STD_SSEVENT)
                regs->tea |= TEA_SSEVENT;

            /* Indicate space-switch event required */
            ssevent = 1;
        }

        /* Obtain new AX from the ASTE and new PASN from the ET */
        regs->cr[4] = (aste[1] & ASTE1_AX) | pasn;

        /* Load the new primary STD */
        regs->cr[1] = pstd;

        /* Update control register 5 with the new PASTEO or LTD */
        regs->cr[5] = (regs->cr[0] & CR0_ASF) ? pasteo : aste[3];

        /* For stacking PC when the S-bit in the entry table is
           one, set SASN and SSTD equal to new PASN and PSTD */
        if ((ete[4] & ETE4_T) && (ete[4] & ETE4_S))
        {
            regs->cr[3] &= ~CR3_SASN;
            regs->cr[3] |= (regs->cr[4] & CR4_PASN);
            regs->cr[7] = regs->cr[1];
        }

    } /* end if(PC-ss) */

#ifdef FEATURE_TRACING
    /* Update trace table address if ASN tracing is active */
    if (regs->cr[12] & CR12_ASNTRACE)
        regs->cr[12] = newcr12;
#endif /*FEATURE_TRACING*/

    /* Generate space switch event if required */
    if (ssevent)
        program_check (regs, PGM_SPACE_SWITCH_EVENT);

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

}


/*-------------------------------------------------------------------*/
/* 0101 PR    - Program Return                                   [E] */
/*-------------------------------------------------------------------*/
void zz_program_return (BYTE inst[], int execflag, REGS *regs)
{
REGS    newregs;                        /* Copy of CPU registers     */
int     etype;                          /* Entry type unstacked      */
int     ssevent = 0;                    /* 1=space switch event      */
U32     alsed;                          /* Absolute addr of LSED of
                                           previous stack entry      */
LSED   *lsedp;                          /* -> LSED in main storage   */
U32     aste[16];                       /* ASN second table entry    */
U32     pasteo;                         /* Primary ASTE origin       */
U32     sasteo;                         /* Secondary ASTE origin     */
U16     oldpasn;                        /* Original primary ASN      */
U16     pasn;                           /* New primary ASN           */
U16     sasn;                           /* New secondary ASN         */
U16     ax;                             /* Authorization index       */
U16     xcode;                          /* Exception code            */

    E(inst, execflag, regs);

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    /* Create a working copy of the CPU registers */
    newregs = *regs;

    /* Save the current primary ASN from CR4 bits 16-31 */
    oldpasn = regs->cr[4] & CR4_PASN;

    /* Perform the unstacking process */
    etype = program_return_unstack (&newregs, &alsed);

    /* Perform PR-cp or PR-ss if unstacked entry was a program call */
    if (etype == LSED_UET_PC)
    {
        /* Extract the new primary ASN from CR4 bits 16-31 */
        pasn = newregs.cr[4] & CR4_PASN;

#ifdef FEATURE_TRACING
        /* Perform tracing if ASN tracing is on */
        if (regs->cr[12] & CR12_ASNTRACE)
            newregs.cr[12] = trace_pr (&newregs, regs);
#endif /*FEATURE_TRACING*/

        /* Perform PASN translation if new PASN not equal old PASN */
        if (pasn != oldpasn)
        {
            /* Special operation exception if ASN translation
               control (control register 14 bit 12) is zero */
            if ((regs->cr[14] & CR14_ASN_TRAN) == 0)
                program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

            /* Translate new primary ASN to obtain ASTE */
            xcode = translate_asn (pasn, &newregs, &pasteo, aste);

            /* Program check if ASN translation exception */
            if (xcode != 0)
                program_check (&newregs, xcode);

            /* Space switch if either current PSTD or new PSTD
               space-switch-event control bit is set to 1 */
            if ((regs->cr[1] & STD_SSEVENT) || (aste[2] & STD_SSEVENT))
            {
                /* [6.5.2.34] Set translation exception address equal
                   to old primary ASN, and set high-order bit if old
                   primary space-switch-event control bit is one */
                newregs.tea = regs->cr[4] & CR4_PASN;
                if (newregs.cr[1] & STD_SSEVENT)
                    newregs.tea |= TEA_SSEVENT;

                /* Indicate space-switch event required */
                ssevent = 1;
            }

            /* Obtain new PSTD and AX from the ASTE */
            newregs.cr[1] = aste[2];
            newregs.cr[4] &= ~CR4_AX;
            newregs.cr[4] |= aste[1] & ASTE1_AX;

            /* Load CR5 with the primary ASTE origin address */
            newregs.cr[5] = pasteo;

#ifdef FEATURE_SUBSPACE_GROUP
            /* Perform subspace replacement on new PSTD */
            newregs.cr[1] = subspace_replace (newregs.cr[1],
                                            pasteo, NULL, &newregs);
#endif /*FEATURE_SUBSPACE_GROUP*/

        } /* end if(pasn!=oldpasn) */

        /* Extract the new secondary ASN from CR3 bits 16-31 */
        sasn = newregs.cr[3] & CR3_SASN;

        /* Set SSTD = PSTD if new SASN is equal to new PASN */
        if (sasn == pasn)
        {
            newregs.cr[7] = newregs.cr[1];
        }
        else /* sasn != pasn */
        {
            /* Special operation exception if ASN translation
               control (control register 14 bit 12) is zero */
            if ((regs->cr[14] & CR14_ASN_TRAN) == 0)
                program_check (&newregs, PGM_SPECIAL_OPERATION_EXCEPTION);

            /* Translate new secondary ASN to obtain ASTE */
            xcode = translate_asn (sasn, &newregs, &sasteo, aste);

            /* Program check if ASN translation exception */
            if (xcode != 0)
                program_check (&newregs, xcode);

            /* Obtain new SSTD from secondary ASTE */
            newregs.cr[7] = aste[2];

            /* Perform SASN authorization using new AX */
            ax = (newregs.cr[4] & CR4_AX) >> 16;
            if (authorize_asn (ax, aste, ATE_SECONDARY, &newregs))
            {
                newregs.tea = sasn;
                program_check (&newregs, PGM_SECONDARY_AUTHORITY_EXCEPTION);
            }

#ifdef FEATURE_SUBSPACE_GROUP
            /* Perform subspace replacement on new SSTD */
            newregs.cr[7] = subspace_replace (newregs.cr[7],
                                            sasteo, NULL, &newregs);
#endif /*FEATURE_SUBSPACE_GROUP*/

        } /* end else(sasn!=pasn) */

    } /* end if(LSED_UET_PC) */

    /* Update the updated CPU registers from the working copy */
    /* *regs = newregs;   ZZDEBUG */
    memcpy(regs->gpr, &newregs.gpr, sizeof(newregs.gpr));
    memcpy(regs->ar, &newregs.ar, sizeof(newregs.ar));
    memcpy(regs->cr, &newregs.cr, sizeof(newregs.cr));
    memcpy(&regs->psw, &newregs.psw, sizeof(newregs.psw));

    /* Set the main storage reference and change bits */
    STORAGE_KEY(alsed) |= (STORKEY_REF | STORKEY_CHANGE);

    /* [5.12.4.4] Clear the next entry size field of the linkage
       stack entry now pointed to by control register 15 */
    lsedp = (LSED*)(sysblk.mainstor + alsed);
    lsedp->nes[0] = 0;
    lsedp->nes[1] = 0;

    /* Generate space switch event if required */
    if (ssevent)
        program_check (&newregs, PGM_SPACE_SWITCH_EVENT);

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

}


/*-------------------------------------------------------------------*/
/* B228 PT    - Program Transfer                               [RRE] */
/*-------------------------------------------------------------------*/
void zz_program_transfer (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U16     pkm;                            /* New program key mask      */
U16     pasn;                           /* New primary ASN           */
int     amode;                          /* New amode                 */
U32     ia;                             /* New instruction address   */
int     prob;                           /* New problem state bit     */
U32     ltd;                            /* Linkage table designation */
U32     pasteo;                         /* Primary ASTE origin       */
U32     aste[16];                       /* ASN second table entry    */
U32     pstd;                           /* Primary STD               */
U16     ax;                             /* Authorization index       */
U16     xcode;                          /* Exception code            */
int     ssevent = 0;                    /* 1=space switch event      */
#ifdef FEATURE_TRACING
U32     newcr12 = 0;                    /* CR12 upon completion      */
#endif /*FEATURE_TRACING*/

    RRE(inst, execflag, regs, r1, r2);

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    /* Special operation exception if DAT is off, or
       not in primary space mode */
    if (REAL_MODE(&(regs->psw))
        || !PRIMARY_SPACE_MODE(&(regs->psw)))
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    /* Extract the PSW key mask from R1 register bits 0-15 */
    pkm = regs->gpr[r1] >> 16;

    /* Extract the ASN from R1 register bits 16-31 */
    pasn = regs->gpr[r1] & 0xFFFF;

#ifdef FEATURE_TRACING
    /* Build trace entry if ASN tracing is on */
    if (regs->cr[12] & CR12_ASNTRACE)
        newcr12 = trace_pt (pasn, regs->gpr[r2], regs);
#endif /*FEATURE_TRACING*/

    /* Extract the amode bit from R2 register bit 0 */
    amode = (regs->gpr[r2] & 0x80000000) ? 1 : 0;

    /* Extract the instruction address from R2 bits 1-30 */
    ia = regs->gpr[r2] & 0x7FFFFFFE;

    /* Extract the problem state bit from R2 register bit 31 */
    prob = regs->gpr[r2] & 0x00000001;

    /* [5.5.3.1] Load the linkage table designation */
    if ((regs->cr[0] & CR0_ASF) == 0)
    {
        /* Obtain the LTD from control register 5 */
        ltd = regs->cr[5];
    }
    else
    {
        /* Obtain the primary ASTE origin from control register 5 */
        pasteo = regs->cr[5] & CR5_PASTEO;
        pasteo = APPLY_PREFIXING (pasteo, regs->pxr);

        /* Program check if PASTE is outside main storage */
        if (pasteo >= sysblk.mainsize)
            program_check (regs, PGM_ADDRESSING_EXCEPTION);

        /* Fetch LTD from PASTE word 3 */
        ltd = fetch_fullword_absolute(pasteo+12);
    }

    /* Special operation exception if subsystem linkage
       control bit in linkage table designation is zero */
    if ((ltd & LTD_SSLINK) == 0)
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    /* Privileged operation exception if in problem state and
       problem bit indicates a change to supervisor state */
    if (regs->psw.prob && prob == 0)
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);

    /* Specification exception if amode is zero and
       new instruction address is not a 24-bit address */
    if (amode == 0 && ia > 0x00FFFFFF)
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Space switch if ASN not equal to current PASN */
    if (pasn != (regs->cr[4] & CR4_PASN))
    {
        /* Special operation exception if ASN translation
           control (control register 14 bit 12) is zero */
        if ((regs->cr[14] & CR14_ASN_TRAN) == 0)
            program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

        /* Translate ASN and generate program check if
           AFX- or ASX-translation exception condition */
        xcode = translate_asn (pasn, regs, &pasteo, aste);
        if (xcode != 0)
            program_check (regs, xcode);

        /* Perform primary address space authorization
           using current authorization index */
        ax = (regs->cr[4] & CR4_AX) >> 16;
        if (authorize_asn (ax, aste, ATE_PRIMARY, regs))
        {
            regs->tea = pasn;
            program_check (regs, PGM_PRIMARY_AUTHORITY_EXCEPTION);
        }

        /* Obtain new primary STD from the ASTE */
        pstd = aste[2];

#ifdef FEATURE_SUBSPACE_GROUP
        /* Perform subspace replacement on new PSTD */
        pstd = subspace_replace (pstd, pasteo, NULL, regs);
#endif /*FEATURE_SUBSPACE_GROUP*/

        /* Space switch if either current PSTD or new PSTD
           space-switch-event control bit is set to 1 */
        if ((regs->cr[1] & STD_SSEVENT) || (pstd & STD_SSEVENT))
        {
            /* [6.5.2.34] Set the translation exception address equal
               to the old primary ASN, with the high-order bit set if
               the old primary space-switch-event control bit is one */
            regs->tea = regs->cr[4] & CR4_PASN;
            if (regs->cr[1] & STD_SSEVENT)
                regs->tea |= TEA_SSEVENT;

            /* Indicate space-switch event required */
            ssevent = 1;
        }

        /* Load new primary STD into control register 1 */
        regs->cr[1] = pstd;

        /* Load new AX and PASN into control register 4 */
        regs->cr[4] = (aste[1] & ASTE1_AX) | pasn;

        /* Load new PASTEO or LTD into control register 5 */
        regs->cr[5] = (regs->cr[0] & CR0_ASF) ? pasteo : aste[3];

    } /* end if(PT-ss) */
    else
    {
        /* For PT-cp use current primary STD */
        pstd = regs->cr[1];
    }

#ifdef FEATURE_TRACING
    /* Update trace table address if ASN tracing is on */
    if (regs->cr[12] & CR12_ASNTRACE)
        regs->cr[12] = newcr12;
#endif /*FEATURE_TRACING*/

    /* Replace PSW amode, instruction address, and problem state bit */
    regs->psw.amode = amode;
    regs->psw.ia = ia;
    regs->psw.prob = prob;

    /* AND control register 3 bits 0-15 with the supplied PKM value
       and replace the SASN in CR3 bits 16-31 with new PASN */
    regs->cr[3] &= (pkm << 16);
    regs->cr[3] |= pasn;

    /* Set secondary STD equal to new primary STD */
    regs->cr[7] = pstd;

    /* Generate space switch event if required */
    if (ssevent)
        program_check (regs, PGM_SPACE_SWITCH_EVENT);

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

}


/*-------------------------------------------------------------------*/
/* B248 PALB  - Purge ALB                                      [RRE] */
/*-------------------------------------------------------------------*/
void zz_purge_alb (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Register values (unused)  */

    RRE(inst, execflag, regs, r1, r2);

    PRIV_CHECK(regs);

    /* Purge the ART lookaside buffer for this CPU */
    purge_alb (regs);

}


/*-------------------------------------------------------------------*/
/* B20D PTLB  - Purge TLB                                        [S] */
/*-------------------------------------------------------------------*/
void zz_purge_tlb (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Purge the translation lookaside buffer for this CPU */
    purge_tlb (regs);

}


/*-------------------------------------------------------------------*/
/* B213 RRB   - Reset Reference Bit                              [S] */
/*-------------------------------------------------------------------*/
void zz_reset_reference_bit (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* Absolute storage addr     */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Load 2K block real address from operand address */
    n = effective_addr2 & 0x00FFF800;

    /* Convert real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Addressing exception if block is outside main storage */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Set the condition code according to the original state
       of the reference and change bits in the storage key */
    regs->psw.cc =
       ((STORAGE_KEY(n) & STORKEY_REF) ? 2 : 0)
       | ((STORAGE_KEY(n) & STORKEY_CHANGE) ? 1 : 0);

    /* Reset the reference bit in the storage key */
    STORAGE_KEY(n) &= ~(STORKEY_REF);

}


/*-------------------------------------------------------------------*/
/* B22A RRBE  - Reset Reference Bit Extended                   [RRE] */
/*-------------------------------------------------------------------*/
void zz_reset_reference_bit_extended (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Register values           */
U32     n;                              /* Abs frame addr stor key   */

    RRE(inst, execflag, regs, r1, r2);

    PRIV_CHECK(regs);

    /* Load 4K block address from R2 register */
    n = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Convert real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Addressing exception if block is outside main storage */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Set the condition code according to the original state
       of the reference and change bits in the storage key */
    regs->psw.cc =
       ((STORAGE_KEY(n) & STORKEY_REF) ? 2 : 0)
       | ((STORAGE_KEY(n) & STORKEY_CHANGE) ? 1 : 0);

    /* Reset the reference bit in the storage key */
    STORAGE_KEY(n) &= ~(STORKEY_REF);

}


/*-------------------------------------------------------------------*/
/* B219 SAC   - Set Address Space Control                        [S] */
/* B279 SAC   - Set Address Space Control Fast                   [S] */
/*-------------------------------------------------------------------*/
void zz_set_address_space_control_x (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
BYTE    mode;                           /* New addressing mode       */
BYTE    oldmode;                        /* Current addressing mode   */
int     ssevent = 0;                    /* 1=space switch event      */

    S(inst, execflag, regs, b2, effective_addr2);

    if(inst[1] == 0x19)
    {
        /* Perform serialization and checkpoint-synchronization */
        PERFORM_SERIALIZATION (regs);
        PERFORM_CHKPT_SYNC (regs);
    }

    /* Isolate bits 20-23 of effective address */
    mode = (effective_addr2 & 0x00000F00) >> 8;

    /* Special operation exception if DAT is off or
       secondary-space control bit is zero */
    if (REAL_MODE(&(regs->psw))
         || (regs->cr[0] & CR0_SEC_SPACE) == 0)
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    /* Privileged operation exception if setting home-space
       mode while in problem state */
    if (mode == 3 && regs->psw.prob)
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);

    /* Special operation exception if setting AR mode
       and address-space function control bit is zero */
    if (mode == 2 && (regs->cr[0] & CR0_ASF) == 0)
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    /* Specification exception if mode is invalid */
    if (mode > 3)
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Save the current address-space control bits */
    oldmode = (regs->psw.armode << 1) | (regs->psw.space);

    /* Reset the address-space control bits in the PSW */
    regs->psw.space = mode & 1;
    regs->psw.armode = mode >> 1;

    /* If switching into or out of home-space mode, and also:
       primary space-switch-event control bit is set; or
       home space-switch-event control bit is set; or
       PER event is to be indicated
       then indicate a space-switch-event */
    if (((oldmode != 3 && mode == 3) || (oldmode == 3 && mode != 3))
         && ( regs->cr[1] & STD_SSEVENT
              || regs->cr[13] & STD_SSEVENT
              || regs->psw.sysmask & PSW_PERMODE ))
      {
        /* Indicate space-switch event required */
        ssevent = 1;

        /* [6.5.2.34] Set the translation exception address */
        if (mode == 3)
        {
            /* When switching into home-space mode, set the
               translation exception address equal to the primary
               ASN, with the high-order bit set equal to the value
               of the primary space-switch-event control bit */
            regs->tea = regs->cr[4] & CR4_PASN;
            if (regs->cr[1] & STD_SSEVENT)
                regs->tea |= TEA_SSEVENT;
        }
        else
        {
            /* When switching out of home-space mode, set the
               translation exception address equal to zero, with
               the high-order bit set equal to the value of the
               home space-switch-event control bit */
            regs->tea = 0;
            if (regs->cr[13] & STD_SSEVENT)
                regs->tea |= TEA_SSEVENT;
        }
    }

    /* Generate a space-switch-event if indicated */
    if (ssevent)
        program_check (regs, PGM_SPACE_SWITCH_EVENT);

    if(inst[1] == 0x19)
    {
        /* Perform serialization and checkpoint-synchronization */
        PERFORM_SERIALIZATION (regs);
        PERFORM_CHKPT_SYNC (regs);
    }

}


/*-------------------------------------------------------------------*/
/* B204 SCK   - Set Clock                                        [S] */
/*-------------------------------------------------------------------*/
void zz_set_clock (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U64     dreg;                           /* Clock value               */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    DW_CHECK(effective_addr2, regs);

    /* Fetch new TOD clock value from operand address */
    dreg = vfetch8 ( effective_addr2, b2, regs);

    /* Obtain the TOD clock update lock */
    obtain_lock (&sysblk.todlock);

    /* Compute the new TOD clock offset in microseconds */
    sysblk.todoffset += (dreg >> 12) - (sysblk.todclk >> 4);

    /* Set the new TOD clock value */
    sysblk.todclk = (dreg & 0xFFFFFFFFFFFFF000ULL) >> 8;

    /* Release the TOD clock update lock */
    release_lock (&sysblk.todlock);

//  /*debug*/logmsg("Set TOD clock=%16.16llX\n", dreg);

    /* Return condition code zero */
    regs->psw.cc = 0;

}


/*-------------------------------------------------------------------*/
/* B206 SCKC  - Set Clock Comparator                             [S] */
/*-------------------------------------------------------------------*/
void zz_set_clock_comparator (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U64     dreg;                           /* Clock value               */


    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    DW_CHECK(effective_addr2, regs);

    /* Fetch clock comparator value from operand location */
    dreg = vfetch8 ( effective_addr2, b2, regs )
                & 0xFFFFFFFFFFFFF000ULL;

//  /*debug*/logmsg("Set clock comparator=%16.16llX\n", dreg);

    /* Obtain the TOD clock update lock */
    obtain_lock (&sysblk.todlock);

    /* Update the clock comparator and set epoch to zero */
    regs->clkc = dreg >> 8;

    /* reset the clock comparator pending flag according to
       the setting of the tod clock */
    if( sysblk.todclk > regs->clkc )
        regs->cpuint = regs->ckpend = 1;
    else
        regs->ckpend = 0;

    /* Release the TOD clock update lock */
    release_lock (&sysblk.todlock);

}


/*-------------------------------------------------------------------*/
/* 0107 SCKPF - Set Clock Programmable Field                     [E] */
/*-------------------------------------------------------------------*/
void zz_set_clock_programmable_field (BYTE inst[], int execflag, REGS *regs)
{
    E(inst, execflag, regs);

    PRIV_CHECK(regs);

    /* Program check if register 0 bits 0-15 are not zeroes */
    if ( regs->gpr[0] & 0xFFFF0000 )
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Set TOD programmable register from register 0 */
    regs->todpr = regs->gpr[0] & 0x0000FFFF;
}


/*-------------------------------------------------------------------*/
/* B208 SPT   - Set CPU Timer                                    [S] */
/*-------------------------------------------------------------------*/
void zz_set_cpu_timer (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U64     dreg;                           /* Timer value               */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    DW_CHECK(effective_addr2, regs);

    /* Fetch the CPU timer value from operand location */
    dreg = vfetch8 ( effective_addr2, b2, regs )
                & 0xFFFFFFFFFFFFF000ULL;

    /* Obtain the TOD clock update lock */
    obtain_lock (&sysblk.todlock);

    /* Update the CPU timer */
    regs->ptimer = dreg;

    /* reset the cpu timer pending flag according to its value */
    if( (S64)regs->ptimer < 0 )
        regs->cpuint = regs->ptpend = 1;
    else
        regs->ptpend = 0;

    /* Release the TOD clock update lock */
    release_lock (&sysblk.todlock);

//  /*debug*/logmsg("Set CPU timer=%16.16llX\n", dreg);

}


/*-------------------------------------------------------------------*/
/* B210 SPX   - Set Prefix                                       [S] */
/*-------------------------------------------------------------------*/
void zz_set_prefix (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* Prefix value              */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    FW_CHECK(effective_addr2, regs);

    /* Perform serialization before fetching the operand */
    PERFORM_SERIALIZATION (regs);

    /* Load new prefix value from operand address */
    n = vfetch4 ( effective_addr2, b2, regs );

    /* Isolate bits 1-19 of new prefix value */
    n &= 0x7FFFF000;

    /* Program check if prefix is invalid absolute address */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Load new value into prefix register */
    regs->pxr = n;

    /* Invalidate the ALB and TLB */
    purge_alb (regs);
    purge_tlb (regs);

    /* Perform serialization after completing the operation */
    PERFORM_SERIALIZATION (regs);

}


/*-------------------------------------------------------------------*/
/* B20A SPKA  - Set PSW Key from Address                         [S] */
/*-------------------------------------------------------------------*/
void zz_set_psw_key_from_address (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
int     n;                              /* Storage key workarea      */

    S(inst, execflag, regs, b2, effective_addr2);

    /* Isolate the key from bits 24-27 of effective address */
    n = effective_addr2 & 0x000000F0;

    /* Privileged operation exception if in problem state
       and the corresponding PSW key mask bit is zero */
    if ( regs->psw.prob
        && ((regs->cr[3] << (n >> 4)) & 0x80000000) == 0 )
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);

    /* Set PSW key */
    regs->psw.pkey = n;

}


/*-------------------------------------------------------------------*/
/* B225 SSAR  - Set Secondary ASN                              [RRE] */
/*-------------------------------------------------------------------*/
void zz_set_secondary_asn (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Register numbers          */
U32     sasn;                           /* New Secondary ASN         */
U32     sstd;                           /* Secondary STD             */
U32     sasteo;                         /* Secondary ASTE origin     */
U32     aste[16];                       /* ASN second table entry    */
U16     xcode;                          /* Exception code            */
U16     ax;                             /* Authorization index       */
#ifdef FEATURE_TRACING
U32     newcr12 = 0;                    /* CR12 upon completion      */
#endif /*FEATURE_TRACING*/

    RRE(inst, execflag, regs, r1, r2);

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    /* Special operation exception if ASN translation control
       (bit 12 of control register 14) is zero or DAT is off */
    if ((regs->cr[14] & CR14_ASN_TRAN) == 0
        || REAL_MODE(&(regs->psw)))
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    /* Load the new ASN from R1 register bits 16-31 */
    sasn = regs->gpr[r1] & CR3_SASN;

#ifdef FEATURE_TRACING
    /* Form trace entry if ASN tracing is on */
    if (regs->cr[12] & CR12_ASNTRACE)
        newcr12 = trace_ssar (sasn, regs);
#endif /*FEATURE_TRACING*/

    /* Test for SSAR to current primary */
    if (sasn == (regs->cr[4] & CR4_PASN))
    {
        /* Set new secondary STD equal to primary STD */
        sstd = regs->cr[1];

    } /* end if(SSAR-cp) */
    else
    { /* SSAR with space-switch */

        /* Perform ASN translation to obtain ASTE */
        xcode = translate_asn (sasn, regs, &sasteo, aste);

        /* Program check if ASN translation exception */
        if (xcode != 0)
            program_check (regs, xcode);

        /* Perform ASN authorization using current AX */
        ax = (regs->cr[4] & CR4_AX) >> 16;
        if (authorize_asn (ax, aste, ATE_SECONDARY, regs))
        {
            regs->tea = sasn;
            program_check (regs, PGM_SECONDARY_AUTHORITY_EXCEPTION);
        }

        /* Load new secondary STD from ASTE word 2 */
        sstd = aste[2];

#ifdef FEATURE_SUBSPACE_GROUP
        /* Perform subspace replacement on new SSTD */
        sstd = subspace_replace (sstd, sasteo, NULL, regs);
#endif /*FEATURE_SUBSPACE_GROUP*/

    } /* end if(SSAR-ss) */

#ifdef FEATURE_TRACING
    /* Update trace table address if ASN tracing is on */
    if (regs->cr[12] & CR12_ASNTRACE)
        regs->cr[12] = newcr12;
#endif /*FEATURE_TRACING*/

    /* Load the new secondary ASN into control register 3 */
    regs->cr[3] &= ~CR3_SASN;
    regs->cr[3] |= sasn;

    /* Load the new secondary STD into control register 7 */
    regs->cr[7] = sstd;

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

}


/*-------------------------------------------------------------------*/
/* 08   SSK   - Set Storage Key                                 [RR] */
/*-------------------------------------------------------------------*/
void zz_set_storage_key (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     n;                              /* Absolute storage addr     */

    RR(inst, execflag, regs, r1, r2);

    PRIV_CHECK(regs);

    /* Program check if R2 bits 28-31 are not zeroes */
    if ( regs->gpr[r2] & 0x0000000F )
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Load 2K block address from R2 register */
    n = regs->gpr[r2] & 0x00FFF800;

    /* Convert real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Addressing exception if block is outside main storage */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Update the storage key from R1 register bits 24-30 */
    STORAGE_KEY(n) &= STORKEY_BADFRM;
    STORAGE_KEY(n) |= regs->gpr[r1] & ~(STORKEY_BADFRM);

//  /*debug*/logmsg("SSK storage block %8.8X key %2.2X\n",
//  /*debug*/       regs->gpr[r2], regs->gpr[r1] & 0xFE);

}


/*-------------------------------------------------------------------*/
/* B22B SSKE  - Set Storage Key extended                       [RRE] */
/*-------------------------------------------------------------------*/
void zz_set_storage_key_extended (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Register numbers          */
U32     n;                              /* Abs frame addr stor key   */

    RRE(inst, execflag, regs, r1, r2);

    PRIV_CHECK(regs);

    /* Load 4K block address from R2 register */
    n = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    /* Convert real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Addressing exception if block is outside main storage */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Update the storage key from R1 register bits 24-30 */
    STORAGE_KEY(n) &= STORKEY_BADFRM;
    STORAGE_KEY(n) |= regs->gpr[r1] & ~(STORKEY_BADFRM);

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

}


/*-------------------------------------------------------------------*/
/* 80   SSM   - Set System Mask                                  [S] */
/*-------------------------------------------------------------------*/
void zz_set_system_mask (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Special operation exception if SSM-suppression is active */
    if ( regs->cr[0] & CR0_SSM_SUPP )
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    /* Load new system mask value from operand address */
    regs->psw.sysmask = vfetchb ( effective_addr2, b2, regs );

    /* For ECMODE, bits 0 and 2-4 of system mask must be zero */
    if (regs->psw.ecmode && (regs->psw.sysmask & 0xB8) != 0)
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

}


/*-------------------------------------------------------------------*/
/* AE   SIGP  - Signal Processor                                [RS] */
/*-------------------------------------------------------------------*/
void zz_signal_procesor (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
REGS   *tregs;                          /* -> Target CPU registers   */
U32     parm;                           /* Signal parameter          */
U32     status = 0;                     /* Signal status             */
U32     abs;                            /* Absolute address          */
U16     cpad;                           /* Target CPU address        */
BYTE    order;                          /* SIGP order code           */
static char *ordername[] = {    "Unassigned",
        /* SIGP_SENSE     */    "Sense",
        /* SIGP_EXTCALL   */    "External call",
        /* SIGP_EMERGENCY */    "Emergency signal",
        /* SIGP_START     */    "Start",
        /* SIGP_STOP      */    "Stop",
        /* SIGP_RESTART   */    "Restart",
        /* SIGP_IPR       */    "Initial program reset",
        /* SIGP_PR        */    "Program reset",
        /* SIGP_STOPSTORE */    "Stop and store status",
        /* SIGP_IMPL      */    "Initial microprogram load",
        /* SIGP_INITRESET */    "Initial CPU reset",
        /* SIGP_RESET     */    "CPU reset",
        /* SIGP_SETPREFIX */    "Set prefix",
        /* SIGP_STORE     */    "Store status",
        /* 0x0F           */    "Unassigned",
        /* 0x10           */    "Unassigned",
        /* SIGP_STOREX    */    "Store extended status at address" };

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Perform serialization before starting operation */
    PERFORM_SERIALIZATION (regs);

    /* Load the target CPU address from R3 bits 16-31 */
    cpad = regs->gpr[r3] & 0xFFFF;

    /* Load the order code from operand address bits 24-31 */
    order = effective_addr2 & 0xFF;

    /* Load the parameter from R1 (if R1 odd), or R1+1 (if even) */
    parm = (r1 & 1) ? regs->gpr[r1] : regs->gpr[r1+1];

    /* Return condition code 3 if target CPU does not exist */
#ifdef FEATURE_CPU_RECONFIG
    if (cpad >= MAX_CPU_ENGINES)
#else /*!FEATURE_CPU_RECONFIG*/
    if (cpad >= sysblk.numcpu)
#endif /*!FEATURE_CPU_RECONFIG*/
    {
        regs->psw.cc = 3;
        return;
    }

    /* Point to CPU register context for the target CPU */
    tregs = sysblk.regs + cpad;

    /* Trace SIGP unless Sense, External Call, Emergency Signal,
       or the target CPU is configured offline */
    if (order > SIGP_EMERGENCY || !tregs->cpuonline)
        logmsg ("CPU%4.4X: SIGP CPU%4.4X %s PARM %8.8X\n",
                regs->cpuad, cpad,
                order > SIGP_STOREX ? ordername[0] : ordername[order],
                parm);

    /* [4.9.2.1] Claim the use of the CPU signaling and response
       facility, and return condition code 2 if the facility is
       busy.  The sigpbusy bit is set while the facility is in
       use by any CPU.  The sigplock must be held while testing
       and setting the value of the sigpbusy bit to one. */
    obtain_lock (&sysblk.sigplock);
    if (sysblk.sigpbusy)
    {
        release_lock (&sysblk.sigplock);
        regs->psw.cc = 2;
        return;
    }
    sysblk.sigpbusy = 1;
    release_lock (&sysblk.sigplock);

    /* Obtain the interrupt lock */
    obtain_lock (&sysblk.intlock);

    /* If the cpu is not part of the configuration then return cc3
       Initial CPU reset may IML a processor that is currently not
       part of the configuration, ie configure the cpu implicitly
       online */
    if (order != SIGP_INITRESET && !tregs->cpuonline)
    {
        sysblk.sigpbusy = 0;
        release_lock(&sysblk.intlock);
        regs->psw.cc = 3;
        return;
    }

    /* Except for the reset orders, return condition code 2 if the
       target CPU is executing a previous start, stop, restart,
       stop and store status, set prefix, or store status order */
    if ((order != SIGP_RESET && order != SIGP_INITRESET)
        && (tregs->cpustate == CPUSTATE_STOPPING
            || tregs->restart))
    {
        sysblk.sigpbusy = 0;
        release_lock(&sysblk.intlock);
        regs->psw.cc = 2;
        return;
    }

    /* If the CPU thread is still starting, ie CPU is still performing
       the IML process then relect an operator intervening status
       to the caller */
    if(tregs->cpustate == CPUSTATE_STARTING)
        status |= SIGP_STATUS_OPERATOR_INTERVENING;
    else
        /* Process signal according to order code */
        switch (order)
        {
        case SIGP_SENSE:
            /* Set status bit 24 if external call interrupt pending */
            if (tregs->extcall)
                status |= SIGP_STATUS_EXTERNAL_CALL_PENDING;

            /* Set status bit 25 if target CPU is stopped */
            if (tregs->cpustate != CPUSTATE_STARTED)
                status |= SIGP_STATUS_STOPPED;

            break;

        case SIGP_EXTCALL:
            /* Exit with status bit 24 set if a previous external
               call interrupt is still pending in the target CPU */
            if (tregs->extcall)
            {
                status |= SIGP_STATUS_EXTERNAL_CALL_PENDING;
                break;
            }

            /* Raise an external call interrupt pending condition */
            tregs->cpuint = tregs->extcall = 1;
            tregs->extccpu = regs->cpuad;

            break;

        case SIGP_EMERGENCY:
            /* Raise an emergency signal interrupt pending condition */
            tregs->cpuint = tregs->emersig = 1;
            tregs->emercpu[regs->cpuad] = 1;

            break;

        case SIGP_START:
            /* Restart the target CPU if it is in the stopped state */
            tregs->cpustate = CPUSTATE_STARTED;

            break;

        case SIGP_STOP:
            /* Put the the target CPU into the stopping state */
            tregs->cpustate = CPUSTATE_STOPPING;

            break;

        case SIGP_RESTART:
            /* Make restart interrupt pending in the target CPU */
            tregs->restart = 1;
            /* Set cpustate to stopping. If the restart is successful,
               then the cpustate will be set to started in cpu.c */
            if(tregs->cpustate == CPUSTATE_STOPPED)
                tregs->cpustate = CPUSTATE_STOPPING;

            break;

        case SIGP_STOPSTORE:
            /* Indicate store status is required when stopped */
            tregs->storstat = 1;

            /* Put the the target CPU into the stopping state */
            tregs->cpustate = CPUSTATE_STOPPING;

            break;

        case SIGP_INITRESET:
            if(tregs->cpuonline)
            {
                /* Signal initial CPU reset function */
                tregs->sigpireset = 1;
                tregs->cpustate = CPUSTATE_STOPPING;
            }
            else
                configure_cpu(tregs);

            break;

        case SIGP_RESET:
            /* Signal CPU reset function */
            tregs->sigpreset = 1;
            tregs->cpustate = CPUSTATE_STOPPING;

            break;

        case SIGP_SETPREFIX:
            /* Exit with operator intervening if the status is
               stopping, such that a retry can be attempted */
            if(tregs->cpustate == CPUSTATE_STOPPING)
            {
                status |= SIGP_STATUS_OPERATOR_INTERVENING;
                break;
            }

            /* Exit with status bit 22 set if CPU is not stopped */
            if (tregs->cpustate != CPUSTATE_STOPPED)
            {
                status |= SIGP_STATUS_INCORRECT_STATE;
                break;
            }

            /* Obtain new prefix from parameter register bits 1-19 */
            abs = parm & 0x7FFFF000;

            /* Exit with status bit 23 set if new prefix is invalid */
            if (abs >= sysblk.mainsize)
            {
                status |= SIGP_STATUS_INVALID_PARAMETER;
                break;
            }

            /* Load new value into prefix register of target CPU */
            tregs->pxr = abs;

            /* Invalidate the ALB and TLB of the target CPU */
            purge_alb (tregs);
            purge_tlb (tregs);

            /* Perform serialization and checkpoint-sync on target CPU */
//          perform_serialization (tregs);
//          perform_chkpt_sync (tregs);

            break;

        case SIGP_STORE:
            /* Exit with operator intervening if the status is
               stopping, such that a retry can be attempted */
            if(tregs->cpustate == CPUSTATE_STOPPING)
            {
                status |= SIGP_STATUS_OPERATOR_INTERVENING;
                break;
            }

            /* Exit with status bit 22 set if CPU is not stopped */
            if (tregs->cpustate != CPUSTATE_STOPPED)
            {
                status |= SIGP_STATUS_INCORRECT_STATE;
                break;
            }

            /* Obtain status address from parameter register bits 1-22 */
            abs = parm & 0x7FFFFE00;

            /* Exit with status bit 23 set if status address invalid */
            if (abs >= sysblk.mainsize)
            {
                status |= SIGP_STATUS_INVALID_PARAMETER;
                break;
            }

            /* Store status at specified main storage address */
            store_status (tregs, abs);

            /* Perform serialization and checkpoint-sync on target CPU */
//          perform_serialization (tregs);

//          perform_chkpt_sync (tregs);

            break;

        case SIGP_STOREX:

        default:
            status = SIGP_STATUS_INVALID_ORDER;
        } /* end switch(order) */

    /* Release the use of the signalling and response facility */
    sysblk.sigpbusy = 0;

    /* Wake up any CPUs waiting for an interrupt or start */
    signal_condition (&sysblk.intcond);

    /* Release the interrupt lock */
    release_lock (&sysblk.intlock);

    /* If status is non-zero, load the status word into
       the R1 register and return condition code 1 */
    if (status != 0)
    {
        regs->gpr[r1] = status;
        regs->psw.cc = 1;
    }
    else
        regs->psw.cc = 0;

    /* Perform serialization after completing operation */
    PERFORM_SERIALIZATION (regs);

}


/*-------------------------------------------------------------------*/
/* B207 STCKC - Store Clock Comparator                           [S] */
/*-------------------------------------------------------------------*/
void zz_store_clock_comparator (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U64     dreg;                           /* Clock value               */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    DW_CHECK(effective_addr2, regs);

    /* Obtain the TOD clock update lock */
    obtain_lock (&sysblk.todlock);

    /* Save clock comparator value and shift out the epoch */
    dreg = regs->clkc << 8;

    /* Release the TOD clock update lock */
    release_lock (&sysblk.todlock);

    /* Store clock comparator value at operand location */
    vstore8 ( dreg, effective_addr2, b2, regs );

//  /*debug*/logmsg("Store clock comparator=%16.16llX\n", dreg);

}


/*-------------------------------------------------------------------*/
/* B6   STCTL - Store Control                                   [RS] */
/*-------------------------------------------------------------------*/
void zz_store_control (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
int     i, d;                           /* Integer work areas        */
BYTE    rwork[64];                      /* Register work areas       */

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    PRIV_CHECK(regs);

    FW_CHECK(effective_addr2, regs);

    /* Copy control registers into work area */
    for ( i = r1, d = 0; ; )
    {
        /* Copy contents of one control register to work area */
        rwork[d++] = (regs->cr[i] & 0xFF000000) >> 24;
        rwork[d++] = (regs->cr[i] & 0xFF0000) >> 16;
        rwork[d++] = (regs->cr[i] & 0xFF00) >> 8;
        rwork[d++] = regs->cr[i] & 0xFF;

        /* Instruction is complete when r3 register is done */
        if ( i == r3 ) break;

        /* Update register number, wrapping from 15 to 0 */
        i++; i &= 15;
    }

    /* Store control register contents at operand address */
    vstorec ( rwork, d-1, effective_addr2, b2, regs );

}


/*-------------------------------------------------------------------*/
/* B212 STAP  - Store CPU Address                                [S] */
/*-------------------------------------------------------------------*/
void zz_store_cpu_address (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    ODD_CHECK(effective_addr2, regs);

    /* Store CPU address at operand address */
    vstore2 ( regs->cpuad, effective_addr2, b2, regs );

}


/*-------------------------------------------------------------------*/
/* B202 STIDP - Store CPU ID                                     [S] */
/*-------------------------------------------------------------------*/
void zz_store_cpu_id (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U64     dreg;                           /* Double word workarea      */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    DW_CHECK(effective_addr2, regs);

    /* Load the CPU ID */
    dreg = sysblk.cpuid;

    /* If first digit of serial is zero, insert processor id */
    if ((dreg & 0x00F0000000000000ULL) == 0)
        dreg |= (U64)(regs->cpuad & 0x0F) << 52;

    /* Store CPU ID at operand address */
    vstore8 ( dreg, effective_addr2, b2, regs );

}


/*-------------------------------------------------------------------*/
/* B209 STPT  - Store CPU Timer                                  [S] */
/*-------------------------------------------------------------------*/
void zz_store_cpu_timer (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
U64     dreg;                           /* Double word workarea      */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    DW_CHECK(effective_addr2, regs);

    /* Obtain the TOD clock update lock */
    obtain_lock (&sysblk.todlock);

    /* Save the CPU timer value */
    dreg = --regs->ptimer;

    /* reset the cpu timer pending flag according to its value */
    if( (S64)regs->ptimer < 0 )
        regs->cpuint = regs->ptpend = 1;
    else
        regs->ptpend = 0;

    /* Release the TOD clock update lock */
    release_lock (&sysblk.todlock);

    /* Store CPU timer value at operand location */
    vstore8 ( dreg, effective_addr2, b2, regs );

//  /*debug*/logmsg("Store CPU timer=%16.16llX\n", dreg);

}


/*-------------------------------------------------------------------*/
/* B211 STPX  - Store Prefix                                     [S] */
/*-------------------------------------------------------------------*/
void zz_store_prefix (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    FW_CHECK(effective_addr2, regs);

    /* Store prefix register at operand address */
    vstore4 ( regs->pxr, effective_addr2, b2, regs );

}


/*-------------------------------------------------------------------*/
/* AC   STNSM - Store Then And Systen Mask                      [SI] */
/*-------------------------------------------------------------------*/
void zz_store_then_and_system_mask (BYTE inst[], int execflag, REGS *regs)
{
BYTE    i2;                             /* Immediate byte of opcode  */
int     b1;                             /* Base of effective addr    */
U32     effective_addr1;                /* Effective address         */

    SI(inst, execflag, regs, i2, b1, effective_addr1);

    PRIV_CHECK(regs);

    /* Store current system mask value into storage operand */
    vstoreb ( regs->psw.sysmask, effective_addr1, b1, regs );

    /* AND system mask with immediate operand */
    regs->psw.sysmask &= i2;

}


/*-------------------------------------------------------------------*/
/* AD   STOSM - Store Then Or Systen Mask                       [SI] */
/*-------------------------------------------------------------------*/
void zz_store_then_or_system_mask (BYTE inst[], int execflag, REGS *regs)
{
BYTE    i2;                             /* Immediate byte of opcode  */
int     b1;                             /* Base of effective addr    */
U32     effective_addr1;                /* Effective address         */

    SI(inst, execflag, regs, i2, b1, effective_addr1);

    PRIV_CHECK(regs);

    /* Store current system mask value into storage operand */
    vstoreb ( regs->psw.sysmask, effective_addr1, b1, regs );

    /* OR system mask with immediate operand */
    regs->psw.sysmask |= i2;

    /* For ECMODE, bits 0 and 2-4 of system mask must be zero */
    if (regs->psw.ecmode && (regs->psw.sysmask & 0xB8) != 0)
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
 
}

/*-------------------------------------------------------------------*/
/* B246 STURA - Store Using Real Address                       [RRE] */
/*-------------------------------------------------------------------*/
void zz_store_using_real_address (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     n;                              /* Unsigned work             */

    RRE(inst, execflag, regs, r1, r2);

    PRIV_CHECK(regs);

    /* R2 register contains operand real storage address */
    n = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Program check if operand not on fullword boundary */
    FW_CHECK(n, regs);

    /* Store R1 register at second operand location */
    vstore4 (regs->gpr[r1], n, USE_REAL_ADDR, regs );

}


/*-------------------------------------------------------------------*/
/* B24C TAR   - Test Access                                    [RRE] */
/*-------------------------------------------------------------------*/
void zz_test_access (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     asteo;                          /* Real address of ASTE      */
U32     aste[16];                       /* ASN second table entry    */
int     protect;                        /* 1=ALE or page protection  */

    RRE(inst, execflag, regs, r1, r2);

    /* Program check if ASF control bit is zero */
    if ((regs->cr[0] & CR0_ASF) == 0)
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    /* Set condition code 0 if ALET value is 0 */
    if (regs->ar[r1] == ALET_PRIMARY)
    {
        regs->psw.cc = 0;
        return;
    }

    /* Set condition code 3 if ALET value is 1 */
    if (regs->ar[r1] == ALET_SECONDARY)
    {
        regs->psw.cc = 3;
        return;
    }

    /* Perform ALET translation using EAX value from register
       R2 bits 0-15, and set condition code 3 if exception */
    if (translate_alet (regs->ar[r1], (regs->gpr[r2] >> 16),
                        ACCTYPE_TAR, regs,
                        &asteo, aste, &protect));
    {
        regs->psw.cc = 3;
        return;
    }

    /* Set condition code 1 or 2 according to whether
       the ALET designates the DUCT or the PASTE */
    regs->psw.cc = (regs->ar[r1] & ALET_PRI_LIST) ? 2 : 1;

}


/*-------------------------------------------------------------------*/
/* B22C TB    - Test Block                                     [RRE] */
/*-------------------------------------------------------------------*/
void zz_test_block (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r2;                         /* Values of R fields        */
U32     n;                              /* Real address              */

    RRE(inst, execflag, regs, r1, r2);

    PRIV_CHECK(regs);

    /* Load 4K block address from R2 register */
    n = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);
    n &= 0xFFFFF000;

    /* Perform serialization */
    PERFORM_SERIALIZATION (regs);

    /* Addressing exception if block is outside main storage */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Protection exception if low-address protection is set */
    if ( n == 0 && (regs->cr[0] & CR0_LOW_PROT) )
    {
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
        regs->tea = (n & TEA_EFFADDR);
        regs->excarid = 0;
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
        program_check (regs, PGM_PROTECTION_EXCEPTION);
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
    PERFORM_SERIALIZATION (regs);

    /* Clear general register 0 */
    regs->gpr[0] = 0;

}


/*-------------------------------------------------------------------*/
/* E501 TPROT - Test Protection                                [SSE] */
/*-------------------------------------------------------------------*/
void zz_test_protection (BYTE inst[], int execflag, REGS *regs)
{
int     b1, b2;                         /* Values of base registers  */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
U32     raddr;                          /* Real address              */
U32     aaddr;                          /* Absolute address          */
BYTE    skey;                           /* Storage key               */
BYTE    akey;                           /* Access key                */
int     private = 0;                    /* 1=Private address space   */
int     protect = 0;                    /* 1=ALE or page protection  */
int     stid;                           /* Segment table indication  */
U16     xcode;                          /* Exception code            */

    SSE(inst, execflag, regs, b1, effective_addr1, b2, effective_addr2); 

    PRIV_CHECK(regs);

    /* Convert logical address to real address */
    if (REAL_MODE(&regs->psw))
        raddr = effective_addr1;
    else {
        /* Return condition code 3 if translation exception */
        if (translate_addr (effective_addr1, b1, regs, ACCTYPE_TPROT, &raddr,
                &xcode, &private, &protect, &stid, NULL, NULL))
            regs->psw.cc = 3;
            return;
    }

    /* Convert real address to absolute address */
    aaddr = APPLY_PREFIXING (raddr, regs->pxr);

    /* Program check if absolute address is outside main storage */
    if (aaddr >= sysblk.mainsize)
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Load access key from operand 2 address bits 24-27 */
    akey = effective_addr2 & 0xF0;

    /* Load the storage key for the absolute address */
    skey = STORAGE_KEY(aaddr);

    /* Return condition code 2 if location is fetch protected */
    if (is_fetch_protected (effective_addr1, skey, akey, private, regs))
        regs->psw.cc = 2;
    else
        /* Return condition code 1 if location is store protected */
        if (is_store_protected (effective_addr1, skey, akey,
                                                private, protect, regs))
            regs->psw.cc = 1;
        else
            /* Return condition code 0 if location is not protected */
            regs->psw.cc = 0;

} 

/*-------------------------------------------------------------------*/
/* 99   TRACE - Trace                                           [RS] */
/*-------------------------------------------------------------------*/
void zz_trace (BYTE inst[], int execflag, REGS *regs)
{
int     r1, r3;                         /* Register numbers          */
int     b2;                             /* effective address base    */
U32     effective_addr2;                /* effective address         */
#if defined(FEATURE_TRACING)
U32     n2,                             /* Operand                   */
        n1;                             /* Addr of trace table entry */
int     i;                              /* Loop counter              */
U64     dreg;                           /* 64-bit work area          */
#endif /*defined(FEATURE_TRACING)*/

    RS(inst, execflag, regs, r1, r3, b2, effective_addr2);

    PRIV_CHECK(regs);

    FW_CHECK(effective_addr2, regs);

#if defined(FEATURE_TRACING)
    /* Exit if explicit tracing (control reg 12 bit 31) is off */
    if ( (regs->cr[12] & CR12_EXTRACE) == 0 )
        return;

    /* Fetch the trace operand */
    n2 = vfetch4 ( effective_addr2, b2, regs );

    /* Exit if bit zero of the trace operand is one */
    if ( (n2 & 0x80000000) )
        return;

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    /* Obtain the trace entry address from control register 12 */
    n1 = regs->cr[12] & CR12_TRACEEA;

    /* Low-address protection program check if trace entry
       address is 0-511 and bit 3 of control register 0 is set */
    if ( n1 < 512 && (regs->cr[0] & CR0_LOW_PROT) )
    {
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
        regs->tea = (n1 & TEA_EFFADDR);
        regs->excarid = 0;
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
        program_check (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Convert trace entry real address to absolute address */
    n1 = APPLY_PREFIXING (n1, regs->pxr);

    /* Program check if trace entry is outside main storage */
    if ( n1 >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Program check if storing the maximum length trace
       entry (76 bytes) would overflow a 4K page boundary */
    if ( ((n1 + 76) & STORAGE_KEY_PAGEMASK) != (n1 & STORAGE_KEY_PAGEMASK) )
        program_check (regs, PGM_TRACE_TABLE_EXCEPTION);

    /* Calculate the number of registers to be traced, minus 1 */
    i = ( r3 < r1 ) ? r3 + 16 - r1 : r3 - r1;

    /* Update the TOD clock */
    update_TOD_clock();

    /* Obtain the TOD clock update lock */
    obtain_lock (&sysblk.todlock);

    /* Retrieve the TOD clock value and shift out the epoch */
    dreg = sysblk.todclk << 8;

    /* Release the TOD clock update lock */
    release_lock (&sysblk.todlock);

    /* Set the main storage change and reference bits */
    STORAGE_KEY(n1) |= (STORKEY_REF | STORKEY_CHANGE);

    /* Build the explicit trace entry */
    sysblk.mainstor[n1++] = (0x70 | i);
    sysblk.mainstor[n1++] = 0x00;
    sysblk.mainstor[n1++] = ((dreg >> 40) & 0xFF);
    sysblk.mainstor[n1++] = ((dreg >> 32) & 0xFF);
    sysblk.mainstor[n1++] = ((dreg >> 24) & 0xFF);
    sysblk.mainstor[n1++] = ((dreg >> 16) & 0xFF);
    sysblk.mainstor[n1++] = ((dreg >> 8) & 0xFF);
    sysblk.mainstor[n1++] = (dreg & 0xFF);
    sysblk.mainstor[n1++] = ((n2 >> 24) & 0xFF);
    sysblk.mainstor[n1++] = ((n2 >> 16) & 0xFF);
    sysblk.mainstor[n1++] = ((n2 >> 8) & 0xFF);
    sysblk.mainstor[n1++] = (n2 & 0xFF);

    /* Store general registers r1 through r3 in the trace entry */
    for ( i = r1; ; )
    {
        sysblk.mainstor[n1++] = ((regs->gpr[i] >> 24) & 0xFF);
        sysblk.mainstor[n1++] = ((regs->gpr[i] >> 16) & 0xFF);
        sysblk.mainstor[n1++] = ((regs->gpr[i] >> 8) & 0xFF);
        sysblk.mainstor[n1++] = (regs->gpr[i] & 0xFF);

        /* Instruction is complete when r3 register is done */
        if ( i == r3 ) break;

        /* Update register number, wrapping from 15 to 0 */
        i++; i &= 15;
    }

    /* Update trace entry address in control register 12 */
    regs->cr[12] &= ~CR12_TRACEEA;
    regs->cr[12] |= n1;

#endif /*defined(FEATURE_TRACING)*/

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

} 
