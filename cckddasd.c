/* CCKDDASD.C   (c) Copyright Roger Bowler, 1999                     */
/*       ESA/390 Compressed CKD Direct Access Storage Device Handler */

/*-------------------------------------------------------------------*/
/* This module contains device functions for compressed emulated     */
/* count-key-data direct access storage devices.                     */
/*-------------------------------------------------------------------*/

//#define CCKD_ITRACEMAX 100000

#ifdef NOTHREAD
#ifndef CCKD_NOTHREAD
#define CCKD_NOTHREAD
#endif
#endif

#include "hercules.h"

#ifndef NO_CCKD

#include "zlib.h"
#ifdef CCKD_BZIP2
#include "bzlib.h"
#endif

#if 0
#undef DEVTRACE
#define DEVTRACE(format, a...) \
 fprintf(sysblk.msgpipew, "%4.4X:" format, dev->devnum, a); \
 fflush (sysblk.msgpipew)
#endif

#ifdef CCKD_ITRACEMAX
#undef DEVTRACE
#define DEVTRACE(format, a...) \
   if (cckd->itracex >= 128 * CCKD_ITRACEMAX) cckd->itracex = 0; \
   sprintf(&cckd->itrace[cckd->itracex], "%4.4X:" format, dev->devnum, a); \
   cckd->itracex += 128
#endif

int     cckddasd_init_handler (DEVBLK *, int, BYTE **);
int     cckd_chkdsk (int ,FILE *, int);
off_t   cckd_lseek (DEVBLK *, int, off_t, int);
ssize_t cckd_read (DEVBLK *, int, char *, size_t);
ssize_t cckd_write (DEVBLK *, int, const void *, size_t);
CCKD_DFWQE *cckd_scan_dfwq(DEVBLK *, unsigned int);
void    cckd_readahead (DEVBLK *);
unsigned char *cckd_read_trk (DEVBLK *, unsigned int, int);
void    cckd_null_rcd (DEVBLK *, unsigned char *, unsigned int);
void    cckd_write_trk (DEVBLK *, unsigned char *, BYTE);
int     cckd_trklen (DEVBLK *, unsigned char *);
void    cckd_dfw (DEVBLK *);
off_t   cckd_get_space (DEVBLK *, unsigned int, unsigned int,
                        unsigned int *, unsigned short *);
void    cckd_rel_space (DEVBLK *, unsigned int, int, int);
void    cckd_gcol (DEVBLK *);
int     cckd_gc_trim_comp (const void *, const void *);
int     cckd_gc_trim (DEVBLK *, int);
void    cckd_gc_percolate (DEVBLK *, int, int);
void    cckd_gc_combine (DEVBLK *, int, int);
int     cckd_gc_len (DEVBLK *, unsigned char *,
                     off_t, unsigned int, unsigned int);
int     cckd_gc_l1x (DEVBLK *, off_t);
void    cckd_swapend (DEVBLK *);
void    cckd_swapend4 (char *);
void    cckd_swapend2 (char *);
int     cckd_endian ();
void    cckd_print_itrace (DEVBLK *);

extern  char eighthexFF[];
char    cckd_empty_l2tab[256*CCKD_L2TAB_SIZE]; /* Empty Level 2 table*/

/*-------------------------------------------------------------------*/
/* Initialize the compressed device handler                          */
/*-------------------------------------------------------------------*/
int cckddasd_init_handler ( DEVBLK *dev, int argc, BYTE *argv[] )
{
int             rc;                     /* Return code               */
int             i;                      /* Index                     */
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */
int             fend, mend;             /* File, machine endian fmt  */
char           *kw, *op;                /* Parameter keyword/option  */

    /* Obtain area for cckd extension */
    dev->cckd_ext = cckd = malloc(sizeof(CCKDDASD_EXT));
    memset(cckd, 0, sizeof(CCKDDASD_EXT));
    memset(&cckd_empty_l2tab, 0, 256*CCKD_L2TAB_SIZE);

    /* Set threading indicator */
#ifndef CCKD_NOTHREAD
    cckd->threading = 1;
#endif

    /* Read the compressed device header */
    lseek(dev->fd, CKDDASD_DEVHDR_SIZE, SEEK_SET);
    read (dev->fd, &cckd->cdevhdr, CCKDDASD_DEVHDR_SIZE);

    /* Read the level 1 table */
    cckd->l1tab = malloc (cckd->cdevhdr.numl1tab*CCKD_L1TAB_SIZE);
    read(dev->fd, cckd->l1tab, cckd->cdevhdr.numl1tab*CCKD_L1TAB_SIZE);

    /* see if we need to change the endian format */
    fend = cckd->cdevhdr.options & CCKD_BIGENDIAN;
    mend = cckd_endian();
    if ((fend !=0 && mend == 0) || (fend == 0 && mend != 0))
    {
        logmsg("cckddasd: converting format from %s to %s\n",
               fend != 0 ? "big-endian" : "little-endian",
               mend != 0 ? "big-endian" : "little-endian");
        cckd_swapend (dev);
        lseek(dev->fd, CKDDASD_DEVHDR_SIZE, SEEK_SET);
        read (dev->fd, &cckd->cdevhdr, CCKDDASD_DEVHDR_SIZE);
    }

    /* call the chkdsk function */
    if ((cckd->cdevhdr.options & CCKD_OPENED) == 0)
        rc = cckd_chkdsk (dev->fd, sysblk.msgpipew, 0);
    else
    {
        logmsg("cckddasd: forcing chkdsk -%d, file not closed\n", 1);
        rc = cckd_chkdsk (dev->fd, sysblk.msgpipew, 1);
    }
    if (rc < 0) return -1;

    /* re-read the compressed device header if file was repaired */
    if (rc > 0)
    {
        lseek(dev->fd, CKDDASD_DEVHDR_SIZE, SEEK_SET);
        read (dev->fd, &cckd->cdevhdr, CCKDDASD_DEVHDR_SIZE);
    }

    /* print compressed device statistics */
    logmsg ("cckddasd: sz %u used %u free %u[%u] fpos %u nbr %u\n",
            cckd->cdevhdr.size, cckd->cdevhdr.used,
            cckd->cdevhdr.free_total, cckd->cdevhdr.free_imbed,
            cckd->cdevhdr.free, cckd->cdevhdr.free_number);

    /* Set current ckddasd position */
    cckd->curpos = 512;

    /* Initialize locks, conditions and attributes */
    initialize_lock (&cckd->filelock);
    initialize_lock (&cckd->dfwlock);
    initialize_lock (&cckd->gclock);
    initialize_lock (&cckd->ralock);
    initialize_condition (&cckd->dfwcond);
    initialize_condition (&cckd->gccond);
    initialize_condition (&cckd->racond);
    initialize_condition (&cckd->rtcond);
    initialize_detach_attr (&cckd->gcattr);
    initialize_detach_attr (&cckd->dfwattr);
    initialize_detach_attr (&cckd->raattr);

    /* process the parameters */
    for (i = 1; i < argc; i++)
    {
        if (strcasecmp ("lazywrite", argv[i]) == 0)
            continue;
        if (strcasecmp ("nolazywrite", argv[i]) == 0)
            continue;
        if (strlen (argv[i]) > 6 &&
            memcmp ("cache", argv[i], 5) == 0)
        {
            kw = strtok (argv[i], "=");
            op = strtok (NULL, " \t");
            if (op) cckd->cachenbr = atoi (op);
            continue;
        }
    }

    /* Initialize the read-ahead thread */
    if (cckd->threading && cckd->cachenbr != 1)
        create_thread (&cckd->ratid, &cckd->raattr, cckd_readahead, dev);

#ifdef CCKD_ITRACEMAX
    cckd->itrace = calloc (CCKD_ITRACEMAX, 128);
#endif

    return 0;
} /* end function cckddasd_init_handler */


/*-------------------------------------------------------------------*/
/* Close a Compressed CKD Device                                     */
/*-------------------------------------------------------------------*/
int cckddasd_close_device (DEVBLK *dev)
{
int             rc;                     /* Return code               */
int             i;                      /* Index                     */
void           *ret;                    /* Return status from thread */
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */

    cckd = dev->cckd_ext;

    /* write the last track image */
    if (cckd->write && cckd->buf)
        cckd_write_trk (dev, cckd->buf, cckd->compress);

    /* terminate threads */
    if (cckd->threading)
    {
        cckd->threading = 0;

        /* Terminate the deferred-write thread by signalling it with
           the threading bit off.  By obtaining the file lock we are
           sure that the thread is not doing anything useful */
        if (cckd->writeinit)
        {
            obtain_lock (&cckd->filelock);
            signal_condition (&cckd->dfwcond);
            release_lock (&cckd->filelock);
            pthread_join (cckd->dfwtid, &ret);
        }

        /* Terminate the garbage-collection thread by signalling it
           with the threading bit off.   By obtaining the file lock
           we are sure that the thread is not doing anything useful */
        if (cckd->gcinit)
        {
            obtain_lock (&cckd->filelock);
            signal_condition (&cckd->gccond);
            release_lock (&cckd->filelock);
            pthread_join (cckd->gctid, &ret);
        }

        /* Terminate read-ahead thread with the threading bit off */
        if (cckd->rainit)
        {
            cckd->nexttrk = 0;
            obtain_lock (&cckd->filelock);
            signal_condition (&cckd->racond);
            release_lock (&cckd->filelock);
            rc = pthread_join (cckd->ratid, &ret);
        }
    }

    /* free the cache */
    if (cckd->cache != NULL)
    {
        for (i = 0; i < cckd->cachenbr; i++)
            if (cckd->cache[i].buf)
                free (cckd->cache[i].buf);
        free (cckd->cache);
    }

    /* turn off the opened bit */
    if ((cckd->cdevhdr.options & CCKD_OPENED) != 0)
    {
        cckd->cdevhdr.options &= 0xff - CCKD_OPENED;

        /* write the compressed device header one more time */
        rc = lseek (dev->fd, CKDDASD_DEVHDR_SIZE, SEEK_SET);
        rc = write (dev->fd, &cckd->cdevhdr, CCKDDASD_DEVHDR_SIZE);
    }

    /* write some statistics */
    logmsg ("%4.4X reads %u  writes %u  hits %u  read aheads %u  missses %u\n",
            dev->devnum, cckd->reads, cckd->writes, cckd->cachehits, cckd->readaheads, cckd->ramisses);

    /* free the cckd extension */
    dev->cckd_ext= NULL;
    free (cckd);

    return 0;
}

/*-------------------------------------------------------------------*/
/* Compressed ckddasd lseek                                          */
/*-------------------------------------------------------------------*/
off_t cckd_lseek(DEVBLK *dev, int fd, off_t offset, int pos)
{
int             i;                      /* Index                     */
off_t           newpos=0;               /* New position              */
off_t           oldpos;                 /* Old position              */
unsigned int    newtrk;                 /* New track                 */
unsigned char  *wrbuf;                  /* Buffer to be written      */
BYTE            wrcompress;             /* Write compression         */
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */

    cckd = dev->cckd_ext;

    /* calculate new file position */
    switch (pos) {

        case SEEK_SET:
            newpos = 0;
            break;

        case SEEK_CUR:
            newpos = cckd->curpos;
            break;

        case SEEK_END:
            newpos = dev->ckdtrks*dev->ckdtrksz + CKDDASD_DEVHDR_SIZE;
            break;
    }
    newpos = newpos + offset;

    /* calculate new track number from the new file position */
    newtrk = ((unsigned int)newpos - CKDDASD_DEVHDR_SIZE)
                            / dev->ckdtrksz;

    /* check for new track */
    if (newtrk != cckd->curtrk || cckd->buf == NULL)
    {
        DEVTRACE ("cckddasd: lseek new trk %d\n", newtrk);
        wrbuf = NULL; wrcompress = cckd->compress;

        /* check if old track needs to be written.
           we are paranoid here because the garbage collector
           might also decide to write the track at this same time */
        if (cckd->write)
        {
            DEVTRACE ("cckddasd: lseek old trk %d needs to be written\n", cckd->curtrk);
            obtain_lock (&cckd->cachelock);
            if (cckd->write)
            {
                cckd->write = cckd->writetime = 0;
                wrbuf = cckd->buf;

                /* set `write pending' bit in the cache array, unless
                   we only have one cache entry; in this case we set
                   the buf to null so read_trk can find it */
                for (i = 0; i < cckd->cachenbr; i++)
                    if (cckd->curtrk == cckd->cache[i].trk)
                    {
                        if (cckd->cachenbr > 1)
                            cckd->cache[i].wrtpending = 1;
                        else cckd->cache[i].buf = NULL;
                        break;
                    }
            }
            release_lock (&cckd->cachelock);
        }

        /* read the new track */
        cckd->buf = cckd_read_trk (dev, newtrk, 1);
        cckd->lasttrk = cckd->curtrk;
        cckd->curtrk = newtrk;
        cckd->trkpos = newtrk * dev->ckdtrksz + CKDDASD_DEVHDR_SIZE;
        cckd->compress = cckd->buf[0];
        cckd->buf[0] = 0;

        /* signal readahead if we are going sequentially */
        if (cckd->rainit && cckd->lasttrk + 1 == cckd->curtrk)
        {
            obtain_lock (&cckd->ralock);
            cckd->nexttrk = cckd->lasttrk + 1;
            release_lock (&cckd->ralock);
            signal_condition (&cckd->racond);
        }

        /* write the old track if necessary */
        if (wrbuf != NULL)
            cckd_write_trk (dev, wrbuf, wrcompress);
    }

    /* set new file position */
    oldpos = cckd->curpos;
    cckd->curpos = newpos;
    return oldpos;

} /* end function cckd_lseek */


/*-------------------------------------------------------------------*/
/* Compressed ckddasd read                                           */
/*-------------------------------------------------------------------*/
ssize_t cckd_read(DEVBLK *dev, int fd, char *buf, size_t N)
{
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */

    cckd = dev->cckd_ext;

    memcpy (buf, &cckd->buf[cckd->curpos - cckd->trkpos], N);
    cckd->curpos += N;
    return N;

} /* end function cckd_read */


/*-------------------------------------------------------------------*/
/* Compressed ckddasd write                                          */
/*-------------------------------------------------------------------*/
ssize_t cckd_write(DEVBLK *dev, int fd, const void *buf, size_t N)
{
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */

    cckd = dev->cckd_ext;

    cckd->writetime = time(NULL);
    cckd->write = 1;
    memcpy (&cckd->buf[cckd->curpos - cckd->trkpos], buf, N);
    cckd->curpos += N;

    /* If this is the very first write then turn on the OPENed bit;
       this will cause 'chkdsk -1' processing if the file is not
       properly closed */
    if ((cckd->cdevhdr.options & CCKD_OPENED) == 0)
    {
        cckd->cdevhdr.options |= CCKD_OPENED;
        lseek(dev->fd, CKDDASD_DEVHDR_SIZE + 3, SEEK_SET);
        write (dev->fd, &cckd->cdevhdr.options, 1);
    }

    return N;

} /* end function cckd_write */


/*-------------------------------------------------------------------*/
/* Deffered Write queue scan                                         */
/*-------------------------------------------------------------------*/
CCKD_DFWQE *cckd_scan_dfwq(DEVBLK *dev, unsigned int trk)
{
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */
CCKD_DFWQE     *dfw;                    /* -> deffered write q elem  */

    cckd = dev->cckd_ext;

    for (dfw = cckd->dfwq; dfw; dfw = dfw->next)
        if (trk == dfw->trk) break;

    DEVTRACE("cckddasd: dfwqscan trk %d: %s\n", trk,
             dfw ? "found" :
             cckd->dfwq ? "not found" : "null queue");

    return dfw;

} /* end function cckd_scan_dfwq */


/*-------------------------------------------------------------------*/
/* Read ahead to next track image(s)                                 */
/*-------------------------------------------------------------------*/
void cckd_readahead(DEVBLK *dev)
{
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */
unsigned int    trk;                    /* Track for read-ahead      */
unsigned int    lasttrk=0;              /* Previous track            */

    cckd = dev->cckd_ext;
    cckd->rainit = 1;

    do /* while threading */
    {
        obtain_lock (&cckd->ralock);
        wait_condition (&cckd->racond, &cckd->ralock);
        trk = cckd->nexttrk;
        cckd->nexttrk = 0;
        release_lock (&cckd->ralock);

        /* read next track */
        if (trk > 0 && trk < dev->ckdtrks && trk != lasttrk)
        {
            DEVTRACE ("cckddasd: readahead trk %d\n", trk);
            cckd_read_trk (dev, trk, 0);
            lasttrk = trk;
        }

        /* read track after next track */
        if (trk > 0 && trk+1 < dev->ckdtrks && trk+1 != lasttrk &&
            cckd->cachenbr >= dev->ckdheads)
        {
            DEVTRACE ("cckddasd: readahead trk %d\n", trk+1);
            cckd_read_trk (dev, trk+1, 0);
            lasttrk = trk+1;
        }

    } while (cckd->threading);

    cckd->rainit = 0;
} /* end thread cckd_readahead */


/*-------------------------------------------------------------------*/
/* Read a track image                                                */
/*                                                                   */
/* there are two threads that could be reading a track, the          */
/* i/o thread and the readahead thread.                              */
/*                                                                   */
/*-------------------------------------------------------------------*/
unsigned char *cckd_read_trk(DEVBLK *dev, unsigned int trk, int active)
{
int             rc;                     /* Return code               */
int             i;                      /* Index variable            */
int		fnd;                    /* Cache index for hit       */
int		lru;                    /* Least-Recently-Used cache
                                            index                    */
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */
CCKD_DFWQE     *dfw;                    /* -> deffered write q elem  */
unsigned int    l1x;                    /* Level 1 table index       */
CCKD_L1TAB     *l1;                     /* Level 1 table address     */
unsigned int    l2x;                    /* Level 2 table index       */
off_t           l2pos;                  /* Level 2 table offset      */
CCKD_L2TAB      l2;                     /* Level 2 table entry       */
unsigned char  *buf;                    /* Uncompressed buffer       */
unsigned long   buflen;                 /* Uncompressed buffer length*/
unsigned char  *cbuf;                   /* Compressed buffer         */
unsigned long   cbuflen;                /* Compressed buffer length  */
BYTE            compress;               /* Compression algorithm     */

    cckd = dev->cckd_ext;

    DEVTRACE("cckddasd: %d rdtrk %d\n", active, trk);

    /* get the cache array if it doesn't exist yet */
    if (cckd->cache == NULL)
    {
        if (cckd->cachenbr == 0) cckd->cachenbr = dev->ckdheads + 2;
        cckd->cache = calloc (cckd->cachenbr, CCKD_CACHE_SIZE);
    }

    /* scan the cache array for the track */
    buf = NULL; fnd = lru = -1;
    obtain_lock (&cckd->cachelock);
    for (i = 0; i < cckd->cachenbr; i++)
    {
        if (trk == cckd->cache[i].trk && cckd->cache[i].buf)
            fnd = i;
        else
        {
            if (active && cckd->cache[i].active)
                cckd->cache[i].active = 0;
            if ((lru == -1 ||
                 cckd->cache[i].tv.tv_sec < cckd->cache[lru].tv.tv_sec ||
                 (cckd->cache[i].tv.tv_sec == cckd->cache[lru].tv.tv_sec &&
                  cckd->cache[i].tv.tv_usec < cckd->cache[lru].tv.tv_usec)) &&
                !cckd->cache[i].active && !cckd->cache[i].wrtpending)
                lru = i;
        }
    }

    /* check for cache hit */
    if (fnd >= 0)
    {
        /* if cache entry was found and called by readahead,
           this is a `readahead miss' */
        if (!active)
        {
             DEVTRACE ("cckddasd: %d rdtrk %d already in cache for readahead\n",
                       active, cckd->cache[fnd].trk);
             cckd->ramisses++;
             release_lock (&cckd->cachelock);
             return NULL;
        }

        /* if read is in progress then wait for it to finish,
           unless we are reading ahead [active = 0] */
        if (cckd->cache[fnd].reading)
        {
            DEVTRACE ("cckddasd: %d rdtrk waiting %d read buf %p\n",
                  active, cckd->cache[fnd].trk, cckd->cache[fnd].buf);
            cckd->cache[fnd].waiting = 1;
            wait_condition (&cckd->rtcond, &cckd->cachelock);
            cckd->cache[fnd].waiting = 0;
        }

        if (active)
        {
            cckd->cachehits++;
            gettimeofday (&cckd->cache[fnd].tv, NULL);
            cckd->cache[fnd].active = 1;
            cckd->cache[fnd].buf[0] = cckd->cache[fnd].compress;
        } else {
            cckd->cache[fnd].tv.tv_sec = cckd->cache[fnd].tv.tv_usec = 0;
            cckd->cache[fnd].active = 0;
        }
        buf = cckd->cache[fnd].buf;
        release_lock (&cckd->cachelock);
        DEVTRACE ("cckddasd: %d rdtrk cache hit %d buf %p %2.2x%2.2x%2.2x%2.2x%2.2x\n",
                  active, trk, buf, buf[0],buf[1],buf[2],buf[3],buf[4]);
        return buf;
    }

    /* if we only have two cache entries and were called by the
       read-ahead thread, then we might not have found an available
       cache entry, since one is active and the other might be
       write-pending */
    if (lru < 0)
    {
        DEVTRACE ("cckddasd: %d rdtrk %d, no available cache entry\n", active, trk);
        release_lock (&cckd->cachelock);
        return NULL;
    }

    DEVTRACE ("cckddasd: %d rdtrk cache miss trk %d\n", active,trk);

    /* if oldest buf has i/o, set to get a new buf */
    if (cckd->cache[lru].reading || cckd->cache[lru].writing)
    {
        DEVTRACE ("cckddasd: %d rdtrk oldest %d has busy i/o buf %p\n",
                  active, cckd->cache[lru].trk, cckd->cache[lru].buf);
        cckd->cache[lru].reading = cckd->cache[lru].writing = 0;
        cckd->cache[lru].buf = NULL;
    }

    /* scan the deferred write queue.  this can happen if the track
       was scheduled for writing, the cache entry was then stolen,
       and the write hasn't yet completed.
       Note the nested locking */
    obtain_lock (&cckd->dfwlock);
    dfw = cckd_scan_dfwq (dev, trk);
    if (dfw != NULL)
    {
        buf = dfw->buf;
        compress = dfw->compress;
        release_lock (&cckd->dfwlock);
        if (cckd->cache[lru].buf != NULL && cckd->cache[lru].buf != buf)
        {
            DEVTRACE ("cckddasd: %d rdtrk old %d freebuf %p\n",
                      active, cckd->cache[lru].trk, cckd->cache[lru].buf);
            free (cckd->cache[lru].buf);
        }
        cckd->cache[lru].buf = buf;
        cckd->cache[lru].trk = trk;
        cckd->cache[lru].writing = 1;
        cckd->cache[lru].compress = compress;
        if (active)
        {
            cckd->cachehits++;
            cckd->cache[lru].active = 1;
            gettimeofday (&cckd->cache[lru].tv, NULL);
            buf[0] = compress;
        } else {
            cckd->cache[lru].tv.tv_sec = cckd->cache[lru].tv.tv_usec = 0;
            cckd->cache[fnd].active = 0;
        }
        release_lock (&cckd->cachelock);
        DEVTRACE ("cckddasd: %d rdtrk %d in dfwq %p buf %p %2.2x%2.2x%2.2x%2.2x%2.2x\n",
                  active, cckd->cache[lru].trk, dfw, buf,
                  buf[0],buf[1],buf[2],buf[3],buf[4]);
        return buf;
    }
    release_lock (&cckd->dfwlock);

    /* get a buffer if there isn't one */
    if (cckd->cache[lru].buf == NULL)
    {
        cckd->cache[lru].buf = malloc (dev->ckdtrksz);
        DEVTRACE ("cckddasd: %d rdtrk trk %d getbuf %p\n",
                  active, trk, cckd->cache[lru].buf);
    }
    else
    {
        DEVTRACE ("cckddasd: %d rdtrk dropping %d from cache buf %p\n",
                  active, cckd->cache[lru].trk, cckd->cache[lru].buf);
    }

    buf = cckd->cache[lru].buf;
    cckd->cache[lru].trk = trk;
    if (active)
    {
        gettimeofday (&cckd->cache[lru].tv, NULL);
        cckd->cache[lru].active = 1;
    } else {
        cckd->readaheads++;
        cckd->cache[lru].tv.tv_sec = cckd->cache[lru].tv.tv_usec = 0;
        cckd->cache[lru].active = 0;
    }
    cckd->cache[lru].reading = 1;
    release_lock (&cckd->cachelock);

    /* calculate level 1 and level 2 indexes */
    l1x = trk >> 8;
    l2x = trk & 0xff;

    /* prevent compressed file updates */
    obtain_lock (&cckd->filelock);

    /* level 1 table lookup */
    l1 = cckd->l1tab;
    l2pos = l1[l1x];

    /* read the track image */
    cbuf = NULL; compress = cckd->cdevhdr.compress;
    if (!l2pos) cckd_null_rcd (dev, buf, trk);
    else
    {   /* read level 2 table entry */
        rc = lseek (dev->fd, l2pos + l2x * CCKD_L2TAB_SIZE, SEEK_SET);
        rc = read (dev->fd, &l2, CCKD_L2TAB_SIZE);
        if (!l2.pos) cckd_null_rcd (dev, buf, trk);
        else
        {   /* read the track header */
            cckd->reads++;
            rc = lseek (dev->fd, l2.pos, SEEK_SET);
            rc = read (dev->fd, buf, CKDDASD_TRKHDR_SIZE);
            compress = buf[0];
            if (compress == 0)
                read (dev->fd, &buf[CKDDASD_TRKHDR_SIZE],
                               l2.len - CKDDASD_TRKHDR_SIZE);
            else
            {
                cbuf = malloc (dev->ckdtrksz - CKDDASD_TRKHDR_SIZE);
                read (dev->fd, cbuf, l2.len - CKDDASD_TRKHDR_SIZE);
            }
            DEVTRACE("cckddasd: %d rdtrk %d off 0x%x len %d hd "
                     "%2.2x%2.2x%2.2x%2.2x%2.2x\n",
                     active, trk, l2.pos, l2.len,
                     buf[0],buf[1],buf[2],buf[3],buf[4]);
        }
    }

    /* release the file lock */
    release_lock (&cckd->filelock);

    /* uncompress the track image */
    switch (buf[0]) {

        case CCKD_COMPRESS_NONE:
            DEVTRACE("cckddasd: %d rdtrk %d not compressed\n",
                     active, trk);
            break;

        case CCKD_COMPRESS_ZLIB:
            /* Uncompress the track image using zlib.
               Note that the track header is not compressed. */
            cbuflen = l2.len - CKDDASD_TRKHDR_SIZE;
            buflen = dev->ckdtrksz;
            rc = uncompress(&buf[CKDDASD_TRKHDR_SIZE],
                            &buflen, cbuf, cbuflen);

            DEVTRACE("cckddasd: %d rdtrk %d uncompressed len %ld code %d\n",
                     active, trk, buflen, rc);

            if (rc != Z_OK)
            {
                logmsg ("%4.4X cckddasd: rdtrk %d uncompress error: %d\n",
                        dev->devnum, trk, rc);
                cckd_null_rcd (dev, buf, trk);
            }
            break;

#ifdef CCKD_BZIP2
        case CCKD_COMPRESS_BZIP2:
            /* Decompress the track image using bzip2.
               Note that the track header is not compressed. */
            cbuflen = l2.len - CKDDASD_TRKHDR_SIZE;
            buflen = dev->ckdtrksz;
            rc = BZ2_bzBuffToBuffDecompress (
                            &buf[CKDDASD_TRKHDR_SIZE],
                            (unsigned int *)&buflen,
                            cbuf, cbuflen, 0, 0);

            DEVTRACE("cckddasd: %d rdtrk %d decompressed len %ld code %d\n",
                     active, trk, buflen, rc);

            if (rc != BZ_OK)
            {
                logmsg ("cckddasd: decompress error for trk %d: %d\n",
                        trk, rc);
                cckd_null_rcd (dev, buf, trk);
            }
            break;
#endif

        default:
            logmsg ("cckddasd: %4.4x unknown compression for trk %d: %d\n",
                    dev->devnum, trk, buf[0]);
            cckd_null_rcd (dev, buf, trk);
            compress = cckd->cdevhdr.compress;
            break;
    }
    if (cbuf) free (cbuf);
    buf[0] = 0;

    obtain_lock (&cckd->cachelock);

    DEVTRACE("cckddasd: %d rdtrk %d complete buf %p %2.2x%2.2x%2.2x%2.2x%2.2x waiting %d\n",
              active, trk, buf, buf[0],buf[1],buf[2],buf[3],buf[4], 
              cckd->cache[lru].waiting);

    /* if we aren't reading the active track image then our cache
       image could have been stolen.  if so, free the buffer */
    if (!active && buf != cckd->cache[lru].buf)
    {
        free (buf);
        buf = NULL;
    }
    /* Turn off the `reading' bit and signal the other
       thread if it was waiting on us. */
    else
    {
        cckd->cache[lru].compress = compress;
        if (active) buf[0] = compress;
        cckd->cache[lru].reading = 0;
        if (cckd->cache[lru].waiting)
        {
            DEVTRACE("cckddasd: %d rdtrk %d signalling read complete\n",
                     active, trk);
            signal_condition (&cckd->rtcond);
        }
    }

    release_lock (&cckd->cachelock);

    return buf;

} /* end function cckd_read_trk */


/*-------------------------------------------------------------------*/
/* Build a null record                                               */
/*-------------------------------------------------------------------*/
void cckd_null_rcd(DEVBLK *dev, unsigned char *buf, unsigned int trk)
{
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */
U16             cyl;                    /* Cylinder                  */
U16             head;                   /* Head                      */
FWORD           cchh;                   /* Cyl, head big-endian      */

    cckd = dev->cckd_ext;

    DEVTRACE("cckddasd: null_rcd trk %d\n", trk);

    /* cylinder and head calculations */
    cyl = trk / dev->ckdheads;
    head = trk % dev->ckdheads;
    cchh[0] = cyl >> 8;
    cchh[1] = cyl & 0xFF;
    cchh[2] = head >> 8;
    cchh[3] = head & 0xFF;

    /* a null track has a 5 byte track hdr, 8 byte r0 count,
       8 byte r0 data, 8 byte r1 count and 8 ff's */
    memset(buf, 0, 37);
    memcpy (&buf[1], cchh, sizeof(cchh));
    memcpy (&buf[5], cchh, sizeof(cchh));
    buf[12] = 8;
    memcpy (&buf[21], cchh, sizeof(cchh));
    buf[25] = 1;
    memcpy (&buf[29], eighthexFF, 8);

    return;

} /* end function cckd_null_rcd */


/*-------------------------------------------------------------------*/
/* Schedule a track image to be written                              */
/*-------------------------------------------------------------------*/
void cckd_write_trk(DEVBLK *dev, unsigned char *buf, BYTE compress)
{
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */
unsigned int    trk;                    /* Track number              */
int             len;                    /* Track length              */
CCKD_DFWQE     *dfw;                    /* -> deffered write q elem  */
CCKD_DFWQE     *olddfw;                 /* Previous queue head       */
int             i;                      /* Loop index                */

    cckd = dev->cckd_ext;
    trk = ((buf[1] << 8) + buf[2]) * dev->ckdheads +
           (buf[3] << 8) + buf[4];
    len = cckd_trklen (dev, buf);
    dfw = NULL;

    DEVTRACE("cckddasd: wrtrk trk %d len %d buf %p %2.2x%2.2x%2.2x%2.2x%2.2x\n",
             trk, len, buf, buf[0], buf[1], buf[2], buf[3], buf[4]);

    /* turn off the `writepending' bit and turn on the `writing' bit */
    obtain_lock (&cckd->cachelock);
    for (i = 0; i < cckd->cachenbr; i++)
        if (cckd->cache[i].trk == trk &&
            cckd->cache[i].buf == buf)
            {
                cckd->cache[i].wrtpending = 0;
                cckd->cache[i].writing = 1;
                break;
            }
    release_lock (&cckd->cachelock);

    /* get the deffered write lock */
    obtain_lock (&cckd->dfwlock);

    /* save current queue head */
    olddfw = cckd->dfwq;

    /* check if track is already in the queue */
    dfw = cckd_scan_dfwq (dev, trk);
    if (dfw != NULL)
    {   /* reuse the existing entry */
        DEVTRACE("cckddasd: wrtrk reusing%s dfw %p oldbuf %p\n",
                 dfw->busy ? " busy" : "", dfw, dfw->buf);
        if (dfw->busy)
            dfw->retry = 1;
        else
            if (buf != dfw->buf) free (dfw->buf);
        dfw->buf = buf;
        dfw->buflen = len;
        dfw->trk = trk;
        dfw->compress = compress;
    }
    else
    {   /* build a new deferred write queue element */
        dfw = malloc (CCKD_DFQWE_SIZE);
        dfw->next = olddfw;
        dfw->buf = buf;
        dfw->buflen = len;
        dfw->trk = trk;
        dfw->busy = dfw->retry = 0;
        dfw->compress = compress;
        /* insert the queue element into the queue */
        cckd->dfwq = dfw;
    }

    DEVTRACE("cckddasd: wrtrk dfw %p: nxt %p buf %p\n",
             dfw, dfw->next, dfw->buf);
    DEVTRACE("                len %u trk %u compress %d\n",
             dfw->buflen, dfw->trk, dfw->compress);

    /* signal the deferred write thread if necessary */
    if (olddfw == NULL)
    {
        if (cckd->threading)
        {
            if (cckd->writeinit == 0)
            {   /* Start threads on first non-null track write */
                cckd->writeinit = 1;
                create_thread (&cckd->dfwtid, &cckd->dfwattr, cckd_dfw, dev);
                create_thread (&cckd->gctid, &cckd->gcattr, cckd_gcol, dev);
            }
            else signal_condition (&cckd->dfwcond);
        }
        else cckd_dfw (dev);
    }

    /* release the deferred write lock */
    release_lock (&cckd->dfwlock);

    return;

} /* end function cckd_write_trk */


/*-------------------------------------------------------------------*/
/* Return length of an uncompressed track image                      */
/*-------------------------------------------------------------------*/
int cckd_trklen(DEVBLK *dev, BYTE *buf)
{
int             size;                   /* Track size                */

    for (size = CKDDASD_TRKHDR_SIZE;
         memcmp (&buf[size], &eighthexFF, 8) != 0; )
    {
        if (size > dev->ckdtrksz) break;

        /* add length of count, key, and data fields */
        size += CKDDASD_RECHDR_SIZE +
                buf[size+5] +
                (buf[size+6] << 8) + buf[size+7];
    }

    /* add length for end-of-track indicator */
    size += CKDDASD_RECHDR_SIZE;

    /* check for missing end-of-track indicator */
    if (size > dev->ckdtrksz ||
        memcmp (&buf[size-CKDDASD_RECHDR_SIZE], &eighthexFF, 8) != 0)
    {
        logmsg ("cckddasd trklen err for %2.2x%2.2x%2.2x%2.2x%2.2x\n",
                buf[0], buf[1], buf[2], buf[3], buf[4]);
        size = dev->ckdtrksz;
    }

    return size;
}


/*-------------------------------------------------------------------*/
/* Deferred Write thread                                             */
/*-------------------------------------------------------------------*/
void cckd_dfw(DEVBLK *dev)
{
int             rc;                     /* Return code               */
int             i;                      /* Index                     */
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */
CCKD_DFWQE     *dfw;                    /* -> deffered write q elem  */
CCKD_DFWQE     *prevdfw;                /* -> previous write q elem  */
unsigned char  *buf;                    /* Buffer                    */
unsigned int    buflen;                 /* Buffer length             */
unsigned int    trk;                    /* Track number              */
BYTE            compress;               /* Compression algorithm     */
unsigned char  *cbuf;                   /* Compressed buffer         */
unsigned long   cbuflen;                /* Compressed buffer length  */
unsigned char  *obuf;                   /* Output buffer             */
unsigned int    obuflen;                /* Output buffer length      */
unsigned int    l1x;                    /* Level 1 table index       */
CCKD_L1TAB     *l1;                     /* Level 1 table address     */
unsigned int    l2x;                    /* Level 2 table index       */
CCKD_L2TAB      l2;                     /* Level 2 table entry       */
CCKD_L2TAB      oldl2;                  /* Original lvl 2 table entry*/
CCKD_L2TAB      emptyl2 = {0, 0, 0};    /* Empty lvl 2 table entry   */
int             fudge;                  /* Size img can be over alloc*/
unsigned int    relpos, rellen, relfudge;  /* Space to be freed      */
char            freehdr[CCKD_FREEHDR_SIZE]; /* Saved free space hdr  */

    cckd = dev->cckd_ext;
    l1 = cckd->l1tab;
    cbuf = malloc (dev->ckdtrksz);

    /* start off holding the deferred-write lock */
    obtain_lock (&cckd->dfwlock);

    do /* while(cckd->threading) */
    {
        while (cckd->dfwq != NULL)
        {   /* get a queue element */
            dfw = cckd->dfwq;
            DEVTRACE("cckddasd: dfw %p nxt %p buf %p len %u trk %u cmp %u\n",
                     dfw, dfw->next, dfw->buf, dfw->buflen, dfw->trk,
                     dfw->compress);
        dfw_retry:
            dfw->busy = 1;
            dfw->retry = 0;
            buf = dfw->buf;
            buflen = dfw->buflen;
            trk = dfw->trk;
            compress = dfw->compress;

            /* special processing for null track */
            if (buflen == CCKD_NULLRCD_SIZE)
            {
                /* remove the entry from the queue */
                cckd->dfwq = dfw->next;
                release_lock (&cckd->dfwlock);

                /* free the queue entry */
                free (dfw);
                DEVTRACE ("cckddasd: dfw nullrcd trk %d\n", trk);

                /* turn off the `writing' bit in the cache entry */
                obtain_lock (&cckd->cachelock);
                for (i = 0; i < cckd->cachenbr; i++)
                    if (cckd->cache[i].trk == trk &&
                        cckd->cache[i].buf == buf)
                    {
                        DEVTRACE ("cckddasd: dfw write off trk %d buf %p\n",
                                  trk, buf);
                        cckd->cache[i].writing = 0;
                        break;
                    }
                release_lock (&cckd->cachelock);
                if (i >= cckd->cachenbr)
                {
                    DEVTRACE ("cckddasd: dfw freebuf trk %d buf %p\n",
                              trk, buf);
                    free (buf);
                }

                /* calculate level 1 and level 2 indexes */
                l1x = trk >> 8;
                l2x = trk & 0xff;

                /* prevent compressed file updates */
                obtain_lock (&cckd->filelock);

                if (l1[l1x])
                {   /* read level 2 table entry */
                    rc = lseek (dev->fd, l1[l1x] + l2x*CCKD_L2TAB_SIZE,
                                SEEK_SET);
                    rc = read (dev->fd, &l2, CCKD_L2TAB_SIZE);

                    if (l2.pos)
                    {   /* update the level 2 table entry */
                        rc = lseek (dev->fd, -CCKD_L2TAB_SIZE,
                                    SEEK_CUR);
                        rc = write (dev->fd, &emptyl2, CCKD_L2TAB_SIZE);
                        /* release space occupied by old image */
                        cckd_rel_space (dev, l2.pos, l2.len,
                                        l2.size - l2.len);
                    }
                }

                /* release the file lock, get the dfw lock & loop */
                release_lock (&cckd->filelock);
                obtain_lock (&cckd->dfwlock);
                continue;

            } /* special processing for null record */

            release_lock (&cckd->dfwlock);

            /* Compress the track image */
            switch (compress)
            {
                case CCKD_COMPRESS_ZLIB:
                    /* Compress the track image using zlib. Note
                       that the track header is not compressed. */
                    memcpy (cbuf, buf, CKDDASD_TRKHDR_SIZE);
                    cbuflen = dev->ckdtrksz - CKDDASD_TRKHDR_SIZE;
                    rc = compress2 (&cbuf[CKDDASD_TRKHDR_SIZE],
                                    &cbuflen,
                                    &buf[CKDDASD_TRKHDR_SIZE],
                                    buflen - CKDDASD_TRKHDR_SIZE,
                                    cckd->cdevhdr.compress_parm);
                    DEVTRACE("cckddasd: dfw compress len %lu rc=%d\n",
                             cbuflen, rc);
                    if (rc == Z_OK)
                    {   /* use compressed track image */
                        obuf = cbuf;
                        obuflen = cbuflen + CKDDASD_TRKHDR_SIZE;
                        obuf[0] = CCKD_COMPRESS_ZLIB;
                    }
                    else
                    {   /* compression error */
                        obuf = buf;
                        obuflen = buflen;
                        obuf[0] = CCKD_COMPRESS_NONE;
                    }
                    break;

#ifdef CCKD_BZIP2
                case CCKD_COMPRESS_BZIP2:
                    /* Compress the track image using bzip2. Note
                       that the track header is not compressed. */
                    memcpy (cbuf, buf, CKDDASD_TRKHDR_SIZE);
                    cbuflen = dev->ckdtrksz - CKDDASD_TRKHDR_SIZE;
                    rc = BZ2_bzBuffToBuffCompress (
                                    &cbuf[CKDDASD_TRKHDR_SIZE],
                                    (unsigned int *)&cbuflen,
                                    &buf[CKDDASD_TRKHDR_SIZE],
                                    buflen - CKDDASD_TRKHDR_SIZE,
                                    cckd->cdevhdr.compress_parm >= 1 &&
                                    cckd->cdevhdr.compress_parm <= 9 ?
                                    cckd->cdevhdr.compress_parm : 5, 0, 0);
                    DEVTRACE("cckddasd: dfw compress len %lu rc=%d\n",
                             cbuflen, rc);
                    if (rc == BZ_OK)
                    {   /* use compressed track image */
                        obuf = cbuf;
                        obuflen = cbuflen + CKDDASD_TRKHDR_SIZE;
                        obuf[0] = CCKD_COMPRESS_BZIP2;
                    }
                    else
                    {   /* compression error */
                        obuf = buf;
                        obuflen = buflen;
                        obuf[0] = CCKD_COMPRESS_NONE;
                    }
                    break;
#endif

                default:
                case CCKD_COMPRESS_NONE:
                    buf[0] = CCKD_COMPRESS_NONE;
                    obuf = buf;
                    obuflen = buflen;
                    break;

            }

            obtain_lock (&cckd->dfwlock);

            /* if the same track was rewritten while we were
               compressing it, free the old buf if it changed
               and try again */
            if (dfw->retry)
            {
                DEVTRACE ("cckddasd: dfw retry trk %d buf %p obuf %p\n",
                          trk, dfw->buf, buf);
                if (dfw->buf != buf) free (buf);
                goto dfw_retry;
            }

            /* remove the entry from the queue */
            prevdfw = (CCKD_DFWQE *)&cckd->dfwq;
            while (prevdfw->next != dfw)
                prevdfw = prevdfw->next;
            prevdfw->next = dfw->next;

            release_lock (&cckd->dfwlock);

            /* free the queue entry */
            free (dfw);

            /* if we are finished with the original buffer
               then we can turn off the `writing' bit in
               the cache entry */
            if (obuf != buf)
            {
                obtain_lock (&cckd->cachelock);
                for (i = 0; i < cckd->cachenbr; i++)
                    if (cckd->cache[i].trk == trk &&
                        cckd->cache[i].buf == buf)
                    {
                        DEVTRACE ("cckddasd: dfw write off trk %d buf %p\n",
                                  trk, buf);
                        cckd->cache[i].writing = 0;
                        break;
                    }
                release_lock (&cckd->cachelock);
                if (i >= cckd->cachenbr)
                {
                    DEVTRACE ("cckddasd: dfw freebuf trk %d buf %p\n",
                              trk, buf);
                    free (buf);
                }
            }

            /* calculate level 1 and level 2 indexes */
            l1x = trk >> 8;
            l2x = trk & 0xff;

            /* prevent compressed file updates */
            obtain_lock (&cckd->filelock);

            /* copy free space header so we can see if it changed */
            memcpy (&freehdr, &cckd->cdevhdr.CCKD_FREEHDR,
                    CCKD_FREEHDR_SIZE);

            /* build a level 2 table if one doesn't exist */
            if (l1[l1x] == 0)
            {   /* get file space for the level 2 table */
                l1[l1x] = cckd_get_space (dev, 256*CCKD_L2TAB_SIZE, 0,
                                          NULL, NULL);
                /* write the new level 2 table to the file */
                rc = lseek (dev->fd, l1[l1x], SEEK_SET);
                rc = write (dev->fd, &cckd_empty_l2tab,
                            256*CCKD_L2TAB_SIZE);
                memset (&l2, 0, CCKD_L2TAB_SIZE);

                /* update the level 1 table entry */
                rc = lseek (dev->fd, CCKD_L1TAB_POS+l1x*CCKD_L1TAB_SIZE,
                            SEEK_SET);
                rc = write (dev->fd, &l1[l1x], CCKD_L1TAB_SIZE);

                DEVTRACE("cckddasd: dfw new l2 for trk %d, pos 0x%x\n",
                         trk, l1[l1x]);
            }
            else
            {   /* read the level 2 table entry */
                rc = lseek (dev->fd, l1[l1x] + l2x*CCKD_L2TAB_SIZE,
                            SEEK_SET);
                rc = read (dev->fd, &l2, CCKD_L2TAB_SIZE);
            }
            DEVTRACE("cckddasd: dfw l2 trk %d: pos 0x%x len %u sz %u\n",
                     trk, l2.pos, l2.len, l2.size);
            memcpy (&oldl2, &l2, CCKD_L2TAB_SIZE);
            l2.len = obuflen;

            /* calculate fudge factor for the track image; this is the
               the amount of free space that could be overallocated in
               case the image grows -- could probably be improved */
            if ((cckd->cdevhdr.options & CCKD_NOFUDGE) == 0)
            {
                fudge = ((dev->ckdtrksz - dev->ckdtrksz/8) - l2.len) / 8;
                if (fudge < 1) fudge = 1;
            }
            else fudge = 0;

            /* space, if any, will not be freed until after the
               new image is written and the level 2 table is
               updated.  this will allow space recovery to find
               the previous image. */
            relpos = rellen = relfudge = 0;

            /* get space for the track image */
            if (l2.pos == 0)
            {   /* get space if no space currently exists */
                cckd_get_space (dev, l2.len, fudge, &l2.pos, &l2.size);
            }
            else
            {   /* space already exists */
                if (l2.len > oldl2.size)
                {   /* old space is too small */
                    if (oldl2.pos + oldl2.size == cckd->cdevhdr.size)
                    { /* old space is at the end, reuse it */
                        cckd->cdevhdr.size +=
                                       l2.len + fudge - oldl2.size;
                        rc = ftruncate (dev->fd, cckd->cdevhdr.size);
                        l2.pos = oldl2.pos;
                        l2.size = l2.len + fudge;
                        /* manually update free statistics */
                        cckd->cdevhdr.used += l2.len - oldl2.len;
                        cckd->cdevhdr.free_imbed += (l2.size - l2.len)-
                                              (oldl2.size - oldl2.len);
                        cckd->cdevhdr.free_total += (l2.size - l2.len)-
                                              (oldl2.size - oldl2.len);
                    }
                    else
                    {   /* get new space if old space is too small */
                        relpos = oldl2.pos; rellen = oldl2.len;
                        relfudge = oldl2.size - oldl2.len;
                        cckd_get_space (dev, l2.len, fudge, &l2.pos,
                                        &l2.size);
                    }
                }
                else
                {   /* we can reuse the existing space */
                    l2.pos = oldl2.pos;
                    if (l2.len+fudge+CCKD_FREEBLK_SIZE <= oldl2.size)
                    {   /* release extra space if old is too large */
                        l2.size = l2.len + fudge;
                        relpos = l2.pos + l2.size;
                        rellen = oldl2.len < l2.size ?
                                 0 : oldl2.len - l2.size;
                        relfudge = oldl2.len < l2.size  ?
                                   oldl2.size - l2.size :
                                   oldl2.size - oldl2.len;
                        /* manually update free statistics */
                        cckd->cdevhdr.used -= oldl2.len < l2.size ?
                                              oldl2.len - l2.len :
                                              l2.size - l2.len;
                        cckd->cdevhdr.free_imbed += oldl2.len<l2.size ?
                                                    oldl2.len - l2.len:
                                                    l2.size - l2.len;
                        cckd->cdevhdr.free_total += oldl2.len<l2.size ?
                                                    oldl2.len - l2.len:
                                                    l2.size - l2.len;
                    }
                    else
                    {   /* existing size remains the same */
                        l2.size = oldl2.size;
                        /* manually update free statistics */
                        cckd->cdevhdr.used += l2.len - oldl2.len;
                        cckd->cdevhdr.free_imbed += (l2.size - l2.len)-
                                              (oldl2.size - oldl2.len);
                        cckd->cdevhdr.free_total += (l2.size - l2.len)-
                                              (oldl2.size - oldl2.len);
                    }
                }
            }

            /* write the new track image */
            rc = lseek (dev->fd, l2.pos, SEEK_SET);
            rc = write (dev->fd, obuf, l2.len);
            cckd->writes++;
            DEVTRACE ("cckddasd: dfw write trk %d 0x%x len %d %2.2x%2.2x%2.2x%2.2x%2.2x\n",
                      trk, l2.pos, l2.len, obuf[0], obuf[1], obuf[2], obuf[3], obuf[4]);

            /* rewrite level 2 table entry if it changed */
            if (memcmp(&l2, &oldl2, CCKD_L2TAB_SIZE) != 0)
            {
                rc = lseek (dev->fd, l1[l1x] + l2x * CCKD_L2TAB_SIZE,
                            SEEK_SET);
                rc = write (dev->fd, &l2, CCKD_L2TAB_SIZE);
                DEVTRACE("cckddasd: dfw upd l2 trk %d: pos 0x%x len %d sz %d\n",
                         trk, l2.pos, l2.len, l2.size);
            }

            /* release any space */
            if (relpos != 0) cckd_rel_space (dev, relpos, rellen, relfudge);

            /* rewrite free space statistics if they changed */
            if (memcmp (&freehdr, &cckd->cdevhdr.CCKD_FREEHDR,
                        CCKD_FREEHDR_SIZE) != 0)
            {
                rc = lseek (dev->fd, CCKD_FREEHDR_POS, SEEK_SET);
                rc = write (dev->fd, &cckd->cdevhdr.CCKD_FREEHDR,
                            CCKD_FREEHDR_SIZE);
            }

            /* release the file lock */
            release_lock (&cckd->filelock);

            /* if we wrote the original buffer, we can now turn
               off the `writing' bit in the cache entry */
            if (obuf == buf)
            {
                obtain_lock (&cckd->cachelock);
                for (i = 0; i < cckd->cachenbr; i++)
                    if (cckd->cache[i].trk == trk &&
                        cckd->cache[i].buf == buf)
                    {
                        DEVTRACE ("cckddasd: dfw writeoff trk %d buf %p\n",
                                  trk, buf);
                        cckd->cache[i].writing = 0;
                        break;
                    }
                release_lock (&cckd->cachelock);
                if (i >= cckd->cachenbr)
                {
                    DEVTRACE ("cckddasd: dfw freebuf trk %d buf %p\n",
                              trk, buf);
                    free (buf);
                }
            }

            obtain_lock (&cckd->dfwlock);

        } /* while (cckd->dfwq != NULL) */

        /* wait for another track to be written */
        if (cckd->threading)
            wait_condition (&cckd->dfwcond, &cckd->dfwlock);

    } while (cckd->threading);

    release_lock (&cckd->dfwlock);
    free (cbuf) ;
#ifdef CCKD_NOTHREAD
    cckd_gcol(dev);
#endif
} /* end thread cckd_dfw */


/*-------------------------------------------------------------------*/
/* Get file space                                                    */
/*-------------------------------------------------------------------*/
off_t cckd_get_space(DEVBLK *dev, unsigned int len, unsigned int fudge,
                     unsigned int *new_pos, unsigned short *new_size)
{
int             rc;                     /* Return code               */
unsigned int    size;                   /* Free space size           */
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */
off_t           fpos;                   /* Free space offset         */
unsigned int    flen;                   /* Free space size           */
CCKD_FREEBLK    free;                   /* Free block                */
off_t           pfpos;                  /* Previous free space offset*/
CCKD_FREEBLK    pfree;                  /* Previous free block       */
off_t           lpos;                   /* Largest free space offset */
unsigned int    largest;                /* Largest free space        */

    cckd = dev->cckd_ext;
    size = len + fudge;
    DEVTRACE("cckddasd: get_space len %d fudge %d sz %d\n",
             len, fudge, size);
    if (len > cckd->cdevhdr.free_largest)
    { /* no free space big enough; add space to end of the file */
    get_space_at_end:
        fpos = cckd->cdevhdr.size;
        cckd->cdevhdr.size += size;
        rc = ftruncate (dev->fd, cckd->cdevhdr.size);
        cckd->cdevhdr.used += len;
        cckd->cdevhdr.free_total += fudge;
        cckd->cdevhdr.free_imbed += fudge;
        if (new_pos) *new_pos = fpos;
        if (new_size) *new_size = size;
        DEVTRACE("cckddasd: get_space_at_end pos 0x%lx sz %d\n",
                 fpos, size);
        return fpos;
    }

    /* scan free space chain */
    pfpos = largest = 0;
    for (fpos = cckd->cdevhdr.free; fpos; fpos = free.pos)
    {
        rc = lseek (dev->fd, fpos, SEEK_SET);
        rc = read (dev->fd, &free, CCKD_FREEBLK_SIZE);
        if ((fudge != 0 && free.len >= len) || free.len == len ||
            free.len >= len + CCKD_FREEBLK_SIZE) break;
        pfpos = fpos;
        memcpy (&pfree, &free, CCKD_FREEBLK_SIZE);
        if (free.len > largest) largest = free.len;
    }

    /* If we didn't find space go get it at the end of file;
       this should only happen when fudge is 0 */
    if (!fpos) goto get_space_at_end;

    flen = free.len;
    if (free.len >= size + CCKD_FREEBLK_SIZE)
    { /* only use a portion of the free space */
        free.len -= size;
        if (free.len > largest) largest = free.len;
        rc = lseek (dev->fd, fpos + size, SEEK_SET);
        rc = write (dev->fd, &free, CCKD_FREEBLK_SIZE);
        if (pfpos)
        {
            pfree.pos = fpos + size;
            rc = lseek (dev->fd, pfpos, SEEK_SET);
            rc = write (dev->fd, &pfree, CCKD_FREEBLK_SIZE);
        }
        else cckd->cdevhdr.free = fpos + size;
    }
    else
    { /* otherwise use the entire free space */
        if (pfpos)
        {
            pfree.pos = free.pos;
            rc = lseek (dev->fd, pfpos, SEEK_SET);
            rc = write (dev->fd, &pfree, CCKD_FREEBLK_SIZE);
        }
        else cckd->cdevhdr.free = free.pos;
        cckd->cdevhdr.free_number--;
        fudge = free.len - len;
        size = len + fudge;
    }

    /* find the next largest free space if we got the largest */
    if (flen == cckd->cdevhdr.free_largest)
    {
        for (lpos = free.pos; lpos; lpos = free.pos)
        {
            rc = lseek (dev->fd, lpos, SEEK_SET);
            rc = read (dev->fd, &free, CCKD_FREEBLK_SIZE);
            if (free.len > largest) largest = free.len;
        }
        cckd->cdevhdr.free_largest = largest;
     }

    /* update free space stats */
    cckd->cdevhdr.used += len;
    cckd->cdevhdr.free_total -= len;
    cckd->cdevhdr.free_imbed += fudge;

    /* update caller's parameters */
    if (new_pos) *new_pos = fpos;
    if (new_size) *new_size = size;

    DEVTRACE("cckddasd: get_space found pos 0x%lx sz %d\n",
             fpos, size);

    return fpos;

} /* end function cckd_get_space */


/*-------------------------------------------------------------------*/
/* Release file space                                                */
/*-------------------------------------------------------------------*/
void cckd_rel_space(DEVBLK *dev, unsigned int pos, int len, int fudge)
{
int             rc;                     /* Return code               */
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */
int             size;                   /* Size of free space        */
off_t           fpos;                   /* Free space offset         */
off_t           pfpos;                  /* Previous free space offset*/
off_t           pfpos2;                 /* Prev free space offset -2 */
CCKD_FREEBLK    free;                   /* Free block                */
CCKD_FREEBLK    pfree;                  /* Previous free block       */
CCKD_FREEBLK    pfree2;                 /* Previous free block -2    */
CCKD_FREEBLK    nfree;                  /* New free block            */

    cckd = dev->cckd_ext;
    size = len + fudge;

    DEVTRACE("cckddasd: rel_space len %d fudge %d sz %d pos 0x%x\n",
             len, fudge, size, pos);

    /* scan free space chain */
    pfpos = pfpos2 = 0;
    for (fpos = cckd->cdevhdr.free; fpos != 0 && fpos < pos;
         fpos = free.pos)
    {
        rc = lseek (dev->fd, fpos, SEEK_SET);
        rc = read (dev->fd, &free, CCKD_FREEBLK_SIZE);
        pfpos2 = pfpos; pfpos = fpos;
        memcpy (&pfree2, &pfree, CCKD_FREEBLK_SIZE);
        memcpy (&pfree, &free, CCKD_FREEBLK_SIZE);
    }

    /* initialize new free space block */
    nfree.pos = fpos;
    nfree.len = size;

    /* if the new space is followed by free space, combine the two */
    if (pos + size == fpos)
    {
        rc = lseek (dev->fd, fpos, SEEK_SET);
        rc = read (dev->fd, &free, CCKD_FREEBLK_SIZE);
        nfree.pos = free.pos;
        nfree.len += free.len;
    }
    else cckd->cdevhdr.free_number++;

    /* if the new space is preceded by free space, combine the two */
    if (pfpos && pfpos + pfree.len == pos)
    {
        pos = pfpos; pfpos = pfpos2;
        nfree.len += pfree.len;
        memcpy (&pfree, &pfree2, CCKD_FREEBLK_SIZE);
        cckd->cdevhdr.free_number--;
    }

    /* if the free space is at the end of file then truncate */
    if (pos + nfree.len == cckd->cdevhdr.size)
    {
        rc = ftruncate (dev->fd, pos);
        /* update previous free block */
        if (pfpos)
        {
            pfree.pos = 0;
            rc = lseek (dev->fd, pfpos, SEEK_SET);
            rc = write (dev->fd, &pfree, CCKD_FREEBLK_SIZE);
        }
        else cckd->cdevhdr.free = 0;
        /* recalculate largest free space size */
        if (nfree.len >= cckd->cdevhdr.free_largest)
        {
            cckd->cdevhdr.free_largest = 0;
            for (fpos = cckd->cdevhdr.free; fpos; fpos = free.pos)
            {
                rc = lseek (dev->fd, fpos, SEEK_SET);
                rc = read (dev->fd, &free, CCKD_FREEBLK_SIZE);
                if (free.len > cckd->cdevhdr.free_largest)
                    cckd->cdevhdr.free_largest = free.len;
            }
        }
        /* update free stats */
        cckd->cdevhdr.size = pos;
        cckd->cdevhdr.used -= len;
        cckd->cdevhdr.free_number--;
        cckd->cdevhdr.free_total -= nfree.len - len;
        cckd->cdevhdr.free_imbed -= fudge;
    }
    /* otherwise write the free headers & update the free stats */
    else
    {
        rc = lseek (dev->fd, pos, SEEK_SET);
        rc = write (dev->fd, &nfree, CCKD_FREEBLK_SIZE);
        if (pfpos)
        {
            pfree.pos = pos;
            rc = lseek (dev->fd, pfpos, SEEK_SET);
            rc = write (dev->fd, &pfree, CCKD_FREEBLK_SIZE);
        }
        else cckd->cdevhdr.free = pos;
        cckd->cdevhdr.used -= len;
        cckd->cdevhdr.free_total += len;
        cckd->cdevhdr.free_imbed -= fudge;
        if (nfree.len > cckd->cdevhdr.free_largest)
            cckd->cdevhdr.free_largest = nfree.len;
    }

} /* end function cckd_rel_space */


/*-------------------------------------------------------------------*/
/* Garbage Collection thread                                         */
/*-------------------------------------------------------------------*/
void cckd_gcol(DEVBLK *dev)
{
int             rc;                     /* Return code               */
int             i;                      /* Loop index                */
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */
unsigned char  *wrbuf;                  /* Buffer to be written      */
unsigned int    wrtrk;                  /* Write track               */
BYTE            wrcompress;             /* Write compression         */
unsigned int    size;                   /* File size                 */
int             wait;                   /* Seconds to wait           */
struct timespec tm;                     /* Time-of-day to wait       */
unsigned int    gc;                     /* Garbage states            */
CCKD_GCOL       empty_gc[5]={           /* Empty gcol                */
                  {0,0,0,0},            /*   Critcal  50%   - 100%   */
                  {0,0,0,0},            /*   Severe   25%   -  50%   */
                  {0,0,0,0},            /*   Moderate 12.5% -  25%   */
                  {0,0,0,0},            /*   Light     6.25%-  12.5% */
                  {0,0,0,0}             /*   None      0%   -   6.25%*/
                            };
CCKD_GCOL       default_gc[5]={         /* default gcol              */
                  {GC_COMBINE,    2, 8, 256*1024},
                  {GC_COMBINE,    4, 4, 128*1024},
                  {GC_PERCOLATE,  8, 4,        0},
                  {GC_PERCOLATE, 10, 2,        0},
                  {GC_PERCOLATE, 20, 1,  32*1024}
                              };
char           *gc_state[]={            /* Garbage states            */
                  "critical", "severe", "moderate", "light", "none"
                           };

    cckd = dev->cckd_ext;
#ifdef CCKD_NOTHREAD
    if (time(NULL) < cckd->gctime) return;
#endif

    /* first-time initialization */
    if (!cckd->gcinit)
    {
        cckd->gcinit=1;
        obtain_lock (&cckd->filelock);

        /* check garbage collection parameters */
        if (memcmp (&cckd->cdevhdr.CCKD_GCHDR, &empty_gc,
                    CCKD_GCHDR_SIZE) == 0)
        {   /* use default garbage collection */
            logmsg ("cckddasd: setting default garbage collection\n");
            memcpy (&cckd->cdevhdr.CCKD_GCHDR, &default_gc,
                    CCKD_GCHDR_SIZE);
            for (gc = 0; gc < 5; gc++)
                if (cckd->cdevhdr.gcol[gc].size == 0)
                    cckd->cdevhdr.gcol[gc].size = dev->ckdtrksz;
            rc = lseek (dev->fd, CCKD_GCHDR_POS, SEEK_SET);
            rc = write (dev->fd, &cckd->cdevhdr.CCKD_GCHDR,
                        CCKD_GCHDR_SIZE);
        }

        /* a file created by the os/390 cckddump program will have a
           single free space at the end of the file which we must
           remove because free space at the end of the file is not
           allowed */
        if (cckd->cdevhdr.free_number == 1)
        {
            CCKD_FREEBLK free;

            lseek (dev->fd, cckd->cdevhdr.free, SEEK_SET);
            read (dev->fd, &free, CCKD_FREEBLK_SIZE);
            if (cckd->cdevhdr.free + free.len == cckd->cdevhdr.size)
            {
                cckd->cdevhdr.size -= free.len;
                cckd->cdevhdr.free = 0;
                cckd->cdevhdr.free_largest = 0;
                cckd->cdevhdr.free_total -= free.len;
                cckd->cdevhdr.free_number = 0;
                rc = lseek (dev->fd, CCKD_FREEHDR_POS, SEEK_SET);
                rc = write (dev->fd, &cckd->cdevhdr.CCKD_FREEHDR,
                            CCKD_FREEHDR_SIZE);
                rc = ftruncate (dev->fd, cckd->cdevhdr.size);
            }
        }

        release_lock (&cckd->filelock);
    }

    time (&cckd->gctime);
    do /* while cckd->threading */
    {
        /* write current track if enough time has elapsed */
        if (cckd->write &&
            cckd->gctime - cckd->writetime >= CCKD_MAX_WRITE_TIME)
        {
            wrbuf = cckd->buf; wrcompress = cckd->compress;
            wrtrk = cckd->curtrk;
            obtain_lock (&cckd->cachelock);
            if (cckd->write && wrtrk == cckd->curtrk &&
                cckd->gctime - cckd->writetime >= CCKD_MAX_WRITE_TIME)
            {
                cckd->write = cckd->writetime = 0;
                /* set `writing-pending' bit in the cache array */
                for (i = 0; i < cckd->cachenbr; i++)
                    if (wrtrk == cckd->cache[i].trk)
                    {
                        cckd->cache[i].wrtpending = 1;
                        break;
                    }
            }
            else wrbuf = NULL;
            release_lock (&cckd->cachelock);
            if (wrbuf)
            {
                DEVTRACE ("gcol writing trk %d buf %p\n",
                          wrtrk, wrbuf);
                cckd_write_trk (dev, wrbuf, wrcompress);
            }
        }

        /* determine garbage state */
        size = cckd->cdevhdr.size;
        if      (cckd->cdevhdr.free_total >= (size = size /2)) gc = 0;
        else if (cckd->cdevhdr.free_total >= (size = size /2)) gc = 1;
        else if (cckd->cdevhdr.free_total >= (size = size /2)) gc = 2;
        else if (cckd->cdevhdr.free_total >= (size = size /2)) gc = 3;
        else gc = 4;

        if (gc < 3)
            logmsg ("cckddasd: %4.4x garbage collection state is %s\n",
                    dev->devnum, gc_state[gc]);

        /* call the garbage collector */
        switch (cckd->cdevhdr.gcol[gc].algorithm)
        {
            default:
            case GC_PERCOLATE:
                cckd_gc_percolate(dev, cckd->cdevhdr.gcol[gc].iterations,
                                       cckd->cdevhdr.gcol[gc].size);
                break;

            case GC_COMBINE:
                cckd_gc_combine(dev, cckd->cdevhdr.gcol[gc].iterations,
                                     cckd->cdevhdr.gcol[gc].size);
                break;

        }

        time (&cckd->gctime);

#ifndef CCKD_NOTHREAD
        /* figure out how long to wait */
        if (cckd->write && cckd->writetime + CCKD_MAX_WRITE_TIME <
                        cckd->gctime + cckd->cdevhdr.gcol[gc].interval)
           wait = cckd->writetime + CCKD_MAX_WRITE_TIME - cckd->gctime;
        else wait = cckd->cdevhdr.gcol[gc].interval;
        if (wait < 1) wait = 1;

        DEVTRACE ( "gcol waiting %d seconds at %s",
                   wait, ctime(&cckd->gctime));

        /* wait a bit */
        tm.tv_sec = cckd->gctime + wait;
        tm.tv_nsec = 0;
        wait_timed_condition ( &cckd->gccond, &cckd->gclock, &tm);
        release_lock (&cckd->gclock);
        time (&cckd->gctime);

        DEVTRACE ( "gcol waking up at %s", ctime(&cckd->gctime));
#endif
    } while (cckd->threading);
} /* end thread cckd_gcol */


/*-------------------------------------------------------------------*/
/* Garbage Collection -- Release imbedded free space - qsort compare */
/*-------------------------------------------------------------------*/
int cckd_gc_trim_comp(const void *a, const void *b)
{
const struct     l2sort {               /* Level 2 sort entry        */
U32              l2x;                   /* Level 2 entry index       */
U16              size;                  /* Imbedded free space       */
              } *x = a, *y = b;         /* Entries to be sorted      */

    if (y->size > x->size)
        return 1;
    else return -1;
} /* end function cckd_gc_trim_comp */


/*-------------------------------------------------------------------*/
/* Garbage Collection -- Release imbedded free space                 */
/*-------------------------------------------------------------------*/
int cckd_gc_trim(DEVBLK *dev, int size)
{
int              rc;                    /* Return code               */
CCKDDASD_EXT    *cckd;                  /* -> cckd extension         */
CCKD_L1TAB      *l1;                    /* Level 1 table address     */
CCKD_L2TAB       l2[256];               /* Level 2 table             */
unsigned int     l1x;                   /* Level 1 table index       */
unsigned int     l2x;                   /* Level 2 table index       */
unsigned int     i;                     /* Integer                   */
unsigned int     released=0;            /* Space released            */
unsigned int     r;                     /* Space released for table  */
unsigned int     fudge;                 /* Space released for track  */
struct           l2sort {               /* Level 2 sort entry        */
U32              l2x;                   /* Level 2 entry index       */
U16              size;                  /* Imbedded free space       */
               } sizetab[256];          /* Sorted lvl 2 space table  */
int              largest;               /* Largest space for table   */
unsigned int     start;                 /* Starting index            */
int              stop;                  /* Stop indicator            */

    cckd = dev->cckd_ext;
    if (size == 0 || size > cckd->cdevhdr.free_imbed)
        size = cckd->cdevhdr.free_imbed;
    l1 = cckd->l1tab;

    DEVTRACE("cckddasd: gcol trim sz %d\n", size);

    obtain_lock (&cckd->filelock);

    if (cckd->gctrkgrp >= cckd->cdevhdr.numl1tab)
        cckd->gctrkgrp = 0;

    stop = 0; start = cckd->gctrkgrp;

    /* loop through level 1 table entries */
    for (l1x = cckd->gctrkgrp; released < size; l1x++)
    {
        if (!(cckd->threading)) break;

        /* check for wrap */
        if (l1x >= cckd->cdevhdr.numl1tab) l1x = 0;

        /* stop if we can't release any more space */
        if (l1x == start)
        {
            if (stop) break;
            stop = 1;
        }

        /* continue if no level 2 table */
        if (l1[l1x] == 0) continue;

        /* read the level 2 table */
        rc = lseek (dev->fd, l1[l1x], SEEK_SET);
        rc = read (dev->fd, &l2, 256 * CCKD_L2TAB_SIZE);

        /* build a table containing imbedded free space sizes */
        largest = 0;
        for (l2x = 0; l2x < 256; l2x++)
        {
            sizetab[l2x].l2x = l2x;
            sizetab[l2x].size = l2[l2x].size - l2[l2x].len;
            if (sizetab[l2x].size > largest)
                largest = sizetab[l2x].size;
        }

        /* continue if no imbedded free space large enough */
        if (largest < CCKD_FREEBLK_SIZE) continue;

        /* turn off stop indicator if we can free something */
        stop = 0;

        DEVTRACE ("cckddasd: gcol trim l2[%d] pos 0x%x largest %d\n",
                  l1x, l1[l1x], largest);

        /* sort the size table by descending imbedded free space */
        qsort ((void *)&sizetab, 256, sizeof (struct l2sort),
               cckd_gc_trim_comp);

        /* remove the 16 largest imbedded free spaces */
        r = 0;
        for (i = 0; i < 16 && sizetab[i].size >= CCKD_FREEBLK_SIZE &&
                    released < size; i++)
        {
            l2x = sizetab[i].l2x;
            DEVTRACE ("cckddasd: gcol trim 0x%x trk %d: %d to %d\n",
                       l2[l2x].pos, l1x * 256 + l2x,
                       l2[l2x].size, l2[l2x].len);
            fudge = l2[l2x].size - l2[l2x].len;
            l2[l2x].size = l2[l2x].len;
            rc = lseek (dev->fd, l1[l1x] + l2x * CCKD_L2TAB_SIZE,
                        SEEK_SET);
            rc = write (dev->fd, &l2[l2x], CCKD_L2TAB_SIZE);
            cckd_rel_space (dev, l2[l2x].pos + l2[l2x].len, 0, fudge);
            r += fudge;
        }
        released += r;
        DEVTRACE ("cckddasd: gcol trimmed l2[%d], released %d\n",
                  l1x, r);
    }

    cckd->gctrkgrp = l1x;
    if (cckd->gctrkgrp >= cckd->cdevhdr.numl1tab)
        cckd->gctrkgrp = 0;

    release_lock (&cckd->filelock);

    return released;

} /* end function cckd_gc_trim */


/*-------------------------------------------------------------------*/
/* Garbage Collection -- Percolate algorithm                         */
/*                                                                   */
/* This algorithm simply moves the first free space towards the      */
/* end of the file by up to `size' bytes.  Free spaces within        */
/* the `size' bytes are combined with the first free space.          */
/* If the free space ends up at the end of the file, then the        */
/* file is truncated.                                                */
/*                                                                   */
/*-------------------------------------------------------------------*/
void cckd_gc_percolate(DEVBLK *dev, int iterations, int size)
{
int             rc;                     /* Return code               */
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */
CCKD_L1TAB     *l1;                     /* Level 1 table address     */
unsigned int    l1x;                    /* Level 1 table index       */
unsigned int    l2x;                    /* Level 2 table index       */
unsigned int    read_len;               /* Read length               */
unsigned int    len;                    /* Length                    */
off_t           fpos;                   /* Free space offset         */
off_t           pos;                    /* Space offset              */
off_t           off;                    /* Buffer offset             */
unsigned int    moved;                  /* Amount of space moved     */
CCKD_FREEBLK    free1;                  /* Free space block          */
CCKD_FREEBLK    free2;                  /* Next free space block     */

    cckd = dev->cckd_ext;
    l1 = cckd->l1tab;

    /* release imbedded free space if imbedded free space exceeds
       total free space (if there are free spaces)             or
       if imbedded free space exceeds 6% of the file size */
    if ((cckd->cdevhdr.free_number > 0 &&
            cckd->cdevhdr.free_imbed * 2 > cckd->cdevhdr.free_total) ||
        cckd->cdevhdr.free_imbed * 16 > cckd->cdevhdr.size )
        rc = cckd_gc_trim (dev, size);

    if (iterations == 0) iterations = 1;
    if (size == 0) size = dev->ckdtrksz;

    DEVTRACE("cckddasd: gcperc iter %d sz %d 1st 0x%x nbr %d\n",
      iterations, size, cckd->cdevhdr.free, cckd->cdevhdr.free_number);

    if (cckd->cdevhdr.free == 0) return;

    /* free the old buffer if it's too short */
    if (cckd->gcbuf != NULL && size > cckd->gcbuflen)
    {
        free (cckd->gcbuf);
        cckd->gcbuf = NULL;
    }

    /* get a buffer if we don't have one */
    if (cckd->gcbuf == NULL)
    {
       cckd->gcbuflen = size > dev->ckdtrksz ?
                        size : dev->ckdtrksz;
       cckd->gcbuf = malloc (cckd->gcbuflen);
    }

    for ( ; iterations && cckd->threading; iterations--)
    {
        /* get the file lock */
        obtain_lock (&cckd->filelock);

        /* exit if no more free space */
        if (cckd->cdevhdr.free == 0)
        {
            release_lock (&cckd->filelock);
            break;
        }

        /* read the free block */
        fpos = cckd->cdevhdr.free;
        rc = lseek (dev->fd, fpos, SEEK_SET);
        rc = read (dev->fd, &free1, CCKD_FREEBLK_SIZE);
        pos = fpos + free1.len;

        DEVTRACE("cckddasd: gcperc free pos 0x%lx len %d\n",
                 fpos, free1.len);

        /* read to end of file or `size' */
        if (pos + size <= cckd->cdevhdr.size)
            read_len = size;
        else read_len = cckd->cdevhdr.size - pos;

        rc = lseek (dev->fd, pos, SEEK_SET);
        rc = read (dev->fd, cckd->gcbuf, read_len);

        /* make sure we read at least 1 complete img */
        len = cckd_gc_len(dev, cckd->gcbuf, pos, 0, read_len);
        if (len > read_len)
        {
            rc = read(dev->fd, &cckd->gcbuf[read_len],
                      len - read_len);
            read_len = len;
        }

        DEVTRACE("cckddasd: gcperc read pos 0x%lx len %u\n",
                 pos, read_len);

        /* relocate each track image or level 2 table in the buffer */
        off = moved = 0;
        while (len !=0 && len + off <= read_len)
        {
            /* determine what we're repositioning */

            if (len == 256 * CCKD_L2TAB_SIZE && cckd->gcl2x == -1)
            {
                l1x = cckd->gcl1x;
                /* repositioning a level 2 table */
                if (memcmp (&cckd->gcbuf[off], &cckd_empty_l2tab, len)
                    != 0)
                {
                    DEVTRACE("cckddasd: gcperc repo l2[%u]: "
                             "0x%x to 0x%x\n",
                             l1x, l1[l1x], l1[l1x] - free1.len);
                    l1[l1x] -= free1.len;
                    rc = lseek (dev->fd, l1[l1x], SEEK_SET);
                    rc = write (dev->fd, &cckd->gcbuf[off], len);
                    rc = lseek (dev->fd,
                                CCKD_L1TAB_POS + l1x * CCKD_L1TAB_SIZE,
                                SEEK_SET);
                    rc = write (dev->fd, &l1[l1x], CCKD_L1TAB_SIZE);
                    moved += len;
                }
                /* remove empty level 2 table */
                else
                {
                    DEVTRACE("cckddasd: gcperc empty l2[%u]\n", l1x);
                    l1[l1x] = 0;
                    rc = lseek (dev->fd,
                                CCKD_L1TAB_POS + l1x * CCKD_L1TAB_SIZE,
                                SEEK_SET);
                    rc = write (dev->fd, &l1[l1x], CCKD_L1TAB_SIZE);
                    free1.len += len;
                }
            }
            else
            {
                /* repositioning a track image */

                l1x = cckd->gcl1x;
                l2x = cckd->gcl2x;
                cckd->gcl2tab.pos -= free1.len;

                /* write the track image to the new position */
                rc = lseek (dev->fd, cckd->gcl2tab.pos, SEEK_SET);
                rc = write (dev->fd, &cckd->gcbuf[off], cckd->gcl2tab.len);

                /* update level 2 table entry in the file */
                rc = lseek (dev->fd, l1[l1x] + l2x * CCKD_L2TAB_SIZE,
                            SEEK_SET);
                rc = write (dev->fd, &cckd->gcl2tab, CCKD_L2TAB_SIZE);

                /* if the level 2 table is in the buffer, then update
                   the buffer, too */
                if (l1[l1x] >= pos + off &&
                    l1[l1x] <= pos + size - (256 * CCKD_L2TAB_SIZE))
                {
                    memcpy (&cckd->gcbuf[l1[l1x]-pos + l2x*CCKD_L2TAB_SIZE],
                            &cckd->gcl2tab, CCKD_L2TAB_SIZE);
                }
                DEVTRACE("cckddasd: gcperc repo trk %u: 0x%x(%u,%u) to 0x%x\n",
                         l1x * 256 + l2x, cckd->gcl2tab.pos + free1.len,
                         cckd->gcl2tab.len, cckd->gcl2tab.size, cckd->gcl2tab.pos);
                moved += len;
            }
            off += len;

            /* check if we're pointing at free space now */
            if (pos + off == free1.pos)
            {
                if (off + CCKD_FREEBLK_SIZE <= size)
                    memcpy (&free2, &cckd->gcbuf[off], CCKD_FREEBLK_SIZE);
                else
                {
                    rc = lseek (dev->fd, free1.pos, SEEK_SET);
                    rc = read (dev->fd, &free2, CCKD_FREEBLK_SIZE);
                }
                DEVTRACE("cckddasd: gcperc combine free 0x%lx len %d\n",
                         pos + off, free2.len);
                free1.pos = free2.pos;
                free1.len += free2.len;
                cckd->cdevhdr.free_number--;
                off += free2.len;
            }

            /* get length of next space */
            len = cckd_gc_len(dev, cckd->gcbuf, pos, off, read_len);
        }

        DEVTRACE("cckddasd: gcperc space moved %d\n",
                 moved);

        /* if the free space is at the end then release it */
        if (fpos + moved + free1.len == cckd->cdevhdr.size)
        {
            DEVTRACE("cckddasd: gcperc del free 0x%lx len %d\n",
                     fpos + moved, free1.len);
            cckd->cdevhdr.size -= free1.len;
            rc = ftruncate (dev->fd, cckd->cdevhdr.size);
            cckd->cdevhdr.free = 0;
            cckd->cdevhdr.free_total -= free1.len;
            cckd->cdevhdr.free_number = 0;
            cckd->cdevhdr.free_largest = 0;
        }
        /* otherwise re-write the relocated free space */
        else
        {
            DEVTRACE("cckddasd: gcperc new free 0x%lx len %d\n",
                     fpos + moved, free1.len);
            cckd->cdevhdr.free = fpos + moved;
            rc = lseek (dev->fd, cckd->cdevhdr.free, SEEK_SET);
            rc = write (dev->fd, &free1, CCKD_FREEBLK_SIZE);
            if (free1.len > cckd->cdevhdr.free_largest)
                cckd->cdevhdr.free_largest = free1.len;
        }

        /* update the free header */
        rc = lseek (dev->fd, CCKD_FREEHDR_POS, SEEK_SET);
        rc = write (dev->fd, &cckd->cdevhdr.CCKD_FREEHDR,
                    CCKD_FREEHDR_SIZE);

        release_lock (&cckd->filelock);
    }

    /* free the buffer if it's not standard */
    if (cckd->gcbuflen > dev->ckdtrksz)
    {
        free (cckd->gcbuf);
        cckd->gcbuf = NULL;
    }

    return;

} /* end function cckd_gc_percolate */


/*-------------------------------------------------------------------*/
/* Garbage Collection -- Combine algorithm                           */
/*                                                                   */
/* This algorithm is similar to the percolate algorithm except       */
/* that instead of selecting the first free space block, it selects  */
/* the free space block that will combine with the most other free   */
/* space blocks within `size' bytes.  In the event of a tie, the     */
/* algorithm will select the free space block that is closest to     */
/* the end of the file.                                              */
/*                                                                   */
/* Athough this algorithm will produce better space results than the */
/* percolate algorithm, it is more expensive because it has to run   */
/* the free space chain and build a table to decide which free space */
/* block to select.                                                  */
/*                                                                   */
/*-------------------------------------------------------------------*/
void cckd_gc_combine(DEVBLK *dev, int iterations, int size)
{
int             rc;                     /* Return code               */
int             f,i,j;                  /* Indices                   */
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */
CCKD_L1TAB     *l1;                     /* Level 1 table address     */
unsigned int    l1x;                    /* Level 1 table index       */
unsigned int    l2x;                    /* Level 2 table index       */
unsigned int    read_len;               /* Read length               */
unsigned int    len;                    /* Length                    */
off_t           pos;                    /* Buffer offset             */
off_t           off;                    /* Offset into buffer        */
off_t           fpos;                   /* Free space offset         */
CCKD_FREEBLK    free1;                  /* Free space block          */
unsigned int    moved;                  /* Amount of space moved     */
struct fspw {                           /* Free space work area      */
off_t           pos;                    /* Free space offset         */
off_t           nxt;                    /* Next free space offset    */
int             len;                    /* Free space length         */
int             cnt;                    /* Count value for free space*/
            }  *fsp;                    /* -> free space work area   */

    cckd = dev->cckd_ext;
    l1 = cckd->l1tab;

    while (cckd->cdevhdr.free_imbed * 2 > cckd->cdevhdr.free_total)
    {
        rc = cckd_gc_trim (dev, size);
        if (rc == 0) break;
    }

    if (iterations == 0) iterations = 1;
    if (size == 0) size = dev->ckdtrksz;

    DEVTRACE("cckddasd: gccomb iter %d sz %d 1st 0x%x nbr %d\n",
             iterations, size, cckd->cdevhdr.free, cckd->cdevhdr.free_number);

    if (cckd->cdevhdr.free == 0) return;

    /* free the old buffer if it's too short */
    if (cckd->gcbuf != NULL && size > cckd->gcbuflen)
    {
        free (cckd->gcbuf);
        cckd->gcbuf = NULL;
    }

    /* get a buffer if we don't have one */
    if (cckd->gcbuf == NULL)
    {
       cckd->gcbuflen = size > dev->ckdtrksz ?
                        size : dev->ckdtrksz;
       cckd->gcbuf = malloc (cckd->gcbuflen);
    }

    for ( ; iterations && cckd->threading; iterations--)
    {
        /* get the file lock */
        obtain_lock (&cckd->filelock);

        /* exit if no more free space */
        if ((pos = cckd->cdevhdr.free) == 0)
        {
            release_lock (&cckd->filelock);
            break;
        }

        /* get free space work area */
        fsp = malloc( sizeof(struct fspw) *
                      cckd->cdevhdr.free_number);
        if (fsp == NULL)
        {
            release_lock (&cckd->filelock);
            logmsg ("cckddasd: gccomb malloc() failed: %s\n",
                    strerror(errno));
            break;
        }

        /* read the free space chain and build the work area */
        for (i = 0, fpos = cckd->cdevhdr.free;
             fpos != 0; i++, fpos = free1.pos)
        {
            rc = lseek (dev->fd, fpos, SEEK_SET);
            rc = read (dev->fd, &free1, CCKD_FREEBLK_SIZE);
            fsp[i].pos = fpos;
            fsp[i].nxt = free1.pos;
            fsp[i].len = free1.len;
            fsp[i].cnt = 0;
        }

        /* generate counts (number of free spaces that can be
           combined with this one) */
        for (i = 0; i < cckd->cdevhdr.free_number; i++)
            for ( j = i + 1 ; j < cckd->cdevhdr.free_number; j++)
            {
                if (fsp[i].pos + size >= fsp[j].pos)
                    fsp[i].cnt++;
                else break;
            }

        /* find the free space with the largest count.  In case of a
           tie, use the one closest to the end of the file */
        for (f = i = 0; i < cckd->cdevhdr.free_number; i++)
            if (fsp[i].cnt >= fsp[f].cnt)
                f = i;

        pos = fsp[f].pos + fsp[f].len;

        DEVTRACE ("cckddasd: gccomb free pos 0x%lx len %d cnt %d\n",
                  fsp[f].pos, fsp[f].len, fsp[f].cnt);

        /* read to end of file or `size' */
        if (pos + size <= cckd->cdevhdr.size)
            read_len = size;
        else
            read_len = cckd->cdevhdr.size - pos;

        rc = lseek (dev->fd, pos, SEEK_SET);
        rc = read (dev->fd, cckd->gcbuf, read_len);

        /* make sure we read at least 1 complete img */
        len = cckd_gc_len(dev, cckd->gcbuf, pos, 0, read_len);
        if (len > read_len)
        {
            rc = read(dev->fd, &cckd->gcbuf[read_len],
                      len - read_len);
            read_len = len;
        }

        DEVTRACE("cckddasd: gccomb read pos 0x%lx len %u\n",
                 pos, read_len);

        /* relocate each track image or level 2 table in the buffer */
        off = moved = 0; j = f;
        while (len !=0 && len + off <= read_len)
        {
            /* determine what we're repositioning */

            if (len == 256 * CCKD_L2TAB_SIZE && cckd->gcl2x == -1)
            {
                l1x = cckd->gcl1x;
                /* repositioning a level 2 table */
                if (memcmp (&cckd->gcbuf[off], &cckd_empty_l2tab, len)
                    != 0)
                {
                    DEVTRACE("cckddasd: gccomb repo l2[%u]: "
                             "0x%x to 0x%x\n",
                             l1x, l1[l1x], l1[l1x] - fsp[f].len);
                    l1[l1x] -= fsp[f].len;
                    rc = lseek (dev->fd, l1[l1x], SEEK_SET);
                    rc = write (dev->fd, &cckd->gcbuf[off], len);
                    rc = lseek (dev->fd,
                                CCKD_L1TAB_POS + l1x * CCKD_L1TAB_SIZE,
                                SEEK_SET);
                    rc = write (dev->fd, &l1[l1x], CCKD_L1TAB_SIZE);
                    moved += len;
                }
                /* remove empty level 2 table */
                else
                {
                    DEVTRACE("cckddasd: gccomb empty l2[%u]\n",l1x);
                    l1[l1x] = 0;
                    rc = lseek (dev->fd,
                                CCKD_L1TAB_POS + l1x * CCKD_L1TAB_SIZE,
                                SEEK_SET);
                    rc = write (dev->fd, &l1[l1x], CCKD_L1TAB_SIZE);
                    fsp[f].len += len;
                }
            }
            else
            {
                /* repositioning a track image */
                l1x = cckd->gcl1x;
                l2x = cckd->gcl2x;
                cckd->gcl2tab.pos -= fsp[f].len;

                /* write the track image to the new position */
                rc = lseek (dev->fd, cckd->gcl2tab.pos, SEEK_SET);
                rc = write (dev->fd, &cckd->gcbuf[off], cckd->gcl2tab.len);

                /* update level 2 table entry in the file */
                rc = lseek (dev->fd, l1[l1x] + l2x * CCKD_L2TAB_SIZE,
                            SEEK_SET);
                rc = write (dev->fd, &cckd->gcl2tab, CCKD_L2TAB_SIZE);

                /* if the level 2 table is in the buffer, then update
                   the buffer, too */
                if (l1[l1x] >= pos + off &&
                    l1[l1x] <= pos + size - (256 * CCKD_L2TAB_SIZE))
                    memcpy (&cckd->gcbuf[l1[l1x]-pos + l2x*CCKD_L2TAB_SIZE],
                            &cckd->gcl2tab, CCKD_L2TAB_SIZE);
                moved += len;
                DEVTRACE("cckddasd: gccomb repo trk %u: 0x%x(%u,%u) to 0x%x\n",
                         l1x * 256 + l2x, cckd->gcl2tab.pos + fsp[f].len,
                         cckd->gcl2tab.len, cckd->gcl2tab.size, cckd->gcl2tab.pos);
            }
            off += len;

            /* check if we are now at a free space */
            if (pos + off == fsp[f].nxt)
            {
                j++;
                fsp[f].nxt = fsp[j].nxt;
                fsp[f].len += fsp[j].len;
                cckd->cdevhdr.free_number--;
                DEVTRACE("cckddasd: gccomb combine free 0x%lx len %d\n",
                         pos + off, fsp[j].len);
                off += fsp[j].len;
            }

            /* get length of next space */
            len = cckd_gc_len(dev, cckd->gcbuf, pos, off, read_len);
        }

        DEVTRACE("cckddasd: gccomb space moved %d\n",
                 moved);

        /* if the free space is at the end then release it */
        if (fsp[f].pos + moved + fsp[f].len == cckd->cdevhdr.size)
        {
            DEVTRACE("cckddasd: gccomb del free 0x%lx len %d\n",
                     fsp[f].pos + moved, fsp[f].len);
            cckd->cdevhdr.size -= fsp[f].len;
            rc = ftruncate (dev->fd, cckd->cdevhdr.size);
            cckd->cdevhdr.free_total -= fsp[f].len;
            if (cckd->cdevhdr.free == fsp[f].pos)
                cckd->cdevhdr.free = 0;
            else
            {   /* update previous free space */
                free1.pos = 0;
                free1.len = fsp[f-1].len;
                rc = lseek (dev->fd, fsp[f-1].pos, SEEK_SET);
                rc = write (dev->fd, &free1, CCKD_FREEBLK_SIZE);
            }
            cckd->cdevhdr.free_number--;
            if (fsp[f].len >= cckd->cdevhdr.free_largest)
            {   /* recalculate largest free space */
                cckd->cdevhdr.free_largest = 0;
                for (i = 0; i < f; i++)
                    if (fsp[i].len > cckd->cdevhdr.free_largest)
                        cckd->cdevhdr.free_largest = fsp[i].len;
            }
        }
        /* otherwise re-write the relocated free space */
        else
        {
            DEVTRACE("cckddasd: gccomb new free 0x%lx len %d\n",
                     fsp[f].pos + moved, fsp[f].len);
            if (cckd->cdevhdr.free == fsp[f].pos)
                cckd->cdevhdr.free += moved;
            else
            {   /* update previous free space */
                free1.pos = fsp[f].pos + moved;
                free1.len = fsp[f-1].len;
                rc = lseek (dev->fd, fsp[f-1].pos, SEEK_SET);
                rc = write (dev->fd, &free1, CCKD_FREEBLK_SIZE);
            }
            free1.pos = fsp[f].nxt;
            free1.len = fsp[f].len;
            rc = lseek (dev->fd, fsp[f].pos + moved, SEEK_SET);
            rc = write (dev->fd, &free1, CCKD_FREEBLK_SIZE);
            if (fsp[f].len > cckd->cdevhdr.free_largest)
                cckd->cdevhdr.free_largest = fsp[f].len;
        }
        free (fsp);

        /* update the free header */
        rc = lseek (dev->fd, CCKD_FREEHDR_POS, SEEK_SET);
        rc = write (dev->fd, &cckd->cdevhdr.CCKD_FREEHDR,
                    CCKD_FREEHDR_SIZE);

        release_lock (&cckd->filelock);
    }

    /* free the buffer if it's not standard */
    if (cckd->gcbuflen > dev->ckdtrksz)
    {
        free (cckd->gcbuf);
        cckd->gcbuf = NULL;
    }

    return;

} /* end function cckd_gc_combine */


/*-------------------------------------------------------------------*/
/* Garbage Collection -- Return length of space                      */
/*                                                                   */
/* Space should either be a track image or a level 2 table.          */
/* Sets the following fields in the CCKD extension:                  */
/*                                                                   */
/*   gcl1x   -  level 1 table index for the space                    */
/*   gcl2x   -  level 2 table index for a track image else -1        */
/*   gcl2tab -  level 2 table entry for a track image                */
/*                                                                   */
/*-------------------------------------------------------------------*/
int cckd_gc_len(DEVBLK *dev, unsigned char *buf,
                off_t pos, unsigned int off, unsigned int len)
{
int             rc;                     /* Return code               */
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */
unsigned int    cyl;                    /* Cylinder  (from trk hdr)  */
unsigned int    hd;                     /* Head      (from trk hdr)  */
unsigned int    trk;                    /* Track number              */
unsigned int    l1x;                    /* Level 1 table index       */
unsigned int    l2x;                    /* Level 2 table index       */
CCKD_L1TAB     *l1;                     /* Level 1 table address     */

    if (off + CKDDASD_TRKHDR_SIZE > len) return 0;
    cckd = dev->cckd_ext;
    if (!(cckd->threading)) return 0;
    cckd->gcl1x = cckd->gcl2x = -1;

    DEVTRACE("cckddasd: gclen bpos 0x%lx off 0x%x len %u pos 0x%lx\n",
             pos, off, len, pos + off);
    DEVTRACE("               data %2.2x%2.2x%2.2x%2.2x%2.2x\n",
             buf[off], buf[off+1], buf[off+2], buf[off+3], buf[off+4]);

    cyl = (buf[off+1] << 8) + buf[off+2];
    hd  = (buf[off+3] << 8) + buf[off+4];

    if (cyl >= dev->ckdcyls || hd >= dev-> ckdheads)
    {
        if ((cckd->gcl1x = cckd_gc_l1x(dev, pos + off)) != -1)
            return 256 * CCKD_L2TAB_SIZE;
        else
        {
            logmsg ("cckddasd: %4.4x unknown space at offset 0x%lx\n",
                    dev->devnum, pos + off);
            cckd->gcl1x = cckd->gcl2x = -1;
            return 0;
        }
    }

    trk = cyl * dev->ckdheads + hd;
    l1x = trk >> 8;
    l2x = trk & 0xff;
    l1 = cckd->l1tab;

    if (l1[l1x] == 0)
    {
        if ((cckd->gcl1x = cckd_gc_l1x(dev, pos + off)) != -1)
            return 256 * CCKD_L2TAB_SIZE;
        else
        {
            logmsg ("cckddasd: %4.4x unknown space at offset 0x%lx\n",
                    dev->devnum, pos + off);
            cckd->gcl1x = cckd->gcl2x = -1;
            return 0;
        }
    }

    /* now check for level 2 table */
    if ((cckd->gcl1x = cckd_gc_l1x(dev, pos + off)) != -1)
        return 256 * CCKD_L2TAB_SIZE;

    /* read the level 2 table entry */
    rc = lseek (dev->fd, l1[l1x] + l2x * CCKD_L2TAB_SIZE,
                SEEK_SET);
    rc = read (dev->fd, &cckd->gcl2tab, CCKD_L2TAB_SIZE);

    cckd->gcl1x = l1x; cckd->gcl2x = l2x;
    if (cckd->gcl2tab.pos == pos + off) return cckd->gcl2tab.size;

    logmsg ("cckddasd: %4.4x unknown space at offset 0x%lx\n",
            dev->devnum, pos + off);
    cckd->gcl1x = cckd->gcl2x = -1;
    return 0;

} /* end function cckd_gc_len */


/*-------------------------------------------------------------------*/
/* Garbage Collection -- Return index for a level 2 table            */
/*-------------------------------------------------------------------*/
int cckd_gc_l1x(DEVBLK *dev, off_t off)
{
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */
CCKD_L1TAB     *l1;                     /* level 1 table address     */
unsigned int    l1x;                    /* level 1 table index       */

    cckd = dev->cckd_ext;
    l1 = cckd->l1tab;
    for (l1x = 0; l1x < cckd->cdevhdr.numl1tab; l1x++)
        if (l1[l1x] == off) return l1x;
    return -1;

} /* end function cckd_gc_l1x */


/*-------------------------------------------------------------------*/
/* Swap endian                                                       */
/*-------------------------------------------------------------------*/
void cckd_swapend (DEVBLK *dev)
{
int               i,j;                  /* Indexes                   */
char              opt=CCKD_BIGENDIAN;   /* Byte with big-endian bit  */
CCKDDASD_DEVHDR   cdevhdr;              /* Compressed ckd header     */
CCKD_L1TAB       *l1;                   /* Level 1 table             */
CCKD_L2TAB        l2[256];              /* Level 2 table             */
CCKD_FREEBLK      free1;                /* Free block                */

    /* fix the compressed ckd header */

    lseek (dev->fd, CKDDASD_DEVHDR_SIZE, SEEK_SET);
    read (dev->fd, &cdevhdr, CCKDDASD_DEVHDR_SIZE);

    cdevhdr.options ^= opt;
    cckd_swapend4 ((char *) &cdevhdr.numl1tab);
    cckd_swapend4 ((char *) &cdevhdr.numl2tab);
    cckd_swapend4 ((char *) &cdevhdr.size);
    cckd_swapend4 ((char *) &cdevhdr.used);
    cckd_swapend4 ((char *) &cdevhdr.free);
    cckd_swapend4 ((char *) &cdevhdr.free_total);
    cckd_swapend4 ((char *) &cdevhdr.free_largest);
    cckd_swapend4 ((char *) &cdevhdr.free_number);
    cckd_swapend4 ((char *) &cdevhdr.free_imbed);
    cckd_swapend2 ((char *) &cdevhdr.compress_parm);
    for (i=0; i<5; i++)
    {
        cckd_swapend4 ((char *) &cdevhdr.gcol[i].algorithm);
        cckd_swapend4 ((char *) &cdevhdr.gcol[i].interval);
        cckd_swapend4 ((char *) &cdevhdr.gcol[i].iterations);
        cckd_swapend4 ((char *) &cdevhdr.gcol[i].size);
    }

    lseek (dev->fd, CKDDASD_DEVHDR_SIZE, SEEK_SET);
    write (dev->fd, &cdevhdr, CCKDDASD_DEVHDR_SIZE);

    /* fix the level 1 and level 2 tables */

    l1 = malloc (cdevhdr.numl1tab * CCKD_L1TAB_SIZE);
    lseek (dev->fd, CCKD_L1TAB_POS, SEEK_SET);
    read (dev->fd, l1, cdevhdr.numl1tab * CCKD_L1TAB_SIZE);
    for (i=0; i<cdevhdr.numl1tab; i++)
    {
        cckd_swapend4 ((char *) &l1[i]);
        lseek (dev->fd, l1[i], SEEK_SET);
        read (dev->fd, &l2, 256*CCKD_L2TAB_SIZE);
        for (j=0; j<256; j++)
        {
            cckd_swapend4 ((char *) &l2[j].pos);
            cckd_swapend2 ((char *) &l2[j].len);
            cckd_swapend2 ((char *) &l2[j].size);
        }
        lseek (dev->fd, l1[i], SEEK_SET);
        write (dev->fd, &l2, 256*CCKD_L2TAB_SIZE);
    }
    lseek (dev->fd, CCKD_L1TAB_POS, SEEK_SET);
    write (dev->fd, l1, cdevhdr.numl1tab * CCKD_L1TAB_SIZE);
    free (l1);

    /* fix the free chain */
    for (i = cdevhdr.free; i; i = free1.pos)
    {
        lseek (dev->fd, i, SEEK_SET);
        read (dev->fd, &free1, CCKD_FREEBLK_SIZE);
        cckd_swapend4 ((char *) &free1.pos);
        cckd_swapend4 ((char *) &free1.len);
        lseek (dev->fd, i, SEEK_SET);
        write (dev->fd, &free1, CCKD_FREEBLK_SIZE);
    }
}


/*-------------------------------------------------------------------*/
/* Swap endian - 4 bytes                                             */
/*-------------------------------------------------------------------*/
void cckd_swapend4 (char *c)
{
 char temp[4];

    memcpy (&temp, c, 4);
    c[0] = temp[3];
    c[1] = temp[2];
    c[2] = temp[1];
    c[3] = temp[0];
}


/*-------------------------------------------------------------------*/
/* Swap endian - 2 bytes                                             */
/*-------------------------------------------------------------------*/
void cckd_swapend2 (char *c)
{
 char temp[2];

    memcpy (&temp, c, 2);
    c[0] = temp[1];
    c[1] = temp[0];
}


/*-------------------------------------------------------------------*/
/* Are we little or big endian?  From Harbison&Steele.               */
/*-------------------------------------------------------------------*/
int cckd_endian()
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
/* Print internal trace                                              */
/*-------------------------------------------------------------------*/
void cckd_print_itrace(DEVBLK *dev)
{
#ifdef CCKD_ITRACEMAX
CCKDDASD_EXT   *cckd;                   /* -> cckd extension         */
int             start, i;               /* Start index, index        */

    cckd = dev->cckd_ext;
    obtain_lock (&cckd->filelock);
    i = start = cckd->itracex;
    do
    {
        if (i >= 128*CCKD_ITRACEMAX) i = 0;
        if (cckd->itrace[i] != '\0')
            logmsg ("%s", &cckd->itrace[i]);
        i+=128;
    } while (i != start);
    release_lock (&cckd->filelock);
#endif
}

#else /* NO_CCKD */

int cckddasd_init_handler ( DEVBLK *dev, int argc, BYTE *argv[] )
{
    logmsg ("%4.4X cckddasd support not generated\n", dev->devnum);
    return -1;
}
int cckddasd_close_device (DEVBLK *dev)
{
    logmsg ("%4.4X cckddasd support not generated\n", dev->devnum);
    return -1;
}
off_t cckd_lseek(DEVBLK *dev, int fd, off_t offset, int pos)
{
    logmsg ("%4.4X cckddasd support not generated\n", dev->devnum);
    return -1;
}
ssize_t cckd_read(DEVBLK *dev, int fd, char *buf, size_t N)
{
    logmsg ("%4.4X cckddasd support not generated\n", dev->devnum);
    return -1;
}
ssize_t cckd_write(DEVBLK *dev, int fd, const void *buf, size_t N)
{
    logmsg ("%4.4X cckddasd support not generated\n", dev->devnum);
    return -1;
}

#endif
