/* FBADASD.C    (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 FBA Direct Access Storage Device Handler     */

/*-------------------------------------------------------------------*/
/* This module contains device handling functions for emulated       */
/* fixed block architecture direct access storage devices.           */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Bit definitions for Define Extent file mask                       */
/*-------------------------------------------------------------------*/
#define FBAMASK_CTL             0xC0    /* Operation control bits... */
#define FBAMASK_CTL_INHFMT      0x00    /* ...inhibit format writes  */
#define FBAMASK_CTL_INHWRT      0x40    /* ...inhibit all writes     */
#define FBAMASK_CTL_ALLWRT      0xC0    /* ...allow all writes       */
#define FBAMASK_CTL_RESV        0x80    /* ...reserved bit setting   */
#define FBAMASK_CE              0x08    /* CE field extent           */
#define FBAMASK_DIAG            0x04    /* Permit diagnostic command */
#define FBAMASK_RESV            0x33    /* Reserved bits - must be 0 */

/*-------------------------------------------------------------------*/
/* Bit definitions for Locate operation byte                         */
/*-------------------------------------------------------------------*/
#define FBAOPER_RESV            0xF0    /* Reserved bits - must be 0 */
#define FBAOPER_CODE            0x0F    /* Operation code bits...    */
#define FBAOPER_WRITE           0x01    /* ...write data             */
#define FBAOPER_READREP         0x02    /* ...read replicated data   */
#define FBAOPER_FMTDEFC         0x04    /* ...format defective block */
#define FBAOPER_WRTVRFY         0x05    /* ...write data and verify  */
#define FBAOPER_READ            0x06    /* ...read data              */


/*-------------------------------------------------------------------*/
/* Initialize the device handler                                     */
/*-------------------------------------------------------------------*/
int fbadasd_init_handler ( DEVBLK *dev, int argc, BYTE *argv[] )
{
int     rc;                             /* Return code               */
struct  stat statbuf;                   /* File information          */
U32     blkspcg;                        /* Blocks per cyclical group */
U32     blkspap;                        /* Blocks per access position*/
U32     blksufh;                        /* Blocks under fixed heads  */
BYTE    unittyp;                        /* Unit type                 */

    /* The first argument is the file name */
    if (argc == 0 || strlen(argv[0]) > sizeof(dev->filename)-1)
    {
        fprintf (stderr,
                "HHC301I File name missing or invalid\n");
        return -1;
    }

    /* Save the file name in the device block */
    strcpy (dev->filename, argv[0]);

    /* Open the device file */
    dev->fd = open (dev->filename, O_RDWR);
    if (dev->fd < 0)
    {
        fprintf (stderr,
                "HHC302I File %s open error: %s\n",
                dev->filename, strerror(errno));
        return -1;
    }

    /* Determine the device size */
    rc = fstat (dev->fd, &statbuf);
    if (rc < 0)
    {
        fprintf (stderr,
                "HHC303I File %s fstat error: %s\n",
                dev->filename, strerror(errno));
        return -1;
    }

    /* Set block size and number of blocks in device */
    dev->fbablksiz = 512;
    dev->fbanumblk = statbuf.st_size / dev->fbablksiz;

    logmsg ("fbadasd: %s blks=%d\n",
            dev->filename, dev->fbanumblk);

    /* Set number of sense bytes */
    dev->numsense = 24;

    /* Set the device dependent characteristics fields */
    switch (dev->devtype) {
    case 0x3310:
        unittyp = 0x01;
        blkspcg = 32;
        blkspap = 352;
        blksufh = 0;
        break;
    case 0x3370:
        unittyp = 0x02;
        blkspcg = 62;
        blkspap = 744;
        blksufh = 0;
        break;
    case 0x9336:
        unittyp = 0x02;
        blkspcg = 64;
        blkspap = 960;
        blksufh = 0;
        break;
    default:
        fprintf (stderr,
                "HHC304I Unsupported device type: %4.4X\n",
                dev->devtype);
        return -1;
    } /* end switch(dev->devtype) */

    /* Initialize the device identifier bytes */
    dev->devid[0] = 0xFF;
    dev->devid[1] = 0x38; /* Control unit type is 3880-1 */
    dev->devid[2] = 0x80;
    dev->devid[3] = 0x01;
    dev->devid[4] = dev->devtype >> 8;
    dev->devid[5] = dev->devtype & 0xFF;
    dev->devid[6] = 0x01;
    dev->numdevid = 7;

    /* Initialize the device characteristics bytes */
    memset (dev->devchar, 0, sizeof(dev->devchar));
    /* Operation modes */
    dev->devchar[0] = 0x30;
    /* Features */
    dev->devchar[1] = 0x08;
    /* Device class */
    dev->devchar[2] = 0x21;
    /* Unit type */
    dev->devchar[3] = unittyp;
    /* Physical record size */
    dev->devchar[4] = (dev->fbablksiz & 0xFF00) >> 8;
    dev->devchar[5] = dev->fbablksiz & 0xFF;
    /* Blocks per cyclical group */
    dev->devchar[6] = (blkspcg & 0xFF000000) >> 24;
    dev->devchar[7] = (blkspcg & 0xFF0000) >> 16;
    dev->devchar[8] = (blkspcg & 0xFF00) >> 8;
    dev->devchar[9] = blkspcg & 0xFF;
    /* Blocks per access position */
    dev->devchar[10] = (blkspap & 0xFF000000) >> 24;
    dev->devchar[11] = (blkspap & 0xFF0000) >> 16;
    dev->devchar[12] = (blkspap & 0xFF00) >> 8;
    dev->devchar[13] = blkspap & 0xFF;
    /* Blocks under movable heads*/
    dev->devchar[14] = (dev->fbanumblk & 0xFF000000) >> 24;
    dev->devchar[15] = (dev->fbanumblk & 0xFF0000) >> 16;
    dev->devchar[16] = (dev->fbanumblk & 0xFF00) >> 8;
    dev->devchar[17] = dev->fbanumblk & 0xFF;
    /* Blocks under fixed heads*/
    dev->devchar[18] = (blksufh & 0xFF000000) >> 24;
    dev->devchar[19] = (blksufh & 0xFF0000) >> 16;
    dev->devchar[20] = (blksufh & 0xFF00) >> 8;
    dev->devchar[21] = blksufh & 0xFF;
    /* Blocks in alternate area */
    dev->devchar[22] = 0;
    dev->devchar[23] = 0;
    /* Blocks in CE+SA areas */
    dev->devchar[24] = 0;
    dev->devchar[25] = 0;
    /* Cyclic period of media in milliseconds */
    dev->devchar[26] = 0;
    dev->devchar[27] = 0;
    /* Minimum time to change access position in milliseconds */
    dev->devchar[28] = 0;
    dev->devchar[29] = 0;
    /* Maximum time to change access position in milliseconds */
    dev->devchar[30] = 0;
    dev->devchar[31] = 0;
    /* Number of device characteristics bytes */
    dev->numdevchar = 32;

    /* Activate I/O tracing */
//  dev->ccwtrace = 1;

    return 0;
} /* end function fbadasd_init_handler */


/*-------------------------------------------------------------------*/
/* Execute a Channel Command Word                                    */
/*-------------------------------------------------------------------*/
void fbadasd_execute_ccw ( DEVBLK *dev, BYTE code, BYTE flags,
        BYTE chained, U16 count, BYTE prevcode, int ccwseq,
        BYTE *iobuf, BYTE *more, BYTE *unitstat, U16 *residual )
{
int     rc;                             /* Return code               */
int     num;                            /* Number of bytes to move   */
long    rba;                            /* Offset for seek           */
BYTE    hexzeroes[512];                 /* Bytes for zero fill       */
int     rem;                            /* Byte count for zero fill  */
int     repcnt;                         /* Replication count         */

    /* Reset extent flag at start of CCW chain */
    if (chained == 0)
        dev->fbaxtdef = 0;

    /* Process depending on CCW opcode */
    switch (code) {

    case 0x02:
    /*---------------------------------------------------------------*/
    /* READ IPL                                                      */
    /*---------------------------------------------------------------*/
        /* Must be first CCW or chained from a previous READ IPL CCW */
        if (chained && prevcode != 0x02)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Zeroize the file mask and set extent for entire device */
        dev->fbamask = 0;
        dev->fbaxblkn = 0;
        dev->fbaxfirst = 0;
        dev->fbaxlast = dev->fbanumblk - 1;

        /* Seek to start of block zero */
        rc = lseek (dev->fd, 0, SEEK_SET);
        if (rc < 0)
        {
            /* Handle seek error condition */
            logmsg ("fbadasd: lseek error: %s\n",
                    strerror(errno));

            /* Set unit check with equipment check */
            dev->sense[0] = SENSE_EC;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Overrun if data chaining */
        if ((flags & CCW_FLAGS_CD))
        {
            dev->sense[0] = SENSE_OR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Calculate number of bytes to read and set residual count */
        num = (count < dev->fbablksiz) ? count : dev->fbablksiz;
        *residual = count - num;
        if (count < dev->fbablksiz) *more = 1;

        /* Read physical block into channel buffer */
        rc = read (dev->fd, iobuf, num);
        if (rc < num)
        {
            /* Handle read error condition */
            if (rc < 0)
                logmsg ("fbadasd: read error: %s\n",
                        strerror(errno));
            else
                logmsg ("fbadasd: unexpected end of file\n");

            /* Set unit check with equipment check */
            dev->sense[0] = SENSE_EC;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Set extent defined flag */
        dev->fbaxtdef = 1;

        /* Set normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x03:
    /*---------------------------------------------------------------*/
    /* CONTROL NO-OPERATION                                          */
    /*---------------------------------------------------------------*/
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x41:
    /*---------------------------------------------------------------*/
    /* WRITE                                                         */
    /*---------------------------------------------------------------*/
        /* Reject if previous command was not LOCATE */
        if (prevcode != 0x43)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Reject if locate command did not specify write operation */
        if ((dev->fbaoper & FBAOPER_CODE) != FBAOPER_WRITE
            && (dev->fbaoper & FBAOPER_CODE) != FBAOPER_WRTVRFY)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Prepare a block of zeroes for write padding */
        memset (hexzeroes, 0, sizeof(hexzeroes));

        /* Write physical blocks of data to the device */
        while (dev->fbalcnum > 0)
        {
            /* Exit if data chaining and this CCW is exhausted */
            if ((flags & CCW_FLAGS_CD) && count == 0)
                break;

            /* Overrun if data chaining within a physical block */
            if ((flags & CCW_FLAGS_CD) && count < dev->fbablksiz)
            {
                dev->sense[0] = SENSE_OR;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }

            /* Calculate number of bytes to write */
            num = (count < dev->fbablksiz) ? count : dev->fbablksiz;

            /* Write physical block from channel buffer */
            if (num > 0)
            {
                rc = write (dev->fd, iobuf, num);
                if (rc < num)
                {
                    /* Handle write error condition */
                    logmsg ("fbadasd: write error: %s\n",
                            strerror(errno));

                    /* Set unit check with equipment check */
                    dev->sense[0] = SENSE_EC;
                    *unitstat = CSW_CE | CSW_DE | CSW_UC;
                    break;
                }
            }

            /* Fill remainder of block with zeroes */
            if (num < dev->fbablksiz)
            {
                rem = dev->fbablksiz - num;
                rc = write (dev->fd, hexzeroes, rem);
                if (rc < rem)
                {
                    /* Handle write error condition */
                    logmsg ("fbadasd: write error: %s\n",
                            strerror(errno));

                    /* Set unit check with equipment check */
                    dev->sense[0] = SENSE_EC;
                    *unitstat = CSW_CE | CSW_DE | CSW_UC;
                    break;
                }
            }

            /* Prepare to write next block */
            count -= num;
            iobuf += num;
            dev->fbalcnum--;

        } /* end while */

        /* Set residual byte count */
        *residual = count;

        /* Set ending status */
        *unitstat |= CSW_CE | CSW_DE;
        break;

    case 0x42:
    /*---------------------------------------------------------------*/
    /* READ                                                          */
    /*---------------------------------------------------------------*/
        /* Reject if previous command was not LOCATE or READ IPL */
        if (prevcode != 0x43 && prevcode != 0x02)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Reject if locate command did not specify read operation */
        if (prevcode != 0x02
            && (dev->fbaoper & FBAOPER_CODE) != FBAOPER_READ
            && (dev->fbaoper & FBAOPER_CODE) != FBAOPER_READREP)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Read physical blocks of data from device */
        while (dev->fbalcnum > 0 && count > 0)
        {
            /* Overrun if data chaining within a physical block */
            if ((flags & CCW_FLAGS_CD) && count < dev->fbablksiz)
            {
                dev->sense[0] = SENSE_OR;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }

            /* Calculate number of bytes to read */
            num = (count < dev->fbablksiz) ? count : dev->fbablksiz;
            if (num < dev->fbablksiz) *more = 1;

            /* Read physical block into channel buffer */
            rc = read (dev->fd, iobuf, num);
            if (rc < num)
            {
                /* Handle read error condition */
                if (rc < 0)
                    logmsg ("fbadasd: read error: %s\n",
                            strerror(errno));
                else
                    logmsg ("fbadasd: unexpected end of file\n");

                /* Set unit check with equipment check */
                dev->sense[0] = SENSE_EC;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }

            /* Prepare to read next block */
            count -= num;
            iobuf += num;
            dev->fbalcnum--;

        } /* end while */

        /* Set residual byte count */
        *residual = count;
        if (dev->fbalcnum > 0) *more = 1;

        /* Set ending status */
        *unitstat |= CSW_CE | CSW_DE;

        break;

    case 0x43:
    /*---------------------------------------------------------------*/
    /* LOCATE                                                        */
    /*---------------------------------------------------------------*/
        /* Calculate residual byte count */
        num = (count < 8) ? count : 8;
        *residual = count - num;

        /* Control information length must be at least 8 bytes */
        if (count < 8)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* LOCATE must be preceded by DEFINE EXTENT or READ IPL */
        if (dev->fbaxtdef == 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Save and validate the operation byte */
        dev->fbaoper = iobuf[0];
        if (dev->fbaoper & FBAOPER_RESV)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Validate and process operation code */
        if ((dev->fbaoper & FBAOPER_CODE) == FBAOPER_WRITE
            || (dev->fbaoper & FBAOPER_CODE) == FBAOPER_WRTVRFY)
        {
            /* Reject command if file mask inhibits all writes */
            if ((dev->fbamask & FBAMASK_CTL) == FBAMASK_CTL_INHWRT)
            {
                dev->sense[0] = SENSE_CR;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
        }
        else if ((dev->fbaoper & FBAOPER_CODE) == FBAOPER_READ
                || (dev->fbaoper & FBAOPER_CODE) == FBAOPER_READREP)
        {
        }
        else if ((dev->fbaoper & FBAOPER_CODE) == FBAOPER_FMTDEFC)
        {
            /* Reject command if file mask inhibits format writes */
            if ((dev->fbamask & FBAMASK_CTL) == FBAMASK_CTL_INHFMT)
            {
                dev->sense[0] = SENSE_CR;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
        }
        else /* Operation code is invalid */
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Byte 1 contains the replication count */
        repcnt = iobuf[1];

        /* Bytes 2-3 contain the block count */
        dev->fbalcnum = (iobuf[2] << 8) | iobuf[3];

        /* Bytes 4-7 contain the displacement of the first block
           relative to the start of the dataset */
        dev->fbalcblk = (iobuf[4] << 24) | (iobuf[5] << 16)
                        | (iobuf[6] << 8) | iobuf[7];

        /* Verify that the block count is non-zero, and that
           the starting and ending blocks fall within the extent */
        if (dev->fbalcnum == 0
            || dev->fbalcnum - 1 > dev->fbaxlast
            || dev->fbalcblk < dev->fbaxfirst
            || dev->fbalcblk > dev->fbaxlast - (dev->fbalcnum - 1))
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* For replicated data, verify that the replication count
           is non-zero and is a multiple of the block count */
        if ((dev->fbaoper & FBAOPER_CODE) == FBAOPER_READREP)
        {
            if (repcnt == 0 || repcnt % dev->fbalcnum != 0)
            {
                dev->sense[0] = SENSE_CR;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
        }

        /* Position device to start of block */
        rba = (dev->fbalcblk - dev->fbaxfirst
                + dev->fbaxblkn) * dev->fbablksiz;

        DEVTRACE("fbadasd: Positioning to %8.8lX (%lu)\n", rba, rba);

        rc = lseek (dev->fd, rba, SEEK_SET);
        if (rc < 0)
        {
            /* Handle seek error condition */
            logmsg ("fbadasd: lseek error: %s\n",
                    strerror(errno));

            /* Set unit check with equipment check */
            dev->sense[0] = SENSE_EC;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x63:
    /*---------------------------------------------------------------*/
    /* DEFINE EXTENT                                                 */
    /*---------------------------------------------------------------*/
        /* Calculate residual byte count */
        num = (count < 16) ? count : 16;
        *residual = count - num;

        /* Control information length must be at least 16 bytes */
        if (count < 16)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Reject if extent previously defined in this CCW chain */
        if (dev->fbaxtdef)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Save and validate the file mask */
        dev->fbamask = iobuf[0];
        if ((dev->fbamask & (FBAMASK_RESV | FBAMASK_CE))
            || (dev->fbamask & FBAMASK_CTL) == FBAMASK_CTL_RESV)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Verify that bytes 1-3 are zeroes */
        if (iobuf[1] != 0 || iobuf[2] != 0 || iobuf[3] != 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Bytes 4-7 contain the block number of the first block
           of the extent relative to the start of the device */
        dev->fbaxblkn = (iobuf[4] << 24) | (iobuf[5] << 16)
                        | (iobuf[6] << 8) | iobuf[7];

        /* Bytes 8-11 contain the block number of the first block
           of the extent relative to the start of the dataset */
        dev->fbaxfirst = (iobuf[8] << 24) | (iobuf[9] << 16)
                        | (iobuf[10] << 8) | iobuf[11];

        /* Bytes 12-15 contain the block number of the last block
           of the extent relative to the start of the dataset */
        dev->fbaxlast = (iobuf[12] << 24) | (iobuf[13] << 16)
                        | (iobuf[14] << 8) | iobuf[15];

        /* Validate the extent description by checking that the
           ending block is not less than the starting block and
           that the ending block does not exceed the device size */
        if (dev->fbaxlast < dev->fbaxfirst
            || dev->fbaxblkn > dev->fbanumblk
            || dev->fbaxlast - dev->fbaxfirst
                >= dev->fbanumblk - dev->fbaxblkn)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Set extent defined flag and return normal status */
        dev->fbaxtdef = 1;
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x64:
    /*---------------------------------------------------------------*/
    /* READ DEVICE CHARACTERISTICS                                   */
    /*---------------------------------------------------------------*/
        /* Calculate residual byte count */
        num = (count < dev->numdevchar) ? count : dev->numdevchar;
        *residual = count - num;
        if (count < dev->numdevchar) *more = 1;

        /* Copy device characteristics bytes to channel buffer */
        memcpy (iobuf, dev->devchar, num);

        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x04:
    /*---------------------------------------------------------------*/
    /* SENSE                                                         */
    /*---------------------------------------------------------------*/
        /* Calculate residual byte count */
        num = (count < dev->numsense) ? count : dev->numsense;
        *residual = count - num;
        if (count < dev->numsense) *more = 1;

        /* Copy device sense bytes to channel I/O buffer */
        memcpy (iobuf, dev->sense, num);

        /* Clear the device sense bytes */
        memset (dev->sense, 0, sizeof(dev->sense));

        /* Return unit status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0xE4:
    /*---------------------------------------------------------------*/
    /* SENSE ID                                                      */
    /*---------------------------------------------------------------*/
        /* Calculate residual byte count */
        num = (count < dev->numdevid) ? count : dev->numdevid;
        *residual = count - num;
        if (count < dev->numdevid) *more = 1;

        /* Copy device identifier bytes to channel I/O buffer */
        memcpy (iobuf, dev->devid, num);

        /* Return unit status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    default:
    /*---------------------------------------------------------------*/
    /* INVALID OPERATION                                             */
    /*---------------------------------------------------------------*/
        /* Set command reject sense byte, and unit check status */
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;

    } /* end switch(code) */

} /* end function fbadasd_execute_ccw */

