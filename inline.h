

/* Interpretive Execution - (c) Copyright Jan Jaeger, 1999-2000      */

/* Storage protection override fix               Jan Jaeger 31/08/00 */

#ifdef IBUF
#ifdef INLINE_INVALIDATE
/*-------------------------------------------------------------------*/
/* Invalidate Fragment                                               */
/*-------------------------------------------------------------------*/
static inline void ibuf_fastinvalidate (U32 abs, U32 len)
{
REGS *regs;
#if MAX_CPU_ENGINES > 1
int i;
#endif

#ifdef IBUF_STAT
BYTE invalid = 1;
#endif

    if ((abs & (FRAG_BYTESIZE - 1)) < (FRAG_BYTESIZE - 8))
    {
#if MAX_CPU_ENGINES > 1
        for (i=0; i < MAX_CPU_ENGINES; i++)
        {
            regs = &sysblk.regs[i];
            if (regs->fragvalid)
            {
#else
                regs = &sysblk.regs[0];
               
#endif
#ifdef IBUF_STAT
                regs->ibufinvalidatex++;
#endif
                regs->icount[(abs >> FRAG_ADDRESSLENGTH) & (FRAG_BUFFER - 1)]
                          = 0;
                regs->fragvalid[(abs >> FRAG_ADDRESSLENGTH) & (FRAG_BUFFER - 1)]
                          = 0;
#ifdef IBUF_STAT
                regs->fraginvalid[(abs >> FRAG_ADDRESSLENGTH) & (FRAG_BUFFER - 1)]
                          = invalid;
#endif
                return;
#if MAX_CPU_ENGINES > 1
            }
        }
#endif
    }
    ibuf_invalidate(abs, len);
}
#endif
#endif
/*-------------------------------------------------------------------*/
/* Add two signed fullwords giving a signed fullword result          */
/* and return condition code                                         */
/*-------------------------------------------------------------------*/
static inline int add_signed ( U32 *result, U32 op1, U32 op2 )
{
U64     r;
U32     x;
int     carry_in, carry_out;
int     cc;

    r = (U64)op1 + op2;
    x = (U32)r;
    carry_in = ((op1 & 0x7FFFFFFF) + (op2 & 0x7FFFFFFF)) >> 31;
    carry_out = r >> 32;
    *result = x;
    cc = (carry_out != carry_in)? 3 :
        (x == 0)? 0 : ((S32)x < 0)? 1 : 2;
    return cc;
} /* end function add_signed */

/*-------------------------------------------------------------------*/
/* Subtract two signed fullwords giving a signed fullword result     */
/* and return condition code                                         */
/*-------------------------------------------------------------------*/
static inline int sub_signed ( U32 *result, U32 op1, U32 op2 )
{
U64     r;
U32     x;
int     carry_in, carry_out;
int     cc;

    r = (U64)op1 + ~op2 + 1;
    x = (U32)r;
    carry_in = ((op1 & 0x7FFFFFFF) + (~op2 & 0x7FFFFFFF) + 1) >> 31;
    carry_out = r >> 32;
    *result = x;
    cc = (carry_out != carry_in)? 3 :
        (x == 0)? 0 : ((S32)x < 0)? 1 : 2;
    return cc;
} /* end function sub_signed */

/*-------------------------------------------------------------------*/
/* Add two unsigned fullwords giving an unsigned fullword result     */
/* and return condition code                                         */
/*-------------------------------------------------------------------*/
static inline int add_logical ( U32 *result, U32 op1, U32 op2 )
{
U64     r;
int     cc;

    r = (U64)op1 + (U64)op2;
    *result = (U32)r;
    if ((r >> 32) == 0) cc = ((U32)r == 0)? 0 : 1;
    else cc = ((U32)r == 0)? 2 : 3;
    return cc;
} /* end function add_logical */

/*-------------------------------------------------------------------*/
/* Subtract two unsigned fullwords giving an unsigned fullword       */
/* and return condition code                                         */
/*-------------------------------------------------------------------*/
static inline int sub_logical ( U32 *result, U32 op1, U32 op2 )
{
U64     r;
int     cc;

    r = (U64)op1 + ~((U32)op2) + 1;
    *result = (U32)r;
    cc = ((U32)r == 0) ? 2 : ((r >> 32) == 0) ? 1 : 3;
    return cc;
} /* end function sub_logical */

/*-------------------------------------------------------------------*/
/* Multiply two signed fullwords giving a signed doubleword result   */
/*-------------------------------------------------------------------*/
static inline void mul_signed ( U32 *resulthi, U32 *resultlo,
                                                     U32 op1, U32 op2 )
{
S64     r;

    r = (S64)(S32)op1 * (S32)op2;
    *resulthi = (U32)((U64)r >> 32);
    *resultlo = (U32)((U64)r & 0xFFFFFFFF);
} /* end function mul_signed */

/*-------------------------------------------------------------------*/
/* Divide a signed doubleword dividend by a signed fullword divisor  */
/* giving a signed fullword remainder and a signed fullword quotient.*/
/* Returns 0 if successful, 1 if divide overflow.                    */
/*-------------------------------------------------------------------*/
static inline int div_signed ( U32 *remainder, U32 *quotient,
                          U32 dividendhi, U32 dividendlo, U32 divisor )
{
U64     dividend;
S64     quot, rem;

    if (divisor == 0) return 1;
    dividend = (U64)dividendhi << 32 | dividendlo;
    quot = (S64)dividend / (S32)divisor;
    rem = (S64)dividend % (S32)divisor;
    if (quot < -2147483648LL || quot > 2147483647LL) return 1;
    *quotient = (U32)quot;
    *remainder = (U32)rem;
    return 0;
} /* end function div_signed */


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

#ifdef FEATURE_FETCH_PROTECTION_OVERRIDE
    /* [3.4.1.2] Fetch protection override allows fetch from first
       2K of non-private address spaces if CR0 bit 6 is set */
    if (addr < 2048
        && (regs->cr[0] & CR0_FETCH_OVRD)
        && private == 0)
        return 0;
#endif /*FEATURE_FETCH_PROTECTION_OVERRIDE*/

#ifdef FEATURE_STORAGE_PROTECTION_OVERRIDE
    /* [3.4.1.1] Storage protection override allows access to
       locations with storage key 9, regardless of the access key,
       provided that CR0 bit 7 is set */
    if ((skey & STORKEY_KEY) == 0x90
        && (regs->cr[0] & CR0_STORE_OVRD))
        return 0;
#endif /*FEATURE_STORAGE_PROTECTION_OVERRIDE*/

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
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
        /* Host low address protection is not applied to guest
           references to guest storage */
        && !regs->sie_active
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
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
    /* [3.4.1.1] Storage protection override allows access to
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
/* The caller is assumed to have already checked that the absolute   */
/* address is within the limit of main storage.                      */
/* All bytes of the word are fetched concurrently as observed by     */
/* other CPUs.  The fullword is first fetched as an integer, then    */
/* the bytes are reversed into host byte order if necessary.         */
/*-------------------------------------------------------------------*/
static inline U32 fetch_fullword_absolute (U32 addr, REGS *regs)
{
U32     i;

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    if(!regs->sie_state || regs->sie_pref)
    {
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
        /* Set the main storage reference bit */
        STORAGE_KEY(addr) |= STORKEY_REF;

        /* Fetch the fullword from absolute storage */
        i = *((U32*)(sysblk.mainstor + addr));
        return ntohl(i);
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    }
    else
        return VFETCH4(regs->sie_mso + addr, USE_PRIMARY_SPACE, regs->hostregs);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

} /* end function fetch_fullword_absolute */


/*-------------------------------------------------------------------*/
/* Fetch a halfword from absolute storage.                           */
/* The caller is assumed to have already checked that the absolute   */
/* address is within the limit of main storage.                      */
/* All bytes of the halfword are fetched concurrently as observed by */
/* other CPUs.  The halfword is first fetched as an integer, then    */
/* the bytes are reversed into host byte order if necessary.         */
/*-------------------------------------------------------------------*/
static inline U16 fetch_halfword_absolute (U32 addr, REGS *regs)
{
U16     i;

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    if(!regs->sie_state || regs->sie_pref)
    {
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
        /* Set the main storage reference bit */
        STORAGE_KEY(addr) |= STORKEY_REF;

        /* Fetch the fullword from absolute storage */
        i = *((U16*)(sysblk.mainstor + addr));
        return ntohs(i);
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    }
    else
        return VFETCH2(regs->sie_mso + addr, USE_PRIMARY_SPACE, regs->hostregs);
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

} /* end function fetch_fullword_absolute */

/*-------------------------------------------------------------------*/
/* Store a fullword into absolute storage.                           */
/* All bytes of the word are stored concurrently as observed by      */
/* other CPUs.  The bytes of the word are reversed if necessary      */
/* and the word is then stored as an integer in absolute storage.    */
/*-------------------------------------------------------------------*/
static inline void store_fullword_absolute (U32 value, U32 addr, REGS *regs)
{
U32     i;

#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    if(!regs->sie_state || regs->sie_pref)
    {
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
        /* Set the main storage reference and change bits */
        STORAGE_KEY(addr) |= (STORKEY_REF | STORKEY_CHANGE);

        /* Store the fullword into absolute storage */
        i = htonl(value);
        *((U32*)(sysblk.mainstor + addr)) = i;
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
    }
    else
#ifndef INLINE_VSTORE
        vstore4(value, regs->sie_mso + addr, USE_PRIMARY_SPACE, regs->hostregs);
#else
        xvstore4(value, regs->sie_mso + addr, USE_PRIMARY_SPACE, regs->hostregs);
#endif
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

} /* end function store_fullword_absolute */

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
static inline U32 subspace_replace (U32 std, U32 asteo, U16 *xcode, REGS *regs)
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
    if (ducto >= regs->mainsize)
        program_interrupt (regs, PGM_ADDRESSING_EXCEPTION);

    /* Fetch DUCT words 0, 1, and 3 from absolute storage
       (note: the DUCT cannot cross a page boundary) */
    duct0 = fetch_fullword_absolute (ducto, regs);
    duct1 = fetch_fullword_absolute (ducto+4, regs);
    duct3 = fetch_fullword_absolute (ducto+12, regs);

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
    if (ssasteo >= regs->mainsize)
        program_interrupt (regs, PGM_ADDRESSING_EXCEPTION);

    /* Fetch subspace ASTE words 0, 2, and 5 from absolute storage
       (note: the ASTE cannot cross a page boundary) */
    ssaste0 = fetch_fullword_absolute (ssasteo, regs);
    ssaste2 = fetch_fullword_absolute (ssasteo+8, regs);
    ssaste5 = fetch_fullword_absolute (ssasteo+20, regs);

    /* ASTE validity exception if subspace ASTE invalid bit is one */
    if (ssaste0 & ASTE0_INVALID)
    {
        if (xcode == NULL)
            program_interrupt (regs, PGM_ASTE_VALIDITY_EXCEPTION);
        else
            *xcode = PGM_ASTE_VALIDITY_EXCEPTION;
        return 0;
    }

    /* ASTE sequence exception if the subspace ASTE sequence
       number does not match the sequence number in the DUCT */
    if ((ssaste5 & ASTE5_ASTESN) != (duct3 & DUCT3_SSASTESN))
    {
        if (xcode == NULL)
            program_interrupt (regs, PGM_ASTE_SEQUENCE_EXCEPTION);
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


#ifndef FEATURE_OPTIMIZE_SAME_PAGE
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
static inline int translate_addr (U32 vaddr, int arn, REGS *regs, int acctype,
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
        if (acctype == ACCTYPE_LRA
#if defined(FEATURE_LOCK_PAGE)
               || acctype == ACCTYPE_LOCKPAGE
               || acctype == ACCTYPE_UNLKPAGE
#endif /*defined(FEATURE_LOCK_PAGE)*/
)
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
    
#if defined(FEATURE_LOCK_PAGE)
            switch(acctype) {
                case ACCTYPE_LOCKPAGE:
                    if(pte & PAGETAB_PGLOCK)
                        regs->psw.cc = 1;
                    else
                    {
                        store_fullword_absolute(pte | PAGETAB_PGLOCK,
                                                pto, regs);
                        regs->psw.cc = 0;
                    }
                    break;

                case ACCTYPE_UNLKPAGE:
                    if(pte & PAGETAB_PGLOCK) {
                        store_fullword_absolute(pte & ~PAGETAB_PGLOCK,
                                                pto, regs);
                        regs->psw.cc = 0;
                    } else
                        regs->psw.cc = 1;
                    break;
            }
#endif /*defined(FEATURE_LOCK_PAGE)*/
    
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

#ifdef INLINE_LOGICAL
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
static inline U32 logical_to_abs (U32 addr, int arn, REGS *regs,
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
#endif

#ifdef INLINE_VSTORE
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
static inline void vstorec (void *src, BYTE len, U32 addr, int arn, REGS *regs)
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
static inline void vstoreb (BYTE value, U32 addr, int arn, REGS *regs)
{
    addr = logical_to_abs (addr, arn, regs, ACCTYPE_WRITE,
                                regs->psw.pkey);
    sysblk.mainstor[addr] = value;
#ifdef IBUF
    FRAG_INVALIDATEX(regs, addr, 1);
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
static inline void vstore2 (U16 value, U32 addr, int arn, REGS *regs)
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
        FRAG_INVALIDATEX(regs, abs1, 2)
    else
    {
        FRAG_INVALIDATEX(regs, abs1, 1);
        FRAG_INVALIDATEX(regs, abs2, 1);
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
static inline void vstore4 (U32 value, U32 addr, int arn, REGS *regs)
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
        FRAG_INVALIDATEX(regs, abs, 4);
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
        FRAG_INVALIDATEX(regs, habs, 4)
    else
    {
        l1 = (habs & STORAGE_KEY_PAGEMASK) +
              STORAGE_KEY_PAGESIZE - habs;
        FRAG_INVALIDATEX(regs, habs, l1);
        FRAG_INVALIDATEX(regs, habs2, 4 - l1);
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
static inline void vstore8 (U64 value, U32 addr, int arn, REGS *regs)
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
        FRAG_INVALIDATEX(regs, abs, 8);
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
        FRAG_INVALIDATEX(regs, habs, 8)
    else
    {
        l1 = (habs & STORAGE_KEY_PAGEMASK) +
              STORAGE_KEY_PAGESIZE - habs;
        FRAG_INVALIDATEX(regs, habs, l1);
        FRAG_INVALIDATEX(regs, habs2, 8 - l1);
    }
#endif

} /* end function vstore8 */
#endif

#ifdef INLINE_VFETCH
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
static inline void vfetchc (void *dest, BYTE len, U32 addr, int arn, REGS *regs)
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
static inline BYTE vfetchb (U32 addr, int arn, REGS *regs)
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
static inline U16 vfetch2 (U32 addr, int arn, REGS *regs)
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
static inline U32 vfetch4 (U32 addr, int arn, REGS *regs)
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
static inline U64 vfetch8 (U32 addr, int arn, REGS *regs)
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
#endif

#ifdef INLINE_IFETCH
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
static inline void instfetch (BYTE *dest, U32 addr, REGS *regs)
{
U32     abs;                            /* Absolute storage address  */
BYTE    akey;                           /* Bits 0-3=key, 4-7=zeroes  */

    /* Obtain current access key from PSW */
    akey = regs->psw.pkey;

    /* Program check if instruction address is odd */
    if (addr & 0x01)
    {
#ifdef CHECK_FRAGADDRESS
        logmsg("SPEC EXCEPTION instfetch\n");
#endif
        program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);
     }

    /* Fetch six bytes if instruction cannot cross a page boundary */
    if ((addr & STORAGE_KEY_BYTEMASK) <= STORAGE_KEY_PAGESIZE - 6)
    {
#ifdef OPTIMIZE_IAABS
#ifdef CHECK_IAABS
        if (regs->iaabs)
        {
            abs = logical_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH, akey);
            if (abs != regs->iaabs)
                logmsg("ERROR iaabs in instfetch %4x %4x\n",
                        abs, regs->iaabs);
        }
        else
            abs = logical_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH, akey);
#else
        if (regs->iaabs)
            abs = regs->iaabs;
        else
            abs = logical_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH, akey);
#endif
#else
        abs = logical_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH, akey);
#endif
        memcpy (dest, sysblk.mainstor+abs, 6);
        return;
    }

    /* Fetch first two bytes of instruction */
#ifdef CHECK_IAABS
    if (regs->iaabs)
    {
        abs = logical_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH, akey);
        if (abs != regs->iaabs)
            logmsg("ERROR iaabs in instfetch %4x %4x\n",
                    abs, regs->iaabs);
    }
    else
        abs = logical_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH, akey);
#else
    if (regs->iaabs)
        abs = regs->iaabs;
    else
        abs = logical_to_abs (addr, 0, regs, ACCTYPE_INSTFETCH, akey);
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

#endif
