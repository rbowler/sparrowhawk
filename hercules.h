/* HERCULES.H	(c) Copyright Roger Bowler, 1999-2000		     */
/*		ESA/390 Emulator Header File			     */

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

/*-------------------------------------------------------------------*/
/* ESA/390 features implemented 				     */
/*-------------------------------------------------------------------*/
#define MAX_CPU_ENGINES 	4
#define FEATURE_ALD_FORMAT	0
#undef	FEATURE_ACCESS_REGISTERS
#undef	FEATURE_BASIC_STORAGE_KEYS
#undef	FEATURE_BCMODE
#undef	FEATURE_BIMODAL_ADDRESSING
#undef	FEATURE_BRANCH_AND_SET_AUTHORITY
#undef	FEATURE_CHANNEL_SUBSYSTEM
#undef	FEATURE_DIRECT_CONTROL
#undef	FEATURE_DUAL_ADDRESS_SPACE
#undef	FEATURE_EXPANDED_STORAGE
#undef	FEATURE_EXTENDED_STORAGE_KEYS
#undef	FEATURE_EXTENDED_TOD_CLOCK
#undef	FEATURE_INTERVAL_TIMER
#undef	FEATURE_HALFWORD_IMMEDIATE
#undef	FEATURE_LINKAGE_STACK
#undef	FEATURE_MSSF_CALL
#undef	FEATURE_MVS_ASSIST
#undef	FEATURE_PAGE_PROTECTION
#undef	FEATURE_PRIVATE_SPACE
#undef	FEATURE_RELATIVE_BRANCH
#undef	FEATURE_S370_CHANNEL
#undef	FEATURE_SEGMENT_PROTECTION
#undef	FEATURE_STORAGE_PROTECTION_OVERRIDE
#undef	FEATURE_SUBSPACE_GROUP
#undef	FEATURE_SUPPRESSION_ON_PROTECTION
#undef	FEATURE_TRACING

#if	ARCH == 370
 #define ARCHITECTURE_NAME	"S/370"
 #define FEATURE_BASIC_STORAGE_KEYS
 #define FEATURE_BCMODE
 #define FEATURE_INTERVAL_TIMER
 #define FEATURE_S370_CHANNEL
 #define FEATURE_SEGMENT_PROTECTION
#elif	ARCH == 390
 #define ARCHITECTURE_NAME	"ESA/390"
 #define FEATURE_ACCESS_REGISTERS
 #define FEATURE_BIMODAL_ADDRESSING
 #define FEATURE_BRANCH_AND_SET_AUTHORITY
 #define FEATURE_CHANNEL_SUBSYSTEM
 #define FEATURE_DUAL_ADDRESS_SPACE
 #define FEATURE_EXTENDED_STORAGE_KEYS
 #define FEATURE_EXTENDED_TOD_CLOCK
 #define FEATURE_HALFWORD_IMMEDIATE
 #define FEATURE_LINKAGE_STACK
 #define FEATURE_MSSF_CALL
 #define FEATURE_MVS_ASSIST
 #define FEATURE_PAGE_PROTECTION
 #define FEATURE_PRIVATE_SPACE
 #define FEATURE_RELATIVE_BRANCH
 #define FEATURE_STORAGE_PROTECTION_OVERRIDE
 #define FEATURE_SUBSPACE_GROUP
 #define FEATURE_SUPPRESSION_ON_PROTECTION
 #define FEATURE_TRACING
#else
 #error Either ARCH=370 or ARCH=390 must be specified
#endif


/*-------------------------------------------------------------------*/
/* Macro definitions for tracing				     */
/*-------------------------------------------------------------------*/
#define logmsg(a...) \
	fprintf(sysblk.msgpipew, ## a)
#define DEVTRACE(format, a...) \
	if(dev->ccwtrace||dev->ccwstep) \
	logmsg("%4.4X:" format, dev->devnum, ## a)

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
typedef pthread_t			TID;
typedef pthread_mutex_t 		LOCK;
typedef pthread_cond_t			COND;
typedef pthread_attr_t			ATTR;
#define initialize_lock(plk) \
	pthread_mutex_init((plk),NULL)
#define obtain_lock(plk) \
	pthread_mutex_lock((plk))
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
#endif

/*-------------------------------------------------------------------*/
/* Prototype definitions for device handler functions		     */
/*-------------------------------------------------------------------*/
struct _DEVBLK;
typedef int DEVIF (struct _DEVBLK *dev, int argc, BYTE *argv[]);
typedef void DEVXF (struct _DEVBLK *dev, BYTE code, BYTE flags,
	BYTE chained, U16 count, BYTE prevcode, int ccwseq,
	BYTE *iobuf, BYTE *more, BYTE *unitstat, U16 *residual);

/*-------------------------------------------------------------------*/
/* Structure definition for CPU register context		     */
/*-------------------------------------------------------------------*/
typedef struct _REGS {			/* Processor registers	     */
	U64	ptimer; 		/* CPU timer		     */
	U64	clkc;			/* 0-7=Clock comparator epoch,
					   8-63=Comparator bits 0-55 */
	U64	instcount;		/* Instruction counter	     */
	TLBE	tlb[256];		/* Translation lookaside buf */
	TID	cputid; 		/* CPU thread identifier     */
	U32	gpr[16];		/* General purpose registers */
	U32	cr[16]; 		/* Control registers	     */
	U32	ar[16]; 		/* Access registers	     */
	U32	fpr[8]; 		/* Floating point registers  */
	U32	pxr;			/* Prefix register	     */
	U32	todpr;			/* TOD programmable register */
	U32	tea;			/* Translation exception addr*/
	U16	cpuad;			/* CPU address for STAP      */
	PSW	psw;			/* Program status word	     */
	BYTE	excarid;		/* Exception access register */
	BYTE	itimer_pending; 	/* 1=Interrupt is pending for
					     the interval timer      */
	BYTE	cpustate;		/* CPU stopped/started state */
	BYTE	restart;		/* 1=Restart interrpt pending*/
	BYTE	extcall;		/* 1=Extcall interrpt pending*/
	U16	extccpu;		/* CPU causing external call */
	BYTE	emersig;		/* 1=Emersig interrpt pending*/
	BYTE	emercpu 		/* Emergency signal flags    */
		    [MAX_CPU_ENGINES];	/* for each CPU (1=pending)  */
	BYTE	storstat;		/* 1=Stop and store status   */
	BYTE	instvalid;		/* 1=Inst field is valid     */
	BYTE	inst[6];		/* Last-fetched instruction  */
	jmp_buf progjmp;		/* longjmp destination for
					   program check return      */
    } REGS;

/* Definitions for CPU state */
#define CPUSTATE_STOPPED	0	/* CPU is stopped	     */
#define CPUSTATE_STOPPING	1	/* CPU is stopping	     */
#define CPUSTATE_STARTED	2	/* CPU is started	     */

/*-------------------------------------------------------------------*/
/* System configuration block					     */
/*-------------------------------------------------------------------*/
typedef struct _SYSBLK {
	U32	mainsize;		/* Main storage size (bytes) */
	BYTE   *mainstor;		/* -> Main storage	     */
	BYTE   *storkeys;		/* -> Main storage key array */
	U32	xpndsize;		/* Expanded size (4K pages)  */
	BYTE   *xpndstor;		/* -> Expanded storage	     */
	BYTE   *xpndkeys;		/* -> Expanded storage keys  */
	U64	cpuid;			/* CPU identifier for STIDP  */
	U64	todclk; 		/* 0-7=TOD clock epoch,
					   8-63=TOD clock bits 0-55  */
	LOCK	todlock;		/* TOD clock update lock     */
	TID	todtid; 		/* Thread-id for TOD update  */
	U32	toduniq;		/* TOD clock uniqueness value*/
	BYTE	loadparm[8];		/* IPL load parameter	     */
	U16	numcpu; 		/* Number of CPUs installed  */
	REGS	regs[MAX_CPU_ENGINES];	/* Registers for each CPU    */
	LOCK	mainlock;		/* Main storage lock	     */
	COND	intcond;		/* Interrupt condition	     */
	LOCK	intlock;		/* Interrupt lock	     */
	LOCK	sigplock;		/* Signal processor lock     */
	ATTR	detattr;		/* Detached thread attribute */
	TID	cnsltid;		/* Thread-id for console     */
	U16	cnslport;		/* Port number for console   */
	struct _DEVBLK *firstdev;	/* -> First device block     */
	U32	servparm;		/* Service signal parameter  */
	unsigned int			/* Flags		     */
		iopending:1,		/* 1=I/O interrupt pending   */
		sigpbusy:1,		/* 1=Signal facility in use  */
		servsig:1,		/* 1=Service signal pending  */
		intkey:1,		/* 1=Interrupt key pending   */
		sigintreq:1,		/* 1=SIGINT request pending  */
		insttrace:1,		/* 1=Instruction trace	     */
		inststep:1,		/* 1=Instruction step	     */
		instbreak:1;		/* 1=Have breakpoint	     */
	U32	breakaddr;		/* Breakpoint address	     */
	FILE   *msgpipew;		/* Message pipe write handle */
	int	msgpiper;		/* Message pipe read handle  */
    } SYSBLK;

/*-------------------------------------------------------------------*/
/* Device configuration block					     */
/*-------------------------------------------------------------------*/
typedef struct _DEVBLK {
	U16	subchan;		/* Subchannel number	     */
	U16	devnum; 		/* Device number	     */
	U16	devtype;		/* Device type		     */
	DEVIF  *devinit;		/* -> Init device function   */
	DEVXF  *devexec;		/* -> Execute CCW function   */
	LOCK	lock;			/* Device block lock	     */
	COND	resumecond;		/* Resume condition	     */
	struct _DEVBLK *nextdev;	/* -> next device block      */
	unsigned int			/* Flags		     */
		pending:1,		/* 1=Interrupt pending	     */
		busy:1, 		/* 1=Device busy	     */
		console:1,		/* 1=Console device	     */
		connected:1,		/* 1=Console client connected*/
		readpending:1,		/* 1=Console read pending    */
		pcipending:1,		/* 1=PCI interrupt pending   */
		ccwtrace:1,		/* 1=CCW trace		     */
		ccwstep:1,		/* 1=CCW single step	     */
		cdwmerge:1;		/* 1=Channel will merge data
					     chained write CCWs      */
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
	int	fd;			/* File descriptor	     */
	/* Device dependent fields for console */
	int	csock;			/* Client socket number      */
	struct	in_addr ipaddr; 	/* Client IP address	     */
	int	rlen3270;		/* Length of data in buffer  */
	int	pos3270;		/* Current screen position   */
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
	/* Device dependent fields for console */
	int	keybdrem;		/* Number of bytes remaining
					   in keyboard read buffer   */
	/* Device dependent fields for printer */
	unsigned int			/* Flags		     */
		diaggate:1,		/* 1=Diagnostic gate command */
		fold:1; 		/* 1=Fold to upper case      */
	int	printpos;		/* Number of bytes already
					   placed in print buffer    */
	int	printrem;		/* Number of bytes remaining
					   in print buffer	     */
	/* Device dependent fields for tapedev */
	BYTE	tapedevt;		/* Tape device type	     */
	void   *omadesc;		/* -> OMA descriptor array   */
	U16	omafiles;		/* Number of OMA tape files  */
	U16	curfilen;		/* Current file number	     */
	U16	curblklen;		/* Length of current block   */
	long	curblkpos;		/* Offset from start of file
					   to current block	     */
	long	nxtblkpos;		/* Offset from start of file
					   to next block	     */
	long	prvblkpos;		/* Offset from start of file
					   to previous block	     */
	U16	curblkrem;		/* Number of bytes unread
					   from current block	     */
	U16	curbufoff;		/* Offset into buffer of data
					   for next data chained CCW */
	/* Device dependent fields for fbadasd */
	unsigned int			/* Flags		     */
		fbaxtdef:1;		/* 1=Extent defined	     */
	U16	fbablksiz;		/* Physical block size	     */
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
		ckdwckd:1;		/* 1=Write R0 or Write CKD   */
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
	U16	ckdrem; 		/* #of bytes from current
					   position to end of field  */
	U16	ckdpos; 		/* Offset into buffer of data
					   for next data chained CCW */

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

/* Functions in module assist.c */
void obtain_local_lock (U32 addr1, int ar1, U32 addr2, int ar2,
	REGS *regs);
void release_local_lock (U32 addr1, int ar1, U32 addr2, int ar2,
	REGS *regs);
void obtain_cms_lock (U32 addr1, int ar1, U32 addr2, int ar2,
	REGS *regs);
void release_cms_lock (U32 addr1, int ar1, U32 addr2, int ar2,
	REGS *regs);

/* Functions in module config.c */
void build_config (BYTE *fname);
DEVBLK *find_device_by_devnum (U16 devnum);
DEVBLK *find_device_by_subchan (U16 subchan);

/* Functions in module panel.c */
void display_inst (REGS *regs, BYTE *inst);
void panel_display (void);

/* Functions in module ipl.c */
int  load_ipl (U16 devnum, REGS *regs);
void cpu_reset (REGS *regs);
void initial_cpu_reset (REGS *regs);

/* Functions in module cpu.c */
void store_psw (PSW *psw, BYTE *addr);
int  load_psw (PSW *psw, BYTE *addr);
void program_check (REGS *regs, int code);
void *cpu_thread (REGS *regs);

/* Functions in module dat.c */
U16  translate_asn (U16 asn, REGS *regs, U32 *asteo, U32 aste[]);
int  authorize_asn (U16 ax, U32 aste[], int atemask, REGS *regs);
U16  translate_alet (U32 alet, U16 eax, int acctype, REGS *regs,
	U32 *asteo, U32 aste[], int *prot);
int  translate_addr (U32 vaddr, int arn, REGS *regs, int acctype,
	U32 *raddr, U16 *xcode, int *priv, int *prot, int *pstid);
void purge_alb (REGS *regs);
void purge_tlb (REGS *regs);
void invalidate_tlb_entry (U32 pte, REGS *regs);
int  test_prot (U32 addr, int arn, REGS *regs, BYTE akey);
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
void move_chars (U32 addr1, int arn1, BYTE key1, U32 addr2,
	int arn2, BYTE key2, int len, REGS *regs);
int  ss_operation (BYTE opcode, U32 addr1, int arn1, U32 addr2,
	int arn2, int len, REGS *regs);
void validate_operand (U32 addr, int arn, int len,
	int acctype, REGS *regs);

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

/* Special value for arn parameter for translate functions in dat.c */
#define USE_REAL_ADDR		(-1)	/* LURA/STURA instruction    */
#define USE_PRIMARY_SPACE	(-2)	/* MVCS/MVCP instructions    */
#define USE_SECONDARY_SPACE	(-3)	/* MVCS/MVCP instructions    */

/* Functions in module decimal.c */
int  shift_and_round_packed (U32 addr, int len, int arn, REGS *regs,
	BYTE round, BYTE shift);
int  zero_and_add_packed (U32 addr1, int len1, int arn1,
	U32 addr2, int len2, int arn2, REGS *regs);
int  compare_packed (U32 addr1, int len1, int arn1,
	U32 addr2, int len2, int arn2, REGS *regs);
int  add_packed (U32 addr1, int len1, int arn1,
	U32 addr2, int len2, int arn2, REGS *regs);
int  subtract_packed (U32 addr1, int len1, int arn1,
	U32 addr2, int len2, int arn2, REGS *regs);
void multiply_packed (U32 addr1, int len1, int arn1,
	U32 addr2, int len2, int arn2, REGS *regs);
void divide_packed (U32 addr1, int len1, int arn1,
	U32 addr2, int len2, int arn2, REGS *regs);
void convert_to_decimal (int r1, U32 addr, int arn, REGS *regs);
void convert_to_binary (int r1, U32 addr, int arn, REGS *regs);
void move_with_offset (U32 addr1, int len1, int arn1,
	U32 addr2, int len2, int arn2, REGS *regs);
void zoned_to_packed (U32 addr1, int len1, int arn1,
	U32 addr2, int len2, int arn2, REGS *regs);
void packed_to_zoned (U32 addr1, int len1, int arn1,
	U32 addr2, int len2, int arn2, REGS *regs);
int  edit_packed (int edmk, U32 addr1, int len1, int arn1,
	U32 addr2, int arn2, REGS *regs);

/* Functions in module block.c */
int  move_long (int r1, int r2, REGS *regs);
int  compare_long (int r1, int r2, REGS *regs);
int  move_long_extended (int r1, int r3, U32 effect, REGS *regs);
int  compare_long_extended (int r1, int r3, U32 effect, REGS *regs);
int  move_page (int r1, int r2, REGS *regs);
int  compute_checksum (int r1, int r2, REGS *regs);
int  move_string (int r1, int r2, REGS *regs);
int  compare_string (int r1, int r2, REGS *regs);
int  search_string (int r1, int r2, REGS *regs);
int  compare_until_substring_equal (int r1, int r2, REGS *regs);

/* Functions in module diagnose.c */
void scpend_call (void);
int  mssf_call (U32 mssf_command, U32 spccb_absolute_addr, REGS *regs);
void diag204_call (int r1, int r2, REGS *regs);

/* Functions in module external.c */
void perform_external_interrupt (REGS *regs);
void *timer_update_thread (void *argp);
void store_status (REGS *ssreg, U32 aaddr);
int  signal_processor (int r1, int r3, U32 eaddr, REGS *regs);

/* Functions in module service.c */
int  service_call (U32 sclp_command, U32 sccb_absolute_addr,
	REGS *regs);

/* Functions in module sort.c */
int  compare_and_form_codeword (REGS *regs, U32 eaddr);
int  update_tree (REGS *regs);

/* Functions in module stack.c */
void form_stack_entry (BYTE etype, U32 retna, U32 calla, REGS *regs);
int  program_return_unstack (REGS *regs, U32 *lsedap);
void extract_stacked_registers (int r1, int r2, REGS *regs);
int  extract_stacked_state (int rn, BYTE code, REGS *regs);
void modify_stacked_state (int rn, REGS *regs);

/* Functions in module xmem.c */
int  set_address_space_control (BYTE mode, REGS *regs);
int  insert_address_space_control (REGS *regs);
void set_secondary_asn (U16 sasn, REGS *regs);
int  load_address_space_parameters (U16 pkm, U16 sasn, U16 ax,
	U16 pasn, U32 func, REGS *regs);
int  program_transfer (U16 pkm, U16 pasn, int amode, U32 ia,
	int prob, REGS *regs);
int  program_return (REGS *regs);
int  program_call (U32 pcnum, REGS *regs);
void branch_and_set_authority (int r1, int r2, REGS *regs);
void branch_in_subspace_group (int r1, int r2, REGS *regs);

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
int  resume_subchan (REGS *regs, DEVBLK *dev);
int  present_io_interrupt (REGS *regs, U32 *ioid, U32 *ioparm,
	BYTE *csw);
void io_reset (void);
int  device_attention (DEVBLK *dev, BYTE unitstat);

/* Functions in module cardrdr.c */
DEVIF cardrdr_init_handler;
DEVXF cardrdr_execute_ccw;

/* Functions in module cardpch.c */
DEVIF cardpch_init_handler;
DEVXF cardpch_execute_ccw;

/* Functions in module console.c */
void *console_connection_handler (void *arg);
DEVIF loc3270_init_handler;
DEVXF loc3270_execute_ccw;
DEVIF constty_init_handler;
DEVXF constty_execute_ccw;

/* Functions in module printer.c */
DEVIF printer_init_handler;
DEVXF printer_execute_ccw;

/* Functions in module tapedev.c */
DEVIF tapedev_init_handler;
DEVXF tapedev_execute_ccw;

/* Functions in module ckddasd.c */
DEVIF ckddasd_init_handler;
DEVXF ckddasd_execute_ccw;

/* Functions in module fbadasd.c */
DEVIF fbadasd_init_handler;
DEVXF fbadasd_execute_ccw;

