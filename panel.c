/* PANEL.C      (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Control Panel Commands                       */

/*-------------------------------------------------------------------*/
/* This module is the control panel for the ESA/390 emulator.        */
/* It provides functions for displaying the PSW and registers        */
/* and a command line for requesting control operations such         */
/* as single stepping and instruction tracing.                       */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Display program status word                                       */
/*-------------------------------------------------------------------*/
static void display_psw (REGS *regs)
{
DWORD   dword;                          /* Doubleword work area      */

    store_psw (&regs->psw, dword);
    printf ("PSW=%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
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
        printf ("R%2.2d=%8.8lX%s", i, regs->gpr[i],
            ((i & 0x03) == 0x03) ? "\n" : "\t");

} /* end function display_regs */

/*-------------------------------------------------------------------*/
/* Display control registers                                         */
/*-------------------------------------------------------------------*/
static void display_cregs (REGS *regs)
{
int     i;

    for (i = 0; i < 16; i++)
        printf ("CR%2.2d=%8.8lX%s", i, regs->cr[i],
            ((i & 0x03) == 0x03) ? "\n" : "\t");

} /* end function display_cregs */

/*-------------------------------------------------------------------*/
/* Display access registers                                          */
/*-------------------------------------------------------------------*/
static void display_aregs (REGS *regs)
{
int     i;

    for (i = 0; i < 16; i++)
        printf ("AR%2.2d=%8.8lX%s", i, regs->ar[i],
            ((i & 0x03) == 0x03) ? "\n" : "\t");

} /* end function display_aregs */

/*-------------------------------------------------------------------*/
/* Display floating point registers                                  */
/*-------------------------------------------------------------------*/
static void display_fregs (REGS *regs)
{

    printf ("FPR0=%8.8lX %8.8lX\t\tFPR2=%8.8lX %8.8lX\n"
            "FPR4=%8.8lX %8.8lX\t\tFPR6=%8.8lX %8.8lX\n",
            regs->fpr[0], regs->fpr[1], regs->fpr[2], regs->fpr[3],
            regs->fpr[4], regs->fpr[5], regs->fpr[6], regs->fpr[7]);

} /* end function display_fregs */

/*-------------------------------------------------------------------*/
/* Display real storage (up to 16 bytes, or until end of 4K page)    */
/* Returns number of bytes displayed                                 */
/*-------------------------------------------------------------------*/
static int display_real (REGS *regs, U32 raddr)
{
U32     aaddr;                          /* Absolute storage address  */
int     blkid;                          /* Main storage 4K block id  */
int     i;                              /* Loop counter              */

    printf ("R:%8.8lX:", raddr);
    aaddr = APPLY_PREFIXING (raddr, regs->pxr);
    if (aaddr >= sysblk.mainsize)
    {
        printf (" Real address is not valid\n");
        return 0;
    }

    blkid = aaddr >> 12;
    printf ("K:%2.2X=", sysblk.storkeys[blkid]);

    for (i = 0; i < 16; i++)
    {
        printf ("%2.2X", sysblk.mainstor[aaddr++]);
        if ((aaddr & 0x3) != 0x0) continue;
        if ((aaddr & 0xFFF) == 0x000) break;
        printf (" ");
    } /* end for(i) */

    printf ("\n");
    return i;

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

    /* Display the PSW */
    store_psw (&regs->psw, dword);
    printf ("PSW=%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X ",
                dword[0], dword[1], dword[2], dword[3],
                dword[4], dword[5], dword[6], dword[7]);

    /* Extract the opcode and determine the instruction length */
    opcode = inst[0];
    ilc = (opcode < 0x40) ? 2 : (opcode < 0xC0) ? 4 : 6;

    /* Display the instruction */
    printf ("INST=%2.2X%2.2X", inst[0], inst[1]);
    if (ilc > 2) printf("%2.2X%2.2X", inst[2], inst[3]);
    if (ilc > 4) printf("%2.2X%2.2X", inst[4], inst[5]);
    printf("\n");

    /* Process the first storage operand */
    if (ilc > 2)
    {
        /* Calculate the effective address of the first operand */
        b1 = inst[2] >> 4;
        addr1 = ((inst[2] & 0x0F) << 8) | inst[3];
        if (b1 != 0)
            addr1 += regs->gpr[b1] &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        /* Apply indexing for RX instructions */
        if ((opcode >= 0x40 && opcode <= 0x7F) || opcode == 0xB1)
        {
            x1 = inst[1] & 0x0F;
            if (x1 != 0)
                addr1 += regs->gpr[x1] &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);
        }

        /* Display storage at first storage operand location */
        printf ("V:%8.8lX:", addr1);
        xcode = virt_to_real (&raddr, addr1, b1, regs,
                                (opcode == 0x44 ? ACCTYPE_INSTFETCH :
                                 opcode == 0xB1 ? ACCTYPE_LRA :
                                                  ACCTYPE_READ));
        if (xcode == 0)
            display_real (regs, raddr);
        else
            printf (" Translation exception %4.4hX\n", xcode);
    }

    /* Process the second storage operand */
    if (ilc > 4)
    {
        /* Calculate the effective address of the second operand */
        b2 = inst[4] >> 4;
        addr2 = ((inst[4] & 0x0F) << 8) | inst[5];
        if (b2 != 0)
            addr2 += regs->gpr[b2] &
                        (regs->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF);

        /* Display storage at second storage operand location */
        printf ("V:%8.8lX:", addr2);
        xcode = virt_to_real (&raddr, addr2, b2, regs, ACCTYPE_READ);
        if (xcode == 0)
            display_real (regs, raddr);
        else
            printf (" Translation exception %4.4hX\n", xcode);
    }

    /* Display the general purpose registers */
    display_regs (regs);

} /* end function display_inst */

/*-------------------------------------------------------------------*/
/* Accept panel commands                                             */
/*-------------------------------------------------------------------*/
void panel_command (REGS *regs)
{
U32     vaddr;                          /* Virtual storage address   */
U32     raddr;                          /* Real storage address      */
U16     xcode;                          /* Exception code            */
U16     devnum;                         /* Device number             */
DEVBLK *dev;                            /* -> Device block           */
BYTE    c;                              /* Character work area       */
BYTE    buf[81];                        /* Command input buffer      */
int     i;                              /* Loop counter              */
int     oneorzero;                      /* 1=x+ command, 0=x-        */
BYTE   *onoroff;                        /* "on" or "off"             */

    while (1)
    {
        /* Display command prompt on console */
        printf ("Enter panel command or press enter to continue\n");
        fflush (stdout);

        /* Read command from console */
        fgets (buf, sizeof(buf), stdin);
        if (buf[strlen(buf)-1] == '\n') buf[strlen(buf)-1] = '\0';

        /* No command - continue execution */
        if (buf[0] == '\0')
            break;

        /* ? command - display help text */
        if (buf[0] == '?')
        {
            printf ("Panel command summary:\n"
                "t+=trace, s+=step, t+devn=CCW trace, s+devn=CCW step\n"
                "g=go, psw=display psw, pr=prefix reg\n"
                "gpr=general purpose regs, cr=control regs\n"
                "ar=access regs, fpr=floating point regs\n"
                "vxxxxxxxx=display virtual storage location xxxxxxxx\n"
                "rxxxxxxxx=display real storage location xxxxxxxx\n");
            continue;
        }

        /* x+ and x- commands - turn switches on or off */
        if (buf[1] == '+' || buf[1] == '-')
        {
            if (buf[1] == '+') {
                oneorzero = 1;
                onoroff = "on";
            } else {
                oneorzero = 0;
                onoroff = "off";
            }

            /* t+ and t- commands - instruction tracing on/off */
            if (buf[0]=='t' && buf[2]=='\0')
            {
                sysblk.insttrace = oneorzero;
                printf ("Instruction tracing is now %s\n", onoroff);
                continue;
            }

            /* s+ and s- commands - instruction stepping on/off */
            if (buf[0]=='s' && buf[2]=='\0')
            {
                sysblk.inststep = oneorzero;
                printf ("Instruction stepping is now %s\n", onoroff);
                continue;
            }

            /* t+devn and t-devn commands - turn CCW tracing on/off */
            /* s+devn and s-devn commands - turn CCW stepping on/off */
            if ((buf[0] == 't' || buf[0] == 's')
                && sscanf(buf+2, "%hx%c", &devnum, &c) == 1)
            {
                dev = find_device_by_devnum (devnum);
                if (dev == NULL)
                {
                    printf ("Device number %4.4X not found\n", devnum);
                    continue;
                }

                if (buf[0] == 't')
                {
                    dev->ccwtrace = oneorzero;
                    printf ("CCW tracing is now %s for device %4.4X\n",
                        onoroff, devnum);
                } else {
                    dev->ccwstep = oneorzero;
                    printf ("CCW stepping is now %s for device %4.4X\n",
                        onoroff, devnum);
                }
                continue;
            }

        } /* end if(+ or -) */

        /* g command - turn off single step and continue execution */
        if (strcmp(buf,"g") == 0)
        {
            sysblk.inststep = 0;
            break;
        }

        /* gpr command - display general purpose registers */
        if (strcmp(buf,"gpr") == 0)
        {
            display_regs (regs);
            continue;
        }

        /* fpr command - display floating point registers */
        if (strcmp(buf,"fpr") == 0)
        {
            display_fregs (regs);
            continue;
        }

        /* cr command - display control registers */
        if (strcmp(buf,"cr") == 0)
        {
            display_cregs (regs);
            continue;
        }

        /* ar command - display access registers */
        if (strcmp(buf,"ar") == 0)
        {
            display_aregs (regs);
            continue;
        }

        /* pr command - display prefix register */
        if (strcmp(buf,"pr") == 0)
        {
            printf ("Prefix=%8.8lX\n", regs->pxr);
            continue;
        }

        /* psw command - display program status word */
        if (strcmp(buf,"psw") == 0)
        {
            display_psw (regs);
            continue;
        }

        /* r command - display real storage */
        if (buf[0] == 'r' && sscanf(buf+1, "%lx%c", &raddr, &c) == 1)
        {
            raddr &= 0x7FFFFFF0;
            for (i = 0; i < 4; i++)
            {
                display_real (regs, raddr);
                raddr += 16;
            } /* end for(i) */
            continue;
        }

        /* v command - display virtual storage */
        if (buf[0] == 'v' && sscanf(buf+1, "%lx%c", &vaddr, &c) == 1)
        {
            vaddr &= 0x7FFFFFF0;
            for (i = 0; i < 4; i++)
            {
                printf ("V:%8.8lX:", vaddr);
                xcode = virt_to_real (&raddr, vaddr, 0, regs,
                        ACCTYPE_READ);
                if (xcode == 0)
                    display_real (regs, raddr);
                else
                    printf (" Translation exception %4.4hX\n",
                            xcode);
                vaddr += 16;
            } /* end for(i) */
            continue;
        }

        /* Invalid command - display error message */
        printf ("%s command invalid. Enter ? for help\n", buf);

    } /* end while */

} /* end function panel_command */
