/* CHANNEL.C    (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Channel Emulator                             */

/*-------------------------------------------------------------------*/
/* This module contains the channel subsystem functions for the      */
/* Hercules ESA/390 emulator.                                        */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* DISPLAY DATA                                                      */
/*-------------------------------------------------------------------*/
static void display_data (U32 addr)
{
BYTE *a;

    if (addr < sysblk.mainsize - 16) {
        a = sysblk.mainstor + addr;
        printf (" Data=%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X"
                " %2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X",
                a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7],
                a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
//      if (memcmp(a+5, "\xc9\xc5\xc1\xc9\xd7\xd3\xf0\xf5", 8)==0)
//          sysblk.insttrace = 1;
    }
    printf("\n");

} /* end function display_data */

/*-------------------------------------------------------------------*/
/* DISPLAY CCW AND DATA                                              */
/*-------------------------------------------------------------------*/
static void display_ccw (DEVBLK *dev, BYTE ccw[], U32 addr)
{

    printf ("%4.4X:CCW=%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X",
            dev->devnum,
            ccw[0], ccw[1], ccw[2], ccw[3],
            ccw[4], ccw[5], ccw[6], ccw[7]);
    display_data (addr);

    if (dev->ccwstep)
        panel_command (&(sysblk.regs[0]));

} /* end function display_ccw */

#ifdef FEATURE_S370_CHANNEL
/*-------------------------------------------------------------------*/
/* DISPLAY CSW                                                       */
/*-------------------------------------------------------------------*/
static void display_csw (DEVBLK *dev)
{
    printf ("%4.4X:Stat=%2.2X%2.2X Count=%2.2X%2.2X  "
            "CCW=%2.2X%2.2X%2.2X\n",
            dev->devnum,
            dev->csw[4], dev->csw[5], dev->csw[6], dev->csw[7],
            dev->csw[1], dev->csw[2], dev->csw[3]);

} /* end function display_csw */
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
/*-------------------------------------------------------------------*/
/* DISPLAY SCSW                                                      */
/*-------------------------------------------------------------------*/
static void display_scsw (DEVBLK *dev)
{
    printf ("%4.4X:Stat=%2.2X%2.2X Count=%2.2X%2.2X  "
            "CCW=%2.2X%2.2X%2.2X%2.2X\n",
            dev->devnum,
            dev->scsw.unitstat, dev->scsw.chanstat,
            dev->scsw.count[0], dev->scsw.count[1],
            dev->scsw.ccwaddr[0], dev->scsw.ccwaddr[1],
            dev->scsw.ccwaddr[2], dev->scsw.ccwaddr[3]);

} /* end function display_scsw */
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

/*-------------------------------------------------------------------*/
/* FETCH AN INDIRECT DATA ADDRESS WORD FROM MAIN STORAGE             */
/*-------------------------------------------------------------------*/
static void fetch_idaw (BYTE code,      /* CCW operation code        */
                        BYTE ccwkey,    /* Bits 0-3=key, 4-7=zeroes  */
                        int idaseq,     /* 0=1st IDAW                */
                        U32 idawaddr,   /* Main storage addr of IDAW */
                        U32 *addr,      /* Returned IDAW contents    */
                        U16 *len,       /* Returned IDA data length  */
                        BYTE *chanstat) /* Returned channel status   */
{
U32     idaw;                           /* IDAW contents             */
U32     idapage;                        /* Addr of next IDA 2K page  */
U16     idalen;                         /* #of bytes until next page */
BYTE    storkey;                        /* Storage key               */

    /* Channel program check if IDAW is not on a fullword
       boundary or is outside limit of main storage */
    if ((idawaddr & 0x00000003)
        || idawaddr >= sysblk.mainsize)
    {
        *chanstat = CSW_PROGC;
        return;
    }

    /* Channel protection check if IDAW is fetch protected */
    storkey = sysblk.storkeys[idawaddr >> 12];
    if ((storkey & STORKEY_FETCH)
        && (storkey & STORKEY_KEY) != ccwkey)
    {
        *chanstat = CSW_PROTC;
        return;
    }

    /* Fetch IDAW from main storage */
    idaw = sysblk.mainstor[idawaddr] << 24
            | sysblk.mainstor[idawaddr+1] << 16
            | sysblk.mainstor[idawaddr+2] << 8
            | sysblk.mainstor[idawaddr+3];

    /* Channel program check if IDAW data
       location is outside main storage */
    if (idaw > sysblk.mainsize)
    {
        *chanstat = CSW_PROGC;
        return;
    }

    /* Channel program check if IDAW data location is not
       on a 2K boundary, except for the first IDAW */
    if (idaseq > 0 && (idaw & 0x7FF) != 0)
    {
        *chanstat = CSW_PROGC;
        return;
    }

    /* Calculate address of next 2K page boundary */
    idapage = (idaw + 0x800) & 0x7FFFF800;
    idalen = idapage - idaw;

    /* Return the address and length for this IDAW */
    *addr = idaw;
    *len = idalen;

} /* end function fetch_idaw */

/*-------------------------------------------------------------------*/
/* COPY DATA BETWEEN CHANNEL I/O BUFFER AND MAIN STORAGE             */
/*-------------------------------------------------------------------*/
static void copy_iobuf (BYTE code,      /* CCW operation code        */
                        BYTE flags,     /* CCW flags                 */
                        U32 addr,       /* Data address              */
                        U16 count,      /* Data count                */
                        BYTE ccwkey,    /* Protection key            */
                        BYTE *iobuf,    /* -> Channel I/O buffer     */
                        BYTE *chanstat) /* Returned channel status   */
{
U32     idawaddr;                       /* Main storage addr of IDAW */
U16     idacount;                       /* IDA bytes remaining       */
int     idaseq;                         /* IDA sequence number       */
U32     idadata;                        /* IDA data address          */
U16     idalen;                         /* IDA data length           */
BYTE    storkey;                        /* Storage key               */
int     i, firstpage, lastpage;         /* 4K page numbers           */
BYTE    readcmd;                        /* 1=READ, SENSE, or RDBACK  */

    /* Set flag to indicate direction of data movement */
    readcmd = IS_CCW_READ(code)
              || IS_CCW_SENSE(code)
              || IS_CCW_RDBACK(code);

    /* Move data when indirect data addressing is used */
    if (flags & CCW_FLAGS_IDA)
    {
        idawaddr = addr;
        idacount = count;

        for (idaseq = 0; idacount > 0; idaseq++)
        {
            /* Fetch the IDAW and set IDA pointer and length */
            fetch_idaw (code, ccwkey, idaseq, idawaddr,
                        &idadata, &idalen, chanstat);

            printf ("IDAW@%8.8lX:%8.8lX len=%4.4X\n",
                    idawaddr, idadata, idalen);
            if (*chanstat != 0) return;

            /* Channel protection check if IDAW data location is
               fetch protected, or if location is store protected
               and command is READ, READ BACKWARD, or SENSE */
            storkey = sysblk.storkeys[idadata >> 12];
            if ((storkey & STORKEY_KEY) != ccwkey
                && ((storkey & STORKEY_FETCH) || readcmd))
            {
                *chanstat = CSW_PROTC;
                return;
            }

            /* Reduce length if less than one page remaining */
            if (idalen > idacount) idalen = idacount;

            /* Copy data between main storage and channel buffer */
            if (readcmd)
                memcpy (sysblk.mainstor + idadata, iobuf, idalen);
            else
                memcpy (iobuf, sysblk.mainstor + idadata, idalen);

            /* Decrement remaining count, increment buffer pointer */
            idacount -= idalen;
            iobuf += idalen;

            /* Increment to next IDAW address */
            idawaddr += 4;

        } /* end for(idaseq) */

    } else {                            /* Non-IDA data addressing */

        /* Channel program check if data is outside main storage */
        if (addr > sysblk.mainsize || sysblk.mainsize - count < addr)
        {
            *chanstat = CSW_PROGC;
            return;
        }

        /* Channel protection check if any data is fetch protected,
           or if location is store protected and command is READ,
           READ BACKWARD, or SENSE */
        firstpage = addr >> 12;
        lastpage = (addr + count - 1) >> 12;
        for (i = firstpage; i <= lastpage; i++)
        {
            storkey = sysblk.storkeys[i];
            if ((storkey & STORKEY_KEY) != ccwkey
                && ((storkey & STORKEY_FETCH) || readcmd))
            {
                *chanstat = CSW_PROTC;
                return;
            }
        } /* end for(i) */

        /* Copy data between main storage and channel buffer */
        if (readcmd)
            memcpy (sysblk.mainstor + addr, iobuf, count);
        else
            memcpy (iobuf, sysblk.mainstor + addr, count);

    } /* end if(!IDA) */

} /* end function copy_iobuf */

/*-------------------------------------------------------------------*/
/* START A CHANNEL PROGRAM                                           */
/*-------------------------------------------------------------------*/
int start_io (DEVBLK *dev, U32 ccwaddr, int ccwfmt, BYTE ccwkey,
                U32 ioparm)
{

    /* Obtain the device lock */
    obtain_lock (&dev->lock);

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Return condition code 1 if status pending */
    if (dev->scsw.flag3 & SCSW3_SC_PEND)
    {
        release_lock (&dev->lock);
        return 1;
    }
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Return condition code 2 if device is busy */
    if (dev->busy || dev->pending)
    {
        release_lock (&dev->lock);
        return 2;
    }

    /* Set the device busy indicator */
    dev->busy = 1;

    /* Release the device lock */
    release_lock (&dev->lock);

    /* Store the start I/O parameters in the device block */
    dev->ccwaddr = ccwaddr;
    dev->ccwfmt = ccwfmt;
    dev->ccwkey = ccwkey;
    dev->ioparm = ioparm;

    /* Execute the CCW chain on a separate thread */
    if ( create_thread (&dev->tid, &sysblk.detattr,
                        execute_ccw_chain, dev) )
    {
        perror("start_io: create_thread");
        return 2;
    }

    /* Return with condition code zero */
    return 0;

} /* end function start_io */

/*-------------------------------------------------------------------*/
/* EXECUTE A CHANNEL PROGRAM                                         */
/*-------------------------------------------------------------------*/
void *execute_ccw_chain (DEVBLK *dev)
{
U32     ccwaddr = dev->ccwaddr;         /* Address of CCW            */
int     ccwfmt = dev->ccwfmt;           /* CCW format (0 or 1)       */
BYTE    ccwkey = dev->ccwkey;           /* Protection key            */
BYTE    storkey;                        /* Storage key               */
BYTE    code = 0;                       /* CCW operation code        */
BYTE    flags;                          /* CCW flags                 */
U32     addr;                           /* CCW data address          */
U16     count;                          /* CCW byte count            */
BYTE   *ccw;                            /* CCW pointer               */
BYTE    unitstat;                       /* Unit status               */
BYTE    chanstat;                       /* Channel status            */
U16     residual;                       /* Residual byte count       */
BYTE    more;                           /* 1=Count exhausted         */
BYTE    tic = 1;                        /* Previous CCW was a TIC    */
BYTE    chain = 1;                      /* 1=Chain to next CCW       */
BYTE    chained = 0;                    /* Command chain and data chain
                                           bits from previous CCW    */
BYTE    prevcode = 0;                   /* Previous CCW opcode       */
DEVXF  *devexec;                        /* -> Execute CCW function   */
int     num;                            /* Number of bytes to move   */
BYTE    iobuf[65536];                   /* Channel I/O buffer        */

    /* Point to the device handler for this device */
    devexec = dev->devexec;

    /* Execute the CCW chain */
    while ( chain )
    {
        /* Clear the channel status and unit status */
        chanstat = 0;
        unitstat = 0;

        /* Channel program check if CCW is not on a doubleword
           boundary or is outside limit of main storage */
        if ((ccwaddr & 0x00000007) || ccwaddr >= sysblk.mainsize)
        {
            chanstat = CSW_PROGC;
            break;
        }

        /* Channel protection check if CCW is fetch protected */
        storkey = sysblk.storkeys[ccwaddr >> 12];
        if ((storkey & STORKEY_FETCH)
            && (storkey & STORKEY_KEY) != ccwkey)
        {
            chanstat = CSW_PROTC;
            break;
        }

        /* Point to the CCW in main storage */
        ccw = sysblk.mainstor + ccwaddr;

        /* Increment to next CCW address */
        ccwaddr += 8;

        /* Extract CCW flags, byte count, and data address */
        if (ccwfmt == 0)
        {
            addr = ((U32)(ccw[1]) << 16) | ((U32)(ccw[2]) << 8)
                   | ccw[3];
            flags = ccw[4];
            count = ((U16)(ccw[6]) << 8) | ccw[7];
        }
        else
        {
            flags = ccw[1];
            count = ((U16)(ccw[2]) << 8) | ccw[3];
            addr = ((U32)(ccw[4]) << 24) | ((U32)(ccw[5]) << 16)
                   | ((U32)(ccw[6]) << 8) | ccw[7];
        }

        /* Display the CCW */
        if (dev->ccwtrace || dev->ccwstep)
            display_ccw (dev, ccw, addr);

        /*----------------------------------------------*/
        /* TRANSFER IN CHANNEL (TIC) command            */
        /*----------------------------------------------*/
        if (IS_CCW_TIC(ccw[0]))
        {
            /* Channel program check if TIC-to-TIC */
            if (tic)
            {
                chanstat = CSW_PROGC;
                break;
            }

            /* Channel program check if format-1 TIC reserved bits set*/
            if (ccwfmt == 1
                && (ccw[0] != 0x08 || flags != 0 || count != 0))
            {
                chanstat = CSW_PROGC;
                break;
            }

            /* Set new CCW address (leaving the values of chained and
               code untouched to allow data-chaining through TIC) */
            tic = 1;
            ccwaddr = addr;
            chain = 1;
            continue;
        } /* end if TIC */

        /*----------------------------------------------*/
        /* Commands other than TRANSFER IN CHANNEL      */
        /*----------------------------------------------*/
        /* Reset the TIC-to-TIC flag */
        tic = 0;

        /* Extract CCW opcode, unless data chaining */
        if ((chained & CCW_FLAGS_CD) == 0)
        {
            prevcode = code;
            code = ccw[0];
        }

        /* Channel program check if invalid flags */
        if (flags & CCW_FLAGS_RESV)
        {
            chanstat = CSW_PROGC;
            break;
        }

        /* Channel program check if unsupported flags */
        if (flags & (CCW_FLAGS_PCI | CCW_FLAGS_SUSP))
        {
            printf ("channel: Unsupported PCI/SUSP for device %4.4X\n",
                    dev->devnum);
            chanstat = CSW_PROGC;
            break;
        }

        /* Channel program check if invalid count */
        if (count == 0 && (ccwfmt == 0 ||
            (flags & CCW_FLAGS_CD) || (chained & CCW_FLAGS_CD)))
        {
            chanstat = CSW_PROGC;
            break;
        }

        /* For WRITE and CONTROL operations, copy data
           from main storage into channel buffer */
        if (IS_CCW_WRITE(code) || IS_CCW_CONTROL(code))
        {
            copy_iobuf (code, flags, addr, count,
                        ccwkey, iobuf, &chanstat);
            if (chanstat != 0) break;
        }

        /* Set chaining flag */
        chain = ( flags & (CCW_FLAGS_CD | CCW_FLAGS_CC) ) ? 1 : 0;

        /* Initialize residual byte count */
        residual = count;
        more = 0;

        /* Process depending on CCW opcode */
        if (code == 0x04) {
            /*-------------------------------------------------------*/
            /* Basic SENSE                                           */
            /*-------------------------------------------------------*/

            /* Obtain the device lock */
            obtain_lock (&dev->lock);

            /* Calculate residual byte count */
            num = (count < dev->numsense) ? count : dev->numsense;
            residual = count - num;
            if (count < dev->numsense) more = 1;

            /* Copy device sense bytes to channel I/O buffer */
            memcpy (iobuf, dev->sense, num);

            /* Clear the device sense bytes */
            memset (dev->sense, 0, sizeof(dev->sense));

            /* Release the device lock */
            release_lock (&dev->lock);

            /* Set unit status */
            unitstat = CSW_CE | CSW_DE;

        }

        else if (code == 0xE4) {
            /*-------------------------------------------------------*/
            /* SENSE ID                                              */
            /*-------------------------------------------------------*/

            /* Calculate residual byte count */
            num = (count < dev->numdevid) ? count : dev->numdevid;
            residual = count - num;
            if (count < dev->numdevid) more = 1;

            /* Copy device identifier bytes to channel I/O buffer */
            memcpy (iobuf, dev->devid, num);

            /* Set unit status */
            unitstat = CSW_CE | CSW_DE;

        }

        else if (IS_CCW_WRITE(code) || IS_CCW_READ(code)
                || IS_CCW_CONTROL(code) || IS_CCW_SENSE(code)
                || IS_CCW_RDBACK(code)) {
            /*-------------------------------------------------------*/
            /* WRITE, READ, CONTROL, other SENSE, and READ BACKWARD  */
            /*-------------------------------------------------------*/

            /* Pass the CCW to the device handler for execution */
            (*devexec) (dev, code, flags, chained, count, prevcode,
                        iobuf, &more, &unitstat, &residual);

        }

        else {
            /*-------------------------------------------------------*/
            /* INVALID OPERATION                                     */
            /*-------------------------------------------------------*/

                chanstat = CSW_PROGC;
                chain = 0;

        } /* end if(code) */

        /* For READ, SENSE, and READ BACKWARD operations, copy data
           from channel buffer to main storage, unless SKIP is set */
        if ((flags & CCW_FLAGS_SKIP) == 0
            && (IS_CCW_READ(code)
                || IS_CCW_SENSE(code)
                || IS_CCW_RDBACK(code)))
        {
            copy_iobuf (code, flags, addr, count - residual,
                        ccwkey, iobuf, &chanstat);
        }

        /* Check for incorrect length */
        if (residual != 0 || (more && (CCW_FLAGS_CD == 0)))
        {
            /* Set incorrect length status if data chaining or
               or if suppress length indication flag is off */
            if ((flags & CCW_FLAGS_CD)
                || (flags & CCW_FLAGS_SLI) == 0)
                chanstat |= CSW_IL;
        }

        /* Trace the results of CCW execution */
        if (dev->ccwtrace || dev->ccwstep)
        {
            /* Display status and residual byte count */
            printf ("%4.4X:Stat=%2.2X%2.2X Count=%4.4X ",
                    dev->devnum, unitstat, chanstat, residual);

            /* Display data for READ or SENSE commands only */
            if (IS_CCW_READ(code) || IS_CCW_SENSE(code))
                display_data (addr);
            else
                printf ("\n");
        }

        /* Increment CCW address if device returned status modifier */
        if (unitstat & CSW_SM)
            ccwaddr += 8;

        /* Terminate the channel program if any unusual status */
        if (chanstat != 0
            || (unitstat & ~CSW_SM) != (CSW_CE | CSW_DE))
            chain = 0;

        /* Update the chaining flags */
        chained = flags & (CCW_FLAGS_CD | CCW_FLAGS_CC);

    } /* end while(chain) */

#ifdef FEATURE_S370_CHANNEL
    /* Build the channel status word */
    dev->csw[0] = ccwkey & 0xF0;
    dev->csw[1] = (ccwaddr & 0xFF0000) >> 16;
    dev->csw[2] = (ccwaddr & 0xFF00) >> 8;
    dev->csw[3] = ccwaddr & 0xFF;
    dev->csw[4] = unitstat;
    dev->csw[5] = chanstat;
    dev->csw[6] = (residual & 0xFF00) >> 8;
    dev->csw[7] = residual & 0xFF;
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Complete the subchannel status word */
    dev->scsw.flag0 = ccwkey & 0xF0;
    dev->scsw.flag1 = (ccwfmt == 1)? SCSW1_F : 0;
    dev->scsw.flag2 = SCSW2_FC_START;
    dev->scsw.flag3 = SCSW3_SC_PRI | SCSW3_SC_SEC | SCSW3_SC_PEND;
    dev->scsw.ccwaddr[0] = (ccwaddr & 0x7F000000) >> 24;
    dev->scsw.ccwaddr[1] = (ccwaddr & 0xFF0000) >> 16;
    dev->scsw.ccwaddr[2] = (ccwaddr & 0xFF00) >> 8;
    dev->scsw.ccwaddr[3] = ccwaddr & 0xFF;
    dev->scsw.unitstat = unitstat;
    dev->scsw.chanstat = chanstat;
    dev->scsw.count[0] = (residual & 0xFF00) >> 8;
    dev->scsw.count[1] = residual & 0xFF;
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Set the interrupt pending flag for this device */
    dev->busy = 0;
    dev->pending = 1;

    /* Signal waiting CPUs that an interrupt is pending */
    obtain_lock (&sysblk.intlock);
    signal_condition (&sysblk.intcond);
    release_lock (&sysblk.intlock);

    return NULL;

} /* end function execute_ccw_chain */

/*-------------------------------------------------------------------*/
/* SUBROUTINE TO WAIT FOR ONE MICROSECOND                            */
/*-------------------------------------------------------------------*/
static inline void yield (void)
{
static struct timeval tv_1usec = {0, 1};

    select (1, NULL, NULL, NULL, &tv_1usec);

} /* end function yield */

#ifdef FEATURE_S370_CHANNEL
/*-------------------------------------------------------------------*/
/* TEST CHANNEL                                                      */
/*-------------------------------------------------------------------*/
int test_channel (REGS *regs, U16 chan)
{
int     devcount = 0;                   /* #of devices on channel    */
DEVBLK *dev;                            /* -> Device control block   */

    /* Find a device on specified channel with pending interrupt */
    for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
    {
        /* Skip the device if not on specified channel */
        if ((dev->devnum & 0xFF00) != chan)
            continue;

        /* Count devices on channel */
        devcount++;

        /* Exit with condition code 1 if interrupt pending */
        if (dev->pending)
            return 1;

    } /* end for(dev) */

    /* Exit with condition code 3 if no devices on channel */
    if (devcount == 0)
        return 3;

    /* Exit with condition code 0 indicating channel available */
    return 0;

} /* end function test_channel */

/*-------------------------------------------------------------------*/
/* TEST I/O                                                          */
/*-------------------------------------------------------------------*/
int test_io (REGS *regs, DEVBLK *dev, BYTE ibyte)
{
int     cc;                             /* Condition code            */
PSA    *psa;                            /* -> Prefixed storage area  */

    /* Obtain the device lock */
    obtain_lock (&dev->lock);

    /* Test device status and set condition code */
    if (dev->busy)
    {
        /* Wait for one microsecond */
//      yield ();

        /* Set condition code 2 if device is busy */
        cc = 2;
    }
    else if (dev->pending)
    {
        /* Set condition code 1 if interrupt pending */
        cc = 1;

        /* Store the channel status word at PSA+X'40' */
        psa = (PSA*)(sysblk.mainstor + regs->pxr);
        memcpy (psa->csw, dev->csw, 8);
        if (dev->ccwtrace || dev->ccwstep)
            display_csw (dev);

        /* Clear the pending interrupt */
        dev->pending = 0;
    }
    else
    {
        /* Set condition code 0 if device is available */
        cc = 0;
    }

    /* Release the device lock */
    release_lock (&dev->lock);

    /* Return the condition code */
    return cc;

} /* end function test_io */
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
/*-------------------------------------------------------------------*/
/* TEST SUBCHANNEL                                                   */
/*-------------------------------------------------------------------*/
/* Input                                                             */
/*      regs    -> CPU register context                              */
/*      dev     -> Device control block                              */
/* Output                                                            */
/*      irb     -> Interruption response block                       */
/* Return value                                                      */
/*      The return value is the condition code for the TSCH          */
/*      instruction:  0=status was pending and is now cleared,       */
/*      1=no status was pending.  The IRB is updated in both cases.  */
/*-------------------------------------------------------------------*/
int test_subchan (REGS *regs, DEVBLK *dev, IRB *irb)
{
int     cc;                             /* Condition code            */

    /* Obtain the device lock */
    obtain_lock (&dev->lock);

    /* Copy the subchannel status word to the IRB */
    irb->scsw = dev->scsw;

    /* Build the extended status word in the IRB */
    memset (&irb->esw, 0, sizeof(ESW));
    irb->esw.lpum = 0x80;
    irb->esw.scl2 = SCL2_FVF_LPUM
                    | SCL2_FVF_USTAT | SCL2_FVF_CCWAD;

    /* Zeroize the extended control word in the IRB */
    memset (irb->ecw, 0, sizeof(irb->ecw));

    /* Clear any pending interrupt */
    dev->pending = 0;

    /* Test device status and set condition code */
    if (dev->scsw.flag3 & SCSW3_SC_PEND)
    {
        /* Set condition code 0 if status pending */
        cc = 0;

        /* Display the subchannel status word */
        if (dev->ccwtrace || dev->ccwstep)
            display_scsw (dev);

        /* Clear the subchannel status bits in the device block */
        dev->scsw.flag2 &= ~(SCSW2_FC | SCSW2_AC);
        dev->scsw.flag3 &= ~(SCSW3_SC);

        /* Signal tn3270 thread to redrive select */
        if (dev->devtype == 0x3270)
            signal_thread (sysblk.tid3270, SIGHUP);
    }
    else
    {
        /* Wait for one microsecond */
//      yield ();

        /* Set condition code 1 if status not pending */
        cc = 1;
    }

    /* Release the device lock */
    release_lock (&dev->lock);

    /* Return the condition code */
    return cc;

} /* end function test_subchan */
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

/*-------------------------------------------------------------------*/
/* TEST WHETHER INTERRUPTS ARE ENABLED FOR THE SPECIFIED DEVICE      */
/* When configured for S/370 channels, the PSW system mask and/or    */
/* the channel masks in control register 2 determine whether the     */
/* device is enabled.  When configured for the XA or ESA channel     */
/* subsystem, the interrupt subclass masks in control register 6     */
/* determine eligability; the PSW system mask is not tested, because */
/* the TPI instruction can operate with I/O interrupts masked off.   */
/* Returns non-zero if interrupts enabled, 0 if interrupts disabled. */
/*-------------------------------------------------------------------*/
static int interrupt_enabled (REGS *regs, DEVBLK *dev)
{
int     i;                              /* Interruption subclass     */

#ifdef FEATURE_S370_CHANNEL
    /* Isolate the channel number */
    i = dev->devnum >> 8;
    if (regs->psw.ecmode == 0 && i < 6)
    {
        /* For BC mode channels 0-5, test system mask bits 0-5 */
        if ((regs->psw.sysmask & (0x80 >> i)) == 0)
            return 0;
    }
    else
    {
        /* For EC mode and channels 6-31, test system mask bit 6 */
        if ((regs->psw.sysmask & PSW_IOMASK) == 0)
            return 0;

        /* If I/O mask is enabled, test channel masks in CR2 */
        if (i > 31) i = 31;
        if ((regs->cr[2] & (0x80000000 >> i)) == 0)
            return 0;
    }
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Ignore this device if subchannel not valid and enabled */
    if ((dev->pmcw.flag5 & (PMCW5_E | PMCW5_V)) != (PMCW5_E | PMCW5_V))
        return 0;

    /* Isolate the interruption subclass */
    i = (dev->pmcw.flag4 & PMCW4_ISC) >> 3;

    /* Test interruption subclass mask bit in CR6 */
    if ((regs->cr[6] & (0x80000000 >> i)) == 0)
        return 0;
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Interrupts are enabled for this device */
    return 1;
} /* end function interrupt_enabled */

/*-------------------------------------------------------------------*/
/* PRESENT PENDING I/O INTERRUPT                                     */
/* Finds a device with a pending condition for which an interrupt    */
/* is allowed by the CPU whose regs structure is passed as a         */
/* parameter.  Clears the interrupt condition and returns the        */
/* I/O address and I/O interruption parameter (for channel subsystem)*/
/* or the I/O address and CSW (for S/370 channels).                  */
/* This routine does not perform a PSW switch.                       */
/* The return value is the condition code for the TPI instruction:   */
/* 0 if no allowable pending interrupt exists, otherwise 1.          */
/*-------------------------------------------------------------------*/
int
present_io_interrupt (REGS *regs, U32 *ioid, U32 *ioparm, BYTE *csw)
{
DEVBLK *dev;                            /* -> Device control block   */

    /* Find a device with pending interrupt */
    for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
    {
        obtain_lock (&dev->lock);
        if (dev->pending && interrupt_enabled(regs, dev))
            break;
        release_lock (&dev->lock);
    } /* end for(dev) */

    /* If no interrupt pending, exit with condition code 0 */
    if (dev == NULL)
    {
        /* Wait for one microsecond */
        yield ();

        return 0;
    }

#ifdef FEATURE_S370_CHANNEL
    /* Extract the I/O address and CSW */
    *ioid = dev->devnum;
    memcpy (csw, dev->csw, 8);
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Extract the I/O address and interrupt parameter */
    *ioid = 0x00010000 | dev->subchan;
    *ioparm = dev->ioparm;
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Reset the interrupt pending and busy flags for the device */
    dev->pending = 0;
    dev->busy = 0;

    /* Release the device lock */
    release_lock (&dev->lock);

    /* Exit with condition code indicating interrupt cleared */
    return 1;

} /* end function present_io_interrupt */

