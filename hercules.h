/* HERCULES.H   (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Emulator Header File                         */

/*-------------------------------------------------------------------*/
/* Header file containing Hercules internal data structures          */
/* and function prototypes.                                          */
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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "esa390.h"

/*-------------------------------------------------------------------*/
/* ESA/390 features implemented                                      */
/*-------------------------------------------------------------------*/
#define CPU_ENGINES             4
#undef  FEATURE_S370_CHANNEL
#define FEATURE_CHANNEL_SUBSYSTEM
#define FEATURE_ALD_FORMAT      0
#define FEATURE_STORAGE_PROTECTION_OVERRIDE
#undef  FEATURE_SUBSPACE_GROUP

/*-------------------------------------------------------------------*/
/* Macro definitions for version number                              */
/*-------------------------------------------------------------------*/
#define STRINGMAC(x)    #x
#define MSTRING(x)      STRINGMAC(x)

/*-------------------------------------------------------------------*/
/* Macro definitions for thread functions                            */
/*-------------------------------------------------------------------*/
#define PTHREAD
#ifdef  PTHREAD
#include <pthread.h>
typedef pthread_t                       TID;
typedef pthread_mutex_t                 LOCK;
typedef pthread_cond_t                  COND;
typedef pthread_attr_t                  ATTR;
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
#else
typedef int                             TID;
typedef int                             LOCK;
typedef int                             COND;
typedef int                             ATTR;
#define initialize_lock(plk)            *(plk)=0
#define obtain_lock(plk)                *(plk)=1
#define release_lock(plk)               *(plk)=0
#define initialize_condition(pcond)     *(pcond)=0
#define signal_condition(pcond)         *(pcond)=1
#define wait_condition(pcond,plk)       *(pcond)=1
#define initialize_detach_attr(pat)     *(pat)=1
#define create_thread(ptid,pat,fn,arg)  (*(ptid)=0,fn(arg),0)
#define signal_thread(tid,signo)        raise(signo)
#endif

/*-------------------------------------------------------------------*/
/* Definition of device handler functions                            */
/*-------------------------------------------------------------------*/
struct _DEVBLK;
typedef int DEVIF (struct _DEVBLK *dev, int argc, BYTE *argv[]);
typedef void DEVXF (struct _DEVBLK *dev,
        BYTE code, BYTE flags, BYTE chained, U16 count, BYTE prevcode,
        BYTE *iobuf, BYTE *more, BYTE *unitstat, U16 *residual);

/*-------------------------------------------------------------------*/
/* System configuration block                                        */
/*-------------------------------------------------------------------*/
typedef struct _SYSBLK {
        U32     mainsize;               /* Main storage size         */
        BYTE   *mainstor;               /* -> Main storage           */
        BYTE   *storkeys;               /* -> Main storage key array */
        U32     xpndsize;               /* Expanded storage size     */
        BYTE   *xpndstor;               /* -> Expanded storage       */
        U64     cpuid;                  /* CPU identifier for STIDP  */
        U64     todclk;                 /* TOD clock                 */
        REGS    regs[CPU_ENGINES];      /* Registers for each CPU    */
        LOCK    mainlock;               /* Main storage lock         */
        COND    intcond;                /* Interrupt condition       */
        LOCK    intlock;                /* Interrupt lock            */
        COND    conscond;               /* Console ready condition   */
        LOCK    conslock;               /* Console ready lock        */
        ATTR    detattr;                /* Detached thread attribute */
        TID     tid3270;                /* Thread-id for tn3270d     */
        U16     port3270;               /* Port number for tn3270d   */
        struct _DEVBLK *firstdev;       /* -> First device block     */
        unsigned int                    /* Flags                     */
                insttrace:1,            /* 1=Instruction trace       */
                inststep:1;             /* 1=Instruction step        */
    } SYSBLK;

/*-------------------------------------------------------------------*/
/* Device configuration block                                        */
/*-------------------------------------------------------------------*/
typedef struct _DEVBLK {
        U16     subchan;                /* Subchannel number         */
        U16     devnum;                 /* Device number             */
        U16     devtype;                /* Device type               */
        DEVXF  *devexec;                /* -> Execute CCW function   */
        LOCK    lock;                   /* Device block lock         */
        struct _DEVBLK *nextdev;        /* -> next device block      */
        unsigned int                    /* Flags                     */
                pending:1,              /* 1=Interrupt pending       */
                busy:1,                 /* 1=Device busy             */
                negotiating:1,          /* 1=3270 client negotiating */
                connected:1,            /* 1=3270 client connected   */
                readpending:1,          /* 1=3270 data read pending  */
                ccwtrace:1,             /* 1=CCW trace               */
                ccwstep:1;              /* 1=CCW single step         */
        PMCW    pmcw;                   /* Path management ctl word  */
        SCSW    scsw;                   /* Subchannel status word(XA)*/
        BYTE    csw[8];                 /* Channel status word (370) */
        int     numsense;               /* Number of sense bytes     */
        BYTE    sense[32];              /* Sense bytes               */
        int     numdevid;               /* Number of device id bytes */
        BYTE    devid[32];              /* Device identifier bytes   */
        int     numdevchar;             /* Number of devchar bytes   */
        BYTE    devchar[32];            /* Device characteristics    */
        TID     tid;                    /* Thread-id executing CCW   */
        U32     ccwaddr;                /* Address of first CCW      */
        int     ccwfmt;                 /* CCW format (0 or 1)       */
        BYTE    ccwkey;                 /* Protection key            */
        U32     ioparm;                 /* SSCH I/O parameter value  */
        BYTE   *buf;                    /* -> Device data buffer     */
        int     bufsize;                /* Device data buffer size   */
        BYTE    filename[256];          /* Unix file name            */
        int     fd;                     /* File descriptor           */
        /* Device dependent fields for loc3270 */
        int     csock;                  /* 3270 client socket number */
        struct  in_addr ipaddr;         /* 3270 client IP address    */
        int     rlen3270;               /* Length of data in buffer  */
        /* Device dependent fields for cardrdr */
        unsigned int                    /* Flags                     */
                ascii:1,                /* 1=Convert ASCII to EBCDIC */
                trunc:1;                /* Truncate overlength record*/
        int     cardpos;                /* Offset of next byte to be
                                           read from data buffer     */
        int     cardrem;                /* Number of bytes remaining
                                           in data buffer            */
        /* Device dependent fields for cardrdr */
        unsigned int                    /* Flags                     */
                fold:1;                 /* 1=Fold to upper case      */
        FILE   *fp;                     /* File pointer              */
        int     printpos;               /* Number of bytes already
                                           placed in print buffer    */
        int     printrem;               /* Number of bytes remaining
                                           in print buffer           */
        /* Device dependent fields for simtape */
        long    curblkpos;              /* Offset from start of file
                                           to current block          */
        long    nxtblkpos;              /* Offset from start of file
                                           to next block             */
        long    prvblkpos;              /* Offset from start of file
                                           to previous block         */
        U32     curblkrem;              /* Number of bytes unread
                                           from current block        */
        /* Device dependent fields for fbadasd */
        unsigned int                    /* Flags                     */
                fbaxtdef:1;             /* 1=Extent defined          */
        U16     fbablksiz;              /* Physical block size       */
        U32     fbanumblk;              /* Number of blocks in device*/
        BYTE    fbaoper;                /* Locate operation byte     */
        BYTE    fbamask;                /* Define extent file mask   */
        U32     fbaxblkn;               /* Offset from start of device
                                           to first block of extent  */
        U32     fbaxfirst;              /* Block number within dataset
                                           of first block of extent  */
        U32     fbaxlast;               /* Block number within dataset
                                           of last block of extent   */
        U32     fbalcblk;               /* Block number within dataset
                                           of first block for locate */
        U16     fbalcnum;               /* Block count for locate    */
        /* Device dependent fields for ckddasd */
        unsigned int                    /* Flags                     */
                ckdxtdef:1,             /* 1=Define Extent processed */
                ckdsetfm:1,             /* 1=Set File Mask processed */
                ckdlocat:1,             /* 1=Locate Record processed */
                ckdseek:1,              /* 1=Seek command processed  */
                ckdskcyl:1,             /* 1=Seek cylinder processed */
                ckdrecal:1,             /* 1=Recalibrate processed   */
                ckdrdipl:1;             /* 1=Read IPL processed      */
        U16     ckdcyls;                /* Number of cylinders       */
        U16     ckdtrks;                /* Number of tracks          */
        U16     ckdheads;               /* #of heads per cylinder    */
        U16     ckdtrksz;               /* Track size                */
        BYTE    ckdfmask;               /* Define extent file mask   */
        BYTE    ckdxgattr;              /* Define extent global attr */
        U16     ckdxblksz;              /* Define extent block size  */
        U16     ckdxbcyl;               /* Define extent begin cyl   */
        U16     ckdxbhead;              /* Define extent begin head  */
        U16     ckdxecyl;               /* Define extent end cyl     */
        U16     ckdxehead;              /* Define extent end head    */
        BYTE    ckdloper;               /* Locate record operation   */
        BYTE    ckdlaux;                /* Locate record aux byte    */
        BYTE    ckdlcount;              /* Locate record count       */
        U16     ckdlskcyl;              /* Locate record seek cyl    */
        U16     ckdlskhead;             /* Locate record seek head   */
        U16     ckdlsrcyl;              /* Locate record search cyl  */
        U16     ckdlsrhead;             /* Locate record search head */
        BYTE    ckdlsrrec;              /* Locate record search rec  */
        BYTE    ckdlsector;             /* Locate record sector      */
        U16     ckdltranlf;             /* Locate record transfer
                                           length factor             */
        U16     ckdcurcyl;              /* Current cylinder          */
        U16     ckdcurhead;             /* Current head              */
        BYTE    ckdcurrec;              /* Current record id         */
        BYTE    ckdcurkl;               /* Current record key length */
        U16     ckdcurdl;               /* Current record data length*/
        BYTE    ckdorient;              /* Current orientation       */
        U16     ckdrem;                 /* #of bytes from current
                                           position to end of field  */

    } DEVBLK;

/*-------------------------------------------------------------------*/
/* Global data areas in module config.c                              */
/*-------------------------------------------------------------------*/
extern SYSBLK   sysblk;                 /* System control block      */
extern BYTE     ascii_to_ebcdic[];      /* Translate table           */
extern BYTE     ebcdic_to_ascii[];      /* Translate table           */

/*-------------------------------------------------------------------*/
/* Function prototypes                                               */
/*-------------------------------------------------------------------*/

/* Functions in module config.c */
void build_config (BYTE *fname);
DEVBLK *find_device_by_devnum (U16 devnum);
DEVBLK *find_device_by_subchan (U16 subchan);

/* Functions in module panel.c */
void display_inst (REGS *regs, BYTE *inst);
void panel_command (REGS *regs);

/* Functions in module cpu.c */
void store_psw (PSW *psw, BYTE *addr);
int  load_psw (PSW *psw, BYTE *addr);
void program_check (int code);
void start_cpu (U32 pswaddr, REGS *regs);

/* Functions in module dat.c */
U16  translate_asn (U16 asn, REGS *regs, U32 *asteo, U32 aste[]);
int  authorize_asn (U16 ax, U32 aste[], int atemask, REGS *regs);
U16  translate_alet (int arn, U16 eax, REGS *regs, int acctype,
        U32 *stdptr, int *prot);
int  translate_addr (U32 vaddr, int arn, REGS *regs, int acctype,
        U32 *raddr, U16 *xcode, int *priv, int *prot);
void purge_alb (void);
void purge_tlb (void);
void vstorei (U64 value, int len, U32 addr, int arn, REGS *regs);
U64  vfetchi (int len, U32 addr, int arn, REGS *regs);
void vstoref (void *src, U32 addr, int arn, REGS *regs);
void vfetchf (void *dest, U32 addr, int arn, REGS *regs);
void vfetchd (void *dest, U32 addr, int arn, REGS *regs);
void instfetch (BYTE *dest, U32 addr, REGS *regs);

/* Access type parameter passed to translate functions in dat.c */
#define ACCTYPE_READ            1       /* Read operand data         */
#define ACCTYPE_WRITE           2       /* Write operand data        */
#define ACCTYPE_INSTFETCH       3       /* Instruction fetch         */
#define ACCTYPE_TAR             4       /* Test Access               */
#define ACCTYPE_LRA             5       /* Load Real Address         */
#define ACCTYPE_TPROT           6       /* Test Protection           */

/* Functions in module channel.c */
int  start_io (DEVBLK *dev, U32 ccwaddr, int ccwfmt, BYTE ccwkey,
        U32 ioparm);
void *execute_ccw_chain (DEVBLK *dev);
int  test_io (REGS *regs, DEVBLK *dev, BYTE ibyte);
int  test_subchan (REGS *regs, DEVBLK *dev, SCSW *scsw, ESW *esw);
int  present_io_interrupt (REGS *regs, U32 *ioid, U32 *ioparm,
        BYTE *csw);

/* Functions in module cardrdr.c */
DEVIF cardrdr_init_handler;
DEVXF cardrdr_execute_ccw;

/* Functions in module printer.c */
DEVIF printer_init_handler;
DEVXF printer_execute_ccw;

/* Functions in module simtape.c */
DEVIF simtape_init_handler;
DEVXF simtape_execute_ccw;

/* Functions in module ckddasd.c */
DEVIF ckddasd_init_handler;
DEVXF ckddasd_execute_ccw;

/* Functions in module fbadasd.c */
DEVIF fbadasd_init_handler;
DEVXF fbadasd_execute_ccw;

/* Functions in module loc3270.c */
void *tn3270d (void *arg);
DEVIF loc3270_init_handler;
DEVXF loc3270_execute_ccw;

