/* SERVICE.C    (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Service Processor                            */

/*-------------------------------------------------------------------*/
/* This module implements the service processor and external         */
/* interrupt functions for the Hercules ESA/390 emulator.            */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Service Call Logical Processor command word definitions           */
/*-------------------------------------------------------------------*/
#define SCLP_READ_SCP_INFO      0x00020001

/*-------------------------------------------------------------------*/
/* Service Call Control Block structure definitions                  */
/*-------------------------------------------------------------------*/
typedef struct _SCCB_HEADER {
        HWORD   length;                 /* Total length of SCCB      */
        BYTE    flag;                   /* Flag byte                 */
        BYTE    resv1[3];               /* Reserved                  */
        BYTE    reas;                   /* Reason code               */
        BYTE    resp;                   /* Response class code       */
    } SCCB_HEADER;

/* Bit definitions for SCCB header flag byte */
#define SCCB_FLAG_SYNC          0x80    /* Synchronous request       */

/* Bit definitions for SCCB header reason code */
#define SCCB_REAS_NONE          0x00    /* No reason                 */
#define SCCB_REAS_NOT_ALIGNED   0x01    /* SCCB not on 2K boundary   */
#define SCCB_REAS_ODD_LENGTH    0x02    /* Length not multiple of 8  */
#define SCCB_REAS_TOO_SHORT     0x03    /* Length is inadequate      */
#define SCCB_REAS_INVALID_CMD   0x01    /* Invalid SCLP command code */

/* Bit definitions for SCCB header response class code */
#define SCCB_RESP_BLOCK_ERROR   0x00    /* Data block error          */
#define SCCB_RESP_INFO          0x10    /* Information returned      */
#define SCCB_RESP_COMPLETE      0x20    /* Command complete          */
#define SCCB_RESP_BACKOUT       0x40    /* Command backed out        */
#define SCCB_RESP_REJECT        0xF0    /* Command reject            */

/* SCP information data area */
typedef struct _SCCB_SCP_INFO {
        HWORD   realinum;               /* Number of real storage
                                           increments installed      */
        BYTE    realiszm;               /* Size of each real storage
                                           increment in MB           */
        BYTE    realbszk;               /* Size of each real storage
                                           block in KB               */
        HWORD   realiint;               /* Real storage increment
                                           block interleave interval */
        HWORD   resv2;                  /* Reserved                  */
        HWORD   numcpu;                 /* Number of CPUs installed  */
        HWORD   offcpu;                 /* Offset from start of SCCB
                                           to CPU information array  */
        HWORD   numhsa;                 /* Number of HSAs            */
        HWORD   offhsa;                 /* Offset from start of SCCB
                                           to HSA information array  */
        BYTE    loadparm[8];            /* Load parameter            */
        FWORD   xpndinum;               /* Number of expanded storage
                                           increments installed      */
        FWORD   xpndsz4K;               /* Number of 4KB blocks in an
                                           expanded storage increment*/
        HWORD   xpndenum;               /* Number of expanded storage
                                           elements installed        */
        HWORD   resv3;                  /* Reserved                  */
        HWORD   vectssiz;               /* Vector section size       */
        HWORD   vectpsum;               /* Vector partial sum number */
        BYTE    ifm1;                   /* Installed facilities 1    */
        BYTE    ifm2;                   /* Installed facilities 2    */
        BYTE    ifm3;                   /* Installed facilities 3    */
        BYTE    ifm4;                   /* Installed facilities 4    */
        BYTE    ifm5;                   /* Installed facilities 5    */
        BYTE    ifm6;                   /* Installed facilities 6    */
        BYTE    ifm7;                   /* Installed facilities 7    */
        BYTE    ifm8;                   /* Installed facilities 8    */
        BYTE    resv4[8];               /* Reserved                  */
        HWORD   maxresgp;               /* Maximum resource group    */
        BYTE    resv5[6];               /* Reserved                  */
        HWORD   nummpf;                 /* Number of entries in MPF
                                           information array         */
        HWORD   offmpf;                 /* Offset from start of SCCB
                                           to MPF information array  */
        BYTE    resv6[4];               /* Reserved                  */
        BYTE    cfg1;                   /* Config characteristics 1  */
        BYTE    cfg2;                   /* Config characteristics 2  */
        BYTE    cfg3;                   /* Config characteristics 3  */
        BYTE    cfg4;                   /* Config characteristics 4  */
        BYTE    cfg5;                   /* Config characteristics 5  */
        BYTE    cfg6;                   /* Config characteristics 6  */
        FWORD   rcci;                   /* Capacity                  */
        BYTE    resv7;                  /* Reserved                  */
        BYTE    numcrl;                 /* Max #of copy and reassign
                                           list elements allowed     */
        FWORD   etrtol;                 /* ETR sync check tolerance  */
        BYTE    resv8[32];              /* Reserved                  */
    } SCCB_SCP_INFO;

/* Bit definitions for installed facilities */
#define SCCB_IFM1_CHANNEL_PATH_INFORMATION              0x80
#define SCCB_IFM1_CHANNEL_PATH_SUBSYSTEM_COMMAND        0x40
#define SCCB_IFM1_CHANNEL_PATH_RECONFIG                 0x20
#define SCCB_IFM1_CPU_INFORMATION                       0x08
#define SCCB_IFM1_CPU_RECONFIG                          0x04
#define SCCB_IFM2_SIGNAL_ALARM                          0x80
#define SCCB_IFM2_WRITE_OPERATOR_MESSAGE                0x40
#define SCCB_IFM2_STORE_STATUS_ON_LOAD                  0x20
#define SCCB_IFM2_RESTART_REASONS                       0x10
#define SCCB_IFM2_INSTRUCTION_ADDRESS_TRACE_BUFFER      0x08
#define SCCB_IFM2_LOAD_PARAMETER                        0x04
#define SCCB_IFM2_READ_AND_WRITE_DATA                   0x02
#define SCCB_IFM3_REAL_STORAGE_INCREMENT_RECONFIG       0x80
#define SCCB_IFM3_REAL_STORAGE_ELEMENT_INFO             0x40
#define SCCB_IFM3_REAL_STORAGE_ELEMENT_RECONFIG         0x20
#define SCCB_IFM3_COPY_AND_REASSIGN_STORAGE             0x10
#define SCCB_IFM3_EXTENDED_STORAGE_USABILITY_MAP        0x08
#define SCCB_IFM3_EXTENDED_STORAGE_ELEMENT_INFO         0x04
#define SCCB_IFM3_EXTENDED_STORAGE_ELEMENT_RECONFIG     0x02
#define SCCB_IFM3_COPY_AND_REASSIGN_STORAGE_LIST        0x01
#define SCCB_IFM4_VECTOR_FEATURE_RECONFIG               0x80
#define SCCB_IFM4_READ_WRITE_EVENT_FEATURE              0x40
#define SCCB_IFM4_READ_RESOURCE_GROUP_INFO              0x08

/* Bit definitions for configuration characteristics */
#define SCCB_CFG1_LOGICALLY_PARTITIONED                 0x80
#define SCCB_CFG1_SUPPRESSION_ON_PROTECTION             0x20
#define SCCB_CFG1_INITIATE_RESET                        0x10
#define SCCB_CFG1_STORE_CHANNEL_SUBSYS_CHARACTERISTICS  0x08
#define SCCB_CFG2_CSLO                                  0x40
#define SCCB_CFG3_DEVICE_ACTIVE_ONLY_MEASUREMENT        0x40
#define SCCB_CFG3_CHECKSUM_INSTRUCTION                  0x01
#define SCCB_CFG4_PERFORM_LOCKED_OPERATION              0x40

/* CPU information array entry */
typedef struct _SCCB_CPU_INFO {
        BYTE    cpa;                    /* CPU address               */
        BYTE    tod;                    /* TOD clock number          */
        BYTE    resv1[2];               /* Reserved                  */
        BYTE    cpf1;                   /* CPU installed features 1  */
        BYTE    cpf2;                   /* CPU installed features 2  */
        BYTE    resv2[10];              /* Reserved                  */
    } SCCB_CPU_INFO;

/* Bit definitions for CPU installed features */
#define SCCB_CPF1_VECTOR_FEATURE_INSTALLED              0x80
#define SCCB_CPF1_VECTOR_FEATURE_CONNECTED              0x40
#define SCCB_CPF1_VECTOR_FEATURE_STANDBY_STATE          0x20
#define SCCB_CPF1_CRYPTO_FEATURE_INSTALLED              0x10
#define SCCB_CPF2_PRIVATE_SPACE_BIT_INSTALLED           0x80
#define SCCB_CPF2_PER2_INSTALLED                        0x01

/* HSA information array entry */
typedef struct _SCCB_HSA_INFO {
        HWORD   hssz;                   /* Size of HSA in 4K blocks  */
        FWORD   ahsa;                   /* Address of HSA            */
    } SCCB_HSA_INFO;

/* MPF information array entry */
typedef struct _SCCB_MPF_INFO {
        HWORD   mpfy;                   /* MPF info array entry      */
    } SCCB_MPF_INFO;


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
        exit(1);
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
        logmsg ("External interrupt: CPU timer=%16.16llX\n",
                regs->ptimer);
        external_interrupt (EXT_CPU_TIMER_INTERRUPT, regs);
        return;
    }

    /* External interrupt if TOD clock exceeds clock comparator */
    if (sysblk.todclk > regs->clkc
        && (regs->psw.sysmask & PSW_EXTMASK)
        && (regs->cr[0] & CR0_XM_CLKC))
    {
        logmsg ("External interrupt: Clock comparator\n");
        external_interrupt (EXT_CLOCK_COMPARATOR_INTERRUPT, regs);
        return;
    }

#ifdef FEATURE_INTERVAL_TIMER
    if (regs->itimer_pending
        && (regs->psw.sysmask & PSW_EXTMASK)
        && (regs->cr[0] & CR0_XM_ITIMER))
    {
        logmsg ("External interrupt: Interval timer\n");
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
        logmsg ("External interrupt: Service signal %8.8lX\n",
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
/* Process service call instruction and return condition code        */
/*-------------------------------------------------------------------*/
int service_call (U32 sclp_command, U32 sccb_absolute_addr)
{
int             i;                      /* Array subscript           */
int             realmb;                 /* Real storage size in MB   */
int             sccblen;                /* Length of SCCB            */
SCCB_HEADER    *sccb;                   /* -> SCCB header            */
SCCB_SCP_INFO  *sccbscp;                /* -> SCCB SCP information   */
SCCB_CPU_INFO  *sccbcpu;                /* -> SCCB CPU information   */
U16             offset;                 /* Offset from start of SCCB */

    /* Program check if SCCB is not on a doubleword boundary */
    if ( sccb_absolute_addr & 0x00000007 )
    {
        program_check (PGM_SPECIFICATION_EXCEPTION);
        return 3;
    }

    /* Program check if SCCB is outside main storage */
    if ( sccb_absolute_addr >= sysblk.mainsize )
    {
        program_check (PGM_ADDRESSING_EXCEPTION);
        return 3;
    }

    /*debug*/logmsg("Service call %8.8lX SCCB=%8.8lX\n",
    /*debug*/       sclp_command, sccb_absolute_addr);

    /* Point to service call control block */
    sccb = (SCCB_HEADER*)(sysblk.mainstor + sccb_absolute_addr);

    /* Load SCCB length from header */
    sccblen = (sccb->length[0] << 8) | sccb->length[1];

    /* Program check if end of SCCB falls outside main storage */
    if ( sysblk.mainsize - sccblen < sccb_absolute_addr )
    {
        program_check (PGM_ADDRESSING_EXCEPTION);
        return 3;
    }

    /* Test SCLP command word */
    switch (sclp_command) {

    case SCLP_READ_SCP_INFO:

        /* Set response code X'0300' if SCCB length
           is insufficient to contain SCP info */
        if ( sccblen < sizeof(SCCB_HEADER) + sizeof(SCCB_SCP_INFO)
                + (sizeof(SCCB_CPU_INFO) * sysblk.numcpu))
        {
            sccb->reas = SCCB_REAS_TOO_SHORT;
            sccb->resp = SCCB_RESP_BLOCK_ERROR;
            break;
        }

        /* Point to SCCB data area following SCCB header */
        sccbscp = (SCCB_SCP_INFO*)(sccb+1);
        memset (sccbscp, 0, sizeof(SCCB_SCP_INFO));

        /* Set main storage size in SCCB */
        realmb = sysblk.mainsize >> 20;
        sccbscp->realinum[0] = (realmb & 0xFF00) >> 8;
        sccbscp->realinum[1] = realmb & 0xFF;
        sccbscp->realiszm = 1;
        sccbscp->realbszk = 4;
        sccbscp->realiint[0] = 0;
        sccbscp->realiint[1] = 1;

        /* Set expanded storage size in SCCB */
        sccbscp->xpndinum[0] = 0;
        sccbscp->xpndinum[1] = 0;
        sccbscp->xpndinum[2] = 0;
        sccbscp->xpndinum[3] = 1;
        sccbscp->xpndsz4K[0] = (sysblk.xpndsize & 0xFF000000) >> 24;
        sccbscp->xpndsz4K[1] = (sysblk.xpndsize & 0xFF0000) >> 16;
        sccbscp->xpndsz4K[2] = (sysblk.xpndsize & 0xFF00) >> 8;
        sccbscp->xpndsz4K[3] = sysblk.xpndsize & 0xFF;
        sccbscp->xpndenum[0] = 0;
        sccbscp->xpndenum[1] = 1;

        /* Set CPU array count and offset in SCCB */
        sccbscp->numcpu[0] = (sysblk.numcpu & 0xFF00) >> 8;
        sccbscp->numcpu[1] = sysblk.numcpu & 0xFF;
        offset = sizeof(SCCB_HEADER) + sizeof(SCCB_SCP_INFO);
        sccbscp->offcpu[0] = (offset & 0xFF00) >> 8;
        sccbscp->offcpu[1] = offset & 0xFF;

        /* Set HSA array count and offset in SCCB */
        sccbscp->numhsa[0] = 0;
        sccbscp->numhsa[1] = 0;
        offset += sizeof(SCCB_CPU_INFO) * sysblk.numcpu;
        sccbscp->offhsa[0] = (offset & 0xFF00) >> 8;
        sccbscp->offhsa[1] = offset & 0xFF;

        /* Move IPL load parameter to SCCB */
        memcpy (sccbscp->loadparm, sysblk.loadparm, 8);

        /* Set installed features bit mask in SCCB */
//      sccbscp->ifm1 = SCCB_IFM1_CHANNEL_PATH_INFORMATION
//                    | SCCB_IFM1_CHANNEL_PATH_SUBSYSTEM_COMMAND
//                    | SCCB_IFM1_CHANNEL_PATH_RECONFIG
//                    | SCCB_IFM1_CPU_INFORMATION
//                    | SCCB_IFM1_CPU_RECONFIG;
//      sccbscp->ifm2 = SCCB_IFM2_SIGNAL_ALARM
//                    | SCCB_IFM2_STORE_STATUS_ON_LOAD
//                    | SCCB_IFM2_RESTART_REASONS
//                    | SCCB_IFM2_LOAD_PARAMETER;
        sccbscp->ifm2 = SCCB_IFM2_LOAD_PARAMETER;
//      sccbscp->cfg1 = SCCB_CFG1_SUPPRESSION_ON_PROTECTION
//                    | SCCB_CFG1_STORE_CHANNEL_SUBSYS_CHARACTERISTICS;

        /* Build the CPU information array after the SCP info */
        sccbcpu = (SCCB_CPU_INFO*)(sccbscp+1);
        for (i = 0; i < sysblk.numcpu; i++, sccbcpu++)
        {
            memset (sccbcpu, 0, sizeof(SCCB_CPU_INFO));
            sccbcpu->cpa = i;
            sccbcpu->tod = 0;
            sccbcpu->cpf2 = SCCB_CPF2_PRIVATE_SPACE_BIT_INSTALLED;
        }

        /* Set response code X'0010' in SCCB header */
        sccb->reas = SCCB_REAS_NONE;
        sccb->resp = SCCB_RESP_INFO;

        break;

    default:
        /* Set response code X'01F0' for invalid SCLP command */
        sccb->reas = SCCB_REAS_INVALID_CMD;
        sccb->resp = SCCB_RESP_REJECT;

        break;

    } /* end switch(sclp_command) */

    /* If immediate response is requested, return condition code 1 */
    if (sccb->flag & SCCB_FLAG_SYNC)
        return 1;

    /* Obtain the interrupt lock */
    obtain_lock (&sysblk.intlock);

    /* Set service signal external interrupt pending */
    sysblk.servparm = sccb_absolute_addr;
    sysblk.servsig = 1;

    /* Release the interrupt lock */
    release_lock (&sysblk.intlock);

    /* Return condition code 0 */
    return 0;

} /* end function service_call */

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

