/* TRACE.C      Implicit tracing functions - Jan Jaeger              */

/* Interpretive Execution - (c) Copyright Jan Jaeger, 1999-2001      */

#include "hercules.h"

#if defined(FEATURE_TRACING)

#include "opcode.h"
#include "inline.h"

/*-------------------------------------------------------------------*/
/* Form implicit branch trace entry                                  */
/*                                                                   */
/* Input:                                                            */
/*      amode   Non-zero if branch destination is a 31-bit address   */
/*      ia      Branch destination address                           */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Updated value for CR12 after adding new trace entry          */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
U32 trace_br (int amode, U32 ia, REGS *regs)
{
U32     n;                              /* Addr of trace table entry */
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
U32     ag,                             /* Abs Guest addr of TTE     */
        ah;                             /* Abs Host addr of TTE      */
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
#ifdef IBUF
U32     haddr;
#endif

    /* Obtain the trace entry address from control register 12 */
    n = regs->cr[12] & CR12_TRACEEA;

    /* Low-address protection program check if trace entry
       address is 0-511 and bit 3 of control register 0 is set */
    if ( n < 512 && (regs->cr[0] & CR0_LOW_PROT) )
    {
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
        regs->tea = (n & TEA_EFFADDR);
        regs->excarid = 0;
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
        program_interrupt (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Program check if trace entry is outside main storage */
    if ( n >= regs->mainsize )
        program_interrupt (regs, PGM_ADDRESSING_EXCEPTION);

    /* Program check if storing would overflow a 4K page boundary */
    if ( ((n + 4) & STORAGE_KEY_PAGEMASK) != (n & STORAGE_KEY_PAGEMASK) )
        program_interrupt (regs, PGM_TRACE_TABLE_EXCEPTION);

    /* Convert trace entry real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    ag = n;

    SIE_TRANSLATE(&n, ACCTYPE_WRITE, regs);

    ah = n;
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Set the main storage change and reference bits */
    STORAGE_KEY(n) |= (STORKEY_REF | STORKEY_CHANGE);

#ifdef IBUF
    haddr = n;
#endif

    /* Build the branch trace entry */
    if (amode)
        sysblk.mainstor[n++] = 0x80 | ((ia & 0x7F000000) >> 24);
    else
        sysblk.mainstor[n++] = 0x00;
    sysblk.mainstor[n++] = (ia & 0xFF0000) >> 16;
    sysblk.mainstor[n++] = (ia & 0xFF00) >> 8;
    sysblk.mainstor[n++] = ia & 0xFF;

    FRAG_INVALIDATE(haddr, 4);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    /* Recalculate the Guest absolute address */
    n = ag + (n - ah);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Convert trace entry absolue address back to real address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Return updated value of control register 12 */
    return (regs->cr[12] & ~CR12_TRACEEA) | n;

} /* end function trace_br */


/*-------------------------------------------------------------------*/
/* Form implicit BSG trace entry                                     */
/*                                                                   */
/* Input:                                                            */
/*      alet    Destination address space ALET                       */
/*      ia      Branch destination address                           */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Updated value for CR12 after adding new trace entry          */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
U32 trace_bsg (U32 alet, U32 ia, REGS *regs)
{
U32     n;                              /* Addr of trace table entry */
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
U32     ag,                             /* Abs Guest addr of TTE     */
        ah;                             /* Abs Host addr of TTE      */
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
#ifdef IBUF
U32     haddr;
#endif

    /* Obtain the trace entry address from control register 12 */
    n = regs->cr[12] & CR12_TRACEEA;

    /* Low-address protection program check if trace entry
       address is 0-511 and bit 3 of control register 0 is set */
    if ( n < 512 && (regs->cr[0] & CR0_LOW_PROT) )
    {
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
        regs->tea = (n & TEA_EFFADDR);
        regs->excarid = 0;
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
        program_interrupt (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Program check if trace entry is outside main storage */
    if ( n >= regs->mainsize )
        program_interrupt (regs, PGM_ADDRESSING_EXCEPTION);

    /* Program check if storing would overflow a 4K page boundary */
    if ( ((n + 8) & STORAGE_KEY_PAGEMASK) != (n & STORAGE_KEY_PAGEMASK) )
        program_interrupt (regs, PGM_TRACE_TABLE_EXCEPTION);

    /* Convert trace entry real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    ag = n;

    SIE_TRANSLATE(&n, ACCTYPE_WRITE, regs);

    ah = n;
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Set the main storage change and reference bits */
    STORAGE_KEY(n) |= (STORKEY_REF | STORKEY_CHANGE);
#ifdef IBUF
    haddr = n;
#endif

    /* Build the Branch in Subspace Group trace entry */
    sysblk.mainstor[n++] = 0x41;
    sysblk.mainstor[n++] = (regs->psw.prob << 7) | ((alet & 0x7F0000) >> 16);
    sysblk.mainstor[n++] = (alet & 0xFF00) >> 8;
    sysblk.mainstor[n++] = alet & 0xFF;
    sysblk.mainstor[n++] = (regs->psw.ia & 0xFF000000) >> 24;
    sysblk.mainstor[n++] = (regs->psw.ia & 0xFF0000) >> 16;
    sysblk.mainstor[n++] = (regs->psw.ia & 0xFF00) >> 8;
    sysblk.mainstor[n++] = regs->psw.ia & 0xFF;
    FRAG_INVALIDATE(haddr, 8);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    /* Recalculate the Guest absolute address */
    n = ag + (n - ah);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Convert trace entry absolue address back to real address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Return updated value of control register 12 */
    return (regs->cr[12] & ~CR12_TRACEEA) | n;

} /* end function trace_bsg */


/*-------------------------------------------------------------------*/
/* Form implicit SSAR trace entry                                    */
/*                                                                   */
/* Input:                                                            */
/*      sasn    Secondary address space number                       */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Updated value for CR12 after adding new trace entry          */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
U32 trace_ssar (U16 sasn, REGS *regs)
{
U32     n;                              /* Addr of trace table entry */
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
U32     ag,                             /* Abs Guest addr of TTE     */
        ah;                             /* Abs Host addr of TTE      */
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
#ifdef IBUF
U32     haddr;
#endif

    /* Obtain the trace entry address from control register 12 */
    n = regs->cr[12] & CR12_TRACEEA;

    /* Low-address protection program check if trace entry
       address is 0-511 and bit 3 of control register 0 is set */
    if ( n < 512 && (regs->cr[0] & CR0_LOW_PROT) )
    {
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
        regs->tea = (n & TEA_EFFADDR);
        regs->excarid = 0;
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
        program_interrupt (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Program check if trace entry is outside main storage */
    if ( n >= regs->mainsize )
        program_interrupt (regs, PGM_ADDRESSING_EXCEPTION);

    /* Program check if storing would overflow a 4K page boundary */
    if ( ((n + 4) & STORAGE_KEY_PAGEMASK) != (n & STORAGE_KEY_PAGEMASK) )
        program_interrupt (regs, PGM_TRACE_TABLE_EXCEPTION);

    /* Convert trace entry real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    ag = n;

    SIE_TRANSLATE(&n, ACCTYPE_WRITE, regs);

    ah = n;
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Set the main storage change and reference bits */
    STORAGE_KEY(n) |= (STORKEY_REF | STORKEY_CHANGE);
#ifdef IBUF
    haddr = n;
#endif

    /* Build the Set Secondary ASN trace entry */
    sysblk.mainstor[n++] = 0x10;
    sysblk.mainstor[n++] = 0x00;
    sysblk.mainstor[n++] = (sasn & 0xFF00) >> 8;
    sysblk.mainstor[n++] = sasn & 0xFF;

    FRAG_INVALIDATE(haddr, 4);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    /* Recalculate the Guest absolute address */
    n = ag + (n - ah);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Convert trace entry absolue address back to real address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Return updated value of control register 12 */
    return (regs->cr[12] & ~CR12_TRACEEA) | n;

} /* end function trace_ssar */


/*-------------------------------------------------------------------*/
/* Form implicit PC trace entry                                      */
/*                                                                   */
/* Input:                                                            */
/*      pcnum   Destination PC number                                */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Updated value for CR12 after adding new trace entry          */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
U32 trace_pc (U32 pcnum, REGS *regs)
{
U32     n;                              /* Addr of trace table entry */
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
U32     ag,                             /* Abs Guest addr of TTE     */
        ah;                             /* Abs Host addr of TTE      */
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
#ifdef IBUF
U32     haddr;
#endif

    /* Obtain the trace entry address from control register 12 */
    n = regs->cr[12] & CR12_TRACEEA;

    /* Low-address protection program check if trace entry
       address is 0-511 and bit 3 of control register 0 is set */
    if ( n < 512 && (regs->cr[0] & CR0_LOW_PROT) )
    {
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
        regs->tea = (n & TEA_EFFADDR);
        regs->excarid = 0;
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
        program_interrupt (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Program check if trace entry is outside main storage */
    if ( n >= regs->mainsize )
        program_interrupt (regs, PGM_ADDRESSING_EXCEPTION);

    /* Program check if storing would overflow a 4K page boundary */
    if ( ((n + 8) & STORAGE_KEY_PAGEMASK) != (n & STORAGE_KEY_PAGEMASK) )
        program_interrupt (regs, PGM_TRACE_TABLE_EXCEPTION);

    /* Convert trace entry real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    ag = n;

    SIE_TRANSLATE(&n, ACCTYPE_WRITE, regs);

    ah = n;
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Set the main storage change and reference bits */
    STORAGE_KEY(n) |= (STORKEY_REF | STORKEY_CHANGE);
#ifdef IBUF
    haddr = n;
#endif

    /* Build the program call trace entry */
    sysblk.mainstor[n++] = 0x21;
    sysblk.mainstor[n++] = regs->psw.pkey | ((pcnum & 0xF0000) >> 16);
    sysblk.mainstor[n++] = (pcnum & 0xFF00) >> 8;
    sysblk.mainstor[n++] = pcnum & 0xFF;
    sysblk.mainstor[n++] = (regs->psw.amode << 31)
                           | ((regs->psw.ia & 0x7F000000) >> 24);
    sysblk.mainstor[n++] = (regs->psw.ia & 0xFF0000) >> 16;
    sysblk.mainstor[n++] = (regs->psw.ia & 0xFF00) >> 8;
    sysblk.mainstor[n++] = (regs->psw.ia & 0xFE) | regs->psw.prob;

    FRAG_INVALIDATE(haddr, 8);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    /* Recalculate the Guest absolute address */
    n = ag + (n - ah);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Convert trace entry absolue address back to real address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Return updated value of control register 12 */
    return (regs->cr[12] & ~CR12_TRACEEA) | n;

} /* end function trace_pc */


/*-------------------------------------------------------------------*/
/* Form implicit PR trace entry                                      */
/*                                                                   */
/* Input:                                                            */
/*      newregs Pointer to registers after PR instruction            */
/*      regs    Pointer to registers before PR instruction           */
/* Return value:                                                     */
/*      Updated value for CR12 after adding new trace entry          */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
U32 trace_pr (REGS *newregs, REGS *regs)
{
U32     n;                              /* Addr of trace table entry */
U16     pasn;                           /* New PASN                  */
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
U32     ag,                             /* Abs Guest addr of TTE     */
        ah;                             /* Abs Host addr of TTE      */
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
#ifdef IBUF
U32     haddr;
#endif

    /* Obtain the trace entry address from control register 12 */
    n = regs->cr[12] & CR12_TRACEEA;

    /* Low-address protection program check if trace entry
       address is 0-511 and bit 3 of control register 0 is set */
    if ( n < 512 && (regs->cr[0] & CR0_LOW_PROT) )
    {
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
        regs->tea = (n & TEA_EFFADDR);
        regs->excarid = 0;
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
        program_interrupt (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Program check if trace entry is outside main storage */
    if ( n >= regs->mainsize )
        program_interrupt (regs, PGM_ADDRESSING_EXCEPTION);

    /* Program check if storing would overflow a 4K page boundary */
    if ( ((n + 12) & STORAGE_KEY_PAGEMASK) != (n & STORAGE_KEY_PAGEMASK) )
        program_interrupt (regs, PGM_TRACE_TABLE_EXCEPTION);

    /* Convert trace entry real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    ag = n;

    SIE_TRANSLATE(&n, ACCTYPE_WRITE, regs);

    ah = n;
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Set the main storage change and reference bits */
    STORAGE_KEY(n) |= (STORKEY_REF | STORKEY_CHANGE);
#ifdef IBUF
    haddr = n;
#endif

    pasn = newregs->cr[4] & CR4_PASN;

    /* Build the program return trace entry */
    sysblk.mainstor[n++] = 0x32;
    sysblk.mainstor[n++] = regs->psw.pkey;
    sysblk.mainstor[n++] = (pasn & 0xFF00) >> 8;
    sysblk.mainstor[n++] = pasn & 0xFF;
    sysblk.mainstor[n++] = (regs->psw.amode << 31)
                           | ((regs->psw.ia & 0x7F000000) >> 24);
    sysblk.mainstor[n++] = (regs->psw.ia & 0xFF0000) >> 16;
    sysblk.mainstor[n++] = (regs->psw.ia & 0xFF00) >> 8;
    sysblk.mainstor[n++] = (regs->psw.ia & 0xFE) | newregs->psw.prob;
    sysblk.mainstor[n++] = (newregs->psw.amode << 31)
                           | ((newregs->psw.ia & 0x7F000000) >> 24);
    sysblk.mainstor[n++] = (newregs->psw.ia & 0xFF0000) >> 16;
    sysblk.mainstor[n++] = (newregs->psw.ia & 0xFF00) >> 8;
    sysblk.mainstor[n++] = newregs->psw.ia & 0xFF;

    FRAG_INVALIDATE(haddr, 12);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    /* Recalculate the Guest absolute address */
    n = ag + (n - ah);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Convert trace entry absolue address back to real address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Return updated value of control register 12 */
    return (regs->cr[12] & ~CR12_TRACEEA) | n;

} /* end function trace_pr */


/*-------------------------------------------------------------------*/
/* Form implicit PT trace entry                                      */
/*                                                                   */
/* Input:                                                            */
/*      pasn    Primary address space number                         */
/*      gpr2    Contents of PT second operand register               */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Updated value for CR12 after adding new trace entry          */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
U32 trace_pt (U16 pasn, U32 gpr2, REGS *regs)
{
U32     n;                              /* Addr of trace table entry */
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
U32     ag,                             /* Abs Guest addr of TTE     */
        ah;                             /* Abs Host addr of TTE      */
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
#ifdef IBUF
U32     haddr;
#endif

    /* Obtain the trace entry address from control register 12 */
    n = regs->cr[12] & CR12_TRACEEA;

    /* Low-address protection program check if trace entry
       address is 0-511 and bit 3 of control register 0 is set */
    if ( n < 512 && (regs->cr[0] & CR0_LOW_PROT) )
    {
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
        regs->tea = (n & TEA_EFFADDR);
        regs->excarid = 0;
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
        program_interrupt (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Program check if trace entry is outside main storage */
    if ( n >= regs->mainsize )
        program_interrupt (regs, PGM_ADDRESSING_EXCEPTION);

    /* Program check if storing would overflow a 4K page boundary */
    if ( ((n + 8) & STORAGE_KEY_PAGEMASK) != (n & STORAGE_KEY_PAGEMASK) )
        program_interrupt (regs, PGM_TRACE_TABLE_EXCEPTION);

    /* Convert trace entry real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    ag = n;

    SIE_TRANSLATE(&n, ACCTYPE_WRITE, regs);

    ah = n;
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Set the main storage change and reference bits */
    STORAGE_KEY(n) |= (STORKEY_REF | STORKEY_CHANGE);
#ifdef IBUF
    haddr = n;
#endif

    /* Build the program transfer trace entry */
    sysblk.mainstor[n++] = 0x31;
    sysblk.mainstor[n++] = regs->psw.pkey;
    sysblk.mainstor[n++] = (pasn & 0xFF00) >> 8;
    sysblk.mainstor[n++] = pasn & 0xFF;
    sysblk.mainstor[n++] = (gpr2 & 0xFF000000) >> 24;
    sysblk.mainstor[n++] = (gpr2 & 0xFF0000) >> 16;
    sysblk.mainstor[n++] = (gpr2 & 0xFF00) >> 8;
    sysblk.mainstor[n++] = gpr2 & 0xFF;

    FRAG_INVALIDATE(haddr, 8);

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    /* Recalculate the Guest absolute address */
    n = ag + (n - ah);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Convert trace entry absolue address back to real address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Return updated value of control register 12 */
    return (regs->cr[12] & ~CR12_TRACEEA) | n;

} /* end function trace_pt */
#endif /*defined(FEATURE_TRACING)*/
