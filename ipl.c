/* IPL.C        (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Initial Program Loader                       */

/*-------------------------------------------------------------------*/
/* This module implements the Initial Program Load (IPL) function    */
/* of the ESA/390 architecture, described in the manual              */
/* SA22-7201-04 ESA/390 Principles of Operation.                     */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* IPL main entry point                                              */
/*-------------------------------------------------------------------*/
int main (int argc, char *argv[])
{
U16     devnum;                         /* Device number             */
DEVBLK *dev;                            /* -> Device control block   */
PSA    *psa;                            /* -> Prefixed storage area  */
BYTE    c;                              /* Work area for sscanf      */
int     stop = 0;                       /* 1=STOP argument present   */
BYTE    unitstat;                       /* IPL device unit status    */
BYTE    chanstat;                       /* IPL device channel status */
int     i;                              /* Array subscript           */
REGS   *regs;                           /* -> CPU register context   */

    /* Display the version identifier */
    printf ("Hercules %s (c)Copyright Roger Bowler, 1994-1999\n",
            MSTRING(VERSION));

    /* Obtain IPL device number from argument */
    if (argc < 2 || strlen(argv[1]) > 4) {
        fprintf (stderr,
                "HHC100I Usage: %s xxxx [stop]\n"
                "\twhere xxxx=IPL device number\n",
                argv[0]);
        exit(1);
    }

    if (sscanf(argv[1], "%hx%c", &devnum, &c) != 1) {
        fprintf (stderr,
                "HHC101I %s is not a valid device number\n",
                argv[1]);
        exit(1);
    }

    if (argc > 2) {
        if (strcasecmp(argv[2], "stop") != 0) {
            fprintf (stderr,
                    "HHC102I %s parameter invalid\n",
                    argv[2]);
            exit(1);
        }
        stop = 1;
    }

    /* Build system configuration */
    build_config ("hercules.cnf");

    /* Point to the device block for the IPL device */
    dev = find_device_by_devnum (devnum);
    if (dev == NULL) {
        fprintf (stderr,
                "HHC103I Device %4.4X not in configuration\n",
                devnum);
        exit(1);
    }

    /* Point to the register context for CPU 0 */
    regs = &(sysblk.regs[0]);

    /* Point to the PSA in main storage */
    psa = (PSA*)(sysblk.mainstor);

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
        fprintf (stderr,
                "HHC105I IPL failed, CSW status=%2.2X%2.2X\n",
                unitstat, chanstat);
        fprintf (stderr,
                "HHC106I Sense=");
        for (i=0; i < dev->numsense; i++)
        {
            fprintf (stderr, "%2.2X", dev->sense[i]);
            if ((i & 3) == 3) fprintf (stderr, " ");
        }
        fprintf (stderr, "\n");
        exit(1);
    }

    /* Reset CCW tracing for the IPL device */
//  dev->ccwtrace = 0;

    /* Clear the interrupt pending and device busy conditions */
    dev->pending = 0;
    dev->busy = 0;
    dev->scsw.flag2 = 0;
    dev->scsw.flag3 = 0;

#ifdef FEATURE_S370_CHANNEL
    /* Store the I/O device address at locations 184-187 */
    psa->ioid[0] = 0;
    psa->ioid[1] = 0;
    psa->ioid[2] = dev->devnum >> 8;
    psa->ioid[3] = dev->devnum & 0xFF;
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

    /* Wait for loc3270 to connect a console */
    obtain_lock (&sysblk.conslock);
    wait_condition (&sysblk.conscond, &sysblk.conslock);
    release_lock (&sysblk.conslock);
    fprintf (stderr,
            "HHC107I IPL proceeding\n");

    /* Set single-instruction stepping if STOP was specified */
    if (stop) sysblk.inststep = 1;

    /* Start CPU 0 using initial PSW at location 0 */
    start_cpu (0, regs);

    return 0;
} /* end function main */
