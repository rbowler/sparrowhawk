/* vector.c     S/370 and ESA/390 Vector Operations                  */

/*-------------------------------------------------------------------*/
/* This module implements the Vector Facility instruction execution  */
/* function of the S/370 and ESA/390 architectures, as described in  */
/* SA22-7125-03 Vector Operations (S/370 & ESA/370)                  */
/* SA22-7207-00 Vector Operations (ESA/390)                          */
/*                                             28/05/2000 Jan Jaeger */
/*-------------------------------------------------------------------*/

#include "hercules.h"

#ifdef FEATURE_VECTOR_FACILITY

#define VOP_CHECK() \
        if(!(regs->cr[0] & CR0_VOP) || !regs->vf.online) \
            program_check(regs, PGM_VECTOR_OPERATION_EXCEPTION)
#define PRIV_CHECK() \
            if(regs->psw.prob) \
                program_check(regs, PGM_PRIVILEGED_OPERATION_EXCEPTION)
#define VR_INUSE(_vr) \
        (regs->vf.vsr & (VSR_VIU0 >> ((_vr) >> 1)))
#define VR_CHANGED(_vr) \
        (regs->vf.vsr & (VSR_VCH0 >> ((_vr) >> 1)))
#define SET_VR_INUSE(_vr) \
        regs->vf.vsr |= (VSR_VIU0 >> ((_vr) >> 1))
#define SET_VR_CHANGED(_vr) \
        regs->vf.vsr |= (VSR_VCH0 >> ((_vr) >> 1))
#define RESET_VR_INUSE(_vr) \
        regs->vf.vsr &= ~(VSR_VIU0 >> ((_vr) >> 1))
#define RESET_VR_CHANGED(_vr) \
        regs->vf.vsr &= ~(VSR_VCH0 >> ((_vr) >> 1))
#define VMR_SET(_section) \
        (regs->vf.vmr[(_section) >> 3] & (0x80 >> ((_section) & 7)))
#define MASK_MODE() \
        (regs->vf.vsr & VSR_M)
#define VECTOR_COUNT() \
            ((regs->vf.vsr & VSR_VCT) >> 32)
#define VECTOR_IX() \
            ((regs->vf.vsr & VSR_VIX) >> 16)

void vector_inst(BYTE *inst, REGS *regs)
{
int     vr1, vr2, vr3, rt2, d2;
#define gr1 vr1
#define rs2 vr2
#define gr2 vr2
#define qr3 vr3
#define b2  vr3
#define r3  vr3
U32     effective_addr2;

U32     n, n1, n2;

U64     d;

BYTE    workvmr[VECTOR_SECTION_SIZE/8];

    switch(inst[0]) 
    {
    case 0xA4:
            /* Format is VST or QST (vr3 eq qr3) */
            vr3 = inst[2] >> 4;
            rt2 = inst[2] & 0x0F;
            vr1 = inst[3] >> 4;
            rs2 = inst[3] & 0x0F;

        switch(inst[1])
        {
        case 0x00:
        /*-----------------------------------------------------------*/
        /* A400: VAE - Add (short)                             [VST] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();

            /*INCOMPLETE*/

            break;


        
        default:
            program_check(regs, PGM_OPERATION_EXCEPTION);
        }       
        break;

   case 0xA5:
            /* Format is QV or VV (vr3 eq qr3) */
            vr3 = inst[2] >> 4;
            vr1 = inst[3] >> 4;
            rs2 = inst[3] & 0x0F;
        
        switch(inst[1])
        {
        case 0x00:
        /*-----------------------------------------------------------*/
        /* A500: VAER - Add (short)                             [VV] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();

            /*INCOMPLETE*/

            break;


        
        default:
            program_check(regs, PGM_OPERATION_EXCEPTION);
        }       
        
        break;

    case 0xA6:
            /* Format is VR, VS or RRE (qr3 eq vr3,b2 eq vr3, gr2 eq vr2) */
            qr3 = inst[2] >> 4;
            vr1 = inst[3] >> 4;
            gr2 = inst[3] & 0x0F;
            d2 = (inst[2] & 0x0F) << 8 | inst[3];
            effective_addr2 = ((b2 ? regs->gpr[b2] : 0) + d2)
                                              & ADDRESS_MAXWRAP(regs);
        
        switch(inst[1])
        {

        case 0x40:
        /*-----------------------------------------------------------*/
        /* A640: VTVM - Test VMR                               [RRE] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            /* Extract vector count (number of active bits in vmr) */
            n = VECTOR_COUNT();

            /* cc0 when the vector count is zero */
            if( n == 0)
            {
                regs->psw.cc = 0;
                break;
            }

            /* Preset condition code according to first bit */
            regs->psw.cc = VMR_SET(0) ? 3 : 0;

            /* Check VMR bit to be equal to the first, 
               exit with cc1 if an unequal bit found */
            for(n1 = 1; n1 < n; n1++)
                if((regs->psw.cc == 0) != (VMR_SET(n1) == 0))
                {
                    regs->psw.cc = 1;
                    break;
                }

            break;

        case 0x41:
        /*-----------------------------------------------------------*/
        /* A641: VCVM - Complement VMR                          [VS] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();

            /* Extract vector count (number of active bits in vmr) */
            n = VECTOR_COUNT();

            /* Bytes - 1 */
            n1 = n >> 3; 

            /* Complement VMR */
            for(n2 = 0; n2 <= n1; n2++)
                regs->vf.vmr[n2] ^= 0xFF;
            
            /* zeroize remainder */
            regs->vf.vmr[n1] &= 0x7F00 >> (n & 7);
            for(n1++; n1 < sizeof(regs->vf.vmr); n1++)
                regs->vf.vmr[n1] = 0;

            break;

        case 0x42:
        /*-----------------------------------------------------------*/
        /* A642: VCZVM - Count Left Zeros in VMR               [RRE] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            /* Extract vector count (number of active bits in vmr) */
            n = VECTOR_COUNT();

            /* cc0 when the vector count is zero */
            if( n == 0)
            {
                regs->psw.cc = 0;
                break;
            }
            
            /* Preset condition code according to first bit */
            regs->psw.cc = VMR_SET(0) ? 3 : 0;

            /* If the VCT is 1 and the first bit is one
               then exit wirh gr1 set to zero */
            regs->gpr[gr1] = 0;
            if(n == 1 && VMR_SET(0))
                break;

            /* Count left zeros, set cc1 and exit if a one is found */
            regs->gpr[gr1] = 1;
            for(n1 = 1; n1 < n; n1++)
            {
                if(!VMR_SET(n1))
                    regs->gpr[gr1]++;
                else
                {
                    regs->psw.cc = 1;
                    break;
                }
            }

            break;

        case 0x43:
        /*-----------------------------------------------------------*/
        /* A643: VCOVM - Count Ones In VMR                     [RRE] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            /* Extract vector count (number of active bits in vmr) */
            n = VECTOR_COUNT();

            /* cc0 when the vector count is zero */
            if( n == 0)
            {
                regs->psw.cc = 0;
                break;
            }
            
            /* Preset condition code according to first bit */
            regs->psw.cc = VMR_SET(0) ? 3 : 0;

            /* Check VMR bit to be equal to the first, 
               Count all ones, set cc1 if a bit is unequal */
            regs->gpr[gr1] = 0;
            for(n1 = 0; n1 < n; n1++)
            {
                if(VMR_SET(n1))
                {
                    regs->gpr[gr1]++;
                    if(!VMR_SET(0))
                        regs->psw.cc = 1;
                }
                else
                    if(VMR_SET(0))
                        regs->psw.cc = 1;
            }

            break;

        case 0x44:
        /*-----------------------------------------------------------*/
        /* A644: VXVC - Exctract VCT                           [RRE] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();

            regs->gpr[gr1] = VECTOR_COUNT();

            break;

        case 0x46:
        /*-----------------------------------------------------------*/
        /* A646: VXVMM - Extract Vector Modes                  [RRE] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();

            regs->gpr[gr1] = (regs->vf.vsr >> 48);

            break;

        case 0x48:
        /*-----------------------------------------------------------*/
        /* A648: VRRS - Restore VR                             [RRE] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            /* Specification exception if gr1 is odd */
            if(gr1 & 1)
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            /* n contrains the current save area address */
            n = regs->gpr[gr1] & ADDRESS_MAXWRAP(regs);

            /* n1 contains the starting element number */
            if((n1 = regs->gpr[gr1 + 1] >> 16) >= VECTOR_SECTION_SIZE)
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            /* Starting address must be eight times the section size aligned */
            if((n - (8 * n1)) & ((VECTOR_SECTION_SIZE * 8) - 1) )
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            /* n2 contains VR pair, which must be an even reg */
            if((n2 = regs->gpr[gr1 + 1] & 0x0000FFFF) & 0x0000FFF1)
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            if( VR_INUSE(n2) )
            {
                /* Set the vector changed bit if in problem state */
                if( regs->psw.prob )
                    SET_VR_CHANGED(n2);

                for(; n1 < VECTOR_SECTION_SIZE; n1++)
                {
                    /* Fetch vr pair from central storage */
                    d = vfetch8(n, gr1, regs);    
                    regs->vf.vr[n2][n1] = d >> 32;
                    regs->vf.vr[n2+1][n1] = d;
    
                    /* Increment element number */
                    n1++;
                    regs->gpr[gr1 + 1] &= 0x0000FFFF;
                    regs->gpr[gr1 + 1] |= n1 << 16;
                    /* Update savearea address */
                    regs->gpr[gr1] += 8;
#if 0
                    /* This is where the instruction may be interrupted */
                    regs->psw.ia -= regs->psw.ilc;
                    return;
#endif
                }

                /* Indicate vr pair restored */
                regs->psw.cc = 2;
            }
            else
            {
                regs->gpr[gr1] += 8 * (VECTOR_SECTION_SIZE - n1);
                /* indicate v2 pair not restored */
                regs->psw.cc = 0;
            }

            /* Set CC2 if vr 14 is restored, CC0 if not restored, 
               CC3 and CC1 for other VR's respectively */
            if(n2 != 14) regs->psw.cc++;

            /* Update the vector pair number, and zero element number */
            n2 += 2;
            regs->gpr[gr1 + 1] = n2;

            break;

        case 0x49:
        /*-----------------------------------------------------------*/
        /* A649: VRSVC - Save Changed VR                       [RRE] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            PRIV_CHECK();

            /* Specification exception if gr1 is odd */
            if(gr1 & 1)
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            /* n contrains the current save area address */
            n = regs->gpr[gr1] & ADDRESS_MAXWRAP(regs);

            /* n1 contains the starting element number */
            if((n1 = regs->gpr[gr1 + 1] >> 16) >= VECTOR_SECTION_SIZE)
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            /* Starting address must be eight times the section size aligned */
            if((n - (8 * n1)) & ((VECTOR_SECTION_SIZE * 8) - 1) )
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            /* n2 contains VR pair, which must be an even reg */
            if((n2 = regs->gpr[gr1 + 1] & 0x0000FFFF) & 0x0000FFF1)
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            if( VR_CHANGED(n2) )
            {
                for(; n1 < VECTOR_SECTION_SIZE; n1++)
                {
                    /* Store vr pair in savearea */
                    d = ((U64)regs->vf.vr[n2][n1] << 32)
                      | regs->vf.vr[n2+1][n1];
                    vstore8(d, n, gr1, regs);    

                    /* Update element number */
                    n1++;
                    regs->gpr[gr1 + 1] &= 0x0000FFFF;
                    regs->gpr[gr1 + 1] |= n1 << 16;
                    regs->gpr[gr1] += 8;
#if 0
                    /* This is where the instruction may be interrupted */
                    regs->psw.ia -= regs->psw.ilc;
                    return;
#endif
                }

                /* Indicate vr pair saved */
                regs->psw.cc = 2;

                /* Reset the VR changed bit */
                RESET_VR_CHANGED(n2);
            }
            else
            {
                regs->gpr[gr1] += 8 * (VECTOR_SECTION_SIZE - n1);
                /* vr pair not saved */
                regs->psw.cc = 0;
            }

            /* Set CC2 if vr 14 is restored, CC0 if not restored, 
               CC3 and CC1 for other VR's respectively */
            if(n2 != 14) regs->psw.cc++;

            /* Update the vector pair number, and zero element number */
            n2 += 2;
            regs->gpr[gr1 + 1] = n2;

            break;

        case 0x4A:
        /*-----------------------------------------------------------*/
        /* A64A: VRSV - Save VR                                [RRE] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            /* Specification exception if gr1 is odd */
            if(gr1 & 1)
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            /* n contrains the current save area address */
            n = regs->gpr[gr1] & ADDRESS_MAXWRAP(regs);

            /* n1 contains the starting element number */
            if((n1 = regs->gpr[gr1 + 1] >> 16) >= VECTOR_SECTION_SIZE)
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            /* Starting address must be eight times the section size aligned */
            if((n - (8 * n1)) & ((VECTOR_SECTION_SIZE * 8) - 1) )
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            /* n2 contains VR pair, which must be an even reg */
            if((n2 = regs->gpr[gr1 + 1] & 0x0000FFFF) & 0x0000FFF1)
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            if( VR_INUSE(n2) )
            {
                for(; n1 < VECTOR_SECTION_SIZE; n1++)
                {
                    /* Store vr pair in savearea */
                    d = ((U64)regs->vf.vr[n2][n1] << 32)
                      | regs->vf.vr[n2+1][n1];
                    vstore8(d, n, gr1, regs);    

                    /* Update element number */
                    n1++;
                    regs->gpr[gr1 + 1] &= 0x0000FFFF;
                    regs->gpr[gr1 + 1] |= n1 << 16;
                    regs->gpr[gr1] += 8;
#if 0
                    /* This is where the instruction may be interrupted */
                    regs->psw.ia -= regs->psw.ilc;
                    return;
#endif
                }

                /* Indicate vr pair restored */
                regs->psw.cc = 2;
            }
            else
            {
                regs->gpr[gr1] += 8 * (VECTOR_SECTION_SIZE - n1);
                /* Indicate vr pair not restored */
                regs->psw.cc = 0;
            }

            /* Set CC2 if vr 14 is restored, CC0 if not restored, 
               CC3 and CC1 for other VR's respectively */
            if(n2 != 14) regs->psw.cc++;

            /* Update the vector pair number, and zero element number */
            n2 += 2;
            regs->gpr[gr1 + 1] = n2;

            break;

        case 0x80:
        /*-----------------------------------------------------------*/
        /* A680: VLVM - Load VMR                                [VS] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            /* Extract vector count (number of active bits in vmr) */
            n = VECTOR_COUNT();
            n1 = n >> 3; 

            vfetchc(regs->vf.vmr, n1,
                    regs->gpr[rs2] & ADDRESS_MAXWRAP(regs), rs2, regs);

            /* Set the inactive bits to zero */
            regs->vf.vmr[n1] &= 0x7F00 >> (n & 7);
            for(n1++; n1 < sizeof(regs->vf.vmr); n1++)
                regs->vf.vmr[n1] = 0;

            break;

        case 0x81:
        /*-----------------------------------------------------------*/
        /* A681: VLCVM - Load VMR Complement                    [VS] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            /* Extract vector count (number of active bits in vmr) */
            n = VECTOR_COUNT();

            /* Number of bytes - 1 */
            n1 = n >> 3; 

            vfetchc(regs->vf.vmr, n1,
                    regs->gpr[rs2] & ADDRESS_MAXWRAP(regs), rs2, regs);

            /* Complement all bits loaded */
            for(n2 = 0; n2 <= n1; n2++)
                regs->vf.vmr[n2] ^= 0xFF;

            /* Set the inactive bits to zero */
            regs->vf.vmr[n1] &= 0x7F00 >> (n & 7);
            for(n1++; n1 < sizeof(regs->vf.vmr); n1++)
                regs->vf.vmr[n1] = 0;

            break;
        case 0x82:
        /*-----------------------------------------------------------*/
        /* A682: VSTVM - Store VMR                              [VS] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            /* Extract vector count (number of active bits in vmr) */
            n = VECTOR_COUNT();
        
            vstorec(regs->vf.vmr, n >> 3,
                    regs->gpr[rs2] & ADDRESS_MAXWRAP(regs), rs2, regs);
            
            break;

        case 0x84:
        /*-----------------------------------------------------------*/
        /* A684: VNVM - AND To VMR                              [VS] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            /* Extract vector count (number of active bits in vmr) */
            n = VECTOR_COUNT();

            /* Number of bytes - 1 */
            n1 = n >> 3; 

            vfetchc(workvmr, n1,
                regs->gpr[rs2] & ADDRESS_MAXWRAP(regs), rs2, regs);

            /* And VMR with workvmr */
            for(n2 = 0; n2 <= n1; n2++)
                regs->vf.vmr[n2] &= workvmr[n2];
            
            /* zeroize remainder */
            regs->vf.vmr[n1] &= 0x7F00 >> (n & 7);
            for(n1++; n1 < sizeof(regs->vf.vmr); n1++)
                regs->vf.vmr[n1] = 0;

            break;

        case 0x85:
        /*-----------------------------------------------------------*/
        /* A685: VOVM - OR To VMR                               [VS] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            /* Extract vector count (number of active bits in vmr) */
            n = VECTOR_COUNT();

            /* Number of bytes - 1 */
            n1 = n >> 3; 

            vfetchc(workvmr, n1,
                regs->gpr[rs2] & ADDRESS_MAXWRAP(regs), rs2, regs);

            /* OR VMR with workvmr */
            for(n2 = 0; n2 <= n1; n2++)
                regs->vf.vmr[n2] |= workvmr[n2];
            
            /* zeroize remainder */
            regs->vf.vmr[n1] &= 0x7F00 >> (n & 7);
            for(n1++; n1 < sizeof(regs->vf.vmr); n1++)
                regs->vf.vmr[n1] = 0;

            break;

        case 0x86:
        /*-----------------------------------------------------------*/
        /* A686: VXVM - Exclusive OR To VMR                     [VS] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            /* Extract vector count (number of active bits in vmr) */
            n = VECTOR_COUNT();

            /* Number of bytes - 1 */
            n1 = n >> 3; 

            vfetchc(workvmr, n1,
                regs->gpr[rs2] & ADDRESS_MAXWRAP(regs), rs2, regs);

            /* OR VMR with workvmr */
            for(n2 = 0; n2 <= n1; n2++)
                regs->vf.vmr[n2] ^= workvmr[n2];
            
            /* zeroize remainder */
            regs->vf.vmr[n1] &= 0x7F00 >> (n & 7);
            for(n1++; n1 < sizeof(regs->vf.vmr); n1++)
                regs->vf.vmr[n1] = 0;

            break;

        case 0xC0:
        /*-----------------------------------------------------------*/
        /* A6C0: VSRSV - Save VSR                                [S] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            /* Operand must be double word aligned */
            if(effective_addr2 & 0x00000007)
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            vstore8(regs->vf.vsr, effective_addr2, b2, regs);
            
            break;

        case 0xC1:
        /*-----------------------------------------------------------*/
        /* A6C1: VMRSV - Save VMR                                [S] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            vstorec(regs->vf.vmr, sizeof(regs->vf.vmr) - 1,
                effective_addr2, b2, regs);
            
            break;

        case 0xC2:
        /*-----------------------------------------------------------*/
        /* A6C2: VSRRS - Restore VSR                             [S] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            /* Operand must be double word aligned */
            if(effective_addr2 & 0x00000007)
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            /* Fetch operand */
            d = vfetch8(effective_addr2, b2, regs);

            /* Check for reserved bits nonzero,
               vector count not greater then section size and
               vector interruption index not greater then section size */
            if((d & VSR_RESV)
                || ((d & VSR_VCT) >> 32) > VECTOR_SECTION_SIZE 
                || ((d & VSR_VIX) >> 16) >= VECTOR_SECTION_SIZE)
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);
            
            /* In problem state the change bit are set corresponding
               the inuse bits */
            if(regs->psw.prob)
            {
                d &= ~VSR_VCH;
                d |= (d & VSR_VIU) >> 8;
            }

            /* Clear any VRs whose inuse bits are being set to zero */
            for(n1 = 0; n1 < 16; n1 += 2)
            {
                if( VR_INUSE(n1)
                    && !((d & VSR_VIU) & (VSR_VCH0 >> (n1 >> 1))) )
                    for(n2 = 0; n2 < VECTOR_SECTION_SIZE; n2++)
                    {
                        regs->vf.vr[n1][n2] = 0;
                        regs->vf.vr[n1+1][n2] = 0;
                    }
            }

            /* Update the vector status register */
            regs->vf.vsr = d;

            break;

        case 0xC3:
        /*-----------------------------------------------------------*/
        /* A6C3: VMRRS - Restore VMR                             [S] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            vfetchc(regs->vf.vmr, sizeof(regs->vf.vmr) - 1,
                effective_addr2, b2, regs);
            
            break;

        case 0xC4:
        /*-----------------------------------------------------------*/
        /* A6C4: VLVCA - Load VCT from Address                   [S] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            n = (b2 ? regs->gpr[b2] : 0) + d2 ;
            regs->psw.cc = ((S32)n == 0) ? 0 :
                           ((S32)n < 0) ? 1 :
                           ((S32)n > VECTOR_SECTION_SIZE) ? 2 : 3;
                   
            n = (S32)n < 0 ? 0 : (S32)n > VECTOR_SECTION_SIZE ? 
                                 VECTOR_SECTION_SIZE : (S32)n;

            regs->vf.vsr &= ~VSR_VCT;
            regs->vf.vsr |= (U64)n << 32;
            
            break;

        case 0xC5:
        /*-----------------------------------------------------------*/
        /* A6C5: VRCL - Clear VR                                 [S] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            /* Set vector interruption index to zero */
            regs->vf.vsr &= ~VSR_VIX;

            /* Clear vr's identified in the bit mask
               n1 contains the vr number   
               n2 contains the bitmask identifying the vr number
               n contains the element number */
            for(n1 = 0, n2 = 0x80; n1 <= 14; n1 += 2, n2 >>= 1)
                if(effective_addr2 & n2)
                {
                    for(n = 0; n < VECTOR_SECTION_SIZE; n++)
                    {
                        regs->vf.vr[n1][n] = 0;
                        regs->vf.vr[n1+1][n] = 0;
                    }
                    RESET_VR_INUSE(n1);
                }
        
            break;

        case 0xC6:
        /*-----------------------------------------------------------*/
        /* A6C6: VSVMM - Set Vector Mask Mode                    [S] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();

            if(effective_addr2 & 1)
                regs->vf.vsr |= VSR_M;
            else
                regs->vf.vsr &= ~VSR_M;

            break;

        case 0xC7:
        /*-----------------------------------------------------------*/
        /* A6C7: VLVXA - Load VIX from Address                   [S] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            n = (b2 ? regs->gpr[b2] : 0) + d2 ;
            regs->psw.cc = ((S32)n == 0) ? 0 :
                           ((S32)n < 0) ? 1 :
                           ((S32)n < VECTOR_COUNT()) ? 2 : 3;
                   
            n = (S32)n < 0 ? 0 : (S32)n > VECTOR_SECTION_SIZE ? 
                                 VECTOR_SECTION_SIZE : (S32)n;

            regs->vf.vsr &= ~VSR_VIX;
            regs->vf.vsr |= (U64)n << 16;
            
            break;

        case 0xC8:
        /*-----------------------------------------------------------*/
        /* A6C8: VSTVP - Store Vector Parameters                 [S] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            /* Operand must be word aligned */
            if(effective_addr2 & 0x00000003)
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            /* Store the section size and partial sum number */
            vstore4(VECTOR_SECTION_SIZE << 16 | VECTOR_PARTIAL_SUM_NUMBER,
                                          effective_addr2, b2, regs);
            
            break;

        case 0xCA:
        /*-----------------------------------------------------------*/
        /* A6CA: VACSV - Save VAC                                [S] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            PRIV_CHECK();

            /* Operand must be double word aligned */
            if(effective_addr2 & 0x00000007)
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            vstore8(regs->vf.vac, effective_addr2, b2, regs);
            
            break;

        case 0xCB:
        /*-----------------------------------------------------------*/
        /* A6CB: VACRS - Restore VAC                             [S] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();
        
            PRIV_CHECK();

            /* Operand must be double word aligned */
            if(effective_addr2 & 0x00000007)
                program_check(regs, PGM_SPECIFICATION_EXCEPTION);

            regs->vf.vac = vfetch8(effective_addr2, b2, regs) & VAC_MASK;
            
            break;

        default:
            program_check(regs, PGM_OPERATION_EXCEPTION);
        }       
        
        break;

    case 0xE4:
            /* Format is RSE */
            r3 = inst[2] >> 4;
            vr1 = inst[3] >> 4;
            b2 = inst[4] >> 4;
            d2 = (inst[4] & 0x0F) << 8 | inst[5];
            effective_addr2 = ((b2 ? regs->gpr[b2] : 0) + d2)
                                              & ADDRESS_MAXWRAP(regs);

        switch(inst[1])
        {
        case 0x00:
        /*-----------------------------------------------------------*/
        /* E400: VLI - Load Indirect (binary)                  [VLI] */
        /*-----------------------------------------------------------*/

            VOP_CHECK();

            /*INCOMPLETE*/

            break;
        

        default:
            program_check(regs, PGM_OPERATION_EXCEPTION);
        }       
        
        break;


    default:
        program_check(regs, PGM_OPERATION_EXCEPTION);
        
    }

}
#endif /*FEATURE_VECTOR_FACILITY*/
