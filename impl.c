/* IMPL.C       (c) Copyright Roger Bowler, 1999-2001                */
/*              Hercules Initialization Module                       */

/*-------------------------------------------------------------------*/
/* This module initializes the Hercules S/370 or ESA/390 emulator.   */
/* It builds the system configuration blocks, creates threads for    */
/* the console server, the timer handler, and central processors,    */
/* and activates the control panel which runs under the main thread. */
/*-------------------------------------------------------------------*/

#include "hercules.h"
#include <getopt.h>

/*-------------------------------------------------------------------*/
/* Signal handler for SIGHUP signal                                  */
/*-------------------------------------------------------------------*/
static void sighup_handler (int signo)
{
//  logmsg ("config: sighup handler entered for thread %lu\n",/*debug*/
//          thread_id());                                     /*debug*/
    return;
} /* end function sighup_handler */

/*-------------------------------------------------------------------*/
/* Signal handler for SIGINT signal                                  */
/*-------------------------------------------------------------------*/
static void sigint_handler (int signo)
{
//  logmsg ("config: sigint handler entered for thread %lu\n",/*debug*/
//          thread_id());                                     /*debug*/

    /* Ignore signal unless presented on console thread */
    if (thread_id() != sysblk.cnsltid)
        return;

    /* Exit if previous SIGINT request was not actioned */
    if (sysblk.sigintreq)
        exit(1);

    /* Set SIGINT request pending flag */
    sysblk.sigintreq = 1;

    /* Activate instruction stepping */
    sysblk.inststep = 1;
    set_doinst();
    return;
} /* end function sigint_handler */

/*-------------------------------------------------------------------*/
/* IMPL main entry point                                             */
/*-------------------------------------------------------------------*/
int main (int argc, char *argv[])
{
BYTE   *cfgfile = "hercules.cnf";       /* -> Configuration filename */
int     c;                              /* Work area for getopt      */

    /* Display the version identifier */
    fprintf (stderr,
            "Hercules %s version %s "
            "(c)Copyright Roger Bowler, 1994-2001\n",
            ARCHITECTURE_NAME, MSTRING(VERSION));

    /* Process the command line options */
    while ((c = getopt(argc, argv, "f:")) != EOF)
    {
        switch (c) {
        case 'f':
            cfgfile = optarg;
            break;
        default:
            fprintf (stderr,
                    "usage: %s [-f config-filename]\n",
                    argv[0]);
            exit (1);
        } /* end switch(c) */
    } /* end while */

    /* Build system configuration */
    build_config (cfgfile);

    /* Register the SIGHUP handler */
    if ( signal (SIGHUP, sighup_handler) == SIG_ERR )
    {
        fprintf (stderr,
                "HHC030I Cannot register SIGHUP handler: %s\n",
                strerror(errno));
        exit(1);
    }

    /* Register the SIGINT handler */
    if ( signal (SIGINT, sigint_handler) == SIG_ERR )
    {
        fprintf (stderr,
                "HHC031I Cannot register SIGINT handler: %s\n",
                strerror(errno));
        exit(1);
    }

    /* Start the console connection thread */
    if ( create_thread (&sysblk.cnsltid, &sysblk.detattr,
                        console_connection_handler, NULL) )
    {
        fprintf (stderr,
                "HHC032I Cannot create console thread: %s\n",
                strerror(errno));
        exit(1);
    }

    /* Start the TOD clock and CPU timer thread */
    if ( create_thread (&sysblk.todtid, &sysblk.detattr,
                        timer_update_thread, NULL) )
    {
        fprintf (stderr,
                "HHC033I Cannot create timer thread: %s\n",
                strerror(errno));
        exit(1);
    }

    /* Activate the control panel */
    panel_display ();

    return 0;
} /* end function main */
