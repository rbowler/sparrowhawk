/* SIE.C        (c) Copyright Jan Jaeger, 1999-2000                  */
/*              Interpretive Execution                               */

/*      This module contains the SIE instruction as                  */
/*      described in IBM S/370 Extended Architecture                 */
/*      Interpretive Execution, SA22-7095-01                         */
/*      and                                                          */
/*      Enterprise Systems Architecture / Extended Configuration     */
/*      Principles of Operation, SC24-5594-02                        */

#include "hercules.h"

#if defined(FEATURE_INTERPRETIVE_EXECUTION)

#include "opcode.h"
#include "inline.h"
#ifdef IBUF
#include "ibuf.h"
#endif

#define SIE_I_WAIT(_guestregs) \
        ((_guestregs)->psw.wait)

#define SIE_I_STOP(_guestregs) \
        ((_guestregs)->siebk->v & SIE_V_STOP)

#define SIE_I_IO(_guestregs) \
        (((_guestregs)->siebk->v & SIE_V_IO) \
           && ((_guestregs)->psw.sysmask \
                 & ((_guestregs)->psw.ecmode ? PSW_IOMASK : 0xFE) ))

#define SIE_I_EXT(_guestregs) \
        (((_guestregs)->siebk->v & SIE_V_EXT) \
          && ((_guestregs)->psw.sysmask & PSW_EXTMASK))

#define SIE_I_HOST(_hostregs) \
        ((sysblk.mckpending && (_hostregs)->psw.mach) \
          || ((sysblk.extpending || (_hostregs)->cpuint) \
              && ((_hostregs)->psw.sysmask & PSW_EXTMASK)) \
          || (_hostregs)->restart \
          || (sysblk.iopending && ((_hostregs)->psw.sysmask & PSW_IOMASK)) \
          || (_hostregs)->psw.wait \
          || (_hostregs)->cpustate != CPUSTATE_STARTED)

/*-------------------------------------------------------------------*/
/* B214 SIE   - Start Interpretive Execution                     [S] */
/*-------------------------------------------------------------------*/
void zz_start_interpretive_execution (BYTE inst[], int execflag, REGS *regs)
{
int     b2;                             /* Values of R fields        */
U32     effective_addr2;                /* address of state desc.    */
REGS    *guestregs;
int     gpv;                            /* guest psw validity        */
int     icode;                          /* Interception code         */
int     n;                              /* Loop counter              */
U16     lhcpu;                          /* Last Host CPU address     */

    S(inst, execflag, regs, b2, effective_addr2);

    SIE_MODE_XC_OPEX(regs);

    PRIV_CHECK(regs);

    SIE_INTERCEPT(regs);

    if(!regs->psw.amode || !PRIMARY_SPACE_MODE(&(regs->psw)))
        program_interrupt (regs, PGM_SPECIAL_OPERATION_EXCEPTION);

    if((effective_addr2 & 0xFF) != 0
      || (effective_addr2 & 0x7FFFF000) == 0
      || (effective_addr2 & 0x7FFFF000) == regs->pxr)
        program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);

    /* Perform serialization and checkpoint synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

    /* Point to SIE copy of the registers */
    guestregs = regs->guestregs;

    /* Absolute address of state descriptor block */
    guestregs->sie_state = effective_addr2;

// logmsg("SIE: state descriptor %8.8X\n",guestregs->sie_state);

    /* Direct pointer to state descriptor block */
    guestregs->siebk = (SIEBK*)(sysblk.mainstor + effective_addr2);

    /* Prefered guest indication */
    guestregs->sie_pref = (guestregs->siebk->m & SIE_M_VR) ? 1 : 0;

    /* Reference and Change Preservation Origin */
    guestregs->sie_rcpo = guestregs->siebk->rcpo[0] << 24
                        | guestregs->siebk->rcpo[1] << 16
                        | guestregs->siebk->rcpo[2] << 8
                        | guestregs->siebk->rcpo[3];

    /* System Control Area Origin */
    guestregs->sie_scao = guestregs->siebk->scao[0] << 24
                        | guestregs->siebk->scao[1] << 16
                        | guestregs->siebk->scao[2] << 8
                        | guestregs->siebk->scao[3];

    /* Load prefix from state descriptor */
    guestregs->pxr = guestregs->siebk->prefix[0] << 24
                   | guestregs->siebk->prefix[1] << 16
                   | guestregs->siebk->prefix[2] << 8
                   | guestregs->siebk->prefix[3];

    /* Load main storage origin */
    guestregs->sie_mso = guestregs->siebk->mso[0] << 24
                       | guestregs->siebk->mso[1] << 16;

    /* Load main storage extend */
    guestregs->mainsize = (( guestregs->siebk->mse[0] << 8
                          | guestregs->siebk->mse[1]) + 1) << 16;

    /* Load expanded storage origin */
    guestregs->sie_xso = guestregs->siebk->xso[0] << 16
                       | guestregs->siebk->xso[1] << 8
                       | guestregs->siebk->xso[2];
    guestregs->sie_xso *= (XSTORE_INCREMENT_SIZE >> XSTORE_PAGESHIFT);

    /* Load expanded storage limit */
    guestregs->sie_xsl = guestregs->siebk->xsl[0] << 16
                       | guestregs->siebk->xsl[1] << 8
                       | guestregs->siebk->xsl[2];
    guestregs->sie_xsl *= (XSTORE_INCREMENT_SIZE >> XSTORE_PAGESHIFT);

    /* Load the CPU timer */
    guestregs->ptimer = ((U64)guestregs->siebk->cputimer[0] << 56)
                      | ((U64)guestregs->siebk->cputimer[1] << 48)
                      | ((U64)guestregs->siebk->cputimer[2] << 40)
                      | ((U64)guestregs->siebk->cputimer[3] << 32)
                      | ((U64)guestregs->siebk->cputimer[4] << 24)
                      | ((U64)guestregs->siebk->cputimer[5] << 16)
                      | ((U64)guestregs->siebk->cputimer[6] << 8)
                      | (U64)guestregs->siebk->cputimer[7];
    /* Reset the CPU timer pending flag according to its value */
    if( (S64)guestregs->ptimer < 0 )
        guestregs->cpuint = guestregs->ptpend = 1;
    else
        guestregs->ptpend = 0;

    /* Load the TOD clock offset for this guest */
    guestregs->sie_epoch = ((U64)guestregs->siebk->epoch[0] << 56)
                         | ((U64)guestregs->siebk->epoch[1] << 48)
                         | ((U64)guestregs->siebk->epoch[2] << 40)
                         | ((U64)guestregs->siebk->epoch[3] << 32)
                         | ((U64)guestregs->siebk->epoch[4] << 24)
                         | ((U64)guestregs->siebk->epoch[5] << 16)
                         | ((U64)guestregs->siebk->epoch[6] << 8)
                         | (U64)guestregs->siebk->epoch[7];
    guestregs->todoffset = regs->todoffset + (guestregs->sie_epoch >> 8);

    /* Load the clock comparator */
    guestregs->clkc = ((U64)guestregs->siebk->clockcomp[0] << 56)
                    | ((U64)guestregs->siebk->clockcomp[1] << 48)
                    | ((U64)guestregs->siebk->clockcomp[2] << 40)
                    | ((U64)guestregs->siebk->clockcomp[3] << 32)
                    | ((U64)guestregs->siebk->clockcomp[4] << 24)
                    | ((U64)guestregs->siebk->clockcomp[5] << 16)
                    | ((U64)guestregs->siebk->clockcomp[6] << 8)
                    | (U64)guestregs->siebk->clockcomp[7];
    guestregs->clkc >>= 8; /* Internal Hercules format */
    /* Reset the clock comparator pending flag according to
       the setting of the TOD clock */
    if( (sysblk.todclk + guestregs->todoffset) > guestregs->clkc )
        guestregs->cpuint = guestregs->ckpend = 1;
    else
        guestregs->ckpend = 0;

    /* Load TOD Programmable Field */
    guestregs->todpr = guestregs->siebk->todpf[0] << 8
                     | guestregs->siebk->todpf[1];

    /* Load the guest registers */
    memcpy(guestregs->gpr, regs->gpr, 14 * sizeof(U32));
    memcpy(guestregs->ar, regs->ar, 16 * sizeof(U32));

    /* Load GR14 */
    guestregs->gpr[14] = guestregs->siebk->gr14[0] << 24
                       | guestregs->siebk->gr14[1] << 16
                       | guestregs->siebk->gr14[2] << 8
                       | guestregs->siebk->gr14[3];

    /* Load GR15 */
    guestregs->gpr[15] = guestregs->siebk->gr15[0] << 24
                       | guestregs->siebk->gr15[1] << 16
                       | guestregs->siebk->gr15[2] << 8
                       | guestregs->siebk->gr15[3];

    /* Load the PSW */
    gpv = load_psw(guestregs, guestregs->siebk->psw);

    /* Load control registers */
    for(n = 0;n < 16; n++)
        guestregs->cr[n] = guestregs->siebk->cr[n][0] << 24
                         | guestregs->siebk->cr[n][1] << 16
                         | guestregs->siebk->cr[n][2] << 8
                         | guestregs->siebk->cr[n][3];

    lhcpu = (guestregs->siebk->lhcpu[0] << 8) | guestregs->siebk->lhcpu[1];

    /* End operation in case of a validity check */
    if(gpv)
    {
        guestregs->siebk->c = SIE_C_VALIDITY;
        return;
    }

    /* If this is not the last host cpu that dispatched this state
       descriptor then clear the guest TLB entries */
    if(regs->cpuad != lhcpu)
    {
        purge_tlb(guestregs);
        purge_alb(guestregs);
    }

    regs->sie_active = 1;

    /* Get PSA pointer and ensure PSA is paged in */
    if(guestregs->sie_pref)
        guestregs->sie_psa = (PSA*)(sysblk.mainstor + guestregs->pxr);
    else
        guestregs->sie_psa = (PSA*)(sysblk.mainstor
                           + logical_to_abs(guestregs->sie_mso
                           + guestregs->pxr, USE_PRIMARY_SPACE, regs,
                             ACCTYPE_SIE, 0) );

    /* If this is a S/370 guest, and the interval timer is enabled
       then initialize the timer */
    if( (guestregs->siebk->m & SIE_M_370)
     && !(guestregs->siebk->m & SIE_M_ITMOF))
    {
    S32 itimer,
        olditimer;
    U32 residue;

        obtain_lock(&sysblk.todlock);

        /* Fetch the residu from the state descriptor */
        residue = guestregs->siebk->residue[0] << 24
               | guestregs->siebk->residue[1] << 16
               | guestregs->siebk->residue[2] << 8
               | guestregs->siebk->residue[3];

        /* Fetch the timer value from location 80 */
        olditimer = (S32)(((U32)(guestregs->sie_psa->inttimer[0]) << 24)
                        | ((U32)(guestregs->sie_psa->inttimer[1]) << 16)
                        | ((U32)(guestregs->sie_psa->inttimer[2]) << 8)
                        | (U32)(guestregs->sie_psa->inttimer[3]));

        /* Bit position 23 of the interval timer is deremented 
           once for each multiple of 3,333 usecs containded in 
           bit position 0-19 of the residue counter */
        itimer = olditimer - ((residue / 3333) >> 4);

        /* Store the timer back */
        guestregs->sie_psa->inttimer[0] = ((U32)itimer >> 24) & 0xFF;
        guestregs->sie_psa->inttimer[1] = ((U32)itimer >> 16) & 0xFF;
        guestregs->sie_psa->inttimer[2] = ((U32)itimer >> 8) & 0xFF;
        guestregs->sie_psa->inttimer[3] = (U32)itimer & 0xFF;

        release_lock(&sysblk.todlock);

        /* Set interrupt flag and interval timer interrupt pending
           if the interval timer went from positive to negative */
        if (itimer < 0 && olditimer >= 0)
            guestregs->cpuint = guestregs->itimer_pending = 1;

    }

    LASTPAGE_INVALIDATE (regs);

    do {
        if(!(icode = setjmp(guestregs->progjmp)))
            while(! SIE_I_WAIT(guestregs)
               && ! SIE_I_STOP(guestregs)
               && ! SIE_I_EXT(guestregs)
               && ! SIE_I_IO(guestregs)
              /* also exit if pendig interrupts for the host cpu */
               && ! SIE_I_HOST(regs) )
            {

                if(guestregs->cpuint && (guestregs->psw.sysmask & PSW_EXTMASK))
                {
                    obtain_lock(&sysblk.intlock);
                    perform_external_interrupt(guestregs);
                    release_lock(&sysblk.intlock);
                }

                guestregs->instvalid = 0;

                instfetch (guestregs->inst, guestregs->psw.ia, guestregs);

                guestregs->instvalid = 1;

                regs->instcount++;

                /* Display the instruction */
// /*ZZDEBUG*/  display_inst (guestregs, guestregs->inst);

#ifdef IBUF
                regs->actentry = NULL;
#endif
                EXECUTE_INSTRUCTION(guestregs->inst, 0, guestregs);

#if MAX_CPU_ENGINES > 1
                /* Perform broadcasted purge of ALB and TLB if requested
                   synchronize_broadcast() must be called until there are
                   no more broadcast pending because synchronize_broadcast()
                   releases and reacquires the mainlock. */

                while (sysblk.brdcstncpu != 0)
                {
                    obtain_lock (&sysblk.intlock);
                    synchronize_broadcast(regs, NULL);
                    release_lock (&sysblk.intlock);
                }
#endif /*MAX_CPU_ENGINES > 1*/

            }

        if(icode == 0)
        {
            if( SIE_I_EXT(guestregs) )
                icode = SIE_INTERCEPT_EXTREQ;
            else
                if( SIE_I_IO(guestregs) )
                    icode = SIE_INTERCEPT_IOREQ;
                else
                    if( SIE_I_STOP(guestregs) )
                        icode = SIE_INTERCEPT_STOPREQ;
                    else
                        if( SIE_I_WAIT(guestregs) )
                            icode = SIE_INTERCEPT_WAIT;
                        else
                            if( SIE_I_HOST(regs) )
                                icode = SIE_HOST_INTERRUPT;
        }

    } while(icode == 0 || icode == SIE_NO_INTERCEPT);

    sie_exit(regs, icode);

    /* Perform serialization and checkpoint synchronization */
    PERFORM_SERIALIZATION (regs);
    PERFORM_CHKPT_SYNC (regs);

} 


/* Exit SIE state, restore registers and update the state descriptor */
void sie_exit (REGS *regs, int code)
{
REGS   *guestregs;
int     n;

    /* point to 'our' sie copy of the registers */
    guestregs = regs->guestregs;

    /* zeroize interception status */
    guestregs->siebk->f = 0;

// logmsg("SIE: interception code %d\n",code);
// display_inst (guestregs, guestregs->instvalid ? guestregs->inst : NULL);

    switch(code)
    {
        case SIE_HOST_INTERRUPT:
           /* If a host interrupt is pending
              then backup the psw and exit */
            regs->psw.ia -= regs->psw.ilc;
            regs->psw.ia &= 0x7FFFFFFF;
            break;
        case SIE_INTERCEPT_PER:
            guestregs->siebk->f |= SIE_F_IF;
            /*fallthru*/
        case SIE_INTERCEPT_INST:
            guestregs->siebk->c = SIE_C_INST;
            break;
        case SIE_INTERCEPT_INSTCOMP:
            guestregs->siebk->c = SIE_C_PGMINST;
            break;
        case SIE_INTERCEPT_WAIT:
            guestregs->siebk->c = SIE_C_WAIT;
            break;
        case SIE_INTERCEPT_STOPREQ:
            guestregs->siebk->c = SIE_C_STOPREQ;
            break;
        case SIE_INTERCEPT_IOREQ:
            guestregs->siebk->c = SIE_C_IOREQ;
            break;
        case SIE_INTERCEPT_EXTREQ:
            guestregs->siebk->c = SIE_C_EXTREQ;
            break;
        case SIE_INTERCEPT_EXT:
            guestregs->siebk->c = SIE_C_EXTINT;
            break;
        case SIE_INTERCEPT_VALIDITY:
            guestregs->siebk->c = SIE_C_VALIDITY;
            break;
        case PGM_OPERATION_EXCEPTION:
            guestregs->siebk->c = SIE_C_OPEREXC;
            break;
        default:
            guestregs->siebk->c = SIE_C_PGMINT;
            break;
    }

    /* Update Last Host CPU address */
    guestregs->siebk->lhcpu[0] = (regs->cpuad >> 8) & 0xFF;
    guestregs->siebk->lhcpu[1] = regs->cpuad & 0xFF;

    /* Save CPU timer  */
    guestregs->siebk->cputimer[0] = (guestregs->ptimer >> 56) & 0xFF;
    guestregs->siebk->cputimer[1] = (guestregs->ptimer >> 48) & 0xFF;
    guestregs->siebk->cputimer[2] = (guestregs->ptimer >> 40) & 0xFF;
    guestregs->siebk->cputimer[3] = (guestregs->ptimer >> 32) & 0xFF;
    guestregs->siebk->cputimer[4] = (guestregs->ptimer >> 24) & 0xFF;
    guestregs->siebk->cputimer[5] = (guestregs->ptimer >> 16) & 0xFF;
    guestregs->siebk->cputimer[6] = (guestregs->ptimer >> 8) & 0xFF;
    guestregs->siebk->cputimer[7] = guestregs->ptimer & 0xFF;

    /* Save clock comparator */
    guestregs->clkc <<= 8; /* Internal Hercules format */
    guestregs->siebk->clockcomp[0] = (guestregs->clkc >> 56) & 0xFF;
    guestregs->siebk->clockcomp[1] = (guestregs->clkc >> 48) & 0xFF;
    guestregs->siebk->clockcomp[2] = (guestregs->clkc >> 40) & 0xFF;
    guestregs->siebk->clockcomp[3] = (guestregs->clkc >> 32) & 0xFF;
    guestregs->siebk->clockcomp[4] = (guestregs->clkc >> 24) & 0xFF;
    guestregs->siebk->clockcomp[5] = (guestregs->clkc >> 16) & 0xFF;
    guestregs->siebk->clockcomp[6] = (guestregs->clkc >> 8) & 0xFF;
    guestregs->siebk->clockcomp[7] = guestregs->clkc & 0xFF;

    /* Save TOD Programmable Field */
    guestregs->siebk->todpf[0] = (guestregs->todpr >> 8) & 0xFF;
    guestregs->siebk->todpf[1] = guestregs->todpr & 0xFF;

    /* Save GR14 */
    guestregs->siebk->gr14[0] = (guestregs->gpr[14] >> 24) & 0xFF;
    guestregs->siebk->gr14[1] = (guestregs->gpr[14] >> 16) & 0xFF;
    guestregs->siebk->gr14[2] = (guestregs->gpr[14] >> 8) & 0xFF;
    guestregs->siebk->gr14[3] = guestregs->gpr[14] & 0xFF;

    /* Save GR15 */
    guestregs->siebk->gr15[0] = (guestregs->gpr[15] >> 24) & 0xFF;
    guestregs->siebk->gr15[1] = (guestregs->gpr[15] >> 16) & 0xFF;
    guestregs->siebk->gr15[2] = (guestregs->gpr[15] >> 8) & 0xFF;
    guestregs->siebk->gr15[3] = guestregs->gpr[15] & 0xFF;

    /* Store the PSW */
    store_psw(guestregs, guestregs->siebk->psw);

    /* save control registers */
    for(n = 0;n < 16; n++)
    {
        guestregs->siebk->cr[n][0] = (guestregs->cr[n] >> 24) & 0xFF;
        guestregs->siebk->cr[n][1] = (guestregs->cr[n] >> 16) & 0xFF;
        guestregs->siebk->cr[n][2] = (guestregs->cr[n] >> 8) & 0xFF;
        guestregs->siebk->cr[n][3] = guestregs->cr[n] & 0xFF;
    }

    /* Update the approprate host registers */
    memcpy(regs->gpr, guestregs->gpr, 14 * sizeof(U32));
    memcpy(regs->ar, guestregs->ar, 16 * sizeof(U32));

    /* Zeroize the interruption parameters */
    memset(guestregs->siebk->ipa, 0, 10);

    if(guestregs->siebk->c == SIE_C_INST
      || guestregs->siebk->c == SIE_C_PGMINST
      || guestregs->siebk->c == SIE_C_OPEREXC)
    {
        /* Indicate interception format 2 */
        guestregs->siebk->f |= SIE_F_IN;

        /* Update interception parameters in the state descriptor */
        if(guestregs->inst[0] != 0x44)
        {
            if(guestregs->instvalid)
                memcpy(guestregs->siebk->ipa, guestregs->inst, guestregs->psw.ilc);
        }
        else
        {
        int exilc;
            guestregs->siebk->f |= SIE_F_EX;
            exilc = (guestregs->exinst[0] < 0x40) ? 2 :
                    (guestregs->exinst[0] < 0xC0) ? 4 : 6;
            memcpy(guestregs->siebk->ipa, guestregs->exinst, exilc);
        }
    }

    /* Indicate we have left SIE mode */
    regs->sie_active = 0;

    LASTPAGE_INVALIDATE(regs);
    REASSIGN_FRAG (regs);

}
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
