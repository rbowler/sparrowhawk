/* DAT.C        (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Dynamic Address Translation                  */

/*-------------------------------------------------------------------*/
/* This module implements the DAT, ALET, and ASN translation         */
/* functions of the ESA/390 architecture, described in the manual    */
/* SA22-7201-04 ESA/390 Principles of Operation.  The numbers in     */
/* square brackets in the comments refer to sections in the manual.  */
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

    i = *((U32*)(sysblk.mainstor + addr));
    return ntohl(i);
} /* end function fetch_fullword_absolute */

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
int     numwords;                       /* ASTE size (16 or 64 words)*/
int     i;                              /* Array subscript           */

    /* [3.9.3.1] Use the AFX to obtain the real address of the AFTE */
    afte_addr = (regs->cr[14] & CR14_AFTO) << 12;
    afte_addr += (asn & ASN_AFX) >> 4;

    /* Addressing exception if AFTE is outside main storage */
    if (afte_addr >= sysblk.mainsize)
        goto asn_addr_excp;

    /* Load the AFTE from main storage. All four bytes must be
       fetched concurrently as observed by other CPUs */
    afte_addr = APPLY_PREFIXING (afte_addr, regs->pxr);
    afte = fetch_fullword_absolute (afte_addr);

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
    if (aste_addr >= sysblk.mainsize)
        goto asn_addr_excp;

    /* Return the real address of the ASTE */
    *asteo = aste_addr;

    /* Fetch the 16- or 64-byte ASN second table entry from real
       storage.  Each fullword of the ASTE must be fetched
       concurrently as observed by other CPUs */
    aste_addr = APPLY_PREFIXING (aste_addr, regs->pxr);
    for (i = 0; i < numwords; i++)
    {
        aste[i] = fetch_fullword_absolute (aste_addr);
        aste_addr += 4;
    }

    /* Check the ASX invalid bit in the ASTE */
    if (aste[0] & ASTE0_INVALID)
        goto asn_asx_tran_excp;

    /* Check the reserved bits in first two words of ASTE */
    if ((aste[0] & ASTE0_RESV) || (aste[1] & ASTE1_RESV)
        || ((aste[0] & ASTE0_BASE)
#ifdef FEATURE_SUBSPACE_GROUP
            && (regs->cr[0] & CR0_ASF)
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
    program_check (code);
    return code;

/* Conditions which the caller may or may not program check */
asn_afx_tran_excp:
    code = PGM_AFX_TRANSLATION_EXCEPTION;
    return code;

asn_asx_tran_excp:
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
    if (ato >= sysblk.mainsize)
        goto auth_addr_excp;

    /* Load the byte containing the authority table entry
       and shift the entry into the leftmost 2 bits */
    ato = APPLY_PREFIXING (ato, regs->pxr);
    ate = sysblk.mainstor[ato];
    ate <<= ((ax & 0x03)*2);

    /* Authorization fails if the specified bit (either X'80' or
       X'40' of the 2 bit authority table entry) is zero */
    if ((ate & atemask) == 0)
        return 1;

    /* Exit with successful return code */
    return 0;

auth_addr_excp:
    program_check (PGM_ADDRESSING_EXCEPTION);
    return 1;
} /* end function authorize_asn */

/*-------------------------------------------------------------------*/
/* Translate ALET value to produce segment table designation         */
/*                                                                   */
/* Input:                                                            */
/*      arn     Access register number containing ALET (AR0 is       */
/*              treated as containing ALET value 0 except when       */
/*              processing the Test Access instruction in which      */
/*              case the actual value in AR0 is used)                */
/*      eax     The authorization index (normally obtained from      */
/*              CR8, but obtained from R2 for Test Access)           */
/*      regs    Pointer to the CPU register context                  */
/*      acctype Type of access requested: READ, WRITE, INSTFETCH,    */
/*              TAR, LRA, or TPROT                                   */
/*      stdptr  Pointer to word to receive segment table designation */
/*      prot    Pointer to field to receive protection indicator     */
/*                                                                   */
/* Output:                                                           */
/*      If successful, the segment table designation corresponding   */
/*      to the ALET value will be stored into the word pointed to    */
/*      by stdptr, and the return value is zero; the protection      */
/*      indicator is set to 1 if access-list controlled protection   */
/*      was detected, otherwise it remains unchanged.                */
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
U16 translate_alet (int arn, U16 eax, REGS *regs, int acctype,
                    U32 *stdptr, int *prot)
{
U32     std;                            /* Segment table designation */
U32     alet;                           /* Address list entry token  */
U32     cb;                             /* DUCT or PASTE address     */
U32     ald;                            /* Access-list designation   */
U32     alo;                            /* Access-list origin        */
int     all;                            /* Access-list length        */
U32     ale[4];                         /* Access-list entry         */
U32     aste_addr;                      /* Address of ASTE           */
U32     aste[16];                       /* ASN second table entry    */
int     i;                              /* Array subscript           */
int     code;                           /* Exception code            */

    /* [5.8.4.1] Select the access-list-entry token */
    alet = (arn == 0 && acctype != ACCTYPE_TAR) ? 0 :
                regs->ar[arn];

    /* Use the ALET to determine the segment table origin */
    switch (alet) {

    case ALET_PRIMARY:
        /* [5.8.4.2] Obtain the primary segment table designation */
        std = regs->cr[1];
        break;

    case ALET_SECONDARY:
        /* [5.8.4.2] Obtain the secondary segment table designation */
        std = regs->cr[7];
        break;

    default:
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
        if (cb >= sysblk.mainsize)
            goto alet_addr_excp;

        /* Load the effective access-list designation (ALD) from
           offset 16 in the control block.  All four bytes must be
           fetched concurrently as observed by other CPUs */
        cb = APPLY_PREFIXING (cb, regs->pxr);
        ald = fetch_fullword_absolute (cb+16);

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
        if (alo >= sysblk.mainsize)
            goto alet_addr_excp;

        /* Fetch the 16-byte access list entry from absolute storage.
           Each fullword of the ALE must be fetched concurrently as
           observed by other CPUs */
        alo = APPLY_PREFIXING (alo, regs->pxr);
        for (i = 0; i < 4; i++)
        {
            ale[i] = fetch_fullword_absolute (alo);
            alo += 4;
        }

        /* Check the ALEN invalid bit in the ALE */
        if (ale[0] & ALE0_INVALID)
            goto alen_tran_excp;

        /* Compare the ALE sequence number with the ALET */
        if ((ale[0] & ALE0_ALESN) != (alet & ALET_ALESN))
            goto ale_seq_excp;

        /* [5.8.4.6] Locate the ASN-second-table entry */
        aste_addr = ale[2] & ALE2_ASTE;

        /* Addressing exception if ASTE is outside real storage */
        if (aste_addr >= sysblk.mainsize)
            goto alet_addr_excp;

        /* Fetch the 64-byte ASN second table entry from real storage.
           Each fullword of the ASTE must be fetched concurrently as
           observed by other CPUs */
        aste_addr = APPLY_PREFIXING (aste_addr, regs->pxr);
        for (i = 0; i < 16; i++)
        {
            aste[i] = fetch_fullword_absolute (aste_addr);
            aste_addr += 4;
        }

        /* Check the ASX invalid bit in the ASTE */
        if (aste[0] & ASTE0_INVALID)
            goto aste_vald_excp;

        /* Compare the ASTE sequence number with the ALE */
        if ((aste[5] & ASTE5_ASTESN) != (ale[3] & ALE3_ASTESN))
            goto aste_seq_excp;

        /* [5.8.4.7] Authorize the use of the access-list entry */

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
                        && (regs->cr[0] & CR0_ASF)
#endif /*FEATURE_SUBSPACE_GROUP*/
                    ))
                goto alet_asn_tran_spec_excp;

            /* Perform extended authorization */
            if (authorize_asn(eax, aste, ATE_SECONDARY, regs) != 0)
                goto ext_auth_excp;
        }

        /* [5.8.4.8] Check for access-list controlled protection */
        if (ale[0] & ALE0_FETCHONLY)
            *prot = 1;

        /* [5.8.4.9] Obtain the STD from word 2 of the ASTE */
        std = aste[2];

    } /* end switch(alet) */

    /* Set segment table designation and return exception code zero */
    *stdptr = std;
    return 0;

/* Conditions which always cause program check */
alet_addr_excp:
    code = PGM_ADDRESSING_EXCEPTION;
    goto alet_prog_check;

alet_asn_tran_spec_excp:
    code = PGM_ASN_TRANSLATION_SPECIFICATION_EXCEPTION;
    goto alet_prog_check;

alet_prog_check:
    program_check (code);
    return code;

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
void purge_alb (void)
{
} /* end function purge_alb */

/*-------------------------------------------------------------------*/
/* Translate a 31-bit virtual address to a real address              */
/*                                                                   */
/* Input:                                                            */
/*      vaddr   31-bit virtual address to be translated              */
/*      arn     Access register number containing ALET (AR0 is       */
/*              treated as containing ALET value 0)                  */
/*      regs    Pointer to the CPU register context                  */
/*      acctype Type of access requested: READ, WRITE, INSTFETCH,    */
/*              LRA, or TPROT                                        */
/*      raddr   Pointer to field to receive real address             */
/*      xcode   Pointer to field to receive exception code           */
/*      priv    Pointer to field to receive private indicator        */
/*      prot    Pointer to field to receive protection indicator     */
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
/*                                                                   */
/*      The private indicator is set to 1 if translation was         */
/*      successful and the STD indicates a private address space;    */
/*      otherwise it remains unchanged.                              */
/*                                                                   */
/*      The protection indicator is set to 1 if translation was      */
/*      successful and either access-list controlled protection or   */
/*      page protection is active; otherwise it remains unchanged.   */
/*                                                                   */
/*      A program check may be generated for addressing and          */
/*      translation specification exceptions, in which case the      */
/*      function does not return.                                    */
/*-------------------------------------------------------------------*/
int translate_addr (U32 vaddr, int arn, REGS *regs, int acctype,
                    U32 *raddr, U16 *xcode, int *priv, int *prot)
{
U32     std;                            /* Segment table descriptor  */
U32     sto;                            /* Segment table origin      */
int     stl;                            /* Segment table length      */
int     private;                        /* 1=Private address space   */
int     protect = 0;                    /* 1=ALE or page protection  */
U32     ste;                            /* Segment table entry       */
U32     pto;                            /* Page table origin         */
int     ptl;                            /* Page table length         */
U32     pte;                            /* Page table entry          */

    /* [3.11.3.1] Load the effective segment table descriptor */
    if (acctype == ACCTYPE_INSTFETCH)
        std = (HOME_SPACE_MODE(&regs->psw)) ? regs->cr[13] :
            regs->cr[1];
    else if (PRIMARY_SPACE_MODE(&regs->psw))
        std = regs->cr[1];
    else if (SECONDARY_SPACE_MODE(&regs->psw))
        std = regs->cr[7];
    else if (HOME_SPACE_MODE(&regs->psw))
        std = regs->cr[13];
    else /* ACCESS_REGISTER_MODE */
    {
        *xcode = translate_alet (arn, ((regs->cr[8] & CR8_EAX) >> 16),
                                regs, acctype, &std, &protect);
        if (*xcode != 0)
            goto tran_alet_excp;
    }

    /* Extract fields from segment table descriptor */
    sto = std & STD_STO;
    stl = std & STD_STL;
    private = std & STD_PRIVATE;

    /* [3.11.3.2] Check the translation format bits in CR0 */
    if ((regs->cr[0] & CR0_TRAN_FMT) != CR0_TRAN_ESA390)
        goto tran_spec_excp;

    /* [3.11.4] Look up the address in the TLB */
    /*INCOMPLETE*/
    /* [10.17] Do not use TLB if processing LRA instruction */
    /*INCOMPLETE*/

    /* [3.11.3.3] Segment table lookup */

    /* Calculate the real address of the segment table entry */
    sto += (vaddr & 0x7FF00000) >> 18;

    /* Generate addressing exception if outside real storage */
    if (sto >= sysblk.mainsize)
        goto address_excp;

    /* Check that the virtual address is within the segment table */
    if ((vaddr >> 24) > stl)
        goto seg_tran_length;

    /* Fetch the segment table entry from real storage.  All four
       bytes must be fetched concurrently as observed by other CPUs */
    sto = APPLY_PREFIXING (sto, regs->pxr);
    ste = fetch_fullword_absolute (sto);

    /* Generate segment translation exception if segment invalid */
    if (ste & SEGTAB_INVALID)
        goto seg_tran_invalid;

    /* Check that all the reserved bits in the STE are zero */
    if (ste & SEGTAB_RESV)
        goto tran_spec_excp;

    /* If the segment table origin register indicates a private
       address space then the STE must not indicate a common segment */
    if (private && (ste & (SEGTAB_COMMON)))
        goto tran_spec_excp;

    /* Isolate page table origin and length */
    pto = ste & SEGTAB_PTO;
    ptl = ste & SEGTAB_PTL;

    /* [3.11.3.4] Page table lookup */

    /* Calculate the real address of the page table entry */
    pto += (vaddr & 0x000FF000) >> 10;

    /* Generate addressing exception if outside real storage */
    if (pto >= sysblk.mainsize)
        goto address_excp;

    /* Check that the virtual address is within the page table */
    if (((vaddr & 0x000FF000) >> 16) > ptl)
        goto page_tran_length;

    /* Fetch the page table entry from real storage.  All four
       bytes must be fetched concurrently as observed by other CPUs */
    pto = APPLY_PREFIXING (pto, regs->pxr);
    pte = fetch_fullword_absolute (pto);

    /* Generate page translation exception if page invalid */
    if (pte & PAGETAB_INVALID)
        goto page_tran_invalid;

    /* Check that all the reserved bits in the PTE are zero */
    if (pte & PAGETAB_RESV)
        goto tran_spec_excp;

    /* Set the protection indicator if page protection is active */
    if (pte & PAGETAB_PROT)
        protect = 1;

    /* [3.11.3.5] Combine the page frame real address with the byte
       index of the virtual address to form the real address */
    *raddr = (pte & PAGETAB_PFRA) | (vaddr & 0xFFF);

    /* Set the private and protection indicators */
    if (private) *priv = 1;
    if (protect) *prot = 1;

    /* Clear exception code and return with zero return code */
    *xcode = 0;
    return 0;

/* Conditions which always cause program check */
address_excp:
    *xcode = PGM_ADDRESSING_EXCEPTION;
    goto tran_prog_check;

tran_spec_excp:
    *xcode = PGM_TRANSLATION_SPECIFICATION_EXCEPTION;
    goto tran_prog_check;

tran_prog_check:
    program_check (*xcode);
    return 4;

/* Conditions which the caller may or may not program check */
seg_tran_invalid:
    *xcode = PGM_SEGMENT_TRANSLATION_EXCEPTION;
    *raddr = sto;
    return 1;

page_tran_invalid:
    *xcode = PGM_PAGE_TRANSLATION_EXCEPTION;
    *raddr = pto;
    return 2;

page_tran_length:
    *xcode = PGM_PAGE_TRANSLATION_EXCEPTION;
    *raddr = pto;
    return 3;

seg_tran_length:
    *xcode = PGM_SEGMENT_TRANSLATION_EXCEPTION;
    *raddr = sto;
    return 3;

tran_alet_excp:
    return 4;

} /* end function translate_addr */

/*-------------------------------------------------------------------*/
/* Purge the translation lookaside buffer                            */
/*-------------------------------------------------------------------*/
void purge_tlb (void)
{
} /* end function purge_tlb */

/*-------------------------------------------------------------------*/
/* Convert virtual to absolute and enforce addressing and protection */
/*-------------------------------------------------------------------*/
static U32 virt_to_abs (U32 vaddr, int arn, REGS *regs, int acctype)
{
int     rc;                             /* Return code               */
U32     raddr;                          /* Real address              */
U32     aaddr;                          /* Absolute address          */
int     key;                            /* Protection key            */
U32     block;                          /* 4K block number           */
int     private = 0;                    /* 1=Private address space   */
int     protect = 0;                    /* 1=ALE or page protection  */
int     fetch_override;                 /* 1=Fetch protect override  */
PSA    *psa;                            /* -> Prefixed storage area  */
U16     code;                           /* Exception code            */

    /* Point to the PSA in main storage */
    psa = (PSA*)(sysblk.mainstor + regs->pxr);

    /* Convert virtual address to real address */
    if (REAL_MODE(&regs->psw)) raddr = vaddr;
    else {
        rc = translate_addr (vaddr, arn, regs, acctype, &raddr, &code,
                            &private, &protect);
        if (rc != 0)
            goto vabs_prog_check;
    }

    /* [3.4.4] Enforce low-address protection for stores into
       locations 0-511 of non-private address spaces if bit 3 of
       control register 0 is set */
    if (acctype == ACCTYPE_WRITE && vaddr < 512
        && (regs->cr[0] & CR0_LOW_PROT)
        && private == 0)
        goto vabs_prot_excp;

    /* Enforce access-list controlled protection and page protection */
    if (acctype == ACCTYPE_WRITE && protect)
        goto vabs_prot_excp;

    /* Convert real address to absolute address */
    aaddr = APPLY_PREFIXING (raddr, regs->pxr);

    /* Program check if absolute address is outside main storage */
    if (aaddr >= sysblk.mainsize)
        goto vabs_addr_excp;

    /* Check key and set reference and change bits */
    key = regs->psw.key << 4;
    block = aaddr >> 12;
    switch (acctype) {

    case ACCTYPE_READ:
    case ACCTYPE_INSTFETCH:
        /* [3.4.1.2] Check for fetch protection override */
        fetch_override = (vaddr < 2048
                        && (regs->cr[0] & CR0_FETCH_OVRD)
                        && private == 0);

        /* [3.4.1] Check for fetch protection */
        if (key != 0 && fetch_override == 0
            && (sysblk.storkeys[block] & STORKEY_FETCH)
            && (sysblk.storkeys[block] & STORKEY_KEY) != key)
            goto vabs_prot_excp;

        /* Set the reference bit in the storage key */
        sysblk.storkeys[block] |= STORKEY_REF;
        break;

    case ACCTYPE_WRITE:
        /* [3.4.1] Check for store protection */
        if (key != 0
#ifdef FEATURE_STORAGE_PROTECTION_OVERRIDE
            /* [3.4.1.1] Ignore protection if storage key is 9 */
            && ((regs->cr[0] & CR0_STORE_OVRD) == 0
                || (sysblk.storkeys[block] & STORKEY_KEY) != 0x90)
#endif /*FEATURE_STORAGE_PROTECTION_OVERRIDE*/
            && (sysblk.storkeys[block] & STORKEY_KEY) != key)
            goto vabs_prot_excp;
        /* Set the reference and change bits in the storage key */
        sysblk.storkeys[block] |= (STORKEY_REF | STORKEY_CHANGE);
        break;
    } /* end switch */

    /* Return the absolute address */
    return aaddr;

vabs_addr_excp:
    program_check (PGM_ADDRESSING_EXCEPTION);
    return 0;

vabs_prot_excp:
    program_check (PGM_PROTECTION_EXCEPTION);
    return 0;

vabs_prog_check:
    if (code == PGM_ALEN_TRANSLATION_EXCEPTION
        || code == PGM_ALE_SEQUENCE_EXCEPTION
        || code == PGM_ASTE_VALIDITY_EXCEPTION
        || code == PGM_ASTE_SEQUENCE_EXCEPTION
        || code == PGM_EXTENDED_AUTHORITY_EXCEPTION)
        /* Store the access register number at PSA+160 */
        psa->excarid = arn;

    if (code == PGM_PAGE_TRANSLATION_EXCEPTION
        || code == PGM_SEGMENT_TRANSLATION_EXCEPTION)
    {
        /* Store the access register number at PSA+160 */
        psa->excarid = (acctype == ACCTYPE_INSTFETCH) ? 0 : arn;

        /* Store the translation exception address at PSA+144 */
        psa->tea[0] = (vaddr & 0x7F000000) >> 24;
        psa->tea[1] = (vaddr & 0xFF0000) >> 16;
        psa->tea[2] = (vaddr & 0xF000) >> 8;
        psa->tea[3] = (regs->psw.space << 1) | regs->psw.armode;
        if (SECONDARY_SPACE_MODE(&regs->psw)
            && acctype != ACCTYPE_INSTFETCH)
            psa->tea[0] |= 0x80;
    }
    program_check (code);
    return 0;

} /* end function virt_to_abs */

/*-------------------------------------------------------------------*/
/* Store an integer value in virtual storage.  The value is stored   */
/* byte by byte because otherwise the Intel processor would store    */
/* the bytes in reverse order.                                       */
/*-------------------------------------------------------------------*/
void vstorei (U64 value, int len, U32 addr, int arn, REGS *regs)
{
U32 abs = 0;                            /* Absolute storage location */
int tran = 1;                           /* 1=Translation required    */

    /* Start from last byte of operand */
    addr += len - 1;
    addr &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

    /* Store bytes in absolute storage from right to left */
    while (len-- > 0) {

        /* Translate virtual address to absolute address */
        if (tran) {
            abs = virt_to_abs (addr, arn, regs, ACCTYPE_WRITE);
        }

        /* Store byte in absolute storage */
        sysblk.mainstor[abs] = value & 0xFF;
        value >>= 8;

        /* Decrement absolute address and virtual address */
        abs--;
        addr--;
        addr &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        /* Translation required again if page boundary crossed */
        tran = ((addr & 0xFFF) == 0xFFF);

    } /* end while */

} /* end function vstorei */

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

    /* Calculate page address of last byte of operand */
    addr2 = (addr + len) & (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    addr2 &= 0xFFFFF000;

    /* Copy data to real storage in either one or two parts
       depending on whether operand crosses a page boundary */
    if (addr2 == (addr & 0xFFFFF000)) {
        addr = virt_to_abs (addr, arn, regs, ACCTYPE_READ);
        memcpy (sysblk.mainstor+addr, src, len+1);
    } else {
        len1 = addr2 - addr;
        len2 = len - len1 + 1;
        addr = virt_to_abs (addr, arn, regs, ACCTYPE_WRITE);
        addr2 = virt_to_abs (addr2, arn, regs, ACCTYPE_WRITE);
        memcpy (sysblk.mainstor+addr, src, len1);
        memcpy (sysblk.mainstor+addr, src+len1, len2);
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
    addr = virt_to_abs (addr, arn, regs, ACCTYPE_WRITE);
    sysblk.mainstor[addr] = value;
} /* end function vstoreb */

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

    /* Calculate page address of last byte of operand */
    addr2 = (addr + len) & (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    addr2 &= 0xFFFFF000;

    /* Copy data from real storage in either one or two parts
       depending on whether operand crosses a page boundary */
    if (addr2 == (addr & 0xFFFFF000)) {
        addr = virt_to_abs (addr, arn, regs, ACCTYPE_READ);
        memcpy (dest, sysblk.mainstor+addr, len+1);
    } else {
        len1 = addr2 - addr;
        len2 = len - len1 + 1;
        addr = virt_to_abs (addr, arn, regs, ACCTYPE_READ);
        addr2 = virt_to_abs (addr2, arn, regs, ACCTYPE_READ);
        memcpy (dest, sysblk.mainstor+addr, len1);
        memcpy (dest+len1, sysblk.mainstor+addr, len2);
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
    addr = virt_to_abs (addr, arn, regs, ACCTYPE_READ);
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

    /* Calculate address of second byte of operand */
    addr2 = (addr + 1) & (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

    /* Get absolute addresses of first byte of operand */
    abs1 = virt_to_abs (addr, arn, regs, ACCTYPE_READ);

    /* Repeat address translation if operand crosses a page boundary */
    if ((addr2 & 0xFFF) == 0x000)
        abs2 = virt_to_abs (addr2, arn, regs, ACCTYPE_READ);
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
U32     abs;                            /* Absolute storage location */

    /* Get absolute addresses of first byte of operand */
    abs = virt_to_abs (addr, arn, regs, ACCTYPE_READ);

    /* Fetch 4 bytes when operand is fullword aligned */
    if ((addr & 0x03) == 0) {
        return (sysblk.mainstor[abs] << 24)
                | (sysblk.mainstor[abs+1] << 16)
                | (sysblk.mainstor[abs+2] << 8)
                | sysblk.mainstor[abs+3];
    }

    /* Fetch 4 bytes when operand is not fullword aligned
       and may therefore cross a page boundary */
    for (i=0, value=0; i < 4; i++) {

        /* Fetch byte from absolute storage */
        value <<= 8;
        value |= sysblk.mainstor[abs];

        /* Increment absolute address and virtual address */
        abs++;
        addr++;
        addr &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        /* Translation required again if page boundary crossed */
        if ((addr & 0xFFF) == 0x000)
            abs = virt_to_abs (addr, arn, regs, ACCTYPE_READ);

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
U32     abs;                            /* Absolute storage location */

    /* Get absolute addresses of first byte of operand */
    abs = virt_to_abs (addr, arn, regs, ACCTYPE_READ);

    /* Fetch 8 bytes when operand is doubleword aligned */
    if ((addr & 0x07) == 0) {
        return ((U64)sysblk.mainstor[abs] << 56)
                | ((U64)sysblk.mainstor[abs+1] << 48)
                | ((U64)sysblk.mainstor[abs+2] << 40)
                | ((U64)sysblk.mainstor[abs+3] << 32)
                | (sysblk.mainstor[abs+4] << 24)
                | (sysblk.mainstor[abs+5] << 16)
                | (sysblk.mainstor[abs+6] << 8)
                | sysblk.mainstor[abs+7];
    }

    /* Fetch 8 bytes when operand is not doubleword aligned
       and may therefore cross a page boundary */
    for (i=0, value=0; i < 8; i++) {

        /* Fetch byte from absolute storage */
        value <<= 8;
        value |= sysblk.mainstor[abs];

        /* Increment absolute address and virtual address */
        abs++;
        addr++;
        addr &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        /* Translation required again if page boundary crossed */
        if ((addr & 0xFFF) == 0x000)
            abs = virt_to_abs (addr, arn, regs, ACCTYPE_READ);

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
U32 abs;                                /* Absolute storage address  */

    /* Program check if instruction address is odd */
    if (addr & 0x01) {
        program_check (PGM_SPECIFICATION_EXCEPTION);
        return;
    }

    /* Fetch first two bytes of instruction */
    abs = virt_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH);
    memcpy (dest, sysblk.mainstor+abs, 2);

    /* Return if two-byte instruction */
    if (dest[0] < 0x40) return;

    /* Fetch next two bytes of instruction */
    abs += 2;
    addr += 2;
    addr &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    if ((addr & 0xFFF) == 0x000) {
        abs = virt_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH);
    }
    memcpy (dest+2, sysblk.mainstor+abs, 2);

    /* Return if four-byte instruction */
    if (dest[0] < 0xC0) return;

    /* Fetch next two bytes of instruction */
    abs += 2;
    addr += 2;
    addr &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    if ((addr & 0xFFF) == 0x000) {
        abs = virt_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH);
    }
    memcpy (dest+4, sysblk.mainstor+abs, 2);

} /* end function instfetch */
