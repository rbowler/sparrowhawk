/* CONFIG.C     (c) Copyright Roger Bowler, 1999-2000                */
/*              ESA/390 Configuration Builder                        */

/*-------------------------------------------------------------------*/
/* This module builds the configuration tables for the Hercules      */
/* ESA/390 emulator.  It reads information about the processors      */
/* and I/O devices from a configuration file.  It allocates          */
/* main storage and expanded storage, initializes control blocks,    */
/* and creates detached threads to handle console attention          */
/* requests and to maintain the TOD clock and CPU timers.            */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      TOD clock offset contributed by Jay Maynard                  */
/*      Dynamic device attach/detach by Jan Jaeger                   */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Internal macro definitions                                        */
/*-------------------------------------------------------------------*/
#define SPACE           ((BYTE)' ')

/*-------------------------------------------------------------------*/
/* Global data areas                                                 */
/*-------------------------------------------------------------------*/
SYSBLK  sysblk;

/*-------------------------------------------------------------------*/
/* Static data areas                                                 */
/*-------------------------------------------------------------------*/
static int  stmt = 0;                   /* Config statement number   */
static BYTE buf[256];                   /* Config statement buffer   */
static BYTE *keyword;                   /* -> Statement keyword      */
static BYTE *operand;                   /* -> First argument         */
#define MAX_ARGS 10                     /* Max #of additional args   */
static int  addargc;                    /* Number of additional args */
static BYTE *addargv[MAX_ARGS];         /* Additional argument array */

/*-------------------------------------------------------------------*/
/* ASCII/EBCDIC TRANSLATE TABLES                                     */
/*-------------------------------------------------------------------*/
unsigned char
ebcdic_to_ascii[] = {
"\x00\x01\x02\x03\xA6\x09\xA7\x7F\xA9\xB0\xB1\x0B\x0C\x0D\x0E\x0F"
"\x10\x11\x12\x13\xB2\xB4\x08\xB7\x18\x19\x1A\xB8\xBA\x1D\xBB\x1F"
"\xBD\xC0\x1C\xC1\xC2\x0A\x17\x1B\xC3\xC4\xC5\xC6\xC7\x05\x06\x07"
"\xC8\xC9\x16\xCB\xCC\x1E\xCD\x04\xCE\xD0\xD1\xD2\x14\x15\xD3\xFC"
"\x20\xD4\x83\x84\x85\xA0\xD5\x86\x87\xA4\xD6\x2E\x3C\x28\x2B\xD7"
"\x26\x82\x88\x89\x8A\xA1\x8C\x8B\x8D\xD8\x21\x24\x2A\x29\x3B\x5E"
"\x2D\x2F\xD9\x8E\xDB\xDC\xDD\x8F\x80\xA5\x7C\x2C\x25\x5F\x3E\x3F"
"\xDE\x90\xDF\xE0\xE2\xE3\xE4\xE5\xE6\x60\x3A\x23\x40\x27\x3D\x22"
"\xE7\x61\x62\x63\x64\x65\x66\x67\x68\x69\xAE\xAF\xE8\xE9\xEA\xEC"
"\xF0\x6A\x6B\x6C\x6D\x6E\x6F\x70\x71\x72\xF1\xF2\x91\xF3\x92\xF4"
"\xF5\x7E\x73\x74\x75\x76\x77\x78\x79\x7A\xAD\xA8\xF6\x5B\xF7\xF8"
"\x9B\x9C\x9D\x9E\x9F\xB5\xB6\xAC\xAB\xB9\xAA\xB3\xBC\x5D\xBE\xBF"
"\x7B\x41\x42\x43\x44\x45\x46\x47\x48\x49\xCA\x93\x94\x95\xA2\xCF"
"\x7D\x4A\x4B\x4C\x4D\x4E\x4F\x50\x51\x52\xDA\x96\x81\x97\xA3\x98"
"\x5C\xE1\x53\x54\x55\x56\x57\x58\x59\x5A\xFD\xEB\x99\xED\xEE\xEF"
"\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\xFE\xFB\x9A\xF9\xFA\xFF"
        };

unsigned char
ascii_to_ebcdic[] = {
"\x00\x01\x02\x03\x37\x2D\x2E\x2F\x16\x05\x25\x0B\x0C\x0D\x0E\x0F"
"\x10\x11\x12\x13\x3C\x3D\x32\x26\x18\x19\x1A\x27\x22\x1D\x35\x1F"
"\x40\x5A\x7F\x7B\x5B\x6C\x50\x7D\x4D\x5D\x5C\x4E\x6B\x60\x4B\x61"
"\xF0\xF1\xF2\xF3\xF4\xF5\xF6\xF7\xF8\xF9\x7A\x5E\x4C\x7E\x6E\x6F"
"\x7C\xC1\xC2\xC3\xC4\xC5\xC6\xC7\xC8\xC9\xD1\xD2\xD3\xD4\xD5\xD6"
"\xD7\xD8\xD9\xE2\xE3\xE4\xE5\xE6\xE7\xE8\xE9\xAD\xE0\xBD\x5F\x6D"
"\x79\x81\x82\x83\x84\x85\x86\x87\x88\x89\x91\x92\x93\x94\x95\x96"
"\x97\x98\x99\xA2\xA3\xA4\xA5\xA6\xA7\xA8\xA9\xC0\x6A\xD0\xA1\x07"
"\x68\xDC\x51\x42\x43\x44\x47\x48\x52\x53\x54\x57\x56\x58\x63\x67"
"\x71\x9C\x9E\xCB\xCC\xCD\xDB\xDD\xDF\xEC\xFC\xB0\xB1\xB2\xB3\xB4"
"\x45\x55\xCE\xDE\x49\x69\x04\x06\xAB\x08\xBA\xB8\xB7\xAA\x8A\x8B"
"\x09\x0A\x14\xBB\x15\xB5\xB6\x17\x1B\xB9\x1C\x1E\xBC\x20\xBE\xBF"
"\x21\x23\x24\x28\x29\x2A\x2B\x2C\x30\x31\xCA\x33\x34\x36\x38\xCF"
"\x39\x3A\x3B\x3E\x41\x46\x4A\x4F\x59\x62\xDA\x64\x65\x66\x70\x72"
"\x73\xE1\x74\x75\x76\x77\x78\x80\x8C\x8D\x8E\xEB\x8F\xED\xEE\xEF"
"\x90\x9A\x9B\x9D\x9F\xA0\xAC\xAE\xAF\xFD\xFE\xFB\x3F\xEA\xFA\xFF"
        };

/*-------------------------------------------------------------------*/
/* Subroutine to read a statement from the configuration file        */
/* The statement is then parsed into keyword, operand, and           */
/* additional arguments.  The output values are:                     */
/* keyword      Points to first word of statement                    */
/* operand      Points to second word of statement                   */
/* addargc      Contains number of additional arguments              */
/* addargv      An array of pointers to each additional argument     */
/* Returns 0 if successful, -1 if end of file                        */
/*-------------------------------------------------------------------*/
static int read_config (BYTE *fname, FILE *fp)
{
int     i;                              /* Array subscript           */
int     stmtlen;                        /* Statement length          */

    while (1)
    {
        /* Increment statement number */
        stmt++;

        /* Read next statement from configuration file */
        if ( fgets (buf, sizeof(buf), fp) == NULL )
        {
            if (feof(fp)) return -1;

            fprintf (stderr,
                    "HHC001I Error reading file %s line %d: %s\n",
                    fname, stmt, strerror(errno));
            exit(1);
        }

        /* Check for DOS end of file character */
        if (buf[0] == '\x1A') return -1;

        /* Check that statement ends with a newline */
        stmtlen = strlen(buf);
        if (stmtlen == 0 || buf[stmtlen-1] != '\n')
        {
            fprintf (stderr,
                    "HHC002I File %s line %d is too long\n",
                    fname, stmt);
            exit(1);
        }

        /* Remove trailing carriage return and line feed */
        stmtlen--;
        if (stmtlen > 0 && buf[stmtlen-1] == '\r') stmtlen--;

        /* Remove trailing blanks and tabs */
        while (stmtlen > 0 && (buf[stmtlen-1] == SPACE
                || buf[stmtlen-1] == '\t')) stmtlen--;
        buf[stmtlen] = '\0';

        /* Ignore comments and null statements */
        if (stmtlen == 0 || buf[0] == '*' || buf[0] == '#')
           continue;

        /* Split the statement into keyword and first operand */
        keyword = strtok (buf, " \t");
        operand = strtok (NULL, " \t");

        /* Extract any additional operands */
        for (addargc = 0; addargc < MAX_ARGS &&
            (addargv[addargc] = strtok (NULL, " \t")) != NULL
                && addargv[addargc][0] != '#';
            addargc++);

        /* Clear any unused additional operand pointers */
        for (i = addargc; i < MAX_ARGS; i++) addargv[i] = NULL;

        break;
    } /* end while */

    return 0;
} /* end function read_config */

/*-------------------------------------------------------------------*/
/* Function to build system configuration                            */
/*-------------------------------------------------------------------*/
void build_config (BYTE *fname)
{
int     rc;                             /* Return code               */
int     i;                              /* Array subscript           */
int     scount;                         /* Statement counter         */
int     cpu;                            /* CPU number                */
int     pfd[2];                         /* Message pipe handles      */
FILE   *fp;                             /* Configuration file pointer*/
BYTE   *sserial;                        /* -> CPU serial string      */
BYTE   *smodel;                         /* -> CPU model string       */
BYTE   *smainsize;                      /* -> Main size string       */
BYTE   *sxpndsize;                      /* -> Expanded size string   */
BYTE   *scnslport;                      /* -> Console port number    */
BYTE   *snumcpu;                        /* -> Number of CPUs         */
BYTE   *sloadparm;                      /* -> IPL load parameter     */
BYTE   *ssysepoch;                      /* -> System epoch           */
BYTE   *stzoffset;                      /* -> System timezone offset */
BYTE   *stoddrag;                       /* -> TOD clock drag factor  */
BYTE    loadparm[8];                    /* Load parameter (EBCDIC)   */
BYTE    version = 0x00;                 /* CPU version code          */
U32     serial;                         /* CPU serial number         */
U16     model;                          /* CPU model number          */
U16     mainsize;                       /* Main storage size (MB)    */
U16     xpndsize;                       /* Expanded storage size (MB)*/
U16     cnslport;                       /* Console port number       */
U16     numcpu;                         /* Number of CPUs            */
S32     sysepoch;                       /* System epoch year         */
S32     tzoffset;                       /* System timezone offset    */
int     toddrag;                        /* TOD clock drag factor     */
BYTE   *sdevnum;                        /* -> Device number string   */
BYTE   *sdevtype;                       /* -> Device type string     */
U16     devnum;                         /* Device number             */
U16     devtype;                        /* Device type               */
BYTE    c;                              /* Work area for sscanf      */

    /* Open the configuration file */
    fp = fopen (fname, "r");
    if (fp == NULL)
    {
        fprintf (stderr,
                "HHC003I Cannot open file %s: %s\n",
                fname, strerror(errno));
        exit(1);
    }

    /* Set the default system parameter values */
    serial = 0x000001;
    model = 0x0586;
    mainsize = 2;
    xpndsize = 0;
    cnslport = 3270;
    numcpu = 1;
    memset (loadparm, 0x4B, 8);
    sysepoch = 1900;
    tzoffset = 0;
    toddrag = 1;

    /* Read records from the configuration file */
    for (scount = 0; ; scount++)
    {
        /* Read next record from the configuration file */
        if ( read_config (fname, fp) )
        {
            fprintf (stderr,
                    "HHC004I No device records in file %s\n",
                    fname);
            exit(1);
        }

        /* Exit loop if first device statement found */
        if (strlen(keyword) <= 4
            && sscanf(keyword, "%x%c", &rc, &c) == 1)
            break;

        /* Clear the operand value pointers */
        sserial = NULL;
        smodel = NULL;
        smainsize = NULL;
        sxpndsize = NULL;
        scnslport = NULL;
        snumcpu = NULL;
        sloadparm = NULL;
        ssysepoch = NULL;
        stzoffset = NULL;
        stoddrag = NULL;

        /* Check for old-style CPU statement */
        if (scount == 0 && addargc == 5 && strlen(keyword) == 6
            && sscanf(keyword, "%x%c", &rc, &c) == 1)
        {
            sserial = keyword;
            smodel = operand;
            smainsize = addargv[0];
            sxpndsize = addargv[1];
            scnslport = addargv[2];
            snumcpu = addargv[3];
            sloadparm = addargv[4];
        }
        else
        {
            if (strcasecmp (keyword, "cpuserial") == 0)
            {
                sserial = operand;
            }
            else if (strcasecmp (keyword, "cpumodel") == 0)
            {
                smodel = operand;
            }
            else if (strcasecmp (keyword, "mainsize") == 0)
            {
                smainsize = operand;
            }
            else if (strcasecmp (keyword, "xpndsize") == 0)
            {
                sxpndsize = operand;
            }
            else if (strcasecmp (keyword, "cnslport") == 0)
            {
                scnslport = operand;
            }
            else if (strcasecmp (keyword, "numcpu") == 0)
            {
                snumcpu = operand;
            }
            else if (strcasecmp (keyword, "loadparm") == 0)
            {
                sloadparm = operand;
            }
            else if (strcasecmp (keyword, "sysepoch") == 0)
            {
                ssysepoch = operand;
            }
            else if (strcasecmp (keyword, "tzoffset") == 0)
            {
                stzoffset = operand;
            }
#ifdef TODCLOCK_DRAG_FACTOR
            else if (strcasecmp (keyword, "toddrag") == 0)
            {
                stoddrag = operand;
            }
#endif /*TODCLOCK_DRAG_FACTOR*/
            else
            {
                fprintf (stderr,
                        "HHC006I Error in %s line %d: "
                        "Unrecognized keyword %s\n",
                        fname, stmt, keyword);
                exit(1);
            }

            /* Check for one and only one operand */
            if (operand == NULL || addargc != 0)
            {
                fprintf (stderr,
                        "HHC005I Error in %s line %d: "
                        "Incorrect number of operands\n",
                        fname, stmt);
                exit(1);
            }
        }

        /* Parse CPU serial number operand */
        if (sserial != NULL)
        {
            if (strlen(sserial) != 6
                || sscanf(sserial, "%x%c", &serial, &c) != 1)
            {
                fprintf (stderr,
                        "HHC007I Error in %s line %d: "
                        "%s is not a valid serial number\n",
                        fname, stmt, sserial);
                exit(1);
            }
        }

        /* Parse CPU model number operand */
        if (smodel != NULL)
        {
            if (strlen(smodel) != 4
                || sscanf(smodel, "%hx%c", &model, &c) != 1)
            {
                fprintf (stderr,
                        "HHC008I Error in %s line %d: "
                        "%s is not a valid CPU model\n",
                        fname, stmt, smodel);
                exit(1);
            }
        }

        /* Parse main storage size operand */
        if (smainsize != NULL)
        {
            if (sscanf(smainsize, "%hu%c", &mainsize, &c) != 1
                || mainsize < 2 || mainsize > 256)
            {
                fprintf (stderr,
                        "HHC009I Error in %s line %d: "
                        "Invalid main storage size %s\n",
                        fname, stmt, smainsize);
                exit(1);
            }
        }

        /* Parse expanded storage size operand */
        if (sxpndsize != NULL)
        {
            if (sscanf(sxpndsize, "%hu%c", &xpndsize, &c) != 1
                || xpndsize > 1024)
            {
                fprintf (stderr,
                        "HHC010I Error in %s line %d: "
                        "Invalid expanded storage size %s\n",
                        fname, stmt, sxpndsize);
                exit(1);
            }
        }

        /* Parse console port number operand */
        if (scnslport != NULL)
        {
            if (sscanf(scnslport, "%hu%c", &cnslport, &c) != 1
                || cnslport == 0)
            {
                fprintf (stderr,
                        "HHC011I Error in %s line %d: "
                        "Invalid console port number %s\n",
                        fname, stmt, scnslport);
                exit(1);
            }
        }

        /* Parse number of CPUs operand */
        if (snumcpu != NULL)
        {
            if (sscanf(snumcpu, "%hu%c", &numcpu, &c) != 1
                || numcpu < 1 || numcpu > MAX_CPU_ENGINES)
            {
                fprintf (stderr,
                        "HHC012I Error in %s line %d: "
                        "Invalid number of CPUs %s\n",
                        fname, stmt, snumcpu);
                exit(1);
            }
        }

        /* Parse load parameter operand */
        if (sloadparm != NULL)
        {
            if (strlen(sloadparm) > 8)
            {
                fprintf (stderr,
                        "HHC013I Error in %s line %d: "
                        "Load parameter %s exceeds 8 characters\n",
                        fname, stmt, sloadparm);
                exit(1);
            }

            /* Convert the load parameter to EBCDIC */
            memset (loadparm, 0x4B, 8);
            for (i = 0; i < strlen(sloadparm); i++)
                loadparm[i] = ascii_to_ebcdic[sloadparm[i]];
        }

        /* Parse system epoch operand */
        if (ssysepoch != NULL)
        {
            if (strlen(ssysepoch) != 4
                || sscanf(ssysepoch, "%d%c", &sysepoch, &c) != 1
                || (sysepoch < 1900) || (sysepoch > 2000))
            {
                fprintf (stderr,
                        "HHC014I Error in %s line %d: "
                        "%s is not a valid system epoch\n",
                        fname, stmt, ssysepoch);
                exit(1);
            }
        }

        /* Parse timezone offset operand */
        if (stzoffset != NULL)
        {
            if (strlen(stzoffset) != 5
                || sscanf(stzoffset, "%d%c", &tzoffset, &c) != 1
                || (tzoffset < -2359) || (tzoffset > 2359))
            {
                fprintf (stderr,
                        "HHC015I Error in %s line %d: "
                        "%s is not a valid timezone offset\n",
                        fname, stmt, stzoffset);
                exit(1);
            }
        }

#ifdef TODCLOCK_DRAG_FACTOR
        /* Parse TOD clock drag factor operand */
        if (stoddrag != NULL)
        {
            if (sscanf(stoddrag, "%u%c", &toddrag, &c) != 1
                || toddrag < 1 || toddrag > 10000)
            {
                fprintf (stderr,
                        "HHC016I Error in %s line %d: "
                        "Invalid TOD clock drag factor %s\n",
                        fname, stmt, stoddrag);
                exit(1);
            }
        }
#endif /*TODCLOCK_DRAG_FACTOR*/

    } /* end for(scount) */

    /* Clear the system configuration block */
    memset (&sysblk, 0, sizeof(SYSBLK));

    /* Direct logmsg output to stderr during initialization */
    sysblk.msgpipew = stderr;

    /* Initialize the CPU registers */
    sysblk.numcpu = numcpu;
    for (cpu = 0; cpu < MAX_CPU_ENGINES; cpu++)
    {
        /* Initialize the processor address register for STAP */
        sysblk.regs[cpu].cpuad = cpu;

        /* Perform initial CPU reset */
        initial_cpu_reset (sysblk.regs + cpu);

    } /* end for(cpu) */

    /* Obtain main storage */
    sysblk.mainsize = mainsize * 1024 * 1024;
    sysblk.mainstor = malloc(sysblk.mainsize);
    if (sysblk.mainstor == NULL)
    {
        fprintf (stderr,
                "HHC020I Cannot obtain %dMB main storage: %s\n",
                mainsize, strerror(errno));
        exit(1);
    }

    /* Obtain main storage key array */
    sysblk.storkeys = malloc(sysblk.mainsize / STORAGE_KEY_PAGESIZE);
    if (sysblk.storkeys == NULL)
    {
        fprintf (stderr,
                "HHC021I Cannot obtain storage key array: %s\n",
                strerror(errno));
        exit(1);
    }

#if 0   /*DEBUG-JJ-20/03/2000*/
    /* Mark selected frames invalid for debugging purposes */
    for (i = 64 ; i < (sysblk.mainsize / STORAGE_KEY_PAGESIZE); i += 2)
        if (i < (sysblk.mainsize / STORAGE_KEY_PAGESIZE) - 64)
            sysblk.storkeys[i] = STORKEY_BADFRM;
        else
            sysblk.storkeys[i++] = STORKEY_BADFRM;
#endif

    if (xpndsize != 0)
    {
#ifdef FEATURE_EXPANDED_STORAGE
        /* Obtain expanded storage */
        sysblk.xpndsize = xpndsize * (1024*1024 / XSTORE_PAGESIZE);
        sysblk.xpndstor = malloc(sysblk.xpndsize * XSTORE_PAGESIZE);
        if (sysblk.xpndstor == NULL)
        {
            fprintf (stderr,
                    "HHC022I Cannot obtain %dMB expanded storage: "
                    "%s\n",
                    xpndsize, strerror(errno));
            exit(1);
        }
#else /*!FEATURE_EXPANDED_STORAGE*/
        fprintf (stderr,
                "HHC024I Expanded storage support not installed\n");
        exit(1);
#endif /*!FEATURE_EXPANDED_STORAGE*/
    } /* end if(sysblk.xpndsize) */

    /* Save the console port number */
    sysblk.cnslport = cnslport;

    /* Build CPU identifier */
    sysblk.cpuid = ((U64)version << 56)
                 | ((U64)serial << 32)
                 | ((U64)model << 16);

    /* Set the load parameter */
    memcpy (sysblk.loadparm, loadparm, 8);

    /* Initialize locks, conditions, and attributes */
    initialize_lock (&sysblk.todlock);
    initialize_lock (&sysblk.mainlock);
    initialize_lock (&sysblk.intlock);
    initialize_lock (&sysblk.sigplock);
    initialize_condition (&sysblk.intcond);
    initialize_detach_attr (&sysblk.detattr);

    /* Set up the system TOD clock offset: compute the number of
       seconds from the designated year to 1970 for TOD clock
       adjustment, then add in the specified time zone offset */
    sysepoch = 1970-sysepoch;
    sysblk.todoffset = (sysepoch*365 + sysepoch/4) * 86400ULL;

    /* Compute the timezone offset in seconds and crank that in */
    tzoffset = (tzoffset/100)*3600 + (tzoffset%100)*60;
    sysblk.todoffset += tzoffset;

    /* Convert the TOD clock offset to microseconds */
    sysblk.todoffset *= 1000000;

    /* Set the TOD clock drag factor */
    sysblk.toddrag = toddrag;

    /* Parse the device configuration statements */
    while(1)
    {
        /* First two fields are device number and device type */
        sdevnum = keyword;
        sdevtype = operand;

        if (sdevnum == NULL || sdevtype == NULL)
        {
            fprintf (stderr,
                    "HHC030I Error in %s line %d: "
                    "Missing device number or device type\n",
                    fname, stmt);
            exit(1);
        }

        if (strlen(sdevnum) > 4
            || sscanf(sdevnum, "%hx%c", &devnum, &c) != 1)
        {
            fprintf (stderr,
                    "HHC031I Error in %s line %d: "
                    "%s is not a valid device number\n",
                    fname, stmt, sdevnum);
            exit(1);
        }

        if (sscanf(sdevtype, "%hx%c", &devtype, &c) != 1)
        {
            fprintf (stderr,
                    "HHC032I Error in %s line %d: "
                    "%s is not a valid device type\n",
                    fname, stmt, sdevtype);
            exit(1);
        }

        /* Build the device configuration block */
        if (attach_device (devnum, devtype, addargc, addargv))
            exit(1);

        /* Read next device record from the configuration file */
        if (read_config (fname, fp))
            break;

    } /* end while(1) */

    /* Create the message pipe */
    rc = pipe (pfd);
    if (rc < 0)
    {
        fprintf (stderr,
                "HHC033I Message pipe creation failed: %s\n",
                strerror(errno));
        exit(1);
    }

    sysblk.msgpiper = pfd[0];
    sysblk.msgpipew = fdopen (pfd[1], "w");
    if (sysblk.msgpipew == NULL)
    {
        fprintf (stderr,
                "HHC034I Message pipe open failed: %s\n",
                strerror(errno));
        exit(1);
    }
    setvbuf (sysblk.msgpipew, NULL, _IOLBF, 0);

    /* Display the version identifier on the control panel */
    logmsg ("Hercules %s version %s "
            "(c)Copyright Roger Bowler, 1994-2000\n",
            ARCHITECTURE_NAME, MSTRING(VERSION));

} /* end function build_config */

/*-------------------------------------------------------------------*/
/* Function to build a device configuration block                    */
/*-------------------------------------------------------------------*/
int attach_device (U16 devnum, U16 devtype,
                   int addargc, BYTE *addargv[])
{
DEVBLK *dev;                            /* -> Device block           */
DEVBLK**dvpp;                           /* -> Device block address   */
DEVIF  *devinit;                        /* -> Device init function   */
DEVQF  *devqdef;                        /* -> Device query function  */
DEVXF  *devexec;                        /* -> Device exec function   */
int     rc;                             /* Return code               */
int     newdevblk = 0;                  /* 1=Newly created devblk    */

    /* Check whether device number has already been defined */
    if (find_device_by_devnum(devnum) != NULL)
    {
        logmsg ("HHC035I device %4.4X already exists\n", devnum);
        return 1;
    }

    /* Determine which device handler to use for this device */
    switch (devtype) {

    case 0x1052:
    case 0x3215:
        devinit = &constty_init_handler;
        devqdef = &constty_query_device;
        devexec = &constty_execute_ccw;
        break;

    case 0x1442:
    case 0x2501:
    case 0x3505:
        devinit = &cardrdr_init_handler;
        devqdef = &cardrdr_query_device;
        devexec = &cardrdr_execute_ccw;
        break;

    case 0x3525:
        devinit = &cardpch_init_handler;
        devqdef = &cardpch_query_device;
        devexec = &cardpch_execute_ccw;
        break;

    case 0x1403:
    case 0x3211:
        devinit = &printer_init_handler;
        devqdef = &printer_query_device;
        devexec = &printer_execute_ccw;
        break;

    case 0x3420:
    case 0x3480:
        devinit = &tapedev_init_handler;
        devqdef = &tapedev_query_device;
        devexec = &tapedev_execute_ccw;
        break;

    case 0x2311:
    case 0x2314:
    case 0x3330:
    case 0x3350:
    case 0x3380:
    case 0x3390:
        devinit = &ckddasd_init_handler;
        devqdef = &ckddasd_query_device;
        devexec = &ckddasd_execute_ccw;
        break;

    case 0x3310:
    case 0x3370:
    case 0x9336:
        devinit = &fbadasd_init_handler;
        devqdef = &fbadasd_query_device;
        devexec = &fbadasd_execute_ccw;
        break;

    case 0x3270:
        devinit = &loc3270_init_handler;
        devqdef = &loc3270_query_device;
        devexec = &loc3270_execute_ccw;
        break;

    case 0x3088:
        devinit = &ctcadpt_init_handler;
        devqdef = &ctcadpt_query_device;
        devexec = &ctcadpt_execute_ccw;
        break;

    default:
        logmsg ("HHC036I Device type %4.4X not recognized\n",
                devtype);
        return 1;
    } /* end switch(devtype) */

    /* Attempt to reuse an existing device block */
    dev = find_unused_device();

    /* If no device block is available, create a new one */
    if (dev == NULL)
    {
        /* Obtain a device block */
        dev = (DEVBLK*)malloc(sizeof(DEVBLK));
        if (dev == NULL)
        {
            logmsg ("HHC037I Cannot obtain device block "
                    "for device %4.4X: %s\n",
                    devnum, strerror(errno));
            return 1;
        }
        memset (dev, 0, sizeof(DEVBLK));

        /* Indicate a newly allocated devblk */
        newdevblk = 1;

        /* Initialize the device lock and condition */
        initialize_lock (&dev->lock);
        initialize_condition (&dev->resumecond);

        /* Assign new subchannel number */
        dev->subchan = sysblk.highsubchan++;
    }

    /* Obtain the device lock */
    obtain_lock(&dev->lock);

    /* Initialize the device block */
    dev->devnum = devnum;
    dev->devtype = devtype;
    dev->devinit = devinit;
    dev->devqdef = devqdef;
    dev->devexec = devexec;
    dev->fd = -1;

    /* Initialize the path management control word */
    dev->pmcw.devnum[0] = dev->devnum >> 8;
    dev->pmcw.devnum[1] = dev->devnum & 0xFF;
    dev->pmcw.lpm = 0x80;
    dev->pmcw.pim = 0x80;
    dev->pmcw.pom = 0xFF;
    dev->pmcw.pam = 0x80;
    dev->pmcw.chpid[0] = dev->devnum >> 8;

    /* Call the device handler initialization function */
    rc = (*devinit)(dev, addargc, addargv);
    if (rc < 0)
    {
        logmsg ("HHC038I Initialization failed for device %4.4X\n",
                devnum);
        release_lock(&dev->lock);

        /* Release the device block if we just acquired it */
        if (newdevblk)
            free(dev);

        return 1;
    }

    /* Obtain device data buffer */
    if (dev->bufsize != 0)
    {
        dev->buf = malloc (dev->bufsize);
        if (dev->buf == NULL)
        {
            logmsg ("HHC039I Cannot obtain buffer "
                    "for device %4.4X: %s\n",
                    dev->devnum, strerror(errno));
            release_lock(&dev->lock);

            /* Release the device block if we just acquired it */
            if(newdevblk)
                free(dev);

            return 1;
        }
    }

    /* If we acquired a new device block, add it to the chain */
    if (newdevblk)
    {
        /* Search for the last device block on the chain */
        for (dvpp = &(sysblk.firstdev); *dvpp != NULL;
                dvpp = &((*dvpp)->nextdev));

        /* Add the new device block to the end of the chain */
        *dvpp = dev;
        dev->nextdev = NULL;
    }

    /* Mark device valid */
    dev->pmcw.flag5 |= PMCW5_V;

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Indicate a CRW is pending for this device */
    dev->crwpending = 1;
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Release device lock */
    release_lock(&dev->lock);

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Signal machine check */
    machine_check_crwpend();
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    return 0;
} /* end function attach_device */

/*-------------------------------------------------------------------*/
/* Function to delete a device configuration block                   */
/*-------------------------------------------------------------------*/
int detach_device (U16 devnum)
{
DEVBLK *dev;                            /* -> Device block           */
int     fileseq;                        /* File seq num for ckddasd  */

    /* Find the device block */
    dev = find_device_by_devnum (devnum);

    if (dev == NULL)
    {
        logmsg ("HHC040I device %4.4X does not exist\n", devnum);
        return 1;
    }

    /* Obtain the device lock */
    obtain_lock(&dev->lock);

    /* Mark device invalid */
    dev->pmcw.flag5 &= ~(PMCW5_E | PMCW5_V);

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Indicate a CRW is pending for this device */
    dev->crwpending = 1;
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Close file or socket */
    if (dev->fd > 2)
    {
        close (dev->fd);
        dev->fd = -1;

        /* Signal console thread to redrive select */
        if (dev->console)
        {
            dev->console = 0;
            signal_thread (sysblk.cnsltid, SIGHUP);
        }
    }

    /* For CKD devices, close additional files */
    for (fileseq = 1; fileseq <= dev->ckdnumfd; fileseq++)
    {
        if (dev->ckdfd[fileseq-1] > 2)
            close (dev->ckdfd[fileseq-1]);
    }
    dev->ckdnumfd = 0;

    /* Release device lock */
    release_lock(&dev->lock);

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Signal machine check */
    machine_check_crwpend();
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    logmsg ("HHC041I device %4.4X detached\n", devnum);

    return 0;
} /* end function detach_device */

/*-------------------------------------------------------------------*/
/* Function to rename a device configuration block                   */
/*-------------------------------------------------------------------*/
int define_device (U16 olddevn, U16 newdevn)
{
DEVBLK *dev;                            /* -> Device block           */

    /* Find the device block */
    dev = find_device_by_devnum (olddevn);

    if (dev == NULL)
    {
        logmsg ("HHC042I device %4.4X does not exist\n", olddevn);
        return 1;
    }

    /* Check that new device number does not already exist */
    if (find_device_by_devnum(newdevn) != NULL)
    {
        logmsg ("HHC043I device %4.4X already exists\n", newdevn);
        return 1;
    }

    /* Obtain the device lock */
    obtain_lock(&dev->lock);

    /* Update the device number in the DEVBLK */
    dev->devnum = newdevn;

    /* Update the device number in the PMCW */
    dev->pmcw.devnum[0] = newdevn >> 8;
    dev->pmcw.devnum[1] = newdevn & 0xFF;

    /* Disable the device */
    dev->pmcw.flag5 &= ~PMCW5_E;

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Indicate a CRW is pending for this device */
    dev->crwpending = 1;
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Release device lock */
    release_lock(&dev->lock);

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Signal machine check */
    machine_check_crwpend();
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    logmsg ("HHC044I device %4.4X defined as %4.4X\n",
            olddevn, newdevn);

    return 0;
} /* end function define_device */

/*-------------------------------------------------------------------*/
/* Function to find an unused device block entry                     */
/*-------------------------------------------------------------------*/
DEVBLK *find_unused_device ()
{
DEVBLK *dev;

    for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
        if (!(dev->pmcw.flag5 & PMCW5_V)) break;

    return dev;

} /* end function find_unused_device */

/*-------------------------------------------------------------------*/
/* Function to find a device block given the device number           */
/*-------------------------------------------------------------------*/
DEVBLK *find_device_by_devnum (U16 devnum)
{
DEVBLK *dev;

    for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
        if (dev->devnum == devnum && dev->pmcw.flag5 & PMCW5_V) break;

    return dev;

} /* end function find_device_by_devnum */

/*-------------------------------------------------------------------*/
/* Function to find a device block given the subchannel number       */
/*-------------------------------------------------------------------*/
DEVBLK *find_device_by_subchan (U16 subchan)
{
DEVBLK *dev;

    for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
        if (dev->subchan == subchan) break;

    return dev;

} /* end function find_device_by_subchan */

