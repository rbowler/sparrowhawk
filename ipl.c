/* IPL.C        (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Initial Program Loader                       */

/*-------------------------------------------------------------------*/
/* This module implements the Initial Program Load (IPL) function    */
/* of the S/370 and ESA/390 architectures, described in the manuals  */
/* GA22-7000-03 System/370 Principles of Operation                   */
/* SA22-7201-04 ESA/390 Principles of Operation                      */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Function to run initial CCW chain from IPL device and load IPLPSW */
/* Returns 0 if successful, -1 if error                              */
/*-------------------------------------------------------------------*/
int load_ipl (U16 devnum, REGS *regs)
{
int     rc;                             /* Return code               */
int     i;                              /* Array subscript           */
int     cpu;                            /* CPU number                */
DEVBLK *dev;                            /* -> Device control block   */
PSA    *psa;                            /* -> Prefixed storage area  */
BYTE    unitstat;                       /* IPL device unit status    */
BYTE    chanstat;                       /* IPL device channel status */

    /* Reset external interrupts */
    sysblk.servsig = 0;
    sysblk.intkey = 0;

    /* Perform initial reset on the IPL CPU */
    initial_cpu_reset (regs);

    /* Perform CPU reset on all other CPUs */
    for (cpu = 0; cpu < sysblk.numcpu; cpu++)
        cpu_reset (sysblk.regs + cpu);

    /* Perform I/O reset */
    io_reset ();

    /* Point to the device block for the IPL device */
    dev = find_device_by_devnum (devnum);
    if (dev == NULL)
    {
        logmsg ("HHC103I Device %4.4X not in configuration\n",
                devnum);
        return -1;
    }

    /* Point to the PSA in main storage */
    psa = (PSA*)(sysblk.mainstor + regs->pxr);

    /* Build the IPL CCW at location 0 */
    psa->iplpsw[0] = 0x02;              /* CCW command = Read */
    psa->iplpsw[1] = 0;                 /* Data address = zero */
    psa->iplpsw[2] = 0;
    psa->iplpsw[3] = 0;
    psa->iplpsw[4] = CCW_FLAGS_CC | CCW_FLAGS_SLI;
                                        /* CCW flags */
    psa->iplpsw[5] = 0;                 /* Reserved byte */
    psa->iplpsw[6] = 0;                 /* Byte count = 24 */
    psa->iplpsw[7] = 24;

    /* Set CCW tracing for the IPL device */
    dev->ccwtrace = 1;

    /* Enable the subchannel for the IPL device */
    dev->pmcw.flag5 |= PMCW5_E;

    /* Execute the IPL channel program */
    dev->ccwaddr = 0;
    dev->ccwfmt = 0;
    dev->ccwkey = 0;

    execute_ccw_chain (dev);

    /* Reset CCW tracing for the IPL device */
    dev->ccwtrace = 0;

    /* Clear the interrupt pending and device busy conditions */
    dev->pending = 0;
    dev->busy = 0;
    dev->scsw.flag2 = 0;
    dev->scsw.flag3 = 0;

    /* Check that load completed normally */
#ifdef FEATURE_S370_CHANNEL
    unitstat = dev->csw[4];
    chanstat = dev->csw[5];
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    unitstat = dev->scsw.unitstat;
    chanstat = dev->scsw.chanstat;
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    if (unitstat != (CSW_CE | CSW_DE) || chanstat != 0) {
        logmsg ("HHC105I IPL failed: CSW status=%2.2X%2.2X\n",
                unitstat, chanstat);
        logmsg ("HHC106I Sense=");
        for (i=0; i < dev->numsense; i++)
        {
            logmsg ("%2.2X", dev->sense[i]);
            if ((i & 3) == 3) logmsg(" ");
        }
        logmsg ("\n");
        return -1;
    }

#ifdef FEATURE_S370_CHANNEL
    /* Test the EC mode bit in the IPL PSW */
    if (psa->iplpsw[1] & 0x08)
    {
        /* In EC mode, store device address at locations 184-187 */
        psa->ioid[0] = 0;
        psa->ioid[1] = 0;
        psa->ioid[2] = dev->devnum >> 8;
        psa->ioid[3] = dev->devnum & 0xFF;
    }
    else
    {
        /* In BC mode, store device address at locations 2-3 */
        psa->iplpsw[2] = dev->devnum >> 8;
        psa->iplpsw[3] = dev->devnum & 0xFF;
    }
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Store X'0001' + subchannel number at locations 184-187 */
    psa->ioid[0] = 0;
    psa->ioid[1] = 1;
    psa->ioid[2] = dev->subchan >> 8;
    psa->ioid[3] = dev->subchan & 0xFF;

    /* Store zeroes at locations 188-191 */
    memset (psa->ioparm, 0, 4);
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Zeroize the interrupt code in the PSW */
    regs->psw.intcode = 0;

    /* Point to PSA in main storage */
    psa = (PSA*)(sysblk.mainstor + regs->pxr);

    /* Load IPL PSW from PSA+X'0' */
    rc = load_psw (&(regs->psw), psa->iplpsw);
    if ( rc )
    {
        logmsg ("HHC107I IPL failed: Invalid IPL PSW: "
                "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
                psa->iplpsw[0], psa->iplpsw[1], psa->iplpsw[2],
                psa->iplpsw[3], psa->iplpsw[4], psa->iplpsw[5],
                psa->iplpsw[6], psa->iplpsw[7]);
        return -1;
    }

    /* Set the CPU into the started state */
    regs->cpustate = CPUSTATE_STARTED;

    /* Signal all CPUs to retest stopped indicator */
    obtain_lock (&sysblk.intlock);
    signal_condition (&sysblk.intcond);
    release_lock (&sysblk.intlock);

    return 0;
} /* end function load_ipl */

/*-------------------------------------------------------------------*/
/* Function to perform CPU reset                                     */
/*-------------------------------------------------------------------*/
void cpu_reset (REGS *regs)
{
int             i;                      /* Array subscript           */

    /* Clear pending interrupts and indicators */
    regs->itimer_pending = 0;
    regs->restart = 0;
    regs->extcall = 0;
    regs->extccpu = 0;
    regs->emersig = 0;
    for (i = 0; i < MAX_CPU_ENGINES; i++)
        regs->emercpu[i] = 0;
    regs->storstat = 0;
    regs->instvalid = 0;
    regs->instcount = 0;

    /* Clear the translation exception identification */
    regs->tea = 0;
    regs->excarid = 0;

    /* Purge the lookaside buffers */
    purge_tlb (regs);
    purge_alb (regs);

    /* Put the CPU into the stopped state */
    regs->cpustate = CPUSTATE_STOPPED;

} /* end function cpu_reset */

/*-------------------------------------------------------------------*/
/* Function to perform initial CPU reset                             */
/*-------------------------------------------------------------------*/
void initial_cpu_reset (REGS *regs)
{
    /* Perform a CPU reset */
    cpu_reset (regs);

    /* Clear the registers */
    memset (&regs->psw, 0, sizeof(PSW));
    memset (regs->gpr, 0, sizeof(regs->gpr));
    memset (regs->cr, 0, sizeof(regs->cr));
    memset (regs->ar, 0, sizeof(regs->ar));
    memset (regs->fpr, 0, sizeof(regs->fpr));
    regs->pxr = 0;
    regs->ptimer = 0;
    regs->clkc = 0;

    /* Initialize external interrupt masks in control register 0 */
    regs->cr[0] = CR0_XM_ITIMER | CR0_XM_INTKEY | CR0_XM_EXTSIG;

#ifdef FEATURE_S370_CHANNEL
    /* For S/370 initialize the channel masks in CR2 */
    regs->cr[2] = 0xFFFFFFFF;
#endif /*FEATURE_S370_CHANNEL*/

    /* Initialize the machine check masks in control register 14 */
    regs->cr[14] = CR14_CHKSTOP | CR14_SYNCMCEL | CR14_XDMGRPT;

#ifndef FEATURE_LINKAGE_STACK
    /* For S/370 initialize the MCEL address in CR15 */
    regs->cr[15] = 512;
#endif /*!FEATURE_LINKAGE_STACK*/

} /* end function initial_cpu_reset */

