/* CTCADPT.C    (c) Copyright Roger Bowler, 2000                     */
/*              ESA/390 Channel-to-Channel Adapter Device Handler    */

/*-------------------------------------------------------------------*/
/* This module contains device handling functions for emulated       */
/* 3088 channel-to-channel adapter devices.                          */
/*-------------------------------------------------------------------*/
/* PLACEHOLDER MODULE ONLY! NO USABLE FUNCTION PROVIDED AT THIS TIME */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Definitions for 3088 model numbers                                */
/*-------------------------------------------------------------------*/
#define CTC_3088_01     0x308801        /* P/390 AWS3172 emulation   */
#define CTC_3088_04     0x308804        /* 3088 model 1 CTCA         */
#define CTC_3088_08     0x308808        /* 3088 model 2 CTCA         */
#define CTC_3088_1F     0x30881F        /* ESCON CTC channel         */
#define CTC_3088_60     0x308860        /* OSA/2 adapter             */

/*-------------------------------------------------------------------*/
/* Subroutine to trace the contents of a buffer                      */
/*-------------------------------------------------------------------*/
static void
packet_trace(BYTE *addr, int len)
{
unsigned int  i, offset;
unsigned char c;
unsigned char print_chars[17];

    for (offset=0; offset < len; )
    {
        memset(print_chars,0,sizeof(print_chars));
        logmsg("+%4.4X  ", offset);
        for (i=0; i < 16; i++)
        {
            c = *addr++;
            if (offset < len) {
                logmsg("%2.2X", c);
                print_chars[i] = '.';
                if (isprint(c)) print_chars[i] = c;
                c = ebcdic_to_ascii[c];
                if (isprint(c)) print_chars[i] = c;
            }
            else {
                logmsg("  ");
            }
            offset++;
            if ((offset & 3) == 0) {
                logmsg(" ");
            }
        } /* end for(i) */
        logmsg(" %s\n", print_chars);
    } /* end for(offset) */

} /* end function packet_trace */

/*-------------------------------------------------------------------*/
/* Initialize the device handler                                     */
/*-------------------------------------------------------------------*/
int ctcadpt_init_handler (DEVBLK *dev, int argc, BYTE *argv[])
{
U32             cutype;                 /* Control unit type         */

    /* Set the control unit type */
    cutype = CTC_3088_60;

    /* Initialize the device dependent fields */
    dev->ctcxmode = 0;

    /* Set number of sense bytes */
    dev->numsense = 1;

    /* Initialize the device identifier bytes */
    dev->devid[0] = 0xFF;
    dev->devid[1] = (cutype >> 16) & 0xFF;
    dev->devid[2] = (cutype >> 8) & 0xFF;
    dev->devid[3] = cutype & 0xFF;
    dev->devid[4] = dev->devtype >> 8;
    dev->devid[5] = dev->devtype & 0xFF;
    dev->devid[6] = 0x01;
    dev->numdevid = 7;

    /* Activate I/O tracing */
    dev->ccwtrace = 1;

    return 0;
} /* end function ctcadpt_init_handler */

/*-------------------------------------------------------------------*/
/* Query the device definition                                       */
/*-------------------------------------------------------------------*/
void ctcadpt_query_device (DEVBLK *dev, BYTE **class,
                int buflen, BYTE *buffer)
{

    *class = "CTCA";
    snprintf (buffer, buflen, "%s",
                dev->filename);

} /* end function ctcadpt_query_device */

/*-------------------------------------------------------------------*/
/* Close the device                                                  */
/*-------------------------------------------------------------------*/
int ctcadpt_close_device ( DEVBLK *dev )
{
    /* Close the device file */
    close (dev->fd);
    dev->fd = -1;

    return 0;
} /* end function ctcadpt_close_device */

/*-------------------------------------------------------------------*/
/* Execute a Channel Command Word                                    */
/*-------------------------------------------------------------------*/
void ctcadpt_execute_ccw (DEVBLK *dev, BYTE code, BYTE flags,
        BYTE chained, U16 count, BYTE prevcode, int ccwseq,
        BYTE *iobuf, BYTE *more, BYTE *unitstat, U16 *residual)
{
int             num;                    /* Number of bytes to move   */
BYTE            opcode;                 /* CCW opcode with modifier
                                           bits masked off           */

    /* Mask off the modifier bits in the CCW opcode */
    if ((code & 0x07) == 0x07)
        opcode = 0x07;
    else if ((code & 0x03) == 0x02)
        opcode = 0x02;
    else if ((code & 0x0F) == 0x0C)
        opcode = 0x0C;
    else if ((code & 0x03) == 0x01)
        opcode = dev->ctcxmode ? (code & 0x83) : 0x01;
    else if ((code & 0x1F) == 0x14)
        opcode = 0x14;
    else if ((code & 0x47) == 0x03)
        opcode = 0x03;
    else if ((code & 0xC7) == 0x43)
        opcode = 0x43;
    else
        opcode = code;

    /* Process depending on CCW opcode */
    switch (opcode) {

    case 0x01:
    /*---------------------------------------------------------------*/
    /* WRITE                                                         */
    /*---------------------------------------------------------------*/
        /* Calculate number of bytes to write and set residual count */
        num = count;
        *residual = count - num;

        /* Trace the contents of the I/O area */
        logmsg ("%4.4X: CTC Write Buffer\n", dev->devnum);
        packet_trace (iobuf, num);

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x81:
    /*---------------------------------------------------------------*/
    /* WRITE EOF                                                     */
    /*---------------------------------------------------------------*/
        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x02:
    /*---------------------------------------------------------------*/
    /* READ                                                          */
    /*---------------------------------------------------------------*/
        /* Set residual count */
        *residual = count;

        /* Return unit exception status */
        *unitstat = CSW_CE | CSW_DE | CSW_UX;
        break;

    case 0x07:
    /*---------------------------------------------------------------*/
    /* CONTROL                                                       */
    /*---------------------------------------------------------------*/
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x03:
    /*---------------------------------------------------------------*/
    /* CONTROL NO-OPERATION                                          */
    /*---------------------------------------------------------------*/
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x43:
    /*---------------------------------------------------------------*/
    /* SET BASIC MODE                                                */
    /*---------------------------------------------------------------*/
        /* Command reject if in basic mode */
        if (dev->ctcxmode == 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Reset extended mode and return normal status */
        dev->ctcxmode = 0;
        *residual = 0;
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0xC3:
    /*---------------------------------------------------------------*/
    /* SET EXTENDED MODE                                             */
    /*---------------------------------------------------------------*/
        dev->ctcxmode = 1;
        *residual = 0;
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0xE3:
    /*---------------------------------------------------------------*/
    /* PREPARE                                                       */
    /*---------------------------------------------------------------*/
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x14:
    /*---------------------------------------------------------------*/
    /* SENSE COMMAND BYTE                                            */
    /*---------------------------------------------------------------*/
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x04:
    /*---------------------------------------------------------------*/
    /* SENSE                                                         */
    /*---------------------------------------------------------------*/
        /* Command reject if in basic mode */
        if (dev->ctcxmode == 0)
        {
            dev->sense[0] = SENSE_CR;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

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

    case 0xE4:
    /*---------------------------------------------------------------*/
    /* SENSE ID                                                      */
    /*---------------------------------------------------------------*/
        /* Calculate residual byte count */
        num = (count < dev->numdevid) ? count : dev->numdevid;
        *residual = count - num;
        if (count < dev->numdevid) *more = 1;

        /* Copy device identifier bytes to channel I/O buffer */
        memcpy (iobuf, dev->devid, num);

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

} /* end function ctcadpt_execute_ccw */

