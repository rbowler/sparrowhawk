/* SERVICE.C    (c) Copyright Roger Bowler, 1999                     */
/*              ESA/390 Service Processor                            */

/*-------------------------------------------------------------------*/
/* This module implements service processor functions                */
/* for the Hercules ESA/390 emulator.                                */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Additional credits:                                               */
/*      Corrections contributed by Jan Jaeger                        */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Service Call Logical Processor command word definitions           */
/*-------------------------------------------------------------------*/
#define SCLP_READ_SCP_INFO      0x00020001
#define SCLP_READ_CHP_INFO      0x00030001
#define SCLP_READ_CSI_INFO      0x001C0001

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
#define SCCB_REAS_NOT_4KBNDRY   0x01    /* SCCB crosses 4K boundary  */
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
        BYTE    installed[32];          /* Channels installed bits   */
        BYTE    owned[32];              /* Channels owned bits       */
        BYTE    online[32];             /* Channels online bits      */
        BYTE    chanset0a[32];          /* 370 channel set 0A        */
        BYTE    chanset1a[32];          /* 370 channel set 1A        */
        BYTE    chanset0b[32];          /* 370 channel set 0B        */
        BYTE    chanset1b[32];          /* 370 channel set 1B        */
        BYTE    csconfig;               /* Channel set configuration */
        BYTE    resv[23];               /* Reserved, set to zero     */
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
SCCB_CHP_INFO  *sccbchp;                /* -> SCCB channel path info */
SCCB_CSI_INFO  *sccbcsi;                /* -> SCCB channel subsys inf*/
U16             offset;                 /* Offset from start of SCCB */
DEVBLK         *dev;                    /* Used to find CHPIDs       */
int             chpbyte;                /* Offset to byte for CHPID  */
int             chpbit;                 /* Bit number for CHPID      */

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

    /*debug*/logmsg("Service call %8.8X SCCB=%8.8X\n",
    /*debug*/       sclp_command, sccb_absolute_addr);

    /* Point to service call control block */
    sccb = (SCCB_HEADER*)(sysblk.mainstor + sccb_absolute_addr);

    /* Load SCCB length from header */
    sccblen = (sccb->length[0] << 8) | sccb->length[1];

    /* Set the main storage reference bit */
    sysblk.storkeys[sccb_absolute_addr >> 12] |= STORKEY_REF;

    /* Program check if end of SCCB falls outside main storage */
    if ( sysblk.mainsize - sccblen < sccb_absolute_addr )
    {
        program_check (PGM_ADDRESSING_EXCEPTION);
        return 3;
    }

    /* Obtain lock if immediate response is not requested */
    if (!(sccb->flag & SCCB_FLAG_SYNC))
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
    switch (sclp_command) {

    case SCLP_READ_SCP_INFO:

        /* Set the main storage change bit */
        sysblk.storkeys[sccb_absolute_addr >> 12] |= STORKEY_CHANGE;

        /* Set response code X'0100' if SCCB crosses a page boundary */
        if ((sccb_absolute_addr & 0x7FFFF000) !=
            ((sccb_absolute_addr + sccblen - 1) & 0x7FFFF000))
        {
            sccb->reas = SCCB_REAS_NOT_4KBNDRY;
            sccb->resp = SCCB_RESP_BLOCK_ERROR;
            break;
        }

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
        sccbscp->ifm[0] = 0
                        | SCCB_IFM0_CHANNEL_PATH_INFORMATION
                        | SCCB_IFM0_CHANNEL_PATH_SUBSYSTEM_COMMAND
//                      | SCCB_IFM0_CHANNEL_PATH_RECONFIG
                        | SCCB_IFM0_CPU_INFORMATION
//                      | SCCB_IFM0_CPU_RECONFIG
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
//                      | SCCB_IFM2_EXTENDED_STORAGE_USABILITY_MAP
//                      | SCCB_IFM2_EXTENDED_STORAGE_ELEMENT_INFO
//                      | SCCB_IFM2_EXTENDED_STORAGE_ELEMENT_RECONFIG
                        ;
        sccbscp->ifm[3] = 0
//                      | SCCB_IFM3_VECTOR_FEATURE_RECONFIG
//                      | SCCB_IFM3_READ_WRITE_EVENT_FEATURE
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
                        ;
        sccbscp->cfg[1] = 0
//                      | SCCB_CFG1_CSLO
                        ;
        sccbscp->cfg[2] = 0
//                      | SCCB_CFG2_DEVICE_ACTIVE_ONLY_MEASUREMENT
//                      | SCCB_CFG2_CALLED_SPACE_IDENTIFICATION
                        | SCCB_CFG2_CHECKSUM_INSTRUCTION
                        ;
        sccbscp->cfg[3] = 0
//                      | SCCB_CFG3_RESUME_PROGRAM
//                      | SCCB_CFG3_PERFORM_LOCKED_OPERATION
#ifdef FEATURE_RELATIVE_BRANCH
                        | SCCB_CFG3_IMMEDIATE_AND_RELATIVE
#endif /*FEATURE_RELATIVE_BRANCH*/
                        | SCCB_CFG3_COMPARE_AND_MOVE_EXTENDED
#ifdef FEATURE_BRANCH_AND_SET_AUTHORITY
                        | SCCB_CFG3_BRANCH_AND_SET_AUTHORITY
#endif /*FEATURE_BRANCH_AND_SET_AUTHORITY*/
//                      | SCCB_CFG3_EXTENDED_FLOATING_POINT
                        ;
        sccbscp->cfg[4] = 0
//                      | SCCB_CFG4_EXTENDED_TOD_CLOCK
//                      | SCCB_CFG4_EXTENDED_TRANSLATION
//                      | SCCB_CFG4_STORE_SYSTEM_INFORMATION
                        ;

        /* Build the CPU information array after the SCP info */
        sccbcpu = (SCCB_CPU_INFO*)(sccbscp+1);
        for (i = 0; i < sysblk.numcpu; i++, sccbcpu++)
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
//                          | SCCB_CPF2_VECTOR_FEATURE_INSTALLED
//                          | SCCB_CPF2_VECTOR_FEATURE_CONNECTED
//                          | SCCB_CPF2_VECTOR_FEATURE_STANDBY_STATE
//                          | SCCB_CPF2_CRYPTO_FEATURE_ACCESSED
//                          | SCCB_CPF2_EXPEDITE_RUN_PROCESSING
                            ;
            sccbcpu->cpf[3] = 0
#ifdef FEATURE_PRIVATE_SPACE
                            | SCCB_CPF3_PRIVATE_SPACE_FEATURE
                            | SCCB_CPF3_FETCH_ONLY_BIT
#endif /*FEATURE_PRIVATE_SPACE*/
//                          | SCCB_CPF3_PER2_INSTALLED
                            ;
            sccbcpu->cpf[4] = 0
//                          | SCCB_CPF4_OMISION_GR_ALTERATION_370
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
        sysblk.storkeys[sccb_absolute_addr >> 12] |= STORKEY_CHANGE;

        /* Set response code X'0100' if SCCB crosses a page boundary */
        if ((sccb_absolute_addr & 0x7FFFF000) !=
            ((sccb_absolute_addr + sccblen - 1) & 0x7FFFF000))
        {
            sccb->reas = SCCB_REAS_NOT_4KBNDRY;
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

        /* Identify CHPIDs installed, owned, and online */
        for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev)
        {
            chpbyte = dev->devnum >> 11;
            chpbit = (dev->devnum >> 8) & 7;

            sccbchp->installed[chpbyte] |= 0x80 >> chpbit;
            sccbchp->owned[chpbyte] |= 0x80 >> chpbit;
            sccbchp->online[chpbyte] |= 0x80 >> chpbit;
        }

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
        sysblk.storkeys[sccb_absolute_addr >> 12] |= STORKEY_CHANGE;

        /* Set response code X'0100' if SCCB crosses a page boundary */
        if ((sccb_absolute_addr & 0x7FFFF000) !=
            ((sccb_absolute_addr + sccblen - 1) & 0x7FFFF000))
        {
            sccb->reas = SCCB_REAS_NOT_4KBNDRY;
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


    default:
        /* Set response code X'01F0' for invalid SCLP command */
        sccb->reas = SCCB_REAS_INVALID_CMD;
        sccb->resp = SCCB_RESP_REJECT;

        break;

    } /* end switch(sclp_command) */

    /* If immediate response is requested, return condition code 1 */
    if (sccb->flag & SCCB_FLAG_SYNC)
        return 1;

    /* Set service signal external interrupt pending */
    sysblk.servparm = sccb_absolute_addr;
    sysblk.servsig = 1;

    /* Release the interrupt lock */
    release_lock (&sysblk.intlock);

    /* Return condition code 0 */
    return 0;

} /* end function service_call */
