/* EXTERNAL.C   (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 External Interrupt and Timer                 */

/*-------------------------------------------------------------------*/
/* This module implements external interrupt and timer               */
/* functions for the Hercules ESA/390 emulator.                      */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Load external interrupt new PSW                                   */
/*-------------------------------------------------------------------*/
static void external_interrupt (int code, REGS *regs)
{
PSA    *psa;
int     rc;

    /* Store the interrupt code in the PSW */
    regs->psw.intcode = code;

    /* Point to PSA in main storage */
    psa = (PSA*)(sysblk.mainstor + regs->pxr);

    /* Store current PSW at PSA+X'18' */
    store_psw (&(regs->psw), psa->extold);

    /* Store CPU address at PSA+X'84' */
    psa->extcpad[0] = regs->cpuad >> 8;
    psa->extcpad[1] = regs->cpuad & 0xFF;

    /* For ECMODE, store external interrupt code at PSA+X'86' */
    if ( regs->psw.ecmode )
    {
        psa->extint[0] = code >> 8;
        psa->extint[1] = code & 0xFF;
    }

    /* Load new PSW from PSA+X'58' */
    rc = load_psw (&(regs->psw), psa->extnew);
    if ( rc )
    {
        logmsg ("Invalid external interrupt new PSW: "
                "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
                psa->extnew[0], psa->extnew[1], psa->extnew[2],
                psa->extnew[3], psa->extnew[4], psa->extnew[5],
                psa->extnew[6], psa->extnew[7]);
        regs->cpustate = CPUSTATE_STOPPED;
    }

} /* end function external_interrupt */

/*-------------------------------------------------------------------*/
/* Perform external interrupt if pending                             */
/*-------------------------------------------------------------------*/
void perform_external_interrupt (REGS *regs)
{
PSA    *psa;                            /* -> Prefixed storage area  */

    /* External interrupt if CPU timer is negative */
    if ((S64)regs->ptimer < 0
        && (regs->psw.sysmask & PSW_EXTMASK)
        && (regs->cr[0] & CR0_XM_PTIMER))
    {
        if (sysblk.insttrace || sysblk.inststep)
        {
            logmsg ("External interrupt: CPU timer=%16.16llX\n",
                    regs->ptimer);
        }
        external_interrupt (EXT_CPU_TIMER_INTERRUPT, regs);
        return;
    }

    /* External interrupt if TOD clock exceeds clock comparator */
    if (sysblk.todclk > regs->clkc
        && sysblk.inststep == 0
        && (regs->psw.sysmask & PSW_EXTMASK)
        && (regs->cr[0] & CR0_XM_CLKC))
    {
        if (sysblk.insttrace || sysblk.inststep)
        {
            logmsg ("External interrupt: Clock comparator\n");
        }
        external_interrupt (EXT_CLOCK_COMPARATOR_INTERRUPT, regs);
        return;
    }

#ifdef FEATURE_INTERVAL_TIMER
    if (regs->itimer_pending
        && (regs->psw.sysmask & PSW_EXTMASK)
        && (regs->cr[0] & CR0_XM_ITIMER))
    {
        if (sysblk.insttrace || sysblk.inststep)
        {
            logmsg ("External interrupt: Interval timer\n");
        }
        external_interrupt (EXT_INTERVAL_TIMER_INTERRUPT, regs);
        regs->itimer_pending = 0;
        return;
    }
#endif /*FEATURE_INTERVAL_TIMER*/

    /* External interrupt if service signal is pending */
    if (sysblk.servsig
        && (regs->psw.sysmask & PSW_EXTMASK)
        && (regs->cr[0] & CR0_XM_SERVSIG))
    {
        sysblk.servparm = APPLY_PREFIXING (sysblk.servparm, regs->pxr);

        logmsg ("External interrupt: Service signal %8.8X\n",
                sysblk.servparm);

        /* Store service signal parameter at PSA+X'80' */
        psa = (PSA*)(sysblk.mainstor + regs->pxr);
        psa->extparm[0] = (sysblk.servparm & 0xFF000000) >> 24;
        psa->extparm[1] = (sysblk.servparm & 0xFF0000) >> 16;
        psa->extparm[2] = (sysblk.servparm & 0xFF00) >> 8;
        psa->extparm[3] = sysblk.servparm & 0xFF;

        /* Reset service signal pending */
        sysblk.servsig = 0;

        /* Generate service signal interrupt */
        external_interrupt (EXT_SERVICE_SIGNAL_INTERRUPT, regs);
        return;
    }

    /* External interrupt if console interrupt key was depressed */
    if (sysblk.intkey
        && (regs->psw.sysmask & PSW_EXTMASK)
        && (regs->cr[0] & CR0_XM_INTKEY))
    {
        logmsg ("External interrupt: Interrupt key\n");

        /* Reset interrupt key pending */
        sysblk.intkey = 0;

        /* Generate interrupt key interrupt */
        external_interrupt (EXT_INTERRUPT_KEY_INTERRUPT, regs);
        return;
    }

} /* end function perform_external_interrupt */

/*-------------------------------------------------------------------*/
/* TOD clock and timer thread                                        */
/*                                                                   */
/* This function runs as a separate thread.  It wakes up every       */
/* x milliseconds, updates the TOD clock, and decrements the         */
/* CPU timer for each CPU.  If any CPU timer goes negative, or       */
/* if the TOD clock exceeds the clock comparator for any CPU,        */
/* it signals any waiting CPUs to wake up and process interrupts.    */
/*-------------------------------------------------------------------*/
void *timer_update_thread (void *argp)
{
#ifdef FEATURE_INTERVAL_TIMER
PSA    *psa;                            /* -> Prefixed storage area  */
S32     itimer;                         /* Interval timer value      */
S32     olditimer;                      /* Previous interval timer   */
#endif /*FEATURE_INTERVAL_TIMER*/
int     cpu;                            /* CPU engine number         */
REGS   *regs;                           /* -> CPU register context   */
int     intflag = 0;                    /* 1=Interrupt possible      */
U64     prev;                           /* Previous TOD clock value  */
U64     diff;                           /* Difference between new and
                                           previous TOD clock values */
U64     dreg;                           /* Double register work area */
struct  timeval tv;                     /* Structure for gettimeofday
                                           and select function calls */
#define CLOCK_RESOLUTION        10      /* TOD clock resolution in
                                           milliseconds              */

    /* Display thread started message on control panel */
//  logmsg ("HHC610I Timer thread started: id=%ld\n",
//          thread_id());

    while (1)
    {
        /* Get current time */
        gettimeofday (&tv, NULL);

        /* Load number of seconds since 00:00:00 01 Jan 1970 */
        dreg = (U64)tv.tv_sec;

        /* Add number of seconds from 1900 to 1970 */
        dreg += 86400ULL * (70*365 + 17);

        /* Convert to microseconds */
        dreg = dreg * 1000000 + tv.tv_usec;

        /* Convert to TOD clock format */
        dreg <<= 12;

        /* Obtain the TOD clock update lock */
        obtain_lock (&sysblk.todlock);

        /* Calculate the difference between the new TOD clock
           value and the previous value, if the clock is set */
        prev = sysblk.todclk & 0xFFFFFFFFFFFFF000ULL;
        diff = (prev == 0 ? 0 : dreg - prev);

        /* Update the TOD clock */
        sysblk.todclk = dreg;

        /* Release the TOD clock update lock */
        release_lock (&sysblk.todlock);

        /* Decrement the CPU timer for each CPU */
        for (cpu = 0; cpu < sysblk.numcpu; cpu++)
        {
            /* Point to the CPU register context */
            regs = sysblk.regs + cpu;

            /* Decrement the CPU timer */
            (S64)regs->ptimer -= diff;

            /* Set interrupt flag if the CPU timer is negative or
               if the TOD clock value exceeds the clock comparator */
            if ((S64)regs->ptimer < 0
                || sysblk.todclk > regs->clkc)
                intflag = 1;

#ifdef FEATURE_INTERVAL_TIMER
            /* Point to PSA in main storage */
            psa = (PSA*)(sysblk.mainstor + regs->pxr);

            /* Decrement bit position 26 of the location 80 timer */
            itimer = (S32)(((U32)(psa->inttimer[0]) << 24)
                                | ((U32)(psa->inttimer[1]) << 16)
                                | ((U32)(psa->inttimer[2]) << 8)
                                | (U32)(psa->inttimer[3]));
            olditimer = itimer;
            itimer -= 32 * CLOCK_RESOLUTION;
            psa->inttimer[0] = ((U32)itimer >> 24) & 0xFF;
            psa->inttimer[1] = ((U32)itimer >> 16) & 0xFF;
            psa->inttimer[2] = ((U32)itimer >> 8) & 0xFF;
            psa->inttimer[3] = (U32)itimer & 0xFF;

            /* Set interrupt flag and interval timer interrupt pending
               if the interval timer went from positive to negative */
            if (itimer < 0 && olditimer > 0)
            {
                intflag = 1;
                regs->itimer_pending = 1;
            }
#endif /*FEATURE_INTERVAL_TIMER*/

        } /* end for(cpu) */

        /* If a CPU timer or clock comparator interrupt condition
           was detected for any CPU, then wake up all waiting CPUs */
        if (intflag)
        {
            obtain_lock (&sysblk.intlock);
            signal_condition (&sysblk.intcond);
            release_lock (&sysblk.intlock);
        }

        /* Sleep for CLOCK_RESOLUTION milliseconds */
        tv.tv_sec = CLOCK_RESOLUTION / 1000;
        tv.tv_usec = (CLOCK_RESOLUTION * 1000) % 1000000;
        select (0, NULL, NULL, NULL, &tv);

    } /* end while */

    return NULL;

} /* end function timer_update_thread */

