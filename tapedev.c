/* TAPEDEV.C    (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Tape Device Handler                          */

/*-------------------------------------------------------------------*/
/* This module contains device handling functions for emulated       */
/* 3420 magnetic tape devices for the Hercules ESA/390 emulator.     */
/*                                                                   */
/* Two emulated tape formats are supported:                          */
/* 1. AWSTAPE   This is the format used by the P/390.                */
/*              The entire tape is contained in a single flat file.  */
/*              Each tape block is preceded by a 6-byte header.      */
/*              Files are separated by tape marks, which consist     */
/*              of headers with zero block length.                   */
/*              The file can reside on disk, CDROM, or 4mm DAT tape. */
/*              AWSTAPE files are readable and writable.             */
/* 2. OMATAPE   This is the Optical Media Attach device format.      */
/*              Each physical file on the tape is represented by     */
/*              a separate flat file.  The collection of files that  */
/*              make up the physical tape is obtained from an ASCII  */
/*              text file called the "tape description file", whose  */
/*              file name is always tapes/xxxxxx.tdf (where xxxxxx   */
/*              is the volume serial number of the tape).            */
/*              Three formats of tape files are supported:           */
/*              * FIXED files contain fixed length EBCDIC blocks     */
/*                with no headers or delimiters. The block length    */
/*                is specified in the TDF file.                      */
/*              * TEXT files contain variable length ASCII blocks    */
/*                delimited by carriage return line feed sequences.  */
/*                The data is translated to EBCDIC by this module.   */
/*              * HEADER files contain variable length blocks of     */
/*                EBCDIC data prefixed by a 16-byte header.          */
/*              The TDF file and all of the tape files must reside   */
/*              reside under the same directory which is normally    */
/*              on CDROM but can be on disk.                         */
/*              OMATAPE files are supported as read-only media.      */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Structure definition for tape block headers                       */
/*-------------------------------------------------------------------*/

/*
 * The integer fields in the AWSTAPE and OMATAPE headers are encoded
 * in the Intel format (i.e. the bytes of the integer are held in
 * reverse order).  For this reason the integers are defined as byte
 * arrays, and the bytes are fetched individually in order to make
 * the code portable across architectures which use either the Intel
 * format or the S/370 format.
 *
 * Block length fields contain the length of the emulated tape block
 * and do not include the length of the header.
 *
 * For the AWSTAPE format, a tape mark is indicated by a header with
 * a block length of zero.
 * For the OMATAPE format, a tape mark is indicated by a header with
 * a block length of X'FFFFFFFF'.
 *
 */

typedef struct _AWSTAPE_BLKHDR {
        HWORD   curblkl;                /* Length of this block      */
        HWORD   prvblkl;                /* Length of previous block  */
        BYTE    flags1;                 /* Flags byte 1              */
        BYTE    flags2;                 /* Flags byte 2              */
    } AWSTAPE_BLKHDR;

typedef struct _OMATAPE_BLKHDR {
        FWORD   curblkl;                /* Length of this block      */
        FWORD   prvhdro;                /* Offset of previous block
                                           header from start of file */
        FWORD   omaid;                  /* OMA identifier (contains
                                           ASCII characters "@HDF")  */
        FWORD   resv;                   /* Reserved                  */
    } OMATAPE_BLKHDR;


/*-------------------------------------------------------------------*/
/* Initialize the device handler                                     */
/*-------------------------------------------------------------------*/
int tapedev_init_handler ( DEVBLK *dev, int argc, BYTE *argv[] )
{
    /* The first argument is the file name */
    if (argc == 0 || strlen(argv[0]) > sizeof(dev->filename)-1)
    {
        printf ("HHC201I File name missing or invalid\n");
        return -1;
    }

    /* Save the file name in the device block */
    strcpy (dev->filename, argv[0]);

    /* Initialize device dependent fields */
    dev->fd = -1;
    dev->curblkpos = -1;
    dev->nxtblkpos = 0;
    dev->prvblkpos = -1;

    /* Set number of sense bytes */
    dev->numsense = 24;

    return 0;
} /* end function tapedev_init_handler */


/*-------------------------------------------------------------------*/
/* Execute a Channel Command Word                                    */
/*-------------------------------------------------------------------*/
void tapedev_execute_ccw ( DEVBLK *dev, BYTE code, BYTE flags,
        BYTE chained, U16 count, BYTE prevcode, int ccwseq,
        BYTE *iobuf, BYTE *more, BYTE *unitstat, U16 *residual )
{
AWSTAPE_BLKHDR  awshdr;                 /* AWSTAPE block header      */
int             rc;                     /* Return code               */
U16             curblkl;                /* Length of current block   */
U16             prvblkl;                /* Length of previous block  */
long            num;                    /* Number of bytes to read   */

    /* Open the device file if necessary */
    if (dev->fd < 0)
    {
        rc = open (dev->filename, O_RDONLY);
        if (rc < 0)
        {
            /* Handle open failure */
            perror("tapedev: open error");

            /* Set unit check with intervention required */
            dev->sense[0] = SENSE_IR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return;
        }
        dev->fd = rc;
    }

    /* Process depending on CCW opcode */
    switch (code) {

    case 0x01:
    /*---------------------------------------------------------------*/
    /* WRITE                                                         */
    /*---------------------------------------------------------------*/
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        break;

    case 0x02:
    /*---------------------------------------------------------------*/
    /* READ FORWARD                                                  */
    /*---------------------------------------------------------------*/
        /* Start a new block if not data-chained from previous CCW */
        if ((chained & CCW_FLAGS_CD) == 0)
        {
            /* Position to start of next block */
            rc = lseek (dev->fd, dev->nxtblkpos, SEEK_SET);
            if (rc < 0)
            {
                /* Handle seek error condition */
                perror("tapedev: lseek error");

                /* Set unit check with equipment check */
                dev->sense[0] = SENSE_EC;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
            dev->curblkpos = dev->nxtblkpos;

            /* Read the 6-byte block header */
            rc = read (dev->fd, &awshdr, sizeof(awshdr));
            if (rc < sizeof(awshdr))
            {
                /* Handle read error condition */
                if (rc < 0)
                    perror("tapedev: read error");
                else
                    printf("tapedev: unexpected end of file\n");

                /* Set unit check with equipment check */
                dev->sense[0] = SENSE_EC;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }

            /* Extract the block lengths from the block header */
            curblkl = ((U16)(awshdr.curblkl[1]) << 8)
                        | awshdr.curblkl[0];
            prvblkl = ((U16)(awshdr.prvblkl[1]) << 8)
                        | awshdr.prvblkl[0];

            /* Calculate the offsets of the next and previous blocks */
            dev->nxtblkpos = dev->curblkpos + sizeof(awshdr) + curblkl;
            dev->prvblkpos = dev->curblkpos - sizeof(awshdr) - prvblkl;

            /* Initialize number of bytes in current block */
            dev->curblkrem = curblkl;

            /* Zero length block (tapemark) produces unit exception */
            if (curblkl == 0)
            {
                *unitstat = CSW_CE | CSW_DE | CSW_UX;
                break;
            }

        } /* end if(!data-chained) */

        /* Calculate number of bytes to read and residual byte count */
        num = (count < dev->curblkrem) ? count : dev->curblkrem;
        *residual = count - num;
        if (count < dev->curblkrem) *more = 1;

        /* Read data from tape file */
        rc = read (dev->fd, iobuf, num);
        if (rc < num)
        {
            /* Handle read error condition */
            if (rc < 0)
                perror("tapedev: read error");
            else
                printf("tapedev: unexpected end of file\n");

            /* Set unit check with equipment check */
            dev->sense[0] = SENSE_EC;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Update length remaining in current block */
        dev->curblkrem -= num;

        /* Set normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x03:
    /*---------------------------------------------------------------*/
    /* CONTROL NO-OPERATION                                          */
    /*---------------------------------------------------------------*/
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x07:
    /*---------------------------------------------------------------*/
    /* REWIND                                                        */
    /*---------------------------------------------------------------*/
        /* Reset next block position to start of file */
        dev->nxtblkpos = 0;

        /* Set unit status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x0F:
    /*---------------------------------------------------------------*/
    /* REWIND UNLOAD                                                 */
    /*---------------------------------------------------------------*/
        /* Close the file and reset position to start */
        close (dev->fd);
        dev->fd = -1;
        dev->nxtblkpos = 0;

        /* Set unit status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x1F:
    /*---------------------------------------------------------------*/
    /* WRITE TAPE MARK                                               */
    /*---------------------------------------------------------------*/
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        break;

    case 0x27:
    /*---------------------------------------------------------------*/
    /* BACKSPACE BLOCK                                               */
    /*---------------------------------------------------------------*/
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        break;

    case 0x2F:
    /*---------------------------------------------------------------*/
    /* BACKSPACE FILE                                                */
    /*---------------------------------------------------------------*/
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        break;

    case 0x37:
    /*---------------------------------------------------------------*/
    /* FORWARD SPACE BLOCK                                           */
    /*---------------------------------------------------------------*/
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        break;

    case 0x3F:
    /*---------------------------------------------------------------*/
    /* FORWARD SPACE FILE                                            */
    /*---------------------------------------------------------------*/
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        break;

    case 0xCB: /* 9-track 800 bpi */
    case 0xC3: /* 9-track 1600 bpi */
    case 0xD3: /* 9-track 6250 bpi */
    /*---------------------------------------------------------------*/
    /* MODE SET                                                      */
    /*---------------------------------------------------------------*/
        *residual = 0;
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

    default:
    /*---------------------------------------------------------------*/
    /* INVALID OPERATION                                             */
    /*---------------------------------------------------------------*/
        /* Set command reject sense byte, and unit check status */
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;

    } /* end switch(code) */

} /* end function tapedev_execute_ccw */

