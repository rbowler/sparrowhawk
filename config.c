/* CONFIG.C     (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Configuration Builder                        */

/*-------------------------------------------------------------------*/
/* This module builds the configuration tables for the Hercules      */
/* ESA/390 emulator.  It reads information about the processors      */
/* and I/O devices from a configuration file.  It allocates          */
/* main storage and expanded storage, initializes control blocks,    */
/* and creates a detached thread to receive console connection       */
/* requests and attention interrupts.                                */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Global data areas                                                 */
/*-------------------------------------------------------------------*/
SYSBLK  sysblk;

/*-------------------------------------------------------------------*/
/* Static data areas                                                 */
/*-------------------------------------------------------------------*/
static int  stmt = 0;                   /* Config statement number   */
static BYTE buf[256];                   /* Config statement buffer   */

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
/* Returns 0 if successful, -1 if end of file                        */
/*-------------------------------------------------------------------*/
static int read_config (BYTE *fname, FILE *fp)
{
    while (1)
    {
        /* Increment statement counter */
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

        /* Check that statement ends with a newline */
        if (buf[strlen(buf)-1] != '\n')
        {
            fprintf (stderr,
                    "HHC002I File %s line %d is too long\n",
                    fname, stmt);
            exit(1);
        }

        /* Ignore comments and null statements */
        if (buf[0] == '\n' || buf[0] == '#')
           continue;

        break;
    } /* end while */

    return 0;
} /* end function read_config */

/*-------------------------------------------------------------------*/
/* Signal handler for SIGHUP signal                                  */
/*-------------------------------------------------------------------*/
static void sighup_handler (int signo)
{
    //printf ("config: sighup handler entered\n"); /*debug*/
    return;
} /* end function sighup_handler */

/*-------------------------------------------------------------------*/
/* Signal handler for SIGINT signal                                  */
/*-------------------------------------------------------------------*/
static void sigint_handler (int signo)
{
    //printf ("config: sigint handler entered\n"); /*debug*/

    /* Activate instruction stepping */
    sysblk.inststep = 1;
    return;
} /* end function sigint_handler */

/*-------------------------------------------------------------------*/
/* Function to build system configuration                            */
/*-------------------------------------------------------------------*/
void build_config (BYTE *fname)
{
int     rc;                             /* Return code               */
int     i;                              /* Array subscript           */
FILE   *fp;                             /* Configuration file pointer*/
BYTE   *sserial;                        /* -> CPU serial string      */
BYTE   *smainsize;                      /* -> Main size string       */
BYTE   *sxpndsize;                      /* -> Expanded size string   */
BYTE   *scnslport;                      /* -> Console port number    */
BYTE   *snumcpu;                        /* -> Number of CPUs         */
BYTE   *sloadparm;                      /* -> IPL load parameter     */
BYTE    version = 0x00;                 /* CPU version code          */
U32     serial;                         /* CPU serial number         */
U16     model = 0x0586;                 /* CPU model number          */
U16     mainsize;                       /* Main storage size (MB)    */
U16     xpndsize;                       /* Expanded storage size (MB)*/
U16     cnslport;                       /* Console port number       */
U16     numcpu;                         /* Number of CPUs            */
BYTE   *sdevnum;                        /* -> Device number string   */
BYTE   *sdevtype;                       /* -> Device type string     */
U16     devnum;                         /* Device number             */
U16     devtype;                        /* Device type               */
BYTE    c;                              /* Work area for sscanf      */
DEVBLK *dev;                            /* -> Device block           */
DEVBLK**dvpp;                           /* -> Device block address   */
#define MAX_ARGS 10                     /* Maximum #of devinit args  */
int     devargc;                        /* Arg count for devinit func*/
BYTE   *devargv[MAX_ARGS];              /* Arg array for devinit func*/
DEVIF  *devinit;                        /* -> Device init function   */
DEVXF  *devexec;                        /* -> Device exec function   */
int     subchan;                        /* Subchannel number         */

    /* Open the configuration file */
    fp = fopen (fname, "r");
    if (fp == NULL)
    {
        fprintf (stderr,
                "HHC003I Cannot open file %s: %s\n",
                fname, strerror(errno));
        exit(1);
    }

    /* Read the CPU record from the configuration file */
    if ( read_config (fname, fp) )
    {
        fprintf (stderr,
                "HHC004I CPU record missing from file %s\n",
                fname);
        exit(1);
    }

    /* Parse the CPU statement */
    sserial = strtok (buf, " \t\n");
    smainsize = strtok (NULL, " \t\n");
    sxpndsize = strtok (NULL, " \t\n");
    scnslport = strtok (NULL, " \t\n");
    snumcpu = strtok (NULL, " \t\n");
    sloadparm = strtok (NULL, " \t\n");

    if (sserial == NULL || smainsize == NULL || sxpndsize == NULL
        || scnslport == NULL || snumcpu == NULL || sloadparm == NULL)
    {
        fprintf (stderr,
                "HHC005I Error in %s line %d: Missing fields\n",
                fname, stmt);
        exit(1);
    }

    if (strlen(sserial) > 6
        || sscanf(sserial, "%lx%c", &serial, &c) != 1)
    {
        fprintf (stderr,
                "HHC006I Error in %s line %d: "
                "%s is not a valid serial number\n",
                fname, stmt, sserial);
        exit(1);
    }

    if (sscanf(smainsize, "%hu%c", &mainsize, &c) != 1
        || mainsize < 2 || mainsize > 256)
    {
        fprintf (stderr,
                "HHC007I Error in %s line %d: "
                "Invalid main storage size %s\n",
                fname, stmt, smainsize);
        exit(1);
    }

    if (sscanf(sxpndsize, "%hu%c", &xpndsize, &c) != 1
        || xpndsize > 1024)
    {
        fprintf (stderr,
                "HHC008I Error in %s line %d: "
                "Invalid expanded storage size %s\n",
                fname, stmt, sxpndsize);
        exit(1);
    }

    if (sscanf(scnslport, "%hu%c", &cnslport, &c) != 1
        || cnslport == 0)
    {
        fprintf (stderr,
                "HHC009I Error in %s line %d: "
                "Invalid console port number %s\n",
                fname, stmt, scnslport);
        exit(1);
    }

    if (sscanf(snumcpu, "%hu%c", &numcpu, &c) != 1
        || numcpu < 1 || numcpu > MAX_CPU_ENGINES)
    {
        fprintf (stderr,
                "HHC010I Error in %s line %d: "
                "Invalid number of CPUs %s\n",
                fname, stmt, snumcpu);
        exit(1);
    }

    if (strlen(sloadparm) > 8)
    {
        fprintf (stderr,
                "HHC011I Error in %s line %d: "
                "Load parameter %s exceeds 8 characters\n",
                fname, stmt, sloadparm);
        exit(1);
    }

    /* Clear the system configuration block */
    memset (&sysblk, 0, sizeof(SYSBLK));

    /* Initialize the CPU registers */
    sysblk.numcpu = numcpu;
    for (i = 0; i < MAX_CPU_ENGINES; i++)
    {
        sysblk.regs[i].cpuad = i;
    } /* end for(i) */

    /* Obtain main storage */
    sysblk.mainsize = mainsize * 1024 * 1024;
    sysblk.mainstor = malloc(sysblk.mainsize);
    if (sysblk.mainstor == NULL)
    {
        fprintf (stderr,
                "HHC012I Cannot obtain %dMB main storage: %s\n",
                mainsize, strerror(errno));
        exit(1);
    }

    /* Obtain main storage key array */
    sysblk.storkeys = malloc(sysblk.mainsize / 4096);
    if (sysblk.storkeys == NULL)
    {
        fprintf (stderr,
                "HHC013I Cannot obtain storage key array: %s\n",
                strerror(errno));
        exit(1);
    }

    if (xpndsize != 0)
    {
#ifdef FEATURE_EXPANDED_STORAGE
        /* Obtain expanded storage */
        sysblk.xpndsize = xpndsize * 256;
        sysblk.xpndstor = malloc(sysblk.xpndsize * 4096);
        if (sysblk.xpndstor == NULL)
        {
            fprintf (stderr,
                    "HHC014I Cannot obtain %dMB expanded storage: "
                    "%s\n",
                    xpndsize, strerror(errno));
            exit(1);
        }

        /* Obtain main storage key array */
        sysblk.xpndkeys = malloc(sysblk.xpndsize);
        if (sysblk.xpndkeys == NULL)
        {
            fprintf (stderr,
                    "HHC015I Cannot obtain expanded storage key "
                    "array: %s\n",
                    strerror(errno));
            exit(1);
        }
#else /*!FEATURE_EXPANDED_STORAGE*/
        fprintf (stderr,
                "HHC016I Expanded storage support not installed\n");
        exit(1);
#endif /*!FEATURE_EXPANDED_STORAGE*/
    } /* end if(sysblk.xpndsize) */

    /* Save the console port number */
    sysblk.cnslport = cnslport;

    /* Build CPU identifier */
    sysblk.cpuid = ((U64)version << 56)
                 | ((U64)serial << 32)
                 | (model << 16);

    /* Set the load parameter */
    memset (sysblk.loadparm, 0x4B, 8);
    for (i = 0; i < strlen(sloadparm); i++)
        sysblk.loadparm[i] = ascii_to_ebcdic[sloadparm[i]];

    /* Initialize locks, conditions, and attributes */
    initialize_lock (&sysblk.mainlock);
    initialize_lock (&sysblk.intlock);
    initialize_lock (&sysblk.conslock);
    initialize_condition (&sysblk.intcond);
    initialize_condition (&sysblk.conscond);
    initialize_detach_attr (&sysblk.detattr);

    /* Build the device configuration blocks */
    dvpp = &(sysblk.firstdev);
    for (subchan = 0; subchan <= 0xFFFF; subchan++)
    {
        /* Read next device record from the configuration file */
        if ( read_config (fname, fp) )
            break;

        /* Parse the device statement */
        sdevnum = strtok (buf, " \t\n");
        sdevtype = strtok (NULL, " \t\n");

        if (sdevnum == NULL || sdevtype == NULL)
        {
            fprintf (stderr,
                    "HHC017I Error in %s line %d: "
                    "Missing device number or device type\n",
                    fname, stmt);
            exit(1);
        }

        if (strlen(sdevnum) > 4
            || sscanf(sdevnum, "%hx%c", &devnum, &c) != 1)
        {
            fprintf (stderr,
                    "HHC018I Error in %s line %d: "
                    "%s is not a valid device number\n",
                    fname, stmt, sdevnum);
            exit(1);
        }

        if (sscanf(sdevtype, "%hx%c", &devtype, &c) != 1)
        {
            fprintf (stderr,
                    "HHC019I Error in %s line %d: "
                    "%s is not a valid device type\n",
                    fname, stmt, sdevtype);
            exit(1);
        }

        /* Use remaining fields in the device statement to create an
           argument array for device handler initialization function */
        for (devargc = 0; devargc < MAX_ARGS &&
            (devargv[devargc] = strtok (NULL, " \t\n")) != NULL;
            devargc++);

        /* Determine which device handler to use for this device */
        switch (devtype) {

        case 0x1052:
        case 0x3215:
            devinit = &constty_init_handler;
            devexec = &constty_execute_ccw;
            break;

        case 0x2501:
        case 0x3505:
            devinit = &cardrdr_init_handler;
            devexec = &cardrdr_execute_ccw;
            break;

        case 0x1403:
        case 0x3211:
            devinit = &printer_init_handler;
            devexec = &printer_execute_ccw;
            break;

        case 0x3420:
        case 0x3480:
            devinit = &simtape_init_handler;
            devexec = &simtape_execute_ccw;
            break;

        case 0x2314:
        case 0x3330:
        case 0x3350:
        case 0x3380:
        case 0x3390:
            devinit = &ckddasd_init_handler;
            devexec = &ckddasd_execute_ccw;
            break;

        case 0x3310:
        case 0x3370:
            devinit = &fbadasd_init_handler;
            devexec = &fbadasd_execute_ccw;
            break;

        case 0x3270:
            devinit = &loc3270_init_handler;
            devexec = &loc3270_execute_ccw;
            break;

        default:
            fprintf (stderr,
                    "HHC020I Error in %s line %d: "
                    "Device type %4.4X not recognized\n",
                    fname, stmt, devtype);
            devinit = NULL;
            devexec = NULL;
            exit(1);
        } /* end switch(devtype) */

        /* Obtain a device block */
        dev = (DEVBLK*)malloc(sizeof(DEVBLK));
        if (dev == NULL)
        {
            fprintf (stderr,
                    "HHC021I Cannot obtain device block "
                    "for device %4.4X: %s\n",
                    devnum, strerror(errno));
            exit(1);
        }
        memset (dev, 0, sizeof(DEVBLK));

        /* Initialize the device block */
        dev->subchan = subchan;
        dev->devnum = devnum;
        dev->devtype = devtype;
        dev->devexec = devexec;

        /* Initialize the path management control word */
        dev->pmcw.flag5 |= PMCW5_V;
        dev->pmcw.devnum[0] = dev->devnum >> 8;
        dev->pmcw.devnum[1] = dev->devnum & 0xFF;
        dev->pmcw.lpm = 0x80;
        dev->pmcw.pim = 0x80;
        dev->pmcw.pom = 0xFF;
        dev->pmcw.pam = 0x80;
        dev->pmcw.chpid[0] = dev->devnum >> 8;

        /* Initialize the device lock */
        initialize_lock (&dev->lock);

        /* Call the device handler initialization function */
        rc = (*devinit)(dev, devargc, devargv);
        if (rc < 0)
        {
            fprintf (stderr,
                    "HHC022I Error in %s line %d: "
                    "Device initialization failed for device %4.4X\n",
                    fname, stmt, devnum);
            exit(1);
        }

        /* Obtain device data buffer */
        if (dev->bufsize != 0)
        {
            dev->buf = malloc (dev->bufsize);
            if (dev->buf == NULL)
            {
                fprintf (stderr,
                        "HHC023I Cannot obtain buffer "
                        "for device %4.4X: %s\n",
                        dev->devnum, strerror(errno));
                exit(1);
            }
        }

        /* Chain the device block to the previous block */
        *dvpp = dev;
        dev->nextdev = NULL;
        dvpp = &(dev->nextdev);

    } /* end for(subchan) */

    /* Check that configuration contains at least one device */
    if (sysblk.firstdev == NULL)
    {
        fprintf (stderr,
                "HHC024I No device records in file %s\n",
                fname);
        exit(1);
    }

    /* Register the SIGHUP handler */
    if ( signal (SIGHUP, sighup_handler) == SIG_ERR )
    {
        fprintf (stderr,
                "HHC025I Cannot register SIGHUP handler: %s\n",
                strerror(errno));
        exit(1);
    }

    /* Register the SIGINT handler */
    if ( signal (SIGINT, sigint_handler) == SIG_ERR )
    {
        fprintf (stderr,
                "HHC026I Cannot register SIGINT handler: %s\n",
                strerror(errno));
        exit(1);
    }

    /* Start the console connection thread */
    if ( create_thread (&sysblk.cnsltid, &sysblk.detattr,
                        console_connection_handler, NULL) )
    {
        fprintf (stderr,
                "HHC027I Cannot create console thread: %s\n",
                strerror(errno));
        exit(1);
    }

} /* end function build_config */


/*-------------------------------------------------------------------*/
/* Function to find a device block given the device number           */
/*-------------------------------------------------------------------*/
DEVBLK *find_device_by_devnum ( U16 devnum )
{
DEVBLK  *dev;

    for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
        if (dev->devnum == devnum) break;

    return dev;

} /* end function find_device_by_devnum */


/*-------------------------------------------------------------------*/
/* Function to find a device block given the subchannel number       */
/*-------------------------------------------------------------------*/
DEVBLK *find_device_by_subchan ( U16 subchan )
{
DEVBLK  *dev;

    for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
        if (dev->subchan == subchan) break;

    return dev;

} /* end function find_device_by_subchan */


