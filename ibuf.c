/* IBUF.C       (c) Copyright Juergen Dobrinski, 1994-2000                */
/*              Instruction prefetch and buffering                        */

#include "hercules.h"

#ifdef IBUF
#include "inline.h"

#include "opcode.h"

#ifdef FOOTPRINT_BUFFER
#define JEXECUTE_INSTRUCTION(_regs) \
        { \
            FRAGENTRY *entry = _regs->actentry; \
            int len; \
            if (entry->inst[0] < 0x40) \
                len = 2; \
            else \
                if (entry->inst[0] < 0xC0) \
                    len = 4; \
                else \
                    len = 6; \
            sysblk.footprregs[(_regs)->cpuad][sysblk.footprptr[(_regs)->cpuad]] = *(_regs); \
            memcpy(&sysblk.footprregs[(_regs)->cpuad][sysblk.footprptr[(_regs)->cpuad]++].inst, entry->inst, len); \
            sysblk.footprptr[(_regs)->cpuad] &= FOOTPRINT_BUFFER - 1; \
            (entry->code)(entry->inst, 0, (_regs)); \
        }
#else
#define JEXECUTE_INSTRUCTION(_regs) \
{ \
        (((FRAGENTRY*)(_regs->actentry))->code)((((FRAGENTRY*)(_regs->actentry))->inst), 0, (_regs)); \
}
#endif

#include "ibuf.h"


#define WP_ADDRESS 0x0

#undef DO_EXECUTE

#define DELAY \
        { \
        U32 i; \
        for (i=0;i < 100; i++); \
        }

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
/* Invalidate Fragment                                               */
/*-------------------------------------------------------------------*/
void ibuf_invalidate (U32 abs, U32 len)
{
REGS *regs;
int j; 
int j0;
int jn;
#ifdef IBUF_STAT
BYTE invalid = 0;
#endif
#if MAX_CPU_ENGINES > 1
int i;
#endif

#ifdef IBUF_STAT
    invalid = 1;
#endif

#if MAX_CPU_ENGINES > 1
    for (i=0; i < MAX_CPU_ENGINES; i++)
    {
        regs = &sysblk.regs[i];
        if (regs->fragvalid)
        {
#else
            regs = &sysblk.regs[0];
#endif
#ifdef IBUF_STAT
            regs->ibufinvalidate++;
#endif
            regs->icount[(abs >> FRAG_ADDRESSLENGTH) & (FRAG_BUFFER - 1)]
                          = 0;
            if ((abs & (FRAG_BUFFERMASK)) == ((abs + len) & (FRAG_BUFFERMASK)))
            {
                   regs->fragvalid[(abs >> FRAG_ADDRESSLENGTH) & 
                                   (FRAG_BUFFER - 1)] = 0;
#ifdef IBUF_STAT
                   regs->fraginvalid[(abs >> FRAG_ADDRESSLENGTH) & 
                                   (FRAG_BUFFER - 1)] = invalid;
#endif
            }
            else 
            { 
                j0 = (abs & (FRAG_BUFFERMASK)) >> FRAG_ADDRESSLENGTH;
                jn = ((abs + len) & (FRAG_BUFFERMASK)) >> FRAG_ADDRESSLENGTH;
                if (j0 < jn)
                { 
                    for (j=j0; j <= jn; j++)
                    {
                        regs->fragvalid[j] = 0;
#ifdef IBUF_STAT
                        regs->fraginvalid[j] = invalid;
#endif
                    }
                } 
                else
                { 
                    for (j=j0; j < FRAG_BUFFER; j++)
                    {
                        regs->fragvalid[j] = 0;
#ifdef IBUF_STAT
                        regs->fraginvalid[j] = invalid;
#endif
                    }
                    for (j=0; j < jn; j++)
                    {
                        regs->fragvalid[j] = 0;
#ifdef IBUF_STAT
                        regs->fraginvalid[j] = invalid;
#endif
                    }
                } 
            }
#if MAX_CPU_ENGINES > 1
        }
    }
#endif
}
#ifndef INLINE_INVALIDATE
/*-------------------------------------------------------------------*/
/* Invalidate Fragment                                               */
/*-------------------------------------------------------------------*/
void ibuf_fastinvalidate (U32 abs, U32 len)
{
REGS *regs;
#if MAX_CPU_ENGINES > 1
int i;
#endif

#ifdef IBUF_STAT
BYTE invalid = 0;
    invalid = 1;
#endif

    if ((abs & (FRAG_BYTESIZE - 1)) < (FRAG_BYTESIZE - 8))
    {
#if MAX_CPU_ENGINES > 1
        for (i=0; i < MAX_CPU_ENGINES; i++)
        {
            regs = &sysblk.regs[i];
            if (regs->fragvalid)
            {
#else
                regs = &sysblk.regs[0];
#endif
#ifdef IBUF_STAT
                regs->ibufinvalidatex++;
#endif
                regs->icount[(abs >> FRAG_ADDRESSLENGTH) & (FRAG_BUFFER - 1)]
                          = 0;
                regs->fragvalid[(abs >> FRAG_ADDRESSLENGTH) & (FRAG_BUFFER - 1)]
                          = 0;
#ifdef IBUF_STAT
                regs->fraginvalid[(abs >> FRAG_ADDRESSLENGTH) & (FRAG_BUFFER - 1)]
                          = invalid;
#endif
                return;
#if MAX_CPU_ENGINES > 1
            }
        }
#endif
    }
    ibuf_invalidate(abs, len);
}
#endif
/*-------------------------------------------------------------------*/
/* Assign compiled fragment                                          */
/*-------------------------------------------------------------------*/
void ibuf_assign_fragment (REGS *regs, U32 ia)
{
FRAGENTRY *fragentry;
U32  abs;                            /* Absolute storage address  */
BYTE akey;                           /* Bits 0-3=key, 4-7=zeroes  */
#ifdef IBUF_SWITCH
U32 i;
BYTE ibufactive = 0;
FRAG *frag;
#endif

  /* search compiled fragment in buffer  */
  debugmsg("assign fragment\n");
#ifdef IBUF_STAT
  regs->ibufassign++;
#endif

  /* Obtain current access key from PSW */
  akey = regs->psw.pkey;

  /* Program check if instruction address is odd */
  regs->instvalid = 0;
  if (ia & 0x01)
  {
#ifdef CHECK_FRAGADDRESS
      logmsg("SPEC EXCEPTION %s %d\n", regs->file, regs->line);
#endif
      program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);
  }

  abs = logical_to_abs (ia, 0, regs, ACCTYPE_INSTFETCH, akey);
  regs->instvalid = 1;
  regs->iaabs = abs;

#ifdef IBUF_SWITCH
  if (regs->actpage)
     ibufactive = 1;
#endif

  regs->actpage = &regs->dict[abs & 
             (STORAGE_KEY_PAGEMASK & ((FRAG_BUFFER * FRAG_BYTESIZE) - 1))];

  fragentry = regs->dict[abs & ((FRAG_BUFFER * FRAG_BYTESIZE) - 1)];
 
  if (fragentry && abs == fragentry->iaabs)
  {
      if (*fragentry->valid)
      {
          regs->actentry = fragentry;
          regs->iaabs = 0;
#ifdef IBUF_STAT
          regs->ibufexecute++;
#endif
          /* execution mode */
#ifdef IBUF_SWITCH
          if (ibufactive)
              return;
          else
              longjmp(regs->progjmp, 0);
#endif
      }
  }

#ifdef IBUF_SWITCH
  i = (abs >> FRAG_ADDRESSLENGTH) & (FRAG_BUFFER - 1);
  frag = &((FRAG*)regs->fragbuffer)[i];
  if ((abs >> FRAG_ADDRESSLENGTH) != (frag->minabs >> FRAG_ADDRESSLENGTH))
  {
      regs->icount[i]++;
      if (regs->icount[i] < IBUF_ICOUNT)
      {
          debugmsg("count too low 1\n");
          /* interpreter mode */
          regs->actpage = NULL;
#ifdef IBUF_STAT
          regs->ibufinterpret++;
#endif
          if (ibufactive)
              longjmp(regs->progjmp, 0);
          else
              return;
      }
  }
#endif

  /* fragment not valid (invalidated or new */
  debugmsg("ibuf_assign end bef compile\n");
  ibuf_compile_frag (regs, ia);

  /* execution mode */
#ifdef IBUF_STAT
  regs->ibufexecute++;
#endif
  regs->iaabs = 0;
#ifdef IBUF_SWITCH
  if (ibufactive)
      return;
  else
      longjmp(regs->progjmp, 0);
#endif
}

#ifndef INLINE_GET
/*-------------------------------------------------------------------*/
/* Get compiled fragment                                             */
/*-------------------------------------------------------------------*/
void ibuf_get_fragentry (REGS *regs, U32 ia)
{
FRAGENTRY *fragentry;
#ifdef IBUF_SWITCH

  debugmsg("get fragentry\n");
#ifdef IBUF_STAT
  regs->ibufget++;
#endif

  if (!regs->actpage)
  {
      regs->ibufexeassign++;
      ibuf_assign_fragment(regs, ia);
      return;
  }
#endif
  debugmsg("get bef actpage %p\n", regs->actpage);
  fragentry = regs->actpage[ia & STORAGE_KEY_BYTEMASK & 
                            (FRAG_BYTESIZE * FRAG_BUFFER - 1)];

  debugmsg("get aft actpage\n");

  if (fragentry && ia == fragentry->ia)
  {
      regs->actentry = fragentry;
#ifdef IBUF_STAT
      regs->ibufexecute++;
#endif
      return;
  }

  /* fragment not valid (invalidated or new */
  debugmsg("ibuf_get end bef compile\n");
  ibuf_assign_fragment(regs, ia);
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
U32  minabs;
U32  minia;
U32  off;
U32  abs = regs->iaabs;
BYTE akey;                           /* Bits 0-3=key, 4-7=zeroes  */
FRAGENTRY *entry;
FRAGENTRY *startentry;
BYTE *startptr;
FRAG *frag;
U16  i;
BYTE opcode;
int eoc;
BYTE *fragvalid;
BYTE *inst;
U32 iaabs;
void **dict;
BYTE extfrag = 0;
zz_func code;

    debugmsg("ibuf_compile %2x\n", ia);

    if (!abs)
    {
        akey = regs->psw.pkey;

        regs->instvalid = 0;
        /* Program check if instruction address is odd */
        if (ia & 0x01)
        {
#ifdef CHECK_FRAGADDRESS
            logmsg("SPEC EXCEPTION %s %d\n", regs->file, regs->line);
#endif
            program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);
        }

        abs = logical_to_abs (ia, 0, regs, ACCTYPE_INSTFETCH, akey);
        regs->instvalid = 1;
    }


    i = (abs >> FRAG_ADDRESSLENGTH) & (FRAG_BUFFER - 1);
    frag = &((FRAG*)regs->fragbuffer)[i];
    fragvalid = &regs->fragvalid[i];

#ifdef IBUF_SWITCH
    regs->icount[i] = 0;
#endif

#ifdef IBUF_STAT
    if (*fragvalid)
    {
        if ((abs >> FRAG_ADDRESSLENGTH) != (frag->minabs >> FRAG_ADDRESSLENGTH))
            regs->ibufdif++;
        else
        {
            if (abs < frag->minabs)
                regs->ibuflow++;
            else
                regs->ibufhigh++;
        }
    }

    switch (regs->fraginvalid[i])
    {
        case 1:
            regs->ibufrecompilestorage++;
            break;
        case 2:
            regs->ibufrecompiledisk++;
            break;
    }
    regs->fraginvalid[i] = 0;
#endif

#if 1
    if ((*fragvalid) &&
        ((abs >> FRAG_ADDRESSLENGTH) == (frag->minabs >> FRAG_ADDRESSLENGTH)) &&
         abs > frag->minabs)
    {
        if (frag->maxind < (FRAG_SIZE - 16))
            extfrag = 1;
        else
        {
#ifdef IBUF_STAT
            regs->ibufoverflow++;
#endif
        }
    }
#endif
     

    *fragvalid = 1;

    if (!extfrag)
    {
        memset(frag->dict, 0, FRAG_BYTESIZE *sizeof(void*));
        frag->maxind = 0;
#ifdef IBUF_STAT
    regs->ibufrecompile++;
#endif
    }

    minia  = (ia & FRAG_BYTEMASK);
    minabs = (abs & FRAG_BYTEMASK);
    off = abs - minabs;

    if (!extfrag)
        frag->minabs = minabs + off;

    startptr = sysblk.mainstor+minabs;
    inst = startptr+off;
    iaabs = minabs + off;
    ia = minia + off;
    dict = frag->dict + off;

    i = 0;
    eoc = 0;
    entry = &frag->entry[frag->maxind];
    startentry = entry;

    while ((off < FRAG_BYTESIZE) && !eoc)
    {
        opcode = (startptr+off)[0];

        code = opcode_table[opcode];

        entry->valid = fragvalid;

        entry->inst = inst;
        entry->iaabs = iaabs;
        entry->ia = ia;

        *dict = entry;

#if 1
        if (code == &operation_exception)
        {
            eoc = 1;
#ifdef CHECK_FRAGPARMS
            memcpy(entry->oinst, entry->inst, 2);
#endif
            entry->inst = NULL;
        }
        else
#endif
        {
        if (opcode < 0x40)
        {    
#ifdef IBUF_FASTOP
            IBUF_RR(entry->inst, entry->raddr.r1, entry->raddr.r2);
#endif
            off += 2;
            inst += 2;
            iaabs += 2;
            ia += 2;
            dict += 2;
#ifdef CHECK_FRAGPARMS
            memcpy(entry->oinst, entry->inst, 2);
#endif
        }
        else
        {
            if (opcode < 0x80 || opcode == 0xB1)
            {
#ifdef IBUF_FASTOP
                IBUF_RX(entry->inst, entry->raddr.r1, entry->raddr.r2,
                          entry->raddr.r3, entry->raddr.addr);
#endif
                off += 4;
                inst += 4;
                iaabs += 4;
                ia += 4;
                dict += 4;
#ifdef CHECK_FRAGPARMS
                memcpy(entry->oinst, entry->inst, 4);
#endif
            }
            else
                if (opcode < 0xC0)
                {
                    off += 4;
                    inst += 4;
                    iaabs += 4;
                    ia += 4;
                    dict += 4;
#ifdef CHECK_FRAGPARMS
                    memcpy(entry->oinst, entry->inst, 4);
#endif
                }
                else
                {
                    off += 6;
                    inst += 6;
                    iaabs += 6;
                    ia += 6;
                    dict += 6;
#ifdef CHECK_FRAGPARMS
                    memcpy(entry->oinst, entry->inst, 6);
#endif
                }
        }
        }
        frag->maxind++;
        if (off >= FRAG_BYTESIZE || frag->maxind >= FRAG_SIZE)
        {
            entry->inst = NULL;
            eoc = 1;
        }
        entry++;
    }
    regs->actentry = startentry;
    debugmsg("ibuf_compile end %p\n", regs->actentry);
}
/*-------------------------------------------------------------------*/
/* Init IBUF                                                         */
/*-------------------------------------------------------------------*/
void ibuf_init (REGS *regs)
{
void **dict = regs->dict;
FRAG *frag;
U32 i;

    for (i=0; i < FRAG_BUFFER; i++)
    {
        frag = &(((FRAG*)regs->fragbuffer)[i]);
        frag->dict = dict;
        dict = &dict[FRAG_BYTESIZE];
    }

}


/*-------------------------------------------------------------------*/
/* instruction interpreter                                           */
/*-------------------------------------------------------------------*/
void *do_interpret (REGS *pregs, int *diswait)
{
int     stepthis = 0;                   /* Stop on this instruction  */
int     shouldbreak;                    /* 1=Stop at breakpoint      */
#define CPU_PRIORITY    15              /* CPU thread priority       */
REGS *regs = pregs;

    while (1)
    {
        debugmsg("beg loop\n");
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
                if ( *diswait == 0 &&
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
                    *diswait = 1;
                }

                regs->int3count++;
                /* Wait for I/O, external or restart interrupt */
                wait_condition (&sysblk.intcond, &sysblk.intlock);
                release_lock (&sysblk.intlock);
                continue;
            } /* end if(wait) */

            /* Reset disabled wait state indicator */
            *diswait = 0;

            /* Release the interrupt lock */
            release_lock (&sysblk.intlock);
#ifdef CHECK_FRAGADDRESS
            strcpy(regs->file, __FILE__);
            regs->line = __LINE__;
#endif
            ibuf_assign_fragment(regs, regs->psw.ia);

        } /* end if(interrupt) */
#ifdef TRACE_INTERRUPT_DELAY
        else
        {
          if (((sysblk.extpending || regs->cpuint)
              && (regs->psw.sysmask & PSW_EXTMASK))
              || (sysblk.mckpending && regs->psw.mach)
#ifndef FEATURE_BCMODE
              || (sysblk.iopending && (regs->psw.sysmask & PSW_IOMASK))
#else /*FEATURE_BCMODE*/
              ||  (sysblk.iopending &&
                  (regs->psw.sysmask & (regs->psw.ecmode ? PSW_IOMASK : 0xFE)))
#endif /*FEATURE_BCMODE*/
#if MAX_CPU_ENGINES > 1
              || sysblk.brdcstncpu != 0
#endif /*MAX_CPU_ENGINES > 1*/
              || (sysblk.intkey && (regs->cr[0] & CR0_XM_INTKEY))
              || (sysblk.servsig && (regs->cr[0] & CR0_XM_SERVSIG))
              || regs->psw.wait)
          {
            regs->int2count++;
            logmsg("Missing interrupts: %llu ", regs->instcount);
            if ((sysblk.extpending || regs->cpuint)
                 && (regs->psw.sysmask & PSW_EXTMASK))
              logmsg("extpending/cpuint\n");

            if (sysblk.mckpending && regs->psw.mach)
              logmsg("mckpending\n");
#ifndef FEATURE_BCMODE
            if (sysblk.iopending && (regs->psw.sysmask & PSW_IOMASK))
              logmsg("iopending\n");
#else /*FEATURE_BCMODE*/
            if (sysblk.iopending &&
               (regs->psw.sysmask & (regs->psw.ecmode ? PSW_IOMASK : 0xFE)))
              logmsg("iopending\n");
#endif /*FEATURE_BCMODE*/
#if MAX_CPU_ENGINES > 1
            if (sysblk.brdcstncpu != 0)
              logmsg("broadcast\n");
#endif /*MAX_CPU_ENGINES > 1*/
            if (sysblk.intkey && (regs->cr[0] & CR0_XM_INTKEY))
              logmsg("intkey\n");
            if (sysblk.servsig && (regs->cr[0] & CR0_XM_SERVSIG))
              logmsg("servsig\n");
            if (regs->psw.wait)
              logmsg("wait\n");
          }
        }
#endif
        debugmsg("aft int\n");

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

        /* execute instruction */

        regs->instvalid = 0;

        /* Fetch the next sequential instruction */
        instfetch (regs->inst, regs->psw.ia, regs);

        /* Set the instruction validity flag */
        regs->instvalid = 1;
        regs->iaabs = 0;

        regs->actentry = NULL;
#ifdef LASTINST
        save_lastinst(regs->lastinst, regs->inst);
#endif
        debugmsg("bef execute %x %x\n", regs->inst[0], regs->inst[1]);
        EXECUTE_INSTRUCTION (regs->inst, 0, regs);
        debugmsg("aft execute\n");

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
    }
    return NULL;

} /* end function do_interpret */
/*-------------------------------------------------------------------*/
/* fragment execution                                                */
/*-------------------------------------------------------------------*/
void do_interrupt (REGS *regs, int *diswait)
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
                    return;
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
                longjmp(regs->progjmp, 0);
            } /* end if(cpustate == STOPPED) */

            /* Test for wait state */
            if (regs->psw.wait)
            {
                /* Test for disabled wait PSW and issue message */
                if ( *diswait == 0 &&
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
                    *diswait = 1;
                }

                regs->int3count++;
                /* Wait for I/O, external or restart interrupt */
                wait_condition (&sysblk.intcond, &sysblk.intlock);
                release_lock (&sysblk.intlock);
                longjmp(regs->progjmp, 0);
            } /* end if(wait) */

            /* Reset disabled wait state indicator */
            *diswait = 0;

            /* Release the interrupt lock */
            release_lock (&sysblk.intlock);
#ifdef CHECK_FRAGADDRESS
            strcpy(regs->file, __FILE__);
            regs->line = __LINE__;
#endif
            ibuf_assign_fragment(regs, regs->psw.ia);

}
/*-------------------------------------------------------------------*/
/* Display instruction                                               */
/*-------------------------------------------------------------------*/
void do_display (REGS *regs)
        {
int     stepthis = 0;                   /* Stop on this instruction  */
int     shouldbreak;                    /* 1=Stop at breakpoint      */
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

/*-------------------------------------------------------------------*/
/* fetch instruction and execute                                     */
/*-------------------------------------------------------------------*/
void do_fetchinst (REGS *regs)
{
            debugmsg("beg execute\n");
            regs->instvalid = 0;

            /* Fetch the next sequential instruction */
            instfetch (regs->inst, regs->psw.ia, regs);

            /* Set the instruction validity flag */
            regs->instvalid = 1;
            regs->iaabs = 0;

            CHECK_FRAGPTR(regs, " bef EXECUTE ");
            regs->actentry = NULL;
#ifdef LASTINST
            save_lastinst(regs->lastinst, regs->inst);
#endif
            debugmsg("bef execute %x %x\n", regs->inst[0], regs->inst[1]);
            EXECUTE_INSTRUCTION (regs->inst, 0, regs);
            debugmsg("aft execute %p %p\n", regs->actpage, regs->actentry);
            if (!regs->actentry)
                GET_FRAGENTRY(regs);
            debugmsg("end execute %p %p\n", regs->actpage, regs->actentry);
}
#ifdef TRACE_INTERRUPT_DELAY
/*-------------------------------------------------------------------*/
/* fragment execution                                                */
/*-------------------------------------------------------------------*/
void interrupt_delay (REGS *regs)
{
          if (((sysblk.extpending || regs->cpuint)
              && (regs->psw.sysmask & PSW_EXTMASK))
              || (sysblk.mckpending && regs->psw.mach)
#ifndef FEATURE_BCMODE
              || (sysblk.iopending && (regs->psw.sysmask & PSW_IOMASK))
#else /*FEATURE_BCMODE*/
              ||  (sysblk.iopending &&
                  (regs->psw.sysmask & (regs->psw.ecmode ? PSW_IOMASK : 0xFE)))
#endif /*FEATURE_BCMODE*/
#if MAX_CPU_ENGINES > 1
              || sysblk.brdcstncpu != 0
#endif /*MAX_CPU_ENGINES > 1*/
              || (sysblk.intkey && (regs->cr[0] & CR0_XM_INTKEY))
              || (sysblk.servsig && (regs->cr[0] & CR0_XM_SERVSIG))
              || regs->psw.wait)
          {
            regs->int2count++;
            logmsg("Missing interrupts: %llu ", regs->instcount);
            if ((sysblk.extpending || regs->cpuint)
                 && (regs->psw.sysmask & PSW_EXTMASK))
              logmsg("extpending/cpuint\n");

            if (sysblk.mckpending && regs->psw.mach)
              logmsg("mckpending\n");
#ifndef FEATURE_BCMODE
            if (sysblk.iopending && (regs->psw.sysmask & PSW_IOMASK))
              logmsg("iopending\n");
#else /*FEATURE_BCMODE*/
            if (sysblk.iopending &&
               (regs->psw.sysmask & (regs->psw.ecmode ? PSW_IOMASK : 0xFE)))
              logmsg("iopending\n");
#endif /*FEATURE_BCMODE*/
#if MAX_CPU_ENGINES > 1
            if (sysblk.brdcstncpu != 0)
              logmsg("broadcast\n");
#endif /*MAX_CPU_ENGINES > 1*/
            if (sysblk.intkey && (regs->cr[0] & CR0_XM_INTKEY))
              logmsg("intkey\n");
            if (sysblk.servsig && (regs->cr[0] & CR0_XM_SERVSIG))
              logmsg("servsig\n");
            if (regs->psw.wait)
              logmsg("wait\n");
          }
}
#endif
#if defined(CHECK_FRAGADDRESS) || defined(CHECK_FRAGPARMS)
/*-------------------------------------------------------------------*/
/* Check internal IBUF structures                                    */
/*-------------------------------------------------------------------*/
void do_checks (REGS *regs)
{
            strcpy(regs->file, __FILE__);
            regs->line = __LINE__;
#ifdef CHECK_FRAGADDRESS
        strcpy(regs->file, __FILE__);
        regs->line = __LINE__;
        if (logical_to_abs(regs->psw.ia, 0, regs, ACCTYPE_INSTFETCH, 
               regs->psw.pkey) != ((FRAGENTRY*)regs->actentry)->iaabs)
        {
            logmsg("WRONG TRANSLATED FRAGENTRY %llu %4x %4x %x %x\n", 
                   regs->instcount,
                   regs->psw.ia, 
                   ((FRAGENTRY*)regs->actentry)->ia,
                   regs->lastinst[0],
                   regs->lastinst[1]);

            regs->iaabs = 0;
            ibuf_assign_fragment (regs, regs->psw.ia);
            
        }
        if ((regs->psw.ia != ((FRAGENTRY*)regs->actentry)->ia) &&  
            ((FRAGENTRY*)(regs->actentry))->inst)
        {
            logmsg("WRONG FRAGENTRY %llu %4x %4x %x %x\n", 
                   regs->instcount,
                   regs->psw.ia, 
                   ((FRAGENTRY*)regs->actentry)->ia,
                   regs->lastinst[0],
                   regs->lastinst[1]);
            regs->iaabs = 0;
            ibuf_get_fragentry (regs, regs->psw.ia);
        }
#endif

#ifdef CHECK_FRAGPARMS
        if (((FRAGENTRY*)(regs->actentry))->inst)
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
                regs->iaabs = 0;
                ibuf_compile_frag(regs, regs->psw.ia);
            }
        }
#endif
}
#endif

/*-------------------------------------------------------------------*/
/* fragment execution                                                */
/*-------------------------------------------------------------------*/
void *do_execute (REGS *pregs, int *diswait)
{
#define CPU_PRIORITY    15              /* CPU thread priority       */
REGS *regs = pregs;

    while(1)
    {
        debugmsg("beg loop execute\n");
        CHECK_FRAGPTR(regs, "beg while");
#if MAX_CPU_ENGINES > 1
        if (regs->doint || (sysblk.brdcstncpu != 0 ))
#else
        if (regs->doint )
#endif
            do_interrupt(regs, diswait);
            if (!regs->cputid)
                return NULL;
#ifdef TRACE_INTERRUPT_DELAY
        else
        {
            interrup_delay(regs);
        }
#endif
        debugmsg("aft int execute %p\n", regs->actentry);

        CHECK_FRAGPTR(regs, " aft interrupt");


        if (!(*((FRAGENTRY*)regs->actentry)->valid))
             /* Compiled fragment not valid (any more); recompile */
        {
            debugmsg("aft if\n");
#ifdef IBUF_STAT
            regs->ibufcodechange++;
#endif
            regs->actpage = NULL;
            longjmp(regs->progjmp, 0);
        }

        debugmsg("aft compile\n");

#if defined(CHECK_FRAGADDRESS) || defined(CHECK_FRAGPARMS)
        do_checks(regs);
#endif

        debugmsg("aft check\n");

        CHECK_FRAGPTR(regs, " bef doinst");

        regs->instcount++;
        if (sysblk.doinst)
            do_display(regs);

#ifdef INSTSTAT
        regs->instcountx[((REGS*)(regs->actentry))->inst[0]]++;
#endif

        CHECK_FRAGPTR(regs, " bef exec case");

#if 1
        /* execute prefetched inst from compiled fragment */
        if(!((FRAGENTRY*)(regs->actentry))->inst)
            do_fetchinst(regs); 
        else
#endif
        {
            debugmsg("end jexecute\n");
#ifdef LASTINST
            save_lastinst(regs->lastinst, 
                      ((FRAGENTRY*)(regs->actentry))->inst);
#endif
            CHECK_FRAGPTR(regs, " bef JEXECUTE ");
#ifdef IBUF_STAT
            regs->ibufexeinst++;
#endif
//            ibuf_compile_frag(regs, regs->psw.ia);
//            JEXECUTE_INSTRUCTION (regs);
            debugmsg("bef execute %x %x\n", 
                     ((FRAGENTRY*)(regs->actentry))->inst[0],
                     ((FRAGENTRY*)(regs->actentry))->inst[1]);
            EXECUTE_INSTRUCTION(((FRAGENTRY*)(regs->actentry))->inst, 
                                      0, regs);
            debugmsg("aft jexecute\n");
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
    }
    return(NULL);
}
/*-------------------------------------------------------------------*/
/* CPU instruction execution thread                                  */
/*-------------------------------------------------------------------*/
void *cpu_thread (REGS *pregs)
{
#define CPU_PRIORITY    15              /* CPU thread priority       */
REGS *regs = pregs;
int   diswait = 0;                      /* 1=Disabled wait state     */

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

    if (!regs->fragbuffer)
        regs->fragbuffer = calloc(FRAG_BUFFER, sizeof(FRAG));

    if (!regs->fragbuffer)
        return(NULL);

    if (!regs->fragvalid)
        regs->fragvalid = calloc(FRAG_BUFFER, sizeof(BYTE));

    if (!regs->fragvalid)
        return(NULL);

#ifdef IBUF_STAT
    if (!regs->fraginvalid)
        regs->fraginvalid = calloc(FRAG_BUFFER, sizeof(BYTE));

    if (!regs->fraginvalid)
        return(NULL);
#endif

    if (!regs->dict)
        regs->dict = calloc(FRAG_BUFFER * FRAG_BYTESIZE, sizeof(void*));

    if (!regs->dict)
    {
        logmsg("ERROR calloc dict\n");
        return(NULL);
    }

    if (!regs->icount)
        regs->icount = calloc(FRAG_BUFFER, sizeof(U64));

    if (!regs->icount)
    {
        logmsg("ERROR calloc icount\n");
        return(NULL);
    }

#if 0
    regs->acticount = &regs->icount[0];
    regs->actfragia = 0;
#endif

    debugmsg("bef reset\n");

    /* Perform initial cpu reset */
    initial_cpu_reset(regs);
    
    ibuf_init(regs);

    /* release the intlock */
    release_lock(&sysblk.intlock);

    /* Establish longjmp destination for program check */
    setjmp(regs->progjmp);

#ifdef DEBUGMSG
    if (regs->instcount > 999999999)
        regs->debugmsg = 1;
#endif

    debugmsg("bef assign\n");
#ifdef CHECK_FRAGADDRESS
    strcpy(regs->file, __FILE__);
    regs->line = __LINE__;
#endif
    if (!regs->psw.wait)
        ibuf_assign_fragment(regs, regs->psw.ia);

    /* Execute the program */

    debugmsg("bef mainloop\n");
#ifdef IBUF_SWITCH
    if (regs->actpage)
    {
        debugmsg("BEF LOOP%p %p\n", regs->actpage, regs->actentry);
        do_execute(regs, &diswait);
    }
    else
    {
        debugmsg("BEF LOOP%p %p\n", regs->actpage, regs->actentry);
        do_interpret(regs, &diswait);
    }
#else
#ifdef DO_EXECUTE
    do_execute(regs, &diswait);
#else
    do_interpret(regs, &diswait);
#endif
#endif


    return NULL;

} /* end function cpu_thread */
#endif
