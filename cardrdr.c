/* CARDRDR.C    (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Card Reader Device Handler                   */

/*-------------------------------------------------------------------*/
/* This module contains device handling functions for emulated       */
/* card reader devices.                                              */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Internal macro definitions                                        */
/*-------------------------------------------------------------------*/
#define CARD_SIZE       80
#define HEX40           ((BYTE)0x40)

/*-------------------------------------------------------------------*/
/* Initialize the device handler                                     */
/*-------------------------------------------------------------------*/
int cardrdr_init_handler ( DEVBLK *dev, int argc, BYTE *argv[] )
{
int     i;                              /* Array subscript           */

    /* The first argument is the file name */
    if (argc == 0 || strlen(argv[0]) > sizeof(dev->filename)-1)
    {
        printf ("HHC401I File name missing or invalid\n");
        return -1;
    }

    /* Save the file name in the device block */
    strcpy (dev->filename, argv[0]);

    /* Initialize device dependent fields */
    dev->fd = -1;
    dev->ascii = 0;
    dev->trunc = 0;
    dev->cardpos = 0;
    dev->cardrem = 0;

    /* Process the driver arguments */
    for (i = 1; i < argc; i++)
    {
        /* ascii means that the card image file consists of
           variable length ASCII records delimited by either
           line-feed or carriage-return line-feed sequences.
           The default format is fixed length 80-byte EBCDIC
           card images with no line-end delimiters. */

        if (strcmp(argv[i], "ascii") == 0)
        {
            dev->ascii = 1;
            continue;
        }

        /* trunc means that records longer than 80 bytes will
           be silently truncated to 80 bytes when processing a
           variable length ASCII file.  The default behaviour
           is to present a data check if an overlength record
           is encountered.  The trunc option is ignored unless
           the ascii option is also specified. */

        if (strcmp(argv[i], "trunc") == 0)
        {
            dev->trunc = 1;
            continue;
        }
        printf ("HHC402I Invalid argument: %s\n",
                argv[i]);
        return -1;
    }

    /* Set length of card image buffer */
    dev->bufsize = CARD_SIZE;

    /* Set number of sense bytes */
    dev->numsense = 1;

    /* Initialize the device identifier bytes */
    dev->devid[0] = 0xFF;
    dev->devid[1] = 0x28; /* Control unit type is 2821-1 */
    dev->devid[2] = 0x21;
    dev->devid[3] = 0x01;
    dev->devid[4] = dev->devtype >> 8;
    dev->devid[5] = dev->devtype & 0xFF;
    dev->devid[6] = 0x01;
    dev->numdevid = 7;

    /* Activate I/O tracing */
    dev->ccwtrace = 1;

    return 0;
} /* end function cardrdr_init_handler */


/*-------------------------------------------------------------------*/
/* Read an 80-byte EBCDIC card image into the device buffer          */
/*-------------------------------------------------------------------*/
static int read_ebcdic ( DEVBLK *dev, BYTE *unitstat )
{
int     rc;                             /* Return code               */

    /* Read 80 bytes of card image data into the device buffer */
    rc = read (dev->fd, dev->buf, CARD_SIZE);

    /* Handle end-of-file condition */
    if (rc == 0)
    {
        close (dev->fd);
        *unitstat = CSW_CE | CSW_DE | CSW_UX;
        return -1;
    }

    /* Handle read error condition */
    if (rc < CARD_SIZE)
    {
        if (rc < 0)
            perror("cardrdr: read error");
        else
            printf("cardrdr: unexpected end of file\n");

        /* Set unit check with equipment check */
        dev->sense[0] = SENSE_EC;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return -1;
    }

    return 0;
} /* end function read_ebcdic */


/*-------------------------------------------------------------------*/
/* Read a variable length ASCII card image into the device buffer    */
/*-------------------------------------------------------------------*/
static int read_ascii ( DEVBLK *dev, BYTE *unitstat )
{
int     rc;                             /* Return code               */
int     i;                              /* Array subscript           */
BYTE    c;                              /* Input character           */

    /* Prefill the card image with EBCDIC blanks */
    memset (dev->buf, HEX40, CARD_SIZE);

    /* Read up to 80 bytes into device buffer */
    for (i = 0; ; )
    {
        /* Read next byte of card image */
        rc = read (dev->fd, &c, 1);

        /* Handle end-of-file condition */
        if (rc == 0)
        {
            /* End of record if there is any data in buffer */
            if (i > 0) break;

            /* Set unit exception to signal end of file */
            close (dev->fd);
            *unitstat = CSW_CE | CSW_DE | CSW_UX;
            return -1;
        }

        /* Handle read error condition */
        if (rc < 0)
        {
            perror("cardrdr: read error");

            /* Set unit check with equipment check */
            dev->sense[0] = SENSE_EC;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return -1;
        }

        /* Ignore carriage return */
        if (c == '\r') continue;

        /* Line-feed indicates end of variable length record */
        if (c == '\n') break;

        /* Test for overlength record */
        if (i >= CARD_SIZE)
        {
            /* Ignore excess characters if trunc option specified */
            if (dev->trunc) continue;

            /* Set unit check with data check */
            dev->sense[0] = SENSE_DC;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return -1;
        }

        /* Convert character to EBCDIC and store in device buffer */
        dev->buf[i++] = ascii_to_ebcdic[c];

    } /* end for(i) */

    return 0;
} /* end function read_ascii */


/*-------------------------------------------------------------------*/
/* Execute a Channel Command Word                                    */
/*-------------------------------------------------------------------*/
void cardrdr_execute_ccw ( DEVBLK *dev, BYTE code, BYTE flags,
        BYTE chained, U16 count, BYTE prevcode, BYTE *iobuf,
        BYTE *more, BYTE *unitstat, U16 *residual )
{
int     rc;                             /* Return code               */
int     num;                            /* Number of bytes to move   */

    /* Open the device file if necessary */
    if (dev->fd < 0)
    {
        rc = open (dev->filename, O_RDONLY);
        if (rc < 0)
        {
            /* Handle open failure */
            perror("cardrdr: open error");

            /* Set unit check with intervention required */
            dev->sense[0] = SENSE_IR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return;
        }
        dev->fd = rc;
    }

    /* Process depending on CCW opcode */
    switch (code) {

    case 0x02:
    /*---------------------------------------------------------------*/
    /* READ                                                          */
    /*---------------------------------------------------------------*/

        /* Read next card if not data-chained from previous CCW */
        if ((chained & CCW_FLAGS_CD) == 0)
        {
            /* Read ASCII or EBCDIC card image */
            if (dev->ascii)
                rc = read_ascii (dev, unitstat);
            else
                rc = read_ebcdic (dev, unitstat);

            /* Return error status if read was unsuccessful */
            if (rc) break;

            /* Initialize number of bytes in current card */
            dev->cardpos = 0;
            dev->cardrem = CARD_SIZE;

        } /* end if(!data-chained) */

        /* Calculate number of bytes to read and set residual count */
        num = (count < dev->cardrem) ? count : dev->cardrem;
        *residual = count - num;
        if (count < dev->cardrem) *more = 1;

        /* Copy data from card image buffer into channel buffer */
        memcpy (iobuf, dev->buf + dev->cardpos, num);

        /* Update number of bytes remaining in card image buffer */
        dev->cardpos += num;
        dev->cardrem -= num;

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x03:
    /*---------------------------------------------------------------*/
    /* CONTROL NO-OPERATION                                          */
    /*---------------------------------------------------------------*/
        *residual = 0;
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

} /* end function cardrdr_execute_ccw */

