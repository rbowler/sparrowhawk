/* CONSOLE.C    (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Console Printer-Keyboard Device Handler      */

/*-------------------------------------------------------------------*/
/* This module contains device handling functions for emulated       */
/* System/370 console printer-keyboard (1052 and 3215) devices.      */
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
int console_init_handler ( DEVBLK *dev, int argc, BYTE *argv[] )
{

    /* Initialize device dependent fields */
    dev->printpos = 0;
    dev->printrem = LINE_LENGTH;
    dev->keybdrem = 0;

    /* Set length of print buffer */
    dev->bufsize = LINE_LENGTH;

    /* Set number of sense bytes */
    dev->numsense = 1;

    /* Initialize the device identifier bytes */
    dev->devid[0] = 0xFF;
    dev->devid[1] = dev->devtype >> 8;
    dev->devid[2] = dev->devtype & 0xFF;
    dev->devid[3] = 0x01;
    dev->devid[4] = dev->devtype >> 8;
    dev->devid[5] = dev->devtype & 0xFF;
    dev->devid[6] = 0x01;
    dev->numdevid = 7;

    /* Activate I/O tracing */
//  dev->ccwtrace = 1;

    return 0;
} /* end function console_init_handler */


/*-------------------------------------------------------------------*/
/* Execute a Channel Command Word                                    */
/*-------------------------------------------------------------------*/
void console_execute_ccw ( DEVBLK *dev, BYTE code, BYTE flags,
        BYTE chained, U16 count, BYTE prevcode, BYTE *iobuf,
        BYTE *more, BYTE *unitstat, U16 *residual )
{
int     i;                              /* Loop counter              */
int     len;                            /* Length of input line      */
int     num;                            /* Number of bytes to move   */
BYTE    c;                              /* Print character           */

    /* Process depending on CCW opcode */
    switch (code) {

    case 0x01:
    /*---------------------------------------------------------------*/
    /* WRITE NO CARRIER RETURN                                       */
    /*---------------------------------------------------------------*/

    case 0x09:
    /*---------------------------------------------------------------*/
    /* WRITE AUTO CARRIER RETURN                                     */
    /*---------------------------------------------------------------*/

        /* Start a new line if not data-chained from previous CCW */
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
            if (!isprint(c)) c = SPACE;
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
            fwrite (dev->buf, i, 1, stdout);

            /* Write newline if required */
            if (code == 0x09)
                printf ("\n");

        } /* end if(!data-chaining) */

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x03:
    /*---------------------------------------------------------------*/
    /* CONTROL NO-OPERATION                                          */
    /*---------------------------------------------------------------*/
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x0A:
    /*---------------------------------------------------------------*/
    /* READ INQUIRY                                                  */
    /*---------------------------------------------------------------*/

        /* Solicit console input if no data in the device buffer */
        if (dev->keybdrem == 0)
        {
            /* Display prompting message on console */
            printf ("HHC901I Enter input for console device %4.4X\n",
                    dev->devnum);

            /* Read keyboard input line from console */
            fgets (dev->buf, LINE_LENGTH, stdin);
            len = strlen(dev->buf);
            if (dev->buf[len-1] == '\n')
                dev->buf[len-1] = '\r';

            /* Translate input line to EBCDIC */
            for (i = 0; dev->buf[i] != '\0'; i++)
                dev->buf[i] = ascii_to_ebcdic[dev->buf[i]];
        }
        else
        {
            /* Use data remaining in device buffer */
            len = dev->keybdrem;
        }

        /* Calculate number of bytes to move and residual byte count */
        num = (count < len) ? count : len;
        *residual = count - num;
        if (count < len) *more = 1;

        /* Copy data from device buffer to channel buffer */
        memcpy (iobuf, dev->buf, num);

        /* If data chaining is specified, save remaining data */
        if ((flags & CCW_FLAGS_CD) && len > count)
        {
            memcpy (dev->buf, dev->buf + count, len - count);
            dev->keybdrem = len - count;
        }
        else
        {
            dev->keybdrem = 0;
        }

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x0B:
    /*---------------------------------------------------------------*/
    /* AUDIBLE ALARM                                                 */
    /*---------------------------------------------------------------*/
        printf ("\b");
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

} /* end function console_execute_ccw */

