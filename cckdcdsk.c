/* CCKDCDSK.C   (c) Copyright Roger Bowler, 1999                     */
/*       Perform chkdsk for a Compressed CKD Direct Access Storage   */
/*       Device file.                                                */

/*-------------------------------------------------------------------*/
/* Perform check function on a compressed ckd file                   */
/*-------------------------------------------------------------------*/

#include "hercules.h"

#ifndef NO_CCKD

#include "zlib.h"
#ifdef CCKD_BZIP2
#include "bzlib.h"
#endif

typedef struct _SPCTAB {                /* Space table               */
off_t           pos;                    /* Space offset              */
unsigned int    len;                    /* Space length              */
unsigned int    siz;                    /* Space size                */
unsigned int    val;                    /* Value for space           */
void           *ptr;                    /* Pointer to recovered space*/
int             typ;                    /* Type of space             */
               } SPCTAB;

#define SPCTAB_NONE     0               /* Ignore this space entry   */
#define SPCTAB_DEVHDR   1               /* Space is device header    */
#define SPCTAB_CDEVHDR  2               /* Space is compressed hdr   */
#define SPCTAB_L1TAB    3               /* Space is level 1 table    */
#define SPCTAB_L2TAB    4               /* Space is level 2 table    */
#define SPCTAB_TRKIMG   5               /* Space is track image      */
#define SPCTAB_FREE     6               /* Space is free block       */
#define SPCTAB_END      7               /* Space is end-of-file      */

#define cdskmsg(m, format, a...) \
 if(m) fprintf (m, "cckdcdsk  " format, ## a)

/*-------------------------------------------------------------------*/
/* Internal functions                                                */
/*-------------------------------------------------------------------*/

int cckd_chkdsk(int, FILE *, int);
int cdsk_spctab_comp(const void *, const void *);
int cdsk_rcvtab_comp(const void *, const void *);
int cdsk_chk_endian ();
int cdsk_valid_trk (int, unsigned char *, int, int, char *);
int cdsk_build_gap (SPCTAB *, int *, SPCTAB *);

#ifdef CCKD_CHKDSK_MAIN
int syntax ();

/*-------------------------------------------------------------------*/
/* Global data areas                                                 */
/*-------------------------------------------------------------------*/

BYTE eighthexFF[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/*-------------------------------------------------------------------*/
/* Main function for stand-alone chkdsk                              */
/*-------------------------------------------------------------------*/

int main (int argc, char *argv[])
{
int             rc;                     /* Return code               */
char           *fn;                     /* File name                 */
int             fd;                     /* File descriptor           */
int             level=1;                /* Chkdsk level checking     */

    /* parse the arguments */
    for (argc--, argv++ ; argc > 0 ; argc--, argv++)
    {
        if(**argv != '-') break;

        switch(argv[0][1])
        {
            case '0':
            case '1':
            case '3':  if (argv[0][2] != '\0') return syntax ();
                       level = (argv[0][1] & 0xf);
                       break;
            default:   return syntax ();
        }
    }
    if (argc != 1) return syntax ();
    fn = argv[0];

    /* open the file */
    fd = open (fn, O_RDWR);
    if (fd < 0)
    {
        fprintf (stderr,
                 "cckdcdsk: error opening file %s: %s\n",
                 fn, strerror(errno));
        return -1;
    }

    /* call the actual chkdsk function */
    rc = cckd_chkdsk (fd, stderr, level);

    close (fd);

    return rc;

}

/*-------------------------------------------------------------------*/
/* print syntax                                                      */
/*-------------------------------------------------------------------*/

int syntax()
{
    fprintf (stderr, "cckdcdsk [-level] file-name\n"
                "\n"
                "       where level is a digit 0 - 3:\n"
                "         0  --  minimal checking\n"
                "         1  --  normal  checking\n"
                "         3  --  maximal checking\n");
    return -1;
}
#else
extern BYTE eighthexFF[8];
#endif

/*-------------------------------------------------------------------*/
/* Perform check function on a compressed ckd file                   */
/*-------------------------------------------------------------------*/

int cckd_chkdsk(int fd, FILE *m, int level)
{
CKDDASD_DEVHDR  devhdr;                 /* CKD device header         */
CCKDDASD_DEVHDR cdevhdr;                /* Compressed CKD device hdr */
CCKDDASD_DEVHDR cdevhdr2;               /* CCKD header 2             */
CCKD_L1TAB     *l1=NULL;                /* -> Primary lookup table   */
CCKD_L2TAB      l2[256], *l2p=NULL;     /* Secondary lookup table    */
unsigned char  *buf=NULL, *buf2=NULL;   /* Buffers for track image   */
unsigned long   buf2len;                /* Buffer length             */
unsigned char  *trkbuf=NULL;            /* Pointer to track image    */
unsigned long   trklen=0;               /* Length of track image     */
int             rc;                     /* Return code               */
int             crc=-1;                 /* Chdsk return code         */
int             i,j,k,t;                /* Indices                   */
struct stat     fst;                    /* File status information   */
int            *cyltab;                 /* Possible cyls for device  */
unsigned int    cyls, hdrcyls;          /* Total cylinders           */
unsigned int    trks;                   /* Total tracks              */
unsigned int    heads, hdrheads;        /* Heads per cylinder        */
unsigned int    rec0len=8;              /* R0 length                 */
unsigned int    maxdlen;                /* Max data length for device*/
unsigned int    trksz, hdrtrksz;        /* Track size                */
off_t           hipos, lopos;           /* Valid high/low offsets    */
off_t           pos;                    /* File offset               */
int             fsperr=0, l1errs=0, l2errs=0, trkerrs=0;
                                        /* Error indicators          */
off_t           fsp;                    /* Free space offset         */
CCKD_FREEBLK    fb;                     /* Free space block          */
int             n;                      /* Size of space tables      */
SPCTAB         *spc=NULL, *rcv=NULL,    /* Space/Recovery tables     */
               *gap=NULL;               /* Gaps in the file          */
int             s=0, r=0;               /* Space/Recovery indices    */
int             trk;                    /* Current track number      */
int             maxlen;                 /* Max length of track image */
int             gaps=0, gapsize=0;      /* Gaps and size in file     */
unsigned char  *gapbuf=NULL;            /* Buffer containing gap data*/
int             x,y;                    /* Lookup table indices      */
int             rcvtrks;                /* Nbr trks to be recovered  */
int  cyls2311[]   = {200, 0};
int  cyls2314[]   = {200, 0};
int  cyls3330[]   = {404, 808, 0};
int  cyls3340[]   = {348, 696, 0};
int  cyls3350[]   = {555, 0};
int  cyls3375[]   = {959, 0};
int  cyls3380[]   = {885, 1770, 2655, 0};
int  cyls3390[]   = {1113, 2226, 3339, 10017, 0};
char *space[]     = {"none", "devhdr", "cdevhdr", "l1tab", "l2tab",
                     "trkimg", "free_blk", "file_end"};
char *compression[] = {"none", "zlib", "bzip2"};

/*-------------------------------------------------------------------*/
/* Read the device header.                                           */
/*-------------------------------------------------------------------*/

    rc = fstat (fd, &fst);
    if (rc < 0)
    {
        cdskmsg (m, "fstat(%d) error: %s\n",
                 fd, strerror(errno));
        return -1;
    }

    rc = lseek (fd, 0, SEEK_SET);
    rc = read (fd, &devhdr, CKDDASD_DEVHDR_SIZE);
    if (rc != CKDDASD_DEVHDR_SIZE)
    {
        cdskmsg (m, "read(%d) devhdr error: %s\n",
                 fd, strerror(errno));
        return -1;
    }

/*-------------------------------------------------------------------*/
/* Perform checks on the device header.  The following fields        */
/* *must* be correct or the chkdsk function terminates:              */
/*      devid                                                        */
/*      heads                                                        */
/*      trksize                                                      */
/*      devtype                                                      */
/* In this circumstance, the header will need to be                  */
/* manually repaired -- see cckdfix.c for a sample                   */
/*-------------------------------------------------------------------*/

    /* Check the identifier */
    if (memcmp(devhdr.devid, "CKD_C370", 8) != 0)
    {
        cdskmsg (m, "file is not a compressed ckd file\n");
        goto cdsk_return;
    }

    /* Check the device type */
    switch (devhdr.devtype) {

    case 0x11:
        heads = 10;
        maxdlen = 3625;
        cyltab = cyls2311;
        break;

    case 0x14:
        heads = 20;
        maxdlen = 7294;
        cyltab = cyls2314;
        break;

    case 0x30:
        heads = 19;
        maxdlen = 13030;
        cyltab = cyls3330;
        break;

    case 0x40:
        heads = 12;
        maxdlen = 8368;
        cyltab = cyls3340;
        break;

    case 0x50:
        heads = 30;
        maxdlen = 19069;
        cyltab = cyls3350;
        break;

    case 0x75:
        heads = 12;
        maxdlen = 35616;
        cyltab = cyls3375;
        break;

    case 0x80:
        heads = 15;
        maxdlen = 47476;
        cyltab = cyls3380;
        break;

    case 0x90:
        heads = 15;
        maxdlen = 56664;
        cyltab = cyls3390;
        break;

    default:
        cdskmsg (m, "Invalid device type 0x%2.2x in header\n",
                 devhdr.devtype);
        goto cdsk_return;

    } /* end switch(devhdr.devtype) */

    /* Check track size */
    trksz = sizeof(CKDDASD_TRKHDR)
              + sizeof(CKDDASD_RECHDR) + rec0len
              + sizeof(CKDDASD_RECHDR) + maxdlen
              + sizeof(eighthexFF);
    trksz = ((trksz+511)/512)*512;

    hdrtrksz = ((U32)(devhdr.trksize[3]) << 24)
             | ((U32)(devhdr.trksize[2]) << 16)
             | ((U32)(devhdr.trksize[1]) << 8)
             | (U32)(devhdr.trksize[0]);

    if (trksz != hdrtrksz)
    {
        cdskmsg (m, "Invalid track size in header: "
                 "expected 0x%x and found 0x%x\n",
                 trksz, hdrtrksz);
        goto cdsk_return;
    }

    /* Check number of heads */
    hdrheads = ((U32)(devhdr.heads[3]) << 24)
               | ((U32)(devhdr.heads[2]) << 16)
               | ((U32)(devhdr.heads[1]) << 8)
               | (U32)(devhdr.heads[0]);

    if (heads != hdrheads)
    {
        cdskmsg (m, "Invalid number of heads in header: "
                 "expected %d and found %d\n",
                 heads, hdrheads);
        goto cdsk_return;
    }

/*-------------------------------------------------------------------*/
/* read the compressed CKD device header                             */
/*-------------------------------------------------------------------*/

    rc = read (fd, &cdevhdr, CCKDDASD_DEVHDR_SIZE);
    if (rc != CCKDDASD_DEVHDR_SIZE)
    {
        cdskmsg (m, "read(%d) cdevhdr error: %s\n",
                 fd, strerror(errno));
        goto cdsk_return;
    }

/*-------------------------------------------------------------------*/
/* Perform checks on the compressed device header.  The following    */
/* fields *must* be correct or the chkdsk function terminates:       */
/*      options bit CCKD_BIGENDIAN                                   */
/*      numl1tab                                                     */
/*      cyls                                                         */
/* In this circumstance, the compressed header will need to be       */
/* manually repaired -- see cckdfix.c for a sample                   */
/*-------------------------------------------------------------------*/

    /* check byte-order */
    if ((cdsk_chk_endian() == 1 && (cdevhdr.options & CCKD_BIGENDIAN) == 0)
     || (cdsk_chk_endian() == 0 && (cdevhdr.options & CCKD_BIGENDIAN) != 0))
    {
        cdskmsg (m, "Invalid byte order bit in header: "
                 "expected %s and found %s\n",
                 cdsk_chk_endian() ? "`big-endian'" : "`little-endian'",
                 cdsk_chk_endian() ? "`little-endian'" : "`big-endian'");
        goto cdsk_return;
    }

    /* Check number of cylinders */
    hdrcyls = ((U32)(cdevhdr.cyls[3]) << 24)
              | ((U32)(cdevhdr.cyls[2]) << 16)
              | ((U32)(cdevhdr.cyls[1]) << 8)
              | (U32)(cdevhdr.cyls[0]);

    for (i = 0; cyltab[i]; i++)
       if (hdrcyls == cyltab[i]) break;
    if (cyltab[i] == 0)
    {
        cdskmsg (m, "Invalid number of cylinders in header: "
                 "expected");
        for (i = 0; cyltab[i]; i++)
          cdskmsg (m," %d%c", cyltab[i], cyltab[i+1] ? ',' : ' ');
        cdskmsg (m, "and found %d\n", hdrcyls);
        goto cdsk_return;
    }

    if (cdevhdr.numl1tab != (hdrcyls * heads + 255) / 256)
    {
        cdskmsg (m, "Invalid number of l1 table entries in header: "
                 "expected %d and found %d\n",
                 (hdrcyls * heads + 255) / 256, cdevhdr.numl1tab);
        goto cdsk_return;
    }
    cyls = hdrcyls;
    trks = cyls * heads;

/*-------------------------------------------------------------------*/
/* Read the primary lookup table                                     */
/* We *must* be able to successfully read this table, too            */
/*-------------------------------------------------------------------*/

    l1 = malloc (cdevhdr.numl1tab * CCKD_L1TAB_SIZE);
    if (l1 == NULL)
    {
        cdskmsg (m, "malloc() l1tab error, size %ud: %s\n",
            (unsigned int) (cdevhdr.numl1tab * CCKD_L1TAB_SIZE),
            strerror(errno));
        goto cdsk_return;
     }

    rc = read (fd, l1, cdevhdr.numl1tab * CCKD_L1TAB_SIZE);
    if (rc != cdevhdr.numl1tab * CCKD_L1TAB_SIZE)
    {
        cdskmsg (m, "read(%d) l1tab error, size %ud: %s\n",
             (int) fd, (unsigned int) (cdevhdr.numl1tab * CCKD_L1TAB_SIZE),
             strerror(errno));
        goto cdsk_return;
    }

/*-------------------------------------------------------------------*/
/* Set space boundaries                                              */
/*-------------------------------------------------------------------*/

    lopos = CCKD_L1TAB_POS + cdevhdr.numl1tab * CCKD_L1TAB_SIZE;
    hipos = fst.st_size;

/*-------------------------------------------------------------------*/
/* Get some buffers                                                  */
/*-------------------------------------------------------------------*/

    buf = malloc(trksz);
    if (buf == NULL)
    {
        cdskmsg (m, "malloc() failed for buffer, size %d: %s\n",
                trksz, strerror(errno));
        goto cdsk_return;
    }
    buf2 = malloc(trksz);
    if (buf2 == NULL)
    {
        cdskmsg (m, "malloc() failed for buffer, size %d: %s\n",
                trksz, strerror(errno));
        goto cdsk_return;
    }

/*-------------------------------------------------------------------*/
/* Check free space chain                                            */
/*                                                                   */
/* Things we check for :                                             */
/*    (1) free space offset is within a valid position in the file   */
/*    (2) free space block can be lseek()ed and read() without error */
/*    (3) length of the free space is at least the size of a free    */
/*        space block                                                */
/*    (4) next free space does not precede this free space           */
/*    (5) free space does not extend beyond the end of the file      */
/*    (6) free space statistics in cdevhdr match up                  */
/*                                                                   */
/* If all goes well we build a free space array (fs[])               */
/*                                                                   */
/*-------------------------------------------------------------------*/

    memset (&cdevhdr2, 0, CCKDDASD_DEVHDR_SIZE);
    cdevhdr2.size = hipos;
    cdevhdr2.used = CKDDASD_DEVHDR_SIZE + CCKDDASD_DEVHDR_SIZE +
                    cdevhdr.numl1tab * CCKD_L1TAB_SIZE;
    cdevhdr2.free = cdevhdr.free;
    for (fsperr = 0, fsp = cdevhdr.free; fsp; fsp = fb.pos)
    {
        fsperr = 1;		          /* turn on error indicator */
        memset (&fb, 0, CCKD_FREEBLK_SIZE);
        if (fsp < lopos || fsp > hipos - CCKD_FREEBLK_SIZE) break;
        rc = lseek (fd, fsp, SEEK_SET);
        if (rc == -1) break;
        rc = read (fd, &fb, CCKD_FREEBLK_SIZE);
        if (rc != CCKD_FREEBLK_SIZE) break;
        if (fb.len < CCKD_FREEBLK_SIZE || fsp + fb.len > hipos) break;
        if (fb.pos && (fb.pos <= fsp + fb.len ||
                       fb.pos > hipos - CCKD_FREEBLK_SIZE)) break;
        fsperr = 0;                       /* reset error indicator   */
        cdevhdr2.free_number++;
        cdevhdr2.free_total += fb.len;
        if (fb.len > cdevhdr2.free_largest)
            cdevhdr2.free_largest = fb.len;
    }

    if (fsperr)
    {
        cdskmsg (m, "free space errors found: pos=0x%lx nxt=0x%x len=%d\n",
                fsp, fb.pos, fb.len);
        if (level == 0)
        {
            level = 1;
            cdskmsg (m, "forcing check level %d\n", level);
        }
    }

/*-------------------------------------------------------------------*/
/* get the space/recovery/gap tables                                 */
/*-------------------------------------------------------------------*/

    n = 4 + cdevhdr.numl1tab + trks + cdevhdr2.free_number;

    spc = malloc (n * sizeof(SPCTAB));
    if (spc == NULL)
    {
        cdskmsg (m, "malloc() failed for space table size %ud: %s\n",
                 (unsigned int) (n * sizeof(SPCTAB)), strerror(errno));
        goto cdsk_return;
    }

    rcv = malloc (n * sizeof(SPCTAB));
    if (rcv == NULL)
    {
        cdskmsg (m, "calloc() failed for recovery table size %ud: %s\n",
                 (unsigned int) (n * sizeof(SPCTAB)), strerror(errno));
        goto cdsk_return;
    }

    gap = malloc (n * sizeof(SPCTAB));
    if (rcv == NULL)
    {
        cdskmsg (m, "calloc() failed for gap table size %ud: %s\n",
                 (unsigned int) (n * sizeof(SPCTAB)), strerror(errno));
        goto cdsk_return;
    }

/*-------------------------------------------------------------------*/
/* populate the space/recovery tables; perform space checks          */
/*-------------------------------------------------------------------*/

space_check:
    memset (spc, 0, n * sizeof(SPCTAB));
    memset (rcv, 0, n * sizeof(SPCTAB));
    memset (gap, 0, n * sizeof(SPCTAB));

    s = r = 0;

    /* ckddasd device header space */
    spc[s].pos = 0;
    spc[s].len = spc[s].siz = CKDDASD_DEVHDR_SIZE;
    spc[s++].typ = SPCTAB_DEVHDR;

    /* compressed ckddasd device header space */
    spc[s].pos = CKDDASD_DEVHDR_SIZE;
    spc[s].len = spc[s].siz = CCKDDASD_DEVHDR_SIZE;
    spc[s++].typ = SPCTAB_CDEVHDR;

    /* level 1 lookup table space */
    spc[s].pos = CCKD_L1TAB_POS;
    spc[s].len = spc[s].siz = cdevhdr.numl1tab * CCKD_L1TAB_SIZE;
    spc[s++].typ = SPCTAB_L1TAB;

    /* end-of-file */
    spc[s].pos = hipos;
    spc[s++].typ = SPCTAB_END;

    /* free spaces */
    for (fsp = cdevhdr.free, i=0; i < cdevhdr2.free_number;
         fsp = fb.pos, i++)
    {
        rc = lseek (fd, fsp, SEEK_SET);
        rc = read (fd, &fb, CCKD_FREEBLK_SIZE);
        spc[s].pos = fsp;
        spc[s].len = spc[s].siz = fb.len;
        spc[s++].typ = SPCTAB_FREE;
    }

    l1errs = l2errs = trkerrs = 0;

    /* level 2 lookup table and track image spaces */
    for (i = 0; i < cdevhdr.numl1tab; i++)
    {
        int valid_l2, valid_trks, invalid_trks;
        char trk_err[256];

        if (l1[i] == 0) continue;

        /* check for valid offset in l1tab entry */
        if (l1[i] < lopos || l1[i] > hipos - 256 * CCKD_L2TAB_SIZE)
        {
            cdskmsg (m, "l1[%d] has bad offset 0x%x\n", i, l1[i]);
            l1errs++;

        /* recover all tracks for the level 2 table entry */
        bad_l2:
            if (level == 0)
            {
                level = 1;
                cdskmsg (m, "forcing check level %d\n", level);
                goto space_check;
            }
            cdskmsg (m, "tracks %d thru %d will be recovered\n",
                     i * 256, i * 256 + 255 < trks ?
                              i * 256 + 255 : trks - 1);
            rcv[r].typ = SPCTAB_L2TAB;
            rcv[r].len = rcv[r].siz = 256 * CCKD_L2TAB_SIZE;
            rcv[r++].val = i;
            for (j = 0; j < 256; j++)
            {
                if (i * 256 + j >= trks) break;
                rcv[r].typ = SPCTAB_TRKIMG;
                rcv[r++].val = i * 256 + j;
            }
            continue;
        }

        /* read the level 2 table */
        rc = lseek (fd, l1[i], SEEK_SET);
        if (rc == -1)
        {
            cdskmsg (m, "l1[%d] lseek error offset 0x%x: %s\n",
                    i, l1[i], strerror(errno));
            l2errs++;
            goto bad_l2;
        }

        rc = read (fd, &l2, 256 * CCKD_L2TAB_SIZE);
        if (rc != 256 * CCKD_L2TAB_SIZE)
        {
            cdskmsg (m, "l1[%d] read error offset 0x%x: %s\n",
                    i , l1[i], strerror(errno));
            l2errs++;
            goto bad_l2;
        }

        /* validate each level 2 entry */
        valid_l2 = 1;
    validate_l2:
        valid_trks = invalid_trks = 0;
        for (j = 0; j < 256; j++)
        {
            if (l2[j].pos == 0) continue;
            trk = i * 256 + j;

            /* consistency check on level 2 table entry */
            if (trk >= trks ||  l2[j].pos < lopos ||
                l2[j].pos + l2[j].size > hipos ||
                l2[j].len <= CKDDASD_TRKHDR_SIZE ||
                l2[j].len > l2[j].size || l2[j].size > trksz)
            {
                sprintf(trk_err, "l2tab inconsistency track %d", trk);
            bad_trk:
                if (level == 0)
                {
                    level = 1;
                    cdskmsg (m, "forcing check level %d\n", level);
                    goto space_check;
                }
                if (valid_l2)
                {   /* start over but with valid_l2 false */
                    valid_l2 = 0;
                    for ( ;
                         spc[s - 1].typ == SPCTAB_TRKIMG &&
                         spc[s - 1].val >= i * 256 &&
                         spc[s - 1].val <= i * 256 + 255;
                         s--)
                        memset (&spc[s - 1], 0, sizeof (SPCTAB));
                    goto validate_l2;
                }
                cdskmsg (m, "%s\n"
                   "  l2[%d,%d] offset 0x%x len %d size %d\n",
                   trk_err, i, j, l2[j].pos, l2[j].len, l2[j].size);
                trkerrs++;
                invalid_trks++;
                cdskmsg (m, " track %d will be recovered\n", trk);
                rcv[r].pos = l2[j].pos;
                rcv[r].len = l2[j].len;
                rcv[r].siz = l2[j].size;
                rcv[r].val = trk;
                rcv[r++].typ = SPCTAB_TRKIMG;
                continue;
            }

            /* read the track header */
            if (level >= 1)
            {
                rc = lseek (fd, l2[j].pos, SEEK_SET);
                if (rc == -1)
                {
                    sprintf (trk_err, "lseek error track %d: %s", trk,
                             strerror(errno));
                    goto bad_trk;
                }
                rc = read (fd, buf, CKDDASD_TRKHDR_SIZE);
                if (rc != CKDDASD_TRKHDR_SIZE)
                {
                    sprintf (trk_err, "read error track %d: %s", trk,
                             strerror(errno));
                    goto bad_trk;
                }

                /* consistency check on track header */
                if (buf[0] > CCKD_COMPRESS_MAX ||
                    (buf[1] * 256 + buf[2]) >= cyls ||
                    (buf[3] * 256 + buf[4]) >= heads ||
                    (buf[1] * 256 + buf[2]) * heads +
                    (buf[3] * 256 + buf[4]) != trk)
                {
                    sprintf (trk_err, "track %d invalid header "
                             "0x%2.2x%2.2x%2.2x%2.2x%2.2x", trk,
                             buf[0], buf[1], buf[2], buf[3], buf[4]);
                    goto bad_trk;
                }
            }

            /* if we've had problems with the level 2 table
               then validate the entire track */
            if (!valid_l2 || level >= 3)
            {
                rc = read (fd, &buf[CKDDASD_TRKHDR_SIZE],
                          l2[j].len - CKDDASD_TRKHDR_SIZE);
                if (rc != l2[j].len - CKDDASD_TRKHDR_SIZE)
                {
                    sprintf (trk_err, "read error track %d: %s",
                             trk, strerror(errno));
                    goto bad_trk;
                }
                switch (buf[0])
                {
                    case CCKD_COMPRESS_NONE:
                        trkbuf = buf;
                        trklen = l2[j].len;
                        break;

                    case CCKD_COMPRESS_ZLIB:
                        memcpy (buf2, buf, CKDDASD_TRKHDR_SIZE);
                        buf2len = trksz - CKDDASD_TRKHDR_SIZE;
                        rc = uncompress (&buf2[CKDDASD_TRKHDR_SIZE],
                                         &buf2len,
                                         &buf[CKDDASD_TRKHDR_SIZE],
                                         l2[j].len);
                        if (rc != Z_OK)
                        {
                            sprintf (trk_err, "uncompress error for "
                                     "track %d: rc=%d", trk, rc);
                            goto bad_trk;
                        }
                        trkbuf = buf2;
                        trklen = buf2len + CKDDASD_TRKHDR_SIZE;
                        break;

#ifdef CCKD_BZIP2
                    case CCKD_COMPRESS_BZIP2:
                        memcpy (buf2, buf, CKDDASD_TRKHDR_SIZE);
                        buf2len = trksz - CKDDASD_TRKHDR_SIZE;
                        rc = BZ2_bzBuffToBuffDecompress (
                                         &buf2[CKDDASD_TRKHDR_SIZE],
                                         (unsigned int *)&buf2len,
                                         &buf[CKDDASD_TRKHDR_SIZE],
                                         l2[j].len, 0, 0);
                        if (rc != BZ_OK)
                        {
                            sprintf (trk_err, "decompress error for "
                                     "track %d: rc=%d", trk, rc);
                            goto bad_trk;
                        }
                        trkbuf = buf2;
                        trklen = buf2len + CKDDASD_TRKHDR_SIZE;
                        break;
#endif

                    default:
                        sprintf (trk_err, "track %d unknown "
                                 "compression: 0x%x", trk,  buf[0]);
                        goto bad_trk;
                }
                trkbuf[0] = 0;
                if (cdsk_valid_trk (trk, trkbuf, heads, trklen, trk_err) != trklen)
                    goto bad_trk;
            }

            /* add track to the space table */
            valid_trks++;
            cdevhdr2.used += l2[j].len;
            cdevhdr2.free_imbed += l2[j].size - l2[j].len;
            cdevhdr2.free_total += l2[j].size - l2[j].len;
            spc[s].pos = l2[j].pos;
            spc[s].len = l2[j].len;
            spc[s].siz = l2[j].size;
            spc[s].val = trk;
            spc[s++].typ = SPCTAB_TRKIMG;
        }

        /* if the level 2 table appears valid, add the table to the
           space table; otherwise add the table to the recover table */
        if (valid_l2 || valid_trks > 0)
        {
            cdevhdr2.used += 256 * CCKD_L2TAB_SIZE;
            spc[s].pos = l1[i];
            spc[s].len = spc[s].siz = 256 * CCKD_L2TAB_SIZE;
            spc[s].typ = SPCTAB_L2TAB;
            spc[s++].val = i;
        }
        else
        {
            cdskmsg (m, "l2[%d] will be recovered due to errors\n", i);
            l2errs++;
            for ( ;
                 spc[s - 1].typ == SPCTAB_TRKIMG &&
                 spc[s - 1].val >= i * 256 &&
                 spc[s - 1].val <= i * 256 + 255;
                 s--)
                memset (&spc[s - 1], 0, sizeof (SPCTAB));
            for ( ;
                 rcv[r - 1].typ == SPCTAB_TRKIMG &&
                 rcv[r - 1].val >= i * 256 &&
                 rcv[r - 1].val <= i * 256 + 255;
                 r--)
                memset (&rcv[r - 1], 0, sizeof (SPCTAB));
            goto bad_l2;
        }
    }

/*-------------------------------------------------------------------*/
/* we will rebuild free space on any kind of error                   */
/*-------------------------------------------------------------------*/

    if (memcmp (&cdevhdr.CCKD_FREEHDR, &cdevhdr2.CCKD_FREEHDR,
        CCKD_FREEHDR_SIZE) != 0 || r || l1errs || l2errs || trkerrs)
        fsperr = 1;

/*-------------------------------------------------------------------*/
/* look for gaps and overlaps                                        */
/*                                                                   */
/* Overlaps aren't handled particularly elegantly, but then,         */
/* overlaps aren't exactly expected either.                          */
/*                                                                   */
/*-------------------------------------------------------------------*/

overlap:
    qsort ((void *)spc, s, sizeof(SPCTAB), cdsk_spctab_comp);
    for ( ; spc[s-1].typ == SPCTAB_NONE; s--);
    for (i = 0; i < s - 1; i++)
    {
//      cdskmsg (m, "%s[%d] pos 0x%x length %d\n",
//              space[spc[i].typ], spc[i].val, spc[i].pos, spc[i].len);
        if (spc[i].pos + spc[i].siz < spc[i+1].pos)
        {
            cdskmsg (m, "gap at pos 0x%lx length %ld\n",
                    spc[i].pos + spc[i].siz,
                    spc[i+1].pos - (spc[i].pos + spc[i].siz));
        }
        else if (spc[i].pos + spc[i].len > spc[i+1].pos)
        {
            cdskmsg (m, "%s at pos 0x%lx length %d overlaps "
                    "%s at pos 0x%lx\n",
                    space[spc[i].typ], spc[i].pos, spc[i].len,
                    space[spc[i+1].typ], spc[i+1].pos);
            for (j = i; j < s; j++)
            {
                if (spc[i].pos + spc[i].siz <= spc[j].pos) break;
                switch (spc[j].typ)
                {
                    case SPCTAB_FREE:
                        fsperr = 1;
                        memset (&spc[j], 0, sizeof(SPCTAB));
                        break;
                    case SPCTAB_L2TAB:
                        l2errs++;
                        memcpy (&rcv[r++], &spc[j], sizeof(SPCTAB));
                        for (k = 0; k < s - 1; k++)
                        {
                            if (spc[k].typ == SPCTAB_TRKIMG &&
                                spc[k].val >= spc[j].val * 256 &&
                                spc[k].val <= spc[j].val * 256 + 255)
                            {
                                memcpy (&rcv[r++], &spc[k], sizeof(SPCTAB));
                                memset (&spc[k], 0, sizeof(SPCTAB));
                            }
                        }
                        memset (&spc[j], 0, sizeof(SPCTAB));
                        break;
                    case SPCTAB_TRKIMG:
                        trkerrs++;
                        memcpy (&rcv[r++], &spc[j], sizeof(SPCTAB));
                        memset (&spc[j], 0, sizeof(SPCTAB));
                        break;
                    case SPCTAB_END:
                        break;
                }
            }
            goto overlap;
        }
    }

    /* build the gap table */
    gaps = cdsk_build_gap (spc, &s, gap);

    /* sort the recovery table by track */
    if (r > 0) qsort ((void *)rcv, r, sizeof(SPCTAB), cdsk_rcvtab_comp);

    /* if any kind of error, indicate free space error */
    if (gaps || r || l1errs || l2errs || trkerrs) fsperr = 1;

#if 0
/*-------------------------------------------------------------------*/
/* some debugging stuff                                              */
/*-------------------------------------------------------------------*/

    cdskmsg (m, "gap table  size %d\n               position   size   data\n", gaps);
    for (i = 0; i < gaps; i++)
    {char buf[5];
        lseek (fd, gap[i].pos, SEEK_SET);
        read (fd, &buf, 5);
        cdskmsg (m, "%3d 0x%8.8x %5d %2.2x%2.2x%2.2x%2.2x%2.2x\n",
                 i+1, (int)gap[i].pos, gap[i].siz, buf[0], buf[1], buf[2], buf[3], buf[4]);
    }
    cdskmsg (m,"recovery table  size %d\n                  type value\n", r);
    for (i = 0; i < r; i++)
        cdskmsg (m, "%3d %8s %5d\n", i+1, space[rcv[i].typ], rcv[i].val);
    if (gaps || r) goto cdsk_return;
#endif

/*-------------------------------------------------------------------*/
/* perform track image space recovery                                */
/*-------------------------------------------------------------------*/

    /* count the number of tracks to be recovered */
    for (i = rcvtrks = 0; i < r; i++)
        if (rcv[i].typ == SPCTAB_TRKIMG) rcvtrks++;

    for (gapsize = i = 0; r > 0 && i < gaps && rcvtrks > 0; i++)
    {
        /* get new buffer if previous isn't large enough */
        if (gap[i].siz > gapsize)
        {
            if (gapbuf != NULL) free (gapbuf);
            gapsize = gap[i].siz;
            gapbuf = malloc (gapsize);
            if (gapbuf ==NULL)
            {
                cdskmsg (m, "malloc failed for gap buf, size %d:%s\n",
                        gapsize, strerror(errno));
                goto cdsk_return;
            }
        }

        /* read the gap */
        rc = lseek (fd, gap[i].pos, SEEK_SET);
        if (rc == -1)
        {
            cdskmsg (m, "lseek failed for gap at pos 0x%lx: %s\n",
                    gap[i].pos, strerror(errno));
            goto cdsk_return;
        }
        rc = read (fd, gapbuf, gap[i].siz);
        if (rc != gap[i].siz)
        {
            cdskmsg (m, "read failed for gap at pos 0x%lx length %d: %s\n",
                    gap[i].pos, gap[i].siz, strerror(errno));
            goto cdsk_return;
        }
        cdskmsg (m, "track recovery for gap at pos 0x%lx length %d\n",
                 gap[i].pos, gap[i].siz);

        /* search for track images in the gap */
        for (j = 0; j < gap[i].siz; )
        {
            /* test for possible track header */
            if (!(gap[i].siz - j > CKDDASD_TRKHDR_SIZE &&
                  gapbuf[j] <= CCKD_COMPRESS_MAX &&
                  (gapbuf[j+1] << 8) + gapbuf[j+2] < cyls &&
                  (gapbuf[j+3] << 8) + gapbuf[j+4] < heads))
            {   j++; continue;
            }

            /* get track number */
            trk = ((gapbuf[j+1] << 8) + gapbuf[j+2]) * heads +
                   (gapbuf[j+3] << 8) + gapbuf[j+4];

            cdskmsg (m, "possible track header for trk %d at pos 0x%lx\n",
                     trk, gap[i].pos + j);

            /* see if track is to be recovered */
            for (k = 0; k < r; k++)
                if (rcv[k].typ == SPCTAB_TRKIMG &&
                    trk <= rcv[k].val) break;

            /* if track is not being recovered, continue */
            if (!(k < r && rcv[k].typ == SPCTAB_TRKIMG &&
                  rcv[k].val == trk))
            {   cdskmsg (m, "track %d is not being recovered\n",trk);
                j++;  continue;
            }

            /* calculate maximum track length */
            if (gap[i].siz - j < trksz) maxlen = gap[i].siz - j;
            else maxlen = trksz;

            cdskmsg (m, "trying to recover track %d at pos 0x%lx\n   "
                        "start length %d compression %s\n",
                     trk, gap[i].pos + j, rcv[k].len,
                     compression[gapbuf[j]]);

            /* get track image and length */
            switch (gapbuf[j])
            {
                case CCKD_COMPRESS_NONE:
                    trkbuf = &gapbuf[j];
                    trklen = cdsk_valid_trk (trk, trkbuf, heads, maxlen, NULL);
                    break;

                case CCKD_COMPRESS_ZLIB:
                    /* try to uncompress the track */
                    memcpy (buf2, &gapbuf[j], CKDDASD_TRKHDR_SIZE);
                    buf2[0] = 0;
                    maxlen -= CKDDASD_TRKHDR_SIZE;

                    /* attempt to uncompress the buffer starting with
                       the length in the recovery table.  We then try
                       lengths on both sides of the recovery length,
                       the theory being that the actual length must be
                       somewhere close to the original length */
                    for (t = 0; rcv[k].len + t <= maxlen ||
                                rcv[k].len - t >  CKDDASD_TRKHDR_SIZE; t++)
                    {
                        /* try the recovery length or the next length
                           greater */
                        if (rcv[k].len + t <= maxlen)
                        {
                            buf2len = trksz - CKDDASD_TRKHDR_SIZE;
                            trklen = rcv[k].len + t - CKDDASD_TRKHDR_SIZE;
                            rc = uncompress (&buf2[CKDDASD_TRKHDR_SIZE],
                                             &buf2len,
                                             &gapbuf[j+CKDDASD_TRKHDR_SIZE],
                                             trklen);
                            buf2len += CKDDASD_TRKHDR_SIZE;
                            if (rc == Z_OK)
                            {
                                if (buf2len == cdsk_valid_trk (
                                      trk, buf2, heads, buf2len, NULL))
                                    break;
                                else rc = -1;
                            }
                        }
                        /* try the next length less than the recovery length */
                        if (rc != Z_OK && rcv[k].len - (t+1) > CKDDASD_TRKHDR_SIZE)
                        {
                            buf2len = trksz - CKDDASD_TRKHDR_SIZE;
                            trklen = rcv[k].len - (t+1) - CKDDASD_TRKHDR_SIZE;
                            rc = uncompress (&buf2[CKDDASD_TRKHDR_SIZE],
                                             &buf2len,
                                             &gapbuf[j+CKDDASD_TRKHDR_SIZE],
                                             trklen);
                            buf2len += CKDDASD_TRKHDR_SIZE;
                            if (rc == Z_OK)
                            {
                                if (buf2len == cdsk_valid_trk (
                                      trk, buf2, heads, buf2len, NULL))
                                    break;
                                else rc = -1;
                            }
                        }
                        if (rc == Z_OK) break;
                    }
                    /* check if we were able to uncompress the track image */
                    if (rc == Z_OK)
                    {   trkbuf = &gapbuf[j]; trklen += CKDDASD_TRKHDR_SIZE;
                    }
                    else
                    {   trkbuf = NULL; trklen = -1;
                    }
                    break;

#ifdef CCKD_BZIP2
                case CCKD_COMPRESS_BZIP2:
                    /* try to decompress the track */
                    memcpy (buf2, &gapbuf[j], CKDDASD_TRKHDR_SIZE);
                    buf2[0] = 0;
                    maxlen -= CKDDASD_TRKHDR_SIZE;

                    /* attempt to decompress the buffer starting with
                       the length in the recovery table.  We then try
                       lengths on both sides of the recovery length,
                       the theory being that the actual length must be
                       somewhere close to the original length */
                    for (t = 0; rcv[k].len + t <= maxlen ||
                                rcv[k].len - t >  CKDDASD_TRKHDR_SIZE; t++)
                    {
                        /* try the next length equal to or greater than
                           the recovery length */
                        if (rcv[k].len + t <= maxlen)
                        {
                            buf2len = trksz - CKDDASD_TRKHDR_SIZE;
                            trklen = rcv[k].len + t;
                            rc = BZ2_bzBuffToBuffDecompress (
                                             &buf2[CKDDASD_TRKHDR_SIZE],
                                             (unsigned int *)&buf2len,
                                             &gapbuf[j+CKDDASD_TRKHDR_SIZE],
                                             trklen, 0, 0);
                            buf2len += CKDDASD_TRKHDR_SIZE;
                            if (rc == BZ_OK)
                            {
                                if (buf2len == cdsk_valid_trk (
                                      trk, buf2, heads, buf2len, NULL))
                                    break;
                                else rc = -1;
                            }
                        }
                        /* try the next length less than the recovery length */
                        if (rc != BZ_OK && rcv[k].len - (t+1) > CKDDASD_TRKHDR_SIZE)
                        {
                            buf2len = trksz - CKDDASD_TRKHDR_SIZE;
                            trklen = rcv[k].len - (t+1);
                            rc = BZ2_bzBuffToBuffDecompress (
                                             &buf2[CKDDASD_TRKHDR_SIZE],
                                             (unsigned int *)&buf2len,
                                             &gapbuf[j+CKDDASD_TRKHDR_SIZE],
                                             trklen, 0, 0);
                            buf2len += CKDDASD_TRKHDR_SIZE;
                            if (rc == BZ_OK)
                            {
                                if (buf2len == cdsk_valid_trk (
                                      trk, buf2, heads, buf2len, NULL))
                                    break;
                                else rc = -1;
                            }
                        }
                        if (rc == BZ_OK) break;
                    }
                    /* check if we were able to uncompress the track image */
                    if (rc == BZ_OK)
                    {   trkbuf = &gapbuf[j]; trklen += CKDDASD_TRKHDR_SIZE;
                    }
                    else
                    {   trkbuf = NULL; trklen = -1;
                    }
                    break;
#endif

            } /* switch gapbuf[j] (ie compression type) */

            /* continue if track couldn't be uncompressed or isn't valid */
            if (trkbuf == NULL || trklen == -1)
            {   cdskmsg (m, "unable to recover track %d at pos 0x%lx\n",
                            trk, gap[i].pos + j);
                j++; continue;
            }

            cdskmsg (m, "track %d recovered, offset 0x%lx length %ld\n",
                    trk, gap[i].pos + j, trklen);

            /* enter the recovered track into the space table */
            spc[s].pos = gap[i].pos + j;
            spc[s].len = spc[s].siz = trklen;
            spc[s].val = trk;
            spc[s++].typ = SPCTAB_TRKIMG;

            /* remove the entry from the recovery table */
            memset (&rcv[k], 0, sizeof(SPCTAB));

            /* update the level 2 table entry */
            x = trk / 256; y = trk % 256;
            for (k = k - 1; k >= 0; k--)
            {
                if (rcv[k].typ == SPCTAB_L2TAB && rcv[k].val == x)
                    break;
            }
            if (k >= 0) /* level 2 table is also being recovered */
            {
                if (rcv[k].ptr == NULL)
                {
                    rcv[k].ptr = calloc (256, CCKD_L2TAB_SIZE);
                    if (rcv[k].ptr == NULL)
                    {
                        cdskmsg (m, "calloc failed l2 recovery buf, size %ud: %s\n",
                                (unsigned int) (256*CCKD_L2TAB_SIZE),
                                strerror(errno));
                        goto cdsk_return;
                    }
                }
                l2p = (CCKD_L2TAB *) rcv[k].ptr;
                l2p[y].pos = spc[s-1].pos;
                l2p[y].len = l2p[y].size = spc[s-1].len;
            }
            else /* level 2 table entry is in the file */
            {
                rc = lseek (fd, l1[x] + y * CCKD_L2TAB_SIZE, SEEK_SET);
                rc = read (fd, &l2, CCKD_L2TAB_SIZE);
                l2[0].pos = spc[s-1].pos;
                l2[0].len = l2[0].size = spc[s-1].len;
                rc = lseek (fd, l1[x] + y * CCKD_L2TAB_SIZE, SEEK_SET);
                rc = write (fd, &l2, CCKD_L2TAB_SIZE);
            }

            /* position past the track image */
            j += trklen;

            /* decrement trks to be recovered; exit if none left */
            rcvtrks--;
            if (rcvtrks == 0) break;
        } /* for each byte in the gap */

    } /* for each gap */

/*-------------------------------------------------------------------*/
/* figure out what we have left to recover                           */
/*                                                                   */
/* 'r' will have the index of the last l2tab entry in the recovery   */
/* or zero if no level 2 tables need to be recovered.                */
/*-------------------------------------------------------------------*/

    if (r > 0)
    {
        qsort ((void *)rcv, r, sizeof(SPCTAB), cdsk_rcvtab_comp);
        for ( ; rcv[r-1].typ == SPCTAB_NONE; r--);
    }
    if (r > 0)
    {
        for (i = j = 0; i < r; i++)
        {
            switch (rcv[i].typ)
            {
                case SPCTAB_L2TAB:
                    j = i;
                    break;
                case SPCTAB_TRKIMG:
                    if (r == 0 || rcv[i].val > rcv[j].val * 256 + 255)
                    {
                        cdskmsg (m, "track %d was not recovered\n",
                                rcv[i].val);
                    }
                    else
                    {    /* level 2 is table being recovered; see if
                            track existed in the old level 2 table */
                         x = rcv[i].val / 256; y = rcv[i].val % 256;
                         rc = lseek (fd, l1[x] + y * CCKD_L2TAB_SIZE,
                                     SEEK_SET);
                         rc = read (fd, &l2, CCKD_L2TAB_SIZE);
                         if (l2[0].pos != 0)
                             cdskmsg (m, "track %d was not recovered\n",
                                     rcv[i].val);
                    }
                    break;
            }
        }
        r = j;
    }

/*-------------------------------------------------------------------*/
/* remove free space if any errors were encountered                  */
/*-------------------------------------------------------------------*/

    if (fsperr)
        for (i = 0; i < s; i++)
            if (spc[i].typ == SPCTAB_FREE)
                memset (&spc[i], 0, sizeof(SPCTAB));

/*-------------------------------------------------------------------*/
/* rebuild the gap/space tables                                      */
/*-------------------------------------------------------------------*/

    gaps = cdsk_build_gap (spc, &s, gap);

/*-------------------------------------------------------------------*/
/* recover any level 2 tables                                        */
/*-------------------------------------------------------------------*/

    for (i = 0; i < r; i++)
    {
        if (rcv[i].typ != SPCTAB_L2TAB) continue;
        if (rcv[i].ptr == NULL)
        {
            cdskmsg (m, "level 2 table %d and tracks %d to %d not recovered\n",
                  rcv[i].val, rcv[i].val * 256, rcv[i].val * 256 + 255);
        }
        else
        {   /* find a space for the recovered level 2 table */
            for (j = 0; j < gaps; j++)
                if (gap[j].siz == 256 * CCKD_L2TAB_SIZE ||
                    gap[j].siz >= 256 * CCKD_L2TAB_SIZE + CCKD_FREEBLK_SIZE)
                    break;
            if (j < gaps)
            {   /* found a place for the recovered level 2 table */
                pos = gap[j].pos;
                gap[j].pos += 256 * CCKD_L2TAB_SIZE;
                gap[j].len = gap[j].siz -= 256 * CCKD_L2TAB_SIZE;
            }
            else
            {   /* don't think this should ever happen */
                pos = hipos;
                rc = ftruncate (fd, pos + 256 * CCKD_L2TAB_SIZE);
                rc = fstat (fd, &fst);
                hipos = fst.st_size;
                spc[s-1].pos = hipos; /* should be SPCTAB_END */
            }
            /* write the recovered level 2 table to the file */
            l2p = (CCKD_L2TAB *) rcv[i].ptr;
            rc = lseek (fd, pos, SEEK_SET);
            rc = write (fd, l2p, 256 * CCKD_L2TAB_SIZE);
            cdskmsg (m, "level 2 table %d recovered\n", rcv[i].val);
            l1[rcv[i].val] = pos;
            spc[s].typ = SPCTAB_L2TAB;
            spc[s].pos = pos;
            spc[s].len = spc[s++].siz = 256 * CCKD_L2TAB_SIZE;
        }
    }

/*-------------------------------------------------------------------*/
/* if level 2 tables were recovered then rewrite the level 1 table   */
/*-------------------------------------------------------------------*/

    if (r > 0)
    {
        rc = lseek (fd, CCKD_L1TAB_POS, SEEK_SET);
        rc = write (fd, l1, cdevhdr.numl1tab * CCKD_L1TAB_SIZE);
    }

/*-------------------------------------------------------------------*/
/* if any errors at all occurred then we rebuild the free space      */
/*-------------------------------------------------------------------*/

    if (fsperr)
    {
/*-------------------------------------------------------------------*/
/* check for short gaps... short gaps will either become imbedded    */
/* free space in a track image or fall off the end of the file.      */
/* Note that we assume that the size of a track image will not       */
/* overflow.  Also note that short gaps are extremely painful;-)     */
/*-------------------------------------------------------------------*/
    short_gap:
        gaps = cdsk_build_gap (spc, &s, gap);
        for (i = j = 0; i < gaps; i++)
        {
            if (gap[i].siz > 0 && gap[i].siz < CCKD_FREEBLK_SIZE)
            {   /* find the space preceding the gap */
                for ( ; j < s; j++)
                    if (spc[j].pos + spc[j].siz == gap[i].pos) break;
                if (spc[j].typ == SPCTAB_TRKIMG)
                {   /* add the gap to imbedded free space */
                    x = spc[j].val / 256; y = spc[j].val % 256;
                    rc = lseek (fd, l1[x] + y * CCKD_L2TAB_SIZE, SEEK_SET);
                    rc = read (fd, &l2, CCKD_L2TAB_SIZE);
                    l2[0].size += gap[i].siz;
                    rc = lseek (fd, -CCKD_L2TAB_SIZE, SEEK_SET);
                    rc = write (fd, &l2, CCKD_L2TAB_SIZE);
                    spc[j].siz += gap[i].siz;
                }
                else switch (spc[j+1].typ)
                {
                    case SPCTAB_END:
                        /* remove the gap from the end of the file */
                        rc = ftruncate (fd, hipos - gap[i].siz);
                        rc = fstat (fd, &fst);
                        hipos = fst.st_size;
                        spc[j+1].pos = hipos;
                        memset (&gap[i], 0, sizeof(SPCTAB));
                        break;

                    case SPCTAB_L2TAB:
                        /* swap with the following level 2 table */
                        rc = lseek (fd, spc[j+1].pos, SEEK_SET);
                        rc = read (fd, &l2, 256 * CCKD_L2TAB_SIZE);
                        l1[spc[j+1].val] -= gap[i].siz;
                        spc[j+1].pos -= gap[i].siz;
                        rc = lseek (fd, spc[j+1].pos, SEEK_SET);
                        rc = write (fd, &l2, 256 * CCKD_L2TAB_SIZE);
                        rc = lseek (fd, CCKD_L1TAB_POS +
                                    spc[j+1].val * CCKD_L1TAB_SIZE,
                                    SEEK_SET);
                        rc = write (fd, &l1[spc[j+1].val], CCKD_L1TAB_SIZE);
                        goto short_gap; /* start over */

                    case SPCTAB_TRKIMG:
                        /* add the gap to imbedded free space */
                        x = spc[j+1].val / 256; y = spc[j+1].val % 256;
                        rc = lseek (fd, l1[x] + y * CCKD_L2TAB_SIZE, SEEK_SET);
                        rc = read (fd, &l2, CCKD_L2TAB_SIZE);
                        rc = lseek (fd, l2[0].pos, SEEK_SET);
                        rc = read (fd, buf, l2[0].len);
                        l2[0].pos  -= gap[i].siz;
                        l2[0].size += gap[i].siz;
                        rc = lseek (fd, l1[x] + y * CCKD_L2TAB_SIZE, SEEK_SET);
                        rc = write (fd, &l2, CCKD_L2TAB_SIZE);
                        rc = lseek (fd, l2[0].pos, SEEK_SET);
                        rc = write (fd, buf, l2[0].len);
                        spc[j+1].pos -= gap[i].siz;
                        spc[j+1].siz += gap[i].siz;
                        break;

                    default:
                        /* this is another fine mess we got ourselves into */
                        cdskmsg (m, "logic error processing short gap at "
                                "0x%lx len %d; following space is %s\n",
                                gap[i].pos, gap[i].siz,
                                space[spc[j+1].typ]);
                        goto cdsk_return;
                }
            }
        }

/*-------------------------------------------------------------------*/
/* Finally, we are ready to rebuild the free space                   */
/*-------------------------------------------------------------------*/

        gaps = cdsk_build_gap (spc, &s, gap);
        memset (&cdevhdr.CCKD_FREEHDR, 0, CCKD_FREEHDR_SIZE);
        cdevhdr.size = hipos;
        cdevhdr.free = gap[0].pos;
        cdevhdr.free_number = gaps;
        for (i = 0; i < gaps; i++)
        {
            if (i < gaps - 1) fb.pos = gap[i+1].pos;
            else fb.pos = 0;
            fb.len = gap[i].siz;
            rc = lseek (fd, gap[i].pos, SEEK_SET);
            rc = write (fd, &fb, CCKD_FREEBLK_SIZE);
            cdevhdr.free_total += gap[i].siz;
            if (gap[i].siz > cdevhdr.free_largest)
                cdevhdr.free_largest = gap[i].siz;
        }
        for (i = 0; i < s - 1; i++)
        {
            cdevhdr.free_total += spc[i].siz - spc[i].len;
            cdevhdr.free_imbed += spc[i].siz - spc[i].len;
            cdevhdr.used += spc[i].len;
        }
        rc = lseek (fd, CCKD_FREEHDR_POS, SEEK_SET);
        rc = write (fd, &cdevhdr.CCKD_FREEHDR, CCKD_FREEHDR_SIZE);
        cdskmsg (m, "free space rebuilt: 1st 0x%x nbr %d\n   "
                    "total %d imbed %d largest %d\n",
                    cdevhdr.free, cdevhdr.free_number, cdevhdr.free_total,
                    cdevhdr.free_imbed, cdevhdr.free_largest);
    }

    crc = fsperr;

/*-------------------------------------------------------------------*/
/* Free all space and return                                         */
/*-------------------------------------------------------------------*/

cdsk_return:

    /* free all malloc()ed storage */
    for (i = 0; i < r; i++) if (rcv[i].ptr) free (rcv[i].ptr);
    if (spc)    free (spc);
    if (rcv)    free (rcv);
    if (gap)    free (gap);
    if (gapbuf) free (gapbuf);
    if (buf)    free (buf);
    if (buf2)   free (buf2);
    if (l1)     free (l1);

    /* if file is ok or has been repaired, turn off the
       opened bit if it's on */
    if (crc >= 0 && (cdevhdr.options & CCKD_OPENED) != 0)
    {
        cdevhdr.options ^= CCKD_OPENED;
        rc = lseek (fd, CKDDASD_DEVHDR_SIZE + 3, SEEK_SET);
        rc = write (fd, &cdevhdr.options, 1);
    }

    return crc;

} /* end function cckd_chkdsk */

/*-------------------------------------------------------------------*/
/* Sort Space Table by pos.  Always return entry type NONE behind    */
/* any other entries                                                 */
/*-------------------------------------------------------------------*/

int cdsk_spctab_comp(const void *a, const void *b)
{
const SPCTAB   *x = a, *y = b;          /* Entries to be sorted      */

    if (x->typ == SPCTAB_NONE) return  1;
    if (y->typ == SPCTAB_NONE) return -1;
    if (x->pos < y->pos) return -1;
    else return 1;
} /* end function cdsk_spctab_comp */


/*-------------------------------------------------------------------*/
/* Sort Recovery Table by val.  Always return entry type NONE behind */
/* any other entries                                                 */
/*-------------------------------------------------------------------*/

int cdsk_rcvtab_comp(const void *a, const void *b)
{
const SPCTAB   *x = a, *y = b;          /* Entries to be sorted      */
unsigned int    v1, v2;                 /* Value for entry           */

    if (x->typ == SPCTAB_NONE) return  1;
    if (y->typ == SPCTAB_NONE) return -1;

    if (x->typ == SPCTAB_L2TAB) v1 = x->val * 256;
    else v1 = x->val;

    if (y->typ == SPCTAB_L2TAB) v2 = y->val * 256;
    else v2 = y->val;

    if (v1 < v2) return -1;
    if (v1 > v2) return  1;

    if (x->typ == SPCTAB_L2TAB) return -1;
    else return 1;
} /* end function cdsk_rcvtab_comp */

/*-------------------------------------------------------------------*/
/* Are we little or big endian?  From Harbison&Steele.               */
/*-------------------------------------------------------------------*/
int cdsk_chk_endian()
{
union
{
    long l;
    char c[sizeof (long)];
}   u;

    u.l = 1;
    return (u.c[sizeof (long) - 1] == 1);
}

/*-------------------------------------------------------------------*/
/* Validate a track image                                            */
/*-------------------------------------------------------------------*/
int cdsk_valid_trk (int trk, unsigned char *buf, int heads, int len, char *msg)
{
int             cyl;                    /* Cylinder                  */
int             head;                   /* Head                      */
char            cchh[4];                /* Cyl, head big-endian      */
int             r;                      /* Record number             */
int             sz;                     /* Track size                */
int             kl,dl;                  /* Key/Data lengths          */

    /* cylinder and head calculations */
    cyl = trk / heads;
    head = trk % heads;
    cchh[0] = cyl >> 8;
    cchh[1] = cyl & 0xFF;
    cchh[2] = head >> 8;
    cchh[3] = head & 0xFF;

    /* validate home address */
    if (buf[0] !=0 || memcmp (&buf[1], cchh, 4) != 0)
    {
        if (msg)
            sprintf (msg, "track %d HA validation error: "
                 "%2.2x%2.2x%2.2x%2.2x%2.2x",
                 trk, buf[0], buf[1], buf[2], buf[3], buf[4]);
        return -1;
    }

    /* validate record 0 */
    if (memcmp (&buf[5], cchh, 4) != 0 ||  buf[9] != 0 ||
        buf[10] != 0   || buf[11] != 0 || buf[12] != 8)
    {
        if (msg)
            sprintf (msg, "track %d R0 validation error: "
                 "%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x",
                 trk, buf[5], buf[6], buf[7], buf[8], buf[9],
                 buf[10], buf[11], buf[12]);
        return -1;
    }

    /* validate records 1 thru n */
    for (r = 1, sz = 21;
         memcmp (&buf[sz], eighthexFF, 8) != 0;
         sz += 8 + kl + dl, r++)
    {
        kl = buf[sz+5];
        dl = buf[sz+6] * 256 + buf[sz+7];
        if (memcmp (&buf[sz], cchh, 4) != 0 || buf[sz+4] != r ||
            sz + 8 + kl + dl >= len)
        {
             if (msg)
                 sprintf (msg, "track %d R%d validation error: "
                     "%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x",
                     trk, r, buf[sz], buf[sz+1], buf[sz+2], buf[sz+3],
                     buf[sz+4], buf[sz+5], buf[sz+6], buf[sz+7]);
             return -1;
        }
    }
    sz += 8;
    if (sz != len && msg)
        sprintf (msg, "track %d size mismatch, expected %d found %d",
                 trk, len, sz);
    return sz;
} /* end function cdsk_valid_trk */

/*-------------------------------------------------------------------*/
/* build gap table                                                   */
/*-------------------------------------------------------------------*/

int cdsk_build_gap (SPCTAB *spc, int *n, SPCTAB *gap)
{
int i, gaps, s;

    s = *n; /* size of space table */

    /* sort the space table by offset */
    qsort ((void *)spc, s, sizeof(SPCTAB), cdsk_spctab_comp);

    /* remove null entries from the end */
    for ( ; spc[s-1].typ == SPCTAB_NONE; s--);

    /* build the gap table */
    for (i = gaps = 0; i < s - 1; i++)
    {
       if (spc[i].pos + spc[i].siz < spc[i+1].pos)
        {
            gap[gaps].pos = spc[i].pos + spc[i].siz;
            gap[gaps++].siz = spc[i+1].pos - (spc[i].pos + spc[i].siz);
        }
    }
    *n = s;
    return gaps;
}

#else /* NO_CCKD */

#ifdef CCKD_CHKDSK_MAIN
int main ( int argc, char *argv[])
{
    fprintf (stderr, "cckdcdsk support not generated\n");
    return -1;
}
#endif

#endif
