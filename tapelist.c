/* TAPELIST.C   Portions (c) Copyright James M. Morrison, 2000-2001 */  
/* Dump AWSTAPE format tape image                                   */ 
/* Program heavily based on Jay Maynard's (c) 2000-2001 TAPEMAP     */

/*-------------------------------------------------------------------*/
/* This program reads an AWSTAPE format tape image file and produces */
/* a dump of the tape, printing any standard label records it finds. */
/*                                                                   */
/* Each block is divided into 80 byte (or less at the end) records,  */
/* translated from EBCDIC to ASCII, and printed to stdout.           */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Structure definition for AWSTAPE block header                     */
/*-------------------------------------------------------------------*/
typedef struct _AWSTAPE_BLKHDR {
        HWORD   curblkl;                /* Length of this block      */
        HWORD   prvblkl;                /* Length of previous block  */
        BYTE    flags1;                 /* Flags byte 1              */
        BYTE    flags2;                 /* Flags byte 2              */
    } AWSTAPE_BLKHDR;

/* Definitions for AWSTAPE_BLKHDR flags byte 1 */
#define AWSTAPE_FLAG1_NEWREC    0x80    /* Start of new record       */
#define AWSTAPE_FLAG1_TAPEMARK  0x40    /* Tape mark                 */
#define AWSTAPE_FLAG1_ENDREC    0x20    /* End of record             */

/*-------------------------------------------------------------------*/
/* Static data areas                                                 */
/*-------------------------------------------------------------------*/
static BYTE vollbl[] = "\xE5\xD6\xD3";  /* EBCDIC characters "VOL"   */
static BYTE hdrlbl[] = "\xC8\xC4\xD9";  /* EBCDIC characters "HDR"   */
static BYTE eoflbl[] = "\xC5\xD6\xC6";  /* EBCDIC characters "EOF"   */
static BYTE eovlbl[] = "\xC5\xD6\xE5";  /* EBCDIC characters "EOV"   */
static BYTE buf[65500];

/*-------------------------------------------------------------------*/
/* EBCDIC to ASCII translate table                                   */
/*-------------------------------------------------------------------*/
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
/* TAPELIST main entry point                                         */
/*-------------------------------------------------------------------*/
int main (int argc, char *argv[])
{
int             i,j;                    /* Array subscript           */
int             len;                    /* Block length              */
int             prevlen;                /* Previous block length     */
BYTE           *filename;               /* -> Input file name        */
int             infd = -1;              /* Input file descriptor     */
int             fileno;                 /* Tape file number          */
int             blkcount;               /* Block count               */
int             curblkl;                /* Current block length      */
int             minblksz;               /* Minimum block size        */
int             maxblksz;               /* Maximum block size        */
int             dumprecs = 9999999; 	/* # blks to dump/file       */
int             dumpedrecs;             /* # records dumped this file*/
int		minlrecl;		/* min(len, sizeof(recbuf)-1)*/
int		bufferoffset;		/* offset into buffer 	     */
BYTE            labelrec[81];           /* Standard label (ASCIIZ)   */
BYTE            recbuf[81];             /* EBCDIC->ASCII workarea    */
AWSTAPE_BLKHDR  awshdr;                 /* AWSTAPE block header      */

    /* The only argument is the tape image file name */
    if (argc == 2 && argv[1] != NULL)
    {
        filename = argv[1];
    }
    else
    {
        printf ("Usage: tapelist filename\n");
        exit (1);
    }

    /* Open the tape device */
    infd = open (filename, O_RDONLY | O_BINARY);
    if (infd < 0)
    {
        printf ("tapelist: Error opening %s: %s\n",
                filename, strerror(errno));
        exit (2);
    }

    /* Read blocks from the input file and report on them */
    fileno = 1;
    blkcount = 0;
    minblksz = 0;
    maxblksz = 0;
    len = 0;
    dumpedrecs = 0;

    while (1)
    {
        /* Save previous block length */
        prevlen = len;

        /* Read the AWSTAPE header block from the tape */
        len = read (infd, buf, sizeof(AWSTAPE_BLKHDR));
        if (len < 0)
        {
            printf ("tapelist: error reading header block from %s: %s\n",
                    filename, strerror(errno));
            exit (3);
        }

        /* Did we finish too soon? */
        if ((len > 0) && (len < sizeof(AWSTAPE_BLKHDR)))
        {
            printf ("tapelist: incomplete block header on %s\n",
                    filename);
            exit(4);
        }
        
        /* Check for end of tape. */
        if (len == 0)
        {
            printf ("End of tape.\n");
            break;
        }
        
        /* Parse the block header */
        memcpy(&awshdr, buf, sizeof(AWSTAPE_BLKHDR));
        
        /* Tapemark? */
        if ((awshdr.flags1 & AWSTAPE_FLAG1_TAPEMARK) != 0)
        {
            /* Print summary of current file */
            printf ("File %u: Blocks=%u, block size min=%u, max=%u\n\n",
                    fileno, blkcount, minblksz, maxblksz);

            /* Reset counters for next file */
            fileno++;
            minblksz = 0;
            maxblksz = 0;
            blkcount = 0;
	    dumpedrecs = 0;

        }
        else /* if(tapemark) */
        {
            /* Count blocks and block sizes */
            blkcount++;
            curblkl = awshdr.curblkl[0] + (awshdr.curblkl[1] << 8);
            if (curblkl > maxblksz) maxblksz = curblkl;
            if (minblksz == 0 || curblkl < minblksz) minblksz = curblkl;

            /* Read the data block. */
            len = read (infd, buf, curblkl);
            if (len < 0)
            {
                printf ("tapelist: error reading data block from %s: %s\n",
                        filename, strerror(errno));
                exit (5);
            }

            /* Did we finish too soon? */
            if ((len > 0) && (len < curblkl))
            {
                printf ("tapelist: incomplete final data block on %s, "
                        "expected %d bytes, got %d\n",
                        filename, curblkl, len);
                exit(6);
            }
        
            /* Check for end of tape */
            if (len == 0)
            {
                printf ("tapelist: header block with no data on %s\n",
                        filename);
                exit(7);
            }
        
            /* Print standard labels */
            if (len == 80 && blkcount < 4
                && (memcmp(buf, vollbl, 3) == 0
                    || memcmp(buf, hdrlbl, 3) == 0
                    || memcmp(buf, eoflbl, 3) == 0
                    || memcmp(buf, eovlbl, 3) == 0))
            {
                for (i=0; i < 80; i++)
                    labelrec[i] = ebcdic_to_ascii[buf[i]];
                labelrec[i] = '\0';
                printf ("%s\n", labelrec);
            } else {
	    	/* print records to be dumped */
	    	if (dumpedrecs < dumprecs) {
		   /* dump each 80 byte (or less) record from each block */
		   for (bufferoffset = 0; 
			bufferoffset < len;
			bufferoffset += minlrecl) { 			

			if (len < (sizeof(recbuf) - 1)) 			    
			    minlrecl = len;
			else
			    minlrecl = sizeof(recbuf) - 1;
			for (j = 0; j < minlrecl; j++)
			    recbuf[j] = ebcdic_to_ascii[
				*(&(buf[j])+ bufferoffset)];
 			dumpedrecs++;
	        	recbuf[j] = '\0';
	        	printf("%s\n", recbuf);
		   }
	    	}
	    }
            
        } /* end if(tapemark) */

    } /* end while */

    /* Close files and exit */
    close (infd);

    return 0;

} /* end function main */
