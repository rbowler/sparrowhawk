/* IO.C         (c) Copyright Roger Bowler, 1994-2000                */
/*              ESA/390 CPU Emulator                                 */

/*-------------------------------------------------------------------*/
/* This module implements all I/O instructions of the                */
/* S/370 and ESA/390 architectures, as described in the manuals      */
/* GA22-7000-03 System/370 Principles of Operation                   */
/* SA22-7201-06 ESA/390 Principles of Operation                      */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      STCPS and SCHM instructions by Jan Jaeger                    */
/*      STCRW instruction by Jan Jaeger                              */
/*      Instruction decode by macros - Jan Jaeger                    */
/*      Instruction decode rework - Jan Jaeger                       */
/*-------------------------------------------------------------------*/

#include "hercules.h"

#include "opcode.h"


#if defined(FEATURE_CHANNEL_SUBSYSTEM)

/*-------------------------------------------------------------------*/
/* B230 CSCH  - Clear Subchannel                                 [S] */
/*-------------------------------------------------------------------*/
void zz_clear_subchannel (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Effective addr base       */
U32     effective_addr2;                /* Effective address         */
DEVBLK *dev;                            /* -> device block           */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Program check if reg 1 bits 0-15 not X'0001' */
    if ( (regs->gpr[1] >> 16) != 0x0001 )
        program_check (regs, PGM_OPERAND_EXCEPTION);

    /* Locate the device block for this subchannel */
    dev = find_device_by_subchan (regs->gpr[1] & 0xFFFF);

    /* Condition code 3 if subchannel does not exist,
       is not valid, or is not enabled */
    if (dev == NULL
        || (dev->pmcw.flag5 & PMCW5_V) == 0
        || (dev->pmcw.flag5 & PMCW5_E) == 0)
    {
        regs->psw.cc = 3;
        return;
    }

    /* Perform clear subchannel and set condition code zero */
    clear_subchan (regs, dev);

    regs->psw.cc = 0;

}


/*-------------------------------------------------------------------*/
/* B231 HSCH  - Halt Subchannel                                  [S] */
/*-------------------------------------------------------------------*/
void zz_halt_subchannel (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Effective addr base       */
U32     effective_addr2;                /* Effective address         */
DEVBLK *dev;                            /* -> device block           */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Program check if reg 1 bits 0-15 not X'0001' */
    if ( (regs->gpr[1] >> 16) != 0x0001 )
        program_check (regs, PGM_OPERAND_EXCEPTION);

    /* Locate the device block for this subchannel */
    dev = find_device_by_subchan (regs->gpr[1] & 0xFFFF);

    /* Condition code 3 if subchannel does not exist,
       is not valid, or is not enabled */
    if (dev == NULL
        || (dev->pmcw.flag5 & PMCW5_V) == 0
        || (dev->pmcw.flag5 & PMCW5_E) == 0)
    {
        regs->psw.cc = 3;
        return;
    }

    /* Perform halt subchannel and set condition code */
    regs->psw.cc = halt_subchan (regs, dev);

}


/*-------------------------------------------------------------------*/
/* B232 MSCH  - Modify Subchannel                                [S] */
/*-------------------------------------------------------------------*/
void zz_modify_subchannel (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Effective addr base       */
U32     effective_addr2;                /* Effective address         */
DEVBLK *dev;                            /* -> device block           */
PMCW    pmcw;                           /* Path management ctl word  */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    FW_CHECK(effective_addr2, regs);

    /* Fetch the updated path management control word */
    vfetchc ( &pmcw, sizeof(PMCW)-1, effective_addr2, b2, regs );

    /* Program check if reserved bits are not zero */
    if (pmcw.flag4 & PMCW4_RESV
        || (pmcw.flag5 & PMCW5_LM) == PMCW5_LM_RESV
        || pmcw.flag24 != 0 || pmcw.flag25 != 0
        || pmcw.flag26 != 0 || (pmcw.flag27 & PMCW27_RESV))
        program_check (regs, PGM_OPERAND_EXCEPTION);

    /* Program check if reg 1 bits 0-15 not X'0001' */
    if ( (regs->gpr[1] >> 16) != 0x0001 )
        program_check (regs, PGM_OPERAND_EXCEPTION);

    /* Locate the device block for this subchannel */
    dev = find_device_by_subchan (regs->gpr[1] & 0xFFFF);

    /* Condition code 3 if subchannel does not exist */
    if (dev == NULL)
    {
        regs->psw.cc = 3;
        return;
    }

    /* If the subchannel is invalid then return cc0 */
    if (!(dev->pmcw.flag5 & PMCW5_V))
    {
        regs->psw.cc = 0;
        return;
    }

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    /* Condition code 1 if subchannel is status pending */
    if (dev->scsw.flag3 & SCSW3_SC_PEND)
    {
        regs->psw.cc = 1;
        return;
    }

    /* Obtain the device lock */
    obtain_lock (&dev->lock);

    /* Condition code 2 if subchannel is busy */
    if (dev->busy || dev->pending)
    {
        regs->psw.cc = 2;
        release_lock (&dev->lock);
        return;
    }

    /* Update the enabled (E), limit mode (LM),
       and measurement mode (MM), and multipath (D) bits */
    dev->pmcw.flag5 &=
        ~(PMCW5_E | PMCW5_LM | PMCW5_MM | PMCW5_D);
    dev->pmcw.flag5 |= (pmcw.flag5 &
        (PMCW5_E | PMCW5_LM | PMCW5_MM | PMCW5_D));

    /* Update the measurement block index */
    memcpy (dev->pmcw.mbi, pmcw.mbi, sizeof(HWORD));

    /* Update the interruption parameter */
    memcpy (dev->pmcw.intparm, pmcw.intparm, sizeof(FWORD));

    /* Update the interruption subclass (ISC) field */
    dev->pmcw.flag4 &= ~PMCW4_ISC;
    dev->pmcw.flag4 |= (pmcw.flag4 & PMCW4_ISC);

    /* Update the path management (LPM and POM) fields */
    dev->pmcw.lpm = pmcw.lpm;
    dev->pmcw.pom = pmcw.pom;

    /* Update the concurrent sense (S) field */
    dev->pmcw.flag27 &= ~PMCW27_S;
    dev->pmcw.flag27 |= (pmcw.flag27 & PMCW27_S);

    release_lock (&dev->lock);

    /* Set condition code 0 */
    regs->psw.cc = 0;

}


#if 0
/*-------------------------------------------------------------------*/
/* B23B RCHP  - Reset Channel Path                               [S] */
/*-------------------------------------------------------------------*/
#endif


/*-------------------------------------------------------------------*/
/* B238 RSCH  - Resume Subchannel                                [S] */
/*-------------------------------------------------------------------*/
void zz_resume_subchannel (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Effective addr base       */
U32     effective_addr2;                /* Effective address         */
DEVBLK *dev;                            /* -> device block           */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Program check if reg 1 bits 0-15 not X'0001' */
    if ( (regs->gpr[1] >> 16) != 0x0001 )
        program_check (regs, PGM_OPERAND_EXCEPTION);

    /* Locate the device block for this subchannel */
    dev = find_device_by_subchan (regs->gpr[1] & 0xFFFF);

    /* Condition code 3 if subchannel does not exist,
       is not valid, or is not enabled */
    if (dev == NULL
        || (dev->pmcw.flag5 & PMCW5_V) == 0
        || (dev->pmcw.flag5 & PMCW5_E) == 0)
    {
        regs->psw.cc = 3;
        return;
    }

    /* Perform resume subchannel and set condition code */
    regs->psw.cc = resume_subchan (regs, dev);

}


#if 0
/*-------------------------------------------------------------------*/
/* B237 SAL   - Set Address Limit                                [S] */
/*-------------------------------------------------------------------*/
#endif


/*-------------------------------------------------------------------*/
/* B23C SCHM  - Set Channel Monitor                              [S] */
/*-------------------------------------------------------------------*/
void zz_set_channel_monitor (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Effective addr base       */
U32     effective_addr2;                /* Effective address         */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Reserved bits in gpr1 must be zero */
    if (regs->gpr[1] & CHM_GPR1_RESV)
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Program check if M bit one and gpr2 address not on
       a 32 byte boundary or highorder bit set */
    if ((regs->gpr[1] & CHM_GPR1_M)
     && (regs->gpr[2] & CHM_GPR2_RESV))
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Set the measurement block origin address */
    if (regs->gpr[1] & CHM_GPR1_M)
    {
        sysblk.mbo = regs->gpr[2] & CHM_GPR2_MBO;
        sysblk.mbk = (regs->gpr[1] & CHM_GPR1_MBK) >> 24;
        sysblk.mbm = 1;
    }
    else
        sysblk.mbm = 0;

    sysblk.mbd = regs->gpr[1] & CHM_GPR1_D;

}


/*-------------------------------------------------------------------*/
/* B233 SSCH  - Start Subchannel                                 [S] */
/*-------------------------------------------------------------------*/
void zz_start_subchannel (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Effective addr base       */
U32     effective_addr2;                /* Effective address         */
DEVBLK *dev;                            /* -> device block           */
U32     ccwaddr;                        /* CCW address for start I/O */
U32     ioparm;                         /* I/O interruption parameter*/
ORB     orb;                            /* Operation request block   */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    FW_CHECK(effective_addr2, regs);

    /* Fetch the operation request block */
    vfetchc ( &orb, sizeof(ORB)-1, effective_addr2, b2, regs );

    /* Program check if reserved bits are not zero */
    if (orb.flag4 & ORB4_RESV
        || orb.flag5 & ORB5_RESV
        || orb.flag7 & ORB7_RESV
        || orb.ccwaddr[0] & 0x80)
        program_check (regs, PGM_OPERAND_EXCEPTION);

    /* Program check if incorrect length suppression */
    if (orb.flag7 & ORB7_L)
        program_check (regs, PGM_OPERAND_EXCEPTION);

    /* Program check if reg 1 bits 0-15 not X'0001' */
    if ( (regs->gpr[1] >> 16) != 0x0001 )
        program_check (regs, PGM_OPERAND_EXCEPTION);

    /* Locate the device block for this subchannel */
    dev = find_device_by_subchan (regs->gpr[1] & 0xFFFF);

    /* Condition code 3 if subchannel does not exist,
       is not valid, or is not enabled */
    if (dev == NULL
        || (dev->pmcw.flag5 & PMCW5_V) == 0
        || (dev->pmcw.flag5 & PMCW5_E) == 0)
    {
        regs->psw.cc = 3;
        return;
    }

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    /* Clear the path not operational mask */
    dev->pmcw.pnom = 0;

    /* Extract CCW address and I/O parameter */
    ccwaddr = (orb.ccwaddr[0] << 24) | (orb.ccwaddr[1] << 16)
                | (orb.ccwaddr[2] << 8) | orb.ccwaddr[3];
    ioparm = (orb.intparm[0] << 24) | (orb.intparm[1] << 16)
                | (orb.intparm[2] << 8) | orb.intparm[3];

    /* Start the channel program and set the condition code */
    regs->psw.cc =
        start_io (dev, ioparm, orb.flag4, orb.flag5,
                        orb.lpm, orb.flag7, ccwaddr);

}


/*-------------------------------------------------------------------*/
/* B23A STCPS - Store Channel Path Status                        [S] */
/*-------------------------------------------------------------------*/
void zz_store_channel_path_status (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Effective addr base       */
U32     effective_addr2;                /* Effective address         */
BYTE    work[32];                       /* Work area                 */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Program check if operand not on 32 byte boundary */
    if ( effective_addr2 & 0x0000001F )
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);

    /*INCOMPLETE, SET TO ALL ZEROS*/
    memset(work,0x00,32);

    /* Store channel path status word at operand address */
    vstorec ( work, 32-1, effective_addr2, b2, regs );

}


/*-------------------------------------------------------------------*/
/* B239 STCRW - Store Channel Report Word                        [S] */
/*-------------------------------------------------------------------*/
void zz_store_channel_report_word (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Effective addr base       */
U32     effective_addr2;                /* Effective address         */
U32     n;                              /* Integer work area         */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    FW_CHECK(effective_addr2, regs);

    /* Obtain any pending channel report */
    n = channel_report();

    /* Store channel report word at operand address */
    vstore4 ( n, effective_addr2, b2, regs );

    /* Indicate if channel report or zeros were stored */
    regs->psw.cc = (n == 0) ? 1 : 0;

}


/*-------------------------------------------------------------------*/
/* B234 STSCH - Store Subchannel                                 [S] */
/*-------------------------------------------------------------------*/
void zz_store_subchannel (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Effective addr base       */
U32     effective_addr2;                /* Effective address         */
DEVBLK *dev;                            /* -> device block           */
SCHIB   schib;                          /* Subchannel information blk*/

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Program check if reg 1 bits 0-15 not X'0001' */
    if ( (regs->gpr[1] >> 16) != 0x0001 )
        program_check (regs, PGM_OPERAND_EXCEPTION);

    /* Locate the device block for this subchannel */
    dev = find_device_by_subchan (regs->gpr[1] & 0xFFFF);

    /* Set condition code 3 if subchannel does not exist */
    if (dev == NULL)
    {
        regs->psw.cc = 3;
        return;
    }

    FW_CHECK(effective_addr2, regs);

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    /* Build the subchannel information block */
    schib.pmcw = dev->pmcw;
    schib.scsw = dev->scsw;
    memset (schib.moddep, 0, sizeof(schib.moddep));

    /* Store the subchannel information block */
    vstorec ( &schib, sizeof(SCHIB)-1, effective_addr2,
                b2, regs );

    /* Set condition code 0 */
    regs->psw.cc = 0;

}


/*-------------------------------------------------------------------*/
/* B236 TPI   - Test Pending Interruption                        [S] */
/*-------------------------------------------------------------------*/
void zz_test_pending_interruption (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Effective addr base       */
U32     effective_addr2;                /* Effective address         */
PSA    *psa;                            /* -> Prefixed storage area  */
U64     dreg;                           /* Double register work area */
U32     ioid;                           /* I/O interruption address  */
U32     ioparm;                         /* I/O interruption parameter*/

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    FW_CHECK(effective_addr2, regs);

    /* validate operand before taking any action */
    if ( effective_addr2 != 0 )
        validate_operand (effective_addr2, b2, 8-1, ACCTYPE_WRITE, regs);

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    /* Obtain the interrupt lock */
    obtain_lock (&sysblk.intlock);

    /* Test and clear pending interrupt, set condition code */
    regs->psw.cc =
        present_io_interrupt (regs, &ioid, &ioparm, NULL);

    /* Release the interrupt lock */
    release_lock (&sysblk.intlock);

    /* Store the SSID word and I/O parameter */
    if ( effective_addr2 == 0 )
    {
        /* If operand address is zero, store in PSA */
        psa = (PSA*)(sysblk.mainstor + regs->pxr);
        psa->ioid[0] = ioid >> 24;
        psa->ioid[1] = (ioid & 0xFF0000) >> 16;
        psa->ioid[2] = (ioid & 0xFF00) >> 8;
        psa->ioid[3] = ioid & 0xFF;
        psa->ioparm[0] = ioparm >> 24;
        psa->ioparm[1] = (ioparm & 0xFF0000) >> 16;
        psa->ioparm[2] = (ioparm & 0xFF00) >> 8;
        psa->ioparm[3] = ioparm & 0xFF;
    }
    else
    {
        /* Otherwise store at operand location */
        dreg = ((U64)ioid << 32) | ioparm;
        vstore8 ( dreg, effective_addr2, b2, regs );
    }

}


/*-------------------------------------------------------------------*/
/* B235 TSCH  - Test Subchannel                                  [S] */
/*-------------------------------------------------------------------*/
void zz_test_subchannel (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Effective addr base       */
U32     effective_addr2;                /* Effective address         */
DEVBLK *dev;                            /* -> device block           */
IRB     irb;                            /* Interruption response blk */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    FW_CHECK(effective_addr2, regs);

    /* Program check if reg 1 bits 0-15 not X'0001' */
    if ( (regs->gpr[1] >> 16) != 0x0001 )
        program_check (regs, PGM_OPERAND_EXCEPTION);

    /* Locate the device block for this subchannel */
    dev = find_device_by_subchan (regs->gpr[1] & 0xFFFF);

    /* Condition code 3 if subchannel does not exist,
       is not valid, or is not enabled */
    if (dev == NULL
        || (dev->pmcw.flag5 & PMCW5_V) == 0
        || (dev->pmcw.flag5 & PMCW5_E) == 0)
    {
        regs->psw.cc = 3;
        return;
    }

    /* validate operand before taking any action */
    validate_operand (effective_addr2, b2, sizeof(IRB)-1,
                                        ACCTYPE_WRITE, regs);

    /* Perform serialization and checkpoint-synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    /* Test and clear pending status, set condition code */
    regs->psw.cc = test_subchan (regs, dev, &irb);

    /* Store the interruption response block */
    vstorec ( &irb, sizeof(IRB)-1, effective_addr2, b2, regs );

}

#endif /*defined(FEATURE_CHANNEL_SUBSYSTEM)*/


#if defined(FEATURE_S370_CHANNEL)

/*-------------------------------------------------------------------*/
/* 9C00 SIO   - Start I/O                                        [S] */
/* 9C01 SIOF  - Start I/O Fast Release                           [S] */
/*-------------------------------------------------------------------*/
void zz_s370_startio (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Effective addr base       */
U32     effective_addr2;                /* Effective address         */
PSA    *psa;                            /* -> prefixed storage area  */
DEVBLK *dev;                            /* -> device block for SIO   */
U32     ccwaddr;                        /* CCW address for start I/O */
BYTE    ccwkey;                         /* Bits 0-3=key, 4=7=zeroes  */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Locate the device block */
    dev = find_device_by_devnum (effective_addr2);

    /* Set condition code 3 if device does not exist */
    if (dev == NULL)
    {
        regs->psw.cc = 3;
        return;
    }

    /* Fetch key and CCW address from the CAW at PSA+X'48' */
    psa = (PSA*)(sysblk.mainstor + regs->pxr);
    ccwkey = psa->caw[0] & 0xF0;
    ccwaddr = (psa->caw[1] << 16) | (psa->caw[2] << 8)
                    | psa->caw[3];

    /* Start the channel program and set the condition code */
    regs->psw.cc =
        start_io (dev, 0, ccwkey, 0, 0, 0, ccwaddr);

}


/*-------------------------------------------------------------------*/
/* 9D00 TIO   - Test I/O                                         [S] */
/* 9D01 CLRIO - Clear I/O                                        [S] */
/*-------------------------------------------------------------------*/
void zz_s370_testio (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
DEVBLK *dev;                            /* -> device block for SIO   */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Locate the device block */
    dev = find_device_by_devnum (effective_addr2);

    /* Set condition code 3 if device does not exist */
    if (dev == NULL)
    {
        regs->psw.cc = 3;
        return;
    }

    /* Test the device and set the condition code */
    regs->psw.cc = test_io (regs, dev, inst[1]);

}


/*-------------------------------------------------------------------*/
/* 9E00 HIO   - Halt I/O                                         [S] */
/* 9E01 HDV   - Halt Device                                      [S] */
/*-------------------------------------------------------------------*/
void zz_s370_haltio (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */
DEVBLK *dev;                            /* -> device block for SIO   */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Locate the device block */
    dev = find_device_by_devnum (effective_addr2);

    /* Set condition code 3 if device does not exist */
    if (dev == NULL)
    {
        regs->psw.cc = 3;
        return;
    }

    /* Test the device and set the condition code */
    regs->psw.cc = halt_io (regs, dev, inst[1]);
}


/*-------------------------------------------------------------------*/
/* 9F00 TCH   - Test Channel                                     [S] */
/*-------------------------------------------------------------------*/
void zz_s370_test_channel (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Test for pending interrupt and set condition code */
    regs->psw.cc = test_channel (regs, effective_addr2 & 0xFF00);

}


/*-------------------------------------------------------------------*/
/* B203 STIDC - Store Channel ID                                 [S] */
/*-------------------------------------------------------------------*/
void zz_s370_store_channelid (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Base of effective addr    */
U32     effective_addr2;                /* Effective address         */

    S(inst, execflag, regs, b2, effective_addr2);

    PRIV_CHECK(regs);

    /* Store Channel ID and set condition code */
    regs->psw.cc =
        store_channel_id (regs, effective_addr2 & 0xFF00);

}

#endif /*defined(FEATURE_S370_CHANNEL)*/
