/* DAT.C        (c) Copyright Roger Bowler, 1999-2000                */
/*              ESA/390 Dynamic Address Translation                  */

/* Interpretive Execution - (c) Copyright Jan Jaeger, 1999-2000      */

/*-------------------------------------------------------------------*/
/* This module implements the DAT, ALET, and ASN translation         */
/* functions of the ESA/390 architecture, described in the manual    */
/* SA22-7201-04 ESA/390 Principles of Operation.  The numbers in     */
/* square brackets in the comments refer to sections in the manual.  */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      S/370 DAT support by Jay Maynard (as described in            */
/*      GA22-7000 System/370 Principles of Operation)                */
/*      Clear remainder of ASTE when ASF=0 - Jan Jaeger              */
/*      S/370 DAT support when running under SIE - Jan Jaeger        */
/*-------------------------------------------------------------------*/

#include "hercules.h"

#include "inline.h"

#include "opcode.h"

/*-------------------------------------------------------------------*/
/* Translate ASN to produce address-space control parameters         */
/*                                                                   */
/* Input:                                                            */
/*      asn     Address space number to be translated                */
/*      regs    Pointer to the CPU register context                  */
/*      asteo   Pointer to a word to receive real address of ASTE    */
/*      aste    Pointer to 16-word area to receive a copy of the     */
/*              ASN second table entry associated with the ASN       */
/*                                                                   */
/* Output:                                                           */
/*      If successful, the ASTE corresponding to the ASN value will  */
/*      be stored into the 16-word area pointed to by aste, and the  */
/*      return value is zero.  Either 4 or 16 words will be stored   */
/*      depending on the value of the ASF control bit (CR0 bit 15).  */
/*      The real address of the ASTE will be stored into the word    */
/*      pointed to by asteo.                                         */
/*                                                                   */
/*      If unsuccessful, the return value is a non-zero exception    */
/*      code indicating AFX-translation or ASX-translation error     */
/*      (this is to allow the LASP instruction to handle these       */
/*      exceptions by setting the condition code).                   */
/*                                                                   */
/*      A program check may be generated for addressing and ASN      */
/*      translation specification exceptions, in which case the      */
/*      function does not return.                                    */
/*-------------------------------------------------------------------*/
U16 translate_asn (U16 asn, REGS *regs, U32 *asteo, U32 aste[])
{
U32     afte_addr;                      /* Address of AFTE           */
U32     afte;                           /* ASN first table entry     */
U32     aste_addr;                      /* Address of ASTE           */
int     code;                           /* Exception code            */
int     numwords;                       /* ASTE size (4 or 16 words) */
int     i;                              /* Array subscript           */

    /* [3.9.3.1] Use the AFX to obtain the real address of the AFTE */
    afte_addr = (regs->cr[14] & CR14_AFTO) << 12;
    afte_addr += (asn & ASN_AFX) >> 4;

    /* Addressing exception if AFTE is outside main storage */
    if (afte_addr >= regs->mainsize)
        goto asn_addr_excp;

    /* Load the AFTE from main storage. All four bytes must be
       fetched concurrently as observed by other CPUs */
    afte_addr = APPLY_PREFIXING (afte_addr, regs->pxr);
    afte = fetch_fullword_absolute (afte_addr, regs);

    /* AFX translation exception if AFTE invalid bit is set */
    if (afte & AFTE_INVALID)
        goto asn_afx_tran_excp;

    /* ASN translation specification exception if reserved bits set */
    if ((regs->cr[0] & CR0_ASF) == 0) {
        if (afte & AFTE_RESV_0)
              goto asn_asn_tran_spec_excp;
    } else {
        if (afte & AFTE_RESV_1)
              goto asn_asn_tran_spec_excp;
    }

    /* [3.9.3.2] Use AFTE and ASX to obtain real address of ASTE */
    if ((regs->cr[0] & CR0_ASF) == 0) {
        aste_addr = afte & AFTE_ASTO_0;
        aste_addr += (asn & ASN_ASX) << 4;
        numwords = 4;
    } else {
        aste_addr = afte & AFTE_ASTO_1;
        aste_addr += (asn & ASN_ASX) << 6;
        numwords = 16;
    }

    /* Ignore carry into bit position 0 of ASTO */
    aste_addr &= 0x7FFFFFFF;

    /* Addressing exception if ASTE is outside main storage */
    if (aste_addr >= regs->mainsize)
        goto asn_addr_excp;

    /* Return the real address of the ASTE */
    *asteo = aste_addr;

    /* Fetch the 16- or 64-byte ASN second table entry from real
       storage.  Each fullword of the ASTE must be fetched
       concurrently as observed by other CPUs */
    aste_addr = APPLY_PREFIXING (aste_addr, regs->pxr);
    for (i = 0; i < numwords; i++)
    {
        aste[i] = fetch_fullword_absolute (aste_addr, regs);
        aste_addr += 4;
    }
    /* Clear remaining words if fewer than 16 words were loaded */
    while (i < 16) aste[i++] = 0;


    /* Check the ASX invalid bit in the ASTE */
    if (aste[0] & ASTE0_INVALID)
        goto asn_asx_tran_excp;

    /* Check the reserved bits in first two words of ASTE */
    if ((aste[0] & ASTE0_RESV) || (aste[1] & ASTE1_RESV)
        || ((aste[0] & ASTE0_BASE)
#ifdef FEATURE_SUBSPACE_GROUP
            && !(regs->cr[0] & CR0_ASF)
#endif /*FEATURE_SUBSPACE_GROUP*/
            ))
        goto asn_asn_tran_spec_excp;

    return 0;

/* Conditions which always cause program check */
asn_addr_excp:
    code = PGM_ADDRESSING_EXCEPTION;
    goto asn_prog_check;

asn_asn_tran_spec_excp:
    code = PGM_ASN_TRANSLATION_SPECIFICATION_EXCEPTION;
    goto asn_prog_check;

asn_prog_check:
    program_interrupt (regs, code);

/* Conditions which the caller may or may not program check */
asn_afx_tran_excp:
    regs->tea = asn;
    code = PGM_AFX_TRANSLATION_EXCEPTION;
    return code;

asn_asx_tran_excp:
    regs->tea = asn;
    code = PGM_ASX_TRANSLATION_EXCEPTION;
    return code;

} /* end function translate_asn */

/*-------------------------------------------------------------------*/
/* Perform ASN authorization process                                 */
/*                                                                   */
/* Input:                                                            */
/*      ax      Authorization index                                  */
/*      aste    Pointer to 16-word area containing a copy of the     */
/*              ASN second table entry associated with the ASN       */
/*      atemask Specifies which authority bit to test in the ATE:    */
/*              ATE_PRIMARY (for PT instruction)                     */
/*              ATE_SECONDARY (for PR, SSAR, and LASP instructions,  */
/*                             and all access register translations) */
/*      regs    Pointer to the CPU register context                  */
/*                                                                   */
/* Operation:                                                        */
/*      The AX is used to select an entry in the authority table     */
/*      pointed to by the ASTE, and an authorization bit in the ATE  */
/*      is tested.  For ATE_PRIMARY (X'80'), the P bit is tested.    */
/*      For ATE_SECONDARY (X'40'), the S bit is tested.              */
/*      Authorization is successful if the ATE falls within the      */
/*      authority table limit and the tested bit value is 1.         */
/*                                                                   */
/* Output:                                                           */
/*      If authorization is successful, the return value is zero.    */
/*      If authorization is unsuccessful, the return value is 1.     */
/*                                                                   */
/*      A program check may be generated for addressing exception    */
/*      if the authority table entry address is invalid, and in      */
/*      this case the function does not return.                      */
/*-------------------------------------------------------------------*/
int authorize_asn (U16 ax, U32 aste[], int atemask, REGS *regs)
{
U32     ato;                            /* Authority table origin    */
int     atl;                            /* Authority table length    */
BYTE    ate;                            /* Authority table entry     */

    /* [3.10.3.1] Authority table lookup */

    /* Isolate the authority table origin and length */
    ato = aste[0] & ASTE0_ATO;
    atl = aste[1] & ASTE1_ATL;

    /* Authorization fails if AX is outside table */
    if ((ax & 0xFFF0) > atl)
        return 1;

    /* Calculate the address of the byte in the authority
       table which contains the 2 bit entry for this AX */
    ato += (ax >> 2);

    /* Ignore carry into bit position 0 */
    ato &= 0x7FFFFFFF;

    /* Addressing exception if ATE is outside main storage */
    if (ato >= regs->mainsize)
        goto auth_addr_excp;

    /* Load the byte containing the authority table entry
       and shift the entry into the leftmost 2 bits */
    ato = APPLY_PREFIXING (ato, regs->pxr);

    SIE_TRANSLATE(&ato, ACCTYPE_SIE, regs);

    ate = sysblk.mainstor[ato];
    ate <<= ((ax & 0x03)*2);

    /* Set the main storage reference bit */
    STORAGE_KEY(ato) |= STORKEY_REF;

    /* Authorization fails if the specified bit (either X'80' or
       X'40' of the 2 bit authority table entry) is zero */
    if ((ate & atemask) == 0)
        return 1;

    /* Exit with successful return code */
    return 0;

auth_addr_excp:
    program_interrupt (regs, PGM_ADDRESSING_EXCEPTION);

    return -1; /* prevent warning from compiler */
} /* end function authorize_asn */

/*-------------------------------------------------------------------*/
/* Translate an ALET to produce the corresponding ASTE               */
/*                                                                   */
/* This routine performs both ordinary ART (as used by DAT when      */
/* operating in access register mode, and by the TAR instruction),   */
/* and special ART (as used by the BSG instruction).  The caller     */
/* is assumed to have already eliminated the special cases of ALET   */
/* values 0 and 1 (which have different meanings depending on        */
/* whether the caller is DAT, TAR, or BSG).                          */
/*                                                                   */
/* Input:                                                            */
/*      alet    ALET value                                           */
/*      eax     The authorization index (normally obtained from      */
/*              CR8; obtained from R2 for TAR; not used for BSG)     */
/*      acctype Type of access requested: READ, WRITE, INSTFETCH,    */
/*              TAR, LRA, TPROT, or BSG                              */
/*      regs    Pointer to the CPU register context                  */
/*      asteo   Pointer to word to receive ASTE origin address       */
/*      aste    Pointer to 16-word area to receive a copy of the     */
/*              ASN second table entry associated with the ALET      */
/*      prot    Pointer to field to receive protection indicator     */
/*                                                                   */
/* Output:                                                           */
/*      If successful, the ASTE is copied into the 16-word area,     */
/*      the real address of the ASTE is stored into the word pointed */
/*      word pointed to by asteop, and the return value is zero;     */
/*      the protection indicator is set to 1 if the fetch-only bit   */
/*      in the ALE is set, otherwise it remains unchanged.           */
/*                                                                   */
/*      If unsuccessful, the return value is a non-zero exception    */
/*      code in the range X'0028' through X'002D' (this is to allow  */
/*      the TAR, LRA, and TPROT instructions to handle these         */
/*      exceptions by setting the condition code).                   */
/*                                                                   */
/*      A program check may be generated for addressing and ASN      */
/*      translation specification exceptions, in which case the      */
/*      function does not return.                                    */
/*-------------------------------------------------------------------*/
U16 translate_alet (U32 alet, U16 eax, int acctype, REGS *regs,
                    U32 *asteo, U32 aste[], int *prot)
{
U32     cb;                             /* DUCT or PASTE address     */
U32     ald;                            /* Access-list designation   */
U32     alo;                            /* Access-list origin        */
int     all;                            /* Access-list length        */
U32     ale[4];                         /* Access-list entry         */
U32     aste_addr;                      /* Real address of ASTE      */
U32     abs;                            /* Absolute address          */
int     i;                              /* Array subscript           */
int     code;                           /* Exception code            */

    /* [5.8.4.3] Check the reserved bits in the ALET */
    if ( alet & ALET_RESV )
        goto alet_spec_excp;

    /* [5.8.4.4] Obtain the effective access-list designation */

    /* Obtain the real address of the control block containing
       the effective access-list designation.  This is either
       the Primary ASTE or the DUCT */
    cb = (alet & ALET_PRI_LIST) ?
            regs->cr[5] & CR5_PASTEO :
            regs->cr[2] & CR2_DUCTO;

    /* Addressing exception if outside main storage */
    if (cb >= regs->mainsize)
        goto alet_addr_excp;

    /* Load the effective access-list designation (ALD) from
       offset 16 in the control block.  All four bytes must be
       fetched concurrently as observed by other CPUs.  Note
       that the DUCT and the PASTE cannot cross a page boundary */
    cb = APPLY_PREFIXING (cb, regs->pxr);
    ald = fetch_fullword_absolute (cb+16, regs);

    /* [5.8.4.5] Access-list lookup */

    /* Isolate the access-list origin and access-list length */
    alo = ald & ALD_ALO;
    all = ald & ALD_ALL;

    /* Check that the ALEN does not exceed the ALL */
    if (((alet & ALET_ALEN) >> ALD_ALL_SHIFT) > all)
        goto alen_tran_excp;

    /* Add the ALEN x 16 to the access list origin */
    alo += (alet & ALET_ALEN) << 4;

    /* Addressing exception if outside main storage */
    if (alo >= regs->mainsize)
        goto alet_addr_excp;

    /* Fetch the 16-byte access list entry from absolute storage.
       Each fullword of the ALE must be fetched concurrently as
       observed by other CPUs */
    alo = APPLY_PREFIXING (alo, regs->pxr);
    for (i = 0; i < 4; i++)
    {
        ale[i] = fetch_fullword_absolute (alo, regs);
        alo += 4;
    }

    /* Check the ALEN invalid bit in the ALE */
    if (ale[0] & ALE0_INVALID)
        goto alen_tran_excp;

    /* For ordinary ART (but not for special ART),
       compare the ALE sequence number with the ALET */
    if (acctype != ACCTYPE_BSG
        && (ale[0] & ALE0_ALESN) != (alet & ALET_ALESN))
        goto ale_seq_excp;

    /* [5.8.4.6] Locate the ASN-second-table entry */
    aste_addr = ale[2] & ALE2_ASTE;

    /* Addressing exception if ASTE is outside main storage */
    abs = APPLY_PREFIXING (aste_addr, regs->pxr);
    if (abs >= regs->mainsize)
        goto alet_addr_excp;

    /* Fetch the 64-byte ASN second table entry from real storage.
       Each fullword of the ASTE must be fetched concurrently as
       observed by other CPUs.  ASTE cannot cross a page boundary */
    for (i = 0; i < 16; i++)
    {
        aste[i] = fetch_fullword_absolute (abs, regs);
        abs += 4;
    }

    /* Check the ASX invalid bit in the ASTE */
    if (aste[0] & ASTE0_INVALID)
        goto aste_vald_excp;

    /* Compare the ASTE sequence number with the ALE */
    if ((aste[5] & ASTE5_ASTESN) != (ale[3] & ALE3_ASTESN))
        goto aste_seq_excp;

    /* [5.8.4.7] For ordinary ART (but not for special ART),
       authorize the use of the access-list entry */
    if (acctype != ACCTYPE_BSG)
    {
        /* If ALE private bit is zero, or the ALE AX equals the
           EAX, then authorization succeeds.  Otherwise perform
           the extended authorization process. */
        if ((ale[0] & ALE0_PRIVATE)
                && (ale[0] & ALE0_ALEAX) != eax)
        {
            /* Check the reserved bits in first two words of ASTE */
            if ((aste[0] & ASTE0_RESV) || (aste[1] & ASTE1_RESV)
                || ((aste[0] & ASTE0_BASE)
#ifdef FEATURE_SUBSPACE_GROUP
                        && (regs->cr[0] & CR0_ASF) == 0
#endif /*FEATURE_SUBSPACE_GROUP*/
                   ))
                goto alet_asn_tran_spec_excp;

            /* Perform extended authorization */
            if (authorize_asn(eax, aste, ATE_SECONDARY, regs) != 0)
                goto ext_auth_excp;
        }

    } /* end if(!ACCTYPE_BSG) */

    /* [5.8.4.8] Check for access-list controlled protection */
    if (ale[0] & ALE0_FETCHONLY)
        *prot = 1;

    /* Return the ASTE origin address */
    *asteo = aste_addr;
    return 0;

/* Conditions which always cause program check */
alet_addr_excp:
    code = PGM_ADDRESSING_EXCEPTION;
    goto alet_prog_check;

alet_asn_tran_spec_excp:
    code = PGM_ASN_TRANSLATION_SPECIFICATION_EXCEPTION;
    goto alet_prog_check;

alet_prog_check:
    program_interrupt (regs, code);

/* Conditions which the caller may or may not program check */
alet_spec_excp:
    code = PGM_ALET_SPECIFICATION_EXCEPTION;
    return code;

alen_tran_excp:
    code = PGM_ALEN_TRANSLATION_EXCEPTION;
    return code;

ale_seq_excp:
    code = PGM_ALE_SEQUENCE_EXCEPTION;
    return code;

aste_vald_excp:
    code = PGM_ASTE_VALIDITY_EXCEPTION;
    return code;

aste_seq_excp:
    code = PGM_ASTE_SEQUENCE_EXCEPTION;
    return code;

ext_auth_excp:
    code = PGM_EXTENDED_AUTHORITY_EXCEPTION;
    return code;

} /* end function translate_alet */

/*-------------------------------------------------------------------*/
/* Purge the ART lookaside buffer                                    */
/*-------------------------------------------------------------------*/
void purge_alb (REGS *regs)
{
} /* end function purge_alb */

#ifndef INLINE_FETCH
/*-------------------------------------------------------------------*/
/* Translate a 31-bit virtual address to a real address              */
/*                                                                   */
/* Input:                                                            */
/*      vaddr   31-bit virtual address to be translated              */
/*      arn     Access register number containing ALET (AR0 is       */
/*              treated as containing ALET value 0), or special      */
/*              value USE_PRIMARY_SPACE or USE_SECONDARY_SPACE)      */
/*      regs    Pointer to the CPU register context                  */
/*      acctype Type of access requested: READ, WRITE, INSTFETCH,    */
/*              LRA, IVSK, TPROT, or STACK                           */
/*      raddr   Pointer to field to receive real address             */
/*      xcode   Pointer to field to receive exception code           */
/*      priv    Pointer to field to receive private indicator        */
/*      prot    Pointer to field to receive protection indicator     */
/*      pstid   Pointer to field to receive indication of which      */
/*              segment table was used for the translation           */
/*      xpblk   Pointer to field to receive expanded storage         */
/*              block number, or NULL                                */
/*      xpkey   Pointer to field to receive expanded storage key     */
/*                                                                   */
/* Output:                                                           */
/*      The return value is set to facilitate the setting of the     */
/*      condition code by the LRA instruction:                       */
/*      0 = Translation successful; real address field contains      */
/*          the real address corresponding to the virtual address    */
/*          supplied by the caller; exception code set to zero.      */
/*      1 = Segment table entry invalid; real address field          */
/*          contains real address of segment table entry;            */
/*          exception code is set to X'0010'.                        */
/*      2 = Page table entry invalid; real address field contains    */
/*          real address of page table entry; exception code         */
/*          is set to X'0011'.                                       */
/*      3 = Segment or page table length exceeded; real address      */
/*          field contains the real address of the entry that        */
/*          would have been fetched if length violation had not      */
/*          occurred; exception code is set to X'0010' or X'0011'.   */
/*      4 = ALET translation error; real address field is not        */
/*          set; exception code is set to X'0028' through X'002D'.   */
/*          The LRA instruction converts this to condition code 3.   */
/*      5 = Page table entry invalid and xpblk pointer is not NULL   */
/*          and page exists in expanded storage; xpblk field is set  */
/*          to the expanded storage block number and xpkey field     */
/*          is set to the protection key of the block.  This is      */
/*          used by the MVPG instruction.                            */
/*                                                                   */
/*      The private indicator is set to 1 if translation was         */
/*      successful and the STD indicates a private address space;    */
/*      otherwise it remains unchanged.                              */
/*                                                                   */
/*      The protection indicator is set to 1 if translation was      */
/*      successful and either access-list controlled protection or   */
/*      page protection is active; otherwise it remains unchanged.   */
/*                                                                   */
/*      The segment table indication field is set to one of the      */
/*      values TEA_ST_PRIMARY, TEA_ST_SECNDRY, TEA_ST_HOME, or       */
/*      TEA_ST_ARMODE if the translation was successful.  This       */
/*      indication is used to set bits 30-31 of the translation      */
/*      exception address in the event of a protection exception     */
/*      when the suppression on protection facility is used.         */
/*                                                                   */
/*      A program check may be generated for addressing and          */
/*      translation specification exceptions, in which case the      */
/*      function does not return.                                    */
/*-------------------------------------------------------------------*/
int translate_addr (U32 vaddr, int arn, REGS *regs, int acctype,
                    U32 *raddr, U16 *xcode, int *priv, int *prot,
                    int *pstid, U32 *xpblk, BYTE *xpkey)
{
U32     std = 0;                        /* Segment table descriptor  */
U32     sto = 0;                        /* Segment table origin      */
int     stl;                            /* Segment table length      */
int     private = 0;                    /* 1=Private address space   */
int     protect = 0;                    /* 1=ALE or page protection  */
U32     ste = 0;                        /* Segment table entry       */
U32     pto;                            /* Page table origin         */
int     ptl;                            /* Page table length         */
TLBE   *tlbp;                           /* -> TLB entry              */
U32     alet;                           /* Access list entry token   */
U32     asteo;                          /* Real address of ASTE      */
U32     aste[16];                       /* ASN second table entry    */
int     stid;                           /* Segment table indication  */
int     cc;                             /* Condition code            */
U16     eax;                            /* Authorization index       */

    /* [3.11.3.1] Load the effective segment table descriptor */
    if (acctype == ACCTYPE_INSTFETCH)
    {
        if (HOME_SPACE_MODE(&regs->psw))
        {
            stid = TEA_ST_HOME;
            std = regs->cr[13];
        }
        else
        {
            stid = TEA_ST_PRIMARY;
            std = regs->cr[1];
        }
    }
    else if (acctype == ACCTYPE_STACK)
    {
        stid = TEA_ST_HOME;
        std = regs->cr[13];
    }
    else if (arn == USE_PRIMARY_SPACE)
    {
        stid = TEA_ST_PRIMARY;
        std = regs->cr[1];
    }
    else if (arn == USE_SECONDARY_SPACE)
    {
        stid = TEA_ST_SECNDRY;
        std = regs->cr[7];
    }
    else if(ACCESS_REGISTER_MODE(&regs->psw)
#if defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)
      || (regs->sie_active
        && (regs->guestregs->siebk->mx & SIE_MX_XC)
        && regs->guestregs->psw.armode)
#endif /*defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)*/
        )
    {
        /* [5.8.4.1] Select the access-list-entry token */
        alet = (arn == 0) ? 0 :
#if defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)
        /* Obtain guest ALET if guest is XC guest in AR mode */
        (regs->sie_active && (regs->guestregs->siebk->mx & SIE_MX_XC) 
         && regs->guestregs->psw.armode)
          ? regs->guestregs->ar[arn] :
        /* else if in SIE mode but not an XC guest in AR mode 
           then the ALET will be zero */
        (regs->sie_active) ? 0 :
#endif /*defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)*/
            regs->ar[arn];

        /* Use the ALET to determine the segment table origin */
        switch (alet) {

        case ALET_PRIMARY:
            /* [5.8.4.2] Obtain primary segment table designation */
            stid = TEA_ST_PRIMARY;
            std = regs->cr[1];
            break;

        case ALET_SECONDARY:
            /* [5.8.4.2] Obtain secondary segment table designation */
            stid = TEA_ST_SECNDRY;
            std = regs->cr[7];
            break;

        default:
            /* Extract the extended AX from CR8 bits 0-15 */
            eax = (regs->cr[8] & CR8_EAX) >> 16;

            /* [5.8.4.3] Perform ALET translation to obtain ASTE */
            *xcode = translate_alet (alet, eax, acctype, regs,
                                    &asteo, aste, &protect);

            /* Exit if ALET translation error */
            if (*xcode != 0)
                goto tran_alet_excp;

            /* [5.8.4.9] Obtain the STD from word 2 of the ASTE */
            stid = TEA_ST_ARMODE;
            std = aste[2];

        } /* end switch(alet) */

    } /* end if(ACCESS_REGISTER_MODE) */
    else if (PRIMARY_SPACE_MODE(&regs->psw))
    {
        stid = TEA_ST_PRIMARY;
        std = regs->cr[1];
    }
    else if (SECONDARY_SPACE_MODE(&regs->psw))
    {
        stid = TEA_ST_SECNDRY;
        std = regs->cr[7];
    }
    else /* HOME_SPACE_MODE */
    {
        stid = TEA_ST_HOME;
        std = regs->cr[13];
    }

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    /* Determine the translation format that is going to be used */
    if(regs->sie_state && (regs->siebk->m & SIE_M_370))
#endif
#if defined(FEATURE_INTERPRETIVE_EXECUTION) || !defined(FEATURE_S390_DAT)
    {
    /* S/370 Dynamic Address Translation */
    U16 pte;

        /* Extract the private space bit from segment table descriptor */

        /* [3.11.3.2] Check the translation format bits in CR0 */
        if ((((regs->cr[0] & CR0_PAGE_SIZE) != CR0_PAGE_SZ_2K) &&
           ((regs->cr[0] & CR0_PAGE_SIZE) != CR0_PAGE_SZ_4K)) ||
           (((regs->cr[0] & CR0_SEG_SIZE) != CR0_SEG_SZ_64K) &&
           ((regs->cr[0] & CR0_SEG_SIZE) != CR0_SEG_SZ_1M)))
           goto tran_spec_excp;

        /* [3.11.4] Look up the address in the TLB */
        /* [10.17] Do not use TLB if processing LRA instruction */

        /* Only a single entry in the TLB will be looked up, namely the
           entry indexed by bits 12-19 of the virtual address */
        if (acctype == ACCTYPE_LRA)
            tlbp = NULL;
        else
            tlbp = &(regs->tlb[(vaddr >> 12) & 0xFF]);

        if (tlbp != NULL
            && ((((regs->cr[0] & CR0_PAGE_SIZE) == CR0_PAGE_SZ_4K) &&
            (vaddr & 0x00FFF000) == tlbp->vaddr) ||
            (((regs->cr[0] & CR0_PAGE_SIZE) == CR0_PAGE_SZ_2K) &&
            (vaddr & 0x00FFF800) == tlbp->vaddr))
            && tlbp->valid
            && (tlbp->common || std == tlbp->std)
            && !(tlbp->common && private))
        {
            pte = tlbp->pte;
        }
        else
        {
            /* [3.11.3.3] Segment table lookup */
    
            /* Calculate the real address of the segment table entry */
            sto = std & STD_370_STO;
            stl = std & STD_370_STL;
            sto += ((regs->cr[0] & CR0_SEG_SIZE) == CR0_SEG_SZ_1M) ?
                ((vaddr & 0x00F00000) >> 18) :
                ((vaddr & 0x00FF0000) >> 14);
    
            /* Check that virtual address is within the segment table */
            if (((regs->cr[0] & CR0_SEG_SIZE) == CR0_SEG_SZ_64K) &&
                ((vaddr << 4) & STD_370_STL) > stl)
                goto seg_tran_length;
    
            /* Generate addressing exception if outside real storage */
            if (sto >= regs->mainsize)
                goto address_excp;
    
            /* Fetch segment table entry from real storage.  All bytes
               must be fetched concurrently as observed by other CPUs */
            sto = APPLY_PREFIXING (sto, regs->pxr);
            ste = fetch_fullword_absolute (sto, regs);
    
            /* Generate segment translation exception if segment invalid */
            if (ste & SEGTAB_370_INVL)
                goto seg_tran_invalid;
    
            /* Check that all the reserved bits in the STE are zero */
            if (ste & SEGTAB_370_RSV)
                goto tran_spec_excp;
    
            /* Isolate page table origin and length */
            pto = ste & SEGTAB_370_PTO;
            ptl = ste & SEGTAB_370_PTL;
    
            /* [3.11.3.4] Page table lookup */
    
            /* Calculate the real address of the page table entry */
            pto += ((regs->cr[0] & CR0_SEG_SIZE) == CR0_SEG_SZ_1M) ?
                (((regs->cr[0] & CR0_PAGE_SIZE) == CR0_PAGE_SZ_4K) ?
                ((vaddr & 0x000FF000) >> 11) :
                ((vaddr & 0x000FF800) >> 10)) :
                (((regs->cr[0] & CR0_PAGE_SIZE) == CR0_PAGE_SZ_4K) ?
                ((vaddr & 0x0000F000) >> 11) :
                ((vaddr & 0x0000F800) >> 10));
    
            /* Generate addressing exception if outside real storage */
            if (pto >= regs->mainsize)
                goto address_excp;
    
            /* Check that the virtual address is within the page table */
            if ((((regs->cr[0] & CR0_SEG_SIZE) == CR0_SEG_SZ_1M) &&
                (((vaddr & 0x000F0000) >> 16) > ptl)) ||
                (((regs->cr[0] & CR0_SEG_SIZE) == CR0_SEG_SZ_64K) &&
                (((vaddr & 0x0000F000) >> 12) > ptl)))
                goto page_tran_length;
    
            /* Fetch the page table entry from real storage.  All bytes
               must be fetched concurrently as observed by other CPUs */
            pto = APPLY_PREFIXING (pto, regs->pxr);
            pte = fetch_halfword_absolute (pto, regs);
    
            /* Generate page translation exception if page invalid */
            if ((((regs->cr[0] & CR0_PAGE_SIZE) == CR0_PAGE_SZ_4K) &&
                (pte & PAGETAB_INV_4K)) ||
                (((regs->cr[0] & CR0_PAGE_SIZE) == CR0_PAGE_SZ_2K) &&
                (pte & PAGETAB_INV_2K)))
                goto page_tran_invalid;
    
            /* Check that all the reserved bits in the PTE are zero */
            if (((regs->cr[0] & CR0_PAGE_SIZE) == CR0_PAGE_SZ_2K) &&
                (pte & PAGETAB_RSV_2K))
                goto tran_spec_excp;
    
            /* [3.11.4.2] Place the translated address in the TLB */
            if (tlbp != NULL)
            {
                tlbp->std = std;
                tlbp->vaddr =
                    ((regs->cr[0] & CR0_PAGE_SIZE) == CR0_PAGE_SZ_4K) ?
                    vaddr & 0x00FFF000 : vaddr & 0x00FFF800;
                tlbp->pte = pte;
                tlbp->common = (ste & SEGTAB_370_CMN) ? 1 : 0;
                tlbp->valid = 1;
            }
    
        } /* end if(!TLB) */
    
        /* Set the protection indicator if page protection is active */
#ifdef FEATURE_SEGMENT_PROTECTION
        if (ste & SEGTAB_370_PROT)
            protect = 1;
#endif
    
        /* [3.11.3.5] Combine the page frame real address with the byte
           index of the virtual address to form the real address */
        *raddr = ((regs->cr[0] & CR0_PAGE_SIZE) == CR0_PAGE_SZ_4K) ?
            (((U32)pte & PAGETAB_PFRA_4K) << 8) | (vaddr & 0xFFF) :
            (((U32)pte & PAGETAB_PFRA_2K) << 8) | (vaddr & 0x7FF);
    
    }
#endif
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    else
#endif
#if defined(FEATURE_INTERPRETIVE_EXECUTION) || defined(FEATURE_S390_DAT)
    {
    /* S/390 Dynamic Address Translation */
    U32 pte;

        /* Extract the private space bit from segment table descriptor */
        private = std & STD_PRIVATE;

        /* [3.11.3.2] Check the translation format bits in CR0 */
        if ((regs->cr[0] & CR0_TRAN_FMT) != CR0_TRAN_ESA390)
            goto tran_spec_excp;

        /* [3.11.4] Look up the address in the TLB */
        /* [10.17] Do not use TLB if processing LRA instruction */

        /* Only a single entry in the TLB will be looked up, namely the
           entry indexed by bits 12-19 of the virtual address */
        if (acctype == ACCTYPE_LRA)
            tlbp = NULL;
        else
            tlbp = &(regs->tlb[(vaddr >> 12) & 0xFF]);

        if (tlbp != NULL
            && (vaddr & 0x7FFFF000) == tlbp->vaddr
            && tlbp->valid
            && (tlbp->common || std == tlbp->std)
            && !(tlbp->common && private))
        {
            pte = tlbp->pte;
        }
        else
        {
            /* [3.11.3.3] Segment table lookup */
    
            /* Calculate the real address of the segment table entry */
            sto = std & STD_STO;
            stl = std & STD_STL;
            sto += (vaddr & 0x7FF00000) >> 18;
    
            /* Generate addressing exception if outside real storage */
            if (sto >= regs->mainsize)
                goto address_excp;
    
            /* Check that virtual address is within the segment table */
            if ((vaddr >> 24) > stl)
                goto seg_tran_length;
    
            /* Fetch segment table entry from real storage.  All bytes
               must be fetched concurrently as observed by other CPUs */
            sto = APPLY_PREFIXING (sto, regs->pxr);
            ste = fetch_fullword_absolute (sto, regs);
    
            /* Generate segment translation exception if segment invalid */
            if (ste & SEGTAB_INVALID)
                goto seg_tran_invalid;
    
            /* Check that all the reserved bits in the STE are zero */
            if (ste & SEGTAB_RESV)
                goto tran_spec_excp;
    
            /* If the segment table origin register indicates a private
               address space then STE must not indicate a common segment */
            if (private && (ste & (SEGTAB_COMMON)))
                goto tran_spec_excp;
    
            /* Isolate page table origin and length */
            pto = ste & SEGTAB_PTO;
            ptl = ste & SEGTAB_PTL;
    
            /* [3.11.3.4] Page table lookup */
    
            /* Calculate the real address of the page table entry */
            pto += (vaddr & 0x000FF000) >> 10;
    
            /* Generate addressing exception if outside real storage */
            if (pto >= regs->mainsize)
                goto address_excp;
    
            /* Check that the virtual address is within the page table */
            if (((vaddr & 0x000FF000) >> 16) > ptl)
                goto page_tran_length;
    
            /* Fetch the page table entry from real storage.  All bytes
               must be fetched concurrently as observed by other CPUs */
            pto = APPLY_PREFIXING (pto, regs->pxr);
            pte = fetch_fullword_absolute (pto, regs);
    
            /* Generate page translation exception if page invalid */
            if (pte & PAGETAB_INVALID)
                goto page_tran_invalid;
    
            /* Check that all the reserved bits in the PTE are zero */
            if (pte & PAGETAB_RESV)
                goto tran_spec_excp;
    
            /* [3.11.4.2] Place the translated address in the TLB */
            if (tlbp != NULL)
            {
                tlbp->std = std;
                tlbp->vaddr = vaddr & 0x7FFFF000;
                tlbp->pte = pte;
                tlbp->common = (ste & SEGTAB_COMMON) ? 1 : 0;
                tlbp->valid = 1;
            }
    
        } /* end if(!TLB) */
    
        /* Set the protection indicator if page protection is active */
        if (pte & PAGETAB_PROT)
            protect = 1;
    
        /* [3.11.3.5] Combine the page frame real address with the byte
           index of the virtual address to form the real address */
        *raddr = (pte & PAGETAB_PFRA) | (vaddr & 0xFFF);

    }
#endif

    /* Set the private and protection indicators */
    if (private) *priv = 1;
    if (protect) *prot = 1;

    /* Set the segment table identification indication */
    *pstid = stid;

    /* Clear exception code and return with zero return code */
    *xcode = 0;
    return 0;

/* Conditions which always cause program check */
address_excp:
//    logmsg("dat.c: addressing exception: %8.8X %8.8X %4.4X %8.8X\n",
//        regs->cr[0],std,pte,vaddr);
    *xcode = PGM_ADDRESSING_EXCEPTION;
    goto tran_prog_check;

tran_spec_excp:
//    logmsg("dat.c: translation specification exception...\n");
//    logmsg("       cr0=%8.8X ste=%8.8X pte=%4.4X vaddr=%8.8X\n",
//        regs->cr[0],ste,pte,vaddr);
    *xcode = PGM_TRANSLATION_SPECIFICATION_EXCEPTION;
    goto tran_prog_check;

tran_prog_check:
    program_interrupt (regs, *xcode);

/* Conditions which the caller may or may not program check */
seg_tran_invalid:
    *xcode = PGM_SEGMENT_TRANSLATION_EXCEPTION;
    *raddr = sto;
    cc = 1;
    goto tran_excp_addr;

page_tran_invalid:
#ifdef FEATURE_EXPANDED_STORAGE
    /* If page is valid in expanded storage, and expanded storage
       block number is requested, return the block number and key */
    #if 0 /* Do not yet know how to find the block number */
    if ((pte & PAGETAB_ESVALID) && xpblk != NULL)
    {
        *xpblk = 0;
        *xpkey = pte & 0xFF;
        if (pte & PAGETAB_PROT)
            protect = 1;
        if (protect) *prot = 1;
        return 5;
    }
    #endif/* Do not yet know how to find the block number */
#endif /*FEATURE_EXPANDED_STORAGE*/
    *xcode = PGM_PAGE_TRANSLATION_EXCEPTION;
    *raddr = pto;
    cc = 2;
    goto tran_excp_addr;

page_tran_length:
    *xcode = PGM_PAGE_TRANSLATION_EXCEPTION;
    *raddr = pto;
    cc = 3;
    goto tran_excp_addr;

seg_tran_length:
    *xcode = PGM_SEGMENT_TRANSLATION_EXCEPTION;
    *raddr = sto;
    cc = 3;
    goto tran_excp_addr;

tran_alet_excp:
    regs->excarid = arn;
    return 4;

tran_excp_addr:
    /* Set the translation exception address */
    regs->tea = vaddr & TEA_EFFADDR;
    if ((std & STD_STO) != (regs->cr[1] & STD_STO))
    {
        if ((std & STD_STO) == (regs->cr[7] & STD_STO))
        {
            if (PRIMARY_SPACE_MODE(&regs->psw)
              || SECONDARY_SPACE_MODE(&regs->psw))
            {
                regs->tea |= TEA_SECADDR | TEA_ST_SECNDRY;
            } else {
                regs->tea |= TEA_ST_SECNDRY;
            }
        } else {
            if ((std & STD_STO) == (regs->cr[13] & STD_STO))
            {
                regs->tea |= TEA_ST_HOME;
            } else {
                regs->tea |= TEA_ST_ARMODE;
            }
        }
    }

    /* Set the exception access identification */
    if (ACCESS_REGISTER_MODE(&regs->psw)
#if defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)
      || (regs->sie_active 
        && (regs->guestregs->siebk->mx & SIE_MX_XC)
        && regs->guestregs->psw.armode)
#endif /*defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)*/
       )
       regs->excarid = (arn < 0 ? 0 : arn);

    /* Return condition code */
    return cc;

} /* end function translate_addr */
#endif

/*-------------------------------------------------------------------*/
/* Purge the translation lookaside buffer                            */
/*-------------------------------------------------------------------*/
void purge_tlb (REGS *regs)
{
    memset (regs->tlb, 0, sizeof(regs->tlb));
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    /* Also clear the guest registers in the SIE copy */
    if(regs->guestregs)
        memset (regs->guestregs->tlb, 0, sizeof(regs->guestregs->tlb));
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
} /* end function purge_tlb */

/*-------------------------------------------------------------------*/
/* Invalidate page table entry                                       */
/*                                                                   */
/* Input:                                                            */
/*      ibyte   0x21=IPTE instruction, 0x59=IESBE instruction        */
/*      r1      First operand register number                        */
/*      r2      Second operand register number                       */
/*      regs    CPU register context                                 */
/*                                                                   */
/*      This function is called by the IPTE and IESBE instructions.  */
/*      It sets the PAGETAB_INVALID bit (for IPTE) or resets the     */
/*      PAGETAB_ESVALID bit (for IESBE) in the page table entry      */
/*      addressed by the page table origin in the R1 register and    */
/*      the page index in the R2 register.  It clears the TLB of     */
/*      all entries whose PFRA matches the page table entry.         */
/*-------------------------------------------------------------------*/
void invalidate_pte (BYTE ibyte, int r1, int r2, REGS *regs)
{
#if MAX_CPU_ENGINES == 1 || defined(FEATURE_INTERPRETIVE_EXECUTION)
int     i;                              /* Array subscript           */
#endif /*MAX_CPU_ENGINES == 1 || defined(FEATURE_INTERPRETIVE_EXECUTION)*/
U32     raddr;                          /* Addr of page table entry  */
U32     pte;

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    /* Determine the machine type for */
    if(regs->sie_state && (regs->siebk->m & SIE_M_370))
#endif
#if defined(FEATURE_INTERPRETIVE_EXECUTION) || !defined(FEATURE_S390_DAT)
    {
        /* Program check if translation format is invalid */
        if ((((regs->cr[0] & CR0_PAGE_SIZE) != CR0_PAGE_SZ_2K) &&
           ((regs->cr[0] & CR0_PAGE_SIZE) != CR0_PAGE_SZ_4K)) ||
           (((regs->cr[0] & CR0_SEG_SIZE) != CR0_SEG_SZ_64K) &&
           ((regs->cr[0] & CR0_SEG_SIZE) != CR0_SEG_SZ_1M)))
            program_interrupt (regs, PGM_TRANSLATION_SPECIFICATION_EXCEPTION);

        /* Combine the page table origin in the R1 register with
           the page index in the R2 register, ignoring carry, to
           form the 31-bit real address of the page table entry */
        raddr = (regs->gpr[r1] & SEGTAB_370_PTO)
                    + (((regs->cr[0] & CR0_SEG_SIZE) == CR0_SEG_SZ_1M) ?
                      (((regs->cr[0] & CR0_PAGE_SIZE) == CR0_PAGE_SZ_4K) ?
                      ((regs->gpr[r2] & 0x000FF000) >> 11) :
                      ((regs->gpr[r2] & 0x000FF800) >> 10)) :
                      (((regs->cr[0] & CR0_PAGE_SIZE) == CR0_PAGE_SZ_4K) ?
                      ((regs->gpr[r2] & 0x0000F000) >> 11) :
                      ((regs->gpr[r2] & 0x0000F800) >> 10)));
        raddr &= 0x00FFFFFF;

        /* Fetch the page table entry from real storage, subject
           to normal storage protection mechanisms */
        pte = vfetch2 ( raddr, USE_REAL_ADDR, regs );
    
        /* Set the page invalid bit in the page table entry,
           again subject to storage protection mechansims */
// /*debug*/ logmsg("dat.c: IPTE issued for entry %4.4X at %8.8X...\n"
//                  "       page table %8.8X, page index %8.8X, cr0 %8.8X\n",
//                  pte, raddr, regs->gpr[r1], regs->gpr[r2], regs->cr[0]);
        if ((regs->cr[0] & CR0_PAGE_SIZE) == CR0_PAGE_SZ_2K)
            pte |= PAGETAB_INV_2K;
        else
            pte |= PAGETAB_INV_4K;
        vstore2 ( pte, raddr, USE_REAL_ADDR, regs );
    }
#endif
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    else
#endif
#if defined(FEATURE_INTERPRETIVE_EXECUTION) || defined(FEATURE_S390_DAT)
    {
        /* Program check if translation format is invalid */
        if ((regs->cr[0] & CR0_TRAN_FMT) != CR0_TRAN_ESA390)
            program_interrupt (regs, PGM_TRANSLATION_SPECIFICATION_EXCEPTION);

        /* Combine the page table origin in the R1 register with
           the page index in the R2 register, ignoring carry, to
           form the 31-bit real address of the page table entry */
        raddr = (regs->gpr[r1] & SEGTAB_PTO)
                    + ((regs->gpr[r2] & 0x000FF000) >> 10);
        raddr &= 0x7FFFFFFF;

        /* Fetch the page table entry from real storage, subject
           to normal storage protection mechanisms */
        pte = vfetch4 ( raddr, USE_REAL_ADDR, regs );

        /* Set the page invalid bit in the page table entry,
           again subject to storage protection mechansims */
        if(ibyte == 0x59)
            pte &= ~PAGETAB_ESVALID;
        else
            pte |= PAGETAB_INVALID;
        vstore4 ( pte, raddr, USE_REAL_ADDR, regs );
    }
#endif

#if MAX_CPU_ENGINES == 1 || defined(FEATURE_INTERPRETIVE_EXECUTION)
#if MAX_CPU_ENGINES > 1
    if(regs->sie_state && !regs->sie_scao)
#endif /*MAX_CPU_ENGINES > 1*/
        /* Clear the TLB of any entries with matching PFRA */
        for (i = 0; i < (sizeof(regs->tlb)/sizeof(TLBE)); i++)
        {
            if ((regs->tlb[i].pte & PAGETAB_PFRA) == (pte & PAGETAB_PFRA)
                && regs->tlb[i].valid)
            {
                regs->tlb[i].valid = 0;
// /*debug*/logmsg ("dat: TLB entry %d invalidated\n", i); /*debug*/
            }
        } /* end for(i) */
#if MAX_CPU_ENGINES > 1
    else
#endif /*MAX_CPU_ENGINES > 1*/
#endif /*MAX_CPU_ENGINES == 1 || defined(FEATURE_INTERPRETIVE_EXECUTION)*/
#if MAX_CPU_ENGINES > 1
    /* Signal each CPU to perform TLB invalidation.
       IPTE must not complete until all CPUs have indicated
       that they have cleared their TLB and have completed
       any storage accesses using the invalidated entries */

    /* This is a sledgehammer approach but clearing all tlb's seems
       to be the only viable alternative at the moment, short of
       building queues and waiting for all other cpu's to clear their
       entries - JJ 09/05/2000 */
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    if(regs->sie_state)
        synchronize_broadcast(regs->hostregs, &sysblk.brdcstptlb);
    else
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
        synchronize_broadcast(regs, &sysblk.brdcstptlb);
#endif /*MAX_CPU_ENGINES > 1*/

} /* end function invalidate_pte */

#ifndef INLINE_FETCH
/*-------------------------------------------------------------------*/
/* Convert logical address to absolute address and check protection  */
/*                                                                   */
/* Input:                                                            */
/*      addr    Logical address to be translated                     */
/*      arn     Access register number (or USE_REAL_ADDR,            */
/*                      USE_PRIMARY_SPACE, USE_SECONDARY_SPACE)      */
/*      regs    CPU register context                                 */
/*      acctype Type of access requested: READ, WRITE, or INSTFETCH  */
/*      akey    Bits 0-3=access key, 4-7=zeroes                      */
/* Returns:                                                          */
/*      Absolute storage address.                                    */
/*                                                                   */
/*      If the PSW indicates DAT-off, or if the access register      */
/*      number parameter is the special value USE_REAL_ADDR,         */
/*      then the addr parameter is treated as a real address.        */
/*      Otherwise addr is a virtual address, so dynamic address      */
/*      translation is called to convert it to a real address.       */
/*      Prefixing is then applied to convert the real address to     */
/*      an absolute address, and then low-address protection,        */
/*      access-list controlled protection, page protection, and      */
/*      key controlled protection checks are applied to the address. */
/*      If successful, the reference and change bits of the storage  */
/*      key are updated, and the absolute address is returned.       */
/*                                                                   */
/*      If the logical address causes an addressing, protection,     */
/*      or translation exception then a program check is generated   */
/*      and the function does not return.                            */
/*-------------------------------------------------------------------*/
U32 logical_to_abs (U32 addr, int arn, REGS *regs,
                    int acctype, BYTE akey)
{
U32     raddr;                          /* Real address              */
U32     aaddr;                          /* Absolute address          */
int     private = 0;                    /* 1=Private address space   */
int     protect = 0;                    /* 1=ALE or page protection  */
int     stid;                           /* Segment table indication  */
#ifdef FEATURE_INTERVAL_TIMER
PSA    *psa;                            /* -> Prefixed storage area  */
S32     itimer;                         /* Interval timer value      */
S32     olditimer;                      /* Previous interval timer   */
#endif /*FEATURE_INTERVAL_TIMER*/
U16     xcode;                          /* Exception code            */
#ifdef FEATURE_OPTIMIZE_SAME_PAGE
LASTPAGE *lastpage;

#ifndef CHECK_PAGEADDR
    if (acctype <=3)
    {
        lastpage = &regs->lastpage[acctype - 1];
        if (lastpage->vaddr == (addr & STORAGE_KEY_PAGEMASK) &&
            lastpage->arn == arn &&
            (lastpage->valid))
        {
            return (lastpage->aaddr + (addr & STORAGE_KEY_BYTEMASK));
        }
    }
#endif
#endif

    /* Convert logical address to real address */
    if ((REAL_MODE(&regs->psw) || arn == USE_REAL_ADDR)
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
      /* Under SIE guest real is always host primary, regardless 
         of the DAT mode */
      && !(regs->sie_active
#if !defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)
                            && arn == USE_PRIMARY_SPACE
#endif /*defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)*/
                                                        )
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
      )
        raddr = addr;
    else {
        if (translate_addr (addr, arn, regs, acctype, &raddr, &xcode,
                            &private, &protect, &stid, NULL, NULL))
            goto vabs_prog_check;
    }

    /* Convert real address to absolute address */
    aaddr = APPLY_PREFIXING (raddr, regs->pxr);

    /* Program check if absolute address is outside main storage */
    if (aaddr >= regs->mainsize)
        goto vabs_addr_excp;

#if defined(FEATURE_INTERVAL_TIMER)
    if(raddr < 88 && raddr >= 76)
    {
        /* Point to PSA in main storage */
        psa = (PSA*)(sysblk.mainstor + regs->pxr);

        /* Obtain the TOD clock update lock */
        obtain_lock (&sysblk.todlock);

        /* Decrement the location 80 timer */
        itimer = (S32)(((U32)(psa->inttimer[0]) << 24)
                            | ((U32)(psa->inttimer[1]) << 16)
                            | ((U32)(psa->inttimer[2]) << 8)
                            | (U32)(psa->inttimer[3]));
        olditimer = itimer--;
        psa->inttimer[0] = ((U32)itimer >> 24) & 0xFF;
        psa->inttimer[1] = ((U32)itimer >> 16) & 0xFF;
        psa->inttimer[2] = ((U32)itimer >> 8) & 0xFF;
        psa->inttimer[3] = (U32)itimer & 0xFF;

        /* Set interrupt flag and interval timer interrupt pending
           if the interval timer went from positive to negative */
        if (itimer < 0 && olditimer >= 0)
            regs->cpuint = regs->itimer_pending = 1;

        /* Release the TOD clock update lock */
        release_lock (&sysblk.todlock);

        /* Check for access to interval timer at location 80 */
        if (sysblk.insttrace || sysblk.inststep)
        {
            psa = (PSA*)(sysblk.mainstor + regs->pxr);
            logmsg ("dat.c: Interval timer accessed: "
                    "%2.2X%2.2X%2.2X%2.2X\n",
                    psa->inttimer[0], psa->inttimer[1],
                    psa->inttimer[2], psa->inttimer[3]);
        }
    }
#endif /*FEATURE_INTERVAL_TIMER*/

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    if(regs->sie_state  && !regs->sie_pref)
    {
    U32 sie_stid;
    U16 sie_xcode;
    int sie_private;

        if (translate_addr (regs->sie_mso + aaddr,
#if defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)
              ((regs->siebk->mx & SIE_MX_XC) && regs->psw.armode && arn > 0) ?
                arn :
#endif /*defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)*/
                USE_PRIMARY_SPACE,
                regs->hostregs, ACCTYPE_SIE, &aaddr, &sie_xcode,
                &sie_private, &protect, &sie_stid, NULL, NULL))
            program_interrupt (regs->hostregs, sie_xcode);

        /* Convert host real address to host absolute address */
        aaddr = APPLY_PREFIXING (aaddr, regs->hostregs->pxr);
    }

    /* Do not apply host key access when SIE fetches/stores data */
    if(regs->sie_active)
        akey = 0;
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

    /* Check protection and set reference and change bits */
    switch (acctype) {

    case ACCTYPE_READ:
    case ACCTYPE_INSTFETCH:
        /* Program check if fetch protected location */
        if (is_fetch_protected (addr, STORAGE_KEY(aaddr), akey,
                                private, regs))
            goto vabs_prot_excp;

        /* Set the reference bit in the storage key */
        STORAGE_KEY(aaddr) |= STORKEY_REF;
        break;

    case ACCTYPE_WRITE:
        /* Program check if store protected location */
        if (is_store_protected (addr, STORAGE_KEY(aaddr), akey,
                                private, protect, regs))
            goto vabs_prot_excp;

        /* Set the reference and change bits in the storage key */
        STORAGE_KEY(aaddr) |= (STORKEY_REF | STORKEY_CHANGE);
        break;
    } /* end switch */

#ifdef FEATURE_OPTIMIZE_SAME_PAGE

#ifdef CHECK_PAGEADDR
    if (acctype <=3)
    {
        lastpage = &regs->lastpage[acctype - 1];
        if (lastpage->vaddr == (addr & STORAGE_KEY_PAGEMASK) &&
            lastpage->arn == arn &&
            (lastpage->valid))
        {
           if (lastpage->aaddr != (aaddr & STORAGE_KEY_PAGEMASK))
              logmsg("WRONG PAGEADDR: %llu %x4 %x4 %x4 %4x %d %x %x\n",
                            regs->instcount, regs->psw.ia, 
                            addr, aaddr, lastpage->aaddr, arn,
                            regs->lastinst[0], regs->lastinst[1]); 
        }
    }
#endif

    if (acctype <=3)
    {
        lastpage = &regs->lastpage[acctype - 1];
        lastpage->vaddr = addr & STORAGE_KEY_PAGEMASK;
        lastpage->aaddr = aaddr & STORAGE_KEY_PAGEMASK;
        lastpage->arn = arn;
        lastpage->valid = 1;
    }
#endif
    /* Return the absolute address */
    return aaddr;

vabs_addr_excp:
    program_interrupt (regs, PGM_ADDRESSING_EXCEPTION);

vabs_prot_excp:
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
    regs->tea = addr & TEA_EFFADDR;
    if (protect && acctype == ACCTYPE_WRITE)
        regs->tea |= TEA_PROT_AP;
    regs->tea |= stid;
    regs->excarid = (arn > 0 ? arn : 0);
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
    program_interrupt (regs, PGM_PROTECTION_EXCEPTION);

vabs_prog_check:
    program_interrupt (regs, xcode);

    return -1; /* prevent warning from compiler */
} /* end function logical_to_abs */

/*-------------------------------------------------------------------*/
/* Store 1 to 256 characters into virtual storage operand            */
/*                                                                   */
/* Input:                                                            */
/*      src     1 to 256 byte input buffer                           */
/*      len     Size of operand minus 1                              */
/*      addr    Logical address of leftmost character of operand     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      range causes an addressing, translation, or protection       */
/*      exception, and in this case no real storage locations are    */
/*      updated, and the function does not return.                   */
/*-------------------------------------------------------------------*/
void vstorec (void *src, BYTE len, U32 addr, int arn, REGS *regs)
{
U32     addr2;                          /* Page address of last byte */
BYTE    len1;                           /* Length to end of page     */
BYTE    len2;                           /* Length after next page    */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Calculate page address of last byte of operand */
    addr2 = (addr + len) & ADDRESS_MAXWRAP(regs);
    addr2 &= STORAGE_KEY_PAGEMASK;

    /* Copy data to real storage in either one or two parts
       depending on whether operand crosses a page boundary */
    if (addr2 == (addr & STORAGE_KEY_PAGEMASK)) {
        addr = logical_to_abs (addr, arn, regs, ACCTYPE_WRITE, akey);
        memcpy (sysblk.mainstor+addr, src, len+1);
#ifdef IBUF
        FRAG_INVALIDATE(addr, len+1);
#endif
    } else {
        len1 = addr2 - addr;
        len2 = len - len1 + 1;
        addr = logical_to_abs (addr, arn, regs, ACCTYPE_WRITE, akey);
        addr2 = logical_to_abs (addr2, arn, regs, ACCTYPE_WRITE, akey);
        memcpy (sysblk.mainstor+addr, src, len1);
        memcpy (sysblk.mainstor+addr2, src+len1, len2);
    }
} /* end function vstorec */

/*-------------------------------------------------------------------*/
/* Store a single byte into virtual storage operand                  */
/*                                                                   */
/* Input:                                                            */
/*      value   Byte value to be stored                              */
/*      addr    Logical address of operand byte                      */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or protection             */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
void vstoreb (BYTE value, U32 addr, int arn, REGS *regs)
{
    addr = logical_to_abs (addr, arn, regs, ACCTYPE_WRITE,
                                regs->psw.pkey);
    sysblk.mainstor[addr] = value;
#ifdef IBUF
    FRAG_INVALIDATE(addr, 1);
#endif
} /* end function vstoreb */

/*-------------------------------------------------------------------*/
/* Store a two-byte integer into virtual storage operand             */
/*                                                                   */
/* Input:                                                            */
/*      value   16-bit integer value to be stored                    */
/*      addr    Logical address of leftmost operand byte             */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or protection             */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
void vstore2 (U16 value, U32 addr, int arn, REGS *regs)
{
U32     addr2;                          /* Address of second byte    */
U32     abs1, abs2;                     /* Absolute addresses        */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Calculate address of second byte of operand */
    addr2 = (addr + 1) & ADDRESS_MAXWRAP(regs);

    /* Get absolute address of first byte of operand */
    abs1 = logical_to_abs (addr, arn, regs, ACCTYPE_WRITE, akey);

    /* Repeat address translation if operand crosses a page boundary */
    if ((addr2 & STORAGE_KEY_BYTEMASK) == 0x000)
        abs2 = logical_to_abs (addr2, arn, regs, ACCTYPE_WRITE, akey);
    else
        abs2 = abs1 + 1;

    /* Store integer value at operand location */
    sysblk.mainstor[abs1] = value >> 8;
    sysblk.mainstor[abs2] = value & 0xFF;

#ifdef IBUF
    if ((abs1 + 1) == abs2)
        FRAG_INVALIDATE(abs1, 2)
    else
    {
        FRAG_INVALIDATE(abs1, 1);
        FRAG_INVALIDATE(abs2, 1);
    }
#endif

} /* end function vstore2 */

/*-------------------------------------------------------------------*/
/* Store a four-byte integer into virtual storage operand            */
/*                                                                   */
/* Input:                                                            */
/*      value   32-bit integer value to be stored                    */
/*      addr    Logical address of leftmost operand byte             */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or protection             */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
void vstore4 (U32 value, U32 addr, int arn, REGS *regs)
{
int     i;                              /* Loop counter              */
int     k;                              /* Shift counter             */
U32     addr2;                          /* Page address of last byte */
U32     abs, abs2;                      /* Absolute addresses        */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */
#ifdef IBUF
U32     habs;
U32     habs2;
BYTE    l1 = 0;
#endif

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Get absolute address of first byte of operand */
    abs = logical_to_abs (addr, arn, regs, ACCTYPE_WRITE, akey);

    /* Store 4 bytes when operand is fullword aligned */
    if ((addr & 0x03) == 0)
    {
        *((U32*)(sysblk.mainstor + abs)) = htonl(value);
#ifdef IBUF
        FRAG_INVALIDATE(abs, 4);
#endif
        return;
    }

    /* Operand is not fullword aligned and may cross a page boundary */

    /* Calculate page address of last byte of operand */
    addr2 = (addr + 3) & ADDRESS_MAXWRAP(regs);
    addr2 &= STORAGE_KEY_PAGEMASK;
    abs2 = logical_to_abs (addr2, arn, regs, ACCTYPE_WRITE, akey);

#ifdef IBUF
    habs = abs;
    habs2 = abs2;
#endif

    /* Store integer value byte by byte at operand location */
    for (i=0, k=24; i < 4; i++, k -= 8) {

        /* Store byte in absolute storage */
        sysblk.mainstor[abs] = (value >> k) & 0xFF;

        /* Increment absolute address and virtual address */
        abs++;
        addr++;
        addr &= ADDRESS_MAXWRAP(regs);

        /* Adjust absolute address if page boundary crossed */
        if (addr == addr2)
            abs = abs2;

    } /* end for */

#ifdef IBUF
    if ((habs & STORAGE_KEY_PAGEMASK) == (habs2 & STORAGE_KEY_PAGEMASK))
        FRAG_INVALIDATE(habs, 4)
    else
    {
        l1 = (habs & STORAGE_KEY_PAGEMASK) +
              STORAGE_KEY_PAGESIZE - habs;
        FRAG_INVALIDATE(habs, l1);
        FRAG_INVALIDATE(habs2, 4 - l1);
    }
#endif

} /* end function vstore4 */

/*-------------------------------------------------------------------*/
/* Store an eight-byte integer into virtual storage operand          */
/*                                                                   */
/* Input:                                                            */
/*      value   64-bit integer value to be stored                    */
/*      addr    Logical address of leftmost operand byte             */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or protection             */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
void vstore8 (U64 value, U32 addr, int arn, REGS *regs)
{
int     i;                              /* Loop counter              */
int     k;                              /* Shift counter             */
U32     addr2;                          /* Page address of last byte */
U32     abs, abs2;                      /* Absolute addresses        */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */
#ifdef IBUF
U32     habs;
U32     habs2;
BYTE    l1 = 0;
#endif

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Get absolute address of first byte of operand */
    abs = logical_to_abs (addr, arn, regs, ACCTYPE_WRITE, akey);

    /* Store 8 bytes when operand is doubleword aligned */
    if ((addr & 0x07) == 0) {
        sysblk.mainstor[abs] = (value >> 56) & 0xFF;
        sysblk.mainstor[abs+1] = (value >> 48) & 0xFF;
        sysblk.mainstor[abs+2] = (value >> 40) & 0xFF;
        sysblk.mainstor[abs+3] = (value >> 32) & 0xFF;
        sysblk.mainstor[abs+4] = (value >> 24) & 0xFF;
        sysblk.mainstor[abs+5] = (value >> 16) & 0xFF;
        sysblk.mainstor[abs+6] = (value >> 8) & 0xFF;
        sysblk.mainstor[abs+7] = value & 0xFF;
#ifdef IBUF
        FRAG_INVALIDATE(abs, 8);
#endif
        return;
    }

    /* Non-doubleword aligned operand may cross a page boundary */

    /* Calculate page address of last byte of operand */
    addr2 = (addr + 7) & ADDRESS_MAXWRAP(regs);
    addr2 &= STORAGE_KEY_PAGEMASK;
    abs2 = logical_to_abs (addr2, arn, regs, ACCTYPE_WRITE, akey);

#ifdef IBUF
    habs = abs;
    habs2 = abs2;
#endif

    /* Store integer value byte by byte at operand location */
    for (i=0, k=56; i < 8; i++, k -= 8) {

        /* Store byte in absolute storage */
        sysblk.mainstor[abs] = (value >> k) & 0xFF;

        /* Increment absolute address and virtual address */
        abs++;
        addr++;
        addr &= ADDRESS_MAXWRAP(regs);

        /* Adjust absolute address if page boundary crossed */
        if (addr == addr2)
            abs = abs2;

    } /* end for */

#ifdef IBUF
    if ((habs & STORAGE_KEY_PAGEMASK) == (habs2 & STORAGE_KEY_PAGEMASK))
        FRAG_INVALIDATE(habs, 8)
    else
    {
        l1 = (habs & STORAGE_KEY_PAGEMASK) +
              STORAGE_KEY_PAGESIZE - habs;
        FRAG_INVALIDATE(habs, l1);
        FRAG_INVALIDATE(habs2, 8 - l1);
    }
#endif

} /* end function vstore8 */

/*-------------------------------------------------------------------*/
/* Fetch a 1 to 256 character operand from virtual storage           */
/*                                                                   */
/* Input:                                                            */
/*      len     Size of operand minus 1                              */
/*      addr    Logical address of leftmost character of operand     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/* Output:                                                           */
/*      dest    1 to 256 byte output buffer                          */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
void vfetchc (void *dest, BYTE len, U32 addr, int arn, REGS *regs)
{
U32     addr2;                          /* Page address of last byte */
BYTE    len1;                           /* Length to end of page     */
BYTE    len2;                           /* Length after next page    */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Calculate page address of last byte of operand */
    addr2 = (addr + len) & ADDRESS_MAXWRAP(regs);
    addr2 &= STORAGE_KEY_PAGEMASK;

    /* Copy data from real storage in either one or two parts
       depending on whether operand crosses a page boundary */
    if (addr2 == (addr & STORAGE_KEY_PAGEMASK)) {
        addr = logical_to_abs (addr, arn, regs, ACCTYPE_READ, akey);
        memcpy (dest, sysblk.mainstor+addr, len+1);
    } else {
        len1 = addr2 - addr;
        len2 = len - len1 + 1;
        addr = logical_to_abs (addr, arn, regs, ACCTYPE_READ, akey);
        addr2 = logical_to_abs (addr2, arn, regs, ACCTYPE_READ, akey);
        memcpy (dest, sysblk.mainstor+addr, len1);
        memcpy (dest+len1, sysblk.mainstor+addr2, len2);
    }
} /* end function vfetchc */

/*-------------------------------------------------------------------*/
/* Fetch a single byte operand from virtual storage                  */
/*                                                                   */
/* Input:                                                            */
/*      addr    Logical address of operand character                 */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/* Returns:                                                          */
/*      Operand byte                                                 */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
BYTE vfetchb (U32 addr, int arn, REGS *regs)
{
    addr = logical_to_abs (addr, arn, regs, ACCTYPE_READ,
                                regs->psw.pkey);
    return sysblk.mainstor[addr];
} /* end function vfetchb */

/*-------------------------------------------------------------------*/
/* Fetch a two-byte integer operand from virtual storage             */
/*                                                                   */
/* Input:                                                            */
/*      addr    Logical address of leftmost byte of operand          */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/* Returns:                                                          */
/*      Operand in 16-bit integer format                             */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
U16 vfetch2 (U32 addr, int arn, REGS *regs)
{
U32     addr2;                          /* Address of second byte    */
U32     abs1, abs2;                     /* Absolute addresses        */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Calculate address of second byte of operand */
    addr2 = (addr + 1) & ADDRESS_MAXWRAP(regs);

    /* Get absolute address of first byte of operand */
    abs1 = logical_to_abs (addr, arn, regs, ACCTYPE_READ, akey);

    /* Repeat address translation if operand crosses a page boundary */
    if ((addr2 & STORAGE_KEY_BYTEMASK) == 0x000)
        abs2 = logical_to_abs (addr2, arn, regs, ACCTYPE_READ, akey);
    else
        abs2 = abs1 + 1;

    /* Return integer value of operand */
    return (sysblk.mainstor[abs1] << 8) | sysblk.mainstor[abs2];
} /* end function vfetch2 */

/*-------------------------------------------------------------------*/
/* Fetch a four-byte integer operand from virtual storage            */
/*                                                                   */
/* Input:                                                            */
/*      addr    Logical address of leftmost byte of operand          */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/* Returns:                                                          */
/*      Operand in 32-bit integer format                             */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
U32 vfetch4 (U32 addr, int arn, REGS *regs)
{
int     i;                              /* Loop counter              */
U32     value;                          /* Accumulated value         */
U32     addr2;                          /* Page address of last byte */
U32     abs, abs2;                      /* Absolute addresses        */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Get absolute address of first byte of operand */
    abs = logical_to_abs (addr, arn, regs, ACCTYPE_READ, akey);

    /* Fetch 4 bytes when operand is fullword aligned */
    if ((addr & 0x03) == 0)
        return ntohl(*((U32*)(sysblk.mainstor + abs)));

    /* Operand is not fullword aligned and may cross a page boundary */

    /* Calculate page address of last byte of operand */
    addr2 = (addr + 3) & ADDRESS_MAXWRAP(regs);
    addr2 &= STORAGE_KEY_PAGEMASK;
    abs2 = logical_to_abs (addr2, arn, regs, ACCTYPE_READ, akey);

    /* Fetch integer value byte by byte from operand location */
    for (i=0, value=0; i < 4; i++) {

        /* Fetch byte from absolute storage */
        value <<= 8;
        value |= sysblk.mainstor[abs];

        /* Increment absolute address and virtual address */
        abs++;
        addr++;
        addr &= ADDRESS_MAXWRAP(regs);

        /* Adjust absolute address if page boundary crossed */
        if (addr == addr2)
            abs = abs2;

    } /* end for */
    return value;

} /* end function vfetch4 */

/*-------------------------------------------------------------------*/
/* Fetch an eight-byte integer operand from virtual storage          */
/*                                                                   */
/* Input:                                                            */
/*      addr    Logical address of leftmost byte of operand          */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/* Returns:                                                          */
/*      Operand in 64-bit integer format                             */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
U64 vfetch8 (U32 addr, int arn, REGS *regs)
{
int     i;                              /* Loop counter              */
U64     value;                          /* Accumulated value         */
U32     addr2;                          /* Page address of last byte */
U32     abs, abs2;                      /* Absolute addresses        */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Get absolute address of first byte of operand */
    abs = logical_to_abs (addr, arn, regs, ACCTYPE_READ, akey);

    /* Fetch 8 bytes when operand is doubleword aligned */
    if ((addr & 0x07) == 0) {
        return ((U64)sysblk.mainstor[abs] << 56)
                | ((U64)sysblk.mainstor[abs+1] << 48)
                | ((U64)sysblk.mainstor[abs+2] << 40)
                | ((U64)sysblk.mainstor[abs+3] << 32)
                | ((U64)sysblk.mainstor[abs+4] << 24)
                | ((U64)sysblk.mainstor[abs+5] << 16)
                | ((U64)sysblk.mainstor[abs+6] << 8)
                | (U64)sysblk.mainstor[abs+7];
    }

    /* Non-doubleword aligned operand may cross a page boundary */

    /* Calculate page address of last byte of operand */
    addr2 = (addr + 7) & ADDRESS_MAXWRAP(regs);
    addr2 &= STORAGE_KEY_PAGEMASK;
    abs2 = logical_to_abs (addr2, arn, regs, ACCTYPE_READ, akey);

    /* Fetch integer value byte by byte from operand location */
    for (i=0, value=0; i < 8; i++) {

        /* Fetch byte from absolute storage */
        value <<= 8;
        value |= sysblk.mainstor[abs];

        /* Increment absolute address and virtual address */
        abs++;
        addr++;
        addr &= ADDRESS_MAXWRAP(regs);

        /* Adjust absolute address if page boundary crossed */
        if (addr == addr2)
            abs = abs2;

    } /* end for */
    return value;

} /* end function vfetch8 */

/*-------------------------------------------------------------------*/
/* Fetch instruction from halfword-aligned virtual storage location  */
/*                                                                   */
/* Input:                                                            */
/*      dest    Pointer to 6-byte area to receive instruction bytes  */
/*      addr    Logical address of leftmost instruction halfword     */
/*      regs    Pointer to the CPU register context                  */
/*                                                                   */
/* Output:                                                           */
/*      If successful, from one to three instruction halfwords will  */
/*      be fetched from main storage and stored into the 6-byte area */
/*      pointed to by dest.                                          */
/*                                                                   */
/*      A program check may be generated if the instruction address  */
/*      is odd, or causes an addressing or translation exception,    */
/*      and in this case the function does not return.               */
/*-------------------------------------------------------------------*/
void instfetch (BYTE *dest, U32 addr, REGS *regs)
{
U32     abs;                            /* Absolute storage address  */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Program check if instruction address is odd */
    if (addr & 0x01)
        program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Fetch six bytes if instruction cannot cross a page boundary */
    if ((addr & STORAGE_KEY_BYTEMASK) <= STORAGE_KEY_PAGESIZE - 6)
    {
        abs = logical_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH, akey);
#ifdef CHECK_FRAGPARMS
        regs->iaabs = abs;
#endif
        memcpy (dest, sysblk.mainstor+abs, 6);
        return;
    }

    /* Fetch first two bytes of instruction */
    abs = logical_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH, akey);
#ifdef CHECK_FRAGPARMS
    regs->iaabs = abs;
#endif
    memcpy (dest, sysblk.mainstor+abs, 2);

    /* Return if two-byte instruction */
    if (dest[0] < 0x40) return;

    /* Fetch next two bytes of instruction */
    abs += 2;
    addr += 2;
    addr &= ADDRESS_MAXWRAP(regs);
    if ((addr & STORAGE_KEY_BYTEMASK) == 0x000) {
        abs = logical_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH, akey);
    }
    memcpy (dest+2, sysblk.mainstor+abs, 2);

    /* Return if four-byte instruction */
    if (dest[0] < 0xC0) return;

    /* Fetch next two bytes of instruction */
    abs += 2;
    addr += 2;
    addr &= ADDRESS_MAXWRAP(regs);
    if ((addr & STORAGE_KEY_BYTEMASK) == 0x000) {
        abs = logical_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH, akey);
    }
    memcpy (dest+4, sysblk.mainstor+abs, 2);

} /* end function instfetch */

#else
/*-------------------------------------------------------------------*/
/* Fetch a single byte operand from virtual storage                  */
/*                                                                   */
/* Input:                                                            */
/*      addr    Logical address of operand character                 */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/* Returns:                                                          */
/*      Operand byte                                                 */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
BYTE xvfetchb (U32 addr, int arn, REGS *regs)
{
    return(vfetchb(addr, arn, regs));
}
/*-------------------------------------------------------------------*/
/* Fetch a two-byte integer operand from virtual storage             */
/*                                                                   */
/* Input:                                                            */
/*      addr    Logical address of leftmost byte of operand          */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/* Returns:                                                          */
/*      Operand in 16-bit integer format                             */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
U16 xvfetch2 (U32 addr, int arn, REGS *regs)

{
    return(vfetch2(addr, arn, regs));
}

/*-------------------------------------------------------------------*/
/* Fetch a four-byte integer operand from virtual storage            */
/*                                                                   */
/* Input:                                                            */
/*      addr    Logical address of leftmost byte of operand          */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/* Returns:                                                          */
/*      Operand in 32-bit integer format                             */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
U32 xvfetch4 (U32 addr, int arn, REGS *regs)
{
    return(vfetch4(addr, arn, regs));
}

/*-------------------------------------------------------------------*/
/* Fetch an eight-byte integer operand from virtual storage          */
/*                                                                   */
/* Input:                                                            */
/*      addr    Logical address of leftmost byte of operand          */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/* Returns:                                                          */
/*      Operand in 64-bit integer format                             */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or fetch protection       */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
U64 xvfetch8 (U32 addr, int arn, REGS *regs)
{
    return(vfetch8(addr, arn, regs));
}
/*-------------------------------------------------------------------*/
/* Store a four-byte integer into virtual storage operand            */
/*                                                                   */
/* Input:                                                            */
/*      value   32-bit integer value to be stored                    */
/*      addr    Logical address of leftmost operand byte             */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or protection             */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
void xvstore4 (U32 value, U32 addr, int arn, REGS *regs)
{
    vstore4(value, addr, arn, regs);
}
/*-------------------------------------------------------------------*/
/* Store an eight-byte integer into virtual storage operand          */
/*                                                                   */
/* Input:                                                            */
/*      value   64-bit integer value to be stored                    */
/*      addr    Logical address of leftmost operand byte             */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*                                                                   */
/*      A program check may be generated if the logical address      */
/*      causes an addressing, translation, or protection             */
/*      exception, and in this case the function does not return.    */
/*-------------------------------------------------------------------*/
void xvstore8 (U64 value, U32 addr, int arn, REGS *regs)
{
    vstore8(value, addr, arn, regs);
}
#endif

/*-------------------------------------------------------------------*/
/* Move characters using specified keys and address spaces           */
/*                                                                   */
/* Input:                                                            */
/*      addr1   Effective address of first operand                   */
/*      arn1    Access register number for first operand,            */
/*              or USE_PRIMARY_SPACE or USE_SECONDARY_SPACE          */
/*      key1    Bits 0-3=first operand access key, 4-7=zeroes        */
/*      addr2   Effective address of second operand                  */
/*      arn2    Access register number for second operand,           */
/*              or USE_PRIMARY_SPACE or USE_SECONDARY_SPACE          */
/*      key2    Bits 0-3=second operand access key, 4-7=zeroes       */
/*      len     Operand length minus 1 (range 0-255)                 */
/*      regs    Pointer to the CPU register context                  */
/*                                                                   */
/*      This function implements the MVC, MVCP, MVCS, MVCK, MVCSK,   */
/*      and MVCDK instructions.  These instructions move up to 256   */
/*      characters using the address space and key specified by      */
/*      the caller for each operand.  Operands are moved byte by     */
/*      byte to ensure correct processing of overlapping operands.   */
/*                                                                   */
/*      The arn parameter for each operand may be an access          */
/*      register number, in which case the operand is in the         */
/*      primary, secondary, or home space, or in the space           */
/*      designated by the specified access register, according to    */
/*      the current PSW addressing mode.                             */
/*                                                                   */
/*      Alternatively the arn parameter may be one of the special    */
/*      values USE_PRIMARY_SPACE or USE_SECONDARY_SPACE in which     */
/*      case the operand is in the specified space regardless of     */
/*      the current PSW addressing mode.                             */
/*                                                                   */
/*      A program check may be generated if either logical address   */
/*      causes an addressing, protection, or translation exception,  */
/*      and in this case the function does not return.               */
/*-------------------------------------------------------------------*/
void move_chars (U32 addr1, int arn1, BYTE key1, U32 addr2,
                int arn2, BYTE key2, int len, REGS *regs)
{
U32     abs1, abs2;                     /* Absolute addresses        */
U32     npv1, npv2;                     /* Next page virtual addrs   */
U32     npa1 = 0, npa2 = 0;             /* Next page absolute addrs  */
int     i;                              /* Loop counter              */
BYTE    obyte;                          /* Operand byte              */

    /* Translate addresses of leftmost operand bytes */
    abs1 = logical_to_abs (addr1, arn1, regs, ACCTYPE_WRITE, key1);
    abs2 = logical_to_abs (addr2, arn2, regs, ACCTYPE_READ, key2);

    /* Calculate page addresses of rightmost operand bytes */
    npv1 = (addr1 + len) & ADDRESS_MAXWRAP(regs);
    npv1 &= STORAGE_KEY_PAGEMASK;
    npv2 = (addr2 + len) & ADDRESS_MAXWRAP(regs);
    npv2 &= STORAGE_KEY_PAGEMASK;

    /* Translate next page addresses if page boundary crossed */
    if (npv1 != (addr1 & STORAGE_KEY_PAGEMASK))
        npa1 = logical_to_abs (npv1, arn1, regs, ACCTYPE_WRITE, key1);
    if (npv2 != (addr2 & STORAGE_KEY_PAGEMASK))
        npa2 = logical_to_abs (npv2, arn2, regs, ACCTYPE_READ, key2);

    /* Process operands from left to right */
    for ( i = 0; i < len+1; i++ )
    {
        /* Fetch a byte from the source operand */
        obyte = sysblk.mainstor[abs2];

        /* Store the byte in the destination operand */
        sysblk.mainstor[abs1] = obyte;

        /* Increment first operand address */
        addr1++;
        addr1 &= ADDRESS_MAXWRAP(regs);
        abs1++;

        /* Adjust absolute address if page boundary crossed */
        if ((addr1 & STORAGE_KEY_BYTEMASK) == 0x000)
            abs1 = npa1;

        /* Increment second operand address */
        addr2++;
        addr2 &= ADDRESS_MAXWRAP(regs);
        abs2++;

        /* Adjust absolute address if page boundary crossed */
        if ((addr2 & STORAGE_KEY_BYTEMASK) == 0x000)
            abs2 = npa2;

    } /* end for(i) */

} /* end function move_chars */


/*-------------------------------------------------------------------*/
/* Validate operand                                                  */
/*                                                                   */
/* Input:                                                            */
/*      addr    Effective address of operand                         */
/*      arn     Access register number                               */
/*      len     Operand length minus 1 (range 0-255)                 */
/*      acctype Type of access requested: READ or WRITE              */
/*      regs    Pointer to the CPU register context                  */
/*                                                                   */
/*      The purpose of this function is to allow an instruction      */
/*      operand to be validated for addressing, protection, and      */
/*      translation exceptions, thus allowing the instruction to     */
/*      be nullified or suppressed before any updates occur.         */
/*                                                                   */
/*      A program check is generated if the operand causes an        */
/*      addressing, protection, or translation exception, and        */
/*      in this case the function does not return.                   */
/*-------------------------------------------------------------------*/
void validate_operand (U32 addr, int arn, int len,
                int acctype, REGS *regs)
{
U32     npv;                            /* Next page virtual address */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Translate address of leftmost operand byte */
    logical_to_abs (addr, arn, regs, acctype, akey);

    /* Calculate page address of rightmost operand byte */
    npv = (addr + len) & ADDRESS_MAXWRAP(regs);
    npv &= STORAGE_KEY_PAGEMASK;

    /* Translate next page address if page boundary crossed */
    if (npv != (addr & STORAGE_KEY_PAGEMASK))
        logical_to_abs (npv, arn, regs, acctype, akey);

} /* end function validate_operand */

