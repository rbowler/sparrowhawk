

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
/* The caller is assumed to have already checked that the absolute   */
/* address is within the limit of main storage.                      */
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
/* Fetch a halfword from absolute storage.                           */
/* The caller is assumed to have already checked that the absolute   */
/* address is within the limit of main storage.                      */
/* All bytes of the halfword are fetched concurrently as observed by */
/* other CPUs.  The halfword is first fetched as an integer, then    */
/* the bytes are reversed into host byte order if necessary.         */
/*-------------------------------------------------------------------*/
static inline U16 fetch_halfword_absolute (U32 addr)
{
U16     i;

    /* Set the main storage reference bit */
    STORAGE_KEY(addr) |= STORKEY_REF;

    /* Fetch the fullword from absolute storage */
    i = *((U16*)(sysblk.mainstor + addr));
    return ntohs(i);
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
    if (ducto >= sysblk.mainsize)
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

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
        program_check (regs, PGM_ADDRESSING_EXCEPTION);

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

