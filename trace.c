/* TRACE.C      (c) Copyright Roger Bowler, 1999-2000                */
/*              ESA/390 Trace Functions                              */

/*-------------------------------------------------------------------*/
/* This module implements ESA/390 implicit and explicit tracing      */
/* for the Hercules S/370 and ESA/390 emulator.                      */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      Branch tracing and cross-memory tracing by Jan Jaeger        */
/*-------------------------------------------------------------------*/

#include "hercules.h"

#ifdef FEATURE_TRACING
/*-------------------------------------------------------------------*/
/* Form explicit trace entry                                         */
/*                                                                   */
/* Input:                                                            */
/*      n2      32-bit operand value of TRACE instruction            */
/*      r1      First register to be traced                          */
/*      r3      Last register to be traced                           */
/*      regs    Pointer to the CPU register context                  */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
void trace_tr (U32 n2, int r1, int r3, REGS *regs)
{
U32     n;                              /* Addr of trace table entry */
int     i;                              /* Loop counter              */
U64     dreg;                           /* 64-bit work area          */

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
        program_check (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Convert trace entry real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Program check if trace entry is outside main storage */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Program check if storing the maximum length trace
       entry (76 bytes) would overflow a 4K page boundary */
    if ( ((n + 76) & STORAGE_KEY_PAGEMASK) != (n & STORAGE_KEY_PAGEMASK) )
        program_check (regs, PGM_TRACE_TABLE_EXCEPTION);

    /* Calculate the number of registers to be traced, minus 1 */
    i = ( r3 < r1 ) ? r3 + 16 - r1 : r3 - r1;

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

    /* Set the main storage change and reference bits */
    STORAGE_KEY(n) |= (STORKEY_REF | STORKEY_CHANGE);

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

} /* end function trace_tr */


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
        program_check (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Convert trace entry real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Program check if trace entry is outside main storage */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Program check if storing would overflow a 4K page boundary */
    if ( ((n + 4) & STORAGE_KEY_PAGEMASK) != (n & STORAGE_KEY_PAGEMASK) )
        program_check (regs, PGM_TRACE_TABLE_EXCEPTION);

    /* Set the main storage change and reference bits */
    STORAGE_KEY(n) |= (STORKEY_REF | STORKEY_CHANGE);

    /* Build the branch trace entry */
    if (amode)
        sysblk.mainstor[n++] = 0x80 | ((ia & 0x7F000000) >> 24);
    else
        sysblk.mainstor[n++] = 0x00;
    sysblk.mainstor[n++] = (ia & 0xFF0000) >> 16;
    sysblk.mainstor[n++] = (ia & 0xFF00) >> 8;
    sysblk.mainstor[n++] = ia & 0xFF;

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
        program_check (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Convert trace entry real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Program check if trace entry is outside main storage */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Program check if storing would overflow a 4K page boundary */
    if ( ((n + 8) & STORAGE_KEY_PAGEMASK) != (n & STORAGE_KEY_PAGEMASK) )
        program_check (regs, PGM_TRACE_TABLE_EXCEPTION);

    /* Set the main storage change and reference bits */
    STORAGE_KEY(n) |= (STORKEY_REF | STORKEY_CHANGE);

    /* Build the Branch in Subspace Group trace entry */
    sysblk.mainstor[n++] = 0x41;
    sysblk.mainstor[n++] = (regs->psw.prob << 7) | ((alet & 0x7F0000) >> 16);
    sysblk.mainstor[n++] = (alet & 0xFF00) >> 8;
    sysblk.mainstor[n++] = alet & 0xFF;
    sysblk.mainstor[n++] = (regs->psw.ia & 0xFF000000) >> 24;
    sysblk.mainstor[n++] = (regs->psw.ia & 0xFF0000) >> 16;
    sysblk.mainstor[n++] = (regs->psw.ia & 0xFF00) >> 8;
    sysblk.mainstor[n++] = regs->psw.ia & 0xFF;

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
        program_check (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Convert trace entry real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Program check if trace entry is outside main storage */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Program check if storing would overflow a 4K page boundary */
    if ( ((n + 4) & STORAGE_KEY_PAGEMASK) != (n & STORAGE_KEY_PAGEMASK) )
        program_check (regs, PGM_TRACE_TABLE_EXCEPTION);

    /* Set the main storage change and reference bits */
    STORAGE_KEY(n) |= (STORKEY_REF | STORKEY_CHANGE);

    /* Build the Set Secondary ASN trace entry */
    sysblk.mainstor[n++] = 0x10;
    sysblk.mainstor[n++] = 0x00;
    sysblk.mainstor[n++] = (sasn & 0xFF00) >> 8;
    sysblk.mainstor[n++] = sasn & 0xFF;

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
        program_check (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Convert trace entry real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Program check if trace entry is outside main storage */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Program check if storing would overflow a 4K page boundary */
    if ( ((n + 8) & STORAGE_KEY_PAGEMASK) != (n & STORAGE_KEY_PAGEMASK) )
        program_check (regs, PGM_TRACE_TABLE_EXCEPTION);

    /* Set the main storage change and reference bits */
    STORAGE_KEY(n) |= (STORKEY_REF | STORKEY_CHANGE);

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
        program_check (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Convert trace entry real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Program check if trace entry is outside main storage */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Program check if storing would overflow a 4K page boundary */
    if ( ((n + 12) & STORAGE_KEY_PAGEMASK) != (n & STORAGE_KEY_PAGEMASK) )
        program_check (regs, PGM_TRACE_TABLE_EXCEPTION);

    pasn = newregs->cr[4] & CR4_PASN;

    /* Set the main storage change and reference bits */
    STORAGE_KEY(n) |= (STORKEY_REF | STORKEY_CHANGE);

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
        program_check (regs, PGM_PROTECTION_EXCEPTION);
    }

    /* Convert trace entry real address to absolute address */
    n = APPLY_PREFIXING (n, regs->pxr);

    /* Program check if trace entry is outside main storage */
    if ( n >= sysblk.mainsize )
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

    /* Program check if storing would overflow a 4K page boundary */
    if ( ((n + 8) & STORAGE_KEY_PAGEMASK) != (n & STORAGE_KEY_PAGEMASK) )
        program_check (regs, PGM_TRACE_TABLE_EXCEPTION);

    /* Set the main storage change and reference bits */
    STORAGE_KEY(n) |= (STORKEY_REF | STORKEY_CHANGE);

    /* Build the program transfer trace entry */
    sysblk.mainstor[n++] = 0x31;
    sysblk.mainstor[n++] = regs->psw.pkey;
    sysblk.mainstor[n++] = (pasn & 0xFF00) >> 8;
    sysblk.mainstor[n++] = pasn & 0xFF;
    sysblk.mainstor[n++] = (gpr2 & 0xFF000000) >> 24;
    sysblk.mainstor[n++] = (gpr2 & 0xFF0000) >> 16;
    sysblk.mainstor[n++] = (gpr2 & 0xFF00) >> 8;
    sysblk.mainstor[n++] = gpr2 & 0xFF;

    /* Return updated value of control register 12 */
    return (regs->cr[12] & ~CR12_TRACEEA) | n;

} /* end function trace_pt */
#endif /*FEATURE_TRACING*/
