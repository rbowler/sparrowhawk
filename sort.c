/* SORT.C       (c) Copyright Peter Kuschnerus, 1999                 */
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
int     cc;                             /* Condition code            */
int     ar1 = 1;                        /* Access register number    */
U32     addr1, addr3;                   /* Operand addresses         */
U32     work;                           /* 32-bit workarea           */
U16     index_limit;                    /* Index limit               */
U16     index;                          /* Index                     */
U16     temp_hw;                        /* TEMPHW                    */
U16     op1, op3;                       /* 16-bit operand values     */
BYTE    operand_control;                /* Operand control bit       */

    /* Check GR1, GR2, GR3 even */
    if ( regs->gpr[1] & 1 || regs->gpr[2] & 1 || regs->gpr[3] & 1 )
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 3;
    }

    /* Get index limit and operand-control bit */
    index_limit = eaddr & 0x7FFE;
    operand_control = eaddr & 1;

    /* Loop until break */
    for (;;)
    {
        /* Check index limit */
        index = regs->gpr[2];
        if ( index > index_limit )
        {
            regs->gpr[2] = regs->gpr[3] | 0x80000000;
            return 0;
        }

        /* fetch 1st operand */
        addr1 = (regs->gpr[1] + index) & ADDRESS_MAXWRAP(regs);
        op1 = vfetch2 ( addr1, ar1, regs );

        /* fetch 3rd operand */
        addr3 = (regs->gpr[3] + index) & ADDRESS_MAXWRAP(regs);
        op3 = vfetch2 ( addr3, ar1, regs );

        regs->gpr[2] += 2;

        /* Compare oprerands */
        if ( op1 > op3 )
        {
            if ( operand_control )
            {
                temp_hw = op3;
                cc = 1;
            }
            else
            {
                temp_hw = ~op1;

                /* Exchange GR1 and GR3 */
                work = regs->gpr[1];
                regs->gpr[1] = regs->gpr[3];
                regs->gpr[3] = work;

                cc = 2;
            }

            /* end of loop */
            break;
        }
        else if ( op1 < op3 )
        {
            if ( operand_control )
            {
                temp_hw = op1;

                /* Exchange GR1 and GR3 */
                work = regs->gpr[1];
                regs->gpr[1] = regs->gpr[3];
                regs->gpr[3] = work;

                cc = 2;
            }
            else
            {
                temp_hw = ~op3;
                cc = 1;
            }

            /* end of loop */
            break;
        }
        /* if equal continue */
    }

    regs->gpr[2] = (regs->gpr[2] << 16) | temp_hw;

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
U32     tempword1;                      /* TEMPWORD1                 */
U32     tempword2;                      /* TEMPWORD2                 */
U32     tempword3;                      /* TEMPWORD3                 */
U32     tempaddress;                    /* TEMPADDRESS               */
int     cc;                             /* Condition code            */
int     ar1 = 4;                        /* Access register number    */

    /* Check GR4, GR5 doubleword alligned */
    if ( regs->gpr[4] & 0x00000007 || regs->gpr[5] & 0x00000007 )
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Loop until break */
    for (;;)
    {
        tempword1 = (regs->gpr[5] >> 1) & 0xFFFFFFF8;
        if ( tempword1 == 0 )
        {
            regs->gpr[5] = 0;
            cc = 1;
            break;
        }

        regs->gpr[5] = tempword1;

        /* Check bit 0 of GR0 */
        if ( ((U32) regs->gpr[0]) < 0 )
        {
            cc = 3;
            break;
        }

        tempaddress = regs->gpr[4] + tempword1;

        /* Fetch doubleword from tempaddress to tempword2 and tempword3 */
        tempaddress &= ADDRESS_MAXWRAP(regs);
        tempword2 = vfetch4 ( tempaddress, ar1, regs );
        tempword3 = vfetch4 ( tempaddress + 4, ar1, regs );

        if ( regs->gpr[0] == tempword2 )
        {
            /* Load GR2 and GR3 from tempword2 and tempword3 */
            regs->gpr[2] = tempword2;
            regs->gpr[3] = tempword3;

            cc = 0;
            break;
        }

        if ( regs->gpr[0] < tempword2 )
        {
            /* Store doubleword from GR0 and GR1 to tempaddress */
            vstore4 ( regs->gpr[0], tempaddress, ar1, regs );
            vstore4 ( regs->gpr[1], tempaddress + 4, ar1, regs );

            /* Load GR0 and GR1 from tempword2 and tempword3 */
            regs->gpr[0] = tempword2;
            regs->gpr[1] = tempword3;
        }
    }

    return cc;

} /* end function update_tree */

