/* DIAGNOSE.C   (c) Copyright Roger Bowler, 2000                     */
/*              ESA/390 Diagnose Functions                           */

/*-------------------------------------------------------------------*/
/* This module implements miscellaneous diagnose functions           */
/* described in SC24-5670 VM/ESA CP Programming Services             */
/* and SC24-5855 VM/ESA CP Diagnosis Reference.                      */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      Hercules-specific diagnose calls by Jay Maynard.             */
/*      Set/reset bad frame indicator call by Jan Jaeger.            */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Internal macro definitions                                        */
/*-------------------------------------------------------------------*/
#define SPACE   ((BYTE)' ')

/*-------------------------------------------------------------------*/
/* Store Extended Identification Code (Function code 0x000)          */
/*-------------------------------------------------------------------*/
static void extid_call (int r1, int r2, REGS *regs)
{
int             i;                      /* Array subscript           */
int             ver, rel;               /* Version and release number*/
U32             idaddr;                 /* Address of storage operand*/
U32             idlen;                  /* Length of storage operand */
BYTE            buf[40];                /* Extended identification   */
struct passwd  *ppwd;                   /* Pointer to passwd entry   */
BYTE           *puser;                  /* Pointer to user name      */
BYTE            c;                      /* Character work area       */

    /* Load storage operand address from R1 register */
    idaddr = regs->gpr[r1];

    /* Program check if operand is not on a doubleword boundary */
    if (idaddr & 0x00000007)
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return;
    }

    /* Load storage operand length from R2 register */
    idlen = regs->gpr[r2];

    /* Program check if operand length is invalid */
    if (idlen < 1)
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return;
    }

    /* Bytes 0-7 contain the system name ("HERCULES" in EBCDIC) */
    memcpy (buf, "\xC8\xC5\xD9\xC3\xE4\xD3\xC5\xE2", 8);

    /* Bytes 8-9 contain the execution environment bits */
    buf[8] = 0x00;
    buf[9] = 0x00;

    /* Byte 10 contains the system product version number */
    sscanf (MSTRING(VERSION), "%d.%d", &ver, &rel);
    buf[10] = ver;

    /* Byte 11 contains version number from STIDP */
    buf[11] = sysblk.cpuid >> 56;

    /* Bytes 12-13 contain MCEL length from STIDP */
    buf[12] = (sysblk.cpuid >> 8) & 0xFF;
    buf[13] = sysblk.cpuid & 0xFF;

    /* Bytes 14-15 contain the CP address */
    buf[14] = (regs->cpuad >> 8) & 0xFF;
    buf[15] = regs->cpuad & 0xFF;

    /* Bytes 16-23 contain the userid in EBCDIC */
    ppwd = getpwuid(getuid());
    puser = (ppwd != NULL ? ppwd->pw_name : "");
    for (i = 0; i < 8; i++)
    {
        c = (*puser == '\0' ? SPACE : *(puser++));
        buf[16+i] = ascii_to_ebcdic[toupper(c)];
    }

    /* Bytes 24-31 contain the program product bitmap */
    memcpy (buf+24, "\x7F\xFE\x00\x00\x00\x00\x00\x00", 8);

    /* Bytes 32-35 contain the time zone differential */
    memset (buf+32, '\0', 4);

    /* Bytes 36-39 contain version, level, and service level */
    buf[36] = ver;
    buf[37] = rel;
    buf[38] = 0x00;
    buf[39] = 0x00;

#if 0
    logmsg ("Diagnose X\'000\':"
            "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X "
            "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n\t\t"
            "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X "
            "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n\t\t"
            "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
            buf[0], buf[1], buf[2], buf[3],
            buf[4], buf[5], buf[6], buf[7],
            buf[8], buf[9], buf[10], buf[11],
            buf[12], buf[13], buf[14], buf[15],
            buf[16], buf[17], buf[18], buf[19],
            buf[20], buf[21], buf[22], buf[23],
            buf[24], buf[25], buf[26], buf[27],
            buf[28], buf[29], buf[30], buf[31],
            buf[32], buf[33], buf[34], buf[35],
            buf[36], buf[37], buf[38], buf[39]);
#endif

    /* Enforce maximum length to store */
    if (idlen > sizeof(buf))
        idlen = sizeof(buf);

    /* Store the extended identification code at operand address */
    vstorec (buf, idlen-1, idaddr, r1, regs);

    /* Deduct number of bytes from the R2 register */
    regs->gpr[r2] -= idlen;

} /* end function extid_call */

/*-------------------------------------------------------------------*/
/* Process CP command (Function code 0x008)                          */
/*-------------------------------------------------------------------*/
static int cpcmd_call (int r1, int r2, REGS *regs)
{
int     i;                              /* Array subscript           */
int     cc = 0;                         /* Condition code            */
U32     cmdaddr;                        /* Address of command string */
U32     cmdlen;                         /* Length of command string  */
U32     respadr;                        /* Address of response buffer*/
U32     maxrlen;                        /* Length of response buffer */
U32     resplen;                        /* Length of actual response */
BYTE    cmdflags;                       /* Command flags             */
#define CMDFLAGS_REJPASSW       0x80    /* Reject password in command*/
#define CMDFLAGS_RESPONSE       0x40    /* Return response in buffer */
#define CMDFLAGS_REQPASSW       0x20    /* Prompt for password       */
#define CMDFLAGS_RESERVED       0x1F    /* Reserved bits, must be 0  */
BYTE    buf[256];                       /* Command buffer (ASCIIZ)   */
BYTE    resp[256];                      /* Response buffer (ASCIIZ)  */

    /* Obtain command address from R1 register */
    cmdaddr = regs->gpr[r1];

    /* Obtain command length and flags from R2 register */
    cmdflags = regs->gpr[r2] >> 24;
    cmdlen = regs->gpr[r2] & 0x00FFFFFF;

    /* Program check if invalid flags, or if command string
       is too long, or if response buffer is specified and
       registers are consecutive or either register
       specifies register 15 */
    if ((cmdflags & CMDFLAGS_RESERVED) || cmdlen > sizeof(buf)-1
        || ((cmdflags & CMDFLAGS_RESPONSE)
            && (r1 == 15 || r2 == 15 || r1 == r2 + 1 || r2 == r1 + 1)))
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Put machine into stopped state if command length is zero */
    if (cmdlen == 0)
    {
        regs->cpustate = CPUSTATE_STOPPED;
        return 0;
    }

    /* Obtain the command string from storage */
    vfetchc (buf, cmdlen-1, cmdaddr, r1, regs);

    /* Display the command on the console */
    for (i = 0; i < cmdlen; i++)
        buf[i] = ebcdic_to_ascii[buf[i]];
    buf[i] = '\0';
    logmsg ("HHC660I %s\n", buf);

    /* Store the response and set length if response requested */
    if (cmdflags & CMDFLAGS_RESPONSE)
    {
        strcpy (resp, "HHC661I Command complete");
        resplen = strlen(resp);
        for (i = 0; i < resplen; i++)
            resp[i] = ascii_to_ebcdic[resp[i]];

        respadr = regs->gpr[r1+1];
        maxrlen = regs->gpr[r2+1];

        if (resplen <= maxrlen)
        {
            vstorec (resp, resplen-1, respadr, r1+1, regs);
            regs->gpr[r2+1] = resplen;
            cc = 0;
        }
        else
        {
            vstorec (resp, maxrlen-1, respadr, r1+1, regs);
            regs->gpr[r2+1] = resplen - maxrlen;
            cc = 1;
        }
    }

    /* Set R2 register to CP completion code */
    regs->gpr[r2] = 0;

    /* Return condition code */
    return cc;

} /* end function cpcmd_call */

/*-------------------------------------------------------------------*/
/* Access Re-IPL data (Function code 0x0B0)                          */
/*-------------------------------------------------------------------*/
static void access_reipl_data (int r1, int r2, REGS *regs)
{
U32     bufadr;                         /* Real addr of data buffer  */
U32     buflen;                         /* Length of data buffer     */

    /* Obtain buffer address and length from R1 and R2 registers */
    bufadr = regs->gpr[r1];
    buflen = regs->gpr[r2];

    /* Program check if buffer length is negative */
    if ((S32)buflen < 0)
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return;
    }

    /* Store IPL information if buffer length is non-zero */
    if (buflen > 0)
    {
        /* Store one byte of zero to indicate no IPL information */
        vstoreb (0, bufadr, USE_REAL_ADDR, regs);
    }

    /* Return code 4 means no re-IPL information available */
    regs->gpr[r2] = 4;

} /* end function access_reipl_data */

/*-------------------------------------------------------------------*/
/* Pseudo Timer Extended (Function code 0x270)                       */
/* Pseudo Timer (Function code 0x00C)                                */
/*-------------------------------------------------------------------*/
static void pseudo_timer (U32 code, int r1, int r2, REGS *regs)
{
int     i;                              /* Array subscript           */
time_t  timeval;                        /* Current time              */
struct  tm *tmptr;                      /* -> Current time structure */
U32     bufadr;                         /* Real addr of data buffer  */
U32     buflen;                         /* Length of data buffer     */
BYTE    buf[64];                        /* Response buffer           */
BYTE    dattim[64];                     /* Date and time (EBCDIC)    */
#define DIAG_DATEFMT_SHORT      0x80    /* Date format mm/dd/yy      */
#define DIAG_DATEFMT_FULL       0x40    /* Date format mm/dd/yyyy    */
#define DIAG_DATEFMT_ISO        0x20    /* Date format yyyy-mm-dd    */
#define DIAG_DATEFMT_SYSDFLT    0x10    /* System-wide default format*/
static  BYTE timefmt[]="%m/%d/%y%H:%M:%S%m/%d/%Y%Y-%m-%d";

    /* Get the current date and time in EBCDIC */
    timeval = time(NULL);
    tmptr = localtime(&timeval);
    strftime(dattim, sizeof(dattim), timefmt, tmptr);
    for (i = 0; dattim[i] != '\0'; i++)
        dattim[i] = ascii_to_ebcdic[dattim[i]];

    /* Obtain buffer address and length from R1 and R2 registers */
    bufadr = regs->gpr[r1];
    buflen = regs->gpr[r2];

    /* Use length 32 if R2 is zero or function code is 00C */
    if (r2 == 0 || code == 0x00C)
        buflen = 32;

    /* Program check if R1 and R2 specify the same non-zero
       register number, or if buffer length is less than or
       equal to zero, or if buffer address is zero, or if
       buffer is not on a doubleword boundary */
    if ((r2 != 0 && r2 == r1)
        || (S32)buflen <= 0
        || bufadr == 0
        || (bufadr & 0x00000007))
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return;
    }

    /* Build the response buffer */
    memset (buf, 0x00, sizeof(buf));
    /* Bytes 0-7 contain the date as EBCDIC MM/DD/YY */
    memcpy (buf, dattim, 8);
    /* Bytes 8-15 contain the time as EBCDIC HH:MM:SS */
    memcpy (buf+8, dattim+8, 8);
    /* Bytes 16-23 contain the virtual CPU time used in microseconds */
    /* Bytes 24-31 contain the total CPU time used in microseconds */
    /* Bytes 32-41 contain the date as EBCDIC MM/DD/YYYY */
    memcpy (buf+32, dattim+16, 10);
    /* Bytes 42-47 contain binary zeroes */
    /* Bytes 48-57 contain the date as EBCDIC YYYY-MM-DD */
    memcpy (buf+48, dattim+26, 10);
    /* Byte 58 contains the diagnose 270 version code */
    buf[58] = 0x01;
    /* Byte 59 contains the user's default date format */
    buf[59] = DIAG_DATEFMT_ISO;
    /* Byte 60 contains the system default date format */
    buf[60] = DIAG_DATEFMT_ISO;
    /* Bytes 61-63 contain binary zeroes */

#if 0
    logmsg ("Diagnose X\'%3.3X\':"
            "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X "
            "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n\t\t"
            "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X "
            "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n\t\t"
            "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X "
            "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n\t\t"
            "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X "
            "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
            code, buf[0], buf[1], buf[2], buf[3],
            buf[4], buf[5], buf[6], buf[7],
            buf[8], buf[9], buf[10], buf[11],
            buf[12], buf[13], buf[14], buf[15],
            buf[16], buf[17], buf[18], buf[19],
            buf[20], buf[21], buf[22], buf[23],
            buf[24], buf[25], buf[26], buf[27],
            buf[28], buf[29], buf[30], buf[31],
            buf[32], buf[33], buf[34], buf[35],
            buf[36], buf[37], buf[38], buf[39],
            buf[40], buf[41], buf[42], buf[43],
            buf[44], buf[45], buf[46], buf[47],
            buf[48], buf[49], buf[50], buf[51],
            buf[52], buf[53], buf[54], buf[55],
            buf[56], buf[57], buf[58], buf[59],
            buf[60], buf[61], buf[63], buf[63]);
#endif

    /* Enforce maximum length to store */
    if (buflen > sizeof(buf))
        buflen = sizeof(buf);

    /* Store the response buffer at the operand location */
    vstorec (buf, buflen-1, bufadr, USE_REAL_ADDR, regs);

} /* end function pseudo_timer */

/*-------------------------------------------------------------------*/
/* Pending Page Release (Function code 0x214)                        */
/*-------------------------------------------------------------------*/
static int diag_ppagerel (int r1, int r2, REGS *regs)
{
U32     abs, start, end;                /* Absolute frame addresses  */
BYTE    skey;                           /* Specified storage key     */
BYTE    func;                           /* Function code...          */
#define DIAG214_EPR             0x00    /* Establish pending release */
#define DIAG214_CPR             0x01    /* Cancel pending release    */
#define DIAG214_CAPR            0x02    /* Cancel all pending release*/
#define DIAG214_CPRV            0x03    /* Cancel and validate       */

    /* Program check if R1 is not an even-numbered register */
    if (r1 & 1)
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Extract the function code from R1+1 register bits 24-31 */
    func = regs->gpr[r1+1] & 0xFF;

    /* Extract the start/end addresses from R1 and R1+1 registers */
    start = regs->gpr[r1] & STORAGE_KEY_PAGEMASK;
    end = regs->gpr[r1+1] & STORAGE_KEY_PAGEMASK;

    /* Validate start/end addresses if function is not CAPR */
    if (func != DIAG214_CAPR
        && (start > end || end >= sysblk.mainsize))
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Process depending on function code */
    switch (func)
    {
    case DIAG214_EPR:  /* Establish Pending Release */
        break;

    case DIAG214_CPR:  /* Cancel Pending Release */
    case DIAG214_CPRV: /* Cancel Pending Release and Validate */

        /* Do not set storage keys if R2 is register 0 */
        if (r2 == 0) break;

        /* Obtain key from R2 register bits 24-28 */
        skey = regs->gpr[r2] & (STORKEY_KEY | STORKEY_FETCH);

        /* Set storage key for each frame within specified range */
        for (abs = start; abs <= end; abs += STORAGE_KEY_PAGESIZE)
        {
            STORAGE_KEY(abs) &= ~(STORKEY_KEY | STORKEY_FETCH);
            STORAGE_KEY(abs) |= skey;
        } /* end for(abs) */

        break;

    case DIAG214_CAPR:  /* Cancel All Pending Releases */
        break;

    default:            /* Invalid function code */
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    } /* end switch(func) */

    /* Return condition code zero */
    return 0;

} /* end function diag_ppagerel */

/*-------------------------------------------------------------------*/
/* Diagnose instruction                                              */
/*-------------------------------------------------------------------*/
void diagnose_call (U32 code, int r1, int r2, REGS *regs)
{
U32             n;                      /* 32-bit operand value      */

    /* Program check if in problem state */
    if ( regs->psw.prob && (code < 0xF00 || code > 0xF08))
    {
        program_check (regs, PGM_PRIVILEGED_OPERATION_EXCEPTION);
        return;
    }

    switch(code) {

    case 0x000:
    /*---------------------------------------------------------------*/
    /* Diagnose 000: Store Extended Identification Code              */
    /*---------------------------------------------------------------*/
        extid_call (r1, r2, regs);
        break;

    case 0x008:
    /*---------------------------------------------------------------*/
    /* Diagnose 008: Virtual Console Function                        */
    /*---------------------------------------------------------------*/
        /* Process CP command and set condition code */
        regs->psw.cc = cpcmd_call (r1, r2, regs);
        break;

    case 0x00C:
    /*---------------------------------------------------------------*/
    /* Diagnose 00C: Pseudo Timer                                    */
    /*---------------------------------------------------------------*/
        pseudo_timer (code, r1, r2, regs);
        break;

    case 0x024:
    /*---------------------------------------------------------------*/
    /* Diagnose 024: Device Type and Features                        */
    /*---------------------------------------------------------------*/
        regs->psw.cc = diag_devtype (r1, r2, regs);
        break;

    case 0x044:
    /*---------------------------------------------------------------*/
    /* Diagnose 044: Voluntary Time Slice End                        */
    /*---------------------------------------------------------------*/
        scpend_call();
        break;

    case 0x05C:
    /*---------------------------------------------------------------*/
    /* Diagnose 05C: Error Message Editing                           */
    /*---------------------------------------------------------------*/
        /* This function is implemented as a no-operation */
        regs->psw.cc = 0;
        break;

    case 0x060:
    /*---------------------------------------------------------------*/
    /* Diagnose 060: Virtual Machine Storage Size                    */
    /*---------------------------------------------------------------*/
        /* Load main storage size in bytes into R1 register */
        regs->gpr[r1] = sysblk.mainsize;
        break;

    case 0x064:
    /*---------------------------------------------------------------*/
    /* Diagnose 064: Named Saved Segment Manipulation                */
    /*---------------------------------------------------------------*/
        /* Return code 44 cond code 2 means segment does not exist */
        regs->gpr[r2] = 44;
        regs->psw.cc = 2;
        break;

#ifdef FEATURE_MSSF_CALL
    case 0x080:
    /*---------------------------------------------------------------*/
    /* Diagnose 080: MSSF Call                                       */
    /*---------------------------------------------------------------*/
        regs->psw.cc = mssf_call (r1, r2, regs);
        break;
#endif /*FEATURE_MSSF_CALL*/

    case 0x0A4:
    /*---------------------------------------------------------------*/
    /* Diagnose 0A4: Synchronous I/O (Standard CMS Blocksize)        */
    /*---------------------------------------------------------------*/
        regs->psw.cc = syncblk_io (r1, r2, regs);
//      logmsg ("Diagnose X\'0A4\': CC=%d, R15=%8.8X\n",      /*debug*/
//              regs->psw.cc, regs->gpr[15]);                 /*debug*/
        break;

    case 0x0A8:
    /*---------------------------------------------------------------*/
    /* Diagnose 0A8: Synchronous General I/O                         */
    /*---------------------------------------------------------------*/
        regs->psw.cc = syncgen_io (r1, r2, regs);
//      logmsg ("Diagnose X\'0A8\': CC=%d, R15=%8.8X\n",      /*debug*/
//              regs->psw.cc, regs->gpr[15]);                 /*debug*/
        break;

    case 0x0B0:
    /*---------------------------------------------------------------*/
    /* Diagnose 0B0: Access Re-IPL Data                              */
    /*---------------------------------------------------------------*/
        access_reipl_data (r1, r2, regs);
        break;

    case 0x0DC:
    /*---------------------------------------------------------------*/
    /* Diagnose 0DC: Control Application Monitor Record Collection   */
    /*---------------------------------------------------------------*/
        /* This function is implemented as a no-operation */
        regs->gpr[r2] = 0;
        regs->psw.cc = 0;
        break;

    case 0x204:
    /*---------------------------------------------------------------*/
    /* Diagnose 204: LPAR RMF Interface                              */
    /*---------------------------------------------------------------*/
        diag204_call (r1, r2, regs);
        regs->psw.cc = 0;
        break;

    case 0x214:
    /*---------------------------------------------------------------*/
    /* Diagnose 214: Pending Page Release                            */
    /*---------------------------------------------------------------*/
        regs->psw.cc = diag_ppagerel (r1, r2, regs);
        break;

    case 0x23C:
    /*---------------------------------------------------------------*/
    /* Diagnose 23C: Address Space Services                          */
    /*---------------------------------------------------------------*/
        /* This function is implemented as a no-operation */
        regs->gpr[r2] = 0;
        break;

    case 0x264:
    /*---------------------------------------------------------------*/
    /* Diagnose 264: CP Communication                                */
    /*---------------------------------------------------------------*/
        /* This function is implemented as a no-operation */
        regs->psw.cc = 0;
        break;

    case 0x270:
    /*---------------------------------------------------------------*/
    /* Diagnose 270: Pseudo Timer Extended                           */
    /*---------------------------------------------------------------*/
        pseudo_timer (code, r1, r2, regs);
        break;

    case 0x274:
    /*---------------------------------------------------------------*/
    /* Diagnose 274: Set Timezone Interrupt Flag                     */
    /*---------------------------------------------------------------*/
        /* This function is implemented as a no-operation */
        regs->psw.cc = 0;
        break;

    case 0xF00:
    /*---------------------------------------------------------------*/
    /* Diagnose F00: Hercules normal mode                            */
    /*---------------------------------------------------------------*/
        sysblk.inststep = 0;
        break;

    case 0xF04:
    /*---------------------------------------------------------------*/
    /* Diagnose F04: Hercules single step mode                       */
    /*---------------------------------------------------------------*/
        sysblk.inststep = 1;
        break;

    case 0xF08:
    /*---------------------------------------------------------------*/
    /* Diagnose F08: Hercules get instruction counter                */
    /*---------------------------------------------------------------*/
        regs->gpr[r1] = (U32)regs->instcount;
        break;

    case 0xF0C:
    /*---------------------------------------------------------------*/
    /* Diagnose F0C: Set/reset bad frame indicator                   */
    /*---------------------------------------------------------------*/
        /* Load 4K block address from R2 register */
        n = regs->gpr[r2] & ADDRESS_MAXWRAP(regs);

        /* Convert real address to absolute address */
        n = APPLY_PREFIXING (n, regs->pxr);

        /* Addressing exception if block is outside main storage */
        if ( n >= sysblk.mainsize )
        {
            program_check (regs, PGM_ADDRESSING_EXCEPTION);
            break;
        }

        /* Update the storage key from R1 register bit 31 */
        STORAGE_KEY(n) &= ~(STORKEY_BADFRM);
        STORAGE_KEY(n) |= regs->gpr[r1] & STORKEY_BADFRM;

        break;

    default:
    /*---------------------------------------------------------------*/
    /* Diagnose xxx: Invalid function code                           */
    /*---------------------------------------------------------------*/
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return;

    } /* end switch(code) */

    return;

} /* end function diagnose_call */
