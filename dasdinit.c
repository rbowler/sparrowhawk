/* DASDINIT.C   (c) Copyright Roger Bowler, 1999-2000                */
/*              Hercules DASD Utilities: DASD image builder          */

/*-------------------------------------------------------------------*/
/* This program creates a disk image file and initializes it as      */
/* a blank FBA or CKD DASD volume.                                   */
/*                                                                   */
/* The program is invoked from the shell prompt using the command:   */
/*                                                                   */
/*      dasdinit filename devtype volser size                        */
/*                                                                   */
/* filename     is the name of the disk image file to be created     */
/*              (this program will not overwrite an existing file)   */
/* devtype      is the emulated device type.                         */
/*              CKD: 2311, 2314, 3330, 3350, 3380, 3390              */
/*              FBA: 3310, 3370                                      */
/* volser       is the volume serial number (1-6 characters)         */
/* size         is the size of the device (in cylinders for CKD      */
/*              devices, or in 512-byte sectors for FBA devices)     */
/*-------------------------------------------------------------------*/

#include "hercules.h"
#include "dasdblks.h"

/*-------------------------------------------------------------------*/
/* Static data areas                                                 */
/*-------------------------------------------------------------------*/
BYTE eighthexFF[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
BYTE iplpsw[8]    = {0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F};
BYTE iplccw1[8]   = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
BYTE iplccw2[8]   = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};


/*-------------------------------------------------------------------*/
/* Subroutine to display command syntax and exit                     */
/*-------------------------------------------------------------------*/
static void
argexit ( int code )
{
    fprintf (stderr,
            "Syntax:\tdasdinit filename devtype volser size\n"
            "where:\tfilename = name of file to be created\n"
            "\tdevtype  = 2311, 2314, 3330, 3350, 3380, 3390 (CKD)\n"
            "\t           3310, 3370 (FBA)\n"
            "\tvolser   = volume serial number (1-6 characters)\n"
            "\tsize     = volume size in cylinders (CKD devices)\n"
            "\t           or in 512-byte sectors (FBA devices)\n");
    exit(code);
} /* end function argexit */

/*-------------------------------------------------------------------*/
/* Subroutine to create a CKD DASD image file                        */
/* Input:                                                            */
/*      fname   DASD image file name                                 */
/*      fd      DASD image file descriptor                           */
/*      devtype Device type                                          */
/*      heads   Number of heads per cylinder                         */
/*      maxdlen Maximum R1 record data length                        */
/*      cyls    Number of cylinders                                  */
/*      volser  Volume serial number                                 */
/*-------------------------------------------------------------------*/
static void
create_ckd (BYTE *fname, int fd, U16 devtype, U32 heads,
            U32 maxdlen, U32 cyls, BYTE *volser)
{
int             rc;                     /* Return code               */
CKDDASD_DEVHDR  devhdr;                 /* Device header             */
CKDDASD_TRKHDR *trkhdr;                 /* -> Track header           */
CKDDASD_RECHDR *rechdr;                 /* -> Record header          */
U32             cyl;                    /* Cylinder number           */
U32             head;                   /* Head number               */
U32             cylsize;                /* Cylinder size in bytes    */
BYTE           *buf;                    /* -> Track data buffer      */
BYTE           *pos;                    /* -> Next position in buffer*/
U32             mincyls;                /* Minimum cylinder count    */
U32             maxcyls;                /* Maximum cylinder count    */
int             keylen = 4;             /* Length of keys            */
int             ipl1len = 24;           /* Length of IPL1 data       */
int             ipl2len = 144;          /* Length of IPL2 data       */
int             vol1len = 80;           /* Length of VOL1 data       */
int             rec0len = 8;            /* Length of R0 data         */
U32             trksize;                /* DASD image track length   */

    /* Compute the DASD image track length */
    trksize = sizeof(CKDDASD_TRKHDR)
                + sizeof(CKDDASD_RECHDR) + rec0len
                + sizeof(CKDDASD_RECHDR) + maxdlen
                + sizeof(eighthexFF);
    trksize = ROUND_UP(trksize,512);

    /* Compute minimum and maximum number of cylinders */
    cylsize = trksize * heads;
    mincyls = 1;
    maxcyls = 0x80000000 / cylsize;
    if (maxcyls > 65536) maxcyls = 65536;

    /* Check for valid number of cylinders */
    if (cyls < mincyls || cyls > maxcyls)
    {
        fprintf (stderr,
                "Cylinder count %u is outside range %u-%u\n",
                cyls, mincyls, maxcyls);
        exit(4);
    }

    /* Obtain track data buffer */
    buf = malloc(trksize);
    if (buf == NULL)
    {
        fprintf (stderr, "Cannot obtain track buffer: %s\n",
                strerror(errno));
        exit(6);
    }

    /* Display progress message */
    fprintf (stderr,
            "Creating %4.4X volume %s: %u cyls, "
            "%u trks/cyl, %u bytes/track\n",
            devtype, volser, cyls, heads, trksize);

    /* Create the device header */
    memset(&devhdr, 0, CKDDASD_DEVHDR_SIZE);
    memcpy(devhdr.devid, "CKD_P370", 8);
    devhdr.heads[3] = (heads >> 24) & 0xFF;
    devhdr.heads[2] = (heads >> 16) & 0xFF;
    devhdr.heads[1] = (heads >> 8) & 0xFF;
    devhdr.heads[0] = heads & 0xFF;
    devhdr.trksize[3] = (trksize >> 24) & 0xFF;
    devhdr.trksize[2] = (trksize >> 16) & 0xFF;
    devhdr.trksize[1] = (trksize >> 8) & 0xFF;
    devhdr.trksize[0] = trksize & 0xFF;
    devhdr.devtype = devtype & 0xFF;
    devhdr.fileseq = 0;
    devhdr.highcyl[1] = 0;
    devhdr.highcyl[0] = 0;

    /* Write the device header */
    rc = write (fd, &devhdr, CKDDASD_DEVHDR_SIZE);
    if (rc < CKDDASD_DEVHDR_SIZE)
    {
        fprintf (stderr, "%s device header write error: %s\n",
                fname, errno ? strerror(errno) : "incomplete");
        exit(1);
    }

    /* Write each cylinder */
    for (cyl = 0; cyl < cyls; cyl++)
    {
        /* Display progress message every 10 cylinders */
        if ((cyl % 10) == 0)
            fprintf (stderr, "Writing cylinder %u\r", cyl);

        for (head = 0; head < heads; head++)
        {
            /* Clear the track to zeroes */
            memset (buf, 0, trksize);

            /* Build the track header */
            trkhdr = (CKDDASD_TRKHDR*)buf;
            trkhdr->bin = 0;
            trkhdr->cyl[0] = (cyl >> 8) & 0xFF;
            trkhdr->cyl[1] = cyl & 0xFF;
            trkhdr->head[0] = (head >> 8) & 0xFF;
            trkhdr->head[1] = head & 0xFF;
            pos = buf + CKDDASD_TRKHDR_SIZE;

            /* Build record zero */
            rechdr = (CKDDASD_RECHDR*)pos;
            pos += CKDDASD_RECHDR_SIZE;
            rechdr->cyl[0] = (cyl >> 8) & 0xFF;
            rechdr->cyl[1] = cyl & 0xFF;
            rechdr->head[0] = (head >> 8) & 0xFF;
            rechdr->head[1] = head & 0xFF;
            rechdr->rec = 0;
            rechdr->klen = 0;
            rechdr->dlen[0] = (rec0len >> 8) & 0xFF;
            rechdr->dlen[1] = rec0len & 0xFF;
            pos += rec0len;

            /* Cyl 0 head 0 contains IPL records and volume label */
            if (cyl == 0 && head == 0)
            {
                /* Build the IPL1 record */
                rechdr = (CKDDASD_RECHDR*)pos;
                pos += CKDDASD_RECHDR_SIZE;
                rechdr->cyl[0] = (cyl >> 8) & 0xFF;
                rechdr->cyl[1] = cyl & 0xFF;
                rechdr->head[0] = (head >> 8) & 0xFF;
                rechdr->head[1] = head & 0xFF;
                rechdr->rec = 1;
                rechdr->klen = keylen;
                rechdr->dlen[0] = (ipl1len >> 8) & 0xFF;
                rechdr->dlen[1] = ipl1len & 0xFF;
                convert_to_ebcdic (pos, keylen, "IPL1");
                pos += keylen;
                memcpy (pos, iplpsw, 8);
                memcpy (pos+8, iplccw1, 8);
                memcpy (pos+16, iplccw2, 8);
                pos += ipl1len;

                /* Build the IPL2 record */
                rechdr = (CKDDASD_RECHDR*)pos;
                pos += CKDDASD_RECHDR_SIZE;
                rechdr->cyl[0] = (cyl >> 8) & 0xFF;
                rechdr->cyl[1] = cyl & 0xFF;
                rechdr->head[0] = (head >> 8) & 0xFF;
                rechdr->head[1] = head & 0xFF;
                rechdr->rec = 2;
                rechdr->klen = keylen;
                rechdr->dlen[0] = (ipl2len >> 8) & 0xFF;
                rechdr->dlen[1] = ipl2len & 0xFF;
                convert_to_ebcdic (pos, keylen, "IPL2");
                pos += keylen;
                pos += ipl2len;

                /* Build the VOL1 record */
                rechdr = (CKDDASD_RECHDR*)pos;
                pos += CKDDASD_RECHDR_SIZE;
                rechdr->cyl[0] = (cyl >> 8) & 0xFF;
                rechdr->cyl[1] = cyl & 0xFF;
                rechdr->head[0] = (head >> 8) & 0xFF;
                rechdr->head[1] = head & 0xFF;
                rechdr->rec = 3;
                rechdr->klen = keylen;
                rechdr->dlen[0] = (vol1len >> 8) & 0xFF;
                rechdr->dlen[1] = vol1len & 0xFF;
                convert_to_ebcdic (pos, keylen, "VOL1");
                pos += keylen;
                convert_to_ebcdic (pos, 4, "VOL1");
                convert_to_ebcdic (pos+4, 6, volser);
                convert_to_ebcdic (pos+37, 14, "HERCULES");
                pos += vol1len;

            } /* end if(cyl==0 && head==0) */

            /* Build the end of track marker */
            memcpy (pos, eighthexFF, 8);

            /* Write the track to the file */
            rc = write (fd, buf, trksize);
            if (rc < trksize)
            {
                fprintf (stderr,
                        "%s cylinder %u head %u write error: %s\n",
                        fname, cyl, head,
                        errno ? strerror(errno) : "incomplete");
                exit(1);
            }

        } /* end for(head) */

    } /* end for(cyl) */

    /* Release data buffer */
    free (buf);

    /* Display completion message */
    fprintf (stderr, "%u cylinders successfully written\n", cyl);

} /* end function create_ckd */

/*-------------------------------------------------------------------*/
/* Subroutine to create an FBA DASD image file                       */
/* Input:                                                            */
/*      fname   DASD image file name                                 */
/*      fd      DASD image file descriptor                           */
/*      devtype Device type                                          */
/*      sectsz  Sector size                                          */
/*      sectors Number of sectors                                    */
/*      volser  Volume serial number                                 */
/*-------------------------------------------------------------------*/
static void
create_fba (BYTE *fname, int fd, U16 devtype,
            U32 sectsz, U32 sectors, BYTE *volser)
{
int             rc;                     /* Return code               */
U32             sectnum;                /* Sector number             */
BYTE           *buf;                    /* -> Sector data buffer     */
U32             minsect;                /* Minimum sector count      */
U32             maxsect;                /* Maximum sector count      */

    /* Compute minimum and maximum number of sectors */
    minsect = 64;
    maxsect = 0x80000000 / sectsz;

    /* Check for valid number of sectors */
    if (sectors < minsect || sectors > maxsect)
    {
        fprintf (stderr,
                "Sector count %u is outside range %u-%u\n",
                sectors, minsect, maxsect);
        exit(4);
    }

    /* Obtain sector data buffer */
    buf = malloc(sectsz);
    if (buf == NULL)
    {
        fprintf (stderr, "Cannot obtain sector buffer: %s\n",
                strerror(errno));
        exit(6);
    }

    /* Display progress message */
    fprintf (stderr,
            "Creating %4.4X volume %s: "
            "%u sectors, %u bytes/sector\n",
            devtype, volser, sectors, sectsz);

    /* Write each sector */
    for (sectnum = 0; sectnum < sectors; sectnum++)
    {
        /* Clear the sector to zeroes */
        memset (buf, 0, sectsz);

        /* Sector 1 contains the volume label */
        if (sectnum == 1)
        {
            convert_to_ebcdic (buf, 4, "VOL1");
            convert_to_ebcdic (buf+4, 6, volser);
        } /* end if(sectnum==1) */

        /* Display progress message every 100 sectors */
        if ((sectnum % 100) == 0)
            fprintf (stderr, "Writing sector %u\r", sectnum);

        /* Write the sector to the file */
        rc = write (fd, buf, sectsz);
        if (rc < sectsz)
        {
            fprintf (stderr, "%s sector %u write error: %s\n",
                    fname, sectnum,
                    errno ? strerror(errno) : "incomplete");
            exit(1);
        }
    } /* end for(sectnum) */

    /* Release data buffer */
    free (buf);

    /* Display completion message */
    fprintf (stderr, "%u sectors successfully written\n", sectnum);

} /* end function create_fba */

/*-------------------------------------------------------------------*/
/* DASDINIT program main entry point                                 */
/*-------------------------------------------------------------------*/
int main ( int argc, char *argv[] )
{
int     fd;                             /* File descriptor           */
U32     size;                           /* Volume size               */
U32     heads = 0;                      /* Number of tracks/cylinder */
U32     maxdlen = 0;                    /* Maximum R1 data length    */
U32     sectsize = 0;                   /* Sector size               */
U16     devtype;                        /* Device type               */
BYTE    type;                           /* C=CKD, F=FBA              */
BYTE    fname[256];                     /* File name                 */
BYTE    volser[7];                      /* Volume serial number      */
BYTE    c;                              /* Character work area       */

    /* Display the program identification message */
    fprintf (stderr,
            "Hercules DASD image file creation program %s "
            "(c)Copyright Roger Bowler, 1999-2000\n",
            MSTRING(VERSION));

    /* Check the number of arguments */
    if (argc != 5)
        argexit(5);

    /* The first argument is the file name */
    if (argv[1] == NULL || strlen(argv[1]) == 0
        || strlen(argv[1]) > sizeof(fname)-1)
        argexit(1);

    strcpy (fname, argv[1]);

    /* The second argument is the device type */
    if (argv[2] == NULL || strlen(argv[2]) != 4
        || sscanf(argv[2], "%hx%c", &devtype, &c) != 1)
        argexit(2);

    /* The third argument is the volume serial number */
    if (argv[3] == NULL || strlen(argv[3]) == 0
        || strlen(argv[3]) > sizeof(volser)-1)
        argexit(3);

    strcpy (volser, argv[3]);
    string_to_upper (volser);

    /* The fourth argument is the volume size */
    if (argv[4] == NULL || strlen(argv[4]) == 0
        || sscanf(argv[4], "%u%c", &size, &c) != 1)
        argexit(4);

    /* Check the device type */
    switch (devtype) {

    case 0x2311:
        type = 'C';
        heads = 10;
        maxdlen = 3625;
        break;

    case 0x2314:
        type = 'C';
        heads = 20;
        maxdlen = 7294;
        break;

    case 0x3330:
        type = 'C';
        heads = 19;
        maxdlen = 13030;
        break;

    case 0x3350:
        type = 'C';
        heads = 30;
        maxdlen = 19069;
        break;

    case 0x3380:
        type = 'C';
        heads = 15;
        maxdlen = 47476;
        break;

    case 0x3390:
        type = 'C';
        heads = 15;
        maxdlen = 56664;
        break;

    case 0x3310:
        type = 'F';
        sectsize = 512;
        break;

    case 0x3370:
        type = 'F';
        sectsize = 512;
        break;

    default:
        type = '?';
        fprintf (stderr, "Unknown device type: %4.4X\n", devtype);
        exit(3);

    } /* end switch(devtype) */

    /* Create the DASD image file */
    fd = open (fname, O_WRONLY | O_CREAT | O_EXCL,
                S_IRUSR | S_IWUSR | S_IRGRP);
    if (fd < 0)
    {
        fprintf (stderr, "%s open error: %s\n",
                fname, strerror(errno));
        exit(1);
    }

    /* Create the device */
    if (type == 'C')
        create_ckd (fname, fd, devtype, heads, maxdlen, size, volser);
    else
        create_fba (fname, fd, devtype, sectsize, size, volser);

    /* Display completion message */
    fprintf (stderr, "DASD initialization successfully completed.\n");
    return 0;

} /* end function main */

