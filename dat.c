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
/* Test for fetch protected storage location.                        */
/*                                                                   */
/* Input:                                                            */
/*      addr    31-bit logical address of storage location           */
/*      skey    Storage key with fetch, reference, and change bits   */
/*              and one low-order zero appended                      */
/*      akey    Access key with 4 low-order zeroes appended          */
/*      private 1=Location is in a private address space             */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      1=Fetch protected, 0=Not fetch protected                     */
/*-------------------------------------------------------------------*/
static inline int is_fetch_protected (U32 addr, BYTE skey, BYTE akey,
                        int private, REGS *regs)
{
    /* [3.4.1] Fetch is allowed if access key is zero, regardless
       of the storage key and fetch protection bit */
    if (akey == 0)
        return 0;

    /* [3.4.1.2] Fetch protection override allows fetch from first
       2K of non-private address spaces if CR0 bit 6 is set */
    if (addr < 2048
        && (regs->cr[0] & CR0_FETCH_OVRD)
        && private == 0)
        return 0;

    /* [3.4.1] Fetch protection prohibits fetch if storage key fetch
       protect bit is on and access key does not match storage key */
    if ((skey & STORKEY_FETCH)
        && akey != (skey & STORKEY_KEY))
        return 1;

    /* Return zero if location is not fetch protected */
    return 0;

} /* end function is_fetch_protected */

/*-------------------------------------------------------------------*/
/* Test for store protected storage location.                        */
/*                                                                   */
/* Input:                                                            */
/*      addr    31-bit logical address of storage location           */
/*      skey    Storage key with fetch, reference, and change bits   */
/*              and one low-order zero appended                      */
/*      akey    Access key with 4 low-order zeroes appended          */
/*      private 1=Location is in a private address space             */
/*      protect 1=Access list protection or page protection applies  */
/*      regs    Pointer to the CPU register context                  */
/* Return value:                                                     */
/*      1=Store protected, 0=Not store protected                     */
/*-------------------------------------------------------------------*/
static inline int is_store_protected (U32 addr, BYTE skey, BYTE akey,
                        int private, int protect, REGS *regs)
{
    /* [3.4.4] Low-address protection prohibits stores into locations
       0-511 of non-private address spaces if CR0 bit 3 is set,
       regardless of the access key and storage key */
    if (addr < 512
        && (regs->cr[0] & CR0_LOW_PROT)
        && private == 0)
        return 1;

    /* Access-list controlled protection prohibits all stores into
       the address space, and page protection prohibits all stores
       into the page, regardless of the access key and storage key */
    if (protect)
        return 1;

    /* [3.4.1] Store is allowed if access key is zero, regardless
       of the storage key */
    if (akey == 0)
        return 0;

#ifdef FEATURE_STORAGE_PROTECTION_OVERRIDE
    /* [3.4.1.1] Storage protection override allows stores into
       locations with storage key 9, regardless of the access key,
       provided that CR0 bit 7 is set */
    if ((skey & STORKEY_KEY) == 0x90
        && (regs->cr[0] & CR0_STORE_OVRD))
        return 0;
#endif /*FEATURE_STORAGE_PROTECTION_OVERRIDE*/

    /* [3.4.1] Store protection prohibits stores if the access
       key does not match the storage key */
    if (akey != (skey & STORKEY_KEY))
        return 1;

    /* Return zero if location is not store protected */
    return 0;

} /* end function is_store_protected */

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
int     numwords;                       /* ASTE size (4 or 16 words) */
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
    if (cb >= sysblk.mainsize)
        goto alet_addr_excp;

    /* Load the effective access-list designation (ALD) from
       offset 16 in the control block.  All four bytes must be
       fetched concurrently as observed by other CPUs.  Note
       that the DUCT and the PASTE cannot cross a page boundary */
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

    /* For ordinary ART (but not for special ART),
       compare the ALE sequence number with the ALET */
    if (acctype != ACCTYPE_BSG
        && (ale[0] & ALE0_ALESN) != (alet & ALET_ALESN))
        goto ale_seq_excp;

    /* [5.8.4.6] Locate the ASN-second-table entry */
    aste_addr = ale[2] & ALE2_ASTE;

    /* Addressing exception if ASTE is outside main storage */
    abs = APPLY_PREFIXING (aste_addr, regs->pxr);
    if (abs >= sysblk.mainsize)
        goto alet_addr_excp;

    /* Fetch the 64-byte ASN second table entry from real storage.
       Each fullword of the ASTE must be fetched concurrently as
       observed by other CPUs.  ASTE cannot cross a page boundary */
    for (i = 0; i < 16; i++)
    {
        aste[i] = fetch_fullword_absolute (abs);
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
void purge_alb (REGS *regs)
{
} /* end function purge_alb */

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
TLBE   *tlbp;                           /* -> TLB entry              */
U32     alet;                           /* Access list entry token   */
U32     asteo;                          /* Real address of ASTE      */
U32     aste[16];                       /* ASN second table entry    */
U16     eax;                            /* Authorization index       */

    /* [3.11.3.1] Load the effective segment table descriptor */
    if (acctype == ACCTYPE_INSTFETCH)
        std = (HOME_SPACE_MODE(&regs->psw)) ? regs->cr[13] :
            regs->cr[1];
    else if (acctype == ACCTYPE_STACK)
        std = regs->cr[13];
    else if (PRIMARY_SPACE_MODE(&regs->psw)
            || arn == USE_PRIMARY_SPACE)
        std = regs->cr[1];
    else if (SECONDARY_SPACE_MODE(&regs->psw)
            || arn == USE_SECONDARY_SPACE)
        std = regs->cr[7];
    else if (HOME_SPACE_MODE(&regs->psw))
        std = regs->cr[13];
    else /* ACCESS_REGISTER_MODE */
    {
        /* [5.8.4.1] Select the access-list-entry token */
        alet = (arn == 0) ? 0 : regs->ar[arn];

        /* Use the ALET to determine the segment table origin */
        switch (alet) {

        case ALET_PRIMARY:
            /* [5.8.4.2] Obtain primary segment table designation */
            std = regs->cr[1];
            break;

        case ALET_SECONDARY:
            /* [5.8.4.2] Obtain secondary segment table designation */
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
            std = aste[2];

        } /* end switch(alet) */

    } /* end if(ACCESS_REGISTER_MODE) */

    /* Extract the private space bit from segment table descriptor */
    private = std & STD_PRIVATE;

    /* [3.11.3.2] Check the translation format bits in CR0 */
    if ((regs->cr[0] & CR0_TRAN_FMT) != CR0_TRAN_ESA390)
        goto tran_spec_excp;

    /* [3.11.4] Look up the address in the TLB */
    /* [10.17] Do not use TLB if processing LRA instruction */

    /* Only a single entry in the TLB will be looked up, namely the
       entry associated with the base/access register being used */
    if (acctype == ACCTYPE_LRA || arn < 0)
        tlbp = NULL;
    else
        tlbp = &(regs->tlb[arn & 0x0F]);

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
        if (sto >= sysblk.mainsize)
            goto address_excp;

        /* Check that virtual address is within the segment table */
        if ((vaddr >> 24) > stl)
            goto seg_tran_length;

        /* Fetch segment table entry from real storage.  All bytes
           must be fetched concurrently as observed by other CPUs */
        sto = APPLY_PREFIXING (sto, regs->pxr);
        ste = fetch_fullword_absolute (sto);

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
        if (pto >= sysblk.mainsize)
            goto address_excp;

        /* Check that the virtual address is within the page table */
        if (((vaddr & 0x000FF000) >> 16) > ptl)
            goto page_tran_length;

        /* Fetch the page table entry from real storage.  All bytes
           must be fetched concurrently as observed by other CPUs */
        pto = APPLY_PREFIXING (pto, regs->pxr);
        pte = fetch_fullword_absolute (pto);

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
void purge_tlb (REGS *regs)
{
    memset (regs->tlb, 0, sizeof(regs->tlb));
} /* end function purge_tlb */

/*-------------------------------------------------------------------*/
/* Invalidate page table entry                                       */
/*                                                                   */
/* Input:                                                            */
/*      pte     Page table entry to be invalidated                   */
/*      regs    CPU register context                                 */
/*                                                                   */
/*      This subroutine clears the TLB of all entries whose PFRA     */
/*      matches the PFRA of the supplied page table entry.           */
/*-------------------------------------------------------------------*/
void invalidate_tlb_entry (U32 pte, REGS *regs)
{
int     i;                              /* Array subscript           */

    for (i = 0; i < (sizeof(regs->tlb)/sizeof(TLBE)); i++)
    {
        if ((regs->tlb[i].pte & PAGETAB_PFRA) == (pte & PAGETAB_PFRA))
        {
            regs->tlb[i].valid = 0;
            printf ("dat: TLB entry %d invalidated\n", i); /*debug*/
        }
    } /* end for(i) */

} /* end function invalidate_tlb_entry */

/*-------------------------------------------------------------------*/
/* Test protection and return condition code                         */
/*                                                                   */
/* Input:                                                            */
/*      addr    Logical address to be tested                         */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*      akey    Access key with 4 low-order zeroes appended          */
/* Returns:                                                          */
/*      Condition code for TPROT instruction:                        */
/*      0=Fetch and store allowed, 1=Fetch allowed but not store,    */
/*      2=No access allowed, 3=Translation not available.            */
/*                                                                   */
/*      If the logical address causes an addressing or translation   */
/*      specification exception then a program check is generated    */
/*      and the function does not return.                            */
/*-------------------------------------------------------------------*/
int test_prot (U32 addr, int arn, REGS *regs, BYTE akey)
{
U32     raddr;                          /* Real address              */
U32     aaddr;                          /* Absolute address          */
BYTE    skey;                           /* Storage key               */
int     private = 0;                    /* 1=Private address space   */
int     protect = 0;                    /* 1=ALE or page protection  */
U16     xcode;                          /* Exception code            */

    /* Convert logical address to real address */
    if (REAL_MODE(&regs->psw))
        raddr = addr;
    else {
        /* Return condition code 3 if translation exception */
        if (translate_addr (addr, arn, regs, ACCTYPE_TPROT, &raddr,
                            &xcode, &private, &protect))
            return 3;
    }

    /* Convert real address to absolute address */
    aaddr = APPLY_PREFIXING (raddr, regs->pxr);

    /* Program check if absolute address is outside main storage */
    if (aaddr >= sysblk.mainsize)
        goto tprot_addr_excp;

    /* Load the storage key for the absolute address */
    skey = sysblk.storkeys[aaddr >> 12];

    /* Return condition code 2 if location is fetch protected */
    if (is_fetch_protected (addr, skey, akey, private, regs))
        return 2;

    /* Return condition code 1 if location is store protected */
    if (is_store_protected (addr, skey, akey, private, protect, regs))
        return 1;

    /* Return condition code 0 if location is not protected */
    return 0;

tprot_addr_excp:
    program_check (PGM_ADDRESSING_EXCEPTION);
    return 3;

} /* end function test_prot */

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
static U32 logical_to_abs (U32 addr, int arn, REGS *regs,
                                int acctype, BYTE akey)
{
U32     raddr;                          /* Real address              */
U32     aaddr;                          /* Absolute address          */
U32     block;                          /* 4K block number           */
int     private = 0;                    /* 1=Private address space   */
int     protect = 0;                    /* 1=ALE or page protection  */
PSA    *psa;                            /* -> Prefixed storage area  */
U16     xcode;                          /* Exception code            */

    /* Convert logical address to real address */
    if (REAL_MODE(&regs->psw) || arn == USE_REAL_ADDR)
        raddr = addr;
    else {
        if (translate_addr (addr, arn, regs, acctype, &raddr, &xcode,
                            &private, &protect))
            goto vabs_prog_check;
    }

    /* Convert real address to absolute address */
    aaddr = APPLY_PREFIXING (raddr, regs->pxr);

    /* Program check if absolute address is outside main storage */
    if (aaddr >= sysblk.mainsize)
        goto vabs_addr_excp;

    /* Check protection and set reference and change bits */
    block = aaddr >> 12;
    switch (acctype) {

    case ACCTYPE_READ:
    case ACCTYPE_INSTFETCH:
        /* Program check if fetch protected location */
        if (is_fetch_protected (addr, sysblk.storkeys[block], akey,
                                private, regs))
            goto vabs_prot_excp;

        /* Set the reference bit in the storage key */
        sysblk.storkeys[block] |= STORKEY_REF;
        break;

    case ACCTYPE_WRITE:
        /* Program check if store protected location */
        if (is_store_protected (addr, sysblk.storkeys[block], akey,
                                private, protect, regs))
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
    /* Point to the PSA in main storage */
    psa = (PSA*)(sysblk.mainstor + regs->pxr);

    if (xcode == PGM_ALEN_TRANSLATION_EXCEPTION
        || xcode == PGM_ALE_SEQUENCE_EXCEPTION
        || xcode == PGM_ASTE_VALIDITY_EXCEPTION
        || xcode == PGM_ASTE_SEQUENCE_EXCEPTION
        || xcode == PGM_EXTENDED_AUTHORITY_EXCEPTION)
        /* Store the access register number at PSA+160 */
        psa->excarid = arn;

    if (xcode == PGM_PAGE_TRANSLATION_EXCEPTION
        || xcode == PGM_SEGMENT_TRANSLATION_EXCEPTION)
    {
        /* Store the access register number at PSA+160 */
        psa->excarid = (acctype == ACCTYPE_INSTFETCH) ? 0 : arn;

        /* Store the translation exception address at PSA+144 */
        psa->tea[0] = (addr & 0x7F000000) >> 24;
        psa->tea[1] = (addr & 0xFF0000) >> 16;
        psa->tea[2] = (addr & 0xF000) >> 8;
        psa->tea[3] = (regs->psw.space << 1) | regs->psw.armode;
        if (SECONDARY_SPACE_MODE(&regs->psw)
            && acctype != ACCTYPE_INSTFETCH)
            psa->tea[0] |= 0x80;
    }
    program_check (xcode);
    return 0;

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
    addr2 = (addr + len) & (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    addr2 &= 0xFFFFF000;

    /* Copy data to real storage in either one or two parts
       depending on whether operand crosses a page boundary */
    if (addr2 == (addr & 0xFFFFF000)) {
        addr = logical_to_abs (addr, arn, regs, ACCTYPE_WRITE, akey);
        memcpy (sysblk.mainstor+addr, src, len+1);
    } else {
        len1 = addr2 - addr;
        len2 = len - len1 + 1;
        addr = logical_to_abs (addr, arn, regs, ACCTYPE_WRITE, akey);
        addr2 = logical_to_abs (addr2, arn, regs, ACCTYPE_WRITE, akey);
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
    addr = logical_to_abs (addr, arn, regs, ACCTYPE_WRITE,
                                regs->psw.pkey);
    sysblk.mainstor[addr] = value;
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
    addr2 = (addr + 1) & (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

    /* Get absolute address of first byte of operand */
    abs1 = logical_to_abs (addr, arn, regs, ACCTYPE_WRITE, akey);

    /* Repeat address translation if operand crosses a page boundary */
    if ((addr2 & 0xFFF) == 0x000)
        abs2 = logical_to_abs (addr2, arn, regs, ACCTYPE_WRITE, akey);
    else
        abs2 = abs1 + 1;

    /* Store integer value at operand location */
    sysblk.mainstor[abs1] = value >> 8;
    sysblk.mainstor[abs2] = value & 0xFF;

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

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Get absolute address of first byte of operand */
    abs = logical_to_abs (addr, arn, regs, ACCTYPE_WRITE, akey);

    /* Store 4 bytes when operand is fullword aligned */
    if ((addr & 0x03) == 0) {
        sysblk.mainstor[abs] = (value >> 24) & 0xFF;
        sysblk.mainstor[abs+1] = (value >> 16) & 0xFF;
        sysblk.mainstor[abs+2] = (value >> 8) & 0xFF;
        sysblk.mainstor[abs+3] = value & 0xFF;
        return;
    }

    /* Operand is not fullword aligned and may cross a page boundary */

    /* Calculate page address of last byte of operand */
    addr2 = (addr + 3) & (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    addr2 &= 0xFFFFF000;
    abs2 = logical_to_abs (addr2, arn, regs, ACCTYPE_WRITE, akey);

    /* Store integer value byte by byte at operand location */
    for (i=0, k=24; i < 4; i++, k -= 8) {

        /* Store byte in absolute storage */
        sysblk.mainstor[abs] = (value >> k) & 0xFF;

        /* Increment absolute address and virtual address */
        abs++;
        addr++;
        addr &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        /* Adjust absolute address if page boundary crossed */
        if (addr == addr2)
            abs = abs2;

    } /* end for */

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
        return;
    }

    /* Non-doubleword aligned operand may cross a page boundary */

    /* Calculate page address of last byte of operand */
    addr2 = (addr + 7) & (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    addr2 &= 0xFFFFF000;
    abs2 = logical_to_abs (addr2, arn, regs, ACCTYPE_WRITE, akey);

    /* Store integer value byte by byte at operand location */
    for (i=0, k=56; i < 8; i++, k -= 8) {

        /* Store byte in absolute storage */
        sysblk.mainstor[abs] = (value >> k) & 0xFF;

        /* Increment absolute address and virtual address */
        abs++;
        addr++;
        addr &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        /* Adjust absolute address if page boundary crossed */
        if (addr == addr2)
            abs = abs2;

    } /* end for */

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
    addr2 = (addr + len) & (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    addr2 &= 0xFFFFF000;

    /* Copy data from real storage in either one or two parts
       depending on whether operand crosses a page boundary */
    if (addr2 == (addr & 0xFFFFF000)) {
        addr = logical_to_abs (addr, arn, regs, ACCTYPE_READ, akey);
        memcpy (dest, sysblk.mainstor+addr, len+1);
    } else {
        len1 = addr2 - addr;
        len2 = len - len1 + 1;
        addr = logical_to_abs (addr, arn, regs, ACCTYPE_READ, akey);
        addr2 = logical_to_abs (addr2, arn, regs, ACCTYPE_READ, akey);
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
    addr2 = (addr + 1) & (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

    /* Get absolute address of first byte of operand */
    abs1 = logical_to_abs (addr, arn, regs, ACCTYPE_READ, akey);

    /* Repeat address translation if operand crosses a page boundary */
    if ((addr2 & 0xFFF) == 0x000)
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
    if ((addr & 0x03) == 0) {
        return (sysblk.mainstor[abs] << 24)
                | (sysblk.mainstor[abs+1] << 16)
                | (sysblk.mainstor[abs+2] << 8)
                | sysblk.mainstor[abs+3];
    }

    /* Operand is not fullword aligned and may cross a page boundary */

    /* Calculate page address of last byte of operand */
    addr2 = (addr + 3) & (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    addr2 &= 0xFFFFF000;
    abs2 = logical_to_abs (addr2, arn, regs, ACCTYPE_READ, akey);

    /* Fetch integer value byte by byte from operand location */
    for (i=0, value=0; i < 4; i++) {

        /* Fetch byte from absolute storage */
        value <<= 8;
        value |= sysblk.mainstor[abs];

        /* Increment absolute address and virtual address */
        abs++;
        addr++;
        addr &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

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
    addr2 = (addr + 7) & (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    addr2 &= 0xFFFFF000;
    abs2 = logical_to_abs (addr2, arn, regs, ACCTYPE_READ, akey);

    /* Fetch integer value byte by byte from operand location */
    for (i=0, value=0; i < 8; i++) {

        /* Fetch byte from absolute storage */
        value <<= 8;
        value |= sysblk.mainstor[abs];

        /* Increment absolute address and virtual address */
        abs++;
        addr++;
        addr &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

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
    if (addr & 0x01) {
        program_check (PGM_SPECIFICATION_EXCEPTION);
        return;
    }

    /* Fetch first two bytes of instruction */
    abs = logical_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH, akey);
    memcpy (dest, sysblk.mainstor+abs, 2);

    /* Return if two-byte instruction */
    if (dest[0] < 0x40) return;

    /* Fetch next two bytes of instruction */
    abs += 2;
    addr += 2;
    addr &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    if ((addr & 0xFFF) == 0x000) {
        abs = logical_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH, akey);
    }
    memcpy (dest+2, sysblk.mainstor+abs, 2);

    /* Return if four-byte instruction */
    if (dest[0] < 0xC0) return;

    /* Fetch next two bytes of instruction */
    abs += 2;
    addr += 2;
    addr &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    if ((addr & 0xFFF) == 0x000) {
        abs = logical_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH, akey);
    }
    memcpy (dest+4, sysblk.mainstor+abs, 2);

} /* end function instfetch */

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
/*      the caller for each operand.                                 */
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
    npv1 = (addr1 + len)
                & (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    npv1 &= 0xFFFFF000;
    npv2 = (addr2 + len)
                & (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    npv2 &= 0xFFFFF000;

    /* Translate next page addresses if page boundary crossed */
    if (npv1 != (addr1 & 0xFFFFF000))
        npa1 = logical_to_abs (npv1, arn1, regs, ACCTYPE_WRITE, key1);
    if (npv2 != (addr2 & 0xFFFFF000))
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
        addr1 &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
        abs1++;

        /* Adjust absolute address if page boundary crossed */
        if ((addr1 & 0xFFF) == 0x000)
            abs1 = npa1;

        /* Increment second operand address */
        addr2++;
        addr2 &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
        abs2++;

        /* Adjust absolute address if page boundary crossed */
        if ((addr2 & 0xFFF) == 0x000)
            abs2 = npa2;

    } /* end for(i) */

} /* end function move_chars */

