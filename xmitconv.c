/* XMITCONV.C   (c) Copyright Roger Bowler, 1999                     */
/*              Hercules XMIT file unpacker                          */

/*-------------------------------------------------------------------*/
/* This program creates a virtual DASD volume from a list of         */
/* datasets previously unloaded using the TSO XMIT command.          */
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

#define IPL1_KEYLEN     4
#define IPL1_DATALEN    24
#define IPL2_KEYLEN     4
#define IPL2_DATALEN    144
#define VOL1_KEYLEN     4
#define VOL1_DATALEN    80

#define EBCDIC_END      "\xC5\xD5\xC4"
#define EBCDIC_TXT      "\xE3\xE7\xE3"

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

/* Bit settings for dataset organization byte 0 */
#define DSORG_IS                0x80    /* Indexed sequential        */
#define DSORG_PS                0x40    /* Physically sequential     */
#define DSORG_DA                0x20    /* Direct access             */
#define DSORG_PO                0x02    /* Partitioned organization  */
#define DSORG_U                 0x01    /* Unmovable                 */

/* Bit settings for dataset organization byte 1 */
#define DSORG_AM                0x08    /* VSAM dataset              */

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
/* Definition of data record block in IEBCOPY unload file            */
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
/* Definition of DSCB records in VTOC                                */
/*-------------------------------------------------------------------*/
typedef struct _DSXTENT {               /* Dataset extent descriptor */
        BYTE    xttype;                 /* Extent type               */
        BYTE    xtseqn;                 /* Extent sequence number    */
        HWORD   xtbcyl;                 /* Extent begin cylinder     */
        HWORD   xtbtrk;                 /* Extent begin track        */
        HWORD   xtecyl;                 /* Extent end cylinder       */
        HWORD   xtetrk;                 /* Extent end track          */
    } DSXTENT;

/* Bit definitions for extent type */
#define XTTYPE_UNUSED           0x00    /* Unused extent descriptor  */
#define XTTYPE_DATA             0x01    /* Data extent               */
#define XTTYPE_OVERFLOW         0x02    /* Overflow extent           */
#define XTTYPE_INDEX            0x04    /* Index extent              */
#define XTTYPE_USERLBL          0x40    /* User label extent         */
#define XTTYPE_SHARCYL          0x80    /* Shared cylinders          */
#define XTTYPE_CYLBOUND         0x81    /* Extent on cyl boundary    */

typedef struct _FORMAT1_DSCB {          /* DSCB1: Dataset descriptor */
        BYTE    ds1dsnam[44];           /* Key (44 byte dataset name)*/
        BYTE    ds1fmtid;               /* Format identifier (0xF1)  */
        BYTE    ds1dssn[6];             /* Volume serial number      */
        HWORD   ds1volsq;               /* Volume sequence number    */
        BYTE    ds1credt[3];            /* Dataset creation date...
                                           ...byte 0: Binary year-1900
                                           ...bytes 1-2: Binary day  */
        BYTE    ds1expdt[3];            /* Dataset expiry date       */
        BYTE    ds1noepv;               /* Number of extents         */
        BYTE    ds1bodbd;               /* #bytes used in last dirblk*/
        BYTE    resv1;                  /* Reserved                  */
        BYTE    ds1syscd[13];           /* System code (IBMOSVS2)    */
        BYTE    resv2[7];               /* Reserved                  */
        BYTE    ds1dsorg[2];            /* Dataset organization      */
        BYTE    ds1recfm;               /* Record format             */
        BYTE    ds1optcd;               /* Option codes              */
        HWORD   ds1blkl;                /* Block length              */
        HWORD   ds1lrecl;               /* Logical record length     */
        BYTE    ds1keyl;                /* Key length                */
        HWORD   ds1rkp;                 /* Relative key position     */
        BYTE    ds1dsind;               /* Dataset indicators        */
        FWORD   ds1scalo;               /* Secondary allocation...
                                           ...byte 0: Allocation units
                                           ...bytes 1-3: Quantity    */
        BYTE    ds1lstar[3];            /* Last used TTR             */
        HWORD   ds1trbal;               /* Bytes unused on last trk  */
        BYTE    resv3[2];               /* Reserved                  */
        DSXTENT ds1ext1;                /* First extent descriptor   */
        DSXTENT ds1ext2;                /* Second extent descriptor  */
        DSXTENT ds1ext3;                /* Third extent descriptor   */
        BYTE    ds1ptrds[5];            /* CCHHR of F2 or F3 DSCB    */
    } FORMAT1_DSCB;

/* Bit definitions for ds1dsind */
#define DS1DSIND_LASTVOL        0x80    /* Last volume of dataset    */
#define DS1DSIND_RACFIND        0x40    /* RACF indicated            */
#define DS1DSIND_BLKSIZ8        0x20    /* Blocksize multiple of 8   */
#define DS1DSIND_PASSWD         0x10    /* Password protected        */
#define DS1DSIND_WRTPROT        0x04    /* Write protected           */
#define DS1DSIND_UPDATED        0x02    /* Updated since last backup */
#define DS1DSIND_SECCKPT        0x01    /* Secure checkpoint dataset */

/* Bit definitions for ds1optcd */
#define DS1OPTCD_ICFDSET        0x80    /* Dataset in ICF catalog    */
#define DS1OPTCD_ICFCTLG        0x40    /* ICF catalog               */

/* Bit definitions for ds1scalo byte 0 */
#define DS1SCALO_UNITS          0xC0    /* Allocation units...       */
#define DS1SCALO_UNITS_ABSTR    0x00    /* ...absolute tracks        */
#define DS1SCALO_UNITS_BLK      0x40    /* ...blocks                 */
#define DS1SCALO_UNITS_TRK      0x80    /* ...tracks                 */
#define DS1SCALO_UNITS_CYL      0xC0    /* ...cylinders              */
#define DS1SCALO_CONTIG         0x08    /* Contiguous space          */
#define DS1SCALO_MXIG           0x04    /* Maximum contiguous extent */
#define DS1SCALO_ALX            0x02    /* Up to 5 largest extents   */
#define DS1SCALO_ROUND          0x01    /* Round to cylinders        */

typedef struct _FORMAT4_DSCB {          /* DSCB4: VTOC descriptor    */
        BYTE    ds4keyid[44];           /* Key (44 bytes of 0x04)    */
        BYTE    ds4fmtid;               /* Format identifier (0xF4)  */
        BYTE    ds4hpchr[5];            /* CCHHR of highest F1 DSCB  */
        HWORD   ds4dsrec;               /* Number of format 0 DSCBs  */
        BYTE    ds4hcchh[4];            /* CCHH of next avail alt trk*/
        HWORD   ds4noatk;               /* Number of avail alt tracks*/
        BYTE    ds4vtoci;               /* VTOC indicators           */
        BYTE    ds4noext;               /* Number of extents in VTOC */
        BYTE    resv1[2];               /* Reserved                  */
        FWORD   ds4devsz;               /* Device size (CCHH)        */
        HWORD   ds4devtk;               /* Device track length       */
        BYTE    ds4devi;                /* Non-last keyed blk overhd */
        BYTE    ds4devl;                /* Last keyed block overhead */
        BYTE    ds4devk;                /* Non-keyed block difference*/
        BYTE    ds4devfg;               /* Device flags              */
        HWORD   ds4devtl;               /* Device tolerance          */
        BYTE    ds4devdt;               /* Number of DSCBs per track */
        BYTE    ds4devdb;               /* Number of dirblks/track   */
        DWORD   ds4amtim;               /* VSAM timestamp            */
        BYTE    ds4vsind;               /* VSAM indicators           */
        HWORD   ds4vscra;               /* CRA track location        */
        DWORD   ds4r2tim;               /* VSAM vol/cat timestamp    */
        BYTE    resv2[5];               /* Reserved                  */
        BYTE    ds4f6ptr[5];            /* CCHHR of first F6 DSCB    */
        DSXTENT ds4vtoce;               /* VTOC extent descriptor    */
        BYTE    resv3[25];              /* Reserved                  */
    } FORMAT4_DSCB;

/* Bit definitions for ds4vtoci */
#define DS4VTOCI_DOS            0x80    /* Format 5 DSCBs not valid  */
#define DS4VTOCI_DOSSTCK        0x10    /* DOS stacked pack          */
#define DS4VTOCI_DOSCNVT        0x08    /* DOS converted pack        */
#define DS4VTOCI_DIRF           0x40    /* VTOC contains errors      */
#define DS4VTOCI_DIRFCVT        0x20    /* DIRF reclaimed            */

/* Bit definitions for ds4devfg */
#define DS4DEVFG_TOL            0x01    /* Tolerance factor applies to
                                           all but last block of trk */

typedef struct _F5AVEXT {               /* Available extent in DSCB5 */
        HWORD   btrk;                   /* Extent begin track address*/
        HWORD   ncyl;                   /* Number of full cylinders  */
        BYTE    ntrk;                   /* Number of odd tracks      */
    } F5AVEXT;

typedef struct _FORMAT5_DSCB {          /* DSCB5: Free space map     */
        BYTE    ds5keyid[4];            /* Key (4 bytes of 0x05)     */
        F5AVEXT ds5avext[8];            /* First 8 available extents */
        BYTE    ds5fmtid;               /* Format identifier (0xF5)  */
        F5AVEXT ds5mavet[18];           /* 18 more available extents */
        BYTE    ds5ptrds[5];            /* CCHHR of next F5 DSCB     */
    } FORMAT5_DSCB;

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
/* Static data areas                                                 */
/*-------------------------------------------------------------------*/
BYTE eighthexFF[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
BYTE twelvehex00[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
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
/* Subroutine to display command syntax and exit                     */
/*-------------------------------------------------------------------*/
static void
argexit ( int code )
{
    fprintf (stderr,
            "xmitconv creates a DASD image file from a list "
            "of TSO XMIT files\n"
            "Syntax:\txmitconv ctlfile outfile devtype [msglevel]\n"
            "where:\tctlfile  = name of input control file\n"
            "\toutfile  = name of DASD image file to be created\n"
            "\tdevtype  = 2311, 2314, 3330, 3350, 3380, 3390\n"
            "\tmsglevel = Value 0-5 controls output verbosity\n");
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
unsigned int    i, xi, offset, startoff = 0;
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
    case 0x0F: return "3390";
    } /* end switch(key) */

    return "????";

} /* end function dasd_name */

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
/*      numhead Number of tracks per cylinder                        */
/*      numcyls Number of cylinders per volume                       */
/*      A NULL address may be specified for any of the output        */
/*      fields if the output value is not required.                  */
/*      The return value is 0 if the record will fit on the track,   */
/*      +1 if the record will not fit on the track, or -1 if error.  */
/*-------------------------------------------------------------------*/
static int
capacity_calc (U16 devtype, int used, int keylen, int datalen,
                int *newused, int *trkbaln, int *physlen,
                int *maxdlen, int *numrecs, int *numhead, int *numcyls)
{
int             heads;                  /* Number of tracks/cylinder */
int             cyls;                   /* Number of cyls/volume     */
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
        heads = 1;
        cyls = 200;
        trklen = 20483;
        maxlen = 20483;
        c = 53; x = 133;
        goto formula1;

    case 0x2302:
        heads = 46;
        cyls = 492;
        trklen = 4984;
        maxlen = 4984;
        c = 20; x = 61; d1 = 537; d2 = 512;
        goto formula2;

    case 0x2303:
        heads = 1;
        cyls = 800;
        trklen = 4892;
        maxlen = 4892;
        c = 38; x = 108;
        goto formula1;

    case 0x2311:
        heads = 10;
        cyls = 200;
        trklen = 3625;
        maxlen = 3625;
        c = 20; x = 61; d1 = 537; d2 = 512;
        goto formula2;

    case 0x2314:
        heads = 20;
        cyls = 200;
        trklen = 7294;
        maxlen = 7294;
        c = 45; x = 101; d1 = 2137; d2 = 2048;
        goto formula2;

    case 0x2321:
        heads = 20;
        cyls = 50;
        trklen = 2000;
        maxlen = 2000;
        heads = 20;
        c = 16; x = 84; d1 = 537; d2 = 512;
        goto formula2;

    formula1:
        b1 = keylen + datalen + (keylen == 0 ? 0 : c);
        b2 = b1 + x;
        nrecs = (trklen - b1)/b2 + 1;
        bytes = (used == 0 ? b1 : b2);
        break;

    formula2:
        b1 = keylen + datalen + (keylen == 0 ? 0 : c);
        b2 = ((keylen + datalen) * d1 / d2)
                + (keylen == 0 ? 0 : c) + x;
        nrecs = (trklen - b1)/b2 + 1;
        bytes = (used == 0 ? b1 : b2);
        break;

    case 0x3330:
        heads = 19;
        cyls = 404;
        trklen = 13165;
        maxlen = 13030;
        c = 56;
        bytes = keylen + datalen + (keylen == 0 ? 0 : c) + 135;
        nrecs = trklen / bytes;
        break;

    case 0x3350:
        heads = 30;
        cyls = 555;
        trklen = 19254;
        maxlen = 19069;
        c = 82;
        bytes = keylen + datalen + (keylen == 0 ? 0 : c) + 185;
        nrecs = trklen / bytes;
        break;

    case 0x3380:
        heads = 15;
        cyls = 885;
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
        heads = 15;
        cyls = 1113;
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

    /* Return track length and maximum data length */
    if (physlen != NULL) *physlen = trklen;
    if (maxdlen != NULL) *maxdlen = maxlen;

    /* Return number of records per track */
    if (numrecs != NULL) *numrecs = nrecs;

    /* Return number of tracks per cylinder
       and usual number of cylinders per volume */
    if (numhead != NULL) *numhead = heads;
    if (numcyls != NULL) *numcyls = cyls;

    /* Return if record will not fit on the track */
    if (used + bytes > trklen)
        return +1;

    /* Calculate number of bytes used and track balance */
    if (newused != NULL) *newused = used + bytes;
    if (trkbaln != NULL) *trkbaln = trklen - used - bytes;

    return 0;
} /* end function capacity_calc */

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
    rechdr->dlen[0] = 0;
    rechdr->dlen[1] = 8;

    /* Set number of bytes used in track buffer */
    *usedv = CKDDASD_TRKHDR_SIZE + CKDDASD_RECHDR_SIZE + 8;

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
        XMINF (4, "\n");
        XMERRF ("%s cylinder %u head %u write error: %s\n",
                ofname, *cyl, *head, strerror(errno));
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
                        usedr, trkbal, NULL, NULL, NULL, NULL, NULL);
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
                            usedr, trkbal, NULL, NULL, NULL,
                            NULL, NULL);
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
        XMINF (4, "\n");
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

    /* Calculate the physical track length, device size, and
       the number of DSCBs and directory blocks per track */
    capacity_calc (devtype, 0, 44, 96, NULL, NULL, &physlen,
                    NULL, &numdscb, &numheads, &numcyls);
    capacity_calc (devtype, 0, 8, 256, NULL, NULL, NULL,
                    NULL, &numdblk, NULL, NULL);

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
        if (infolvl >= 5) packet_trace (datablk, 152);

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
        XMINFF (2, "File %lu: DSNAME=%s\n",
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
/*      blkpp   Address of pointer to chain field of previous block  */
/* Output:                                                           */
/*      dirblu  Number of bytes used in directory block              */
/*      The return value is 0 if successful, 1 if end of directory,  */
/*      or -1 if an error occurred.                                  */
/*                                                                   */
/* Each directory block is saved in a chained list.                  */
/* Directory information is listed if infolvl is 3 or greater.       */
/*-------------------------------------------------------------------*/
static int
process_dirblk (DATABLK *xbuf, int blklen, DATABLK ***blkpp,
                int *dirblu)
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

    /* Point to start of directory block data area */
    dirent = xbuf->kdarea + 8;

    /* Load number of bytes in directory block */
    rem = (dirent[0] << 8) | dirent[1];
    if (rem < 2 || rem > 256)
    {
        XMERR ("Directory block byte count is invalid\n");
        return -1;
    }

    /* Return number of bytes used in directory block */
    *dirblu = rem;

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
        if (infolvl >= 5) packet_trace (xbuf, xreclen);

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

            /* Process directory block or member block */
            if (enddir == 0)
            {
                rc = process_dirblk (datablk, blklen, &dirblkpp,
                                    dirblu);
                if (rc < 0) return -1;
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
                        devtype, heads, trklen, maxtrks,
                        trkbuf, &outusedv, &outusedr, &outtrkbr,
                        &outtrk, &outcyl, &outhead, &outrec);
            if (rc < 0) return -1;

            XMINFF (4, "-> CCHHR=%4.4X%4.4X%2.2X "
                    "(TTR=%4.4X%2.2X)\n",
                    outcyl, outhead, outrec, outtrk, outrec);

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

    /* Close input file and release buffers */
    close (xfd);
    while (dirblkp != NULL)
    {
        datablk = (DATABLK*)(dirblkp->header);
        free (dirblkp);
        dirblkp = datablk;
    }
    free (xbuf);

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
/*      method  Processing method (X=XMIT, E=Empty)                  */
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
        *method = 'X';
    else if (strcasecmp(pimeth, "EMPTY") == 0)
        *method = 'E';
    else
    {
        XMERRF ("Invalid initialization method: %s\n", pimeth);
        return -1;
    }

    /* Locate the initialization file name */
    if (*method != 'E')
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
BYTE            dsname[45];             /* Dataset name (ASCIIZ)     */
BYTE            method;                 /* X=XMIT, E=EMPTY           */
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

        case 'X':
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

        case 'E':
            /* Create empty dataset */
            tracks = 0;
            dirblu = 0;

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
/* XMITCONV main entry point                                         */
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
            "Hercules XMIT file conversion program %s "
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

    /* Obtain number of heads and track length for device type */
    rc = capacity_calc (devtype, 0, 0, 0, NULL, NULL, NULL,
                        &outmaxdl, NULL, &outheads, NULL);
    if (rc < 0)
        argexit(3);

    /* Calculate the track size of the virtual device */
    outtrklv = outmaxdl + 104;

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
    ofd = open (ofname, O_WRONLY | O_CREAT | O_TRUNC,
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
