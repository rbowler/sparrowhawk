/* CHANNEL.C    (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Channel Emulator                             */

/*-------------------------------------------------------------------*/
/* This module contains the channel subsystem functions for the      */
/* Hercules S/370 and ESA/390 emulator.                              */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* FORMAT I/O BUFFER DATA                                            */
/*-------------------------------------------------------------------*/
static void format_iobuf_data (U32 addr, BYTE *area)
{
BYTE   *a;                              /* -> Byte in main storage   */
int     i, j;                           /* Array subscripts          */
BYTE    c;                              /* Character work area       */

    area[0] = '\0';
    if (addr < sysblk.mainsize - 16)
    {
        a = sysblk.mainstor + addr;
        j = sprintf (area,
                "=>%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X"
                " %2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X ",
                a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7],
                a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);

        for (i = 0; i < 16; i++)
        {
            c = ebcdic_to_ascii[*a++];
            if (!isprint(c)) c = '.';
            area[j++] = c;
        }
        area[j] = '\0';
    }

} /* end function format_iobuf_data */

/*-------------------------------------------------------------------*/
/* DISPLAY CHANNEL COMMAND WORD AND DATA                             */
/*-------------------------------------------------------------------*/
static void display_ccw (DEVBLK *dev, BYTE ccw[], U32 addr)
{
BYTE    area[64];                       /* Data display area         */

    format_iobuf_data (addr, area);
    logmsg ("%4.4X:CCW=%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X%s\n",
            dev->devnum,
            ccw[0], ccw[1], ccw[2], ccw[3],
            ccw[4], ccw[5], ccw[6], ccw[7], area);

//  if (dev->ccwstep)
//      panel_command (&(sysblk.regs[0]));

} /* end function display_ccw */

#ifdef FEATURE_S370_CHANNEL
/*-------------------------------------------------------------------*/
/* DISPLAY CHANNEL STATUS WORD                                       */
/*-------------------------------------------------------------------*/
static void display_csw (DEVBLK *dev, BYTE csw[])
{
    logmsg ("%4.4X:Stat=%2.2X%2.2X Count=%2.2X%2.2X  "
            "CCW=%2.2X%2.2X%2.2X\n",
            dev->devnum,
            csw[4], csw[5], csw[6], csw[7],
            csw[1], csw[2], csw[3]);

} /* end function display_csw */
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
/*-------------------------------------------------------------------*/
/* DISPLAY SUBCHANNEL STATUS WORD                                    */
/*-------------------------------------------------------------------*/
static void display_scsw (DEVBLK *dev, SCSW scsw)
{
    logmsg ("%4.4X:SCSW=%2.2X%2.2X%2.2X%2.2X "
            "Stat=%2.2X%2.2X Count=%2.2X%2.2X  "
            "CCW=%2.2X%2.2X%2.2X%2.2X\n",
            dev->devnum,
            scsw.flag0, scsw.flag1, scsw.flag2, scsw.flag3,
            scsw.unitstat, scsw.chanstat,
            scsw.count[0], scsw.count[1],
            scsw.ccwaddr[0], scsw.ccwaddr[1],
            scsw.ccwaddr[2], scsw.ccwaddr[3]);

} /* end function display_scsw */
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

/*-------------------------------------------------------------------*/
/* FETCH A CHANNEL COMMAND WORD FROM MAIN STORAGE                    */
/*-------------------------------------------------------------------*/
static void fetch_ccw (
                        BYTE ccwkey,    /* Bits 0-3=key, 4-7=zeroes  */
                        int ccwfmt,     /* CCW format (0 or 1)       */
                        U32 ccwaddr,    /* Main storage addr of CCW  */
                        BYTE *code,     /* Returned operation code   */
                        U32 *addr,      /* Returned data address     */
                        BYTE *flags,    /* Returned flags            */
                        U16 *count,     /* Returned data count       */
                        BYTE *chanstat) /* Returned channel status   */
{
BYTE    storkey;                        /* Storage key               */
BYTE   *ccw;                            /* CCW pointer               */

    /* Channel program check if CCW is not on a doubleword
       boundary or is outside limit of main storage */
    if ((ccwaddr & 0x00000007) || ccwaddr >= sysblk.mainsize)
    {
        *chanstat = CSW_PROGC;
        return;
    }

    /* Channel protection check if CCW is fetch protected */
    storkey = STORAGE_KEY(ccwaddr);
    if (ccwkey != 0 && (storkey & STORKEY_FETCH)
        && (storkey & STORKEY_KEY) != ccwkey)
    {
        *chanstat = CSW_PROTC;
        return;
    }

    /* Set the main storage reference bit for the CCW location */
    STORAGE_KEY(ccwaddr) |= STORKEY_REF;

    /* Point to the CCW in main storage */
    ccw = sysblk.mainstor + ccwaddr;

    /* Extract CCW opcode, flags, byte count, and data address */
    if (ccwfmt == 0)
    {
        *code = ccw[0];
        *addr = ((U32)(ccw[1]) << 16) | ((U32)(ccw[2]) << 8)
                    | ccw[3];
        *flags = ccw[4];
        *count = ((U16)(ccw[6]) << 8) | ccw[7];
    }
    else
    {
        *code = ccw[0];
        *flags = ccw[1];
        *count = ((U16)(ccw[2]) << 8) | ccw[3];
        *addr = ((U32)(ccw[4]) << 24) | ((U32)(ccw[5]) << 16)
                    | ((U32)(ccw[6]) << 8) | ccw[7];
    }

} /* end function fetch_ccw */

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
    storkey = STORAGE_KEY(idawaddr);
    if (ccwkey != 0 && (storkey & STORKEY_FETCH)
        && (storkey & STORKEY_KEY) != ccwkey)
    {
        *chanstat = CSW_PROTC;
        return;
    }

    /* Set the main storage reference bit for the IDAW location */
    STORAGE_KEY(idawaddr) |= STORKEY_REF;

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
static void copy_iobuf (DEVBLK *dev,    /* -> Device block           */
                        BYTE code,      /* CCW operation code        */
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
BYTE    area[64];                       /* Data display area         */

    /* Exit if no bytes are to be copied */
    if (count == 0)
        return;

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

            /* Exit if fetch_idaw detected channel program check */
            if (*chanstat != 0) return;

            /* Channel protection check if IDAW data location is
               fetch protected, or if location is store protected
               and command is READ, READ BACKWARD, or SENSE */
            storkey = STORAGE_KEY(idadata);
            if (ccwkey != 0 && (storkey & STORKEY_KEY) != ccwkey
                && ((storkey & STORKEY_FETCH) || readcmd))
            {
                *chanstat = CSW_PROTC;
                return;
            }

            /* Reduce length if less than one page remaining */
            if (idalen > idacount) idalen = idacount;

            /* Set the main storage reference and change bits */
            STORAGE_KEY(idadata) |=
                (readcmd ? (STORKEY_REF|STORKEY_CHANGE) : STORKEY_REF);

            /* Copy data between main storage and channel buffer */
            if (readcmd)
                memcpy (sysblk.mainstor + idadata, iobuf, idalen);
            else
                memcpy (iobuf, sysblk.mainstor + idadata, idalen);

            /* Display the IDAW if CCW tracing is on */
            if (dev->ccwtrace || dev->ccwstep)
            {
                format_iobuf_data (idadata, area);
                logmsg ("%4.4X:IDAW=%8.8X Len=%3.3X%s\n",
                        dev->devnum, idadata, idalen, area);
            }

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
        firstpage = addr >> STORAGE_KEY_PAGESHIFT;
        lastpage = (addr + count - 1) >> STORAGE_KEY_PAGESHIFT;
        for (i = firstpage; i <= lastpage; i++)
        {
            storkey = sysblk.storkeys[i];
            if (ccwkey != 0 && (storkey & STORKEY_KEY) != ccwkey
                && ((storkey & STORKEY_FETCH) || readcmd))
            {
                *chanstat = CSW_PROTC;
                return;
            }
        } /* end for(i) */

        /* Set the main storage reference and change bits */
        for (i = firstpage; i <= lastpage; i++)
        {
            sysblk.storkeys[i] |=
                (readcmd ? (STORKEY_REF|STORKEY_CHANGE) : STORKEY_REF);
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
/* This function is called by the SIO and SSCH instructions          */
/*-------------------------------------------------------------------*/
/* Input                                                             */
/*      dev     -> Device control block                              */
/*      ioparm  I/O interruption parameter from ORB                  */
/*      orb4    ORB byte 4 (protect key and suspend control bit)     */
/*      orb5    ORB byte 5 (CCW format, prefetch control, initial    */
/*              status interrupt control, address limit checking,    */
/*              and suppress suspend interrupt control bits)         */
/*      orb6    ORB byte 6 (logical path mask)                       */
/*      orb7    ORB byte 7 (incorrect length suppression control)    */
/*      ccwaddr Absolute address of start of channel program         */
/* Output                                                            */
/*      The I/O parameters are stored in the device block, and a     */
/*      thread is created to execute the CCW chain asynchronously.   */
/*      The return value is the condition code for the SIO or        */
/*      SSCH instruction.                                            */
/* Note                                                              */
/*      For S/370 SIO, only the protect key and CCW address are      */
/*      valid, all other ORB parameters are set to zero.             */
/*-------------------------------------------------------------------*/
int start_io (DEVBLK *dev, U32 ioparm, BYTE orb4, BYTE orb5,
                BYTE orb6, BYTE orb7, U32 ccwaddr)
{

    /* Obtain the device lock */
    obtain_lock (&dev->lock);

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Return condition code 1 if status pending */
    if ((dev->scsw.flag3 & SCSW3_SC_PEND)
        || (dev->pciscsw.flag3 & SCSW3_SC_PEND))
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

#ifdef FEATURE_S370_CHANNEL
#endif /*FEATURE_S370_CHANNEL*/

    /* Set the device busy indicator */
    dev->busy = 1;

    /* Signal console thread to redrive select */
    if (dev->console)
    {
        signal_thread (sysblk.cnsltid, SIGHUP);
    }

    /* Store the start I/O parameters in the device block */
    dev->ccwaddr = ccwaddr;
    dev->ccwfmt = (orb5 & ORB5_F) ? 1 : 0;
    dev->ccwkey = orb4 & ORB4_KEY;

    /* Initialize the subchannel status word */
    memset (&dev->scsw, 0, sizeof(SCSW));
    memset (&dev->pciscsw, 0, sizeof(SCSW));
    dev->scsw.flag0 = (dev->ccwkey & SCSW0_KEY);
    if (orb4 & ORB4_S) dev->scsw.flag0 |= SCSW0_S;
    if (orb5 & ORB5_F) dev->scsw.flag1 |= SCSW1_F;
    if (orb5 & ORB5_P) dev->scsw.flag1 |= SCSW1_P;
    if (orb5 & ORB5_I) dev->scsw.flag1 |= SCSW1_I;
    if (orb5 & ORB5_A) dev->scsw.flag1 |= SCSW1_A;
    if (orb5 & ORB5_U) dev->scsw.flag1 |= SCSW1_U;

    /* Make the subchannel start-pending */
    dev->scsw.flag2 = SCSW2_FC_START | SCSW2_AC_START;

    /* Copy the I/O parameter to the path management control word */
    dev->pmcw.intparm[0] = (ioparm >> 24) & 0xFF;
    dev->pmcw.intparm[1] = (ioparm >> 16) & 0xFF;
    dev->pmcw.intparm[2] = (ioparm >> 8) & 0xFF;
    dev->pmcw.intparm[3] = ioparm & 0xFF;

    /* Release the device lock */
    release_lock (&dev->lock);

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
BYTE    ccwkey = dev->ccwkey;           /* Bits 0-3=key, 4-7=zero    */
BYTE    opcode;                         /* CCW operation code        */
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
BYTE    prev_chained = 0;               /* Chaining flags from CCW
                                           preceding the data chain  */
BYTE    code = 0;                       /* Current CCW opcode        */
BYTE    prevcode = 0;                   /* Previous CCW opcode       */
BYTE    tracethis = 0;                  /* 1=Trace this CCW only     */
BYTE    area[64];                       /* Message area              */
DEVXF  *devexec;                        /* -> Execute CCW function   */
int     ccwseq = 0;                     /* CCW sequence number       */
int     bufpos = 0;                     /* Position in I/O buffer    */
BYTE    iobuf[65536];                   /* Channel I/O buffer        */

    /* Point to the device handler for this device */
    devexec = dev->devexec;

    /* Turn off the start pending bit in the SCSW */
    dev->scsw.flag2 &= ~SCSW2_AC_START;

    /* Set the subchannel active and device active bits in the SCSW */
    dev->scsw.flag3 |= (SCSW3_AC_SCHAC | SCSW3_AC_DEVAC);

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Generate an initial status I/O interruption if requested */
    if (dev->scsw.flag1 & SCSW1_I)
    {
        /* Obtain the device lock */
        obtain_lock (&dev->lock);

        /* Update the CCW address in the SCSW */
        dev->scsw.ccwaddr[0] = (ccwaddr & 0xFF000000) >> 24;
        dev->scsw.ccwaddr[1] = (ccwaddr & 0xFF0000) >> 16;
        dev->scsw.ccwaddr[2] = (ccwaddr & 0xFF00) >> 8;
        dev->scsw.ccwaddr[3] = ccwaddr & 0xFF;

        /* Set the zero condition-code flag in the SCSW */
        dev->scsw.flag1 |= SCSW1_Z;

        /* Set intermediate status in the SCSW */
        dev->scsw.flag3 = SCSW3_SC_INTER | SCSW3_SC_PEND;

        /* Set interrupt pending flag */
        dev->pending = 1;

        /* Release the device lock */
        release_lock (&dev->lock);

        logmsg ("channel: Device %4.4X initial status interrupt\n",
                dev->devnum);

        /* Signal waiting CPUs that interrupt is pending */
        obtain_lock (&sysblk.intlock);
        sysblk.iopending = 1;
        signal_condition (&sysblk.intcond);
        release_lock (&sysblk.intlock);
    }
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Execute the CCW chain */
    while ( chain )
    {
        /* Clear the channel status and unit status */
        chanstat = 0;
        unitstat = 0;

        /* Fetch the next CCW */
        fetch_ccw (ccwkey, ccwfmt, ccwaddr, &opcode, &addr,
                    &flags, &count, &chanstat);

        /* Point to the CCW in main storage */
        ccw = sysblk.mainstor + ccwaddr;

        /* Increment to next CCW address */
        ccwaddr += 8;

        /* Update the CCW address in the SCSW */
        dev->scsw.ccwaddr[0] = (ccwaddr & 0xFF000000) >> 24;
        dev->scsw.ccwaddr[1] = (ccwaddr & 0xFF0000) >> 16;
        dev->scsw.ccwaddr[2] = (ccwaddr & 0xFF00) >> 8;
        dev->scsw.ccwaddr[3] = ccwaddr & 0xFF;

        /* Exit if fetch_ccw detected channel program check */
        if (chanstat != 0) break;

        /* Test for halt subchannel request */
        if (dev->scsw.flag2 & SCSW2_FC_HALT)
        {
            /* Clear the halt pending flag and exit */
            dev->scsw.flag2 &= SCSW2_AC_HALT;
            break;
        }

        /* Display the CCW */
        if (dev->ccwtrace || dev->ccwstep)
            display_ccw (dev, ccw, addr);

        /*----------------------------------------------*/
        /* TRANSFER IN CHANNEL (TIC) command            */
        /*----------------------------------------------*/
        if (IS_CCW_TIC(opcode))
        {
            /* Channel program check if TIC-to-TIC */
            if (tic)
            {
                chanstat = CSW_PROGC;
                break;
            }

            /* Channel program check if format-1 TIC reserved bits set*/
            if (ccwfmt == 1
                && (opcode != 0x08 || flags != 0 || count != 0))
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

        /* Update current CCW opcode, unless data chaining */
        if ((chained & CCW_FLAGS_CD) == 0)
        {
            prevcode = code;
            code = opcode;
        }

        /* Channel program check if invalid flags */
        if (flags & CCW_FLAGS_RESV)
        {
            chanstat = CSW_PROGC;
            break;
        }

#ifdef FEATURE_S370_CHANNEL
        /* For S/370, channel program check if suspend flag is set */
        if (flags & CCW_FLAGS_SUSP)
        {
            chanstat = CSW_PROGC;
            break;
        }
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
        /* Suspend channel program if suspend flag is set */
        if (flags & CCW_FLAGS_SUSP)
        {
///*debug*/ /* Trace the CCW if not already done */
///*debug*/ if (!(dev->ccwtrace || dev->ccwstep || tracethis))
///*debug*/ {
///*debug*/     display_ccw (dev, ccw, addr);
///*debug*/     tracethis = 1;
///*debug*/ }

            /* Channel program check if the ORB suspend control bit
               was zero, or if this is a data chained CCW */
            if ((dev->scsw.flag0 & SCSW0_S) == 0
                || (chained & CCW_FLAGS_CD))
            {
                chanstat = CSW_PROGC;
                break;
            }

            /* Obtain the device lock */
            obtain_lock (&dev->lock);

            /* Suspend the device if not already resume pending */
            if ((dev->scsw.flag2 & SCSW2_AC_RESUM) == 0)
            {
                /* Set the subchannel status word to suspended */
                dev->scsw.flag3 = SCSW3_AC_SUSP
                                    | SCSW3_SC_INTER
                                    | SCSW3_SC_PEND;
                dev->scsw.unitstat = 0;
                dev->scsw.chanstat = 0;
                dev->scsw.count[0] = count >> 8;
                dev->scsw.count[1] = count & 0xFF;

                /* Generate I/O interrupt unless the ORB specified
                   that suspend interrupts are to be suppressed */
                if ((dev->scsw.flag1 & SCSW1_U) == 0)
                {
                    /* Set interrupt pending flag */
                    dev->pending = 1;

                    /* Release the device lock */
                    release_lock (&dev->lock);

                    /* Signal waiting CPUs that interrupt is pending */
                    obtain_lock (&sysblk.intlock);
                    sysblk.iopending = 1;
                    signal_condition (&sysblk.intcond);
                    release_lock (&sysblk.intlock);

                    /* Obtain the device lock */
                    obtain_lock (&dev->lock);
                }

                /* Suspend the device until resume instruction */
                if (dev->ccwtrace || dev->ccwstep || tracethis)
                    logmsg ("channel: Device %4.4X suspended\n",
                            dev->devnum);

                while ((dev->scsw.flag2 & SCSW2_AC_RESUM) == 0)
                    wait_condition (&dev->resumecond, &dev->lock);

                if (dev->ccwtrace || dev->ccwstep || tracethis)
                    logmsg ("channel: Device %4.4X resumed\n",
                            dev->devnum);

                /* Reset the suspended status in the SCSW */
                dev->scsw.flag3 &= ~SCSW3_AC_SUSP;
                dev->scsw.flag3 |= (SCSW3_AC_SCHAC | SCSW3_AC_DEVAC);
            }

            /* Reset the resume pending flag */
            dev->scsw.flag2 &= ~SCSW2_AC_RESUM;

            /* Release the device lock */
            release_lock (&dev->lock);

            /* Reset fields as if starting a new channel program */
            code = 0;
            tic = 1;
            chain = 1;
            chained = 0;
            prev_chained = 0;
            prevcode = 0;
            ccwseq = 0;
            bufpos = 0;

            /* Go back and refetch the suspended CCW */
            ccwaddr -= 8;
            continue;

        } /* end if(CCW_FLAGS_SUSP) */
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

        /* Signal I/O interrupt if PCI flag is set */
        if (flags & CCW_FLAGS_PCI)
        {
            /* Obtain the device lock */
            obtain_lock (&dev->lock);

            /* Set PCI interrupt pending flag */
            dev->pcipending = 1;

///*debug*/ /* Trace the CCW if not already done */
///*debug*/ if (!(dev->ccwtrace || dev->ccwstep || tracethis))
///*debug*/ {
///*debug*/     display_ccw (dev, ccw, addr);
///*debug*/     tracethis = 1;
///*debug*/ }
///*debug*/ logmsg ("%4.4X: PCI flag set\n", dev->devnum);

#ifdef FEATURE_S370_CHANNEL
            /* Save the PCI CSW replacing any previous pending PCI */
            dev->pcicsw[0] = ccwkey;
            dev->pcicsw[1] = (ccwaddr & 0xFF0000) >> 16;
            dev->pcicsw[2] = (ccwaddr & 0xFF00) >> 8;
            dev->pcicsw[3] = ccwaddr & 0xFF;
            dev->pcicsw[4] = 0;
            dev->pcicsw[5] = CSW_PCI;
            dev->pcicsw[6] = 0;
            dev->pcicsw[7] = 0;
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
            dev->pciscsw.flag0 = ccwkey & SCSW0_KEY;
            dev->pciscsw.flag1 = (ccwfmt == 1 ? SCSW1_F : 0);
            dev->pciscsw.flag2 = SCSW2_FC_START;
            dev->pciscsw.flag3 = SCSW3_AC_SCHAC | SCSW3_AC_DEVAC
                                | SCSW3_SC_INTER | SCSW3_SC_PRI
                                | SCSW3_SC_PEND;
            dev->pciscsw.ccwaddr[0] = (ccwaddr & 0xFF000000) >> 24;
            dev->pciscsw.ccwaddr[1] = (ccwaddr & 0xFF0000) >> 16;
            dev->pciscsw.ccwaddr[2] = (ccwaddr & 0xFF00) >> 8;
            dev->pciscsw.ccwaddr[3] = ccwaddr & 0xFF;
            dev->pciscsw.unitstat = 0;
            dev->pciscsw.chanstat = CSW_PCI;
            dev->pciscsw.count[0] = 0;
            dev->pciscsw.count[1] = 0;
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

            /* Release the device lock */
            release_lock (&dev->lock);

            /* Signal waiting CPUs that an interrupt is pending */
            obtain_lock (&sysblk.intlock);
            sysblk.iopending = 1;
            signal_condition (&sysblk.intcond);
            release_lock (&sysblk.intlock);

        } /* end if(CCW_FLAGS_PCI) */

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
            /* Channel program check if data exceeds buffer size */
            if (bufpos + count > sizeof(iobuf))
            {
                chanstat = CSW_PROGC;
                break;
            }

            /* Copy data into channel buffer */
            copy_iobuf (dev, code, flags, addr, count,
                        ccwkey, iobuf + bufpos, &chanstat);
            if (chanstat != 0) break;

            /* Update number of bytes in channel buffer */
            bufpos += count;

            /* If device handler has requested merging of data
               chained write CCWs, then collect data from all CCWs
               in chain before passing buffer to device handler */
            if (dev->cdwmerge)
            {
                if (flags & CCW_FLAGS_CD)
                {
                    /* If this is the first CCW in the data chain, then
                       save the chaining flags from the previous CCW */
                    if ((chained & CCW_FLAGS_CD) == 0)
                        prev_chained = chained;

                    /* Process next CCW in data chain */
                    chained = CCW_FLAGS_CD;
                    chain = 1;
                    continue;
                }

                /* If this is the last CCW in the data chain, then
                   restore the chaining flags from the previous CCW */
                if (chained & CCW_FLAGS_CD)
                    chained = prev_chained;

            } /* end if(dev->cdwmerge) */

            /* Reset the total count at end of data chain */
            count = bufpos;
            bufpos = 0;
        }

        /* Set chaining flag */
        chain = ( flags & (CCW_FLAGS_CD | CCW_FLAGS_CC) ) ? 1 : 0;

        /* Initialize residual byte count */
        residual = count;
        more = 0;

        /* Channel program check if invalid CCW opcode */
        if (!(IS_CCW_WRITE(code) || IS_CCW_READ(code)
                || IS_CCW_CONTROL(code) || IS_CCW_SENSE(code)
                || IS_CCW_RDBACK(code)))
        {
            chanstat = CSW_PROGC;
            break;
        }

        /* Pass the CCW to the device handler for execution */
        (*devexec) (dev, code, flags, chained, count, prevcode,
                    ccwseq, iobuf, &more, &unitstat, &residual);

        /* For READ, SENSE, and READ BACKWARD operations, copy data
           from channel buffer to main storage, unless SKIP is set */
        if ((flags & CCW_FLAGS_SKIP) == 0
            && (IS_CCW_READ(code)
                || IS_CCW_SENSE(code)
                || IS_CCW_RDBACK(code)))
        {
            copy_iobuf (dev, code, flags, addr, count - residual,
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

        /* Force tracing for this CCW if any unusual status occurred */
        if ((chanstat & (CSW_PROGC | CSW_PROTC | CSW_CDC | CSW_CCC
                                | CSW_ICC | CSW_CHC))
            || ((unitstat & CSW_UC) && dev->sense[0] != 0))
        {
            /* Trace the CCW if not already done */
            if (!(dev->ccwtrace || dev->ccwstep || tracethis))
                display_ccw (dev, ccw, addr);

            /* Activate tracing for this CCW chain only */
            tracethis = 1;
        }

        /* Trace the results of CCW execution */
        if (dev->ccwtrace || dev->ccwstep || tracethis)
        {
            /* Format data for READ or SENSE commands only */
            if (IS_CCW_READ(code) || IS_CCW_SENSE(code))
                format_iobuf_data (addr, area);
            else
                area[0] = '\0';

            /* Display status and residual byte count */
            logmsg ("%4.4X:Stat=%2.2X%2.2X Count=%4.4X %s\n",
                    dev->devnum, unitstat, chanstat, residual, area);

            /* Display sense bytes if unit check is indicated */
            if (unitstat & CSW_UC)
            {
                logmsg ("%4.4X:Sense=%2.2X%2.2X%2.2X%2.2X "
                        "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X "
                        "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X "
                        "%2.2X%2.2X%2.2X%2.2X\n",
                        dev->devnum, dev->sense[0], dev->sense[1],
                        dev->sense[2], dev->sense[3], dev->sense[4],
                        dev->sense[5], dev->sense[6], dev->sense[7],
                        dev->sense[8], dev->sense[9], dev->sense[10],
                        dev->sense[11], dev->sense[12], dev->sense[13],
                        dev->sense[14], dev->sense[15], dev->sense[16],
                        dev->sense[17], dev->sense[18], dev->sense[19],
                        dev->sense[20], dev->sense[21], dev->sense[22],
                        dev->sense[23]);
                if (dev->sense[0] != 0 || dev->sense[1] != 0)
                {
                    logmsg ("%4.4X:Sense=%s%s%s%s%s%s%s%s"
                            "%s%s%s%s%s%s%s%s\n",
                            dev->devnum,
                            (dev->sense[0] & SENSE_CR) ? "CMDREJ " : "",
                            (dev->sense[0] & SENSE_IR) ? "INTREQ " : "",
                            (dev->sense[0] & SENSE_BOC) ? "BOC " : "",
                            (dev->sense[0] & SENSE_EC) ? "EQC " : "",
                            (dev->sense[0] & SENSE_DC) ? "DCK " : "",
                            (dev->sense[0] & SENSE_OR) ? "OVR " : "",
                            (dev->sense[0] & SENSE_CC) ? "CCK " : "",
                            (dev->sense[0] & SENSE_OC) ? "OCK " : "",
                            (dev->sense[1] & SENSE1_PER) ? "PER " : "",
                            (dev->sense[1] & SENSE1_ITF) ? "ITF " : "",
                            (dev->sense[1] & SENSE1_EOC) ? "EOC " : "",
                            (dev->sense[1] & SENSE1_MTO) ? "MSG " : "",
                            (dev->sense[1] & SENSE1_NRF) ? "NRF " : "",
                            (dev->sense[1] & SENSE1_FP) ? "FP " : "",
                            (dev->sense[1] & SENSE1_WRI) ? "WRI " : "",
                            (dev->sense[1] & SENSE1_IE) ? "IE " : "");
                }
            }
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

        /* Update the CCW sequence number unless data chained */
        if ((flags & CCW_FLAGS_CD) == 0)
            ccwseq++;

    } /* end while(chain) */

    /* Obtain the device lock */
    obtain_lock (&dev->lock);

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
    dev->scsw.flag3 &= ~(SCSW3_AC_SCHAC | SCSW3_AC_DEVAC);
    dev->scsw.flag3 |= (SCSW3_SC_PRI | SCSW3_SC_SEC | SCSW3_SC_PEND);
    dev->scsw.ccwaddr[0] = (ccwaddr & 0x7F000000) >> 24;
    dev->scsw.ccwaddr[1] = (ccwaddr & 0xFF0000) >> 16;
    dev->scsw.ccwaddr[2] = (ccwaddr & 0xFF00) >> 8;
    dev->scsw.ccwaddr[3] = ccwaddr & 0xFF;
    dev->scsw.unitstat = unitstat;
    dev->scsw.chanstat = chanstat;
    dev->scsw.count[0] = (residual & 0xFF00) >> 8;
    dev->scsw.count[1] = residual & 0xFF;

    /* Set alert status if terminated by any unusual condition */
    if (chanstat != 0 || unitstat != (CSW_CE | CSW_DE))
        dev->scsw.flag3 |= SCSW3_SC_ALERT;

    /* Build the format-1 extended status word */
    memset (&dev->esw, 0, sizeof(ESW));
    dev->esw.lpum = 0x80;

    /* Clear the extended control word */
    memset (dev->ecw, 0, sizeof(dev->ecw));

    /* Return sense information if PMCW allows concurrent sense */
    if ((unitstat & CSW_UC) && (dev->pmcw.flag27 & PMCW27_S))
    {
        dev->scsw.flag1 |= SCSW1_E;
        dev->esw.erw0 |= ERW0_S;
        dev->esw.erw1 = (dev->numsense < sizeof(dev->ecw)) ?
                        dev->numsense : sizeof(dev->ecw);
        memcpy (dev->ecw, dev->sense, dev->esw.erw1 & ERW1_SCNT);
        memset (dev->sense, 0, sizeof(dev->sense));
    }
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Set the interrupt pending flag for this device */
    dev->busy = 0;
    dev->pending = 1;

    /* Signal console thread to redrive select */
    if (dev->console)
    {
        signal_thread (sysblk.cnsltid, SIGHUP);
    }

    /* Release the device lock */
    release_lock (&dev->lock);

    /* Signal waiting CPUs that an interrupt is pending */
    obtain_lock (&sysblk.intlock);
    sysblk.iopending = 1;
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
/* STORE CHANNEL ID                                                  */
/*-------------------------------------------------------------------*/
int store_channel_id (REGS *regs, U16 chan)
{
U32     chanid;                         /* Channel identifier word   */
int     devcount = 0;                   /* #of devices on channel    */
DEVBLK *dev;                            /* -> Device control block   */
PSA    *psa;                            /* -> Prefixed storage area  */

    /* Find a device on specified channel with pending interrupt */
    for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
    {
        /* Skip the device if not on specified channel */
        if ((dev->devnum & 0xFF00) != chan)
            continue;

        /* Count devices on channel */
        devcount++;

    } /* end for(dev) */

    /* Exit with condition code 3 if no devices on channel */
    if (devcount == 0)
        return 3;

    /* Construct the channel id word */
    chanid = CHANNEL_BMX;

    /* Store the channel id word at PSA+X'A8' */
    psa = (PSA*)(sysblk.mainstor + regs->pxr);
    psa->chanid[0] = (chanid >> 24) & 0xFF;
    psa->chanid[1] = (chanid >> 16) & 0xFF;
    psa->chanid[2] = (chanid >> 8) & 0xFF;
    psa->chanid[3] = chanid & 0xFF;

    /* Exit with condition code 0 indicating channel id stored */
    return 0;

} /* end function test_channel */

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
        if (dev->pending || dev->pcipending)
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
    else if (dev->pcipending)
    {
        /* Set condition code 1 if PCI interrupt pending */
        cc = 1;

        /* Store the channel status word at PSA+X'40' */
        psa = (PSA*)(sysblk.mainstor + regs->pxr);
        memcpy (psa->csw, dev->pcicsw, 8);
        if (dev->ccwtrace || dev->ccwstep)
            display_csw (dev, dev->pcicsw);

        /* Clear the pending PCI interrupt */
        dev->pcipending = 0;
    }
    else if (dev->pending)
    {
        /* Set condition code 1 if interrupt pending */
        cc = 1;

        /* Store the channel status word at PSA+X'40' */
        psa = (PSA*)(sysblk.mainstor + regs->pxr);
        memcpy (psa->csw, dev->csw, 8);
        if (dev->ccwtrace || dev->ccwstep)
            display_csw (dev, dev->csw);

        /* Clear the pending interrupt */
        dev->pending = 0;

        /* Signal console thread to redrive select */
        if (dev->console)
        {
            signal_thread (sysblk.cnsltid, SIGHUP);
        }
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

    /* Return PCI SCSW if PCI status is pending */
    if (dev->pciscsw.flag3 & SCSW3_SC_PEND)
    {
        /* Display the subchannel status word */
        if (dev->ccwtrace || dev->ccwstep)
            display_scsw (dev, dev->pciscsw);

        /* Copy the PCI SCSW to the IRB */
        irb->scsw = dev->pciscsw;

        /* Clear the ESW and ECW in the IRB */
        memset (&irb->esw, 0, sizeof(ESW));
        irb->esw.lpum = 0x80;
        memset (irb->ecw, 0, sizeof(irb->ecw));

        /* Clear the pending PCI status */
        dev->pciscsw.flag2 &= ~(SCSW2_FC | SCSW2_AC);
        dev->pciscsw.flag3 &= ~(SCSW3_SC);

        /* Release the device lock */
        release_lock (&dev->lock);

        /* Return condition code 0 to indicate status was pending */
        return 0;

    } /* end if(pcipending) */

    /* Copy the subchannel status word to the IRB */
    irb->scsw = dev->scsw;

    /* Copy the extended status word to the IRB */
    irb->esw = dev->esw;

    /* Copy the extended control word to the IRB */
    memcpy (irb->ecw, dev->ecw, sizeof(irb->ecw));

    /* Clear any pending interrupt */
    dev->pending = 0;

    /* Test device status and set condition code */
    if (dev->scsw.flag3 & SCSW3_SC_PEND)
    {
        /* Set condition code 0 if status pending */
        cc = 0;

        /* Display the subchannel status word */
        if (dev->ccwtrace || dev->ccwstep)
            display_scsw (dev, dev->scsw);

        /* [14.3.13] If status is anything other than intermediate with
           pending then clear the function control, activity control,
           status control, and path not-operational bits in the SCSW */
        if ((dev->scsw.flag3 & SCSW3_SC)
                                != (SCSW3_SC_INTER | SCSW3_SC_PEND))
        {
            dev->scsw.flag2 &= ~(SCSW2_FC | SCSW2_AC);
            dev->scsw.flag3 &= ~(SCSW3_AC_SUSP);
            dev->scsw.flag1 &= ~(SCSW1_N);
        }
        else
        {
            /* [14.3.13] Clear the function control bits if function
               code is halt and the channel program is suspended */
            if ((dev->scsw.flag2 & SCSW2_FC_HALT)
                && (dev->scsw.flag3 & SCSW3_AC_SUSP))
                dev->scsw.flag2 &= ~(SCSW2_FC);

            /* [14.3.13] Clear the activity control bits if function
               code is start+halt and channel program is suspended */
            if ((dev->scsw.flag2 & (SCSW2_FC_START | SCSW2_FC_HALT))
                                    == (SCSW2_FC_START | SCSW2_FC_HALT)
                && (dev->scsw.flag3 & SCSW3_AC_SUSP))
            {
                dev->scsw.flag2 &= ~(SCSW2_AC);
                dev->scsw.flag3 &= ~(SCSW3_AC_SUSP);
                dev->scsw.flag1 &= ~(SCSW1_N);
            }

            /* [14.3.13] Clear the resume pending bit if function code
               is start without halt and channel program suspended */
            if ((dev->scsw.flag2 & (SCSW2_FC_START | SCSW2_FC_HALT))
                                        == SCSW2_FC_START
                && (dev->scsw.flag3 & SCSW3_AC_SUSP))
            {
                dev->scsw.flag2 &= ~(SCSW2_AC_RESUM);
                dev->scsw.flag1 &= ~(SCSW1_N);
            }

        } /* end if(INTER+PEND) */

        /* Clear the status bits in the SCSW */
        dev->scsw.flag3 &= ~(SCSW3_SC);

        /* Signal console thread to redrive select */
        if (dev->console)
        {
            signal_thread (sysblk.cnsltid, SIGHUP);
        }
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

/*-------------------------------------------------------------------*/
/* CLEAR SUBCHANNEL                                                  */
/*-------------------------------------------------------------------*/
/* Input                                                             */
/*      regs    -> CPU register context                              */
/*      dev     -> Device control block                              */
/*-------------------------------------------------------------------*/
void clear_subchan (REGS *regs, DEVBLK *dev)
{

    /* Obtain the device lock */
    obtain_lock (&dev->lock);

    /* [15.3.2] Perform clear function subchannel modification */
    dev->pmcw.pom = 0xFF;
    dev->pmcw.lpum = 0x00;
    dev->pmcw.pnom = 0x00;

    /* [15.3.3] Perform clear function signaling and completion */
    dev->scsw.flag2 &= ~(SCSW2_FC | SCSW2_AC);
    dev->scsw.flag2 |= SCSW2_FC_CLEAR;
    dev->scsw.flag3 &= ~(SCSW3_AC | SCSW3_SC);
    dev->scsw.flag3 = SCSW3_SC_PRI | SCSW3_SC_PEND;

    /* If the device is busy then set a clear pending condition */
    if (dev->busy)
        dev->scsw.flag2 |= SCSW2_AC_CLEAR;

    /* Clear any pending interrupt */
    dev->pcipending = 0;
    dev->pending = 0;

    /* For 3270 device, clear any pending input */
    if (dev->devtype == 0x3270)
    {
        dev->readpending = 0;
        dev->rlen3270 = 0;
    }

    /* Signal console thread to redrive select */
    if (dev->console)
    {
        signal_thread (sysblk.cnsltid, SIGHUP);
    }

    /* Release the device lock */
    release_lock (&dev->lock);

    /* Signal waiting CPUs that an interrupt may be pending */
    obtain_lock (&sysblk.intlock);
    sysblk.iopending = 1;
    signal_condition (&sysblk.intcond);
    release_lock (&sysblk.intlock);

} /* end function clear_subchan */

/*-------------------------------------------------------------------*/
/* HALT SUBCHANNEL                                                   */
/*-------------------------------------------------------------------*/
/* Input                                                             */
/*      regs    -> CPU register context                              */
/*      dev     -> Device control block                              */
/* Return value                                                      */
/*      The return value is the condition code for the HSCH          */
/*      instruction:  0=Halt initiated, 1=Non-intermediate status    */
/*      pending, 2=Busy                                              */
/*-------------------------------------------------------------------*/
int halt_subchan (REGS *regs, DEVBLK *dev)
{

    /* Obtain the device lock */
    obtain_lock (&dev->lock);

    /* Set condition code 1 if subchannel is status pending alone or
       is status pending with alert, primary, or secondary status */
    if ((dev->scsw.flag3 & SCSW3_SC) == SCSW3_SC_PEND
        || ((dev->scsw.flag3 & SCSW3_SC_PEND)
            && (dev->scsw.flag3 &
                    (SCSW3_SC_ALERT | SCSW3_SC_PRI | SCSW3_SC_SEC))))
    {
        logmsg ("%4.4X: Halt subchannel: cc=1\n", dev->devnum);
        release_lock (&dev->lock);
        return 1;
    }

    /* Set condition code 2 if the halt function or the clear
       function is already in progress at the subchannel */
    if (dev->scsw.flag2 & (SCSW2_FC_HALT | SCSW2_FC_CLEAR))
    {
        logmsg ("%4.4X: Halt subchannel: cc=2\n", dev->devnum);
        release_lock (&dev->lock);
        return 2;
    }

    /* [15.4.2] Perform halt function signaling and completion */
    dev->scsw.flag2 |= SCSW2_FC_HALT;
    dev->scsw.flag3 &= ~SCSW3_SC_PEND;

    /* If the device is busy then signal subchannel to halt */
    if (dev->busy)
    {
        /* Set halt pending condition */
        dev->scsw.flag2 |= SCSW2_AC_HALT;

        /* Clear any pending interrupt */
        dev->pcipending = 0;
        dev->pending = 0;

        /* Signal the subchannel to resume if it is suspended */
        if (dev->scsw.flag3 & SCSW3_AC_SUSP)
        {
            dev->scsw.flag2 |= SCSW2_AC_RESUM;
            signal_condition (&dev->resumecond);
        }
    }
    else
    {
        /* If device is idle, make subchannel status pending */
        dev->scsw.flag3 |= SCSW3_SC_PEND;
        dev->scsw.unitstat = 0;
        dev->scsw.chanstat = 0;
        dev->pending = 1;
    }

    /* For 3270 device, clear any pending input */
    if (dev->devtype == 0x3270)
    {
        dev->readpending = 0;
        dev->rlen3270 = 0;
    }

    /* Signal console thread to redrive select */
    if (dev->console)
    {
        signal_thread (sysblk.cnsltid, SIGHUP);
    }

    /* Release the device lock */
    release_lock (&dev->lock);

    /* Signal waiting CPUs that an interrupt may be pending */
    obtain_lock (&sysblk.intlock);
    sysblk.iopending = 1;
    signal_condition (&sysblk.intcond);
    release_lock (&sysblk.intlock);

    /* Return condition code zero */
    logmsg ("%4.4X: Halt subchannel: cc=0\n", dev->devnum);
    return 0;

} /* end function halt_subchan */

/*-------------------------------------------------------------------*/
/* RESUME SUBCHANNEL                                                 */
/*-------------------------------------------------------------------*/
/* Input                                                             */
/*      regs    -> CPU register context                              */
/*      dev     -> Device control block                              */
/* Return value                                                      */
/*      The return value is the condition code for the RSCH          */
/*      instruction:  0=subchannel has been made resume pending,     */
/*      1=status was pending, 2=resume not allowed                   */
/*-------------------------------------------------------------------*/
int resume_subchan (REGS *regs, DEVBLK *dev)
{

    /* Obtain the device lock */
    obtain_lock (&dev->lock);

    /* Set condition code 1 if subchannel has status pending */
    if (dev->scsw.flag3 & SCSW3_SC_PEND)
    {
        logmsg ("%4.4X: Resume subchannel: cc=1\n", dev->devnum);
        release_lock (&dev->lock);
        return 1;
    }

    /* Set condition code 2 if subchannel has any function other
       than the start function alone, is already resume pending,
       or the ORB for the SSCH did not specify suspend control */
    if ((dev->scsw.flag2 & SCSW2_FC) != SCSW2_FC_START
        || (dev->scsw.flag2 & SCSW2_AC_RESUM)
        || (dev->scsw.flag0 & SCSW0_S) == 0)
    {
        logmsg ("%4.4X: Resume subchannel: cc=2\n", dev->devnum);
        release_lock (&dev->lock);
        return 2;
    }

    /* Clear the path not-operational mask if in suspend state */
    if (dev->scsw.flag3 & SCSW3_AC_SUSP)
        dev->pmcw.pnom = 0x00;

    /* Set the resume pending flag and signal the subchannel */
    dev->scsw.flag2 |= SCSW2_AC_RESUM;
    signal_condition (&dev->resumecond);
//  logmsg ("%4.4X: Resume subchannel: cc=0\n", dev->devnum);

    /* Release the device lock */
    release_lock (&dev->lock);

    /* Return condition code zero */
    return 0;

} /* end function resume_subchan */
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
/* Note: The caller MUST hold the interrupt lock (sysblk.intlock).   */
/*-------------------------------------------------------------------*/
int
present_io_interrupt (REGS *regs, U32 *ioid, U32 *ioparm, BYTE *csw)
{
DEVBLK *dev;                            /* -> Device control block   */

    /* Turn off the I/O interrupt pending flag */
    sysblk.iopending = 0;

    /* Find a device with pending interrupt */
    for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
    {
        obtain_lock (&dev->lock);
        if (dev->pending || dev->pcipending)
        {
            /* Turn on the I/O interrupt pending flag */
            sysblk.iopending = 1;

            /* Exit loop if enabled for interrupts from this device */
            if (interrupt_enabled(regs, dev))
                break;
        }
        release_lock (&dev->lock);
    } /* end for(dev) */

    /* If no enabled interrupt pending, exit with condition code 0 */
    if (dev == NULL)
    {
        return 0;
    }

#ifdef FEATURE_S370_CHANNEL
    /* Extract the I/O address and CSW */
    *ioid = dev->devnum;
    memcpy (csw, dev->pcipending ? dev->pcicsw : dev->csw, 8);

    /* Display the channel status word */
    if (dev->ccwtrace || dev->ccwstep)
        display_csw (dev, csw);
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Extract the I/O address and interrupt parameter */
    *ioid = 0x00010000 | dev->subchan;
    *ioparm = (dev->pmcw.intparm[0] << 24)
            | (dev->pmcw.intparm[1] << 16)
            | (dev->pmcw.intparm[2] << 8)
            | dev->pmcw.intparm[3];
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Reset the interrupt pending flag for the device */
    if (dev->pcipending)
    {
        dev->pcipending = 0;
    }
    else
    {
        dev->pending = 0;
    }

    /* Signal console thread to redrive select */
    if (dev->console)
    {
        signal_thread (sysblk.cnsltid, SIGHUP);
    }

    /* Release the device lock */
    release_lock (&dev->lock);

    /* Exit with condition code indicating interrupt cleared */
    return 1;

} /* end function present_io_interrupt */

/*-------------------------------------------------------------------*/
/* I/O RESET                                                         */
/* Resets status of all devices ready for IPL.  Note that device     */
/* positioning is not affected by I/O reset; thus the system can     */
/* be IPLed from current position in a tape or card reader file.     */
/*-------------------------------------------------------------------*/
void
io_reset (void)
{
DEVBLK *dev;                            /* -> Device control block   */

    /* Reset each device in the configuration */
    for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
    {
        obtain_lock (&dev->lock);

        dev->pending = 0;
        dev->busy = 0;
        dev->readpending = 0;
        dev->pcipending = 0;
        dev->pmcw.intparm[0] = 0;
        dev->pmcw.intparm[1] = 0;
        dev->pmcw.intparm[2] = 0;
        dev->pmcw.intparm[3] = 0;
        dev->pmcw.flag4 &= ~PMCW4_ISC;
        dev->pmcw.flag5 &= ~(PMCW5_E | PMCW5_LM | PMCW5_MM | PMCW5_D);
        dev->pmcw.pnom = 0;
        dev->pmcw.lpum = 0;
        dev->pmcw.mbi[0] = 0;
        dev->pmcw.mbi[1] = 0;
        dev->pmcw.flag27 &= ~PMCW27_S;
        memset (&dev->scsw, 0, sizeof(SCSW));
        memset (&dev->pciscsw, 0, sizeof(SCSW));
        memset (dev->sense, 0, sizeof(dev->sense));

        release_lock (&dev->lock);

    } /* end for(dev) */

    /* Signal console thread to redrive select */
    signal_thread (sysblk.cnsltid, SIGHUP);

} /* end function io_reset */

/*-------------------------------------------------------------------*/
/* DEVICE ATTENTION                                                  */
/* Raises an unsolicited interrupt condition for a specified device. */
/* Return value is 0 if successful, 1 if device is busy or pending   */
/*-------------------------------------------------------------------*/
int
device_attention (DEVBLK *dev, BYTE unitstat)
{
    /* Obtain the device lock */
    obtain_lock (&dev->lock);

    /* If device is already busy or interrupt pending or
       status pending then do not present interrupt */
    if (dev->busy || dev->pending
        || (dev->scsw.flag3 & SCSW3_SC_PEND))
    {
        release_lock (&dev->lock);
        return 1;
    }

#ifdef FEATURE_S370_CHANNEL
    /* Set CSW for attention interrupt */
    dev->csw[0] = 0;
    dev->csw[1] = 0;
    dev->csw[2] = 0;
    dev->csw[3] = 0;
    dev->csw[4] = unitstat;
    dev->csw[5] = 0;
    dev->csw[6] = 0;
    dev->csw[7] = 0;
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Set SCSW for attention interrupt */
    dev->scsw.flag0 = 0;
    dev->scsw.flag1 = 0;
    dev->scsw.flag2 = 0;
    dev->scsw.flag3 = SCSW3_SC_ALERT | SCSW3_SC_PEND;
    dev->scsw.ccwaddr[0] = 0;
    dev->scsw.ccwaddr[1] = 0;
    dev->scsw.ccwaddr[2] = 0;
    dev->scsw.ccwaddr[3] = 0;
    dev->scsw.unitstat = unitstat;
    dev->scsw.chanstat = 0;
    dev->scsw.count[0] = 0;
    dev->scsw.count[1] = 0;
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Set the interrupt pending flag for this device */
    dev->pending = 1;

    /* Release the device lock */
    release_lock (&dev->lock);

    /* Signal waiting CPUs that an interrupt is pending */
    obtain_lock (&sysblk.intlock);
    sysblk.iopending = 1;
    signal_condition (&sysblk.intcond);
    release_lock (&sysblk.intlock);

    return 0;
} /* end function device_attention */

