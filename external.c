/* EXTERNAL.C   (c) Copyright Roger Bowler, 1999-2000                */
/*              ESA/390 External Interrupt and Timer                 */

/*-------------------------------------------------------------------*/
/* This module implements external interrupt, timer, and signalling  */
/* functions for the Hercules ESA/390 emulator.                      */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      TOD clock offset contributed by Jay Maynard                  */
/*      Correction to timer interrupt by Valery Pogonchenko          */
/*      TOD clock drag factor contributed by Jan Jaeger              */
/*      Synchronized broadcasting contributed by Jan Jaeger          */
/*      CPU timer and clock comparator interrupt improvements by     */
/*          Jan Jaeger, after a suggestion by Willem Koynenberg      */
/*      Prevent TOD clock and CPU timer from going back - Jan Jaeger */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Load external interrupt new PSW                                   */
/*-------------------------------------------------------------------*/
static void external_interrupt (int code, REGS *regs)
{
PSA    *psa;
int     rc;

    /* reset the cpuint indicator */
    regs->cpuint = regs->storstat
#ifdef FEATURE_INTERVAL_TIMER
                    || regs->itimer_pending
#endif /*FEATURE_INTERVAL_TIMER*/
                    || regs->extcall
                    || regs->emersig
                    || regs->ckpend
                    || regs->ptpend;

    /* Set the main storage reference and change bits */
    STORAGE_KEY(regs->pxr) |= (STORKEY_REF | STORKEY_CHANGE);

    /* Store the interrupt code in the PSW */
    regs->psw.intcode = code;

    /* Point to PSA in main storage */
    psa = (PSA*)(sysblk.mainstor + regs->pxr);

    /* Store current PSW at PSA+X'18' */
    store_psw (&(regs->psw), psa->extold);

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
        release_lock(&sysblk.intlock);
        logmsg ("CPU%4.4X: Invalid external new PSW: "
                "%2.2X%2.2X%2.2X%2.2X %2.2X%2.2X%2.2X%2.2X\n",
                regs->cpuad,
                psa->extnew[0], psa->extnew[1], psa->extnew[2],
                psa->extnew[3], psa->extnew[4], psa->extnew[5],
                psa->extnew[6], psa->extnew[7]);
        program_check(regs, rc);
    }

} /* end function external_interrupt */

/*-------------------------------------------------------------------*/
/* Perform external interrupt if pending                             */
/*                                                                   */
/* This function is called by the CPU to check whether any           */
/* external interrupt conditions are pending, and to perform         */
/* an external interrupt if so.  If multiple external interrupts     */
/* are pending, then only the highest priority interrupt is taken,   */
/* and any other interrupts remain pending.  Remaining interrupts    */
/* will be processed one-by-one during subsequent calls.             */
/*                                                                   */
/* Important notes:                                                  */
/* (i)  This function must NOT be called if the CPU is disabled      */
/*      for external interrupts (PSW bit 7 is zero).                 */
/* (ii) The caller MUST hold the interrupt lock (sysblk.intlock)     */
/*      to ensure correct serialization of interrupt pending bits.   */
/*-------------------------------------------------------------------*/
void perform_external_interrupt (REGS *regs)
{
PSA    *psa;                            /* -> Prefixed storage area  */
U16     cpuad;                          /* Originating CPU address   */

    /* External interrupt if console interrupt key was depressed */
    if (sysblk.intkey
        && (regs->cr[0] & CR0_XM_INTKEY))
    {
        logmsg ("External interrupt: Interrupt key\n");

        /* Reset interrupt key pending */
        sysblk.intkey = 0;
        sysblk.extpending = sysblk.servsig;

        /* Generate interrupt key interrupt */
        external_interrupt (EXT_INTERRUPT_KEY_INTERRUPT, regs);
        return;
    }

    /* External interrupt if emergency signal is pending */
    if (regs->emersig
        && (regs->cr[0] & CR0_XM_EMERSIG))
    {
        /* Find first CPU which generated an emergency signal */
        for (cpuad = 0; regs->emercpu[cpuad] == 0; cpuad++)
        {
            if (cpuad >= MAX_CPU_ENGINES)
            {
                regs->emersig = 0;
                return;
            }
        } /* end for(cpuad) */

// /*debug*/ logmsg ("External interrupt: Emergency Signal from CPU %d\n",
// /*debug*/    cpuad);

        /* Reset the indicator for the CPU which was found */
        regs->emercpu[cpuad] = 0;

        /* Store originating CPU address at PSA+X'84' */
        psa = (PSA*)(sysblk.mainstor + regs->pxr);
        psa->extcpad[0] = cpuad >> 8;
        psa->extcpad[1] = cpuad & 0xFF;

        /* Reset emergency signal pending flag if there are
           no other CPUs which generated emergency signal */
        regs->emersig = 0;
        while (++cpuad < MAX_CPU_ENGINES)
        {
            if (regs->emercpu[cpuad])
            {
                regs->cpuint = regs->emersig = 1;
                break;
            }
        } /* end while */

        /* Generate emergency signal interrupt */
        external_interrupt (EXT_EMERGENCY_SIGNAL_INTERRUPT, regs);
        return;
    }

    /* External interrupt if external call is pending */
    if (regs->extcall
        && (regs->cr[0] & CR0_XM_EXTCALL))
    {
//  /*debug*/logmsg ("External interrupt: External Call from CPU %d\n",
//  /*debug*/       regs->extccpu);

        /* Reset external call pending */
        regs->extcall = 0;

        /* Store originating CPU address at PSA+X'84' */
        psa = (PSA*)(sysblk.mainstor + regs->pxr);
        psa->extcpad[0] = regs->extccpu >> 8;
        psa->extcpad[1] = regs->extccpu & 0xFF;

        /* Generate external call interrupt */
        external_interrupt (EXT_EXTERNAL_CALL_INTERRUPT, regs);
        return;
    }

    /* External interrupt if TOD clock exceeds clock comparator */
    if (sysblk.todclk > regs->clkc
        && sysblk.insttrace == 0
        && sysblk.inststep == 0
        && (regs->cr[0] & CR0_XM_CLKC))
    {
        if (sysblk.insttrace || sysblk.inststep)
        {
            logmsg ("External interrupt: Clock comparator\n");
        }
        external_interrupt (EXT_CLOCK_COMPARATOR_INTERRUPT, regs);
        return;
    }

    /* External interrupt if CPU timer is negative */
    if ((S64)regs->ptimer < 0
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

    /* External interrupt if interval timer interrupt is pending */
#ifdef FEATURE_INTERVAL_TIMER
    if (regs->itimer_pending
        && (regs->cr[0] & CR0_XM_ITIMER))
    {
        if (sysblk.insttrace || sysblk.inststep)
        {
            logmsg ("External interrupt: Interval timer\n");
        }
        regs->itimer_pending = 0;
        external_interrupt (EXT_INTERVAL_TIMER_INTERRUPT, regs);

        return;
    }
#endif /*FEATURE_INTERVAL_TIMER*/

    /* External interrupt if service signal is pending */
    if (sysblk.servsig
        && (regs->cr[0] & CR0_XM_SERVSIG))
    {
        /* Apply prefixing if the parameter is a storage address */
        if ((sysblk.servparm & 0x00000007) == 0)
            sysblk.servparm =
                APPLY_PREFIXING (sysblk.servparm, regs->pxr);

//      logmsg ("External interrupt: Service signal %8.8X\n",
//              sysblk.servparm);

        /* Store service signal parameter at PSA+X'80' */
        psa = (PSA*)(sysblk.mainstor + regs->pxr);
        psa->extparm[0] = (sysblk.servparm & 0xFF000000) >> 24;
        psa->extparm[1] = (sysblk.servparm & 0xFF0000) >> 16;
        psa->extparm[2] = (sysblk.servparm & 0xFF00) >> 8;
        psa->extparm[3] = sysblk.servparm & 0xFF;

        /* Reset service signal pending */
        sysblk.servsig = 0;
        sysblk.extpending = sysblk.intkey;

        /* Generate service signal interrupt */
        external_interrupt (EXT_SERVICE_SIGNAL_INTERRUPT, regs);
        return;
    }

    /* reset the cpuint indicator */
    regs->cpuint = regs->storstat
#ifdef FEATURE_INTERVAL_TIMER
                    || regs->itimer_pending
#endif /*FEATURE_INTERVAL_TIMER*/
                    || regs->extcall
                    || regs->emersig
                    || regs->ckpend
                    || regs->ptpend;

} /* end function perform_external_interrupt */

/*-------------------------------------------------------------------*/
/* Update TOD clock                                                  */
/*                                                                   */
/* This function updates the TOD clock. It is called by the timer    */
/* thread, and by the STCK instruction handler. It depends on the    */
/* timer update thread to process any interrupts except for a        */
/* clock comparator interrupt which becomes pending during its       */
/* execution; those are signaled by this routine before it finishes. */
/*-------------------------------------------------------------------*/
void update_TOD_clock(void)
{
struct timeval	tv;			/* Current time              */
U64		dreg;			/* Double register work area */
int		intflag = 0;		/* Need to signal interrupt  */
int		cpu;			/* CPU counter               */
REGS	       *regs;			/* -> CPU register context   */

    /* Get current time */
    gettimeofday (&tv, NULL);

    /* Load number of seconds since 00:00:00 01 Jan 1970 */
    dreg = (U64)tv.tv_sec;

    /* Convert to microseconds */
    dreg = dreg * 1000000 + tv.tv_usec;

#ifdef TODCLOCK_DRAG_FACTOR
    if (sysblk.toddrag > 1)
        dreg = sysblk.todclock_init +
		(dreg - sysblk.todclock_init) / sysblk.toddrag;
#endif /*TODCLOCK_DRAG_FACTOR*/

    /* Obtain the TOD clock update lock */
    obtain_lock (&sysblk.todlock);

    /* Add number of microseconds from TOD base to 1970 */
    dreg += sysblk.todoffset;

    /* Shift left 4 bits so that bits 0-7=TOD Clock Epoch,
       bits 8-59=TOD Clock bits 0-51, bits 60-63=zero */
    dreg <<= 4;

    /* Ensure that the clock does not go backwards and always
       returns a unique value in the microsecond range */
    if( dreg > sysblk.todclk)
        sysblk.todclk = dreg;
    else sysblk.todclk += 16;

    /* Release the TOD clock update lock */
    release_lock (&sysblk.todlock);

    /* Access the diffent register contexts with the intlock held */
    obtain_lock (&sysblk.intlock);

    /* Decrement the CPU timer for each CPU */
#ifdef FEATURE_CPU_RECONFIG 
    for (cpu = 0; cpu < MAX_CPU_ENGINES; cpu++)
      if(sysblk.regs[cpu].cpuonline)
#else /*!FEATURE_CPU_RECONFIG*/
    for (cpu = 0; cpu < sysblk.numcpu; cpu++)
#endif /*!FEATURE_CPU_RECONFIG*/
    {
        /* Point to the CPU register context */
        regs = sysblk.regs + cpu;

	/* Signal clock comparator interrupt if needed */
        if(sysblk.todclk > regs->clkc)
            regs->cpuint = regs->ckpend = intflag = 1;
        else
            regs->ckpend = 0;

    } /* end for(cpu) */

    /* If a CPU timer or clock comparator interrupt condition
       was detected for any CPU, then wake up all waiting CPUs */
    if (intflag)
        signal_condition (&sysblk.intcond);

    release_lock(&sysblk.intlock);

} /* end function update_TOD_clock */

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
#ifdef MIPS_COUNTING
int     msecctr = 0;                    /* Millisecond counter       */
#endif /*MIPS_COUNTING*/
int     cpu;                            /* CPU engine number         */
REGS   *regs;                           /* -> CPU register context   */
int     intflag = 0;                    /* 1=Interrupt possible      */
U64     prev;                           /* Previous TOD clock value  */
U64     diff;                           /* Difference between new and
                                           previous TOD clock values */
struct  timeval tv;                     /* Structure for gettimeofday
                                           and select function calls */

    /* Display thread started message on control panel */
    logmsg ("HHC610I Timer thread started: tid=%8.8lX, pid=%d\n",
            thread_id(), getpid());
    
#ifdef TODCLOCK_DRAG_FACTOR
    /* Get current time */
    gettimeofday (&tv, NULL);

    /* Load number of seconds since 00:00:00 01 Jan 1970 */
    sysblk.todclock_init = (U64)tv.tv_sec;

    /* Convert to microseconds */
    sysblk.todclock_init = sysblk.todclock_init * 1000000 + tv.tv_usec;
#endif /*TODCLOCK_DRAG_FACTOR*/

    while (1)
    {
	/* Save the old TOD clock value */
        prev = sysblk.todclk;
        
        /* Update TOD clock */
        update_TOD_clock();

        /* Get the difference between the last TOD saved and this one */
        diff = (prev == 0 ? 0 : sysblk.todclk - prev);

        /* Shift the epoch out of the difference for the CPU timer */
        diff <<= 8;

        /* Access the diffent register contexts with the intlock held */
        obtain_lock(&sysblk.intlock);

        /* Decrement the CPU timer for each CPU */
#ifdef FEATURE_CPU_RECONFIG 
        for (cpu = 0; cpu < MAX_CPU_ENGINES; cpu++)
          if(sysblk.regs[cpu].cpuonline)
#else /*!FEATURE_CPU_RECONFIG*/
        for (cpu = 0; cpu < sysblk.numcpu; cpu++)
#endif /*!FEATURE_CPU_RECONFIG*/
        {
            /* Point to the CPU register context */
            regs = sysblk.regs + cpu;

            /* Decrement the CPU timer if the CPU is running */
            if(regs->cpustate == CPUSTATE_STARTED && (S64)diff > 0)
                (S64)regs->ptimer -= (S64)diff;

            /* Set interrupt flag if the CPU timer is negative */
            if ((S64)regs->ptimer < 0)
                regs->cpuint = regs->ptpend = intflag = 1;
            else
                regs->ptpend = 0;

#ifdef FEATURE_INTERVAL_TIMER
            /* Point to PSA in main storage */
            psa = (PSA*)(sysblk.mainstor + regs->pxr);

            /* Decrement the location 80 timer */
            itimer = (S32)(((U32)(psa->inttimer[0]) << 24)
                                | ((U32)(psa->inttimer[1]) << 16)
                                | ((U32)(psa->inttimer[2]) << 8)
                                | (U32)(psa->inttimer[3]));
            olditimer = itimer;
            
            /* The interval timer is decremented as though bit 23 is
               decremented by one every 1/300 of a second. This comes
               out to subtracting 768 (X'300') every 1/100 of a second.
               76800/CLK_TCK comes out to 768 on Intel versions of
               Linux, where the clock ticks every 1/100 second; it
               comes out to 75 on the Alpha, with its 1024/second
               tick interval. See 370 POO page 4-29. (ESA doesn't
               even have an interval timer.) */
            itimer -= 76800 / CLK_TCK;
            psa->inttimer[0] = ((U32)itimer >> 24) & 0xFF;
            psa->inttimer[1] = ((U32)itimer >> 16) & 0xFF;
            psa->inttimer[2] = ((U32)itimer >> 8) & 0xFF;
            psa->inttimer[3] = (U32)itimer & 0xFF;

            /* Set interrupt flag and interval timer interrupt pending
               if the interval timer went from positive to negative */
            if (itimer < 0 && olditimer >= 0)
                regs->cpuint = regs->itimer_pending = intflag = 1;
#endif /*FEATURE_INTERVAL_TIMER*/

        } /* end for(cpu) */

        /* If a CPU timer or clock comparator interrupt condition
           was detected for any CPU, then wake up all waiting CPUs */
        if (intflag)
            signal_condition (&sysblk.intcond);

        release_lock(&sysblk.intlock);

#ifdef MIPS_COUNTING
        /* Calculate MIPS rate...allow for the Alpha's 1024 ticks/second
           internal clock, as well as everyone else's 100/second */
        msecctr += 1024 / CLK_TCK;
        if (msecctr > 999)
        {
#ifdef FEATURE_CPU_RECONFIG 
            for (cpu = 0; cpu < MAX_CPU_ENGINES; cpu++)
              if(sysblk.regs[cpu].cpuonline)
#else /*!FEATURE_CPU_RECONFIG*/
            for (cpu = 0; cpu < sysblk.numcpu; cpu++)
#endif /*!FEATURE_CPU_RECONFIG*/
            {
                /* Point to the CPU register context */
                regs = sysblk.regs + cpu;

                /* Calculate instructions/millisecond for this CPU */
                regs->mipsrate =
                    (regs->instcount - regs->prevcount) / msecctr;

                /* Save the instruction counter */
                regs->prevcount = regs->instcount;

            } /* end for(cpu) */

            /* Reset the millisecond counter */
            msecctr = 0;

        } /* end if(msecctr) */
#endif /*MIPS_COUNTING*/

        /* Sleep for one system clock tick by specifying a one-microsecond
           delay, which will get stretched out to the next clock tick */
        tv.tv_sec = 0;
        tv.tv_usec = 1;
        select (0, NULL, NULL, NULL, &tv);

    } /* end while */

    return NULL;

} /* end function timer_update_thread */

/*-------------------------------------------------------------------*/
/* Store Status                                                      */
/* Input:                                                            */
/*      sregs   Register context of CPU whose status is to be stored */
/*      aaddr   A valid absolute address of a 512-byte block into    */
/*              which status is to be stored                         */
/*-------------------------------------------------------------------*/
void store_status (REGS *ssreg, U32 aaddr)
{
U64     dreg;                           /* Double register work area */
U32     n;                              /* 32 bit work area          */
int     i;                              /* Array subscript           */
PSA    *sspsa;                          /* -> Store status area      */

    /* Point to the PSA into which status is to be stored */
    sspsa = (PSA*)(sysblk.mainstor + aaddr);

    /* Store CPU timer in bytes 216-223 */
    dreg = ssreg->ptimer;
    sspsa->storeptmr[0] = (dreg >> 56) & 0xFF;
    sspsa->storeptmr[1] = (dreg >> 48) & 0xFF;
    sspsa->storeptmr[2] = (dreg >> 40) & 0xFF;
    sspsa->storeptmr[3] = (dreg >> 32) & 0xFF;
    sspsa->storeptmr[4] = (dreg >> 24) & 0xFF;
    sspsa->storeptmr[5] = (dreg >> 16) & 0xFF;
    sspsa->storeptmr[6] = (dreg >> 8) & 0xFF;
    sspsa->storeptmr[7] = dreg & 0xFF;

    /* Store clock comparator in bytes 224-231 */
    dreg = ssreg->clkc << 8;
    sspsa->storeclkc[0] = (dreg >> 56) & 0xFF;
    sspsa->storeclkc[1] = (dreg >> 48) & 0xFF;
    sspsa->storeclkc[2] = (dreg >> 40) & 0xFF;
    sspsa->storeclkc[3] = (dreg >> 32) & 0xFF;
    sspsa->storeclkc[4] = (dreg >> 24) & 0xFF;
    sspsa->storeclkc[5] = (dreg >> 16) & 0xFF;
    sspsa->storeclkc[6] = (dreg >> 8) & 0xFF;
    sspsa->storeclkc[7] = dreg & 0xFF;

    /* Store PSW in bytes 256-263 */
    store_psw (&(ssreg->psw), sspsa->storepsw);

    /* Store prefix register in bytes 264-267 */
    sspsa->storepfx[0] = (ssreg->pxr >> 24) & 0xFF;
    sspsa->storepfx[1] = (ssreg->pxr >> 16) & 0xFF;
    sspsa->storepfx[2] = (ssreg->pxr >> 8) & 0xFF;
    sspsa->storepfx[3] = ssreg->pxr & 0xFF;

    /* Store access registers in bytes 288-351 */
    for (i = 0; i < 16; i++)
    {
        n = ssreg->ar[i];
        sspsa->storear[i][0] = (n >> 24) & 0xFF;
        sspsa->storear[i][1] = (n >> 16) & 0xFF;
        sspsa->storear[i][2] = (n >> 8) & 0xFF;
        sspsa->storear[i][3] = n & 0xFF;
    } /* end for(i) */

    /* Store floating-point registers in bytes 352-383 */
    for (i = 0; i < 8; i++)
    {
        n = ssreg->fpr[i];
        sspsa->storefpr[i][0] = (n >> 24) & 0xFF;
        sspsa->storefpr[i][1] = (n >> 16) & 0xFF;
        sspsa->storefpr[i][2] = (n >> 8) & 0xFF;
        sspsa->storefpr[i][3] = n & 0xFF;
    } /* end for(i) */

    /* Store general-purpose registers in bytes 384-447 */
    for (i = 0; i < 16; i++)
    {
        n = ssreg->gpr[i];
        sspsa->storegpr[i][0] = (n >> 24) & 0xFF;
        sspsa->storegpr[i][1] = (n >> 16) & 0xFF;
        sspsa->storegpr[i][2] = (n >> 8) & 0xFF;
        sspsa->storegpr[i][3] = n & 0xFF;
    } /* end for(i) */

    /* Store control registers in bytes 448-511 */
    for (i = 0; i < 16; i++)
    {
        n = ssreg->cr[i];
        sspsa->storecr[i][0] = (n >> 24) & 0xFF;
        sspsa->storecr[i][1] = (n >> 16) & 0xFF;
        sspsa->storecr[i][2] = (n >> 8) & 0xFF;
        sspsa->storecr[i][3] = n & 0xFF;
    } /* end for(i) */

    logmsg ("HHC611I CPU %d status stored "
            "at absolute location %8.8X\n",
            ssreg->cpuad, aaddr);

} /* end function store_status */


#if MAX_CPU_ENGINES > 1
/*-------------------------------------------------------------------*/
/* Synchronize broadcast request                                     */
/* Input:                                                            */
/*      regs    A pointer to the CPU register context                */
/*      type    A pointer to the request counter in the sysblk for   */
/*              the requested function (brdcstptlb or brdcstpalb),   */
/*              or zero in case of being a target being synchronized */
/*                                                                   */
/* If the type is zero then the intlock MUST be held, else           */
/* the intlock MUST NOT be held.                                     */
/*                                                                   */
/* Signals all other CPU's to perform a requested function           */
/* synchronously, such as purging the ALB and TLB buffers.           */
/* The CPU issuing the broadcast request will wait until             */
/* all other CPU's have performed the requested action.         *JJ  */
/*-------------------------------------------------------------------*/
void synchronize_broadcast (REGS *regs, U32 *type)
{
int     i;                              /* Array subscript           */

    /* If type is specified then obtain lock and increment counter */
    if (type != NULL)
    {
        /* Obtain the intlock for CSP or IPTE */
        obtain_lock (&sysblk.intlock);

        /* Increment the counter for the specified function */
        (*type)++;
    }

    /* Initiate synchronization if this is the initiating CPU */
    if (sysblk.brdcstncpu == 0)
    {
        /* Set number of CPU's to synchronize */
        sysblk.brdcstncpu = sysblk.numcpu;

        /* Redrive all stopped CPU's */
#ifdef FEATURE_CPU_RECONFIG 
        for (i = 0; i < MAX_CPU_ENGINES; i++)
          if(sysblk.regs[i].cpuonline)
#else /*!FEATURE_CPU_RECONFIG*/
        for (i = 0; i < sysblk.numcpu; i++)
#endif /*!FEATURE_CPU_RECONFIG*/
            if (sysblk.regs[i].cpustate == CPUSTATE_STOPPED)
                sysblk.regs[i].cpustate = CPUSTATE_STOPPING;
        signal_condition (&sysblk.intcond);
    }

    /* If this CPU is the last to enter, then signal all
       waiting CPU's that the synchronization is complete
       else wait for the synchronization to compete */
    if (--sysblk.brdcstncpu == 0)
        signal_condition (&sysblk.brdcstcond);
    else
        wait_condition (&sysblk.brdcstcond, &sysblk.intlock);

    /* Purge ALB if requested */
    if (sysblk.brdcstpalb != regs->brdcstpalb)
    {
        purge_alb(regs);
        regs->brdcstpalb = sysblk.brdcstpalb;
    }

    /* Purge TLB if requested */
    if (sysblk.brdcstptlb != regs->brdcstptlb)
    {
        purge_tlb(regs);
        regs->brdcstptlb = sysblk.brdcstptlb;
    }

    /* release intlock */
    if(type != NULL)
        release_lock(&sysblk.intlock);

} /* end function synchronize_broadcast */
#endif /*MAX_CPU_ENGINES > 1*/
