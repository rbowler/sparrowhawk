/* DASDLOAD.C   (c) Copyright Roger Bowler, 1999                     */
/*              Hercules DASD Utilities: DASD image loader           */

/*-------------------------------------------------------------------*/
/* This program creates a virtual DASD volume from a list of         */
/* datasets previously unloaded using the TSO XMIT command.          */
/*-------------------------------------------------------------------*/

#include "hercules.h"
#include "dasdblks.h"

/*-------------------------------------------------------------------*/
/* Internal macro definitions                                        */
/*-------------------------------------------------------------------*/
#define SPACE           ((BYTE)' ')
#define CASERET(s)      case s: return (#s)
#define XMINF(lvl,format) \
        ((infolvl<lvl)?0:fprintf(stdout, format))
#define XMINFF(lvl,format,a...) \
        ((infolvl<lvl)?0:fprintf(stdout, format, ## a))
#define XMERR(format) \
        fprintf(stdout, "Error: " format)
#define XMERRF(format,a...) \
        fprintf(stdout, "Error: " format, ## a)

#define R0_DATALEN      8
#define IPL1_KEYLEN     4
#define IPL1_DATALEN    24
#define IPL2_KEYLEN     4
#define IPL2_DATALEN    144
#define VOL1_KEYLEN     4
#define VOL1_DATALEN    80

#define EBCDIC_END      "\xC5\xD5\xC4"
#define EBCDIC_TXT      "\xE3\xE7\xE3"

/*-------------------------------------------------------------------*/
/* Definition of LOGREC header record                                */
/*-------------------------------------------------------------------*/
typedef struct _DIPHDR {
        HWORD   recid;                  /* Record identifier (0xFFFF)*/
        HWORD   bcyl;                   /* Extent begin cylinder     */
        HWORD   btrk;                   /* Extent begin track        */
        HWORD   ecyl;                   /* Extent end cylinder       */
        HWORD   etrk;                   /* Extent end track          */
        BYTE    resv;                   /* Unused                    */
        BYTE    restart[7];             /* Restart area BBCCHHR      */
        HWORD   trkbal;                 /* Bytes remaining on track  */
        HWORD   trklen;                 /* Total bytes on track      */
        BYTE    reused[7];              /* Last reused BBCCHHR       */
        HWORD   lasthead;               /* Last track on cylinder    */
        HWORD   trklen90;               /* 90% of track length       */
        BYTE    devcode;                /* Device type code          */
        BYTE    cchh90[4];              /* 90% full track CCHH       */
        BYTE    switches;               /* Switches                  */
        BYTE    endid;                  /* Check byte (0xFF)         */
    } DIPHDR;

/*-------------------------------------------------------------------*/
/* Definition of internal extent descriptor array entry              */
/*-------------------------------------------------------------------*/
typedef struct _EXTDESC {
        U16     bcyl;                   /* Begin cylinder            */
        U16     btrk;                   /* Begin track               */
        U16     ecyl;                   /* End cylinder              */
        U16     etrk;                   /* End track                 */
        U16     ntrk;                   /* Number of tracks          */
    } EXTDESC;

/*-------------------------------------------------------------------*/
/* Definition of internal TTR conversion table array entry           */
/*-------------------------------------------------------------------*/
typedef struct _TTRCONV {
        BYTE    origttr[3];             /* TTR in original dataset   */
        BYTE    outpttr[3];             /* TTR in output dataset     */
    } TTRCONV;

/*-------------------------------------------------------------------*/
/* Definitions for dataset initialization methods                    */
/*-------------------------------------------------------------------*/
#define METHOD_EMPTY    0
#define METHOD_XMIT     1
#define METHOD_DIP      2
#define METHOD_CVOL     3

/*-------------------------------------------------------------------*/
/* Static data areas                                                 */
/*-------------------------------------------------------------------*/
BYTE eighthexFF[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
BYTE twelvehex00[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
BYTE cvol_low_key[] = {0, 0, 0, 0, 0, 0, 0, 1};
BYTE iplpsw[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
BYTE iplccw1[8] = {0x06, 0x00, 0x3A, 0x98, 0x60, 0x00, 0x00, 0x60};
BYTE iplccw2[8] = {0x08, 0x00, 0x3A, 0x98, 0x00, 0x00, 0x00, 0x00};
BYTE ipl2data[] = {0x07, 0x00, 0x3A, 0xB8, 0x40, 0x00, 0x00, 0x06,
                   0x31, 0x00, 0x3A, 0xBE, 0x40, 0x00, 0x00, 0x05,
                   0x08, 0x00, 0x3A, 0xA0, 0x00, 0x00, 0x00, 0x00,
                   0x06, 0x00, 0x00, 0x00, 0x20, 0x00, 0x19, 0x60,
                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /*BBCCHH*/
                   0x00, 0x00, 0x00, 0x00, 0x04}; /*CCHHR*/
BYTE noiplpsw[8] = {0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F};
BYTE noiplccw1[8] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
BYTE noiplccw2[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* Information message level: 0=None, 1=File name, 2=File information,
   3=Member information, 4=Text units, record headers, 5=Dump data */
int  infolvl = 1;

/*-------------------------------------------------------------------*/
/* Subroutine to display command syntax and exit                     */
/*-------------------------------------------------------------------*/
static void
argexit ( int code )
{
    fprintf (stderr,
            "dasdload creates a DASD image file from a list "
            "of TSO XMIT files\n"
            "Syntax:\tdasdload ctlfile outfile devtype [msglevel]\n"
            "where:\tctlfile  = name of input control file\n"
            "\toutfile  = name of DASD image file to be created\n"
            "\tdevtype  = 2311, 2314, 3330, 3350, 3380, 3390\n"
            "\tmsglevel = Value 0-5 controls output verbosity\n");
    exit(code);
} /* end function argexit */

/*-------------------------------------------------------------------*/
/* Subroutine to load a S/390 integer value from a buffer            */
/*-------------------------------------------------------------------*/
static int
make_int (BYTE *src, int srclen)
{
int             result = 0;             /* Result accumulator        */
int             i;                      /* Array subscript           */

    for (i=0; i < srclen; i++)
    {
        result <<= 8;
        result |= src[i];
    }

    return result;

} /* end function make_int */

/*-------------------------------------------------------------------*/
/* Subroutine to return the name of a dataset organization           */
/*-------------------------------------------------------------------*/
static BYTE *
dsorg_name (BYTE *dsorg)
{
static BYTE     name[8];                /* Name of dsorg             */

    if (dsorg[0] & DSORG_IS)
        strcpy (name, "IS");
    else if (dsorg[0] & DSORG_PS)
        strcpy (name, "PS");
    else if (dsorg[0] & DSORG_DA)
        strcpy (name, "DA");
    else if (dsorg[0] & DSORG_PO)
        strcpy (name, "PO");

    if (dsorg[0] & DSORG_U) strcat (name, "U");

    return name;
} /* end function dsorg_name */

/*-------------------------------------------------------------------*/
/* Subroutine to return the name of a record format                  */
/*-------------------------------------------------------------------*/
static BYTE *
recfm_name (BYTE *recfm)
{
static BYTE     name[8];                /* Name of record format     */

    switch (recfm[0] & RECFM_FORMAT) {
    case RECFM_FORMAT_V:
        strcpy (name, "V"); break;
    case RECFM_FORMAT_F:
        strcpy (name, "F"); break;
    case RECFM_FORMAT_U:
        strcpy (name, "U"); break;
    default:
        strcpy (name,"??");
    } /* end switch */

    if (recfm[0] & RECFM_TRKOFLOW) strcat (name, "T");
    if (recfm[0] & RECFM_BLOCKED) strcat (name, "B");
    if (recfm[0] & RECFM_SPANNED) strcat (name, "S");

    switch (recfm[0] & RECFM_CTLCHAR) {
    case RECFM_CTLCHAR_A:
        strcpy (name, "A"); break;
    case RECFM_CTLCHAR_M:
        strcpy (name, "M"); break;
    } /* end switch */

    return name;
} /* end function recfm_name */

/*-------------------------------------------------------------------*/
/* Subroutine to return the name of a DASD device from the UCB type  */
/*-------------------------------------------------------------------*/
static BYTE *
dasd_name (FWORD ucbtype)
{
    if (ucbtype[2] != 0x20) return "????";

    switch (ucbtype[3]) {
    case 0x01: return "2311";
    case 0x02: return "2301";
    case 0x03: return "2303";
    case 0x04: return "2302";
    case 0x05: return "2321";
    case 0x06: return "2305-1";
    case 0x07: return "2305-2";
    case 0x08: return "2314";
    case 0x09: return "3330";
    case 0x0A: return "3350";
    case 0x0B: return "3380";
    case 0x0D: return "3330-11";
    case 0x0F: return "3390";
    } /* end switch(key) */

    return "????";

} /* end function dasd_name */

/*-------------------------------------------------------------------*/
/* Subroutine to return the UCBTYPE of a DASD device                 */
/*-------------------------------------------------------------------*/
static U32
ucbtype_code (U16 devtype)
{
    switch (devtype) {
    case 0x2311: return 0x30002001;
    case 0x2301: return 0x30402002;
    case 0x2303: return 0x30002003;
    case 0x2302: return 0x30002004;
    case 0x2321: return 0x30002005;
    case 0x2305: return 0x30002006;
    case 0x2314: return 0x30C02008;
    case 0x3330: return 0x30502009;
    case 0x3350: return 0x3050200A;
    case 0x3380: return 0x3050200B;
    case 0x3390: return 0x3050200F;
    } /* end switch(key) */

    return 0;

} /* end function ucbtype_code */

/*-------------------------------------------------------------------*/
/* Subroutine to calculate relative track address                    */
/* Input:                                                            */
/*      cyl     Cylinder number                                      */
/*      head    Head number                                          */
/*      heads   Number of heads per cylinder                         */
/*      numext  Number of extents                                    */
/*      xarray  Array containing 1-16 extent descriptions            */
/* Output:                                                           */
/*      The return value is the relative track number,               */
/*      or -1 if an error occurred.                                  */
/*-------------------------------------------------------------------*/
static int
calculate_ttr (int cyl, int head, int heads, int numext,
                EXTDESC xarray[])
{
int     i;                              /* Array subscript           */
int     track;                          /* Relative track number     */

    /* Search the extent descriptor array */
    for (i = 0, track = 0; i < numext; track += xarray[i++].ntrk)
    {
        if (cyl < xarray[i].bcyl || cyl > xarray[i].ecyl)
            continue;

        if (cyl == xarray[i].bcyl && head < xarray[i].btrk)
            continue;

        if (cyl == xarray[i].ecyl && head > xarray[i].etrk)
            continue;

        track += (cyl - xarray[i].bcyl) * heads
                - xarray[i].btrk + head;
        break;
    } /* end for(i) */

    /* Error if track was not found in extent table */
    if (i == numext)
    {
        XMERRF ("CCHH=%4.4X%4.4X not found in extent table\n",
                cyl, head);
        return -1;
    }

    /* Return relative track number */
    return track;
} /* end function calculate_ttr */

/*-------------------------------------------------------------------*/
/* Subroutine to read IPL text from an EBCDIC object file            */
/* Input:                                                            */
/*      iplfnm  Name of EBCDIC card image object file                */
/*      iplbuf  Address of buffer in which to build IPL text record  */
/*      buflen  Length of buffer                                     */
/* Output:                                                           */
/*      The return value is the length of the IPL text built         */
/*      in the buffer if successful, or -1 if error                  */
/* Note:                                                             */
/*      Only TXT records are processed; ESD and RLD records are      */
/*      ignored because the IPL text is non-relocatable and is       */
/*      assumed to have zero origin.  An END card must be present.   */
/*-------------------------------------------------------------------*/
static int
read_ipl_text (BYTE *iplfnm, BYTE *iplbuf, int buflen)
{
int             rc;                     /* Return code               */
int             ipllen = 0;             /* Length of IPL text        */
int             txtlen;                 /* Byte count from TXT card  */
int             txtadr;                 /* Address from TXT card     */
int             tfd;                    /* Object file descriptor    */
BYTE            objrec[80];             /* Object card image         */

    /* Open the object file */
    tfd = open (iplfnm, O_RDONLY);
    if (tfd < 0)
    {
        XMERRF ("Cannot open %s: %s\n",
                iplfnm, strerror(errno));
        return -1;
    }

    /* Read the object file */
    while (1)
    {
        /* Read a card image from the object file */
        rc = read (tfd, objrec, 80);
        if (rc < 80)
        {
            XMERRF ("Cannot read %s: %s\n",
                    iplfnm, strerror(errno));
            close (tfd);
            return -1;
        }

        /* Column 1 of each object card must contain X'02' */
        if (objrec[0] != 0x02)
        {
            XMERRF ("%s is not a valid object file\n",
                    iplfnm);
            close (tfd);
            return -1;
        }

        /* Exit if END card has been read */
        if (memcmp(objrec+1, EBCDIC_END, 3) == 0)
            break;

        /* Ignore any cards which are not TXT cards */
        if (memcmp(objrec+1, EBCDIC_TXT, 3) != 0)
            continue;

        /* Load the address from TXT card columns 6-8 */
        txtadr = (objrec[5] << 16) | (objrec[6] << 8) | objrec[7];

        /* Load the byte count from TXT card columns 11-12 */
        txtlen = (objrec[10] << 8) | objrec[11];

        XMINFF (5, "IPL text address=%6.6X length=%4.4X\n",
                txtadr, txtlen);

        /* Check that the byte count is valid */
        if (txtlen > 56)
        {
            XMERRF ("TXT record in %s has invalid count %d\n",
                    iplfnm, txtlen);
            close (tfd);
            return -1;
        }

        /* Check that the text falls within the buffer */
        if (txtadr + txtlen > buflen)
        {
            XMERRF ("IPL text in %s exceeds %d bytes\n",
                    iplfnm, buflen);
            close (tfd);
            return -1;
        }

        /* Copy the IPL text to the buffer */
        memcpy (iplbuf + txtadr, objrec+16, txtlen);

        /* Update the total size of the IPL text */
        if (txtadr + txtlen > ipllen)
            ipllen = txtadr + txtlen;

    } /* end while */

    return ipllen;
} /* end function read_ipl_text */

/*-------------------------------------------------------------------*/
/* Subroutine to initialize the output track buffer                  */
/* Input:                                                            */
/*      trklen  Track length of virtual output device                */
/*      trkbuf  Pointer to track buffer                              */
/*      cyl     Cylinder number on output device                     */
/*      head    Head number on output device                         */
/* Output:                                                           */
/*      usedv   Number of bytes written to track of virtual device   */
/*-------------------------------------------------------------------*/
static void
init_track (int trklen, BYTE *trkbuf, int cyl, int head, int *usedv)
{
CKDDASD_TRKHDR *trkhdr;                 /* -> Track header           */
CKDDASD_RECHDR *rechdr;                 /* -> Record header          */

    /* Clear the track buffer to zeroes */
    memset (trkbuf, 0, trklen);

    /* Build the home address in the track buffer */
    trkhdr = (CKDDASD_TRKHDR*)trkbuf;
    trkhdr->bin = 0;
    trkhdr->cyl[0] = (cyl >> 8) & 0xFF;
    trkhdr->cyl[1] = cyl & 0xFF;
    trkhdr->head[0] = (head >> 8) & 0xFF;
    trkhdr->head[1] = head & 0xFF;

    /* Build a standard record zero in the track buffer */
    rechdr = (CKDDASD_RECHDR*)(trkbuf + CKDDASD_TRKHDR_SIZE);
    rechdr->cyl[0] = (cyl >> 8) & 0xFF;
    rechdr->cyl[1] = cyl & 0xFF;
    rechdr->head[0] = (head >> 8) & 0xFF;
    rechdr->head[1] = head & 0xFF;
    rechdr->rec = 0;
    rechdr->klen = 0;
    rechdr->dlen[0] = (R0_DATALEN >> 8) & 0xFF;
    rechdr->dlen[1] = R0_DATALEN & 0xFF;

    /* Set number of bytes used in track buffer */
    *usedv = CKDDASD_TRKHDR_SIZE + CKDDASD_RECHDR_SIZE + R0_DATALEN;

    /* Build end of track marker at end of buffer */
    memcpy (trkbuf + *usedv, eighthexFF, 8);

} /* end function init_track */

/*-------------------------------------------------------------------*/
/* Subroutine to write track buffer to output file                   */
/* Input:                                                            */
/*      ofd     Output file descriptor                               */
/*      ofname  Output file name                                     */
/*      heads   Number of tracks per cylinder on output device       */
/*      trklen  Track length of virtual output device                */
/*      trkbuf  Pointer to track buffer                              */
/* Input/output:                                                     */
/*      usedv   Number of bytes written to track of virtual device   */
/*      reltrk  Relative track number on output device               */
/*      cyl     Cylinder number on output device                     */
/*      head    Head number on output device                         */
/* Output:                                                           */
/*      The return value is 0 if successful, -1 if error occurred.   */
/*-------------------------------------------------------------------*/
static int
write_track (int ofd, BYTE *ofname, int heads, int trklen,
            BYTE *trkbuf, int *usedv, int *reltrk, int *cyl, int *head)
{
int             rc;                     /* Return code               */

    /* Build end of track marker at end of buffer */
    memcpy (trkbuf + *usedv, eighthexFF, 8);

    /* Write the current track to the file */
    rc = write (ofd, trkbuf, trklen);
    if (rc < trklen)
    {
        XMERRF ("%s cyl %u head %u write error: %s\n",
                ofname, *cyl, *head,
                errno ? strerror(errno) : "incomplete");
        return -1;
    }

    /* Reset values for next track */
    (*reltrk)++;
    (*head)++;
    if (*head >= heads)
    {
        (*cyl)++;
        *head = 0;
    }
    *usedv = 0;

    return 0;
} /* end function write_track */

/*-------------------------------------------------------------------*/
/* Subroutine to add a data block to the output track buffer         */
/* Input:                                                            */
/*      ofd     Output file descriptor                               */
/*      ofname  Output file name                                     */
/*      blk     Pointer to data block                                */
/*      keylen  Key length                                           */
/*      datalen Data length                                          */
/*      devtype Output device type                                   */
/*      heads   Number of tracks per cylinder on output device       */
/*      trklen  Track length of virtual output device                */
/*      maxtrk  Maximum number of tracks to be written               */
/*      trkbuf  Pointer to track buffer                              */
/* Input/output:                                                     */
/*      usedv   Number of bytes written to track of virtual device   */
/*      usedr   Number of bytes written to track, calculated         */
/*              according to the formula for a real device           */
/*      trkbal  Number of bytes remaining on track, calculated       */
/*              according to the formula for a real device           */
/*      reltrk  Relative track number on output device               */
/*      cyl     Cylinder number on output device                     */
/*      head    Head number on output device                         */
/*      rec     Record number on output device                       */
/* Output:                                                           */
/*      The return value is 0 if successful, -1 if error occurred.   */
/*-------------------------------------------------------------------*/
static int
write_block (int ofd, BYTE *ofname, DATABLK *blk, int keylen,
            int datalen, U16 devtype, int heads, int trklen,
            int maxtrk, BYTE *trkbuf, int *usedv, int *usedr,
            int *trkbal, int *reltrk, int *cyl, int *head, int *rec)
{
int             rc;                     /* Return code               */
int             cc;                     /* Capacity calculation code */
CKDDASD_RECHDR *rechdr;                 /* -> Record header          */

    /* Determine whether record will fit on current track */
    cc = capacity_calc (devtype, *usedr, keylen, datalen,
                        usedr, trkbal, NULL, NULL, NULL, NULL, NULL,
                        NULL, NULL, NULL, NULL, NULL);
    if (cc < 0) return -1;

    /* Move to next track if record will not fit */
    if (cc > 0 && *usedr > 0)
    {
        /* Write current track to output file */
        rc = write_track (ofd, ofname, heads, trklen, trkbuf,
                        usedv, reltrk, cyl, head);
        if (rc < 0) return -1;

        /* Clear bytes used and record number for new track */
        *usedr = 0;
        *rec = 0;

        /* Determine whether record will fit on new track */
        cc = capacity_calc (devtype, *usedr, keylen, datalen,
                            usedr, trkbal, NULL, NULL, NULL, NULL,
                            NULL, NULL, NULL, NULL, NULL, NULL);
        if (cc < 0) return -1;

    } /* end if */

    /* Error if record will not even fit on an empty track */
    if (cc > 0)
    {
        XMERRF ("Input record CCHHR=%2.2X%2.2X%2.2X%2.2X%2.2X "
                "exceeds output device track size\n",
                blk->cyl[0], blk->cyl[1],
                blk->head[0], blk->head[1], blk->rec);
        return -1;
    }

    /* Determine whether end of extent has been reached */
    if (*reltrk >= maxtrk)
    {
        XMERRF ("Dataset exceeds extent size: reltrk=%d, maxtrk=%d\n",
                *reltrk, maxtrk);
        return -1;
    }

    /* Build home address and record 0 if new track */
    if (*usedv == 0)
    {
        init_track (trklen, trkbuf, *cyl, *head, usedv);
    }

    /* Double check that record will not exceed virtual track size */
    if (*usedv + CKDDASD_RECHDR_SIZE + keylen + datalen + 8
        > trklen)
    {
        XMERRF ("Input record CCHHR=%2.2X%2.2X%2.2X%2.2X%2.2X "
                "exceeds virtual device track size\n",
                blk->cyl[0], blk->cyl[1],
                blk->head[0], blk->head[1], blk->rec);
        return -1;
    }

    /* Add data block to virtual track buffer */
    (*rec)++;
    rechdr = (CKDDASD_RECHDR*)(trkbuf + *usedv);
    rechdr->cyl[0] = (*cyl >> 8) & 0xFF;
    rechdr->cyl[1] = *cyl & 0xFF;
    rechdr->head[0] = (*head >> 8) & 0xFF;
    rechdr->head[1] = *head & 0xFF;
    rechdr->rec = *rec;
    rechdr->klen = keylen;
    rechdr->dlen[0] = (datalen >> 8) & 0xFF;
    rechdr->dlen[1] = datalen & 0xFF;
    *usedv += CKDDASD_RECHDR_SIZE;
    memcpy (trkbuf + *usedv, blk->kdarea, keylen + datalen);
    *usedv += keylen + datalen;

    return 0;
} /* end function write_block */

/*-------------------------------------------------------------------*/
/* Subroutine to write track zero                                    */
/* Input:                                                            */
/*      ofd     Output file descriptor                               */
/*      ofname  Output file name                                     */
/*      volser  Volume serial number (ASCIIZ)                        */
/*      devtype Output device type                                   */
/*      heads   Number of tracks per cylinder on output device       */
/*      trklen  Track length of virtual output device                */
/*      trkbuf  Pointer to track buffer                              */
/*      iplfnm  Name of file containing IPL text object deck         */
/* Output:                                                           */
/*      reltrk  Next relative track number on output device          */
/*      outcyl  Cylinder number of next track on output device       */
/*      outhead Head number of next track on output device           */
/*      The return value is 0 if successful, -1 if error occurred.   */
/*-------------------------------------------------------------------*/
static int
write_track_zero (int ofd, BYTE *ofname, BYTE *volser, U16 devtype,
            int heads, int trklen, BYTE *trkbuf, BYTE *iplfnm,
            int *reltrk, int *outcyl, int *outhead)
{
int             rc;                     /* Return code               */
int             outusedv = 0;           /* Output bytes used on track
                                           of virtual device         */
int             outusedr = 0;           /* Output bytes used on track
                                           of real device            */
int             outtrkbr = 0;           /* Output bytes remaining on
                                           track of real device      */
int             outtrk = 0;             /* Output relative track     */
int             outrec = 0;             /* Output record number      */
int             keylen;                 /* Key length                */
int             datalen;                /* Data length               */
int             maxtrks = 1;            /* Maximum track count       */
DATABLK        *datablk;                /* -> data block             */
BYTE            buf[4096];              /* Buffer for data block     */

    /* For 2311 the IPL text will not fit on track 0 record 4,
       so adjust the IPL2 so that it loads from track 1 record 1 */
    if (devtype == 0x2311)
    {
        memcpy (ipl2data + 32, "\x00\x00\x00\x00\x00\x01", 6);
        memcpy (ipl2data + 38, "\x00\x00\x00\x01\x01", 5);
        maxtrks = 2;
    }

    /* Initialize the track buffer */
    *outcyl = 0; *outhead = 0;
    init_track (trklen, trkbuf, *outcyl, *outhead, &outusedv);

    /* Build the IPL1 record */
    memset (buf, 0, sizeof(buf));
    datablk = (DATABLK*)buf;
    convert_to_ebcdic (datablk->kdarea, 4, "IPL1");

    if (iplfnm != NULL)
    {
        memcpy (datablk->kdarea+4, iplpsw, 8);
        memcpy (datablk->kdarea+12, iplccw1, 8);
        memcpy (datablk->kdarea+20, iplccw2, 8);
    }
    else
    {
        memcpy (datablk->kdarea+4, noiplpsw, 8);
        memcpy (datablk->kdarea+12, noiplccw1, 8);
        memcpy (datablk->kdarea+20, noiplccw2, 8);
    }

    keylen = IPL1_KEYLEN;
    datalen = IPL1_DATALEN;

    rc = write_block (ofd, ofname, datablk, keylen, datalen,
                devtype, heads, trklen, maxtrks,
                trkbuf, &outusedv, &outusedr, &outtrkbr,
                &outtrk, outcyl, outhead, &outrec);
    if (rc < 0) return -1;

    /* Build the IPL2 record */
    memset (buf, 0, sizeof(buf));
    datablk = (DATABLK*)buf;
    convert_to_ebcdic (datablk->kdarea, 4, "IPL2");

    if (iplfnm != NULL)
    {
        memcpy (datablk->kdarea+4, ipl2data, sizeof(ipl2data));
    }

    keylen = IPL2_KEYLEN;
    datalen = IPL2_DATALEN;

    rc = write_block (ofd, ofname, datablk, keylen, datalen,
                devtype, heads, trklen, maxtrks,
                trkbuf, &outusedv, &outusedr, &outtrkbr,
                &outtrk, outcyl, outhead, &outrec);
    if (rc < 0) return -1;

    /* Build the VOL1 record */
    memset (buf, 0, sizeof(buf));
    datablk = (DATABLK*)buf;
    convert_to_ebcdic (datablk->kdarea, 4, "VOL1");
    convert_to_ebcdic (datablk->kdarea+4, 4, "VOL1");
    convert_to_ebcdic (datablk->kdarea+8, 6, volser);
    datablk->kdarea[14] = 0x40;
    convert_to_ebcdic (datablk->kdarea+41, 14, "HERCULES");
    keylen = VOL1_KEYLEN;
    datalen = VOL1_DATALEN;

    rc = write_block (ofd, ofname, datablk, keylen, datalen,
                devtype, heads, trklen, maxtrks,
                trkbuf, &outusedv, &outusedr, &outtrkbr,
                &outtrk, outcyl, outhead, &outrec);
    if (rc < 0) return -1;

    /* Build the IPL text from the object file */
    if (iplfnm != NULL)
    {
        memset (buf, 0, sizeof(buf));
        datalen = read_ipl_text (iplfnm, buf+12, sizeof(buf)-12);
        if (datalen < 0) return -1;

        datablk = (DATABLK*)buf;
        keylen = 0;

        rc = write_block (ofd, ofname, datablk, keylen, datalen,
                    devtype, heads, trklen, maxtrks,
                    trkbuf, &outusedv, &outusedr, &outtrkbr,
                    &outtrk, outcyl, outhead, &outrec);
        if (rc < 0) return -1;
    }

    /* Write track zero to the output file */
    rc = write_track (ofd, ofname, heads, trklen, trkbuf,
                    &outusedv, reltrk, outcyl, outhead);
    if (rc < 0) return -1;

    return 0;
} /* end function write_track_zero */

/*-------------------------------------------------------------------*/
/* Subroutine to update a data block in the output file              */
/* Input:                                                            */
/*      ofd     Output file descriptor                               */
/*      ofname  Output file name                                     */
/*      blk     Pointer to data block structure                      */
/*      cyl     Cylinder number                                      */
/*      head    Head number                                          */
/*      rec     Record number                                        */
/*      keylen  Key length                                           */
/*      datalen Data length                                          */
/*      heads   Number of tracks per cylinder on output device       */
/*      trklen  Track length of virtual output device                */
/* Output:                                                           */
/*      The return value is 0 if successful, -1 if error occurred.   */
/*-------------------------------------------------------------------*/
static int
update_block (int ofd, BYTE *ofname, DATABLK *blk, int cyl, int head,
            int rec, int keylen, int datalen, int heads, int trklen)
{
int             rc;                     /* Return code               */
int             klen;                   /* Record key length         */
int             dlen;                   /* Record data length        */
off_t           currpos;                /* Current position in file  */
off_t           seekpos;                /* Seek position for lseek   */
off_t           skiplen;                /* Number of bytes to skip   */
CKDDASD_TRKHDR  trkhdr;                 /* Track header              */
CKDDASD_RECHDR  rechdr;                 /* Record header             */

    /* Save the current position in the output file */
    currpos = lseek (ofd, 0, SEEK_CUR);

    /* Seek to start of track header */
    seekpos = CKDDASD_DEVHDR_SIZE
            + (((cyl * heads) + head) * trklen);

    rc = lseek (ofd, seekpos, SEEK_SET);
    if (rc < 0)
    {
        XMERRF ("%s cyl %d head %d seek error: %s\n",
                ofname, cyl, head, strerror(errno));
        return -1;
    }

    /* Read the track header */
    rc = read (ofd, &trkhdr, CKDDASD_TRKHDR_SIZE);
    if (rc < CKDDASD_TRKHDR_SIZE)
    {
        XMERRF ("%s cyl %d head %d read error: %s\n",
                ofname, cyl, head, strerror(errno));
        return -1;
    }

    /* Validate the track header */
    if (trkhdr.bin != 0
        || trkhdr.cyl[0] != (cyl >> 8)
        || trkhdr.cyl[1] != (cyl & 0xFF)
        || trkhdr.head[0] != (head >> 8)
        || trkhdr.head[1] != (head & 0xFF))
    {
        XMERRF ("%s cyl %d head %d invalid track header "
                "%2.2X%2.2X%2.2X%2.2X%2.2X at offset %8.8lX\n",
                ofname, cyl, head,
                trkhdr.bin, trkhdr.cyl[0], trkhdr.cyl[1],
                trkhdr.head[0], trkhdr.head[1], seekpos);
        return -1;
    }

    /* Search for the record to be updated */
    while (1)
    {
        /* Read the next record header */
        rc = read (ofd, &rechdr, CKDDASD_RECHDR_SIZE);
        if (rc < CKDDASD_RECHDR_SIZE)
        {
            XMERRF ("%s cyl %d head %d read error: %s\n",
                    ofname, cyl, head, strerror(errno));
            return -1;
        }

        /* Check for end of track */
        if (memcmp(&rechdr, eighthexFF, 8) == 0)
        {
            XMERRF ("%s cyl %d head %d rec %d record not found\n",
                    ofname, cyl, head, rec);
            return -1;
        }

        /* Extract record key length and data length */
        klen = rechdr.klen;
        dlen = (rechdr.dlen[0] << 8) | rechdr.dlen[1];

        /* Exit loop if matching record number */
        if (rechdr.rec == rec)
            break;

        /* Skip the key and data areas */
        skiplen = klen + dlen;
        rc = lseek (ofd, skiplen, SEEK_CUR);
        if (rc < 0)
        {
            XMERRF ("%s cyl %d head %d rec %d seek %ld error: %s\n",
                    ofname, cyl, head, rec, skiplen, strerror(errno));
            return -1;
        }

    } /* end while */

    /* Check for attempt to change key length or data length */
    if (keylen != klen || datalen != dlen)
    {
        XMERRF ("Cannot update cyl %d head %d rec %d: "
                "Unmatched KL/DL\n",
                cyl, head, rec);
        return -1;
    }

    /* Write the updated block to the file */
    rc = write (ofd, blk->kdarea, keylen + datalen);
    if (rc < keylen + datalen)
    {
        XMERRF ("%s cyl %u head %u rec %d write error: %s\n",
                ofname, cyl, head, rec, strerror(errno));
        return -1;
    }

    /* Restore original file position */
    rc = lseek (ofd, currpos, SEEK_SET);
    if (rc < 0)
    {
        XMERRF ("%s offset %8.8lX seek error: %s\n",
                ofname, currpos, strerror(errno));
        return -1;
    }

    XMINFF (4, "Updating cyl %u head %u rec %d kl %d dl %d\n",
                cyl, head, rec, keylen, datalen);

    return 0;
} /* end function update_block */

/*-------------------------------------------------------------------*/
/* Subroutine to build a format 1 DSCB                               */
/* Input:                                                            */
/*      blkpp   Address of pointer to chain field of previous DSCB   */
/*      dsname  Dataset name (ASCIIZ)                                */
/*      volser  Volume serial number (ASCIIZ)                        */
/*      dsorg   1st byte of dataset organization bits                */
/*      recfm   1st byte of record format bits                       */
/*      lrecl   Logical record length                                */
/*      blksz   Block size                                           */
/*      keyln   Key length                                           */
/*      dirblu  Bytes used in last directory block                   */
/*      lasttrk Relative track number of last-used track of dataset  */
/*      lastrec Record number of last-used block of dataset          */
/*      trkbal  Bytes remaining on last-used track                   */
/*      units   Allocation units (C=CYL, T=TRK)                      */
/*      spsec   Secondary allocation quantity                        */
/*      bcyl    Extent begin cylinder number                         */
/*      bhead   Extent begin head number                             */
/*      ecyl    Extent end cylinder number                           */
/*      ehead   Extent end head number                               */
/* Output:                                                           */
/*      The return value is 0 if successful, or -1 if error          */
/*                                                                   */
/* This subroutine allocates a DATABLK structure, builds a DSCB      */
/* within the structure, and adds the structure to the DSCB chain.   */
/*-------------------------------------------------------------------*/
static int
build_format1_dscb (DATABLK ***blkpp, BYTE *dsname, BYTE *volser,
                BYTE dsorg, BYTE recfm, int lrecl, int blksz,
                int keyln, int dirblu, int lasttrk, int lastrec,
                int trkbal, BYTE units, int spsec,
                int bcyl, int bhead, int ecyl, int ehead)
{
DATABLK        *datablk;                /* -> Data block structure   */
FORMAT1_DSCB   *f1dscb;                 /* -> DSCB within data block */
int             blklen;                 /* Size of data block        */
struct tm      *tmptr;                  /* -> Date and time structure*/
time_t          timeval;                /* Current time value        */

    /* Obtain the current time */
    time(&timeval);
    tmptr = localtime(&timeval);

    /* Allocate storage for a DATABLK structure */
    blklen = 12 + sizeof(FORMAT1_DSCB);
    datablk = (DATABLK*)malloc(blklen);
    if (datablk == NULL)
    {
        XMERRF ("Cannot obtain storage for DSCB: %s\n",
                strerror(errno));
        return -1;
    }

    /* Clear the data block and chain it to the previous one */
    memset (datablk, 0, blklen);
    **blkpp = datablk;
    datablk->header = 0;
    *blkpp = (DATABLK**)(&datablk->header);

    /* Point to the DSCB within the data block */
    f1dscb = (FORMAT1_DSCB*)(datablk->kdarea);

    /* Build the format 1 DSCB */
    convert_to_ebcdic (f1dscb->ds1dsnam, 44, dsname);
    f1dscb->ds1fmtid = 0xF1;
    convert_to_ebcdic (f1dscb->ds1dssn, 6, volser);
    f1dscb->ds1volsq[0] = 0;
    f1dscb->ds1volsq[1] = 1;
    f1dscb->ds1credt[0] = tmptr->tm_year;
    f1dscb->ds1credt[1] = (tmptr->tm_yday >> 8) & 0xFF;
    f1dscb->ds1credt[2] = tmptr->tm_yday & 0xFF;
    f1dscb->ds1expdt[0] = 0;
    f1dscb->ds1expdt[1] = 0;
    f1dscb->ds1expdt[2] = 0;
    f1dscb->ds1noepv = 1;
    f1dscb->ds1bodbd = dirblu;
    convert_to_ebcdic (f1dscb->ds1syscd, 13, "HERCULES");
    f1dscb->ds1dsorg[0] = dsorg;
    f1dscb->ds1dsorg[1] = 0;
    f1dscb->ds1recfm = recfm;
    f1dscb->ds1optcd = 0;
    f1dscb->ds1blkl[0] = (blksz >> 8) & 0xFF;
    f1dscb->ds1blkl[1] = blksz & 0xFF;
    f1dscb->ds1lrecl[0] = (lrecl >> 8) & 0xFF;
    f1dscb->ds1lrecl[1] = lrecl & 0xFF;
    f1dscb->ds1keyl = keyln;
    f1dscb->ds1rkp[0] = 0;
    f1dscb->ds1rkp[1] = 0;
    f1dscb->ds1dsind = DS1DSIND_LASTVOL;
    if ((blksz & 0x07) == 0)
        f1dscb->ds1dsind |= DS1DSIND_BLKSIZ8;
    f1dscb->ds1scalo[0] =
        (units == 'C' ? DS1SCALO_UNITS_CYL : DS1SCALO_UNITS_TRK);
    f1dscb->ds1scalo[1] = (spsec >> 16) & 0xFF;
    f1dscb->ds1scalo[2] = (spsec >> 8) & 0xFF;
    f1dscb->ds1scalo[3] = spsec & 0xFF;
    f1dscb->ds1lstar[0] = (lasttrk >> 8) & 0xFF;
    f1dscb->ds1lstar[1] = lasttrk & 0xFF;
    f1dscb->ds1lstar[2] = lastrec;
    f1dscb->ds1trbal[0] = (trkbal >> 8) & 0xFF;
    f1dscb->ds1trbal[1] = trkbal & 0xFF;
    f1dscb->ds1ext1.xttype =
        (units == 'C' ? XTTYPE_CYLBOUND : XTTYPE_DATA);
    f1dscb->ds1ext1.xtseqn = 0;
    f1dscb->ds1ext1.xtbcyl[0] = (bcyl >> 8) & 0xFF;
    f1dscb->ds1ext1.xtbcyl[1] = bcyl & 0xFF;
    f1dscb->ds1ext1.xtbtrk[0] = (bhead >> 8) & 0xFF;
    f1dscb->ds1ext1.xtbtrk[1] = bhead & 0xFF;
    f1dscb->ds1ext1.xtecyl[0] = (ecyl >> 8) & 0xFF;
    f1dscb->ds1ext1.xtecyl[1] = ecyl & 0xFF;
    f1dscb->ds1ext1.xtetrk[0] = (ehead >> 8) & 0xFF;
    f1dscb->ds1ext1.xtetrk[1] = ehead & 0xFF;

    return 0;
} /* end function build_format1_dscb */

/*-------------------------------------------------------------------*/
/* Subroutine to build a format 4 DSCB                               */
/* Input:                                                            */
/*      blkpp   Address of pointer to chain field of previous DSCB   */
/*      devtype Output device type                                   */
/* Output:                                                           */
/*      The return value is 0 if successful, or -1 if error          */
/*                                                                   */
/* This subroutine allocates a DATABLK structure, builds a DSCB      */
/* within the structure, and adds the structure to the DSCB chain.   */
/*                                                                   */
/* Note: The VTOC extent descriptor, the highest F1 DSCB address,    */
/* and the number of unused DSCBs, are set to zeroes here and must   */
/* be updated later when the VTOC size and location are known.       */
/* The device size in cylinders is set to the normal size for the    */
/* device type and must be updated when the actual total number of   */
/* cylinders written to the volume is known.                         */
/*-------------------------------------------------------------------*/
static int
build_format4_dscb (DATABLK ***blkpp, U16 devtype)
{
DATABLK        *datablk;                /* -> Data block structure   */
FORMAT4_DSCB   *f4dscb;                 /* -> DSCB within data block */
int             blklen;                 /* Size of data block        */
int             numdscb;                /* Number of DSCBs per track */
int             numdblk;                /* Number of dir blks/track  */
int             physlen;                /* Physical track length     */
int             numcyls;                /* Device size in cylinders  */
int             numheads;               /* Number of heads/cylinder  */
int             kbconst;                /* Keyed block constant      */
int             lbconst;                /* Last keyed block constant */
int             nkconst;                /* Non-keyed block constant  */
BYTE            devflag;                /* Device flags for VTOC     */
int             tolfact;                /* Device tolerance          */

    /* Calculate the physical track length, block overheads, device
       size, and the number of DSCBs and directory blocks per track */
    capacity_calc (devtype, 0, 44, 96, NULL, NULL, &physlen, &kbconst,
                    &lbconst, &nkconst, &devflag, &tolfact, NULL,
                    &numdscb, &numheads, &numcyls);
    capacity_calc (devtype, 0, 8, 256, NULL, NULL, NULL,
                    NULL, NULL, NULL, NULL, NULL, NULL,
                    &numdblk, NULL, NULL);

    /* Allocate storage for a DATABLK structure */
    blklen = 12 + sizeof(FORMAT4_DSCB);
    datablk = (DATABLK*)malloc(blklen);
    if (datablk == NULL)
    {
        XMERRF ("Cannot obtain storage for DSCB: %s\n",
                strerror(errno));
        return -1;
    }

    /* Clear the data block and chain it to the previous one */
    memset (datablk, 0, blklen);
    **blkpp = datablk;
    datablk->header = 0;
    *blkpp = (DATABLK**)(&datablk->header);

    /* Point to the DSCB within the data block */
    f4dscb = (FORMAT4_DSCB*)(datablk->kdarea);

    /* Build the format 4 DSCB */
    memset (f4dscb->ds4keyid, 0x04, 44);
    f4dscb->ds4fmtid = 0xF4;
    f4dscb->ds4hcchh[0] = (numcyls >> 8) & 0xFF;
    f4dscb->ds4hcchh[1] = numcyls & 0xFF;
    f4dscb->ds4hcchh[2] = 0;
    f4dscb->ds4hcchh[3] = 0;
    f4dscb->ds4noatk[0] = 0;
    f4dscb->ds4noatk[1] = 0;
    f4dscb->ds4vtoci = DS4VTOCI_DOS;
    f4dscb->ds4noext = 1;
    f4dscb->ds4devsz[0] = (numcyls >> 8) & 0xFF;
    f4dscb->ds4devsz[1] = numcyls & 0xFF;
    f4dscb->ds4devsz[2] = (numheads >> 8) & 0xFF;
    f4dscb->ds4devsz[3] = numheads & 0xFF;
    f4dscb->ds4devtk[0] = (physlen >> 8) & 0xFF;
    f4dscb->ds4devtk[1] = physlen & 0xFF;
    f4dscb->ds4devi = kbconst;
    f4dscb->ds4devl = lbconst;
    f4dscb->ds4devk = nkconst;
    f4dscb->ds4devfg = devflag;
    f4dscb->ds4devtl[0] = (tolfact >> 8) & 0xFF;
    f4dscb->ds4devtl[1] = tolfact & 0xFF;
    f4dscb->ds4devdt = numdscb;
    f4dscb->ds4devdb = numdblk;

    return 0;
} /* end function build_format4_dscb */

/*-------------------------------------------------------------------*/
/* Subroutine to build a format 5 DSCB                               */
/* Input:                                                            */
/*      blkpp   Address of pointer to chain field of previous DSCB   */
/* Output:                                                           */
/*      The return value is 0 if successful, or -1 if error          */
/*                                                                   */
/* This subroutine allocates a DATABLK structure, builds a DSCB      */
/* within the structure, and adds the structure to the DSCB chain.   */
/*                                                                   */
/* Note: The format 5 DSCB is built with no free space extents.      */
/* The DOS bit which is set in ds4vtoci forces the operating system  */
/* VTOC conversion routine to calculate the free space and update    */
/* the format 5 DSCB the first time the volume is accessed.          */
/*-------------------------------------------------------------------*/
static int
build_format5_dscb (DATABLK ***blkpp)
{
DATABLK        *datablk;                /* -> Data block structure   */
FORMAT5_DSCB   *f5dscb;                 /* -> DSCB within data block */
int             blklen;                 /* Size of data block        */

    /* Allocate storage for a DATABLK structure */
    blklen = 12 + sizeof(FORMAT5_DSCB);
    datablk = (DATABLK*)malloc(blklen);
    if (datablk == NULL)
    {
        XMERRF ("Cannot obtain storage for DSCB: %s\n",
                strerror(errno));
        return -1;
    }

    /* Clear the data block and chain it to the previous one */
    memset (datablk, 0, blklen);
    **blkpp = datablk;
    datablk->header = 0;
    *blkpp = (DATABLK**)(&datablk->header);

    /* Point to the DSCB within the data block */
    f5dscb = (FORMAT5_DSCB*)(datablk->kdarea);

    /* Build the format 5 DSCB */
    memset (f5dscb->ds5keyid, 0x05, 4);
    f5dscb->ds5fmtid = 0xF5;

    return 0;
} /* end function build_format5_dscb */

/*-------------------------------------------------------------------*/
/* Subroutine to write the VTOC                                      */
/* Input:                                                            */
/*      numdscb Number of DSCBs including format 4 and format 5      */
/*      blkptr  Pointer to data block containing first DSCB          */
/*      ofd     Output file descriptor                               */
/*      ofname  Output file name                                     */
/*      devtype Output device type                                   */
/*      heads   Number of tracks per cylinder on output device       */
/*      trklen  Track length of virtual output device                */
/*      trkbuf  Pointer to track buffer                              */
/*      outcyl  Starting cylinder number for VTOC                    */
/*      outhead Starting head number for VTOC                        */
/* Output:                                                           */
/*      nxtcyl  Starting cylinder number for next dataset            */
/*      nxthead Starting head number for next dataset                */
/*      The return value is 0 if successful, or -1 if error          */
/*-------------------------------------------------------------------*/
static int
write_vtoc (int numdscb, DATABLK *blkptr, int ofd, BYTE *ofname,
            U16 devtype, int heads, int trklen, BYTE *trkbuf,
            int outcyl, int outhead, int *nxtcyl, int *nxthead)
{
int             rc;                     /* Return code               */
int             i;                      /* Array subscript           */
DATABLK        *datablk;                /* -> Data block structure   */
FORMAT4_DSCB   *f4dscb;                 /* -> Format 4 DSCB          */
int             dscbpertrk;             /* Number of DSCBs per track */
int             numtrk;                 /* Size of VTOC in tracks    */
int             numf0dscb;              /* Number of unused DSCBs    */
int             abstrk;                 /* Absolute track number     */
int             endcyl;                 /* VTOC end cylinder number  */
int             endhead;                /* VTOC end head number      */
int             endrec;                 /* Last used record number   */
int             numcyls;                /* Volume size in cylinders  */
int             outusedv = 0;           /* Bytes used in track buffer*/
int             outusedr = 0;           /* Bytes used on real track  */
int             outtrkbr;               /* Bytes left on real track  */
int             outtrk = 0;             /* Relative track number     */
int             outrec = 0;             /* Output record number      */
BYTE            blankblk[152];          /* Data block for blank DSCB */

    /* Point to the format 4 DSCB within the first data block */
    f4dscb = (FORMAT4_DSCB*)(blkptr->kdarea);

    /* Calculate the minimum number of tracks required for the VTOC */
    dscbpertrk = f4dscb->ds4devdt;
    numtrk = (numdscb + dscbpertrk - 1) / dscbpertrk;

    XMINFF (2, "VTOC starts at cyl %d head %d and is %d tracks\n",
            outcyl, outhead, numtrk);

    /* Calculate the CCHHR of the last format 1 DSCB */
    abstrk = (outcyl * heads) + outhead;
    abstrk += numtrk - 1;
    endcyl = abstrk / heads;
    endhead = abstrk % heads;
    endrec = ((numdscb - 1) % dscbpertrk) + 1;

    /* Update the last format 1 CCHHR in the format 4 DSCB */
    f4dscb->ds4hpchr[0] = (endcyl >> 8) & 0xFF;
    f4dscb->ds4hpchr[1] = endcyl & 0xFF;
    f4dscb->ds4hpchr[2] = (endhead >> 8) & 0xFF;
    f4dscb->ds4hpchr[3] = endhead & 0xFF;
    f4dscb->ds4hpchr[4] = endrec;

    /* Calculate the number of format 0 DSCBs required to
       fill out the unused space at the end of the VTOC */
    numf0dscb = (numtrk * dscbpertrk) - numdscb;

    /* Update the format 0 DSCB count in the format 4 DSCB */
    f4dscb->ds4dsrec[0] = (numf0dscb >> 8) & 0xFF;
    f4dscb->ds4dsrec[1] = numf0dscb & 0xFF;

    /* Update the VTOC extent descriptor in the format 4 DSCB */
    f4dscb->ds4vtoce.xttype =
        (endhead == heads - 1 ? XTTYPE_CYLBOUND : XTTYPE_DATA);
    f4dscb->ds4vtoce.xtseqn = 0;
    f4dscb->ds4vtoce.xtbcyl[0] = (outcyl >> 8) & 0xFF;
    f4dscb->ds4vtoce.xtbcyl[1] = outcyl & 0xFF;
    f4dscb->ds4vtoce.xtbtrk[0] = (outhead >> 8) & 0xFF;
    f4dscb->ds4vtoce.xtbtrk[1] = outhead & 0xFF;
    f4dscb->ds4vtoce.xtecyl[0] = (endcyl >> 8) & 0xFF;
    f4dscb->ds4vtoce.xtecyl[1] = endcyl & 0xFF;
    f4dscb->ds4vtoce.xtetrk[0] = (endhead >> 8) & 0xFF;
    f4dscb->ds4vtoce.xtetrk[1] = endhead & 0xFF;

    /* Assuming that the VTOC is the last dataset on the volume,
       update the volume size in cylinders in the format 4 DSCB */
    numcyls = endcyl + 1;
    f4dscb->ds4devsz[0] = (numcyls >> 8) & 0xFF;
    f4dscb->ds4devsz[1] = numcyls & 0xFF;

    /* Format the track buffer */
    init_track (trklen, trkbuf, outcyl, outhead, &outusedv);

    /* Write the format 4, format 5, and format 1 DSCBs to the VTOC */
    datablk = blkptr;
    for (i = 0; i < numdscb; i++)
    {
        /* Add next DSCB to the track buffer */
        rc = write_block (ofd, ofname, datablk, 44, 96,
                    devtype, heads, trklen, numtrk,
                    trkbuf, &outusedv, &outusedr, &outtrkbr,
                    &outtrk, &outcyl, &outhead, &outrec);
        if (rc < 0) return -1;

        XMINFF (4, "Format %d DSCB CCHHR=%4.4X%4.4X%2.2X "
                "(TTR=%4.4X%2.2X)\n",
                datablk->kdarea[0] == 0x04 ? 4 :
                datablk->kdarea[0] == 0x05 ? 5 : 1,
                outcyl, outhead, outrec, outtrk, outrec);
        if (infolvl >= 5) data_dump (datablk, 152);

        datablk = (DATABLK*)(datablk->header);

    } /* end for(i) */

    /* Fill the remainder of the VTOC with format 0 DSCBs */
    for (i = 0; i < numf0dscb; i++)
    {
        /* Add a format 0 DSCB to the track buffer */
        memset (blankblk, 0, sizeof(blankblk));
        datablk = (DATABLK*)blankblk;
        rc = write_block (ofd, ofname, datablk, 44, 96,
                    devtype, heads, trklen, numtrk,
                    trkbuf, &outusedv, &outusedr, &outtrkbr,
                    &outtrk, &outcyl, &outhead, &outrec);
        if (rc < 0) return -1;

        XMINFF (4, "Format 0 DSCB CCHHR=%4.4X%4.4X%2.2X "
                "(TTR=%4.4X%2.2X)\n",
                outcyl, outhead, outrec, outtrk, outrec);

    } /* end for(i) */

    /* Write data remaining in last track buffer */
    rc = write_track (ofd, ofname, heads, trklen, trkbuf,
                    &outusedv, &outtrk, &outcyl, &outhead);
    if (rc < 0) return -1;

    /* Return starting address of next dataset */
    *nxtcyl = outcyl;
    *nxthead = outhead;

    return 0;
} /* end function write_vtoc */

/*-------------------------------------------------------------------*/
/* Subroutine to return the name of a text unit                      */
/*-------------------------------------------------------------------*/
static BYTE *
tu_name (U16 key)
{
    switch (key) {
    CASERET(INMDDNAM);
    CASERET(INMDSNAM);
    CASERET(INMMEMBR);
    CASERET(INMDIR  );
    CASERET(INMEXPDT);
    CASERET(INMTERM );
    CASERET(INMBLKSZ);
    CASERET(INMDSORG);
    CASERET(INMLRECL);
    CASERET(INMRECFM);
    CASERET(INMTNODE);
    CASERET(INMTUID );
    CASERET(INMFNODE);
    CASERET(INMFUID );
    CASERET(INMLREF );
    CASERET(INMLCHG );
    CASERET(INMCREAT);
    CASERET(INMFVERS);
    CASERET(INMFTIME);
    CASERET(INMTTIME);
    CASERET(INMFACK );
    CASERET(INMERRCD);
    CASERET(INMUTILN);
    CASERET(INMUSERP);
    CASERET(INMRECCT);
    CASERET(INMSIZE );
    CASERET(INMFFM  );
    CASERET(INMNUMF );
    CASERET(INMTYPE );
    } /* end switch(key) */

    return "????????";

} /* end function tu_name */

/*-------------------------------------------------------------------*/
/* Subroutine to extract next text unit from buffer                  */
/* Input:                                                            */
/*      xbuf    Pointer to start of buffer                           */
/*      bufpos  Position of next text unit within buffer             */
/*      bufrem  Number of bytes remaining in buffer                  */
/*      pkey    Pointer to field to receive text unit key            */
/*      pnum    Pointer to field to receive number of data items     */
/*      maxnum  Maximum number of data items expected                */
/*      plen    Pointer to array to receive data item lengths        */
/*      pdata   Pointer to array to receive data item pointers       */
/* Output:                                                           */
/*      The function return value is the total length of the         */
/*      text unit, or -1 if error.                                   */
/*                                                                   */
/* Text units are listed if infolvl is 4 or greater.                 */
/*-------------------------------------------------------------------*/
static int
next_tu (BYTE *xbuf, int bufpos, int bufrem, U16 *pkey, U16 *pnum,
        U16 maxnum, U16 plen[], BYTE *pdata[])
{
int     i, j;                           /* Array subscripts          */
U16     key, num;                       /* Text unit header          */
int     field;                          /* Field number              */
int     offset;                         /* Offset into text unit     */
U16     len;                            /* Field length              */
BYTE   *name;                           /* Text unit name            */
BYTE    c, hex[17], chars[9];           /* Character work areas      */

    /* Error if remaining length is insufficient for header */
    if (bufrem < 4)
    {
        XMERR ("Incomplete text unit\n");
        return -1;
    }

    /* Load the key and field count from the first 4 bytes */
    key = (xbuf[bufpos] << 8) | xbuf[bufpos+1];
    num = (xbuf[bufpos+2] << 8) | xbuf[bufpos+3];

    /* Obtain the text unit name */
    name = tu_name(key);

    /* Print the text unit name and field count */
    XMINFF (4, "\t+%4.4X %-8.8s %4.4X %4.4X ", bufpos, name, key, num);

    /* Error if number of fields exceeds maximum */
    if (num > maxnum)
    {
        XMINF (4, "\n");
        XMERR ("Too many fields in text unit\n");
        return -1;
    }

    /* Point to first field */
    offset = 4;
    bufrem -= 4;

    /* Process each field in text unit */
    for (field = 0; field < num; field++)
    {
        /* Error if remaining length is insufficient for length */
        if (bufrem < 2)
        {
            XMINF (4, "\n");
            XMERR ("Incomplete text unit\n");
            return -1;
        }

        /* Load field length from next 2 bytes */
        len = (xbuf[bufpos+offset] << 8) | xbuf[bufpos+offset+1];
        offset += 2;
        bufrem -= 2;

        /* Error if remaining length is insufficient for data */
        if (bufrem < len)
        {
            XMINF (4, "\n");
            XMERR ("Incomplete text unit\n");
            return -1;
        }

        /* Print field length and data */
        if (field > 0) XMINF (4, "\n\t\t\t\t ");
        XMINFF (4, "%4.4X ", len);
        memset (hex, '\0', sizeof(hex));
        memset (chars, '\0', sizeof(chars));
        for (i = 0, j = 0; i < len; i++, j++)
        {
            if (i > 0 && (i & 0x07) == 0)
            {
                XMINFF (4, "%-16.16s %-8.8s\n\t\t\t\t      ",
                    hex, chars);
                memset (hex, '\0', sizeof(hex));
                memset (chars, '\0', sizeof(chars));
                j = 0;
            }
            sprintf(hex+2*j, "%2.2X", xbuf[bufpos+offset+i]);
            c = ebcdic_to_ascii[xbuf[bufpos+offset+i]];
            if (!isprint(c)) c = '.';
            chars[j] = c;
        } /* end for(i) */
        XMINFF (4, "%-16.16s %-8.8s", hex, chars);

        /* Save field length and pointer in array */
        plen[field] = len;
        pdata[field] = xbuf + bufpos + offset;

        /* Get offset of next field in text unit */
        offset += len;
        bufrem -= len;

    } /* end for */

    /* Print newline at end of text unit */
    XMINF (4, "\n");

    /* Return key, number of fields, and total length */
    *pkey = key;
    *pnum = num;
    return offset;

} /* end function next_tu */

/*-------------------------------------------------------------------*/
/* Subroutine to assemble a logical record from segments             */
/* Input:                                                            */
/*      xfd     Input file descriptor                                */
/*      xfname  Input file name                                      */
/*      xbuf    Pointer to buffer to receive logical record          */
/* Output:                                                           */
/*      ctl     Zero=data record, non-zero=control record            */
/*      The return value is the logical record length,               */
/*      or -1 if an error occurred.                                  */
/*-------------------------------------------------------------------*/
static int
read_xmit_rec (int xfd, BYTE *xfname, BYTE *xbuf, BYTE *ctl)
{
int             rc;                     /* Return code               */
int             xreclen = 0;            /* Cumulative record length  */
int             segnum;                 /* Segment counter           */
int             seglen;                 /* Segment data length       */
BYTE            ctlind = 0x00;          /* 0x20=Control record       */
BYTE            seghdr[2];              /* Segment length and flags  */

    for (segnum = 0; ; segnum++)
    {
        /* Read the segment length and flags */
        rc = read (xfd, seghdr, 2);
        if (rc < 2)
        {
            XMERRF ("%s read error: %s\n",
                    xfname, strerror(errno));
            return -1;
        }

        /* Check for valid segment header */
        if (seghdr[0] < 2 || (seghdr[1] & 0x1F) != 0)
        {
            XMERRF ("%s invalid segment header: %2.2X%2.2X\n",
                    xfname, seghdr[0], seghdr[1]);
            return -1;
        }

        /* Check flags for first segment */
        if (segnum == 0)
        {
            /* Check that first segment indicator is set */
            if ((seghdr[1] & 0x80) == 0)
            {
                XMERRF ("%s first segment indicator expected\n",
                        xfname);
                return -1;
            }

            /* Save the control record indicator */
            ctlind = (seghdr[1] & 0x20);
        }

        /* Check flags for subsequent segments */
        if (segnum > 0)
        {
            /* Check that first segment indicator is not set */
            if (seghdr[1] & 0x80)
            {
                XMERRF ("%s first segment indicator not expected\n",
                        xfname);
                return -1;
            }

            /* Check if ctlrec indicator matches first segment */
            if ((seghdr[1] & 0x20) != ctlind)
            {
                XMERRF ("%s control record indicator mismatch\n",
                        xfname);
                return -1;
            }
        }

        /* Read segment data into buffer */
        seglen = seghdr[0] - 2;
        rc = read (xfd, xbuf + xreclen, seglen);
        if (rc < seglen)
        {
            XMERRF ("%s read error: %s\n",
                    xfname, strerror(errno));
            return -1;
        }

        /* Accumulate total record length */
        xreclen += seglen;

        /* Exit if last segment of record */
        if (seghdr[1] & 0x40)
            break;

    } /* end for(segnum) */

    /* Return record length and control indicator */
    *ctl = ctlind;
    return xreclen;

} /* end function read_xmit_rec */

/*-------------------------------------------------------------------*/
/* Subroutine to process an INMR02 control record                    */
/* Input:                                                            */
/*      xbuf    Pointer to buffer containing control record          */
/*      xreclen Length of control record                             */
/*      dsorg   Pointer to byte to receive dataset organization      */
/*      recfm   Pointer to byte to receive record format             */
/*      lrecl   Pointer to integer to receive logical record length  */
/*      blksz   Pointer to integer to receive block size             */
/*      keyln   Pointer to integer to receive key length             */
/*      dirnm   Pointer to integer to number of directory blocks     */
/* Output:                                                           */
/*      If the record contains the text unit INMUTILN=IEBCOPY        */
/*      then the dataset attributes are returned and the function    */
/*      return value is 1.  Otherwise the return value is 0          */
/*      and the dataset attributes remain unchanged.                 */
/*      If an error occurs then the return value is -1.              */
/*                                                                   */
/* File information is listed if infolvl is 2 or greater.            */
/*-------------------------------------------------------------------*/
static int
process_inmr02 (BYTE *xbuf, int xreclen, BYTE *dsorg, BYTE *recfm,
                U16 *lrecl, U16 *blksz, U16 *keyln, U16 *dirnm)
{
int             rc;                     /* Return code               */
int             i;                      /* Array subscript           */
int             len;                    /* String length             */
U32             filenum;                /* File number               */
int             bufpos;                 /* Position of TU in buffer  */
int             bufrem;                 /* Bytes remaining in buffer */
BYTE            tuutiln[9];             /* Utility name              */
BYTE            tukeyln;                /* Key length                */
HWORD           tudsorg;                /* Data set organization     */
HWORD           turecfm;                /* Record format             */
U16             tulrecl;                /* Logical record length     */
U16             tublksz;                /* Block size                */
int             tudirct;                /* Number of directory blocks*/
U16             tukey;                  /* Text unit key             */
U16             tunum;                  /* Number of text unit fields*/
BYTE            tudsnam[45];            /* Data set name             */
#define MAXNUM  20                      /* Maximum number of fields  */
U16             fieldlen[MAXNUM];       /* Array of field lengths    */
BYTE           *fieldptr[MAXNUM];       /* Array of field pointers   */

    /* Extract the file number which follows the record name */
    filenum = (xbuf[6] << 24) | (xbuf[7] << 16)
            | (xbuf[8] << 8) | xbuf[9];

    /* Point to the first text unit */
    bufpos = 10;
    bufrem = xreclen-10;

    /* Clear values to be loaded from text units */
    memset (tudsnam, 0, sizeof(tudsnam));
    memset (tuutiln, 0, sizeof(tuutiln));
    memset (tudsorg, 0, sizeof(tudsorg));
    memset (turecfm, 0, sizeof(turecfm));
    tulrecl = 0;
    tublksz = 0;
    tukeyln = 0;
    tudirct = 0;

    /* Process each text unit */
    while (bufrem > 0)
    {
        /* Extract the next text unit */
        rc = next_tu (xbuf, bufpos, bufrem, &tukey, &tunum,
                MAXNUM, fieldlen, fieldptr);
        if (rc < 0)
        {
            XMERRF ("Invalid text unit at offset %4.4X\n",
                    bufpos + 2);
            return -1;
        }
        bufpos += rc;
        bufrem -= rc;

        /* Save the values from selected text units */
        switch (tukey) {
        case INMUTILN:
            make_asciiz (tuutiln, sizeof(tuutiln),
                        fieldptr[0], fieldlen[0]);
            break;
        case INMDSORG:
            if (fieldlen[0] > sizeof(tudsorg))
                fieldlen[0] = sizeof(tudsorg);
            memcpy (tudsorg, fieldptr[0], fieldlen[0]);
            break;
        case INMRECFM:
            if (fieldlen[0] > sizeof(turecfm))
                fieldlen[0] = sizeof(turecfm);
            memcpy (turecfm, fieldptr[0], fieldlen[0]);
            break;
        case INMLRECL:
            tulrecl = make_int (fieldptr[0], fieldlen[0]);
            break;
        case INMBLKSZ:
            tublksz = make_int (fieldptr[0], fieldlen[0]);
            break;
        case INMTYPE:
            tukeyln = make_int (fieldptr[0], fieldlen[0]);
            break;
        case INMDIR:
            tudirct = make_int (fieldptr[0], fieldlen[0]);
            break;
        case INMDSNAM:
            memset (tudsnam, 0, sizeof(tudsnam));
            for (i = 0; i < tunum; i++)
            {
                len = strlen(tudsnam);
                if (i > 0 && len < sizeof(tudsnam) - 1)
                    tudsnam[len++] = '.';
                make_asciiz (tudsnam + len, sizeof(tudsnam) - len,
                                fieldptr[i], fieldlen[i]);
            } /* end for(i) */
        } /* end switch(tukey) */
    } /* end while(bufrem) */

    /* Return the dataset values if this is the IEBCOPY record */
    if (strcmp(tuutiln, "IEBCOPY") == 0)
    {
        XMINFF (2, "File %u: DSNAME=%s\n",
                filenum, tudsnam);
        XMINFF (2, "DSORG=%s RECFM=%s "
                "LRECL=%d BLKSIZE=%d KEYLEN=%d DIRBLKS=%d\n",
                dsorg_name(tudsorg), recfm_name(turecfm),
                tulrecl, tublksz, tukeyln, tudirct);
        *dsorg = tudsorg[0];
        *recfm = turecfm[0];
        *lrecl = tulrecl;
        *blksz = tublksz;
        *keyln = tukeyln;
        *dirnm = tudirct;
    }

    return 0;
} /* end function process_inmr02 */

/*-------------------------------------------------------------------*/
/* Subroutine to process a control record other than INMR02          */
/* Input:                                                            */
/*      xbuf    Pointer to buffer containing control record          */
/*      xreclen Length of control record                             */
/* Output:                                                           */
/*      The return value is 0 if successful, or -1 if error.         */
/*-------------------------------------------------------------------*/
static int
process_inmrxx (BYTE *xbuf, int xreclen)
{
int             rc;                     /* Return code               */
int             bufpos;                 /* Position of TU in buffer  */
int             bufrem;                 /* Bytes remaining in buffer */
U16             tukey;                  /* Text unit key             */
U16             tunum;                  /* Number of text unit fields*/
#define MAXNUM  20                      /* Maximum number of fields  */
U16             fieldlen[MAXNUM];       /* Array of field lengths    */
BYTE           *fieldptr[MAXNUM];       /* Array of field pointers   */

    /* Point to the first text unit */
    bufpos = 6;
    bufrem = xreclen-6;

    /* Process each text unit */
    while (bufrem > 0)
    {
        /* Extract the next text unit */
        rc = next_tu (xbuf, bufpos, bufrem, &tukey, &tunum,
                MAXNUM, fieldlen, fieldptr);
        if (rc < 0)
        {
            XMERRF ("Invalid text unit at offset %4.4X\n",
                    bufpos + 2);
            return -1;
        }

        bufpos += rc;
        bufrem -= rc;

    } /* end while(bufrem) */

    return 0;
} /* end function process_inmrxx */

/*-------------------------------------------------------------------*/
/* Subroutine to process a COPYR1 header record                      */
/* Input:                                                            */
/*      xbuf    Pointer to buffer containing header record           */
/*      xreclen Length of header record                              */
/* Output:                                                           */
/*      The return value is the number of tracks per cylinder,       */
/*      or -1 if an error occurred.                                  */
/*-------------------------------------------------------------------*/
static int
process_copyr1 (BYTE *xbuf, int xreclen)
{
COPYR1 *copyr1 = (COPYR1*)xbuf;         /* -> COPYR1 header record   */
U16     blksize;                        /* Block size                */
U16     lrecl;                          /* Logical record length     */
BYTE    keylen;                         /* Key length                */
U16     cyls;                           /* Number of cylinders       */
U16     heads;                          /* Number of tracks/cylinder */

    /* Check COPYR1 record for correct length */
    if (xreclen != sizeof(COPYR1)
        && xreclen != sizeof(COPYR1) - 4)
    {
        XMERR ("COPYR1 record length is invalid\n");
        return -1;
    }

    /* Check that COPYR1 header identifier is correct */
    if (memcmp(copyr1->hdrid, COPYR1_HDRID, 3) != 0)
    {
        XMERR ("COPYR1 header identifier not correct\n");
        return -1;
    }

    /* Check that the dataset is an old format unload */
    if ((copyr1->uldfmt & COPYR1_ULD_FORMAT)
            != COPYR1_ULD_FORMAT_OLD)
    {
        XMERR ("COPYR1 unload format is unsupported\n");
        return -1;
    }

    blksize = (copyr1->ds1blkl[0] << 8) | copyr1->ds1blkl[1];
    lrecl = (copyr1->ds1lrecl[0] << 8) | copyr1->ds1lrecl[1];
    keylen = copyr1->ds1keyl;

    /* Display original dataset information */
    XMINFF (2, "Original dataset: "
            "DSORG=%s RECFM=%s LRECL=%d BLKSIZE=%d KEYLEN=%d\n",
            dsorg_name(copyr1->ds1dsorg),
            recfm_name(&copyr1->ds1recfm),
            lrecl, blksize, keylen);

    XMINFF (2, "Dataset was unloaded from device type "
            "%2.2X%2.2X%2.2X%2.2X (%s)\n",
            copyr1->ucbtype[0], copyr1->ucbtype[1],
            copyr1->ucbtype[2], copyr1->ucbtype[3],
            dasd_name(copyr1->ucbtype));

    cyls = (copyr1->cyls[0] << 8) | copyr1->cyls[1];
    heads = (copyr1->heads[0] << 8) | copyr1->heads[1];

    XMINFF (2, "Original device has %d cyls and %d heads\n",
            cyls, heads);

    return heads;
} /* end function process_copyr1 */

/*-------------------------------------------------------------------*/
/* Subroutine to process a COPYR2 header record                      */
/* Input:                                                            */
/*      xbuf    Pointer to buffer containing header record           */
/*      xreclen Length of header record                              */
/*      xarray  Pointer to array to receive 1-16 extent descriptions */
/* Output:                                                           */
/*      The return value is the number of extents,                   */
/*      or -1 if an error occurred.                                  */
/*                                                                   */
/* Extent information is listed if infolvl is 4 or greater.          */
/*-------------------------------------------------------------------*/
static int
process_copyr2 (BYTE *xbuf, int xreclen, EXTDESC xarray[])
{
COPYR2 *copyr2 = (COPYR2*)xbuf;         /* -> COPYR2 header record   */
int     numext;                         /* Number of extents         */
int     i;                              /* Array subscript           */

    /* Check COPYR2 record for correct length */
    if (xreclen != sizeof(COPYR2))
    {
        XMERR ("COPYR2 record length is invalid\n");
        return -1;
    }

    /* Get number of extents from DEB basic section */
    numext = copyr2->debbasic[0];
    if (numext < 1 || numext > 16)
    {
        XMERRF ("Invalid number of extents %d\n", numext);
        return -1;
    }

    /* Copy each extent descriptor into the array */
    for (i = 0; i < numext; i++)
    {
        xarray[i].bcyl = (copyr2->debxtent[i][6] << 8)
                        | copyr2->debxtent[i][7];
        xarray[i].btrk = (copyr2->debxtent[i][8] << 8)
                        | copyr2->debxtent[i][9];
        xarray[i].ecyl = (copyr2->debxtent[i][10] << 8)
                        | copyr2->debxtent[i][11];
        xarray[i].etrk = (copyr2->debxtent[i][12] << 8)
                        | copyr2->debxtent[i][13];
        xarray[i].ntrk = (copyr2->debxtent[i][14] << 8)
                        | copyr2->debxtent[i][15];

        XMINFF (4, "Extent %d: Begin CCHH=%4.4X%4.4X "
                "End CCHH=%4.4X%4.4X Tracks=%4.4X\n",
                i, xarray[i].bcyl, xarray[i].btrk,
                xarray[i].ecyl, xarray[i].etrk, xarray[i].ntrk);

    } /* end for(i) */

    /* Return number of extents */
    return numext;
} /* end function process_copyr2 */

/*-------------------------------------------------------------------*/
/* Subroutine to process a directory block record                    */
/* Input:                                                            */
/*      xbuf    Pointer to directory block                           */
/*      blklen  Length of directory block                            */
/*      cyl     Cylinder number of directory block in output file    */
/*      head    Head number of directory block in output file        */
/*      rec     Record number of directory block in output file      */
/*      blkpp   Address of pointer to chain field of previous block  */
/* Output:                                                           */
/*      dirblu  Number of bytes used in directory block              */
/*      The return value is 0 if successful, 1 if end of directory,  */
/*      or -1 if an error occurred.                                  */
/*                                                                   */
/* Each directory block is saved in a chained list.                  */
/* The copy of the data block is updated with the cylinder, head,    */
/* and record number of the directory block in the output file.      */
/* Directory information is listed if infolvl is 3 or greater.       */
/*-------------------------------------------------------------------*/
static int
process_dirblk (DATABLK *xbuf, int blklen, int cyl, int head, int rec,
                DATABLK ***blkpp, int *dirblu)
{
int             size;                   /* Size of directory entry   */
int             i, j;                   /* Array subscripts          */
int             k;                      /* Userdata halfword count   */
DATABLK        *blkp;                   /* -> Copy of directory block*/
BYTE           *dirptr;                 /* -> Next byte within block */
int             dirrem;                 /* Number of bytes remaining */
PDSDIR         *dirent;                 /* -> Directory entry        */
BYTE            memname[9];             /* Member name (ASCIIZ)      */
BYTE            c, hex[49], chars[25];  /* Character work areas      */

    /* Check for end of directory */
    if (blklen == 12 && memcmp(xbuf, twelvehex00, 12) == 0)
    {
        XMINFF (3, "End of directory\n");
        return 1;
    }

    /* Check directory block record for correct length */
    if (blklen != 276)
    {
        XMERR ("Directory block record length is invalid\n");
        return -1;
    }

    /* Obtain storage for a copy of the directory block */
    blkp = (DATABLK*)malloc(blklen);
    if (blkp == NULL)
    {
        XMERRF ("Cannot obtain storage for directory block: %s\n",
                strerror(errno));
        return -1;
    }

    /* Copy the directory block to the end of the chained list */
    memcpy (blkp, xbuf, blklen);
    **blkpp = blkp;
    blkp->header = 0;
    *blkpp = (DATABLK**)(&blkp->header);

    /* Update the CCHHR in the copy of the directory block */
    blkp->cyl[0] = (cyl >> 8) & 0xFF;
    blkp->cyl[1] = cyl & 0xFF;
    blkp->head[0] = (head >> 8) & 0xFF;
    blkp->head[1] = head & 0xFF;
    blkp->rec = rec;

    /* Load number of bytes in directory block */
    dirptr = xbuf->kdarea + 8;
    dirrem = (dirptr[0] << 8) | dirptr[1];
    if (dirrem < 2 || dirrem > 256)
    {
        XMERR ("Directory block byte count is invalid\n");
        return -1;
    }

    /* Return number of bytes used in directory block */
    *dirblu = dirrem;

    /* Point to first directory entry */
    dirptr += 2;
    dirrem -= 2;

    /* Process each directory entry */
    while (dirrem > 0)
    {
        /* Point to next directory entry */
        dirent = (PDSDIR*)dirptr;

        /* Test for end of directory */
        if (memcmp(dirent->pds2name, eighthexFF, 8) == 0)
            break;

        /* Extract the member name */
        make_asciiz (memname, sizeof(memname), dirent->pds2name, 8);

        /* Display the directory entry */
        XMINFF (3, "%s %-8.8s TTR=%2.2X%2.2X%2.2X ",
                (dirent->pds2indc & PDS2INDC_ALIAS) ?
                        " Alias" : "Member",
                memname, dirent->pds2ttrp[0],
                dirent->pds2ttrp[1], dirent->pds2ttrp[2]);

        /* Load the user data halfword count */
        k = dirent->pds2indc & PDS2INDC_LUSR;

        /* Print the user data */
        if (k > 0) XMINFF (3, "Userdata=");
        memset (hex, '\0', sizeof(hex));
        memset (chars, '\0', sizeof(chars));
        for (i = 0, j = 0; i < k*2; i++, j++)
        {
            if (i == 8 || i == 32 || i == 56)
            {
                if (i == 8)
                    XMINFF (3, "%-16.16s %-8.8s\n  ", hex, chars);
                else
                    XMINFF (3, "%-16.16s %-16.16s %16.16s %-24.24s\n  ",
                        hex, hex+16, hex+32, chars);
                memset (hex, '\0', sizeof(hex));
                memset (chars, '\0', sizeof(chars));
                j = 0;
            }
            sprintf(hex+2*j, "%2.2X", dirent->pds2usrd[i]);
            c = ebcdic_to_ascii[dirent->pds2usrd[i]];
            if (!isprint(c)) c = '.';
            chars[j] = c;
        } /* end for(i) */
        if (i <= 8)
            XMINFF (3, "%-16.16s %-8.8s\n", hex, chars);
        else
            XMINFF (3, "%-16.16s %-16.16s %-16.16s %-24.24s\n",
                hex, hex+16, hex+32, chars);

        /* Point to next directory entry */
        size = 12 + k*2;
        dirptr += size;
        dirrem -= size;
    }

    return 0;
} /* end function process_dirblk */

/*-------------------------------------------------------------------*/
/* Subroutine to replace a TTR in a PDS directory                    */
/* Input:                                                            */
/*      memname Member name (ASCIIZ)                                 */
/*      ttrptr  Pointer to TTR to be replaced                        */
/*      ttrtab  Pointer to TTR conversion table                      */
/*      numttr  Number of entries in TTR conversion table            */
/* Output:                                                           */
/*      The TTR is replaced using the TTR conversion table.          */
/*                                                                   */
/* Return value is 0 if successful, or -1 if TTR not in table.       */
/* Directory information is listed if infolvl is 3 or greater.       */
/*-------------------------------------------------------------------*/
static int
replace_ttr (BYTE *memname, BYTE *ttrptr, TTRCONV *ttrtab, int numttr)
{
int             i;                      /* Array subscript           */

    /* Search for the TTR in the conversion table */
    for (i = 0; i < numttr; i++)
    {
        if (memcmp(ttrptr, ttrtab[i].origttr, 3) == 0)
        {
            XMINFF (4, "Member %s TTR=%2.2X%2.2X%2.2X "
                    "replaced by TTR=%2.2X%2.2X%2.2X\n",
                    memname, ttrptr[0], ttrptr[1], ttrptr[2],
                    ttrtab[i].outpttr[0], ttrtab[i].outpttr[1],
                    ttrtab[i].outpttr[2]);
            memcpy (ttrptr, ttrtab[i].outpttr, 3);
            return 0;
        }
    }

    /* Return error if TTR not found in conversion table */
    XMERRF ("Member %s TTR=%2.2X%2.2X%2.2X not found in dataset\n",
            memname, ttrptr[0], ttrptr[1], ttrptr[2]);
    return -1;

} /* end function replace_ttr */

/*-------------------------------------------------------------------*/
/* Subroutine to update a directory block record                     */
/* Input:                                                            */
/*      xbuf    Pointer to directory block                           */
/*      ttrtab  Pointer to TTR conversion table                      */
/*      numttr  Number of entries in TTR conversion table            */
/* Output:                                                           */
/*      Each original TTR in the directory block is replaced by the  */
/*      corresponding output TTR from the TTR conversion table.      */
/*                                                                   */
/* Return value is 0 if successful, or -1 if any directory entry     */
/* contains a TTR which is not found in the TTR conversion table.    */
/*-------------------------------------------------------------------*/
static int
update_dirblk (DATABLK *xbuf, TTRCONV *ttrtab, int numttr)
{
int             rc;                     /* Return code               */
int             size;                   /* Size of directory entry   */
int             k;                      /* Userdata halfword count   */
BYTE           *dirptr;                 /* -> Next byte within block */
int             dirrem;                 /* Number of bytes remaining */
PDSDIR         *dirent;                 /* -> Directory entry        */
BYTE           *ttrptr;                 /* -> User TTR               */
int             n;                      /* Number of user TTRs       */
BYTE            memname[9];             /* Member name (ASCIIZ)      */

    /* Load number of bytes in directory block */
    dirptr = xbuf->kdarea + 8;
    dirrem = (dirptr[0] << 8) | dirptr[1];
    if (dirrem < 2 || dirrem > 256)
    {
        XMERR ("Directory block byte count is invalid\n");
        return -1;
    }

    /* Point to first directory entry */
    dirptr += 2;
    dirrem -= 2;

    /* Process each directory entry */
    while (dirrem > 0)
    {
        /* Point to next directory entry */
        dirent = (PDSDIR*)dirptr;

        /* Test for end of directory */
        if (memcmp(dirent->pds2name, eighthexFF, 8) == 0)
            break;

        /* Extract the member name */
        make_asciiz (memname, sizeof(memname), dirent->pds2name, 8);

        /* Replace the member TTR */
        rc = replace_ttr (memname, dirent->pds2ttrp, ttrtab, numttr);
        if (rc < 0) return -1;

        /* Load the number of user TTRs */
        n = (dirent->pds2indc & PDS2INDC_NTTR) >> PDS2INDC_NTTR_SHIFT;

        /* Replace the user TTRs */
        ttrptr = dirent->pds2usrd;
        while (n > 0)
        {
            rc = replace_ttr (memname, ttrptr, ttrtab, numttr);
            if (rc < 0) return -1;
            ttrptr += 4;
            n--;
        } /* end while(n) */

        /* Load the user data halfword count */
        k = dirent->pds2indc & PDS2INDC_LUSR;

        /* Point to next directory entry */
        size = 12 + k*2;
        dirptr += size;
        dirrem -= size;
    }

    return 0;
} /* end function update_dirblk */

/*-------------------------------------------------------------------*/
/* Subroutine to read an XMIT file and write to DASD image file      */
/* Input:                                                            */
/*      xfname  XMIT input file name                                 */
/*      ofname  DASD image file name                                 */
/*      ofd     DASD image file descriptor                           */
/*      trkbuf  Pointer to output track buffer                       */
/*      devtype Output device type                                   */
/*      heads   Output device number of tracks per cylinder          */
/*      trklen  Output device virtual track length                   */
/*      outcyl  Output starting cylinder number                      */
/*      outhead Output starting head number                          */
/*      maxtrks Maximum extent size in tracks                        */
/* Output:                                                           */
/*      odsorg  Dataset organization                                 */
/*      orecfm  Record format                                        */
/*      olrecl  Logical record length                                */
/*      oblksz  Block size                                           */
/*      okeyln  Key length                                           */
/*      dirblu  Bytes used in last directory block                   */
/*      lastrec Record number of last block written                  */
/*      trkbal  Number of bytes remaining on last track              */
/*      numtrks Number of tracks written                             */
/*      nxtcyl  Starting cylinder number for next dataset            */
/*      nxthead Starting head number for next dataset                */
/*-------------------------------------------------------------------*/
static int
process_xmit_file (BYTE *xfname, BYTE *ofname, int ofd, BYTE *trkbuf,
                U16 devtype, int heads, int trklen,
                int outcyl, int outhead, int maxtrks,
                BYTE *odsorg, BYTE *orecfm,
                int *olrecl, int *oblksz, int *okeyln,
                int *dirblu, int *lastrec, int *trkbal,
                int *numtrks, int *nxtcyl, int *nxthead)
{
int             rc = 0;                 /* Return code               */
int             xfd;                    /* XMIT file descriptor      */
BYTE           *xbuf;                   /* -> Logical record buffer  */
int             xreclen;                /* Logical record length     */
BYTE            xctl;                   /* 0x20=Control record       */
BYTE            xrecname[8];            /* XMIT control record name  */
int             datarecn = 0;           /* Data record counter       */
BYTE            dsorg;                  /* Dataset organization      */
BYTE            recfm;                  /* Dataset record format     */
U16             lrecl;                  /* Dataset record length     */
U16             blksz;                  /* Dataset block size        */
U16             keyln;                  /* Dataset key length        */
U16             dirnm;                  /* Number of directory blocks*/
int             enddir = 0;             /* 1=End of directory found  */
BYTE           *blkptr;                 /* -> Data block in record   */
DATABLK        *datablk;                /* -> Data block             */
int             blktrk;                 /* Data block relative track */
int             blkcyl;                 /* Data block cylinder number*/
int             blkhead;                /* Data block head number    */
int             blkrec;                 /* Data block record number  */
int             keylen;                 /* Key length of data block  */
int             datalen;                /* Data length of data block */
int             blklen;                 /* Total length of data block*/
int             origheads = 0;          /* Number of tracks/cylinder
                                           on original dataset       */
int             numext = 0;             /* Number of extents         */
EXTDESC         xarray[16];             /* Extent descriptor array   */
DATABLK        *dirblkp;                /* -> First directory block  */
DATABLK       **dirblkpp;               /* -> Directory chain pointer*/
int             dirblkn;                /* #of directory blocks read */
int             outusedv = 0;           /* Output bytes used on track
                                           of virtual device         */
int             outusedr = 0;           /* Output bytes used on track
                                           of real device            */
int             outtrkbr = 0;           /* Output bytes remaining on
                                           track of real device      */
int             outtrk = 0;             /* Output relative track     */
int             outrec = 0;             /* Output record number      */
#define MAXTTR  10000                   /* TTR conversion table size */
TTRCONV        *ttrtab;                 /* -> TTR conversion table   */
int             numttr = 0;             /* TTR table array index     */

    /* Open the input file */
    xfd = open (xfname, O_RDONLY);
    if (xfd < 0)
    {
        XMERRF ("Cannot open %s: %s\n",
                xfname, strerror(errno));
        return -1;
    }

    /* Obtain the input logical record buffer */
    xbuf = malloc (65536);
    if (xbuf == NULL)
    {
        XMERRF ("Cannot obtain input buffer: %s\n",
                strerror(errno));
        close (xfd);
        return -1;
    }

    /* Obtain storage for the TTR conversion table */
    ttrtab = (TTRCONV*)malloc (sizeof(TTRCONV)*MAXTTR);
    if (ttrtab == NULL)
    {
        XMERRF ("Cannot obtain storage for TTR table: %s\n",
                strerror(errno));
        free (xbuf);
        close (xfd);
        return -1;
    }

    /* Display the file information message */
    XMINFF (1, "Processing file %s\n", xfname);

    /* Initialize the directory block chain */
    dirblkp = NULL;
    dirblkpp = &dirblkp;
    dirblkn = 0;

    /* Read each logical record */
    while (1)
    {
        rc = read_xmit_rec (xfd, xfname, xbuf, &xctl);
        if (rc < 0) return -1;
        xreclen = rc;

        /* Process control records */
        if (xctl)
        {
            /* Extract the control record name */
            make_asciiz (xrecname, sizeof(xrecname), xbuf, 6);
            XMINFF (4, "Control record: %s length %d\n",
                        xrecname, xreclen);

            /* Exit if control record is a trailer record */
            if (strcmp(xrecname, "INMR06") == 0)
                break;

            /* Process control record according to type */
            if (strcmp(xrecname, "INMR02") == 0)
            {
                rc = process_inmr02 (xbuf, xreclen, &dsorg, &recfm,
                                     &lrecl, &blksz, &keyln, &dirnm);
                if (rc < 0) return -1;
            }
            else
            {
                rc = process_inmrxx (xbuf, xreclen);
                if (rc < 0) return -1;
            }

            /* Reset the data counter if data control record */
            if (strcmp(xrecname, "INMR03") == 0)
            {
                datarecn = 0;
            }

            /* Loop to get next record */
            continue;

        } /* end if(xctl) */

        /* Process data records */
        datarecn++;
        XMINFF (4, "Data record: length %d\n", xreclen);
        if (infolvl >= 5) data_dump (xbuf, xreclen);

        /* Process IEBCOPY header record 1 */
        if (datarecn == 1)
        {
            origheads = process_copyr1 (xbuf, xreclen);
            if (origheads < 0) exit(1);
            continue;
        }

        /* Process IEBCOPY header record 2 */
        if (datarecn == 2)
        {
            numext = process_copyr2 (xbuf, xreclen, xarray);
            if (numext < 0) exit(1);
            continue;
        }

        /* Process each data block in data record */
        blkptr = xbuf;
        while (xreclen > 0)
        {
            /* Compute the length of the block */
            datablk = (DATABLK*)blkptr;
            blkcyl = (datablk->cyl[0] << 8)
                    | datablk->cyl[1];
            blkhead = (datablk->head[0] << 8)
                    | datablk->head[1];
            blkrec = datablk->rec;
            keylen = datablk->klen;
            datalen = (datablk->dlen[0] << 8)
                    | datablk->dlen[1];
            blklen = 12 + keylen + datalen;

            /* Calculate the TTR in the original dataset */
            blktrk = (enddir == 0) ? 0 :
                        calculate_ttr (blkcyl, blkhead,
                                origheads, numext, xarray);

            /* Write the data block to the output file */
            rc = write_block (ofd, ofname, datablk, keylen, datalen,
                        devtype, heads, trklen, maxtrks,
                        trkbuf, &outusedv, &outusedr, &outtrkbr,
                        &outtrk, &outcyl, &outhead, &outrec);
            if (rc < 0)
            {
                XMERRF ("Input record CCHHR=%4.4X%4.4X%2.2X "
                        "(TTR=%4.4X%2.2X) KL=%d DL=%d\n",
                        blkcyl, blkhead, blkrec,
                        blktrk, blkrec, keylen, datalen);
                return -1;
            }

            XMINFF (4, "CCHHR=%4.4X%4.4X%2.2X "
                        "(TTR=%4.4X%2.2X) KL=%d DL=%d "
                        "-> CCHHR=%4.4X%4.4X%2.2X "
                        "(TTR=%4.4X%2.2X)\n",
                        blkcyl, blkhead, blkrec,
                        blktrk, blkrec, keylen, datalen,
                        outcyl, outhead, outrec, outtrk, outrec);

            /* Process directory block or member block */
            if (enddir == 0)
            {
                rc = process_dirblk (datablk, blklen,
                                    outcyl, outhead, outrec,
                                    &dirblkpp, dirblu);
                if (rc < 0) return -1;
                enddir = rc;

                /* Count the number of directory blocks read */
                if (enddir == 0) dirblkn++;
            }
            else /* Not a directory block */
            {
                /* Check that TTR conversion table is not full */
                if (numttr >= MAXTTR)
                {
                    XMERRF ("TTR count exceeds %d, increase MAXTTR\n",
                            MAXTTR);
                    return -1;
                }

                /* Add an entry to the TTR conversion table */
                ttrtab[numttr].origttr[0] = (blktrk >> 8) & 0xFF;
                ttrtab[numttr].origttr[1] = blktrk & 0xFF;
                ttrtab[numttr].origttr[2] = blkrec;
                ttrtab[numttr].outpttr[0] = (outtrk >> 8) & 0xFF;
                ttrtab[numttr].outpttr[1] = outtrk & 0xFF;
                ttrtab[numttr].outpttr[2] = outrec;
                numttr++;
            }

            /* Point to next data block in data record */
            xreclen -= blklen;
            blkptr += blklen;

        } /* end while(xreclen) */

    } /* end while(1) */

    /* Return the last record number and track balance */
    *lastrec = outrec;
    *trkbal = outtrkbr;

    /* Write any data remaining in track buffer */
    rc = write_track (ofd, ofname, heads, trklen, trkbuf,
                    &outusedv, &outtrk, &outcyl, &outhead);
    if (rc < 0) return -1;

    /* Update the directory and rewrite to output file */
    for (datablk = dirblkp; datablk != NULL;
                datablk = (DATABLK*)(datablk->header))
    {
        /* Update TTR pointers in this directory block */
        rc = update_dirblk (datablk, ttrtab, numttr);
        if (rc < 0) return -1;

        /* Rewrite the updated directory block */
        blkcyl = (datablk->cyl[0] << 8) | datablk->cyl[1];
        blkhead = (datablk->head[0] << 8) | datablk->head[1];
        blkrec = datablk->rec;
        keylen = datablk->klen;
        datalen = (datablk->dlen[0] << 8) | datablk->dlen[1];

        rc = update_block (ofd, ofname, datablk, blkcyl, blkhead,
                        blkrec, keylen, datalen, heads, trklen);
        if (rc < 0) return -1;

    } /* end for(datablk) */

    /* Close input file and release buffers */
    close (xfd);
    while (dirblkp != NULL)
    {
        datablk = (DATABLK*)(dirblkp->header);
        free (dirblkp);
        dirblkp = datablk;
    }
    free (xbuf);
    free (ttrtab);

    /* Return the dataset attributes */
    *odsorg = dsorg;
    *orecfm = recfm;
    *olrecl = lrecl;
    *oblksz = blksz;
    *okeyln = keyln;

    /* Return number of tracks and starting address of next dataset */
    *numtrks = outtrk;
    *nxtcyl = outcyl;
    *nxthead = outhead;
    return 0;

} /* end function process_xmit_file */

/*-------------------------------------------------------------------*/
/* Subroutine to initialize a SYSCTLG dataset as an OS CVOL          */
/* Input:                                                            */
/*      ofname  DASD image file name                                 */
/*      ofd     DASD image file descriptor                           */
/*      volser  Volume serial number                                 */
/*      trkbuf  Pointer to output track buffer                       */
/*      devtype Output device type                                   */
/*      heads   Output device number of tracks per cylinder          */
/*      trklen  Output device virtual track length                   */
/*      outcyl  Output starting cylinder number                      */
/*      outhead Output starting head number                          */
/*      extsize Extent size in tracks                                */
/* Output:                                                           */
/*      lastrec Record number of last block written                  */
/*      trkbal  Number of bytes remaining on last track              */
/*      numtrks Number of tracks written                             */
/*      nxtcyl  Starting cylinder number for next dataset            */
/*      nxthead Starting head number for next dataset                */
/* Note:                                                             */
/*      This subroutine builds a minimal SYSCTLG containing only     */
/*      the entries required on an OS/360 IPL volume.                */
/*-------------------------------------------------------------------*/
static int
cvol_initialize (BYTE *ofname, int ofd, BYTE *volser, BYTE *trkbuf,
                U16 devtype, int heads, int trklen,
                int outcyl, int outhead, int extsize,
                int *lastrec, int *trkbal,
                int *numtrks, int *nxtcyl, int *nxthead)
{
int             rc;                     /* Return code               */
int             i;                      /* Array subscript           */
int             keylen;                 /* Key length of data block  */
int             datalen;                /* Data length of data block */
int             outusedv = 0;           /* Output bytes used on track
                                           of virtual device         */
int             outusedr = 0;           /* Output bytes used on track
                                           of real device            */
int             outtrkbr = 0;           /* Output bytes remaining on
                                           track of real device      */
int             outtrk = 0;             /* Output relative track     */
int             outrec = 0;             /* Output record number      */
int             blkptrk;                /* Number of blocks per track*/
int             totblks;                /* Number of blocks in CVOL  */
int             bytes;                  /* Bytes used in this block  */
U32             ucbtype;                /* UCB device type           */
PDSDIR         *catent;                 /* -> Catalog entry          */
DATABLK         datablk;                /* Data block                */
#define NUM_SYS1_DATASETS       8       /* Number of SYS1 datasets   */
static BYTE    *sys1name[NUM_SYS1_DATASETS] =
                {"DUMP", "IMAGELIB", "LINKLIB", "NUCLEUS",
                "PARMLIB", "PROCLIB", "SAMPLIB", "SYSJOBQE"};

    /* Set the key length and data length for SYSCTLG dataset */
    keylen = 8;
    datalen = 256;

    /* Obtain the number of blocks which will fit on a track */
    capacity_calc (devtype, 0, keylen, datalen, NULL, NULL,
                    NULL, NULL, NULL, NULL, NULL,
                    NULL, NULL, &blkptrk, NULL, NULL);

    /* Calculate the total number of blocks in the catalog */
    totblks = extsize * blkptrk;

    /* Get the UCB device type */
    ucbtype = ucbtype_code (devtype);

    /*-----------------------------------*/
    /* Initialize the volume index block */
    /*-----------------------------------*/
    memset (datablk.kdarea, 0, keylen + datalen);

    /* The key field contains all X'FF' */
    memcpy (datablk.kdarea, eighthexFF, 8);

    /* The first entry begins after the 2 byte count field */
    bytes = 2;
    catent = (PDSDIR*)(datablk.kdarea + keylen + bytes);

    /* Build the volume index control entry (VICE) */

    /* The VICE name is X'0000000000000001' */
    memcpy (catent->pds2name, cvol_low_key, 8);

    /* Set TTR to highest block in volume index, i.e. X'000001' */
    catent->pds2ttrp[0] = 0;
    catent->pds2ttrp[1] = 0;
    catent->pds2ttrp[2] = 1;

    /* Indicator byte X'05' means 5 user halfwords follow, and
       uniquely identifies this catalog entry as a VICE */
    catent->pds2indc = 5;

    /* Set the TTR of the last block of the catalog */
    catent->pds2usrd[0] = ((extsize - 1) >> 8) & 0xFF;
    catent->pds2usrd[1] = (extsize - 1) & 0xFF;
    catent->pds2usrd[2] = blkptrk;

    /* The next byte contains the number of blocks per track */
    catent->pds2usrd[3] = blkptrk;

    /* The remaining 6 bytes of userdata are zeroes */
    catent->pds2usrd[4] = 0;
    catent->pds2usrd[5] = 0;
    catent->pds2usrd[6] = 0;
    catent->pds2usrd[7] = 0;
    catent->pds2usrd[8] = 0;
    catent->pds2usrd[9] = 0;

    /* Increment bytes used by the length of the VICE */
    bytes += 22;
    catent = (PDSDIR*)(datablk.kdarea + keylen + bytes);

    /* Build the index pointer for SYS1 */
    convert_to_ebcdic (catent->pds2name, 8, "SYS1");

    /* Set TTR of the SYS1 index block, i.e. X'000002' */
    catent->pds2ttrp[0] = 0;
    catent->pds2ttrp[1] = 0;
    catent->pds2ttrp[2] = 2;

    /* Indicator byte X'00' means no user halfwords follow, and
       uniquely identifies this catalog entry as an index pointer */
    catent->pds2indc = 0;

    /* Increment bytes used by the length of the index pointer */
    bytes += 12;
    catent = (PDSDIR*)(datablk.kdarea + keylen + bytes);

    /* Set the last entry in block marker */
    memcpy (catent->pds2name, eighthexFF, 8);

    /* Increment bytes used by the last entry marker */
    bytes += 12;
    catent = (PDSDIR*)(datablk.kdarea + keylen + bytes);

    /* Set the number of bytes used in this block */
    datablk.kdarea[keylen+0] = (bytes >> 8) & 0xFF;
    datablk.kdarea[keylen+1] = bytes & 0xFF;

    /* Write the volume index block to the output file */
    rc = write_block (ofd, ofname, &datablk, keylen, datalen,
                devtype, heads, trklen, extsize,
                trkbuf, &outusedv, &outusedr, &outtrkbr,
                &outtrk, &outcyl, &outhead, &outrec);
    if (rc < 0) return -1;

    XMINFF (4, "Catalog block at cyl %d head %d rec %d\n",
            outcyl, outhead, outrec);
    if (infolvl >= 5) data_dump (datablk.kdarea, keylen + datalen);

    /* Count number of blocks written */
    totblks--;

    /*---------------------------------*/
    /* Initialize the SYS1 index block */
    /*---------------------------------*/
    memset (datablk.kdarea, 0, keylen + datalen);

    /* The key field contains all X'FF' */
    memcpy (datablk.kdarea, eighthexFF, 8);

    /* The first entry begins after the 2 byte count field */
    bytes = 2;
    catent = (PDSDIR*)(datablk.kdarea + keylen + bytes);

    /* Build the index control entry (ICE) */

    /* The ICE name is X'0000000000000001' */
    memcpy (catent->pds2name, cvol_low_key, 8);

    /* Set TTR to highest block in this index, i.e. X'000002' */
    catent->pds2ttrp[0] = 0;
    catent->pds2ttrp[1] = 0;
    catent->pds2ttrp[2] = 2;

    /* Indicator byte X'03' means 3 user halfwords follow, and
       uniquely identifies this catalog entry as an ICE */
    catent->pds2indc = 3;

    /* Set the TTR of this block */
    catent->pds2usrd[0] = 0;
    catent->pds2usrd[1] = 0;
    catent->pds2usrd[2] = 2;

    /* The next byte contains the alias count */
    catent->pds2usrd[3] = 0;

    /* The remaining 2 bytes of userdata are zeroes */
    catent->pds2usrd[4] = 0;
    catent->pds2usrd[5] = 0;

    /* Increment bytes used by the length of the ICE */
    bytes += 18;
    catent = (PDSDIR*)(datablk.kdarea + keylen + bytes);

    /* Build the dataset pointers for SYS1.xxxxxxxx datasets */
    for (i = 0; i < NUM_SYS1_DATASETS; i++)
    {
        /* Set the name of the dataset pointer entry */
        convert_to_ebcdic (catent->pds2name, 8, sys1name[i]);

        /* Set the TTR to zero */
        catent->pds2ttrp[0] = 0;
        catent->pds2ttrp[1] = 0;
        catent->pds2ttrp[2] = 0;

        /* Indicator byte X'07' means 7 user halfwords follow, and
           uniquely identifies the entry as a dataset pointer */
        catent->pds2indc = 7;

        /* The next two bytes contain the volume count (X'0001') */
        catent->pds2usrd[0] = 0;
        catent->pds2usrd[1] = 1;

        /* The next four bytes contain the UCB type */
        catent->pds2usrd[2] = (ucbtype >> 24) & 0xFF;
        catent->pds2usrd[3] = (ucbtype >> 16) & 0xFF;
        catent->pds2usrd[4] = (ucbtype >> 8) & 0xFF;
        catent->pds2usrd[5] = ucbtype & 0xFF;

        /* The next six bytes contain the volume serial number */
        convert_to_ebcdic (catent->pds2usrd+6, 6, volser);

        /* The next two bytes contain the volume seq.no. (X'0001') */
        catent->pds2usrd[12] = 0;
        catent->pds2usrd[13] = 1;

        /* Increment bytes used by the length of the dataset pointer */
        bytes += 26;
        catent = (PDSDIR*)(datablk.kdarea + keylen + bytes);

    } /* end for(i) */

    /* Set the last entry in block marker */
    memcpy (catent->pds2name, eighthexFF, 8);

    /* Increment bytes used by the last entry marker */
    bytes += 12;
    catent = (PDSDIR*)(datablk.kdarea + keylen + bytes);

    /* Set the number of bytes used in this block */
    datablk.kdarea[keylen+0] = (bytes >> 8) & 0xFF;
    datablk.kdarea[keylen+1] = bytes & 0xFF;

    /* Write the index block to the output file */
    rc = write_block (ofd, ofname, &datablk, keylen, datalen,
                devtype, heads, trklen, extsize,
                trkbuf, &outusedv, &outusedr, &outtrkbr,
                &outtrk, &outcyl, &outhead, &outrec);
    if (rc < 0) return -1;

    XMINFF (4, "Catalog block at cyl %d head %d rec %d\n",
            outcyl, outhead, outrec);
    if (infolvl >= 5) data_dump (datablk.kdarea, keylen + datalen);

    /* Count number of blocks written */
    totblks--;

    /*--------------------------------------------*/
    /* Initialize remaining unused catalog blocks */
    /*--------------------------------------------*/
    while (totblks > 0)
    {
        memset (datablk.kdarea, 0, keylen + datalen);

        /* The key field contains all X'FF' */
        memcpy (datablk.kdarea, eighthexFF, 8);

        /* The first entry begins after the 2 byte count field */
        bytes = 2;
        catent = (PDSDIR*)(datablk.kdarea + keylen + bytes);

        /* Set the last entry in block marker */
        memcpy (catent->pds2name, eighthexFF, 8);

        /* Increment bytes used by the last entry marker */
        bytes += 12;
        catent = (PDSDIR*)(datablk.kdarea + keylen + bytes);

        /* Set the number of bytes used in this block */
        datablk.kdarea[keylen+0] = (bytes >> 8) & 0xFF;
        datablk.kdarea[keylen+1] = bytes & 0xFF;

        /* Write the volume index block to the output file */
        rc = write_block (ofd, ofname, &datablk, keylen, datalen,
                    devtype, heads, trklen, extsize,
                    trkbuf, &outusedv, &outusedr, &outtrkbr,
                    &outtrk, &outcyl, &outhead, &outrec);
        if (rc < 0) return -1;

        XMINFF (4, "Catalog block at cyl %d head %d rec %d\n",
                outcyl, outhead, outrec);
        if (infolvl >= 5) data_dump (datablk.kdarea, keylen + datalen);

        /* Count number of blocks written */
        totblks--;

    } /* end while(totblks) */

    /* Set the last record number to X'FF' so that OS/360 catalog
       management routines can recognize that the CVOL has been
       initialized by detecting X'FF' at ds1lstar+2 in the VTOC */
    *lastrec = 0xFF;

    /* Return the track balance */
    *trkbal = outtrkbr;

    /* Write data remaining in track buffer */
    rc = write_track (ofd, ofname, heads, trklen, trkbuf,
                    &outusedv, &outtrk, &outcyl, &outhead);
    if (rc < 0) return -1;

    /* Return number of tracks and starting address of next dataset */
    *numtrks = outtrk;
    *nxtcyl = outcyl;
    *nxthead = outhead;
    return 0;

} /* end function cvol_initialize */

/*-------------------------------------------------------------------*/
/* Subroutine to initialize a LOGREC dataset with IFCDIP00 header    */
/* Input:                                                            */
/*      ofname  DASD image file name                                 */
/*      ofd     DASD image file descriptor                           */
/*      trkbuf  Pointer to output track buffer                       */
/*      devtype Output device type                                   */
/*      heads   Output device number of tracks per cylinder          */
/*      trklen  Output device virtual track length                   */
/*      outcyl  Output starting cylinder number                      */
/*      outhead Output starting head number                          */
/*      extsize Extent size in tracks                                */
/* Output:                                                           */
/*      lastrec Record number of last block written                  */
/*      trkbal  Number of bytes remaining on last track              */
/*      numtrks Number of tracks written                             */
/*      nxtcyl  Starting cylinder number for next dataset            */
/*      nxthead Starting head number for next dataset                */
/*-------------------------------------------------------------------*/
static int
dip_initialize (BYTE *ofname, int ofd, BYTE *trkbuf,
                U16 devtype, int heads, int trklen,
                int outcyl, int outhead, int extsize,
                int *lastrec, int *trkbal,
                int *numtrks, int *nxtcyl, int *nxthead)
{
int             rc;                     /* Return code               */
int             keylen;                 /* Key length of data block  */
int             datalen;                /* Data length of data block */
int             outusedv = 0;           /* Output bytes used on track
                                           of virtual device         */
int             outusedr = 0;           /* Output bytes used on track
                                           of real device            */
int             outtrkbr = 0;           /* Output bytes remaining on
                                           track of real device      */
int             outtrk = 0;             /* Output relative track     */
int             outrec = 0;             /* Output record number      */
int             remlen;                 /* Bytes remaining on 1st trk*/
int             physlen;                /* Physical track length     */
int             lasthead;               /* Highest head on cylinder  */
int             endcyl;                 /* Extent end cylinder       */
int             endhead;                /* Extent end head           */
int             trklen90;               /* 90% of track length       */
int             cyl90;                  /* 90% full cylinder number  */
int             head90;                 /* 90% full head number      */
int             reltrk90;               /* 90% full relative track   */
DIPHDR         *diphdr;                 /* -> Record in data block   */
DATABLK         datablk;                /* Data block                */

    /* Set the key length and data length for the header record */
    keylen = 0;
    datalen = sizeof(DIPHDR);

    /* Obtain the physical track size and the track balance
       remaining on the first track after the header record */
    capacity_calc (devtype, 0, keylen, datalen, NULL, &remlen,
                    &physlen, NULL, NULL, NULL, NULL, NULL,
                    NULL, NULL, NULL, NULL);

    /* Calculate the end of extent cylinder and head */
    lasthead = heads - 1;
    endcyl = outcyl;
    endhead = outhead + extsize - 1;
    while (endhead >= heads)
    {
        endhead -= heads;
        endcyl++;
    }

    /* Calculate the 90% full cylinder and head */
    trklen90 = physlen * 9 / 10;
    reltrk90 = extsize * trklen90 / physlen;
    if (reltrk90 == 0) reltrk90 = 1;
    cyl90 = outcyl;
    head90 = outhead + reltrk90 - 1;
    while (head90 >= heads)
    {
        head90 -= heads;
        cyl90++;
    }

    /* Initialize the DIP header record */
    diphdr = (DIPHDR*)(datablk.kdarea);
    memset (diphdr, 0, sizeof(DIPHDR));
    diphdr->recid[0] = 0xFF;
    diphdr->recid[1] = 0xFF;
    diphdr->bcyl[0] = (outcyl >> 8) & 0xFF;
    diphdr->bcyl[1] = outcyl & 0xFF;
    diphdr->btrk[0] = (outhead >> 8) & 0xFF;
    diphdr->btrk[1] = outhead & 0xFF;
    diphdr->ecyl[0] = (endcyl >> 8) & 0xFF;
    diphdr->ecyl[1] = endcyl & 0xFF;
    diphdr->etrk[0] = (endhead >> 8) & 0xFF;
    diphdr->etrk[1] = endhead & 0xFF;
    diphdr->restart[2] = (outcyl >> 8) & 0xFF;
    diphdr->restart[3] = outcyl & 0xFF;
    diphdr->restart[4] = (outhead >> 8) & 0xFF;
    diphdr->restart[5] = outhead & 0xFF;
    diphdr->restart[6] = 1;
    diphdr->trkbal[0] = (remlen >> 8) & 0xFF;
    diphdr->trkbal[1] = remlen & 0xFF;
    diphdr->trklen[0] = (physlen >> 8) & 0xFF;
    diphdr->trklen[1] = physlen & 0xFF;
    diphdr->reused[2] = (outcyl >> 8) & 0xFF;
    diphdr->reused[3] = outcyl & 0xFF;
    diphdr->reused[4] = (outhead >> 8) & 0xFF;
    diphdr->reused[5] = outhead & 0xFF;
    diphdr->reused[6] = 1;
    diphdr->lasthead[0] = (lasthead >> 8) & 0xFF;
    diphdr->lasthead[1] = lasthead & 0xFF;
    diphdr->trklen90[0] = (trklen90 >> 8) & 0xFF;
    diphdr->trklen90[1] = trklen90 & 0xFF;
    diphdr->devcode = (ucbtype_code(devtype) & 0x0F) | 0xF0;
    diphdr->cchh90[0] = (cyl90 >> 8) & 0xFF;
    diphdr->cchh90[1] = cyl90 & 0xFF;
    diphdr->cchh90[2] = (head90 >> 8) & 0xFF;
    diphdr->cchh90[3] = head90 & 0xFF;
    diphdr->endid = 0xFF;

    /* Write the data block to the output file */
    rc = write_block (ofd, ofname, &datablk, keylen, datalen,
                devtype, heads, trklen, extsize,
                trkbuf, &outusedv, &outusedr, &outtrkbr,
                &outtrk, &outcyl, &outhead, &outrec);
    if (rc < 0) return -1;

    XMINFF (3, "DIP complete at cyl %d head %d rec %d\n",
            outcyl, outhead, outrec);
    if (infolvl >= 5) data_dump (diphdr, sizeof(DIPHDR));

    /* Return the last record number and track balance */
    *lastrec = outrec;
    *trkbal = outtrkbr;

    /* Write data remaining in track buffer */
    rc = write_track (ofd, ofname, heads, trklen, trkbuf,
                    &outusedv, &outtrk, &outcyl, &outhead);
    if (rc < 0) return -1;

    /* Return number of tracks and starting address of next dataset */
    *numtrks = outtrk;
    *nxtcyl = outcyl;
    *nxthead = outhead;
    return 0;

} /* end function dip_initialize */

/*-------------------------------------------------------------------*/
/* Subroutine to read a statement from the control file              */
/* Input:                                                            */
/*      cfp     Control file pointer                                 */
/*      cfname  Control file name                                    */
/*      stmt    Buffer to receive control statement                  */
/*      sbuflen Length of statement buffer                           */
/* Output:                                                           */
/*      pstmtno Statement number                                     */
/*      The return value is 0 if a statement was successfully read,  */
/*      +1 if end of file, or -1 if error                            */
/*-------------------------------------------------------------------*/
static int
read_ctrl_stmt (FILE *cfp, BYTE *cfname, BYTE *stmt, int sbuflen,
                int *pstmtno)
{
int             stmtlen;                /* Length of input statement */
static int      stmtno = 0;             /* Statement number          */

    while (1)
    {
        /* Read next record from control file */
        stmtno++;
        *pstmtno = stmtno;
        if (fgets (stmt, sbuflen, cfp) == NULL)
        {
            /* Return code +1 if end of control file */
            if (feof(cfp)) return +1;

            /* Return code -1 if control file input error */
            XMERRF ("Cannot read %s line %d: %s\n",
                    cfname, stmtno, strerror(errno));
            return -1;
        }

        /* Check for DOS end of file character */
        if (stmt[0] == '\x1A')
            return +1;

        /* Check that end of statement has been read */
        stmtlen = strlen(stmt);
        if (stmtlen == 0 || stmt[stmtlen-1] != '\n')
        {
            XMERRF ("Line too long in %s line %d\n",
                    cfname, stmtno);
            return -1;
        }

        /* Remove trailing carriage return and line feed */
        stmtlen--;
        if (stmtlen > 0 && stmt[stmtlen-1] == '\r') stmtlen--;

        /* Remove trailing spaces and tab characters */
        while (stmtlen > 0 && (stmt[stmtlen-1] == SPACE
                || stmt[stmtlen-1] == '\t')) stmtlen--;
        stmt[stmtlen] = '\0';

        /* Print the input statement */
        XMINFF (0, "------- %s\n", stmt);

        /* Ignore comment statements */
        if (stmtlen == 0 || stmt[0] == '#' || stmt[0] == '*')
            continue;

        break;
    } /* end while */

    return 0;
} /* end function read_ctrl_stmt */

/*-------------------------------------------------------------------*/
/* Subroutine to parse a dataset statement from the control file     */
/* Input:                                                            */
/*      stmt    Control statement                                    */
/* Output:                                                           */
/*      dsname  ASCIIZ dataset name (1-44 bytes + terminator)        */
/*      method  Processing method (see METHOD_xxx defines)           */
/*                                                                   */
/*      The following field is returned only for the XMIT method:    */
/*      ifptr   Pointer to XMIT initialization file name             */
/*                                                                   */
/*      The following fields are returned for non-XMIT methods:      */
/*      units   Allocation units (C=CYL, T=TRK)                      */
/*      sppri   Primary allocation quantity                          */
/*      spsec   Secondary allocation quantity                        */
/*      spdir   Directory allocation quantity                        */
/*      dsorg   1st byte of dataset organization bits                */
/*      recfm   1st byte of record format bits                       */
/*      lrecl   Logical record length                                */
/*      blksz   Block size                                           */
/*      keyln   Key length                                           */
/*      The return value is 0 if successful, or -1 if error.         */
/* Control statement format:                                         */
/*      dsname method [initfile] [space [dcbattrib]]                 */
/*      The method can be:                                           */
/*      XMIT = load PDS from initfile containing an IEBCOPY unload   */
/*             dataset created using the TSO TRANSMIT command        */
/*      EMPTY = create empty dataset (do not specify initfile)       */
/*      DIP = initialize LOGREC dataset with IFCDIP00 header record  */
/*      CVOL = initialize SYSCTLG dataset as an OS CVOL              */
/*      The space allocation can be:                                 */
/*      CYL [pri [sec [dir]]]                                        */
/*      TRK [pri [sec [dir]]]                                        */
/*      If primary quantity is omitted then the dataset will be      */
/*      allocated the minimum number of tracks or cylinders needed   */
/*      to contain the data loaded from the initfile.                */
/*      Default allocation is in tracks.                             */
/*      The dcb attributes can be:                                   */
/*      dsorg recfm lrecl blksize keylen                             */
/*      For the XMIT method the dcb attributes are taken from the    */
/*      initialization file and need not be specified.               */
/*      Examples:                                                    */
/*      SYS1.PARMLIB XMIT /cdrom/os360/reslibs/parmlib.xmi           */
/*      SYS1.NUCLEUS XMIT /cdrom/os360/reslibs/nucleus.xmi CYL       */
/*      SYS1.SYSJOBQE EMPTY CYL 10 0 0 DA F 176 176 0                */
/*      SYS1.DUMP EMPTY CYL 10 2 0 PS FB 4104 4104 0                 */
/*-------------------------------------------------------------------*/
static int
parse_ctrl_stmt (BYTE *stmt, BYTE *dsname, BYTE *method, BYTE **ifptr,
                BYTE *units, int *sppri, int *spsec, int *spdir,
                BYTE *dsorg, BYTE *recfm,
                int *lrecl, int *blksz, int *keyln)
{
BYTE           *pdsnam;                 /* -> dsname in input stmt   */
BYTE           *punits;                 /* -> allocation units       */
BYTE           *psppri;                 /* -> primary space quantity */
BYTE           *pspsec;                 /* -> secondary space qty.   */
BYTE           *pspdir;                 /* -> directory space qty.   */
BYTE           *pdsorg;                 /* -> dataset organization   */
BYTE           *precfm;                 /* -> record format          */
BYTE           *plrecl;                 /* -> logical record length  */
BYTE           *pblksz;                 /* -> block size             */
BYTE           *pkeyln;                 /* -> key length             */
BYTE           *pimeth;                 /* -> initialization method  */
BYTE           *pifile;                 /* -> initialization filename*/
BYTE            c;                      /* Character work area       */

    /* Parse the input statement */
    pdsnam = strtok (stmt, " \t");
    pimeth = strtok (NULL, " \t");

    /* Check that all mandatory fields are present */
    if (pdsnam == NULL || pimeth == NULL)
    {
        XMERR ("DSNAME or initialization method missing\n");
        return -1;
    }

    /* Return the dataset name in EBCDIC and ASCII */
    string_to_upper (pdsnam);
    memset (dsname, 0, 45);
    strncpy (dsname, pdsnam, 44);

    /* Set default dataset attribute values */
    *units = 'T';
    *sppri = 1;
    *spsec = 0;
    *spdir = 0;
    *dsorg = 0x00;
    *recfm = 0x00;
    *lrecl = 0;
    *blksz = 0;
    *keyln = 0;
    *ifptr = NULL;

    /* Test for valid initialization method */
    if (strcasecmp(pimeth, "XMIT") == 0)
        *method = METHOD_XMIT;
    else if (strcasecmp(pimeth, "EMPTY") == 0)
        *method = METHOD_EMPTY;
    else if (strcasecmp(pimeth, "DIP") == 0)
        *method = METHOD_DIP;
    else if (strcasecmp(pimeth, "CVOL") == 0)
        *method = METHOD_CVOL;
    else
    {
        XMERRF ("Invalid initialization method: %s\n", pimeth);
        return -1;
    }

    /* Locate the initialization file name */
    if (*method == METHOD_XMIT)
    {
        pifile = strtok (NULL, " \t");
        if (pifile == NULL)
        {
            XMERR ("Initialization file name missing\n");
            return -1;
        }
        *ifptr = pifile;
    }

    /* Determine the space allocation units */
    punits = strtok (NULL, " \t");
    if (punits == NULL) return 0;

    string_to_upper (punits);
    if (strcmp(punits, "CYL") == 0)
        *units = 'C';
    else if (strcmp(punits, "TRK") == 0)
        *units = 'T';
    else
    {
        XMERRF ("Invalid allocation units: %s\n",
                punits);
        return -1;
    }

    /* Determine the primary space allocation quantity */
    psppri = strtok (NULL, " \t");
    if (psppri == NULL) return 0;

    if (sscanf(psppri, "%u%c", sppri, &c) != 1)
    {
        XMERRF ("Invalid primary space: %s\n",
                psppri);
        return -1;
    }

    /* Determine the secondary space allocation quantity */
    pspsec = strtok (NULL, " \t");
    if (pspsec == NULL) return 0;

    if (sscanf(pspsec, "%u%c", spsec, &c) != 1)
    {
        XMERRF ("Invalid secondary space: %s\n",
                pspsec);
        return -1;
    }

    /* Determine the directory space allocation quantity */
    pspdir = strtok (NULL, " \t");
    if (pspdir == NULL) return 0;

    if (sscanf(pspdir, "%u%c", spdir, &c) != 1)
    {
        XMERRF ("Invalid directory space: %s\n",
                pspsec);
        return -1;
    }

    /* Determine the dataset organization */
    pdsorg = strtok (NULL, " \t");
    if (pdsorg == NULL) return 0;

    string_to_upper (pdsorg);
    if (strcmp(pdsorg, "IS") == 0)
        *dsorg = DSORG_IS;
    else if (strcmp(pdsorg, "PS") == 0)
        *dsorg = DSORG_PS;
    else if (strcmp(pdsorg, "DA") == 0)
        *dsorg = DSORG_DA;
    else if (strcmp(pdsorg, "PO") == 0)
        *dsorg = DSORG_PO;
    else
    {
        XMERRF ("Invalid dataset organization: %s\n",
                pdsorg);
        return -1;
    }

    /* Determine the record format */
    precfm = strtok (NULL, " \t");
    if (precfm == NULL) return 0;

    string_to_upper (precfm);
    if (strcmp(precfm, "F") == 0)
        *recfm = RECFM_FORMAT_F;
    else if (strcmp(precfm, "FB") == 0)
        *recfm = RECFM_FORMAT_F | RECFM_BLOCKED;
    else if (strcmp(precfm, "FBS") == 0)
        *recfm = RECFM_FORMAT_F | RECFM_BLOCKED | RECFM_SPANNED;
    else if (strcmp(precfm, "V") == 0)
        *recfm = RECFM_FORMAT_V;
    else if (strcmp(precfm, "VB") == 0)
        *recfm = RECFM_FORMAT_V | RECFM_BLOCKED;
    else if (strcmp(precfm, "VBS") == 0)
        *recfm = RECFM_FORMAT_V | RECFM_BLOCKED | RECFM_SPANNED;
    else if (strcmp(precfm, "U") == 0)
        *recfm = RECFM_FORMAT_U;
    else
    {
        XMERRF ("Invalid record format: %s\n",
                precfm);
        return -1;
    }

    /* Determine the logical record length */
    plrecl = strtok (NULL, " \t");
    if (plrecl == NULL) return 0;

    if (sscanf(plrecl, "%u%c", lrecl, &c) != 1
        || *lrecl > 32767)
    {
        XMERRF ("Invalid logical record length: %s\n",
                plrecl);
        return -1;
    }

    /* Determine the block size */
    pblksz = strtok (NULL, " \t");
    if (pblksz == NULL) return 0;

    if (sscanf(pblksz, "%u%c", blksz, &c) != 1
        || *blksz > 32767)
    {
        XMERRF ("Invalid block size: %s\n",
                pblksz);
        return -1;
    }

    /* Determine the key length */
    pkeyln = strtok (NULL, " \t");
    if (pkeyln == NULL) return 0;

    if (sscanf(pkeyln, "%u%c", keyln, &c) != 1
        || *keyln > 255)
    {
        XMERRF ("Invalid key length: %s\n",
                pkeyln);
        return -1;
    }

    return 0;
} /* end function parse_ctrl_stmt */

/*-------------------------------------------------------------------*/
/* Subroutine to process the control file                            */
/* Input:                                                            */
/*      cfp     Control file pointer                                 */
/*      cfname  Control file name                                    */
/*      ofname  DASD image file name                                 */
/*      ofd     DASD image file descriptor                           */
/*      volser  Output volume serial number (ASCIIZ)                 */
/*      devtype Output device type                                   */
/*      heads   Output device number of tracks per cylinder          */
/*      trklen  Output device virtual track length                   */
/*      trkbuf  Pointer to output track buffer                       */
/*      outcyl  Output starting cylinder number                      */
/*      outhead Output starting head number                          */
/* Output:                                                           */
/*      Datasets are written to the DASD image file as indicated     */
/*      by the control statements.                                   */
/*-------------------------------------------------------------------*/
static int
process_control_file (FILE *cfp, BYTE *cfname, BYTE *ofname, int ofd,
                BYTE *volser, U16 devtype, int heads,
                int trklen, BYTE *trkbuf, int outcyl, int outhead)
{
int             rc;                     /* Return code               */
int             n;                      /* Integer work area         */
BYTE            dsname[45];             /* Dataset name (ASCIIZ)     */
BYTE            method;                 /* Initialization method     */
BYTE           *ifname;                 /* ->Initialization file name*/
BYTE            units;                  /* C=CYL, T=TRK              */
int             sppri;                  /* Primary space quantity    */
int             spsec;                  /* Secondary space quantity  */
int             spdir;                  /* Directory space quantity  */
BYTE            dsorg;                  /* Dataset organization      */
BYTE            recfm;                  /* Record format             */
int             lrecl;                  /* Logical record length     */
int             blksz;                  /* Block size                */
int             keyln;                  /* Key length                */
BYTE            stmt[256];              /* Control file statement    */
int             stmtno;                 /* Statement number          */
int             mintrks;                /* Minimum size of dataset   */
int             maxtrks;                /* Maximum size of dataset   */
int             outusedv;               /* Bytes used in track buffer*/
int             outusedr;               /* Bytes used on real track  */
int             tracks = 0;             /* Tracks used in dataset    */
int             numdscb = 0;            /* Number of DSCBs           */
DATABLK        *firstdscb = NULL;       /* -> First DSCB in chain    */
DATABLK       **dscbpp = &firstdscb;    /* -> DSCB chain pointer     */
DATABLK        *datablk;                /* -> Data block structure   */
int             dirblu;                 /* Bytes used in last dirblk */
int             lasttrk;                /* Relative track number of
                                           last used track of dataset*/
int             lastrec;                /* Record number of last used
                                           block of dataset          */
int             trkbal;                 /* Bytes unused on last track*/
int             bcyl;                   /* Dataset begin cylinder    */
int             bhead;                  /* Dataset begin head        */
int             ecyl;                   /* Dataset end cylinder      */
int             ehead;                  /* Dataset end head          */
BYTE            nullblk[12];            /* Data block for EOF record */
BYTE            volvtoc[5];             /* VTOC begin CCHHR          */
off_t           seekpos;                /* Seek position for lseek   */

    /* Initialize the DSCB chain with format 4 and format 5 DSCBs */
    rc = build_format4_dscb (&dscbpp, devtype);
    if (rc < 0) return -1;
    numdscb++;

    rc = build_format5_dscb (&dscbpp);
    if (rc < 0) return -1;
    numdscb++;

    /* Read dataset statements from control file */
    while (1)
    {
        /* Read next statement from control file */
        rc = read_ctrl_stmt (cfp, cfname, stmt, sizeof(stmt), &stmtno);
        if (rc < 0) return -1;

        /* Exit if end of file */
        if (rc > 0)
            break;

        /* Parse dataset statement from control file */
        rc = parse_ctrl_stmt (stmt, dsname, &method, &ifname,
                &units, &sppri, &spsec, &spdir,
                &dsorg, &recfm, &lrecl, &blksz, &keyln);

        /* Exit if error in control file */
        if (rc < 0)
        {
            XMERRF ("Invalid statement in %s line %d\n",
                    cfname, stmtno);
            return -1;
        }

        /* Write empty tracks if allocation is in cylinders */
        while (units == 'C' && outhead != 0)
        {
            /* Initialize track buffer with empty track */
            init_track (trklen, trkbuf, outcyl, outhead, &outusedv);

            /* Write track to output file */
            rc = write_track (ofd, ofname, heads, trklen, trkbuf,
                            &outusedv, &tracks, &outcyl, &outhead);
            if (rc < 0) break;

        } /* end while */

        XMINFF (1, "Creating dataset %s at cyl %d head %d\n",
                dsname, outcyl, outhead);
        bcyl = outcyl;
        bhead = outhead;

        /* Calculate minimum size of dataset in tracks */
        mintrks = (units == 'C' ? sppri * heads : sppri);

        /* Create dataset according to method specified */
        switch (method) {

        case METHOD_XMIT:
            /* Create dataset using XMIT file as input */
            maxtrks = 32767;
            rc = process_xmit_file (ifname, ofname, ofd, trkbuf,
                                    devtype, heads, trklen,
                                    outcyl, outhead, maxtrks,
                                    &dsorg, &recfm,
                                    &lrecl, &blksz, &keyln,
                                    &dirblu, &lastrec, &trkbal,
                                    &tracks, &outcyl, &outhead);
            if (rc < 0) return -1;
            break;

        case METHOD_DIP:
            /* Initialize LOGREC dataset */
            rc = dip_initialize (ofname, ofd, trkbuf,
                                    devtype, heads, trklen,
                                    outcyl, outhead, mintrks,
                                    &lastrec, &trkbal,
                                    &tracks, &outcyl, &outhead);
            if (rc < 0) return -1;
            break;

        case METHOD_CVOL:
            /* Initialize SYSCTLG dataset */
            rc = cvol_initialize (ofname, ofd, volser, trkbuf,
                                    devtype, heads, trklen,
                                    outcyl, outhead, mintrks,
                                    &lastrec, &trkbal,
                                    &tracks, &outcyl, &outhead);
            if (rc < 0) return -1;
            break;

        default:
        case METHOD_EMPTY:
            /* Create empty dataset */
            tracks = 0;
            dirblu = 0;
            lastrec = 0;

            /* Create the end of file record */
            datablk = (DATABLK*)nullblk;
            rc = write_block (ofd, ofname, datablk, 0, 0, devtype,
                            heads, trklen, mintrks, trkbuf, &outusedv,
                            &outusedr, &trkbal, &tracks, &outcyl,
                            &outhead, &lastrec);
            if (rc < 0) return -1;

            /* Write track to output file */
            rc = write_track (ofd, ofname, heads, trklen, trkbuf,
                            &outusedv, &tracks, &outcyl, &outhead);
            if (rc < 0) return -1;

            break;

        } /* end switch(method) */

        /* Calculate the relative track number of last used track */
        lasttrk = tracks - 1;

        /* Round up space allocation if allocated in cylinders */
        if (units == 'C')
        {
            n = (tracks + heads - 1) / heads * heads;
            if (mintrks < n) mintrks = n;
        }

        /* Fill unused space in dataset with empty tracks */
        while (tracks < mintrks)
        {
            /* Initialize track buffer with empty track */
            init_track (trklen, trkbuf, outcyl, outhead, &outusedv);

            /* Write track to output file */
            rc = write_track (ofd, ofname, heads, trklen, trkbuf,
                            &outusedv, &tracks, &outcyl, &outhead);
            if (rc < 0) return -1;

        } /* end while(tracks) */

        /* Print number of tracks written to dataset */
        XMINFF (2, "Dataset %s contains %d track%s\n",
                dsname, tracks, (tracks == 1 ? "" : "s"));

        /* Calculate end of extent cylinder and head */
        ecyl = (outhead > 0 ? outcyl : outcyl - 1);
        ehead = (outhead > 0 ? outhead - 1 : heads - 1);

        /* Create format 1 DSCB for the dataset */
        rc = build_format1_dscb (&dscbpp, dsname, volser,
                                dsorg, recfm, lrecl, blksz,
                                keyln, dirblu, lasttrk, lastrec,
                                trkbal, units, spsec,
                                bcyl, bhead, ecyl, ehead);
        if (rc < 0) return -1;
        numdscb++;

    } /* end while */

    /* Build the VTOC start CCHHR */
    volvtoc[0] = (outcyl >> 8) & 0xFF;
    volvtoc[1] = outcyl & 0xFF;
    volvtoc[2] = (outhead >> 8) & 0xFF;
    volvtoc[3] = outhead & 0xFF;
    volvtoc[4] = 1;

    /* Write the VTOC */
    rc = write_vtoc (numdscb, firstdscb, ofd, ofname, devtype,
                    heads, trklen, trkbuf, outcyl, outhead,
                    &outcyl, &outhead);
    if (rc < 0) return -1;

    /* Write empty tracks up to end of cylinder */
    while (outhead != 0)
    {
        /* Initialize track buffer with empty track */
        init_track (trklen, trkbuf, outcyl, outhead, &outusedv);

        /* Write track to output file */
        rc = write_track (ofd, ofname, heads, trklen, trkbuf,
                        &outusedv, &tracks, &outcyl, &outhead);
        if (rc < 0) return -1;

    } /* end while */

    XMINFF (0, "Total of %d cylinders written to %s\n",
            outcyl, ofname);

    /* Update the VTOC pointer in the volume label */
    seekpos = CKDDASD_DEVHDR_SIZE
            + CKDDASD_TRKHDR_SIZE + CKDDASD_RECHDR_SIZE + 8
            + CKDDASD_RECHDR_SIZE + IPL1_KEYLEN + IPL1_DATALEN
            + CKDDASD_RECHDR_SIZE + IPL2_KEYLEN + IPL2_DATALEN
            + CKDDASD_RECHDR_SIZE + VOL1_KEYLEN + 11;

    XMINFF (5, "Updating VTOC pointer %2.2X%2.2X%2.2X%2.2X%2.2X "
            "at offset %8.8lX\n",
            volvtoc[0], volvtoc[1], volvtoc[2], volvtoc[3],
            volvtoc[4], seekpos);

    rc = lseek (ofd, seekpos, SEEK_SET);
    if (rc < 0)
    {
        XMERRF ("Cannot seek to VOL1 record: %s\n",
                strerror(errno));
        return -1;
    }

    rc = write (ofd, volvtoc, sizeof(volvtoc));
    if (rc < sizeof(volvtoc))
    {
        XMERRF ("Cannot update VOL1 record: %s\n",
                strerror(errno));
        return -1;
    }

    /* Release the DSCB buffers */
    while (firstdscb != NULL)
    {
        datablk = (DATABLK*)(firstdscb->header);
        free (firstdscb);
        firstdscb = datablk;
    }

    return 0;

} /* end function process_control_file */

/*-------------------------------------------------------------------*/
/* DASDLOAD main entry point                                         */
/*-------------------------------------------------------------------*/
int main (int argc, char *argv[])
{
int             rc = 0;                 /* Return code               */
BYTE           *cfname;                 /* -> Control file name      */
BYTE           *ofname;                 /* -> Output file name       */
FILE           *cfp;                    /* Control file pointer      */
int             ofd;                    /* Output file descriptor    */
BYTE           *volser;                 /* -> Volume serial (ASCIIZ) */
BYTE           *iplfnm;                 /* -> IPL text file or NULL  */
BYTE            c;                      /* Character work area       */
BYTE           *trkbuf;                 /* -> Output track buffer    */
CKDDASD_DEVHDR  devhdr;                 /* Device header             */
U16             devtype;                /* Output device type        */
int             outheads;               /* Output device trks/cyl    */
int             outmaxdl;               /* Output device maximum size
                                           record data length value  */
int             outtrklv;               /* Output device track length
                                           of virtual device         */
int             reltrk;                 /* Output track number       */
int             outcyl;                 /* Output cylinder number    */
int             outhead;                /* Output head number        */
BYTE            stmt[256];              /* Control file statement    */
int             stmtno;                 /* Statement number          */

    /* Display the program identification message */
    fprintf (stderr,
            "Hercules DASD loader program %s "
            "(c)Copyright Roger Bowler, 1999\n",
            MSTRING(VERSION));

    /* Check the number of arguments */
    if (argc < 4)
        argexit(4);

    /* The first argument is the control file name */
    cfname = argv[1];
    if (argv[1] == NULL || strlen(argv[1]) == 0)
        argexit(1);

    /* The second argument is the DASD image file name */
    ofname = argv[2];
    if (argv[2] == NULL || strlen(argv[2]) == 0)
        argexit(2);

    /* The third argument is the device type */
    if (argv[3] == NULL || strlen(argv[3]) != 4
        || sscanf(argv[3], "%hx%c", &devtype, &c) != 1)
        argexit(3);

    /* The optional fourth argument is the message level */
    if (argc > 4 && argv[4] != NULL)
    {
        if (sscanf(argv[4], "%u%c", &infolvl, &c) != 1)
            argexit(4);
    }

    /* Obtain number of heads and maximum data length for device */
    rc = capacity_calc (devtype, 0, 0, 0, NULL, NULL, NULL,
                        NULL, NULL, NULL, NULL, NULL,
                        &outmaxdl, NULL, &outheads, NULL);
    if (rc < 0)
    {
        XMERRF ("Unknown device type: %4.4X\n", devtype);
        argexit(3);
    }

    /* Calculate the track size of the virtual device */
    outtrklv = sizeof(CKDDASD_TRKHDR)
                + sizeof(CKDDASD_RECHDR) + R0_DATALEN
                + sizeof(CKDDASD_RECHDR) + outmaxdl
                + sizeof(eighthexFF);
    outtrklv += 0x1FF;
    outtrklv &= 0xFFFFFE00;

    /* Open the control file */
    cfp = fopen (cfname, "r");
    if (cfp == NULL)
    {
        XMERRF ("Cannot open %s: %s\n",
                cfname, strerror(errno));
        return -1;
    }

    /* Read first statement from control file */
    rc = read_ctrl_stmt (cfp, cfname, stmt, sizeof(stmt), &stmtno);
    if (rc < 0) return -1;

    /* Error if end of file */
    if (rc > 0)
    {
        XMERRF ("Volume serial statement missing from %s\n",
                cfname);
        return -1;
    }

    /* Parse the volume serial statement */
    volser = strtok (stmt, " \t");
    iplfnm = strtok (NULL, " \t");

    if (volser == NULL || strlen(volser) == 0 || strlen(volser) > 6)
    {
        XMERRF ("Invalid volume serial in %s line %d\n",
                cfname, stmtno);
        return -1;
    }
    string_to_upper (volser);

    /* Create the output file */
    ofd = open (ofname, O_RDWR | O_CREAT | O_TRUNC,
                S_IRUSR | S_IWUSR | S_IRGRP);
    if (ofd < 0)
    {
        XMERRF ("Cannot open %s: %s\n",
                ofname, strerror(errno));
        return -1;
    }

    /* Obtain the output track buffer */
    trkbuf = malloc (outtrklv);
    if (trkbuf == NULL)
    {
        XMERRF ("Cannot obtain track buffer: %s\n",
                strerror(errno));
        return -1;
    }

    /* Display progress message */
    XMINFF (0, "Creating %4.4X volume %s: "
            "%u trks/cyl, %u bytes/track\n",
            devtype, volser, outheads, outtrklv);

    /* Create the CKD device header */
    memset(&devhdr, 0, CKDDASD_DEVHDR_SIZE);
    memcpy(devhdr.devid, "CKD_P370", 8);
    devhdr.heads = outheads;
    devhdr.trksize = outtrklv;
    devhdr.devtype = devtype & 0xFF;
    devhdr.fileseq = 0;
    devhdr.highcyl = 0;

    /* Write the CKD device header to the DASD image file */
    rc = write (ofd, &devhdr, CKDDASD_DEVHDR_SIZE);
    if (rc < CKDDASD_DEVHDR_SIZE)
    {
        XMERRF ("%s device header write error: %s\n",
                ofname, strerror(errno));
        return -1;
    }

    /* Write track zero to the DASD image file */
    rc = write_track_zero (ofd, ofname, volser, devtype,
                        outheads, outtrklv, trkbuf, iplfnm,
                        &reltrk, &outcyl, &outhead);
    if (rc < 0)
        return -1;

    /* Process the control file to create the datasets */
    rc = process_control_file (cfp, cfname, ofname, ofd,
                        volser, devtype, outheads, outtrklv, trkbuf,
                        outcyl, outhead);

    /* Close files and release buffers */
    fclose (cfp);
    close (ofd);
    free (trkbuf);

    return rc;

} /* end function main */
