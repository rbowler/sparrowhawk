/* XMEM.C       (c) Copyright Roger Bowler, 1999-2000                */
/*              ESA/390 Cross Memory Routines                        */

/*-------------------------------------------------------------------*/
/* This module implements the cross-memory instructions of the       */
/* ESA/390 architecture, described in the manual SA22-7201-04.       */
/* The numbers in square brackets refer to sections in the manual.   */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      Correction to LASP instruction by Jan Jaeger                 */
/*      Implicit tracing by Jan Jaeger                               */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Fetch a fullword from absolute storage.                           */
/* All bytes of the word are fetched concurrently as observed by     */
/* other CPUs.  The fullword is first fetched as an integer, then    */
/* the bytes are reversed into host byte order if necessary.         */
/*-------------------------------------------------------------------*/
static inline U32 fetch_fullword_absolute (U32 addr)
{
U32     i;

    /* Set the main storage reference bit */
    STORAGE_KEY(addr) |= STORKEY_REF;

    /* Fetch the fullword from absolute storage */
    i = *((U32*)(sysblk.mainstor + addr));
    return ntohl(i);
} /* end function fetch_fullword_absolute */

/*-------------------------------------------------------------------*/
/* Store a fullword into absolute storage.                           */
/* All bytes of the word are stored concurrently as observed by      */
/* other CPUs.  The bytes of the word are reversed if necessary      */
/* and the word is then stored as an integer in absolute storage.    */
/*-------------------------------------------------------------------*/
static inline void store_fullword_absolute (U32 value, U32 addr)
{
U32     i;

    /* Set the main storage reference and change bits */
    STORAGE_KEY(addr) |= (STORKEY_REF | STORKEY_CHANGE);

    /* Store the fullword into absolute storage */
    i = htonl(value);
    *((U32*)(sysblk.mainstor + addr)) = i;
} /* end function store_fullword_absolute */

#ifdef FEATURE_SUBSPACE_GROUP
/*-------------------------------------------------------------------*/
/* Perform subspace replacement                                      */
/*                                                                   */
/* Input:                                                            */
/*      std     Original segment table designation (STD)             */
/*      asteo   ASTE origin obtained by ASN translation              */
/*      xcode   Pointer to field to receive exception code, or NULL  */
/*      regs    Pointer to the CPU register context                  */
/* Output:                                                           */
/*      xcode   Exception code or zero (if xcode is not NULL)        */
/* Return value:                                                     */
/*      On successful completion, the exception code field (if not   */
/*      NULL) is set to zero, and the function return value is the   */
/*      STD resulting from subspace replacement, or is the original  */
/*      STD if subspace replacement is not applicable.               */
/* Operation:                                                        */
/*      If the ASF control (CR0 bit 15) is one, and the STD is a     */
/*      member of a subspace-group (STD bit 22 is one), and the      */
/*      dispatchable unit is subspace active (DUCT word 1 bit 0 is   */
/*      one), and the ASTE obtained by ASN translation is the ASTE   */
/*      for the base space of the dispatchable unit, then bits 1-23  */
/*      and 25-31 of the STD are replaced by bits 1-23 and 25-31 of  */
/*      the STD in the ASTE for the subspace in which the            */
/*      dispatchable unit last had control; otherwise the STD        */
/*      remains unchanged.                                           */
/* Error conditions:                                                 */
/*      If an ASTE validity exception or ASTE sequence exception     */
/*      occurs, and the xcode parameter is a non-NULL pointer,       */
/*      then the exception code is returned in the xcode field       */
/*      and the function return value is zero.                       */
/*      For all other error conditions a program check is generated  */
/*      and the function does not return.                            */
/*-------------------------------------------------------------------*/
U32 subspace_replace (U32 std, U32 asteo, U16 *xcode, REGS *regs)
{
U32     ducto;                          /* DUCT origin               */
U32     duct0;                          /* DUCT word 0               */
U32     duct1;                          /* DUCT word 1               */
U32     duct3;                          /* DUCT word 3               */
U32     ssasteo;                        /* Subspace ASTE origin      */
U32     ssaste0;                        /* Subspace ASTE word 0      */
U32     ssaste2;                        /* Subspace ASTE word 2      */
U32     ssaste5;                        /* Subspace ASTE word 5      */

    /* Clear the exception code field, if provided */
    if (xcode != NULL) *xcode = 0;

    /* Return the original STD unchanged if the address-space function
       control (CR0 bit 15) is zero, or if the subspace-group control
       (bit 22 of the STD) is zero */
    if ((regs->cr[0] & CR0_ASF) == 0
        || (std & STD_GROUP) == 0)
        return std;

    /* Load the DUCT origin address */
    ducto = regs->cr[2] & CR2_DUCTO;
    ducto = APPLY_PREFIXING (ducto, regs->pxr);

    /* Program check if DUCT origin address is invalid */
    if (ducto >= sysblk.mainsize)
    {
        program_check (regs, PGM_ADDRESSING_EXCEPTION);
        return 0;
    }

    /* Fetch DUCT words 0, 1, and 3 from absolute storage
       (note: the DUCT cannot cross a page boundary) */
    duct0 = fetch_fullword_absolute (ducto);
    duct1 = fetch_fullword_absolute (ducto+4);
    duct3 = fetch_fullword_absolute (ducto+12);

    /* Return the original STD unchanged if the dispatchable unit is
       not subspace active or if the ASTE obtained by ASN translation
       is not the same as the base ASTE for the dispatchable unit */
    if ((duct1 & DUCT1_SA) == 0
        || asteo != (duct0 & DUCT0_BASTEO))
        return std;

    /* Load the subspace ASTE origin from the DUCT */
    ssasteo = duct1 & DUCT1_SSASTEO;
    ssasteo = APPLY_PREFIXING (ssasteo, regs->pxr);

    /* Program check if ASTE origin address is invalid */
    if (ssasteo >= sysblk.mainsize)
    {
        program_check (regs, PGM_ADDRESSING_EXCEPTION);
        return 0;
    }

    /* Fetch subspace ASTE words 0, 2, and 5 from absolute storage
       (note: the ASTE cannot cross a page boundary) */
    ssaste0 = fetch_fullword_absolute (ssasteo);
    ssaste2 = fetch_fullword_absolute (ssasteo+8);
    ssaste5 = fetch_fullword_absolute (ssasteo+20);

    /* ASTE validity exception if subspace ASTE invalid bit is one */
    if (ssaste0 & ASTE0_INVALID)
    {
        if (xcode == NULL)
            program_check (regs, PGM_ASTE_VALIDITY_EXCEPTION);
        else
            *xcode = PGM_ASTE_VALIDITY_EXCEPTION;
        return 0;
    }

    /* ASTE sequence exception if the subspace ASTE sequence
       number does not match the sequence number in the DUCT */
    if ((ssaste5 & ASTE5_ASTESN) != (duct3 & DUCT3_SSASTESN))
    {
        if (xcode == NULL)
            program_check (regs, PGM_ASTE_SEQUENCE_EXCEPTION);
        else
            *xcode = PGM_ASTE_SEQUENCE_EXCEPTION;
        return 0;
    }

    /* Replace bits 1-23 and 25-31 of the STD with the
       corresponding bits from the subspace ASTE STD */
    std &= (STD_SSEVENT | STD_SAEVENT);
    std |= (ssaste2 & ~(STD_SSEVENT | STD_SAEVENT));

    /* Return the STD resulting from subspace replacement */
    return std;

} /* end function subspace_replace */
#endif /*FEATURE_SUBSPACE_GROUP*/

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
int     ssevent = 0;                    /* 1=space switch event      */

    /* Special operation exception if DAT is off or
       secondary-space control bit is zero */
    if (REAL_MODE(&(regs->psw))
         || (regs->cr[0] & CR0_SEC_SPACE) == 0)
    {
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
        return 0;
    }

    /* Privileged operation exception if setting home-space
       mode while in problem state */
    if (mode == 3 && regs->psw.prob)
    {
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
        return 0;
    }

    /* Special operation exception if setting AR mode
       and address-space function control bit is zero */
    if (mode == 1 && (regs->cr[0] & CR0_ASF) == 0)
    {
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
        return 0;
    }

    /* Specification exception if mode is invalid */
    if (mode > 3)
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

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

    /* Return the space switch event flag */
    return ssevent;

} /* end function set_address_space_control */

/*-------------------------------------------------------------------*/
/* Insert Address Space Control                                      */
/*                                                                   */
/* Input:                                                            */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      Returns the addressing mode from the current PSW:            */
/*      0=Primary, 1=Secondary, 2=AR mode, 3=Home                    */
/*      The IAC instruction uses this value as its condition code    */
/*      and also as the value inserted into register bits 16-23.     */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int insert_address_space_control (REGS *regs)
{
int     mode;                           /* Current addressing mode   */

    /* Special operation exception if DAT is off */
    if (REAL_MODE(&(regs->psw)))
    {
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
        return 0;
    }

    /* Privileged operation exception if in problem state
       and the extraction-authority control bit is zero */
    if ( regs->psw.prob
         && (regs->cr[0] & CR0_EXT_AUTH) == 0 )
    {
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
        return 0;
    }

    /* Extract the address-space control bits from the PSW */
    mode = (regs->psw.armode << 1) | (regs->psw.space);

    /* Return the current addressing mode */
    return mode;

} /* end function insert_address_space_control */

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
#ifdef FEATURE_TRACING
U32     newcr12 = 0;                    /* CR12 upon completion      */
#endif /*FEATURE_TRACING*/

    /* Special operation exception if ASN translation control
       (bit 12 of control register 14) is zero or DAT is off */
    if ((regs->cr[14] & CR14_ASN_TRAN) == 0
        || REAL_MODE(&(regs->psw)))
    {
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
        return;
    }

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
        {
            program_check (regs, xcode);
            return;
        }

        /* Perform ASN authorization using current AX */
        ax = (regs->cr[4] & CR4_AX) >> 16;
        if (authorize_asn (ax, aste, ATE_SECONDARY, regs))
        {
            regs->tea = sasn;
            program_check (regs, PGM_SECONDARY_AUTHORITY_EXCEPTION);
            return;
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
int load_address_space_parameters (U16 pkm_d, U16 sasn_d, U16 ax_d,
                                U16 pasn_d, U32 func, REGS *regs)
{
U32     aste[16];                       /* ASN second table entry    */
U32     pstd;                           /* Primary STD               */
U32     sstd;                           /* Secondary STD             */
U32     ltd;                            /* Linkage table descriptor  */
U32     ax;                             /* Authorisation index       */
U32     pasteo;                         /* Primary ASTE origin       */
U32     sasteo;                         /* Secondary ASTE origin     */
#ifdef FEATURE_SUBSPACE_GROUP
U16     xcode;                          /* Exception code            */
#endif /*FEATURE_SUBSPACE_GROUP*/

    /* PASN translation */

    /* Perform PASN translation if PASN not equal to current
       PASN, or if LASP function bit 29 is set */
    if ((func & 0x00000004)
        || pasn_d != (regs->cr[4] & CR4_PASN))
    {
        /* Translate PASN and return condition code 1 if
           AFX- or ASX-translation exception condition */
        if (translate_asn (pasn_d, regs, &pasteo, aste))
            return 1;

        /* Obtain new PSTD and LTD from ASTE */
        pstd = aste[2];
        ltd = aste[3];
        ax = (aste[1] & ASTE1_AX) >> 16;

#ifdef FEATURE_SUBSPACE_GROUP
        /* Perform subspace replacement on new PSTD */
        pstd = subspace_replace (pstd, pasteo, &xcode, regs);

        /* Return condition code 1 if ASTE exception was recognized */
        if (xcode != 0)
            return 1;
#endif /*FEATURE_SUBSPACE_GROUP*/

        /* Return condition code 3 if either current STD
           or new STD indicates a space switch event */
        if ((regs->cr[1] & STD_SSEVENT)
            || (aste[2] & STD_SSEVENT))
            return 3;

    }
    else
    {
        /* Load current PSTD and LTD or PASTEO */
        pstd = regs->cr[1];
        ltd = regs->cr[5];
        pasteo = regs->cr[5];
        ax = (regs->cr[4] & CR4_AX) >> 16;
    }

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
        if (!(func & 0x00000004)
            && (func & 0x00000001)
            && (sasn_d == (regs->cr[3] & CR3_SASN)))
        {
            sstd = regs->cr[7];
        }
        else
        {
            /* Translate SASN and return condition code 2 if
               AFX- or ASX-translation exception condition */
            if (translate_asn (sasn_d, regs, &sasteo, aste))
                return 2;

            /* Obtain new SSTD from secondary ASTE */
            sstd = aste[2];

#ifdef FEATURE_SUBSPACE_GROUP
            /* Perform subspace replacement on new SSTD */
            sstd = subspace_replace (sstd, sasteo, &xcode, regs);

            /* Return condition code 2 if ASTE exception was recognized */
            if (xcode != 0)
                return 2;
#endif /*FEATURE_SUBSPACE_GROUP*/

            /* Perform SASN authorization if bit 31 of the
               LASP function bits is 0 */
            if (!(func & 0x00000001))
            {
                /* Condition code 2 if SASN authorization fails */
                if (authorize_asn (ax, aste, ATE_SECONDARY, regs))
                    return 2;

            } /* end if(SASN authorization) */

        } /* end if(SASN translation) */

    } /* end if(SASN = PASN) */

    /* If bit 30 of the LASP function bits is zero,
       use the current AX instead of the AX specified
       in the first operand */
    if ((func & 0x00000002))
        ax = ax_d;

    /* Perform control-register loading */
    regs->cr[1] = pstd;
    regs->cr[3] = (pkm_d << 16) | sasn_d;
    regs->cr[4] = (ax << 16) | pasn_d;
    regs->cr[5] = (regs->cr[0] & CR0_ASF) ? pasteo : ltd;
    regs->cr[7] = sstd;

    /* Return condition code zero */
    return 0;

} /* end function load_address_space_parameters */

/*-------------------------------------------------------------------*/
/* Program Transfer                                                  */
/*                                                                   */
/* Input:                                                            */
/*      r1      First operand register number                        */
/*      r2      Second operand register number                       */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      1=Space switch event indicated, 0=No space switch event      */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int program_transfer (int r1, int r2, REGS *regs)
{
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

    /* Special operation exception if DAT is off, or
       not in primary space mode */
    if (REAL_MODE(&(regs->psw))
        || !PRIMARY_SPACE_MODE(&(regs->psw)))
    {
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
        return 0;
    }

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
        {
            program_check (regs, PGM_ADDRESSING_EXCEPTION);
            return 0;
        }

        /* Fetch LTD from PASTE word 3 */
        ltd = fetch_fullword_absolute(pasteo+12);
    }

    /* Special operation exception if subsystem linkage
       control bit in linkage table designation is zero */
    if ((ltd & LTD_SSLINK) == 0)
    {
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
        return 0;
    }

    /* Privileged operation exception if in problem state and
       problem bit indicates a change to supervisor state */
    if (regs->psw.prob && prob == 0)
    {
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
        return 0;
    }

    /* Specification exception if amode is zero and
       new instruction address is not a 24-bit address */
    if (amode == 0 && ia > 0x00FFFFFF)
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Space switch if ASN not equal to current PASN */
    if (pasn != (regs->cr[4] & CR4_PASN))
    {
        /* Special operation exception if ASN translation
           control (control register 14 bit 12) is zero */
        if ((regs->cr[14] & CR14_ASN_TRAN) == 0)
        {
            program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
            return 0;
        }

        /* Translate ASN and generate program check if
           AFX- or ASX-translation exception condition */
        xcode = translate_asn (pasn, regs, &pasteo, aste);
        if (xcode != 0)
        {
            program_check (regs, xcode);
            return 0;
        }

        /* Perform primary address space authorization
           using current authorization index */
        ax = (regs->cr[4] & CR4_AX) >> 16;
        if (authorize_asn (ax, aste, ATE_PRIMARY, regs))
        {
            regs->tea = pasn;
            program_check (regs, PGM_PRIMARY_AUTHORITY_EXCEPTION);
            return 0;
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
            {
                program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
                return 0;
            }

            /* Translate new primary ASN to obtain ASTE */
            xcode = translate_asn (pasn, &newregs, &pasteo, aste);

            /* Program check if ASN translation exception */
            if (xcode != 0)
            {
                program_check (&newregs, xcode);
                return 0;
            }

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
            {
                program_check (&newregs, PGM_SPECIAL_OPERATION_EXCEPTION);
                return 0;
            }

            /* Translate new secondary ASN to obtain ASTE */
            xcode = translate_asn (sasn, &newregs, &sasteo, aste);

            /* Program check if ASN translation exception */
            if (xcode != 0)
            {
                program_check (&newregs, xcode);
                return 0;
            }

            /* Obtain new SSTD from secondary ASTE */
            newregs.cr[7] = aste[2];

            /* Perform SASN authorization using new AX */
            ax = (newregs.cr[4] & CR4_AX) >> 16;
            if (authorize_asn (ax, aste, ATE_SECONDARY, &newregs))
            {
                newregs.tea = sasn;
                program_check (&newregs, PGM_SECONDARY_AUTHORITY_EXCEPTION);
                return 0;
            }

#ifdef FEATURE_SUBSPACE_GROUP
            /* Perform subspace replacement on new SSTD */
            newregs.cr[7] = subspace_replace (newregs.cr[7],
                                            sasteo, NULL, &newregs);
#endif /*FEATURE_SUBSPACE_GROUP*/

        } /* end else(sasn!=pasn) */

    } /* end if(LSED_UET_PC) */

    /* Update the current CPU registers from the working copy */
    *regs = newregs;

    /* Set the main storage reference and change bits */
    STORAGE_KEY(alsed) |= (STORKEY_REF | STORKEY_CHANGE);

    /* [5.12.4.4] Clear the next entry size field of the linkage
       stack entry now pointed to by control register 15 */
    lsedp = (LSED*)(sysblk.mainstor + alsed);
    lsedp->nes[0] = 0;
    lsedp->nes[1] = 0;

    /* Return the space switch event flag */
    return ssevent;

} /* end function program_return */

/*-------------------------------------------------------------------*/
/* Program Call                                                      */
/*                                                                   */
/* Input:                                                            */
/*      pcnum   20-bit PC number                                     */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      1=Space switch event indicated, 0=No space switch event      */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
int program_call (U32 pcnum, REGS *regs)
{
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
#ifdef FEATURE_TRACING
U32     newcr12 = 0;                    /* CR12 upon completion      */
#endif /*FEATURE_TRACING*/

    /* Special operation exception if DAT is off, or if
       in secondary space mode or home space mode */
    if (REAL_MODE(&(regs->psw)) || regs->psw.space == 1)
    {
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
        return 0;
    }

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
        {
            program_check (regs, PGM_ADDRESSING_EXCEPTION);
            return 0;
        }

        /* Fetch LTD from PASTE word 3 */
        ltd = fetch_fullword_absolute(pasteo+12);
    }

#ifdef FEATURE_TRACING
    /* Form trace entry if ASN tracing is active */
    if (regs->cr[12] & CR12_ASNTRACE)
        newcr12 = trace_pc (pcnum, regs);
#endif /*FEATURE_TRACING*/

    /* Special operation exception if subsystem linkage
       control bit in linkage table designation is zero */
    if ((ltd & LTD_SSLINK) == 0)
    {
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
        return 0;
    }

    /* [5.5.3.2] Linkage table lookup */

    /* Extract the linkage table origin and length from the LTD */
    lto = ltd & LTD_LTO;
    ltl = ltd & LTD_LTL;

    /* Program check if linkage index is outside the linkage table */
    if (ltl < ((pcnum & PC_LX) >> 13))
    {
        regs->tea = pcnum;
        program_check (regs, PGM_LX_TRANSLATION_EXCEPTION);
        return 0;
    }

    /* Calculate the address of the linkage table entry */
    lto += (pcnum & PC_LX) >> 6;
    lto &= 0x7FFFFFFF;

    /* Program check if linkage table entry is outside real storage */
    if (lto >= sysblk.mainsize)
    {
        program_check (regs, PGM_ADDRESSING_EXCEPTION);
        return 0;
    }

    /* Fetch linkage table entry from real storage.  All bytes
       must be fetched concurrently as observed by other CPUs */
    lto = APPLY_PREFIXING (lto, regs->pxr);
    lte = fetch_fullword_absolute(lto);

    /* Program check if linkage entry invalid bit is set */
    if (lte & LTE_INVALID)
    {
        regs->tea = pcnum;
        program_check (regs, PGM_LX_TRANSLATION_EXCEPTION);
        return 0;
    }

    /* [5.5.3.3] Entry table lookup */

    /* Extract the entry table origin and length from the LTE */
    eto = lte & LTE_ETO;
    etl = lte & LTE_ETL;

    /* Program check if entry index is outside the entry table */
    if (etl < ((pcnum & PC_EX) >> 2))
    {
        regs->tea = pcnum;
        program_check (regs, PGM_EX_TRANSLATION_EXCEPTION);
        return 0;
    }

    /* Calculate the starting address of the entry table entry */
    eto += (pcnum & PC_EX) << ((regs->cr[0] & CR0_ASF) ? 5 : 4);
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
        {
            program_check (regs, PGM_ADDRESSING_EXCEPTION);
            return 0;
        }

        /* Fetch one word of the entry table entry */
        ete[i] = fetch_fullword_absolute (abs);
        eto += 4;
        eto &= 0x7FFFFFFF;
    }

    /* Clear remaining words if fewer than 8 words were loaded */
    while (i < 8) ete[i++] = 0;

    /* Program check if basic program call in AR mode */
    if ((ete[4] & ETE4_T) == 0 && regs->psw.armode)
    {
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
        return 0;
    }

    /* Program check if addressing mode is zero and the
       entry instruction address is not a 24-bit address */
    if ((ete[1] & ETE1_AMODE) == 0
        && (ete[1] & ETE1_EIA) > 0x00FFFFFF)
    {
        program_check (regs, PGM_PC_TRANSLATION_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Program check if in problem state and the PKM in control
       register 3 produces zero when ANDed with the AKM in the ETE */
    if (regs->psw.prob
        && ((regs->cr[3] & CR3_KEYMASK) & (ete[0] & ETE0_AKM)) == 0)
    {
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
        return 0;
    }

    /* Obtain the new primary ASN from the entry table */
    pasn = ete[0] & ETE0_ASN;

    /* Perform ASN translation if ASN is non-zero */
    if (pasn != 0)
    {
        /* Program check if ASN translation control is zero */
        if ((regs->cr[14] & CR14_ASN_TRAN) == 0)
        {
            program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
            return 0;
        }

        /* Perform ASN translation to obtain ASTE */
        xcode = translate_asn (pasn, regs, &pasteo, aste);

        /* Program check if ASN translation exception */
        if (xcode != 0)
        {
            program_check (regs, xcode);
            return 0;
        }

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

        /* Perform the stacking process */
        retn = regs->psw.ia;
        if (regs->psw.amode) retn |= 0x80000000;
        form_stack_entry (LSED_UET_PC, retn, pcnum, regs);

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

    /* Return the space switch event flag */
    return ssevent;

} /* end function program_call */

/*-------------------------------------------------------------------*/
/* Branch and Set Authority                                          */
/*                                                                   */
/* Input:                                                            */
/*      r1      First operand register number                        */
/*      r2      Second operand register number                       */
/*      regs    Pointer to the CPU register context                  */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
void branch_and_set_authority (int r1, int r2, REGS *regs)
{
U32     ducto;                          /* DUCT origin               */
U32     duct8;                          /* DUCT word 8               */
U32     duct9;                          /* DUCT word 9               */
BYTE    key;                            /* New PSW key               */
#ifdef FEATURE_TRACING
U32     newcr12 = 0;                    /* CR12 upon completion      */
#endif /*FEATURE_TRACING*/

    /* Special operation exception if CR0 bit 15 is zero */
    if ((regs->cr[0] & CR0_ASF) == 0)
    {
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
        return;
    }

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
        return;
    }

    /* Convert DUCT real address to absolute address */
    ducto = APPLY_PREFIXING (ducto, regs->pxr);

    /* Program check if DUCT origin address is invalid */
    if (ducto >= sysblk.mainsize)
    {
        program_check (regs, PGM_ADDRESSING_EXCEPTION);
        return;
    }

    /* Load DUCT words 8 and 9 */
    duct8 = fetch_fullword_absolute (ducto+32);
    duct9 = fetch_fullword_absolute (ducto+36);

    /* Perform base authority or reduced authority operation */
    if ((duct9 & DUCT9_RA) == 0)
    {
        /* In base authority state R2 cannot specify register zero */
        if (r2 == 0)
        {
            program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
            return;
        }

        /* Obtain the new PSW key from R1 register bits 24-27 */
        key = regs->gpr[r1] & 0x000000F0;

        /* Privileged operation exception if in problem state and
           current PSW key mask does not permit new key value */
        if (regs->psw.prob
            && ((regs->cr[3] << (key >> 4)) & 0x80000000) == 0 )
        {
            program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
            return;
        }

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
        {
            program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
            return;
        }

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
            return;
        }

    } /* end if(BSA-ra) */

#ifdef FEATURE_TRACING
    /* Update trace table address if branch tracing is on */
    if ((regs->cr[12] & CR12_BRTRACE) && (r2 != 0))
        regs->cr[12] = newcr12;
#endif /*FEATURE_TRACING*/

} /* end function branch_and_set_authority */

#ifdef FEATURE_SUBSPACE_GROUP
/*-------------------------------------------------------------------*/
/* Branch in Subspace Group                                          */
/*                                                                   */
/* Input:                                                            */
/*      r1      First operand register number                        */
/*      r2      Second operand register number                       */
/*      regs    Pointer to the CPU register context                  */
/*                                                                   */
/*      This function does not return if a program check occurs.     */
/*-------------------------------------------------------------------*/
void branch_in_subspace_group (int r1, int r2, REGS *regs)
{
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

    /* Special operation exception if DAT is off or CR0 bit 15 is 0 */
    if (REAL_MODE(&(regs->psw))
        || (regs->cr[0] & CR0_ASF) == 0)
    {
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
        return;
    }

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
        return;
    }

    /* Convert DUCT real address to absolute address */
    ducto = APPLY_PREFIXING (ducto, regs->pxr);

    /* Program check if DUCT origin address is invalid */
    if (ducto >= sysblk.mainsize)
    {
        program_check (regs, PGM_ADDRESSING_EXCEPTION);
        return;
    }

    /* Fetch DUCT words 0, 1, and 3 from absolute storage
       (note: the DUCT cannot cross a page boundary) */
    duct0 = fetch_fullword_absolute (ducto);
    duct1 = fetch_fullword_absolute (ducto+4);
    duct3 = fetch_fullword_absolute (ducto+12);

    /* Special operation exception if the current primary ASTE origin
       is not the same as the base ASTE for the dispatchable unit */
    if ((regs->cr[5] & CR5_PASTEO) != (duct0 & DUCT0_BASTEO))
    {
        program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
        return;
    }

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
        {
            program_check (regs, PGM_ADDRESSING_EXCEPTION);
            return;
        }

        /* Fetch destination ASTE word 2 from absolute storage
           (note: the ASTE cannot cross a page boundary) */
        daste[2] = fetch_fullword_absolute (abs+8);

        break;

    case ALET_SECONDARY: /* Branch to last-used subspace */

        /* Load the subspace ASTE origin from the DUCT */
        dasteo = duct1 & DUCT1_SSASTEO;

        /* Special operation exception if SSASTEO is zero */
        if (dasteo == 0)
        {
            program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
            return;
        }

        /* Convert the ASTE origin to an absolute address */
        abs = APPLY_PREFIXING (dasteo, regs->pxr);

        /* Program check if ASTE origin address is invalid */
        if (abs >= sysblk.mainsize)
        {
            program_check (regs, PGM_ADDRESSING_EXCEPTION);
            return;
        }

        /* Fetch subspace ASTE words 0, 2, and 5 from absolute
           storage (note: the ASTE cannot cross a page boundary) */
        daste[0] = fetch_fullword_absolute (abs);
        daste[2] = fetch_fullword_absolute (abs+8);
        daste[5] = fetch_fullword_absolute (abs+20);

        /* ASTE validity exception if ASTE invalid bit is one */
        if (daste[0] & ASTE0_INVALID)
        {
            program_check (regs, PGM_ASTE_VALIDITY_EXCEPTION);
            return;
        }

        /* ASTE sequence exception if the subspace ASTE sequence
           number does not match the sequence number in the DUCT */
        if ((daste[5] & ASTE5_ASTESN) != (duct3 & DUCT3_SSASTESN))
        {
            program_check (regs, PGM_ASTE_SEQUENCE_EXCEPTION);
            return;
        }

        break;

    default: /* ALET not 0 or 1 */

        /* Perform special ART to obtain destination ASTE */
        xcode = translate_alet (alet, 0, ACCTYPE_BSG, regs,
                                &dasteo, daste, &protect);

        /* Program check if ALET translation error */
        if (xcode != 0)
        {
            program_check (regs, xcode);
            return;
        }

        /* Special operation exception if the destination ASTE
           is the base space of a different subspace group */
        if (dasteo != (duct0 & DUCT0_BASTEO)
                && ((daste[2] & STD_GROUP) == 0
                    || (daste[0] & ASTE0_BASE) == 0))
        {
            program_check (regs, PGM_SPECIAL_OPERATION_EXCEPTION);
            return;
        }

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

} /* end function branch_in_subspace_group */
#endif /*FEATURE_SUBSPACE_GROUP*/

