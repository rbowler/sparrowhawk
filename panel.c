/* PANEL.C      (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Control Panel Commands                       */

/*-------------------------------------------------------------------*/
/* This module is the control panel for the ESA/390 emulator.        */
/* It provides functions for displaying the PSW and registers        */
/* and a command line for requesting control operations such         */
/* as IPL, stop, start, single stepping, instruction tracing,        */
/* and storage displays. It displays messages issued by other        */
/* threads via the logmsg macro, and optionally also writes          */
/* all messages to a log file if stdout is redirected.               */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      breakpoint command contributed by Dan Horak                  */
/*      devinit command contributed by Jay Maynard                   */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Internal macro definitions                                        */
/*-------------------------------------------------------------------*/
#define SPACE           ((BYTE)' ')

/*-------------------------------------------------------------------*/
/* Display program status word                                       */
/*-------------------------------------------------------------------*/
static void display_psw (REGS *regs)
{
DWORD   dword;                          /* Doubleword work area      */

    store_psw (&regs->psw, dword);
    logmsg ("PSW=%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
            dword[0], dword[1], dword[2], dword[3],
            dword[4], dword[5], dword[6], dword[7]);

} /* end function display_psw */

/*-------------------------------------------------------------------*/
/* Display general purpose registers                                 */
/*-------------------------------------------------------------------*/
static void display_regs (REGS *regs)
{
int     i;

    for (i = 0; i < 16; i++)
        logmsg ("R%2.2d=%8.8X%s", i, regs->gpr[i],
            ((i & 0x03) == 0x03) ? "\n" : "\t");

} /* end function display_regs */

/*-------------------------------------------------------------------*/
/* Display control registers                                         */
/*-------------------------------------------------------------------*/
static void display_cregs (REGS *regs)
{
int     i;

    for (i = 0; i < 16; i++)
        logmsg ("CR%2.2d=%8.8X%s", i, regs->cr[i],
            ((i & 0x03) == 0x03) ? "\n" : "\t");

} /* end function display_cregs */

/*-------------------------------------------------------------------*/
/* Display access registers                                          */
/*-------------------------------------------------------------------*/
static void display_aregs (REGS *regs)
{
int     i;

    for (i = 0; i < 16; i++)
        logmsg ("AR%2.2d=%8.8X%s", i, regs->ar[i],
            ((i & 0x03) == 0x03) ? "\n" : "\t");

} /* end function display_aregs */

/*-------------------------------------------------------------------*/
/* Display floating point registers                                  */
/*-------------------------------------------------------------------*/
static void display_fregs (REGS *regs)
{

    logmsg ("FPR0=%8.8X %8.8X\t\tFPR2=%8.8X %8.8X\n"
            "FPR4=%8.8X %8.8X\t\tFPR6=%8.8X %8.8X\n",
            regs->fpr[0], regs->fpr[1], regs->fpr[2], regs->fpr[3],
            regs->fpr[4], regs->fpr[5], regs->fpr[6], regs->fpr[7]);

} /* end function display_fregs */

/*-------------------------------------------------------------------*/
/* Display real storage (up to 16 bytes, or until end of 4K page)    */
/* Returns number of characters placed in display buffer             */
/*-------------------------------------------------------------------*/
static int display_real (REGS *regs, U32 raddr, BYTE *buf)
{
U32     aaddr;                          /* Absolute storage address  */
int     blkid;                          /* Main storage 4K block id  */
int     i, j;                           /* Loop counters             */
int     n;                              /* Number of bytes in buffer */
BYTE    hbuf[40];                       /* Hexadecimal buffer        */
BYTE    cbuf[17];                       /* Character buffer          */
BYTE    c;                              /* Character work area       */

    n = sprintf (buf, "R:%8.8X:", raddr);
    aaddr = APPLY_PREFIXING (raddr, regs->pxr);
    if (aaddr >= sysblk.mainsize)
    {
        n += sprintf (buf+n, " Real address is not valid");
        return n;
    }

    blkid = aaddr >> 12;
    n += sprintf (buf+n, "K:%2.2X=", sysblk.storkeys[blkid]);

    memset (hbuf, SPACE, sizeof(hbuf));
    memset (cbuf, SPACE, sizeof(cbuf));

    for (i = 0, j = 0; i < 16; i++)
    {
        c = sysblk.mainstor[aaddr++];
        j += sprintf (hbuf+j, "%2.2X", c);
        if ((aaddr & 0x3) == 0x0) hbuf[j++] = SPACE;
        c = ebcdic_to_ascii[c];
        if (!isprint(c)) c = '.';
        cbuf[i] = c;
        if ((aaddr & 0xFFF) == 0x000) break;
    } /* end for(i) */

    n += sprintf (buf+n, "%36.36s %16.16s", hbuf, cbuf);
    return n;

} /* end function display_real */

/*-------------------------------------------------------------------*/
/* Convert virtual address to real address                           */
/*                                                                   */
/* Input:                                                            */
/*      vaddr   Virtual address to be translated                     */
/*      arn     Access register number                               */
/*      regs    CPU register context                                 */
/*      acctype Type of access (ACCTYPE_INSTFETCH, ACCTYPE_READ,     */
/*              or ACCTYPE_LRA)                                      */
/* Output:                                                           */
/*      raptr   Points to word in which real address is returned     */
/* Return value:                                                     */
/*      0=translation successful, non-zero=exception code            */
/*-------------------------------------------------------------------*/
static U16 virt_to_real (U32 *raptr, U32 vaddr, int arn, REGS *regs,
                        int acctype)
{
int     rc;                             /* Return code               */
U32     raddr;                          /* Real address              */
U16     xcode;                          /* Exception code            */
int     private = 0;                    /* 1=Private address space   */
int     protect = 0;                    /* 1=ALE or page protection  */

    if (REAL_MODE(&regs->psw) && acctype != ACCTYPE_LRA) {
        *raptr = vaddr;
        return 0;
    }

    rc = translate_addr (vaddr, arn, regs, acctype,
                        &raddr, &xcode, &private, &protect);
    if (rc) return xcode;

    *raptr = raddr;
    return 0;
} /* end function virt_to_real */

/*-------------------------------------------------------------------*/
/* Display instruction                                               */
/*-------------------------------------------------------------------*/
void display_inst (REGS *regs, BYTE *inst)
{
DWORD   dword;                          /* Doubleword work area      */
BYTE    opcode;                         /* Instruction operation code*/
int     b1, b2, x1;                     /* Register numbers          */
int     ilc;                            /* Instruction length        */
U32     addr1, addr2;                   /* Operand addresses         */
U32     raddr;                          /* Real address              */
U16     xcode;                          /* Exception code            */
BYTE    buf[100];                       /* Message buffer            */
int     n;                              /* Number of bytes in buffer */

    /* Display the PSW */
    store_psw (&regs->psw, dword);
    n = sprintf (buf,
                "PSW=%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X ",
                dword[0], dword[1], dword[2], dword[3],
                dword[4], dword[5], dword[6], dword[7]);

    /* Extract the opcode and determine the instruction length */
    opcode = inst[0];
    ilc = (opcode < 0x40) ? 2 : (opcode < 0xC0) ? 4 : 6;

    /* Display the instruction */
    n += sprintf (buf+n, "INST=%2.2X%2.2X", inst[0], inst[1]);
    if (ilc > 2) n += sprintf (buf+n, "%2.2X%2.2X", inst[2], inst[3]);
    if (ilc > 4) n += sprintf (buf+n, "%2.2X%2.2X", inst[4], inst[5]);
    logmsg ("%s\n", buf);

    /* Process the first storage operand */
    if (ilc > 2)
    {
        /* Calculate the effective address of the first operand */
        b1 = inst[2] >> 4;
        addr1 = ((inst[2] & 0x0F) << 8) | inst[3];
        if (b1 != 0)
        {
            addr1 += regs->gpr[b1];
            addr1 &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
        }

        /* Apply indexing for RX instructions */
        if ((opcode >= 0x40 && opcode <= 0x7F) || opcode == 0xB1)
        {
            x1 = inst[1] & 0x0F;
            if (x1 != 0)
            {
                addr1 += regs->gpr[x1];
                addr1 &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
            }
        }

        /* Display storage at first storage operand location */
        n = sprintf (buf, "V:%8.8X:", addr1);
        xcode = virt_to_real (&raddr, addr1, b1, regs,
                                (opcode == 0x44 ? ACCTYPE_INSTFETCH :
                                 opcode == 0xB1 ? ACCTYPE_LRA :
                                                  ACCTYPE_READ));
        if (xcode == 0)
            n += display_real (regs, raddr, buf+n);
        else
            n += sprintf (buf+n," Translation exception %4.4hX",xcode);

        logmsg ("%s\n", buf);
    }

    /* Process the second storage operand */
    if (ilc > 4)
    {
        /* Calculate the effective address of the second operand */
        b2 = inst[4] >> 4;
        addr2 = ((inst[4] & 0x0F) << 8) | inst[5];
        if (b2 != 0)
        {
            addr2 += regs->gpr[b2];
            addr2 &= (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
        }

        /* Display storage at second storage operand location */
        n = sprintf (buf, "V:%8.8X:", addr2);
        xcode = virt_to_real (&raddr, addr2, b2, regs, ACCTYPE_READ);
        if (xcode == 0)
            n += display_real (regs, raddr, buf+n);
        else
            n += sprintf (buf+n," Translation exception %4.4hX",xcode);

        logmsg ("%s\n", buf);
    }

    /* Display the general purpose registers */
    display_regs (regs);

} /* end function display_inst */

/*-------------------------------------------------------------------*/
/* Parse a storage range or storage alteration operand               */
/*                                                                   */
/* Valid formats for a storage range operand are:                    */
/*      startaddr                                                    */
/*      startaddr-endaddr                                            */
/*      startaddr.length                                             */
/* where startaddr, endaddr, and length are hexadecimal values.      */
/*                                                                   */
/* Valid format for a storage alteration operand is:                 */
/*      startaddr=hexstring (up to 32 pairs of digits)               */
/*                                                                   */
/* Return values:                                                    */
/*      0  = operand contains valid storage range display syntax;    */
/*           start/end of range is returned in saddr and eaddr       */
/*      >0 = operand contains valid storage alteration syntax;       */
/*           return value is number of bytes to be altered;          */
/*           start/end/value are returned in saddr, eaddr, newval    */
/*      -1 = error message issued                                    */
/*-------------------------------------------------------------------*/
static int parse_range (BYTE *operand, U32 *saddr, U32 *eaddr,
                        BYTE *newval)
{
int     rc;                             /* Return code               */
int     n;                              /* Number of bytes altered   */
int     h1, h2;                         /* Hexadecimal digits        */
BYTE   *s;                              /* Alteration value pointer  */
BYTE    delim;                          /* Operand delimiter         */
BYTE    c;                              /* Character work area       */

    rc = sscanf(operand, "%x%c%x%c", saddr, &delim, eaddr, &c);

    /* Process storage alteration operand */
    if (rc > 2 && delim == '=')
    {
        s = strchr (operand, '=');
        for (n = 0;;)
        {
            h1 = *(++s);
            h1 = toupper(h1);
            if (h1 == '\0') break;
            if (h1 == SPACE || h1 == '\t') continue;
            h2 = *(++s);
            h2 = toupper(h2);
            h1 = (h1 >= '0' && h1 <= '9') ? h1 - '0' :
                 (h1 >= 'A' && h1 <= 'F') ? h1 - 'A' + 10 : -1;
            h2 = (h2 >= '0' && h2 <= '9') ? h2 - '0' :
                 (h2 >= 'A' && h2 <= 'F') ? h2 - 'A' + 10 : -1;
            if (h1 < 0 || h2 < 0 || n >= 32)
            {
                logmsg ("Invalid value: %s\n", operand);
                return -1;
            }
            newval[n++] = (h1 << 4) | h2;
        } /* end for(n) */
        *saddr &= 0x7FFFFFFF;
        *eaddr = *saddr + n - 1;
        if (*eaddr > 0x7FFFFFFF)
        {
            logmsg ("Invalid wrap: %s\n", operand);
            return -1;
        }
        return n;
    }

    /* Process storage range operand */
    if (rc == 1)
        *eaddr = *saddr + 0x3F;
    else
    {
        if (rc != 3 || !(delim == '-' || delim == '.'))
        {
            logmsg ("Invalid operand: %s\n", operand);
            return -1;
        }
        if (delim == '.') *eaddr += *saddr - 1;
    }

    *saddr &= 0x7FFFFFFF;
    *eaddr &= 0x7FFFFFFF;

    /* Check for valid range */
    if (*eaddr < *saddr)
    {
        logmsg ("Invalid range: %s\n", operand);
        return -1;
    }

    return 0;
} /* end function parse_range */

/*-------------------------------------------------------------------*/
/* Execute a panel command                                           */
/*-------------------------------------------------------------------*/
static void *panel_command (void *cmdline)
{
BYTE    cmd[80];                        /* Copy of panel command     */
int     cpu;                            /* CPU engine number         */
REGS   *regs;                           /* -> CPU register context   */
U32     aaddr;                          /* Absolute storage address  */
U32     vaddr;                          /* Virtual storage address   */
U32     raddr;                          /* Real storage address      */
U32     eaddr;                          /* Storage ending address    */
U16     xcode;                          /* Exception code            */
U16     devnum;                         /* Device number             */
DEVBLK *dev;                            /* -> Device block           */
BYTE    c;                              /* Character work area       */
int     rc;                             /* Return code               */
int     i;                              /* Loop counter              */
int     oneorzero;                      /* 1=x+ command, 0=x-        */
BYTE   *onoroff;                        /* "on" or "off"             */
BYTE   *fname;                          /* -> File name (ASCIIZ)     */
int     fd;                             /* File descriptor           */
int     len;                            /* Number of bytes read      */
BYTE   *loadparm;                       /* -> IPL parameter (ASCIIZ) */
BYTE    buf[100];                       /* Message buffer            */
int     n;                              /* Number of bytes in buffer */
BYTE    newval[32];                     /* Storage alteration value  */
BYTE   *devascii;                       /* ASCII text device number  */
#define MAX_ARGS 10                     /* Max num of devinit args   */
int     devargc;                        /* Arg count for devinit     */
BYTE   *devargv[MAX_ARGS];              /* Arg array for devinit     */

    /* Copy panel command to work area */
    memset (cmd, 0, sizeof(cmd));
    strncpy (cmd, (BYTE*)cmdline, sizeof(cmd)-1);

    /* Echo the command to the control panel */
    if (cmd[0] != '\0')
        logmsg ("%s\n", cmd);

    /* Set target CPU for commands and displays */
    cpu = 0;
    regs = sysblk.regs + cpu;

    /* ? command - display help text */
    if (cmd[0] == '?')
    {
        logmsg ("Panel command summary:\n"
            "t+=trace, s+=step, t+devn=CCW trace, s+devn=CCW step\n"
            "g=go, psw=display psw, pr=prefix reg\n"
            "gpr=general purpose regs, cr=control regs\n"
            "ar=access regs, fpr=floating point regs\n"
            "v addr[.len] or v addr-addr = display virtual storage\n"
            "r addr[.len] or r addr-addr = display real storage\n"
            "v addr=value or r addr=value = alter storage\n"
            "b addr = set breakpoint, b- = delete breakpoint\n"
            "i devn=I/O attention interrupt, ext=external interrupt\n"
            "stop=stop CPU, start=start CPU, restart=PSW restart\n"
            "loadcore filename=load core image from file\n"
            "loadparm xxxxxxxx=set IPL parameter, ipl devn=IPL\n"
            "devinit devn arg [arg...] = reinitialize device\n"
            "quit/exit=terminate\n");
        return NULL;
    }

    /* g command - turn off single stepping and start CPU */
    if (strcmp(cmd,"g") == 0)
    {
        sysblk.inststep = 0;
        strcpy (cmd, "start");
    }

    /* start command (or just Enter) - start CPU */
    if (cmd[0] == '\0' || strcmp(cmd,"start") == 0)
    {
        /* Restart the CPU if it is in the stopped state */
        regs->cpustate = CPUSTATE_STARTED;

        /* Signal stopped CPUs to retest stopped indicator */
        obtain_lock (&sysblk.intlock);
        signal_condition (&sysblk.intcond);
        release_lock (&sysblk.intlock);

        return NULL;
    }

    /* stop command - stop CPU */
    if (strcmp(cmd,"stop") == 0)
    {
        regs->cpustate = CPUSTATE_STOPPING;
        return NULL;
    }

    /* x+ and x- commands - turn switches on or off */
    if (cmd[1] == '+' || cmd[1] == '-')
    {
        if (cmd[1] == '+') {
            oneorzero = 1;
            onoroff = "on";
        } else {
            oneorzero = 0;
            onoroff = "off";
        }

        /* t+ and t- commands - instruction tracing on/off */
        if (cmd[0]=='t' && cmd[2]=='\0')
        {
            sysblk.insttrace = oneorzero;
            logmsg ("Instruction tracing is now %s\n", onoroff);
            return NULL;
        }

        /* s+ and s- commands - instruction stepping on/off */
        if (cmd[0]=='s' && cmd[2]=='\0')
        {
            sysblk.inststep = oneorzero;
            logmsg ("Instruction stepping is now %s\n", onoroff);
            return NULL;
        }

        /* t+devn and t-devn commands - turn CCW tracing on/off */
        /* s+devn and s-devn commands - turn CCW stepping on/off */
        if ((cmd[0] == 't' || cmd[0] == 's')
            && sscanf(cmd+2, "%hx%c", &devnum, &c) == 1)
        {
            dev = find_device_by_devnum (devnum);
            if (dev == NULL)
            {
                logmsg ("Device number %4.4X not found\n", devnum);
                return NULL;
            }

            if (cmd[0] == 't')
            {
                dev->ccwtrace = oneorzero;
                logmsg ("CCW tracing is now %s for device %4.4X\n",
                    onoroff, devnum);
            } else {
                dev->ccwstep = oneorzero;
                logmsg ("CCW stepping is now %s for device %4.4X\n",
                    onoroff, devnum);
            }
            return NULL;
        }

    } /* end if(+ or -) */

    /* gpr command - display general purpose registers */
    if (strcmp(cmd,"gpr") == 0)
    {
        display_regs (regs);
        return NULL;
    }

    /* fpr command - display floating point registers */
    if (strcmp(cmd,"fpr") == 0)
    {
        display_fregs (regs);
        return NULL;
    }

    /* cr command - display control registers */
    if (strcmp(cmd,"cr") == 0)
    {
        display_cregs (regs);
        return NULL;
    }

    /* ar command - display access registers */
    if (strcmp(cmd,"ar") == 0)
    {
        display_aregs (regs);
        return NULL;
    }

    /* pr command - display prefix register */
    if (strcmp(cmd,"pr") == 0)
    {
        logmsg ("Prefix=%8.8X\n", regs->pxr);
        return NULL;
    }

    /* psw command - display program status word */
    if (strcmp(cmd,"psw") == 0)
    {
        display_psw (regs);
        return NULL;
    }

    /* restart command - generate restart interrupt */
    if (strcmp(cmd,"restart") == 0)
    {
        /* Indicate that a restart interrupt is pending */
        regs->restart = 1;
        logmsg ("Restart key depressed\n");

        /* Signal waiting CPUs that an interrupt is pending */
        obtain_lock (&sysblk.intlock);
        signal_condition (&sysblk.intcond);
        release_lock (&sysblk.intlock);

        return NULL;
    }

    /* r command - display or alter real storage */
    if (cmd[0] == 'r')
    {
        /* Parse the range or alteration operand */
        rc = parse_range (cmd+1, &raddr, &eaddr, newval);
        if (rc < 0) return NULL;

        /* Alter real storage */
        if (rc > 0)
        {
            for (i = 0; i < rc && raddr+i < sysblk.mainsize; i++)
            {
                aaddr = raddr + i;
                aaddr = APPLY_PREFIXING (aaddr, regs->pxr);
                sysblk.mainstor[aaddr] = newval[i];
                sysblk.storkeys[aaddr >> 12] |=
                                (STORKEY_REF | STORKEY_CHANGE);
            } /* end for(i) */
        }

        /* Display real storage */
        for (i = 0; i < 999 && raddr <= eaddr; i++)
        {
            display_real (regs, raddr, buf);
            logmsg ("%s\n", buf);
            raddr += 16;
        } /* end for(i) */

        return NULL;
    }

    /* v command - display or alter virtual storage */
    if (cmd[0] == 'v')
    {
        /* Reject the command if no segment table */
        if (regs->cr[1] == 0)
        {
            logmsg ("Virtual storage not available\n");
            return NULL;
        }

        /* Parse the range or alteration operand */
        rc = parse_range (cmd+1, &vaddr, &eaddr, newval);
        if (rc < 0) return NULL;

        /* Alter virtual storage */
        if (rc > 0
            && virt_to_real (&raddr, vaddr, 0, regs, ACCTYPE_LRA) == 0
            && virt_to_real (&raddr, eaddr, 0, regs, ACCTYPE_LRA) == 0)
        {
            for (i = 0; i < rc && raddr+i < sysblk.mainsize; i++)
            {
                virt_to_real (&raddr, vaddr+i, 0, regs, ACCTYPE_LRA);
                aaddr = APPLY_PREFIXING (raddr, regs->pxr);
                sysblk.mainstor[aaddr] = newval[i];
                sysblk.storkeys[aaddr >> 12] |=
                                (STORKEY_REF | STORKEY_CHANGE);
            } /* end for(i) */
        }

        /* Display virtual storage */
        for (i = 0; i < 999 && vaddr <= eaddr; i++)
        {
            n = sprintf (buf, "V:%8.8X:", vaddr);
            xcode = virt_to_real (&raddr, vaddr, 0, regs,
                    ACCTYPE_LRA);
            if (xcode == 0)
                n += display_real (regs, raddr, buf+n);
            else
                n += sprintf (buf+n,
                        " Translation exception %4.4hX",
                        xcode);
            logmsg ("%s\n", buf);
            vaddr += 16;
        } /* end for(i) */

        return NULL;
    }

    /* b command - set breakpoint */
    if (cmd[0] == 'b')
    {
        if (cmd[1] == '-')
        {
            logmsg ("Deleting breakpoint\n");
            sysblk.instbreak = 0;
            return NULL;
        }

        if (sscanf(cmd+1, "%x%c", &sysblk.breakaddr, &c) == 1)
        {
            sysblk.instbreak = 1;
            logmsg ("Setting breakpoint at %8.8X\n", sysblk.breakaddr);
            return NULL;
        }
    }

    /* i command - generate I/O attention interrupt for device */
    if (cmd[0] == 'i'
        && sscanf(cmd+1, "%hx%c", &devnum, &c) == 1)
    {
        dev = find_device_by_devnum (devnum);
        if (dev == NULL)
        {
            logmsg ("Device number %4.4X not found\n", devnum);
            return NULL;
        }

        /* Obtain the device lock */
        obtain_lock (&dev->lock);

        /* Raise attention interrupt for the device */
        rc = device_attention (dev, CSW_ATTN);

        /* Release the device lock */
        release_lock (&dev->lock);

        /* Issue error message is device was busy or pending */
        if (rc != 0)
        {
            logmsg ("Device %4.4X busy or interrupt pending\n",
                    devnum);
        }

        return NULL;
    } /* end if(i) */

    /* ext command - generate external interrupt */
    if (strcmp(cmd,"ext") == 0)
    {
        sysblk.intkey = 1;
        logmsg ("Interrupt key depressed\n");
        return NULL;
    }

    /* loadcore filename command - load a core image file */
    if (memcmp(cmd,"loadcore",8)==0)
    {
        /* Locate the operand */
        fname = strtok (cmd + 8, " \t");
        if (fname == NULL)
        {
            logmsg ("loadcore rejected: filename missing\n");
            return NULL;
        }

        /* Command is valid only when CPU is stopped */
        if (regs->cpustate != CPUSTATE_STOPPED)
        {
            logmsg ("loadcore rejected: CPU not stopped\n");
            return NULL;
        }

        /* Open the specified file name */
        fd = open (fname, O_RDONLY);
        if (fd < 0)
        {
            logmsg ("Cannot open %s: %s\n",
                    fname, strerror(errno));
            return NULL;
        }

        /* Read the file into absolute storage */
        logmsg ("Loading %s\n", fname);

        len = read (fd, sysblk.mainstor, sysblk.mainsize);
        if (len < 0)
        {
            logmsg ("Cannot read %s: %s\n",
                    fname, strerror(errno));
            close (fd);
            return NULL;
        }

        /* Close file and issue status message */
        close (fd);
        logmsg ("%d bytes read from %s\n",
                len, fname);
        return NULL;
    }

    /* loadparm xxxxxxxx command - set IPL parameter */
    if (memcmp(cmd,"loadparm",8)==0)
    {
        loadparm = strtok (cmd + 8, " \t");
        /* Update IPL parameter if operand is specified */
        if (loadparm != NULL)
        {
            memset (sysblk.loadparm, 0x4B, 8);
            for (i = 0; i < strlen(loadparm) && i < 8; i++)
            {
                c = loadparm[i];
                c = toupper(c);
                if (!isprint(c)) c = '.';
                sysblk.loadparm[i] = ascii_to_ebcdic[c];
            }
        }
        /* Display IPL parameter */
        logmsg ("LOADPARM=%c%c%c%c%c%c%c%c\n",
                ebcdic_to_ascii[sysblk.loadparm[0]],
                ebcdic_to_ascii[sysblk.loadparm[1]],
                ebcdic_to_ascii[sysblk.loadparm[2]],
                ebcdic_to_ascii[sysblk.loadparm[3]],
                ebcdic_to_ascii[sysblk.loadparm[4]],
                ebcdic_to_ascii[sysblk.loadparm[5]],
                ebcdic_to_ascii[sysblk.loadparm[6]],
                ebcdic_to_ascii[sysblk.loadparm[7]]);
        return NULL;
    }

    /* ipl xxxx command - IPL from device xxxx */
    if (memcmp(cmd,"ipl",3)==0)
    {
        if (regs->cpustate != CPUSTATE_STOPPED)
        {
            logmsg ("ipl rejected: CPU not stopped\n");
            return NULL;
        }
        if (sscanf(cmd+3, "%hx%c", &devnum, &c) != 1)
        {
            logmsg ("Device number %s is invalid\n", cmd+3);
            return NULL;
        }
        load_ipl (devnum, regs);
        return NULL;
    }

    /* quit or exit command - terminate the emulator */
    if (strcmp(cmd,"quit") == 0 || strcmp(cmd,"exit") == 0)
    {
        exit(0);
    }

    /* devinit command - assign/open a file for a configured device */
    if (memcmp(cmd,"devinit",7)==0)
    {
        devascii = strtok(cmd+7," \t");
        if (devascii == NULL
            || sscanf(devascii, "%hx%c", &devnum, &c) != 1)
        {
            logmsg ("Device number %s is invalid\n",devascii);
            return NULL;
        }
        dev = find_device_by_devnum (devnum);
        if (dev == NULL)
        {
            logmsg ("Device number %4.4X not found\n", devnum);
            return NULL;
        }

        /* Set up remaining arguments for initialization handler */
        for (devargc = 0; devargc < MAX_ARGS &&
            (devargv[devargc] = strtok(NULL," \t")) != NULL;
            devargc++);

        /* Obtain the device lock */
        obtain_lock (&dev->lock);

        /* Reject if device is busy or interrupt pending */
        if (dev->busy || dev->pending
            || (dev->scsw.flag3 & SCSW3_SC_PEND))
        {
            release_lock (&dev->lock);
            logmsg ("Device %4.4X busy or interrupt pending\n",
                    devnum);
            return NULL;
        }

        /* Close the existing file, if any */
        if (dev->fd > 2)
        {
            if (close(dev->fd) < 0)
            {
                /* Close failed; log the event */
                logmsg ("Error closing file %s: %s\n",
                        dev->filename,strerror(errno));
            }
            dev->fd = -1;
        }

        /* Call the device init routine to do the hard work */
        if (devargc > 0)
        {
            rc = (*(dev->devinit))(dev, devargc, devargv);
            if (rc < 0)
            {
                logmsg ("Initialization failed for device %4.4X\n",
                        devnum);
            } else {
                logmsg ("Device %4.4X initialized\n",
                        devnum);
            }
        }

        /* Raise unsolicited device end interrupt for the device */
        device_attention (dev, CSW_DE);

        /* Release the device lock */
        release_lock (&dev->lock);

        return NULL;
    }

    /* Invalid command - display error message */
    logmsg ("%s command invalid. Enter ? for help\n", cmd);
    return NULL;

} /* end function panel_command */

/*-------------------------------------------------------------------*/
/* Definitions for ANSI control sequences                            */
/*-------------------------------------------------------------------*/
#define ANSI_SAVE_CURSOR        "\x1B[s"
#define ANSI_CURSOR_UP          "\x1B[1A"
#define ANSI_CURSOR_DOWN        "\x1B[1B"
#define ANSI_CURSOR_FORWARD     "\x1B[1C"
#define ANSI_CURSOR_BACKWARD    "\x1B[1D"
#define ANSI_POSITION_CURSOR    "\x1B[%d;%dH"
#define ANSI_ROW1_COL1          "\x1B[1;1H"
#define ANSI_ROW1_COL80         "\x1B[1;80H"
#define ANSI_ROW22_COL80        "\x1B[22;80H"
#define ANSI_ROW23_COL1         "\x1B[23;1H"
#define ANSI_ROW24_COL1         "\x1B[24;1H"
#define ANSI_ROW24_COL79        "\x1B[24;79H"
#define ANSI_BLACK_GREEN        "\x1B[30;42m"
#define ANSI_YELLOW_RED         "\x1B[33;1;41m"
#define ANSI_WHITE_BLACK        "\x1B[0m"
#define ANSI_HIGH_INTENSITY     "\x1B[1m"
#define ANSI_ERASE_EOL          "\x1B[K"
#define ANSI_ERASE_SCREEN       "\x1B[2J"
#define ANSI_RESTORE_CURSOR     "\x1B[u"

/*-------------------------------------------------------------------*/
/* Definitions for keyboard input sequences                          */
/*-------------------------------------------------------------------*/
#define KBD_HOME                "\x1B[1~"
#define KBD_INSERT              "\x1B[2~"
#define KBD_DELETE              "\x1B[3~"
#define KBD_END                 "\x1B[4~"
#define KBD_PAGE_UP             "\x1B[5~"
#define KBD_PAGE_DOWN           "\x1B[6~"
#define KBD_UP_ARROW            "\x1B[A"
#define KBD_DOWN_ARROW          "\x1B[B"
#define KBD_RIGHT_ARROW         "\x1B[C"
#define KBD_LEFT_ARROW          "\x1B[D"

/*-------------------------------------------------------------------*/
/* Cleanup routine                                                   */
/*-------------------------------------------------------------------*/
static void system_cleanup (void)
{
struct termios kbattr;                  /* Terminal I/O structure    */

    /* Restore the terminal mode */
    tcgetattr (STDIN_FILENO, &kbattr);
    kbattr.c_lflag |= (ECHO | ICANON);
    tcsetattr (STDIN_FILENO, TCSANOW, &kbattr);

    /* Reset the cursor position */
    fprintf (stderr,
            ANSI_ROW24_COL79
            ANSI_WHITE_BLACK
            "\n");
    fprintf (stderr,
            "Hercules %s terminated\n",
            ARCHITECTURE_NAME);

} /* end function system_cleanup */

/*-------------------------------------------------------------------*/
/* Panel display thread                                              */
/*                                                                   */
/* This function runs on the main thread.  It receives messages      */
/* from other threads and displays them on the screen.  It accepts   */
/* panel commands from the keyboard and executes them.  It samples   */
/* the PSW periodically and displays it on the screen status line.   */
/*                                                                   */
/* Note that this routine must not attempt to write messages into    */
/* the message pipe by calling the logmsg function, because this     */
/* will cause a deadlock when the pipe becomes full during periods   */
/* of high message activity.  For this reason a separate thread is   */
/* created to process all commands entered.                          */
/*-------------------------------------------------------------------*/
void panel_display (void)
{
int     rc;                             /* Return code               */
int     i, n;                           /* Array subscripts          */
BYTE   *msgbuf;                         /* Circular message buffer   */
int     msgslot = 0;                    /* Next available buffer slot*/
int     nummsgs = 0;                    /* Number of msgs in buffer  */
int     firstmsgn = 0;                  /* Number of first message to
                                           be displayed relative to
                                           oldest message in buffer  */
#define MAX_MSGS                800     /* Number of slots in buffer */
#define MSG_SIZE                80      /* Size of one message       */
#define BUF_SIZE    (MAX_MSGS*MSG_SIZE) /* Total size of buffer      */
#define NUM_LINES               22      /* Number of scrolling lines */
#define CMD_SIZE                60      /* Length of command line    */
int     cpu;                            /* CPU engine number         */
REGS   *regs;                           /* -> CPU register context   */
DWORD   curpsw;                         /* Current PSW               */
DWORD   prvpsw;                         /* Previous PSW              */
BYTE    prvstate = 0xFF;                /* Previous stopped state    */
U64     prvicount = 0;                  /* Previous instruction count*/
BYTE    pswmask;                        /* PSW interruption mask     */
BYTE    pswwait;                        /* PSW wait state bit        */
BYTE    redraw_msgs;                    /* 1=Redraw message area     */
BYTE    redraw_cmd;                     /* 1=Redraw command line     */
BYTE    redraw_status;                  /* 1=Redraw status line      */
BYTE    readbuf[MSG_SIZE];              /* Message read buffer       */
int     readoff = 0;                    /* Number of bytes in readbuf*/
BYTE    cmdline[CMD_SIZE+1];            /* Command line buffer       */
int     cmdoff = 0;                     /* Number of bytes in cmdline*/
TID     cmdtid;                         /* Command thread identifier */
BYTE    c;                              /* Character work area       */
FILE   *confp;                          /* Console file pointer      */
FILE   *logfp;                          /* Log file pointer          */
struct termios kbattr;                  /* Terminal I/O structure    */
BYTE    kbbuf[6];                       /* Keyboard input buffer     */
int     kblen;                          /* Number of chars in kbbuf  */
int     pipefd;                         /* Pipe file descriptor      */
int     keybfd;                         /* Keyboard file descriptor  */
int     maxfd;                          /* Highest file descriptor   */
fd_set  readset;                        /* Select file descriptors   */
struct  timeval tv;                     /* Select timeout structure  */
#define INACTIVITY_INTERVAL     500     /* Interval in milliseconds  */

    /* Obtain storage for the circular message buffer */
    msgbuf = malloc (BUF_SIZE);
    if (msgbuf == NULL)
    {
        fprintf (stderr,
                "panel: Cannot obtain message buffer: %s\n",
                strerror(errno));
        return;
    }

    /* If stdout is not redirected, then write screen output
       to stdout and do not produce a log file.  If stdout is
       redirected, then write screen output to stderr and
       write the logfile to stdout */
    if (isatty(STDOUT_FILENO))
    {
        confp = stdout;
        logfp = NULL;
    }
    else
    {
        confp = stderr;
        logfp = stdout;
    }

    /* Set screen output stream to fully buffered */
    setvbuf (confp, NULL, _IOFBF, 0);

    /* Set up the input file descriptors */
    pipefd = sysblk.msgpiper;
    keybfd = STDIN_FILENO;

    /* Register the system cleanup exit routine */
    atexit (system_cleanup);

    /* Put the terminal into cbreak mode */
    tcgetattr (keybfd, &kbattr);
    kbattr.c_lflag &= ~(ECHO | ICANON);
    kbattr.c_cc[VMIN] = 0;
    kbattr.c_cc[VTIME] = 0;
    tcsetattr (keybfd, TCSANOW, &kbattr);

    /* Clear the screen */
    fprintf (confp,
            ANSI_ERASE_SCREEN);
    redraw_msgs = 1;
    redraw_cmd = 1;
    redraw_status = 1;

    /* Set target CPU for commands and displays */
    cpu = 0;
    regs = sysblk.regs + cpu;

    /* Process messages and commands */
    while (1)
    {
        /* Set the file descriptors for select */
        FD_ZERO (&readset);
        FD_SET (keybfd, &readset);
        FD_SET (pipefd, &readset);
        maxfd = keybfd;
        if (pipefd > maxfd) maxfd = pipefd;

        /* Wait for a message to arrive, a key to be pressed,
           or the inactivity interval to expire */
        tv.tv_sec = INACTIVITY_INTERVAL / 1000;
        tv.tv_usec = (INACTIVITY_INTERVAL * 1000) % 1000000;
        rc = select (maxfd + 1, &readset, NULL, NULL, &tv);
        if (rc < 0 )
        {
            if (errno == EINTR) continue;
            fprintf (stderr,
                    "panel: select: %s\n",
                    strerror(errno));
            break;
        }

        /* If keyboard input has arrived then process it */
        if (FD_ISSET(keybfd, &readset))
        {
            /* Read character(s) from the keyboard */
            kblen = read (keybfd, kbbuf, sizeof(kbbuf)-1);
            if (kblen < 0)
            {
                fprintf (stderr,
                        "panel: keyboard read: %s\n",
                        strerror(errno));
                break;
            }
            kbbuf[kblen] = '\0';

            /* Process characters in the keyboard buffer */
            for (i = 0; i < kblen; )
            {
                /* Test for home command */
                if (strcmp(kbbuf+i, KBD_HOME) == 0)
                {
                    if (firstmsgn == 0) break;
                    firstmsgn = 0;
                    redraw_msgs = 1;
                    break;
                }

                /* Test for end command */
                if (strcmp(kbbuf+i, KBD_END) == 0)
                {
                    if (firstmsgn + NUM_LINES >= nummsgs) break;
                    firstmsgn = nummsgs - NUM_LINES;
                    redraw_msgs = 1;
                    break;
                }

                /* Test for line up command */
                if (strcmp(kbbuf+i, KBD_UP_ARROW) == 0)
                {
                    if (firstmsgn == 0) break;
                    firstmsgn--;
                    redraw_msgs = 1;
                    break;
                }

                /* Test for line down command */
                if (strcmp(kbbuf+i, KBD_DOWN_ARROW) == 0)
                {
                    if (firstmsgn + NUM_LINES >= nummsgs) break;
                    firstmsgn++;
                    redraw_msgs = 1;
                    break;
                }

                /* Test for page up command */
                if (strcmp(kbbuf+i, KBD_PAGE_UP) == 0)
                {
                    if (firstmsgn == 0) break;
                    firstmsgn -= NUM_LINES;
                    if (firstmsgn < 0) firstmsgn = 0;
                    redraw_msgs = 1;
                    break;
                }

                /* Test for page down command */
                if (strcmp(kbbuf+i, KBD_PAGE_DOWN) == 0)
                {
                    if (firstmsgn + NUM_LINES >= nummsgs) break;
                    firstmsgn += NUM_LINES;
                    if (firstmsgn > nummsgs - NUM_LINES)
                        firstmsgn = nummsgs - NUM_LINES;
                    redraw_msgs = 1;
                    break;
                }

                /* Ignore any other escape sequence */
                if (kbbuf[i] == '\x1B')
                    break;

                /* Process backspace character */
                if (kbbuf[i] == '\b' || kbbuf[i] == '\x7F'
                    || strcmp(kbbuf+i, KBD_LEFT_ARROW) == 0)
                {
                    if (cmdoff > 0) cmdoff--;
                    i++;
                    redraw_cmd = 1;
                    break;
                }

                /* Process the command if newline was read */
                if (kbbuf[i] == '\n')
                {
                    cmdline[cmdoff] = '\0';
                    create_thread (&cmdtid, &sysblk.detattr,
                                panel_command, cmdline);
                    cmdoff = 0;
                    redraw_cmd = 1;
                    break;
                }

                /* Ignore non-printable characters */
                if (!isprint(kbbuf[i]))
                {
                    logmsg ("%2.2X\n", kbbuf[i]);
                    i++;
                    continue;
                }

                /* Append the character to the command buffer */
                if (cmdoff < CMD_SIZE-1) cmdline[cmdoff++] = kbbuf[i];
                i++;
                redraw_cmd = 1;

            } /* end for(i) */
        }

        /* If a message has arrived then receive it */
        if (FD_ISSET(pipefd, &readset))
        {
            /* Clear the message buffer */
            memset (readbuf, SPACE, MSG_SIZE);

            /* Read message bytes until newline */
            while (1)
            {
                /* Read a byte from the message pipe */
                rc = read (pipefd, &c, 1);
                if (rc < 1)
                {
                    fprintf (stderr,
                            "panel: message pipe read: %s\n",
                            strerror(errno));
                    break;
                }

                /* Exit if newline was read */
                if (c == '\n') break;

                /* Handle tab character */
                if (c == '\t')
                {
                    readoff += 8;
                    readoff &= 0xFFFFFFF8;
                    continue;
                }

                /* Eliminate non-printable characters */
                if (!isprint(c)) c = SPACE;

                /* Append the byte to the read buffer */
                if (readoff < MSG_SIZE) readbuf[readoff++] = c;

            } /* end while */

            /* Exit if read was unsuccessful */
            if (rc < 1) break;

            /* Copy the message to the log file if present */
            if (logfp != NULL)
            {
                fprintf (logfp, "%.*s\n", readoff, readbuf);
                if (ferror(logfp))
                {
                    fclose (logfp);
                    logfp = NULL;
                }
            }

            /* Copy message to circular buffer and empty read buffer */
            memcpy (msgbuf + (msgslot * MSG_SIZE), readbuf, MSG_SIZE);
            readoff = 0;

            /* Update message count and next available slot number */
            if (nummsgs < MAX_MSGS)
                msgslot = ++nummsgs;
            else
                msgslot++;
            if (msgslot == MAX_MSGS) msgslot = 0;

            /* Calculate the first line to display */
            firstmsgn = nummsgs - NUM_LINES;
            if (firstmsgn < 0) firstmsgn = 0;

            /* Set the display update indicator */
            redraw_msgs = 1;

        }

        /* Obtain the PSW for target CPU */
        store_psw (&regs->psw, curpsw);

        /* Set the display update indicator if the PSW has changed
           or if the instruction counter has changed, or if
           the CPU stopped state has changed */
        if (memcmp(curpsw, prvpsw, sizeof(curpsw)) != 0
            || regs->instcount != prvicount
            || regs->cpustate != prvstate)
        {
            redraw_status = 1;
            memcpy (prvpsw, curpsw, sizeof(prvpsw));
            prvicount = regs->instcount;
            prvstate = regs->cpustate;
        }

        /* Rewrite the screen if display update indicators are set */
        if (redraw_msgs)
        {
            /* Display messages in scrolling area */
            for (i=0; i < NUM_LINES && firstmsgn + i < nummsgs; i++)
            {
                n = (nummsgs < MAX_MSGS) ? 0 : msgslot;
                n += firstmsgn + i;
                if (n >= MAX_MSGS) n -= MAX_MSGS;
                fprintf (confp,
                        ANSI_POSITION_CURSOR
                        ANSI_WHITE_BLACK,
                        i+1, 1);
                fwrite (msgbuf + (n * MSG_SIZE), MSG_SIZE, 1, confp);
            }

            /* Display the scroll indicators */
            if (firstmsgn > 0)
                fprintf (confp, ANSI_ROW1_COL80 "+");
            if (firstmsgn + i < nummsgs)
                fprintf (confp, ANSI_ROW22_COL80 "V");
        } /* end if(redraw_msgs) */

        if (redraw_status)
        {
            /* Isolate the PSW interruption mask and wait bit */
            pswmask = (curpsw[1] & 0x08) ?
                            (curpsw[0] & 0x03) : curpsw[0];
            pswwait = curpsw[1] & 0x02;

            /* Display the PSW and instruction counter for CPU 0 */
            fprintf (confp,
                    ANSI_ROW24_COL1
                    ANSI_YELLOW_RED
                    "PSW=%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X"
                    " %s (%llu instructions executed)"
                    ANSI_ERASE_EOL,
                    curpsw[0], curpsw[1], curpsw[2], curpsw[3],
                    curpsw[4], curpsw[5], curpsw[6], curpsw[7],
                    (regs->cpustate == CPUSTATE_STOPPING ? "STOPPING" :
                        regs->cpustate == CPUSTATE_STOPPED ? "STOPPED" :
                        (pswwait && pswmask == 0) ? "DISABLED WAIT" :
                        pswwait ? "ENABLED WAIT" :
                        "RUNNING"),
                    regs->instcount);
        } /* end if(redraw_status) */

        if (redraw_cmd)
        {
            /* Display the command line */
            fprintf (confp,
                    ANSI_ROW23_COL1
                    ANSI_WHITE_BLACK
                    ANSI_HIGH_INTENSITY
                    "Command ==> "
                    ANSI_WHITE_BLACK);

            for (i = 0; i < cmdoff; i++)
                putc (cmdline[i], confp);

            fprintf (confp,
                    ANSI_ERASE_EOL);

        } /* end if(redraw_cmd) */

        /* Flush screen buffer and reset display update indicators */
        if (redraw_msgs || redraw_cmd || redraw_status)
        {
            fprintf (confp,
                    ANSI_POSITION_CURSOR,
                    23, 13+cmdoff);
            fflush (confp);
            redraw_msgs = 0;
            redraw_cmd = 0;
            redraw_status = 0;
        }

    } /* end while */

    return;

} /* end function panel_display */

