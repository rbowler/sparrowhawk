/* CKDDASD.C    (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 CKD Direct Access Storage Device Handler     */

/*-------------------------------------------------------------------*/
/* This module contains device handling functions for emulated       */
/* count-key-data direct access storage devices.                     */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Structure definitions for CKD headers                             */
/*-------------------------------------------------------------------*/
typedef struct _CKDDASD_DEVHDR {        /* Device header             */
        BYTE    devid[8];               /* Device identifier         */
        U32     heads;                  /* #of heads per cylinder    */
        U32     trksize;                /* Track size                */
        U32     flags;                  /* Flags                     */
        BYTE    resv[492];              /* Reserved                  */
    } CKDDASD_DEVHDR;

typedef struct _CKDDASD_TRKHDR {        /* Track header              */
        BYTE    bin;                    /* Bin number                */
        HWORD   cyl;                    /* Cylinder number           */
        HWORD   head;                   /* Head number               */
    } CKDDASD_TRKHDR;

typedef struct _CKDDASD_RECHDR {        /* Record header             */
        HWORD   cyl;                    /* Cylinder number           */
        HWORD   head;                   /* Head number               */
        BYTE    rec;                    /* Record number             */
        BYTE    klen;                   /* Key length                */
        HWORD   dlen;                   /* Data length               */
    } CKDDASD_RECHDR;

#define CKDDASD_DEVHDR_SIZE     sizeof(CKDDASD_DEVHDR)
#define CKDDASD_TRKHDR_SIZE     sizeof(CKDDASD_TRKHDR)
#define CKDDASD_RECHDR_SIZE     sizeof(CKDDASD_RECHDR)


/*-------------------------------------------------------------------*/
/* Bit definitions for File Mask                                     */
/*-------------------------------------------------------------------*/
#define CKDMASK_WRCTL           0xC0    /* Write control bits...     */
#define CKDMASK_WRCTL_INHWR0    0x00    /* ...inhibit write HA/R0    */
#define CKDMASK_WRCTL_INHWRT    0x40    /* ...inhibit all writes     */
#define CKDMASK_WRCTL_ALLWRU    0x80    /* ...write update only      */
#define CKDMASK_WRCTL_ALLWRT    0xC0    /* ...allow all writes       */
#define CKDMASK_RESV            0x20    /* Reserved bits - must be 0 */
#define CKDMASK_SKCTL           0x18    /* Seek control bits...      */
#define CKDMASK_SKCTL_ALLSKR    0x00    /* ...allow all seek/recalib */
#define CKDMASK_SKCTL_CYLHD     0x08    /* ...allow seek cyl/head    */
#define CKDMASK_SKCTL_HEAD      0x10    /* ...allow seek head only   */
#define CKDMASK_SKCTL_INHSMT    0x18    /* ...inhibit seek and MT    */
#define CKDMASK_AAUTH           0x06    /* Access auth bits...       */
#define CKDMASK_AAUTH_NORMAL    0x00    /* ...normal authorization   */
#define CKDMASK_AAUTH_DSF       0x02    /* ...device support auth    */
#define CKDMASK_AAUTH_OLT       0x04    /* ...diagnostic auth        */
#define CKDMASK_AAUTH_DSFNCR    0x06    /* ...device support with no
                                              correction or retry    */
#define CKDMASK_PCI_FETCH       0x01    /* PCI fetch mode            */

/*-------------------------------------------------------------------*/
/* Bit definitions for Locate operation byte                         */
/*-------------------------------------------------------------------*/
#define CKDOPER_ORIENTATION     0xC0    /* Orientation bits...       */
#define CKDOPER_ORIENT_COUNT    0x00    /* ...orient to count        */
#define CKDOPER_ORIENT_HOME     0x40    /* ...orient to home address */
#define CKDOPER_ORIENT_DATA     0x80    /* ...orient to data area    */
#define CKDOPER_ORIENT_INDEX    0xC0    /* ...orient to index        */
#define CKDOPER_CODE            0x3F    /* Operation code bits...    */
#define CKDOPER_ORIENT          0x00    /* ...orient                 */
#define CKDOPER_WRITE           0x01    /* ...write data             */
#define CKDOPER_FORMAT          0x03    /* ...format write           */
#define CKDOPER_RDDATA          0x06    /* ...read data              */
#define CKDOPER_RDANY           0x0A    /* ...read any               */
#define CKDOPER_WRTTRK          0x0B    /* ...write track            */
#define CKDOPER_RDTRKS          0x0C    /* ...read tracks            */
#define CKDOPER_READ            0x16    /* ...read                   */

/*-------------------------------------------------------------------*/
/* Bit definitions for Locate auxiliary byte                         */
/*-------------------------------------------------------------------*/
#define CKDLAUX_TLFVALID        0x80    /* TLF field is valid        */
#define CKDLAUX_RESV            0x7E    /* Reserved bits - must be 0 */
#define CKDLAUX_RDCNTSUF        0x01    /* Suffixed read count CCW   */

/*-------------------------------------------------------------------*/
/* Definitions for ckdorient field in device block                   */
/*-------------------------------------------------------------------*/
#define CKDORIENT_NONE          0       /* Orientation unknown       */
#define CKDORIENT_INDEX         1       /* Oriented after track hdr  */
#define CKDORIENT_COUNT         2       /* Oriented after count field*/
#define CKDORIENT_KEY           3       /* Oriented after key field  */
#define CKDORIENT_DATA          4       /* Oriented after data field */


/*-------------------------------------------------------------------*/
/* Static data areas                                                 */
/*-------------------------------------------------------------------*/
BYTE eighthexFF[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};


/*-------------------------------------------------------------------*/
/* Initialize the device handler                                     */
/*-------------------------------------------------------------------*/
int ckddasd_init_handler ( DEVBLK *dev, int argc, BYTE *argv[] )
{
int             rc;                     /* Return code               */
struct stat     statbuf;                /* File information          */
CKDDASD_DEVHDR  devhdr;                 /* Device header             */
int             har0len;                /* Length of HA + R0         */

    /* The first argument is the file name */
    if (argc == 0 || strlen(argv[0]) > sizeof(dev->filename)-1)
    {
        printf ("HHC351I File name missing or invalid\n");
        return -1;
    }

    /* Save the file name in the device block */
    strcpy (dev->filename, argv[0]);

    /* Open the device file */
    dev->fd = open (dev->filename, O_RDONLY);
    if (dev->fd < 0)
    {
        printf ("HHC352I %s open error: %s\n",
                dev->filename, strerror(errno));
        return -1;
    }

    /* Determine the device size */
    rc = fstat (dev->fd, &statbuf);
    if (rc < 0)
    {
        printf ("HHC353I %s fstat error: %s\n",
                dev->filename, strerror(errno));
        return -1;
    }

    /* Read the device header */
    rc = read (dev->fd, &devhdr, CKDDASD_DEVHDR_SIZE);
    if (rc < CKDDASD_DEVHDR_SIZE)
    {
        if (rc < 0)
            printf ("HHC354I %s read error: %s\n",
                    dev->filename, strerror(errno));
        else
            printf ("HHC355I %s CKD header incomplete\n",
                    dev->filename);
        return -1;
    }

    /* Check the device header identifier */
    if (memcmp(devhdr.devid, "CKD_P370", 8) != 0)
    {
        printf ("HHC356I %s CKD header invalid\n",
                dev->filename);
    }

    /* Set device dependent fields */
    dev->ckdheads = devhdr.heads;
    dev->ckdtrksz = devhdr.trksize;
    dev->ckdtrks = (statbuf.st_size - CKDDASD_DEVHDR_SIZE)
                        / dev->ckdtrksz;
    dev->ckdcyls = dev->ckdtrks / dev->ckdheads;

    printf ("ckddasd: %s cyls=%d heads=%d tracks=%d trklen=%d\n",
            dev->filename, dev->ckdcyls, dev->ckdheads,
            dev->ckdtrks, dev->ckdtrksz);

    /* Consistency check device header */
    if (dev->ckdcyls * dev->ckdheads != dev->ckdtrks
        || (dev->ckdtrks * dev->ckdtrksz) + CKDDASD_DEVHDR_SIZE
                        != statbuf.st_size)
    {
        printf ("HHC357I %s CKD header inconsistent with file size\n",
                dev->filename);
    }

    /* Set number of sense bytes */
    dev->numsense = 24;

    /* Initialize the device identifier bytes */
    dev->devid[0] = 0xFF;
    dev->devid[1] = 0x39; /* Control unit type is 3990-C2 */
    dev->devid[2] = 0x90;
    dev->devid[3] = 0xC2;
    dev->devid[4] = dev->devtype >> 8;
    dev->devid[5] = dev->devtype & 0xFF;
    dev->devid[6] = 0x02;
    dev->numdevid = 7;

    /* Initialize the device characteristics bytes */
    memset (dev->devchar, 0, sizeof(dev->devchar));
    memcpy (dev->devchar, dev->devid+1, 6);
    memcpy (dev->devchar+6, "\xD0\x00\x00\x02", 4);
    dev->devchar[10] = 0x20;            /* Device class */
    dev->devchar[11] = 0x26;            /* Unit type */
    dev->devchar[12] = dev->ckdcyls >> 8;
    dev->devchar[13] = dev->ckdcyls & 0xFF;
    dev->devchar[14] = dev->ckdheads >> 8;
    dev->devchar[15] = dev->ckdheads & 0xFF;
    dev->devchar[16] = 240;             /* Number of sectors */
    dev->devchar[17] = (dev->ckdtrksz >> 16) & 0xFF;
    dev->devchar[18] = (dev->ckdtrksz >> 8) & 0xFF;
    dev->devchar[19] = dev->ckdtrksz & 0xFF;
    har0len = CKDDASD_TRKHDR_SIZE + CKDDASD_RECHDR_SIZE + 8;
    dev->devchar[20] = (har0len >> 8) & 0xFF;
    dev->devchar[21] = har0len & 0xFF;
    dev->numdevchar = 64;

    /* Activate I/O tracing */
    dev->ccwtrace = 1;

    return 0;
} /* end function ckddasd_init_handler */


/*-------------------------------------------------------------------*/
/* Skip past count, key, or data fields                              */
/*-------------------------------------------------------------------*/
static int ckd_skip ( DEVBLK *dev, int skiplen, BYTE *unitstat)
{
int             rc;                     /* Return code               */

    printf("ckddasd: skipping %d bytes\n", skiplen);

    rc = lseek (dev->fd, skiplen, SEEK_CUR);
    if (rc < 0)
    {
        /* Handle seek error condition */
        perror("ckddasd: lseek error");

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    return 0;
} /* end function ckd_skip */


/*-------------------------------------------------------------------*/
/* Seek to a specified cylinder and head                             */
/*-------------------------------------------------------------------*/
static int ckd_seek ( DEVBLK *dev, U16 cyl, U16 head,
                CKDDASD_TRKHDR *trkhdr, BYTE *unitstat )
{
int             rc;                     /* Return code               */
off_t           seekpos;                /* Seek position for lseek   */

    printf("ckddasd: seeking to cyl %d head %d\n", cyl, head);

    if (cyl >= dev->ckdcyls || head >= dev->ckdheads)
    {
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Seek to start of track header */
    seekpos = CKDDASD_DEVHDR_SIZE
            + (((cyl * dev->ckdheads) + head) * dev->ckdtrksz);

    rc = lseek (dev->fd, seekpos, SEEK_SET);
    if (rc < 0)
    {
        /* Handle seek error condition */
        perror("ckddasd: lseek error");

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Read the track header */
    rc = read (dev->fd, trkhdr, CKDDASD_TRKHDR_SIZE);
    if (rc < CKDDASD_TRKHDR_SIZE)
    {
        /* Handle read error condition */
        perror("ckddasd: read error");

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Validate the track header */
    if (trkhdr->bin != 0
        || trkhdr->cyl[0] != (cyl >> 8)
        || trkhdr->cyl[1] != (cyl & 0xFF)
        || trkhdr->head[0] != (head >> 8)
        || trkhdr->head[1] != (head & 0xFF))
    {
        printf("ckddasd: invalid track header\n");

        /* Unit check with equipment check and invalid track format */
        dev->sense[0] = SENSE_EC;
        dev->sense[1] = SENSE1_ITF;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Set device orientation fields */
    dev->ckdcurcyl = cyl;
    dev->ckdcurhead = head;
    dev->ckdcurrec = 0;
    dev->ckdcurkl = 0;
    dev->ckdcurdl = 0;
    dev->ckdrem = 0;
    dev->ckdorient = CKDORIENT_INDEX;

    return 0;
} /* end function ckd_seek */


/*-------------------------------------------------------------------*/
/* Advance to next track for multitrack operation                    */
/*-------------------------------------------------------------------*/
static int mt_advance ( DEVBLK *dev, BYTE *unitstat )
{
int             rc;                     /* Return code               */
U16             cyl;                    /* Next cyl for multitrack   */
U16             head;                   /* Next head for multitrack  */
CKDDASD_TRKHDR  trkhdr;                 /* CKD track header          */

    /* File protect error if not within domain of Locate Record
       and file mask inhibits seek and multitrack operations */
    if (dev->ckdlocat == 0 &&
        (dev->ckdfmask & CKDMASK_SKCTL) == CKDMASK_SKCTL_INHSMT)
    {
        dev->sense[1] = SENSE1_FP;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* End of cylinder error if not within domain of Locate Record
       and current track is last track of cylinder */
    if (dev->ckdlocat == 0
        && dev->ckdcurhead >= dev->ckdheads - 1)
    {
        dev->sense[1] = SENSE1_EOC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Advance to next track */
    cyl = dev->ckdcurcyl;
    head = dev->ckdcurhead + 1;
    if (head >= dev->ckdheads)
    {
        head = 0;
        cyl++;
    }

    /* File protect error if next track is outside the
       limits of the device or outside the defined extent */
    if (cyl >= dev->ckdcyls
        || (dev->ckdxtdef
            && (cyl < dev->ckdxbcyl || cyl > dev->ckdxecyl
                || (cyl == dev->ckdxbcyl && head < dev->ckdxbhead)
                || (cyl == dev->ckdxecyl && head > dev->ckdxehead)
            )))
    {
        dev->sense[1] = SENSE1_FP;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Seek to next track */
    rc = ckd_seek (dev, cyl, head, &trkhdr, unitstat);
    if (rc < 0) return -1;

    /* Successful return */
    return 0;

} /* end function mt_advance */

/*-------------------------------------------------------------------*/
/* Read count field                                                  */
/*-------------------------------------------------------------------*/
static int ckd_read_count ( DEVBLK *dev, BYTE code,
                CKDDASD_RECHDR *rechdr, BYTE *unitstat)
{
int             rc;                     /* Return code               */
int             skiplen;                /* Number of bytes to skip   */
int             skipr0 = 0;             /* 1=Skip record zero        */
U16             cyl;                    /* Cylinder number for seek  */
U16             head;                   /* Head number for seek      */
CKDDASD_TRKHDR  trkhdr;                 /* CKD track header          */

    /* Skip record 0 for all operations except READ TRACK, READ R0,
       SEARCH ID EQUAL, SEARCH ID HIGH, and SEARCH ID EQUAL OR HIGH */
    if (code != 0xDE
        && (code & 0x7F) != 0x16
        && (code & 0x7F) != 0x31
        && (code & 0x7F) != 0x51
        && (code & 0x7F) != 0x71)
        skipr0 = 1;

    /* Search for next count field */
    for ( ; ; )
    {
        /* If oriented to count or key field, skip key and data */
        if (dev->ckdorient == CKDORIENT_COUNT)
            skiplen = dev->ckdcurkl + dev->ckdcurdl;
        else if (dev->ckdorient == CKDORIENT_KEY)
            skiplen = dev->ckdcurdl;
        else
            skiplen = 0;

        if (skiplen > 0)
        {
            rc = ckd_skip (dev, skiplen, unitstat);
            if (rc < 0) return rc;
        }

        /* Read record header (count field) */
        rc = read (dev->fd, rechdr, CKDDASD_RECHDR_SIZE);
        if (rc < CKDDASD_RECHDR_SIZE)
        {
            /* Handle read error condition */
            perror("ckddasd: read error");

            /* Set unit check with equipment check */
            dev->sense[0] = SENSE_EC;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return -1;
        }

        /* Set the device orientation fields */
        dev->ckdcurrec = rechdr->rec;
        dev->ckdrem = 0;
        dev->ckdorient = CKDORIENT_COUNT;
        dev->ckdcurkl = rechdr->klen;
        dev->ckdcurdl = (rechdr->dlen[0] << 8) + rechdr->dlen[1];

        printf("ckddasd: record %d kl %d dl %d\n",
                dev->ckdcurrec, dev->ckdcurkl, dev->ckdcurdl);

        /* Skip record zero if user data record required */
        if (skipr0 && rechdr->rec == 0)
            continue;

        /* Test for logical end of track and exit if not */
        if (memcmp(rechdr, eighthexFF, 8) != 0)
            break;

        /* No record found error if this is the second end of track
           in this channel program without an intervening read of
           the home address or data area, or without an intervening
           write, sense, or control command */
        if (dev->ckdxmark)
        {
            dev->sense[1] = SENSE1_NRF;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return -1;
        }

        /* Set index marker found flag */
        dev->ckdxmark = 1;

        /* Test for multitrack operation */
        if ((code & 0x80) == 0)
        {
            /* If non-multitrack, return to start of current track */
            cyl = dev->ckdcurcyl;
            head = dev->ckdcurhead;
            rc = ckd_seek (dev, cyl, head, &trkhdr, unitstat);
            if (rc < 0) return -1;
        }
        else
        {
            /* If multitrack, attempt to advance to next track */
            rc = mt_advance (dev, unitstat);
            if (rc < 0) return -1;
        }

    } /* end for */

    return 0;

} /* end function ckd_read_count */


/*-------------------------------------------------------------------*/
/* Read key field                                                    */
/*-------------------------------------------------------------------*/
static int ckd_read_key ( DEVBLK *dev, BYTE code,
                BYTE *buf, BYTE *unitstat)
{
int             rc;                     /* Return code               */
CKDDASD_RECHDR  rechdr;                 /* CKD record header         */

    /* If not oriented to count field, read next count field */
    if (dev->ckdorient != CKDORIENT_COUNT)
    {
        rc = ckd_read_count (dev, code, &rechdr, unitstat);
        if (rc < 0) return rc;
    }

    printf("ckddasd: reading %d bytes\n", dev->ckdcurkl);

    /* Read key field */
    if (dev->ckdcurkl > 0)
    {
        rc = read (dev->fd, buf, dev->ckdcurkl);
        if (rc < dev->ckdcurkl)
        {
            /* Handle read error condition */
            perror("ckddasd: read error");

            /* Set unit check with equipment check */
            dev->sense[0] = SENSE_EC;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return -1;
        }
    }

    /* Set the device orientation fields */
    dev->ckdrem = 0;
    dev->ckdorient = CKDORIENT_KEY;

    return 0;
} /* end function ckd_read_key */


/*-------------------------------------------------------------------*/
/* Read data field                                                   */
/*-------------------------------------------------------------------*/
static int ckd_read_data ( DEVBLK *dev, BYTE code,
                BYTE *buf, BYTE *unitstat)
{
int             rc;                     /* Return code               */
CKDDASD_RECHDR  rechdr;                 /* CKD record header         */
int             skiplen;                /* Number of bytes to skip   */

    /* If not oriented to count or key field, read next count field */
    if (dev->ckdorient != CKDORIENT_COUNT
        && dev->ckdorient != CKDORIENT_KEY)
    {
        rc = ckd_read_count (dev, code, &rechdr, unitstat);
        if (rc < 0) return rc;
    }

    /* If oriented to count field, skip the key field */
    if (dev->ckdorient == CKDORIENT_COUNT)
        skiplen = dev->ckdcurkl;
    else
        skiplen = 0;

    if (skiplen > 0)
    {
        rc = ckd_skip (dev, skiplen, unitstat);
        if (rc < 0) return rc;
    }

    printf("ckddasd: reading %d bytes\n", dev->ckdcurdl);

    /* Read data field */
    if (dev->ckdcurdl > 0)
    {
        rc = read (dev->fd, buf, dev->ckdcurdl);
        if (rc < dev->ckdcurdl)
        {
            /* Handle read error condition */
            perror("ckddasd: read error");

            /* Set unit check with equipment check */
            dev->sense[0] = SENSE_EC;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return -1;
        }
    }

    /* Set the device orientation fields */
    dev->ckdrem = 0;
    dev->ckdorient = CKDORIENT_DATA;

    return 0;
} /* end function ckd_read_data */


/*-------------------------------------------------------------------*/
/* Write count key and data fields                                   */
/*-------------------------------------------------------------------*/
static int ckd_write_ckd ( DEVBLK *dev, BYTE *buf, U16 len,
                BYTE *unitstat)
{
int             rc;                     /* Return code               */
CKDDASD_RECHDR  rechdr;                 /* CKD record header         */
BYTE            recnum;                 /* Record number             */
BYTE            keylen;                 /* Key length                */
U16             datalen;                /* Data length               */
U16             ckdlen;                 /* Count+key+data length     */
off_t           curpos;                 /* Current position in file  */
off_t           nxtpos;                 /* Position of next track    */

    /* Copy the count field from the buffer */
    memset (&rechdr, 0, CKDDASD_RECHDR_SIZE);
    memcpy (&rechdr, buf, (len < CKDDASD_RECHDR_SIZE) ?
                                len : CKDDASD_RECHDR_SIZE);

    /* Extract the record number, key length and data length */
    recnum = rechdr.rec;
    keylen = rechdr.klen;
    datalen = (rechdr.dlen[0] << 8) + rechdr.dlen[1];

    /* Calculate total count key and data size */
    ckdlen = CKDDASD_RECHDR_SIZE + keylen + datalen;

    /* Determine the current position in the file */
    curpos = lseek (dev->fd, 0, SEEK_CUR);
    if (curpos < 0)
    {
        /* Handle seek error condition */
        perror("ckddasd: lseek error");

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Calculate the position of the next track in the file */
    nxtpos = CKDDASD_DEVHDR_SIZE
            + (((dev->ckdcurcyl * dev->ckdheads) + dev->ckdcurhead)
                * dev->ckdtrksz) + dev->ckdtrksz;

    /* Check that there is enough space on the current track to
       contain the complete record plus an end of track marker */
    if (curpos + ckdlen + 8 >= nxtpos)
    {
        /* Unit check with equipment check and invalid track format */
        dev->sense[0] = SENSE_EC;
        dev->sense[1] = SENSE1_ITF;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Pad the I/O buffer with zeroes if necessary */
    while (len < ckdlen) buf[len++] = '\0';

    printf("ckddasd: writing record %d kl %d dl %d\n",
            recnum, keylen, datalen);

    /* Write count key and data */
    rc = write (dev->fd, buf, ckdlen);
    if (rc < ckdlen)
    {
        /* Handle write error condition */
        perror("ckddasd: write error");

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Logically erase rest of track by writing end of track marker */
    rc = write (dev->fd, eighthexFF, 8);
    if (rc < CKDDASD_RECHDR_SIZE)
    {
        /* Handle write error condition */
        perror("ckddasd: write error");

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Backspace over end of track marker */
    rc = lseek (dev->fd, -(CKDDASD_RECHDR_SIZE), SEEK_CUR);
    if (rc < 0)
    {
        /* Handle seek error condition */
        perror("ckddasd: lseek error");

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Set the device orientation fields */
    dev->ckdcurrec = recnum;
    dev->ckdcurkl = keylen;
    dev->ckdcurdl = datalen;
    dev->ckdrem = 0;
    dev->ckdorient = CKDORIENT_DATA;

    return 0;
} /* end function ckd_write_ckd */


/*-------------------------------------------------------------------*/
/* Execute a Channel Command Word                                    */
/*-------------------------------------------------------------------*/
void ckddasd_execute_ccw ( DEVBLK *dev, BYTE code, BYTE flags,
        BYTE chained, U16 count, BYTE prevcode, BYTE *iobuf,
        BYTE *more, BYTE *unitstat, U16 *residual )
{
int             rc;                     /* Return code               */
CKDDASD_TRKHDR  trkhdr;                 /* CKD track header (HA)     */
CKDDASD_RECHDR  rechdr;                 /* CKD record header (count) */
int             size;                   /* Number of bytes available */
int             num;                    /* Number of bytes to move   */
U16             bin;                    /* Bin number                */
U16             cyl;                    /* Cylinder number           */
U16             head;                   /* Head number               */
BYTE            fmask;                  /* File mask                 */
BYTE            key[256];               /* Key for search operations */

    /* Command reject if data chaining */
    if (flags & CCW_FLAGS_CD)
    {
        printf ("ckddasd: CKD data chaining not supported\n");
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return;
    }

    /* Reset flags at start of CCW chain */
    if (chained == 0)
    {
        dev->ckdxtdef = 0;
        dev->ckdsetfm = 0;
        dev->ckdlocat = 0;
        dev->ckdseek = 0;
        dev->ckdskcyl = 0;
        dev->ckdrecal = 0;
        dev->ckdrdipl = 0;
        dev->ckdfmask = 0;
        dev->ckdxmark = 0;
        dev->ckdhaeq = 0;
        dev->ckdideq = 0;
        dev->ckdkyeq = 0;
        dev->ckdwckd = 0;
    }

    /* Reset index marker flag if write, sense, or control command,
       or any read command except read count or read sector */
    if (IS_CCW_WRITE(code) || IS_CCW_SENSE(code)
        || IS_CCW_CONTROL(code)
        || (IS_CCW_READ(code)
            && (code & 0x7F) != 0x12
            && (code & 0x7F) != 0x22))
        dev->ckdxmark = 0;

    /* Process depending on CCW opcode */
    switch (code) {

    case 0x02:
    /*---------------------------------------------------------------*/
    /* READ IPL                                                      */
    /*---------------------------------------------------------------*/
        /* Command reject if preceded by a Define Extent or
           Set File Mask, or within the domain of a Locate Record */
        if (dev->ckdxtdef || dev->ckdsetfm || dev->ckdlocat)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Set define extent parameters */
        dev->ckdxtdef = 1;
        dev->ckdsetfm = 1;
        dev->ckdfmask = 0;
        dev->ckdxgattr = 0;
        dev->ckdxblksz = 0;
        dev->ckdxbcyl = 0;
        dev->ckdxbhead = 0;
        dev->ckdxecyl = dev->ckdcyls - 1;
        dev->ckdxehead = dev->ckdheads - 1;

        /* Set locate record parameters */
        dev->ckdloper = CKDOPER_ORIENT_DATA | CKDOPER_RDDATA;
        dev->ckdlaux = 0;
        dev->ckdlcount = 2;
        dev->ckdlskcyl = 0;
        dev->ckdlskhead = 0;
        dev->ckdlsrcyl = 0;
        dev->ckdlsrhead = 0;
        dev->ckdlsrrec = 0;
        dev->ckdlsector = 0;
        dev->ckdltranlf = 0;

        /* Seek to start of cylinder zero track zero */
        rc = ckd_seek (dev, 0, 0, &trkhdr, unitstat);
        if (rc < 0) break;

        /* Read count field for first record following R0 */
        rc = ckd_read_count (dev, code, &rechdr, unitstat);
        if (rc < 0) break;

        /* Calculate number of bytes to read and set residual count */
        num = (count < dev->ckdcurdl) ? count : dev->ckdcurdl;
        *residual = count - num;
        if (count < dev->ckdcurdl) *more = 1;

        /* Read data field */
        rc = ckd_read_data (dev, code, iobuf, unitstat);
        if (rc < 0) break;

        /* Set command processed flag */
        dev->ckdrdipl = 1;

        /* Return unit exception if data length is zero */
        if (dev->ckdcurdl == 0)
            *unitstat = CSW_CE | CSW_DE | CSW_UX;
        else
            *unitstat = CSW_CE | CSW_DE;

        break;

    case 0x03:
    /*---------------------------------------------------------------*/
    /* CONTROL NO-OPERATION                                          */
    /*---------------------------------------------------------------*/
        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x06:
    case 0x86:
    /*---------------------------------------------------------------*/
    /* READ DATA                                                     */
    /*---------------------------------------------------------------*/
        /* Command reject if not preceded by a Seek, Seek Cylinder,
           Locate Record, Read IPL, or Recalibrate command */
        if (dev->ckdseek == 0 && dev->ckdskcyl == 0
            && dev->ckdlocat == 0 && dev->ckdrdipl == 0
            && dev->ckdrecal == 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Check operation code if within domain of a Locate Record */
        if (dev->ckdlocat)
        {
            if (!((dev->ckdloper & CKDOPER_CODE) == CKDOPER_RDDATA
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_RDANY
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_READ))
            {
                dev->sense[0] = SENSE_CR;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
        }

        /* If not oriented to count or key field, read next count */
        if (dev->ckdorient != CKDORIENT_COUNT
            && dev->ckdorient != CKDORIENT_KEY)
        {
            rc = ckd_read_count (dev, code, &rechdr, unitstat);
            if (rc < 0) break;
        }

        /* Calculate number of bytes to read and set residual count */
        num = (count < dev->ckdcurdl) ? count : dev->ckdcurdl;
        *residual = count - num;
        if (count < dev->ckdcurdl) *more = 1;

        /* Read data field */
        rc = ckd_read_data (dev, code, iobuf, unitstat);
        if (rc < 0) break;

        /* Return unit exception if data length is zero */
        if (dev->ckdcurdl == 0)
            *unitstat = CSW_CE | CSW_DE | CSW_UX;
        else
            *unitstat = CSW_CE | CSW_DE;

        break;

    case 0x0E:
    case 0x8E:
    /*---------------------------------------------------------------*/
    /* READ KEY AND DATA                                             */
    /*---------------------------------------------------------------*/
        /* Command reject if not preceded by a Seek, Seek Cylinder,
           Locate Record, Read IPL, or Recalibrate command */
        if (dev->ckdseek == 0 && dev->ckdskcyl == 0
            && dev->ckdlocat == 0 && dev->ckdrdipl == 0
            && dev->ckdrecal == 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Check operation code if within domain of a Locate Record */
        if (dev->ckdlocat)
        {
            if (!((dev->ckdloper & CKDOPER_CODE) == CKDOPER_RDDATA
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_RDANY
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_READ))
            {
                dev->sense[0] = SENSE_CR;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
        }

        /* If not oriented to count field, read next count */
        if (dev->ckdorient != CKDORIENT_COUNT)
        {
            rc = ckd_read_count (dev, code, &rechdr, unitstat);
            if (rc < 0) break;
        }

        /* Calculate number of bytes to read and set residual count */
        size = dev->ckdcurkl + dev->ckdcurdl;
        num = (count < size) ? count : size;
        *residual = count - num;
        if (count < size) *more = 1;

        /* Read key field */
        rc = ckd_read_key (dev, code, iobuf, unitstat);
        if (rc < 0) break;

        /* Read data field */
        rc = ckd_read_data (dev, code, iobuf+dev->ckdcurkl, unitstat);
        if (rc < 0) break;

        /* Return unit exception if data length is zero */
        if (dev->ckdcurdl == 0)
            *unitstat = CSW_CE | CSW_DE | CSW_UX;
        else
            *unitstat = CSW_CE | CSW_DE;

        break;

    case 0x12:
    case 0x92:
    /*---------------------------------------------------------------*/
    /* READ COUNT                                                    */
    /*---------------------------------------------------------------*/
        /* Command reject if not preceded by a Seek, Seek Cylinder,
           Locate Record, Read IPL, or Recalibrate command */
        if (dev->ckdseek == 0 && dev->ckdskcyl == 0
            && dev->ckdlocat == 0 && dev->ckdrdipl == 0
            && dev->ckdrecal == 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Check operation code if within domain of a Locate Record */
        if (dev->ckdlocat)
        {
            if (!((dev->ckdloper & CKDOPER_CODE) == CKDOPER_RDDATA
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_RDANY
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_READ
                  || ((dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRITE
                      && (dev->ckdlaux & CKDLAUX_RDCNTSUF))))
            {
                dev->sense[0] = SENSE_CR;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
        }

        /* Read next count field */
        rc = ckd_read_count (dev, code, &rechdr, unitstat);
        if (rc < 0) break;

        /* Calculate number of bytes to read and set residual count */
        num = (count < CKDDASD_RECHDR_SIZE) ?
                                count : CKDDASD_RECHDR_SIZE;
        *residual = count - num;
        if (count < CKDDASD_RECHDR_SIZE) *more = 1;

        /* Copy count field to I/O buffer */
        memcpy (iobuf, &rechdr, num);

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x16:
    case 0x96:
    /*---------------------------------------------------------------*/
    /* READ RECORD ZERO                                              */
    /*---------------------------------------------------------------*/
        /* Command reject if not preceded by a Seek, Seek Cylinder,
           Locate Record, Read IPL, or Recalibrate command */
        if (dev->ckdseek == 0 && dev->ckdskcyl == 0
            && dev->ckdlocat == 0 && dev->ckdrdipl == 0
            && dev->ckdrecal == 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Check operation code if within domain of a Locate Record */
        if (dev->ckdlocat)
        {
            if (!((dev->ckdloper & CKDOPER_CODE) == CKDOPER_RDDATA
                  || ((dev->ckdloper & CKDOPER_CODE) == CKDOPER_READ
                      && ((dev->ckdloper & CKDOPER_ORIENTATION)
                                == CKDOPER_ORIENT_HOME
                          || (dev->ckdloper & CKDOPER_ORIENTATION)
                                == CKDOPER_ORIENT_INDEX
                        ))))
            {
                dev->sense[0] = SENSE_CR;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
        }

        /* For multitrack operation outside domain of a Locate Record,
           attempt to advance to the next track before reading R0 */
        if ((code & 0x80) && dev->ckdlocat == 0)
        {
            rc = mt_advance (dev, unitstat);
            if (rc < 0) break;
        }

        /* Seek to beginning of track */
        rc = ckd_seek (dev, dev->ckdcurcyl, dev->ckdcurhead,
                        &trkhdr, unitstat);
        if (rc < 0) break;

        /* Read the count field for record zero */
        rc = ckd_read_count (dev, code, &rechdr, unitstat);
        if (rc < 0) break;

        /* Calculate number of bytes to read and set residual count */
        size = CKDDASD_RECHDR_SIZE + dev->ckdcurkl + dev->ckdcurdl;
        num = (count < size) ? count : size;
        *residual = count - num;
        if (count < size) *more = 1;

        /* Copy count field to I/O buffer */
        memcpy (iobuf, &rechdr, CKDDASD_RECHDR_SIZE);

        /* Read key field */
        rc = ckd_read_key (dev, code,
                            iobuf + CKDDASD_RECHDR_SIZE, unitstat);
        if (rc < 0) break;

        /* Read data field */
        rc = ckd_read_data (dev, code,
                            iobuf + CKDDASD_RECHDR_SIZE + dev->ckdcurkl,
                            unitstat);
        if (rc < 0) break;

        /* Return unit exception if data length is zero */
        if (dev->ckdcurdl == 0)
            *unitstat = CSW_CE | CSW_DE | CSW_UX;
        else
            *unitstat = CSW_CE | CSW_DE;

        break;

    case 0x1A:
    case 0x9A:
    /*---------------------------------------------------------------*/
    /* READ HOME ADDRESS                                             */
    /*---------------------------------------------------------------*/
        /* Command reject if not preceded by a Seek, Seek Cylinder,
           Locate Record, Read IPL, or Recalibrate command */
        if (dev->ckdseek == 0 && dev->ckdskcyl == 0
            && dev->ckdlocat == 0 && dev->ckdrdipl == 0
            && dev->ckdrecal == 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Check operation code if within domain of a Locate Record */
        if (dev->ckdlocat)
        {
            if (!((dev->ckdloper & CKDOPER_CODE) == CKDOPER_RDDATA
                  || ((dev->ckdloper & CKDOPER_CODE) == CKDOPER_READ
                      && (dev->ckdloper & CKDOPER_ORIENTATION)
                                == CKDOPER_ORIENT_INDEX
                    )))
            {
                dev->sense[0] = SENSE_CR;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
        }

        /* For multitrack operation outside domain of a Locate Record,
           attempt to advance to the next track before reading HA */
        if ((code & 0x80) && dev->ckdlocat == 0)
        {
            rc = mt_advance (dev, unitstat);
            if (rc < 0) break;
        }

        /* Seek to beginning of track */
        rc = ckd_seek (dev, dev->ckdcurcyl, dev->ckdcurhead,
                        &trkhdr, unitstat);
        if (rc < 0) break;

        /* Calculate number of bytes to read and set residual count */
        size = CKDDASD_TRKHDR_SIZE;
        num = (count < size) ? count : size;
        *residual = count - num;
        if (count < size) *more = 1;

        /* Copy home address field to I/O buffer */
        memcpy (iobuf, &trkhdr, CKDDASD_TRKHDR_SIZE);

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;

        break;

    case 0x1E:
    case 0x9E:
    /*---------------------------------------------------------------*/
    /* READ COUNT KEY AND DATA                                       */
    /*---------------------------------------------------------------*/
        /* Command reject if not preceded by a Seek, Seek Cylinder,
           Locate Record, Read IPL, or Recalibrate command */
        if (dev->ckdseek == 0 && dev->ckdskcyl == 0
            && dev->ckdlocat == 0 && dev->ckdrdipl == 0
            && dev->ckdrecal == 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Check operation code if within domain of a Locate Record */
        if (dev->ckdlocat)
        {
            if (!((dev->ckdloper & CKDOPER_CODE) == CKDOPER_RDDATA
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_RDANY
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_READ))
            {
                dev->sense[0] = SENSE_CR;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
        }

        /* Read next count field */
        rc = ckd_read_count (dev, code, &rechdr, unitstat);
        if (rc < 0) break;

        /* Calculate number of bytes to read and set residual count */
        size = CKDDASD_RECHDR_SIZE + dev->ckdcurkl + dev->ckdcurdl;
        num = (count < size) ? count : size;
        *residual = count - num;
        if (count < size) *more = 1;

        /* Copy count field to I/O buffer */
        memcpy (iobuf, &rechdr, CKDDASD_RECHDR_SIZE);

        /* Read key field */
        rc = ckd_read_key (dev, code,
                            iobuf + CKDDASD_RECHDR_SIZE, unitstat);
        if (rc < 0) break;

        /* Read data field */
        rc = ckd_read_data (dev, code,
                            iobuf + CKDDASD_RECHDR_SIZE + dev->ckdcurkl,
                            unitstat);
        if (rc < 0) break;

        /* Return unit exception if data length is zero */
        if (dev->ckdcurdl == 0)
            *unitstat = CSW_CE | CSW_DE | CSW_UX;
        else
            *unitstat = CSW_CE | CSW_DE;

        break;

    case 0x07: /* SEEK */
    case 0x0B: /* SEEK CYLINDER */
    case 0x1B: /* SEEK HEAD */
    /*---------------------------------------------------------------*/
    /* SEEK                                                          */
    /*---------------------------------------------------------------*/
        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if Seek Head not preceded by a Seek,
           Seek Cylinder, Locate Record, Read IPL, or Recalibrate */
        if (code == 0x1B && dev->ckdseek == 0 && dev->ckdskcyl == 0
            && dev->ckdlocat == 0 && dev->ckdrdipl == 0
            && dev->ckdrecal == 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* File protected if file mask does not allow requested seek */
        if ((code == 0x07
            && (dev->ckdfmask & CKDMASK_SKCTL) != CKDMASK_SKCTL_ALLSKR)
           || (code == 0x0B
            && (dev->ckdfmask & CKDMASK_SKCTL) != CKDMASK_SKCTL_ALLSKR
            && (dev->ckdfmask & CKDMASK_SKCTL) != CKDMASK_SKCTL_CYLHD)
           || (code == 0x1B
            && (dev->ckdfmask & CKDMASK_SKCTL) == CKDMASK_SKCTL_INHSMT))
        {
            dev->sense[1] = SENSE1_FP;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Set residual count */
        num = (count < 6) ? count : 6;
        *residual = count - num;

        /* Command reject if count is less than 6 */
        if (count < 6)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Extract the BBCCHH seek address from the I/O buffer */
        bin = (iobuf[0] << 8) | iobuf[1];
        cyl = (iobuf[2] << 8) | iobuf[3];
        head = (iobuf[4] << 8) | iobuf[5];

        /* For Seek Head, use the current cylinder number */
        if (code == 0x1B)
            cyl = dev->ckdcurcyl;

        /* Command reject if seek address is invalid */
        if (bin != 0 || cyl >= dev->ckdcyls || head >= dev->ckdheads)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* File protected if outside defined extent */
        if (dev->ckdxtdef
            && (cyl < dev->ckdxbcyl || cyl > dev->ckdxecyl
                || (cyl == dev->ckdxbcyl && head < dev->ckdxbhead)
                || (cyl == dev->ckdxecyl && head > dev->ckdxehead)))
        {
            dev->sense[1] = SENSE1_FP;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Seek to specified cylinder and head */
        rc = ckd_seek (dev, cyl, head, &trkhdr, unitstat);
        if (rc < 0) break;

        /* Set command processed flag */
        dev->ckdseek = 1;

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x1F:
    /*---------------------------------------------------------------*/
    /* SET FILE MASK                                                 */
    /*---------------------------------------------------------------*/
        /* Command reject if preceded by a Define Extent or
           Set File Mask, or within the domain of a Locate Record */
        if (dev->ckdxtdef || dev->ckdsetfm || dev->ckdlocat)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Set residual count */
        num = (count < 1) ? count : 1;
        *residual = count - num;

        /* Command reject if count is less than 1 */
        if (count < 1)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Extract the new file mask from the I/O buffer */
        fmask = iobuf[0];

        /* Command reject if file mask is invalid */
        if ((fmask & CKDMASK_RESV) != 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Set the file mask to the new value */
        dev->ckdfmask = fmask;

        /* Set command processed flag */
        dev->ckdsetfm = 1;

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x22:
    /*---------------------------------------------------------------*/
    /* READ SECTOR                                                   */
    /*---------------------------------------------------------------*/
        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Set residual count */
        num = (count < 1) ? count : 1;
        *residual = count - num;
        if (count < 1) *more = 1;

        /* Return sector number in I/O buffer */
        iobuf[0] = 0;

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x23:
    /*---------------------------------------------------------------*/
    /* SET SECTOR                                                    */
    /*---------------------------------------------------------------*/
        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if not preceded by a Seek, Seek Cylinder,
           Locate Record, Read IPL, or Recalibrate command */
        if (dev->ckdseek == 0 && dev->ckdskcyl == 0
            && dev->ckdlocat == 0 && dev->ckdrdipl == 0
            && dev->ckdrecal == 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Set residual count */
        num = (count < 1) ? count : 1;
        *residual = count - num;

        /* Command reject if count is less than 1 */
        if (count < 1)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x29: case 0xA9: /* SEARCH KEY EQUAL */
    case 0x49: case 0xC9: /* SEARCH KEY HIGH */
    case 0x69: case 0xE9: /* SEARCH KEY EQUAL OR HIGH */
    /*---------------------------------------------------------------*/
    /* SEARCH KEY                                                    */
    /*---------------------------------------------------------------*/
        /* Command reject if not preceded by a Seek, Seek Cylinder,
           Locate Record, Read IPL, or Recalibrate command */
        if (dev->ckdseek == 0 && dev->ckdskcyl == 0
            && dev->ckdlocat == 0 && dev->ckdrdipl == 0
            && dev->ckdrecal == 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Read next key */
        rc = ckd_read_key (dev, code, key, unitstat);
        if (rc < 0) break;

        /* Calculate number of compare bytes and set residual count */
        num = (count < dev->ckdcurkl) ? count : dev->ckdcurkl;
        *residual = count - num;

        /* Reorient to data area if key length is zero */
        if (dev->ckdcurkl == 0)
        {
            dev->ckdorient = CKDORIENT_DATA;
            *unitstat = CSW_CE | CSW_DE;
            break;
        }

        /* Compare key with search argument */
        rc = memcmp(key, iobuf, num);

        /* Return status modifier if compare result matches */
        if (((code & 0x20) && (rc == 0))
            || ((code & 0x40) && (rc > 0)))
            *unitstat = CSW_SM | CSW_CE | CSW_DE;
        else
            *unitstat = CSW_CE | CSW_DE;

        /* Set flag if entire key was equal for SEARCH KEY EQUAL */
        if (rc == 0 && num == dev->ckdcurkl && (code & 0x7F) == 0x29)
            dev->ckdkyeq = 1;
        else
            dev->ckdkyeq = 0;

        break;

    case 0x31: case 0xB1: /* SEARCH ID EQUAL */
    case 0x51: case 0xD1: /* SEARCH ID HIGH */
    case 0x71: case 0xF1: /* SEARCH ID EQUAL OR HIGH */
    /*---------------------------------------------------------------*/
    /* SEARCH ID                                                     */
    /*---------------------------------------------------------------*/
        /* Command reject if not preceded by a Seek, Seek Cylinder,
           Locate Record, Read IPL, or Recalibrate command */
        if (dev->ckdseek == 0 && dev->ckdskcyl == 0
            && dev->ckdlocat == 0 && dev->ckdrdipl == 0
            && dev->ckdrecal == 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Read next count field */
        rc = ckd_read_count (dev, code, &rechdr, unitstat);
        if (rc < 0) break;

        /* Calculate number of compare bytes and set residual count */
        num = (count < 5) ? count : 5;
        *residual = count - num;

        /* Compare count with search argument */
        rc = memcmp(&rechdr, iobuf, num);

        /* Return status modifier if compare result matches */
        if (((code & 0x20) && (rc == 0))
            || ((code & 0x40) && (rc > 0)))
            *unitstat = CSW_SM | CSW_CE | CSW_DE;
        else
            *unitstat = CSW_CE | CSW_DE;

        /* Set flag if entire id compared equal for SEARCH ID EQUAL */
        if (rc == 0 && num == 5 && (code & 0x7F) == 0x31)
            dev->ckdideq = 1;
        else
            dev->ckdideq = 0;

        break;

    case 0x39:
    case 0xB9:
    /*---------------------------------------------------------------*/
    /* SEARCH HOME ADDRESS EQUAL                                     */
    /*---------------------------------------------------------------*/
        /* Command reject if not preceded by a Seek, Seek Cylinder,
           Locate Record, Read IPL, or Recalibrate command */
        if (dev->ckdseek == 0 && dev->ckdskcyl == 0
            && dev->ckdlocat == 0 && dev->ckdrdipl == 0
            && dev->ckdrecal == 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* For multitrack operation, advance to next track */
        if (code & 0x80)
        {
            rc = mt_advance (dev, unitstat);
            if (rc < 0) break;
        }

        /* Seek to beginning of track */
        rc = ckd_seek (dev, dev->ckdcurcyl, dev->ckdcurhead,
                        &trkhdr, unitstat);
        if (rc < 0) break;

        /* Calculate number of compare bytes and set residual count */
        num = (count < 4) ? count : 4;
        *residual = count - num;

        /* Compare CCHH portion of track header with search argument */
        rc = memcmp(&trkhdr+1, iobuf, num);

        /* Return status modifier if compare result matches */
        if (rc == 0)
            *unitstat = CSW_SM | CSW_CE | CSW_DE;
        else
            *unitstat = CSW_CE | CSW_DE;

        /* Set flag if entire home address compared equal */
        if (rc == 0 && num == 4)
            dev->ckdhaeq = 1;
        else
            dev->ckdhaeq = 0;

        break;

#if 0 /* Write commands still under development */
    case 0x05:
    /*---------------------------------------------------------------*/
    /* WRITE DATA                                                    */
    /*---------------------------------------------------------------*/
        /* Command reject if the current track is in the DSF area */
        /*INCOMPLETE*/
            /*INCOMPLETE*/ /*Sense Format 0 message 2*/

        /* Command reject if not within the domain of a Locate Record
           and not preceded by either a Search ID Equal or Search Key
           Equal that compared equal on all bytes */
           /*INCOMPLETE*/ /*Write CKD allows intervening Read/Write
             key and data commands, Write Data does not!!! Rethink
             the handling of these flags*/
        if (dev->ckdlocat == 0 && dev->ckdideq == 0
            && dev->ckdkyeq == 0)
        {
            dev->sense[0] = SENSE_CR;
            /*INCOMPLETE*/ /*Sense Format 0 message 2*/
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if file mask inhibits all write commands */
        if ((dev->ckdfmask & CKDMASK_WRCTL) == CKDMASK_WRCTL_INHWRT)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            /*INCOMPLETE*/ /*Sense Format 0 message 2*/
            break;
        }

        /* Check operation code if within domain of a Locate Record */
        if (dev->ckdlocat)
        {
            if (!(((dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRITE
        /*INCOMPLETE*/ && dev->ckdlcount == 1 /*+ 1 if rdcount suffix */)
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRTTRK))
            {
                dev->sense[0] = SENSE_CR;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                /*INCOMPLETE*/ /*Sense Format 0 message 2*/
                break;
            }
            /*INCOMPLETE*/ /*Use transfer length factor and check
            against CKD conversion mode*/
        }

        /* If data length is zero, terminate with unit exception */
        if (dev->ckdcurdl == 0)
        {
            *unitstat = CSW_CE | CSW_DE | CSW_UX;
            break;
        }

        /* Write data */
        rc = ckd_write_data (dev, iobuf, count, unitstat);
        if (rc < 0) break;

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;

        break;

    case 0x0D:
    /*---------------------------------------------------------------*/
    /* WRITE KEY AND DATA                                            */
    /*---------------------------------------------------------------*/
        /* Command reject if the current track is in the DSF area */
        /*INCOMPLETE*/
            /*INCOMPLETE*/ /*Sense Format 0 message 2*/

        /* Command reject if not within the domain of a Locate Record
           and not preceded by a Search ID Equal that compared equal
           on all bytes */
           /*INCOMPLETE*/ /*Write CKD allows intervening Read/Write
             key and data commands, Write Key Data does not!!! Rethink
             the handling of these flags*/
        if (dev->ckdlocat == 0 && dev->ckdideq == 0)
        {
            dev->sense[0] = SENSE_CR;
            /*INCOMPLETE*/ /*Sense Format 0 message 2*/
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if file mask inhibits all write commands */
        if ((dev->ckdfmask & CKDMASK_WRCTL) == CKDMASK_WRCTL_INHWRT)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            /*INCOMPLETE*/ /*Sense Format 0 message 2*/
            break;
        }

        /* Check operation code if within domain of a Locate Record */
        if (dev->ckdlocat)
        {
            if (!(((dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRITE
        /*INCOMPLETE*/ && dev->ckdlcount == 1 /*+ 1 if rdcount suffix */)
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRTTRK))
            {
                dev->sense[0] = SENSE_CR;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                /*INCOMPLETE*/ /*Sense Format 0 message 2*/
                break;
            }
            /*INCOMPLETE*/ /*Use transfer length factor and check
            against CKD conversion mode*/
        }

        /* If data length is zero, terminate with unit exception */
        if (dev->ckdcurdl == 0)
        {
            *unitstat = CSW_CE | CSW_DE | CSW_UX;
            break;
        }

        /* Write key and data */
        rc = ckd_write_kd (dev, iobuf, count, unitstat);
        if (rc < 0) break;

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;

        break;

    case 0x15:
    /*---------------------------------------------------------------*/
    /* WRITE RECORD ZERO                                             */
    /*---------------------------------------------------------------*/
        /* Command reject if the current track is in the DSF area */
        /*INCOMPLETE*/
            /*INCOMPLETE*/ /*Sense Format 0 message 2*/

        /* Command reject if not within the domain of a Locate Record
           and not preceded by either a Search Home Address that
           compared equal on all 4 bytes, or a Write Home Address not
           within the domain of a Locate Record */
        if (dev->ckdlocat == 0 && dev->ckdhaeq == 0)
        {
            dev->sense[0] = SENSE_CR;
            /*INCOMPLETE*/ /*Sense Format 0 message 2*/
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if file mask does not permit Write R0 */
        if ((dev->ckdfmask & CKDMASK_WRCTL) != CKDMASK_WRCTL_ALLWRT)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            /*INCOMPLETE*/ /*Sense Format 0 message 2*/
            break;
        }

        /* Check operation code if within domain of a Locate Record */
        if (dev->ckdlocat)
        {
            if (!((dev->ckdloper & CKDOPER_CODE) == CKDOPER_FORMAT
                    && ((dev->ckdloper & CKDOPER_ORIENTATION)
                                == CKDOPER_ORIENT_HOME
                          || (dev->ckdloper & CKDOPER_ORIENTATION)
                                == CKDOPER_ORIENT_INDEX
                       )))
            {
                dev->sense[0] = SENSE_CR;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                /*INCOMPLETE*/ /*Sense Format 0 message 2*/
                break;
            }
        }

        /* Write R0 count key and data */
        rc = ckd_write_ckd (dev, iobuf, count, unitstat);
        if (rc < 0) break;

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;

        /* Set flag if Write R0 outside domain of a locate record */
        if (dev->ckdlocat == 0)
            dev->ckdwckd = 1;
        else
            dev->ckdwckd = 0;

        break;

#endif
    case 0x1D:
    /*---------------------------------------------------------------*/
    /* WRITE COUNT KEY AND DATA                                      */
    /*---------------------------------------------------------------*/
        /* Command reject if the current track is in the DSF area */
        /*INCOMPLETE*/
            /*INCOMPLETE*/ /*Sense Format 0 message 2*/

        /* Command reject if previous command was a Write R0 that
           assigned an alternate track */
        /*INCOMPLETE*/
            /*INCOMPLETE*/ /*Sense Format 0 message 2*/

        /* Command reject if not within the domain of a Locate Record
           and not preceded by either a Search ID Equal or Search Key
           Equal that compared equal on all bytes, or a Write R0 or
           Write CKD not within the domain of a Locate Record */
        if (dev->ckdlocat == 0 && dev->ckdideq == 0
            && dev->ckdkyeq == 0 && dev->ckdwckd == 0)
        {
            dev->sense[0] = SENSE_CR;
            /*INCOMPLETE*/ /*Sense Format 0 message 2*/
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if file mask does not permit Write CKD */
        if ((dev->ckdfmask & CKDMASK_WRCTL) != CKDMASK_WRCTL_ALLWRT
            && (dev->ckdfmask & CKDMASK_WRCTL) != CKDMASK_WRCTL_INHWR0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            /*INCOMPLETE*/ /*Sense Format 0 message 2*/
            break;
        }

        /* Check operation code if within domain of a Locate Record */
        if (dev->ckdlocat)
        {
            if (!((dev->ckdloper & CKDOPER_CODE) == CKDOPER_FORMAT
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRTTRK))
            {
                dev->sense[0] = SENSE_CR;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                /*INCOMPLETE*/ /*Sense Format 0 message 2*/
                break;
            }
        }

        /* Write count key and data */
        rc = ckd_write_ckd (dev, iobuf, count, unitstat);
        if (rc < 0) break;

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;

        /* Set flag if Write CKD outside domain of a locate record */
        if (dev->ckdlocat == 0)
            dev->ckdwckd = 1;
        else
            dev->ckdwckd = 0;

        break;

    case 0x64:
    /*---------------------------------------------------------------*/
    /* READ DEVICE CHARACTERISTICS                                   */
    /*---------------------------------------------------------------*/
        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat == 0)
        {
            dev->sense[0] = SENSE_CR;
            /*INCOMPLETE*/ /*Sense Format 0 message 2*/
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Calculate residual byte count */
        num = (count < dev->numdevchar) ? count : dev->numdevchar;
        *residual = count - num;
        if (count < dev->numdevchar) *more = 1;

        /* Copy device characteristics bytes to channel buffer */
        memcpy (iobuf, dev->devchar, num);

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

    /* Reset the flags which ensure correct positioning for write
       commands */

    /* Reset search HA flag if command was not SEARCH HA EQUAL
       or WRITE HA */
    if ((code & 0x7F) != 0x39 && (code & 0x7F) != 0x19)
        dev->ckdhaeq = 0;

    /* Reset search id flag if command was not SEARCH ID EQUAL,
       READ/WRITE KEY AND DATA, or READ/WRITE DATA */
    if ((code & 0x7F) != 0x31
        && (code & 0x7F) != 0x0E && (code & 0x7F) != 0x0D
        && (code & 0x7F) != 0x06 && (code & 0x7F) != 0x05)
        dev->ckdideq = 0;

    /* Reset search key flag if command was not SEARCH KEY EQUAL
       or READ/WRITE DATA */
    if ((code & 0x7F) != 0x29
        && (code & 0x7F) != 0x06 && (code & 0x7F) != 0x05)
        dev->ckdkyeq = 0;

    /* Reset write CKD flag if command was not WRITE R0 or WRITE CKD */
    if (code != 0x15 && code != 0x1D)
        dev->ckdwckd = 0;

} /* end function ckddasd_execute_ccw */

