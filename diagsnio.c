/* DIAGSNIO.C   (c) Copyright Roger Bowler, 2000                     */
/*              ESA/390 Diagnose Synchronous I/O Functions           */

/*-------------------------------------------------------------------*/
/* This module implements diagnose synchronous I/O functions         */
/* described in SC24-5670 VM/ESA CP Programming Services.            */
/*-------------------------------------------------------------------*/

#include "hercules.h"

/*-------------------------------------------------------------------*/
/* Synchronous Block I/O Parameter List                              */
/*-------------------------------------------------------------------*/
typedef struct _HCPSBIOP {
        HWORD   devnum;                 /* Device number             */
        BYTE    akey;                   /* Bits 0-3=key, 4-7=zeroes  */
        BYTE    type;                   /* I/O request type          */
        FWORD   blksize;                /* Fixed block size          */
        FWORD   sbiaddr;                /* Address of SBILIST        */
        FWORD   sbicount;               /* Number of SBILIST entries */
        FWORD   blkcount;               /* Number of blocks processed*/
        BYTE    unitstat;               /* Device status             */
        BYTE    chanstat;               /* Subchannel status         */
        HWORD   residual;               /* Residual byte count       */
        BYTE    lpm;                    /* Logical path mask         */
        BYTE    resv1[5];               /* Reserved bytes, must be 0 */
        HWORD   sensecount;             /* Number of sense bytes     */
        BYTE    resv2[24];              /* Reserved bytes, must be 0 */
        BYTE    sense[32];              /* Sense bytes               */
    } HCPSBIOP;

/* Definitions for I/O request type */
#define HCPSBIOP_WRITE        0x01
#define HCPSBIOP_READ         0x02

/*-------------------------------------------------------------------*/
/* Synchronous General I/O Parameter List                            */
/*-------------------------------------------------------------------*/
typedef struct _HCPSGIOP {
        HWORD   devnum;                 /* Device number             */
        BYTE    akey;                   /* Bits 0-3=key, 4-7=zeroes  */
        BYTE    flag;                   /* Flags                     */
        FWORD   resv1;                  /* Reserved word, must be 0  */
        FWORD   ccwaddr;                /* Address of channel program*/
        FWORD   resv2;                  /* Reserved word, must be 0  */
        FWORD   lastccw;                /* CCW address at interrupt  */
        BYTE    unitstat;               /* Device status             */
        BYTE    chanstat;               /* Subchannel status         */
        HWORD   residual;               /* Residual byte count       */
        BYTE    lpm;                    /* Logical path mask         */
        BYTE    resv3[5];               /* Reserved bytes, must be 0 */
        HWORD   sensecount;             /* Number of sense bytes     */
        BYTE    resv4[24];              /* Reserved bytes, must be 0 */
        BYTE    sense[32];              /* Sense bytes               */
    } HCPSGIOP;

/* Bit definitions for flags */
#define HCPSGIOP_FORMAT1_CCW  0x80      /* 1=Format-1 CCW            */
#define HCPSGIOP_FLAG_RESV    0x7F      /* Reserved bits, must be 0  */

/*-------------------------------------------------------------------*/
/* Fetch a fullword from absolute storage.                           */
/*-------------------------------------------------------------------*/
static inline U32 fetch_fullword_absolute (U32 addr)
{
U32     i;

    /* Set the main storage reference bit */
    STORAGE_KEY(addr) |= STORKEY_REF;

    /* Fetch the fullword from absolute storage */
    i = *((U32*)(sysblk.mainstor + addr));
    return ntohl(i);
} /* end function fetch_fullword_absolute */

/*-------------------------------------------------------------------*/
/* Device Type and Features (Function code 0x024)                    */
/*-------------------------------------------------------------------*/
int diag_devtype (int r1, int r2, REGS *regs)
{
U32             vdevinfo;               /* Virtual device information*/
U32             rdevinfo;               /* Real device information   */
DEVBLK         *dev;                    /* -> Device block           */
U16             devnum;                 /* Device number             */

    /* Return console information if R1 register is all ones */
    if (regs->gpr[r1] == 0xFFFFFFFF)
    {
        regs->gpr[r1] = 0x00000009;
    }

    /* Extract the device number from the R1 register */
    devnum = regs->gpr[r1];

    /* Locate the device block */
    dev = find_device_by_devnum (devnum);

    /* Return condition code 3 if device does not exist */
    if (dev == NULL) return 3;

    /* Set the device information according to device type */
    switch (dev->devtype) {
    case 0x3215:
        vdevinfo = 0x80000000;
        rdevinfo = 0x80000050;
        break;
    case 0x2501:
        vdevinfo = 0x20810000;
        rdevinfo = 0x20810000;
        break;
    case 0x2540:
        vdevinfo = 0x20820000;
        rdevinfo = 0x20820000;
        break;
    case 0x3505:
        vdevinfo = 0x20840000;
        rdevinfo = 0x20840000;
        break;
    case 0x3370:
        vdevinfo = 0x01020000;
        rdevinfo = 0x01020000;
        break;
    default:
        vdevinfo = 0x02010000;
        rdevinfo = 0x02010000;
    } /* end switch */

    /* Return virtual device information in the R2 register */
    regs->gpr[r2] = vdevinfo;

    /* Return real device information in the R2+1 register */
    if (r2 != 15)
        regs->gpr[r2+1] = rdevinfo;

    logmsg ("Diagnose X\'024\':"
            "devnum=%4.4X vdevinfo=%8.8X rdevinfo=%8.8X\n",
            devnum, vdevinfo, rdevinfo);

    /* Return condition code 0 */
    return 0;

} /* end function diag_devtype */

/*-------------------------------------------------------------------*/
/* Process Synchronous Fixed Block I/O call (Function code 0x0A4)    */
/*-------------------------------------------------------------------*/
int syncblk_io (int r1, int r2, REGS *regs)
{
int             i;                      /* Array subscript           */
int             numsense;               /* Number of sense bytes     */
U32             iopaddr;                /* Address of HCPSBIOP       */
HCPSBIOP        ioparm;                 /* I/O parameter list        */
DEVBLK         *dev;                    /* -> Device block           */
U16             devnum;                 /* Device number             */
U16             residual;               /* Residual byte count       */
U32             blksize;                /* Fixed block size          */
U32             sbiaddr;                /* Addr of SBILIST           */
U32             sbicount;               /* Number of SBILIST entries */
U32             blkcount;               /* Number of blocks processed*/
U32             blknum;                 /* Block number              */
U32             absadr;                 /* Absolute storage address  */
BYTE            accum;                  /* Work area                 */
BYTE            unitstat = 0;           /* Device status             */
BYTE            chanstat = 0;           /* Subchannel status         */
BYTE            skey1, skey2;           /* Storage keys of first and
                                           last byte of I/O buffer   */

    /* Register R1 contains the real address of the parameter list */
    iopaddr = regs->gpr[r1];

    /* Program check if parameter list not on fullword boundary */
    if (iopaddr & 0x00000003)
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Ensure that parameter list operand is addressable */
    validate_operand (iopaddr, USE_REAL_ADDR, sizeof(ioparm)-1,
                        ACCTYPE_WRITE, regs);

    /* Fetch the parameter list from real storage */
    vfetchc (&ioparm, sizeof(ioparm)-1, iopaddr, USE_REAL_ADDR, regs);

    /* Load numeric fields from the parameter list */
    devnum = (ioparm.devnum[0] << 8) | ioparm.devnum[1];
    blksize = (ioparm.blksize[0] << 24)
                | (ioparm.blksize[1] << 16)
                | (ioparm.blksize[2] << 8)
                | ioparm.blksize[3];
    sbiaddr = (ioparm.sbiaddr[0] << 24)
                | (ioparm.sbiaddr[1] << 16)
                | (ioparm.sbiaddr[2] << 8)
                | ioparm.sbiaddr[3];
    sbicount = (ioparm.sbicount[0] << 24)
                | (ioparm.sbicount[1] << 16)
                | (ioparm.sbicount[2] << 8)
                | ioparm.sbicount[3];

    /* Locate the device block */
    dev = find_device_by_devnum (devnum);

    /* Set return code 2 and cond code 1 if device does not exist
       or does not support the synchronous I/O call */
    if (dev == NULL || dev->devtype != 0x3370)
    {
        regs->gpr[15] = 2;
        return 1;
    }

    /* Program check if protect key bits 4-7 are not zero
       or if I/O request type is not read or write */
    if ((ioparm.akey & 0x0F)
        || !(ioparm.type == HCPSBIOP_WRITE
            || ioparm.type == HCPSBIOP_READ))
    {
        program_check (regs, PGM_OPERAND_EXCEPTION);
        return 0;
    }

    /* Set return code 8 and cond code 2 if blocksize is invalid */
    if (!(blksize == 512 || blksize == 1024
            || blksize == 2048 || blksize == 4096))
    {
        regs->gpr[15] = 8;
        return 2;
    }

    /* Program check if SBILIST is not on a doubleword boundary */
    if (sbiaddr & 0x00000007)
    {
        program_check (regs, PGM_OPERAND_EXCEPTION);
        return 0;
    }

    /* Program check if reserved fields are not zero */
    for (accum = 0, i = 0; i < sizeof(ioparm.resv1); i++)
        accum |= ioparm.resv1[i];
    for (i = 0; i < sizeof(ioparm.resv2); i++)
        accum |= ioparm.resv2[i];
    if (accum != 0)
    {
        program_check (regs, PGM_OPERAND_EXCEPTION);
        return 0;
    }

    /* Set return code 11 and cond code 2 if SBI count is invalid */
    if (sbicount < 1 || sbicount > 500)
    {
        regs->gpr[15] = 11;
        return 2;
    }

    /* Obtain the device lock */
    obtain_lock (&dev->lock);

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Return code 5 and condition code 1 if status pending */
    if ((dev->scsw.flag3 & SCSW3_SC_PEND)
        || (dev->pciscsw.flag3 & SCSW3_SC_PEND))
    {
        release_lock (&dev->lock);
        regs->gpr[15] = 5;
        return 1;
    }
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Return code 5 and condition code 1 if device is busy */
    if (dev->busy || dev->pending)
    {
        release_lock (&dev->lock);
        regs->gpr[15] = 5;
        return 1;
    }

    /* Set the device busy indicator */
    dev->busy = 1;

    /* Release the device lock */
    release_lock (&dev->lock);

    /* Process each entry in the SBILIST */
    for (blkcount = 0; blkcount < sbicount; blkcount++)
    {
        /* Return code 10 and cond code 2 if SBILIST entry
           is outside main storage or is fetch protected.
           Note that the SBI address is an absolute address
           and is not subject to fetch-protection override
           or storage-protection override mechanisms, and
           an SBILIST entry cannot cross a page boundary */
        if (sbiaddr >= sysblk.mainsize
            || ((STORAGE_KEY(sbiaddr) & STORKEY_FETCH)
                && (STORAGE_KEY(sbiaddr) & STORKEY_KEY) != ioparm.akey
                && ioparm.akey != 0))
        {
            regs->gpr[15] = 10;
            return 2;
        }

        /* Load block number and data address from SBILIST */
        blknum = fetch_fullword_absolute(sbiaddr);
        absadr = fetch_fullword_absolute(sbiaddr+4);

        if (dev->ccwtrace || dev->ccwstep)
        {
            logmsg ("%4.4X:Diagnose X\'0A4\':%s "
                    "blk=%8.8X adr=%8.8X len=%8.8X\n",
                    dev->devnum,
                    (ioparm.type == HCPSBIOP_WRITE ? "WRITE" : "READ"),
                    blknum, absadr, blksize);
        }

        /* Return code 12 and cond code 2 if buffer exceeds storage */
        if (absadr >= sysblk.mainsize - blksize)
        {
            regs->gpr[15] = 12;
            return 2;
        }

        /* Channel protection check if access key does not match
           storage keys of buffer.  Note that the buffer address is
           an absolute address, the buffer cannot span more than two
           pages, and the access is not subject to fetch-protection
           override, storage-protection override, or low-address
           protection */
        skey1 = STORAGE_KEY(absadr);
        skey2 = STORAGE_KEY(absadr + blksize - 1);
        if (ioparm.akey != 0
            && (
                   ((skey1 & STORKEY_KEY) != ioparm.akey
                    && ((skey1 & STORKEY_FETCH)
                        || ioparm.type == HCPSBIOP_READ))
                || ((skey2 & STORKEY_KEY) != ioparm.akey
                    && ((skey2 & STORKEY_FETCH)
                        || ioparm.type == HCPSBIOP_READ))
            ))
        {
            chanstat |= CSW_PROTC;
            break;
        }

        /* Call device handler to read or write one block */
        fbadasd_syncblk_io (dev, ioparm.type, blknum, blksize,
                            sysblk.mainstor + absadr,
                            &unitstat, &residual);

        /* Set incorrect length if residual count is non-zero */
        if (residual != 0)
            chanstat |= CSW_IL;

        /* Exit if any unusual status */
        if (unitstat != (CSW_CE | CSW_DE) || chanstat != 0)
            break;

        /* Point to next SBILIST entry */
        sbiaddr += 8;

    } /* end for(blkcount) */

    /* Reset the device busy indicator */
    obtain_lock (&dev->lock);
    dev->busy = 0;
    release_lock (&dev->lock);

    /* Store the block count in the parameter list */
    ioparm.blkcount[0] = (blkcount >> 24) & 0xFF;
    ioparm.blkcount[1] = (blkcount >> 16) & 0xFF;
    ioparm.blkcount[2] = (blkcount >> 8) & 0xFF;
    ioparm.blkcount[3] = blkcount & 0xFF;

    /* Store the device and subchannel status in the parameter list */
    ioparm.unitstat = unitstat;
    ioparm.chanstat = chanstat;

    /* Store the residual byte count in the parameter list */
    ioparm.residual[0] = (residual >> 8) & 0xFF;
    ioparm.residual[1] = residual & 0xFF;

    /* Return sense data if unit check occurred */
    if (unitstat & CSW_UC)
    {
        numsense = dev->numsense;
        if (numsense > sizeof(ioparm.sense))
            numsense = sizeof(ioparm.sense);
        ioparm.sensecount[0] = (numsense >> 8) & 0xFF;
        ioparm.sensecount[1] = numsense & 0xFF;
        memcpy (ioparm.sense, dev->sense, numsense);
    }

    /* Store the updated parameter list in real storage */
    vstorec (&ioparm, sizeof(ioparm)-1, iopaddr, USE_REAL_ADDR, regs);

    /* If I/O error occurred, set return code 13 and cond code 3 */
    if (unitstat != (CSW_CE | CSW_DE) || chanstat != 0)
    {
        regs->gpr[15] = 13;
        return 3;
    }

    /* Set return code 0 and cond code 0 */
    regs->gpr[15] = 0;
    return 0;

} /* end function syncblk_io */

/*-------------------------------------------------------------------*/
/* Process Synchronous General I/O call (Function code 0x0A8)        */
/*-------------------------------------------------------------------*/
int syncgen_io (int r1, int r2, REGS *regs)
{
int             i;                      /* Array subscript           */
int             numsense;               /* Number of sense bytes     */
U32             iopaddr;                /* Address of HCPSGIOP       */
HCPSGIOP        ioparm;                 /* I/O parameter list        */
DEVBLK         *dev;                    /* -> Device block           */
U16             devnum;                 /* Device number             */
U16             residual;               /* Residual byte count       */
U32             ccwaddr;                /* Address of channel program*/
U32             lastccw;                /* CCW address at interrupt  */
BYTE            accum;                  /* Work area                 */
BYTE            unitstat = 0;           /* Device status             */
BYTE            chanstat = 0;           /* Subchannel status         */

    /* Register R1 contains the real address of the parameter list */
    iopaddr = regs->gpr[r1];

    /* Program check if parameter list not on fullword boundary */
    if (iopaddr & 0x00000003)
    {
        program_check (regs, PGM_SPECIFICATION_EXCEPTION);
        return 0;
    }

    /* Ensure that parameter list operand is addressable */
    validate_operand (iopaddr, USE_REAL_ADDR, sizeof(ioparm)-1,
                        ACCTYPE_WRITE, regs);

    /* Fetch the parameter list from real storage */
    vfetchc (&ioparm, sizeof(ioparm)-1, iopaddr, USE_REAL_ADDR, regs);

    /* Load numeric fields from the parameter list */
    devnum = (ioparm.devnum[0] << 8) | ioparm.devnum[1];
    ccwaddr = (ioparm.ccwaddr[0] << 24)
                | (ioparm.ccwaddr[1] << 16)
                | (ioparm.ccwaddr[2] << 8)
                | ioparm.ccwaddr[3];

    /* Locate the device block */
    dev = find_device_by_devnum (devnum);

    /* Set return code 1 and cond code 1 if device does not exist */
    if (dev == NULL)
    {
        regs->gpr[15] = 1;
        return 1;
    }

    /* Program check if protect key bits 4-7 are not zero
       or if the reserved bits in the flag byte are not zero */
    if ((ioparm.akey & 0x0F) || (ioparm.flag & HCPSGIOP_FLAG_RESV))
    {
        program_check (regs, PGM_OPERAND_EXCEPTION);
        return 0;
    }

#ifdef FEATURE_S370_CHANNEL
    /* Program check if flag byte specifies format-1 CCW */
    if (ioparm.flag & HCPSGIOP_FORMAT1_CCW)
    {
        program_check (regs, PGM_OPERAND_EXCEPTION);
        return 0;
    }
#endif /*FEATURE_S370_CHANNEL*/

    /* Program check if CCW is not on a doubleword boundary,
       or if CCW address exceeds maximum according to CCW format */
    if ((ccwaddr & 0x00000007) || ccwaddr >
           ((ioparm.flag & HCPSGIOP_FORMAT1_CCW) ?
                        0x7FFFFFFF : 0x00FFFFFF))
    {
        program_check (regs, PGM_OPERAND_EXCEPTION);
        return 0;
    }

    /* Program check if reserved fields are not zero */
    for (accum = 0, i = 0; i < sizeof(ioparm.resv1); i++)
        accum |= ioparm.resv1[i];
    for (i = 0; i < sizeof(ioparm.resv2); i++)
        accum |= ioparm.resv2[i];
    for (i = 0; i < sizeof(ioparm.resv3); i++)
        accum |= ioparm.resv3[i];
    for (i = 0; i < sizeof(ioparm.resv4); i++)
        accum |= ioparm.resv4[i];
    if (accum != 0)
    {
        program_check (regs, PGM_OPERAND_EXCEPTION);
        return 0;
    }

    /* Obtain the device lock */
    obtain_lock (&dev->lock);

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    /* Return code 5 and condition code 1 if status pending */
    if ((dev->scsw.flag3 & SCSW3_SC_PEND)
        || (dev->pciscsw.flag3 & SCSW3_SC_PEND))
    {
        release_lock (&dev->lock);
        regs->gpr[15] = 5;
        return 1;
    }
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Return code 5 and condition code 1 if device is busy */
    if (dev->busy || dev->pending)
    {
        release_lock (&dev->lock);
        regs->gpr[15] = 5;
        return 1;
    }

    /* Set the device busy indicator */
    dev->busy = 1;

    /* Release the device lock */
    release_lock (&dev->lock);

    /* Execute the channel program synchronously */
    dev->ccwaddr = ccwaddr;
    dev->ccwfmt = ((ioparm.flag & HCPSGIOP_FORMAT1_CCW) ? 1 : 0);
    dev->ccwkey = ioparm.akey;

    execute_ccw_chain (dev);

    /* Obtain status, CCW address, and residual byte count */
#ifdef FEATURE_S370_CHANNEL
    lastccw = (dev->csw[1] << 16) || (dev->csw[2] << 8)
                || dev->csw[3];
    unitstat = dev->csw[4];
    chanstat = dev->csw[5];
    residual = (dev->csw[6] << 8) || dev->csw[7];
#endif /*FEATURE_S370_CHANNEL*/

#ifdef FEATURE_CHANNEL_SUBSYSTEM
    lastccw = (dev->scsw.ccwaddr[0] << 24)
                || (dev->scsw.ccwaddr[1] << 16)
                || (dev->scsw.ccwaddr[2] << 8)
                || dev->scsw.ccwaddr[3];
    unitstat = dev->scsw.unitstat;
    chanstat = dev->scsw.chanstat;
    residual = (dev->scsw.count[0] << 8) || dev->scsw.count[1];
#endif /*FEATURE_CHANNEL_SUBSYSTEM*/

    /* Clear the interrupt pending and device busy conditions */
    obtain_lock (&dev->lock);
    dev->pending = 0;
    dev->busy = 0;
    dev->scsw.flag2 = 0;
    dev->scsw.flag3 = 0;
    release_lock (&dev->lock);

    /* Store the last CCW address in the parameter list */
    ioparm.lastccw[0] = (lastccw >> 24) & 0xFF;
    ioparm.lastccw[1] = (lastccw >> 16) & 0xFF;
    ioparm.lastccw[2] = (lastccw >> 8) & 0xFF;
    ioparm.lastccw[3] = lastccw & 0xFF;

    /* Store the device and subchannel status in the parameter list */
    ioparm.unitstat = unitstat;
    ioparm.chanstat = chanstat;

    /* Store the residual byte count in the parameter list */
    ioparm.residual[0] = (residual >> 8) & 0xFF;
    ioparm.residual[1] = residual & 0xFF;

    /* Return sense data if unit check occurred */
    if (unitstat & CSW_UC)
    {
        numsense = dev->numsense;
        if (numsense > sizeof(ioparm.sense))
            numsense = sizeof(ioparm.sense);
        ioparm.sensecount[0] = (numsense >> 8) & 0xFF;
        ioparm.sensecount[1] = numsense & 0xFF;
        memcpy (ioparm.sense, dev->sense, numsense);
    }

    /* Store the updated parameter list in real storage */
    vstorec (&ioparm, sizeof(ioparm)-1, iopaddr, USE_REAL_ADDR, regs);

    /* If I/O error occurred, set return code 13 and cond code 3 */
    if (unitstat != (CSW_CE | CSW_DE) || chanstat != 0)
    {
        regs->gpr[15] = 13;
        return 3;
    }

    /* Return with condition code 0 and register 15 unchanged */
    return 0;

} /* end function syncgen_io */

