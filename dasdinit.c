/* DASDINIT.C   (c) Copyright Roger Bowler, 1999                     */
/*              Hercules DASD Image Builder                          */

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

/*-------------------------------------------------------------------*/
/* Static data areas                                                 */
/*-------------------------------------------------------------------*/
BYTE eighthexFF[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/*-------------------------------------------------------------------*/
/* ASCII/EBCDIC translate table                                      */
/*-------------------------------------------------------------------*/
unsigned char
ascii_to_ebcdic[] = {
"\x00\x01\x02\x03\x37\x2D\x2E\x2F\x16\x05\x25\x0B\x0C\x0D\x0E\x0F"
"\x10\x11\x12\x13\x3C\x3D\x32\x26\x18\x19\x1A\x27\x22\x1D\x35\x1F"
"\x40\x5A\x7F\x7B\x5B\x6C\x50\x7D\x4D\x5D\x5C\x4E\x6B\x60\x4B\x61"
"\xF0\xF1\xF2\xF3\xF4\xF5\xF6\xF7\xF8\xF9\x7A\x5E\x4C\x7E\x6E\x6F"
"\x7C\xC1\xC2\xC3\xC4\xC5\xC6\xC7\xC8\xC9\xD1\xD2\xD3\xD4\xD5\xD6"
"\xD7\xD8\xD9\xE2\xE3\xE4\xE5\xE6\xE7\xE8\xE9\xAD\xE0\xBD\x5F\x6D"
"\x79\x81\x82\x83\x84\x85\x86\x87\x88\x89\x91\x92\x93\x94\x95\x96"
"\x97\x98\x99\xA2\xA3\xA4\xA5\xA6\xA7\xA8\xA9\xC0\x6A\xD0\xA1\x07"
"\x68\xDC\x51\x42\x43\x44\x47\x48\x52\x53\x54\x57\x56\x58\x63\x67"
"\x71\x9C\x9E\xCB\xCC\xCD\xDB\xDD\xDF\xEC\xFC\xB0\xB1\xB2\xB3\xB4"
"\x45\x55\xCE\xDE\x49\x69\x04\x06\xAB\x08\xBA\xB8\xB7\xAA\x8A\x8B"
"\x09\x0A\x14\xBB\x15\xB5\xB6\x17\x1B\xB9\x1C\x1E\xBC\x20\xBE\xBF"
"\x21\x23\x24\x28\x29\x2A\x2B\x2C\x30\x31\xCA\x33\x34\x36\x38\xCF"
"\x39\x3A\x3B\x3E\x41\x46\x4A\x4F\x59\x62\xDA\x64\x65\x66\x70\x72"
"\x73\xE1\x74\x75\x76\x77\x78\x80\x8C\x8D\x8E\xEB\x8F\xED\xEE\xEF"
"\x90\x9A\x9B\x9D\x9F\xA0\xAC\xAE\xAF\xFD\xFE\xFB\x3F\xEA\xFA\xFF"
        };


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
/* Subroutine to convert a null-terminated string to upper case      */
/*-------------------------------------------------------------------*/
static void
string_to_upper (BYTE *source)
{
int     i;                              /* Array subscript           */

    for (i = 0; source[i] != '\0'; i++)
        source[i] = toupper(source[i]);

} /* end function string_to_upper */

/*-------------------------------------------------------------------*/
/* Subroutine to convert a string to EBCDIC and pad with blanks      */
/*-------------------------------------------------------------------*/
static void
convert_to_ebcdic (BYTE *dest, int len, BYTE *source)
{
int     i;                              /* Array subscript           */

    for (i = 0; i < len && source[i] != '\0'; i++)
        dest[i] = ascii_to_ebcdic[source[i]];

    while (i < len)
        dest[i++] = 0x40;

} /* end function convert_to_ebcdic */

/*-------------------------------------------------------------------*/
/* Subroutine to create a CKD DASD image file                        */
/* Input:                                                            */
/*      fname   DASD image file name                                 */
/*      fd      DASD image file descriptor                           */
/*      devtype Device type                                          */
/*      heads   Number of heads per cylinder                         */
/*      trksize Length of a track on DASD image file                 */
/*      cyls    Number of cylinders                                  */
/*      volser  Volume serial number                                 */
/*-------------------------------------------------------------------*/
static void
create_ckd (BYTE *fname, int fd, U16 devtype, U32 heads,
            U32 trksize, U32 cyls, BYTE *volser)
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
int             ipl2len = 4096;         /* Length of IPL2 data       */
int             vol1len = 80;           /* Length of VOL1 data       */
int             rec0len = 8;            /* Length of R0 data         */
U16             vtoccyl = 0;            /* VTOC cylinder number      */
U16             vtochead = 1;           /* VTOC head number          */
BYTE            vtocrec = 1;            /* VTOC record number        */

    /* Compute minimum and maximum number of cylinders */
    cylsize = trksize * heads;
    mincyls = 1;
    maxcyls = 0x80000000 / cylsize;
    if (maxcyls > 65536) maxcyls = 65536;

    /* Check for valid number of cylinders */
    if (cyls < mincyls || cyls > maxcyls)
    {
        fprintf (stderr,
                "Cylinder count %lu is outside range %lu-%lu\n",
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
            "Creating %4.4X volume %s: %lu cyls, "
            "%lu trks/cyl, %lu bytes/track\n",
            devtype, volser, cyls, heads, trksize);

    /* Create the device header */
    memset(&devhdr, 0, CKDDASD_DEVHDR_SIZE);
    memcpy(devhdr.devid, "CKD_P370", 8);
    devhdr.heads = heads;
    devhdr.trksize = trksize;

    /* Write the device header */
    rc = write (fd, &devhdr, CKDDASD_DEVHDR_SIZE);
    if (rc < CKDDASD_DEVHDR_SIZE)
    {
        fprintf (stderr, "%s device header write error: %s\n",
                fname, strerror(errno));
        exit(1);
    }

    /* Write each cylinder */
    for (cyl = 0; cyl < cyls; cyl++)
    {
        /* Display progress message every 10 cylinders */
        if ((cyl % 10) == 0)
            fprintf (stderr, "Writing cylinder %lu\r", cyl);

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
                pos[10] = 0x40;
                pos[11] = (vtoccyl >> 8) & 0xFF;
                pos[12] = vtoccyl & 0xFF;
                pos[13] = (vtochead >> 8) & 0xFF;
                pos[14] = vtochead & 0xFF;
                pos[15] = vtocrec;
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
                        "%s cylinder %lu head %lu write error: %s\n",
                        fname, cyl, head, strerror(errno));
                exit(1);
            }

        } /* end for(head) */

    } /* end for(cyl) */

    /* Release data buffer */
    free (buf);

    /* Display completion message */
    fprintf (stderr, "%lu cylinders successfully written\n", cyl);

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
                "Sector count %lu is outside range %lu-%lu\n",
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
            "%lu sectors, %lu bytes/sector\n",
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
            fprintf (stderr, "Writing sector %lu\r", sectnum);

        /* Write the sector to the file */
        rc = write (fd, buf, sectsz);
        if (rc < sectsz)
        {
            fprintf (stderr, "%s sector %lu write error: %s\n",
                    fname, sectnum, strerror(errno));
            exit(1);
        }
    } /* end for(sectnum) */

    /* Release data buffer */
    free (buf);

    /* Display completion message */
    fprintf (stderr, "%lu sectors successfully written\n", sectnum);

} /* end function create_fba */

/*-------------------------------------------------------------------*/
/* DASDINIT program main entry point                                 */
/*-------------------------------------------------------------------*/
int main ( int argc, char *argv[] )
{
int     fd;                             /* File descriptor           */
U32     size;                           /* Volume size               */
U32     heads = 0;                      /* Number of tracks/cylinder */
U32     trksize = 0;                    /* Track size                */
U32     sectsize = 0;                   /* Sector size               */
U32     trkovhd;                        /* CKD track overhead        */
U16     devtype;                        /* Device type               */
BYTE    type;                           /* C=CKD, F=FBA              */
BYTE    fname[256];                     /* File name                 */
BYTE    volser[7];                      /* Volume serial number      */
BYTE    c;                              /* Character work area       */

    /* Display the program identification message */
    fprintf (stderr,
            "Hercules DASD image file creation program %s "
            "(c)Copyright Roger Bowler, 1999\n",
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
        || sscanf(argv[4], "%lu%c", &size, &c) != 1)
        argexit(4);

    /* Calculate the track overhead */
    trkovhd = 140;

    /* Check the device type */
    switch (devtype) {

    case 0x2311:
        type = 'C';
        heads = 10;
        trksize = 3625 + trkovhd;
        break;

    case 0x2314:
        type = 'C';
        heads = 20;
        trksize = 7294 + trkovhd;
        break;

    case 0x3330:
        type = 'C';
        heads = 19;
        trksize = 13030 + trkovhd;
        break;

    case 0x3350:
        type = 'C';
        heads = 30;
        trksize = 19069 + trkovhd;
        break;

    case 0x3380:
        type = 'C';
        heads = 15;
        trksize = 47476 + trkovhd;
        break;

    case 0x3390:
        type = 'C';
        heads = 15;
        trksize = 56664 + trkovhd;
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

    /* Create a new file, with error if file already exists */
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
        create_ckd (fname, fd, devtype, heads, trksize, size, volser);
    else
        create_fba (fname, fd, devtype, sectsize, size, volser);

    /* Display completion message */
    fprintf (stderr, "DASD initialization successfully completed.\n");
    return 0;

} /* end function main */


