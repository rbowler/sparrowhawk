/* XMEM.C       (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Cross Memory Routines                        */

/*-------------------------------------------------------------------*/
/* This module implements the ESA/390 cross-memory instructions.     */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Set Address Space Control                                         */
/*                                                                   */
/* Input:                                                            */
/*      mode    Addressing mode to be set:                           */
/*              0=Primary, 1=Secondary, 2=AR mode, 3=Home            */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      1=Space switch event indicated, 0=No space switch event      */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int set_address_space_control (BYTE mode, REGS *regs)
{
BYTE    oldmode;                        /* Current addressing mode   */

    /* Special operation exception if DAT is off or
       secondary-space control bit is zero */
    if (REAL_MODE(&(regs->psw))
         || (regs->cr[0] & CR0_SEC_SPACE) == 0)
    {
        program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
        return 0;
    }

    /* Privileged operation exception if setting home-space
       mode while in problem state */
    if (mode == 3 && regs->psw.prob)
    {
        program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
        return 0;
    }

    /* Special operation exception if setting AR mode
       and address-space function control bit is zero */
    if (mode == 1 && (regs->cr[0] & CR0_ASF) == 0)
    {
        program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
        return 0;
    }

    /* Specification exception if mode is invalid */
    if (mode > 3)
    {
        program_check (PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Save the current address-space control bits */
    oldmode = (regs->psw.armode << 1) & (regs->psw.space);

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
        return 1;
    }

    return 0;

} /* end function set_address_space_control */

/*-------------------------------------------------------------------*/
/* Set Secondary ASN                                                 */
/*                                                                   */
/* Input:                                                            */
/*      sasn    Secondary address space number                       */
/*      regs    Pointer to the CPU register context                  */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
void set_secondary_asn (U16 sasn, REGS *regs)
{
U32     sstd;                           /* Secondary STD             */
U32     sasteo;                         /* Secondary ASTE origin     */
U32     aste[16];                       /* ASN second table entry    */
U16     xcode;                          /* Exception code            */
U16     ax;                             /* Authorization index       */

    /* Special operation exception if ASN translation control
       (bit 12 of control register 14) is zero or DAT is off */
    if ((regs->cr[14] & CR14_ASN_TRAN) == 0
        || REAL_MODE(&(regs->psw)))
    {
        program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
        return;
    }

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
        {
            program_check (xcode);
            return;
        }

        /* Perform ASN authorization using current AX */
        ax = (regs->cr[4] & CR4_AX) >> 16;
        if (authorize_asn (ax, aste, ATE_SECONDARY, regs))
        {
            program_check (PGM_SECONDARY_AUTHORITY_EXCEPTION);
            return;
        }

        /* Load new secondary STD from ASTE word 2 */
        sstd = aste[2];

    } /* end if(SSAR-ss) */

    /* Load the new secondary ASN into control register 3 */
    regs->cr[3] &= ~CR3_SASN;
    regs->cr[3] |= sasn;

    /* Load the new secondary STD into control register 7 */
    regs->cr[7] = sstd;

#ifdef FEATURE_SUBSPACE_GROUP
    if (regs->cr[0] & CR0_ASF)
    {
The description in this paragraph applies if the  subspace-group
facility is  installed  and  the  address-space-function control, bit
15 of control register 0, is one.   After the  new  SSTD  has  been
placed in  control register  7, if (1) the subspace-group-control bit,
bit 22, in the SSTD is one, (2) the dispatchable unit  is  subspace
active, and (3)  the  ASTE obtained  by  ASN  translation  is  the
ASTE  for  the base space of the dispatchable unit, then bits   1-23
and  25-31  of the SSTD  in  control register  7 are replaced by bits
1-23 and 25-31 of the STD in the ASTE for the subspace in which the
dispatchable unit last had control.    Further details are in
"Subspace-Replacement Operations" in topic 5.9.2.
    } /* end if(CR0_ASF) */
#endif /*FEATURE_SUBSPACE_GROUP*/

    return;

} /* end function set_secondary_asn */

/*-------------------------------------------------------------------*/
/* Load Address Space Parameters                                     */
/*                                                                   */
/* Input:                                                            */
/*      pkm     PSW key mask                                         */
/*      sasn    Secondary ASN                                        */
/*      ax      Authorization index                                  */
/*      pasn    Primary ASN                                          */
/*      func    LASP function bits                                   */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Returns the condition code for the LASP instruction.         */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int load_address_space_parameters (U16 pkm, U16 sasn, U16 ax,
                                U16 pasn, U32 func, REGS *regs)
{
U32     aste[16];                       /* ASN second table entry    */
U32     pstd;                           /* Primary STD               */
U32     sstd;                           /* Secondary STD             */
U32     ltd;                            /* Linkage table descriptor  */
U32     pasteo;                         /* Primary ASTE origin       */
U32     sasteo;                         /* Secondary ASTE origin     */

    /* PASN translation */

    /* Perform PASN translation if PASN not equal to current
       PASN, or if LASP function bit 29 is set */
    if ((func & 0x00000004)
        || pasn != (regs->cr[4] & CR4_PASN))
    {
        /* Translate PASN and return condition code 1 if
           AFX- or ASX-translation exception condition */
        if (translate_asn (pasn, regs, &pasteo, aste))
            return 1;

#ifdef FEATURE_SUBSPACE_GROUP
        if (regs->cr[0] & CR0_ASF)
        {
The  description  in this paragraph applies if the subspace-group
facility is installed, the ASF control is one, and PASN translation is
performed.  After  STD-p has been obtained, if (1) the
subspace-group-control bit, bit 22, in STD-p is one, (2) the
dispatchable unit is subspace active, and (3) PASTEO-p designates the
ASTE for the base space of the dispatchable  unit, then  a copy of
STD-p, called STD-rp, is made, and bits  1-23 and 25-31 of STD-rp are
replaced by bits 1-23 and 25-31 of the STD in the ASTE for  the
subspace in which the dispatchable unit last had control.  Further
details are  in "Subspace-Replacement Operations" in topic 5.9.2.  If
bit 0 in the subspace ASTE is one, or if the  ASTE  sequence  number
(ASTESN)  in  the subspace  ASTE does not equal the subspace ASTESN in
the dispatchable-unit control table, an exception is not recognized;
instead, condition  code  1 is set, and the control registers remain
unchanged.
        } /* end if(CR0_ASF) */
#endif /*FEATURE_SUBSPACE_GROUP*/

        /* Return condition code 3 if either current STD
           or new STD indicates a space switch event */
        if ((regs->cr[1] & STD_SSEVENT)
            || (aste[2] & STD_SSEVENT))
            return 3;

        /* Obtain new PSTD and LTD from ASTE */
        pstd = aste[2];
        ltd = aste[3];

        /* If bit 30 of the LASP function bits is zero,
           use the AX from the primary ASTE instead of the
           AX specified in the first operand */
        if ((func & 0x00000002) == 0)
            ax = (aste[1] & ASTE1_AX) >> 16;
    }
    else
    {
        /* Load current PSTD and LTD or PASTEO */
        pstd = regs->cr[1];
        ltd = regs->cr[5];
        pasteo = regs->cr[5];

        /* If bit 30 of the LASP function bits is zero,
           use the current AX instead of the AX specified
           in the first operand */
        if ((func & 0x00000002) == 0)
            ax = (regs->cr[4] & CR4_AX) >> 16;
    }

    /* SASN translation */

    /* If new SASN = new PASN then set new SSTD = new PSTD */
    if (sasn == pasn)
    {
        sstd = pstd;
    }
    else

    /* If new SASN = current SASN, and bit 29 of the LASP
       function bits is 0, and bit 31 of the LASP function bits
       is 1, use current SSTD in control register 7 */
    if ((func & 0x00000004) == 0
        || (func & 0x00000001)
        || sasn == (regs->cr[3] & CR3_SASN))
    {
        sstd = regs->cr[7];
    }
    else
    {
        /* Translate SASN and return condition code 2 if
           AFX- or ASX-translation exception condition */
        if (translate_asn (sasn, regs, &sasteo, aste))
            return 2;

        /* Obtain new SSTD from secondary ASTE */
        sstd = aste[2];

#ifdef FEATURE_SUBSPACE_GROUP
        if (regs->cr[0] & CR0_ASF)
        {
The  description  in this paragraph applies if the subspace-group
facility is installed, the ASF control is one, and SASN translation is
performed.  After  STD-s has been obtained, if (1) the
subspace-group-control bit, bit 22, in STD-s is one, (2) the
dispatchable unit is subspace active, and (3) SASTEO-s designates the
ASTE for the base space of the dispatchable  unit, then  a copy of
STD-s, called STD-rs, is made, and bits  1-23 and 25-31 of STD-rs are
replaced by bits 1-23 and 25-31 of the STD in the ASTE for  the
subspace in which the dispatchable unit last had control.  Further
details are  in "Subspace-Replacement Operations" in topic 5.9.2.  If
bit 0 in the subspace ASTE is one, or if the  ASTE  sequence  number
(ASTESN)  in  the subspace  ASTE does not equal the subspace ASTESN in
the dispatchable-unit control table, an exception is not recognized;
instead, condition  code  2 is set, and the control registers remain
unchanged.
        } /* end if(CR0_ASF) */
#endif /*FEATURE_SUBSPACE_GROUP*/

        /* Perform SASN authorization if bit 31 of the
           LASP function bits is not 0 */
        if (func & 0x00000001)
        {
            /* Condition code 2 if SASN authorization fails */
            if (authorize_asn (ax, aste, ATE_SECONDARY, regs))
                return 2;

        } /* end if(SASN authorization) */

    } /* end if(SASN translation) */

    /* Perform control-register loading */
    regs->cr[1] = pstd;
    regs->cr[3] = (pkm << 16) | sasn;
    regs->cr[4] = (ax << 16) | pasn;
    regs->cr[5] = (regs->cr[0] & CR0_ASF) ? pasteo : ltd;
    regs->cr[7] = sstd;

    /* Return condition code zero */
    return 0;

} /* end function load_address_space_parameters */

/*-------------------------------------------------------------------*/
/* Program Transfer                                                  */
/*                                                                   */
/* Input:                                                            */
/*      pkm     PSW key mask                                         */
/*      pasn    Primary address space number                         */
/*      amode   Addressing mode (1=31, 0=24)                         */
/*      ia      Instruction address                                  */
/*      prob    Problem state (1=Problem state, 0=Supervisor state)  */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      1=Space switch event indicated, 0=No space switch event      */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int program_transfer (U16 pkm, U16 pasn, int amode, U32 ia, int prob,
                        REGS *regs)
{
U32     ltd;                            /* Linkage table descriptor  */
U32     pasteo;                         /* Primary ASTE origin       */
U32     aste[16];                       /* ASN second table entry    */
U32     pstd;                           /* Primary STD               */
U16     ax;                             /* Authorization index       */
U16     xcode;                          /* Exception code            */
int     ssevent;                        /* 1=space switch event      */

    /* Special operation exception if DAT is off, or
       not in primary space mode */
    if (REAL_MODE(&(regs->psw))
        || !PRIMARY_SPACE_MODE(&(regs->psw)))
    {
        program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
        return 0;
    }

    /* Load the linkage table designation */
    if ((regs->cr[0] & CR0_ASF) == 0)
    {
        /* Obtain the LTD from control register 5 */
        ltd = regs->cr[5];
    }
    else
    {
        /* Obtain the primary ASTE origin from control reg 5 */
        pasteo = regs->cr[5] & CR5_PASTEO;
        pasteo = APPLY_PREFIXING (pasteo, regs->pxr);

        /* Program check if PASTE is outside main storage */
        if (pasteo >= sysblk.mainsize)
        {
            program_check (PGM_ADDRESSING_EXCEPTION);
            return 0;
        }

        /* Fetch LTD from PASTE word 3 */
        ltd = sysblk.mainstor[pasteo+12] << 24
            | sysblk.mainstor[pasteo+13] << 16
            | sysblk.mainstor[pasteo+14] << 8
            | sysblk.mainstor[pasteo+15];
    }

    /* Special operation exception if subsystem linkage
       control bit in linkage table designation is zero */
    if ((ltd & LTD_SSLINK) == 0)
    {
        program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
        return 0;
    }

    /* Privileged operation exception if in problem state and
       problem bit indicates a change to supervisor state */
    if (regs->psw.prob && prob == 0)
    {
        program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
        return 0;
    }

    /* Specification exception if amode is zero and
       new instruction address is not a 24-bit address */
    if (amode == 0 && ia > 0x00FFFFFF)
    {
        program_check (PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Space switch if ASN not equal to current PASN */
    if (pasn != (regs->cr[4] & CR4_PASN))
    {
        /* Special operation exception if ASN translation
           control (control register 14 bit 12) is zero */
        if ((regs->cr[14] & CR14_ASN_TRAN) == 0)
        {
            program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
            return 0;
        }

        /* Translate ASN and generate program check if
           AFX- or ASX-translation exception condition */
        xcode = translate_asn (pasn, regs, &pasteo, aste);
        if (xcode != 0)
        {
            program_check (xcode);
            return 0;
        }

        /* Perform primary address space authorization
           using current authorization index */
        ax = (regs->cr[4] & CR4_AX) >> 16;
        if (authorize_asn (ax, aste, ATE_PRIMARY, regs))
        {
            program_check (PGM_PRIMARY_AUTHORITY_EXCEPTION);
            return 0;
        }

#ifdef FEATURE_SUBSPACE_GROUP
        if (regs->cr[0] & CR0_ASF)
        {
The description in this paragraph applies if the  subspace-group
facility is  installed  and  the  ASF control is one.   After the new
PSTD has been placed in control register 1 and the  new  primary-ASTE
origin  has  been placed  in  control register 5, if (1) the
subspace-group-control bit, bit 22, in the PSTD is one, (2) the
dispatchable unit is subspace active,  and (3)  the primary-ASTE
origin designates the ASTE for the base space of the dispatchable
unit, then bits   1-23 and  25-31  of  the  PSTD  in  control register
1 are replaced by bits 1-23 and 25-31 of the STD in the ASTE for the
subspace in which the  dispatchable  unit  last  had  control.    This
replacement  occurs before a replacement of the SSTD in control
register 7 by the PSTD.  Further details are in "Subspace-Replacement
Operations"  in topic 5.9.2.
        }
#endif /*FEATURE_SUBSPACE_GROUP*/

        /* Obtain new primary STD from the ASTE */
        pstd = aste[2];

        /* Set flag if either current PSTD or new PSTD
           space switch event bit is set to 1 */
        ssevent = (regs->cr[1] & STD_SSEVENT) || (pstd & STD_SSEVENT);

        /* Load new primary STD into control register 1 */
        regs->cr[1] = pstd;

        /* Load new AX and PASN into control register 4 */
        regs->cr[4] = (aste[1] & ASTE1_AX) | pasn;

        /* Load new LTD or PASTEO into control register 5 */
        regs->cr[5] = ((regs->cr[0] & CR0_ASF) == 0) ?
                                aste[3] : pasteo;

    } /* end if(PT-ss) */
    else
    {
        /* For PT-cp use current primary STD */
        pstd = regs->cr[1];

        /* Clear space switch event flag */
        ssevent = 0;
    }

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

    /* Return the space switch event flag */
    return ssevent;

} /* end function program_transfer */

/*-------------------------------------------------------------------*/
/* Program Return                                                    */
/*                                                                   */
/* Input:                                                            */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      1=Space switch event indicated, 0=No space switch event      */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int program_return (REGS *regs)
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

        /* Perform PASN translation if new PASN not equal old PASN */
        if (pasn != oldpasn)
        {
            /* Special operation exception if ASN translation
               control (control register 14 bit 12) is zero */
            if ((regs->cr[14] & CR14_ASN_TRAN) == 0)
            {
                program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
                return 0;
            }

            /* Translate new primary ASN to obtain ASTE */
            xcode = translate_asn (pasn, &newregs, &pasteo, aste);

            /* Program check if ASN translation exception */
            if (xcode != 0)
            {
                program_check (xcode);
                return 0;
            }

            /* Set flag if either the current PSTD or the
               new PSTD indicates a space switch event */
            ssevent = (regs->cr[1] & STD_SSEVENT)
                        || (aste[2] & STD_SSEVENT);

            /* Obtain new PSTD and AX from the ASTE */
            newregs.cr[1] = aste[2];
            newregs.cr[4] &= ~CR4_AX;
            newregs.cr[4] |= aste[1] & ASTE1_AX;

            /* Load CR5 with the primary ASTE origin address */
            newregs.cr[5] = pasteo;

#ifdef FEATURE_SUBSPACE_GROUP
            if (regs->cr[0] & CR0_ASF)
            {
The description in this paragraph applies if the  subspace-group
facility is installed   and   PASN   translation  has  occurred.  If
(1)  the subspace-group-control bit, bit 22, in  the  new  PSTD  is
one,  (2)  the dispatchable  unit is subspace active, and (3) the new
primary-ASTE origin designates the ASTE for the base space of the
dispatchable unit, then bits 1-23 and 25-31 of the new PSTD in control
register 1 are replaced by bits 1-23  and 25-31  of  the  STD  in  the
ASTE for the subspace in which the dispatchable unit last had control.
This replacement occurs, in the case when  the  new SASN is equal to
the new PASN, before the SSTD is set equal to the PSTD.  Further
details are in "Subspace-Replacement Operations"  in topic 5.9.2.
            } /* end if(CR0_ASF) */
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
            {
                program_check (PGM_SPECIAL_OPERATION_EXCEPTION);
                return 0;
            }

            /* Translate new secondary ASN to obtain ASTE */
            xcode = translate_asn (sasn, &newregs, &sasteo, aste);

            /* Program check if ASN translation exception */
            if (xcode != 0)
            {
                program_check (xcode);
                return 0;
            }

            /* Obtain new SSTD from secondary ASTE */
            newregs.cr[7] = aste[2];

            /* Perform SASN authorization using new AX */
            ax = (newregs.cr[4] & CR4_AX) >> 16;
            if (authorize_asn (ax, aste, ATE_SECONDARY, &newregs))
            {
                program_check (PGM_SECONDARY_AUTHORITY_EXCEPTION);
                return 0;
            }

#ifdef FEATURE_SUBSPACE_GROUP
            if (regs->cr[0] & CR0_ASF)
            {
The description in this paragraph applies if the  subspace-group
facility is installed and SASN translation and authorization have
occurred.  If (1) the  subspace-group-control  bit,  bit 22, in the
new SSTD is one, (2) the dispatchable unit is subspace active, and (3)
the ASTE origin obtained by SASN   translation   designates  the  ASTE
for  the  base  space  of  the dispatchable unit, then bits  1-23 and
25-31 of the new  SSTD  in control register  7 are replaced by bits
1-23 and 25-31 of the STD in the ASTE for the subspace in which the
dispatchable unit last  had  control.  Further details are in
"Subspace-Replacement Operations" in topic 5.9.2.
            } /* end if(CR0_ASF) */
#endif /*FEATURE_SUBSPACE_GROUP*/

        } /* end else(sasn!=pasn) */

    } /* end if(LSED_UET_PC) */

    /* Update the current CPU registers from the working copy */
    *regs = newregs;

    /* [5.12.4.4] Clear the next entry size field of the linkage
       stack entry now pointed to by control register 15 */
    lsedp = (LSED*)(sysblk.mainstor + alsed);
    lsedp->nes[0] = 0;
    lsedp->nes[1] = 0;

    /* Return the space switch event flag */
    return ssevent;

} /* end function program_return */

