/* SORT.C       Written by Peter Kuschnerus, 1999                    */
/*              ESA/390 Sorting Instructions                         */

/*-------------------------------------------------------------------*/
/* This module implements the ESA/390 Compare and Form Codeword      */
/* and Update Tree instructions described in the manual              */
/* SA22-7201-04 ESA/390 Principles of Operation.                     */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Compare and Form Codeword                                         */
/*                                                                   */
/* Input:                                                            */
/*      regs    Pointer to a copy of the CPU register context        */
/*      eaddr   Effective address of the CFC instruction             */
/* Return value:                                                     */
/*      Returns the condition code for the CFC instruction.          */
/*-------------------------------------------------------------------*/
int compare_and_form_codeword (REGS *regs, U32 eaddr)
{
U32     n, n1, n2;                      /* 32-bit operand values     */
U16     h1, h2, h3;                     /* 16-bit operand values     */
BYTE    obyte;                          /* Operand control bit       */
int     cc;                             /* Condition code            */
int     ar1 = 1;                        /* Access register number    */
U32     addr1, addr3;                   /* Operand addresses         */

    if ( regs->gpr[1] & 1 || regs->gpr[2] & 1 || regs->gpr[3] & 1 )
    {
        program_check (PGM_SPECIFICATION_EXCEPTION);
        return 3;
    }

    n1 = eaddr & 0x00007FFE;
    obyte = eaddr & 1;

    for (;;)
    {
        n2 = regs->gpr[2] & 0x0000FFFF;
        if ( n2 > n1 )
        {
            regs->gpr[2] = regs->gpr[3];
            regs->gpr[2] |= 0x80000000;
            return 0;
        }

        regs->gpr[2] += 2;

        addr1 = (regs->gpr[1] + n2) &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
        addr3 = (regs->gpr[3] + n2) &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        h1 = vfetch2 ( addr1, ar1, regs );
        h3 = vfetch2 ( addr3, ar1, regs );

        if ( h1 > h3 )
        {
            if ( obyte )
            {
                h2 = h3;
                cc = 1;
            }
            else
            {
                h2 = ~h1;
                n = regs->gpr[1];
                regs->gpr[1] = regs->gpr[3];
                regs->gpr[3] = n;
                cc = 2;
            }

            /* end of loop */
            break;
        }
        else if ( h1 < h3 )
        {
            if ( obyte )
            {
                h2 = h1;
                n = regs->gpr[1];
                regs->gpr[1] = regs->gpr[3];
                regs->gpr[3] = n;
                cc = 2;
            }
            else
            {
                h2 = ~h3;
                cc = 1;
            }

            /* end of loop */
            break;
        }
        /* if equal continue */
    }

    regs->gpr[2] <<= 16;
    regs->gpr[2] |= h2;

    /* end of instruction */
    return cc;

} /* end function compare_and_form_codeword */

/*-------------------------------------------------------------------*/
/* Update Tree                                                       */
/*                                                                   */
/* Input:                                                            */
/*      regs    Pointer to a copy of the CPU register context        */
/* Return value:                                                     */
/*      Returns the condition code for the UPT instruction.          */
/*-------------------------------------------------------------------*/
int update_tree (REGS *regs)
{
U32     d, h, i, j;                     /* 32-bit operand values     */
int     cc;                             /* Condition code            */
int     ar1 = 4;                        /* Access register number    */

    if ( regs->gpr[4] & 0x00000007 || regs->gpr[5] & 0x00000007 )
    {
        program_check (PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    for (;;)
    {
        d = (regs->gpr[5] >> 1) & 0xFFFFFFF8;
        if ( d == 0 )
        {
            regs->gpr[5] = 0;
            cc = 1;
            break;
        }

        regs->gpr[5] = d;
        if ( ((U32) regs->gpr[0]) < 0 )
        {
            cc = 3;
            break;
        }

        j = regs->gpr[4] + d;
        j &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
        h = vfetch4 ( j, ar1, regs );
        i = vfetch4 ( j + 4, ar1, regs );
        if ( regs->gpr[0] == h )
        {
            regs->gpr[2] = h;
            regs->gpr[3] = i;
            cc = 0;
            break;
        }

        if ( regs->gpr[0] < h )
        {
            vstore4 ( regs->gpr[0], j, ar1, regs );
            vstore4 ( regs->gpr[1], j + 4, ar1, regs );
            regs->gpr[0] = h;
            regs->gpr[1] = i;
        }
    }

    return cc;

} /* end function update_tree */

