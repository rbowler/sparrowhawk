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

#include "hercules.h"

#define MAX_BLKLEN              65530   /* Maximum I/O buffer size   */

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
static int open_awstape (DEVBLK *dev)
{
int             rc;                     /* Return code               */

    /* Open the AWSTAPE file */
    rc = open (dev->filename, O_RDWR);

    /* Check for successful open */
    if (rc < 0)
    {
        printf ("HHC201I Error opening %s: %s\n",
                dev->filename, strerror(errno));
        return -1;
    }

    /* Store the file descriptor in the device block */
    dev->fd = rc;
    return 0;

} /* end function open_awstape */

/*-------------------------------------------------------------------*/
/* Open the OMATAPE file defined by an OMA descriptor table entry    */
/*                                                                   */
/* If successful, the file descriptor is stored in the device block  */
/* and the return value is zero.  Otherwise the return value is -1.  */
/*-------------------------------------------------------------------*/
static int open_omatape (DEVBLK *dev, OMATAPE_DESC *omadesc)
{
int             rc;                     /* Return code               */

    /* Open the OMATAPE file */
    rc = open (omadesc->filename, O_RDONLY);

    /* Check for successful open */
    if (rc < 0)
    {
        printf ("HHC202I Error opening %s: %s\n",
                omadesc->filename, strerror(errno));
        return -1;
    }

    /* Store the file descriptor in the device block */
    dev->fd = rc;
    return 0;

} /* end function open_omatape */

/*-------------------------------------------------------------------*/
/* Open a SCSI tape device                                           */
/*                                                                   */
/* If successful, the file descriptor is stored in the device block  */
/* and the return value is zero.  Otherwise the return value is -1.  */
/*-------------------------------------------------------------------*/
static int open_scsitape (DEVBLK *dev)
{
int             rc;                     /* Return code               */
struct mtop     opblk;                  /* Area for MTIOCTOP ioctl   */

    /* Open the SCSI tape device */
    rc = open (dev->filename, O_RDWR);

    /* Check for successful open */
    if (rc < 0)
    {
        printf ("HHC203I Error opening %s: %s\n",
                dev->filename, strerror(errno));
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
        printf ("tapeconv: Error setting attributes for %s: %s\n",
                dev->filename, strerror(errno));
        return -1;
    }

    /* Rewind the tape to the beginning */
    opblk.mt_op = MTREW;
    opblk.mt_count = 1;
    rc = ioctl (dev->fd, MTIOCTOP, (char*)&opblk);
    if (rc < 0)
    {
        printf ("tapeconv: Error rewinding %s: %s\n",
                dev->filename, strerror(errno));
        return -1;
    }

    return 0;

} /* end function open_scsitape */

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
            printf ("HHC204I Error reading block header "
                    "at offset %8.8lX in file %s: %s\n",
                    dev->curblkpos, dev->filename, strerror(errno));
        else
            printf ("HHC205I Unexpected end of file in block header "
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
            printf ("HHC206I Error reading data block "
                    "at offset %8.8lX in file %s: %s\n",
                    dev->curblkpos, dev->filename, strerror(errno));
        else
            printf ("HHC207I Unexpected end of file in data block "
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
        printf ("HHC208I Error reading data block from %s: %s\n",
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

    /* Initialize current block position */
    dev->curblkpos = dev->nxtblkpos;

    /* Read the 16-byte block header */
    rc = read (dev->fd, &omahdr, sizeof(omahdr));
    if (rc < sizeof(omahdr))
    {
        /* Handle read error condition */
        if (rc < 0)
            printf ("HHC209I Error reading block header "
                    "at offset %8.8lX in file %s: %s\n",
                    dev->curblkpos, omadesc->filename,
                    strerror(errno));
        else
            printf ("HHC210I Unexpected end of file in block header "
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
    if (curblkl < 1 || curblkl > MAX_BLKLEN
        || memcmp(omahdr.omaid, "@HDF", 4) != 0)
    {
        printf ("HHC211I Invalid block header "
                "at offset %8.8lX in file %s\n",
                dev->curblkpos, omadesc->filename);

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Calculate the offsets of the next and previous blocks */
    dev->nxtblkpos = dev->curblkpos + sizeof(omahdr) + curblkl;
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
            printf ("HHC212I Error reading data block "
                    "at offset %8.8lX in file %s: %s\n",
                    dev->curblkpos, omadesc->filename,
                    strerror(errno));
        else
            printf ("HHC213I Unexpected end of file in data block "
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
            printf ("HHC214I Error reading data block "
                    "at offset %8.8lX in file %s: %s\n",
                    dev->curblkpos, omadesc->filename,
                    strerror(errno));
        else
            printf ("HHC215I Unexpected end of file in data block "
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
            printf ("HHC216I Error reading data block "
                    "at offset %8.8lX in file %s: %s\n",
                    dev->curblkpos, omadesc->filename,
                    strerror(errno));
        else
            printf ("HHC217I Unexpected end of file in data block "
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
        printf ("HHC218I Invalid zero length block "
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
        printf ("HHC219I Invalid filename %s: "
                "TDF files must be in the TAPES subdirectory\n",
                dev->filename+pathlen);
        return -1;
    }
    pathlen -= 7;

    /* Open the tape descriptor file */
    fd = open (dev->filename, O_RDONLY);
    if (fd < 0)
    {
        printf ("HHC220I Error opening TDF file %s: %s\n",
                dev->filename, strerror(errno));
        return -1;
    }

    /* Determine the size of the tape descriptor file */
    rc = fstat (fd, &statbuf);
    if (rc < 0)
    {
        printf ("HHC221I %s fstat error: %s\n",
                dev->filename, strerror(errno));
        close (fd);
        return -1;
    }
    tdfsize = statbuf.st_size;

    /* Obtain a buffer for the tape descriptor file */
    tdfbuf = malloc (tdfsize);
    if (tdfbuf == NULL)
    {
        printf ("HHC222I Cannot obtain buffer for TDF file %s: %s\n",
                dev->filename, strerror(errno));
        close (fd);
        return -1;
    }

    /* Read the tape descriptor file into the buffer */
    rc = read (fd, tdfbuf, tdfsize);
    if (rc < tdfsize)
    {
        printf ("HHC223I Error reading TDF file %s: %s\n",
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
        printf ("HHC224I %s is not a valid TDF file\n",
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
        printf ("HHC225I Cannot obtain buffer for TDF array: %s\n",
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
            printf ("HHC226I Filename or format missing in "
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
            printf ("HHC227I Filename %s too long in "
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
                printf ("HHC228I RECSIZE keyword missing in "
                        "line %d of file %s\n",
                        stmt, dev->filename);
                free (tdftab);
                free (tdfbuf);
                return -1;
            }

            /* Check for valid fixed block length */
            if (tdfblklen == NULL
                || sscanf(tdfblklen, "%lu%c", &blklen, &c) != 1
                || blklen < 1 || blklen > MAX_BLKLEN)
            {
                printf ("HHC229I Invalid record size %s in "
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
            printf ("HHC230I Invalid record format %s in "
                    "line %d of file %s\n",
                    tdfformat, stmt, dev->filename);
            free (tdftab);
            free (tdfbuf);
            return -1;
        }
    } /* end for(filecount) */

    /* Save the TDF descriptor array pointer in the device block */
    dev->omadesc = tdftab;

    /* Release the TDF file buffer and exit */
    free (tdfbuf);
    return 0;

} /* end function read_omadesc */

/*-------------------------------------------------------------------*/
/* Initialize the device handler                                     */
/*-------------------------------------------------------------------*/
int tapedev_init_handler (DEVBLK *dev, int argc, BYTE *argv[])
{
int             rc;                     /* Return code               */
int             len;                    /* Length of file name       */

    /* The first argument is the file name */
    if (argc == 0 || strlen(argv[0]) > sizeof(dev->filename)-1)
    {
        printf ("HHC231I File name missing or invalid\n");
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
    dev->curfilen = 1;
    dev->curblkpos = -1;
    dev->nxtblkpos = 0;
    dev->prvblkpos = -1;
    dev->curblkrem = 0;
    dev->curbufoff = 0;

    /* Set number of sense bytes */
    dev->numsense = 24;

    /* Read the OMA tape descriptor file */
    if (dev->tapedevt == TAPEDEVT_OMATAPE)
    {
        rc = read_omadesc (dev);
        if (rc < 0) return -1;
    }

    return 0;
} /* end function tapedev_init_handler */


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

    /* If this is a data-chained READ, then return any data remaining
       in the buffer which was not used by the previous CCW */
    if (chained & CCW_FLAGS_CD)
    {
        memmove (iobuf, iobuf + dev->curblkpos, dev->curblkrem);
        num = (count < dev->curblkrem) ? count : dev->curblkrem;
        *residual = count - num;
        if (count < dev->curblkrem) *more = 1;
        dev->curblkrem -= num;
        dev->curblkpos = num;
        *unitstat = CSW_CE | CSW_DE;
        return;
    }

    /* Command reject if data chaining and command is not READ */
    if ((flags & CCW_FLAGS_CD) && code != 0x02)
    {
        printf("HHC232I Data chaining not supported for CCW %2.2X\n",
                code);
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return;
    }

    /* Open the device file if necessary */
    if (dev->fd < 0)
    {
        /* Open the device file according to device type */
        switch (dev->tapedevt)
        {
        default:
        case TAPEDEVT_AWSTAPE:
            rc = open_awstape (dev);
            break;

        case TAPEDEVT_SCSITAPE:
            rc = open_scsitape (dev);
            break;

        case TAPEDEVT_OMATAPE:
            omadesc = (OMATAPE_DESC*)(dev->omadesc);
            omadesc += dev->curfilen;
            rc = open_omatape (dev, omadesc);
            break;
        } /* end switch(dev->tapedevt) */

        /* Unit check if open was unsuccessful */
        if (rc < 0)
        {
            dev->sense[0] = SENSE_IR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return;
        }
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
        dev->curblkpos = num;

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
            }

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

