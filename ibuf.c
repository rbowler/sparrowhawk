/* IBUF.C       (c) Copyright Juergen Dobrinski, 1994-2000                */
/*              Instruction prefetch and buffering                        */

#include "hercules.h"

#ifdef IBUF
#include "inline.h"

#include "opcode.h"

#define JEXECUTE_INSTRUCTION(_regs) \
        (((FRAGENTRY*)(_regs->actentry))->code)((((FRAGENTRY*)(_regs->actentry))->inst), 0, (_regs))

#include "ibuf.h"

#define WP_ADDRESS 0x0

#undef DUMMYCOMPILE
#undef DUMMYNOTRANS

/*-------------------------------------------------------------------*/
/* Dummy instruction execition function for perf. measurements       */
/*-------------------------------------------------------------------*/
void zz_dummy (BYTE inst[], int execflag, REGS *regs)
{
//  GET_FRAGENTRY(regs);
  ((FRAGENTRY*)regs->actentry)--;
}

#ifdef LASTINST
/*-------------------------------------------------------------------*/
/* Save last instruction                                             */
/*-------------------------------------------------------------------*/
void save_lastinst (BYTE *lastinst, BYTE *inst)
{
int len;

    if (inst[0] < 0x40)
        len = 2;
    else
        if (inst[0] < 0xC0)
            len = 4;
        else
            len = 6;

        memcpy(lastinst, inst, len);
}
#endif
/*-------------------------------------------------------------------*/
/* Reload last instruction                                           */
/*-------------------------------------------------------------------*/
void ibuf_loadinst (REGS *regs)
{
FRAGENTRY *entry;
int len;

    entry = (FRAGENTRY*)regs->actentry;

    if (entry)
    {
        if (entry->inst)
        {
            if (entry->inst[0] < 0x40)
                len = 2;
            else
                if (entry->inst[0] < 0xC0)
                    len = 4;
                else
                    len = 6;

            memcpy(regs->inst, entry->inst, len);
        }
    }
}
  
/*-------------------------------------------------------------------*/
/* Compile fragment                                                  */
/*-------------------------------------------------------------------*/
void ibuf_compile_frag (REGS *regs, U32 ia)
{
U32  abs;                            /* Absolute storage address  */
U32  minabs;
U32  minia;
U32  off;
BYTE akey;                           /* Bits 0-3=key, 4-7=zeroes  */
zz_func code;
FRAGENTRY *entry;
BYTE *startptr;
FRAG *frag;
#ifndef DUMMYCOMPILE
U16  i;
U32  maxabs;
BYTE opcode;
int eoc;
#endif

    debugmsg("ibuf_compile %2x\n", ia);

#ifndef DUMMYCOMPILE


    akey = regs->psw.pkey;

    regs->instvalid = 0;
    /* Program check if instruction address is odd */
    if (ia & 0x01)
        program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);

    abs = logical_to_abs (ia, 0, regs, ACCTYPE_INSTFETCH, akey);
    regs->instvalid = 1;

    regs->actfrag = FRAG_BUFFERADDRESS(regs, abs);
    frag = regs->actfrag;

    memset(&frag->dict, 0, FRAG_BYTESIZE *sizeof(U32));

    regs->ibufrecompile++;

    frag->firstia = ia;
    frag->valid = 1;

    minia  = (ia & FRAG_BYTEMASK);
    minabs = (abs & FRAG_BYTEMASK);
    maxabs = minabs + FRAG_BYTESIZE;
    off = abs - minabs;

    startptr = sysblk.mainstor+minabs;

    code = NULL;
    i = 0;
    eoc = 0;
    entry = frag->entry;

    while ((off < FRAG_BYTESIZE) && !eoc)
    {
        opcode = (startptr+off)[0];

        code = opcode_table[opcode];

        entry->code = code;
        entry->inst = startptr+off;
        entry->ia = minia + off;
        entry->valid = 1;
#ifdef CHECK_FRAGPARMS
        entry->iaabs = minabs + off;
#endif

        frag->dict[off] = i;

        if (code == &operation_exception)
        {
            eoc = 1;
            entry->code = NULL;
#ifdef CHECK_FRAGPARMS
            memcpy(entry->oinst, entry->inst, 1);
#endif
        }
        else
        {
            if (opcode < 0x40)
            {    
                IBUF_RR(entry->inst, entry->raddr.r1, entry->raddr.r2);
                off += 2;
#ifdef CHECK_FRAGPARMS
                memcpy(entry->oinst, entry->inst, 2);
#endif
            }
            else
           {
                if (opcode < 0x80 || opcode == 0xB1)
                {
                    IBUF_RX(entry->inst, entry->raddr.r1, entry->raddr.r2,
                                entry->raddr.r3, entry->raddr.addr);
                    off += 4;
#ifdef CHECK_FRAGPARMS
                    memcpy(entry->oinst, entry->inst, 4);
#endif
                }
                else
                    if (opcode < 0xC0)
                    {
                        off += 4;
#ifdef CHECK_FRAGPARMS
                        memcpy(entry->oinst, entry->inst, 4);
#endif
                    }
                    else
                    {
                         off += 6;
#ifdef CHECK_FRAGPARMS
                        memcpy(entry->oinst, entry->inst, 6);
#endif
                    }
            }
        }
        if (off >= FRAG_BYTESIZE)
        {
            entry->code = NULL;
        }
        entry++;
        i++;
    }

#else
    akey = regs->psw.pkey;

    regs->instvalid = 0;
    /* Program check if instruction address is odd */
#ifndef DUMMYNOTRANS
    if (ia & 0x01)
        program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);

    abs = logical_to_abs (ia, 0, regs, ACCTYPE_INSTFETCH, akey);
#else
    abs = 0;
#endif
    regs->instvalid = 1;

    regs->actfrag = FRAG_BUFFERADDRESS(regs, abs);
    frag = regs->actfrag;

    memset(&frag->dict, 0, FRAG_BYTESIZE *sizeof(U32));

    frag->firstia = ia;
    frag->valid = 1;

    minia  = (ia & FRAG_BYTEMASK);
    minabs = (abs & FRAG_BYTEMASK);
    off = abs - minabs;

    startptr = sysblk.mainstor+minabs;

    code = NULL;
    entry = frag->entry;

    code = NULL;

    entry->code = code;
    entry->inst = startptr+off;
    entry->ia = minia + off;
    entry->valid = 1;
#ifdef CHECK_FRAGPARMS
    entry->iaabs = minabs + off;
#endif
#endif

    regs->actentry = &frag->entry[0];
    debugmsg("ibuf_compile end\n");
}

/*-------------------------------------------------------------------*/
/* CPU instruction execution thread                                  */
/*-------------------------------------------------------------------*/
void *cpu_thread (REGS *pregs)
{
int     stepthis = 0;                   /* Stop on this instruction  */
int     diswait = 0;                    /* 1=Disabled wait state     */
int     shouldbreak;                    /* 1=Stop at breakpoint      */
#define CPU_PRIORITY    15              /* CPU thread priority       */
REGS *regs = pregs;

#ifdef FEATURE_WATCHPOINT
    regs->watchpoint = WP_ADDRESS;
#endif

#if 0
    {
    int i;

    for (i=0; i < 256; i++)
      opcode_table[i] = &zz_dummy;
    }
#endif
    /* Display thread started message on control panel */
    logmsg ("HHC620I CPU%4.4X thread started: tid=%8.8lX, pid=%d, "
            "priority=%d\n",
            regs->cpuad, thread_id(), getpid(),
            getpriority(PRIO_PROCESS,0));

    /* Set CPU thread priority */
    if (setpriority(PRIO_PROCESS, 0, CPU_PRIORITY))
        logmsg ("HHC621I CPU thread set priority failed: %s\n",
                strerror(errno));
  
    logmsg ("HHC622I CPU%4.4X priority adjusted to %d\n",
            regs->cpuad, getpriority(PRIO_PROCESS,0));
  

    /* Add this CPU to the configuration. Also ajust  
       the number of CPU's to perform synchronisation as the 
       synchonization process relies on the number of CPU's
       in the configuration to accurate */
    obtain_lock(&sysblk.intlock);
    if(regs->cpustate != CPUSTATE_STARTING)
    {
        logmsg("HHC623I CPU%4.4X thread already started\n",
            regs->cpuad);
        release_lock(&sysblk.intlock);
        return NULL;
    }
    sysblk.numcpu++;
#if MAX_CPU_ENGINES > 1
    if (sysblk.brdcstncpu != 0)
        sysblk.brdcstncpu++;
#endif /*MAX_CPU_ENGINES > 1*/

    /* Perform initial cpu reset */
    initial_cpu_reset(regs);
    
    if (!regs->fragbuffer)
    {
        regs->fragbuffer = calloc(FRAG_BUFFER, sizeof(FRAG));
    }
    if (!regs->fragbuffer)
      return(NULL);

    regs->actfrag = regs->fragbuffer;

    /* release the intlock */
    release_lock(&sysblk.intlock);

    /* Establish longjmp destination for program check */
    setjmp(regs->progjmp);

    debugmsg("bef get_fragment\n");
    ibuf_assign_fragment(regs, regs->psw.ia);

    /* Clear the disabled wait state flag */
    diswait = 0;

    /* Execute the program */


    debugmsg("bef mainloop\n");
    while (1)
    {
        CHECK_FRAGPTR(regs, "beg while");
#if MAX_CPU_ENGINES > 1
        if (regs->doint || (sysblk.brdcstncpu != 0 ))
#else
        if (regs->doint )
#endif
        {
            regs->int1count++;

            /* Obtain the interrupt lock */
            obtain_lock (&sysblk.intlock);

#if MAX_CPU_ENGINES > 1
            /* Perform broadcasted purge of ALB and TLB if requested
               synchronize_broadcast() must be called until there are
               no more broadcast pending because synchronize_broadcast()
               releases and reacquires the mainlock. */
            while (sysblk.brdcstncpu != 0)
                synchronize_broadcast(regs, NULL);
#endif /*MAX_CPU_ENGINES > 1*/

            /* Take interrupts if CPU is not stopped */
            if (regs->cpustate == CPUSTATE_STARTED)
            {
                /* If a machine check is pending and we are enabled for
                   machine checks then take the interrupt */
                if (sysblk.mckpending && regs->psw.mach) 
                {
                    PERFORM_SERIALIZATION (regs);
                    PERFORM_CHKPT_SYNC (regs);
                    perform_mck_interrupt (regs);
                }

                /* If enabled for external interrupts, invite the
                   service processor to present a pending interrupt */
                if (regs->psw.sysmask & PSW_EXTMASK)
                {
                    PERFORM_SERIALIZATION (regs);
                    PERFORM_CHKPT_SYNC (regs);
                    perform_external_interrupt (regs);
                }

                /* If an I/O interrupt is pending, and this CPU is
                   enabled for I/O interrupts, invite the channel
                   subsystem to present a pending interrupt */
                if (sysblk.iopending &&
#ifdef FEATURE_BCMODE
                    (regs->psw.sysmask &
                        (regs->psw.ecmode ? PSW_IOMASK : 0xFE))
#else /*!FEATURE_BCMODE*/
                    (regs->psw.sysmask & PSW_IOMASK)
#endif /*!FEATURE_BCMODE*/
                   )
                {
                    PERFORM_SERIALIZATION (regs);
                    PERFORM_CHKPT_SYNC (regs);
                    perform_io_interrupt (regs);
                }

            } /*if(cpustate == CPU_STARTED)*/

            /* If CPU is stopping, change status to stopped */
            if (regs->cpustate == CPUSTATE_STOPPING)
            {
                /* Change CPU status to stopped */
                regs->cpustate = CPUSTATE_STOPPED;

                if (!regs->cpuonline)
                {
                    /* Remove this CPU from the configuration. Only do this
                       when no synchronization is in progress as the 
                       synchonization process relies on the number of CPU's
                       in the configuration to accurate. The first thing
                       we do during interrupt processing is synchronize
                       the broadcast functions so we are safe to manipulate
                       the number of CPU's in the configuration.  */

                    sysblk.numcpu--;

                    /* Clear all regs */
                    initial_cpu_reset(regs);

                    /* Display thread ended message on control panel */
                    logmsg ("HHC624I CPU%4.4X thread ended: tid=%8.8lX, pid=%d\n",
                            regs->cpuad, thread_id(), getpid());

                    release_lock(&sysblk.intlock);

                    /* Thread exit */
                    regs->cputid = 0;
                    return NULL;
                }

                /* If initial CPU reset pending then perform reset */
                if (regs->sigpireset)
                {
                    PERFORM_SERIALIZATION (regs);
                    PERFORM_CHKPT_SYNC (regs);
                    initial_cpu_reset(regs);
                }

                /* If a CPU reset is pending then perform the reset */
                if (regs->sigpreset)
                {
                    PERFORM_SERIALIZATION (regs);
                    PERFORM_CHKPT_SYNC (regs);
                    cpu_reset(regs);
                }

                /* Store status at absolute location 0 if requested */
                if (regs->storstat)
                {
                    regs->storstat = 0;
                    store_status (regs, 0);
                }
            } /* end if(cpustate == STOPPING) */

            /* Perform restart interrupt if pending */
            if (regs->restart)
            {
                PERFORM_SERIALIZATION (regs);
                PERFORM_CHKPT_SYNC (regs);
                regs->restart = 0;
                restart_interrupt (regs);
            } /* end if(restart) */

            /* This is where a stopped CPU will wait */
            if (regs->cpustate == CPUSTATE_STOPPED)
            {
                /* Wait until there is work to do */
                while (regs->cpustate == CPUSTATE_STOPPED)
                    wait_condition (&sysblk.intcond, &sysblk.intlock);
                release_lock (&sysblk.intlock);
                continue;
            } /* end if(cpustate == STOPPED) */

            /* Test for wait state */
            if (regs->psw.wait)
            {
                /* Test for disabled wait PSW and issue message */
                if ( diswait == 0 &&
#ifdef FEATURE_BCMODE
                (regs->psw.sysmask &
                    (regs->psw.ecmode ?
                        (PSW_IOMASK | PSW_EXTMASK) : 0xFF)) == 0)
#else /*!FEATURE_BCMODE*/
                (regs->psw.sysmask & (PSW_IOMASK | PSW_EXTMASK)) == 0)
#endif /*!FEATURE_BCMODE*/
                {
                    logmsg ("CPU%4.4X: Disabled wait state code %8.8X\n",
                            regs->cpuad,
                            regs->psw.ia | (regs->psw.amode << 31));
#ifdef INSTRUCTION_COUNTING
                    logmsg ("%llu instructions executed\n",
                            regs->instcount);
#endif /*INSTRUCTION_COUNTING*/
                    diswait = 1;
                }

                regs->int3count++;
                /* Wait for I/O, external or restart interrupt */
                wait_condition (&sysblk.intcond, &sysblk.intlock);
                release_lock (&sysblk.intlock);
                continue;
            } /* end if(wait) */

            /* Reset disabled wait state indicator */
            diswait = 0;

            /* Release the interrupt lock */
            release_lock (&sysblk.intlock);
            ibuf_assign_fragment(regs, regs->psw.ia);

        } /* end if(interrupt) */

        CHECK_FRAGPTR(regs, " aft interrupt");

        if (!((FRAG*)regs->actfrag)->valid) /* Compiled fragment not valid (any more); recompile */
        {
            regs->ibufcodechange++;
            ibuf_compile_frag (regs, regs->psw.ia);
        }

#ifdef CHECK_FRAGADDRESS
        if ((regs->psw.ia != ((FRAGENTRY*)regs->actentry)->ia) &&  
            ((FRAGENTRY*)(regs->actentry))->code)
        {
            logmsg("WRONG FRAGENTRY %llu %4x %4x %x %x %4x\n", 
                   regs->instcount,
                   regs->psw.ia, 
                   ((FRAGENTRY*)regs->actentry)->ia,
                   regs->lastinst[0],
                   regs->lastinst[1],
                   (U32)((FRAGENTRY*)(regs->actentry))->code);
            ibuf_get_fragentry (regs, regs->psw.ia);
        }
#ifndef DUMMYNOTRANS
        if (logical_to_abs(regs->psw.ia, 0, regs, ACCTYPE_INSTFETCH, 
               regs->psw.pkey) != ((FRAGENTRY*)regs->actentry)->iaabs)
        {
            logmsg("WRONG TRANSLATED FRAGENTRY %llu %4x %4x %x %x %4x\n", 
                   regs->instcount,
                   regs->psw.ia, 
                   ((FRAGENTRY*)regs->actentry)->ia,
                   regs->lastinst[0],
                   regs->lastinst[1],
                   (U32)((FRAGENTRY*)(regs->actentry))->code);
            ibuf_assign_fragment (regs, regs->psw.ia);
            
         }
#endif
#endif
#ifdef CHECK_FRAGPARMS
        if (((FRAGENTRY*)(regs->actentry))->code)
        {
            FRAGENTRY *entry;
            int len = 0;

            entry = (FRAGENTRY*)regs->actentry;
            if (entry->inst[0] < 0x40)
            {
                len = 2;
            }
            else
                if (entry->inst[0] < 0xC0)
                    len = 4;
                else
                    len = 6;

            if (memcmp(entry->inst, entry->oinst, len))
            {
                logmsg("ERROR COMPILE XX %llu %4x %4x %4x op %x %x decoded: %x %x\n", 
                       regs->instcount, 
                       entry->iaabs, 
                       regs->psw.ia, 
                       entry->ia, 
                       entry->inst[0],
                       entry->inst[1], 
                       entry->oinst[0],
                       entry->oinst[1]);
                ibuf_compile_frag(regs, regs->psw.ia);
            }
        }
#endif

        CHECK_FRAGPTR(regs, " bef doinst");

        regs->instcount++;
        if (sysblk.doinst)
        {
           /* Test for breakpoint */
           shouldbreak = sysblk.instbreak
                          && (regs->psw.ia == sysblk.breakaddr);
           if (sysblk.insttrace || sysblk.inststep || shouldbreak)

           {
               LOAD_INST(regs);

               display_inst (regs, regs->inst);
               if (sysblk.inststep || stepthis || shouldbreak)
               {
                   /* Put CPU into stopped state */
                   regs->cpustate = CPUSTATE_STOPPED;

                   /* Wait for start command from panel */
                   obtain_lock (&sysblk.intlock);
                   while (regs->cpustate == CPUSTATE_STOPPED)
                       wait_condition (&sysblk.intcond, &sysblk.intlock);
                   release_lock (&sysblk.intlock);
               }
           }
        }
//        debugmsg("beg exec\n");

#ifdef INSTSTAT
        regs->instcountx[((REGS*)(regs->actentry))->inst[0]]++;
#endif

        CHECK_FRAGPTR(regs, " bef exec case");

        /* execute prefetched inst from compiled fragment */
#ifdef DO_INSTFETCH
        if (1)
#else
        if (!((FRAGENTRY*)(regs->actentry))->code)
#endif
        {
            regs->instvalid = 0;

            /* Fetch the next sequential instruction */
            instfetch (regs->inst, regs->psw.ia, regs);

            /* Set the instruction validity flag */
            regs->instvalid = 1;

            CHECK_FRAGPTR(regs, " bef EXECUTE ");
            regs->actentry = NULL;
#ifdef LASTINST
            save_lastinst(regs->lastinst, regs->inst);
#endif
            EXECUTE_INSTRUCTION (regs->inst, 0, regs);
#ifdef NO_REASSIGN
            ibuf_assign_fragment (regs, regs->psw.ia);
            ((FRAGENTRY*)regs->actentry)--;
#else
            GET_FRAGENTRY(regs);
#endif

        }
        else
        {
            debugmsg("BEF EXECUTE %llu %4x %4x\n", 
                   regs->instcount,
                   regs->psw.ia, 
                   ((FRAGENTRY*)regs->actentry)->ia);
#ifdef LASTINST
            save_lastinst(regs->lastinst, 
                          ((FRAGENTRY*)(regs->actentry))->inst);
#endif
            CHECK_FRAGPTR(regs, " bef JEXECUTE ");
            JEXECUTE_INSTRUCTION (regs);
#ifdef NO_REASSIGN
            ibuf_assign_fragment (regs, regs->psw.ia);
            ((FRAGENTRY*)regs->actentry)--;
#endif
            debugmsg("AFT EXECUTE %llu %4x %4x\n", 
                   regs->instcount,
                   regs->psw.ia, 
                   ((FRAGENTRY*)regs->actentry)->ia);
        }
        /* guessing next instruction, might be wrong in case of branch or psw change */
#ifdef FEATURE_WATCHPOINT 
        {
           BYTE *ptr;

           ptr = (sysblk.mainstor+regs->watchpoint);
           if (*ptr != regs->oldvalue)
           {
              LOAD_INST(regs);
              logmsg("watchpoint %4x %llu %4x %x %x op ", 
                      regs->watchpoint,
                      regs->instcount, regs->psw.ia,
                      regs->oldvalue, *ptr);
              logmsg("%x %x %x\n", 
                      regs->lastinst[0],
                      regs->lastinst[1],
                      regs->lastinst[2]);
              regs->oldvalue = *ptr;
           }
        }
#endif
        CHECK_FRAGPTR(regs, " bef increment ");
        {
        ((FRAGENTRY*)regs->actentry)++;

        debugmsg("AFT INCREMENT %llu %4x %4x\n", 
                   regs->instcount,
                   regs->psw.ia, 
                   ((FRAGENTRY*)regs->actentry)->ia);
        }
    }
    return NULL;

} /* end function cpu_thread */

#endif
