/* STACK.C      (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Linkage Stack Operations                     */

/*-------------------------------------------------------------------*/
/* This module implements the linkage stack functions of ESA/390     */
/* described in SA22-7201-04 ESA/390 Principles of Operation.        */
/* The numbers in square brackets refer to sections in the manual.   */
/*-------------------------------------------------------------------*/

#include "hercules.h"

#undef  STACK_DEBUG

/*-------------------------------------------------------------------*/
/* Convert linkage stack virtual address to absolute address         */
/*                                                                   */
/* Input:                                                            */
/*      vaddr   Virtual address of stack entry                       */
/*      regs    Pointer to the CPU register context                  */
/*      acctype Type of access requested: READ or WRITE              */
/* Return value:                                                     */
/*      Absolute address of stack entry.                             */
/*                                                                   */
/*      The virtual address is translated using the segment table    */
/*      for the home address space.  Key-controlled protection does  */
/*      not apply to linkage stack operations, but page protection   */
/*      and low-address protection do apply.                         */
/*                                                                   */
/*      A program check may be generated if the stack address causes */
/*      an addressing, protection, or translation exception, and in  */
/*      this case the function does not return.                      */
/*-------------------------------------------------------------------*/
static U32 abs_stack_addr (U32 vaddr, REGS *regs, int acctype)
{
int     rc;                             /* Return code               */
U32     raddr;                          /* Real address              */
U32     aaddr;                          /* Absolute address          */
int     private = 0;                    /* 1=Private address space   */
int     protect = 0;                    /* 1=ALE or page protection  */
int     stid;                           /* Segment table indication  */
U16     xcode;                          /* Exception code            */

    /* Convert to real address using home segment table */
    rc = translate_addr (vaddr, 0, regs, ACCTYPE_STACK, &raddr,
                            &xcode, &private, &protect, &stid);
    if (rc != 0)
    {
        program_check (regs, xcode);
        return 0;
    }

    /* Low-address protection prohibits stores into locations
       0-511 of non-private address spaces if CR0 bit 3 is set */
    if (acctype == ACCTYPE_WRITE
        && vaddr < 512
        && (regs->cr[0] & CR0_LOW_PROT)
        && private == 0)
    {
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
        regs->tea = (vaddr & TEA_EFFADDR) | TEA_ST_HOME;
        regs->excarid = 0;
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
        program_check (regs, PGM_PROTECTION_EXCEPTION);
        return 0;
    }

    /* Page protection prohibits all stores into the page */
    if (acctype == ACCTYPE_WRITE && protect)
    {
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
        regs->tea = (vaddr & TEA_EFFADDR) | TEA_PROT_AP | TEA_ST_HOME;
        regs->excarid = 0;
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
        program_check (regs, PGM_PROTECTION_EXCEPTION);
        return 0;
    }

    /* Convert real address to absolute address */
    aaddr = APPLY_PREFIXING (raddr, regs->pxr);

    /* Program check if absolute address is outside main storage */
    if (aaddr >= sysblk.mainsize)
    {
        program_check (regs, PGM_ADDRESSING_EXCEPTION);
        return 0;
    }

    /* Set the reference and change bits in the storage key */
    STORAGE_KEY(aaddr) |= STORKEY_REF;
    if (acctype == ACCTYPE_WRITE)
        STORAGE_KEY(aaddr) |= STORKEY_CHANGE;

    /* Return absolute address */
    return aaddr;

} /* end function abs_stack_addr */

/*-------------------------------------------------------------------*/
/* Form a new entry on the linkage stack                             */
/*                                                                   */
/* Input:                                                            */
/*      etype   Linkage stack entry type                             */
/*      retna   Return amode and instruction address to be stored    */
/*              in bytes 140-143 of the new stack entry              */
/*      calla   Called amode and instruction address (for BAKR)      */
/*              or called PC number (for PC) to be stored in         */
/*              bytes 148-151 of the new stack entry                 */
/*      regs    Pointer to the CPU register context                  */
/*                                                                   */
/*      This function performs the stacking process for the          */
/*      Branch and Stack (BAKR) and Program Call (PC) instructions.  */
/*                                                                   */
/*      In the event of any stack error, this function generates     */
/*      a program check and does not return.                         */
/*-------------------------------------------------------------------*/
void form_stack_entry (BYTE etype, U32 retna, U32 calla, REGS *regs)
{
U32     lsea;                           /* Linkage stack entry addr  */
U32     abs;                            /* Absolute addr new entry   */
U32     absold;                         /* Absolute addr old entry   */
LSED    lsed;                           /* Linkage stack entry desc. */
LSED    lsed2;                          /* New entry descriptor      */
U16     rfs;                            /* Remaining free space      */
U32     fsha;                           /* Forward section hdr addr  */
U32     bsea;                           /* Backward stack entry addr */
int     size = LSSE_SIZE;               /* Size of new stack entry   */
int     i;                              /* Array subscript           */

    /* [5.12.3] Special operation exception if CR0 bit 15 is zero,
       or if DAT is off, or if not primary-space mode or AR-mode */
    if ((regs->cr[0] & CR0_ASF) == 0
        || REAL_MODE(&regs->psw)
        || regs->psw.space == 1)
    {
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
        return;
    }

    /* [5.12.3.1] Locate space for a new linkage stack entry */

    /* Obtain the virtual address of the current entry from CR15 */
    lsea = regs->cr[15] & CR15_LSEA;

    /* Fetch the entry descriptor of the current entry */
    absold = abs_stack_addr (lsea, regs, ACCTYPE_READ);
    memcpy (&lsed, sysblk.mainstor+absold, sizeof(LSED));

#ifdef STACK_DEBUG
    logmsg ("stack: Current stack entry at %8.8lX\n", lsea);
    logmsg ("stack: et=%2.2X si=%2.2X rfs=%2.2X%2.2X nes=%2.2X%2.2X\n",
            lsed.uet, lsed.si, lsed.rfs[0],
            lsed.rfs[1], lsed.nes[0], lsed.nes[1]);
#endif /*STACK_DEBUG*/

    /* Check whether the current linkage stack section has enough
       remaining free space to contain the new stack entry */
    rfs = (lsed.rfs[0] << 8) | lsed.rfs[1];
    if (rfs < size)
    {
        /* Program check if remaining free space not a multiple of 8 */
        if ((rfs & 0x07) != 0)
        {
            program_check (regs, PGM_STACK_SPECIFICATION_EXCEPTION);
            return;
        }

        /* Not enough space, so fetch the second word of the
           trailer entry of the current linkage stack section */
        lsea += sizeof(LSED) + rfs;
        lsea &= 0x7FFFFFFF;
        abs = abs_stack_addr (lsea, regs, ACCTYPE_READ);
        fsha = (sysblk.mainstor[abs+4] << 24)
                | (sysblk.mainstor[abs+5] << 16)
                | (sysblk.mainstor[abs+6] << 8)
                | sysblk.mainstor[abs+7];

#ifdef STACK_DEBUG
        logmsg ("stack: Forward section header addr %8.8lX\n", fsha);
#endif /*STACK_DEBUG*/

        /* Stack full exception if forward address is not valid */
        if ((fsha & LSTE1_FVALID) == 0)
        {
            program_check (regs, PGM_STACK_FULL_EXCEPTION);
            return;
        }

        /* Extract the forward section header address, which points to
           the entry descriptor (words 2-3) of next section's header */
        fsha &= LSTE1_FSHA;

        /* Fetch the entry descriptor of the next section's header */
        abs = abs_stack_addr (fsha, regs, ACCTYPE_READ);
        memcpy (&lsed, sysblk.mainstor+abs, sizeof(LSED));

#ifdef STACK_DEBUG
        logmsg ("stack: et=%2.2X si=%2.2X rfs=%2.2X%2.2X "
                "nes=%2.2X%2.2X\n",
                lsed.uet, lsed.si, lsed.rfs[0],
                lsed.rfs[1], lsed.nes[0], lsed.nes[1]);
#endif /*STACK_DEBUG*/

        /* Program check if the next linkage stack section does not
           have enough free space to contain the new stack entry */
        rfs = (lsed.rfs[0] << 8) | lsed.rfs[1];
        if (rfs < size)
        {
            program_check (regs, PGM_STACK_SPECIFICATION_EXCEPTION);
            return;
        }

        /* Calculate the virtual address of the new section's header
           entry, which is 8 bytes before the entry descriptor */
        lsea = fsha - 8;
        lsea &= 0x7FFFFFFF;

        /* Form the backward stack entry address and place it
           in word 1 of the new section's header entry */
        bsea = LSHE1_BVALID | (regs->cr[15] & CR15_LSEA);
        abs = abs_stack_addr (lsea, regs, ACCTYPE_WRITE);
        sysblk.mainstor[abs+4] = (bsea >> 24) & 0xFF;
        sysblk.mainstor[abs+5] = (bsea >> 16) & 0xFF;
        sysblk.mainstor[abs+6] = (bsea >> 8) & 0xFF;
        sysblk.mainstor[abs+7] = bsea & 0xFF;

        /* Update CR15 to contain the virtual address of the entry
           descriptor of the new section's header entry */
        lsea = fsha;
        regs->cr[15] = lsea & CR15_LSEA;

    } /* end if(rfs<size) */

    /* [5.12.3.2] Form the new stack entry */

    /* Calculate the virtual address of the new stack entry */
    lsea += sizeof(LSED);
    lsea &= 0x7FFFFFFF;

#ifdef STACK_DEBUG
    logmsg ("stack: New stack entry at %8.8lX\n", lsea);
#endif /*STACK_DEBUG*/

    /* Store general registers 0-15 in bytes 0-63 of the new entry */
    abs = abs_stack_addr (lsea, regs, ACCTYPE_WRITE);
    for (i = 0; i < 16; i++)
    {
        /* Store the general register in the stack entry */
        sysblk.mainstor[abs] = (regs->gpr[i] >> 24) & 0xFF;
        sysblk.mainstor[abs+1] = (regs->gpr[i] >> 16) & 0xFF;
        sysblk.mainstor[abs+2] = (regs->gpr[i] >> 8) & 0xFF;
        sysblk.mainstor[abs+3] = regs->gpr[i] & 0xFF;

#ifdef STACK_DEBUG
        logmsg ("stack: GPR%d=%8.8lX stored at V:%8.8lX A:%8.8lX\n",
                i, regs->gpr[i], lsea, abs);
#endif /*STACK_DEBUG*/

        /* Update the virtual and absolute addresses */
        lsea += 4;
        lsea &= 0x7FFFFFFF;
        abs += 4;

        /* Recalculate absolute address if page boundary crossed */
        if ((lsea & STORAGE_KEY_BYTEMASK) == 0x000)
            abs = abs_stack_addr (lsea, regs, ACCTYPE_WRITE);

    } /* end for(i) */

    /* Store access registers 0-15 in bytes 64-127 of the new entry */
    for (i = 0; i < 16; i++)
    {
        /* Store the access register in the stack entry */
        sysblk.mainstor[abs] = (regs->ar[i] >> 24) & 0xFF;
        sysblk.mainstor[abs+1] = (regs->ar[i] >> 16) & 0xFF;
        sysblk.mainstor[abs+2] = (regs->ar[i] >> 8) & 0xFF;
        sysblk.mainstor[abs+3] = regs->ar[i] & 0xFF;

#ifdef STACK_DEBUG
        logmsg ("stack: AR%d=%8.8lX stored at V:%8.8lX A:%8.8lX\n",
                i, regs->ar[i], lsea, abs);
#endif /*STACK_DEBUG*/

        /* Update the virtual and absolute addresses */
        lsea += 4;
        lsea &= 0x7FFFFFFF;
        abs += 4;

        /* Recalculate absolute address if page boundary crossed */
        if ((lsea & STORAGE_KEY_BYTEMASK) == 0x000)
            abs = abs_stack_addr (lsea, regs, ACCTYPE_WRITE);

    } /* end for(i) */

    /* Store the PKM, SASN, EAX, and PASN in bytes 128-135 */
    sysblk.mainstor[abs] = (regs->cr[3] >> 24) & 0xFF;
    sysblk.mainstor[abs+1] = (regs->cr[3] >> 16) & 0xFF;
    sysblk.mainstor[abs+2] = (regs->cr[3] >> 8) & 0xFF;
    sysblk.mainstor[abs+3] = regs->cr[3] & 0xFF;
    sysblk.mainstor[abs+4] = (regs->cr[8] >> 24) & 0xFF;
    sysblk.mainstor[abs+5] = (regs->cr[8] >> 16) & 0xFF;
    sysblk.mainstor[abs+6] = (regs->cr[4] >> 8) & 0xFF;
    sysblk.mainstor[abs+7] = regs->cr[4] & 0xFF;

    /* Update virtual and absolute addresses to point to byte 136 */
    lsea += 8;
    lsea &= 0x7FFFFFFF;
    abs += 8;

    /* Recalculate absolute address if page boundary crossed */
    if ((lsea & STORAGE_KEY_BYTEMASK) == 0x000)
        abs = abs_stack_addr (lsea, regs, ACCTYPE_WRITE);

    /* Store the current PSW in bytes 136-143 */
    store_psw (&regs->psw, sysblk.mainstor+abs);

    /* Replace bytes 140-143 by the return address */
    sysblk.mainstor[abs+4] = (retna >> 24) & 0xFF;
    sysblk.mainstor[abs+5] = (retna >> 16) & 0xFF;
    sysblk.mainstor[abs+6] = (retna >> 8) & 0xFF;
    sysblk.mainstor[abs+7] = retna & 0xFF;

#ifdef STACK_DEBUG
    logmsg ("stack: PSW=%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X "
            "stored at V:%8.8lX A:%8.8lX\n",
            sysblk.mainstor[abs], sysblk.mainstor[abs+1],
            sysblk.mainstor[abs+2], sysblk.mainstor[abs+3],
            sysblk.mainstor[abs+4], sysblk.mainstor[abs+5],
            sysblk.mainstor[abs+6], sysblk.mainstor[abs+7],
            lsea, abs);
#endif /*STACK_DEBUG*/

    /* Update virtual and absolute addresses to point to byte 144 */
    lsea += 8;
    lsea &= 0x7FFFFFFF;
    abs += 8;

    /* Recalculate absolute address if page boundary crossed */
    if ((lsea & STORAGE_KEY_BYTEMASK) == 0x000)
        abs = abs_stack_addr (lsea, regs, ACCTYPE_WRITE);

    /* Store the called address or PC number in bytes 148-151 */
    sysblk.mainstor[abs+4] = (calla >> 24) & 0xFF;
    sysblk.mainstor[abs+5] = (calla >> 16) & 0xFF;
    sysblk.mainstor[abs+6] = (calla >> 8) & 0xFF;
    sysblk.mainstor[abs+7] = calla & 0xFF;

    /* Update virtual and absolute addresses to point to byte 152 */
    lsea += 8;
    lsea &= 0x7FFFFFFF;
    abs += 8;

    /* Recalculate absolute address if page boundary crossed */
    if ((lsea & STORAGE_KEY_BYTEMASK) == 0x000)
        abs = abs_stack_addr (lsea, regs, ACCTYPE_WRITE);

    /* Store zeroes in bytes 152-159 */
    memset (sysblk.mainstor+abs, 0, 8);

    /* Update virtual and absolute addresses to point to byte 160 */
    lsea += 8;
    lsea &= 0x7FFFFFFF;
    abs += 8;

    /* Recalculate absolute address if page boundary crossed */
    if ((lsea & STORAGE_KEY_BYTEMASK) == 0x000)
        abs = abs_stack_addr (lsea, regs, ACCTYPE_WRITE);

    /* Build the new linkage stack entry descriptor */
    memset (&lsed2, 0, sizeof(LSED));
    lsed2.uet = etype & LSED_UET_ET;
    lsed2.si = lsed.si;
    rfs -= size;
    lsed2.rfs[0] = (rfs >> 8) & 0xFF;
    lsed2.rfs[1] = rfs & 0xFF;

    /* Store the linkage stack entry descriptor in bytes 160-167 */
    memcpy (sysblk.mainstor+abs, &lsed2, sizeof(LSED));

#ifdef STACK_DEBUG
    logmsg ("stack: New stack entry at %8.8lX\n", lsea);
    logmsg ("stack: et=%2.2X si=%2.2X rfs=%2.2X%2.2X nes=%2.2X%2.2X\n",
            lsed2.uet, lsed2.si, lsed2.rfs[0],
            lsed2.rfs[1], lsed2.nes[0], lsed2.nes[1]);
#endif /*STACK_DEBUG*/

    /* [5.12.3.3] Update the current entry */
    lsed.nes[0] = (size >> 8) & 0xFF;
    lsed.nes[1] = size & 0xFF;
    memcpy (sysblk.mainstor+absold, &lsed, sizeof(LSED));

#ifdef STACK_DEBUG
    logmsg ("stack: Previous stack entry updated at A:%8.8lX\n",
            absold);
    logmsg ("stack: et=%2.2X si=%2.2X rfs=%2.2X%2.2X nes=%2.2X%2.2X\n",
            lsed.uet, lsed.si, lsed.rfs[0],
            lsed.rfs[1], lsed.nes[0], lsed.nes[1]);
#endif /*STACK_DEBUG*/

    /* [5.12.3.4] Update control register 15 */
    regs->cr[15] = lsea & CR15_LSEA;

#ifdef STACK_DEBUG
    logmsg ("stack: CR15=%8.8lX\n", regs->cr[15]);
#endif /*STACK_DEBUG*/

} /* end function form_stack_entry */

/*-------------------------------------------------------------------*/
/* Locate the current linkage stack entry                            */
/*                                                                   */
/* Input:                                                            */
/*      prinst  1=PR instruction, 0=EREG/ESTA/MSTA instruction       */
/*      lsedptr Pointer to an LSED structure                         */
/*      regs    Pointer to the CPU register context                  */
/* Output:                                                           */
/*      The entry descriptor for the current state entry in the      */
/*      linkage stack is copied into the LSED structure.             */
/*      The home virtual address of the entry descriptor is          */
/*      returned as the function return value.                       */
/*                                                                   */
/*      This function performs the first part of the unstacking      */
/*      process for the Program Return (PR), Extract Stacked         */
/*      Registers (EREG), Extract Stacked State (ESTA), and          */
/*      Modify Stacked State (MSTA) instructions.                    */
/*                                                                   */
/*      In the event of any stack error, this function generates     */
/*      a program check and does not return.                         */
/*-------------------------------------------------------------------*/
static U32 locate_stack_entry (int prinst, LSED *lsedptr, REGS *regs)
{
U32     lsea;                           /* Linkage stack entry addr  */
U32     abs;                            /* Absolute address          */
U32     bsea;                           /* Backward stack entry addr */

    /* [5.12.4] Special operation exception if CR0 bit 15 is zero,
       or if DAT is off, or if in secondary-space mode */
    if ((regs->cr[0] & CR0_ASF) == 0
        || REAL_MODE(&regs->psw)
        || SECONDARY_SPACE_MODE(&regs->psw))
    {
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
        return 0;
    }

    /* Special operation exception if home space mode PR instruction */
    if (prinst && HOME_SPACE_MODE(&regs->psw))
    {
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
        return 0;
    }

    /* [5.12.4.1] Locate current entry and process header entry */

    /* Obtain the virtual address of the current entry from CR15 */
    lsea = regs->cr[15] & CR15_LSEA;

    /* Fetch the entry descriptor of the current entry */
    abs = abs_stack_addr (lsea, regs, ACCTYPE_READ);
    memcpy (lsedptr, sysblk.mainstor+abs, sizeof(LSED));

#ifdef STACK_DEBUG
    logmsg ("stack: Stack entry located at %8.8lX\n", lsea);
    logmsg ("stack: et=%2.2X si=%2.2X rfs=%2.2X%2.2X nes=%2.2X%2.2X\n",
            lsedptr->uet, lsedptr->si, lsedptr->rfs[0],
            lsedptr->rfs[1], lsedptr->nes[0], lsedptr->nes[1]);
#endif /*STACK_DEBUG*/

    /* Check for a header entry */
    if ((lsedptr->uet & LSED_UET_ET) == LSED_UET_HDR)
    {
        /* For PR instruction only, generate stack operation exception
           if the unstack suppression bit in the header entry is set */
        if (prinst && (lsedptr->uet & LSED_UET_U))
        {
            program_check (regs, PGM_STACK_OPERATION_EXCEPTION);
            return 0;
        }

        /* Calculate the virtual address of the header entry,
           which is 8 bytes before the entry descriptor */
        lsea -= 8;
        lsea &= 0x7FFFFFFF;

        /* Fetch the backward stack entry address from header word 1 */
        abs = abs_stack_addr (lsea, regs, ACCTYPE_READ);
        bsea = (sysblk.mainstor[abs+4] << 24)
                | (sysblk.mainstor[abs+5] << 16)
                | (sysblk.mainstor[abs+6] << 8)
                | sysblk.mainstor[abs+7];

        /* Stack empty exception if backward address is not valid */
        if ((bsea & LSHE1_BVALID) == 0)
        {
            program_check (regs, PGM_STACK_EMPTY_EXCEPTION);
            return 0;
        }

        /* Extract the virtual address of the entry descriptor
           of the last entry in the previous section */
        lsea = bsea & LSHE1_BSEA;

        /* Fetch the entry descriptor of the designated entry */
        abs = abs_stack_addr (lsea, regs, ACCTYPE_READ);
        memcpy (lsedptr, sysblk.mainstor+abs, sizeof(LSED));

#ifdef STACK_DEBUG
        logmsg ("stack: Stack entry located at %8.8lX\n", lsea);
        logmsg ("stack: et=%2.2X si=%2.2X rfs=%2.2X%2.2X "
                "nes=%2.2X%2.2X\n",
                lsedptr->uet, lsedptr->si, lsedptr->rfs[0],
                lsedptr->rfs[1], lsedptr->nes[0], lsedptr->nes[1]);
#endif /*STACK_DEBUG*/

        /* Stack specification exception if this is also a header */
        if ((lsedptr->uet & LSED_UET_ET) == LSED_UET_HDR)
        {
            program_check (regs, PGM_STACK_SPECIFICATION_EXCEPTION);
            return 0;
        }

        /* For PR instruction only, update control register 15 */
        if (prinst)
        {
            regs->cr[15] = lsea & CR15_LSEA;
        }

    } /* end if(LSED_UET_HDR) */

    /* [5.12.4.2] Check for a state entry */

    /* Stack type exception if this is not a state entry */
    if ((lsedptr->uet & LSED_UET_ET) != LSED_UET_BAKR
        && (lsedptr->uet & LSED_UET_ET) != LSED_UET_PC)
    {
        program_check (regs, PGM_STACK_TYPE_EXCEPTION);
        return 0;
    }

    /* [5.12.4.3] For PR instruction only, stack operation exception
       if the unstack suppression bit in the state entry is set */
    if (prinst && (lsedptr->uet & LSED_UET_U))
    {
        program_check (regs, PGM_STACK_OPERATION_EXCEPTION);
        return 0;
    }

    /* Return the virtual address of the entry descriptor */
    return lsea;

} /* end function locate_stack_entry */

/*-------------------------------------------------------------------*/
/* Unstack registers                                                 */
/*                                                                   */
/* Input:                                                            */
/*      lsea    Virtual address of linkage stack entry descriptor    */
/*      r1      The number of the first register to be loaded        */
/*      r2      The number of the last register to be loaded         */
/*      regs    Pointer to the CPU register context                  */
/*                                                                   */
/*      This function loads a range of general registers and         */
/*      access registers from the specified linkage stack entry.     */
/*      It is called by the Extract Stacked Registers (EREG) and     */
/*      Program Return (PR) instructions after they have located     */
/*      the current state entry in the linkage stack.                */
/*                                                                   */
/*      If a translation exception occurs when accessing the stack   */
/*      entry, then a program check will be generated by the         */
/*      abs_stack_addr subroutine, and the function will not return. */
/*      Since the stack entry can only span at most two pages, and   */
/*      the caller will have already successfully accessed the       */
/*      entry descriptor which is at the end of the stack entry,     */
/*      the only place a translation exception can occur is when     */
/*      attempting to load the first register, in which case the     */
/*      operation is nullified with all registers unchanged.         */
/*-------------------------------------------------------------------*/
static void unstack_registers (U32 lsea, int r1, int r2, REGS *regs)
{
U32     abs = 0;                        /* Absolute address          */
int     i;                              /* Array subscript           */
int     tranreqd;                       /* 1=Translation required    */

    /* Point back to byte 0 of the state entry */
    lsea -= LSSE_SIZE - sizeof(LSED);
    lsea &= 0x7FFFFFFF;

    /* Set indicator to force calculation of absolute address */
    tranreqd = 1;

#ifdef STACK_DEBUG
    logmsg ("stack: Unstacking registers %d-%d from %8.8lX\n",
            r1, r2, lsea);
#endif /*STACK_DEBUG*/

    /* Load general registers from bytes 0-63 of the state entry */
    for (i = 0; i < 16; i++)
    {
        /* Recalculate absolute address if page boundary crossed */
        if ((lsea & STORAGE_KEY_BYTEMASK) == 0x000)
            tranreqd = 1;

        /* Calculate absolute address if required */
        if (tranreqd)
        {
            abs = abs_stack_addr (lsea, regs, ACCTYPE_READ);
            tranreqd = 0;
        }

        /* Load the general register from the stack entry */
        if ((r1 <= r2 && i >= r1 && i <= r2)
            || (r1 > r2 && (i >= r1 || i <= r2)))
        {
            regs->gpr[i] = (sysblk.mainstor[abs] << 24)
                            | (sysblk.mainstor[abs+1] << 16)
                            | (sysblk.mainstor[abs+2] << 8)
                            | sysblk.mainstor[abs+3];

#ifdef STACK_DEBUG
            logmsg ("stack: GPR%d=%8.8lX "
                    "loaded from V:%8.8lX A:%8.8lX\n",
                    i, regs->gpr[i], lsea, abs);
#endif /*STACK_DEBUG*/
        }

        /* Update the virtual and absolute addresses */
        lsea += 4;
        lsea &= 0x7FFFFFFF;
        abs += 4;

    } /* end for(i) */

    /* Load access registers from bytes 64-127 of the state entry */
    for (i = 0; i < 16; i++)
    {
        /* Recalculate absolute address if page boundary crossed */
        if ((lsea & STORAGE_KEY_BYTEMASK) == 0x000)
            tranreqd = 1;

        /* Calculate absolute address if required */
        if (tranreqd)
        {
            abs = abs_stack_addr (lsea, regs, ACCTYPE_READ);
            tranreqd = 0;
        }

        /* Load the access register from the stack entry */
        if ((r1 <= r2 && i >= r1 && i <= r2)
            || (r1 > r2 && (i >= r1 || i <= r2)))
        {
            regs->ar[i] = (sysblk.mainstor[abs] << 24)
                            | (sysblk.mainstor[abs+1] << 16)
                            | (sysblk.mainstor[abs+2] << 8)
                            | sysblk.mainstor[abs+3];

#ifdef STACK_DEBUG
            logmsg ("stack: AR%d=%8.8lX "
                    "loaded from V:%8.8lX A:%8.8lX\n",
                    i, regs->ar[i], lsea, abs);
#endif /*STACK_DEBUG*/
        }

        /* Update the virtual and absolute addresses */
        lsea += 4;
        lsea &= 0x7FFFFFFF;
        abs += 4;

    } /* end for(i) */

} /* end function unstack_registers */

/*-------------------------------------------------------------------*/
/* Program return unstack                                            */
/*                                                                   */
/* Input:                                                            */
/*      regs    Pointer to a copy of the CPU register context        */
/* Output:                                                           */
/*      lsedap  The absolute address of the entry descriptor of      */
/*              the new current entry on the linkage stack.          */
/* Return value:                                                     */
/*      The type of entry unstacked: LSED_UET_BAKR or LSED_UET_PC    */
/*                                                                   */
/*      This function performs the restoring and updating parts      */
/*      of the unstacking process for the Program Return (PR)        */
/*      instruction.  If a program exception occurs during the PR    */
/*      instruction (either during or after the unstack), then the   */
/*      effects of the instruction must be nullified or suppressed.  */
/*      This is achieved by updating a copy of the CPU register      */
/*      context instead of the actual register context.              */
/*      The current register context is replaced by the copy         */
/*      only on successful completion of the PR instruction.         */
/*                                                                   */
/*      In the event of any stack error, this function generates     */
/*      a program check and does not return.                         */
/*-------------------------------------------------------------------*/
int program_return_unstack (REGS *regs, U32 *lsedap)
{
LSED    lsed;                           /* Linkage stack entry desc. */
U32     lsea;                           /* Linkage stack entry addr  */
U32     abs;                            /* Absolute address          */
int     permode;                        /* 1=PER mode is set in PSW  */
int     rc;                             /* Return code               */
U16     pkm;                            /* PSW key mask              */
U16     sasn;                           /* Secondary ASN             */
U16     eax;                            /* Extended AX               */
U16     pasn;                           /* Primary ASN               */

    /* Find the virtual address of the entry descriptor
       of the current state entry in the linkage stack */
    lsea = locate_stack_entry (1, &lsed, regs);

    /* [5.12.4.3] Restore information from stack entry */

    /* Load registers 2-14 from the stack entry */
    unstack_registers (lsea, 2, 14, regs);

    /* Point back to byte 128 of the state entry */
    lsea -= LSSE_SIZE - sizeof(LSED);
    lsea += 128;
    lsea &= 0x7FFFFFFF;

    /* Translate virtual address to absolute address */
    abs = abs_stack_addr (lsea, regs, ACCTYPE_READ);

    /* For a call state entry, replace the PKM, SASN, EAX, and PASN */
    if ((lsed.uet & LSED_UET_ET) == LSED_UET_PC)
    {
        /* Fetch the PKM from bytes 128-129 of the stack entry */
        pkm = (sysblk.mainstor[abs] << 8)
                | sysblk.mainstor[abs+1];

        /* Fetch the SASN from bytes 130-131 of the stack entry */
        sasn = (sysblk.mainstor[abs+2] << 8)
                | sysblk.mainstor[abs+3];

        /* Fetch the EAX from bytes 132-133 of the stack entry */
        eax = (sysblk.mainstor[abs+4] << 8)
                | sysblk.mainstor[abs+5];

        /* Fetch the PASN from bytes 134-135 of the stack entry */
        pasn = (sysblk.mainstor[abs+6] << 8)
                | sysblk.mainstor[abs+7];

        /* Load PKM into CR3 bits 0-15 and SASN into CR3 bits 16-31 */
        regs->cr[3] = (pkm << 16) | sasn;

        /* Load EAX into CR8 bits 0-15 */
        regs->cr[8] &= ~CR8_EAX;
        regs->cr[8] |= (eax << 16);

        /* Load PASN into CR4 bits 16-31 */
        regs->cr[4] &= ~CR4_PASN;
        regs->cr[4] |= pasn;

    } /* end if(LSED_UET_PC) */

    /* Update virtual and absolute addresses to point to byte 136 */
    lsea += 8;
    lsea &= 0x7FFFFFFF;
    abs += 8;

    /* Recalculate absolute address if page boundary crossed */
    if ((lsea & STORAGE_KEY_BYTEMASK) == 0x000)
        abs = abs_stack_addr (lsea, regs, ACCTYPE_READ);

    /* Save the PER mode bit from the current PSW */
    permode = (regs->psw.sysmask & PSW_PERMODE) ? 1 : 0;

#ifdef STACK_DEBUG
    logmsg ("stack: PSW=%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X "
            "loaded from V:%8.8lX A:%8.8lX\n",
            sysblk.mainstor[abs], sysblk.mainstor[abs+1],
            sysblk.mainstor[abs+2], sysblk.mainstor[abs+3],
            sysblk.mainstor[abs+4], sysblk.mainstor[abs+5],
            sysblk.mainstor[abs+6], sysblk.mainstor[abs+7],
            lsea, abs);
#endif /*STACK_DEBUG*/

    /* Load new PSW from bytes 136-143 of the stack entry */
    rc = load_psw (&regs->psw, sysblk.mainstor+abs);
    if (rc) {
        program_check (regs, rc);
        return 0;
    }

    /* Restore the PER mode bit from the current PSW */
    if (permode)
        regs->psw.sysmask |= PSW_PERMODE;
    else
        regs->psw.sysmask &= ~PSW_PERMODE;

    /* [5.12.4.4] Calculate the virtual address of the entry
       descriptor of the preceding linkage stack entry.  The
       next entry size field of this entry will be cleared on
       successful completion of the PR instruction */
    lsea -= 136 + sizeof(LSED);
    lsea &= 0x7FFFFFFF;
    *lsedap = abs_stack_addr (lsea, regs, ACCTYPE_WRITE);

    /* [5.12.4.5] Update CR15 to point to the previous entry */
    regs->cr[15] = lsea & CR15_LSEA;

#ifdef STACK_DEBUG
    logmsg ("stack: CR15=%8.8lX\n", regs->cr[15]);
#endif /*STACK_DEBUG*/

    /* Return the entry type of the unstacked state entry */
    return (lsed.uet & LSED_UET_ET);

} /* end function program_return_unstack */

/*-------------------------------------------------------------------*/
/* Extract stacked registers                                         */
/*                                                                   */
/* Input:                                                            */
/*      r1      The number of the first register to be loaded        */
/*      r2      The number of the last register to be loaded         */
/*      regs    Pointer to the CPU register context                  */
/*                                                                   */
/*      This function loads a range of general registers and         */
/*      access registers from the current linkage stack entry.       */
/*                                                                   */
/*      In the event of any stack error, this function generates     */
/*      a program check and does not return.                         */
/*-------------------------------------------------------------------*/
void extract_stacked_registers (int r1, int r2, REGS *regs)
{
LSED    lsed;                           /* Linkage stack entry desc. */
U32     lsea;                           /* Linkage stack entry addr  */

    /* Find the virtual address of the entry descriptor
       of the current state entry in the linkage stack */
    lsea = locate_stack_entry (0, &lsed, regs);

    /* Load registers from the stack entry */
    unstack_registers (lsea, r1, r2, regs);

} /* end function extract_stacked_registers */

/*-------------------------------------------------------------------*/
/* Extract stacked state                                             */
/*                                                                   */
/* Input:                                                            */
/*      rn      The number of the even register in an even/odd pair  */
/*      code    Extraction code:                                     */
/*              0=Extract PKM/SASN/EAX/PASN, 1=Extract PSW,          */
/*              2=Extract called address, 3=Extract modifiable area  */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Returns the condition code for the ESTA instruction:         */
/*      0=State entry is BAKR, 1=State entry is PC                   */
/*                                                                   */
/*      This function loads 8 bytes of information from the current  */
/*      linkage stack state entry into a general register pair.      */
/*      The extraction code indicates which bytes are to be loaded.  */
/*                                                                   */
/*      In the event of any stack error, this function generates     */
/*      a program check and does not return.                         */
/*-------------------------------------------------------------------*/
int extract_stacked_state (int rn, BYTE code, REGS *regs)
{
LSED    lsed;                           /* Linkage stack entry desc. */
U32     lsea;                           /* Linkage stack entry addr  */
U32     abs;                            /* Absolute address          */

    /* Program check if rn is odd, or if extraction code is invalid */
    if ((rn & 1) || code > 3)
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 3;
    }

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
    regs->gpr[rn] = (sysblk.mainstor[abs] << 24)
                        | (sysblk.mainstor[abs+1] << 16)
                        | (sysblk.mainstor[abs+2] << 8)
                        | sysblk.mainstor[abs+3];
    regs->gpr[rn+1] = (sysblk.mainstor[abs+4] << 24)
                        | (sysblk.mainstor[abs+5] << 16)
                        | (sysblk.mainstor[abs+6] << 8)
                        | sysblk.mainstor[abs+7];

    /* Return condition code depending on entry type */
    return ((lsed.uet & LSED_UET_ET) == LSED_UET_PC) ? 1 : 0;

} /* end function extract_stacked_state */

/*-------------------------------------------------------------------*/
/* Modify stacked state                                              */
/*                                                                   */
/* Input:                                                            */
/*      rn      The number of the even register in an even/odd pair  */
/*      regs    Pointer to the CPU register context                  */
/*                                                                   */
/*      This function stores 8 bytes of information from a general   */
/*      register pair into the modifiable area (bytes 152-159) of    */
/*      the current linkage stack state entry.                       */
/*                                                                   */
/*      In the event of any stack error, this function generates     */
/*      a program check and does not return.                         */
/*-------------------------------------------------------------------*/
void modify_stacked_state (int rn, REGS *regs)
{
LSED    lsed;                           /* Linkage stack entry desc. */
U32     lsea;                           /* Linkage stack entry addr  */
U32     abs;                            /* Absolute address          */

    /* Program check if rn is odd */
    if (rn & 1)
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return;
    }

    /* Find the virtual address of the entry descriptor
       of the current state entry in the linkage stack */
    lsea = locate_stack_entry (0, &lsed, regs);

    /* Point back to byte 152 of the state entry */
    lsea -= LSSE_SIZE - sizeof(LSED);
    lsea += 152;
    lsea &= 0x7FFFFFFF;

    /* Store the general register pair into the state entry */
    abs = abs_stack_addr (lsea, regs, ACCTYPE_WRITE);
    sysblk.mainstor[abs] = (regs->gpr[rn] >> 24) & 0xFF;
    sysblk.mainstor[abs+1] = (regs->gpr[rn] >> 16) & 0xFF;
    sysblk.mainstor[abs+2] = (regs->gpr[rn] >> 8) & 0xFF;
    sysblk.mainstor[abs+3] = regs->gpr[rn] & 0xFF;
    sysblk.mainstor[abs+4] = (regs->gpr[rn+1] >> 24) & 0xFF;
    sysblk.mainstor[abs+5] = (regs->gpr[rn+1] >> 16) & 0xFF;
    sysblk.mainstor[abs+6] = (regs->gpr[rn+1] >> 8) & 0xFF;
    sysblk.mainstor[abs+7] = regs->gpr[rn+1] & 0xFF;

} /* end function modify_stacked_state */

