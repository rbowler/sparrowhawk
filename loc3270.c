/* LOC3270.C    (c)Copyright Roger Bowler, 1999                      */
/*              ESA/390 Local Non-SNA 3270 Device Handler            */

/*-------------------------------------------------------------------*/
/* This module contains device handling functions for local 3270     */
/* devices for the Hercules ESA/390 emulator.                        */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Telnet command definitions                                        */
/*-------------------------------------------------------------------*/
#define BINARY          0       /* Binary Transmission */
#define IS              0       /* Used by terminal-type negotiation */
#define SEND            1       /* Used by terminal-type negotiation */
#define TERMINAL_TYPE   24      /* Terminal Type */
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
#define RCV_BUFLEN      4096
#define TNSDEBUG(lvl,format,a...) \
        if(debug>=lvl)printf("loc3270: " format, ## a)

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
        fprintf(stderr, "+%4.4X  ", offset);
        for (i=0; i < 16; i++)
        {
            c = *addr++;
            if (offset < len) {
                fprintf(stderr, "%2.2X", c);
                print_chars[i] = '.';
                if (isprint(c)) print_chars[i] = c;
                c = ebcdic_to_ascii[c];
                if (isprint(c)) print_chars[i] = c;
            }
            else {
                fprintf(stderr, "  ");
            }
            offset++;
            if ((offset & 3) == 0) {
                fprintf(stderr, " ");
            }
        } /* end for(i) */
        fprintf(stderr, " %s\n", print_chars);
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
        perror ("tn3270d: send");
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
            perror ("tn3270d: recv");
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
expect (int csock, BYTE *buf, int len, char *caption)
{
int     rc;                             /* Return code               */

    rc = recv_packet (csock, buf, len, 0);
    if (rc < 0) return -1;

    if (memcmp(buf, buf, len) != 0) {
        TNSDEBUG(2, "Expected %s\n", caption);
        return -1;
    }
    TNSDEBUG(2, "Received %s\n", caption);

    return 0;

} /* end function expect */


/*-------------------------------------------------------------------*/
/* SUBROUTINE TO NEGOTIATE TELNET PARAMETERS                         */
/*-------------------------------------------------------------------*/
static int
negotiate(int csock, BYTE *model, BYTE *extended)
{
int    rc;                              /* Return code               */
BYTE  *termtype;                        /* Addr of terminal type     */
BYTE   buf[512];                        /* Telnet buffer             */
static BYTE do_term[] = { IAC, DO, TERMINAL_TYPE };
static BYTE will_term[] = { IAC, WILL, TERMINAL_TYPE };
static BYTE req_type[] = { IAC, SB, TERMINAL_TYPE, SEND, IAC, SE };
static BYTE type_is[] = { IAC, SB, TERMINAL_TYPE, IS };
static BYTE do_eor[] = { IAC, DO, EOR, IAC, WILL, EOR };
static BYTE will_eor[] = { IAC, WILL, EOR, IAC, DO, EOR };
static BYTE do_bin[] = { IAC, DO, BINARY, IAC, WILL, BINARY };
static BYTE will_bin[] = { IAC, WILL, BINARY, IAC, DO, BINARY };

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

    /* Validate the terminal type */
    if (memcmp(termtype, "IBM-", 4) != 0) {
        return -1;
    }

    if (memcmp(termtype+4,"DYNAMIC",7) == 0) {
        *model = 'X';
        *extended = 'Y';
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
        *extended = 'N';

        if (termtype[8]=='-') {
            if (termtype[9] < '2' || termtype[9] > '5')
                return -1;
            *model = termtype[9];
            if (memcmp(termtype+10, "-E", 2) == 0)
                *extended = 'Y';
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
               RCV_BUFLEN - dev->rlen3270, 0);

    if (rc < 0) {
        perror ("tn3270d: recv");
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
    if (eor == 0 && dev->rlen3270 >= RCV_BUFLEN)
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
/* TN3270 NEW CLIENT CONNECTION THREAD                               */
/*-------------------------------------------------------------------*/
static void *connect_client (DEVBLK *dev)
{
int                     rc;             /* Return code               */
int                     len;            /* Data length               */
BYTE                    model;          /* 3270 model (2,3,4,5,X)    */
BYTE                    extended;       /* Extended attributes (Y,N) */
BYTE                    buf[512];       /* Message buffer            */

    /* Send connection message to client */
    len = sprintf (buf,
                "Connected to Hercules ESA/390 %s at %s (%s %s)\r\n",
                MSTRING(VERSION), hostinfo.nodename,
                hostinfo.sysname, hostinfo.release);
    rc = send_packet (dev->csock, buf, len, NULL);

    /* Negotiate telnet parameters */
    rc = negotiate (dev->csock, &model, &extended);
    if (rc == 0) {
        dev->connected = 1;
        dev->negotiating = 0;
    } else {
        close (dev->csock);
        dev->csock = 0;
        dev->negotiating = 0;
        return NULL;
    }

    /* Signal IPL thread that a console is now available */
    obtain_lock (&sysblk.conslock);
    signal_condition (&sysblk.conscond);
    release_lock (&sysblk.conslock);

    /* Signal tn3270d main thread to redrive its select loop */
    signal_thread (sysblk.tid3270, SIGHUP);

    return NULL;

} /* end function connect_client */


/*-------------------------------------------------------------------*/
/* TN3270 CONNECTION AND ATTENTION HANDLER THREAD                    */
/*-------------------------------------------------------------------*/
void *tn3270d (void *arg)
{
int                     rc;             /* Return code               */
int                     i;              /* Loop counter              */
int                     lsock;          /* Socket for listening      */
int                     csock;          /* Socket for conversation   */
struct sockaddr_in      server;         /* Server address structure  */
struct sockaddr_in      client;         /* Client address structure  */
int                     namelen;        /* Length of client structure*/
struct hostent         *pHE;            /* Addr of hostent structure */
char                   *clientip;       /* Addr of client ip address */
char                   *clientname;     /* Addr of client hostname   */
int                     term_flag=0;    /* Termination flag          */
fd_set                  readset;        /* Read bit map for select   */
int                     maxfd;          /* Highest fd for select     */
DEVBLK                 *dev;            /* -> Device block           */
BYTE                    unitstat;       /* Status after receive data */

    /* Get information about this system */
    uname (&hostinfo);

    /* Obtain a socket */
    lsock = socket (AF_INET, SOCK_STREAM, 0);

    if (lsock < 0) {
        perror ("loc3270: socket");
        exit(2);
    }

    /* Prepare the sockaddr structure for the bind */
    memset (&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = sysblk.port3270;
    server.sin_port = htons(server.sin_port);

    /* Attempt to bind the socket to the port */
    for (i = 0; i < 10; i++)
    {
        rc = bind (lsock, (struct sockaddr *)&server, sizeof(server));

        if (rc == 0 || errno != EADDRINUSE) break;

        fprintf (stderr,
                "HHC601I Waiting for port %u to become free\n",
                sysblk.port3270);
        sleep(10);
    } /* end for(i) */

    if (rc != 0) {
        perror ("loc3270: bind");
        exit(3);
    }

    /* Put the socket into listening state */
    rc = listen (lsock, 10);

    if (rc < 0) {
        perror ("loc3270: listen");
        exit(4);
    }

    fprintf (stderr,
            "HHC602I Waiting for console connection on port %u\n",
            sysblk.port3270);

    /* Handle connection requests and attention interrupts */
    while (term_flag == 0) {

        /* Initialize the select parameters */
        maxfd = lsock;
        FD_ZERO (&readset);
        FD_SET (lsock, &readset);

        /* Include the socket for each connected 3270 */
        for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
        {
            if (dev->devtype == 0x3270 && dev->connected)
            {
                FD_SET (dev->csock, &readset);
                if (dev->csock > maxfd) maxfd = dev->csock;
            }
        } /* end for(dev) */

        /* Wait for a file descriptor to become ready */
        rc = select ( maxfd+1, &readset, NULL, NULL, NULL );

//      /*debug*/printf("loc3270: select rc=%d\n",rc);

        if (rc == 0) continue;

        if (rc < 0 ){
            if (errno == EINTR) continue;
            perror ("loc3270: select");
            break;
        }

        /* If a client connection request has arrived then accept it */
        if (FD_ISSET(lsock, &readset))
        {
            /* Accept a connection and create conversation socket */
            namelen = sizeof(client);
            csock = accept (lsock, (struct sockaddr *)&client, &namelen);

            if (csock < 0) {
                perror ("loc3270: accept");
                break;
            }

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

            /* Look for an available 3270 device */
            for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
            {
                /* Obtain the device lock */
                obtain_lock (&dev->lock);

                /* Test for available 3270 */
                if (dev->devtype == 0x3270 && dev->connected == 0
                    && dev->negotiating == 0)
                {
                    /* Claim this device for the client */
                    dev->csock = csock;
                    dev->negotiating = 1;
                    dev->ipaddr = client.sin_addr;
                    release_lock (&dev->lock);
                    break;
                }

                /* Release the device lock */
                release_lock (&dev->lock);

            } /* end for(dev) */

            /* Reject the connection if no available device */
            if (dev == NULL) {
                TNSDEBUG(1, "Connection rejected, no available 3270\n");
                close (csock);
                continue;
            }

            /* Create a thread to complete the client connection */
            if ( create_thread (&dev->tid, &sysblk.detattr,
                                connect_client, dev) )
            {
                perror("loc3270: connect_client create_thread");
                dev->negotiating = 0;
                dev->csock = 0;
                close (csock);
            }

        } /* end if(lsock) */

        /* Check if any connected client has data ready to send */
        for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
        {
            /* Obtain the device lock */
            obtain_lock (&dev->lock);

            /* Test for connected 3270 with data available */
            if (dev->devtype == 0x3270 && dev->connected
                && FD_ISSET (dev->csock, &readset))
            {
                /* Receive 3270 input data from the client */
                unitstat = recv_3270_data (dev);

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

                /* If device is already busy or interrupt pending or
                   status pending then do not present interrupt */
                if (dev->busy || dev->pending
                    || (dev->scsw.flag3 & SCSW3_SC_PEND))
                {
                    release_lock (&dev->lock);
                    continue;
                }

#ifdef FEATURE_S370_CHANNEL
                /* Set CSW for attention interrupt */
                dev->csw[0] = 0;
                dev->csw[1] = 0;
                dev->csw[2] = 0;
                dev->csw[3] = 0;
                dev->csw[4] = unitstat;
                dev->csw[5] = 0;
                dev->csw[6] = 0;
                dev->csw[7] = 0;
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
                /* Set SCSW for attention interrupt */
                dev->scsw.flag0 = SCSW0_CC_1;
                dev->scsw.flag1 = 0;
                dev->scsw.flag2 = 0;
                dev->scsw.flag3 = SCSW3_SC_ALERT | SCSW3_SC_PEND;
                dev->scsw.ccwaddr[0] = 0;
                dev->scsw.ccwaddr[1] = 0;
                dev->scsw.ccwaddr[2] = 0;
                dev->scsw.ccwaddr[3] = 0;
                dev->scsw.unitstat = unitstat;
                dev->scsw.chanstat = 0;
                dev->scsw.count[0] = 0;
                dev->scsw.count[1] = 0;
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

                /* Trace the attention interrupt */
                TNSDEBUG(2, "%4.4X attention request\n", dev->devnum);

                /* Set the interrupt pending flag for this device */
                dev->pending = 1;

                /* Signal waiting CPUs that an interrupt is pending */
                obtain_lock (&sysblk.intlock);
                signal_condition (&sysblk.intcond);
                release_lock (&sysblk.intlock);
            }

            /* Release the device lock */
            release_lock (&dev->lock);

        } /* end for(dev) */

    } /* end while */

    /* Close the listening socket */
    close (lsock);

    return NULL;

} /* end function tn3270d */


/*-------------------------------------------------------------------*/
/* INITIALIZE THE DEVICE HANDLER                                     */
/*-------------------------------------------------------------------*/
int loc3270_init_handler ( DEVBLK *dev, int argc, BYTE *argv[] )
{
    /* Set number of sense bytes */
    dev->numsense = 1;

    /* Set the size of the device buffer */
    dev->bufsize = RCV_BUFLEN;

    return 0;
} /* end function loc3270_init_handler */


/*-------------------------------------------------------------------*/
/* EXECUTE A CHANNEL COMMAND WORD                                    */
/*-------------------------------------------------------------------*/
void loc3270_execute_ccw ( DEVBLK *dev, BYTE code, BYTE flags,
        BYTE chained, U16 count, BYTE prevcode, BYTE *iobuf,
        BYTE *more, BYTE *unitstat, U16 *residual )
{
int             rc;                     /* Return code               */
int             num;                    /* Number of bytes to copy   */
int             len;                    /* Data length               */
BYTE            wcc;                    /* tn3270 write control char */
BYTE            buf[4096];              /* tn3270 write buffer       */

    /* Unit check with intervention required if no client connected */
    if (dev->connected == 0)
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
        if (rc < 0) {
            dev->sense[0] = SENSE_DC;
            *unitstat = CSW_CE | CSW_DE | CSW_UC;
        } else {
            *unitstat = CSW_CE | CSW_DE;
        }

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
            memcpy (dev->buf, dev->buf + count, len - count);
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

        /* Signal tn3270d thread to redrive its select loop */
        signal_thread (sysblk.tid3270, SIGHUP);

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

