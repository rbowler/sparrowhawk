/* HERCULES.H	(c) Copyright Roger Bowler, 1999-2001		     */
/*		ESA/390 Emulator Header File			     */

/* Interpretive Execution - (c) Copyright Jan Jaeger, 1999-2001      */

/*-------------------------------------------------------------------*/
/* Header file containing Hercules internal data structures	     */
/* and function prototypes.					     */
/*-------------------------------------------------------------------*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <termios.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "version.h"
#include "hetlib.h"

/*-------------------------------------------------------------------*/
/* Macro definitions for implementation options 		     */
/*-------------------------------------------------------------------*/
#define SMP_SERIALIZATION		/* Serialize storage for SMP */
#define CKD_MAXFILES		4	/* Max files per CKD volume  */
#define CKD_KEY_TRACING 		/* Trace CKD search keys     */
#define MIPS_COUNTING			/* Display MIPS on ctl panel */
#define TODCLOCK_DRAG_FACTOR		/* Enable toddrag feature    */
#define PANEL_REFRESH_RATE              /* Enable panrate feature    */
#define PANEL_REFRESH_RATE_FAST 50      /* Fast refresh rate         */
#define PANEL_REFRESH_RATE_SLOW 500     /* Slow refresh rate         */

#define VECTOR_SECTION_SIZE     128     /* Vector section size       */
#define VECTOR_PARTIAL_SUM_NUMBER 1     /* Vector partial sum number */

/*-------------------------------------------------------------------*/
/* Performance options                                               */
/*-------------------------------------------------------------------*/
#undef EXPERIMENTAL
#undef CHECK_OPTIMIZATION

#ifdef EXPERIMENTAL
#ifdef CHECK_OPTIMIZATION
#undef INLINE_LOGICAL               /* inline code for dat.c         */
#undef INLINE_IFETCH                /* inline code for dat.c         */
#undef INLINE_VFETCH                /* inline code for dat.c         */
#undef INLINE_VSTORE                /* inline code for dat.c         */
#define INTERRUPT_OPTIMIZE          /* enable check for doint flag   */
#undef  TRACE_INTERRUPT_DELAY       /* trace missing interrups       */

#define OPTIMIZE_IAABS              /* IAABS optimization            */
#define CHECK_IAABS                 /* Check IAABS optimization      */

#define IBUF                        /* inst. prefetch and buffering  */
#define IBUF_FASTOP                 /* RR, RX Fastexecution          */
#define INLINE_GET                  /* inline code for ibuf.h        */
#define INLINE_INVALIDATE           /* inline code for ibuf.h        */
#define  IBUF_STAT                   /* IBUF statistics               */
#define IBUF_SWITCH                 /* Switch IBUF dynamically on/off*/
#define CHECK_FRAGPARMS             /* check compiled operation parms*/
#define CHECK_FRAGADDRESS           /* check inst.addr in comp. op   */
#undef  DO_INSTFETCH                /* Do allways instfetch          */
#define CHECK_PTR                   /* check fragment/entry pointer  */
#define DEBUGMSG                    /* Write debugmessages           */
#undef  FLUSHLOG                    /* use logfile with flush        */

#define FEATURE_OPTIMIZE_SAME_PAGE  /* fast address translation      */
#define CHECK_PAGEADDR              /* check fast address translation*/

#define FEATURE_WATCHPOINT          /* watchpoint for phys. addr     */
#undef  INSTSTAT                    /* instruction stat              */
#else /* no CHECK_OPIMIZATION */
#define INLINE_LOGICAL               /* inline code for dat.c         */
#define INLINE_IFETCH                /* inline code for dat.c         */
#define INLINE_VFETCH                /* inline code for dat.c         */
#define INLINE_VSTORE                /* inline code for dat.c         */
#define INTERRUPT_OPTIMIZE          /* enable check for doint flag   */
#undef  TRACE_INTERRUPT_DELAY       /* trace missing interrups       */

#define OPTIMIZE_IAABS              /* IAABS optimization            */
#undef CHECK_IAABS                  /* Check IAABS optimization      */

#define IBUF                        /* inst. prefetch and buffering  */
#define IBUF_FASTOP                 /* RR, RX Fastexecution          */
#define INLINE_GET                  /* inline code for ibuf.h        */
#define INLINE_INVALIDATE           /* inline code for ibuf.h        */
#undef  IBUF_STAT                   /* IBUF statistics               */
#define IBUF_SWITCH                 /* Switch IBUF dynamically on/off*/
#undef  CHECK_FRAGPARMS             /* check compiled operation parms*/
#undef  CHECK_FRAGADDRESS           /* check inst.addr in comp. op   */
#undef DO_INSTFETCH                /* Do allways instfetch          */
#undef  CHECK_PTR                   /* check fragment/entry pointer  */
#undef  DEBUGMSG                    /* Write debugmessages           */
#undef  FLUSHLOG                    /* use logfile with flush        */

#define FEATURE_OPTIMIZE_SAME_PAGE  /* fast address translation      */
#undef  CHECK_PAGEADDR              /* check fast address translation*/

#undef  FEATURE_WATCHPOINT          /* watchpoint for phys. addr     */
#undef  INSTSTAT                    /* instruction stat              */
#endif
#else /* No EXPERIMENTAL */
#define INLINE_LOGICAL               /* inline code for dat.c         */
#define INLINE_IFETCH                /* inline code for dat.c         */
#define INLINE_VFETCH                /* inline code for dat.c         */
#define INLINE_VSTORE                /* inline code for dat.c         */
#define INTERRUPT_OPTIMIZE          /* enable check for doint flag   */
#undef  TRACE_INTERRUPT_DELAY       /* trace missing interrups       */

#undef  OPTIMIZE_IAABS               /* IAABS optimization            */
#undef  CHECK_IAABS                  /* Check IAABS optimization      */


#undef IBUF                         /* inst. prefetch and buffering  */
#undef IBUF_FASTOP                 /* RR, RX Fastexecution          */
#undef INLINE_GET                   /* inline code for ibuf.h        */
#undef INLINE_INVALIDATE            /* inline code for ibuf.h        */
#undef IBUF_STAT                   /* IBUF statistics               */
#undef IBUF_SWITCH                  /* Switch IBUF dynamically on/off*/
#undef CHECK_FRAGPARMS              /* check compiled operation parms*/
#undef CHECK_FRAGADDRESS            /* check inst.addr in comp. op   */
#undef DO_INSTFETCH                 /* Do allways instfetch          */
#undef CORRECT_FRAGADDRESS          /* check and correct inst. adr   */
#undef CHECK_PTR                    /* check fragment/entry pointer  */
#undef  DEBUGMSG                    /* Write debugmessages           */
#undef FLUSHLOG                     /* use logfile with flush        */

#define FEATURE_OPTIMIZE_SAME_PAGE   /* fast address translation      */
#undef  CHECK_PAGEADDR              /* check fast address translation*/

#undef FEATURE_WATCHPOINT           /* watchpoint for phys. addr     */
#endif

#undef LASTINST
#ifdef CHECK_FRAGADDRESS
#define LASTINST
#else
#ifdef CHECK_PAGEADDR
#define LASTINST
#else
#ifdef FEATURE_WATCHPOINT
#define LASTINST
#endif
#endif
#endif
/*-------------------------------------------------------------------*/
/* Windows 32-specific definitions                                   */
/*-------------------------------------------------------------------*/
#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef WIN32
#define socklen_t int
/* fake loading of windows.h and winsock.h so we can use             */
/* pthreads-win32 instead of the native gygwin pthreads support,     */
/* which doesn't include pthread_cond bits                           */
#define _WINDOWS_
#define _WINSOCKAPI_
#define _WINDOWS_H
#define _WINSOCK_H
#define HANDLE int
#undef CLK_TCK
#define CLK_TCK 100	/* fix timer tick rate */
#endif

/*-------------------------------------------------------------------*/
/* ESA/390 features implemented 				     */
/*-------------------------------------------------------------------*/
#define FEATURE_ALD_FORMAT	0
#undef	FEATURE_ACCESS_REGISTERS
#undef	FEATURE_BASIC_STORAGE_KEYS
#undef	FEATURE_BCMODE
#undef	FEATURE_BIMODAL_ADDRESSING
#undef	FEATURE_BINARY_FLOATING_POINT
#undef	FEATURE_BRANCH_AND_SET_AUTHORITY
#undef	FEATURE_BROADCASTED_PURGING
#undef  FEATURE_CALLED_SPACE_IDENTIFICATION
#undef	FEATURE_CHANNEL_SUBSYSTEM
#undef	FEATURE_CHECKSUM_INSTRUCTION
#undef	FEATURE_COMPARE_AND_MOVE_EXTENDED
#undef	FEATURE_CPU_RECONFIG
#undef	FEATURE_DIRECT_CONTROL
#undef	FEATURE_DUAL_ADDRESS_SPACE
#undef	FEATURE_EXPANDED_STORAGE
#undef	FEATURE_EXTENDED_STORAGE_KEYS
#undef	FEATURE_EXTENDED_TOD_CLOCK
#undef	FEATURE_EXTENDED_TRANSLATION
#undef	FEATURE_FETCH_PROTECTION_OVERRIDE
#undef	FEATURE_HEXADECIMAL_FLOATING_POINT
#undef	FEATURE_HYPERVISOR
#undef	FEATURE_IMMEDIATE_AND_RELATIVE
#undef	FEATURE_INTERPRETIVE_EXECUTION
#undef	FEATURE_INTERVAL_TIMER
#undef	FEATURE_LINKAGE_STACK
#undef	FEATURE_LOCK_PAGE
#undef	FEATURE_MOVE_PAGE_FACILITY_2
#undef	FEATURE_MSSF_CALL
#undef  FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE
#undef	FEATURE_MVS_ASSIST
#undef	FEATURE_PAGE_PROTECTION
#undef	FEATURE_PER2
#undef	FEATURE_PRIVATE_SPACE
#undef	FEATURE_S370_CHANNEL
#undef	FEATURE_S390_DAT
#undef	FEATURE_SEGMENT_PROTECTION
#undef	FEATURE_SQUARE_ROOT
#undef  FEATURE_STORAGE_KEY_ASSIST
#undef	FEATURE_STORAGE_PROTECTION_OVERRIDE
#undef	FEATURE_STORE_SYSTEM_INFORMATION
#undef	FEATURE_SUBSPACE_GROUP
#undef	FEATURE_SUPPRESSION_ON_PROTECTION
#undef	FEATURE_SYSTEM_CONSOLE
#undef	FEATURE_TRACING
#undef	FEATURE_VECTOR_FACILITY
#undef	FEATURE_4K_STORAGE_KEYS
#undef	FEATURE_HERCULES_DIAGCALLS
#undef	FEATURE_EMULATE_VM
#undef  FEATURE_CMPSC
#undef  FEATURE_PLO

#if	ARCH == 370
 #define ARCHITECTURE_NAME	"S/370"
 #define MAX_CPU_ENGINES	1
 #define FEATURE_BASIC_STORAGE_KEYS
 #define FEATURE_BCMODE
 #define FEATURE_HEXADECIMAL_FLOATING_POINT
 #define FEATURE_INTERVAL_TIMER
 #define FEATURE_SEGMENT_PROTECTION
 #define FEATURE_SYSTEM_CONSOLE
 #define FEATURE_S370_CHANNEL
 #define FEATURE_HERCULES_DIAGCALLS
 #define FEATURE_EMULATE_VM
#elif	ARCH == 390
 #define ARCHITECTURE_NAME	"ESA/390"
 #define MAX_CPU_ENGINES	6
 #define FEATURE_ACCESS_REGISTERS
 #define FEATURE_BIMODAL_ADDRESSING
 #define FEATURE_BRANCH_AND_SET_AUTHORITY
 #define FEATURE_BROADCASTED_PURGING
 #define FEATURE_CALLED_SPACE_IDENTIFICATION
 #define FEATURE_CHANNEL_SUBSYSTEM
 #define FEATURE_CHECKSUM_INSTRUCTION
 #define FEATURE_COMPARE_AND_MOVE_EXTENDED
 #define FEATURE_CPU_RECONFIG
 #define FEATURE_DUAL_ADDRESS_SPACE
 #define FEATURE_EXPANDED_STORAGE
 #define FEATURE_EXTENDED_STORAGE_KEYS
 #define FEATURE_EXTENDED_TOD_CLOCK
 #define FEATURE_EXTENDED_TRANSLATION
 #define FEATURE_FETCH_PROTECTION_OVERRIDE
 #define FEATURE_HEXADECIMAL_FLOATING_POINT
 #define FEATURE_HYPERVISOR
 #define FEATURE_IMMEDIATE_AND_RELATIVE
 #define FEATURE_INTERPRETIVE_EXECUTION
 #define FEATURE_LOCK_PAGE
 #define FEATURE_LINKAGE_STACK
 #undef  FEATURE_MOVE_PAGE_FACILITY_2
 #define FEATURE_MSSF_CALL
 #define FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE
 #define FEATURE_MVS_ASSIST
 #define FEATURE_PAGE_PROTECTION
 #define FEATURE_PER2
 #define FEATURE_PRIVATE_SPACE
 #define FEATURE_S390_DAT
 #define FEATURE_SEGMENT_PROTECTION
 #define FEATURE_SQUARE_ROOT
 #define FEATURE_STORAGE_KEY_ASSIST
 #define FEATURE_STORAGE_PROTECTION_OVERRIDE
 #define FEATURE_STORE_SYSTEM_INFORMATION
 #define FEATURE_SUBSPACE_GROUP
 #define FEATURE_SUPPRESSION_ON_PROTECTION
 #define FEATURE_SYSTEM_CONSOLE
 #define FEATURE_TRACING
 #define FEATURE_VECTOR_FACILITY
 #define FEATURE_4K_STORAGE_KEYS
 #define FEATURE_HERCULES_DIAGCALLS
 #define FEATURE_EMULATE_VM
 #define FEATURE_CMPSC
 #define FEATURE_PLO
#else
 #error Either ARCH=370 or ARCH=390 must be specified
#endif


/*-------------------------------------------------------------------*/
/* ESA/390 data structures					     */
/*-------------------------------------------------------------------*/
#include "esa390.h"


/*-------------------------------------------------------------------*/
/* Footprint trace buffer size, must be a power of 2                 */
/*-------------------------------------------------------------------*/
// #define FOOTPRINT_BUFFER        4096


/*-------------------------------------------------------------------*/
/* Macro definitions for storage keys				     */
/*-------------------------------------------------------------------*/
#ifdef FEATURE_4K_STORAGE_KEYS
 #define STORAGE_KEY(absaddr) sysblk.storkeys[(absaddr)>>12]
 #define STORAGE_KEY_PAGESHIFT	12
 #define STORAGE_KEY_PAGESIZE	4096
 #define STORAGE_KEY_PAGEMASK	0x7FFFF000
 #define STORAGE_KEY_BYTEMASK	0x00000FFF
#else
 #define STORAGE_KEY(absaddr) sysblk.storkeys[(absaddr)>>11]
 #define STORAGE_KEY_PAGESHIFT	11
 #define STORAGE_KEY_PAGESIZE	2048
 #define STORAGE_KEY_PAGEMASK	0x7FFFF800
 #define STORAGE_KEY_BYTEMASK	0x000007FF
#endif

/*-------------------------------------------------------------------*/
/* Macro definitions for expanded storage			     */
/*-------------------------------------------------------------------*/
#define XSTORE_INCREMENT_SIZE	0x00100000
#define XSTORE_PAGESHIFT	12
#define XSTORE_PAGESIZE 	4096
#define XSTORE_PAGEMASK 	0x7FFFF000
#if defined(FEATURE_EXPANDED_STORAGE) \
    && !defined(FEATURE_4K_STORAGE_KEYS)
 #error Expanded storage cannot be defined with 2K storage keys
#endif
#if defined(FEATURE_MOVE_PAGE_FACILITY_2) \
    && !defined(FEATURE_4K_STORAGE_KEYS)
 #error Move page facility cannot be defined with 2K storage keys
#endif

/*-------------------------------------------------------------------*/
/* Macro definitions for address wraparound			     */
/*-------------------------------------------------------------------*/
#ifdef FEATURE_BIMODAL_ADDRESSING
 #define ADDRESS_MAXWRAP(_register_context) \
	 ((_register_context)->psw.amode ? 0x7FFFFFFF : 0x00FFFFFF)
#else
 #define ADDRESS_MAXWRAP(_register_context) \
	 (0x00FFFFFF)
#endif

/*-------------------------------------------------------------------*/
/* Macro definitions for tracing				     */
/*-------------------------------------------------------------------*/
#ifndef FLUSHLOG
#define logmsg(a...) \
        { \
	fprintf(sysblk.msgpipew, a); \
        }
#else
#define logmsg(a...) \
        { \
	fprintf(sysblk.msgpipew, a); \
        fflush(sysblk.msgpipew); \
        }
#endif
#define DEVTRACE(format, a...) \
	if(dev->ccwtrace||dev->ccwstep) \
	fprintf(sysblk.msgpipew, "%4.4X:" format, dev->devnum, a)

/*-------------------------------------------------------------------*/
/* Macro definitions for version number 			     */
/*-------------------------------------------------------------------*/
#define STRINGMAC(x)	#x
#define MSTRING(x)	STRINGMAC(x)

/*-------------------------------------------------------------------*/
/* Macro definitions for thread functions			     */
/*-------------------------------------------------------------------*/
#ifndef NOTHREAD
#ifdef WIN32
#define DWORD int	/* will be undefined later */
#endif
#include <pthread.h>
#ifdef WIN32
#undef DWORD
#endif
typedef pthread_t			TID;
typedef pthread_mutex_t 		LOCK;
typedef pthread_cond_t			COND;
typedef pthread_attr_t			ATTR;
#define initialize_lock(plk) \
	pthread_mutex_init((plk),NULL)
#define obtain_lock(plk) \
	pthread_mutex_lock((plk))
#if MAX_CPU_ENGINES == 1
 #define OBTAIN_MAINLOCK(_register_context)
 #define RELEASE_MAINLOCK(_register_context)
#else
 #define OBTAIN_MAINLOCK(_register_context) \
	{ \
	    pthread_mutex_lock(&sysblk.mainlock); \
	    (_register_context)->mainlock = 1; \
	}
 #define RELEASE_MAINLOCK(_register_context) \
	{ \
	    (_register_context)->mainlock = 0; \
	    pthread_mutex_unlock(&sysblk.mainlock); \
	}
#endif
#define release_lock(plk) \
	pthread_mutex_unlock((plk))
#define initialize_condition(pcond) \
	pthread_cond_init((pcond),NULL)
#define signal_condition(pcond) \
	pthread_cond_broadcast((pcond))
#define wait_condition(pcond,plk) \
	pthread_cond_wait((pcond),(plk))
#define initialize_detach_attr(pat) \
	pthread_attr_init((pat)); \
	pthread_attr_setdetachstate((pat),PTHREAD_CREATE_DETACHED)
typedef void*THREAD_FUNC(void*);
#define create_thread(ptid,pat,fn,arg) \
	pthread_create(ptid,pat,(THREAD_FUNC*)&fn,arg)
#define signal_thread(tid,signo) \
	pthread_kill(tid,signo)
#define thread_id() \
	pthread_self()
#define wait_timed_condition(pcond,plk,tm) \
	pthread_cond_timedwait((pcond),(plk),(tm))
#else
typedef int				TID;
typedef int				LOCK;
typedef int				COND;
typedef int				ATTR;
#define initialize_lock(plk)		*(plk)=0
#define obtain_lock(plk)		*(plk)=1
#define release_lock(plk)		*(plk)=0
#define initialize_condition(pcond)	*(pcond)=0
#define signal_condition(pcond) 	*(pcond)=1
#define wait_condition(pcond,plk)	*(pcond)=1
#define initialize_detach_attr(pat)	*(pat)=1
#define create_thread(ptid,pat,fn,arg)	(*(ptid)=0,fn(arg),0)
#define signal_thread(tid,signo)	raise(signo)
#define thread_id()			0
#define wait_timed_condition(pcond,plk,tm) *(pcond)=1
#define OBTAIN_MAINLOCK(_register_context)
#define RELEASE_MAINLOCK(_register_context)
#endif

/*-------------------------------------------------------------------*/
/* Prototype definitions for device handler functions		     */
/*-------------------------------------------------------------------*/
struct _DEVBLK;
typedef int DEVIF (struct _DEVBLK *dev, int argc, BYTE *argv[]);
typedef void DEVQF (struct _DEVBLK *dev, BYTE **class, int buflen,
	BYTE *buffer);
typedef void DEVXF (struct _DEVBLK *dev, BYTE code, BYTE flags,
	BYTE chained, U16 count, BYTE prevcode, int ccwseq,
	BYTE *iobuf, BYTE *more, BYTE *unitstat, U16 *residual);
typedef int DEVCF (struct _DEVBLK *dev);

/*-------------------------------------------------------------------*/
/* Structure definition for the Vector Facility 		     */
/*-------------------------------------------------------------------*/
#ifdef FEATURE_VECTOR_FACILITY
typedef struct _VFREGS {		/* Vector Facility Registers*/
        unsigned int
                online:1;               /* 1=VF is online            */
        U64     vsr;                    /* Vector Status Register    */
        U64     vac;                    /* Vector Activity Count     */
        BYTE    vmr[VECTOR_SECTION_SIZE/8];  /* Vector Mask Register */
        U32     vr[16][VECTOR_SECTION_SIZE]; /* Vector Registers     */
    } VFREGS;
#endif /*FEATURE_VECTOR_FACILITY*/

/*-------------------------------------------------------------------*/
/* Structure definition for page translation optimization	     */
/*-------------------------------------------------------------------*/
#ifdef FEATURE_OPTIMIZE_SAME_PAGE
typedef struct _LASTPAGE {		/* Vector Facility Registers*/
        int     valid;
        U32     vaddr;
        int     arn;
        U32     aaddr;
    } LASTPAGE;

#define LASTPAGE_INVALIDATE(_r) { \
           int i; \
           for (i = 0; i < 3; i++) \
               regs->lastpage[i].valid = 0; \
           }
#else
#define LASTPAGE_INVALIDATE(_r)
#endif /*FEATURE_OPTIMIZE_SAME_PAGE*/


/*-------------------------------------------------------------------*/
/* Definition for instruction buffering                              */
/*-------------------------------------------------------------------*/
#ifdef CHECK_FRAGADDRESS
#define SAVE_FILE \
         { \
             strcpy(regs->file, __FILE__); \
             regs->line = __LINE__; \
         }
#else
#define SAVE_FILE { }
#endif

#ifdef IBUF
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
#define REASSIGN_FRAG(_r) { \
        if (!(_r)->psw.wait && !(_r)->sie_state) \
            { \
                SAVE_FILE; \
                ibuf_assign_fragment((_r), (_r)->psw.ia); \
            } \
        }

#define GET_FRAGENTRY(_r) { \
        if (!(_r)->psw.wait && !(_r)->sie_state) \
            { \
            SAVE_FILE; \
            ibuf_get_fragentry((_r), (_r)->psw.ia); \
            } \
        }
#else
#define REASSIGN_FRAG(_r) { \
        if (!(_r)->psw.wait) \
            { \
                SAVE_FILE; \
                ibuf_assign_fragment((_r), (_r)->psw.ia); \
            } \
        }

#define GET_FRAGENTRY(_r) { \
        if (!(_r)->psw.wait) \
            { \
            SAVE_FILE; \
            ibuf_get_fragentry((_r), (_r)->psw.ia); \
            } \
        }
#endif

#ifdef CHECK_PTR
#define CHECK_FRAGPTR(_r, _s) \
        { \
            if (!_r->actentry) \
            { \
                logmsg("actentry = NULL %llu %s\n", _r->instcount, _s); \
                GET_FRAGENTRY(_r); \
                if (!_r->actentry) \
                    logmsg("actentry = NULL aft get %s\n", _s); \
            } \
        }
#else
#define CHECK_FRAGPTR(_r, _s)
#endif
#define LOAD_INST(_r) ibuf_loadinst(_r)
#else
#define REASSIGN_FRAG(_r)
#define GET_FRAGENTRY(_r)
#define CHECK_FRAGPTR(_r, _s)
#define LOAD_INST(_r)
#endif

#ifdef DEBUGMSG
#define debugmsg(a...) \
        { \
            if (regs->debugmsg) \
            { \
                if (regs->actpage) \
                    logmsg("EXECUTE %llu ", regs->instcount) \
                else \
                    logmsg("INTERPRET %llu ", regs->instcount); \
                logmsg(a); \
            } \
        }
#else
#define debugmsg(a...)
#endif

/*-------------------------------------------------------------------*/
/* Definition for watchpoints                                        */
/*-------------------------------------------------------------------*/
#ifdef FEATURE_WATCHPOINT
#define WATCH(_s) \
     { \
           REGS *regs; \
           BYTE *ptr; \
           regs = &sysblk.regs[0]; \
           ptr = (sysblk.mainstor+regs->watchpoint); \
           if (*ptr != regs->oldvalue) \
           { \
              logmsg("watchpoint %s %4x %x %x \n",  \
                      _s, \
                      regs->watchpoint, \
                      regs->oldvalue, *ptr); \
              regs->oldvalue = *ptr; \
           } \
       };
#else
#define WATCH(_s)
#endif
/*-------------------------------------------------------------------*/
/* Structure definition for CPU register context		     */
/*-------------------------------------------------------------------*/
typedef struct _REGS {			/* Processor registers	     */
	U64	ptimer; 		/* CPU timer		     */
	U64	clkc;			/* 0-7=Clock comparator epoch,
					   8-63=Comparator bits 0-55 */
        S64     todoffset;              /* TOD offset for this CPU   */
	U64	instcount;		/* Instruction counter	     */
	U64	prevcount;		/* Previous instruction count*/
	U32	mipsrate;		/* Instructions/millisecond  */
	U32	siocount;		/* SIO/SSCH counter          */
	U32	siosrate;		/* IOs per second            */
	TLBE	tlb[256];		/* Translation lookaside buf */
	TID	cputid; 		/* CPU thread identifier     */
	U32	gpr[16];		/* General purpose registers */
	U32	cr[16]; 		/* Control registers	     */
	U32	ar[16]; 		/* Access registers	     */
	U32	fpr[8]; 		/* Floating point registers  */
	U32	pxr;			/* Prefix register	     */
	U32	todpr;			/* TOD programmable register */
	U32	tea;			/* Translation exception addr*/
        U32     moncode;                /* Monitor event code        */
        U16     monclass;               /* Monitor event class       */
	U16	cpuad;			/* CPU address for STAP      */
	PSW	psw;			/* Program status word	     */
	BYTE	excarid;		/* Exception access register */
        DWORD   exinst;                 /* Target of Execute (EX)    */

        U32     mainsize;               /* Central Storage size or   */
                                        /* guest storage size (SIE)  */
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
        U32     sie_state;              /* Address of the SIE state
                                           descriptor block or 0 when
                                           not running under SIE     */
        SIEBK  *siebk;                  /* Sie State Desc structure  */
        struct _REGS *hostregs;         /* Pointer to the hypervisor
                                           register context          */
        struct _REGS *guestregs;        /* Pointer to the guest
                                           register context          */
        PSA    *sie_psa;                /* PSA of guest CPU          */
        U32     sie_mso;                /* Main Storage Origin       */
        U32     sie_xso;                /* eXpanded Storage Origin   */
        U32     sie_xsl;                /* eXpanded Storage Limit    */
        U32     sie_rcpo;               /* Ref and Change Preserv.   */
        U32     sie_scao;               /* System Contol Area        */
        S64     sie_epoch;              /* TOD offset in state desc. */
        unsigned int
                sie_active:1,           /* Sie active (host only)    */
                sie_pref:1;             /* Preferred-storage mode    */
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/

	BYTE	cpustate;		/* CPU stopped/started state */
	unsigned int			/* Flags		     */
		cpuonline:1,		/* 1=CPU is online           */
		cpuint:1,		/* 1=There is an interrupt
					     pending for this CPU    */
		itimer_pending:1,	/* 1=Interrupt is pending for
					     the interval timer      */
		restart:1,		/* 1=Restart interrpt pending*/
		extcall:1,		/* 1=Extcall interrpt pending*/
		emersig:1,		/* 1=Emersig interrpt pending*/
		ptpend:1,		/* 1=CPU timer int pending   */
		ckpend:1,               /* 1=Clock comp int pending  */
		storstat:1,		/* 1=Stop and store status   */
		sigpreset:1,		/* 1=SIGP cpu reset received */
		sigpireset:1,		/* 1=SIGP initial cpu reset  */
		instvalid:1;		/* 1=Inst field is valid     */
	BYTE	emercpu 		/* Emergency signal flags    */
		    [MAX_CPU_ENGINES];	/* for each CPU (1=pending)  */
	U16	extccpu;		/* CPU causing external call */
	BYTE	inst[6];		/* Last-fetched instruction  */
#if MAX_CPU_ENGINES > 1
	U32	brdcstpalb;		/* purge_alb() pending	     */
	U32	brdcstptlb;		/* purge_tlb() pending	     */
	unsigned int			/* Flags		     */
		mainlock:1;		/* MAINLOCK held indicator   */
#ifdef SMP_SERIALIZATION
		/* Locking and unlocking the serialisation lock causes
		   the processor cache to be flushed this is used to
		   mimic the S/390 serialisation operation.  To avoid
		   contention, each S/390 CPU has its own lock	     */
	LOCK	serlock;		/* Serialization lock	     */
#endif /*SMP_SERIALIZATION*/
#endif /*MAX_CPU_ENGINES > 1*/

#ifdef FEATURE_VECTOR_FACILITY
        VFREGS *vf;                     /* Vector Facility           */
#endif /*FEATURE_VECTOR_FACILITY*/

	jmp_buf progjmp;		/* longjmp destination for
					   program check return      */
	U64	int1count;		
	U64	int2count;		
	U64	int3count;

#ifdef FEATURE_WATCHPOINT
        U32     watchpoint;
        BYTE    oldvalue;
#endif
        int     doint;

        U32     iaabs;

#ifdef DEBUGMSG
        BYTE    debugmsg;
#endif

#ifdef IBUF
#ifdef IBUF_STAT
        U64     ibufrecompile;
        U64     ibufrecompilestorage;
        U64     ibufrecompiledisk;
        U64     ibufexecute;		
        U64     ibufinterpret;	
        U64     ibufcodechange;	
        U64     ibufexeinst;
        U64     ibufget;
        U64     ibufassign;
        U64     ibufexeassign;
        U64     ibufinvalidate;
        U64     ibufinvalidatex;
        U64     ibuflow;
        U64     ibufhigh;
        U64     ibufdif;
        U64     ibufoverflow;
        BYTE*   fraginvalid;
#endif
        void*   fragbuffer;
        void*   actentry;
        void**  actpage;
        void**  dict;
        U64*    icount;
        BYTE*    fragvalid;

#ifdef LASTINST
        BYTE    lastinst[3];
#endif
#endif

#ifdef CHECK_FRAGADDRESS
        char file[40];
        int  line;
#endif

#ifdef FEATURE_OPTIMIZE_SAME_PAGE
        LASTPAGE lastpage[3];
#endif
#ifdef INSTSTAT
        U64     instcountx[256];
#endif
    } REGS;

/* Definitions for CPU state */
#define CPUSTATE_STOPPED	0	/* CPU is stopped	     */
#define CPUSTATE_STOPPING	1	/* CPU is stopping	     */
#define CPUSTATE_STARTED	2	/* CPU is started	     */
#define CPUSTATE_STARTING       3	/* CPU is starting           */

/*-------------------------------------------------------------------*/
/* System configuration block					     */
/*-------------------------------------------------------------------*/
typedef struct _SYSBLK {
	U32	mainsize;		/* Main storage size (bytes) */
	BYTE   *mainstor;		/* -> Main storage	     */
	BYTE   *storkeys;		/* -> Main storage key array */
	U32	xpndsize;		/* Expanded size (4K pages)  */
	BYTE   *xpndstor;		/* -> Expanded storage	     */
	U64	cpuid;			/* CPU identifier for STIDP  */
	U64	todclk; 		/* 0-7=TOD clock epoch,
					   8-63=TOD clock bits 0-55  */
	S64	todoffset;		/* Difference in microseconds
					   between TOD and Unix time */
#ifdef TODCLOCK_DRAG_FACTOR
	U64	todclock_init;		/* TOD clock value at start  */
#endif /* TODCLOCK_DRAG_FACTOR */
	LOCK	todlock;		/* TOD clock update lock     */
	TID	todtid; 		/* Thread-id for TOD update  */
	BYTE	loadparm[8];		/* IPL load parameter	     */
	U16	numcpu; 		/* Number of CPUs installed  */
	REGS	regs[MAX_CPU_ENGINES];	/* Registers for each CPU    */
#ifdef FEATURE_VECTOR_FACILITY
        VFREGS  vf[MAX_CPU_ENGINES];    /* Vector Facility           */
#endif /*FEATURE_VECTOR_FACILITY*/
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
        REGS    sie_regs[MAX_CPU_ENGINES];  /* SIE copy of regs      */
#endif /*defined(FEATURE_INTERPRETIVE_EXECUTION)*/
#if defined(FOOTPRINT_BUFFER)
        REGS    footprregs[MAX_CPU_ENGINES][FOOTPRINT_BUFFER];
        U32     footprptr[MAX_CPU_ENGINES];
#endif
	LOCK	mainlock;		/* Main storage lock	     */
	COND	intcond;		/* Interrupt condition	     */
	LOCK	intlock;		/* Interrupt lock	     */
	LOCK	sigplock;		/* Signal processor lock     */
	ATTR	detattr;		/* Detached thread attribute */
	TID	cnsltid;		/* Thread-id for console     */
	U16	cnslport;		/* Port number for console   */
	U32	mbo;			/* Measurement block origin  */
	BYTE	mbk;			/* Measurement block key     */
	int	mbm;			/* Measurement block mode    */
	int	mbd;			/* Device connect time mode  */
	int	toddrag;		/* TOD clock drag factor     */
        int     panrate;                /* Panel refresh rate        */
	struct _DEVBLK *firstdev;	/* -> First device block     */
	U16	highsubchan;		/* Highest subchannel + 1    */
        U32     addrlimval;             /* Address limit value (SAL) */
	U32	servparm;		/* Service signal parameter  */
	U32	cp_recv_mask;		/* Syscons CP receive mask   */
	U32	cp_send_mask;		/* Syscons CP send mask      */
	U32	sclp_recv_mask; 	/* Syscons SCLP receive mask */
	U32	sclp_send_mask; 	/* Syscons SCLP send mask    */
	BYTE	scpcmdstr[123+1];	/* Operator command string   */
	int	scpcmdtype;		/* Operator command type     */
	unsigned int			/* Flags		     */
		iopending:1,		/* 1=I/O interrupt pending   */
		mckpending:1,		/* 1=MCK interrupt pending   */
		extpending:1,		/* 1=EXT interrupt pending   */
		crwpending:1,		/* 1=Channel report pending  */
		sigpbusy:1,		/* 1=Signal facility in use  */
		servsig:1,		/* 1=Service signal pending  */
		intkey:1,		/* 1=Interrupt key pending   */
		sigintreq:1,		/* 1=SIGINT request pending  */
		insttrace:1,		/* 1=Instruction trace	     */
		inststep:1,		/* 1=Instruction step	     */
		instbreak:1;		/* 1=Have breakpoint	     */
#if MAX_CPU_ENGINES > 1
	U32	brdcstpalb;		/* purge_alb() pending	     */
	U32	brdcstptlb;		/* purge_tlb() pending	     */
	int	brdcstncpu;		/* number of CPUs waiting    */
	COND	brdcstcond;		/* Broadcast condition	     */
#endif /*MAX_CPU_ENGINES > 1*/
	U32	breakaddr;		/* Breakpoint address	     */
	FILE   *msgpipew;		/* Message pipe write handle */
	int	msgpiper;		/* Message pipe read handle  */
        U64     pgminttr;               /* Program int trace mask    */
        int     pcpu;                   /* Tgt CPU panel cmd & displ */
        int     doinst;
    } SYSBLK;

/* Definitions for OS tailoring - msb eq mon event, lsb eq oper exc. */
#define OS_NONE 	0x7FFFFFFFF7DE7FFFULL	/* No spec OS tail.  */
#define OS_OS390	0x7FF77BFFF7DE7FFFULL   /* OS/390            */
#define OS_VM		0x7FFFFFFFF7DE7FFCULL   /* VM	             */
#define OS_LINUX	0x7FFFFFFFF7DE7FD6ULL   /* Linux	     */

/*-------------------------------------------------------------------*/
/* Device configuration block					     */
/*-------------------------------------------------------------------*/
typedef struct _DEVBLK {
	U16	subchan;		/* Subchannel number	     */
	U16	devnum; 		/* Device number	     */
	U16	devtype;		/* Device type		     */
	DEVIF  *devinit;		/* -> Init device function   */
	DEVQF  *devqdef;		/* -> Query device function  */
	DEVXF  *devexec;		/* -> Execute CCW function   */
	DEVCF  *devclos;		/* -> Close device function  */
	LOCK	lock;			/* Device block lock	     */
	COND	resumecond;		/* Resume condition	     */
	COND	loopercond;		/* Loop or die condition     */
	int	loopercmd;		/* Loop or die command       */
	struct _DEVBLK *nextdev;	/* -> next device block      */
	unsigned int			/* Flags		     */
		pending:1,		/* 1=Interrupt pending	     */
		busy:1, 		/* 1=Device busy	     */
		console:1,		/* 1=Console device	     */
		connected:1,		/* 1=Console client connected*/
		readpending:2,		/* 1=Console read pending    */
		pcipending:1,		/* 1=PCI interrupt pending   */
		ccwtrace:1,		/* 1=CCW trace		     */
		ccwstep:1,		/* 1=CCW single step	     */
		cdwmerge:1,		/* 1=Channel will merge data
					     chained write CCWs      */
		crwpending:1;		/* 1=CRW pending	     */
	PMCW	pmcw;			/* Path management ctl word  */
	SCSW	scsw;			/* Subchannel status word(XA)*/
	SCSW	pciscsw;		/* PCI subchannel status word*/
	BYTE	csw[8]; 		/* Channel status word(S/370)*/
	BYTE	pcicsw[8];		/* PCI channel status word   */
	ESW	esw;			/* Extended status word      */
	BYTE	ecw[32];		/* Extended control word     */
	int	numsense;		/* Number of sense bytes     */
	BYTE	sense[32];		/* Sense bytes		     */
	int	numdevid;		/* Number of device id bytes */
	BYTE	devid[32];		/* Device identifier bytes   */
	int	numdevchar;		/* Number of devchar bytes   */
	BYTE	devchar[64];		/* Device characteristics    */
	BYTE	pgid[11];		/* Path Group ID	     */
	TID	tid;			/* Thread-id executing CCW   */
	U32	ccwaddr;		/* Address of first CCW      */
	int	ccwfmt; 		/* CCW format (0 or 1)	     */
	BYTE	ccwkey; 		/* Bits 0-3=key, 4-7=zeroes  */
	BYTE   *buf;			/* -> Device data buffer     */
	int	bufsize;		/* Device data buffer size   */
	BYTE	filename[256];		/* Unix file name	     */
	int	fd;			/* File desc / socket number */
	/* Device dependent fields for console */
	struct	in_addr ipaddr; 	/* Client IP address	     */
	int	rlen3270;		/* Length of data in buffer  */
	int	pos3270;		/* Current screen position   */
	BYTE   	aid3270;		/* Current input AID value   */
	BYTE	mod3270;		/* 3270 model number	     */
	unsigned int			/* Flags		     */
		eab3270:1;		/* 1=Extended attributes     */
	int	keybdrem;		/* Number of bytes remaining
					   in keyboard read buffer   */
	/* Device dependent fields for cardrdr */
	unsigned int			/* Flags		     */
		rdreof:1,		/* 1=Unit exception at EOF   */
		ebcdic:1,		/* 1=Card deck is EBCDIC     */
		ascii:1,		/* 1=Convert ASCII to EBCDIC */
		trunc:1;		/* Truncate overlength record*/
	int	cardpos;		/* Offset of next byte to be
					   read from data buffer     */
	int	cardrem;		/* Number of bytes remaining
					   in data buffer	     */
	/* Device dependent fields for ctcadpt */
	unsigned int			/* Flags		     */
		ctcxmode:1;		/* 0=Basic mode, 1=Extended  */
	BYTE	ctctype;		/* CTC_xxx device type	     */
	struct _DEVBLK *ctcpair;	/* -> Paired device block    */
	int	ctcpos;			/* next byte offset	     */
	int	ctcrem;			/* bytes remaining in buffer */
	int	ctclastpos;		/* last packet read	     */
	int	ctclastrem;		/* last packet read	     */
	/* Device dependent fields for printer */
	unsigned int			/* Flags		     */
		crlf:1, 		/* 1=CRLF delimiters, 0=LF   */
		diaggate:1,		/* 1=Diagnostic gate command */
		fold:1; 		/* 1=Fold to upper case      */
	int	printpos;		/* Number of bytes already
					   placed in print buffer    */
	int	printrem;		/* Number of bytes remaining
					   in print buffer	     */
	/* Device dependent fields for tapedev */
	unsigned int			/* Flags		     */
		readonly:1;		/* 1=Tape is write-protected */
	struct				/* HET device parms	     */
	{
		U16	compress:1;	/* 1=Compression enabled     */
		U16	method:3;	/* Compression method	     */
		U16	level:4;	/* Compression level	     */
		U16	chksize;	/* Chunk size                */
	}	tdparms;		/* HET device parms	     */
	HETB	*hetb;			/* HET control block	     */
	BYTE	tapedevt;		/* Tape device type	     */
	void   *omadesc;		/* -> OMA descriptor array   */
	U16	omafiles;		/* Number of OMA tape files  */
	U16	curfilen;		/* Current file number	     */
	long	nxtblkpos;		/* Offset from start of file
					   to next block	     */
	long	prvblkpos;		/* Offset from start of file
					   to previous block	     */
	U16	curblkrem;		/* Number of bytes unread
					   from current block	     */
	U16	curbufoff;		/* Offset into buffer of data
					   for next data chained CCW */
	long	blockid;		/* Current device block ID   */
	/* Device dependent fields for fbadasd */
	unsigned int			/* Flags		     */
		fbaxtdef:1;		/* 1=Extent defined	     */
	U16	fbablksiz;		/* Physical block size	     */
	U32	fbaorigin;		/* Device origin block number*/
	U32	fbanumblk;		/* Number of blocks in device*/
	BYTE	fbaoper;		/* Locate operation byte     */
	BYTE	fbamask;		/* Define extent file mask   */
	U32	fbaxblkn;		/* Offset from start of device
					   to first block of extent  */
	U32	fbaxfirst;		/* Block number within dataset
					   of first block of extent  */
	U32	fbaxlast;		/* Block number within dataset
					   of last block of extent   */
	U32	fbalcblk;		/* Block number within dataset
					   of first block for locate */
	U16	fbalcnum;		/* Block count for locate    */
	/* Device dependent fields for ckddasd */
	unsigned int			/* Flags		     */
		ckd3990:1,		/* 1=Control unit is 3990    */
		ckdxtdef:1,		/* 1=Define Extent processed */
		ckdsetfm:1,		/* 1=Set File Mask processed */
		ckdlocat:1,		/* 1=Locate Record processed */
		ckdspcnt:1,		/* 1=Space Count processed   */
		ckdseek:1,		/* 1=Seek command processed  */
		ckdskcyl:1,		/* 1=Seek cylinder processed */
		ckdrecal:1,		/* 1=Recalibrate processed   */
		ckdrdipl:1,		/* 1=Read IPL processed      */
		ckdxmark:1,		/* 1=End of track mark found */
		ckdhaeq:1,		/* 1=Search Home Addr Equal  */
		ckdideq:1,		/* 1=Search ID Equal	     */
		ckdkyeq:1,		/* 1=Search Key Equal	     */
		ckdwckd:1,		/* 1=Write R0 or Write CKD   */
		ckdtrkof:1,		/* 1=Track ovfl on this blk  */
		ckdlazywrt:1,		/* 1=Lazy write on trk update*/
		ckdnoftio:1, 		/* 1=No full track i/o       */
		ckdrdonly:1, 		/* 1=Open read only          */
		ckdfakewrt:1; 		/* 1=Fake successful write
					     for read only file      */
	U16	ckdcyls;		/* Number of cylinders	     */
	U16	ckdtrks;		/* Number of tracks	     */
	U16	ckdheads;		/* #of heads per cylinder    */
	U16	ckdtrksz;		/* Track size		     */
	U16	ckdmaxr0len;		/* Maximum length of R0 data */
	U16	ckdmaxr1len;		/* Maximum length of R1 data */
	BYTE	ckdsectors;		/* Number of sectors	     */
	BYTE	ckdfmask;		/* Define extent file mask   */
	BYTE	ckdxgattr;		/* Define extent global attr */
	U16	ckdxblksz;		/* Define extent block size  */
	U16	ckdxbcyl;		/* Define extent begin cyl   */
	U16	ckdxbhead;		/* Define extent begin head  */
	U16	ckdxecyl;		/* Define extent end cyl     */
	U16	ckdxehead;		/* Define extent end head    */
	BYTE	ckdloper;		/* Locate record operation   */
	BYTE	ckdlaux;		/* Locate record aux byte    */
	BYTE	ckdlcount;		/* Locate record count	     */
	U16	ckdltranlf;		/* Locate record transfer
					   length factor	     */
	U16	ckdcurcyl;		/* Current cylinder	     */
	U16	ckdcurhead;		/* Current head 	     */
	BYTE	ckdcurrec;		/* Current record id	     */
	BYTE	ckdcurkl;		/* Current record key length */
	U16	ckdcurdl;		/* Current record data length*/
	BYTE	ckdorient;		/* Current orientation	     */
	BYTE	ckdcuroper;		/* Curr op: read=6, write=5  */
	U16	ckdrem; 		/* #of bytes from current
					   position to end of field  */
	U16	ckdpos; 		/* Offset into buffer of data
					   for next data chained CCW */
	int	ckdnumfd;		/* Number of CKD image files */
	int	ckdfd[CKD_MAXFILES];	/* CKD image file descriptors*/
	U16	ckdlocyl[CKD_MAXFILES]; /* Lowest cylinder number
					   in each CKD image file    */
	U16	ckdhicyl[CKD_MAXFILES]; /* Highest cylinder number
					   in each CKD image file    */
	BYTE   *ckdtrkbuf;		/* Track image buffer        */
	int	ckdtrkfd;		/* Track image fd            */
	int	ckdtrkfn;		/* Track image file nbr      */
	off_t	ckdtrkpos;		/* Track image offset        */
	off_t	ckdcurpos;		/* Current offset            */
	off_t	ckdlopos;		/* Write low offset          */
	off_t	ckdhipos;		/* Write high offset         */
	struct _CKDDASD_CACHE *ckdcache;/* Cache table               */
	int 	ckdcachenbr;		/* Cache table size          */
	int	ckdcachehits;		/* Cache hits                */
	int	ckdcachemisses;		/* Cache misses              */
	BYTE	ckdsfn[256];		/* Shadow file name	     */
	void   *cckd_ext;		/* -> Compressed ckddasd
					   extension otherwise NULL  */
    } DEVBLK;

#define LOOPER_WAIT 0
#define LOOPER_EXEC 1
#define LOOPER_DIE  2

/*-------------------------------------------------------------------*/
/* Structure definitions for CKD headers			     */
/*-------------------------------------------------------------------*/
typedef struct _CKDDASD_DEVHDR {	/* Device header	     */
	BYTE	devid[8];		/* Device identifier	     */
	FWORD	heads;			/* #of heads per cylinder
					   (bytes in reverse order)  */
	FWORD	trksize;		/* Track size (reverse order)*/
	BYTE	devtype;		/* Last 2 digits of device type
					   (0x80=3380, 0x90=3390)    */
	BYTE	fileseq;		/* CKD image file sequence no.
					   (0x00=only file, 0x01=first
					   file of multiple files)   */
	HWORD	highcyl;		/* Highest cylinder number on
					   this file, or zero if this
					   is the last or only file
					   (bytes in reverse order)  */
	BYTE	resv[492];		/* Reserved		     */
    } CKDDASD_DEVHDR;

typedef struct _CKDDASD_TRKHDR {	/* Track header 	     */
	BYTE	bin;			/* Bin number		     */
	HWORD	cyl;			/* Cylinder number	     */
	HWORD	head;			/* Head number		     */
    } CKDDASD_TRKHDR;

typedef struct _CKDDASD_RECHDR {	/* Record header	     */
	HWORD	cyl;			/* Cylinder number	     */
	HWORD	head;			/* Head number		     */
	BYTE	rec;			/* Record number	     */
	BYTE	klen;			/* Key length		     */
	HWORD	dlen;			/* Data length		     */
    } CKDDASD_RECHDR;

typedef struct _CKDDASD_CACHE {		/* Cache entry               */
	int 	trk;			/* Track number              */
	BYTE   *buf;			/* Buffer address            */
	struct timeval	tv;		/* Time last used            */
    } CKDDASD_CACHE;

#define CKDDASD_DEVHDR_SIZE	sizeof(CKDDASD_DEVHDR)
#define CKDDASD_TRKHDR_SIZE	sizeof(CKDDASD_TRKHDR)
#define CKDDASD_RECHDR_SIZE	sizeof(CKDDASD_RECHDR)
#define CKDDASD_CACHE_SIZE 	sizeof(CKDDASD_CACHE)

/*-------------------------------------------------------------------*/
/* Structure definitions for Compressed CKD devices                  */
/*-------------------------------------------------------------------*/
typedef struct _CCKDDASD_DEVHDR {       /* Compress device header    */
/*  0 */BYTE             vrm[3];        /* Version Release Modifier  */
/*  3 */BYTE             options;       /* Options byte              */
/*  4 */U32              numl1tab;      /* Size of lvl 1 table       */
/*  8 */U32              numl2tab;      /* Size of lvl 2 tables      */
/* 12 */U32              size;          /* File size                 */
/* 16 */U32              used;          /* File used                 */
/* 20 */U32              free;          /* Position to free space    */
/* 24 */U32              free_total;    /* Total free space          */
/* 28 */U32              free_largest;  /* Largest free space        */
/* 32 */U32              free_number;   /* Number free spaces        */
/* 36 */U32              free_imbed;    /* [deprecated]              */
/* 40 */FWORD            cyls;          /* Cylinders on device       */
/* 44 */BYTE             resv1;         /* Reserved                  */
/* 45 */BYTE             compress;      /* Compression algorithm     */
/* 46 */S16              compress_parm; /* Compression parameter     */
/* 48 */BYTE             resv2[464];    /* Reserved                  */
    } CCKDDASD_DEVHDR;

#define CCKD_VERSION           0
#define CCKD_RELEASE           2
#define CCKD_MODLVL            1

#define CCKD_NOFUDGE           1         /* [deprecated]             */
#define CCKD_BIGENDIAN         2
#define CCKD_OPENED            128


#define CCKD_FREEHDR           size
#define CCKD_FREEHDR_SIZE      28
#define CCKD_FREEHDR_POS       CKDDASD_DEVHDR_SIZE+12

#define CCKD_COMPRESS_NONE     0
#define CCKD_COMPRESS_ZLIB     1
#ifndef CCKD_BZIP2
#define CCKD_COMPRESS_MAX      CCKD_COMPRESS_ZLIB
#else
#define CCKD_COMPRESS_BZIP2    2
#define CCKD_COMPRESS_MAX      CCKD_COMPRESS_BZIP2
#endif

typedef struct _CCKD_DFWQE {            /* Defferred write queue elem*/
        struct _CCKD_DFWQE *next;       /* -> next queue element     */
        unsigned int     trk;           /* Track number              */
        void            *buf;           /* Buffer                    */
        unsigned int     busy:1,        /* Busy indicator            */
                         retry:1;       /* Retry write               */
    } CCKD_DFWQE;

typedef struct _CCKD_L2ENT {            /* Level 2 table entry       */
        U32              pos;           /* Track offset              */
        U16              len;           /* Track length              */
        U16              size;          /* Track size [deprecated]   */
    } CCKD_L2ENT;
typedef CCKD_L2ENT     CCKD_L2TAB[256]; /* Level 2 table             */

typedef U32            CCKD_L1ENT;      /* Level 1 table entry       */
typedef CCKD_L1ENT     CCKD_L1TAB[];    /* Level 1 table             */

typedef struct _CCKD_FREEBLK {          /* Free block                */
        U32              pos;           /* Position next free blk    */
        U32              len;           /* Length this free blk      */
	int		 prev;          /* Index to prev free blk    */
        int              next;          /* Index to next free blk    */
        int              cnt;           /* Garbage Collection count  */
    } CCKD_FREEBLK;

typedef struct _CCKD_CACHE {		/* Cache structure           */
	int		 trk;		/* Cached track number       */
	int		 sfx;		/* Cached l2tab file index   */
	int		 l1x;		/* Cached l2tab index        */
	BYTE		*buf;		/* Cached buffer address     */
	struct timeval	 tv;		/* Time last used            */
        unsigned int     used:1,        /* Cache entry was used      */
                         updated:1,     /* Cache buf was updated     */
                         reading:1,     /* Cache buf being read      */
                         writing:1,     /* Cache buf being written   */
                         waiting:1;     /* Thread waiting for i/o    */
    } CCKD_CACHE;

#define CCKDDASD_DEVHDR_SIZE   sizeof(CCKDDASD_DEVHDR)
#define CCKD_L1ENT_SIZE        sizeof(CCKD_L1ENT)
#define CCKD_L1TAB_POS         CKDDASD_DEVHDR_SIZE+CCKDDASD_DEVHDR_SIZE
#define CCKD_L2ENT_SIZE        sizeof(CCKD_L2ENT)
#define CCKD_L2TAB_SIZE        sizeof(CCKD_L2TAB)
#define CCKD_DFWQE_SIZE        sizeof(CCKD_DFWQE)
#define CCKD_FREEBLK_SIZE      8
#define CCKD_FREEBLK_ISIZE     sizeof(CCKD_FREEBLK)
#define CCKD_CACHE_SIZE        sizeof(CCKD_CACHE)
#define CCKD_NULLTRK_SIZE      37
#define GC_COMBINE_LO          0
#define GC_COMBINE_HI          1

/* adjustable values */

#define CCKD_L2CACHE_NBR       32       /* Number of secondary lookup
                                           tables that will be cached
                                           in storage                */
#define CCKD_MAX_WRITE_TIME    60       /* Number of seconds a track
                                           image remains idle until
                                           it is written             */ 
#define CCKD_COMPRESS_MIN      512      /* Track images smaller than
                                           this won't be compressed  */
#define CCKD_MAX_SF            8        /* Maximum number of shadow
                                           files: 0 to 9 [0 disables
                                           shadow file support]      */
#define CCKD_MAX_RA            9        /* Number of readahead
                                           threads: 1 - 9            */ 
#define CCKD_MAX_DFW           9        /* Number of deferred write
                                           threads: 1 - 9            */
#define CCKD_MAX_DFWQ_DEPTH    64       /* Track reads will be locked
                                           when the deferred-write-
                                           queue gets this large     */

typedef struct _CCKDDASD_EXT {          /* Ext for compressed ckd    */
        unsigned int     curpos;        /* Current ckd file position */
        unsigned int     trkpos;        /* Current track position    */
        int              curtrk;        /* Current track             */
        unsigned int     writeinit:1,   /* Write threads init'd      */
                         gcinit:1,      /* Garbage collection init'd */
                         threading:1,   /* Threading is active       */
                         l1updated:1,   /* Level 1 table updated     */
                         l2updated:1;   /* Level 2 table updated     */
        LOCK             filelock;      /* File lock                 */
        CCKDDASD_DEVHDR  cdevhdr[CCKD_MAX_SF+1];/* cckd device hdr   */
        CCKD_L1ENT      *l1[CCKD_MAX_SF+1]; /* Level 1 tables        */
        int              fd[CCKD_MAX_SF+1]; /* File descriptors      */
        BYTE             swapend[CCKD_MAX_SF+1]; /* Swap endian flag */
        BYTE             open[CCKD_MAX_SF+1];    /* Open flag        */
        int              sfn;           /* Number active shadow files*/
        int              sfx;           /* Active level 2 file index */
        int              l1x;           /* Active level 2 table index*/
        CCKD_L2ENT      *l2;            /* Active level 2 table      */
        CCKD_CACHE      *l2cache;       /* Level 2 table cache       */
        CCKD_FREEBLK    *free;          /* Internal free space chain */
        int              freenbr;       /* Number free space entries */
        int              free1st;       /* Index of 1st entry        */
        int              freeavail;     /* Index of available entry  */
        COND             gccond;        /* GC condition              */
        LOCK             gclock;        /* GC lock                   */
        ATTR             gcattr;        /* GC thread attribute       */
        TID              gctid;         /* GC thread id              */
        time_t           gctime;        /* GC last collection        */
        BYTE            *gcbuf;         /* GC buffer address         */
        int              gcbuflen;      /* GC buffer length          */
        int              gcl1x;         /* GC lvl 1 index (see gclen)*/
        int              gcl2x;         /* GC lvl 2 index (see gclen)*/
        int              dfwid;         /* Deferred write id         */
        int              dfwaiting;     /* Deferred write waiting    */
        ATTR             dfwattr;       /* Deferred write thread attr*/
        TID              dfwtid;        /* Deferred write thread id  */
        LOCK             dfwlock;       /* Deferred write lock       */
        COND             dfwcond;       /* Deferred write condition  */
        CCKD_DFWQE      *dfwq;          /* Deffered write queue      */
        int              dfwqdepth;     /* Deffered write q depth    */
	int		 ra;		/* Readahead index           */
#if (CCKD_MAX_RA > 0)
        int              rainit[CCKD_MAX_RA]; /* Readahead init'd    */
        ATTR             raattr[CCKD_MAX_RA]; /* Readahead attr      */
        TID              ratid[CCKD_MAX_RA];  /* Readahead thread id */
        LOCK             ralock[CCKD_MAX_RA]; /* Readahead lock      */
        COND             racond[CCKD_MAX_RA]; /* Readahead condition */
        int              ratrk[CCKD_MAX_RA];  /* Readahead track     */
#endif
        COND             rtcond;        /* Read track condition      */
	LOCK		 cachelock;	/* Cache lock                */
	CCKD_CACHE	*cache;		/* Cache pointer             */
        CCKD_CACHE      *active;        /* Active cache entry        */
        BYTE            *cachebuf[CCKD_MAX_RA+1];/* Buffers for read */
        int              reads[CCKD_MAX_SF+1];   /* Nbr track reads  */
        int              l2reads[CCKD_MAX_SF+1]; /* Nbr l2 reads     */
        int              writes[CCKD_MAX_SF+1];  /* Nbr track writes */
        int              totreads;      /* Total nbr trk reads       */
        int              totwrites;     /* Total nbr trk writes      */
        int              totl2reads;    /* Total nbr l2 reads        */
        int              cachehits;     /* Cache hits                */
	int     	 readaheads;    /* Number trks read ahead    */
        int              switches;      /* Number trk switches       */
        int              misses;        /* Number readahead misses   */
        int              l2cachenbr;    /* Size of level 2 cache     */
        int              max_dfwq;      /* Max size of dfw queue     */
        int              max_ra;        /* Max nbr readahead threads */
        int              max_dfw;       /* Max nbr dfw threads       */
        int              max_wt;        /* Max write wait time       */
        LOCK             termlock;      /* Termination lock          */
        COND             termcond;      /* Termination condition     */
#ifdef  CCKD_ITRACEMAX
        char            *itrace;        /* Internal trace table      */
        int              itracex;       /* Internal trace index      */
#endif
    } CCKDDASD_EXT;

#define CCKD_OPEN_NONE         0
#define CCKD_OPEN_RO           1
#define CCKD_OPEN_RD           2
#define CCKD_OPEN_RW           3

/*-------------------------------------------------------------------*/
/* Global data areas in module config.c 			     */
/*-------------------------------------------------------------------*/
extern SYSBLK	sysblk; 		/* System control block      */
extern BYTE	ascii_to_ebcdic[];	/* Translate table	     */
extern BYTE	ebcdic_to_ascii[];	/* Translate table	     */

/*-------------------------------------------------------------------*/
/* structure definitions for module ibuf.c          			     */
/*-------------------------------------------------------------------*/

#define FRAG_ADDRESSLENGTH   6     /* Bits of the fragment size     */

#define FRAG_BYTESIZE   64       /* Size of /370 code fragments   */

#define FRAG_SIZE       128       /* Size of /370 code fragments   */

#define FRAG_INSTSIZE   (FRAG_BYTESIZE / 2) /* number of cmp. inst */

#define FRAG_BYTEMASK   (0x7FFFFFFF - (FRAG_BYTESIZE -1))

#define FRAG_BUFFER     1024       /* Number of fragments in cache  */

#define IBUF_ICOUNT     64

#define FRAG_BUFFERMASK (((FRAG_BUFFER * FRAG_BYTESIZE) - 1) - (FRAG_BYTESIZE - 1))

#ifdef IBUF
#define FRAG_INVALIDATEIO(_a, _s) { \
           ibuf_invalidate(_a, _s); \
           }
#define FRAG_INVALIDATE(_a, _s) { \
           ibuf_invalidate(_a, _s); \
           }
#define FRAG_INVALIDATEX(_r, _a, _s) { \
           ibuf_fastinvalidate(_a, _s); \
           }
#else
#define FRAG_INVALIDATE(_a, _s) { }
#define FRAG_INVALIDATEIO(_a, _s) { }
#define FRAG_INVALIDATEX(_r, _a, _s) { }
#endif

#define FRAG_PER_PAGE    (STORAGE_KEY_PAGESIZE / FRAG_BYTESIZE)

typedef void (*zz_func) (BYTE inst[], int execflag, REGS *regs);

typedef struct _RADDR {	/* Compiled Register and Address */
        BYTE r1;
        BYTE r2;
        BYTE r3;
        BYTE r4;
        U32 addr;
    } RADDR;

typedef struct _FRAGENTRY {	/* Compiled Code Entry */
#ifdef IBUF_FASTOP
        RADDR raddr;
#endif
        U32     ia;
        U32     iaabs;
        BYTE    *inst;
        BYTE    *valid;
#ifdef CHECK_FRAGPARMS
        BYTE    oinst[6];
#endif
    } FRAGENTRY;

typedef struct _FRAG {		/* Compliled Code Fragments */
        void **dict;
        U32 minabs;
        U16 maxind;
        FRAGENTRY entry[FRAG_SIZE];   /* compiled instruction */
    } FRAG;

/*-------------------------------------------------------------------*/
/* Function prototypes						     */
/*-------------------------------------------------------------------*/

#ifdef INLINE_VFETCH
#define VFETCH2(addr, arn, regs)  xvfetch2(addr, arn, regs)
#define VFETCH4(addr, arn, regs)  xvfetch4(addr, arn, regs)
#define VFETCH8(addr, arn, regs)  xvfetch8(addr, arn, regs)
#else
#define VFETCH2(addr, arn, regs)  vfetch2(addr, arn, regs)
#define VFETCH4(addr, arn, regs)  vfetch4(addr, arn, regs)
#define VFETCH8(addr, arn, regs)  vfetch8(addr, arn, regs)
#endif

#ifdef INLINE_VSTORE
#define VSTORE4(value, addr, arn, regs)  xvstore4(value, addr, arn, regs)
#define VSTORE8(value, addr, arn, regs)  xvstore8(value, addr, arn, regs)
#else
#define VSTORE4(value, addr, arn, regs)  vstore4(value, addr, arn, regs)
#define VSTORE8(value, addr, arn, regs)  vstore8(value, addr, arn, regs)
#endif

/* Functions in module config.c */
void build_config (BYTE *fname);
DEVBLK *find_device_by_devnum (U16 devnum);
DEVBLK *find_device_by_subchan (U16 subchan);
DEVBLK *find_unused_device ();
int  attach_device (U16 devnum, U16 devtype, int addargc,
	BYTE *addargv[]);
int  detach_device (U16 devnum);
int  define_device (U16 olddev, U16 newdev);
int  configure_cpu (REGS *regs);
int  deconfigure_cpu (REGS *regs);

/* Functions in module panel.c */
void display_inst (REGS *regs, BYTE *inst);
void panel_display (void);

/* Functions in module ipl.c */
int  load_ipl (U16 devnum, REGS *regs);
void cpu_reset (REGS *regs);
void initial_cpu_reset (REGS *regs);

/* Functions in module cpu.c */
void store_psw (REGS *regs, BYTE *addr);
int  load_psw (REGS *regs, BYTE *addr);
void program_interrupt (REGS *regs, int code);
void restart_interrupt (REGS *regs);
void perform_io_interrupt (REGS *regs);
void perform_mck_interrupt (REGS *regs);
void *cpu_thread (REGS *regs);
void *set_doint(REGS *regs);
void *set_doinst();
#ifdef IBUF
void ibuf_loadinst(REGS *regs);
void ibuf_invalidate(U32 abs, U32 len);
#ifndef INLINE_INVALIDATE
void ibuf_fastinvalidate(U32 abs, U32 len);
#endif
void ibuf_compile_frag (REGS *regs, U32 ia);
void ibuf_assign_fragment (REGS *regs, U32 ia);
#ifndef INLINE_GET
void ibuf_get_frag (REGS *regs, U32 ia);
#endif
#endif

/* Functions in module dat.c */
U16  translate_asn (U16 asn, REGS *regs, U32 *asteo, U32 aste[]);
int  authorize_asn (U16 ax, U32 aste[], int atemask, REGS *regs);
U16  translate_alet (U32 alet, U16 eax, int acctype, REGS *regs,
	U32 *asteo, U32 aste[], int *prot);
#ifdef FEATURE_OPTIMIZE_SAME_PAGE
int  translate_addr (U32 vaddr, int arn, REGS *regs, int acctype,
	U32 *raddr, U16 *xcode, int *priv, int *prot, int *pstid,
	U32 *xpblk, BYTE *xpkey);
#endif
void purge_alb (REGS *regs);
void purge_tlb (REGS *regs);
void invalidate_pte (BYTE ibyte, int r1, int r2, REGS *regs);
#ifndef INLINE_LOGICAL
U32  logical_to_abs (U32 addr, int arn, REGS *regs, int acctype,
	BYTE akey);
#endif

#ifndef INLINE_VSTORE
void vstorec (void *src, BYTE len, U32 addr, int arn, REGS *regs);
void vstoreb (BYTE value, U32 addr, int arn, REGS *regs);
void vstore2 (U16 value, U32 addr, int arn, REGS *regs);
void vstore4 (U32 value, U32 addr, int arn, REGS *regs);
void vstore8 (U64 value, U32 addr, int arn, REGS *regs);
#else
void xvstore4 (U32 value, U32 addr, int arn, REGS *regs);
void xvstore8 (U64 value, U32 addr, int arn, REGS *regs);
#endif

#ifndef INLINE_VFETCH
void vfetchc (void *dest, BYTE len, U32 addr, int arn, REGS *regs);
BYTE vfetchb (U32 addr, int arn, REGS *regs);
U16  vfetch2 (U32 addr, int arn, REGS *regs);
U32  vfetch4 (U32 addr, int arn, REGS *regs);
U64  vfetch8 (U32 addr, int arn, REGS *regs);
#else
BYTE  xvfetchb (U32 addr, int arn, REGS *regs);
U16  xvfetch2 (U32 addr, int arn, REGS *regs);
U32  xvfetch4 (U32 addr, int arn, REGS *regs);
U64  xvfetch8 (U32 addr, int arn, REGS *regs);
#endif

#ifndef INLINE_IFETCH
void instfetch (BYTE *dest, U32 addr, REGS *regs);
#endif

void validate_operand (U32 addr, int arn, int len,
	int acctype, REGS *regs);
void move_chars (U32 addr1, int arn1, BYTE key1, U32 addr2,
                int arn2, BYTE key2, int len, REGS *regs);

/* Access type parameter passed to translate functions in dat.c */
#define ACCTYPE_READ		1	/* Read operand data	     */
#define ACCTYPE_WRITE		2	/* Write operand data	     */
#define ACCTYPE_INSTFETCH	3	/* Instruction fetch	     */
#define ACCTYPE_TAR		4	/* Test Access		     */
#define ACCTYPE_LRA		5	/* Load Real Address	     */
#define ACCTYPE_TPROT		6	/* Test Protection	     */
#define ACCTYPE_IVSK		7	/* Insert Virtual Storage Key*/
#define ACCTYPE_STACK		8	/* Linkage stack operations  */
#define ACCTYPE_BSG		9	/* Branch in Subspace Group  */
#define ACCTYPE_LOCKPAGE       10	/* Lock page                 */
#define ACCTYPE_UNLKPAGE       11	/* Unlock page               */
#define ACCTYPE_SIE            12	/* SIE host translation      */

/* Special value for arn parameter for translate functions in dat.c */
#define USE_REAL_ADDR		(-1)	/* Real address              */
#define USE_PRIMARY_SPACE	(-2)	/* Primary space virtual     */
#define USE_SECONDARY_SPACE	(-3)	/* Secondary space virtual   */

/* Interception codes used by longjmp/SIE */
#define SIE_NO_INTERCEPT        (-1)    /* Continue (after pgmint)   */
#define SIE_HOST_INTERRUPT      (-2)    /* Host interrupt pending    */
#define SIE_INTERCEPT_INST      (-3)    /* Instruction interception  */
#define SIE_INTERCEPT_INSTCOMP  (-4)    /* Instr. int TS/CS/CDS      */
#define SIE_INTERCEPT_EXTREQ    (-5)    /* External interrupt        */
#define SIE_INTERCEPT_IOREQ     (-6)    /* I/O interrupt             */
#define SIE_INTERCEPT_WAIT      (-7)    /* Wait state loaded         */
#define SIE_INTERCEPT_STOPREQ   (-8)    /* STOP reqeust              */
#define SIE_INTERCEPT_RESTART   (-9)    /* Restart interrupt         */
#define SIE_INTERCEPT_MCK      (-10)    /* Machine Check interrupt   */
#define SIE_INTERCEPT_EXT      (-11)    /* External interrupt pending*/
#define SIE_INTERCEPT_VALIDITY (-12)    /* SIE validity check        */
#define SIE_INTERCEPT_PER      (-13)    /* SIE guest per event       */

/* Functions in module diagmssf.c */
void scpend_call (void);
int  mssf_call (int r1, int r2, REGS *regs);
void diag204_call (int r1, int r2, REGS *regs);

/* Functions in module diagnose.c */
void diagnose_call (U32 code, int r1, int r2, REGS *regs);

/* Functions in module vm.c */
int  diag_devtype (int r1, int r2, REGS *regs);
int  syncblk_io (int r1, int r2, REGS *regs);
int  syncgen_io (int r1, int r2, REGS *regs);
void extid_call (int r1, int r2, REGS *regs);
int  cpcmd_call (int r1, int r2, REGS *regs);
void pseudo_timer (U32 code, int r1, int r2, REGS *regs);
void access_reipl_data (int r1, int r2, REGS *regs);
int  diag_ppagerel (int r1, int r2, REGS *regs);

/* Functions in module external.c */
void perform_external_interrupt (REGS *regs);
void update_TOD_clock (void);
void *timer_update_thread (void *argp);
void store_status (REGS *ssreg, U32 aaddr);
void synchronize_broadcast (REGS *regs, U32 *type);

/* Functions in module machchk.c */
int  present_mck_interrupt (REGS *regs, U64 *mcic, U32 *xdmg,
	U32 *fsta);
U32  channel_report (void);
void machine_check_crwpend (void);

/* Functions in module service.c */
void scp_command (BYTE *command, int priomsg);

/* Functions in module sie.c */
void sie_exit (REGS *regs, int code);

/* Functions in module stack.c */
void form_stack_entry (BYTE etype, U32 retna, U32 calla, U32 csi, REGS *regs);
int  program_return_unstack (REGS *regs, U32 *lsedap);
U32 locate_stack_entry (int prinst, LSED *lsedptr, REGS *regs);
void unstack_registers (U32 lsea, int r1, int r2, REGS *regs);
U32 abs_stack_addr (U32 vaddr, REGS *regs, int acctype);

/* Functions in module trace.c */
U32  trace_br (int amode, U32 ia, REGS *regs);
U32  trace_bsg (U32 alet, U32 ia, REGS *regs);
U32  trace_ssar (U16 sasn, REGS *regs);
U32  trace_pc (U32 pcnum, REGS *regs);
U32  trace_pr (REGS *newregs, REGS *regs);
U32  trace_pt (U16 pasn, U32 gpr2, REGS *regs);

/* Functions in module channel.c */
int  start_io (DEVBLK *dev, U32 ioparm, BYTE orb4, BYTE orb5,
	BYTE orb6, BYTE orb7, U32 ccwaddr);
void *execute_ccw_chain (DEVBLK *dev);
int  store_channel_id (REGS *regs, U16 chan);
int  test_channel (REGS *regs, U16 chan);
int  test_io (REGS *regs, DEVBLK *dev, BYTE ibyte);
int  test_subchan (REGS *regs, DEVBLK *dev, IRB *irb);
void clear_subchan (REGS *regs, DEVBLK *dev);
int  halt_subchan (REGS *regs, DEVBLK *dev);
int  halt_io (REGS *regs, DEVBLK *dev, BYTE ibyte);
int  resume_subchan (REGS *regs, DEVBLK *dev);
int  present_io_interrupt (REGS *regs, U32 *ioid, U32 *ioparm,
	BYTE *csw);
void io_reset (void);
int  device_attention (DEVBLK *dev, BYTE unitstat);

/* Functions in module cardrdr.c */
DEVIF cardrdr_init_handler;
DEVQF cardrdr_query_device;
DEVXF cardrdr_execute_ccw;
DEVCF cardrdr_close_device;

/* Functions in module cardpch.c */
DEVIF cardpch_init_handler;
DEVQF cardpch_query_device;
DEVXF cardpch_execute_ccw;
DEVCF cardpch_close_device;

/* Functions in module console.c */
void *console_connection_handler (void *arg);
DEVIF loc3270_init_handler;
DEVQF loc3270_query_device;
DEVXF loc3270_execute_ccw;
DEVCF loc3270_close_device;
DEVIF constty_init_handler;
DEVQF constty_query_device;
DEVXF constty_execute_ccw;
DEVCF constty_close_device;

/* Functions in module ctcadpt.c */
DEVIF ctcadpt_init_handler;
DEVQF ctcadpt_query_device;
DEVXF ctcadpt_execute_ccw;
DEVCF ctcadpt_close_device;

/* Functions in module printer.c */
DEVIF printer_init_handler;
DEVQF printer_query_device;
DEVXF printer_execute_ccw;
DEVCF printer_close_device;

/* Functions in module tapedev.c */
DEVIF tapedev_init_handler;
DEVQF tapedev_query_device;
DEVXF tapedev_execute_ccw;
DEVCF tapedev_close_device;

/* Functions in module ckddasd.c */
DEVIF ckddasd_init_handler;
DEVQF ckddasd_query_device;
DEVXF ckddasd_execute_ccw;
DEVCF ckddasd_close_device;

/* Functions in module fbadasd.c */
DEVIF fbadasd_init_handler;
DEVQF fbadasd_query_device;
DEVXF fbadasd_execute_ccw;
DEVCF fbadasd_close_device;
void fbadasd_syncblk_io (DEVBLK *dev, BYTE type, U32 blknum,
	U32 blksize, BYTE *iobuf, BYTE *unitstat, U16 *residual);

/* Functions in module cckddasd.c */
DEVIF   cckddasd_init_handler;
int 	cckddasd_close_device (DEVBLK *);
off_t   cckd_lseek(DEVBLK *, int, off_t, int);
ssize_t cckd_read(DEVBLK *, int, char *, size_t);
ssize_t cckd_write(DEVBLK *, int, const void *, size_t);
