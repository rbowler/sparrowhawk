/* PRINTER.C    (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Line Printer Device Handler                  */

/*-------------------------------------------------------------------*/
/* This module contains device handling functions for emulated       */
/* System/370 line printer devices.                                  */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Internal macro definitions                                        */
/*-------------------------------------------------------------------*/
#define LINE_LENGTH     150
#define SPACE           ((BYTE)' ')

/*-------------------------------------------------------------------*/
/* Initialize the device handler                                     */
/*-------------------------------------------------------------------*/
int printer_init_handler ( DEVBLK *dev, int argc, BYTE *argv[] )
{

    /* The first argument is the file name */
    if (argc == 0 || strlen(argv[0]) > sizeof(dev->filename)-1)
    {
        printf ("HHC401I File name missing or invalid\n");
        return -1;
    }

    /* Save the file name in the device block */
    strcpy (dev->filename, argv[0]);

    /* Initialize device dependent fields */
    dev->fp = NULL;
    dev->printpos = 0;
    dev->printrem = LINE_LENGTH;
    dev->fold = 0;

    /* Set length of print buffer */
    dev->bufsize = LINE_LENGTH;

    /* Set number of sense bytes */
    dev->numsense = 1;

    /* Activate I/O tracing */
    dev->ccwtrace = 1;

    return 0;
} /* end function printer_init_handler */


/*-------------------------------------------------------------------*/
/* Execute a Channel Command Word                                    */
/*-------------------------------------------------------------------*/
void printer_execute_ccw ( DEVBLK *dev, BYTE code, BYTE flags,
        BYTE chained, U16 count, BYTE prevcode, int ccwseq,
        BYTE *iobuf, BYTE *more, BYTE *unitstat, U16 *residual )
{
int     i;                              /* Loop counter              */
int     num;                            /* Number of bytes to move   */
char   *eor;                            /* -> end of record string   */
BYTE    c;                              /* Print character           */

    /* Open the device file if necessary */
    if (dev->fp == NULL)
    {
        dev->fp = fopen (dev->filename, "w");
        if (dev->fp == NULL)
        {
            /* Handle open failure */
            perror("printer: open error");

            /* Set unit check with intervention required */
            dev->sense[0] = SENSE_IR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            return;
        }
    }

    /* Process depending on CCW opcode */
    switch (code) {

    case 0x01:
    /*---------------------------------------------------------------*/
    /* WRITE WITHOUT SPACING                                         */
    /*---------------------------------------------------------------*/
        eor = "\r";
        goto write;

    case 0x09:
    /*---------------------------------------------------------------*/
    /* WRITE AND SPACE 1 LINE                                        */
    /*---------------------------------------------------------------*/
        eor = "\r\n";
        goto write;

    case 0x11:
    /*---------------------------------------------------------------*/
    /* WRITE AND SPACE 2 LINES                                       */
    /*---------------------------------------------------------------*/
        eor = "\r\n\n";
        goto write;

    case 0x19:
    /*---------------------------------------------------------------*/
    /* WRITE AND SPACE 3 LINES                                       */
    /*---------------------------------------------------------------*/
        eor = "\r\n\n\n";
        goto write;

    case 0x89:
    /*---------------------------------------------------------------*/
    /* WRITE AND SKIP TO CHANNEL 1                                   */
    /*---------------------------------------------------------------*/
        eor = "\r\f";
        goto write;

    write:
        /* Start a new record if not data-chained from previous CCW */
        if ((chained & CCW_FLAGS_CD) == 0)
        {
            dev->printpos = 0;
            dev->printrem = LINE_LENGTH;

        } /* end if(!data-chained) */

        /* Calculate number of bytes to write and set residual count */
        num = (count < dev->printrem) ? count : dev->printrem;
        *residual = count - num;

        /* Copy data from channel buffer to print buffer */
        for (i = 0; i < num; i++)
        {
            c = ebcdic_to_ascii[iobuf[i]];

            if (dev->fold) c = toupper(c);
            if (c == 0) c = SPACE;

            dev->buf[dev->printpos] = c;
            dev->printpos++;
            dev->printrem--;
        } /* end for(i) */

        /* Perform end of record processing if not data-chaining */
        if ((flags & CCW_FLAGS_CD) == 0)
        {
            /* Truncate trailing blanks from print line */
            for (i = dev->printpos; i > 0; i--)
                if (dev->buf[i-1] != SPACE) break;

            /* Write print line */
            fwrite (dev->buf, i, 1, dev->fp);

            /* Write carriage return and line feed(s) */
            fprintf (dev->fp, "%s", eor);

        } /* end if(!data-chaining) */

        /* Equipment check if error occurred writing to printer file */
        if (ferror(dev->fp))
        {
            printf ("printer: error writing to %s: %s\n",
                    dev->filename, strerror(errno));
            dev->sense[0] = SENSE_EC;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x03:
    /*---------------------------------------------------------------*/
    /* CONTROL NO-OPERATION                                          */
    /*---------------------------------------------------------------*/
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x0B:
    /*---------------------------------------------------------------*/
    /* SPACE 1 LINE IMMEDIATE                                        */
    /*---------------------------------------------------------------*/
        fprintf (dev->fp, "\r\n");
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x13:
    /*---------------------------------------------------------------*/
    /* SPACE 2 LINES IMMEDIATE                                       */
    /*---------------------------------------------------------------*/
        fprintf (dev->fp, "\r\n\n");
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x1B:
    /*---------------------------------------------------------------*/
    /* SPACE 3 LINES IMMEDIATE                                       */
    /*---------------------------------------------------------------*/
        fprintf (dev->fp, "\r\n\n\n");
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x23:
    /*---------------------------------------------------------------*/
    /* UNFOLD                                                        */
    /*---------------------------------------------------------------*/
        dev->fold = 0;
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x43:
    /*---------------------------------------------------------------*/
    /* FOLD                                                          */
    /*---------------------------------------------------------------*/
        dev->fold = 1;
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x73:
    /*---------------------------------------------------------------*/
    /* BLOCK DATA CHECK                                              */
    /*---------------------------------------------------------------*/
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x7B:
    /*---------------------------------------------------------------*/
    /* ALLOW DATA CHECK                                              */
    /*---------------------------------------------------------------*/
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x8B:
    /*---------------------------------------------------------------*/
    /* SKIP TO CHANNEL 1 IMMEDIATE                                   */
    /*---------------------------------------------------------------*/
        fprintf (dev->fp, "\r\f");
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0xEB:
    /*---------------------------------------------------------------*/
    /* UCS GATE LOAD                                                 */
    /*---------------------------------------------------------------*/
        /* Command reject if not first command in chain */
        if (chained != 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0xF3:
    /*---------------------------------------------------------------*/
    /* LOAD UCS BUFFER AND FOLD                                      */
    /*---------------------------------------------------------------*/
        /* Command reject if not chained to UCS GATE command */
        if (prevcode != 0xEB)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Set fold indicator and return normal status */
        dev->fold = 1;
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0xFB:
    /*---------------------------------------------------------------*/
    /* LOAD UCS BUFFER (NO FOLD)                                     */
    /*---------------------------------------------------------------*/
        /* Command reject if not chained to UCS GATE command */
        if (prevcode != 0xEB)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Reset fold indicator and return normal status */
        dev->fold = 0;
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

} /* end function printer_execute_ccw */

