/* CONSOLE.C    (c)Copyright Roger Bowler, 1999                      */
/*              ESA/390 Console Device Handler                       */

/*-------------------------------------------------------------------*/
/* This module contains device handling functions for console        */
/* devices for the Hercules ESA/390 emulator.                        */
/*                                                                   */
/* Telnet support is provided for two classes of console device:     */
/* - local non-SNA 3270 display consoles via tn3270                  */
/* - 1052 and 3215 console printer keyboards via regular telnet      */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Telnet command definitions                                        */
/*-------------------------------------------------------------------*/
#define BINARY          0       /* Binary Transmission */
#define IS              0       /* Used by terminal-type negotiation */
#define SEND            1       /* Used by terminal-type negotiation */
#define ECHO_OPTION     1       /* Echo option */
#define SUPPRESS_GA     3       /* Suppress go-ahead option */
#define TIMING_MARK     6       /* Timing mark option */
#define TERMINAL_TYPE   24      /* Terminal type option */
#define EOR             25      /* End of record option */
#define EOR_MARK        239     /* End of record marker */
#define SE              240     /* End of subnegotiation parameters */
#define NOP             241     /* No operation */
#define DATA_MARK       242     /* The data stream portion of a Synch.
                                   This should always be accompanied
                                   by a TCP Urgent notification */
#define BRK             243     /* Break character */
#define IP              244     /* Interrupt Process */
#define AO              245     /* Abort Output */
#define AYT             246     /* Are You There */
#define EC              247     /* Erase character */
#define EL              248     /* Erase Line */
#define GA              249     /* Go ahead */
#define SB              250     /* Subnegotiation of indicated option */
#define WILL            251     /* Indicates the desire to begin
                                   performing, or confirmation that
                                   you are now performing, the
                                   indicated option */
#define WONT            252     /* Indicates the refusal to perform,
                                   or continue performing, the
                                   indicated option */
#define DO              253     /* Indicates the request that the
                                   other party perform, or
                                   confirmation that you are expecting
                                   the other party to perform, the
                                   indicated option */
#define DONT            254     /* Indicates the demand that the
                                   other party stop performing,
                                   or confirmation that you are no
                                   longer expecting the other party
                                   to perform, the indicated option */
#define IAC             255     /* Interpret as Command */

/*-------------------------------------------------------------------*/
/* Internal macro definitions                                        */
/*-------------------------------------------------------------------*/
#define TNSDEBUG(lvl,format,a...) \
        if(debug>=lvl)logmsg("console: " format, ## a)
#define TNSERROR(format,a...) \
        logmsg("console: " format, ## a)
#define BUFLEN_3270     4096
#define LINE_LENGTH     150
#define SPACE           ((BYTE)' ')


/*-------------------------------------------------------------------*/
/* Static data areas                                                 */
/*-------------------------------------------------------------------*/
static unsigned int debug = 1;          /* Debug level: 1=status,
                                           2=headers, 3=buffers      */
static struct utsname hostinfo;         /* Host info for this system */


/*-------------------------------------------------------------------*/
/* SUBROUTINE TO TRACE THE CONTENTS OF AN ASCII MESSAGE PACKET       */
/*-------------------------------------------------------------------*/
static void
packet_trace(BYTE *addr, int len)
{
unsigned int  i, offset;
unsigned char c;
unsigned char print_chars[17];

    if (debug < 3) return;

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
/* SUBROUTINE TO REMOVE ANY IAC SEQUENCES FROM THE DATA STREAM       */
/* Returns the new length after deleting IAC commands                */
/*-------------------------------------------------------------------*/
static int
remove_iac (BYTE *buf, int len)
{
int     m, n, c;

    for (m=0, n=0; m < len; ) {
        /* Interpret IAC commands */
        if (buf[m] == IAC) {
            /* Treat IAC in last byte of buffer as IAC NOP */
            c = (++m < len)? buf[m++] : NOP;
            /* Process IAC command */
            switch (c) {
            case IAC: /* Insert single IAC in buffer */
                buf[n++] = IAC;
                break;
            case BRK: /* Set ATTN indicator */
                break;
            case IP: /* Set SYSREQ indicator */
                break;
            case WILL: /* Skip option negotiation command */
            case WONT:
            case DO:
            case DONT:
                m++;
                break;
            case SB: /* Skip until IAC SE sequence found */
                for (; m < len; m++) {
                    if (buf[m] != IAC) continue;
                    if (++m >= len) break;
                    if (buf[m] == SE) { m++; break; }
                } /* end for */
            default: /* Ignore NOP or unknown command */
                break;
            } /* end switch(c) */
        } else {
            /* Copy data bytes */
            if (n < m) buf[n] = buf[m];
            m++; n++;
        }
    } /* end for */

    if (n < m) {
        TNSDEBUG(3, "%d IAC bytes removed, newlen=%d\n", m-n, n);
        packet_trace (buf, n);
    }

    return n;

} /* end function remove_iac */


/*-------------------------------------------------------------------*/
/* SUBROUTINE TO DOUBLE UP ANY IAC BYTES IN THE DATA STREAM          */
/* Returns the new length after inserting extra IAC bytes            */
/*-------------------------------------------------------------------*/
static int
double_up_iac (BYTE *buf, int len)
{
int     m, n, x, newlen;

    /* Count the number of IAC bytes in the data */
    for (x=0, n=0; n < len; n++)
        if (buf[n] == IAC) x++;

    /* Exit if nothing to do */
    if (x == 0) return len;

    /* Insert extra IAC bytes backwards from the end of the buffer */
    newlen = len + x;
    TNSDEBUG(3, "%d IAC bytes added, newlen=%d\n", x, newlen);
    for (n=newlen, m=len; n > m; ) {
        buf[--n] = buf[--m];
        if (buf[n] == IAC) buf[--n] = IAC;
    }
    packet_trace (buf, newlen);
    return newlen;

} /* end function double_up_iac */


/*-------------------------------------------------------------------*/
/* SUBROUTINE TO TRANSLATE A NULL-TERMINATED STRING TO EBCDIC        */
/*-------------------------------------------------------------------*/
static BYTE *
translate_to_ebcdic (BYTE *str)
{
int     i;                              /* Array subscript           */
BYTE    c;                              /* Character work area       */

    for (i = 0; str[i] != '\0'; i++)
    {
        c = str[i];
        str[i] = (isprint(c) ? ascii_to_ebcdic[c] : SPACE);
    }

    return str;
} /* end function translate_to_ebcdic */


/*-------------------------------------------------------------------*/
/* SUBROUTINE TO SEND A DATA PACKET TO THE CLIENT                    */
/*-------------------------------------------------------------------*/
static int
send_packet (int csock, BYTE *buf, int len, char *caption)
{
int     rc;                             /* Return code               */

    if (caption != NULL) {
        TNSDEBUG(2, "Sending %s\n", caption);
        packet_trace (buf, len);
    }

    rc = send (csock, buf, len, 0);

    if (rc < 0) {
        TNSERROR("send: %s\n", strerror(errno));
        return -1;
    } /* end if(rc) */

    return 0;

} /* end function send_packet */


/*-------------------------------------------------------------------*/
/* SUBROUTINE TO RECEIVE A DATA PACKET FROM THE CLIENT               */
/* This subroutine receives bytes from the client.  It stops when    */
/* the receive buffer is full, or when the last two bytes received   */
/* consist of the IAC character followed by a specified delimiter.   */
/* If zero bytes are received, this means the client has closed the  */
/* connection, and this is treated as an error.                      */
/* Input:                                                            */
/*      csock is the socket number                                   */
/*      buf points to area to receive data                           */
/*      reqlen is the number of bytes requested                      */
/*      delim is the delimiter character (0=no delimiter)            */
/* Output:                                                           */
/*      buf is updated with data received                            */
/*      The return value is the number of bytes received, or         */
/*      -1 if an error occurred.                                     */
/*-------------------------------------------------------------------*/
static int
recv_packet (int csock, BYTE *buf, int reqlen, BYTE delim)
{
int     rc=0;                           /* Return code               */
int     rcvlen=0;                       /* Length of data received   */

    while (rcvlen < reqlen) {

        rc = recv (csock, buf + rcvlen, reqlen - rcvlen, 0);

        if (rc < 0) {
            TNSERROR("recv: %s\n", strerror(errno));
            return -1;
        }

        if (rc == 0) {
            TNSDEBUG(1, "Connection closed by client\n");
            return -1;
        }

        rcvlen += rc;

        if (delim != '\0' && rcvlen >= 2
            && buf[rcvlen-2] == IAC && buf[rcvlen-1] == delim)
            break;
    }

    TNSDEBUG(2, "Packet received length=%d\n", rcvlen);
    packet_trace (buf, rcvlen);

    return rcvlen;

} /* end function recv_packet */


/*-------------------------------------------------------------------*/
/* SUBROUTINE TO RECEIVE A PACKET AND COMPARE WITH EXPECTED VALUE    */
/*-------------------------------------------------------------------*/
static int
expect (int csock, BYTE *expected, int len, char *caption)
{
int     rc;                             /* Return code               */
BYTE    buf[512];                       /* Receive buffer            */

    rc = recv_packet (csock, buf, len, 0);
    if (rc < 0) return -1;

    if (memcmp(buf, expected, len) != 0) {
        TNSDEBUG(2, "Expected %s\n", caption);
        return -1;
    }
    TNSDEBUG(2, "Received %s\n", caption);

    return 0;

} /* end function expect */


/*-------------------------------------------------------------------*/
/* SUBROUTINE TO NEGOTIATE TELNET PARAMETERS                         */
/* This subroutine negotiates the terminal type with the client      */
/* and uses the terminal type to determine whether the client        */
/* is to be supported as a 3270 display console or as a 1052/3215    */
/* printer-keyboard console.                                         */
/*                                                                   */
/* Valid display terminal types are "IBM-NNNN", "IBM-NNNN-M", and    */
/* "IBM-NNNN-M-E", where NNNN is 3270, 3277, 3278, 3279, 3178, 3179, */
/* or 3180, M indicates the screen size (2=25x80, 3=32x80, 4=43x80,  */
/* 5=27x132, X=determined by Read Partition Query command), and      */
/* -E is an optional suffix indicating that the terminal supports    */
/* extended attributes. Displays are negotiated into tn3270 mode.    */
/*                                                                   */
/* Terminal types whose first four characters are not "IBM-" are     */
/* handled as printer-keyboard consoles using telnet line mode.      */
/*                                                                   */
/* Input:                                                            */
/*      csock   Socket number for client connection                  */
/* Output:                                                           */
/*      class   D=3270 display console, K=printer-keyboard console   */
/*      model   3270 model indicator (2,3,4,5,X)                     */
/*      extatr  3270 extended attributes (Y,N)                       */
/* Return value:                                                     */
/*      0=negotiation successful, -1=negotiation error               */
/*-------------------------------------------------------------------*/
static int
negotiate(int csock, BYTE *class, BYTE *model, BYTE *extatr)
{
int    rc;                              /* Return code               */
BYTE  *termtype;                        /* Pointer to terminal type  */
BYTE   buf[512];                        /* Telnet negotiation buffer */
static BYTE do_term[] = { IAC, DO, TERMINAL_TYPE };
static BYTE will_term[] = { IAC, WILL, TERMINAL_TYPE };
static BYTE req_type[] = { IAC, SB, TERMINAL_TYPE, SEND, IAC, SE };
static BYTE type_is[] = { IAC, SB, TERMINAL_TYPE, IS };
static BYTE do_eor[] = { IAC, DO, EOR, IAC, WILL, EOR };
static BYTE will_eor[] = { IAC, WILL, EOR, IAC, DO, EOR };
static BYTE do_bin[] = { IAC, DO, BINARY, IAC, WILL, BINARY };
static BYTE will_bin[] = { IAC, WILL, BINARY, IAC, DO, BINARY };
#if 0
static BYTE do_tmark[] = { IAC, DO, TIMING_MARK };
static BYTE will_tmark[] = { IAC, WILL, TIMING_MARK };
static BYTE wont_sga[] = { IAC, WONT, SUPPRESS_GA };
static BYTE dont_sga[] = { IAC, DONT, SUPPRESS_GA };
#endif
static BYTE wont_echo[] = { IAC, WONT, ECHO_OPTION };
static BYTE dont_echo[] = { IAC, DONT, ECHO_OPTION };

    /* Perform terminal-type negotiation */
    rc = send_packet (csock, do_term, sizeof(do_term),
                        "IAC DO TERMINAL_TYPE");
    if (rc < 0) return -1;

    rc = expect (csock, will_term, sizeof(will_term),
                        "IAC WILL TERMINAL_TYPE");
    if (rc < 0) return -1;

    /* Request terminal type */
    rc = send_packet (csock, req_type, sizeof(req_type),
                        "IAC SB TERMINAL_TYPE SEND IAC SE");
    if (rc < 0) return -1;

    rc = recv_packet (csock, buf, sizeof(buf)-2, SE);
    if (rc < 0) return -1;

    if (rc < sizeof(type_is) + 2
        || memcmp(buf, type_is, sizeof(type_is)) != 0
        || buf[rc-2] != IAC || buf[rc-1] != SE) {
        TNSDEBUG(2, "Expected IAC SB TERMINAL_TYPE IS\n");
        return -1;
    }
    buf[rc-2] = '\0';
    termtype = buf + sizeof(type_is);
    TNSDEBUG(2, "Received IAC SB TERMINAL_TYPE IS %s IAC SE\n",
            termtype);

    /* Test for non-display terminal type */
    if (memcmp(termtype, "IBM-", 4) != 0)
    {
#if 0
        /* Perform line mode negotiation */
        rc = send_packet (csock, do_tmark, sizeof(do_tmark),
                            "IAC DO TIMING_MARK");
        if (rc < 0) return -1;

        rc = expect (csock, will_tmark, sizeof(will_tmark),
                            "IAC WILL TIMING_MARK");
        if (rc < 0) return 0;

        rc = send_packet (csock, wont_sga, sizeof(wont_sga),
                            "IAC WONT SUPPRESS_GA");
        if (rc < 0) return -1;

        rc = expect (csock, dont_sga, sizeof(dont_sga),
                            "IAC DONT SUPPRESS_GA");
        if (rc < 0) return -1;
#endif

        if (memcmp(termtype, "ANSI", 4) == 0)
        {
            rc = send_packet (csock, wont_echo, sizeof(wont_echo),
                                "IAC WONT ECHO");
            if (rc < 0) return -1;

            rc = expect (csock, dont_echo, sizeof(dont_echo),
                                "IAC DONT ECHO");
            if (rc < 0) return -1;
        }

        /* Return printer-keyboard terminal class */
        *class = 'K';
        return 0;
    }

    /* Determine display terminal model */
    if (memcmp(termtype+4,"DYNAMIC",7) == 0) {
        *model = 'X';
        *extatr = 'Y';
    } else {
        if (!(memcmp(termtype+4, "3277", 4) == 0
              || memcmp(termtype+4, "3270", 4) == 0
              || memcmp(termtype+4, "3178", 4) == 0
              || memcmp(termtype+4, "3278", 4) == 0
              || memcmp(termtype+4, "3179", 4) == 0
              || memcmp(termtype+4, "3180", 4) == 0
              || memcmp(termtype+4, "3279", 4) == 0))
            return -1;

        *model = '2';
        *extatr = 'N';

        if (termtype[8]=='-') {
            if (termtype[9] < '2' || termtype[9] > '5')
                return -1;
            *model = termtype[9];
            if (memcmp(termtype+10, "-E", 2) == 0)
                *extatr = 'Y';
        }
    }

    /* Perform end-of-record negotiation */
    rc = send_packet (csock, do_eor, sizeof(do_eor),
                        "IAC DO EOR IAC WILL EOR");
    if (rc < 0) return -1;

    rc = expect (csock, will_eor, sizeof(will_eor),
                        "IAC WILL EOR IAC DO EOR");
    if (rc < 0) return -1;

    /* Perform binary negotiation */
    rc = send_packet (csock, do_bin, sizeof(do_bin),
                        "IAC DO BINARY IAC WILL BINARY");
    if (rc < 0) return -1;

    rc = expect (csock, will_bin, sizeof(will_bin),
                        "IAC WILL BINARY IAC DO BINARY");
    if (rc < 0) return -1;

    /* Return display terminal class */
    *class = 'D';
    return 0;

} /* end function negotiate */


/*-------------------------------------------------------------------*/
/* SUBROUTINE TO RECEIVE 3270 DATA FROM THE CLIENT                   */
/* This subroutine receives bytes from the client and appends them   */
/* to any data already in the 3270 receive buffer.                   */
/* If zero bytes are received, this means the client has closed the  */
/* connection, and attention and unit check status is returned.      */
/* If the buffer is filled before receiving end of record, then      */
/* attention and unit check status is returned.                      */
/* If the data ends with IAC followed by EOR_MARK, then the data     */
/* is scanned to remove any IAC sequences, attention status is       */
/* returned, and the read pending indicator is set.                  */
/* If the data accumulated in the buffer does not yet constitute a   */
/* complete record, then zero status is returned, and a further      */
/* call must be made to this subroutine when more data is available. */
/*-------------------------------------------------------------------*/
static BYTE
recv_3270_data (DEVBLK *dev)
{
int     rc;                             /* Return code               */
int     eor = 0;                        /* 1=End of record received  */

    /* If there is a complete data record already in the buffer
       then discard it before reading more data */
    if (dev->readpending)
    {
        dev->rlen3270 = 0;
        dev->readpending = 0;
    }

    /* Receive bytes from client */
    rc = recv (dev->csock, dev->buf + dev->rlen3270,
               BUFLEN_3270 - dev->rlen3270, 0);

    if (rc < 0) {
        TNSERROR("recv: %s\n", strerror(errno));
        dev->sense[0] = SENSE_EC;
        return (CSW_ATTN | CSW_UC);
    }

    /* If zero bytes were received then client has closed connection */
    if (rc == 0) {
        TNSDEBUG(1, "Connection closed by client\n");
        dev->sense[0] = SENSE_IR;
        return (CSW_ATTN | CSW_UC);
    }

    /* Update number of bytes in receive buffer */
    dev->rlen3270 += rc;

    /* Check whether Attn indicator was received */
    if (dev->rlen3270 >= 2
        && dev->buf[dev->rlen3270 - 2] == IAC
        && dev->buf[dev->rlen3270 - 1] == BRK)
        eor = 1;

    /* Check whether SysRq indicator was received */
    if (dev->rlen3270 >= 2
        && dev->buf[dev->rlen3270 - 2] == IAC
        && dev->buf[dev->rlen3270 - 1] == IP)
        eor = 1;

    /* Check whether end of record marker was received */
    if (dev->rlen3270 >= 2
        && dev->buf[dev->rlen3270 - 2] == IAC
        && dev->buf[dev->rlen3270 - 1] == EOR_MARK)
        eor = 1;

    /* If record is incomplete, test for buffer full */
    if (eor == 0 && dev->rlen3270 >= BUFLEN_3270)
    {
        TNSDEBUG(1, "3270 buffer overflow\n");
        dev->sense[0] = SENSE_DC;
        return (CSW_ATTN | CSW_UC);
    }

    /* Return zero status if record is incomplete */
    if (eor == 0) return 0;

    /* Trace the complete 3270 data packet */
    TNSDEBUG(2, "Packet received length=%d\n", dev->rlen3270);
    packet_trace (dev->buf, dev->rlen3270);

    /* Strip off the telnet EOR marker */
    dev->rlen3270 -= 2;

    /* Remove any embedded IAC commands */
    dev->rlen3270 = remove_iac (dev->buf, dev->rlen3270);

    /* Set the read pending indicator and return attention status */
    dev->readpending = 1;
    return (CSW_ATTN);

} /* end function recv_3270_data */


/*-------------------------------------------------------------------*/
/* SUBROUTINE TO RECEIVE 1052/3215 DATA FROM THE CLIENT              */
/* This subroutine receives keyboard input characters from the       */
/* client, and appends the characters to any data already in the     */
/* keyboard buffer.                                                  */
/* If zero bytes are received, this means the client has closed the  */
/* connection, and attention and unit check status is returned.      */
/* If the buffer is filled before receiving end of record, then      */
/* attention and unit check status is returned.                      */
/* If a break indication (control-C, IAC BRK, or IAC IP) is          */
/* received, the attention and unit exception status is returned.    */
/* When carriage return and line feed (CRLF) is received, then       */
/* the CRLF is discarded, the data in the keyboard buffer is         */
/* translated to EBCDIC, the read pending indicator is set, and      */
/* attention status is returned.                                     */
/* If CRLF has not yet been received, then zero status is returned,  */
/* and a further call must be made to this subroutine when more      */
/* data is available.                                                */
/*-------------------------------------------------------------------*/
static BYTE
recv_1052_data (DEVBLK *dev)
{
int     num;                            /* Number of bytes received  */
int     i;                              /* Array subscript           */
BYTE    buf[LINE_LENGTH];               /* Receive buffer            */
BYTE    c;                              /* Character work area       */

    /* Receive bytes from client */
    num = recv (dev->csock, buf, LINE_LENGTH, 0);

    /* Return unit check if error on receive */
    if (num < 0) {
        TNSERROR("recv: %s\n", strerror(errno));
        dev->sense[0] = SENSE_EC;
        return (CSW_ATTN | CSW_UC);
    }

    /* If zero bytes were received then client has closed connection */
    if (num == 0) {
        TNSDEBUG(1, "Connection closed by client\n");
        dev->sense[0] = SENSE_IR;
        return (CSW_ATTN | CSW_UC);
    }

    /* Trace the bytes received */
    TNSDEBUG(2, "Bytes received length=%d\n", num);
    packet_trace (buf, num);

    /* Copy received bytes to keyboard buffer */
    for (i = 0; i < num; i++)
    {
        /* Decrement keyboard buffer pointer if backspace received */
        if (buf[i] == 0x08)
        {
            if (dev->keybdrem > 0) dev->keybdrem--;
            continue;
        }

        /* Return unit exception if control-C received */
        if (buf[i] == 0x03)
        {
            dev->keybdrem = 0;
            return (CSW_ATTN | CSW_UX);
        }

        /* Return unit check if buffer is full */
        if (dev->keybdrem >= LINE_LENGTH)
        {
            TNSDEBUG(1, "Console keyboard buffer overflow\n");
            dev->keybdrem = 0;
            dev->sense[0] = SENSE_EC;
            return (CSW_ATTN | CSW_UC);
        }

        /* Copy character to keyboard buffer */
        dev->buf[dev->keybdrem++] = buf[i];

        /* Decrement keyboard buffer pointer if telnet
           erase character sequence received */
        if (dev->keybdrem >= 2
            && dev->buf[dev->keybdrem - 2] == IAC
            && dev->buf[dev->keybdrem - 1] == EC)
        {
            dev->keybdrem -= 2;
            if (dev->keybdrem > 0) dev->keybdrem--;
            continue;
        }

        /* Zeroize keyboard buffer pointer if telnet
           erase line sequence received */
        if (dev->keybdrem >= 2
            && dev->buf[dev->keybdrem - 2] == IAC
            && dev->buf[dev->keybdrem - 1] == EL)
        {
            dev->keybdrem = 0;
            continue;
        }

        /* Zeroize keyboard buffer pointer if telnet
           carriage return sequence received */
        if (dev->keybdrem >= 2
            && dev->buf[dev->keybdrem - 2] == '\r'
            && dev->buf[dev->keybdrem - 1] == '\0')
        {
            dev->keybdrem = 0;
            continue;
        }

        /* Return unit exception if telnet break sequence received */
        if (dev->keybdrem >= 2
            && dev->buf[dev->keybdrem - 2] == IAC
            && (dev->buf[dev->keybdrem - 1] == BRK
                || dev->buf[dev->keybdrem - 1] == IP))
        {
            dev->keybdrem = 0;
            return (CSW_ATTN | CSW_UX);
        }

        /* Return unit check with overrun if telnet CRLF
           sequence received and more data follows the CRLF */
        if (dev->keybdrem >= 2
            && dev->buf[dev->keybdrem - 2] == '\r'
            && dev->buf[dev->keybdrem - 1] == '\n'
            && i < num - 1)
        {
            TNSDEBUG(1, "Console keyboard buffer overrun\n");
            dev->keybdrem = 0;
            dev->sense[0] = SENSE_OR;
            return (CSW_ATTN | CSW_UC);
        }

    } /* end for(i) */

    /* Return zero status if CRLF was not yet received */
    if (dev->keybdrem < 2
        || dev->buf[dev->keybdrem - 2] != '\r'
        || dev->buf[dev->keybdrem - 1] != '\n')
        return 0;

    /* Trace the complete keyboard data packet */
    TNSDEBUG(2, "Packet received length=%d\n", dev->keybdrem);
    packet_trace (dev->buf, dev->keybdrem);

    /* Strip off the CRLF sequence */
    dev->keybdrem -= 2;

    /* Translate the keyboard buffer to EBCDIC */
    for (i = 0; i < dev->keybdrem; i++)
    {
        c = dev->buf[i];
        dev->buf[i] = (isprint(c) ? ascii_to_ebcdic[c] : SPACE);
    } /* end for(i) */

    /* Trace the EBCDIC input data */
    TNSDEBUG(2, "Input data line length=%d\n", dev->keybdrem);
    packet_trace (dev->buf, dev->keybdrem);

    /* Set the read pending indicator and return attention status */
    dev->readpending = 1;
    return (CSW_ATTN);

} /* end function recv_1052_data */


/*-------------------------------------------------------------------*/
/* NEW CLIENT CONNECTION THREAD                                      */
/*-------------------------------------------------------------------*/
static void *connect_client (int *csockp)
{
int                     rc;             /* Return code               */
DEVBLK                 *dev;            /* -> Device block           */
int                     len;            /* Data length               */
int                     csock;          /* Socket for conversation   */
struct sockaddr_in      client;         /* Client address structure  */
socklen_t               namelen;        /* Length of client structure*/
struct hostent         *pHE;            /* Addr of hostent structure */
char                   *clientip;       /* Addr of client ip address */
char                   *clientname;     /* Addr of client hostname   */
BYTE                    class;          /* D=3270, K=3215/1052       */
BYTE                    model;          /* 3270 model (2,3,4,5,X)    */
BYTE                    extended;       /* Extended attributes (Y,N) */
BYTE                    buf[200];       /* Message buffer            */
BYTE                    conmsg[80];     /* Connection message        */
BYTE                    rejmsg[80];     /* Rejection message         */

    /* Load the socket address from the thread parameter */
    csock = *csockp;

    /* Obtain the client's IP address */
    namelen = sizeof(client);
    rc = getpeername (csock, (struct sockaddr *)&client, &namelen);

    /* Log the client's IP address and hostname */
    clientip = inet_ntoa(client.sin_addr);

    pHE = gethostbyaddr ((unsigned char*)(&client.sin_addr),
                         sizeof(client.sin_addr), AF_INET);

    if (pHE != NULL && pHE->h_name != NULL
     && pHE->h_name[0] != '\0') {
        clientname = pHE->h_name;
    } else {
        clientname = "host name unknown";
    }

    TNSDEBUG(1, "Received connection from %s (%s)\n",
            clientip, clientname);

    /* Negotiate telnet parameters */
    rc = negotiate (csock, &class, &model, &extended);
    if (rc != 0)
    {
        close (csock);
        return NULL;
    }

    /* Look for an available console device */
    for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
    {
        /* Loop if non-matching device type */
        if (class == 'D' && dev->devtype != 0x3270)
            continue;

        if (class == 'K' && dev->devtype != 0x1052
            && dev->devtype != 0x3215)
            continue;

        /* Obtain the device lock */
        obtain_lock (&dev->lock);

        /* Test for available device */
        if (dev->connected == 0)
        {
            /* Claim this device for the client */
            dev->connected = 1;
            dev->csock = csock;
            dev->ipaddr = client.sin_addr;
            release_lock (&dev->lock);
            break;
        }

        /* Release the device lock */
        release_lock (&dev->lock);

    } /* end for(dev) */

    /* Build connection message for client */
    len = sprintf (conmsg,
                "Hercules %s version %s at %s (%s %s)",
                ARCHITECTURE_NAME, MSTRING(VERSION),
                hostinfo.nodename, hostinfo.sysname,
                hostinfo.release);

    if (dev != NULL)
        len += sprintf (conmsg + len, " device %4.4X", dev->devnum);

    /* Reject the connection if no available console device */
    if (dev == NULL)
    {
        /* Build the rejection message */
        len = sprintf (rejmsg,
                "Connection rejected, no available %s device",
                (class=='D' ? "3270" : "1052 or 3215"));
        TNSDEBUG(1, "%s\n", rejmsg);

        /* Send connection rejection message to client */
        if (class == 'D')
        {
            len = sprintf (buf,
                        "\xF5\x40\x11\x40\x40\x1D\x60%s"
                        "\x11\xC1\x50\x1D\x60%s",
                        translate_to_ebcdic(conmsg),
                        translate_to_ebcdic(rejmsg));
            buf[len++] = IAC;
            buf[len++] = EOR_MARK;
        }
        else
        {
            len = sprintf (buf, "%s\r\n%s\r\n", conmsg, rejmsg);
        }
        rc = send_packet (csock, buf, len, "CONNECTION RESPONSE");

        /* Close the connection and terminate the thread */
        sleep (5);
        close (csock);
        return NULL;
    }

    TNSDEBUG(1, "Client %s connected to %4.4X device %4.4X\n",
            clientip, dev->devtype, dev->devnum);

    /* Send connection message to client */
    if (class == 'D')
    {
        len = sprintf (buf,
                    "\xF5\x40\x11\x40\x40\x1D\x60%s",
                    translate_to_ebcdic(conmsg));
        buf[len++] = IAC;
        buf[len++] = EOR_MARK;
    }
    else
    {
        len = sprintf (buf, "%s\r\n", conmsg);
    }
    rc = send_packet (csock, buf, len, "CONNECTION RESPONSE");

    /* Signal connection thread to redrive its select loop */
    signal_thread (sysblk.cnsltid, SIGHUP);

    return NULL;

} /* end function connect_client */


/*-------------------------------------------------------------------*/
/* CONSOLE CONNECTION AND ATTENTION HANDLER THREAD                   */
/*-------------------------------------------------------------------*/
void *console_connection_handler (void *arg)
{
int                     rc;             /* Return code               */
int                     lsock;          /* Socket for listening      */
int                     csock;          /* Socket for conversation   */
struct sockaddr_in      server;         /* Server address structure  */
int                     term_flag=0;    /* Termination flag          */
fd_set                  readset;        /* Read bit map for select   */
int                     maxfd;          /* Highest fd for select     */
TID                     tidneg;         /* Negotiation thread id     */
DEVBLK                 *dev;            /* -> Device block           */
BYTE                    unitstat;       /* Status after receive data */

    /* Display thread started message on control panel */
//  logmsg ("HHC600I Console connection thread started: id=%ld\n",
//          thread_id());

    /* Get information about this system */
    uname (&hostinfo);

    /* Obtain a socket */
    lsock = socket (AF_INET, SOCK_STREAM, 0);

    if (lsock < 0)
    {
        TNSERROR("socket: %s\n", strerror(errno));
        return NULL;
    }

    /* Prepare the sockaddr structure for the bind */
    memset (&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = sysblk.cnslport;
    server.sin_port = htons(server.sin_port);

    /* Attempt to bind the socket to the port */
    while (1)
    {
        rc = bind (lsock, (struct sockaddr *)&server, sizeof(server));

        if (rc == 0 || errno != EADDRINUSE) break;

        logmsg ("HHC601I Waiting for port %u to become free\n",
                sysblk.cnslport);
        sleep(10);
    } /* end while */

    if (rc != 0)
    {
        TNSERROR("bind: %s\n", strerror(errno));
        return NULL;
    }

    /* Put the socket into listening state */
    rc = listen (lsock, 10);

    if (rc < 0)
    {
        TNSERROR("listen: %s\n", strerror(errno));
        return NULL;
    }

    logmsg ("HHC602I Waiting for console connection on port %u\n",
            sysblk.cnslport);

    /* Handle connection requests and attention interrupts */
    while (term_flag == 0) {

        /* Initialize the select parameters */
        maxfd = lsock;
        FD_ZERO (&readset);
        FD_SET (lsock, &readset);

        /* Include the socket for each connected console */
        for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
        {
            if (dev->console
                && dev->connected
                && dev->busy == 0)
            {
                FD_SET (dev->csock, &readset);
                if (dev->csock > maxfd) maxfd = dev->csock;
            }
        } /* end for(dev) */

        /* Wait for a file descriptor to become ready */
        rc = select ( maxfd+1, &readset, NULL, NULL, NULL );

        if (rc == 0) continue;

        if (rc < 0 )
        {
            if (errno == EINTR) continue;
            TNSERROR("select: %s\n", strerror(errno));
            break;
        }

        /* If a client connection request has arrived then accept it */
        if (FD_ISSET(lsock, &readset))
        {
            /* Accept a connection and create conversation socket */
            csock = accept (lsock, NULL, NULL);

            if (csock < 0)
            {
                TNSERROR("accept: %s\n", strerror(errno));
                continue;
            }

            /* Create a thread to complete the client connection */
            if ( create_thread (&tidneg, &sysblk.detattr,
                                connect_client, &csock) )
            {
                TNSERROR("connect_client create_thread: %s\n",
                        strerror(errno));
                close (csock);
            }

        } /* end if(lsock) */

        /* Check if any connected client has data ready to send */
        for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
        {
            /* Obtain the device lock */
            obtain_lock (&dev->lock);

            /* Test for connected console with data available */
            if (dev->console
                && dev->connected
                && FD_ISSET (dev->csock, &readset)
                && dev->busy == 0)
            {
                /* Receive console input data from the client */
                if (dev->devtype == 0x3270)
                    unitstat = recv_3270_data (dev);
                else
                    unitstat = recv_1052_data (dev);

                /* Nothing more to do if incomplete record received */
                if (unitstat == 0)
                {
                    release_lock (&dev->lock);
                    continue;
                }

                /* Close the connection if an error occurred */
                if (unitstat & CSW_UC)
                {
                    close (dev->csock);
                    dev->csock = 0;
                    dev->connected = 0;
                }

                /* Indicate that data is available at the device */
                dev->readpending = 1;

                /* Release the device lock */
                release_lock (&dev->lock);

                /* Raise attention interrupt for the device */
                rc = device_attention (dev, unitstat);

                /* Trace the attention request */
                TNSDEBUG(2, "%4.4X attention request %s\n",
                        dev->devnum,
                        (rc == 0 ? "raised" : "rejected"));

                continue;
            } /* end if(data available) */

            /* Release the device lock */
            release_lock (&dev->lock);

        } /* end for(dev) */

    } /* end while */

    /* Close the listening socket */
    close (lsock);

    return NULL;

} /* end function console_connection_handler */


/*-------------------------------------------------------------------*/
/* INITIALIZE THE 3270 DEVICE HANDLER                                */
/*-------------------------------------------------------------------*/
int loc3270_init_handler ( DEVBLK *dev, int argc, BYTE *argv[] )
{
    /* Indicate that this is a console device */
    dev->console = 1;

    /* Set number of sense bytes */
    dev->numsense = 1;

    /* Set the size of the device buffer */
    dev->bufsize = BUFLEN_3270;

    /* Initialize the device identifier bytes */
    dev->devid[0] = 0xFF;
    dev->devid[1] = 0x32; /* Control unit type is 3274-1D */
    dev->devid[2] = 0x74;
    dev->devid[3] = 0x1D;
    dev->devid[4] = 0x32; /* Device type is 3278-2 */
    dev->devid[5] = 0x78;
    dev->devid[6] = 0x02;
    dev->numdevid = 7;

    /* Activate CCW tracing */
//  dev->ccwtrace = 1;

    return 0;
} /* end function loc3270_init_handler */


/*-------------------------------------------------------------------*/
/* INITIALIZE THE 1052/3215 DEVICE HANDLER                           */
/*-------------------------------------------------------------------*/
int constty_init_handler ( DEVBLK *dev, int argc, BYTE *argv[] )
{
    /* Indicate that this is a console device */
    dev->console = 1;

    /* Set number of sense bytes */
    dev->numsense = 1;

    /* Initialize device dependent fields */
    dev->keybdrem = 0;

    /* Set length of print buffer */
    dev->bufsize = LINE_LENGTH;

    /* Initialize the device identifier bytes */
    dev->devid[0] = 0xFF;
    dev->devid[1] = dev->devtype >> 8;
    dev->devid[2] = dev->devtype & 0xFF;
    dev->devid[3] = 0x00;
    dev->devid[4] = dev->devtype >> 8;
    dev->devid[5] = dev->devtype & 0xFF;
    dev->devid[6] = 0x00;
    dev->numdevid = 7;

    /* Activate I/O tracing */
//  dev->ccwtrace = 1;

    return 0;
} /* end function constty_init_handler */


/*-------------------------------------------------------------------*/
/* EXECUTE A 3270 CHANNEL COMMAND WORD                               */
/*-------------------------------------------------------------------*/
void loc3270_execute_ccw ( DEVBLK *dev, BYTE code, BYTE flags,
        BYTE chained, U16 count, BYTE prevcode, int ccwseq,
        BYTE *iobuf, BYTE *more, BYTE *unitstat, U16 *residual )
{
int             rc;                     /* Return code               */
int             num;                    /* Number of bytes to copy   */
int             len;                    /* Data length               */
BYTE            wcc;                    /* tn3270 write control char */
BYTE            buf[32768];             /* tn3270 write buffer       */

    /* Unit check with intervention required if no client connected */
    if (dev->connected == 0 && !IS_CCW_SENSE(code))
    {
        dev->sense[0] = SENSE_IR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return;
    }

    /* Process depending on CCW opcode */
    switch (code) {

    case 0x03:
    /*---------------------------------------------------------------*/
    /* CONTROL NO-OPERATION                                          */
    /*---------------------------------------------------------------*/
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x0B:
    /*---------------------------------------------------------------*/
    /* SELECT                                                        */
    /*---------------------------------------------------------------*/
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x0F:
    /*---------------------------------------------------------------*/
    /* ERASE ALL UNPROTECTED                                         */
    /*---------------------------------------------------------------*/
        wcc = 0x6F;
        goto write;

    case 0x01:
    /*---------------------------------------------------------------*/
    /* WRITE                                                         */
    /*---------------------------------------------------------------*/
        wcc = 0xF1;
        goto write;

    case 0x05:
    /*---------------------------------------------------------------*/
    /* ERASE/WRITE                                                   */
    /*---------------------------------------------------------------*/
        wcc = 0xF5;
        goto write;

    case 0x0D:
    /*---------------------------------------------------------------*/
    /* ERASE/WRITE ALTERNATE                                         */
    /*---------------------------------------------------------------*/
        wcc = 0x7E;
        goto write;

    case 0x11:
    /*---------------------------------------------------------------*/
    /* WRITE STRUCTURED FIELD                                        */
    /*---------------------------------------------------------------*/
        wcc = 0xF3;
        goto write;

    write:
    /*---------------------------------------------------------------*/
    /* All write commands, and the EAU control command, come here    */
    /*---------------------------------------------------------------*/
        /* Initialize the data length */
        len = 0;

        /* Move write control character to first byte of buffer
           unless data-chained from previous CCW */
        if ((chained & CCW_FLAGS_CD) == 0)
            buf[len++] = wcc;

        /* Calculate number of bytes to move and residual byte count */
        num = sizeof(buf) / 2;
        num = (count < num) ? count : num;
        *residual = count - num;

        /* Copy data from channel buffer to device buffer */
        memcpy (buf + len, iobuf, num);
        len += num;

        /* Double up any IAC bytes in the data */
        len = double_up_iac (buf, len);

        /* Append telnet EOR marker at end of data */
        if ((flags & CCW_FLAGS_CD) == 0) {
            buf[len++] = IAC;
            buf[len++] = EOR_MARK;
        }

        /* Send the data to the client */
        rc = send_packet(dev->csock, buf, len, "3270 data");
        if (rc < 0)
        {
            dev->sense[0] = SENSE_DC;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
            break;
        }

        /* Return normal status */
        *unitstat = CSW_CE | CSW_DE;
        break;

    case 0x06:
    /*---------------------------------------------------------------*/
    /* READ MODIFIED                                                 */
    /*---------------------------------------------------------------*/

    case 0x02:
    /*---------------------------------------------------------------*/
    /* READ BUFFER                                                   */
    /*---------------------------------------------------------------*/
        /* Obtain the device lock */
        obtain_lock (&dev->lock);

        /* Get length of data available at the device */
        if (dev->readpending)
            len = dev->rlen3270;
        else
            len = 0;

        /* Calculate number of bytes to move and residual byte count */
        num = (count < len) ? count : len;
        *residual = count - num;
        if (count < len) *more = 1;

        /* Copy data from device buffer to channel buffer */
        memcpy (iobuf, dev->buf, num);

        /* If data chaining is specified, save remaining data */
        if ((flags & CCW_FLAGS_CD) && len > count)
        {
            memmove (dev->buf, dev->buf + count, len - count);
            dev->rlen3270 = len - count;
        }
        else
        {
            dev->rlen3270 = 0;
            dev->readpending = 0;
        }

        /* Set normal status */
        *unitstat = CSW_CE | CSW_DE;

        /* Release the device lock */
        release_lock (&dev->lock);

        /* Signal connection thread to redrive its select loop */
        signal_thread (sysblk.cnsltid, SIGHUP);

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

} /* end function loc3270_execute_ccw */


/*-------------------------------------------------------------------*/
/* EXECUTE A 1052/3215 CHANNEL COMMAND WORD                          */
/*-------------------------------------------------------------------*/
void constty_execute_ccw ( DEVBLK *dev, BYTE code, BYTE flags,
        BYTE chained, U16 count, BYTE prevcode, int ccwseq,
        BYTE *iobuf, BYTE *more, BYTE *unitstat, U16 *residual )
{
int     rc;                             /* Return code               */
int     len;                            /* Length of data            */
int     num;                            /* Number of bytes to move   */
BYTE    c;                              /* Print character           */
BYTE    stat;                           /* Unit status               */

    /* Unit check with intervention required if no client connected */
    if (dev->connected == 0 && !IS_CCW_SENSE(code))
    {
        dev->sense[0] = SENSE_IR;
        *unitstat = CSW_CE | CSW_DE | CSW_UC;
        return;
    }

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

        /* Calculate number of bytes to write and set residual count */
        num = (count < LINE_LENGTH) ? count : LINE_LENGTH;
        *residual = count - num;

        /* Translate data in channel buffer to ASCII */
        for (len = 0; len < num; len++)
        {
            c = ebcdic_to_ascii[iobuf[len]];
            if (!isprint(c)) c = SPACE;
            iobuf[len] = c;
        } /* end for(len) */

        /* Perform end of record processing if not data-chaining */
        if ((flags & CCW_FLAGS_CD) == 0)
        {
            /* Append carriage return and newline if required */
            if (code == 0x09)
            {
                iobuf[len++] = '\r';
                iobuf[len++] = '\n';
            }

        } /* end if(!data-chaining) */

        /* Send the data to the client */
        rc = send_packet (dev->csock, iobuf, len, NULL);
        if (rc < 0)
        {
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

    case 0x0A:
    /*---------------------------------------------------------------*/
    /* READ INQUIRY                                                  */
    /*---------------------------------------------------------------*/

        /* Solicit console input if no data in the device buffer */
        if (dev->keybdrem == 0)
        {
            /* Display prompting message on console */
            len = sprintf (dev->buf,
                    "HHC901I Enter input for console device %4.4X\r\n",
                    dev->devnum);
            rc = send_packet (dev->csock, dev->buf, len, NULL);
            if (rc < 0)
            {
                dev->sense[0] = SENSE_EC;
                *unitstat = CSW_CE | CSW_DE | CSW_UC;
                break;
            }

            /* Accumulate client input data into device buffer */
            while (1) {

                /* Receive client data and increment dev->keybdrem */
                stat = recv_1052_data (dev);

                /* Exit if error or end of line */
                if (stat != 0)
                    break;

            } /* end while */

            /* Exit if error status */
            if (stat != CSW_ATTN)
            {
                *unitstat = (CSW_CE | CSW_DE) | (stat & ~CSW_ATTN);
                break;
            }

        }

        /* Calculate number of bytes to move and residual byte count */
        len = dev->keybdrem;
        num = (count < len) ? count : len;
        *residual = count - num;
        if (count < len) *more = 1;

        /* Copy data from device buffer to channel buffer */
        memcpy (iobuf, dev->buf, num);

        /* If data chaining is specified, save remaining data */
        if ((flags & CCW_FLAGS_CD) && len > count)
        {
            memmove (dev->buf, dev->buf + count, len - count);
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
        rc = send_packet (dev->csock, "\a", 1, NULL);
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

} /* end function constty_execute_ccw */

