/* SIMTAPE.C    (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Simulated Tape Device Handler                */

/*-------------------------------------------------------------------*/
/* This module contains device handling functions for emulated       */
/* 3420 magnetic tape devices for the Hercules ESA/390 emulator.     */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Structure definition for tape block header                        */
/*-------------------------------------------------------------------*/
typedef struct _SIMTAPE_BLKHDR {
        BYTE    curblkll, curblklh;     /* Length of this block
                                           (low & high order bytes)  */
        BYTE    prvblkll, prvblklh;     /* Length of previous block
                                           (low & high order bytes)  */
        BYTE    flags1;                 /* Flags byte 1              */
        BYTE    flags2;                 /* Flags byte 2              */
    } SIMTAPE_BLKHDR;


/*-------------------------------------------------------------------*/
/* Initialize the device handler                                     */
/*-------------------------------------------------------------------*/
int simtape_init_handler ( DEVBLK *dev, int argc, BYTE *argv[] )
{
    /* The first argument is the file name */
    if (argc == 0 || strlen(argv[0]) > sizeof(dev->filename)-1)
    {
        printf ("HHC201I File name missing or invalid\n");
        return -1;
    }

    /* Save the file name in the device block */
    strcpy (dev->filename, argv[0]);

    /* Initialize device dependent fields */
    dev->fd = -1;
    dev->curblkpos = -1;
    dev->nxtblkpos = 0;
    dev->prvblkpos = -1;

    /* Set number of sense bytes */
    dev->numsense = 24;

    return 0;
} /* end function simtape_init_handler */


/*-------------------------------------------------------------------*/
/* Execute a Channel Command Word                                    */
/*-------------------------------------------------------------------*/
void simtape_execute_ccw ( DEVBLK *dev, BYTE code, BYTE flags,
        BYTE chained, U16 count, BYTE prevcode, int ccwseq,
        BYTE *iobuf, BYTE *more, BYTE *unitstat, U16 *residual )
{
SIMTAPE_BLKHDR  hdr;                    /* Tape block header         */
int             rc;                     /* Return code               */
U16             curblkl;                /* Length of current block   */
U16             prvblkl;                /* Length of previous block  */
long            num;                    /* Number of bytes to read   */

    /* Open the device file if necessary */
    if (dev->fd < 0)
    {
        rc = open (dev->filename, O_RDONLY);
        if (rc < 0)
        {
            /* Handle open failure */
            perror("simtape: open error");

            /* Set unit check with intervention required */
            dev->sense[0] = SENSE_IR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return;
        }
        dev->fd = rc;
    }

    /* Process depending on CCW opcode */
    switch (code) {

    case 0x01:
    /*---------------------------------------------------------------*/
    /* WRITE                                                         */
    /*---------------------------------------------------------------*/
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        break;

    case 0x02:
    /*---------------------------------------------------------------*/
    /* READ FORWARD                                                  */
    /*---------------------------------------------------------------*/
        /* Start a new block if not data-chained from previous CCW */
        if ((chained & CCW_FLAGS_CD) == 0)
        {
            /* Position to start of next block */
            rc = lseek (dev->fd, dev->nxtblkpos, SEEK_SET);
            if (rc < 0)
            {
                /* Handle seek error condition */
                perror("simtape: lseek error");

                /* Set unit check with equipment check */
                dev->sense[0] = SENSE_EC;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }
            dev->curblkpos = dev->nxtblkpos;

            /* Read the 6-byte block header */
            rc = read (dev->fd, &hdr, sizeof(hdr));
            if (rc < sizeof(hdr))
            {
                /* Handle read error condition */
                if (rc < 0)
                    perror("simtape: read error");
                else
                    printf("simtape: unexpected end of file\n");

                /* Set unit check with equipment check */
                dev->sense[0] = SENSE_EC;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }

            /* Extract the block lengths from the block header */
            curblkl = ((U16)hdr.curblklh << 8) | hdr.curblkll;
            prvblkl = ((U16)hdr.prvblklh << 8) | hdr.prvblkll;

            /* Calculate the offsets of the next and previous blocks */
            dev->nxtblkpos = dev->curblkpos + sizeof(hdr) + curblkl;
            dev->prvblkpos = dev->curblkpos - sizeof(hdr) - prvblkl;

            /* Initialize number of bytes in current block */
            dev->curblkrem = curblkl;

            /* Zero length block (tapemark) produces unit exception */
            if (curblkl == 0)
            {
                *unitstat = CSW_CE | CSW_DE | CSW_UX;
                break;
            }

        } /* end if(!data-chained) */

        /* Calculate number of bytes to read and residual byte count */
        num = (count < dev->curblkrem) ? count : dev->curblkrem;
        *residual = count - num;
        if (count < dev->curblkrem) *more = 1;

        /* Read data from tape file */
        rc = read (dev->fd, iobuf, num);
        if (rc < num)
        {
            /* Handle read error condition */
            if (rc < 0)
                perror("simtape: read error");
            else
                printf("simtape: unexpected end of file\n");

            /* Set unit check with equipment check */
            dev->sense[0] = SENSE_EC;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Update length remaining in current block */
        dev->curblkrem -= num;

        /* Set normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x03:
    /*---------------------------------------------------------------*/
    /* CONTROL NO-OPERATION                                          */
    /*---------------------------------------------------------------*/
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x07:
    /*---------------------------------------------------------------*/
    /* REWIND                                                        */
    /*---------------------------------------------------------------*/
        /* Reset next block position to start of file */
        dev->nxtblkpos = 0;

        /* Set unit status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x0F:
    /*---------------------------------------------------------------*/
    /* REWIND UNLOAD                                                 */
    /*---------------------------------------------------------------*/
        /* Close the file and reset position to start */
        close (dev->fd);
        dev->fd = -1;
        dev->nxtblkpos = 0;

        /* Set unit status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x1F:
    /*---------------------------------------------------------------*/
    /* WRITE TAPE MARK                                               */
    /*---------------------------------------------------------------*/
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        break;

    case 0x27:
    /*---------------------------------------------------------------*/
    /* BACKSPACE BLOCK                                               */
    /*---------------------------------------------------------------*/
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        break;

    case 0x2F:
    /*---------------------------------------------------------------*/
    /* BACKSPACE FILE                                                */
    /*---------------------------------------------------------------*/
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        break;

    case 0x37:
    /*---------------------------------------------------------------*/
    /* FORWARD SPACE BLOCK                                           */
    /*---------------------------------------------------------------*/
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        break;

    case 0x3F:
    /*---------------------------------------------------------------*/
    /* FORWARD SPACE FILE                                            */
    /*---------------------------------------------------------------*/
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        break;

    case 0xCB: /* 9-track 800 bpi */
    case 0xC3: /* 9-track 1600 bpi */
    case 0xD3: /* 9-track 6250 bpi */
    /*---------------------------------------------------------------*/
    /* MODE SET                                                      */
    /*---------------------------------------------------------------*/
        *residual = 0;
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x04:
    /*---------------------------------------------------------------*/
    /* SENSE                                                         */
    /*---------------------------------------------------------------*/
        /* Calculate residual byte count */
        num = (count < dev->numsense) ? count : dev->numsense;
        *residual = count - num;
        if (count < dev->numsense) *more = 1;

        /* Copy device sense bytes to channel I/O buffer */
        memcpy (iobuf, dev->sense, num);

        /* Clear the device sense bytes */
        memset (dev->sense, 0, sizeof(dev->sense));

        /* Return unit status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    default:
    /*---------------------------------------------------------------*/
    /* INVALID OPERATION                                             */
    /*---------------------------------------------------------------*/
        /* Set command reject sense byte, and unit check status */
        dev->sense[0] = SENSE_CR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;

    } /* end switch(code) */

} /* end function simtape_execute_ccw */

