/* BLOCK.C      (c) Copyright Roger Bowler, 1994-1999                */
/*              ESA/390 Block Memory Instructions                    */

/*-------------------------------------------------------------------*/
/* This module implements the ESA/390 instructions which operate     */
/* on blocks of memory in excess of 256 bytes, i.e. Move Long,       */
/* Compare Long, Move Page, Checksum, and String instructions.       */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Move Long                                                         */
/*                                                                   */
/* Input:                                                            */
/*      r1      First operand register number                        */
/*      r2      Second operand register number                       */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Returns the condition code for the MVCL instruction.         */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int move_long (int r1, int r2, REGS *regs)
{
int     cc;                             /* Condition code            */
U32     addr1, addr2;                   /* Operand addresses         */
U32     len1, len2;                     /* Operand lengths           */
U32     n;                              /* Work area                 */
BYTE    obyte;                          /* Operand byte              */
BYTE    pad;                            /* Padding byte              */

    /* Program check if either R1 or R2 register is odd */
    if ((r1 & 1) || (r2 & 1))
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Determine the destination and source addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Load padding byte from bits 0-7 of R2+1 register */
    pad = regs->gpr[r2+1] >> 24;

    /* Load operand lengths from bits 8-31 of R1+1 and R2+1 */
    len1 = regs->gpr[r1+1] & 0x00FFFFFF;
    len2 = regs->gpr[r2+1] & 0x00FFFFFF;

    /* Test for destructive overlap */
    if ( len2 > 1 && len1 > 1
        && (!ACCESS_REGISTER_MODE(&(regs->psw))
            || (r1 == 0 ? 0 : regs->ar[r1])
               != (r2 == 0 ? 0 : regs->ar[r2])))
    {
        n = addr2 + ((len2 < len1) ? len2 : len1) - 1;
        n &= ADDRESS_MAXWRAP(regs);
        if ((n > addr2
                && (addr1 > addr2 && addr1 <= n))
          || (n <= addr2
                && (addr1 > addr2 || addr1 <= n)))
        {
            regs->gpr[r1] = addr1;
            regs->gpr[r2] = addr2;
            cc = 3;
            logmsg ("MVCL destructive overlap\n");
            logmsg ("R%2.2d=%8.8X  R%2.2d=%8.8X  "
                    "R%2.2d=%8.8X  R%2.2d=%8.8X\n",
                    r1, regs->gpr[r1], r1+1, regs->gpr[r1+1],
                    r2, regs->gpr[r2], r2+1, regs->gpr[r2+1]);
            return cc;
        }
    }

    /* Set the condition code according to the lengths */
    cc = (len1 < len2) ? 1 : (len1 > len2) ? 2 : 0;

    /* Process operands from left to right */
    while (len1 > 0)
    {
        /* Fetch byte from source operand, or use padding byte */
        if (len2 > 0)
        {
            obyte = vfetchb ( addr2, r2, regs );
            addr2++;
            addr2 &= ADDRESS_MAXWRAP(regs);
            len2--;
        }
        else
            obyte = pad;

        /* Store the byte in the destination operand */
        vstoreb ( obyte, addr1, r1, regs );
        addr1++;
        addr1 &= ADDRESS_MAXWRAP(regs);
        len1--;

        /* Update the registers */
        regs->gpr[r1] = addr1;
        regs->gpr[r1+1] &= 0xFF000000;
        regs->gpr[r1+1] |= len1;
        regs->gpr[r2] = addr2;
        regs->gpr[r2+1] &= 0xFF000000;
        regs->gpr[r2+1] |= len2;

        /* Instruction can be interrupted when a CPU determined
           number of bytes have been processed.  The instruction
           address will be backed up, and the instruction will
           be re-executed.  This is consistent with operation
           under a hypervisor such as LPAR or VM.                *JJ */
        if ((len1 > 255) && !(addr1 & 0xFFF))
        {
            regs->psw.ia -= regs->psw.ilc;
            regs->psw.ia &= ADDRESS_MAXWRAP(regs);
            break;
        }

    } /* end while(len1) */

    return cc;

} /* end function move_long */

/*-------------------------------------------------------------------*/
/* Compare Long                                                      */
/*                                                                   */
/* Input:                                                            */
/*      r1      First operand register number                        */
/*      r2      Second operand register number                       */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Returns the condition code for the CLCL instruction.         */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int compare_long (int r1, int r2, REGS *regs)
{
int     cc = 0;                         /* Condition code            */
U32     addr1, addr2;                   /* Operand addresses         */
U32     len1, len2;                     /* Operand lengths           */
BYTE    byte1, byte2;                   /* Operand bytes             */
BYTE    pad;                            /* Padding byte              */

    /* Program check if either R1 or R2 register is odd */
    if ((r1 & 1) || (r2 & 1))
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Determine the destination and source addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Load padding byte from bits 0-7 of R2+1 register */
    pad = regs->gpr[r2+1] >> 24;

    /* Load operand lengths from bits 8-31 of R1+1 and R2+1 */
    len1 = regs->gpr[r1+1] & 0x00FFFFFF;
    len2 = regs->gpr[r2+1] & 0x00FFFFFF;

    /* Process operands from left to right */
    while (len1 > 0 || len2 > 0)
    {
        /* Fetch a byte from each operand, or use padding byte */
        byte1 = (len1 > 0) ? vfetchb (addr1, r1, regs) : pad;
        byte2 = (len2 > 0) ? vfetchb (addr2, r2, regs) : pad;

        /* Compare operand bytes, set condition code if unequal */
        if (byte1 != byte2)
        {
            cc = (byte1 < byte2) ? 1 : 2;
            break;
        } /* end if */

        /* Update the first operand address and length */
        if (len1 > 0)
        {
            addr1++;
            addr1 &= ADDRESS_MAXWRAP(regs);
            len1--;
        }

        /* Update the second operand address and length */
        if (len2 > 0)
        {
            addr2++;
            addr2 &= ADDRESS_MAXWRAP(regs);
            len2--;
        }

        /* Instruction can be interrupted when a CPU determined
           number of bytes have been processed.  The instruction
           address will be backed up, and the instruction will
           be re-executed.  This is consistent with operation
           under a hypervisor such as LPAR or VM.                *JJ */
        if ((len1 + len2 > 255) && !((addr1 - len2) & 0xFFF))
        {
            regs->psw.ia -= regs->psw.ilc;
            regs->psw.ia &= ADDRESS_MAXWRAP(regs);
            break;
        }

    } /* end while(len1||len2) */

    /* Update the registers */
    regs->gpr[r1] = addr1;
    regs->gpr[r1+1] &= 0xFF000000;
    regs->gpr[r1+1] |= len1;
    regs->gpr[r2] = addr2;
    regs->gpr[r2+1] &= 0xFF000000;
    regs->gpr[r2+1] |= len2;

    return cc;

} /* end function compare_long */

/*-------------------------------------------------------------------*/
/* Move Long Extended                                                */
/*                                                                   */
/* Input:                                                            */
/*      r1      First operand register number                        */
/*      r3      Second operand register number                       */
/*      effect  Effective address operand                            */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Returns the condition code for the MVCLE instruction.        */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int move_long_extended (int r1, int r3, U32 effect, REGS *regs)
{
int     i;                              /* Loop counter              */
int     cc;                             /* Condition code            */
U32     addr1, addr2;                   /* Operand addresses         */
U32     len1, len2;                     /* Operand lengths           */
BYTE    obyte;                          /* Operand byte              */
BYTE    pad;                            /* Padding byte              */

    /* Program check if either R1 or R3 register is odd */
    if ((r1 & 1) || (r3 & 1))
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Load padding byte from bits 24-31 of effective address */
    pad = effect & 0xFF;

    /* Determine the destination and source addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r3] & ADDRESS_MAXWRAP(regs);

    /* Load operand lengths from bits 0-31 of R1+1 and R3+1 */
    len1 = regs->gpr[r1+1];
    len2 = regs->gpr[r3+1];

    /* Set the condition code according to the lengths */
    cc = (len1 < len2) ? 1 : (len1 > len2) ? 2 : 0;

    /* Process operands from left to right */
    for (i = 0; len1 > 0 || len2 > 0 ; i++)
    {
        /* If 4096 bytes have been moved, exit with cc=3 */
        if (i >= 4096)
        {
            cc = 3;
            break;
        }

        /* Fetch byte from source operand, or use padding byte */
        if (len2 > 0)
        {
            obyte = vfetchb ( addr2, r3, regs );
            addr2++;
            addr2 &= ADDRESS_MAXWRAP(regs);
            len2--;
        }
        else
            obyte = pad;

        /* Store the byte in the destination operand */
        vstoreb ( obyte, addr1, r1, regs );
        addr1++;
        addr1 &= ADDRESS_MAXWRAP(regs);
        len1--;

        /* Update the registers */
        regs->gpr[r1] = addr1;
        regs->gpr[r1+1] = len1;
        regs->gpr[r3] = addr2;
        regs->gpr[r3+1] = len2;

    } /* end for(i) */

    return cc;

} /* end function move_long_extended */

/*-------------------------------------------------------------------*/
/* Compare Long Extended                                             */
/*                                                                   */
/* Input:                                                            */
/*      r1      First operand register number                        */
/*      r3      Second operand register number                       */
/*      effect  Effective address operand                            */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Returns the condition code for the CLCLE instruction.        */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int compare_long_extended (int r1, int r3, U32 effect, REGS *regs)
{
int     i;                              /* Loop counter              */
int     cc = 0;                         /* Condition code            */
U32     addr1, addr2;                   /* Operand addresses         */
U32     len1, len2;                     /* Operand lengths           */
BYTE    byte1, byte2;                   /* Operand bytes             */
BYTE    pad;                            /* Padding byte              */

    /* Program check if either R1 or R3 register is odd */
    if ((r1 & 1) || (r3 & 1))
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Load padding byte from bits 24-31 of effective address */
    pad = effect & 0xFF;

    /* Determine the destination and source addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r3] & ADDRESS_MAXWRAP(regs);

    /* Load operand lengths from bits 0-31 of R1+1 and R3+1 */
    len1 = regs->gpr[r1+1];
    len2 = regs->gpr[r3+1];

    /* Process operands from left to right */
    for (i = 0; len1 > 0 || len2 > 0 ; i++)
    {
        /* If 4096 bytes have been compared, exit with cc=3 */
        if (i >= 4096)
        {
            cc = 3;
            break;
        }

        /* Fetch a byte from each operand, or use padding byte */
        byte1 = (len1 > 0) ? vfetchb (addr1, r1, regs) : pad;
        byte2 = (len2 > 0) ? vfetchb (addr2, r3, regs) : pad;

        /* Compare operand bytes, set condition code if unequal */
        if (byte1 != byte2)
        {
            cc = (byte1 < byte2) ? 1 : 2;
            break;
        } /* end if */

        /* Update the first operand address and length */
        if (len1 > 0)
        {
            addr1++;
            addr1 &= ADDRESS_MAXWRAP(regs);
            len1--;
        }

        /* Update the second operand address and length */
        if (len2 > 0)
        {
            addr2++;
            addr2 &= ADDRESS_MAXWRAP(regs);
            len2--;
        }

    } /* end for(i) */

    /* Update the registers */
    regs->gpr[r1] = addr1;
    regs->gpr[r1+1] = len1;
    regs->gpr[r3] = addr2;
    regs->gpr[r3+1] = len2;

    return cc;

} /* end function compare_long_extended */

#if 0 /*INCOMPLETE*/
/*-------------------------------------------------------------------*/
/* Move Page (Facility 2)                                            */
/*                                                                   */
/* Input:                                                            */
/*      r1      First operand register number                        */
/*      r2      Second operand register number                       */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Returns the condition code for the MVPG instruction.         */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int move_page (int r1, int r2, REGS *regs)
{
int     rc;                             /* Return code               */
U32     vaddr1, vaddr2;                 /* Virtual addresses         */
U32     raddr1, raddr2;                 /* Real addresses            */
U32     aaddr1, aaddr2;                 /* Absolute addresses        */
int     xpvalid1 = 0, xpvalid2 = 0;     /* 1=Operand in expanded stg */
U32     xpblk1, xpblk2;                 /* Expanded storage block#   */
int     priv = 0;                       /* 1=Private address space   */
int     prot = 0;                       /* 1=Protected page          */
int     stid;                           /* Segment table indication  */
U32     xaddr;                          /* Address causing exception */
U16     xcode;                          /* Exception code            */
BYTE    akey;                           /* Access key                */

    /* Specification exception if register 0 bits 16-19 are
       not all zero, or if bits 20 and 21 are both ones */
    if ((regs->gpr[0] & 0x0000F000) != 0
        || (regs->gpr[0] & 0x00000C00) == 0x00000C00)
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 3;
    }

    /* If register 0 bits 20 and 21 are both zero, use PSW key */
    if (regs->gpr[0] & 0x00000C00)
    {
        akey = regs->psw.pkey;
    }
    else
    {
        /* Extract the access key from register 0 bits 24-28 */
        akey = regs->gpr[0] & 0x000000F0;

        /* Priviliged operation exception if in problem state, and
           the specified key is not permitted by the PSW key mask */
        if ( regs->psw.prob
            && ((regs->cr[3] << (akey >> 4)) & 0x80000000) == 0 )
        {
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            return 3;
        }
    }

    /* Determine the logical addresses of each operand */
    vaddr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    vaddr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Isolate the page addresses of each operand */
    vaddr1 &= 0x7FFFF000;
    vaddr2 &= 0x7FFFF000;

    /* Translate the second operand address to a real address */
    rc = translate_addr (vaddr2, r2, regs, ACCTYPE_READ,
                        &raddr2, &xcode, &priv, &prot, &stid);

#ifdef FACILITY_EXPANDED_STORAGE
    /* If page invalid, locate the page in expanded storage using the
       real address of the page table entry which is now in raddr2 */
    if (rc == 2)
    {
        xpvalid2 = locate_expanded_page (raddr2, &xpblk2, regs);
    }
#endif /*FACILITY_EXPANDED_STORAGE*/

    /* Program check if second operand is not valid
       in either main storage or expanded storage */
    if (rc != 0 && xpvalid2 == 0)
    {
        xaddr = vaddr2;
        goto mvpg_progck;
    }

    /* Translate the first operand address to a real address */
    rc = translate_addr (vaddr1, r1, regs, ACCTYPE_WRITE,
                        &raddr1, &xcode, &priv, &prot, &stid);

#ifdef FACILITY_EXPANDED_STORAGE
    /* If page invalid, locate the page in expanded storage using the
       real address of the page table entry which is now in raddr1 */
    if (rc == 2)
    {
        xpvalid1 = locate_expanded_page (raddr1, &xpblk1, regs);
    }
#endif /*FACILITY_EXPANDED_STORAGE*/

    /* Program check if operand not valid in main or expanded */
    if (rc != 0 && xpvalid1 == 0)
    {
        xaddr = vaddr1;
        goto mvpg_progck;
    }

    /* Program check if both operands are in expanded storage, or
       if first operand is in expanded storage and the destination
       reference intention (register 0 bit 22) is set to one */
    if ((xpvalid1 && xpvalid2)
        || (xpvalid1 && (regs->gpr[0] & 0x00000200)))
    {
        xaddr = vaddr1;
        xcode = PGM_PAGE_TRANSLATION_EXCEPTION;
        goto mvpg_progck;
    }

    /* Perform addressing and protection checks */
    /*INCOMPLETE*/

    /* Perform page movement */
    if (xpvalid2)
    {
        /* Set the main storage reference and change bits */
        STORAGE_KEY(aaddr1) |= (STORKEY_REF | STORKEY_CHANGE);

        /* Move 4K bytes from expanded storage to main storage */
        memcpy (sysblk.mainstor + aaddr1,
                sysblk.xpndstor + (xpblk2 << 12),
                4096);
    }
    else if (xpvalid1)
    {
        /* Set the main storage reference bit */
        STORAGE_KEY(aaddr2) |= STORKEY_REF;

        /* Move 4K bytes from main storage to expanded storage */
        memcpy (sysblk.xpndstor + (xpblk1 << 12),
                sysblk.mainstor + aaddr2,
                4096);
    }
    else
    {
        /* Set the main storage reference and change bits */
        STORAGE_KEY(aaddr1) |= (STORKEY_REF | STORKEY_CHANGE);
        STORAGE_KEY(aaddr2) |= STORKEY_REF;

        /* Move 4K bytes from main storage to main storage */
        memcpy (sysblk.mainstor + aaddr1,
                sysblk.mainstor + aaddr2,
                4096);
    }

    /* Return condition code zero */
    return 0;

mvpg_progck:
    /* If page translation exception and condition code option
       in register 0 bit 23 is set, return condition code */
    if ((regs->gpr[0] & 0x00000100)
        && xcode == PGM_PAGE_TRANSLATION_EXCEPTION)
        return (xaddr == vaddr2 ? 2 : 1)

    /* Otherwise generate program check */
    program_check (regs, xcode);
    return 3;
} /* end function move_page */
#endif /*INCOMPLETE*/

/*-------------------------------------------------------------------*/
/* Compute Checksum                                                  */
/*                                                                   */
/* Input:                                                            */
/*      r1      First operand register number                        */
/*      r2      Second operand register number                       */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Returns the condition code for the Checksum instruction.     */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int compute_checksum (int r1, int r2, REGS *regs)
{
U32     addr2;                          /* Address of second operand */
U32     len;                            /* Operand length            */
int     i, j;                           /* Loop counters             */
int     cc = 0;                         /* Condition code            */
U32     n;                              /* Word loaded from operand  */
U64     dreg;                           /* Checksum accumulator      */

    /* Specification exception if R2 is odd */
    if (r2 & 1)
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Obtain the second operand address and length from R2, R2+1 */
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);
    len = regs->gpr[r2+1];

    /* Initialize the checksum from the first operand register */
    dreg = regs->gpr[r1];

    /* Process each fullword of second operand */
    for ( i = 0; len > 0 ; i++ )
    {
        /* If 1024 words have been processed, exit with cc=3 */
        if ( i >= 1024 )
        {
            cc = 3;
            break;
        }

        /* Fetch fullword from second operand */
        if (len >= 4)
        {
            n = vfetch4 ( addr2, r2, regs );
            addr2 += 4;
            addr2 &= ADDRESS_MAXWRAP(regs);
            len -= 4;
        }
        else
        {
            /* Fetch final 1, 2, or 3 bytes and pad with zeroes */
            for (j = 0, n = 0; j < 4; j++)
            {
                n <<= 8;
                if (len > 0)
                {
                    n |= vfetchb ( addr2, r2, regs );
                    addr2++;
                    addr2 &= ADDRESS_MAXWRAP(regs);
                    len--;
                }
            } /* end for(j) */
        }

        /* Accumulate the fullword into the checksum */
        dreg += n;

        /* Carry 32 bit overflow into bit 31 */
        if (dreg > 0xFFFFFFFFULL)
        {
            dreg &= 0xFFFFFFFFULL;
            dreg++;
        }
    } /* end for(i) */

    /* Load the updated checksum into the R1 register */
    regs->gpr[r1] = dreg;

    /* Update the operand address and length registers */
    regs->gpr[r2] = addr2;
    regs->gpr[r2+1] = len;

    /* Return with condition code 0 or 3 */
    return cc;

} /* end function compute_checksum */

/*-------------------------------------------------------------------*/
/* Move String                                                       */
/*                                                                   */
/* Input:                                                            */
/*      r1      First operand register number                        */
/*      r2      Second operand register number                       */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Returns the condition code for the MVST instruction.         */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int move_string (int r1, int r2, REGS *regs)
{
int     i;                              /* Loop counter              */
U32     addr1, addr2;                   /* Operand addresses         */
BYTE    sbyte;                          /* String character          */
BYTE    termchar;                       /* Terminating character     */

    /* Program check if bits 0-23 of register 0 not zero */
    if ((regs->gpr[0] & 0xFFFFFF00) != 0)
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Load string terminating character from register 0 bits 24-31 */
    termchar = regs->gpr[0] & 0xFF;

    /* Determine the destination and source addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Move up to 4096 bytes until terminating character */
    for (i = 0; i < 4096; i++)
    {
        /* Fetch a byte from the source operand */
        sbyte = vfetchb ( addr2, r2, regs );

        /* Store the byte in the destination operand */
        vstoreb ( sbyte, addr1, r1, regs );

        /* Check if string terminating character was moved */
        if (sbyte == termchar)
        {
            /* Set r1 to point to terminating character */
            regs->gpr[r1] = addr1;

            /* Return condition code 1 */
            return 1;
        }

        /* Increment operand addresses */
        addr1++;
        addr1 &= ADDRESS_MAXWRAP(regs);
        addr2++;
        addr2 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */

    /* Set R1 and R2 to point to next character of each operand */
    regs->gpr[r1] = addr1;
    regs->gpr[r2] = addr2;

    /* Return condition code 3 */
    return 3;

} /* end function move_string */

/*-------------------------------------------------------------------*/
/* Compare String                                                    */
/*                                                                   */
/* Input:                                                            */
/*      r1      First operand register number                        */
/*      r2      Second operand register number                       */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Returns the condition code for the CLST instruction.         */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int compare_string (int r1, int r2, REGS *regs)
{
int     i;                              /* Loop counter              */
int     cc;                             /* Condition code            */
U32     addr1, addr2;                   /* Operand addresses         */
BYTE    byte1, byte2;                   /* Operand bytes             */
BYTE    termchar;                       /* Terminating character     */

    /* Program check if bits 0-23 of register 0 not zero */
    if ((regs->gpr[0] & 0xFFFFFF00) != 0)
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Load string terminating character from register 0 bits 24-31 */
    termchar = regs->gpr[0] & 0xFF;

    /* Determine the operand addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Initialize the condition code to 3 */
    cc = 3;

    /* Compare up to 4096 bytes until terminating character */
    for (i = 0; i < 4096; i++)
    {
        /* Fetch a byte from each operand */
        byte1 = vfetchb ( addr1, r1, regs );
        byte2 = vfetchb ( addr2, r2, regs );

        /* If both bytes are the terminating character then the
           strings are equal so return condition code 0
           and leave the R1 and R2 registers unchanged */
        if (byte1 == termchar && byte2 == termchar)
            return 0;

        /* If first operand byte is the terminating character,
           or if the first operand byte is lower than the
           second operand byte, then return condition code 1 */
        if (byte1 == termchar || byte1 < byte2)
        {
            cc = 1;
            break;
        }

        /* If second operand byte is the terminating character,
           or if the first operand byte is higher than the
           second operand byte, then return condition code 2 */
        if (byte2 == termchar || byte1 > byte2)
        {
            cc = 2;
            break;
        }

        /* Increment operand addresses */
        addr1++;
        addr1 &= ADDRESS_MAXWRAP(regs);
        addr2++;
        addr2 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */

    /* Set R1 and R2 to point to current character of each operand */
    regs->gpr[r1] = addr1;
    regs->gpr[r2] = addr2;

    /* Return condition code */
    return cc;

} /* end function compare_string */

/*-------------------------------------------------------------------*/
/* Search String                                                     */
/*                                                                   */
/* Input:                                                            */
/*      r1      End of operand register number                       */
/*      r2      Start of operand register number                     */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Returns the condition code for the SRST instruction.         */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int search_string (int r1, int r2, REGS *regs)
{
int     i;                              /* Loop counter              */
U32     addr1, addr2;                   /* End/start addresses       */
BYTE    sbyte;                          /* String character          */
BYTE    termchar;                       /* Terminating character     */

    /* Program check if bits 0-23 of register 0 not zero */
    if ((regs->gpr[0] & 0xFFFFFF00) != 0)
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Load string terminating character from register 0 bits 24-31 */
    termchar = regs->gpr[0] & 0xFF;

    /* Determine the operand end and start addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Search up to 4096 bytes until end of operand */
    for (i = 0; i < 4096; i++)
    {
        /* If operand end address has been reached, return condition
           code 2 and leave the R1 and R2 registers unchanged */
        if (addr2 == addr1)
            return 2;

        /* Fetch a byte from the operand */
        sbyte = vfetchb ( addr2, r2, regs );

        /* If the terminating character was found, return condition
           code 1 and load the address of the character into R1 */
        if (sbyte == termchar)
        {
            regs->gpr[r1] = addr2;
            return 1;
        }

        /* Increment operand address */
        addr2++;
        addr2 &= ADDRESS_MAXWRAP(regs);

    } /* end for(i) */

    /* Set R2 to point to next character of operand */
    regs->gpr[r2] = addr2;

    /* Return condition code 3 */
    return 3;

} /* end function search_string */

/*-------------------------------------------------------------------*/
/* Compare Until Substring Equal                                     */
/*                                                                   */
/* Input:                                                            */
/*      r1      First operand register number                        */
/*      r2      Second operand register number                       */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Returns the condition code for the CUSE instruction.         */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int compare_until_substring_equal (int r1, int r2, REGS *regs)
{
int     i;                              /* Loop counter              */
int     cc = 0;                         /* Condition code            */
U32     addr1, addr2;                   /* Operand addresses         */
S32     len1, len2;                     /* Operand lengths           */
BYTE    byte1, byte2;                   /* Operand bytes             */
BYTE    pad;                            /* Padding byte              */
BYTE    sublen;                         /* Substring length          */
BYTE    equlen = 0;                     /* Equal byte counter        */
U32     eqaddr1, eqaddr2;               /* Address of equal substring*/
S32     remlen1, remlen2;               /* Lengths remaining         */

    /* Program check if either R1 or R2 register is odd */
    if ((r1 & 1) || (r2 & 1))
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Load substring length from bits 24-31 of register 0 */
    sublen = regs->gpr[0] & 0xFF;

    /* Load padding byte from bits 24-31 of register 1 */
    pad = regs->gpr[1] & 0xFF;

    /* Determine the destination and source addresses */
    addr1 = regs->gpr[r1] & ADDRESS_MAXWRAP(regs);
    addr2 = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

    /* Load signed operand lengths from R1+1 and R2+1 */
    len1 = (S32)(regs->gpr[r1+1]);
    len2 = (S32)(regs->gpr[r2+1]);

    /* Initialize equal string addresses and lengths */
    eqaddr1 = addr1;
    eqaddr2 = addr2;
    remlen1 = len1;
    remlen2 = len2;

    /* If substring length is zero, exit with condition code 0 */
    if (sublen == 0)
        return 0;

    /* If both operand lengths are zero, exit with condition code 2 */
    if (len1 == 0 && len2 == 0)
        return 2;

    /* Process operands from left to right */
    for (i = 0; len1 > 0 || len2 > 0 ; i++)
    {
        /* If equal byte count has reached substring length
           exit with condition code zero */
        if (equlen == sublen)
        {
            cc = 0;
            break;
        }

        /* If 4096 bytes have been compared, and the last bytes
           compared were unequal, exit with condition code 3 */
        if (equlen == 0 && i >= 4096)
        {
            cc = 3;
            break;
        }

        /* Fetch byte from first operand, or use padding byte */
        if (len1 > 0)
            byte1 = vfetchb ( addr1, r1, regs );
        else
            byte1 = pad;

        /* Fetch byte from second operand, or use padding byte */
        if (len2 > 0)
            byte2 = vfetchb ( addr2, r2, regs );
        else
            byte2 = pad;

        /* Test if bytes compare equal */
        if (byte1 == byte2)
        {
            /* If this is the first equal byte, save the start of
               substring addresses and remaining lengths */
            if (equlen == 0)
            {
                eqaddr1 = addr1;
                eqaddr2 = addr2;
                remlen1 = len1;
                remlen2 = len2;
            }

            /* Count the number of equal bytes */
            equlen++;

            /* Set condition code 1 */
            cc = 1;
        }
        else
        {
            /* Reset equal byte count and set condition code 2 */
            equlen = 0;
            cc = 2;
        }

        /* Update the first operand address and length */
        if (len1 > 0)
        {
            addr1++;
            addr1 &= ADDRESS_MAXWRAP(regs);
            len1--;
        }

        /* Update the second operand address and length */
        if (len2 > 0)
        {
            addr2++;
            addr2 &= ADDRESS_MAXWRAP(regs);
            len2--;
        }

    } /* end for(i) */

    /* Update the registers */
    if (cc < 2)
    {
        /* Update R1 and R2 to point to the equal substring */
        regs->gpr[r1] = eqaddr1;
        regs->gpr[r2] = eqaddr2;

        /* Set R1+1 and R2+1 to length remaining in each
           operand after the start of the substring */
        regs->gpr[r1+1] = (U32)remlen1;
        regs->gpr[r2+1] = (U32)remlen2;
    }
    else
    {
        /* Update R1 and R2 to point to next bytes to compare */
        regs->gpr[r1] = addr1;
        regs->gpr[r2] = addr2;

        /* Set R1+1 and R2+1 to remaining operand lengths */
        regs->gpr[r1+1] = (U32)len1;
        regs->gpr[r2+1] = (U32)len2;
    }

    /* Return condition code */
    return cc;

} /* end function compare_until_substring_equal */

