/* CTCADPT.C    (c) Copyright Roger Bowler, 2000                     */
/*              ESA/390 Channel-to-Channel Adapter Device Handler    */
/* vmnet modifications (c) Copyright Willem Konynenberg, 2000        */

/*-------------------------------------------------------------------*/
/* This module contains device handling functions for emulated       */
/* 3088 channel-to-channel adapter devices.                          */
/*-------------------------------------------------------------------*/
/* Implements virtual CTC networking with an external vmnet program  */
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
/* Definitions for SLIP encapsulation                                */
/*-------------------------------------------------------------------*/
#define SLIP_END        0300
#define SLIP_ESC        0333
#define SLIP_ESC_END    0334
#define SLIP_ESC_ESC    0335


#if 0
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
#endif

void init_vmnet(DEVBLK *dev, DEVBLK *xdev, int argc, BYTE *argv[])
{
int sockfd[2];
int r, i;
BYTE *ipaddress;

    if (argc < 2) {
        logmsg ("%4.4X: Not enough arguments to start vmnet\n",
                        dev->devnum);
        return;
    }

    ipaddress = argv[0];
    argc--;
    argv++;

    if (socketpair (AF_UNIX, SOCK_STREAM, 0, sockfd) < 0) {
        logmsg ("%4.4X: Failed: socketpair: %s\n",
                        dev->devnum, strerror(errno));
        return;
    }

    r = fork ();

    if (r < 0) {
        logmsg ("%4.4X: Failed: fork: %s\n",
                        dev->devnum, strerror(errno));
        return;
    } else if (r == 0) {
        /* child */
        close (0);
        close (1);
        dup (sockfd[1]);
        dup (sockfd[1]);
        r = (sockfd[0] > sockfd[1]) ? sockfd[0] : sockfd[1];
        for (i = 3; i <= r; i++) {
            close (i);
        }

	/* the ugly cast is to silence a compiler warning due to const */
        execv (argv[0], (char *const *)argv);

        exit (1);
    }

    close (sockfd[1]);
    dev->fd = sockfd[0];
    xdev->fd = sockfd[0];

    /* We just blindly copy these out in the hope vmnet will pick them
     * up correctly.  I don't feel like implementing a complete login
     * scripting facility here...
     */
    write(dev->fd, ipaddress, strlen(ipaddress));
    write(dev->fd, "\n", 1);
}

static int write_vmnet(DEVBLK *dev, BYTE *iobuf, U16 count, BYTE *unitstat)
{
int blklen = (iobuf[0]<<8) | iobuf[1];
int pktlen;
BYTE *p = iobuf + 2;
BYTE *buffer = dev->buf;
int len = 0, rem;

    if (count < blklen) {
        logmsg ("%4.4X: bad block length: %d < %d\n",
                dev->devnum, count, blklen);
        blklen = count;
    }
    while (p < iobuf + blklen) {
        pktlen = (p[0]<<8) | p[1];

        rem = iobuf + blklen - p;

        if (rem < pktlen) {
            logmsg ("%4.4X: bad packet length: %d < %d\n",
                    dev->devnum, rem, pktlen);
            pktlen = rem;
        }
        if (pktlen < 6) {
        logmsg ("%4.4X: bad packet length: %d < 6\n",
                    dev->devnum, pktlen);
            pktlen = 6;
        }

        pktlen -= 6;
        p += 6;

        while (pktlen--) {
            switch (*p) {
            case SLIP_END:
                buffer[len++] = SLIP_ESC;
                buffer[len++] = SLIP_ESC_END;
                break;
            case SLIP_ESC:
                buffer[len++] = SLIP_ESC;
                buffer[len++] = SLIP_ESC_ESC;
                break;
            default:
                buffer[len++] = *p;
                break;
            }
            p++;
        }
        buffer[len++] = SLIP_END;
        write(dev->fd, buffer, len);   /* should check error conditions? */
        len = 0;
    }

    *unitstat = CSW_CE | CSW_DE;

    return count;
}

static int bufgetc(DEVBLK *dev, int blocking)
{
BYTE *bufp = dev->buf + dev->ctcpos, *bufend = bufp + dev->ctcrem;
int n;

    if (bufp >= bufend) {
        if (blocking == 0) return -1;
        do {
            n = read(dev->fd, dev->buf, dev->bufsize);
            if (n <= 0) {
                if (n == 0) {
                    /* VMnet died on us. */
                    logmsg ("%4.4X: Error: EOF on read, CTC network down\n",
                            dev->devnum);
                    /* -2 will cause an error status to be set */
                    return -2;
                }
                logmsg ("%4.4X: Error: read: %s\n",
                        dev->devnum, strerror(errno));
                sleep(2);
            }
        } while (n <= 0);
        dev->ctcrem = n;
        bufend = &dev->buf[n];
        dev->ctclastpos = dev->ctclastrem = dev->ctcpos = 0;
        bufp = dev->buf;
    }

    dev->ctcpos++;
    dev->ctcrem--;

    return *bufp;
}

static void setblkheader(BYTE *iobuf, int buflen)
{
    iobuf[0] = (buflen >> 8) & 0xFF;
    iobuf[1] = buflen & 0xFF;
}

static void setpktheader(BYTE *iobuf, int packetpos, int packetlen)
{
    iobuf[packetpos] = (packetlen >> 8) & 0xFF;
    iobuf[packetpos+1] = packetlen & 0xFF;
    iobuf[packetpos+2] = 0x08;
    iobuf[packetpos+3] = 0;
    iobuf[packetpos+4] = 0;
    iobuf[packetpos+5] = 0;
}

/* read data from the CTC connection.
 * If a packet overflows the iobuf or the read buffer runs out, there are
 * 2 possibilities:
 * - block has single packet: continue reading packet, drop bytes,
 *   then return truncated packet.
 * - block has multiple packets: back up on last packet and return
 *   what we have.  Do this last packet in the next IO.
 */  
static int read_vmnet(DEVBLK *dev, BYTE *iobuf, U16 count, BYTE *unitstat)
{
int             c;                      /* next byte to process      */
int             len = 8;                /* length of block           */
int             lastlen = 2;            /* block length at last pckt */

    dev->ctclastpos = dev->ctcpos;
    dev->ctclastrem = dev->ctcrem;

    while (1) {
        c = bufgetc(dev, lastlen == 2);
        if (c < 0) {
            /* End of input buffer.  Return what we have. */

            setblkheader (iobuf, lastlen);

            dev->ctcpos = dev->ctclastpos;
            dev->ctcrem = dev->ctclastrem;

            *unitstat = CSW_CE | CSW_DE | (c == -2 ? CSW_UX : 0);

            return lastlen;
        }
        switch (c) {
        case SLIP_END:
            if (len > 8) {
                /* End of packet.  Set up for next. */

                setpktheader (iobuf, lastlen, len-lastlen);

                dev->ctclastpos = dev->ctcpos;
                dev->ctclastrem = dev->ctcrem;
                lastlen = len;

                len += 6;
            }
            break;
        case SLIP_ESC:
            c = bufgetc(dev, lastlen == 2);
            if (c < 0) {
                /* End of input buffer.  Return what we have. */

                setblkheader (iobuf, lastlen);

                dev->ctcpos = dev->ctclastpos;
                dev->ctcrem = dev->ctclastrem;

                *unitstat = CSW_CE | CSW_DE | (c == -2 ? CSW_UX : 0);

                return lastlen;
            }
            switch (c) {
            case SLIP_ESC_END:
                c = SLIP_END;
                break;
            case SLIP_ESC_ESC:
                c = SLIP_ESC;
                break;
            }
            /* FALLTHRU */
        default:
            if (len < count) {
                iobuf[len++] = c;
            } else if (lastlen > 2) {
                /* IO buffer is full and we have data to return */

                setblkheader (iobuf, lastlen);

                dev->ctcpos = dev->ctclastpos;
                dev->ctcrem = dev->ctclastrem;

                *unitstat = CSW_CE | CSW_DE | (c == -2 ? CSW_UX : 0);

                return lastlen;
            } /* else truncate end of very large single packet... */
        }
    }
}

/*-------------------------------------------------------------------*/
/* Initialize the device handler                                     */
/*-------------------------------------------------------------------*/
int ctcadpt_init_handler (DEVBLK *dev, int argc, BYTE *argv[])
{
U32             cutype;                 /* Control unit type         */
U16             xdevnum;                /* Pair device devnum        */
BYTE            c;                      /* tmp for scanf             */
DEVBLK          *xdev;                  /* Pair device               */

    /* parameters for network CTC are:
     *    devnum of the other CTC device of the pair
     *    ipaddress
     *    vmnet command line
     *
     * CTC adapters are used in pairs, one for READ, one for WRITE.
     * The vmnet is only initialised when both are initialised.
     */
    if (argc < 3) {
        logmsg("%4.4X: Not enough parameters\n", dev->devnum);
        return -1;
    }
    if (strlen(argv[0]) > 4
        || sscanf(argv[0], "%hx%c", &xdevnum, &c) != 1) {
        logmsg("%4.4X: Bad device number '%s'\n", dev->devnum, argv[0]);
        return -1;
    }
    xdev = find_device_by_devnum(xdevnum);
    if (xdev != NULL) {
        init_vmnet(dev, xdev, argc - 1, &argv[1]);
    }
    strcpy(dev->filename, "vmnet");

    /* Set the control unit type */
    /* Linux/390 currently only supports 3088 model 2 CTCA and ESCON */
    cutype = CTC_3088_08;

    /* Initialize the device dependent fields */
    dev->ctcxmode = 0;
    dev->ctcpos = 0;
    dev->ctcrem = 0;

    /* Set length of buffer */
    /* This size guarantees we can write a full iobuf of 65536
     * as a SLIP packet in a single write.  Probably overkill... */
    dev->bufsize = 65536 * 2 + 1;

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
    dev->ccwtrace = 0;

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
        *residual = count - write_vmnet(dev, iobuf, count, unitstat);
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

        *residual = count - read_vmnet(dev, iobuf, count, unitstat);
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

