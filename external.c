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
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Load external interrupt new PSW                                   */
/*-------------------------------------------------------------------*/
static void external_interrupt (int code, REGS *regs)
{
PSA    *psa;
int     rc;

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
                regs->cpuint = regs->itimer_pending
                                || regs->extcall
                                || regs->storstat;
                return;
            }
        } /* end for(cpuad) */

        logmsg ("External interrupt: Emergency Signal from CPU %d\n",
                cpuad);

        /* Reset the indicator for the CPU which was found */
        regs->emercpu[cpuad] = 0;

        /* Store originating CPU address at PSA+X'84' */
        psa = (PSA*)(sysblk.mainstor + regs->pxr);
        psa->extcpad[0] = cpuad >> 8;
        psa->extcpad[1] = cpuad & 0xFF;

        /* Reset emergency signal pending flag if there are
           no other CPUs which generated emergency signal */
        regs->emersig = 0;
        regs->cpuint = regs->itimer_pending
                        || regs->extcall
                        || regs->storstat;
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
        regs->cpuint = regs->itimer_pending
                        || regs->emersig
                        || regs->storstat;

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
        external_interrupt (EXT_INTERVAL_TIMER_INTERRUPT, regs);
        regs->itimer_pending = 0;
        regs->cpuint = regs->extcall
                        || regs->emersig
                        || regs->storstat;

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
#ifdef MIPS_COUNTING
int     msecctr = 0;                    /* Millisecond counter       */
#endif /*MIPS_COUNTING*/
int     cpu;                            /* CPU engine number         */
REGS   *regs;                           /* -> CPU register context   */
int     intflag = 0;                    /* 1=Interrupt possible      */
#ifdef TODCLOCK_DRAG_FACTOR
U64     init;                           /* Initial TOD value         */
#endif /*TODCLOCK_DRAG_FACTOR*/
U64     prev;                           /* Previous TOD clock value  */
U64     diff;                           /* Difference between new and
                                           previous TOD clock values */
U64     dreg;                           /* Double register work area */
struct  timeval tv;                     /* Structure for gettimeofday
                                           and select function calls */
#define CLOCK_RESOLUTION        10      /* TOD clock resolution in
                                           milliseconds              */

    /* Display thread started message on control panel */
    logmsg ("HHC610I Timer thread started: tid=%8.8lX, pid=%d\n",
            thread_id(), getpid());

#ifdef TODCLOCK_DRAG_FACTOR
    /* Get current time */
    gettimeofday (&tv, NULL);

    /* Load number of seconds since 00:00:00 01 Jan 1970 */
    init = (U64)tv.tv_sec;

    /* Convert to microseconds */
    init = init * 1000000 + tv.tv_usec;
#endif /*TODCLOCK_DRAG_FACTOR*/

    while (1)
    {
        /* Get current time */
        gettimeofday (&tv, NULL);

        /* Load number of seconds since 00:00:00 01 Jan 1970 */
        dreg = (U64)tv.tv_sec;

        /* Convert to microseconds */
        dreg = dreg * 1000000 + tv.tv_usec;

#ifdef TODCLOCK_DRAG_FACTOR
        if (sysblk.toddrag > 1)
            dreg = init + (dreg - init) / sysblk.toddrag;
#endif /*TODCLOCK_DRAG_FACTOR*/

        /* Obtain the TOD clock update lock */
        obtain_lock (&sysblk.todlock);

        /* Add number of microseconds from TOD base to 1970 */
        dreg += sysblk.todoffset;

        /* Shift left 4 bits so that bits 0-7=TOD Clock Epoch,
           bits 8-59=TOD Clock bits 0-51, bits 60-63=zero */
        dreg <<= 4;

        /* Calculate the difference between the new TOD clock
           value and the previous value, if the clock is set */
        prev = sysblk.todclk;
        diff = (prev == 0 ? 0 : dreg - prev);

        /* Update the TOD clock */
        sysblk.todclk = dreg;

        /* Reset the TOD clock uniqueness value */
        sysblk.toduniq = 0;

        /* Release the TOD clock update lock */
        release_lock (&sysblk.todlock);

        /* Shift the epoch out of the difference for the CPU timer */
        diff <<= 8;

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
            {
                intflag = 1;
                obtain_lock(&sysblk.intlock);
                regs->cpuint = 1;
                release_lock(&sysblk.intlock);
            }

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
            if (itimer < 0 && olditimer >= 0)
            {
                intflag = 1;
                obtain_lock(&sysblk.intlock);
                regs->cpuint = regs->itimer_pending = 1;
                release_lock(&sysblk.intlock);
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

#ifdef MIPS_COUNTING
        /* Calculate MIPS rate */
        msecctr += CLOCK_RESOLUTION;
        if (msecctr > 999)
        {
            for (cpu = 0; cpu < sysblk.numcpu; cpu++)
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

        /* Sleep for CLOCK_RESOLUTION milliseconds */
        tv.tv_sec = CLOCK_RESOLUTION / 1000;
        tv.tv_usec = (CLOCK_RESOLUTION * 1000) % 1000000;
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

/*-------------------------------------------------------------------*/
/* Signal processor                                                  */
/* Input:                                                            */
/*      r1      Register number for status and parameter operand     */
/*      r3      Register number for target CPU address operand       */
/*      eaddr   Effective address operand of SIGP instruction        */
/*      regs    Register context of CPU executing SIGP instruction   */
/* Output:                                                           */
/*      Return value is the condition code for the SIGP instruction. */
/*-------------------------------------------------------------------*/
int signal_processor (int r1, int r3, U32 eaddr, REGS *regs)
{
REGS   *tregs;                          /* -> Target CPU registers   */
U32     parm;                           /* Signal parameter          */
U32     status = 0;                     /* Signal status             */
U32     abs;                            /* Absolute address          */
U16     cpad;                           /* Target CPU address        */
BYTE    order;                          /* SIGP order code           */

    /* Load the target CPU address from R3 bits 16-31 */
    cpad = regs->gpr[r3] & 0xFFFF;

    /* Load the order code from operand address bits 24-31 */
    order = eaddr & 0xFF;

    /* Load the parameter from R1 (if R1 odd), or R1+1 (if even) */
    parm = (r1 & 1) ? regs->gpr[r1] : regs->gpr[r1+1];

    /* Return condition code 3 if target CPU does not exist */
    if (cpad >= sysblk.numcpu)
        return 3;

//  /*debug*/logmsg("SIGP CPU %4.4X ORDER %2.2X PARM %8.8X\n",
//  /*debug*/       cpad, order, parm);

    /* [4.9.2.1] Claim the use of the CPU signaling and response
       facility, and return condition code 2 if the facility is
       busy.  The sigpbusy bit is set while the facility is in
       use by any CPU.  The sigplock must be held while testing
       or changing the value of the sigpbusy bit. */
    obtain_lock (&sysblk.sigplock);
    if (sysblk.sigpbusy)
    {
        release_lock (&sysblk.sigplock);
        return 2;
    }
    sysblk.sigpbusy = 1;
    release_lock (&sysblk.sigplock);

    /* Point to CPU register context for the target CPU */
    tregs = sysblk.regs + cpad;

    /* Except for the reset orders, return condition code 2 if the
       target CPU is executing a previous start, stop, restart,
       stop and store status, set prefix, or store status order */
    if ((order != SIGP_RESET && order != SIGP_INITRESET)
        && (tregs->cpustate == CPUSTATE_STOPPING
            || tregs->restart))
    {
        sysblk.sigpbusy = 0;
        return 2;
    }

    /* Obtain the interrupt lock */
    obtain_lock (&sysblk.intlock);

    /* Process signal according to order code */
    switch (order)
    {
    case SIGP_SENSE:
        /* Set status bit 24 if external call interrupt pending */
        if (tregs->extcall)
            status |= SIGP_STATUS_EXTERNAL_CALL_PENDING;

        /* Set status bit 25 if target CPU is stopped */
        if (tregs->cpustate == CPUSTATE_STOPPED)
            status |= SIGP_STATUS_STOPPED;

        break;

    case SIGP_EXTCALL:
        /* Exit with status bit 24 set if a previous external
           call interrupt is still pending in the target CPU */
        if (tregs->extcall)
        {
            status |= SIGP_STATUS_EXTERNAL_CALL_PENDING;
            break;
        }

        /* Raise an external call interrupt pending condition */
        tregs->cpuint = tregs->extcall = 1;
        tregs->extccpu = regs->cpuad;

        break;

    case SIGP_EMERGENCY:
        /* Raise an emergency signal interrupt pending condition */
        tregs->cpuint = tregs->emersig = 1;
        tregs->emercpu[regs->cpuad] = 1;

        break;

    case SIGP_START:
        /* Restart the target CPU if it is in the stopped state */
        tregs->cpustate = CPUSTATE_STARTED;

        break;

    case SIGP_STOP:
        /* Put the the target CPU into the stopping state */
        tregs->cpustate = CPUSTATE_STOPPING;

        break;

    case SIGP_RESTART:
        /* Make restart interrupt pending in the target CPU */
        tregs->restart = 1;

        break;

    case SIGP_STOPSTORE:
        /* Indicate store status is required when stopped */
        tregs->storstat = 1;

        /* Put the the target CPU into the stopping state */
        tregs->cpustate = CPUSTATE_STOPPING;

        break;

    case SIGP_INITRESET:
        /* Perform initial CPU reset function */
        initial_cpu_reset (tregs);

        break;

    case SIGP_RESET:
        /* Perform CPU reset function */
        cpu_reset (tregs);

        break;

    case SIGP_SETPREFIX:
        /* Exit with status bit 22 set if CPU is not stopped */
        if (tregs->cpustate != CPUSTATE_STOPPED)
        {
            status |= SIGP_STATUS_INCORRECT_STATE;
            break;
        }

        /* Obtain new prefix from parameter register bits 1-19 */
        abs = parm & 0x7FFFF000;

        /* Exit with status bit 23 set if new prefix is invalid */
        if (abs >= sysblk.mainsize)
        {
            status |= SIGP_STATUS_INVALID_PARAMETER;
            break;
        }

        /* Load new value into prefix register of target CPU */
        tregs->pxr = abs;

        /* Invalidate the ALB and TLB of the target CPU */
        purge_alb (tregs);
        purge_tlb (tregs);

        /* Perform serialization and checkpoint-sync on target CPU */
//      perform_serialization (tregs);
//      perform_chkpt_sync (tregs);

        break;

    case SIGP_STORE:
        /* Exit with status bit 22 set if CPU is not stopped */
        if (tregs->cpustate != CPUSTATE_STOPPED)
        {
            status |= SIGP_STATUS_INCORRECT_STATE;
            break;
        }

        /* Obtain status address from parameter register bits 1-22 */
        abs = parm & 0x7FFFFE00;

        /* Exit with status bit 23 set if status address invalid */
        if (abs >= sysblk.mainsize)
        {
            status |= SIGP_STATUS_INVALID_PARAMETER;
            break;
        }

        /* Store status at specified main storage address */
        store_status (tregs, abs);

        /* Perform serialization and checkpoint-sync on target CPU */
//      perform_serialization (tregs);
//      perform_chkpt_sync (tregs);

        break;

    default:
        status = SIGP_STATUS_INVALID_ORDER;
    } /* end switch(order) */

    /* Release the use of the signalling and response facility */
    sysblk.sigpbusy = 0;

    /* Wake up any CPUs waiting for an interrupt or start */
    signal_condition (&sysblk.intcond);

    /* Release the interrupt lock */
    release_lock (&sysblk.intlock);

    /* If status is non-zero, load the status word into
       the R1 register and return condition code 1 */
    if (status != 0)
    {
        regs->gpr[r1] = status;
        return 1;
    }

    /* Return condition code zero */
    return 0;

} /* end function signal_processor */

#if MAX_CPU_ENGINES > 1
/*-------------------------------------------------------------------*/
/* Issue Broadcast Request                                           */
/* Input:                                                            */
/*      type    A pointer to the request counter in the sysblk for   */
/*              the requested function (brdcstptlb or brdcstpalb).   */
/*                                                                   */
/* Signals all other CPU's to perform a requested function           */
/* synchronously, such as purging the ALB and TLB buffers.  In       */
/* theory the CPU issuing the broadcast request should wait until    */
/* all other CPU's have performed the requested action.  In this     */
/* implementation, the issuing CPU will continue, and all other      */
/* CPU's will finish the currently executing instruction before      */
/* performing the requsted action.  This mode of operation is        */
/* probably within tolerance limits. *JJ*                            */
/*-------------------------------------------------------------------*/
void issue_broadcast_request (U64 *type)
{
    /* Obtain the interrupt lock */
    obtain_lock(&sysblk.intlock);

    /* Increment the broadcast request counter */
    sysblk.broadcast++;

    /* Increment the counter for the specified function */
    (*type)++;

    /* Release the interrupt lock */
    release_lock(&sysblk.intlock);

} /* end function issue_broadcast request */

/*-------------------------------------------------------------------*/
/* Perform Broadcast Request                                         */
/*                                                                   */
/* Note: The caller MUST hold the interrupt lock (sysblk.intlock)    */
/*-------------------------------------------------------------------*/
void perform_broadcast_request (REGS *regs)
{
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

    /* Reset broadcast counter for this CPU */
    regs->broadcast = sysblk.broadcast;

} /* end function perform_broadcast request */
#endif /*MAX_CPU_ENGINES > 1*/
