/* OPCODE.H     Instruction decoding functions - 02/07/00 Jan Jaeger */

/* Interpretive Execution - (c) Copyright Jan Jaeger, 1999-2000      */

typedef void (*zz_func) (BYTE inst[], int execflag, REGS *regs);

extern zz_func opcode_table[];
extern zz_func opcode_01xx[];
extern zz_func opcode_a4xx[];
extern zz_func opcode_a5xx[];
extern zz_func opcode_a6xx[];
extern zz_func opcode_a7xx[];
extern zz_func opcode_b2xx[];
extern zz_func opcode_b3xx[];
extern zz_func opcode_e4xx[];
extern zz_func opcode_e5xx[];

void execute_01xx (BYTE inst[], int execflag, REGS *regs);
void execute_a4xx (BYTE inst[], int execflag, REGS *regs);
void execute_a5xx (BYTE inst[], int execflag, REGS *regs);
void execute_a6xx (BYTE inst[], int execflag, REGS *regs);
void execute_a7xx (BYTE inst[], int execflag, REGS *regs);
void execute_b2xx (BYTE inst[], int execflag, REGS *regs);
void execute_b3xx (BYTE inst[], int execflag, REGS *regs);
void execute_e4xx (BYTE inst[], int execflag, REGS *regs);
void execute_e5xx (BYTE inst[], int execflag, REGS *regs);
void operation_exception (BYTE inst[], int execflag, REGS *regs);
void dummy_instruction (BYTE inst[], int execflag, REGS *regs);


/* The footprint_buffer option saves a copy of the register context
   every time an instruction is executed.  This is for problem 
   determination only, as it severely impacts performance.       *JJ */

#if !defined(FOOTPRINT_BUFFER)

#define EXECUTE_INSTRUCTION(_instruction, _execflag, _regs) \
        opcode_table[((_instruction)[0])]((_instruction), (_execflag), (_regs))

#else /*defined(FOOTPRINT_BUFFER)*/

#define EXECUTE_INSTRUCTION(_instruction, _execflag, _regs) \
        { \
            sysblk.footprregs[(_regs)->cpuad][sysblk.footprptr[(_regs)->cpuad]] = *(_regs); \
            memcpy(&sysblk.footprregs[(_regs)->cpuad][sysblk.footprptr[(_regs)->cpuad]++].inst,(_instruction),6); \
            sysblk.footprptr[(_regs)->cpuad] &= FOOTPRINT_BUFFER - 1; \
            opcode_table[((_instruction)[0])]((_instruction), (_execflag), (_regs)); \
        }

#endif /*defined(FOOTPRINT_BUFFER)*/


#define ODD_CHECK(_r, _regs) \
        if( (_r) & 1 ) \
            program_interrupt( (_regs), PGM_SPECIFICATION_EXCEPTION)

#define ODD2_CHECK(_r1, _r2, _regs) \
        if( ((_r1) & 1) || ((_r2) & 1) ) \
            program_interrupt( (_regs), PGM_SPECIFICATION_EXCEPTION)
 
#define FW_CHECK(_value, _regs) \
        if( (_value) & 3 ) \
            program_interrupt( (_regs), PGM_SPECIFICATION_EXCEPTION)
 
#define DW_CHECK(_value, _regs) \
        if( (_value) & 7 ) \
            program_interrupt( (_regs), PGM_SPECIFICATION_EXCEPTION)
 
        /* Program check if r1 is not 0, 2, 4, or 6 */
#define HFPREG_CHECK(_r, _regs) \
        if( (_r) & 9 ) \
            program_interrupt( (_regs), PGM_SPECIFICATION_EXCEPTION)

        /* Program check if r1 and r2 are not 0, 2, 4, or 6 */
#define HFPREG2_CHECK(_r1, _r2, _regs) \
        if( ((_r1) & 9) || ((_r2) & 9) ) \
            program_interrupt( (_regs), PGM_SPECIFICATION_EXCEPTION)
 
        /* Program check if r1 is not 0 or 4 */
#define HFPODD_CHECK(_r, _regs) \
        if( (_r) & 11 ) \
            program_interrupt( (_regs), PGM_SPECIFICATION_EXCEPTION)

        /* Program check if r1 and r2 are not 0 or 4 */
#define HFPODD2_CHECK(_r1, _r2, _regs) \
        if( ((_r1) & 11) || ((_r2) & 11) ) \
            program_interrupt( (_regs), PGM_SPECIFICATION_EXCEPTION)
 
#define PRIV_CHECK(_regs) \
        if( (_regs)->psw.prob ) \
            program_interrupt( (_regs), PGM_PRIVILEGED_OPERATION_EXCEPTION)

#define E(_inst, _execflag, _regs) \
        { \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 2; \
                (_regs)->psw.ia += 2; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

#define RR(_inst, _execflag, _regs, _r1, _r2) \
        { \
            (_r1) = (_inst)[1] >> 4; \
            (_r2) = (_inst)[1] & 0x0F; \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 2; \
                (_regs)->psw.ia += 2; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

#define RR_SVC(_inst, _execflag, _regs, _svc) \
        { \
            (_svc) = (_inst)[1]; \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 2; \
                (_regs)->psw.ia += 2; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

#define RX(_inst, _execflag, _regs, _r1, _b2, _effective_addr2) \
        { \
            (_r1) = (_inst)[1] >> 4; \
            (_b2) = (_inst)[1] & 0x0F; \
            (_effective_addr2) = (((_inst)[2] & 0x0F) << 8) | (_inst)[3]; \
            if((_b2) != 0) \
            { \
                (_effective_addr2) += (_regs)->gpr[(_b2)]; \
                (_effective_addr2) &= ADDRESS_MAXWRAP((_regs)); \
            } \
            (_b2) = (_inst)[2] >> 4; \
            if((_b2) != 0) \
            { \
                (_effective_addr2) += (_regs)->gpr[(_b2)]; \
                (_effective_addr2) &= ADDRESS_MAXWRAP((_regs)); \
            } \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 4; \
                (_regs)->psw.ia += 4; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

#define S(_inst, _execflag, _regs, _b2, _effective_addr2) \
        { \
            (_b2) = (_inst)[2] >> 4; \
            (_effective_addr2) = (((_inst)[2] & 0x0F) << 8) | (_inst)[3]; \
            if((_b2) != 0) \
            { \
                (_effective_addr2) += (_regs)->gpr[(_b2)]; \
                (_effective_addr2) &= ADDRESS_MAXWRAP((_regs)); \
            } \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 4; \
                (_regs)->psw.ia += 4; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

#define RS(_inst, _execflag, _regs, _r1, _r3, _b2, _effective_addr2) \
        { \
            (_r1) = (_inst)[1] >> 4; \
            (_r3) = (_inst)[1] & 0x0F; \
            (_b2) = (_inst)[2] >> 4; \
            (_effective_addr2) = (((_inst)[2] & 0x0F) << 8) | (_inst)[3]; \
            if((_b2) != 0) \
            { \
                (_effective_addr2) += (_regs)->gpr[(_b2)]; \
                (_effective_addr2) &= ADDRESS_MAXWRAP((_regs)); \
            } \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 4; \
                (_regs)->psw.ia += 4; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

#define RI(_inst, _execflag, _regs, _r1, _r3, _i2) \
        { \
            (_r1) = (_inst)[1] >> 4; \
            (_r3) = (_inst)[1] & 0x0F; \
            (_i2) = ((_inst)[2] << 8) | (_inst)[3]; \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 4; \
                (_regs)->psw.ia += 4; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

#define SI(_inst, _execflag, _regs, _i2, _b1, _effective_addr1) \
        { \
            (_i2) = (_inst)[1]; \
            (_b1) = (_inst)[2] >> 4; \
            (_effective_addr1) = (((_inst)[2] & 0x0F) << 8) | (_inst)[3]; \
            if((_b1) != 0) \
            { \
                (_effective_addr1) += (_regs)->gpr[(_b1)]; \
                (_effective_addr1) &= ADDRESS_MAXWRAP((_regs)); \
            } \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 4; \
                (_regs)->psw.ia += 4; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

#define RRE(_inst, _execflag, _regs, _r1, _r2) \
        { \
            (_r1) = (_inst)[3] >> 4; \
            (_r2) = (_inst)[3] & 0x0F; \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 4; \
                (_regs)->psw.ia += 4; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

#define SS(_inst, _execflag, _regs, _r1, _r3, \
            _b1, _effective_addr1, _b2, _effective_addr2) \
        { \
            (_r1) = (_inst)[1] >> 4; \
            (_r3) = (_inst)[1] & 0x0F; \
            (_b1) = (_inst)[2] >> 4; \
            (_effective_addr1) = (((_inst)[2] & 0x0F) << 8) | (_inst)[3]; \
            if((_b1) != 0) \
            { \
                (_effective_addr1) += (_regs)->gpr[(_b1)]; \
                (_effective_addr1) &= ADDRESS_MAXWRAP((_regs)); \
            } \
            (_b2) = (_inst)[4] >> 4; \
            (_effective_addr2) = (((_inst)[4] & 0x0F) << 8) | (_inst)[5]; \
            if((_b2) != 0) \
            { \
                (_effective_addr2) += (_regs)->gpr[(_b2)]; \
                (_effective_addr2) &= ADDRESS_MAXWRAP((_regs)); \
            } \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 6; \
                (_regs)->psw.ia += 6; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

#define SS_L(_inst, _execflag, _regs, _l, \
            _b1, _effective_addr1, _b2, _effective_addr2) \
        { \
            (_l) = (_inst)[1]; \
            (_b1) = (_inst)[2] >> 4; \
            (_effective_addr1) = (((_inst)[2] & 0x0F) << 8) | (_inst)[3]; \
            if((_b1) != 0) \
            { \
                (_effective_addr1) += (_regs)->gpr[(_b1)]; \
                (_effective_addr1) &= ADDRESS_MAXWRAP((_regs)); \
            } \
            (_b2) = (_inst)[4] >> 4; \
            (_effective_addr2) = (((_inst)[4] & 0x0F) << 8) | (_inst)[5]; \
            if((_b2) != 0) \
            { \
                (_effective_addr2) += (_regs)->gpr[(_b2)]; \
                (_effective_addr2) &= ADDRESS_MAXWRAP((_regs)); \
            } \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 6; \
                (_regs)->psw.ia += 6; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

#define SSE(_inst, _execflag, _regs, _b1, _effective_addr1, \
                                     _b2, _effective_addr2) \
        { \
            (_b1) = (_inst)[2] >> 4; \
            (_effective_addr1) = (((_inst)[2] & 0x0F) << 8) | (_inst)[3]; \
            if((_b1) != 0) \
            { \
                (_effective_addr1) += (_regs)->gpr[(_b1)]; \
                (_effective_addr1) &= ADDRESS_MAXWRAP((_regs)); \
            } \
            (_b2) = (_inst)[4] >> 4; \
            (_effective_addr2) = (((_inst)[4] & 0x0F) << 8) | (_inst)[5]; \
            if((_b2) != 0) \
            { \
                (_effective_addr2) += (_regs)->gpr[(_b2)]; \
                (_effective_addr2) &= ADDRESS_MAXWRAP((_regs)); \
            } \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 6; \
                (_regs)->psw.ia += 6; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }


#if defined(FEATURE_INTERPRETIVE_EXECUTION)

#define SIE_INTERCEPT(_regs) \
        { \
            if((_regs)->sie_state) \
                longjmp((_regs)->progjmp, SIE_INTERCEPT_INST); \
        }

#define SIE_MODE_370(_regs) \
        { \
            if(!((_regs)->sie_state && ((_regs)->siebk->m & SIE_M_370))) \
                program_interrupt((_regs), PGM_OPERATION_EXCEPTION); \
        }

#define SIE_MODE_XA(_regs) \
        { \
            if((_regs)->sie_state && !((_regs)->siebk->m & SIE_M_XA)) \
                program_interrupt((_regs), PGM_OPERATION_EXCEPTION); \
        }

#define SIE_TRANSLATE(_addr, _acctype, _regs) \
        { \
            if((_regs)->sie_state && !(_regs)->sie_pref) \
                *(_addr) = logical_to_abs ((_regs)->sie_mso + *(_addr), \
                  USE_PRIMARY_SPACE, (_regs)->hostregs, (_acctype), 0); \
        }

#else /*!defined(FEATURE_INTERPRETIVE_EXECUTION)*/

#define SIE_INTERCEPT(_regs)
#define SIE_MODE_370(_regs)
#define SIE_MODE_XA(_regs)
#define SIE_TRANSLATE(_addr, _acctype, _regs)

#endif /*!defined(FEATURE_INTERPRETIVE_EXECUTION)*/


#if defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)

#define SIE_MODE_XC_OPEX(_regs) \
        { \
            if(((_regs)->sie_state && ((_regs)->siebk->mx & SIE_MX_XC))) \
                program_interrupt((_regs), PGM_OPERATION_EXCEPTION); \
        }
        
#else /*!defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)*/

#define SIE_MODE_XC_OPEX(_regs)

#endif /*!defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE)*/

#if defined(FEATURE_VECTOR_FACILITY)

#define VOP_CHECK(_regs) \
        if(!((_regs)->cr[0] & CR0_VOP) || !(_regs)->vf->online) \
            program_interrupt((_regs), PGM_VECTOR_OPERATION_EXCEPTION)

#define VR_INUSE(_vr, _regs) \
        ((_regs)->vf->vsr & (VSR_VIU0 >> ((_vr) >> 1)))

#define VR_CHANGED(_vr, _regs) \
        ((_regs)->vf->vsr & (VSR_VCH0 >> ((_vr) >> 1)))

#define SET_VR_INUSE(_vr, _regs) \
        (_regs)->vf->vsr |= (VSR_VIU0 >> ((_vr) >> 1))

#define SET_VR_CHANGED(_vr, _regs) \
        (_regs)->vf->vsr |= (VSR_VCH0 >> ((_vr) >> 1))

#define RESET_VR_INUSE(_vr, _regs) \
        (_regs)->vf->vsr &= ~(VSR_VIU0 >> ((_vr) >> 1))

#define RESET_VR_CHANGED(_vr, _regs) \
        (_regs)->vf->vsr &= ~(VSR_VCH0 >> ((_vr) >> 1))

#define VMR_SET(_section, _regs) \
        ((_regs)->vf->vmr[(_section) >> 3] & (0x80 >> ((_section) & 7)))

#define MASK_MODE(_regs) \
        ((_regs)->vf->vsr & VSR_M)

#define VECTOR_COUNT(_regs) \
            (((_regs)->vf->vsr & VSR_VCT) >> 32)

#define VECTOR_IX(_regs) \
            (((_regs)->vf->vsr & VSR_VIX) >> 16)

/* VST and QST formats are the same */
#define VST(_inst, _execflag, _regs, _vr3, _rt2, _vr1, _rs2) \
        { \
            (_qr3) = (_inst)[2] >> 4; \
            (_rt2) = (_inst)[2] & 0x0F; \
            (_vr1) = (_inst)[3] >> 4; \
            (_rs2) = (_inst)[3] & 0x0F; \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 4; \
                (_regs)->psw.ia += 4; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

/* VR, VV and QV formats are the same */
#define VR(_inst, _execflag, _regs, _qr3, _vr1, _vr2) \
        { \
            (_qr3) = (_inst)[2] >> 4; \
            (_vr1) = (_inst)[3] >> 4; \
            (_vr2) = (_inst)[3] & 0x0F; \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 4; \
                (_regs)->psw.ia += 4; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

#define VS(_inst, _execflag, _regs, _rs2) \
        { \
            (_rs2) = (_inst)[3] & 0x0F; \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 4; \
                (_regs)->psw.ia += 4; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

#define RSE(_inst, _execflag, _regs, _r3, _vr1, \
                                     _b2, _effective_addr2) \
        { \
            (_r3) = (_inst)[2] >> 4; \
            (_vr1) = (_inst)[3] >> 4; \
            (_b2) = (_inst)[4] >> 4; \
            (_effective_addr2) = (((_inst)[4] & 0x0F) << 8) | (_inst)[5]; \
            if((_b2) != 0) \
            { \
                (_effective_addr2) += (_regs)->gpr[(_b2)]; \
                (_effective_addr2) &= ADDRESS_MAXWRAP((_regs)); \
            } \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 6; \
                (_regs)->psw.ia += 6; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

/* S format instructions where the effective address does not wrap */
#define S_NW(_inst, _execflag, _regs, _b2, _effective_addr2) \
        { \
            (_b2) = (_inst)[2] >> 4; \
            (_effective_addr2) = (((_inst)[2] & 0x0F) << 8) | (_inst)[3]; \
            if((_b2) != 0) \
            { \
                (_effective_addr2) += (_regs)->gpr[(_b2)]; \
            } \
            if( !(_execflag) ) \
            { \
                (_regs)->psw.ilc = 4; \
                (_regs)->psw.ia += 4; \
                (_regs)->psw.ia &= ADDRESS_MAXWRAP((_regs)); \
            } \
        }

#endif /*defined(FEATURE_VECTOR_FACILITY)*/


#if MAX_CPU_ENGINES > 1 && defined(SMP_SERIALIZATION)
        /* In order to syncronize mainstorage access we need to flush
           the cache on all processors, this needs special case when
           running on an SMP machine, as the physical CPU's actually
           need to perform this function.  This is accomplished by
           obtaining and releasing a mutex lock, which is intended to
           serialize storage access */
#define PERFORM_SERIALIZATION(_regs) \
        { \
            obtain_lock(&regs->serlock); \
            release_lock(&regs->serlock); \
        }
#else  /*!SERIALIZATION*/
#define PERFORM_SERIALIZATION(_regs)
#endif /*SERIALIZATION*/


#define PERFORM_CHKPT_SYNC(_regs)


/* Instructions in assist.c */
void zz_obtain_local_lock (BYTE inst[], int execflag, REGS *regs);
void zz_release_local_lock (BYTE inst[], int execflag, REGS *regs);
void zz_obtain_cms_lock (BYTE inst[], int execflag, REGS *regs);
void zz_release_cms_lock (BYTE inst[], int execflag, REGS *regs);

/* Instructions in cmpsc.c */
void zz_compression_call (BYTE inst[], int execflag, REGS *regs);

/* Instructions in control.c */
void zz_branch_and_set_authority (BYTE inst[], int execflag, REGS *regs);
void zz_branch_in_subspace_group (BYTE inst[], int execflag, REGS *regs);
void zz_branch_and_stack (BYTE inst[], int execflag, REGS *regs);
void zz_compare_and_swap_and_purge (BYTE inst[], int execflag, REGS *regs);
void zz_diagnose (BYTE inst[], int execflag, REGS *regs);
void zz_extract_primary_asn (BYTE inst[], int execflag, REGS *regs);
void zz_extract_secondary_asn (BYTE inst[], int execflag, REGS *regs);
void zz_extract_stacked_registers (BYTE inst[], int execflag, REGS *regs);
void zz_extract_stacked_state (BYTE inst[], int execflag, REGS *regs);
void zz_insert_address_space_control (BYTE inst[], int execflag, REGS *regs);
void zz_insert_psw_key (BYTE inst[], int execflag, REGS *regs);
void zz_insert_storage_key (BYTE inst[], int execflag, REGS *regs);
void zz_insert_storage_key_extended (BYTE inst[], int execflag, REGS *regs);
void zz_insert_virtual_storage_key (BYTE inst[], int execflag, REGS *regs);
void zz_invalidate_page_table_entry (BYTE inst[], int execflag, REGS *regs);
void zz_load_address_space_parameters (BYTE inst[], int execflag, REGS *regs);
void zz_load_control (BYTE inst[], int execflag, REGS *regs);
void zz_load_psw (BYTE inst[], int execflag, REGS *regs);
void zz_load_real_address (BYTE inst[], int execflag, REGS *regs);
void zz_load_using_real_address (BYTE inst[], int execflag, REGS *regs);
void zz_modify_stacked_state (BYTE inst[], int execflag, REGS *regs);
void zz_move_to_primary (BYTE inst[], int execflag, REGS *regs);
void zz_move_to_secondary (BYTE inst[], int execflag, REGS *regs);
void zz_move_with_destination_key (BYTE inst[], int execflag, REGS *regs);
void zz_move_with_key (BYTE inst[], int execflag, REGS *regs);
void zz_move_with_source_key (BYTE inst[], int execflag, REGS *regs);
void zz_program_call (BYTE inst[], int execflag, REGS *regs);
void zz_program_return (BYTE inst[], int execflag, REGS *regs);
void zz_program_transfer (BYTE inst[], int execflag, REGS *regs);
void zz_purge_alb (BYTE inst[], int execflag, REGS *regs);
void zz_purge_tlb (BYTE inst[], int execflag, REGS *regs);
void zz_reset_reference_bit (BYTE inst[], int execflag, REGS *regs);
void zz_reset_reference_bit_extended (BYTE inst[], int execflag, REGS *regs);
void zz_set_address_space_control_x (BYTE inst[], int execflag, REGS *regs);
void zz_set_clock (BYTE inst[], int execflag, REGS *regs);
void zz_set_clock_comparator (BYTE inst[], int execflag, REGS *regs);
void zz_set_clock_programmable_field (BYTE inst[], int execflag, REGS *regs);
void zz_set_cpu_timer (BYTE inst[], int execflag, REGS *regs);
void zz_set_prefix (BYTE inst[], int execflag, REGS *regs);
void zz_set_psw_key_from_address (BYTE inst[], int execflag, REGS *regs);
void zz_set_secondary_asn (BYTE inst[], int execflag, REGS *regs);
void zz_set_storage_key (BYTE inst[], int execflag, REGS *regs);
void zz_set_storage_key_extended (BYTE inst[], int execflag, REGS *regs);
void zz_set_system_mask (BYTE inst[], int execflag, REGS *regs);
void zz_signal_procesor (BYTE inst[], int execflag, REGS *regs);
void zz_store_clock_comparator (BYTE inst[], int execflag, REGS *regs);
void zz_store_control (BYTE inst[], int execflag, REGS *regs);
void zz_store_cpu_address (BYTE inst[], int execflag, REGS *regs);
void zz_store_cpu_id (BYTE inst[], int execflag, REGS *regs);
void zz_store_cpu_timer (BYTE inst[], int execflag, REGS *regs);
void zz_store_prefix (BYTE inst[], int execflag, REGS *regs);
void zz_store_then_and_system_mask (BYTE inst[], int execflag, REGS *regs);
void zz_store_then_or_system_mask (BYTE inst[], int execflag, REGS *regs);
void zz_store_using_real_address (BYTE inst[], int execflag, REGS *regs);
void zz_test_access (BYTE inst[], int execflag, REGS *regs);
void zz_test_block (BYTE inst[], int execflag, REGS *regs);
void zz_test_protection (BYTE inst[], int execflag, REGS *regs);
void zz_trace (BYTE inst[], int execflag, REGS *regs);


/* Instructions in decimal.c */
void zz_add_decimal (BYTE inst[], int execflag, REGS *regs);
void zz_compare_decimal (BYTE inst[], int execflag, REGS *regs);
void zz_divide_decimal (BYTE inst[], int execflag, REGS *regs);
void zz_edit_x_edit_and_mark (BYTE inst[], int execflag, REGS *regs);
void zz_multiply_decimal (BYTE inst[], int execflag, REGS *regs);
void zz_shift_and_round_decimal (BYTE inst[], int execflag, REGS *regs);
void zz_subtract_decimal (BYTE inst[], int execflag, REGS *regs);
void zz_zero_and_add (BYTE inst[], int execflag, REGS *regs);


/* Instructions in vm.c */
void zz_inter_user_communication_vehicle (BYTE inst[], int execflag, REGS *regs);


/* Instructions in vm.c */
void zz_start_interpretive_execution (BYTE inst[], int execflag, REGS *regs);


/* Instructions in float.c */
void zz_load_positive_float_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_load_negative_float_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_load_and_test_float_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_load_complement_float_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_halve_float_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_round_float_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_multiply_float_ext_reg (BYTE inst[], int execflag, REGS *regs);
void zz_multiply_float_long_to_ext_reg (BYTE inst[], int execflag, REGS *regs);
void zz_load_float_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_compare_float_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_add_float_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_subtract_float_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_multiply_float_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_divide_float_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_add_unnormal_float_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_subtract_unnormal_float_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_load_positive_float_short_reg (BYTE inst[], int execflag, REGS *regs);
void zz_load_negative_float_short_reg (BYTE inst[], int execflag, REGS *regs);
void zz_load_and_test_float_short_reg (BYTE inst[], int execflag, REGS *regs);
void zz_load_complement_float_short_reg (BYTE inst[], int execflag, REGS *regs);
void zz_halve_float_short_reg (BYTE inst[], int execflag, REGS *regs);
void zz_round_float_short_reg (BYTE inst[], int execflag, REGS *regs);
void zz_add_float_ext_reg (BYTE inst[], int execflag, REGS *regs);
void zz_subtract_float_ext_reg (BYTE inst[], int execflag, REGS *regs);
void zz_load_float_short_reg (BYTE inst[], int execflag, REGS *regs);
void zz_compare_float_short_reg (BYTE inst[], int execflag, REGS *regs);
void zz_add_float_short_reg (BYTE inst[], int execflag, REGS *regs);
void zz_subtract_float_short_reg (BYTE inst[], int execflag, REGS *regs);
void zz_multiply_float_short_to_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_divide_float_short_reg (BYTE inst[], int execflag, REGS *regs);
void zz_add_unnormal_float_short_reg (BYTE inst[], int execflag, REGS *regs);
void zz_subtract_unnormal_float_short_reg (BYTE inst[], int execflag, REGS *regs);
void zz_store_float_long (BYTE inst[], int execflag, REGS *regs);
void zz_multiply_float_long_to_ext (BYTE inst[], int execflag, REGS *regs);
void zz_load_float_long (BYTE inst[], int execflag, REGS *regs);
void zz_compare_float_long (BYTE inst[], int execflag, REGS *regs);
void zz_add_float_long (BYTE inst[], int execflag, REGS *regs);
void zz_subtract_float_long (BYTE inst[], int execflag, REGS *regs);
void zz_multiply_float_long (BYTE inst[], int execflag, REGS *regs);
void zz_divide_float_long (BYTE inst[], int execflag, REGS *regs);
void zz_add_unnormal_float_long (BYTE inst[], int execflag, REGS *regs);
void zz_subtract_unnormal_float_long (BYTE inst[], int execflag, REGS *regs);
void zz_store_float_short (BYTE inst[], int execflag, REGS *regs);
void zz_load_float_short (BYTE inst[], int execflag, REGS *regs);
void zz_compare_float_short (BYTE inst[], int execflag, REGS *regs);
void zz_add_float_short (BYTE inst[], int execflag, REGS *regs);
void zz_subtract_float_short (BYTE inst[], int execflag, REGS *regs);
void zz_multiply_float_short_to_long (BYTE inst[], int execflag, REGS *regs);
void zz_divide_float_short (BYTE inst[], int execflag, REGS *regs);
void zz_add_unnormal_float_short (BYTE inst[], int execflag, REGS *regs);
void zz_subtract_unnormal_float_short (BYTE inst[], int execflag, REGS *regs);
void zz_divide_float_ext_reg (BYTE inst[], int execflag, REGS *regs);
void zz_squareroot_float_long_reg (BYTE inst[], int execflag, REGS *regs);
void zz_squareroot_float_short_reg (BYTE inst[], int execflag, REGS *regs);


/* Instructions in general.c */
void zz_add_register (BYTE inst[], int execflag, REGS *regs);
void zz_add (BYTE inst[], int execflag, REGS *regs);
void zz_add_halfword (BYTE inst[], int execflag, REGS *regs);
void zz_add_halfword_immediate (BYTE inst[], int execflag, REGS *regs);
void zz_add_logical_register (BYTE inst[], int execflag, REGS *regs);
void zz_add_logical (BYTE inst[], int execflag, REGS *regs);
void zz_and_register (BYTE inst[], int execflag, REGS *regs);
void zz_and (BYTE inst[], int execflag, REGS *regs);
void zz_and_immediate (BYTE inst[], int execflag, REGS *regs);
void zz_and_character (BYTE inst[], int execflag, REGS *regs);
void zz_branch_and_link_register (BYTE inst[], int execflag, REGS *regs);
void zz_branch_and_link (BYTE inst[], int execflag, REGS *regs);
void zz_branch_and_save_register (BYTE inst[], int execflag, REGS *regs);
void zz_branch_and_save (BYTE inst[], int execflag, REGS *regs);
void zz_branch_and_save_and_set_mode (BYTE inst[], int execflag, REGS *regs);
void zz_branch_and_set_mode (BYTE inst[], int execflag, REGS *regs);
void zz_branch_on_condition_register (BYTE inst[], int execflag, REGS *regs);
void zz_branch_on_condition (BYTE inst[], int execflag, REGS *regs);
void zz_branch_on_count_register (BYTE inst[], int execflag, REGS *regs);
void zz_branch_on_count (BYTE inst[], int execflag, REGS *regs);
void zz_branch_on_index_high (BYTE inst[], int execflag, REGS *regs);
void zz_branch_on_index_low_or_equal (BYTE inst[], int execflag, REGS *regs);
void zz_branch_relative_on_condition (BYTE inst[], int execflag, REGS *regs);
void zz_branch_relative_and_save (BYTE inst[], int execflag, REGS *regs);
void zz_branch_relative_on_count (BYTE inst[], int execflag, REGS *regs);
void zz_branch_relative_on_index_high (BYTE inst[], int execflag, REGS *regs);
void zz_branch_relative_on_index_low_or_equal (BYTE inst[], int execflag, REGS *regs);
void zz_checksum (BYTE inst[], int execflag, REGS *regs);
void zz_compare_register (BYTE inst[], int execflag, REGS *regs);
void zz_compare (BYTE inst[], int execflag, REGS *regs);
void zz_compare_and_form_codeword (BYTE inst[], int execflag, REGS *regs);
void zz_compare_and_swap (BYTE inst[], int execflag, REGS *regs);
void zz_compare_double_and_swap (BYTE inst[], int execflag, REGS *regs);
void zz_compare_halfword (BYTE inst[], int execflag, REGS *regs);
void zz_compare_halfword_immediate (BYTE inst[], int execflag, REGS *regs);
void zz_compare_logical_register (BYTE inst[], int execflag, REGS *regs);
void zz_compare_logical (BYTE inst[], int execflag, REGS *regs);
void zz_compare_logical_immediate (BYTE inst[], int execflag, REGS *regs);
void zz_compare_logical_character (BYTE inst[], int execflag, REGS *regs);
void zz_compare_logical_characters_under_mask (BYTE inst[], int execflag, REGS *regs);
void zz_compare_logical_long (BYTE inst[], int execflag, REGS *regs);
void zz_compare_logical_long_extended (BYTE inst[], int execflag, REGS *regs);
void zz_compare_logical_string (BYTE inst[], int execflag, REGS *regs);
void zz_compare_until_substring_equal (BYTE inst[], int execflag, REGS *regs);
void zz_convert_to_binary (BYTE inst[], int execflag, REGS *regs);
void zz_convert_to_decimal (BYTE inst[], int execflag, REGS *regs);
void zz_copy_access (BYTE inst[], int execflag, REGS *regs);
void zz_divide_register (BYTE inst[], int execflag, REGS *regs);
void zz_divide (BYTE inst[], int execflag, REGS *regs);
void zz_exclusive_or_register (BYTE inst[], int execflag, REGS *regs);
void zz_exclusive_or (BYTE inst[], int execflag, REGS *regs);
void zz_exclusive_or_immediate (BYTE inst[], int execflag, REGS *regs);
void zz_exclusive_or_character (BYTE inst[], int execflag, REGS *regs);
void zz_execute (BYTE inst[], int execflag, REGS *regs);
void zz_extract_access_register (BYTE inst[], int execflag, REGS *regs);
void zz_insert_character (BYTE inst[], int execflag, REGS *regs);
void zz_insert_characters_under_mask (BYTE inst[], int execflag, REGS *regs);
void zz_insert_program_mask (BYTE inst[], int execflag, REGS *regs);
void zz_load (BYTE inst[], int execflag, REGS *regs);
void zz_load_register (BYTE inst[], int execflag, REGS *regs);
void zz_load_access_multiple (BYTE inst[], int execflag, REGS *regs);
void zz_load_address (BYTE inst[], int execflag, REGS *regs);
void zz_load_address_extended (BYTE inst[], int execflag, REGS *regs);
void zz_load_and_test_register (BYTE inst[], int execflag, REGS *regs);
void zz_load_complement_register (BYTE inst[], int execflag, REGS *regs);
void zz_load_halfword (BYTE inst[], int execflag, REGS *regs);
void zz_load_halfword_immediate (BYTE inst[], int execflag, REGS *regs);
void zz_load_multiple (BYTE inst[], int execflag, REGS *regs);
void zz_load_negative_register (BYTE inst[], int execflag, REGS *regs);
void zz_load_positive_register (BYTE inst[], int execflag, REGS *regs);
void zz_monitor_call (BYTE inst[], int execflag, REGS *regs);
void zz_move_immediate (BYTE inst[], int execflag, REGS *regs);
void zz_move_character (BYTE inst[], int execflag, REGS *regs);
void zz_move_inverse (BYTE inst[], int execflag, REGS *regs);
void zz_move_long (BYTE inst[], int execflag, REGS *regs);
void zz_move_long_extended (BYTE inst[], int execflag, REGS *regs);
void zz_move_numerics (BYTE inst[], int execflag, REGS *regs);
void zz_move_string (BYTE inst[], int execflag, REGS *regs);
void zz_move_with_offset (BYTE inst[], int execflag, REGS *regs);
void zz_move_zones (BYTE inst[], int execflag, REGS *regs);
void zz_multiply_register (BYTE inst[], int execflag, REGS *regs);
void zz_multiply (BYTE inst[], int execflag, REGS *regs);
void zz_multiply_halfword (BYTE inst[], int execflag, REGS *regs);
void zz_multiply_halfword_immediate (BYTE inst[], int execflag, REGS *regs);
void zz_multiply_single_register (BYTE inst[], int execflag, REGS *regs);
void zz_multiply_single (BYTE inst[], int execflag, REGS *regs);
void zz_or_register (BYTE inst[], int execflag, REGS *regs);
void zz_or (BYTE inst[], int execflag, REGS *regs);
void zz_or_immediate (BYTE inst[], int execflag, REGS *regs);
void zz_or_character (BYTE inst[], int execflag, REGS *regs);
void zz_perform_locked_operation (BYTE inst[], int execflag, REGS *regs);
void zz_pack (BYTE inst[], int execflag, REGS *regs);
void zz_search_string (BYTE inst[], int execflag, REGS *regs);
void zz_set_access_register (BYTE inst[], int execflag, REGS *regs);
void zz_set_program_mask (BYTE inst[], int execflag, REGS *regs);
void zz_shift_left_double (BYTE inst[], int execflag, REGS *regs);
void zz_shift_left_double_logical (BYTE inst[], int execflag, REGS *regs);
void zz_shift_left_single (BYTE inst[], int execflag, REGS *regs);
void zz_shift_left_single_logical (BYTE inst[], int execflag, REGS *regs);
void zz_shift_right_double (BYTE inst[], int execflag, REGS *regs);
void zz_shift_right_double_logical (BYTE inst[], int execflag, REGS *regs);
void zz_shift_right_single (BYTE inst[], int execflag, REGS *regs);
void zz_shift_right_single_logical (BYTE inst[], int execflag, REGS *regs);
void zz_store (BYTE inst[], int execflag, REGS *regs);
void zz_store_access_multiple (BYTE inst[], int execflag, REGS *regs);
void zz_store_character (BYTE inst[], int execflag, REGS *regs);
void zz_store_characters_under_mask (BYTE inst[], int execflag, REGS *regs);
void zz_store_clock (BYTE inst[], int execflag, REGS *regs);
void zz_store_clock_extended (BYTE inst[], int execflag, REGS *regs);
void zz_store_halfword (BYTE inst[], int execflag, REGS *regs);
void zz_store_multiple (BYTE inst[], int execflag, REGS *regs);
void zz_subtract_register (BYTE inst[], int execflag, REGS *regs);
void zz_subtract (BYTE inst[], int execflag, REGS *regs);
void zz_subtract_halfword (BYTE inst[], int execflag, REGS *regs);
void zz_subtract_logical_register (BYTE inst[], int execflag, REGS *regs);
void zz_subtract_logical (BYTE inst[], int execflag, REGS *regs);
void zz_supervisor_call (BYTE inst[], int execflag, REGS *regs);
void zz_test_and_set (BYTE inst[], int execflag, REGS *regs);
void zz_test_under_mask (BYTE inst[], int execflag, REGS *regs);
void zz_test_under_mask_high (BYTE inst[], int execflag, REGS *regs);
void zz_test_under_mask_low (BYTE inst[], int execflag, REGS *regs);
void zz_translate (BYTE inst[], int execflag, REGS *regs);
void zz_translate_and_test (BYTE inst[], int execflag, REGS *regs);
void zz_unpack (BYTE inst[], int execflag, REGS *regs);
void zz_update_tree (BYTE inst[], int execflag, REGS *regs);


/* Instructions in io.c */
void zz_clear_subchannel (BYTE inst[], int execflag, REGS *regs);
void zz_halt_subchannel (BYTE inst[], int execflag, REGS *regs);
void zz_modify_subchannel (BYTE inst[], int execflag, REGS *regs);
void zz_resume_subchannel (BYTE inst[], int execflag, REGS *regs);
void zz_set_address_limit (BYTE inst[], int execflag, REGS *regs);
void zz_set_channel_monitor (BYTE inst[], int execflag, REGS *regs);
void zz_reset_channel_path (BYTE inst[], int execflag, REGS *regs);
void zz_start_subchannel (BYTE inst[], int execflag, REGS *regs);
void zz_store_channel_path_status (BYTE inst[], int execflag, REGS *regs);
void zz_store_channel_report_word (BYTE inst[], int execflag, REGS *regs);
void zz_store_subchannel (BYTE inst[], int execflag, REGS *regs);
void zz_test_pending_interruption (BYTE inst[], int execflag, REGS *regs);
void zz_test_subchannel (BYTE inst[], int execflag, REGS *regs);
void zz_s370_startio (BYTE inst[], int execflag, REGS *regs);
void zz_s370_testio (BYTE inst[], int execflag, REGS *regs);
void zz_s370_haltio (BYTE inst[], int execflag, REGS *regs);
void zz_s370_test_channel (BYTE inst[], int execflag, REGS *regs);
void zz_s370_store_channelid (BYTE inst[], int execflag, REGS *regs);
void zz_s370_connect_channel_set (BYTE inst[], int execflag, REGS *regs);
void zz_s370_disconnect_channel_set (BYTE inst[], int execflag, REGS *regs);


/* Instructions in service.c */
void zz_service_call (BYTE inst[], int execflag, REGS *regs);


/* Instructions in xstore.c */
void zz_page_in (BYTE inst[], int execflag, REGS *regs);
void zz_page_out (BYTE inst[], int execflag, REGS *regs);
void zz_lock_page (BYTE inst[], int execflag, REGS *regs);
void zz_invalidate_expanded_storage_block_entry (BYTE inst[], int execflag, REGS *regs);
void zz_move_page (BYTE inst[], int execflag, REGS *regs);


/* Instructions in vector.c */
void zz_v_test_vmr (BYTE inst[], int execflag, REGS *regs);
void zz_v_complement_vmr (BYTE inst[], int execflag, REGS *regs);
void zz_v_count_left_zeros_in_vmr (BYTE inst[], int execflag, REGS *regs);
void zz_v_count_ones_in_vmr (BYTE inst[], int execflag, REGS *regs);
void zz_v_extract_vct (BYTE inst[], int execflag, REGS *regs);
void zz_v_extract_vector_modes (BYTE inst[], int execflag, REGS *regs);
void zz_v_restore_vr (BYTE inst[], int execflag, REGS *regs);
void zz_v_save_changed_vr (BYTE inst[], int execflag, REGS *regs);
void zz_v_save_vr (BYTE inst[], int execflag, REGS *regs);
void zz_v_load_vmr (BYTE inst[], int execflag, REGS *regs);
void zz_v_load_vmr_complement (BYTE inst[], int execflag, REGS *regs);
void zz_v_store_vmr (BYTE inst[], int execflag, REGS *regs);
void zz_v_and_to_vmr (BYTE inst[], int execflag, REGS *regs);
void zz_v_or_to_vmr (BYTE inst[], int execflag, REGS *regs);
void zz_v_exclusive_or_to_vmr (BYTE inst[], int execflag, REGS *regs);
void zz_v_save_vsr (BYTE inst[], int execflag, REGS *regs);
void zz_v_save_vmr (BYTE inst[], int execflag, REGS *regs);
void zz_v_restore_vsr (BYTE inst[], int execflag, REGS *regs);
void zz_v_restore_vmr (BYTE inst[], int execflag, REGS *regs);
void zz_v_load_vct_from_address (BYTE inst[], int execflag, REGS *regs);
void zz_v_clear_vr (BYTE inst[], int execflag, REGS *regs);
void zz_v_set_vector_mask_mode (BYTE inst[], int execflag, REGS *regs);
void zz_v_load_vix_from_address (BYTE inst[], int execflag, REGS *regs);
void zz_v_store_vector_parameters (BYTE inst[], int execflag, REGS *regs);
void zz_v_save_vac (BYTE inst[], int execflag, REGS *regs);
void zz_v_restore_vac (BYTE inst[], int execflag, REGS *regs);
