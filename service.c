/* SERVICE.C    (c) Copyright Roger Bowler, 1999-2000                */
/*              ESA/390 Service Processor                            */

/*-------------------------------------------------------------------*/
/* This module implements service processor functions                */
/* for the Hercules ESA/390 emulator.                                */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      Corrections contributed by Jan Jaeger                        */
/*      HMC system console functions by Jan Jaeger 2000-02-08        */
/*      Expanded storage support by Jan Jaeger                       */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Service Call Logical Processor command word definitions           */
/*-------------------------------------------------------------------*/
#define SCLP_READ_SCP_INFO      0x00020001
#define SCLP_READ_CHP_INFO      0x00030001
#define SCLP_READ_CSI_INFO      0x001C0001

#define SCLP_READ_XST_MAP       0x00250001

#define SCLP_WRITE_EVENT_DATA   0x00760005
#define SCLP_READ_EVENT_DATA    0x00770005
#define SCLP_WRITE_EVENT_MASK   0x00780005

#define SCLP_DECONFIGURE_CPU    0x00100001
#define SCLP_CONFIGURE_CPU      0x00110001

#define SCLP_DISCONNECT_VF      0x001A0001
#define SCLP_CONNECT_VF         0x001B0001

#define SCLP_COMMAND_MASK       0xFFFF00FF
#define SCLP_COMMAND_CLASS      0x000000FF
#define SCLP_RESOURCE_MASK      0x0000FF00
#define SCLP_RESOURCE_SHIFT     8

/*-------------------------------------------------------------------*/
/* Service Call Control Block structure definitions                  */
/*-------------------------------------------------------------------*/
typedef struct _SCCB_HEADER {
        HWORD   length;                 /* Total length of SCCB      */
        BYTE    flag;                   /* Flag byte                 */
        BYTE    resv1[2];               /* Reserved                  */
        BYTE    type;                   /* Request type              */
        BYTE    reas;                   /* Reason code               */
        BYTE    resp;                   /* Response class code       */
    } SCCB_HEADER;

/* Bit definitions for SCCB header flag byte */
#define SCCB_FLAG_SYNC          0x80    /* Synchronous request       */

/* Bit definitions for SCCB header request type */
#define SCCB_TYPE_VARIABLE      0x80    /* Variable request          */

/* Bit definitions for SCCB header reason code */
#define SCCB_REAS_NONE          0x00    /* No reason                 */
#define SCCB_REAS_NOT_PGBNDRY   0x01    /* SCCB crosses page boundary*/
#define SCCB_REAS_ODD_LENGTH    0x02    /* Length not multiple of 8  */
#define SCCB_REAS_TOO_SHORT     0x03    /* Length is inadequate      */
#define SCCB_REAS_NOACTION      0x02    /* Resource in req. state    */
#define SCCB_REAS_STANDBY       0x04    /* Resource in standby state */
#define SCCB_REAS_INVALID_CMD   0x01    /* Invalid SCLP command code */
#define SCCB_REAS_INVALID_RSCP  0x03    /* Invalid resource in parm  */
#define SCCB_REAS_IMPROPER_RSC  0x05    /* Resource in improper state*/
#define SCCB_REAS_INVALID_RSC   0x09    /* Invalid resource          */

/* Bit definitions for SCCB header response class code */
#define SCCB_RESP_BLOCK_ERROR   0x00    /* Data block error          */
#define SCCB_RESP_INFO          0x10    /* Information returned      */
#define SCCB_RESP_COMPLETE      0x20    /* Command complete          */
#define SCCB_RESP_BACKOUT       0x40    /* Command backed out        */
#define SCCB_RESP_REJECT        0xF0    /* Command reject            */

#ifdef FEATURE_SYSTEM_CONSOLE
#define SCCB_REAS_NO_EVENTS     0x60    /* No outstanding EVENTs     */
#define SCCB_RESP_NO_EVENTS     0xF0
#define SCCB_REAS_EVENTS_SUP    0x62    /* All events suppressed     */
#define SCCB_RESP_EVENTS_SUP    0xF0
#define SCCB_REAS_INVALID_MASK  0x70    /* Invalid events mask       */
#define SCCB_RESP_INVALID_MASK  0xF0
#define SCCB_REAS_MAX_BUFF      0x71    /* Buffer exceeds maximum    */
#define SCCB_RESP_MAX_BUFF      0xF0
#define SCCB_REAS_BUFF_LEN_ERR  0x72    /* Buffer len verification   */
#define SCCB_RESP_BUFF_LEN_ERR  0xF0
#define SCCB_REAS_SYNTAX_ERROR  0x73    /* Buffer syntax error       */
#define SCCB_RESP_SYNTAX_ERROR  0xF0
#define SCCB_REAS_INVALID_MSKL  0x74    /* Invalid mask length       */
#define SCCB_RESP_INVALID_MSKL  0xF0
#define SCCB_REAS_EXCEEDS_SCCB  0x75    /* Exceeds SCCB max capacity */
#define SCCB_RESP_EXCEEDS_SCCB  0xF0
#endif /*FEATURE_SYSTEM_CONSOLE*/

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
        BYTE    ifm[8];                 /* Installed facilities      */
        BYTE    resv4[8];               /* Reserved                  */
        HWORD   maxresgp;               /* Maximum resource group    */
        BYTE    resv5[6];               /* Reserved                  */
        HWORD   nummpf;                 /* Number of entries in MPF
                                           information array         */
        HWORD   offmpf;                 /* Offset from start of SCCB
                                           to MPF information array  */
        BYTE    resv6[4];               /* Reserved                  */
        BYTE    cfg[6];                 /* Config characteristics    */
        FWORD   rcci;                   /* Capacity                  */
        BYTE    resv7;                  /* Reserved                  */
        BYTE    numcrl;                 /* Max #of copy and reassign
                                           list elements allowed     */
        FWORD   etrtol;                 /* ETR sync check tolerance  */
        BYTE    resv8[32];              /* Reserved                  */
    } SCCB_SCP_INFO;

/* Bit definitions for installed facilities */
#define SCCB_IFM0_CHANNEL_PATH_INFORMATION              0x80
#define SCCB_IFM0_CHANNEL_PATH_SUBSYSTEM_COMMAND        0x40
#define SCCB_IFM0_CHANNEL_PATH_RECONFIG                 0x20
#define SCCB_IFM0_CPU_INFORMATION                       0x08
#define SCCB_IFM0_CPU_RECONFIG                          0x04
#define SCCB_IFM1_SIGNAL_ALARM                          0x80
#define SCCB_IFM1_WRITE_OPERATOR_MESSAGE                0x40
#define SCCB_IFM1_STORE_STATUS_ON_LOAD                  0x20
#define SCCB_IFM1_RESTART_REASONS                       0x10
#define SCCB_IFM1_INSTRUCTION_ADDRESS_TRACE_BUFFER      0x08
#define SCCB_IFM1_LOAD_PARAMETER                        0x04
#define SCCB_IFM1_READ_AND_WRITE_DATA                   0x02
#define SCCB_IFM2_REAL_STORAGE_INCREMENT_RECONFIG       0x80
#define SCCB_IFM2_REAL_STORAGE_ELEMENT_INFO             0x40
#define SCCB_IFM2_REAL_STORAGE_ELEMENT_RECONFIG         0x20
#define SCCB_IFM2_COPY_AND_REASSIGN_STORAGE             0x10
#define SCCB_IFM2_EXTENDED_STORAGE_USABILITY_MAP        0x08
#define SCCB_IFM2_EXTENDED_STORAGE_ELEMENT_INFO         0x04
#define SCCB_IFM2_EXTENDED_STORAGE_ELEMENT_RECONFIG     0x02
#define SCCB_IFM2_COPY_AND_REASSIGN_STORAGE_LIST        0x01
#define SCCB_IFM3_VECTOR_FEATURE_RECONFIG               0x80
#define SCCB_IFM3_READ_WRITE_EVENT_FEATURE              0x40
#define SCCB_IFM3_READ_RESOURCE_GROUP_INFO              0x08

/* Bit definitions for configuration characteristics */
#define SCCB_CFG0_LOGICALLY_PARTITIONED                 0x80
#define SCCB_CFG0_SUPPRESSION_ON_PROTECTION             0x20
#define SCCB_CFG0_INITIATE_RESET                        0x10
#define SCCB_CFG0_STORE_CHANNEL_SUBSYS_CHARACTERISTICS  0x08
#define SCCB_CFG0_MVPG_FOR_ALL_GUESTS                   0x04
#define SCCB_CFG0_UNKNOWN_BUT_SET_UNDER_VM              0x02
#define SCCB_CFG1_CSLO                                  0x40
#define SCCB_CFG2_DEVICE_ACTIVE_ONLY_MEASUREMENT        0x40
#define SCCB_CFG2_CALLED_SPACE_IDENTIFICATION           0x02
#define SCCB_CFG2_CHECKSUM_INSTRUCTION                  0x01
#define SCCB_CFG3_RESUME_PROGRAM                        0x80
#define SCCB_CFG3_PERFORM_LOCKED_OPERATION              0x40
#define SCCB_CFG3_IMMEDIATE_AND_RELATIVE                0x10
#define SCCB_CFG3_COMPARE_AND_MOVE_EXTENDED             0x08
#define SCCB_CFG3_BRANCH_AND_SET_AUTHORITY              0x04
#define SCCB_CFG3_EXTENDED_FLOATING_POINT               0x02
#define SCCB_CFG3_UNKNOWN_BUT_SET_UNDER_VM              0x01
#define SCCB_CFG4_EXTENDED_TOD_CLOCK                    0x80
#define SCCB_CFG4_EXTENDED_TRANSLATION                  0x40
#define SCCB_CFG4_STORE_SYSTEM_INFORMATION              0x08

/* CPU information array entry */
typedef struct _SCCB_CPU_INFO {
        BYTE    cpa;                    /* CPU address               */
        BYTE    tod;                    /* TOD clock number          */
        BYTE    cpf[14];                /* RCPU facility map         */
    } SCCB_CPU_INFO;

/* Bit definitions for CPU installed features */
#define SCCB_CPF0_SIE_370_MODE                          0x80
#define SCCB_CPF0_SIE_XA_MODE                           0x40
#define SCCB_CPF0_SIE_SET_II_370_MODE                   0x20
#define SCCB_CPF0_SIE_SET_II_XA_MODE                    0x10
#define SCCB_CPF0_SIE_NEW_INTERCEPT_FORMAT              0x08
#define SCCB_CPF0_STORAGE_KEY_ASSIST                    0x04
#define SCCB_CPF0_MULTIPLE_CONTROLLED_DATA_SPACE        0x02
#define SCCB_CPF1_IO_INTERPRETATION_LEVEL_2             0x40
#define SCCB_CPF1_GUEST_PER_ENHANCED                    0x20
#define SCCB_CPF1_SIGP_INTERPRETATION_ASSIST            0x08
#define SCCB_CPF1_RCP_BYPASS_FACILITY                   0x04
#define SCCB_CPF1_REGION_RELOCATE_FACILITY              0x02
#define SCCB_CPF1_EXPEDITE_TIMER_PROCESSING             0x01
#define SCCB_CPF2_VECTOR_FEATURE_INSTALLED              0x80
#define SCCB_CPF2_VECTOR_FEATURE_CONNECTED              0x40
#define SCCB_CPF2_VECTOR_FEATURE_STANDBY_STATE          0x20
#define SCCB_CPF2_CRYPTO_FEATURE_ACCESSED               0x10
#define SCCB_CPF2_EXPEDITE_RUN_PROCESSING               0x04
#define SCCB_CPF3_PRIVATE_SPACE_FEATURE                 0x80
#define SCCB_CPF3_FETCH_ONLY_BIT                        0x40
#define SCCB_CPF3_PER2_INSTALLED                        0x01
#define SCCB_CPF4_OMISION_GR_ALTERATION_370             0x80
#define SCCB_CPF5_GUEST_WAIT_STATE_ASSIST               0x40
#define SCCB_CPF13_CRYPTO_UNIT_ID                       0x01

/* HSA information array entry */
typedef struct _SCCB_HSA_INFO {
        HWORD   hssz;                   /* Size of HSA in 4K blocks  */
        FWORD   ahsa;                   /* Address of HSA            */
    } SCCB_HSA_INFO;

/* MPF information array entry */
typedef struct _SCCB_MPF_INFO {
        HWORD   mpfy;                   /* MPF info array entry      */
    } SCCB_MPF_INFO;

/* Channel path information data area */
typedef struct _SCCB_CHP_INFO {
#ifdef FEATURE_CHANNEL_SUBSYSTEM
        BYTE    installed[32];          /* Channels installed bits   */
        BYTE    standby[32];            /* Channels standby bits     */
        BYTE    online[32];             /* Channels online bits      */
#else /*!FEATURE_CHANNEL_SUBSYSTEM*/
        BYTE    chanset0a[32];          /* 370 channel set 0A        */
        BYTE    chanset1a[32];          /* 370 channel set 1A        */
        BYTE    chanset0b[32];          /* 370 channel set 0B        */
        BYTE    chanset1b[32];          /* 370 channel set 1B        */
        BYTE    csconfig;               /* Channel set configuration */
        BYTE    resv[23];               /* Reserved, set to zero     */
#endif /*!FEATURE_CHANNEL_SUBSYSTEM*/
    } SCCB_CHP_INFO;

/* Read Channel Subsystem Information data area */
typedef struct _SCCB_CSI_INFO {
        BYTE    csif[8];                /* Channel Subsystem installed
                                           facility field            */
        BYTE    resv[48];
    } SCCB_CSI_INFO;

/* Bit definitions for channel subsystem installed facilities */
#define SCCB_CSI0_CANCEL_IO_REQUEST_FACILITY            0x02
#define SCCB_CSI0_CONCURRENT_SENSE_FACILITY             0x01

#ifdef FEATURE_SYSTEM_CONSOLE
/* Write Event Mask */
typedef struct _SCCB_EVENT_MASK {
        HWORD   reserved;
        HWORD   length;                 /* Event mask length         */
        BYTE    masks[32];              /* Event masks               */
//      FWORD   cp_recv_mask;           /* These mask fields have    */
//      FWORD   cp_send_mask;           /* the length defined by     */
//      FWORD   sclp_recv_mask;         /* the length halfword       */
#define SCCB_EVENT_SUPP_RECV_MASK       0x40800000
//      FWORD   sclp_send_mask;
#define SCCB_EVENT_SUPP_SEND_MASK       0x80800000
    } SCCB_EVENT_MASK;

/* Read/Write Event Data Header */
typedef struct _SCCB_EVD_HDR {
        HWORD   totlen;                 /* Event Data Buffer total
                                           length                    */
        BYTE    type;
#define SCCB_EVD_TYPE_OPCMD     0x01    /* Operator command          */
#define SCCB_EVD_TYPE_MSG       0x02    /* Message from Control Pgm  */
#define SCCB_EVD_TYPE_PRIOR     0x09    /* Priority message/command  */
        BYTE    flag;
#define SCCB_EVD_FLAG_PROC      0x80    /* Event successful          */
        HWORD   resv;                   /* Reserved for future use   */
    } SCCB_EVD_HDR;

/* Read/Write Event Data Buffer */
typedef struct _SCCB_EVD_BK {
        HWORD   msglen;
        BYTE    const1[51];
        HWORD   cplen;                  /* CP message length         */
        BYTE    const2[24];
        HWORD   tdlen;                  /* Text Data length          */
        BYTE    const3[2];
        BYTE    sdtlen;
        BYTE    const4;                 /* Self defining tag         */
        BYTE    tmlen;
        BYTE    const5;                 /* Text Message format       */
//      BYTE    txtmsg[n];
    } SCCB_EVD_BK;

/* Message Control Data Block */
typedef struct _SCCB_MCD_BK {
        HWORD   length;                 /* Total length of MCD       */
        HWORD   type;                   /* Type must be 0x0001       */
        FWORD   tag;                    /* Tag must be 0xD4C4C240    */
        FWORD   revcd;                  /* Revision code 0x00000001  */
    } SCCB_MCD_BK;

/* Message Control Data Block Header */
typedef struct _SCCB_OBJ_HDR {
        HWORD   length;                 /* Total length of OBJ       */
        HWORD   type;                   /* Object type               */
#define SCCB_OBJ_TYPE_GENERAL   0x0001  /* General Object            */
#define SCCB_OBJ_TYPE_CPO       0x0002  /* Control Program Object    */
#define SCCB_OBJ_TYPE_NLS       0x0003  /* NLS data Object           */
#define SCCB_OBJ_TYPE_MESSAGE   0x0004  /* Message Text Object       */
    } SCCB_OBJ_HDR;

/* Message Control Data Block Message Text Object */
typedef struct _SCCB_MTO_BK {
        HWORD   ltflag;                 /* Line type flag            */
#define SCCB_MTO_LTFLG0_CNTL    0x80    /* Control text line         */
#define SCCB_MTO_LTFLG0_LABEL   0x40    /* Label text line           */
#define SCCB_MTO_LTFLG0_DATA    0x20    /* Data text line            */
#define SCCB_MTO_LTFLG0_END     0x10    /* Last line of message      */
#define SCCB_MTO_LTFLG0_PROMPT  0x08    /* Prompt line - response
                                           requested (WTOR)          */
#define SCCB_MTO_LTFLG0_DBCS    0x04    /* DBCS text                 */
#define SCCB_MTO_LTFLG0_MIX     0x02    /* Mixed SBCS/DBCS text      */
#define SCCB_MTO_LTFLG1_OVER    0x01    /* Foreground presentation
                                           field override            */
        FWORD   presattr;               /* Presentation Attribute
                                           Byte 0 - control
                                           Byte 1 - color
                                           Byte 2 - highlighting
                                           Byte 3 - intensity        */
#define SCCB_MTO_PRATTR0_ALARM  0x80    /* Sound alarm (console)     */
#define SCCB_MTO_PRATTR3_HIGH   0xE8    /* Highlighted               */
#define SCCB_MTO_PRATTR3_NORM   0xE4    /* Normal                    */
    } SCCB_MTO_BK;

/* Message Control Data Block General Object */
typedef struct _SCCB_MGO_BK {
        FWORD   seq;                    /* Message DOM ID            */
        BYTE    time[11];               /* C'HH.MM.SS.th'            */
        BYTE    resv1;
        BYTE    date[7];                /* C'YYYYDDD'                */
        BYTE    resv2;
        FWORD   mflag;                  /* Message Flags             */
#define SCCB_MGO_MFLAG0_DOM     0x80    /* Delete Operator Message   */
#define SCCB_MGO_MFLAG0_ALARM   0x40    /* Sound the SCLP alarm      */
#define SCCB_MGO_MFLAG0_HOLD    0x20    /* Hold message until DOM    */
        FWORD   presattr;               /* Presentation Attribute
                                           Byte 0 - control
                                           Byte 1 - color
                                           Byte 2 - highlighting
                                           Byte 3 - intensity        */
#define SCCB_MGO_PRATTR0_ALARM  0x80    /* Sound alarm (console)     */
#define SCCB_MGO_PRATTR3_HIGH   0xE8    /* Highlighted               */
#define SCCB_MGO_PRATTR3_NORM   0xE4    /* Normal                    */
        FWORD   bckattr;                /* Background presentation
                                           attributes - covers all
                                           message-test foreground
                                           presentation attribute
                                           field overrides           */
        BYTE    sysname[8];             /* Originating system name   */
        BYTE    jobname[8];             /* Jobname or guestname      */
    } SCCB_MGO_BK;

/* Message Control Data Block NLS Object */
typedef struct _SCCB_NLS_BK {
        HWORD   scpgid;                 /* CPGID for SBCS (def 037)  */
        HWORD   scpsgid;                /* CPSGID for SBCS (def 637) */
        HWORD   dcpgid;                 /* CPGID for DBCS (def 037)  */
        HWORD   dcpsgid;                /* CPSGID for DBCS (def 637) */
    } SCCB_NLS_BK;
#endif /*FEATURE_SYSTEM_CONSOLE*/

#ifdef FEATURE_EXPANDED_STORAGE
typedef struct _SCCB_XST_INFO {
        HWORD   elmid;                  /* Extended storage element
                                                                id   */
        BYTE    resv1[6];
        FWORD   elmsin;                 /* Starting increment number */
        FWORD   elmein;                 /* Ending increment number   */
        BYTE    elmchar;                /* Element characteristics   */
#define SCCB_XST_INFO_ELMCHAR_REQ 0x80; /* Required element          */
        BYTE    resv2[39];
    } SCCB_XST_INFO;

typedef struct _SCCB_XST_MAP {
        FWORD   incnum;                 /* Increment number          */
        FWORD   resv;
//      BYTE    map[];                  /* Bitmap of all usable
//                                         expanded storage blocks   */
    } SCCB_XST_MAP;
#endif /*FEATURE_EXPANDED_STORAGE*/

/*-------------------------------------------------------------------*/
/* Process service call instruction and return condition code        */
/*-------------------------------------------------------------------*/
int service_call (U32 sclp_command, U32 sccb_real_addr, REGS *regs)
{
int             i;                      /* Array subscript           */
int             realmb;                 /* Real storage size in MB   */
U32             sccb_absolute_addr;     /* Absolute address of SCCB  */
int             sccblen;                /* Length of SCCB            */
SCCB_HEADER    *sccb;                   /* -> SCCB header            */
SCCB_SCP_INFO  *sccbscp;                /* -> SCCB SCP information   */
SCCB_CPU_INFO  *sccbcpu;                /* -> SCCB CPU information   */
SCCB_CHP_INFO  *sccbchp;                /* -> SCCB channel path info */
SCCB_CSI_INFO  *sccbcsi;                /* -> SCCB channel subsys inf*/
U16             offset;                 /* Offset from start of SCCB */
#ifdef FEATURE_CHANNEL_SUBSYSTEM
DEVBLK         *dev;                    /* Used to find CHPIDs       */
int             chpbyte;                /* Offset to byte for CHPID  */
int             chpbit;                 /* Bit number for CHPID      */
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/
#ifdef FEATURE_SYSTEM_CONSOLE
SCCB_EVENT_MASK*evd_mask;               /* Event mask                */
SCCB_EVD_HDR   *evd_hdr;                /* Event header              */
int             evd_len;                /* Length of event data      */
SCCB_EVD_BK    *evd_bk;                 /* Event data                */
SCCB_MCD_BK    *mcd_bk;                 /* Message Control Data      */
int             mcd_len;                /* Length of MCD             */
SCCB_OBJ_HDR   *obj_hdr;                /* Object Header             */
int             obj_len;                /* Length of Object          */
int             obj_type;               /* Object type               */
SCCB_MTO_BK    *mto_bk;                 /* Message Text Object       */
BYTE           *event_msg;              /* Message Text pointer      */
int             event_msglen;           /* Message Text length       */
BYTE            message[4089];          /* Maximum event data buffer
                                           length plus one for \0    */
static BYTE     const1_template[] = {
        0x13,0x10,                      /* MDS message unit          */
        0x00,0x25,0x13,0x11,            /* MDS routine info          */
             0x0E,0x81,                 /* origin location name      */
                  0x03,0x01,0x00,       /* Net ID                    */
                  0x03,0x02,0x00,       /* NAU Name                  */
                  0x06,0x03,0x00,0x00,0x00,0x00,  /* Appl id         */
             0x0E,0x82,                 /* Destinition location name */
                  0x03,0x01,0x00,       /* Net ID                    */
                  0x03,0x02,0x00,       /* NAU Name                  */
                  0x06,0x03,0x00,0x00,0x00,0x00,  /* Appl id         */
             0x05,0x90,0x00,0x00,0x00,  /* Flags (MDS type = req)    */
        0x00,0x0C,0x15,0x49,            /* Agent unit-of-work        */
             0x08,0x01,                 /* Requestor loc name        */
                  0x03,0x01,0x00,       /* Requestor Net ID          */
                  0x03,0x02,0x00        /* Requestor Node ID         */
        };

static BYTE    const2_template[] = {
        0x12,0x12,                      /* CP-MSU                    */
        0x00,0x12,0x15,0x4D,            /* RTI                       */
             0x0E,0x06,                 /* Name List                 */
                  0x06,0x10,0x00,0x03,0x00,0x00,  /* Cascaded
                                                       resource list */
                  0x06,0x60,0xD6,0xC3,0xC6,0xC1,  /* OAN (C'OCFA')   */
        0x00,0x04,0x80,0x70             /* Operate request           */
        };

static BYTE    const3_template[] = {
        0x13,0x20                       /* Text data                 */
        };

static BYTE    const4_template = {
        0x31                            /* Self-defining             */
        };

static BYTE    const5_template = {
        0x30                            /* Text data                 */
        };

int             masklen;                /* Length of event mask      */
#endif /*FEATURE_SYSTEM_CONSOLE*/

#ifdef FEATURE_EXPANDED_STORAGE
SCCB_XST_MAP    *sccbxmap;              /* Xstore usability map      */
int             xstincnum;              /* Number of expanded storage
                                                         increments  */
int             xstblkinc;              /* Number of expanded storage
                                               blocks per increment  */
BYTE            *xstmap;                /* Xstore bitmap, zero means
                                                           available */
#endif /*FEATURE_EXPANDED_STORAGE*/

    /* Obtain the absolute address of the SCCB */
    sccb_absolute_addr = APPLY_PREFIXING (sccb_real_addr, regs->pxr);

    /* Program check if SCCB is not on a doubleword boundary */
    if ( sccb_absolute_addr & 0x00000007 )
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 3;
    }

    /* Program check if SCCB is outside main storage */
    if ( sccb_absolute_addr >= sysblk.mainsize )
    {
        program_check (regs, PGM_ADDRESSING_EXCEPTION);
        return 3;
    }

//  /*debug*/logmsg("Service call %8.8X SCCB=%8.8X\n",
//  /*debug*/       sclp_command, sccb_absolute_addr);

    /* Point to service call control block */
    sccb = (SCCB_HEADER*)(sysblk.mainstor + sccb_absolute_addr);

    /* Load SCCB length from header */
    sccblen = (sccb->length[0] << 8) | sccb->length[1];

    /* Set the main storage reference bit */
    STORAGE_KEY(sccb_absolute_addr) |= STORKEY_REF;

    /* Program check if end of SCCB falls outside main storage */
    if ( sysblk.mainsize - sccblen < sccb_absolute_addr )
    {
        program_check (regs, PGM_ADDRESSING_EXCEPTION);
        return 3;
    }

    /* Obtain lock if immediate response is not requested */
    if (!(sccb->flag & SCCB_FLAG_SYNC)
        || (sclp_command & SCLP_COMMAND_CLASS) == 0x01)
    {
        /* Obtain the interrupt lock */
        obtain_lock (&sysblk.intlock);

        /* If a service signal is pending then return condition
           code 2 to indicate that service processor is busy */
        if (sysblk.servsig)
        {
            release_lock (&sysblk.intlock);
            return 2;
        }
    }

    /* Test SCLP command word */
    switch (sclp_command & SCLP_COMMAND_MASK) {

    case SCLP_READ_SCP_INFO:

        /* Set the main storage change bit */
        STORAGE_KEY(sccb_absolute_addr) |= STORKEY_CHANGE;

        /* Set response code X'0100' if SCCB crosses a page boundary */
        if ((sccb_absolute_addr & STORAGE_KEY_PAGEMASK) !=
            ((sccb_absolute_addr + sccblen - 1) & STORAGE_KEY_PAGEMASK))
        {
            sccb->reas = SCCB_REAS_NOT_PGBNDRY;
            sccb->resp = SCCB_RESP_BLOCK_ERROR;
            break;
        }

        /* Set response code X'0300' if SCCB length
           is insufficient to contain SCP info */
#ifdef FEATURE_CPU_RECONFIG
        if ( sccblen < sizeof(SCCB_HEADER) + sizeof(SCCB_SCP_INFO)
                + (sizeof(SCCB_CPU_INFO) * MAX_CPU_ENGINES))
#else /*!FEATURE_CPU_RECONFIG*/
        if ( sccblen < sizeof(SCCB_HEADER) + sizeof(SCCB_SCP_INFO)
                + (sizeof(SCCB_CPU_INFO) * sysblk.numcpu))
#endif /*!FEATURE_CPU_RECONFIG*/
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

#ifdef FEATURE_EXPANDED_STORAGE
        /* Set expanded storage size in SCCB */
        xstincnum = (sysblk.xpndsize << XSTORE_PAGESHIFT)
                        / XSTORE_INCREMENT_SIZE;
        sccbscp->xpndinum[0] = (xstincnum & 0xFF0000) >> 24;
        sccbscp->xpndinum[1] = (xstincnum & 0xFF00) >> 16;
        sccbscp->xpndinum[2] = (xstincnum & 0xFF) >> 8;
        sccbscp->xpndinum[3] = xstincnum & 0xFF;
        xstblkinc = XSTORE_INCREMENT_SIZE >> XSTORE_PAGESHIFT;
        sccbscp->xpndsz4K[0] = (xstblkinc & 0xFF000000) >> 24;
        sccbscp->xpndsz4K[1] = (xstblkinc & 0xFF0000) >> 16;
        sccbscp->xpndsz4K[2] = (xstblkinc & 0xFF00) >> 8;
        sccbscp->xpndsz4K[3] = xstblkinc & 0xFF;
#endif /*FEATURE_EXPANDED_STORAGE*/

#ifdef FEATURE_VECTOR_FACILITY
        /* Set the Vector section size in the SCCB */
        sccbscp->vectssiz[0] = (VECTOR_SECTION_SIZE & 0xFF00) >> 8;
        sccbscp->vectssiz[1] = VECTOR_SECTION_SIZE & 0xFF;
        /* Set the Vector partial sum number in the SCCB */
        sccbscp->vectpsum[0] = (VECTOR_PARTIAL_SUM_NUMBER & 0xFF00) >> 8;
        sccbscp->vectpsum[1] = VECTOR_PARTIAL_SUM_NUMBER & 0xFF;
#endif /*FEATURE_VECTOR_FACILITY*/

#ifdef FEATURE_CPU_RECONFIG
        /* Set CPU array count and offset in SCCB */
        sccbscp->numcpu[0] = (MAX_CPU_ENGINES & 0xFF00) >> 8;
        sccbscp->numcpu[1] = MAX_CPU_ENGINES & 0xFF;
#else /*!FEATURE_CPU_RECONFIG*/
        /* Set CPU array count and offset in SCCB */
        sccbscp->numcpu[0] = (sysblk.numcpu & 0xFF00) >> 8;
        sccbscp->numcpu[1] = sysblk.numcpu & 0xFF;
#endif /*!FEATURE_CPU_RECONFIG*/
        offset = sizeof(SCCB_HEADER) + sizeof(SCCB_SCP_INFO);
        sccbscp->offcpu[0] = (offset & 0xFF00) >> 8;
        sccbscp->offcpu[1] = offset & 0xFF;

        /* Set HSA array count and offset in SCCB */
        sccbscp->numhsa[0] = 0;
        sccbscp->numhsa[1] = 0;
#ifdef FEATURE_CPU_RECONFIG
        offset += sizeof(SCCB_CPU_INFO) * MAX_CPU_ENGINES;
#else /*!FEATURE_CPU_RECONFIG*/
        offset += sizeof(SCCB_CPU_INFO) * sysblk.numcpu;
#endif /*!FEATURE_CPU_RECONFIG*/
        sccbscp->offhsa[0] = (offset & 0xFF00) >> 8;
        sccbscp->offhsa[1] = offset & 0xFF;

        /* Move IPL load parameter to SCCB */
        memcpy (sccbscp->loadparm, sysblk.loadparm, 8);

        /* Set installed features bit mask in SCCB */
        sccbscp->ifm[0] = 0
                        | SCCB_IFM0_CHANNEL_PATH_INFORMATION
                        | SCCB_IFM0_CHANNEL_PATH_SUBSYSTEM_COMMAND
//                      | SCCB_IFM0_CHANNEL_PATH_RECONFIG
//                      | SCCB_IFM0_CPU_INFORMATION
#ifdef FEATURE_CPU_RECONFIG
                        | SCCB_IFM0_CPU_RECONFIG
#endif /*FEATURE_CPU_RECONFIG*/
                        ;
        sccbscp->ifm[1] = 0
//                      | SCCB_IFM1_SIGNAL_ALARM
//                      | SCCB_IFM1_WRITE_OPERATOR_MESSAGE
//                      | SCCB_IFM1_STORE_STATUS_ON_LOAD
//                      | SCCB_IFM1_RESTART_REASONS
//                      | SCCB_IFM1_INSTRUCTION_ADDRESS_TRACE_BUFFER
                        | SCCB_IFM1_LOAD_PARAMETER
                        ;
        sccbscp->ifm[2] = 0
//                      | SCCB_IFM2_REAL_STORAGE_INCREMENT_RECONFIG
//                      | SCCB_IFM2_REAL_STORAGE_ELEMENT_INFO
//                      | SCCB_IFM2_REAL_STORAGE_ELEMENT_RECONFIG
//                      | SCCB_IFM2_COPY_AND_REASSIGN_STORAGE
#ifdef FEATURE_EXPANDED_STORAGE
                        | SCCB_IFM2_EXTENDED_STORAGE_USABILITY_MAP
#endif /*FEATURE_EXPANDED_STORAGE*/
//                      | SCCB_IFM2_EXTENDED_STORAGE_ELEMENT_INFO
//                      | SCCB_IFM2_EXTENDED_STORAGE_ELEMENT_RECONFIG
                        ;
        sccbscp->ifm[3] = 0
#if defined(FEATURE_VECTOR_FACILITY) && defined(FEATURE_CPU_RECONFIG)
                        | SCCB_IFM3_VECTOR_FEATURE_RECONFIG
#endif /*FEATURE_VECTOR_FACILITY*/
#ifdef FEATURE_SYSTEM_CONSOLE
                        | SCCB_IFM3_READ_WRITE_EVENT_FEATURE
#endif /*FEATURE_SYSTEM_CONSOLE*/
//                      | SCCB_IFM3_READ_RESOURCE_GROUP_INFO
                        ;
        sccbscp->cfg[0] = 0
                        | SCCB_CFG0_LOGICALLY_PARTITIONED
#ifdef FEATURE_SUPPRESSION_ON_PROTECTION
                        | SCCB_CFG0_SUPPRESSION_ON_PROTECTION
#endif /*FEATURE_SUPPRESSION_ON_PROTECTION*/
//                      | SCCB_CFG0_INITIATE_RESET
//                      | SCCB_CFG0_STORE_CHANNEL_SUBSYS_CHARACTERISTICS
//                      | SCCB_CFG0_MVPG_FOR_ALL_GUESTS
//                      | SCCB_CFG0_UNKNOWN_BUT_SET_UNDER_VM
                        ;
        sccbscp->cfg[1] = 0
//                      | SCCB_CFG1_CSLO
                        ;
        sccbscp->cfg[2] = 0
//                      | SCCB_CFG2_DEVICE_ACTIVE_ONLY_MEASUREMENT
#ifdef FEATURE_CALLED_SPACE_IDENTIFICATION
                        | SCCB_CFG2_CALLED_SPACE_IDENTIFICATION
#endif /*FEATURE_CALLED_SPACE_IDENTIFICATION*/
#ifdef FEATURE_CHECKSUM_INSTRUCTION
                        | SCCB_CFG2_CHECKSUM_INSTRUCTION
#endif /*FEATURE_CHECKSUM_INSTRUCTION*/
                        ;
        sccbscp->cfg[3] = 0
//                      | SCCB_CFG3_RESUME_PROGRAM
//                      | SCCB_CFG3_PERFORM_LOCKED_OPERATION
#ifdef FEATURE_IMMEDIATE_AND_RELATIVE
                        | SCCB_CFG3_IMMEDIATE_AND_RELATIVE
#endif /*FEATURE_IMMEDIATE_AND_RELATIVE*/
#ifdef FEATURE_COMPARE_AND_MOVE_EXTENDED
                        | SCCB_CFG3_COMPARE_AND_MOVE_EXTENDED
#endif /*FEATURE_COMPARE_AND_MOVE_EXTENDED*/
#ifdef FEATURE_BRANCH_AND_SET_AUTHORITY
                        | SCCB_CFG3_BRANCH_AND_SET_AUTHORITY
#endif /*FEATURE_BRANCH_AND_SET_AUTHORITY*/
//                      | SCCB_CFG3_EXTENDED_FLOATING_POINT
//                      | SCCB_CFG3_UNKNOWN_BUT_SET_UNDER_VM
                        ;
        sccbscp->cfg[4] = 0
#ifdef FEATURE_EXTENDED_TOD_CLOCK
                        | SCCB_CFG4_EXTENDED_TOD_CLOCK
#endif /*FEATURE_EXTENDED_TOD_CLOCK*/
//                      | SCCB_CFG4_EXTENDED_TRANSLATION
//                      | SCCB_CFG4_STORE_SYSTEM_INFORMATION
                        ;

        /* Build the CPU information array after the SCP info */
        sccbcpu = (SCCB_CPU_INFO*)(sccbscp+1);
#ifdef FEATURE_CPU_RECONFIG
        for (i = 0; i < MAX_CPU_ENGINES; i++, sccbcpu++)
#else /*!FEATURE_CPU_RECONFIG*/
        for (i = 0; i < sysblk.numcpu; i++, sccbcpu++)
#endif /*!FEATURE_CPU_RECONFIG*/
        {
            memset (sccbcpu, 0, sizeof(SCCB_CPU_INFO));
            sccbcpu->cpa = sysblk.regs[i].cpuad;
            sccbcpu->tod = 0;
            sccbcpu->cpf[0] = 0
//                          | SCCB_CPF0_SIE_370_MODE
//                          | SCCB_CPF0_SIE_XA_MODE
//                          | SCCB_CPF0_SIE_SET_II_370_MODE
//                          | SCCB_CPF0_SIE_SET_II_XA_MODE
//                          | SCCB_CPF0_SIE_NEW_INTERCEPT_FORMAT
//                          | SCCB_CPF0_STORAGE_KEY_ASSIST
//                          | SCCB_CPF0_MULTIPLE_CONTROLLED_DATA_SPACE
                            ;
            sccbcpu->cpf[1] = 0
//                          | SCCB_CPF1_IO_INTERPRETATION_LEVEL_2
//                          | SCCB_CPF1_GUEST_PER_ENHANCED
//                          | SCCB_CPF1_SIGP_INTERPRETATION_ASSIST
//                          | SCCB_CPF1_RCP_BYPASS_FACILITY
//                          | SCCB_CPF1_REGION_RELOCATE_FACILITY
//                          | SCCB_CPF1_EXPEDITE_TIMER_PROCESSING
                            ;
            sccbcpu->cpf[2] = 0
//                          | SCCB_CPF2_CRYPTO_FEATURE_ACCESSED
//                          | SCCB_CPF2_EXPEDITE_RUN_PROCESSING
                            ;

#ifdef FEATURE_VECTOR_FACILITY
#ifndef FEATURE_CPU_RECONFIG
            if(sysblk.regs[i].vf.online)
#endif /*!FEATURE_CPU_RECONFIG*/
              sccbcpu->cpf[2] |= SCCB_CPF2_VECTOR_FEATURE_INSTALLED;
            if(sysblk.regs[i].vf.online)
                sccbcpu->cpf[2] |= SCCB_CPF2_VECTOR_FEATURE_CONNECTED;
#ifdef FEATURE_CPU_RECONFIG
            else
                sccbcpu->cpf[2] |= SCCB_CPF2_VECTOR_FEATURE_STANDBY_STATE;
#endif /*FEATURE_CPU_RECONFIG*/
#endif /*FEATURE_VECTOR_FACILITY*/

            sccbcpu->cpf[3] = 0
#ifdef FEATURE_PRIVATE_SPACE
                            | SCCB_CPF3_PRIVATE_SPACE_FEATURE
                            | SCCB_CPF3_FETCH_ONLY_BIT
#endif /*FEATURE_PRIVATE_SPACE*/
//                          | SCCB_CPF3_PER2_INSTALLED
                            ;
            sccbcpu->cpf[4] = 0
                            | SCCB_CPF4_OMISION_GR_ALTERATION_370
                            ;
            sccbcpu->cpf[5] = 0
//                          | SCCB_CPF5_GUEST_WAIT_STATE_ASSIST
                            ;
            sccbcpu->cpf[13] = 0
//                          | SCCB_CPF13_CRYPTO_UNIT_ID
                            ;
        }

        /* Set response code X'0010' in SCCB header */
        sccb->reas = SCCB_REAS_NONE;
        sccb->resp = SCCB_RESP_INFO;

        break;

    case SCLP_READ_CHP_INFO:

        /* Set the main storage change bit */
        STORAGE_KEY(sccb_absolute_addr) |= STORKEY_CHANGE;

        /* Set response code X'0100' if SCCB crosses a page boundary */
        if ((sccb_absolute_addr & STORAGE_KEY_PAGEMASK) !=
            ((sccb_absolute_addr + sccblen - 1) & STORAGE_KEY_PAGEMASK))
        {
            sccb->reas = SCCB_REAS_NOT_PGBNDRY;
            sccb->resp = SCCB_RESP_BLOCK_ERROR;
            break;
        }

        /* Set response code X'0300' if SCCB length
           is insufficient to contain channel path info */
        if ( sccblen < sizeof(SCCB_HEADER) + sizeof(SCCB_CHP_INFO))
        {
            sccb->reas = SCCB_REAS_TOO_SHORT;
            sccb->resp = SCCB_RESP_BLOCK_ERROR;
            break;
        }

        /* Point to SCCB data area following SCCB header */
        sccbchp = (SCCB_CHP_INFO*)(sccb+1);
        memset (sccbchp, 0, sizeof(SCCB_CHP_INFO));

#ifdef FEATURE_CHANNEL_SUBSYSTEM
        /* Identify CHPIDs installed, standby, and online */
        for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
        {
            chpbyte = dev->devnum >> 11;
            chpbit = (dev->devnum >> 8) & 7;

            sccbchp->installed[chpbyte] |= 0x80 >> chpbit;
            if (dev->pmcw.flag5 & PMCW5_V)
                sccbchp->online[chpbyte] |= 0x80 >> chpbit;
            else
                sccbchp->standby[chpbyte] |= 0x80 >> chpbit;
        }
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

#ifdef FEATURE_S370_CHANNEL
        /* For S/370, initialize identifiers for channel set 0A */
        for (i = 0; i < 16; i++)
        {
            sccbchp->chanset0a[2*i] = 0x80;
            sccbchp->chanset0a[2*i+1] = i;
        } /* end for(i) */

        /* Set the channel set configuration byte */
        sccbchp->csconfig = 0xC0;
#endif /*FEATURE_S370_CHANNEL*/

        /* Set response code X'0010' in SCCB header */
        sccb->reas = SCCB_REAS_NONE;
        sccb->resp = SCCB_RESP_INFO;

        break;

    case SCLP_READ_CSI_INFO:

        /* Set the main storage change bit */
        STORAGE_KEY(sccb_absolute_addr) |= STORKEY_CHANGE;

        /* Set response code X'0100' if SCCB crosses a page boundary */
        if ((sccb_absolute_addr & STORAGE_KEY_PAGEMASK) !=
            ((sccb_absolute_addr + sccblen - 1) & STORAGE_KEY_PAGEMASK))
        {
            sccb->reas = SCCB_REAS_NOT_PGBNDRY;
            sccb->resp = SCCB_RESP_BLOCK_ERROR;
            break;
        }

        /* Set response code X'0300' if SCCB length
           is insufficient to contain channel path info */
        if ( sccblen < sizeof(SCCB_HEADER) + sizeof(SCCB_CSI_INFO))
        {
            sccb->reas = SCCB_REAS_TOO_SHORT;
            sccb->resp = SCCB_RESP_BLOCK_ERROR;
            break;
        }

        /* Point to SCCB data area following SCCB header */
        sccbcsi = (SCCB_CSI_INFO*)(sccb+1);
        memset (sccbcsi, 0, sizeof(SCCB_CSI_INFO));

        sccbcsi->csif[0] = 0
//                      | SCCB_CSI0_CANCEL_IO_REQUEST_FACILITY
                        | SCCB_CSI0_CONCURRENT_SENSE_FACILITY
                        ;

        /* Set response code X'0010' in SCCB header */
        sccb->reas = SCCB_REAS_NONE;
        sccb->resp = SCCB_RESP_INFO;

        break;

#ifdef FEATURE_SYSTEM_CONSOLE
    case SCLP_WRITE_EVENT_DATA:

        /* Set the main storage change bit */
        STORAGE_KEY(sccb_absolute_addr) |= STORKEY_CHANGE;

        /* Set response code X'0100' if SCCB crosses a page boundary */
        if ((sccb_absolute_addr & STORAGE_KEY_PAGEMASK) !=
            ((sccb_absolute_addr + sccblen - 1) & STORAGE_KEY_PAGEMASK))
        {
            sccb->reas = SCCB_REAS_NOT_PGBNDRY;
            sccb->resp = SCCB_RESP_BLOCK_ERROR;
            break;
        }

        /* Point to SCCB data area following SCCB header */
        evd_hdr = (SCCB_EVD_HDR*)(sccb+1);
        evd_len = (evd_hdr->totlen[0] << 8) | evd_hdr->totlen[1];

        if (evd_hdr->type != SCCB_EVD_TYPE_MSG &&
            evd_hdr->type != SCCB_EVD_TYPE_PRIOR)
        {
            sccb->reas = SCCB_REAS_NONE;
            sccb->resp = SCCB_RESP_REJECT;
            break;
        }

        /* Indicate Event Processed */
        evd_hdr->flag |= SCCB_EVD_FLAG_PROC;

        /* Point to the Message Control Data Block */
        mcd_bk = (SCCB_MCD_BK*)(evd_hdr+1);
        mcd_len = (mcd_bk->length[0] << 8) | mcd_bk->length[1];

        obj_hdr = (SCCB_OBJ_HDR*)(mcd_bk+1);

        while (mcd_len > sizeof(SCCB_MCD_BK))
        {
            obj_len = (obj_hdr->length[0] << 8) | obj_hdr->length[1];
            obj_type = (obj_hdr->type[0] << 8) | obj_hdr->type[1];
            if (obj_type == SCCB_OBJ_TYPE_MESSAGE)
            {
                mto_bk = (SCCB_MTO_BK*)(obj_hdr+1);
                event_msg = (BYTE*)(mto_bk+1);
                event_msglen = obj_len -
                        (sizeof(SCCB_OBJ_HDR) + sizeof(SCCB_MTO_BK));
                if (event_msglen < 0)
                {
                    sccb->reas = SCCB_REAS_BUFF_LEN_ERR;
                    sccb->resp = SCCB_RESP_BUFF_LEN_ERR;
                    break;
                }

                /* Print line unless it is a response prompt */
                if (!(mto_bk->ltflag[0] & SCCB_MTO_LTFLG0_PROMPT))
                {
                    for (i = 0; i < event_msglen; i++)
                        message[i] = isprint(ebcdic_to_ascii[event_msg[i]]) ?
                            ebcdic_to_ascii[event_msg[i]] : 0x20;
                    message[event_msglen] = '\0';
                    logmsg ("%s\n", message);
// if(!memcmp(message,"*IEE479W",8)) regs->cpustate = CPUSTATE_STOPPING;
                }
            }
            mcd_len -= obj_len;
            (BYTE*)obj_hdr += obj_len;
        }

        /* Set response code X'0020' in SCCB header */
        sccb->reas = SCCB_REAS_NONE;
        sccb->resp = SCCB_RESP_COMPLETE;

        break;

    case SCLP_READ_EVENT_DATA:

        /* Set the main storage change bit */
        STORAGE_KEY(sccb_absolute_addr) |= STORKEY_CHANGE;

        /* Set response code X'0100' if SCCB crosses a page boundary */
        if ((sccb_absolute_addr & STORAGE_KEY_PAGEMASK) !=
            ((sccb_absolute_addr + sccblen - 1) & STORAGE_KEY_PAGEMASK))
        {
            sccb->reas = SCCB_REAS_NOT_PGBNDRY;
            sccb->resp = SCCB_RESP_BLOCK_ERROR;
            break;
        }

        /* Set response code X'62F0' if CP receive mask is zero */
        if (sysblk.cp_recv_mask == 0)
        {
            sccb->reas = SCCB_REAS_EVENTS_SUP;
            sccb->resp = SCCB_RESP_EVENTS_SUP;
            break;
        }

        /* Set response code X'60F0' if no outstanding events */
        event_msglen = strlen(sysblk.scpcmdstr);
        if (event_msglen == 0)
        {
            sccb->reas = SCCB_REAS_NO_EVENTS;
            sccb->resp = SCCB_RESP_NO_EVENTS;
            break;
        }

        /* Point to SCCB data area following SCCB header */
        evd_hdr = (SCCB_EVD_HDR*)(sccb+1);

        /* Point to the Event Data Block */
        evd_bk = (SCCB_EVD_BK*)(evd_hdr+1);

        /* Point to SCP command */
        event_msg = (BYTE*)(evd_bk+1);

        evd_len = event_msglen +
                        sizeof(SCCB_EVD_HDR) + sizeof(SCCB_EVD_BK);

        /* Set response code X'75F0' if SCCB length exceeded */
        if ((evd_len + sizeof(SCCB_HEADER)) > sccblen)
        {
            sccb->reas = SCCB_REAS_EXCEEDS_SCCB;
            sccb->resp = SCCB_RESP_EXCEEDS_SCCB;
            break;
        }

        /* Zero all fields */
        memset (evd_hdr, 0, evd_len);

        /* Update SCCB length field if variable request */
        if (sccb->type & SCCB_TYPE_VARIABLE)
        {
            /* Set new SCCB length */
            sccblen = evd_len + sizeof(SCCB_HEADER);
            sccb->length[0] = (sccblen >> 8) & 0xFF;
            sccb->length[1] = sccblen & 0xFF;
            sccb->type &= ~SCCB_TYPE_VARIABLE;
        }

        /* Set length in event header */
        evd_hdr->totlen[0] = (evd_len >> 8) & 0xFF;
        evd_hdr->totlen[1] = evd_len & 0xFF;

        /* Set type in event header */
        evd_hdr->type = sysblk.scpcmdtype ?
                        SCCB_EVD_TYPE_PRIOR : SCCB_EVD_TYPE_OPCMD;

        /* Set message length in event data block */
        i = evd_len - sizeof(SCCB_EVD_HDR);
        evd_bk->msglen[0] = (i >> 8) & 0xFF;
        evd_bk->msglen[1] = i & 0xFF;
        memcpy (evd_bk->const1, const1_template,
                                sizeof(const1_template));
        i -= sizeof(const1_template) + 2;
        evd_bk->cplen[0] = (i >> 8) & 0xFF;
        evd_bk->cplen[1] = i & 0xFF;
        memcpy (evd_bk->const2, const2_template,
                                sizeof(const2_template));
        i -= sizeof(const2_template) + 2;
        evd_bk->tdlen[0] = (i >> 8) & 0xFF;
        evd_bk->tdlen[1] = i & 0xFF;
        memcpy (evd_bk->const3, const3_template,
                                sizeof(const3_template));
        i -= sizeof(const3_template) + 2;
        evd_bk->sdtlen = i;
        evd_bk->const4 = const4_template;
        i -= 2;
        evd_bk->tmlen = i;
        evd_bk->const5 = const5_template;

        /* Copy and translate command */
        for (i = 0; i < event_msglen; i++)
                event_msg[i] = ascii_to_ebcdic[sysblk.scpcmdstr[i]];

        /* Clear the command string (It has been read) */
        sysblk.scpcmdstr[0] = '\0';

        /* Set response code X'0020' in SCCB header */
        sccb->reas = SCCB_REAS_NONE;
        sccb->resp = SCCB_RESP_COMPLETE;

        break;

    case SCLP_WRITE_EVENT_MASK:

        /* Set the main storage change bit */
        STORAGE_KEY(sccb_absolute_addr) |= STORKEY_CHANGE;

        /* Set response code X'0100' if SCCB crosses a page boundary */
        if ((sccb_absolute_addr & STORAGE_KEY_PAGEMASK) !=
            ((sccb_absolute_addr + sccblen - 1) & STORAGE_KEY_PAGEMASK))
        {
            sccb->reas = SCCB_REAS_NOT_PGBNDRY;
            sccb->resp = SCCB_RESP_BLOCK_ERROR;
            break;
        }

        /* Point to SCCB data area following SCCB header */
        evd_mask = (SCCB_EVENT_MASK*)(sccb+1);

        /* Get length of single mask field */
        masklen = (evd_mask->length[0] << 8) | evd_mask->length[1];

        for (i = 0; i < 4; i++)
        {
            sysblk.cp_recv_mask <<= 8;
            sysblk.cp_send_mask <<= 8;
            if (i < masklen)
            {
                sysblk.cp_recv_mask |= evd_mask->masks[i];
                sysblk.cp_send_mask |= evd_mask->masks[i + masklen];
            }
        }

        /* Initialize sclp send and receive masks */
        sysblk.sclp_recv_mask = SCCB_EVENT_SUPP_RECV_MASK;
        sysblk.sclp_send_mask = SCCB_EVENT_SUPP_SEND_MASK;

        /* Clear any pending command */
        sysblk.scpcmdstr[0] = '\0';

        /* Write the events that we support back */
        memset (&evd_mask->masks[2 * masklen], 0, 2 * masklen);
        for (i = 0; (i < 4) && (i < masklen); i++)
        {
            evd_mask->masks[i + (2 * masklen)] |=
                (sysblk.sclp_recv_mask >> ((3-i)*8)) & 0xFF;
            evd_mask->masks[i + (3 * masklen)] |=
                (sysblk.sclp_send_mask >> ((3-i)*8)) & 0xFF;
        }

        if (sysblk.cp_recv_mask != 0 || sysblk.cp_send_mask != 0)
            logmsg ("HHC701I SYSCONS interface active\n");
        else
            logmsg ("HHC702I SYSCONS interface inactive\n");

        /* Set response code X'0020' in SCCB header */
        sccb->reas = SCCB_REAS_NONE;
        sccb->resp = SCCB_RESP_COMPLETE;

        break;
#endif /*FEATURE_SYSTEM_CONSOLE*/

#ifdef FEATURE_EXPANDED_STORAGE
   case SCLP_READ_XST_MAP:

        /* Set the main storage change bit */
        STORAGE_KEY(sccb_absolute_addr) |= STORKEY_CHANGE;

        /* Set response code X'0100' if SCCB crosses a page boundary */
        if ((sccb_absolute_addr & STORAGE_KEY_PAGEMASK) !=
            ((sccb_absolute_addr + sccblen - 1) & STORAGE_KEY_PAGEMASK))
        {
            sccb->reas = SCCB_REAS_NOT_PGBNDRY;
            sccb->resp = SCCB_RESP_BLOCK_ERROR;
            break;
        }

        /* Calculate number of blocks per increment */
        xstblkinc = XSTORE_INCREMENT_SIZE / XSTORE_PAGESIZE;

        /* Set response code X'0300' if SCCB length
           is insufficient to contain xstore info */
        if ( sccblen < sizeof(SCCB_HEADER) + sizeof(SCCB_XST_MAP)
                + xstblkinc/8)
        {
            sccb->reas = SCCB_REAS_TOO_SHORT;
            sccb->resp = SCCB_RESP_BLOCK_ERROR;
            break;
        }

        /* Point to SCCB data area following SCCB header */
        sccbxmap = (SCCB_XST_MAP*)(sccb+1);

        /* Verify expanded storage increment number */
        xstincnum = (sysblk.xpndsize << XSTORE_PAGESHIFT)
                        / XSTORE_INCREMENT_SIZE;
        i = sccbxmap->incnum[0] << 24 | sccbxmap->incnum[1] << 16
          | sccbxmap->incnum[2] << 8 | sccbxmap->incnum[3];
        if ( i < 1 || i > xstincnum )
        {
            sccb->reas = SCCB_REAS_INVALID_RSC;
            sccb->resp = SCCB_RESP_REJECT;
            break;
        }

        /* Point to bitmap */
        xstmap = (BYTE*)(sccbxmap+1);

        /* Set all blocks available */
        memset (xstmap, 0x00, xstblkinc/8);

        /* Set response code X'0010' in SCCB header */
        sccb->reas = SCCB_REAS_NONE;
        sccb->resp = SCCB_RESP_INFO;

        break;

#endif /*FEATURE_EXPANDED_STORAGE*/

#ifdef FEATURE_CPU_RECONFIG

    case SCLP_CONFIGURE_CPU:

        i = (sclp_command & SCLP_RESOURCE_MASK) >> SCLP_RESOURCE_SHIFT;

        /* Return invalid resource in parm if target does not exist */
        if(i >= MAX_CPU_ENGINES)
        {
            sccb->reas = SCCB_REAS_INVALID_RSCP;
            sccb->resp = SCCB_RESP_REJECT;
            break;
        }

        /* Add cpu to the configuration */
        configure_cpu(sysblk.regs + i);

        /* Set response code X'0020' in SCCB header */
        sccb->reas = SCCB_REAS_NONE;
        sccb->resp = SCCB_RESP_COMPLETE;
        break;

    case SCLP_DECONFIGURE_CPU:

        i = (sclp_command & SCLP_RESOURCE_MASK) >> SCLP_RESOURCE_SHIFT;

        /* Return invalid resource in parm if target does not exist */
        if(i >= MAX_CPU_ENGINES)
        {
            sccb->reas = SCCB_REAS_INVALID_RSCP;
            sccb->resp = SCCB_RESP_REJECT;
            break;
        }

        /* Take cpu out of the configuration */
        deconfigure_cpu(sysblk.regs + i);

        /* Set response code X'0020' in SCCB header */
        sccb->reas = SCCB_REAS_NONE;
        sccb->resp = SCCB_RESP_COMPLETE;
        break;

#ifdef FEATURE_VECTOR_FACILITY

    case SCLP_DISCONNECT_VF:

        i = (sclp_command & SCLP_RESOURCE_MASK) >> SCLP_RESOURCE_SHIFT;

        /* Return invalid resource in parm if target does not exist */
        if(i >= MAX_CPU_ENGINES)
        {
            sccb->reas = SCCB_REAS_INVALID_RSCP;
            sccb->resp = SCCB_RESP_REJECT;
            break;
        }

        if(sysblk.regs[i].vf.online)
            logmsg("CPU%4.4X: Vector Facility configured offline\n",i);

        /* Take the VF out of the configuration */
        sysblk.regs[i].vf.online = 0;

        /* Set response code X'0020' in SCCB header */
        sccb->reas = SCCB_REAS_NONE;
        sccb->resp = SCCB_RESP_COMPLETE;
        break;

    case SCLP_CONNECT_VF:

        i = (sclp_command & SCLP_RESOURCE_MASK) >> SCLP_RESOURCE_SHIFT;

        /* Return invalid resource in parm if target does not exist */
        if(i >= MAX_CPU_ENGINES)
        {
            sccb->reas = SCCB_REAS_INVALID_RSCP;
            sccb->resp = SCCB_RESP_REJECT;
            break;
        }

        /* Return improper state if associated cpu is offline */
        if(!sysblk.regs[i].cpuonline)
        {
            sccb->reas = SCCB_REAS_IMPROPER_RSC;
            sccb->resp = SCCB_RESP_REJECT;
            break;
        }

        if(!sysblk.regs[i].vf.online)
            logmsg("CPU%4.4X: Vector Facility configured online\n",i);

        /* Mark the VF online to the CPU */
        sysblk.regs[i].vf.online = 1;

        /* Set response code X'0020' in SCCB header */
        sccb->reas = SCCB_REAS_NONE;
        sccb->resp = SCCB_RESP_COMPLETE;
        break;

#endif /*FEATURE_VECTOR_FACILITY*/

#endif /*FEATURE_CPU_RECONFIG*/

    default:

#if 0
        logmsg("Invalid service call command word:%8.8X SCCB=%8.8X\n",
            sclp_command, sccb_absolute_addr);

        logmsg("SCCB data area:\n");
        for(i = 0; i < sccblen; i++)
        {
            logmsg("%2.2X",sysblk.mainstor[sccb_real_addr + i]);
            if(i % 32 == 31)
                logmsg("\n");
            else
                if(i % 4 == 3)
                    logmsg(" ");
        }
        if(i % 32 != 31)
            logmsg("\n");
#endif    

        /* Set response code X'01F0' for invalid SCLP command */
        sccb->reas = SCCB_REAS_INVALID_CMD;
        sccb->resp = SCCB_RESP_REJECT;

        break;

    } /* end switch(sclp_command) */

    /* If immediate response is requested, return condition code 1 */
    if ((sccb->flag & SCCB_FLAG_SYNC)
        && (sclp_command & SCLP_COMMAND_CLASS) != 0x01)
        return 1;

    /* Set service signal external interrupt pending */
    sysblk.servparm = sccb_absolute_addr;
    sysblk.extpending = sysblk.servsig = 1;

    /* Release the interrupt lock */
    release_lock (&sysblk.intlock);

    /* Return condition code 0 */
    return 0;

} /* end function service_call */

#ifdef FEATURE_SYSTEM_CONSOLE
/*-------------------------------------------------------------------*/
/* Issue SCP command                                                 */
/*                                                                   */
/* This function is called from the control panel when the operator  */
/* enters an HMC system console SCP command or SCP priority message. */
/* The command is queued for processing by the SCLP_READ_EVENT_DATA  */
/* service call, and a service signal interrupt is made pending.     */
/*                                                                   */
/* Input:                                                            */
/*      command Null-terminated ASCII command string                 */
/*      priomsg 0=SCP command, 1=SCP priority message                */
/*-------------------------------------------------------------------*/
void scp_command (BYTE *command, int priomsg)
{
    /* Error if disabled for priority messages */
    if (priomsg && !(sysblk.cp_recv_mask & 0x00800000))
    {
        logmsg ("HHC703I SCP not receiving priority messages\n");
        return;
    }

    /* Error if disabled for commands */
    if (!priomsg && !(sysblk.cp_recv_mask & 0x80000000))
    {
        logmsg ("HHC704I SCP not receiving commands\n");
        return;
    }

    /* Error if command string is missing */
    if (strlen(command) < 1)
    {
        logmsg ("HHC705I No SCP command\n");
        return;
    }

    /* Obtain the interrupt lock */
    obtain_lock (&sysblk.intlock);

    /* If a service signal is pending then reject the command
       with message indicating that service processor is busy */
    if (sysblk.servsig)
    {
        logmsg ("HHC706I Service Processor busy\n");

        /* Release the interrupt lock */
        release_lock (&sysblk.intlock);
        return;
    }

    /* Save command string and message type for read event data */
    sysblk.scpcmdtype = priomsg;
    strncpy (sysblk.scpcmdstr, command, sizeof(sysblk.scpcmdstr));

    /* Ensure termination of the command string */
    sysblk.scpcmdstr[sizeof(sysblk.scpcmdstr)-1] = '\0';

    /* Set service signal interrupt pending for read event data */
    sysblk.servparm = 1;
    sysblk.extpending = sysblk.servsig = 1;

    /* Release the interrupt lock */
    release_lock (&sysblk.intlock);

} /* end function scp_command */
#endif /*FEATURE_SYSTEM_CONSOLE*/
