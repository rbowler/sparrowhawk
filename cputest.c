/* CPUTEST.C    (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 CPU Test Driver                              */

/*-------------------------------------------------------------------*/
/* This module is a test driver for executing CPU instructions       */
/* specified as command arguments.                                   */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Convert hexadecimal string to binary                              */
/*-------------------------------------------------------------------*/
static int hexpack (BYTE *dest, BYTE *source)
{
unsigned int x,h;

for (x=0; *source; x++)                 /* process each source char  */
{
        h = *source++;                  /* pull char from source     */

        if (h >= '0' && h <= '9')       /* convert char to binary    */
                h = h - '0';
        else if (h >= 'A' && h <= 'F')
                h = h - 'A' + 10;
        else if (h >= 'a' && h <= 'f')
                h = h - 'a' + 10;
        else return -1;                 /* exit if invalid hex char  */

        if ((x & 1) == 0)               /* if this is high digit     */
                *dest = h << 4;         /* result to left half byte  */
        else                            /* if it's the low digit     */
                *dest++ |= h;           /* result to right half byte */
} /* end for(x) */

return x/2;                             /* Return destination length */
} /* end function hexpack */

/*-------------------------------------------------------------------*/
/* CPUTEST main entry point                                          */
/*-------------------------------------------------------------------*/
int main (int argc, char *argv[])
{
#define START_ADDR 0x1000
#define PSW_LOCATION 0
int     n, i, rc;
PSW     psw;
REGS   *regs;

    /* Display the version identifier */
    printf ("Hercules %s (c)Copyright Roger Bowler, 1994-1999\n",
            MSTRING(VERSION));

    /* Check that instruction arguments are present */
    if (argc < 2) {
        printf ("Usage: %s instruction [instruction...]\n", argv[0]);
        exit(1);
    }

    /* Build system configuration */
    build_config ("hercules.cnf");
    sysblk.inststep = 1;

    /* Build program in storage from instruction arguments */
    for (n=START_ADDR, i=1; i < argc; i++) {
        rc = hexpack (sysblk.mainstor + n, argv[i]);
        if (rc < 0) {
            printf ("Invalid hex string: %s\n", argv[i]);
            exit(1);
        }
        n += rc;
    }

    /* Build the initial PSW */
    memset (&psw, 0, sizeof(psw));
    psw.sysmask = PSW_IOMASK | PSW_EXTMASK;
    psw.pkey = 0x00;
    psw.ecmode = 1;
    psw.mach = 1;
    psw.prob = 0;
    psw.ia = START_ADDR;

    /* Point to the register context for CPU 0 */
    regs = &(sysblk.regs[0]);

    /* Initialize register 15 to program starting address */
    regs->gpr[15] = START_ADDR;

    /* Store the initial PSW in main storage and start the CPU */
    store_psw (&psw, sysblk.mainstor + PSW_LOCATION);

    start_cpu (PSW_LOCATION, regs);

    return 0;
} /* end function main */
