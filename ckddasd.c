/* CKDDASD.C    (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 CKD Direct Access Storage Device Handler     */

/*-------------------------------------------------------------------*/
/* This module contains device handling functions for emulated       */
/* count-key-data direct access storage devices.                     */
/*-------------------------------------------------------------------*/

#include "hercules.h"

#undef  CKD_KEY_TRACING

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
#define CKDMASK_AAUTH_DIAG      0x04    /* ...diagnostic auth        */
#define CKDMASK_AAUTH_DSFNCR    0x06    /* ...device support with no
                                              correction or retry    */
#define CKDMASK_PCI_FETCH       0x01    /* PCI fetch mode            */

/*-------------------------------------------------------------------*/
/* Bit definitions for Define Extent global attributes byte          */
/*-------------------------------------------------------------------*/
#define CKDGATR_ARCH            0xC0    /* Architecture mode...      */
#define CKDGATR_ARCH_ECKD       0xC0    /* ...extended CKD mode      */
#define CKDGATR_CKDCONV         0x20    /* CKD conversion mode       */
#define CKDGATR_SSOP            0x1C    /* Subsystem operation mode..*/
#define CKDGATR_SSOP_NORMAL     0x00    /* ...normal cache           */
#define CKDGATR_SSOP_BYPASS     0x04    /* ...bypass cache           */
#define CKDGATR_SSOP_INHIBIT    0x08    /* ...inhibit cache loading  */
#define CKDGATR_SSOP_SEQACC     0x0C    /* ...sequential access      */
#define CKDGATR_SSOP_LOGGING    0x10    /* ...logging mode           */
#define CKDGATR_USE_CACHE_FW    0x02    /* Use cache fast write      */
#define CKDGATR_INH_DASD_FW     0x01    /* Inhibit DASD fast write   */

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
/* Bit definitions for Diagnostic Control subcommand byte            */
/*-------------------------------------------------------------------*/
#define DIAGCTL_INHIBIT_WRITE   0x02    /* Inhibit Write             */                             �
#define DIAGCTL_SET_GUAR_PATH   0x04    /* Set Guaranteed Path       */
#define DIAGCTL_ENABLE_WRITE    0x08    /* Enable Write              */
#define DIAGCTL_3380_TC_MODE    0x09    /* 3380 Track Compat Mode    */
#define DIAGCTL_INIT_SUBSYS     0x0B    /* Diagnostic Init Subsys    */
#define DIAGCTL_UNFENCE         0x0C    /* Unfence                   */
#define DIAGCTL_ACCDEV_UNKCOND  0x0F    /* Access Device Unknown Cond*/
#define DIAGCTL_MAINT_RESERVE   0x10    /* Media Maintenance Reserve */
#define DIAGCTL_MAINT_RELEASE   0x11    /* Media Maintenance Release */
#define DIAGCTL_MAINT_QUERY     0x12    /* Media Maintenance Query   */

/*-------------------------------------------------------------------*/
/* Definitions for sense data format codes and message codes         */
/*-------------------------------------------------------------------*/
#define FORMAT_0                0       /* Program or System Checks  */
#define FORMAT_1                1       /* Device Equipment Checks   */
#define FORMAT_2                2       /* 3990 Equipment Checks     */
#define FORMAT_3                3       /* 3990 Control Checks       */
#define FORMAT_4                4       /* Data Checks               */
#define FORMAT_5                5       /* Data Check + Displacement */
#define FORMAT_6                6       /* Usage Stats/Overrun Errors*/
#define FORMAT_7                7       /* Device Control Checks     */
#define FORMAT_8                8       /* Device Equipment Checks   */
#define FORMAT_9                9       /* Device Rd/Wrt/Seek Checks */
#define FORMAT_F                15      /* Cache Storage Checks      */
#define MESSAGE_0               0       /* Message 0                 */
#define MESSAGE_1               1       /* Message 1                 */
#define MESSAGE_2               2       /* Message 2                 */
#define MESSAGE_3               3       /* Message 3                 */
#define MESSAGE_4               4       /* Message 4                 */
#define MESSAGE_5               5       /* Message 5                 */
#define MESSAGE_6               6       /* Message 6                 */
#define MESSAGE_7               7       /* Message 7                 */
#define MESSAGE_8               8       /* Message 8                 */
#define MESSAGE_9               9       /* Message 9                 */
#define MESSAGE_A               10      /* Message A                 */
#define MESSAGE_B               11      /* Message B                 */
#define MESSAGE_C               12      /* Message C                 */
#define MESSAGE_D               13      /* Message D                 */
#define MESSAGE_E               14      /* Message E                 */
#define MESSAGE_F               15      /* Message F                 */


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
int             tracklen;               /* Physical track length     */
int             har0len;                /* Length of HA + R0         */
int             rpscalc;                /* RPS calculation factors   */
int             formula;                /* Track capacity formula    */
int             f1, f2, f3, f4, f5, f6; /* Track capacity factors    */
U16             cutype;                 /* Control unit type         */
BYTE            cumodel;                /* Control unit model number */
BYTE            cucode;                 /* Control unit type code    */
BYTE            devmodel;               /* Device model number       */
BYTE            devclass;               /* Device class              */
BYTE            devtcode;               /* Device type code          */
U32             sctlfeat;               /* Storage control features  */

    /* The first argument is the file name */
    if (argc == 0 || strlen(argv[0]) > sizeof(dev->filename)-1)
    {
        printf ("HHC351I File name missing or invalid\n");
        return -1;
    }

    /* Save the file name in the device block */
    strcpy (dev->filename, argv[0]);

    /* Open the device file */
    dev->fd = open (dev->filename, O_RDWR);
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
    dev->numsense = 32;

    /* Set the device and control unit identifiers */
    devclass = 0x20;

    switch (dev->devtype) {
    case 0x3390:
        cutype = 0x3990; cumodel = 0xC2; cucode = 0x10;
        if (dev->ckdcyls > 3339)
            { devmodel = 0x0C; devtcode = 0x32; } /*3390-9*/
        else if (dev->ckdcyls > 2226)
            { devmodel = 0x0A; devtcode = 0x24; } /*3390-3*/
        else if (dev->ckdcyls > 1113)
            { devmodel = 0x06; devtcode = 0x27; } /*3390-2*/
        else
            { devmodel = 0x02; devtcode = 0x26; } /*3390-1*/
        dev->ckdsectors = 224;
        dev->ckdmaxr0len = 57326;
        dev->ckdmaxr1len = 56664;
        tracklen = 58786;
        har0len = 1428;
        sctlfeat = 0xD0000002;
        formula = 2;
        f1 = 34; f2 = 19; f3 = 9; f4 = 6; f5 = 116; f6 = 6;
        rpscalc = 0x7708;
        break;
    case 0x3380:
        cutype = 0x3880; cumodel = 0x03; cucode = 0x10;
        if (dev->ckdcyls > 1770)
            { devmodel = 0x1E; sctlfeat = 0xD0000003; } /*3880K*/
        else if (dev->ckdcyls > 885)
            { devmodel = 0x0A; sctlfeat = 0x50000003; } /*3880E*/
        else
            { devmodel = 0x02; sctlfeat = 0x50000003; } /*3880A*/
        devtcode = 0x0E;
        dev->ckdsectors = 222;
        dev->ckdmaxr0len = 47988;
        dev->ckdmaxr1len = 47476;
        tracklen = 47968;
        har0len = 1088;
        formula = 1;
        f1 = 32; f2 = 1; f3 = 0xEC; f4 = 0; f5 = 0xEC; f6 = 0;
        rpscalc = 0x5007;
        break;
    case 0x3350:
        cutype = 0x3880; cumodel = 0x01; cucode = 0x00;
        devmodel = 0x00; devtcode = 0x00;
        dev->ckdsectors = 128;
        dev->ckdmaxr0len = 19254;
        dev->ckdmaxr1len = 19069;
        tracklen = 19254;
        har0len = 185;
        sctlfeat = 0x50000103;
        formula = 0;
        f1 = 0; f2 = 0; f3 = 0; f4 = 0; f5 = 0; f6 = 0;
        rpscalc = 0x0000;
        break;
    case 0x3330:
        cutype = 0x3880; cumodel = 0x01; cucode = 0x00;
        if (dev->ckdcyls > 404)
            devmodel = 0x11; /*3330-11*/
        else
            devmodel = 0x01; /*3330-1*/
        devtcode = 0x00;
        dev->ckdsectors = 128;
        dev->ckdmaxr0len = 13165;
        dev->ckdmaxr1len = 13030;
        tracklen = 19165;
        har0len = 135;
        sctlfeat = 0x50000103;
        formula = 0;
        f1 = 0; f2 = 0; f3 = 0; f4 = 0; f5 = 0; f6 = 0;
        rpscalc = 0x0000;
        break;
    default:
        cutype = 0x3880; cumodel = 0x01; cucode = 0x10;
        devmodel = 0x00; devtcode = 0x00;
        dev->ckdsectors = 0;
        dev->ckdmaxr0len = 0;
        dev->ckdmaxr1len = 0;
        tracklen = 0;
        har0len = 0;
        sctlfeat = 0x50000103;
        formula = 0;
        f1 = 0; f2 = 0; f3 = 0; f4 = 0; f5 = 0; f6 = 0;
        rpscalc = 0x0000;
    } /* end switch(dev->devtype) */

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
    memset (dev->devchar, 0, sizeof(dev->devchar));
    memcpy (dev->devchar, dev->devid+1, 6);
    dev->devchar[6] = (sctlfeat >> 24) & 0xFF;
    dev->devchar[7] = (sctlfeat >> 16) & 0xFF;
    dev->devchar[8] = (sctlfeat >> 8) & 0xFF;
    dev->devchar[9] = sctlfeat & 0xFF;
    dev->devchar[10] = devclass;
    dev->devchar[11] = devtcode;
    dev->devchar[12] = (dev->ckdcyls >> 8) & 0xFF;
    dev->devchar[13] = dev->ckdcyls & 0xFF;
    dev->devchar[14] = (dev->ckdheads >> 8) & 0xFF;
    dev->devchar[15] = dev->ckdheads & 0xFF;
    dev->devchar[16] = dev->ckdsectors;
    dev->devchar[17] = (tracklen >> 16) & 0xFF;
    dev->devchar[18] = (tracklen >> 8) & 0xFF;
    dev->devchar[19] = tracklen & 0xFF;
    dev->devchar[20] = (har0len >> 8) & 0xFF;
    dev->devchar[21] = har0len & 0xFF;
    dev->devchar[22] = formula;
    dev->devchar[23] = f1;
    dev->devchar[24] = f2;
    dev->devchar[25] = f3;
    dev->devchar[26] = f4;
    dev->devchar[27] = f5;
    dev->devchar[28] = (dev->ckdcyls >> 8) & 0xFF;
    dev->devchar[29] = dev->ckdcyls & 0xFF;
    dev->devchar[30] = 0;
    dev->devchar[31] = 0;
    dev->devchar[32] = (dev->ckdcyls >> 8) & 0xFF;
    dev->devchar[33] = dev->ckdcyls & 0xFF;
    dev->devchar[34] = 0;
    dev->devchar[35] = 0;
    dev->devchar[36] = (dev->ckdcyls >> 8) & 0xFF;
    dev->devchar[37] = dev->ckdcyls & 0xFF;
    dev->devchar[38] = 0;
    dev->devchar[39] = 0;
    dev->devchar[40] = devtcode;
    dev->devchar[41] = devtcode;
    dev->devchar[42] = cucode;
    dev->devchar[43] = 0;
    dev->devchar[44] = (dev->ckdmaxr0len >> 8) & 0xFF;
    dev->devchar[45] = dev->ckdmaxr0len & 0xFF;
    dev->devchar[46] = 0;
    dev->devchar[47] = 0;
    dev->devchar[48] = f6;
    dev->devchar[49] = (rpscalc >> 8) & 0xFF;
    dev->devchar[50] = rpscalc & 0xFF;
    dev->numdevchar = 64;

    /* Activate I/O tracing */
    dev->ccwtrace = 1;

    return 0;
} /* end function ckddasd_init_handler */


/*-------------------------------------------------------------------*/
/* Build sense data                                                  */
/*-------------------------------------------------------------------*/
static void ckd_build_sense ( DEVBLK *dev, BYTE sense0, BYTE sense1,
                BYTE sense2, BYTE format, BYTE message )
{
    /* Clear the sense bytes */
    memset (dev->sense, 0, sizeof(dev->sense));

    /* Sense bytes 0-2 are specified by caller */
    dev->sense[0] = sense0;
    dev->sense[1] = sense1;
    dev->sense[2] = sense2;

    /* Sense byte 3 contains the residual locate record count
       if imprecise ending is indicated in sense byte 1 */
    if (sense1 & SENSE1_IE)
        dev->sense[3] = dev->ckdlcount;

    /* Sense byte 4 is the physical device address */
    dev->sense[4] = 0;

    /* Sense byte 5 contains bits 8-15 of the cylinder address
       and sense byte 6 contains bits 4-7 of the cylinder
       address followed by bits 12-15 of the head address,
       unless the device has more than 4095 cylinders, in
       which case sense bytes 5 and 6 both contain X'FF' */
    if (dev->ckdcyls > 4095)
    {
        dev->sense[5] = 0xFF;
        dev->sense[6] = 0xFF;
    }
    else
    {
        dev->sense[5] = dev->ckdcurcyl & 0xFF;
        dev->sense[6] = ((dev->ckdcurcyl >> 4) & 0xF0)
                        | (dev->ckdcurhead & 0x0F);
    }

    /* Sense byte 7 contains the format code and message type */
    dev->sense[7] = (format << 4) | (message & 0x0F);

    /* Sense bytes 8-23 depend on the format code */
    switch (format) {

    case FORMAT_4: /* Data check */
    case FORMAT_5: /* Data check with displacement information */
        /* Sense bytes 8-12 contain the CCHHR of the record in error */
        dev->sense[8] = dev->ckdcurcyl >> 8;
        dev->sense[9] = dev->ckdcurcyl & 0xFF;
        dev->sense[10] = dev->ckdcurhead >> 8;
        dev->sense[11] = dev->ckdcurhead & 0xFF;
        dev->sense[12] = dev->ckdcurrec;
        break;

    } /* end switch(format) */

    /* Sense byte 27 bit 0 indicates 24-byte compatability sense data*/
    dev->sense[27] = 0x80;

    /* Sense bytes 29-30 contain the cylinder address */
    dev->sense[29] = dev->ckdcurcyl >> 8;
    dev->sense[30] = dev->ckdcurcyl & 0xFF;

    /* Sense byte 31 contains the head address */
    dev->sense[31] = dev->ckdcurhead & 0xFF;

} /* end function ckd_build_sense */

/*-------------------------------------------------------------------*/
/* Skip past count, key, or data fields                              */
/*-------------------------------------------------------------------*/
static int ckd_skip ( DEVBLK *dev, int skiplen, BYTE *unitstat )
{
int             rc;                     /* Return code               */

    DEVTRACE("ckddasd: skipping %d bytes\n", skiplen);

    rc = lseek (dev->fd, skiplen, SEEK_CUR);
    if (rc < 0)
    {
        /* Handle seek error condition */
        perror("ckddasd: lseek error");

        /* Set unit check with equipment check */
        ckd_build_sense (dev, SENSE_EC, 0, 0,
                        FORMAT_1, MESSAGE_0);
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

    DEVTRACE("ckddasd: seeking to cyl %d head %d\n", cyl, head);

    if (cyl >= dev->ckdcyls || head >= dev->ckdheads)
    {
        ckd_build_sense (dev, SENSE_CR, 0, 0,
                        FORMAT_0, MESSAGE_4);
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
        ckd_build_sense (dev, SENSE_EC, 0, 0,
                        FORMAT_1, MESSAGE_0);
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
        ckd_build_sense (dev, SENSE_EC, 0, 0,
                        FORMAT_1, MESSAGE_0);
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
        ckd_build_sense (dev, 0, SENSE1_FP, 0, 0, 0);
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* End of cylinder error if not within domain of Locate Record
       and current track is last track of cylinder */
    if (dev->ckdlocat == 0
        && dev->ckdcurhead >= dev->ckdheads - 1)
    {
        ckd_build_sense (dev, 0, SENSE1_EOC, 0, 0, 0);
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
        ckd_build_sense (dev, 0, SENSE1_FP, 0, 0, 0);
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
       SEARCH ID EQUAL, SEARCH ID HIGH, SEARCH ID EQUAL OR HIGH,
       LOCATE RECORD, and WRITE CKD NEXT TRACK */
    if (code != 0xDE
        && (code & 0x7F) != 0x16
        && (code & 0x7F) != 0x31
        && (code & 0x7F) != 0x51
        && (code & 0x7F) != 0x71
        && code != 0x47
        && code != 0x9D)
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
            ckd_build_sense (dev, SENSE_EC, 0, 0,
                            FORMAT_1, MESSAGE_0);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return -1;
        }

        /* Set the device orientation fields */
        dev->ckdcurrec = rechdr->rec;
        dev->ckdrem = 0;
        dev->ckdorient = CKDORIENT_COUNT;
        dev->ckdcurkl = rechdr->klen;
        dev->ckdcurdl = (rechdr->dlen[0] << 8) + rechdr->dlen[1];

        DEVTRACE("ckddasd: cyl %d head %d record %d kl %d dl %d\n",
                dev->ckdcurcyl, dev->ckdcurhead,
                dev->ckdcurrec, dev->ckdcurkl, dev->ckdcurdl);

        /* Skip record zero if user data record required */
        if (skipr0 && rechdr->rec == 0)
            continue;

        /* Test for logical end of track and exit if not */
        if (memcmp(rechdr, eighthexFF, 8) != 0)
            break;

        /* If this is a READ TRACK command, return with the
           end of track marker in the record header field */
        if (code == 0xDE)
            break;

        /* End of track found, so terminate with no record found
           error if this is a LOCATE RECORD or WRITE CKD NEXT TRACK
           command; or if this is the second end of track in this
           channel program without an intervening read of the home
           address or data area and without an intervening write,
           sense, or control command */
        if (code == 0x47 || code == 0x9D || dev->ckdxmark)
        {
            ckd_build_sense (dev, 0, SENSE1_NRF, 0, 0, 0);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return -1;
        }

        /* Test for multitrack operation */
        if ((code & 0x80) == 0)
        {
            /* If non-multitrack, return to start of current track */
            cyl = dev->ckdcurcyl;
            head = dev->ckdcurhead;
            rc = ckd_seek (dev, cyl, head, &trkhdr, unitstat);
            if (rc < 0) return -1;

            /* Set index marker found flag */
            dev->ckdxmark = 1;
        }
        else
        {
            /* If multitrack, attempt to advance to next track */
            rc = mt_advance (dev, unitstat);
            if (rc < 0) return -1;

            /* Set index marker flag if non-search command */
            if ((code & 0x7F) != 0x31
                && (code & 0x7F) != 0x51
                && (code & 0x7F) != 0x71
                && (code & 0x7F) != 0x29
                && (code & 0x7F) != 0x49
                && (code & 0x7F) != 0x69)
                dev->ckdxmark = 1;
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

    DEVTRACE("ckddasd: reading %d bytes\n", dev->ckdcurkl);

    /* Read key field */
    if (dev->ckdcurkl > 0)
    {
        rc = read (dev->fd, buf, dev->ckdcurkl);
        if (rc < dev->ckdcurkl)
        {
            /* Handle read error condition */
            perror("ckddasd: read error");

            /* Set unit check with equipment check */
            ckd_build_sense (dev, SENSE_EC, 0, 0,
                            FORMAT_1, MESSAGE_0);
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

    DEVTRACE("ckddasd: reading %d bytes\n", dev->ckdcurdl);

    /* Read data field */
    if (dev->ckdcurdl > 0)
    {
        rc = read (dev->fd, buf, dev->ckdcurdl);
        if (rc < dev->ckdcurdl)
        {
            /* Handle read error condition */
            perror("ckddasd: read error");

            /* Set unit check with equipment check */
            ckd_build_sense (dev, SENSE_EC, 0, 0,
                            FORMAT_1, MESSAGE_0);
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
int             skiplen;                /* Number of bytes to skip   */

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
        ckd_build_sense (dev, SENSE_EC, 0, 0,
                        FORMAT_1, MESSAGE_0);
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
        /* Unit check with invalid track format */
        ckd_build_sense (dev, 0, SENSE1_ITF, 0, 0, 0);
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Pad the I/O buffer with zeroes if necessary */
    while (len < ckdlen) buf[len++] = '\0';

    DEVTRACE("ckddasd: writing cyl %d head %d record %d kl %d dl %d\n",
            dev->ckdcurcyl, dev->ckdcurhead, recnum, keylen, datalen);

    /* Write count key and data */
    rc = write (dev->fd, buf, ckdlen);
    if (rc < ckdlen)
    {
        /* Handle write error condition */
        perror("ckddasd: write error");

        /* Set unit check with equipment check */
        ckd_build_sense (dev, SENSE_EC, 0, 0,
                        FORMAT_1, MESSAGE_0);
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
        ckd_build_sense (dev, SENSE_EC, 0, 0,
                        FORMAT_1, MESSAGE_0);
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
        ckd_build_sense (dev, SENSE_EC, 0, 0,
                        FORMAT_1, MESSAGE_0);
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
/* Write key and data fields                                         */
/*-------------------------------------------------------------------*/
static int ckd_write_kd ( DEVBLK *dev, BYTE *buf, U16 len,
                BYTE *unitstat)
{
int             rc;                     /* Return code               */
U16             kdlen;                  /* Key+data length           */

    /* Unit check if not oriented to count area */
    if (dev->ckdorient != CKDORIENT_COUNT)
    {
        printf("ckddasd: Write KD orientation error\n");
        ckd_build_sense (dev, SENSE_CR, 0, 0,
                        FORMAT_0, MESSAGE_2);
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Calculate total key and data size */
    kdlen = dev->ckdcurkl + dev->ckdcurdl;

    /* Pad the I/O buffer with zeroes if necessary */
    while (len < kdlen) buf[len++] = '\0';

    DEVTRACE("ckddasd: updating record %d kl %d dl %d\n",
            dev->ckdcurrec, dev->ckdcurkl, dev->ckdcurdl);

    /* Write key and data */
    rc = write (dev->fd, buf, kdlen);
    if (rc < kdlen)
    {
        /* Handle write error condition */
        perror("ckddasd: write error");

        /* Set unit check with equipment check */
        ckd_build_sense (dev, SENSE_EC, 0, 0,
                        FORMAT_1, MESSAGE_0);
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Set the device orientation fields */
    dev->ckdrem = 0;
    dev->ckdorient = CKDORIENT_DATA;

    return 0;
} /* end function ckd_write_kd */


/*-------------------------------------------------------------------*/
/* Write data field                                                  */
/*-------------------------------------------------------------------*/
static int ckd_write_data ( DEVBLK *dev, BYTE *buf, U16 len,
                BYTE *unitstat)
{
int             rc;                     /* Return code               */
int             skiplen;                /* Number of bytes to skip   */

    /* Unit check if not oriented to count or key areas */
    if (dev->ckdorient != CKDORIENT_COUNT
        && dev->ckdorient != CKDORIENT_KEY)
    {
        printf("ckddasd: Write data orientation error\n");
        ckd_build_sense (dev, SENSE_CR, 0, 0,
                        FORMAT_0, MESSAGE_2);
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
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

    /* Pad the I/O buffer with zeroes if necessary */
    while (len < dev->ckdcurdl) buf[len++] = '\0';

    DEVTRACE("ckddasd: updating record %d dl %d\n",
            dev->ckdcurrec, dev->ckdcurdl);

    /* Write data */
    rc = write (dev->fd, buf, dev->ckdcurdl);
    if (rc < dev->ckdcurdl)
    {
        /* Handle write error condition */
        perror("ckddasd: write error");

        /* Set unit check with equipment check */
        ckd_build_sense (dev, SENSE_EC, 0, 0,
                        FORMAT_1, MESSAGE_0);
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    /* Set the device orientation fields */
    dev->ckdrem = 0;
    dev->ckdorient = CKDORIENT_DATA;

    return 0;
} /* end function ckd_write_data */


/*-------------------------------------------------------------------*/
/* Execute a Channel Command Word                                    */
/*-------------------------------------------------------------------*/
void ckddasd_execute_ccw ( DEVBLK *dev, BYTE code, BYTE flags,
        BYTE chained, U16 count, BYTE prevcode, int ccwseq,
        BYTE *iobuf, BYTE *more, BYTE *unitstat, U16 *residual )
{
int             rc;                     /* Return code               */
CKDDASD_TRKHDR  trkhdr;                 /* CKD track header (HA)     */
CKDDASD_RECHDR  rechdr;                 /* CKD record header (count) */
int             size;                   /* Number of bytes available */
int             num;                    /* Number of bytes to move   */
int             skiplen;                /* Number of bytes to skip   */
U16             bin;                    /* Bin number                */
U16             cyl;                    /* Cylinder number           */
U16             head;                   /* Head number               */
BYTE            cchhr[5];               /* Search argument           */
BYTE            sector;                 /* Sector number             */
BYTE            key[256];               /* Key for search operations */

    /* If this is a data-chained READ, then return any data remaining
       in the buffer which was not used by the previous CCW */
    if (chained & CCW_FLAGS_CD)
    {
        memmove (iobuf, iobuf + dev->ckdpos, dev->ckdrem);
        num = (count < dev->ckdrem) ? count : dev->ckdrem;
        *residual = count - num;
        if (count < dev->ckdrem) *more = 1;
        dev->ckdrem -= num;
        dev->ckdpos = num;
        *unitstat = CSW_CE | CSW_DE;
        return;
    }

    /* Command reject if data chaining and command is not READ */
    if ((flags & CCW_FLAGS_CD) && code != 0x02
        && (code & 0x7F) != 0x1E && (code & 0x7F) != 0x1A
        && (code & 0x7F) != 0x16 && (code & 0x7F) != 0x12
        && (code & 0x7F) != 0x0E && (code & 0x7F) != 0x06)
    {
        printf("ckddasd: Data chaining not supported for CCW %2.2X\n",
                code);
        ckd_build_sense (dev, SENSE_CR, 0, 0,
                        FORMAT_0, MESSAGE_1);
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return;
    }

    /* Reset flags at start of CCW chain */
    if (chained == 0)
    {
        dev->ckdxtdef = 0;
        dev->ckdsetfm = 0;
        dev->ckdlocat = 0;
        dev->ckdspcnt = 0;
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
        dev->ckdlcount = 0;
    }

    /* Reset index marker flag if sense or control command,
       or any write command (other search ID or search key),
       or any read command except read count or read sector */
    if (IS_CCW_SENSE(code) || IS_CCW_CONTROL(code)
        || (IS_CCW_WRITE(code)
            && (code & 0x7F) != 0x31
            && (code & 0x7F) != 0x51
            && (code & 0x7F) != 0x71
            && (code & 0x7F) != 0x29
            && (code & 0x7F) != 0x49
            && (code & 0x7F) != 0x69)
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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
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
        dev->ckdltranlf = 0;

        /* Seek to start of cylinder zero track zero */
        rc = ckd_seek (dev, 0, 0, &trkhdr, unitstat);
        if (rc < 0) break;

        /* Read count field for first record following R0 */
        rc = ckd_read_count (dev, code, &rechdr, unitstat);
        if (rc < 0) break;

        /* Calculate number of bytes to read and set residual count */
        size = dev->ckdcurdl;
        num = (count < size) ? count : size;
        *residual = count - num;
        if (count < size) *more = 1;

        /* Read data field */
        rc = ckd_read_data (dev, code, iobuf, unitstat);
        if (rc < 0) break;

        /* Set command processed flag */
        dev->ckdrdipl = 1;

        /* Save size and offset of data not used by this CCW */
        dev->ckdrem = size - num;
        dev->ckdpos = num;

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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
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
                ckd_build_sense (dev, SENSE_CR, 0, 0,
                                FORMAT_0, MESSAGE_2);
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
        size = dev->ckdcurdl;
        num = (count < size) ? count : size;
        *residual = count - num;
        if (count < size) *more = 1;

        /* Read data field */
        rc = ckd_read_data (dev, code, iobuf, unitstat);
        if (rc < 0) break;

        /* Save size and offset of data not used by this CCW */
        dev->ckdrem = size - num;
        dev->ckdpos = num;

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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
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
                ckd_build_sense (dev, SENSE_CR, 0, 0,
                                FORMAT_0, MESSAGE_2);
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

        /* Save size and offset of data not used by this CCW */
        dev->ckdrem = size - num;
        dev->ckdpos = num;

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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
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
                ckd_build_sense (dev, SENSE_CR, 0, 0,
                                FORMAT_0, MESSAGE_2);
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
        }

        /* Read next count field */
        rc = ckd_read_count (dev, code, &rechdr, unitstat);
        if (rc < 0) break;

        /* Calculate number of bytes to read and set residual count */
        size = CKDDASD_RECHDR_SIZE;
        num = (count < size) ? count : size;
        *residual = count - num;
        if (count < size) *more = 1;

        /* Copy count field to I/O buffer */
        memcpy (iobuf, &rechdr, CKDDASD_RECHDR_SIZE);

        /* Save size and offset of data not used by this CCW */
        dev->ckdrem = size - num;
        dev->ckdpos = num;

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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
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
                ckd_build_sense (dev, SENSE_CR, 0, 0,
                                FORMAT_0, MESSAGE_2);
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

        /* Save size and offset of data not used by this CCW */
        dev->ckdrem = size - num;
        dev->ckdpos = num;

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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
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
                ckd_build_sense (dev, SENSE_CR, 0, 0,
                                FORMAT_0, MESSAGE_2);
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

        /* Save size and offset of data not used by this CCW */
        dev->ckdrem = size - num;
        dev->ckdpos = num;

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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
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
                ckd_build_sense (dev, SENSE_CR, 0, 0,
                                FORMAT_0, MESSAGE_2);
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

        /* Save size and offset of data not used by this CCW */
        dev->ckdrem = size - num;
        dev->ckdpos = num;

        /* Return unit exception if data length is zero */
        if (dev->ckdcurdl == 0)
            *unitstat = CSW_CE | CSW_DE | CSW_UX;
        else
            *unitstat = CSW_CE | CSW_DE;

        break;

    case 0xDE:
    /*---------------------------------------------------------------*/
    /* READ TRACK                                                    */
    /*---------------------------------------------------------------*/
        /* Command reject if not within the domain of a Locate Record
           that specifies a read tracks operation */
        if (dev->ckdlocat == 0
            || (dev->ckdloper & CKDOPER_CODE) != CKDOPER_RDTRKS)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if not chained from a Locate Record
           command or from another Read Track command */
        if (chained == 0
            || (prevcode != 0x47 && prevcode != 0xDE))
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Advance to next track if chained from previous read track */
        if (prevcode == 0xDE)
        {
            rc = mt_advance (dev, unitstat);
            if (rc < 0) break;
        }

        /* Read each record on the track into the I/O buffer */
        for (size = 0; ; )
        {
            /* Read next count field */
            rc = ckd_read_count (dev, code, &rechdr, unitstat);
            if (rc < 0) break;

            /* Copy count field to I/O buffer */
            memcpy (iobuf + size, &rechdr, CKDDASD_RECHDR_SIZE);
            size += CKDDASD_RECHDR_SIZE;

            /* Exit if end of track marker was read */
            if (memcmp (&rechdr, eighthexFF, 8) == 0)
                break;

            /* Read key field */
            rc = ckd_read_key (dev, code, iobuf + size, unitstat);
            if (rc < 0) break;
            size += dev->ckdcurkl;

            /* Read data field */
            rc = ckd_read_data (dev, code, iobuf + size, unitstat);
            if (rc < 0) break;
            size += dev->ckdcurdl;

        } /* end for(size) */

        /* Set the residual count */
        num = (count < size) ? count : size;
        *residual = count - num;
        if (count < size) *more = 1;

        /* Save size and offset of data not used by this CCW */
        dev->ckdrem = size - num;
        dev->ckdpos = num;

        /* Return normal status */
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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if Seek Head not preceded by a Seek,
           Seek Cylinder, Locate Record, Read IPL, or Recalibrate */
        if (code == 0x1B && dev->ckdseek == 0 && dev->ckdskcyl == 0
            && dev->ckdlocat == 0 && dev->ckdrdipl == 0
            && dev->ckdrecal == 0)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
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
            ckd_build_sense (dev, 0, SENSE1_FP, 0, 0, 0);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Set residual count */
        num = (count < 6) ? count : 6;
        *residual = count - num;

        /* Command reject if count is less than 6 */
        if (count < 6)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_3);
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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* File protected if outside defined extent */
        if (dev->ckdxtdef
            && (cyl < dev->ckdxbcyl || cyl > dev->ckdxecyl
                || (cyl == dev->ckdxbcyl && head < dev->ckdxbhead)
                || (cyl == dev->ckdxecyl && head > dev->ckdxehead)))
        {
            ckd_build_sense (dev, 0, SENSE1_FP, 0, 0, 0);
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

    case 0x13:
    /*---------------------------------------------------------------*/
    /* RECALIBRATE                                                   */
    /*---------------------------------------------------------------*/
        /* Command reject if recalibrate is issued to a 3390 */
        if (dev->devtype == 0x3390)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_1);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* File protected if the file mask does not allow recalibrate,
           or if the file mask specifies diagnostic authorization */
        if ((dev->ckdfmask & CKDMASK_SKCTL) != CKDMASK_SKCTL_ALLSKR
            || (dev->ckdfmask & CKDMASK_AAUTH) == CKDMASK_AAUTH_DIAG)
        {
            ckd_build_sense (dev, 0, SENSE1_FP, 0, 0, 0);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* File protected if cyl 0 head 0 is outside defined extent */
        if (dev->ckdxtdef
            && (dev->ckdxbcyl > 0 || dev->ckdxbhead > 0))
        {
            ckd_build_sense (dev, 0, SENSE1_FP, 0, 0, 0);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Seek to cylinder 0 head 0 */
        rc = ckd_seek (dev, 0, 0, &trkhdr, unitstat);
        if (rc < 0) break;

        /* Set command processed flag */
        dev->ckdrecal = 1;

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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Set residual count */
        num = (count < 1) ? count : 1;
        *residual = count - num;

        /* Command reject if count is less than 1 */
        if (count < 1)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_3);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Extract the file mask from the I/O buffer */
        dev->ckdfmask = iobuf[0];

        /* Command reject if file mask is invalid */
        if ((dev->ckdfmask & CKDMASK_RESV) != 0)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if not preceded by a Seek, Seek Cylinder,
           Locate Record, Read IPL, or Recalibrate command */
        if (dev->ckdseek == 0 && dev->ckdskcyl == 0
            && dev->ckdlocat == 0 && dev->ckdrdipl == 0
            && dev->ckdrecal == 0)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Set residual count */
        num = (count < 1) ? count : 1;
        *residual = count - num;

        /* Command reject if count is less than 1 */
        if (count < 1)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_3);
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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
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

#ifdef CKD_KEY_TRACING
        /* If the search was successful, trace the first 8 bytes of
           the key, which will usually be a dataset name or member
           name and can provide useful debugging information */
        if (*unitstat & CSW_SM)
        {
            BYTE module[8]; int i;
            for (i=0; i < 8; i++)
                module[i] = ebcdic_to_ascii[iobuf[i]];
            printf ("Search key %8.8s\n", module);
        }
#endif /*CKD_KEY_TRACING*/

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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
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
        rc = memcmp(&(trkhdr.cyl), iobuf, num);

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

    case 0x05:
    /*---------------------------------------------------------------*/
    /* WRITE DATA                                                    */
    /*---------------------------------------------------------------*/
        /* Command reject if the current track is in the DSF area */
        if (dev->ckdcurcyl >= dev->ckdcyls)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if not within the domain of a Locate Record
           and not preceded by either a Search ID Equal or Search Key
           Equal that compared equal on all bytes */
           /*INCOMPLETE*/ /*Write CKD allows intervening Read/Write
             key and data commands, Write Data does not!!! Rethink
             the handling of these flags*/
        if (dev->ckdlocat == 0 && dev->ckdideq == 0
            && dev->ckdkyeq == 0)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if file mask inhibits all write commands */
        if ((dev->ckdfmask & CKDMASK_WRCTL) == CKDMASK_WRCTL_INHWRT)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Check operation code if within domain of a Locate Record */
        if (dev->ckdlocat)
        {
            if (!(((dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRITE
                       && dev->ckdlcount ==
                            (dev->ckdlaux & CKDLAUX_RDCNTSUF) ? 2 : 1)
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRTTRK))
            {
                ckd_build_sense (dev, SENSE_CR, 0, 0,
                                FORMAT_0, MESSAGE_2);
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }

            /* If not operating in CKD conversion mode, check that the
               data length is equal to the transfer length factor,
               except when writing a R0 data area under the control
               of a Locate Record Write Track operation, in which
               case a transfer length factor of 8 is used instead */
            if ((dev->ckdxgattr & CKDGATR_CKDCONV) == 0)
            {
                if ((dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRTTRK
                    && dev->ckdcurrec == 0)
                    num = 8;
                else
                    num = dev->ckdltranlf;

                if (dev->ckdcurdl != num)
                {
                    /* Unit check with invalid track format */
                    ckd_build_sense (dev, 0, SENSE1_ITF, 0, 0, 0);
                    *unitstat = CSW_CE | CSW_DE | CSW_UC;
                    break;
                }
            }
        } /* end if(ckdlocat) */

        /* If data length is zero, terminate with unit exception */
        if (dev->ckdcurdl == 0)
        {
            *unitstat = CSW_CE | CSW_DE | CSW_UX;
            break;
        }

        /* Write data */
        rc = ckd_write_data (dev, iobuf, count, unitstat);
        if (rc < 0) break;

        /* Calculate number of bytes written and set residual count */
        size = dev->ckdcurdl;
        num = (count < size) ? count : size;
        *residual = count - num;

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;

        break;

    case 0x0D:
    /*---------------------------------------------------------------*/
    /* WRITE KEY AND DATA                                            */
    /*---------------------------------------------------------------*/
        /* Command reject if the current track is in the DSF area */
        if (dev->ckdcurcyl >= dev->ckdcyls)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if not within the domain of a Locate Record
           and not preceded by a Search ID Equal that compared equal
           on all bytes */
           /*INCOMPLETE*/ /*Write CKD allows intervening Read/Write
             key and data commands, Write Key Data does not!!! Rethink
             the handling of these flags*/
        if (dev->ckdlocat == 0 && dev->ckdideq == 0)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if file mask inhibits all write commands */
        if ((dev->ckdfmask & CKDMASK_WRCTL) == CKDMASK_WRCTL_INHWRT)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Check operation code if within domain of a Locate Record */
        if (dev->ckdlocat)
        {
            if (!(((dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRITE
                       && dev->ckdlcount ==
                            (dev->ckdlaux & CKDLAUX_RDCNTSUF) ? 2 : 1)
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRTTRK))
            {
                ckd_build_sense (dev, SENSE_CR, 0, 0,
                                FORMAT_0, MESSAGE_2);
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }

            /* If not operating in CKD conversion mode, check that the
               key + data length equals the transfer length factor */
            if ((dev->ckdxgattr & CKDGATR_CKDCONV) == 0
                && dev->ckdcurkl + dev->ckdcurdl != dev->ckdltranlf)
            {
                /* Unit check with invalid track format */
                ckd_build_sense (dev, 0, SENSE1_ITF, 0, 0, 0);
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
        } /* end if(ckdlocat) */

        /* If data length is zero, terminate with unit exception */
        if (dev->ckdcurdl == 0)
        {
            *unitstat = CSW_CE | CSW_DE | CSW_UX;
            break;
        }

        /* Write key and data */
        rc = ckd_write_kd (dev, iobuf, count, unitstat);
        if (rc < 0) break;

        /* Calculate number of bytes written and set residual count */
        size = dev->ckdcurkl + dev->ckdcurdl;
        num = (count < size) ? count : size;
        *residual = count - num;

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;

        break;

    case 0x15:
    /*---------------------------------------------------------------*/
    /* WRITE RECORD ZERO                                             */
    /*---------------------------------------------------------------*/
        /* Command reject if the current track is in the DSF area */
        if (dev->ckdcurcyl >= dev->ckdcyls)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if not within the domain of a Locate Record
           and not preceded by either a Search Home Address that
           compared equal on all 4 bytes, or a Write Home Address not
           within the domain of a Locate Record */
        if (dev->ckdlocat == 0 && dev->ckdhaeq == 0)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if file mask does not permit Write R0 */
        if ((dev->ckdfmask & CKDMASK_WRCTL) != CKDMASK_WRCTL_ALLWRT)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
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
                ckd_build_sense (dev, SENSE_CR, 0, 0,
                                FORMAT_0, MESSAGE_2);
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
        }

        /* Write R0 count key and data */
        rc = ckd_write_ckd (dev, iobuf, count, unitstat);
        if (rc < 0) break;

        /* Calculate number of bytes written and set residual count */
        size = CKDDASD_RECHDR_SIZE + dev->ckdcurkl + dev->ckdcurdl;
        num = (count < size) ? count : size;
        *residual = count - num;

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;

        /* Set flag if Write R0 outside domain of a locate record */
        if (dev->ckdlocat == 0)
            dev->ckdwckd = 1;
        else
            dev->ckdwckd = 0;

        break;

    case 0x1D:
    /*---------------------------------------------------------------*/
    /* WRITE COUNT KEY AND DATA                                      */
    /*---------------------------------------------------------------*/
        /* Command reject if the current track is in the DSF area */
        if (dev->ckdcurcyl >= dev->ckdcyls)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if previous command was a Write R0 that
           assigned an alternate track - not implemented */

        /* Command reject if not within the domain of a Locate Record
           and not preceded by either a Search ID Equal or Search Key
           Equal that compared equal on all bytes, or a Write R0 or
           Write CKD not within the domain of a Locate Record */
        if (dev->ckdlocat == 0 && dev->ckdideq == 0
            && dev->ckdkyeq == 0 && dev->ckdwckd == 0)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if file mask does not permit Write CKD */
        if ((dev->ckdfmask & CKDMASK_WRCTL) != CKDMASK_WRCTL_ALLWRT
            && (dev->ckdfmask & CKDMASK_WRCTL) != CKDMASK_WRCTL_INHWR0)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Check operation code if within domain of a Locate Record */
        if (dev->ckdlocat)
        {
            if (!((dev->ckdloper & CKDOPER_CODE) == CKDOPER_FORMAT
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRTTRK))
            {
                ckd_build_sense (dev, SENSE_CR, 0, 0,
                                FORMAT_0, MESSAGE_2);
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
        }

        /* Write count key and data */
        rc = ckd_write_ckd (dev, iobuf, count, unitstat);
        if (rc < 0) break;

        /* Calculate number of bytes written and set residual count */
        size = CKDDASD_RECHDR_SIZE + dev->ckdcurkl + dev->ckdcurdl;
        num = (count < size) ? count : size;
        *residual = count - num;

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;

        /* Set flag if Write CKD outside domain of a locate record */
        if (dev->ckdlocat == 0)
            dev->ckdwckd = 1;
        else
            dev->ckdwckd = 0;

        break;

    case 0x9D:
    /*---------------------------------------------------------------*/
    /* WRITE COUNT KEY AND DATA NEXT TRACK                           */
    /*---------------------------------------------------------------*/
        /* Command reject if not within the domain of a Locate Record
           that specifies a format write operation */
        if (dev->ckdlocat == 0
            || (dev->ckdloper & CKDOPER_CODE) != CKDOPER_FORMAT)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if not chained from a Write CKD or
           another Write CKD Next Track command */
        if (chained == 0
            || (prevcode != 0x1D && prevcode != 0x9D))
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Advance to next track */
        rc = mt_advance (dev, unitstat);
        if (rc < 0) break;

        /* Read the count field for record zero */
        rc = ckd_read_count (dev, code, &rechdr, unitstat);
        if (rc < 0) break;

        /* Write count key and data */
        rc = ckd_write_ckd (dev, iobuf, count, unitstat);
        if (rc < 0) break;

        /* Calculate number of bytes written and set residual count */
        size = CKDDASD_RECHDR_SIZE + dev->ckdcurkl + dev->ckdcurdl;
        num = (count < size) ? count : size;
        *residual = count - num;

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;

        break;

    case 0x47:
    /*---------------------------------------------------------------*/
    /* LOCATE RECORD                                                 */
    /*---------------------------------------------------------------*/
        /* Calculate residual byte count */
        num = (count < 16) ? count : 16;
        *residual = count - num;

        /* Control information length must be at least 16 bytes */
        if (count < 16)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_3);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if within the domain of a Locate Record,
           or not preceded by a Define Extent or Read IPL command */
        if (dev->ckdlocat
            || (dev->ckdxtdef == 0 && dev->ckdrdipl == 0))
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Byte 0 contains the locate record operation byte */
        dev->ckdloper = iobuf[0];

        /* Validate the locate record operation code (bits 2-7) */
        if (!((dev->ckdloper & CKDOPER_CODE) == CKDOPER_ORIENT
              || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRITE
              || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_FORMAT
              || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_RDDATA
              || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRTTRK
              || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_RDTRKS
              || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_READ))
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Check for valid combination of orientation and opcode */
        if (   ((dev->ckdloper & CKDOPER_ORIENTATION)
                                        == CKDOPER_ORIENT_HOME
                && !((dev->ckdloper & CKDOPER_CODE) == CKDOPER_ORIENT
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_FORMAT
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_RDDATA
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_RDTRKS
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_READ))
            ||
               ((dev->ckdloper & CKDOPER_ORIENTATION)
                                        == CKDOPER_ORIENT_DATA
                && !((dev->ckdloper & CKDOPER_CODE) == CKDOPER_ORIENT
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRITE
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_RDDATA
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_READ))
            ||
               ((dev->ckdloper & CKDOPER_ORIENTATION)
                                        == CKDOPER_ORIENT_INDEX
                && !((dev->ckdloper & CKDOPER_CODE) == CKDOPER_FORMAT
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_READ))
            )
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Byte 1 contains the locate record auxiliary byte */
        dev->ckdlaux = iobuf[1];

        /* Validate the auxiliary byte */
        if ((dev->ckdlaux & CKDLAUX_RESV) != 0
            || ((dev->ckdlaux & CKDLAUX_RDCNTSUF)
                && !((dev->ckdloper & CKDOPER_CODE) == CKDOPER_WRITE
                  || (dev->ckdloper & CKDOPER_CODE) == CKDOPER_READ))
            )
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Byte 2 must contain zeroes */
        if (iobuf[2] != 0)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Byte 3 contains the locate record domain count */
        dev->ckdlcount = iobuf[3];

        /* Validate the locate record domain count */
        if (   ((dev->ckdloper & CKDOPER_CODE) == CKDOPER_ORIENT
                && dev->ckdlcount != 0)
            || ((dev->ckdloper & CKDOPER_CODE) != CKDOPER_ORIENT
                && dev->ckdlcount == 0)
            || ((dev->ckdlaux & CKDLAUX_RDCNTSUF)
                && dev->ckdlcount < 2)
            )
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Bytes 4-7 contain the seek address */
        cyl = (iobuf[4] << 8) | iobuf[5];
        head = (iobuf[6] << 8) | iobuf[7];

        /* Command reject if seek address is not valid */
        if (cyl >= dev->ckdcyls || head >= dev->ckdheads)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* File protect error if seek address is outside extent */
        if (cyl < dev->ckdxbcyl || cyl > dev->ckdxecyl
            || (cyl == dev->ckdxbcyl && head < dev->ckdxbhead)
            || (cyl == dev->ckdxecyl && head > dev->ckdxehead))
        {
            ckd_build_sense (dev, 0, SENSE1_FP, 0, 0, 0);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Bytes 8-12 contain the search argument */
        memcpy (cchhr, iobuf+8, 5);

        /* Byte 13 contains the sector number */
        sector = iobuf[13];

        /* Command reject if sector number is not valid */
        if (sector != 0xFF && sector >= dev->ckdsectors)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Bytes 14-15 contain the transfer length factor */
        dev->ckdltranlf = (iobuf[14] << 8) | iobuf[15];

        /* Validate the transfer length factor */
        if (   ((dev->ckdlaux & CKDLAUX_TLFVALID) == 0
                && dev->ckdltranlf != 0)
            || ((dev->ckdlaux & CKDLAUX_TLFVALID)
                && dev->ckdltranlf == 0)
            || dev->ckdltranlf > dev->ckdxblksz)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* If transfer length factor is not supplied then use
           the blocksize from the define extent command */
        if ((dev->ckdlaux & CKDLAUX_TLFVALID) == 0)
            dev->ckdltranlf = dev->ckdxblksz;

        /* Seek to the required track */
        rc = ckd_seek (dev, cyl, head, &trkhdr, unitstat);
        if (rc < 0) break;

        /* Set normal status */
        *unitstat = CSW_CE | CSW_DE;

        /* Perform search according to specified orientation */
        switch ((dev->ckdloper & CKDOPER_ORIENTATION)) {

        case CKDOPER_ORIENT_HOME:
            /* For home orientation, compare the search CCHH
               with the CCHH in the track header */
            if (memcmp (&(trkhdr.cyl), cchhr, 4) != 0)
            {
                ckd_build_sense (dev, 0, SENSE1_NRF, 0, 0, 0);
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
            }
            break;

        case CKDOPER_ORIENT_COUNT:
        case CKDOPER_ORIENT_DATA:
            /* For count or data orientation, search the track
               for a count field matching the specified CCHHR */
            while (1)
            {
                /* Read next count field and exit at end of track
                   with sense data indicating no record found */
                rc = ckd_read_count (dev, code, &rechdr, unitstat);
                if (rc < 0) break;

                /* Compare the count field with the search CCHHR */
                if (memcmp (&rechdr, cchhr, 5) == 0)
                    break;

            } /* end while */

        } /* end switch(CKDOPER_ORIENTATION) */

        /* Exit if search ended with error status */
        if (*unitstat != (CSW_CE | CSW_DE))
            break;

        /* Reorient past data if data orientation is specified */
        if ((dev->ckdloper & CKDOPER_ORIENTATION)
                        == CKDOPER_ORIENT_DATA)
        {
            /* Skip past key and data fields */
            skiplen = dev->ckdcurkl + dev->ckdcurdl;
            if (skiplen > 0)
            {
                rc = ckd_skip (dev, skiplen, unitstat);
                if (rc < 0) break;
            }

            /* Set the device orientation fields */
            dev->ckdrem = 0;
            dev->ckdorient = CKDORIENT_DATA;
        }

        /* Set locate record flag and return normal status */
        dev->ckdlocat = 1;
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
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_3);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if within the domain of a Locate Record, or
           preceded by Define Extent, Space Count, or Set File Mask,
           or (for 3390 only) preceded by Read IPL */
        if (dev->ckdlocat
            || dev->ckdxtdef || dev->ckdspcnt || dev->ckdsetfm
            || (dev->ckdrdipl && dev->devtype == 0x3390))
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Bytes 0-1 contain the file mask and global attributes */
        dev->ckdfmask = iobuf[0];
        dev->ckdxgattr = iobuf[1];

        /* Validate the global attributes byte bits 0-1 */
        if ((dev->ckdxgattr & CKDGATR_ARCH) != CKDGATR_ARCH_ECKD)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Validate the file mask */
        if ((dev->ckdfmask & CKDMASK_RESV) != 0)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Bytes 2-3 contain the extent block size */
        dev->ckdxblksz = (iobuf[2] << 8) | iobuf[3];

        /* If extent block size is zero then use the maximum R0
           record length (as returned in device characteristics
           bytes 44 and 45) plus 8 */
        if (dev->ckdxblksz == 0)
            dev->ckdxblksz = dev->ckdmaxr0len + 8;

        /* Validate the extent block */
        if (dev->ckdxblksz > dev->ckdmaxr0len + 8)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Bytes 4-6 must contain zeroes */
        if (iobuf[4] != 0 || iobuf[5] != 0 || iobuf[6] != 0)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Bytes 8-11 contain the extent begin cylinder and head */
        dev->ckdxbcyl = (iobuf[8] << 8) | iobuf[9];
        dev->ckdxbhead = (iobuf[10] << 8) | iobuf[11];

        /* Bytes 12-15 contain the extent end cylinder and head */
        dev->ckdxecyl = (iobuf[12] << 8) | iobuf[13];
        dev->ckdxehead = (iobuf[14] << 8) | iobuf[15];

        /* Validate the extent description by checking that the
           ending track is not less than the starting track and
           that the extent does not exceed the device size */
        if (dev->ckdxecyl < dev->ckdxbcyl
            || (dev->ckdxecyl == dev->ckdxbcyl
                && dev->ckdxehead < dev->ckdxbhead)
//          || dev->ckdxecyl >= dev->ckdcyls
            || dev->ckdxbhead >= dev->ckdheads
            || dev->ckdxehead >= dev->ckdheads)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Set extent defined flag and return normal status */
        dev->ckdxtdef = 1;
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x64:
    /*---------------------------------------------------------------*/
    /* READ DEVICE CHARACTERISTICS                                   */
    /*---------------------------------------------------------------*/
        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
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

    case 0x5B:
    /*---------------------------------------------------------------*/
    /* SUSPEND MULTIPATH RECONNECTION                                */
    /*---------------------------------------------------------------*/
        /* Command reject if within the domain of a Locate Record
           or if chained from any other CCW */
        if (dev->ckdlocat || chained)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0xF3:
    /*---------------------------------------------------------------*/
    /* DIAGNOSTIC CONTROL                                            */
    /*---------------------------------------------------------------*/
        /* Calculate residual byte count */
        num = (count < 4) ? count : 4;
        *residual = count - num;

        /* Control information length must be at least 4 bytes */
        if (count < 4)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_3);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if byte 0 does not contain a valid
           subcommand code, or if bytes 2-3 are not zero */
        if (!(iobuf[0] == DIAGCTL_MAINT_QUERY
//            || iobuf[0] == DIAGCTL_MAINT_RESERVE
//            || iobuf[0] == DIAGCTL_MAINT_RELEASE
//            || iobuf[0] == DIAGCTL_INHIBIT_WRITE
//            || iobuf[0] == DIAGCTL_SET_GUAR_PATH
//            || iobuf[0] == DIAGCTL_ENABLE_WRITE
//            || iobuf[0] == DIAGCTL_3380_TC_MODE
//            || iobuf[0] == DIAGCTL_INIT_SUBSYS
//            || iobuf[0] == DIAGCTL_UNFENCE
//            || iobuf[0] == DIAGCTL_ACCDEV_UNKCOND
             ) || iobuf[2] != 0 || iobuf[3] != 0)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_4);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Command reject if file mask does not specify
           diagnostic or device support authorization */
        if ((dev->ckdfmask & CKDMASK_AAUTH) == CKDMASK_AAUTH_NORMAL)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_5);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x94:
    /*---------------------------------------------------------------*/
    /* DEVICE RELEASE                                                */
    /*---------------------------------------------------------------*/
        /* Command reject if within the domain of a Locate Record, or
           preceded by Define Extent, Space Count, or Set File Mask,
           or (for 3390 only) preceded by Read IPL */
        if (dev->ckdlocat
            || dev->ckdxtdef || dev->ckdspcnt || dev->ckdsetfm
            || (dev->ckdrdipl && dev->devtype == 0x3390))
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Perform the operation of a sense command */
        goto sense;

    case 0x14: /* UNCONDITIONAL RESERVE */
    case 0xB4: /* DEVICE RESERVE */
    /*---------------------------------------------------------------*/
    /* DEVICE RESERVE                                                */
    /*---------------------------------------------------------------*/
        /* Command reject if within the domain of a Locate Record,
           or indeed if preceded by any command at all apart from
           Suspend Multipath Reconnection */
        if (dev->ckdlocat
            || ccwseq > 1
            || (chained && prevcode != 0x5B))
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Perform the operation of a sense command */
        goto sense;

    case 0x04:
    /*---------------------------------------------------------------*/
    /* SENSE                                                         */
    /*---------------------------------------------------------------*/
        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

    sense:
        /* Calculate residual byte count */
        num = (count < dev->numsense) ? count : dev->numsense;
        *residual = count - num;
        if (count < dev->numsense) *more = 1;

        /* Copy device sense bytes to channel I/O buffer */
        memcpy (iobuf, dev->sense, num);

        /* Clear the device sense bytes */
        memset (dev->sense, 0, sizeof(dev->sense));

        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0xE4:
    /*---------------------------------------------------------------*/
    /* SENSE ID                                                      */
    /*---------------------------------------------------------------*/
        /* Command reject if within the domain of a Locate Record */
        if (dev->ckdlocat)
        {
            ckd_build_sense (dev, SENSE_CR, 0, 0,
                            FORMAT_0, MESSAGE_2);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

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
        ckd_build_sense (dev, SENSE_CR, 0, 0,
                        FORMAT_0, MESSAGE_1);
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

    /* If within the domain of a locate record then decrement the
       count of CCWs remaining to be processed within the domain */
    if (dev->ckdlocat && code != 0x047)
    {
        if (dev->ckdlcount > 0)
            dev->ckdlcount--;

        /* Reset flag if no more CCWs in the domain */
        if (dev->ckdlcount == 0)
            dev->ckdlocat = 0;

        /* Command reject with incomplete domain if CCWs remain
           but command chaining is not specified */
        if (dev->ckdlcount > 0 && (flags & CCW_FLAGS_CC) == 0)
        {
            ckd_build_sense (dev, SENSE_CR | SENSE_OC, 0, 0, 0, 0);
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
        }
    } /* end if(ckdlocat) */

} /* end function ckddasd_execute_ccw */

