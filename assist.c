/* ASSIST.C     (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 MVS Assist Routines                          */

/*-------------------------------------------------------------------*/
/* This module contains routines which process the MVS Assist        */
/* instructions described in the manual GA22-7079-01.                */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Control block offsets fixed by architecture                       */
/*-------------------------------------------------------------------*/

/* Prefixed storage area offsets */
#define PSALCPUA        0x2F4           /* Logical CPU address       */
#define PSAHLHI         0x2F8           /* Locks held indicators     */

/* Bit settings for PSAHLHI */
#define PSACMSLI        0x00000002      /* CMS lock held indicator   */
#define PSALCLLI        0x00000001      /* Local lock held indicator */

/* Address space control block offsets */
#define ASCBLOCK        0x080           /* Local lock                */
#define ASCBLSWQ        0x084           /* Local lock suspend queue  */

/* Lock interface table offsets */
#define LITOLOC         (-16)           /* Obtain local error exit   */
#define LITRLOC         (-12)           /* Release local error exit  */
#define LITOCMS         (-8)            /* Obtain CMS error exit     */
#define LITRCMS         (-4)            /* Release CMS error exit    */


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
/* Store a fullword into absolute storage.                           */
/* All bytes of the word are stored concurrently as observed by      */
/* other CPUs.  The bytes of the word are reversed if necessary      */
/* and the word is then stored as an integer in absolute storage.    */
/*-------------------------------------------------------------------*/
static inline void store_fullword_absolute (U32 value, U32 addr)
{
U32     i;

    i = htonl(value);
    *((U32*)(sysblk.mainstor + addr)) = i;
} /* end function store_fullword_absolute */

/*-------------------------------------------------------------------*/
/* Obtain Local Lock                                                 */
/*                                                                   */
/* Input:                                                            */
/*      addr1   Logical address of ASCB pointer                      */
/*      ar1     Access register number associated with operand 1     */
/*      addr2   Logical address of highest lock held indicators      */
/*      ar2     Access register number associated with operand 1     */
/*-------------------------------------------------------------------*/
void obtain_local_lock (U32 addr1, int ar1, U32 addr2, int ar2,
                        REGS *regs)
{
U32     ascb_addr;                      /* Virtual address of ASCB   */
U32     hlhi_word;                      /* Highest lock held word    */
U32     lit_addr;                       /* Virtual address of lock
                                           interface table           */
U32     lock;                           /* Lock value                */
U32     lcpa;                           /* Logical CPU address       */
U32     newia;                          /* Unsuccessful branch addr  */

    /* Privileged operation exception if in problem state */
    if (regs->psw.prob)
    {
        program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
        return;
    }

    /* Specification exception if operands are not on word boundary */
    if ((addr1 & 0x00000003) || (addr2 & 0x00000003))
    {
        program_check (PGM_SPECIFICATION_EXCEPTION);
        return;
    }

    /* Obtain main-storage access lock */
    obtain_lock (&sysblk.mainlock);

    /* Load ASCB address from first operand location */
    ascb_addr = vfetch4 ( addr1, ar1, regs );

    /* Load locks held bits from second operand location */
    hlhi_word = vfetch4 ( addr2, ar2, regs );

    /* Fetch our logical CPU address from PSALCPUA */
    lcpa = fetch_fullword_absolute ( PSALCPUA + regs->pxr);

    /* Fetch the local lock from the ASCB */
    lock = vfetch4 ( ascb_addr + ASCBLOCK, 0, regs );

    /* Obtain the local lock if not already held by any CPU */
    if (lock == 0 && (hlhi_word & PSALCLLI) == 0)
    {
        /* Store the unchanged value into the second operand to
           ensure suppression in the event of an access exception */
        vstore4 ( hlhi_word, addr2, ar2, regs );

        /* Store our logical CPU address in ASCBLOCK */
        vstore4 ( lcpa, ascb_addr + ASCBLOCK, 0, regs );

        /* Set the local lock held bit in the second operand */
        hlhi_word |= PSALCLLI;
        vstore4 ( hlhi_word, addr2, ar2, regs );

        /* Set register 13 to zero to indicate lock obtained */
        regs->gpr[13] = 0;
    }
    else
    {
        /* Fetch the lock interface table address from the
           second word of the second operand, and load the
           new instruction address and amode from LITOLOC */
        lit_addr = vfetch4 ( addr2 + 4, ar2, regs );
        newia = vfetch4 ( lit_addr + LITOLOC, 0, regs );

        /* Save the link information in register 12 */
        regs->gpr[12] = regs->psw.ia;

        /* Copy LITOLOC into register 13 to signify obtain failure */
        regs->gpr[13] = newia;

        /* Update the PSW instruction address */
        regs->psw.ia = newia &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    }

    /* Release main-storage access lock */
    release_lock (&sysblk.mainlock);

} /* end function obtain_local_lock */

/*-------------------------------------------------------------------*/
/* Release Local Lock                                                */
/*                                                                   */
/* Input:                                                            */
/*      addr1   Logical address of ASCB pointer                      */
/*      ar1     Access register number associated with operand 1     */
/*      addr2   Logical address of highest lock held indicators      */
/*      ar2     Access register number associated with operand 1     */
/*-------------------------------------------------------------------*/
void release_local_lock (U32 addr1, int ar1, U32 addr2, int ar2,
                        REGS *regs)
{
U32     ascb_addr;                      /* Virtual address of ASCB   */
U32     hlhi_word;                      /* Highest lock held word    */
U32     lit_addr;                       /* Virtual address of lock
                                           interface table           */
U32     lock;                           /* Lock value                */
U32     susp;                           /* Lock suspend queue        */
U32     lcpa;                           /* Logical CPU address       */
U32     newia;                          /* Unsuccessful branch addr  */

    /* Privileged operation exception if in problem state */
    if (regs->psw.prob)
    {
        program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
        return;
    }

    /* Specification exception if operands are not on word boundary */
    if ((addr1 & 0x00000003) || (addr2 & 0x00000003))
    {
        program_check (PGM_SPECIFICATION_EXCEPTION);
        return;
    }

    /* Obtain main-storage access lock */
    obtain_lock (&sysblk.mainlock);

    /* Load ASCB address from first operand location */
    ascb_addr = vfetch4 ( addr1, ar1, regs );

    /* Load locks held bits from second operand location */
    hlhi_word = vfetch4 ( addr2, ar2, regs );

    /* Fetch our logical CPU address from PSALCPUA */
    lcpa = fetch_fullword_absolute ( PSALCPUA + regs->pxr );

    /* Fetch the local lock and the suspend queue from the ASCB */
    lock = vfetch4 ( ascb_addr + ASCBLOCK, 0, regs );
    susp = vfetch4 ( ascb_addr + ASCBLSWQ, 0, regs );

    /* Test if this CPU holds the local lock, and does not hold
       any CMS lock, and the local lock suspend queue is empty */
    if (lock == lcpa
        && (hlhi_word & (PSALCLLI | PSACMSLI)) == PSALCLLI
        && susp == 0)
    {
        /* Store the unchanged value into the second operand to
           ensure suppression in the event of an access exception */
        vstore4 ( hlhi_word, addr2, ar2, regs );

        /* Set the local lock to zero */
        vstore4 ( 0, ascb_addr + ASCBLOCK, 0, regs );

        /* Clear the local lock held bit in the second operand */
        hlhi_word &= ~PSALCLLI;
        vstore4 ( hlhi_word, addr2, ar2, regs );

        /* Set register 13 to zero to indicate lock released */
        regs->gpr[13] = 0;
    }
    else
    {
        /* Fetch the lock interface table address from the
           second word of the second operand, and load the
           new instruction address and amode from LITRLOC */
        lit_addr = vfetch4 ( addr2 + 4, ar2, regs );
        newia = vfetch4 ( lit_addr + LITRLOC, 0, regs );

        /* Save the link information in register 12 */
        regs->gpr[12] = regs->psw.ia;

        /* Copy LITRLOC into register 13 to signify release failure */
        regs->gpr[13] = newia;

        /* Update the PSW instruction address */
        regs->psw.ia = newia &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    }

    /* Release main-storage access lock */
    release_lock (&sysblk.mainlock);

} /* end function release_local_lock */

/*-------------------------------------------------------------------*/
/* Obtain CMS Lock                                                   */
/*                                                                   */
/* Input:                                                            */
/*      addr1   Logical address of ASCB pointer                      */
/*      ar1     Access register number associated with operand 1     */
/*      addr2   Logical address of highest lock held indicators      */
/*      ar2     Access register number associated with operand 1     */
/*-------------------------------------------------------------------*/
void obtain_cms_lock (U32 addr1, int ar1, U32 addr2, int ar2,
                        REGS *regs)
{
U32     ascb_addr;                      /* Virtual address of ASCB   */
U32     hlhi_word;                      /* Highest lock held word    */
U32     lit_addr;                       /* Virtual address of lock
                                           interface table           */
U32     lock_addr;                      /* Lock address              */
int     lock_arn;                       /* Lock access register      */
U32     lock;                           /* Lock value                */
U32     newia;                          /* Unsuccessful branch addr  */

    /* Privileged operation exception if in problem state */
    if (regs->psw.prob)
    {
        program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
        return;
    }

    /* Specification exception if operands are not on word boundary */
    if ((addr1 & 0x00000003) || (addr2 & 0x00000003))
    {
        program_check (PGM_SPECIFICATION_EXCEPTION);
        return;
    }

    /* General register 11 contains the lock address */
    lock_addr = regs->gpr[11] &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    lock_arn = 11;

    /* Obtain main-storage access lock */
    obtain_lock (&sysblk.mainlock);

    /* Load ASCB address from first operand location */
    ascb_addr = vfetch4 ( addr1, ar1, regs );

    /* Load locks held bits from second operand location */
    hlhi_word = vfetch4 ( addr2, ar2, regs );

    /* Fetch the lock addressed by general register 11 */
    lock = vfetch4 ( lock_addr, lock_arn, regs );

    /* Obtain the lock if not held by any ASCB, and if this CPU
       holds the local lock and does not hold a CMS lock */
    if (lock == 0
        && (hlhi_word & (PSALCLLI | PSACMSLI)) == PSALCLLI)
    {
        /* Store the unchanged value into the second operand to
           ensure suppression in the event of an access exception */
        vstore4 ( hlhi_word, addr2, ar2, regs );

        /* Store the ASCB address in the CMS lock */
        vstore4 ( ascb_addr, lock_addr, lock_arn, regs );

        /* Set the CMS lock held bit in the second operand */
        hlhi_word |= PSACMSLI;
        vstore4 ( hlhi_word, addr2, ar2, regs );

        /* Set register 13 to zero to indicate lock obtained */
        regs->gpr[13] = 0;
    }
    else
    {
        /* Fetch the lock interface table address from the
           second word of the second operand, and load the
           new instruction address and amode from LITOCMS */
        lit_addr = vfetch4 ( addr2 + 4, ar2, regs );
        newia = vfetch4 ( lit_addr + LITOCMS, 0, regs );

        /* Save the link information in register 12 */
        regs->gpr[12] = regs->psw.ia;

        /* Copy LITOCMS into register 13 to signify obtain failure */
        regs->gpr[13] = newia;

        /* Update the PSW instruction address */
        regs->psw.ia = newia &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    }

    /* Release main-storage access lock */
    release_lock (&sysblk.mainlock);

} /* end function obtain_cms_lock */

/*-------------------------------------------------------------------*/
/* Release CMS Lock                                                  */
/*                                                                   */
/* Input:                                                            */
/*      addr1   Logical address of ASCB pointer                      */
/*      ar1     Access register number associated with operand 1     */
/*      addr2   Logical address of highest lock held indicators      */
/*      ar2     Access register number associated with operand 1     */
/*-------------------------------------------------------------------*/
void release_cms_lock (U32 addr1, int ar1, U32 addr2, int ar2,
                        REGS *regs)
{
U32     ascb_addr;                      /* Virtual address of ASCB   */
U32     hlhi_word;                      /* Highest lock held word    */
U32     lit_addr;                       /* Virtual address of lock
                                           interface table           */
U32     lock_addr;                      /* Lock address              */
int     lock_arn;                       /* Lock access register      */
U32     lock;                           /* Lock value                */
U32     susp;                           /* Lock suspend queue        */
U32     newia;                          /* Unsuccessful branch addr  */

    /* Privileged operation exception if in problem state */
    if (regs->psw.prob)
    {
        program_check (PGM_PRIVILEGED_OPERATION_EXCEPTION);
        return;
    }

    /* Specification exception if operands are not on word boundary */
    if ((addr1 & 0x00000003) || (addr2 & 0x00000003))
    {
        program_check (PGM_SPECIFICATION_EXCEPTION);
        return;
    }

    /* General register 11 contains the lock address */
    lock_addr = regs->gpr[11] &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    lock_arn = 11;

    /* Obtain main-storage access lock */
    obtain_lock (&sysblk.mainlock);

    /* Load ASCB address from first operand location */
    ascb_addr = vfetch4 ( addr1, ar1, regs );

    /* Load locks held bits from second operand location */
    hlhi_word = vfetch4 ( addr2, ar2, regs );

    /* Fetch the CMS lock and the suspend queue word */
    lock = vfetch4 ( lock_addr, lock_arn, regs );
    susp = vfetch4 ( lock_addr + 4, lock_arn, regs );

    /* Test if current ASCB holds this lock, the locks held indicators
       show a CMS lock is held, and the lock suspend queue is empty */
    if (lock == ascb_addr
        && (hlhi_word & PSACMSLI)
        && susp == 0)
    {
        /* Store the unchanged value into the second operand to
           ensure suppression in the event of an access exception */
        vstore4 ( hlhi_word, addr2, ar2, regs );

        /* Set the CMS lock to zero */
        vstore4 ( 0, lock_addr, lock_arn, regs );

        /* Clear the CMS lock held bit in the second operand */
        hlhi_word &= ~PSACMSLI;
        vstore4 ( hlhi_word, addr2, ar2, regs );

        /* Set register 13 to zero to indicate lock released */
        regs->gpr[13] = 0;
    }
    else
    {
        /* Fetch the lock interface table address from the
           second word of the second operand, and load the
           new instruction address and amode from LITRCMS */
        lit_addr = vfetch4 ( addr2 + 4, ar2, regs );
        newia = vfetch4 ( lit_addr + LITRCMS, 0, regs );

        /* Save the link information in register 12 */
        regs->gpr[12] = regs->psw.ia;

        /* Copy LITRCMS into register 13 to signify release failure */
        regs->gpr[13] = newia;

        /* Update the PSW instruction address */
        regs->psw.ia = newia &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
    }

    /* Release main-storage access lock */
    release_lock (&sysblk.mainlock);

} /* end function release_cms_lock */

