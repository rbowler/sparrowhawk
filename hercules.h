/* HERCULES.H	(c) Copyright Roger Bowler, 1999-2000		     */
/*		ESA/390 Emulator Header File			     */

/* Interpretive Execution - (c) Copyright Jan Jaeger, 1999-2000      */

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
#include "esa390.h"
#include "version.h"

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
#define DWORD int	/* will be undefined later */
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
#define logmsg(a...) \
	fprintf(sysblk.msgpipew, a)
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
		ckdtrkof:1;		/* 1=Track ovfl on this blk  */
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

    } DEVBLK;

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

#define CKDDASD_DEVHDR_SIZE	sizeof(CKDDASD_DEVHDR)
#define CKDDASD_TRKHDR_SIZE	sizeof(CKDDASD_TRKHDR)
#define CKDDASD_RECHDR_SIZE	sizeof(CKDDASD_RECHDR)

/*-------------------------------------------------------------------*/
/* Global data areas in module config.c 			     */
/*-------------------------------------------------------------------*/
extern SYSBLK	sysblk; 		/* System control block      */
extern BYTE	ascii_to_ebcdic[];	/* Translate table	     */
extern BYTE	ebcdic_to_ascii[];	/* Translate table	     */

/*-------------------------------------------------------------------*/
/* Function prototypes						     */
/*-------------------------------------------------------------------*/

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
void *cpu_thread (REGS *regs);

/* Functions in module dat.c */
U16  translate_asn (U16 asn, REGS *regs, U32 *asteo, U32 aste[]);
int  authorize_asn (U16 ax, U32 aste[], int atemask, REGS *regs);
U16  translate_alet (U32 alet, U16 eax, int acctype, REGS *regs,
	U32 *asteo, U32 aste[], int *prot);
int  translate_addr (U32 vaddr, int arn, REGS *regs, int acctype,
	U32 *raddr, U16 *xcode, int *priv, int *prot, int *pstid,
	U32 *xpblk, BYTE *xpkey);
void purge_alb (REGS *regs);
void purge_tlb (REGS *regs);
void invalidate_pte (BYTE ibyte, int r1, int r2, REGS *regs);
U32  logical_to_abs (U32 addr, int arn, REGS *regs, int acctype,
	BYTE akey);
void vstorec (void *src, BYTE len, U32 addr, int arn, REGS *regs);
void vstoreb (BYTE value, U32 addr, int arn, REGS *regs);
void vstore2 (U16 value, U32 addr, int arn, REGS *regs);
void vstore4 (U32 value, U32 addr, int arn, REGS *regs);
void vstore8 (U64 value, U32 addr, int arn, REGS *regs);
void vfetchc (void *dest, BYTE len, U32 addr, int arn, REGS *regs);
BYTE vfetchb (U32 addr, int arn, REGS *regs);
U16  vfetch2 (U32 addr, int arn, REGS *regs);
U32  vfetch4 (U32 addr, int arn, REGS *regs);
U64  vfetch8 (U32 addr, int arn, REGS *regs);
void instfetch (BYTE *dest, U32 addr, REGS *regs);
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
#define ACCTYPE_SIE            11	/* SIE host translation      */

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
