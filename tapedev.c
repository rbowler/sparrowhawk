/* TAPEDEV.C    (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Tape Device Handler                          */

/*-------------------------------------------------------------------*/
/* This module contains device handling functions for emulated       */
/* 3420 magnetic tape devices for the Hercules ESA/390 emulator.     */
/*                                                                   */
/* Three emulated tape formats are supported:                        */
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
/* 3. SCSITAPE  This format allows reading and writing of 4mm or     */
/*              8mm DAT tape, 9-track open-reel tape, or 3480-type   */
/*              cartridge on an appropriate SCSI-attached drive.     */
/*              All SCSI tapes are processed using the generalized   */
/*              SCSI tape driver (st.c) which is controlled using    */
/*              the MTIOCxxx set of IOCTL commands.                  */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      3480 commands contributed by Jan Jaeger                      */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Reference information for OMA file formats:                       */
/* SC53-1200 S/370 and S/390 Optical Media Attach/2 User's Guide     */
/* SC53-1201 S/370 and S/390 Optical Media Attach/2 Technical Ref    */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Internal macro definitions                                        */
/*-------------------------------------------------------------------*/
#define MAX_BLKLEN              65530   /* Maximum I/O buffer size   */

/*-------------------------------------------------------------------*/
/* Definitions for 3480 commands                                     */
/*-------------------------------------------------------------------*/

/* Format control byte for Load Display command */
#define FCB_FS                  0xE0    /* Format control bits...    */
#define FCB_FS_NODISP           0x60    /* Do not display messages   */
#define FCB_AM                  0x10    /* Alternate messages        */
#define FCB_BM                  0x80    /* Blinking message          */
#define FCB_DM                  0x40    /* Display low/high message  */

/* Path state byte for Sense Path Group ID command */
#define SPG_PATHSTAT            0xC0    /* Pathing status bits...    */
#define SPG_PATHSTAT_RESET      0x00    /* ...reset                  */
#define SPG_PATHSTAT_RESV       0x40    /* ...reserved bit setting   */
#define SPG_PATHSTAT_UNGROUPED  0x80    /* ...ungrouped              */
#define SPG_PATHSTAT_GROUPED    0xC0    /* ...grouped                */
#define SPG_PARTSTAT            0x30    /* Partitioning status bits..*/
#define SPG_PARTSTAT_IENABLED   0x00    /* ...implicitly enabled     */
#define SPG_PARTSTAT_RESV       0x10    /* ...reserved bit setting   */
#define SPG_PARTSTAT_DISABLED   0x20    /* ...disabled               */
#define SPG_PARTSTAT_XENABLED   0x30    /* ...explicitly enabled     */
#define SPG_PATHMODE            0x08    /* Path mode bit...          */
#define SPG_PATHMODE_SINGLE     0x00    /* ...single path mode       */
#define SPG_PATHMODE_RESV       0x08    /* ...reserved bit setting   */
#define SPG_RESERVED            0x07    /* Reserved bits, must be 0  */

/* Function control byte for Set Path Group ID command */
#define SPG_SET_MULTIPATH       0x80    /* Set multipath mode        */
#define SPG_SET_COMMAND         0x60    /* Set path command bits...  */
#define SPG_SET_ESTABLISH       0x00    /* ...establish group        */
#define SPG_SET_DISBAND         0x20    /* ...disband group          */
#define SPG_SET_RESIGN          0x40    /* ...resign from group      */
#define SPG_SET_COMMAND_RESV    0x60    /* ...reserved bit setting   */
#define SPG_SET_RESV            0x1F    /* Reserved bits, must be 0  */

/*-------------------------------------------------------------------*/
/* Definitions for tape device type field in device block            */
/*-------------------------------------------------------------------*/
#define TAPEDEVT_AWSTAPE        1       /* AWSTAPE format disk file  */
#define TAPEDEVT_OMATAPE        2       /* OMATAPE format disk files */
#define TAPEDEVT_SCSITAPE       3       /* Physical SCSI tape        */

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
 * OMATAPE blocks are followed by padding bytes if necessary so that
 * the next header starts on a 16-byte boundary.
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

/* Definitions for AWSTAPE_BLKHDR flags byte 1 */
#define AWSTAPE_FLAG1_NEWREC    0x80    /* Start of new record       */
#define AWSTAPE_FLAG1_TAPEMARK  0x40    /* Tape mark                 */
#define AWSTAPE_FLAG1_ENDREC    0x20    /* End of record             */

typedef struct _OMATAPE_BLKHDR {
        FWORD   curblkl;                /* Length of this block      */
        FWORD   prvhdro;                /* Offset of previous block
                                           header from start of file */
        FWORD   omaid;                  /* OMA identifier (contains
                                           ASCII characters "@HDF")  */
        FWORD   resv;                   /* Reserved                  */
    } OMATAPE_BLKHDR;

/*-------------------------------------------------------------------*/
/* Structure definition for OMA tape descriptor array                */
/*-------------------------------------------------------------------*/
typedef struct _OMATAPE_DESC {
        BYTE    filename[256];          /* Filename of data file     */
        BYTE    format;                 /* H=HEADERS,T=TEXT,F=FIXED  */
        BYTE    resv;                   /* Reserved for alignment    */
        U16     blklen;                 /* Fixed block length        */
    } OMATAPE_DESC;


/*-------------------------------------------------------------------*/
/* Open an AWSTAPE format file                                       */
/*                                                                   */
/* If successful, the file descriptor is stored in the device block  */
/* and the return value is zero.  Otherwise the return value is -1.  */
/*-------------------------------------------------------------------*/
static int open_awstape (DEVBLK *dev, BYTE *unitstat)
{
int             rc;                     /* Return code               */

    /* Open the AWSTAPE file */
    rc = open (dev->filename, O_RDWR);

    /* Check for successful open */
    if (rc < 0)
    {
        logmsg ("HHC201I Error opening %s: %s\n",
                dev->filename, strerror(errno));

        dev->sense[0] = SENSE_IR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Store the file descriptor in the device block */
    dev->fd = rc;
    return 0;

} /* end function open_awstape */

/*-------------------------------------------------------------------*/
/* Read a block from an AWSTAPE format file                          */
/*                                                                   */
/* If successful, return value is block length read.                 */
/* If tapemark, return value is zero and unitstat is set to CE+DE+UX */
/* If error, return value is -1 and unitstat is set to CE+DE+UC      */
/*-------------------------------------------------------------------*/
static int read_awstape (DEVBLK *dev, BYTE *buf, BYTE *unitstat)
{
int             rc;                     /* Return code               */
AWSTAPE_BLKHDR  awshdr;                 /* AWSTAPE block header      */
U16             curblkl;                /* Length of current block   */
U16             prvblkl;                /* Length of previous block  */

    /* Initialize current block position */
    dev->curblkpos = dev->nxtblkpos;

    /* Read the 6-byte block header */
    rc = read (dev->fd, &awshdr, sizeof(awshdr));
    if (rc < sizeof(awshdr))
    {
        /* Handle read error condition */
        if (rc < 0)
            logmsg ("HHC202I Error reading block header "
                    "at offset %8.8lX in file %s: %s\n",
                    dev->curblkpos, dev->filename, strerror(errno));
        else
            logmsg ("HHC203I Unexpected end of file in block header "
                    "at offset %8.8lX in file %s\n",
                    dev->curblkpos, dev->filename);

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Extract the block lengths from the block header */
    curblkl = ((U16)(awshdr.curblkl[1]) << 8)
                | awshdr.curblkl[0];
    prvblkl = ((U16)(awshdr.prvblkl[1]) << 8)
                | awshdr.prvblkl[0];

    /* Calculate the offsets of the next and previous blocks */
    dev->curblklen = curblkl;
    dev->nxtblkpos = dev->curblkpos + sizeof(awshdr) + curblkl;
    dev->prvblkpos = dev->curblkpos - sizeof(awshdr) - prvblkl;

    /* Zero length block (tapemark) produces unit exception */
    if (curblkl == 0)
    {
        *unitstat = CSW_CE | CSW_DE | CSW_UX;
        return 0;
    }

    /* Read data block from tape file */
    rc = read (dev->fd, buf, curblkl);
    if (rc < curblkl)
    {
        /* Handle read error condition */
        if (rc < 0)
            logmsg ("HHC204I Error reading data block "
                    "at offset %8.8lX in file %s: %s\n",
                    dev->curblkpos, dev->filename, strerror(errno));
        else
            logmsg ("HHC205I Unexpected end of file in data block "
                    "at offset %8.8lX in file %s\n",
                    dev->curblkpos, dev->filename);

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Return normal status and block length */
    *unitstat = CSW_CE | CSW_DE;
    return curblkl;

} /* end function read_awstape */

/*-------------------------------------------------------------------*/
/* Write a block to an AWSTAPE format file                           */
/*                                                                   */
/* If successful, return value is zero.                              */
/* If error, return value is -1 and unitstat is set to CE+DE+UC      */
/*-------------------------------------------------------------------*/
static int write_awstape (DEVBLK *dev, BYTE *buf, U16 curblkl,
                        BYTE *unitstat)
{
int             rc;                     /* Return code               */
AWSTAPE_BLKHDR  awshdr;                 /* AWSTAPE block header      */
U16             prvblkl;                /* Length of previous block  */

    /* Initialize current block position */
    prvblkl = dev->curblklen;
    dev->curblkpos = dev->nxtblkpos;
    dev->curblklen = curblkl;

    /* Build the 6-byte block header */
    awshdr.curblkl[0] = curblkl & 0xFF;
    awshdr.curblkl[1] = (curblkl >> 8) & 0xFF;
    awshdr.prvblkl[0] = prvblkl & 0xFF;
    awshdr.prvblkl[1] = (prvblkl >>8) & 0xFF;
    awshdr.flags1 = AWSTAPE_FLAG1_NEWREC | AWSTAPE_FLAG1_ENDREC;
    awshdr.flags2 = 0;

    /* Write the block header */
    rc = write (dev->fd, &awshdr, sizeof(awshdr));
    if (rc < sizeof(awshdr))
    {
        /* Handle write error condition */
        logmsg ("HHC210I Error writing block header "
                "at offset %8.8lX in file %s: %s\n",
                dev->curblkpos, dev->filename, strerror(errno));

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Calculate the offsets of the next and previous blocks */
    dev->nxtblkpos = dev->curblkpos + sizeof(awshdr) + curblkl;
    dev->prvblkpos = dev->curblkpos - sizeof(awshdr) - prvblkl;

    /* Write the data block */
    rc = write (dev->fd, buf, curblkl);
    if (rc < curblkl)
    {
        /* Handle write error condition */
        logmsg ("HHC211I Error writing data block "
                "at offset %8.8lX in file %s: %s\n",
                dev->curblkpos, dev->filename, strerror(errno));

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Return normal status */
    *unitstat = CSW_CE | CSW_DE;
    return 0;

} /* end function write_awstape */

/*-------------------------------------------------------------------*/
/* Write a tapemark to an AWSTAPE format file                        */
/*                                                                   */
/* If successful, return value is zero.                              */
/* If error, return value is -1 and unitstat is set to CE+DE+UC      */
/*-------------------------------------------------------------------*/
static int write_awsmark (DEVBLK *dev, BYTE *unitstat)
{
int             rc;                     /* Return code               */
AWSTAPE_BLKHDR  awshdr;                 /* AWSTAPE block header      */
U16             prvblkl;                /* Length of previous block  */

    /* Initialize current block position */
    prvblkl = dev->curblklen;
    dev->curblkpos = dev->nxtblkpos;
    dev->curblklen = 0;

    /* Build the 6-byte block header */
    awshdr.curblkl[0] = 0;
    awshdr.curblkl[1] = 0;
    awshdr.prvblkl[0] = prvblkl & 0xFF;
    awshdr.prvblkl[1] = (prvblkl >>8) & 0xFF;
    awshdr.flags1 = AWSTAPE_FLAG1_TAPEMARK;
    awshdr.flags2 = 0;

    /* Write the block header */
    rc = write (dev->fd, &awshdr, sizeof(awshdr));
    if (rc < sizeof(awshdr))
    {
        /* Handle write error condition */
        logmsg ("HHC212I Error writing block header "
                "at offset %8.8lX in file %s: %s\n",
                dev->curblkpos, dev->filename, strerror(errno));

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Calculate the offsets of the next and previous blocks */
    dev->nxtblkpos = dev->curblkpos + sizeof(awshdr);
    dev->prvblkpos = dev->curblkpos - sizeof(awshdr) - prvblkl;

    /* Return normal status */
    *unitstat = CSW_CE | CSW_DE;
    return 0;

} /* end function write_awsmark */

/*-------------------------------------------------------------------*/
/* Forward space to next logical file of AWSTAPE format file         */
/*                                                                   */
/* If successful, return value is zero, and the current file         */
/* number in the device block in incremented.                        */
/* If error, return value is -1 and unitstat is set to CE+DE+UC      */
/*-------------------------------------------------------------------*/
static int fsf_awstape (DEVBLK *dev, BYTE *unitstat)
{
int             rc;                     /* Return code               */
AWSTAPE_BLKHDR  awshdr;                 /* AWSTAPE block header      */
U16             curblkl;                /* Length of current block   */
U16             prvblkl;                /* Length of previous block  */

    while (1)
    {
        /* Initialize current block position */
        dev->curblkpos = dev->nxtblkpos;

        /* Read the 6-byte block header */
        rc = read (dev->fd, &awshdr, sizeof(awshdr));
        if (rc < sizeof(awshdr))
        {
            /* Handle read error condition */
            if (rc < 0)
                logmsg ("HHC213I Error reading block header "
                        "at offset %8.8lX in file %s: %s\n",
                        dev->curblkpos, dev->filename,
                        strerror(errno));
            else
                logmsg ("HHC214I Unexpected end of file in block "
                        "header at offset %8.8lX in file %s\n",
                        dev->curblkpos, dev->filename);

            /* Set unit check with equipment check */
            dev->sense[0] = SENSE_EC;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return -1;
        }

        /* Extract the block lengths from the block header */
        curblkl = ((U16)(awshdr.curblkl[1]) << 8)
                    | awshdr.curblkl[0];
        prvblkl = ((U16)(awshdr.prvblkl[1]) << 8)
                    | awshdr.prvblkl[0];

        /* Calculate the offsets of the next and previous blocks */
        dev->curblklen = curblkl;
        dev->nxtblkpos = dev->curblkpos + sizeof(awshdr) + curblkl;
        dev->prvblkpos = dev->curblkpos - sizeof(awshdr) - prvblkl;

        /* Exit loop if zero length block (tapemark) was read */
        if (curblkl == 0) break;

        /* Skip past data block to next block header */
        rc = lseek (dev->fd, curblkl, SEEK_CUR);
        if (rc < 0)
        {
            /* Handle seek error condition */
            logmsg ("HHC215I Error seeking past data block "
                    "at offset %8.8lX in file %s: %s\n",
                    dev->curblkpos, dev->filename, strerror(errno));

            /* Set unit check with equipment check */
            dev->sense[0] = SENSE_EC;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return -1;
        }

    } /* end while */

    /* Increment current file number */
    dev->curfilen++;

    /* Return normal status */
    *unitstat = CSW_CE | CSW_DE;
    return 0;

} /* end function fsf_awstape */

/*-------------------------------------------------------------------*/
/* Open a SCSI tape device                                           */
/*                                                                   */
/* If successful, the file descriptor is stored in the device block  */
/* and the return value is zero.  Otherwise the return value is -1.  */
/*-------------------------------------------------------------------*/
static int open_scsitape (DEVBLK *dev, BYTE *unitstat)
{
int             rc;                     /* Return code               */
struct mtop     opblk;                  /* Area for MTIOCTOP ioctl   */

    /* Open the SCSI tape device */
    rc = open (dev->filename, O_RDONLY);

    /* Check for successful open */
    if (rc < 0)
    {
        logmsg ("HHC220I Error opening %s: %s\n",
                dev->filename, strerror(errno));

        dev->sense[0] = SENSE_IR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Store the file descriptor in the device block */
    dev->fd = rc;

    /* Set the tape device to process variable length blocks */
    opblk.mt_op = MTSETBLK;
    opblk.mt_count = 0;
    rc = ioctl (dev->fd, MTIOCTOP, (char*)&opblk);
    if (rc < 0)
    {
        logmsg ("HHC221I Error setting attributes for %s: %s\n",
                dev->filename, strerror(errno));

        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Rewind the tape to the beginning */
    opblk.mt_op = MTREW;
    opblk.mt_count = 1;
    rc = ioctl (dev->fd, MTIOCTOP, (char*)&opblk);
    if (rc < 0)
    {
        logmsg ("HHC222I Error rewinding %s: %s\n",
                dev->filename, strerror(errno));

        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    return 0;

} /* end function open_scsitape */

/*-------------------------------------------------------------------*/
/* Read a block from a SCSI tape device                              */
/*                                                                   */
/* If successful, return value is block length read.                 */
/* If tapemark, return value is zero and unitstat is set to CE+DE+UX */
/* If error, return value is -1 and unitstat is set to CE+DE+UC      */
/*-------------------------------------------------------------------*/
static int read_scsitape (DEVBLK *dev, BYTE *buf, BYTE *unitstat)
{
int             rc;                     /* Return code               */

    /* Read data block from SCSI tape device */
    rc = read (dev->fd, buf, MAX_BLKLEN);
    if (rc < 0)
    {
        /* Handle read error condition */
        logmsg ("HHC223I Error reading data block from %s: %s\n",
                dev->filename, strerror(errno));

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Zero length block (tapemark) produces unit exception */
    if (rc == 0)
    {
        *unitstat = CSW_CE | CSW_DE | CSW_UX;
        return 0;
    }

    /* Return normal status and block length */
    *unitstat = CSW_CE | CSW_DE;
    return rc;

} /* end function read_scsitape */

/*-------------------------------------------------------------------*/
/* Write a block to a SCSI tape device                               */
/*                                                                   */
/* If successful, return value is zero.                              */
/* If error, return value is -1 and unitstat is set to CE+DE+UC      */
/*-------------------------------------------------------------------*/
static int write_scsitape (DEVBLK *dev, BYTE *buf, U16 len,
                        BYTE *unitstat)
{
int             rc;                     /* Return code               */

    /* Write data block to SCSI tape device */
    rc = write (dev->fd, buf, len);
    if (rc < len)
    {
        /* Handle write error condition */
        logmsg ("HHC224I Error writing data block to %s: %s\n",
                dev->filename, strerror(errno));

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Return normal status */
    *unitstat = CSW_CE | CSW_DE;
    return 0;

} /* end function write_scsitape */

/*-------------------------------------------------------------------*/
/* Write a tapemark to a SCSI tape device                            */
/*                                                                   */
/* If successful, return value is zero.                              */
/* If error, return value is -1 and unitstat is set to CE+DE+UC      */
/*-------------------------------------------------------------------*/
static int write_scsimark (DEVBLK *dev, BYTE *unitstat)
{
int             rc;                     /* Return code               */
struct mtop     opblk;                  /* Area for MTIOCTOP ioctl   */

    /* Write tape mark to SCSI tape */
    opblk.mt_op = MTWEOF;
    opblk.mt_count = 1;
    rc = ioctl (dev->fd, MTIOCTOP, (char*)&opblk);
    if (rc < 0)
    {
        /* Handle write error condition */
        logmsg ("HHC225I Error writing tapemark to %s: %s\n",
                dev->filename, strerror(errno));

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Return normal status */
    *unitstat = CSW_CE | CSW_DE;
    return 0;

} /* end function write_scsimark */

/*-------------------------------------------------------------------*/
/* Forward space to next file of SCSI tape device                    */
/*                                                                   */
/* If successful, return value is zero, and the current file         */
/* number in the device block in incremented.                        */
/* If error, return value is -1 and unitstat is set to CE+DE+UC      */
/*-------------------------------------------------------------------*/
static int fsf_scsitape (DEVBLK *dev, BYTE *unitstat)
{
int             rc;                     /* Return code               */
struct mtop     opblk;                  /* Area for MTIOCTOP ioctl   */

    /* Forward space file on SCSI tape */
    opblk.mt_op = MTFSF;
    opblk.mt_count = 1;
    rc = ioctl (dev->fd, MTIOCTOP, (char*)&opblk);
    if (rc < 0)
    {
        /* Handle error condition */
        logmsg ("HHC226I Forward space file error on %s: %s\n",
                dev->filename, strerror(errno));

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Increment current file number */
    dev->curfilen++;

    /* Return normal status */
    *unitstat = CSW_CE | CSW_DE;
    return 0;

} /* end function fsf_scsitape */

/*-------------------------------------------------------------------*/
/* Read a block from an OMA tape file in OMA headers format          */
/*                                                                   */
/* If successful, return value is block length read.                 */
/* If tapemark, return value is zero and unitstat is set to CE+DE+UX */
/* If error, return value is -1 and unitstat is set to CE+DE+UC      */
/*-------------------------------------------------------------------*/
static int read_omaheaders (DEVBLK *dev, OMATAPE_DESC *omadesc,
                        BYTE *buf, BYTE *unitstat)
{
int             rc;                     /* Return code               */
OMATAPE_BLKHDR  omahdr;                 /* OMATAPE block header      */
S32             curblkl;                /* Length of current block   */
S32             prvhdro;                /* Offset of previous header */
int             padding;                /* Number of padding bytes   */

    /* Seek to start of block header */
    dev->curblkpos = dev->nxtblkpos;

    rc = lseek (dev->fd, dev->curblkpos, SEEK_SET);
    if (rc < 0)
    {
        /* Handle seek error condition */
        logmsg ("HHC230I Error seeking to offset %8.8lX "
                "in file %s: %s\n",
                dev->curblkpos, dev->filename, strerror(errno));

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Read the 16-byte block header */
    rc = read (dev->fd, &omahdr, sizeof(omahdr));
    if (rc < sizeof(omahdr))
    {
        /* Handle read error condition */
        if (rc < 0)
            logmsg ("HHC231I Error reading block header "
                    "at offset %8.8lX in file %s: %s\n",
                    dev->curblkpos, omadesc->filename,
                    strerror(errno));
        else
            logmsg ("HHC232I Unexpected end of file in block header "
                    "at offset %8.8lX in file %s\n",
                    dev->curblkpos, omadesc->filename);

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Extract the current block length and previous header offset */
    curblkl = (S32)(((U32)(omahdr.curblkl[3]) << 24)
                    | ((U32)(omahdr.curblkl[2]) << 16)
                    | ((U32)(omahdr.curblkl[1]) << 8)
                    | omahdr.curblkl[0]);
    prvhdro = (S32)((U32)(omahdr.prvhdro[3]) << 24)
                    | ((U32)(omahdr.prvhdro[2]) << 16)
                    | ((U32)(omahdr.prvhdro[1]) << 8)
                    | omahdr.prvhdro[0];

    /* Check for valid block header */
    if (curblkl < -1 || curblkl == 0 || curblkl > MAX_BLKLEN
        || memcmp(omahdr.omaid, "@HDF", 4) != 0)
    {
        logmsg ("HHC233I Invalid block header "
                "at offset %8.8lX in file %s\n",
                dev->curblkpos, omadesc->filename);

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Calculate the number of padding bytes which follow the data */
    padding = (16 - (curblkl & 15)) & 15;

    /* Calculate the offsets of the next and previous blocks */
    dev->curblklen = curblkl;
    dev->nxtblkpos = dev->curblkpos
                        + sizeof(omahdr) + curblkl + padding;
    dev->prvblkpos = prvhdro;

    /* Block length -1 (tapemark) produces unit exception */
    if (curblkl == -1)
    {
        *unitstat = CSW_CE | CSW_DE | CSW_UX;
        return 0;
    }

    /* Read data block from tape file */
    rc = read (dev->fd, buf, curblkl);
    if (rc < curblkl)
    {
        /* Handle read error condition */
        if (rc < 0)
            logmsg ("HHC234I Error reading data block "
                    "at offset %8.8lX in file %s: %s\n",
                    dev->curblkpos, omadesc->filename,
                    strerror(errno));
        else
            logmsg ("HHC235I Unexpected end of file in data block "
                    "at offset %8.8lX in file %s\n",
                    dev->curblkpos, omadesc->filename);

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Return normal status and block length */
    *unitstat = CSW_CE | CSW_DE;
    return curblkl;

} /* end function read_omaheaders */

/*-------------------------------------------------------------------*/
/* Read a block from an OMA tape file in fixed block format          */
/*                                                                   */
/* If successful, return value is block length read.                 */
/* If tapemark, return value is zero and unitstat is set to CE+DE+UX */
/* If error, return value is -1 and unitstat is set to CE+DE+UC      */
/*-------------------------------------------------------------------*/
static int read_omafixed (DEVBLK *dev, OMATAPE_DESC *omadesc,
                        BYTE *buf, BYTE *unitstat)
{
int             rc;                     /* Return code               */

    /* Initialize current block position */
    dev->curblkpos = dev->nxtblkpos;

    /* Calculate the offsets of the next and previous blocks */
    dev->curblklen = omadesc->blklen;
    dev->nxtblkpos = dev->curblkpos + omadesc->blklen;
    dev->prvblkpos = dev->curblkpos - omadesc->blklen;

    /* Read fixed length data block from tape file */
    rc = read (dev->fd, buf, omadesc->blklen);
    if (rc < omadesc->blklen)
    {
        /* End of file (tapemark) produces unit exception */
        if (rc == 0)
        {
            *unitstat = CSW_CE | CSW_DE | CSW_UX;
            return 0;
        }

        /* Handle read error condition */
        if (rc < 0)
            logmsg ("HHC236I Error reading data block "
                    "at offset %8.8lX in file %s: %s\n",
                    dev->curblkpos, omadesc->filename,
                    strerror(errno));
        else
            logmsg ("HHC237I Unexpected end of file in data block "
                    "at offset %8.8lX in file %s\n",
                    dev->curblkpos, omadesc->filename);

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Return normal status and block length */
    *unitstat = CSW_CE | CSW_DE;
    return omadesc->blklen;

} /* end function read_omafixed */

/*-------------------------------------------------------------------*/
/* Read a block from an OMA tape file in ASCII text format           */
/*                                                                   */
/* If successful, return value is block length read.                 */
/* If tapemark, return value is zero and unitstat is set to CE+DE+UX */
/* If error, return value is -1 and unitstat is set to CE+DE+UC      */
/*-------------------------------------------------------------------*/
static int read_omatext (DEVBLK *dev, OMATAPE_DESC *omadesc,
                        BYTE *buf, BYTE *unitstat)
{
int             rc;                     /* Return code               */
int             num;                    /* Number of characters read */
int             pos;                    /* Position in I/O buffer    */
BYTE            c;                      /* Character work area       */

    /* Initialize current block position */
    dev->curblkpos = dev->nxtblkpos;

    /* Read data from tape file until end of line */
    for (num = 0, pos = 0; ; num++)
    {
        rc = read (dev->fd, &c, 1);
        if (rc < 1) break;

        /* Ignore carriage return character */
        if (c == '\r') continue;

        /* Exit if newline character */
        if (c == '\n') break;

        /* Ignore characters in excess of I/O buffer length */
        if (pos >= MAX_BLKLEN) continue;

        /* Translate character to EBCDIC and copy to I/O buffer */
        buf[pos++] = ascii_to_ebcdic[c];

    } /* end for(num) */

    /* Calculate the offsets of the next and previous blocks */
    dev->curblklen = num;
    dev->nxtblkpos = dev->curblkpos + num;
    dev->prvblkpos = -1;

    /* End of file (tapemark) produces unit exception */
    if (rc == 0 && num == 0)
    {
        *unitstat = CSW_CE | CSW_DE | CSW_UX;
        return 0;
    }

    /* Handle read error condition */
    if (rc < 1)
    {
        if (rc < 0)
            logmsg ("HHC240I Error reading data block "
                    "at offset %8.8lX in file %s: %s\n",
                    dev->curblkpos, omadesc->filename,
                    strerror(errno));
        else
            logmsg ("HHC241I Unexpected end of file in data block "
                    "at offset %8.8lX in file %s\n",
                    dev->curblkpos, omadesc->filename);

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Check for invalid zero length block */
    if (pos == 0)
    {
        logmsg ("HHC242I Invalid zero length block "
                "at offset %8.8lX in file %s\n",
                dev->curblkpos, omadesc->filename);

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Return normal status and block length */
    *unitstat = CSW_CE | CSW_DE;
    return pos;

} /* end function read_omatext */

/*-------------------------------------------------------------------*/
/* Forward space to next file of OMA tape device                     */
/*                                                                   */
/* For OMA tape devices, the forward space file operation is         */
/* achieved by closing the current file and incrementing the         */
/* current file number in the device block.  This will cause         */
/* the next file to be opened when the next CCW is processed.        */
/*-------------------------------------------------------------------*/
static int fsf_omatape (DEVBLK *dev, BYTE *unitstat)
{
    /* Close the current OMA file */
    close (dev->fd);
    dev->fd = -1;
    dev->nxtblkpos = 0;
    dev->curblklen = 0;
    dev->curblkpos = -1;
    dev->nxtblkpos = 0;
    dev->prvblkpos = -1;

    /* Increment current file number */
    dev->curfilen++;

    /* Return normal status */
    *unitstat = CSW_CE | CSW_DE;
    return 0;

} /* end function fsf_omatape */

/*-------------------------------------------------------------------*/
/* Read the OMA tape descriptor file                                 */
/*-------------------------------------------------------------------*/
static int read_omadesc (DEVBLK *dev)
{
int             rc;                     /* Return code               */
int             i;                      /* Array subscript           */
int             pathlen;                /* Length of TDF path name   */
int             tdfsize;                /* Size of TDF file in bytes */
int             filecount;              /* Number of files           */
int             stmt;                   /* TDF file statement number */
int             fd;                     /* TDF file descriptor       */
struct stat     statbuf;                /* TDF file information      */
U32             blklen;                 /* Fixed block length        */
int             tdfpos;                 /* Position in TDF buffer    */
BYTE           *tdfbuf;                 /* -> TDF file buffer        */
BYTE           *tdfrec;                 /* -> TDF record             */
BYTE           *tdffilenm;              /* -> Filename in TDF record */
BYTE           *tdfformat;              /* -> Format in TDF record   */
BYTE           *tdfreckwd;              /* -> Keyword in TDF record  */
BYTE           *tdfblklen;              /* -> Length in TDF record   */
OMATAPE_DESC   *tdftab;                 /* -> Tape descriptor array  */
BYTE            c;                      /* Work area for sscanf      */

    /* Isolate the base path name of the TDF file */
    for (pathlen = strlen(dev->filename); pathlen > 0; )
    {
        pathlen--;
        if (dev->filename[pathlen-1] == '/') break;
    }

    if (pathlen < 7
        || strncasecmp(dev->filename+pathlen-7, "/tapes/", 7) != 0)
    {
        logmsg ("HHC243I Invalid filename %s: "
                "TDF files must be in the TAPES subdirectory\n",
                dev->filename+pathlen);
        return -1;
    }
    pathlen -= 7;

    /* Open the tape descriptor file */
    fd = open (dev->filename, O_RDONLY);
    if (fd < 0)
    {
        logmsg ("HHC244I Error opening TDF file %s: %s\n",
                dev->filename, strerror(errno));
        return -1;
    }

    /* Determine the size of the tape descriptor file */
    rc = fstat (fd, &statbuf);
    if (rc < 0)
    {
        logmsg ("HHC245I %s fstat error: %s\n",
                dev->filename, strerror(errno));
        close (fd);
        return -1;
    }
    tdfsize = statbuf.st_size;

    /* Obtain a buffer for the tape descriptor file */
    tdfbuf = malloc (tdfsize);
    if (tdfbuf == NULL)
    {
        logmsg ("HHC246I Cannot obtain buffer for TDF file %s: %s\n",
                dev->filename, strerror(errno));
        close (fd);
        return -1;
    }

    /* Read the tape descriptor file into the buffer */
    rc = read (fd, tdfbuf, tdfsize);
    if (rc < tdfsize)
    {
        logmsg ("HHC247I Error reading TDF file %s: %s\n",
                dev->filename, strerror(errno));
        free (tdfbuf);
        close (fd);
        return -1;
    }

    /* Close the tape descriptor file */
    close (fd);

    /* Check that the first record is a TDF header */
    if (memcmp(tdfbuf, "@TDF", 4) != 0)
    {
        logmsg ("HHC248I %s is not a valid TDF file\n",
                dev->filename);
        free (tdfbuf);
        return -1;
    }

    /* Count the number of linefeeds in the tape descriptor file
       to determine the size of the descriptor array required */
    for (i = 0, filecount = 0; i < tdfsize; i++)
    {
        if (tdfbuf[i] == '\n') filecount++;
    } /* end for(i) */

    /* Obtain storage for the tape descriptor array */
    tdftab = (OMATAPE_DESC*)malloc (filecount * sizeof(OMATAPE_DESC));
    if (tdftab == NULL)
    {
        logmsg ("HHC249I Cannot obtain buffer for TDF array: %s\n",
                strerror(errno));
        free (tdfbuf);
        return -1;
    }

    /* Build the tape descriptor array */
    for (filecount = 1, tdfpos = 0, stmt = 1; ; filecount++)
    {
        /* Clear the tape descriptor array entry */
        memset (&(tdftab[filecount]), 0, sizeof(OMATAPE_DESC));

        /* Point past the next linefeed in the TDF file */
        while (tdfpos < tdfsize && tdfbuf[tdfpos++] != '\n');
        stmt++;

        /* Exit at end of TDF file */
        if (tdfpos >= tdfsize) break;

        /* Mark the end of the TDF record with a null terminator */
        tdfrec = tdfbuf + tdfpos;
        while (tdfpos < tdfsize && tdfbuf[tdfpos]!='\r'
            && tdfbuf[tdfpos]!='\n') tdfpos++;
        if (tdfpos >= tdfsize) break;
        tdfbuf[tdfpos] = '\0';

        /* Exit if TM or EOT record */
        if (strcasecmp(tdfrec, "TM") == 0
            || strcasecmp(tdfrec, "EOT") == 0)
            break;

        /* Parse the TDF record */
        tdffilenm = strtok (tdfrec, " \t");
        tdfformat = strtok (NULL, " \t");
        tdfreckwd = strtok (NULL, " \t");
        tdfblklen = strtok (NULL, " \t");

        /* Check for missing fields */
        if (tdffilenm == NULL || tdfformat == NULL)
        {
            logmsg ("HHC250I Filename or format missing in "
                    "line %d of file %s\n",
                    stmt, dev->filename);
            free (tdftab);
            free (tdfbuf);
            return -1;
        }

        /* Check that the file name is not too long */
        if (pathlen + 1 + strlen(tdffilenm)
                > sizeof(tdftab[filecount].filename) - 1)
        {
            logmsg ("HHC251I Filename %s too long in "
                    "line %d of file %s\n",
                    tdffilenm, stmt, dev->filename);
            free (tdftab);
            free (tdfbuf);
            return -1;
        }

        /* Convert the file name to Unix format */
        for (i = 0; i < strlen(tdffilenm); i++)
        {
            if (tdffilenm[i] == '\\')
                tdffilenm[i] = '/';
            else
                tdffilenm[i] = tolower(tdffilenm[i]);
        } /* end for(i) */

        /* Prefix the file name with the base path name and
           save it in the tape descriptor array */
        strncpy (tdftab[filecount].filename, dev->filename, pathlen);
        if (tdffilenm[0] != '/')
            strcat (tdftab[filecount].filename, "/");
        strcat (tdftab[filecount].filename, tdffilenm);

        /* Check for valid file format code */
        if (strcasecmp(tdfformat, "HEADERS") == 0)
        {
            tdftab[filecount].format = 'H';
        }
        else if (strcasecmp(tdfformat, "TEXT") == 0)
        {
            tdftab[filecount].format = 'T';
        }
        else if (strcasecmp(tdfformat, "FIXED") == 0)
        {
            /* Check for RECSIZE keyword */
            if (tdfreckwd == NULL
                || strcasecmp(tdfreckwd, "RECSIZE") != 0)
            {
                logmsg ("HHC252I RECSIZE keyword missing in "
                        "line %d of file %s\n",
                        stmt, dev->filename);
                free (tdftab);
                free (tdfbuf);
                return -1;
            }

            /* Check for valid fixed block length */
            if (tdfblklen == NULL
                || sscanf(tdfblklen, "%u%c", &blklen, &c) != 1
                || blklen < 1 || blklen > MAX_BLKLEN)
            {
                logmsg ("HHC253I Invalid record size %s in "
                        "line %d of file %s\n",
                        tdfblklen, stmt, dev->filename);
                free (tdftab);
                free (tdfbuf);
                return -1;
            }

            /* Set format and block length in descriptor array */
            tdftab[filecount].format = 'F';
            tdftab[filecount].blklen = blklen;
        }
        else
        {
            logmsg ("HHC254I Invalid record format %s in "
                    "line %d of file %s\n",
                    tdfformat, stmt, dev->filename);
            free (tdftab);
            free (tdfbuf);
            return -1;
        }
    } /* end for(filecount) */

    /* Save the file count and TDF array pointer in the device block */
    dev->omafiles = filecount;
    dev->omadesc = tdftab;

    /* Release the TDF file buffer and exit */
    free (tdfbuf);
    return 0;

} /* end function read_omadesc */

/*-------------------------------------------------------------------*/
/* Open the OMATAPE file defined by the current file number          */
/*                                                                   */
/* The OMA tape descriptor file is read if necessary.                */
/* If successful, the file descriptor is stored in the device block  */
/* and the return value is zero.  Otherwise the return value is -1.  */
/*-------------------------------------------------------------------*/
static int open_omatape (DEVBLK *dev, BYTE *unitstat)
{
int             rc;                     /* Return code               */
OMATAPE_DESC   *omadesc;                /* -> OMA descriptor entry   */

    /* Read the OMA descriptor file if necessary */
    if (dev->omadesc == NULL)
    {
        rc = read_omadesc (dev);
        if (rc < 0)
        {
            dev->sense[0] = SENSE_IR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return -1;
        }
    }

    /* Unit exception if beyond end of tape */
    if (dev->curfilen >= dev->omafiles)
    {
        logmsg ("HHC255I Attempt to access beyond end of tape %s\n",
                dev->filename);

        *unitstat = CSW_CE | CSW_DE | CSW_UX;
        return -1;
    }

    /* Point to the current file entry in the OMA descriptor table */
    omadesc = (OMATAPE_DESC*)(dev->omadesc);
    omadesc += dev->curfilen;

    /* Open the OMATAPE file */
    rc = open (omadesc->filename, O_RDONLY);

    /* Check for successful open */
    if (rc < 0)
    {
        logmsg ("HHC256I Error opening %s: %s\n",
                omadesc->filename, strerror(errno));

        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Store the file descriptor in the device block */
    dev->fd = rc;
    return 0;

} /* end function open_omatape */

/*-------------------------------------------------------------------*/
/* Issue mount message as result of load display channel command     */
/*-------------------------------------------------------------------*/
static void issue_mount_msg (DEVBLK *dev, BYTE *buf)
{
int             i;                      /* Array subscript           */
BYTE            msg1[9], msg2[9];       /* Message areas (ASCIIZ)    */
BYTE            fcb;                    /* Format Control Byte       */

    /* Pick up format control byte */
    fcb = *buf++;

    /* Check to see if this is a 'nomessage' message */
    if ((fcb & FCB_FS) != FCB_FS_NODISP)
    {
        /* Copy and translate messages */
        for (i = 0; i < 8; i++)
            msg1[i] = ebcdic_to_ascii[*buf++];
        msg1[8] = '\0';
        for (i = 0; i < 8; i++)
            msg2[i] = ebcdic_to_ascii[*buf++];
        msg2[8] = '\0';

        /* If not both messages are to be shown
           then zeroize appropriate message */
        if (!(fcb & FCB_AM))
        {
            if (fcb & FCB_DM)
                msg1[0] = '\0';
            else
                msg2[0] = '\0';
        }

        /* Display message on console */
        logmsg ("HHC260I Tape msg %4.4X: %c %s %s\n",
                dev->devnum,
                (fcb & FCB_BM) ? '*' : ' ',   /* Indicate blinking */
                msg1,
                msg2);

    } /* end if(FCB_FS_NODISP) */

} /* end function issue_mount_message */

/*-------------------------------------------------------------------*/
/* Initialize the device handler                                     */
/*-------------------------------------------------------------------*/
int tapedev_init_handler (DEVBLK *dev, int argc, BYTE *argv[])
{
int             len;                    /* Length of file name       */
U16             cutype;                 /* Control unit type         */
BYTE            cumodel;                /* Control unit model number */
BYTE            devmodel;               /* Device model number       */
BYTE            devclass;               /* Device class              */
BYTE            devtcode;               /* Device type code          */
U32             sctlfeat;               /* Storage control features  */

    /* Release the previous OMA descriptor array if allocated */
    if (dev->omadesc != NULL)
    {
        free (dev->omadesc);
        dev->omadesc = NULL;
    }

    /* The first argument is the file name */
    if (argc == 0 || strlen(argv[0]) > sizeof(dev->filename)-1)
    {
        fprintf (stderr,
                "HHC261I File name missing or invalid\n");
        return -1;
    }

    /* Save the file name in the device block */
    strcpy (dev->filename, argv[0]);

    /* Use the file name to determine the device type */
    len = strlen(dev->filename);
    if (len >= 4 && strcasecmp(dev->filename + len - 4, ".tdf") == 0)
        dev->tapedevt = TAPEDEVT_OMATAPE;
    else if (len >= 5 && memcmp(dev->filename, "/dev/", 5) == 0)
        dev->tapedevt = TAPEDEVT_SCSITAPE;
    else
        dev->tapedevt = TAPEDEVT_AWSTAPE;

    /* Initialize device dependent fields */
    dev->fd = -1;
    dev->omadesc = NULL;
    dev->omafiles = 0;
    dev->curfilen = 1;
    dev->curblklen = 0;
    dev->curblkpos = -1;
    dev->nxtblkpos = 0;
    dev->prvblkpos = -1;
    dev->curblkrem = 0;
    dev->curbufoff = 0;

    /* Set number of sense bytes */
    dev->numsense = 24;

    /* Determine the control unit type and model number */
    if (dev->devtype == 0x3480)
    {
        cutype = 0x3480;
        cumodel = 0x01;
        devmodel = 0x01;
        devclass = 0x80;
        devtcode = 0x80;
        sctlfeat = 0x00000200;
    }
    else
    {
        cutype = 0x3803;
        cumodel = 0x02;
        devmodel = 0x06;
        devclass = 0x80;
        devtcode = 0x20;
        sctlfeat = 0x00000000;
    }

    /* Initialize the device identifier bytes */
    dev->devid[0] = 0xFF;
    dev->devid[1] = cutype >> 8;
    dev->devid[2] = cutype & 0xFF;
    dev->devid[3] = cumodel;
    dev->devid[4] = dev->devtype >> 8;
    dev->devid[5] = dev->devtype & 0xFF;
    dev->devid[6] = devmodel;
    dev->numdevid = 7;

    /* Initialize the device characteristics bytes */
    if (cutype != 0x3803)
    {
        memset (dev->devchar, 0, sizeof(dev->devchar));
        memcpy (dev->devchar, dev->devid+1, 6);
        dev->devchar[6] = (sctlfeat >> 24) & 0xFF;
        dev->devchar[7] = (sctlfeat >> 16) & 0xFF;
        dev->devchar[8] = (sctlfeat >> 8) & 0xFF;
        dev->devchar[9] = sctlfeat & 0xFF;
        dev->devchar[10] = devclass;
        dev->devchar[11] = devtcode;
        dev->devchar[40] = devtcode;
        dev->devchar[41] = devtcode;
        dev->numdevchar = 64;
    }

    return 0;
} /* end function tapedev_init_handler */


/*-------------------------------------------------------------------*/
/* Query the device definition                                       */
/*-------------------------------------------------------------------*/
void tapedev_query_device (DEVBLK *dev, BYTE **class,
                int buflen, BYTE *buffer)
{

    *class = "TAPE";
    snprintf (buffer, buflen, "%s", dev->filename);

} /* end function tapedev_query_device */


/*-------------------------------------------------------------------*/
/* Execute a Channel Command Word                                    */
/*-------------------------------------------------------------------*/
void tapedev_execute_ccw (DEVBLK *dev, BYTE code, BYTE flags,
        BYTE chained, U16 count, BYTE prevcode, int ccwseq,
        BYTE *iobuf, BYTE *more, BYTE *unitstat, U16 *residual)
{
int             rc;                     /* Return code               */
int             len;                    /* Length of data block      */
long            num;                    /* Number of bytes to read   */
OMATAPE_DESC   *omadesc;                /* -> OMA descriptor entry   */
struct mtop     opblk;                  /* Area for MTIOCTOP ioctl   */

    /* If this is a data-chained READ, then return any data remaining
       in the buffer which was not used by the previous CCW */
    if (chained & CCW_FLAGS_CD)
    {
        memmove (iobuf, iobuf + dev->curbufoff, dev->curblkrem);
        num = (count < dev->curblkrem) ? count : dev->curblkrem;
        *residual = count - num;
        if (count < dev->curblkrem) *more = 1;
        dev->curblkrem -= num;
        dev->curbufoff = num;
        *unitstat = CSW_CE | CSW_DE;
        return;
    }

    /* Command reject if data chaining and command is not READ */
    if ((flags & CCW_FLAGS_CD) && code != 0x02)
    {
        logmsg("HHC262I Data chaining not supported for CCW %2.2X\n",
                code);
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return;
    }

    /* Open the device file if necessary */
    if (dev->fd < 0 && !IS_CCW_SENSE(code) && code != 0x03)
    {
        /* Open the device file according to device type */
        switch (dev->tapedevt)
        {
        default:
        case TAPEDEVT_AWSTAPE:
            rc = open_awstape (dev, unitstat);
            break;

        case TAPEDEVT_SCSITAPE:
            rc = open_scsitape (dev, unitstat);
            break;

        case TAPEDEVT_OMATAPE:
            rc = open_omatape (dev, unitstat);
            break;
        } /* end switch(dev->tapedevt) */

        /* Exit with unit status if open was unsuccessful */
        if (rc < 0) return;
    }

    /* Process depending on CCW opcode */
    switch (code) {

    case 0x01:
    /*---------------------------------------------------------------*/
    /* WRITE                                                         */
    /*---------------------------------------------------------------*/
        /* Write a block from the tape according to device type */
        switch (dev->tapedevt)
        {
        default:
        case TAPEDEVT_AWSTAPE:
            rc = write_awstape (dev, iobuf, count, unitstat);
            break;

        case TAPEDEVT_SCSITAPE:
            rc = write_scsitape (dev, iobuf, count, unitstat);
            break;

        case TAPEDEVT_OMATAPE:
            dev->sense[0] = SENSE_CR;
            dev->sense[1] = SENSE1_FP;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            rc = -1;
            break;

        } /* end switch(dev->tapedevt) */

        /* Exit with unit check status if write error condition */
        if (rc < 0)
            break;

        /* Set normal status */
        *residual = 0;
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x02:
    /*---------------------------------------------------------------*/
    /* READ FORWARD                                                  */
    /*---------------------------------------------------------------*/
        /* Read a block from the tape according to device type */
        switch (dev->tapedevt)
        {
        default:
        case TAPEDEVT_AWSTAPE:
            len = read_awstape (dev, iobuf, unitstat);
            break;

        case TAPEDEVT_SCSITAPE:
            len = read_scsitape (dev, iobuf, unitstat);
            break;

        case TAPEDEVT_OMATAPE:
            omadesc = (OMATAPE_DESC*)(dev->omadesc);
            omadesc += dev->curfilen;

            switch (omadesc->format)
            {
            default:
            case 'H':
                len = read_omaheaders (dev, omadesc, iobuf, unitstat);
                break;
            case 'F':
                len = read_omafixed (dev, omadesc, iobuf, unitstat);
                break;
            case 'T':
                len = read_omatext (dev, omadesc, iobuf, unitstat);
                break;
            } /* end switch(omadesc->format) */

            break;

        } /* end switch(dev->tapedevt) */

        /* Exit with unit check status if read error condition */
        if (len < 0)
            break;

        /* Calculate number of bytes to read and residual byte count */
        num = (count < len) ? count : len;
        *residual = count - num;
        if (count < dev->curblkrem) *more = 1;

        /* Save size and offset of data not used by this CCW */
        dev->curblkrem = len - num;
        dev->curbufoff = num;

        /* Handle tape mark condition */
        if (len == 0)
        {
            /* Increment current file number */
            dev->curfilen++;

            /* For OMA tapes, close current tape image file.  The next
               file will be opened when the next CCW is processed */
            if (dev->tapedevt == TAPEDEVT_OMATAPE)
            {
                close (dev->fd);
                dev->fd = -1;
                dev->nxtblkpos = 0;
                dev->curblklen = 0;
                dev->curblkpos = -1;
                dev->nxtblkpos = 0;
                dev->prvblkpos = -1;
            } /* end if(OMATAPE) */

            /* Exit with unit exception status */
            break;
        }

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
        /* For SCSI tape, issue rewind command */
        if (dev->tapedevt == TAPEDEVT_SCSITAPE)
        {
            opblk.mt_op = MTREW;
            opblk.mt_count = 1;
            rc = ioctl (dev->fd, MTIOCTOP, (char*)&opblk);
            if (rc < 0)
            {
                logmsg ("HHC263I Error rewinding %s: %s\n",
                        dev->filename, strerror(errno));
                dev->sense[0] = SENSE_EC;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            } /* end if(rc) */
        } /* end if(SCSITAPE) */

        /* For OMA tape, close the current file */
        if (dev->tapedevt == TAPEDEVT_OMATAPE)
        {
            close (dev->fd);
            dev->fd = -1;
        } /* end if(OMATAPE) */

        /* For AWSTAPE file, seek to start of file */
        if (dev->tapedevt == TAPEDEVT_AWSTAPE)
        {
            rc = lseek (dev->fd, 0, SEEK_SET);
            if (rc < 0)
            {
                /* Handle seek error condition */
                logmsg ("HHC264I Error seeking to start of %s: %s\n",
                        dev->filename, strerror(errno));

                /* Set unit check with equipment check */
                dev->sense[0] = SENSE_EC;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
        } /* end if(AWSTAPE) */

        /* Reset position counters to start of file */
        dev->nxtblkpos = 0;
        dev->curfilen = 1;
        dev->curblklen = 0;
        dev->curblkpos = -1;
        dev->nxtblkpos = 0;
        dev->prvblkpos = -1;

        /* Set unit status */
        *residual = 0;
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x0F:
    /*---------------------------------------------------------------*/
    /* REWIND UNLOAD                                                 */
    /*---------------------------------------------------------------*/
        /* For SCSI tape, issue rewind unload command */
        if (dev->tapedevt == TAPEDEVT_SCSITAPE)
        {
            opblk.mt_op = MTOFFL;
            opblk.mt_count = 1;
            rc = ioctl (dev->fd, MTIOCTOP, (char*)&opblk);
            if (rc < 0)
            {
                logmsg ("HHC265I Error unloading %s: %s\n",
                        dev->filename, strerror(errno));
                dev->sense[0] = SENSE_EC;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            } /* end if(rc) */
        } /* end if(SCSITAPE) */

        /* Close the file and reset position counters */
        close (dev->fd);
        dev->fd = -1;
        dev->nxtblkpos = 0;
        dev->curfilen = 1;
        dev->curblklen = 0;
        dev->curblkpos = -1;
        dev->nxtblkpos = 0;
        dev->prvblkpos = -1;

        /* Set unit status */
        *residual = 0;
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x1F:
    /*---------------------------------------------------------------*/
    /* WRITE TAPE MARK                                               */
    /*---------------------------------------------------------------*/
        /* Write a tapemark according to device type */
        switch (dev->tapedevt)
        {
        default:
        case TAPEDEVT_AWSTAPE:
            rc = write_awsmark (dev, unitstat);
            break;

        case TAPEDEVT_SCSITAPE:
            rc = write_scsimark (dev, unitstat);
            break;

        case TAPEDEVT_OMATAPE:
            dev->sense[0] = SENSE_CR;
            dev->sense[1] = SENSE1_FP;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            rc = -1;
            break;

        } /* end switch(dev->tapedevt) */

        /* Exit with unit check status if write error condition */
        if (rc < 0)
            break;

        /* Increment current file number */
        dev->curfilen++;

        /* Set normal status */
        *residual = 0;
        *unitstat = CSW_CE | CSW_DE;
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
        /* Forward space to next file according to device type */
        switch (dev->tapedevt)
        {
        default:
        case TAPEDEVT_AWSTAPE:
            rc = fsf_awstape (dev, unitstat);
            break;

        case TAPEDEVT_SCSITAPE:
            rc = fsf_scsitape (dev, unitstat);
            break;

        case TAPEDEVT_OMATAPE:
            rc = fsf_omatape (dev, unitstat);
            break;

        } /* end switch(dev->tapedevt) */

        /* Exit with unit check status if error condition */
        if (rc < 0)
            break;

        /* Set normal status */
        *residual = 0;
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0xCB: /* 9-track 800 bpi */
    case 0xC3: /* 9-track 1600 bpi */
    case 0xD3: /* 9-track 6250 bpi */
    case 0xDB: /* 3480 mode set */
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

    case 0x34:
    /*---------------------------------------------------------------*/
    /* SENSE PATH GROUP ID                                           */
    /*---------------------------------------------------------------*/
        /* Command reject if path group feature is not available */
        if (dev->devtype != 0x3480)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Calculate residual byte count */
        num = (count < 12) ? count : 12;
        *residual = count - num;
        if (count < 12) *more = 1;

        /* Byte 0 is the path group state byte */
        iobuf[0] = SPG_PATHSTAT_RESET
                | SPG_PARTSTAT_IENABLED
                | SPG_PATHMODE_SINGLE;

        /* Bytes 1-11 contain the path group identifier */
        memcpy (iobuf+1, dev->pgid, 11);

        /* Return unit status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0xAF:
    /*---------------------------------------------------------------*/
    /* SET PATH GROUP ID                                             */
    /*---------------------------------------------------------------*/
        /* Command reject if path group feature is not available */
        if (dev->devtype != 0x3480)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Calculate residual byte count */
        num = (count < 12) ? count : 12;
        *residual = count - num;

        /* Control information length must be at least 12 bytes */
        if (count < 12)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Byte 0 is the path group state byte */
        if ((iobuf[0] & SPG_SET_COMMAND) == SPG_SET_ESTABLISH)
        {
            /* Bytes 1-11 contain the path group identifier */
            memcpy (dev->pgid, iobuf+1, 11);
        }

        /* Return unit status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x64:
    /*---------------------------------------------------------------*/
    /* READ DEVICE CHARACTERISTICS                                   */
    /*---------------------------------------------------------------*/
        /* Command reject if device characteristics not available */
        if (dev->numdevchar == 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Calculate residual byte count */
        num = (count < dev->numdevchar) ? count : dev->numdevchar;
        *residual = count - num;
        if (count < dev->numdevchar) *more = 1;

        /* Copy device characteristics bytes to channel buffer */
        memcpy (iobuf, dev->devchar, num);

        /* Return unit status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x9F:
    /*---------------------------------------------------------------*/
    /* LOAD DISPLAY                                                  */
    /*---------------------------------------------------------------*/
        /* Command reject if load display is not available */
        if (dev->devtype != 0x3480)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Calculate residual byte count */
        num = (count < 17) ? count : 17;
        *residual = count - num;

        /* Issue message on 3480 matrix display */
        issue_mount_msg (dev, iobuf);

        /* Return unit status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0xB7:
    case 0xC7:
    /*---------------------------------------------------------------*/
    /* ASSIGN/UNASSIGN                                               */
    /*---------------------------------------------------------------*/
        /* Command reject if path assignment is not supported */
        if (dev->devtype != 0x3480)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Calculate residual byte count */
        num = (count < 11) ? count : 11;
        *residual = count - num;

        /* Control information length must be at least 11 bytes */
        if (count < 11)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

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

