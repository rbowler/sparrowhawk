/* XMITPROC.C   (c) Copyright Roger Bowler, 1999                     */
/*              Hercules XMIT file unpacker                          */

/*-------------------------------------------------------------------*/
/* This program unpacks an XMIT file onto a DASD image.              */
/*-------------------------------------------------------------------*/

#include "hercules.h"

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
        fprintf(stderr, "Error: " format)
#define XMERRF(format,a...) \
        fprintf(stderr, "Error: " format, ## a)

/*-------------------------------------------------------------------*/
/* Text unit keys for transmit/receive                               */
/*-------------------------------------------------------------------*/
#define INMDDNAM        0x0001          /* DDNAME for the file       */
#define INMDSNAM        0x0002          /* Name of the file          */
#define INMMEMBR        0x0003          /* Member name list          */
#define INMSECND        0x000B          /* Secondary space quantity  */
#define INMDIR          0x000C          /* Directory space quantity  */
#define INMEXPDT        0x0022          /* Expiration date           */
#define INMTERM         0x0028          /* Data transmitted as msg   */
#define INMBLKSZ        0x0030          /* Block size                */
#define INMDSORG        0x003C          /* File organization         */
#define INMLRECL        0x0042          /* Logical record length     */
#define INMRECFM        0x0049          /* Record format             */
#define INMTNODE        0x1001          /* Target node name/number   */
#define INMTUID         0x1002          /* Target user ID            */
#define INMFNODE        0x1011          /* Origin node name/number   */
#define INMFUID         0x1012          /* Origin user ID            */
#define INMLREF         0x1020          /* Date last referenced      */
#define INMLCHG         0x1021          /* Date last changed         */
#define INMCREAT        0x1022          /* Creation date             */
#define INMFVERS        0x1023          /* Origin vers# of data fmt  */
#define INMFTIME        0x1024          /* Origin timestamp          */
#define INMTTIME        0x1025          /* Destination timestamp     */
#define INMFACK         0x1026          /* Originator request notify */
#define INMERRCD        0x1027          /* RECEIVE command error code*/
#define INMUTILN        0x1028          /* Name of utility program   */
#define INMUSERP        0x1029          /* User parameter string     */
#define INMRECCT        0x102A          /* Transmitted record count  */
#define INMSIZE         0x102C          /* File size in bytes        */
#define INMFFM          0x102D          /* Filemode number           */
#define INMNUMF         0x102F          /* #of files transmitted     */
#define INMTYPE         0x8012          /* Dataset type              */

/*-------------------------------------------------------------------*/
/* Definitions of IEBCOPY header records                             */
/*-------------------------------------------------------------------*/
typedef struct _COPYR1 {                /* IEBCOPY header record 1   */
        BYTE    uldfmt;                 /* Unload format             */
        BYTE    hdrid[3];               /* Header identifier         */
        HWORD   ds1dsorg;               /* Dataset organization      */
        HWORD   ds1blkl;                /* Block size                */
        HWORD   ds1lrecl;               /* Logical record length     */
        BYTE    ds1recfm;               /* Record format             */
        BYTE    ds1keyl;                /* Key length                */
        BYTE    ds1optcd;               /* Option codes              */
        BYTE    ds1smsfg;               /* SMS indicators            */
        HWORD   uldblksz;               /* Block size of container   */
                                        /* Start of DEVTYPE fields   */
        FWORD   ucbtype;                /* Original device type      */
        FWORD   maxblksz;               /* Maximum block size        */
        HWORD   cyls;                   /* Number of cylinders       */
        HWORD   heads;                  /* Number of tracks/cylinder */
        HWORD   tracklen;               /* Track length              */
        HWORD   overhead;               /* Block overhead            */
        BYTE    keyovhead;              /* Keyed block overhead      */
        BYTE    devflags;               /* Flags                     */
        HWORD   tolerance;              /* Tolerance factor          */
                                        /* End of DEVTYPE fields     */
        HWORD   hdrcount;               /* Number of header records
                                           (if zero, then 2 headers) */
        BYTE    resv1;                  /* Reserved                  */
        BYTE    ds1refd[3];             /* Last reference date       */
        BYTE    ds1scext[3];            /* Secondary space extension */
        BYTE    ds1scalo[4];            /* Secondary allocation      */
        BYTE    ds1lstar[3];            /* Last track used TTR       */
        HWORD   ds1trbal;               /* Last track balance        */
        HWORD   resv2;                  /* Reserved                  */
    } COPYR1;

/* Bit settings for unload format byte */
#define COPYR1_ULD_FORMAT       0xC0    /* Bits 0-1=unload format... */
#define COPYR1_ULD_FORMAT_OLD   0x00    /* ...old format             */
#define COPYR1_ULD_FORMAT_PDSE  0x40    /* ...PDSE format            */
#define COPYR1_ULD_FORMAT_ERROR 0x80    /* ...error during unload    */
#define COPYR1_ULD_FORMAT_XFER  0xC0    /* ...transfer format        */
#define COPYR1_ULD_PROGRAM      0x10    /* Bit 3=Contains programs   */
#define COPYR1_ULD_PDSE         0x01    /* Bit 7=Contains PDSE       */

/* Bit settings for header identifier */
#define COPYR1_HDRID    "\xCA\x6D\x0F"  /* Constant value for hdrid  */

/* Bit settings for dataset organization */
#define DSORG_IS                0x80    /* Indexed sequential        */
#define DSORG_PS                0x40    /* Physically sequential     */
#define DSORG_DA                0x20    /* Direct access             */
#define DSORG_PO                0x02    /* Partitioned organization  */
#define DSORG_U                 0x01    /* Unmovable                 */

/* Bit settings for record format */
#define RECFM_FORMAT            0xC0    /* Bits 0-1=Record format    */
#define RECFM_FORMAT_V          0x40    /* ...variable length        */
#define RECFM_FORMAT_F          0x80    /* ...fixed length           */
#define RECFM_FORMAT_U          0xC0    /* ...undefined length       */
#define RECFM_TRKOFLOW          0x20    /* Bit 2=Track overflow      */
#define RECFM_BLOCKED           0x10    /* Bit 3=Blocked             */
#define RECFM_SPANNED           0x08    /* Bit 4=Spanned or standard */
#define RECFM_CTLCHAR           0x06    /* Bits 5-6=Carriage control */
#define RECFM_CTLCHAR_A         0x02    /* ...ANSI carriage control  */
#define RECFM_CTLCHAR_M         0x04    /* ...Machine carriage ctl.  */

typedef struct _COPYR2 {                /* IEBCOPY header record 2   */
        BYTE    debbasic[16];           /* Last 16 bytes of basic
                                           section of original DEB   */
        BYTE    debxtent[16][16];       /* First 16 extent descriptors
                                           from original DEB         */
        FWORD   resv;                   /* Reserved                  */
    } COPYR2;

/*-------------------------------------------------------------------*/
/* Definition of extent descriptor                                   */
/*-------------------------------------------------------------------*/
typedef struct _EXTDESC {
        U16     bcyl;                   /* Begin cylinder            */
        U16     btrk;                   /* Begin track               */
        U16     ecyl;                   /* End cylinder              */
        U16     etrk;                   /* End track                 */
        U16     ntrk;                   /* Number of tracks          */
    } EXTDESC;

/*-------------------------------------------------------------------*/
/* Definition of data record block                                   */
/*-------------------------------------------------------------------*/
typedef struct _DATABLK {
        U32     header;                 /* Data block header         */
        HWORD   cyl;                    /* Cylinder number           */
        HWORD   head;                   /* Head number               */
        BYTE    rec;                    /* Record number             */
        BYTE    klen;                   /* Key length                */
        HWORD   dlen;                   /* Data length               */
        BYTE    kdarea[32760];          /* Key and data area         */
    } DATABLK;

/*-------------------------------------------------------------------*/
/* Static data areas                                                 */
/*-------------------------------------------------------------------*/
BYTE eighthexFF[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
BYTE twelvehex00[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* Information message level: 0=None, 1=File name, 2=File information,
   3=Member information, 4=Text units, record headers, 5=Dump data */
int  infolvl = 4;

/*-------------------------------------------------------------------*/
/* ASCII to EBCDIC translate tables                                  */
/*-------------------------------------------------------------------*/
static unsigned char
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

static unsigned char
ebcdic_to_ascii[] = {
"\x00\x01\x02\x03\xA6\x09\xA7\x7F\xA9\xB0\xB1\x0B\x0C\x0D\x0E\x0F"
"\x10\x11\x12\x13\xB2\xB4\x08\xB7\x18\x19\x1A\xB8\xBA\x1D\xBB\x1F"
"\xBD\xC0\x1C\xC1\xC2\x0A\x17\x1B\xC3\xC4\xC5\xC6\xC7\x05\x06\x07"
"\xC8\xC9\x16\xCB\xCC\x1E\xCD\x04\xCE\xD0\xD1\xD2\x14\x15\xD3\xFC"
"\x20\xD4\x83\x84\x85\xA0\xD5\x86\x87\xA4\xD6\x2E\x3C\x28\x2B\xD7"
"\x26\x82\x88\x89\x8A\xA1\x8C\x8B\x8D\xD8\x21\x24\x2A\x29\x3B\x5E"
"\x2D\x2F\xD9\x8E\xDB\xDC\xDD\x8F\x80\xA5\x7C\x2C\x25\x5F\x3E\x3F"
"\xDE\x90\xDF\xE0\xE2\xE3\xE4\xE5\xE6\x60\x3A\x23\x40\x27\x3D\x22"
"\xE7\x61\x62\x63\x64\x65\x66\x67\x68\x69\xAE\xAF\xE8\xE9\xEA\xEC"
"\xF0\x6A\x6B\x6C\x6D\x6E\x6F\x70\x71\x72\xF1\xF2\x91\xF3\x92\xF4"
"\xF5\x7E\x73\x74\x75\x76\x77\x78\x79\x7A\xAD\xA8\xF6\x5B\xF7\xF8"
"\x9B\x9C\x9D\x9E\x9F\xB5\xB6\xAC\xAB\xB9\xAA\xB3\xBC\x5D\xBE\xBF"
"\x7B\x41\x42\x43\x44\x45\x46\x47\x48\x49\xCA\x93\x94\x95\xA2\xCF"
"\x7D\x4A\x4B\x4C\x4D\x4E\x4F\x50\x51\x52\xDA\x96\x81\x97\xA3\x98"
"\x5C\xE1\x53\x54\x55\x56\x57\x58\x59\x5A\xFD\xEB\x99\xED\xEE\xEF"
"\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\xFE\xFB\x9A\xF9\xFA\xFF"
        };

/*-------------------------------------------------------------------*/
/* Subroutine to convert an EBCDIC string to an ASCIIZ string.       */
/* Removes trailing blanks and adds a terminating null.              */
/* Returns the length of the ASCII string excluding terminating null */
/*-------------------------------------------------------------------*/
static int
make_asciiz (BYTE *dest, int destlen, BYTE *src, int srclen)
{
int             len;                    /* Result length             */

    for (len=0; len < srclen && len < destlen-1; len++)
    {
        dest[len] = ebcdic_to_ascii[src[len]];
        if (dest[len] == SPACE) break;
    }
    dest[len] = '\0';

    return len;

} /* end function make_asciiz */

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
/* Subroutine to print a data block in hex and character format.     */
/*-------------------------------------------------------------------*/
static void
packet_trace ( void *addr, int len )
{
unsigned int    maxlen = 512;
unsigned int    i, xi, offset, startoff;
BYTE            c;
BYTE           *pchar;
BYTE            print_chars[17];
BYTE            hex_chars[64];
BYTE            prev_hex[64] = "";
int             firstsame = 0;
int             lastsame = 0;

    pchar = (unsigned char*)addr;

    for (offset=0; ; )
    {
        if (offset >= maxlen && offset <= len - maxlen)
        {
            offset += 16;
            pchar += 16;
            prev_hex[0] = '\0';
            continue;
        }
        if ( offset > 0 )
        {
            if ( strcmp ( hex_chars, prev_hex ) == 0 )
            {
                if ( firstsame == 0 ) firstsame = startoff;
                lastsame = startoff;
            }
            else
            {
                if ( firstsame != 0 )
                {
                    if ( lastsame == firstsame )
                        printf ("Line %4.4X same as above\n",
                                firstsame );
                    else
                        printf ("Lines %4.4X to %4.4X same as above\n",
                                firstsame, lastsame );
                    firstsame = lastsame = 0;
                }
                printf ("+%4.4X %s %s\n",
                        startoff, hex_chars, print_chars);
                strcpy ( prev_hex, hex_chars );
            }
        }

        if ( offset >= len ) break;

        memset ( print_chars, 0, sizeof(print_chars) );
        memset ( hex_chars, SPACE, sizeof(hex_chars) );
        startoff = offset;
        for (xi=0, i=0; i < 16; i++)
        {
            c = *pchar++;
            if (offset < len) {
                sprintf(hex_chars+xi, "%2.2X", c);
                print_chars[i] = '.';
                if (isprint(c)) print_chars[i] = c;
                c = ebcdic_to_ascii[c];
                if (isprint(c)) print_chars[i] = c;
            }
            offset++;
            xi += 2;
            hex_chars[xi] = SPACE;
            if ((offset & 3) == 0) xi++;
        } /* end for(i) */
        hex_chars[xi] = '\0';

    } /* end for(offset) */

} /* end function packet_trace */

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
/* Subroutine to return the name of a DASD device                    */
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
    case 0x0F: return "3390";
    } /* end switch(key) */

    return "????";

} /* end function dasd_name */

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
/*      buf     Pointer to start of buffer                           */
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
next_tu (BYTE *buf, int bufpos, int bufrem, U16 *pkey, U16 *pnum,
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
    key = (buf[bufpos] << 8) | buf[bufpos+1];
    num = (buf[bufpos+2] << 8) | buf[bufpos+3];

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
        len = (buf[bufpos+offset] << 8) | buf[bufpos+offset+1];
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
            sprintf(hex+2*j, "%2.2X", buf[bufpos+offset+i]);
            c = ebcdic_to_ascii[buf[bufpos+offset+i]];
            if (!isprint(c)) c = '.';
            chars[j] = c;
        } /* end for(i) */
        XMINFF (4, "%-16.16s %-8.8s", hex, chars);

        /* Save field length and pointer in array */
        plen[field] = len;
        pdata[field] = buf + bufpos + offset;

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
/*      fd      Input file descriptor                                */
/*      fname   Input file name                                      */
/*      buf     Pointer to buffer to receive logical record          */
/* Output:                                                           */
/*      ctl     Zero=data record, non-zero=control record            */
/*      The return value is the logical record length,               */
/*      or -1 if an error occurred.                                  */
/*-------------------------------------------------------------------*/
static int
read_rec (int fd, BYTE *fname, BYTE *buf, BYTE *ctl)
{
int             rc;                     /* Return code               */
int             reclen = 0;             /* Cumulative record length  */
int             segnum;                 /* Segment counter           */
int             seglen;                 /* Segment data length       */
BYTE            ctlind = 0x00;          /* 0x20=Control record       */
BYTE            seghdr[2];              /* Segment length and flags  */

    for (segnum = 0; ; segnum++)
    {
        /* Read the segment length and flags */
        rc = read (fd, seghdr, 2);
        if (rc < 2)
        {
            XMERRF ("%s read error: %s\n",
                    fname, strerror(errno));
            return -1;
        }

        /* Check for valid segment header */
        if (seghdr[0] < 2 || (seghdr[1] & 0x1F) != 0)
        {
            XMERRF ("%s invalid segment header: %2.2X%2.2X\n",
                    fname, seghdr[0], seghdr[1]);
            return -1;
        }

        /* Check flags for first segment */
        if (segnum == 0)
        {
            /* Check that first segment indicator is set */
            if ((seghdr[1] & 0x80) == 0)
            {
                XMERRF ("%s first segment indicator expected\n",
                        fname);
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
                        fname);
                return -1;
            }

            /* Check if ctlrec indicator matches first segment */
            if ((seghdr[1] & 0x20) != ctlind)
            {
                XMERRF ("%s control record indicator mismatch\n",
                        fname);
                return -1;
            }
        }

        /* Read segment data into buffer */
        seglen = seghdr[0] - 2;
        rc = read (fd, buf + reclen, seglen);
        if (rc < seglen)
        {
            XMERRF ("%s read error: %s\n",
                    fname, strerror(errno));
            return -1;
        }

        /* Accumulate total record length */
        reclen += seglen;

        /* Exit if last segment of record */
        if (seghdr[1] & 0x40)
            break;

    } /* end for(segnum) */

    /* Return record length and control indicator */
    *ctl = ctlind;
    return reclen;

} /* end function read_rec */

/*-------------------------------------------------------------------*/
/* Subroutine to process an INMR02 control record                    */
/* Input:                                                            */
/*      buf     Pointer to buffer containing control record          */
/*      reclen  Length of control record                             */
/*      dsorg   Pointer to 2-byte field to receive dataset org       */
/*      recfm   Pointer to 2-byte field to receive record format     */
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
process_inmr02 (BYTE *buf, int reclen, BYTE *dsorg, BYTE *recfm,
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
    filenum = (buf[6] << 24) | (buf[7] << 16)
            | (buf[8] << 8) | buf[9];

    /* Point to the first text unit */
    bufpos = 10;
    bufrem = reclen-10;

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
        rc = next_tu (buf, bufpos, bufrem, &tukey, &tunum,
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
        XMINFF (2, "File %lu: DSNAME=%s\n",
                filenum, tudsnam);
        XMINFF (2, "DSORG=%s RECFM=%s "
                "LRECL=%d BLKSIZE=%d KEYLEN=%d DIRBLKS=%d\n",
                dsorg_name(tudsorg), recfm_name(turecfm),
                tulrecl, tublksz, tukeyln, tudirct);
        memcpy (dsorg, tudsorg, 2);
        memcpy (recfm, turecfm, 2);
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
/*      buf     Pointer to buffer containing control record          */
/*      reclen  Length of control record                             */
/* Output:                                                           */
/*      The return value is 0 if successful, or -1 if error.         */
/*-------------------------------------------------------------------*/
static int
process_inmrxx (BYTE *buf, int reclen)
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
    bufrem = reclen-6;

    /* Process each text unit */
    while (bufrem > 0)
    {
        /* Extract the next text unit */
        rc = next_tu (buf, bufpos, bufrem, &tukey, &tunum,
                MAXNUM, fieldlen, fieldptr);
        if (rc < 0)
        {
            printf ("Error in text unit at offset %4.4X\n",
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
/*      buf     Pointer to buffer containing header record           */
/*      reclen  Length of header record                              */
/* Output:                                                           */
/*      The return value is the number of tracks per cylinder,       */
/*      or -1 if an error occurred.                                  */
/*-------------------------------------------------------------------*/
static int
process_copyr1 (BYTE *buf, int reclen)
{
COPYR1 *copyr1 = (COPYR1*)buf;          /* -> COPYR1 header record   */
U16     blksize;                        /* Block size                */
U16     lrecl;                          /* Logical record length     */
BYTE    keylen;                         /* Key length                */
U16     cyls;                           /* Number of cylinders       */
U16     heads;                          /* Number of tracks/cylinder */

    /* Check COPYR1 record for correct length */
    if (reclen != sizeof(COPYR1)
        && reclen != sizeof(COPYR1) - 4)
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
/*      buf     Pointer to buffer containing header record           */
/*      reclen  Length of header record                              */
/*      xarray  Pointer to array to receive 1-16 extent descriptions */
/* Output:                                                           */
/*      The return value is the number of extents,                   */
/*      or -1 if an error occurred.                                  */
/*                                                                   */
/* Extent information is listed if infolvl is 4 or greater.          */
/*-------------------------------------------------------------------*/
static int
process_copyr2 (BYTE *buf, int reclen, EXTDESC xarray[])
{
COPYR2 *copyr2 = (COPYR2*)buf;          /* -> COPYR2 header record   */
int     numext;                         /* Number of extents         */
int     i;                              /* Array subscript           */

    /* Check COPYR2 record for correct length */
    if (reclen != sizeof(COPYR2))
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
/*      buf     Pointer to directory block                           */
/*      blklen  Length of directory block                            */
/*      blkpp   Pointer to chain field of previous block             */
/* Output:                                                           */
/*      The return value is 0 if successful, 1 if end of directory,  */
/*      or -1 if an error occurred.                                  */
/*                                                                   */
/* Each directory block is saved in a chained list.                  */
/* Directory information is listed if infolvl is 3 or greater.       */
/*-------------------------------------------------------------------*/
static int
process_dirblk (DATABLK *buf, int blklen, DATABLK **blkpp)
{
int             rem;                    /* Number of bytes remaining */
int             size;                   /* Size of directory entry   */
int             i, j;                   /* Array subscripts          */
int             k;                      /* Userdata halfword count   */
DATABLK        *blkp;                   /* -> Copy of directory block*/
BYTE           *dirent;                 /* -> Directory entry        */
BYTE            memname[9];             /* Member name (ASCIIZ)      */
BYTE            c, hex[49], chars[25];  /* Character work areas      */

    /* Check for end of directory */
    if (blklen == 12 && memcmp(buf, twelvehex00, 12) == 0)
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
    memcpy (blkp, buf, blklen);
    *blkpp = blkp;
    blkp->header = 0;
    blkpp = (DATABLK**)(&blkp->header);

    /* Point to start of directory block data area */
    dirent = buf->kdarea + 8;

    /* Load number of directory bytes remaining */
    rem = (dirent[0] << 8) | dirent[1];
    if (rem < 2 || rem > 256)
    {
        XMERR ("Directory block byte count is invalid\n");
        return -1;
    }

    /* Process each directory entry */
    rem -= 2;
    dirent += 2;
    while (rem > 0)
    {
        /* Test for end of directory */
        if (memcmp(dirent, eighthexFF, 8) == 0)
            break;

        /* Extract the member name */
        make_asciiz (memname, sizeof(memname), dirent, 8);

        /* Display the directory entry */
        XMINFF (3, "%s %-8.8s TTR=%2.2X%2.2X%2.2X ",
                (dirent[11] & 0x80) ? " Alias" : "Member",
                memname, dirent[8], dirent[9], dirent[10]);

        /* Load the user data halfword count */
        k = dirent[11] & 0x1F;

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
            sprintf(hex+2*j, "%2.2X", dirent[12+i]);
            c = ebcdic_to_ascii[dirent[12+i]];
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
        dirent += size;
        rem -= size;
    }

    return 0;
} /* end function process_dirblk */

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
/* Subroutine to calculate physical device track capacities          */
/* Input:                                                            */
/*      devtype Device type                                          */
/*      used    Number of bytes used so far on track,                */
/*              excluding home address and record 0                  */
/*      keylen  Key length of proposed new record                    */
/*      datalen Data length of proposed new record                   */
/* Output:                                                           */
/*      newused Number of bytes used including proposed new record   */
/*      trkbaln Number of bytes remaining on track                   */
/*      physlen Number of bytes on physical track                    */
/*      maxdlen Maximum data length for non-keyed record 1           */
/*      numrecs Number of records of specified length per track      */
/*      A NULL address may be specified for any of the output        */
/*      fields if the output value is not required.                  */
/*      The return value is 0 if the record will fit on the track,   */
/*      +1 if the record will not fit on the track, or -1 if error.  */
/*-------------------------------------------------------------------*/
static int
capacity_calc (U16 devtype, int used, int keylen, int datalen,
                int *newused, int *trkbaln, int *physlen,
                int *maxdlen, int *numrecs)
{
int             trklen;                 /* Physical track length     */
int             maxlen;                 /* Maximum data length       */
int             bytes;                  /* Bytes used by new block   */
int             nrecs;                  /* Number of record/track    */
int             c, d1, d2, x;           /* 23xx/3330/3350 factors    */
int             b1, b2;                 /* 23xx calculations         */
int             f1, f2, f3, f4, f5, f6; /* 3380/3390 factors         */
int             fl1, fl2, int1, int2;   /* 3380/3390 calculations    */

    switch (devtype)
    {
    case 0x2301:
        trklen = 20483;
        maxlen = 20483;
        c = 53; x = 133;
        goto formula1;

    case 0x2302:
        trklen = 4984;
        maxlen = 4984;
        c = 20; x = 61; d1 = 537; d2 = 512;
        goto formula2;

    case 0x2303:
        trklen = 4892;
        maxlen = 4892;
        c = 38; x = 108;
        goto formula1;

    case 0x2311:
        trklen = 3625;
        maxlen = 3625;
        c = 20; x = 61; d1 = 537; d2 = 512;
        goto formula2;

    case 0x2314:
        trklen = 7294;
        maxlen = 7294;
        c = 45; x = 101; d1 = 2137; d2 = 2048;
        goto formula2;

    case 0x2321:
        trklen = 2000;
        maxlen = 2000;
        c = 16; x = 84; d1 = 537; d2 = 512;
        goto formula2;

    formula1:
        b1 = keylen + datalen + (keylen == 0 ? 0 : c);
        b2 = b1 + x;
        nrecs = (trklen - b1)/b2 + 1;
        bytes = (used == 0 ? b1 : b2);
        break;

    formula2:
        if (used == 0)
        b1 = keylen + datalen + (keylen == 0 ? 0 : c);
        b2 = ((keylen + datalen) * d1 / d2)
                + (keylen == 0 ? 0 : c) + x;
        nrecs = (trklen - b1)/b2 + 1;
        bytes = (used == 0 ? b1 : b2);
        break;

    case 0x3330:
        trklen = 13165;
        maxlen = 13030;
        c = 56;
        bytes = keylen + datalen + (keylen == 0 ? 0 : c) + 135;
        nrecs = trklen / bytes;
        break;

    case 0x3350:
        trklen = 19254;
        maxlen = 19069;
        c = 82;
        bytes = keylen + datalen + (keylen == 0 ? 0 : c) + 185;
        nrecs = trklen / bytes;
        break;

    case 0x3380:
        trklen = 47968;
        maxlen = 47476;
        f1 = 32; f2 = 492; f3 = 236;
        fl1 = datalen + f2;
        fl2 = (keylen == 0 ? 0 : keylen + f3);
        fl1 = ((fl1 + f1 - 1) / f1) * f1;
        fl2 = ((fl2 + f1 - 1) / f1) * f1;
        bytes = fl1 + fl2;
        nrecs = trklen / bytes;
        break;

    case 0x3390:
        trklen = 58786;
        maxlen = 56664;
        f1 = 34; f2 = 19; f3 = 9; f4 = 6; f5 = 116; f6 = 6;
        int1 = ((datalen + f6) + (f5*2-1)) / (f5*2);
        int2 = ((keylen + f6) + (f5*2-1)) / (f5*2);
        fl1 = (f1 * f2) + datalen + f6 + f4*int1;
        fl2 = (keylen == 0 ? 0 : (f1 * f3) + keylen + f6 + f4*int2);
        fl1 = ((fl1 + f1 - 1) / f1) * f1;
        fl2 = ((fl2 + f1 - 1) / f1) * f1;
        bytes = fl1 + fl2;
        nrecs = trklen / bytes;
        break;

    default:
        XMERRF ("Unknown device type: %4.4X\n", devtype);
        return -1;
    } /* end switch(devtype) */

    /* Return if record will not fit on the track */
    if (used + bytes > trklen)
        return +1;

    /* Calculate number of bytes used and track balance */
    if (newused != NULL) *newused = used + bytes;
    if (trkbaln != NULL) *trkbaln = trklen - used - bytes;

    /* Return track length and maximum data length */
    if (physlen != NULL) *physlen = trklen;
    if (maxdlen != NULL) *maxdlen = maxlen;

    /* Return number of records per track */
    if (numrecs != NULL) *numrecs = nrecs;

    return 0;
} /* end function capacity_calc */

/*-------------------------------------------------------------------*/
/* Subroutine to initialize the output track buffer                  */
/* Input:                                                            */
/*      trklen  Track length of virtual output device                */
/*      buf     Pointer to track buffer                              */
/*      cyl     Cylinder number on output device                     */
/*      head    Head number on output device                         */
/* Output:                                                           */
/*      usedv   Number of bytes written to track of virtual device   */
/*-------------------------------------------------------------------*/
static void
init_track (int trklen, BYTE *buf, int cyl, int head, int *usedv)
{
CKDDASD_TRKHDR *trkhdr;                 /* -> Track header           */
CKDDASD_RECHDR *rechdr;                 /* -> Record header          */

    /* Clear the track buffer to zeroes */
    memset (buf, 0, trklen);

    /* Build the home address in the track buffer */
    trkhdr = (CKDDASD_TRKHDR*)buf;
    trkhdr->bin = 0;
    trkhdr->cyl[0] = (cyl >> 8) & 0xFF;
    trkhdr->cyl[1] = cyl & 0xFF;
    trkhdr->head[0] = (head >> 8) & 0xFF;
    trkhdr->head[1] = head & 0xFF;

    /* Build a standard record zero in the track buffer */
    rechdr = (CKDDASD_RECHDR*)(buf + CKDDASD_TRKHDR_SIZE);
    rechdr->cyl[0] = (cyl >> 8) & 0xFF;
    rechdr->cyl[1] = cyl & 0xFF;
    rechdr->head[0] = (head >> 8) & 0xFF;
    rechdr->head[1] = head & 0xFF;
    rechdr->rec = 0;
    rechdr->klen = 0;
    rechdr->dlen[0] = 0;
    rechdr->dlen[1] = 8;

    /* Set number of bytes used in track buffer */
    *usedv = CKDDASD_TRKHDR_SIZE + CKDDASD_RECHDR_SIZE + 8;

    /* Build end of track marker at end of buffer */
    memcpy (buf + *usedv, eighthexFF, 8);

} /* end function init_track */

/*-------------------------------------------------------------------*/
/* Subroutine to write track buffer to output file                   */
/* Input:                                                            */
/*      fd      Output file descriptor                               */
/*      fname   Output file name                                     */
/*      heads   Number of tracks per cylinder on output device       */
/*      trklen  Track length of virtual output device                */
/*      buf     Pointer to track buffer                              */
/* Input/output:                                                     */
/*      usedv   Number of bytes written to track of virtual device   */
/*      reltrk  Relative track number on output device               */
/*      cyl     Cylinder number on output device                     */
/*      head    Head number on output device                         */
/* Output:                                                           */
/*      The return value is 0 if successful, -1 if error occurred.   */
/*-------------------------------------------------------------------*/
static int
write_track (int fd, BYTE *fname, int heads, int trklen, BYTE *buf,
            int *usedv, int *reltrk, int *cyl, int *head)
{
int             rc;                     /* Return code               */

    /* Build end of track marker at end of buffer */
    memcpy (buf + *usedv, eighthexFF, 8);

    /* Write the current track to the file */
    rc = write (fd, buf, trklen);
    if (rc < trklen)
    {
        XMINF (4, "\n");
        XMERRF ("%s cylinder %u head %u write error: %s\n",
                fname, *cyl, *head, strerror(errno));
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
/*      fd      Output file descriptor                               */
/*      fname   Output file name                                     */
/*      blk     Pointer to data block                                */
/*      keylen  Key length                                           */
/*      datalen Data length                                          */
/*      devtype Output device type                                   */
/*      heads   Number of tracks per cylinder on output device       */
/*      trklen  Track length of virtual output device                */
/*      maxtrk  Maximum number of tracks to be written               */
/*      buf     Pointer to track buffer                              */
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
write_block (int fd, BYTE *fname, DATABLK *blk, int keylen,
            int datalen, U16 devtype, int heads, int trklen,
            int maxtrk, BYTE *buf, int *usedv, int *usedr, int *trkbal,
            int *reltrk, int *cyl, int *head, int *rec)
{
int             rc;                     /* Return code               */
int             cc;                     /* Capacity calculation code */
CKDDASD_RECHDR *rechdr;                 /* -> Record header          */

    /* Determine whether record will fit on current track */
    cc = capacity_calc (devtype, *usedr, keylen, datalen,
                        usedr, trkbal, NULL, NULL, NULL);
    if (cc < 0) return -1;

    /* Move to next track if record will not fit */
    if (cc > 0 && *usedr > 0)
    {
        /* Write current track to output file */
        rc = write_track (fd, fname, heads, trklen, buf,
                        usedv, reltrk, cyl, head);
        if (rc < 0) return -1;

        /* Clear bytes used and record number for new track */
        *usedr = 0;
        *rec = 0;

        /* Determine whether record will fit on new track */
        cc = capacity_calc (devtype, *usedr, keylen, datalen,
                            usedr, trkbal, NULL, NULL, NULL);
        if (cc < 0) return -1;

    } /* end if */

    /* Error if record will not even fit on an empty track */
    if (cc > 0)
    {
        XMINF (4, "\n");
        XMERRF ("Input record CCHHR=%2.2X%2.2X%2.2X%2.2X%2.2X "
                "exceeds output device track size\n",
                blk->cyl[0], blk->cyl[1],
                blk->head[0], blk->head[1], blk->rec);
        return -1;
    }

    /* Determine whether end of extent has been reached */
    if (*reltrk >= maxtrk)
    {
        XMINF (4, "\n");
        XMERR ("Dataset exceeds extent size\n");
        return -1;
    }

    /* Build home address and record 0 if new track */
    if (*usedv == 0)
    {
        init_track (trklen, buf, *cyl, *head, usedv);
    }

    /* Double check that record will not exceed virtual track size */
    if (*usedv + CKDDASD_RECHDR_SIZE + keylen + datalen + 8
        > trklen)
    {
        XMINF (4, "\n");
        XMERRF ("Input record CCHHR=%2.2X%2.2X%2.2X%2.2X%2.2X "
                "exceeds virtual device track size\n",
                blk->cyl[0], blk->cyl[1],
                blk->head[0], blk->head[1], blk->rec);
        return -1;
    }

    /* Add data block to virtual track buffer */
    (*rec)++;
    rechdr = (CKDDASD_RECHDR*)(buf + *usedv);
    rechdr->cyl[0] = (*cyl >> 8) & 0xFF;
    rechdr->cyl[1] = *cyl & 0xFF;
    rechdr->head[0] = (*head >> 8) & 0xFF;
    rechdr->head[1] = *head & 0xFF;
    rechdr->rec = *rec;
    rechdr->klen = keylen;
    rechdr->dlen[0] = (datalen >> 8) & 0xFF;
    rechdr->dlen[1] = datalen & 0xFF;
    *usedv += CKDDASD_RECHDR_SIZE;
    memcpy (buf + *usedv, blk->kdarea, keylen + datalen);
    *usedv += keylen + datalen;

    return 0;
} /* end function write_block */

/*-------------------------------------------------------------------*/
/* XMITPROC main entry point                                         */
/*-------------------------------------------------------------------*/
int main (int argc, char *argv[])
{
int             rc = 0;                 /* Return code               */
int             ifd, ofd;               /* I/O file descriptors      */
BYTE           *ifname, *ofname;        /* I/O file names            */
int             reclen;                 /* Logical record length     */
BYTE           *ibuf;                   /* -> Logical record buffer  */
BYTE           *trkbuf;                 /* -> Output track buffer    */
BYTE            ctl;                    /* 0x20=Control record       */
BYTE            recname[8];             /* Record name               */
int             datarecn;               /* Data record counter       */
HWORD           dsorg;                  /* Dataset organization      */
HWORD           recfm;                  /* Dataset record format     */
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
int             origheads;              /* Number of tracks/cylinder
                                           on original dataset       */
int             numext;                 /* Number of extents         */
EXTDESC         xarray[16];             /* Extent descriptor array   */
DATABLK        *dirblkp;                /* -> First directory block  */
DATABLK       **dirblkpp;               /* -> Directory chain pointer*/
int             dirblkn;                /* #of directory blocks read */
U16             devtype = 0x2311;       /* Output device type        */
int             outheads = 10;          /* Output device trks/cyl    */
int             outtrklv= 3765;         /* Output device track length
                                           of virtual device         */
int             outextsz = 20;          /* Output extent size in trks*/
int             outusedv = 0;           /* Output bytes used on track
                                           of virtual device         */
int             outusedr = 0;           /* Output bytes used on track
                                           of real device            */
int             outtrkbr = 0;           /* Output bytes remaining on
                                           track of real device      */
int             outtrk = 0;             /* Output relative track     */
int             outcyl = 0x05D1;        /* Output cylinder number    */
int             outhead = 0x0007;       /* Output head number        */
int             outrec = 0;             /* Output record number      */

    /* Get the input and output file names */
    if (argc < 3)
    {
        XMERR ("Input and output file names required\n");
        return -1;
    }

    ifname = argv[1];
    ofname = argv[2];

    /* Open the input file */
    ifd = open (ifname, O_RDONLY);
    if (ifd < 0)
    {
        XMERRF ("Cannot open %s: %s\n",
                ifname, strerror(errno));
        return -1;
    }

    /* Create the output file */
    ofd = open (ofname, O_WRONLY | O_CREAT,
                S_IRUSR | S_IWUSR | S_IRGRP);
    if (ofd < 0)
    {
        XMERRF ("Cannot open %s: %s\n",
                ofname, strerror(errno));
        return -1;
    }

    /* Obtain the input logical record buffer */
    ibuf = malloc (65536);
    if (ibuf == NULL)
    {
        XMERRF ("Cannot obtain input buffer: %s\n",
                strerror(errno));
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

    /* Display the file information message */
    XMINFF (1, "Processing file %s\n", ifname);

    /* Initialize the directory block chain */
    dirblkp = NULL;
    dirblkpp = &dirblkp;
    dirblkn = 0;

    /* Read each logical record */
    while (1)
    {
        rc = read_rec (ifd, ifname, ibuf, &ctl);
        if (rc < 0) break;
        reclen = rc;

        /* Process control records */
        if (ctl)
        {
            /* Extract the control record name */
            make_asciiz (recname, sizeof(recname), ibuf, 6);
            XMINFF (4, "Control record: %s length %d\n",
                        recname, reclen);

            /* Exit if control record is a trailer record */
            if (strcmp(recname, "INMR06") == 0)
                break;

            /* Process control record according to type */
            if (strcmp(recname, "INMR02") == 0)
            {
                rc = process_inmr02 (ibuf, reclen, dsorg, recfm,
                                     &lrecl, &blksz, &keyln, &dirnm);
                if (rc < 0) break;
            }
            else
            {
                rc = process_inmrxx (ibuf, reclen);
                if (rc < 0) break;
            }

            /* Reset the data counter if data control record */
            if (strcmp(recname, "INMR03") == 0)
            {
                datarecn = 0;
            }

            /* Loop to get next record */
            continue;

        } /* end if(ctl) */

        /* Process data records */
        datarecn++;
        XMINFF (4, "Data record: length %d\n", reclen);
        if (infolvl >= 5) packet_trace (ibuf, reclen);

        /* Process IEBCOPY header record 1 */
        if (datarecn == 1)
        {
            origheads = process_copyr1 (ibuf, reclen);
            if (origheads < 0) exit(1);
            continue;
        }

        /* Process IEBCOPY header record 2 */
        if (datarecn == 2)
        {
            numext = process_copyr2 (ibuf, reclen, xarray);
            if (numext < 0) exit(1);
            continue;
        }

        /* Process each data block in data record */
        blkptr = ibuf;
        while (reclen > 0)
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

            /* Process directory block or member block */
            if (enddir == 0)
            {
                rc = process_dirblk (datablk, blklen, dirblkpp);
                if (rc < 0) break;
                enddir = rc;

                /* Count the number of directory blocks read */
                if (enddir == 0) dirblkn++;
            }
            else
            {
                blktrk = calculate_ttr (blkcyl, blkhead,
                        origheads, numext, xarray);
                XMINFF (4, "CCHHR=%4.4X%4.4X%2.2X "
                        "(TTR=%4.4X%2.2X) KL=%d DL=%d ",
                        blkcyl, blkhead, blkrec,
                        blktrk, blkrec, keylen, datalen);
            }

            /* Write the data block to the output file */
            rc = write_block (ofd, ofname, datablk, keylen, datalen,
                        devtype, outheads, outtrklv, outextsz,
                        trkbuf, &outusedv, &outusedr, &outtrkbr,
                        &outtrk, &outcyl, &outhead, &outrec);
            if (rc < 0) break;

            XMINFF (4, "-> CCHHR=%4.4X%4.4X%2.2X "
                    "(TTR=%4.4X%2.2X)\n",
                    outcyl, outhead, outrec, outtrk, outrec);

            /* Point to next data block in data record */
            reclen -= blklen;
            blkptr += blklen;

        } /* end while(reclen) */

        /* Exit if error occurred */
        if (rc < 0) break;

    } /* end while(1) */

    /* Write any data remaining in track buffer, and format
       the remainder of the extent with empty tracks */
    while (outtrk < outextsz)
    {
        /* Write current track to output file */
        rc = write_track (ofd, ofname, outheads, outtrklv, trkbuf,
                        &outusedv, &outtrk, &outcyl, &outhead);
        if (rc < 0) break;

        /* Initialize track buffer with next empty track */
        init_track (outtrklv, trkbuf, outcyl, outhead, &outusedv);

    } /* end while */

    /* Close files and release buffers */
    close (ifd);
    while (dirblkp != NULL)
    {
        datablk = (DATABLK*)(dirblkp->header);
        free (dirblkp);
        dirblkp = datablk;
    }
    free (ibuf);
    free (trkbuf);

    return rc;

} /* end function main */
