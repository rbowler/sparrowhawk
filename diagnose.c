/* DIAGNOSE.C   (c) Copyright Jan Jaeger, 1999                       */
/*              ESA/390 Diagnose Functions                           */

/*-------------------------------------------------------------------*/
/* This module implements various diagnose functions                 */
/* MSSF call as described in SA22-7098-0                             */
/* SCPEND as described in GC19-6215-0 which is also used with PR/SM  */
/*                                                                   */
/*                                             04/12/1999 Jan Jaeger */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* MSSF Call service processor commands                              */
/*-------------------------------------------------------------------*/
#define MSSF_READ_CONFIG_INFO   0x00020001
#define MSSF_READ_CHP_STATUS    0x00030001

/*-------------------------------------------------------------------*/
/* Service Processor Command Control Block                           */
/*-------------------------------------------------------------------*/
typedef struct _SPCCB_HEADER {
        HWORD   length;                 /* Total length of SPCCB     */
        BYTE    resv1[4];               /* Reserved                  */
        HWORD   resp;                   /* Response code             */
    } SPCCB_HEADER;

#define SPCCB_REAS_COMPLETE   0x00
#define SPCCB_RESP_COMPLETE   0x10
#define SPCCB_REAS_CHECK      0x00
#define SPCCB_RESP_CHECK      0x40
#define SPCCB_REAS_BADLENGTH  0x01
#define SPCCB_RESP_BADLENGTH  0xF0
#define SPCCB_REAS_NOT2KALIGN 0x01
#define SPCCB_RESP_NOT2KALIGN 0x00
#define SPCCB_REAS_UNASSIGNED 0x06
#define SPCCB_RESP_UNASSIGNED 0xF0

/*-------------------------------------------------------------------*/
/* Service Processor Command Control Block                           */
/*-------------------------------------------------------------------*/
typedef struct _SPCCB_CONFIG_INFO {
        BYTE    totstori;               /* Total number of installed
                                           storage increments.       */
        BYTE    storisiz;               /* Storage increment size in
                                           units of one megabyte.    */
        BYTE    hex04;                  /* This field contains the
                                           value of 04 hex.          */
        BYTE    hex01;                  /* This field contains the
                                           value of either 0 hex or
                                           02 hex.                   */
        FWORD   reserved;               /* Reserved.                 */
        HWORD   toticpu;                /* Total number of installed
                                           CPUs.  This number also
                                           specifies the total number
                                           of entries in the CPU-
                                           description list.         */
        HWORD   officpu;                /* Offset in the SPCCB to the
                                           first entry in the CPU-
                                           description list.         */
        HWORD   tothsa;                 /* Total number of machine-
                                           storage areas reserved for
                                           the requesting
                                           configuration.  This number
                                           also specifies the total
                                           number of entries in the
                                           machine-storage-area-
                                           description list.        */
        HWORD   offhsa;                 /* Offset in the SPCCB to the
                                           first entry in the
                                           machine-storage-area-
                                           description list.        */
        BYTE    loadparm[8];            /* Load parameter.  This field
                                           conains eight bytes of
                                           EBCDIC information specified
                                           by the operator for the
                                           manually initiated load
                                           function.  If the operator
                                           specifies fewer then eight
                                           characters,  they are left-
                                           justified and padded on the
                                           right with 40 hex (preferred),
                                           or F0 hex,  or 00 hex to
                                           form an eight byte load
                                           parameter.  If the operator
                                           does not specify a load
                                           parameter,  a default load
                                           parameter is formed,
                                           consisting of F1 hex (preferred),
                                           or 40 hex,  or 00 hex,  padded
                                           on the right with 40 hex
                                           (preferred), or F0 hex,  of
                                           00 hex.  The load parameter
                                           is set to this default value
                                           by the activation of the
                                           system-reset-clear key or by
                                           performing certain operator
                                           functions.                */
    } SPCCB_CONFIG_INFO;

typedef struct _SPCCB_CPU_INFO {
        BYTE    cpuaddr;                /* Rightmost eight bits of the
                                           CPU address               */
        BYTE    todid;                  /* Identity of the TOD clock
                                           accessed by this CPU      */
    } SPCCB_CPU_INFO;

typedef struct _SPCCB_HSA_INFO {
        BYTE    hsasize;                /* Bits 1-15 of this field
                                           specify the size of the
                                           area in units of 4K bytes
                                           or 32K bytes.  When bit 0
                                           of byte 0 is zero,  the
                                           size is in units of 32K
                                           bytes;  otherwise,  the
                                           size is in units of 4K
                                           bytes.                    */
        BYTE    hsaaddr;                /* Absolute address of the
                                           start of the machine-
                                           storage area.             */
    } SPCCB_HSA_INFO;

typedef struct _SPCCB_CHP_STATUS {
        BYTE    installed[32];          /* Installed-channel-path bit
                                           map.                      */
        BYTE    assigned[32];           /* Assigned-channel-path bit
                                           map.                      */
        BYTE    configured[32];         /* Configured-channel-path bit
                                           map.                      */
        BYTE    reserved[152];          /* Reserved.                 */
    } SPCCB_CHP_STATUS;

/*-------------------------------------------------------------------*/
/* Process SCPEND call (Function code 0x044)                         */
/*-------------------------------------------------------------------*/
void scpend_call(void)
{
        usleep(1L);                     /* Just go to the dispatcher
                                           for a minimum delay       */
} /* end function scpend_call */

#ifdef FEATURE_MSSF_CALL
/*-------------------------------------------------------------------*/
/* Process MSSF call (Function code 0x080)                           */
/*-------------------------------------------------------------------*/
int mssf_call (U32 spccb_absolute_addr, U32 mssf_command)
{
int               spccblen;            /* Length of SPCCB            */
SPCCB_HEADER      *spccb;              /* -> SPCCB header            */
SPCCB_CONFIG_INFO *spccbconfig;        /* -> SPCCB CONFIG info       */
SPCCB_CPU_INFO    *spccbcpu;           /* -> SPCCB CPU information   */
SPCCB_CHP_STATUS  *spccbchp;           /* -> SPCCB channel path info */
U16               offset;              /* Offset from start of SPCCB */
int               i;                   /* loop counter               */
DEVBLK            *dev;                /* Device block pointer       */

    /* Program check if SPCCB is not on a doubleword boundary */
    if ( spccb_absolute_addr & 0x00000007 )
    {
        program_check (PGM_SPECIFICATION_EXCEPTION);
        return 3;
    }

    /* Program check if SPCCB is outside main storage */
    if ( spccb_absolute_addr >= sysblk.mainsize )
    {
        program_check (PGM_ADDRESSING_EXCEPTION);
        return 3;
    }

    /*debug*/logmsg("MSSF call %8.8X SPCCB=%8.8X\n",
    /*debug*/       mssf_command, spccb_absolute_addr);

    /* Point to Service Processor Command Control Block */
    spccb = (SPCCB_HEADER*)(sysblk.mainstor + spccb_absolute_addr);

    /* Load SPCCB length from header */
    spccblen = (spccb->length[0] << 8) | spccb->length[1];

    /* Mark page referenced */
    sysblk.storkeys[spccb_absolute_addr >> 12] |= STORKEY_REF;

    /* Program check if end of SPCCB falls outside main storage */
    if ( sysblk.mainsize - spccblen < spccb_absolute_addr )
    {
        program_check (PGM_ADDRESSING_EXCEPTION);
        return 3;
    }

    /* Obtain the interrupt lock */
    obtain_lock (&sysblk.intlock);

    /* If a service signal is pending then we cannot process the request */
    if( sysblk.servsig == 1 ) {
        release_lock (&sysblk.intlock);
        return 2;   /* Service Processor Busy */
    }

    if( spccb_absolute_addr & 0x7ffff800 ) {
        spccb->resp[0] = SPCCB_REAS_NOT2KALIGN;
        spccb->resp[1] = SPCCB_RESP_NOT2KALIGN;
    } else
        /* Test MSSF command word */
        switch (mssf_command) {

        case MSSF_READ_CONFIG_INFO:

            /* Set response code X'01F0' if SPCCB length
               is insufficient to contain CONFIG info */
            if ( spccblen < 64 )
            {
                spccb->resp[0] = SPCCB_REAS_BADLENGTH;
                spccb->resp[1] = SPCCB_RESP_BADLENGTH;
            break;
            }

            /* Point to SPCCB data area following SPCCB header */
            spccbconfig = (SPCCB_CONFIG_INFO*)(spccb+1);
            memset (spccbconfig, 0, sizeof(SPCCB_CONFIG_INFO));

            /* Set main storage size in SPCCB */
            spccbconfig->totstori = sysblk.mainsize >> 20;
            spccbconfig->storisiz = 1;
            spccbconfig->hex04 = 0x04;
            spccbconfig->hex01 = 0x01;

            /* Set CPU array count and offset in SPCCB */
            spccbconfig->toticpu[0] = (sysblk.numcpu & 0xFF00) >> 8;
            spccbconfig->toticpu[1] = sysblk.numcpu & 0xFF;
            offset = sizeof(SPCCB_HEADER) + sizeof(SPCCB_CONFIG_INFO);
            spccbconfig->officpu[0] = (offset & 0xFF00) >> 8;
            spccbconfig->officpu[1] = offset & 0xFF;

            /* Set HSA array count and offset in SPCCB */
            spccbconfig->tothsa[0] = 0;
            spccbconfig->tothsa[1] = 0;
            offset += sizeof(SPCCB_CPU_INFO) * sysblk.numcpu;
            spccbconfig->offhsa[0] = (offset & 0xFF00) >> 8;
            spccbconfig->offhsa[1] = offset & 0xFF;

            /* Move IPL load parameter to SPCCB */
            memcpy (spccbconfig->loadparm, sysblk.loadparm, 8);

            /* Build the CPU information array after the SCP info */
            spccbcpu = (SPCCB_CPU_INFO*)(spccbconfig+1);
            for (i = 0; i < sysblk.numcpu; i++, spccbcpu++)
            {
                memset (spccbcpu, 0, sizeof(SPCCB_CPU_INFO));
                spccbcpu->cpuaddr = sysblk.regs[i].cpuad;
                spccbcpu->todid = 0;
            }

            /* Set response code X'0010' in SPCCB header */
            spccb->resp[0] = SPCCB_REAS_COMPLETE;
            spccb->resp[1] = SPCCB_RESP_COMPLETE;

            break;

        case MSSF_READ_CHP_STATUS:

            /* Set response code X'0300' if SPCCB length
               is insufficient to contain channel path info */
            if ( spccblen < sizeof(SPCCB_HEADER) + sizeof(SPCCB_CHP_STATUS))
            {
                spccb->resp[0] = SPCCB_REAS_BADLENGTH;
                spccb->resp[1] = SPCCB_RESP_BADLENGTH;
                break;
            }

            /* Point to SPCCB data area following SPCCB header */
            spccbchp = (SPCCB_CHP_STATUS*)(spccb+1);
            memset (spccbchp, 0, sizeof(SPCCB_CHP_STATUS));

            /* Identify CHPIDs used */
            for (dev = sysblk.firstdev; dev != NULL; dev = dev->nextdev) {
                spccbchp->installed[dev->devnum >> 11] |= 0x80 >> ((dev->devnum >> 8) & 7);
                spccbchp->assigned[dev->devnum >> 11] |= 0x80 >> ((dev->devnum >> 8) & 7);
                spccbchp->configured[dev->devnum >> 11] |= 0x80 >> ((dev->devnum >> 8) & 7);
            }

            /* Set response code X'0010' in SPCCB header */
            spccb->resp[0] = SPCCB_REAS_COMPLETE;
            spccb->resp[1] = SPCCB_RESP_COMPLETE;

            break;

        default:
            /* Set response code X'06F0' for invalid MSSF command */
            spccb->resp[0] = SPCCB_REAS_UNASSIGNED;
            spccb->resp[1] = SPCCB_RESP_UNASSIGNED;

            break;

        } /* end switch(mssf_command) */

    /* Mark page changed */
    sysblk.storkeys[spccb_absolute_addr >> 12] |= STORKEY_CHANGE;

    /* Set service signal external interrupt pending */
    sysblk.servparm = spccb_absolute_addr;
    sysblk.servsig = 1;

    /* Release the interrupt lock */
    release_lock (&sysblk.intlock);

    /* Return condition code 0: Command initiated */
    return 0;

} /* end function mssf_call */
#endif /* FEATURE_MSSF_CALL */
