/* DASDPDSU.C   (c) Copyright Roger Bowler, 1999                     */
/*              Hercules DASD Utilities: PDS unloader                */

/*-------------------------------------------------------------------*/
/* This program unloads members of a partitioned dataset from        */
/* a virtual DASD volume and copies each member to a flat file.      */
/*                                                                   */
/* The command format is:                                            */
/*      dasdpdsu ckdfile dsname                                      */
/* where ckdfile is the name of the CKD image file                   */
/* and dsname is the name of the PDS to be unloaded.                 */
/* Each member is copied to a file memname.mac                       */
/*-------------------------------------------------------------------*/

#include "hercules.h"
#include "dasdblks.h"

/*-------------------------------------------------------------------*/
/* Static data areas                                                 */
/*-------------------------------------------------------------------*/
BYTE           *fname;                  /* -> CKD image file name    */
int             fd;                     /* CKD image file descriptor */
int             trksz;                  /* CKD image track size      */
BYTE           *trkbuf = NULL;          /* -> Track buffer           */
int             curcyl = -1;            /* Cylinder number of track
                                           currently in track buffer */
int             curhead = -1;           /* Head number of track
                                           currently in track buffer */
int             heads;                  /* Tracks per cylinder       */

BYTE eighthexFF[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/*-------------------------------------------------------------------*/
/* Subroutine to read a track from the CKD DASD image                */
/* Input:                                                            */
/*      cyl     Cylinder number                                      */
/*      head    Head number                                          */
/* Output:                                                           */
/*      The track is read into trkbuf, and curcyl and curhead        */
/*      are set to the cylinder and head number.                     */
/*                                                                   */
/* Return value is 0 if successful, -1 if error                      */
/*-------------------------------------------------------------------*/
static int
read_track (int cyl, int head)
{
int             rc;                     /* Return code               */
int             len;                    /* Record length             */
off_t           seekpos;                /* Seek position for lseek   */
CKDDASD_TRKHDR *trkhdr;                 /* -> Track header           */

    fprintf (stderr,
            "Reading cyl %d head %d\n",
            cyl, head);

    /* Seek to start of track header */
    seekpos = CKDDASD_DEVHDR_SIZE
            + (((cyl * heads) + head) * trksz);

    rc = lseek (fd, seekpos, SEEK_SET);
    if (rc < 0)
    {
        fprintf (stderr,
                "%s lseek error: %s\n",
                fname, strerror(errno));
        return -1;
    }

    /* Read the track */
    len = read (fd, trkbuf, trksz);
    if (len < 0)
    {
        fprintf (stderr,
                "%s read error: %s\n",
                fname, strerror(errno));

        return -1;
    }

    /* Validate the track header */
    trkhdr = (CKDDASD_TRKHDR*)trkbuf;
    if (trkhdr->bin != 0
        || trkhdr->cyl[0] != (cyl >> 8)
        || trkhdr->cyl[1] != (cyl & 0xFF)
        || trkhdr->head[0] != (head >> 8)
        || trkhdr->head[1] != (head & 0xFF))
    {
        fprintf (stderr, "Invalid track header\n");
        return -1;
    }

    /* Set current cylinder and head */
    curcyl = cyl;
    curhead = head;

    return 0;
} /* end function read_track */

/*-------------------------------------------------------------------*/
/* Subroutine to read a block from the CKD DASD image                */
/* Input:                                                            */
/*      cyl     Cylinder number of requested block                   */
/*      head    Head number of requested block                       */
/*      rec     Record number of requested block                     */
/* Output:                                                           */
/*      keyptr  Pointer to record key                                */
/*      keylen  Actual key length                                    */
/*      dataptr Pointer to record data                               */
/*      datalen Actual data length                                   */
/*                                                                   */
/* Return value is 0 if successful, +1 if end of track, -1 if error  */
/*-------------------------------------------------------------------*/
static int
read_block (int cyl, int head, int rec, BYTE **keyptr, int *keylen,
            BYTE **dataptr, int *datalen)
{
int             rc;                     /* Return code               */
BYTE           *ptr;                    /* -> byte in track buffer   */
CKDDASD_RECHDR *rechdr;                 /* -> Record header          */
int             kl;                     /* Key length                */
int             dl;                     /* Data length               */

    /* Read the required track into the track buffer if necessary */
    if (curcyl != cyl || curhead != head)
    {
        rc = read_track (cyl, head);
        if (rc < 0) return -1;
    }

    /* Search for the requested record in the track buffer */
    ptr = trkbuf;
    ptr += CKDDASD_TRKHDR_SIZE;

    while (1)
    {
        /* Exit with record not found if end of track */
        if (memcmp(ptr, eighthexFF, 8) == 0)
            return +1;

        /* Extract key length and data length from count field */
        rechdr = (CKDDASD_RECHDR*)ptr;
        kl = rechdr->klen;
        dl = (rechdr->dlen[0] << 8) | rechdr->dlen[1];

        /* Exit if requested record number found */
        if (rechdr->rec == rec)
            break;

        /* Point past count key and data to next block */
        ptr += CKDDASD_RECHDR_SIZE + kl + dl;
    }

    /* Return key and data pointers and lengths */
    if (keyptr != NULL) *keyptr = ptr + CKDDASD_RECHDR_SIZE;
    if (keylen != NULL) *keylen = kl;
    if (dataptr != NULL) *dataptr = ptr + CKDDASD_RECHDR_SIZE + kl;
    if (datalen != NULL) *datalen = dl;
    return 0;

} /* end function read_block */

/*-------------------------------------------------------------------*/
/* Subroutine to search a dataset for a specified key                */
/* Input:                                                            */
/*      key     Key value                                            */
/*      keylen  Key length                                           */
/*      noext   Number of extents                                    */
/*      extent  Dataset extent array                                 */
/* Output:                                                           */
/*      cyl     Cylinder number of requested block                   */
/*      head    Head number of requested block                       */
/*      rec     Record number of requested block                     */
/*                                                                   */
/* Return value is 0 if successful, +1 if key not found, -1 if error */
/*-------------------------------------------------------------------*/
static int
search_key_equal (BYTE *key, int keylen, int noext, DSXTENT extent[],
                int *cyl, int *head, int *rec)
{
int             rc;                     /* Return code               */
int             ccyl;                   /* Cylinder number           */
int             chead;                  /* Head number               */
int             cext;                   /* Extent sequence number    */
int             ecyl;                   /* Extent end cylinder       */
int             ehead;                  /* Extent end head           */
BYTE           *ptr;                    /* -> byte in track buffer   */
CKDDASD_RECHDR *rechdr;                 /* -> Record header          */
int             kl;                     /* Key length                */
int             dl;                     /* Data length               */

    /* Start at first track of first extent */
    cext = 0;
    ccyl = (extent[cext].xtbcyl[0] << 8) | extent[cext].xtbcyl[1];
    chead = (extent[cext].xtbtrk[0] << 8) | extent[cext].xtbtrk[1];
    ecyl = (extent[cext].xtecyl[0] << 8) | extent[cext].xtecyl[1];
    ehead = (extent[cext].xtetrk[0] << 8) | extent[cext].xtetrk[1];

    fprintf (stderr,
            "Searching extent %d begin (%d,%d) end (%d,%d)\n",
            cext, ccyl, chead, ecyl, ehead);

    while (1)
    {
        /* Read the required track into the track buffer */
        rc = read_track (ccyl, chead);
        if (rc < 0) return -1;

        /* Search for the requested record in the track buffer */
        ptr = trkbuf;
        ptr += CKDDASD_TRKHDR_SIZE;

        while (1)
        {
            /* Exit loop at end of track */
            if (memcmp(ptr, eighthexFF, 8) == 0)
                break;

            /* Extract key length and data length from count field */
            rechdr = (CKDDASD_RECHDR*)ptr;
            kl = rechdr->klen;
            dl = (rechdr->dlen[0] << 8) | rechdr->dlen[1];

            /* Return if requested record key found */
            if (kl == keylen
                && memcmp(ptr + CKDDASD_RECHDR_SIZE, key, 44) == 0)
            {
                *cyl = ccyl;
                *head = chead;
                *rec = rechdr->rec;
                return 0;
            }

            /* Point past count key and data to next block */
            ptr += CKDDASD_RECHDR_SIZE + kl + dl;

        } /* end while */

        /* Point to the next track */
        chead++;
        if (chead >= heads)
        {
            ccyl++;
            chead = 0;
        }

        /* Loop if next track is within current extent */
        if (ccyl < ecyl || (ccyl == ecyl && chead <= ehead))
            continue;

        /* Move to next extent */
        cext++;
        if (cext >= noext) break;
        ccyl = (extent[cext].xtbcyl[0] << 8) | extent[cext].xtbcyl[1];
        chead = (extent[cext].xtbtrk[0] << 8) | extent[cext].xtbtrk[1];
        ecyl = (extent[cext].xtecyl[0] << 8) | extent[cext].xtecyl[1];
        ehead = (extent[cext].xtetrk[0] << 8) | extent[cext].xtetrk[1];

        fprintf (stderr,
                "Searching extent %d begin (%d,%d) end (%d,%d)\n",
                cext, ccyl, chead, ecyl, ehead);

    } /* end while */

    /* Return record not found at end of extents */
    return +1;

} /* end function search_key_equal */

/*-------------------------------------------------------------------*/
/* Subroutine to convert relative track to cylinder and head         */
/* Input:                                                            */
/*      tt      Relative track number                                */
/*      noext   Number of extents in dataset                         */
/*      extent  Dataset extent array                                 */
/* Output:                                                           */
/*      cyl     Cylinder number                                      */
/*      head    Head number                                          */
/*                                                                   */
/* Return value is 0 if successful, or -1 if error                   */
/*-------------------------------------------------------------------*/
static int
convert_tt (int tt, int noext, DSXTENT extent[], int *cyl, int *head)
{
int             i;                      /* Extent sequence number    */
int             trk;                    /* Relative track number     */
int             bcyl;                   /* Extent begin cylinder     */
int             btrk;                   /* Extent begin head         */
int             ecyl;                   /* Extent end cylinder       */
int             etrk;                   /* Extent end head           */
int             start;                  /* Extent begin track        */
int             end;                    /* Extent end track          */
int             extsize;                /* Extent size in tracks     */

    for (i = 0, trk = tt; i < noext; i++)
    {
        bcyl = (extent[i].xtbcyl[0] << 8) | extent[i].xtbcyl[1];
        btrk = (extent[i].xtbtrk[0] << 8) | extent[i].xtbtrk[1];
        ecyl = (extent[i].xtecyl[0] << 8) | extent[i].xtecyl[1];
        etrk = (extent[i].xtetrk[0] << 8) | extent[i].xtetrk[1];

        start = (bcyl * heads) + btrk;
        end = (ecyl * heads) + etrk;
        extsize = end - start + 1;

        if (trk <= extsize)
        {
            trk += start;
            *cyl = trk / heads;
            *head = trk % heads;
            return 0;
        }

        trk -= extsize;

    } /* end for(i) */

    fprintf (stderr,
            "Track %d not found in extent table\n",
            tt);
    return -1;

} /* end function convert_tt */

/*-------------------------------------------------------------------*/
/* Subroutine to process a member                                    */
/* Input:                                                            */
/*      noext   Number of extents in dataset                         */
/*      extent  Dataset extent array                                 */
/*      memname Member name (ASCIIZ)                                 */
/*      ttr     Member TTR                                           */
/*                                                                   */
/* Return value is 0 if successful, or -1 if error                   */
/*-------------------------------------------------------------------*/
static int
process_member (int noext, DSXTENT extent[], BYTE *memname, BYTE *ttr)
{
int             rc;                     /* Return code               */
int             len;                    /* Record length             */
int             trk;                    /* Relative track number     */
int             cyl;                    /* Cylinder number           */
int             head;                   /* Head number               */
int             rec;                    /* Record number             */
BYTE           *buf;                    /* -> Data block             */
FILE           *ofp;                    /* Output file pointer       */
BYTE            ofname[256];            /* Output file name          */
int             offset;                 /* Offset of record in buffer*/
BYTE            card[81];               /* Logical record (ASCIIZ)   */

    /* Build the output file name */
    memset (ofname, 0, sizeof(ofname));
    strncpy (ofname, memname, 8);
    string_to_lower (ofname);
    strcat (ofname, ".mac");

    /* Open the output file */
    ofp = fopen (ofname, "w");
    if (ofp == NULL)
    {
        fprintf (stderr,
                "Cannot open %s: %s\n",
                ofname, strerror(errno));
        return -1;
    }

    /* Point to the start of the member */
    trk = (ttr[0] << 8) | ttr[1];
    rec = ttr[2];

    fprintf (stderr,
            "Member %s TTR=%4.4X%2.2X\n",
            memname, trk, rec);

    /* Read the member */
    while (1)
    {
        /* Convert relative track to cylinder and head */
        rc = convert_tt (trk, noext, extent, &cyl, &head);
        if (rc < 0) return -1;

        fprintf (stderr,
                "CCHHR=%4.4X%4.4X%2.2X\n",
                cyl, head, rec);

        /* Read a data block */
        rc = read_block (cyl, head, rec, NULL, NULL, &buf, &len);
        if (rc < 0) return -1;

        /* Move to next track if record not found */
        if (rc > 0)
        {
            trk++;
            rec = 1;
            continue;
        }

        /* Exit at end of member */
        if (len == 0) break;

        /* Check length of data block */
        if (len % 80 != 0)
        {
            fprintf (stderr,
                    "Bad block length %d at cyl %d head %d rec %d\n",
                    len, cyl, head, rec);
            return -1;
        }

        /* Process each record in the data block */
        for (offset = 0; offset < len; offset += 80)
        {
            make_asciiz (card, sizeof(card), buf + offset, 72);
            fprintf (ofp, "%s\n", card);
            if (ferror(ofp))
            {
                fprintf (stderr,
                        "Error writing %s: %s\n",
                        ofname, strerror(errno));
                return -1;
            }
        } /* end for(offset) */

        /* Point to the next data block */
        rec++;

    } /* end while */

    /* Close the output file and exit */
    fclose (ofp);
    return 0;

} /* end function process_member */

/*-------------------------------------------------------------------*/
/* Subroutine to process a directory block                           */
/* Input:                                                            */
/*      noext   Number of extents in dataset                         */
/*      extent  Dataset extent array                                 */
/*      dirblk  Pointer to directory block                           */
/*                                                                   */
/* Return value is 0 if OK, +1 if end of directory, or -1 if error   */
/*-------------------------------------------------------------------*/
static int
process_dirblk (int noext, DSXTENT extent[], BYTE *dirblk)
{
int             rc;                     /* Return code               */
int             size;                   /* Size of directory entry   */
int             k;                      /* Userdata halfword count   */
BYTE           *dirptr;                 /* -> Next byte within block */
int             dirrem;                 /* Number of bytes remaining */
PDSDIR         *dirent;                 /* -> Directory entry        */
BYTE            memname[9];             /* Member name (ASCIIZ)      */

    /* Load number of bytes in directory block */
    dirptr = dirblk;
    dirrem = (dirptr[0] << 8) | dirptr[1];
    if (dirrem < 2 || dirrem > 256)
    {
        fprintf (stderr, "Directory block byte count is invalid\n");
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
            return +1;

        /* Extract the member name */
        make_asciiz (memname, sizeof(memname), dirent->pds2name, 8);

        /* Process the member */
        rc = process_member (noext, extent, memname, dirent->pds2ttrp);
        if (rc < 0) return -1;

        /* Load the user data halfword count */
        k = dirent->pds2indc & PDS2INDC_LUSR;

        /* Point to next directory entry */
        size = 12 + k*2;
        dirptr += size;
        dirrem -= size;
    }

    return 0;
} /* end function process_dirblk */

/*-------------------------------------------------------------------*/
/* DASDPDSU main entry point                                         */
/*-------------------------------------------------------------------*/
int main (int argc, char *argv[])
{
int             rc;                     /* Return code               */
int             len;                    /* Record length             */
int             cyl;                    /* Cylinder number           */
int             head;                   /* Head number               */
int             rec;                    /* Record number             */
int             trk;                    /* Relative track number     */
BYTE           *vol1data;               /* -> Volume label           */
FORMAT1_DSCB   *f1dscb;                 /* -> Format 1 DSCB          */
FORMAT3_DSCB   *f3dscb;                 /* -> Format 3 DSCB          */
FORMAT4_DSCB   *f4dscb;                 /* -> Format 4 DSCB          */
BYTE            dsname[44];             /* Dataset name (EBCDIC)     */
BYTE            dsnama[45];             /* Dataset name (ASCIIZ)     */
BYTE            volser[7];              /* Volume serial (ASCIIZ)    */
int             noext;                  /* Number of extents         */
DSXTENT         extent[16];             /* Extent descriptor array   */
BYTE           *blkptr;                 /* -> PDS directory block    */
BYTE            dirblk[256];            /* Copy of directory block   */
CKDDASD_DEVHDR  devhdr;                 /* CKD device header         */

    /* Display the program identification message */
    fprintf (stderr,
            "Hercules PDS unload program %s "
            "(c)Copyright Roger Bowler, 1999\n",
            MSTRING(VERSION));

    /* Check the number of arguments */
    if (argc != 3)
    {
        fprintf (stderr,
                "Usage: %s ckdfile pdsname\n",
                argv[0]);
        return -1;
    }

    /* The first argument is the name of the CKD image file */
    fname = argv[1];

    /* The second argument is the dataset name */
    strncpy (dsnama, argv[2], sizeof(dsnama)-1);
    string_to_upper (dsnama);
    convert_to_ebcdic (dsname, sizeof(dsname), dsnama);

    /* Open the CKD image file */
    fd = open (fname, O_RDONLY);
    if (fd < 0)
    {
        fprintf (stderr,
                "Cannot open %s: %s\n",
                fname, strerror(errno));
        return -1;
    }

    /* Read the device header */
    len = read (fd, &devhdr, CKDDASD_DEVHDR_SIZE);
    if (len < 0)
    {
        fprintf (stderr,
                "%s read error: %s\n",
                fname, strerror(errno));
        return -1;
    }

    /* Check the device header identifier */
    if (len < CKDDASD_DEVHDR_SIZE
        || memcmp(devhdr.devid, "CKD_P370", 8) != 0)
    {
        fprintf (stderr,
                "%s CKD header invalid\n",
                fname);
        return -1;
    }

    /* Extract the number of heads and the track size */
    heads = devhdr.heads;
    trksz = devhdr.trksize;
    fprintf (stderr,
            "%s heads=%d trklen=%d\n",
            fname, heads, trksz);

    /* Obtain the track buffer */
    trkbuf = malloc (trksz);
    if (trkbuf == NULL)
    {
        fprintf (stderr,
                "Cannot obtain track buffer: %s\n",
                strerror(errno));
        return -1;
    }

    /* Read the volume label */
    rc = read_block (0, 0, 3, NULL, NULL, &vol1data, &len);
    if (rc < 0) return -1;
    if (rc > 0)
    {
        fprintf (stderr, "VOL1 record not found\n");
        return -1;
    }

    make_asciiz (volser, sizeof(volser), vol1data+4, 6);
    cyl = (vol1data[11] << 8) | vol1data[12];
    head = (vol1data[13] << 8) | vol1data[14];
    rec = vol1data[15];

    fprintf (stderr,
            "VOLSER=%s VTOC=%4.4X%4.4X%2.2X\n",
             volser, cyl, head, rec);

    /* Read the format 4 DSCB */
    rc = read_block (cyl, head, rec, (BYTE**)&f4dscb, &len, NULL, NULL);
    if (rc < 0) return -1;
    if (rc > 0)
    {
        fprintf (stderr, "F4DSCB record not found\n");
        return -1;
    }

    fprintf (stderr,
            "VTOC start %2.2X%2.2X%2.2X%2.2X "
            "end %2.2X%2.2X%2.2X%2.2X\n",
            f4dscb->ds4vtoce.xtbcyl[0], f4dscb->ds4vtoce.xtbcyl[1],
            f4dscb->ds4vtoce.xtbtrk[0], f4dscb->ds4vtoce.xtbtrk[1],
            f4dscb->ds4vtoce.xtecyl[0], f4dscb->ds4vtoce.xtecyl[1],
            f4dscb->ds4vtoce.xtetrk[0], f4dscb->ds4vtoce.xtetrk[1]);

    /* Search for the requested dataset in the VTOC */
    rc = search_key_equal (dsname, sizeof(dsname),
                            1, &(f4dscb->ds4vtoce),
                            &cyl, &head, &rec);
    if (rc < 0) return -1;
    if (rc > 0)
    {
        fprintf (stderr,
                "Dataset %s not found in VTOC\n",
                dsnama);
        return -1;
    }

    fprintf (stderr,
            "DSNAME=%s F1DSCB CCHHR=%4.4X%4.4X%2.2X\n",
            dsnama, cyl, head, rec);

    /* Read the format 1 DSCB */
    rc = read_block (cyl, head, rec, (BYTE**)&f1dscb, &len, NULL, NULL);
    if (rc < 0) return -1;
    if (rc > 0)
    {
        fprintf (stderr, "F1DSCB record not found\n");
        return -1;
    }

    /* Extract number of extents and first 3 extent descriptors */
    noext = f1dscb->ds1noepv;
    extent[0] = f1dscb->ds1ext1;
    extent[1] = f1dscb->ds1ext2;
    extent[2] = f1dscb->ds1ext3;

    /* Obtain additional extent descriptors */
    if (noext > 3)
    {
        /* Read the format 3 DSCB */
        cyl = (f1dscb->ds1ptrds[0] << 8) | f1dscb->ds1ptrds[1];
        head = (f1dscb->ds1ptrds[2] << 8) | f1dscb->ds1ptrds[3];
        rec = f1dscb->ds1ptrds[4];
        rc = read_block (cyl, head, rec,
                        (BYTE**)&f3dscb, &len, NULL, NULL);
        if (rc < 0) return -1;
        if (rc > 0)
        {
            fprintf (stderr, "F3DSCB record not found\n");
            return -1;
        }

        /* Extract the next 13 extent descriptors */
        extent[3] = f3dscb->ds3extnt[0];
        extent[4] = f3dscb->ds3extnt[1];
        extent[5] = f3dscb->ds3extnt[2];
        extent[6] = f3dscb->ds3extnt[3];
        extent[7] = f3dscb->ds3adext[0];
        extent[8] = f3dscb->ds3adext[1];
        extent[9] = f3dscb->ds3adext[2];
        extent[10] = f3dscb->ds3adext[3];
        extent[11] = f3dscb->ds3adext[4];
        extent[12] = f3dscb->ds3adext[5];
        extent[13] = f3dscb->ds3adext[6];
        extent[14] = f3dscb->ds3adext[7];
        extent[15] = f3dscb->ds3adext[8];
    }

    /* Point to the start of the directory */
    trk = 0;
    rec = 1;

    /* Read the directory */
    while (1)
    {
        /* Convert relative track to cylinder and head */
        rc = convert_tt (trk, noext, extent, &cyl, &head);
        if (rc < 0) return -1;

        /* Read a directory block */
        fprintf (stderr,
                "Reading directory block at cyl %d head %d rec %d\n",
                cyl, head, rec);

        rc = read_block (cyl, head, rec, NULL, NULL, &blkptr, &len);
        if (rc < 0) return -1;

        /* Move to next track if block not found */
        if (rc > 0)
        {
            trk++;
            rec = 1;
            continue;
        }

        /* Exit at end of directory */
        if (len == 0) break;

        /* Copy the directory block */
        memcpy (dirblk, blkptr, sizeof(dirblk));

        /* Process each member in the directory block */
        rc = process_dirblk (noext, extent, dirblk);
        if (rc < 0) return -1;
        if (rc > 0) break;

        /* Point to the next directory block */
        rec++;

    } /* end while */

    fprintf (stderr,
            "End of directory\n");

    /* Close the CKD image file and exit */
    close (fd);
    free (trkbuf);
    return 0;

} /* end function main */
