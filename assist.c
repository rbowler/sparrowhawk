/* ASSIST.C     (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 MVS Assist Routines                          */

/*-------------------------------------------------------------------*/
/* This module contains routines which process the MVS Assist        */
/* instructions described in the manual GA22-7079-01.                */
/*-------------------------------------------------------------------*/

/*              Instruction decode rework - Jan Jaeger               */

#include "hercules.h"

#include "opcode.h"

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
/* E504       - Obtain Local Lock                              [SSE] */
/*-------------------------------------------------------------------*/
void zz_obtain_local_lock (BYTE inst[], int execflag, REGS *regs)
{
int     b1, b2;                         /* Values of base field      */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
U32     ascb_addr;                      /* Virtual address of ASCB   */
U32     hlhi_word;                      /* Highest lock held word    */
U32     lit_addr;                       /* Virtual address of lock
                                           interface table           */
U32     lock;                           /* Lock value                */
U32     lcpa;                           /* Logical CPU address       */
U32     newia;                          /* Unsuccessful branch addr  */

    SSE(inst, execflag, regs, b1, effective_addr1, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Specification exception if operands are not on word boundary */
    if ((effective_addr1 & 0x00000003) || (effective_addr2 & 0x00000003))
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

    PERFORM_SERIALIZATION(regs);

    /* Obtain main-storage access lock */
    OBTAIN_MAINLOCK(regs);

    /* Load ASCB address from first operand location */
    ascb_addr = vfetch4 ( effective_addr1, b1, regs );

    /* Load locks held bits from second operand location */
    hlhi_word = vfetch4 ( effective_addr2, b2, regs );

    /* Fetch our logical CPU address from PSALCPUA */
    lcpa = vfetch4 ( PSALCPUA, 0, regs );

    /* Fetch the local lock from the ASCB */
    lock = vfetch4 ( ascb_addr + ASCBLOCK, 0, regs );

    /* Obtain the local lock if not already held by any CPU */
    if (lock == 0
        && (hlhi_word & PSALCLLI) == 0)
    {
        /* Store the unchanged value into the second operand to
           ensure suppression in the event of an access exception */
        vstore4 ( hlhi_word, effective_addr2, b2, regs );

        /* Store our logical CPU address in ASCBLOCK */
        vstore4 ( lcpa, ascb_addr + ASCBLOCK, 0, regs );

        /* Set the local lock held bit in the second operand */
        hlhi_word |= PSALCLLI;
        vstore4 ( hlhi_word, effective_addr2, b2, regs );

        /* Set register 13 to zero to indicate lock obtained */
        regs->gpr[13] = 0;
    }
    else
    {
        /* Fetch the lock interface table address from the
           second word of the second operand, and load the
           new instruction address and amode from LITOLOC */
        lit_addr = vfetch4 ( effective_addr2 + 4, b2, regs );
        newia = vfetch4 ( lit_addr + LITOLOC, 0, regs );

        /* Save the link information in register 12 */
        regs->gpr[12] = regs->psw.ia;

        /* Copy LITOLOC into register 13 to signify obtain failure */
        regs->gpr[13] = newia;

        /* Update the PSW instruction address */
        regs->psw.ia = newia & ADDRESS_MAXWRAP(regs);
    }

    /* Release main-storage access lock */
    RELEASE_MAINLOCK(regs);

    PERFORM_SERIALIZATION(regs);

} /* end function obtain_local_lock */


/*-------------------------------------------------------------------*/
/* E505       - Release Local Lock                             [SSE] */
/*-------------------------------------------------------------------*/
void zz_release_local_lock (BYTE inst[], int execflag, REGS *regs)
{
int     b1, b2;                         /* Values of base field      */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
U32     ascb_addr;                      /* Virtual address of ASCB   */
U32     hlhi_word;                      /* Highest lock held word    */
U32     lit_addr;                       /* Virtual address of lock
                                           interface table           */
U32     lock;                           /* Lock value                */
U32     susp;                           /* Lock suspend queue        */
U32     lcpa;                           /* Logical CPU address       */
U32     newia;                          /* Unsuccessful branch addr  */

    SSE(inst, execflag, regs, b1, effective_addr1, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Specification exception if operands are not on word boundary */
    if ((effective_addr1 & 0x00000003) || (effective_addr2 & 0x00000003))
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Obtain main-storage access lock */
    OBTAIN_MAINLOCK(regs);

    /* Load ASCB address from first operand location */
    ascb_addr = vfetch4 ( effective_addr1, b1, regs );

    /* Load locks held bits from second operand location */
    hlhi_word = vfetch4 ( effective_addr2, b2, regs );

    /* Fetch our logical CPU address from PSALCPUA */
    lcpa = vfetch4 ( PSALCPUA, 0, regs );

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
        vstore4 ( hlhi_word, effective_addr2, b2, regs );

        /* Set the local lock to zero */
        vstore4 ( 0, ascb_addr + ASCBLOCK, 0, regs );

        /* Clear the local lock held bit in the second operand */
        hlhi_word &= ~PSALCLLI;
        vstore4 ( hlhi_word, effective_addr2, b2, regs );

        /* Set register 13 to zero to indicate lock released */
        regs->gpr[13] = 0;
    }
    else
    {
        /* Fetch the lock interface table address from the
           second word of the second operand, and load the
           new instruction address and amode from LITRLOC */
        lit_addr = vfetch4 ( effective_addr2 + 4, b2, regs );
        newia = vfetch4 ( lit_addr + LITRLOC, 0, regs );

        /* Save the link information in register 12 */
        regs->gpr[12] = regs->psw.ia;

        /* Copy LITRLOC into register 13 to signify release failure */
        regs->gpr[13] = newia;

        /* Update the PSW instruction address */
        regs->psw.ia = newia & ADDRESS_MAXWRAP(regs);
    }

    /* Release main-storage access lock */
    RELEASE_MAINLOCK(regs);

} /* end function release_local_lock */


/*-------------------------------------------------------------------*/
/* E506       - Obtain CMS Lock                                [SSE] */
/*-------------------------------------------------------------------*/
void zz_obtain_cms_lock (BYTE inst[], int execflag, REGS *regs)
{
int     b1, b2;                         /* Values of base field      */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
U32     ascb_addr;                      /* Virtual address of ASCB   */
U32     hlhi_word;                      /* Highest lock held word    */
U32     lit_addr;                       /* Virtual address of lock
                                           interface table           */
U32     lock_addr;                      /* Lock address              */
int     lock_arn;                       /* Lock access register      */
U32     lock;                           /* Lock value                */
U32     newia;                          /* Unsuccessful branch addr  */

    SSE(inst, execflag, regs, b1, effective_addr1, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Specification exception if operands are not on word boundary */
    if ((effective_addr1 & 0x00000003) || (effective_addr2 & 0x00000003))
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

    PERFORM_SERIALIZATION(regs);

    /* General register 11 contains the lock address */
    lock_addr = regs->gpr[11] & ADDRESS_MAXWRAP(regs);
    lock_arn = 11;

    /* Obtain main-storage access lock */
    OBTAIN_MAINLOCK(regs);

    /* Load ASCB address from first operand location */
    ascb_addr = vfetch4 ( effective_addr1, b1, regs );

    /* Load locks held bits from second operand location */
    hlhi_word = vfetch4 ( effective_addr2, b2, regs );

    /* Fetch the lock addressed by general register 11 */
    lock = vfetch4 ( lock_addr, lock_arn, regs );

    /* Obtain the lock if not held by any ASCB, and if this CPU
       holds the local lock and does not hold a CMS lock */
    if (lock == 0
        && (hlhi_word & (PSALCLLI | PSACMSLI)) == PSALCLLI)
    {
        /* Store the unchanged value into the second operand to
           ensure suppression in the event of an access exception */
        vstore4 ( hlhi_word, effective_addr2, b2, regs );

        /* Store the ASCB address in the CMS lock */
        vstore4 ( ascb_addr, lock_addr, lock_arn, regs );

        /* Set the CMS lock held bit in the second operand */
        hlhi_word |= PSACMSLI;
        vstore4 ( hlhi_word, effective_addr2, b2, regs );

        /* Set register 13 to zero to indicate lock obtained */
        regs->gpr[13] = 0;
    }
    else
    {
        /* Fetch the lock interface table address from the
           second word of the second operand, and load the
           new instruction address and amode from LITOCMS */
        lit_addr = vfetch4 ( effective_addr2 + 4, b2, regs );
        newia = vfetch4 ( lit_addr + LITOCMS, 0, regs );

        /* Save the link information in register 12 */
        regs->gpr[12] = regs->psw.ia;

        /* Copy LITOCMS into register 13 to signify obtain failure */
        regs->gpr[13] = newia;

        /* Update the PSW instruction address */
        regs->psw.ia = newia & ADDRESS_MAXWRAP(regs);
    }

    /* Release main-storage access lock */
    RELEASE_MAINLOCK(regs);

    PERFORM_SERIALIZATION(regs);

} /* end function obtain_cms_lock */


/*-------------------------------------------------------------------*/
/* E507       - Release CMS Lock                               [SSE] */
/*-------------------------------------------------------------------*/
void zz_release_cms_lock (BYTE inst[], int execflag, REGS *regs)
{
int     b1, b2;                         /* Values of base field      */
U32     effective_addr1,
        effective_addr2;                /* Effective addresses       */
U32     ascb_addr;                      /* Virtual address of ASCB   */
U32     hlhi_word;                      /* Highest lock held word    */
U32     lit_addr;                       /* Virtual address of lock
                                           interface table           */
U32     lock_addr;                      /* Lock address              */
int     lock_arn;                       /* Lock access register      */
U32     lock;                           /* Lock value                */
U32     susp;                           /* Lock suspend queue        */
U32     newia;                          /* Unsuccessful branch addr  */

    SSE(inst, execflag, regs, b1, effective_addr1, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Specification exception if operands are not on word boundary */
    if ((effective_addr1 & 0x00000003) || (effective_addr2 & 0x00000003))
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

    /* General register 11 contains the lock address */
    lock_addr = regs->gpr[11] & ADDRESS_MAXWRAP(regs);
    lock_arn = 11;

    /* Obtain main-storage access lock */
    OBTAIN_MAINLOCK(regs);

    /* Load ASCB address from first operand location */
    ascb_addr = vfetch4 ( effective_addr1, b1, regs );

    /* Load locks held bits from second operand location */
    hlhi_word = vfetch4 ( effective_addr2, b2, regs );

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
        vstore4 ( hlhi_word, effective_addr2, b2, regs );

        /* Set the CMS lock to zero */
        vstore4 ( 0, lock_addr, lock_arn, regs );

        /* Clear the CMS lock held bit in the second operand */
        hlhi_word &= ~PSACMSLI;
        vstore4 ( hlhi_word, effective_addr2, b2, regs );

        /* Set register 13 to zero to indicate lock released */
        regs->gpr[13] = 0;
    }
    else
    {
        /* Fetch the lock interface table address from the
           second word of the second operand, and load the
           new instruction address and amode from LITRCMS */
        lit_addr = vfetch4 ( effective_addr2 + 4, b2, regs );
        newia = vfetch4 ( lit_addr + LITRCMS, 0, regs );

        /* Save the link information in register 12 */
        regs->gpr[12] = regs->psw.ia;

        /* Copy LITRCMS into register 13 to signify release failure */
        regs->gpr[13] = newia;

        /* Update the PSW instruction address */
        regs->psw.ia = newia & ADDRESS_MAXWRAP(regs);
    }

    /* Release main-storage access lock */
    RELEASE_MAINLOCK(regs);

} /* end function release_cms_lock */

