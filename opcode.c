/* OPCODE.C     Instruction decoding functions - 02/07/00 Jan Jaeger */

#include "hercules.h"

#include "opcode.h"

#if !defined(FEATURE_CHANNEL_SUBSYSTEM)
 #define zz_clear_subchannel                    operation_exception
 #define zz_halt_subchannel                     operation_exception
 #define zz_modify_subchannel                   operation_exception
 #define zz_resume_subchannel                   operation_exception
 #define zz_set_channel_monitor                 operation_exception
 #define zz_start_subchannel                    operation_exception
 #define zz_store_channel_path_status           operation_exception
 #define zz_store_channel_report_word           operation_exception
 #define zz_store_subchannel                    operation_exception
 #define zz_test_pending_interruption           operation_exception
 #define zz_test_subchannel                     operation_exception
#endif /*!defined(FEATURE_CHANNEL_SUBSYSTEM)*/


#if !defined(FEATURE_S370_CHANNEL)
 #define zz_s370_startio                        operation_exception
 #define zz_s370_testio                         operation_exception
 #define zz_s370_haltio                         operation_exception
 #define zz_s370_test_channel                   operation_exception
 #define zz_s370_store_channelid                operation_exception
#endif /*!defined(FEATURE_S370_CHANNEL)*/ 


#if !defined(FEATURE_IMMEDIATE_AND_RELATIVE)
 #define zz_test_under_mask_high                operation_exception
 #define zz_test_under_mask_low                 operation_exception
 #define zz_branch_relative_on_condition        operation_exception
 #define zz_branch_relative_and_save            operation_exception
 #define zz_branch_relative_on_count            operation_exception
 #define zz_load_halfword_immediate             operation_exception
 #define zz_add_halfword_immediate              operation_exception
 #define zz_multiply_halfword_immediate         operation_exception
 #define zz_compare_halfword_immediate          operation_exception
 #define zz_multiply_single_register            operation_exception
 #define zz_multiply_single                     operation_exception
 #define zz_branch_relative_on_index_high       operation_exception
 #define zz_branch_relative_on_index_low_or_equal operation_exception
#endif /*!defined(FEATURE_IMMEDIATE_AND_RELATIVE)*/


#if !defined(FEATURE_COMPARE_AND_MOVE_EXTENDED)
 #define zz_compare_logical_long_extended       operation_exception
 #define zz_move_long_extended                  operation_exception
#endif /*!defined(FEATURE_COMPARE_AND_MOVE_EXTENDED)*/


#if !defined(FEATURE_CHECKSUM_INSTRUCTION)
 #define zz_checksum                            operation_exception
#endif /*!defined(FEATURE_CHECKSUM_INSTRUCTION)*/


#if !defined(FEATURE_PLO)
 #define zz_perform_locked_operation            operation_exception
#endif /*!defined(FEATURE_PLO)*/


#if !defined(FEATURE_SUBSPACE_GROUP)
 #define zz_branch_in_subspace_group            operation_exception
#endif /*!defined(FEATURE_SUBSPACE_GROUP)*/


#if !defined(FEATURE_BRANCH_AND_SET_AUTHORITY)
 #define zz_branch_and_set_authority            operation_exception
#endif /*!defined(FEATURE_BRANCH_AND_SET_AUTHORITY)*/


#if !defined(FEATURE_EXPANDED_STORAGE)
 #define zz_page_in                             operation_exception
 #define zz_page_out                            operation_exception
#endif /*!defined(FEATURE_EXPANDED_STORAGE)*/


#if !defined(FEATURE_BROADCASTED_PURGING)
 #define zz_compare_and_swap_and_purge          operation_exception
#endif /*!defined(FEATURE_BROADCASTED_PURGING)*/


#if !defined(FEATURE_BIMODAL_ADDRESSING)
 #define zz_branch_and_set_mode                 operation_exception
 #define zz_branch_and_save_and_set_mode        operation_exception
#endif /*!defined(FEATURE_BIMODAL_ADDRESSING)*/


#if !defined(FEATURE_MOVE_PAGE_FACILITY_2)
 #define zz_move_page                           operation_exception
 #define zz_invalidate_expanded_storage_block_entry operation_exception
#endif /*!defined(FEATURE_MOVE_PAGE_FACILITY_2)*/


#if !defined(FEATURE_BASIC_STORAGE_KEYS)
 #define zz_insert_storage_key                  operation_exception
 #define zz_set_storage_key                     operation_exception
 #define zz_reset_reference_bit                 operation_exception
#endif /*!defined(FEATURE_BASIC_STORAGE_KEYS)*/


#if !defined(FEATURE_LINKAGE_STACK)
 #define zz_branch_and_stack                    operation_exception
 #define zz_modify_stacked_state                operation_exception
 #define zz_extract_stacked_registers           operation_exception
 #define zz_extract_stacked_state               operation_exception
#endif /*!defined(FEATURE_LINKAGE_STACK)*/


#if !defined(FEATURE_DUAL_ADDRESS_SPACE)
 #define zz_insert_address_space_control        operation_exception
 #define zz_set_secondary_asn                   operation_exception
 #define zz_extract_primary_asn                 operation_exception
 #define zz_extract_secondary_asn               operation_exception
 #define zz_program_call                        operation_exception
 #define zz_program_transfer                    operation_exception
 #define zz_set_address_space_control_x         operation_exception
 #define zz_load_address_space_parameters       operation_exception
#endif /*!defined(FEATURE_DUAL_ADDRESS_SPACE)*/


#if !defined(FEATURE_ACCESS_REGISTERS)
 #define zz_load_access_multiple                operation_exception
 #define zz_store_access_multiple               operation_exception
 #define zz_purge_alb                           operation_exception
 #define zz_test_access                         operation_exception
 #define zz_copy_access                         operation_exception
 #define zz_set_access_register                 operation_exception
 #define zz_extract_access_register             operation_exception
#endif /*!defined(FEATURE_ACCESS_REGISTERS)*/


#if !defined(FEATURE_EXTENDED_STORAGE_KEYS)
 #define zz_insert_storage_key_extended         operation_exception
 #define zz_reset_reference_bit_extended        operation_exception
 #define zz_set_storage_key_extended            operation_exception
#endif /*!defined(FEATURE_EXTENDED_STORAGE_KEYS)*/


#if !defined(FEATURE_EXTENDED_TOD_CLOCK)
 #define zz_set_clock_programmable_field        operation_exception
 #define zz_store_clock_extended                operation_exception
#endif /*!defined(FEATURE_EXTENDED_TOD_CLOCK)*/


#if !defined(FEATURE_VECTOR_FACILITY)
 #define execute_a4xx                           operation_exception
 #define execute_a5xx                           operation_exception
 #define execute_a6xx                           operation_exception
 #define execute_e4xx                           operation_exception
#endif /*!defined(FEATURE_VECTOR_FACILITY)*/


#if !defined(FEATURE_HEXADECIMAL_FLOATING_POINT)
 #define zz_load_positive_float_long_reg        operation_exception
 #define zz_load_negative_float_long_reg        operation_exception
 #define zz_load_and_test_float_long_reg        operation_exception
 #define zz_load_complement_float_long_reg      operation_exception
 #define zz_halve_float_long_reg                operation_exception
 #define zz_round_float_long_reg                operation_exception
 #define zz_multiply_float_ext_reg              operation_exception
 #define zz_multiply_float_long_to_ext_reg      operation_exception
 #define zz_load_float_long_reg                 operation_exception
 #define zz_compare_float_long_reg              operation_exception
 #define zz_add_float_long_reg                  operation_exception
 #define zz_subtract_float_long_reg             operation_exception
 #define zz_multiply_float_long_reg             operation_exception
 #define zz_divide_float_long_reg               operation_exception
 #define zz_add_unnormal_float_long_reg         operation_exception
 #define zz_subtract_unnormal_float_long_reg    operation_exception
 #define zz_load_positive_float_short_reg       operation_exception
 #define zz_load_negative_float_short_reg       operation_exception
 #define zz_load_and_test_float_short_reg       operation_exception
 #define zz_load_complement_float_short_reg     operation_exception
 #define zz_halve_float_short_reg               operation_exception
 #define zz_round_float_short_reg               operation_exception
 #define zz_add_float_ext_reg                   operation_exception
 #define zz_subtract_float_ext_reg              operation_exception
 #define zz_load_float_short_reg                operation_exception
 #define zz_compare_float_short_reg             operation_exception
 #define zz_add_float_short_reg                 operation_exception
 #define zz_subtract_float_short_reg            operation_exception
 #define zz_multiply_float_short_to_long_reg    operation_exception
 #define zz_divide_float_short_reg              operation_exception
 #define zz_add_unnormal_float_short_reg        operation_exception
 #define zz_subtract_unnormal_float_short_reg   operation_exception
 #define zz_store_float_long                    operation_exception
 #define zz_multiply_float_long_to_ext          operation_exception
 #define zz_load_float_long                     operation_exception
 #define zz_compare_float_long                  operation_exception
 #define zz_add_float_long                      operation_exception
 #define zz_subtract_float_long                 operation_exception
 #define zz_multiply_float_long                 operation_exception
 #define zz_divide_float_long                   operation_exception
 #define zz_add_unnormal_float_long             operation_exception
 #define zz_subtract_unnormal_float_long        operation_exception
 #define zz_store_float_short                   operation_exception
 #define zz_load_float_short                    operation_exception
 #define zz_compare_float_short                 operation_exception
 #define zz_add_float_short                     operation_exception
 #define zz_subtract_float_short                operation_exception
 #define zz_multiply_float_short_to_long        operation_exception
 #define zz_divide_float_short                  operation_exception
 #define zz_add_unnormal_float_short            operation_exception
 #define zz_subtract_unnormal_float_short       operation_exception
 #define zz_divide_float_ext_reg                operation_exception
#endif /*!defined(FEATURE_HEXADECIMAL_FLOATING_POINT)*/


#if !defined(FEATURE_EMULATE_VM)
 #define zz_inter_user_communication_vehicle    operation_exception
#endif /*!defined(FEATURE_EMULATE_VM)*/


#if !defined(FEATURE_CMPSC)
 #define zz_compression_call			operation_exception
#endif /*!defined(FEATURE_CMPSC)*/


zz_func opcode_table[256] = {
 /*00*/         &operation_exception,            
 /*01*/         &execute_01xx,                          /* 01XX      */
 /*02*/         &operation_exception,            
 /*03*/         &operation_exception,            
 /*04*/         &zz_set_program_mask,                   /* SPM       */ 
 /*05*/         &zz_branch_and_link_register,           /* BALR      */
 /*06*/         &zz_branch_on_count_register,           /* BCTR      */
 /*07*/         &zz_branch_on_condition_register,       /* BCR       */
 /*08*/         &zz_set_storage_key,                    /* SSK       */
 /*09*/         &zz_insert_storage_key,                 /* ISK       */
 /*0A*/         &zz_supervisor_call,                    /* SVC       */
 /*0B*/         &zz_branch_and_set_mode,                /* BSM       */
 /*0C*/         &zz_branch_and_save_and_set_mode,       /* BASSM     */
 /*0D*/         &zz_branch_and_save_register,           /* BASR      */
 /*0E*/         &zz_move_long,                          /* MVCL      */
 /*0F*/         &zz_compare_logical_long,               /* CLCL      */
 /*10*/         &zz_load_positive_register,             /* LPR       */
 /*11*/         &zz_load_negative_register,             /* LNR       */
 /*12*/         &zz_load_and_test_register,             /* LTR       */
 /*13*/         &zz_load_complement_register,           /* LCR       */
 /*14*/         &zz_and_register,                       /* NR        */   
 /*15*/         &zz_compare_logical_register,           /* CLR       */
 /*16*/         &zz_or_register,                        /* OR        */
 /*17*/         &zz_exclusive_or_register,              /* XR        */
 /*18*/         &zz_load_register,                      /* LR        */
 /*19*/         &zz_compare_register,                   /* CR        */
 /*1A*/         &zz_add_register,                       /* AR        */
 /*1B*/         &zz_subtract_register,                  /* SR        */
 /*1C*/         &zz_multiply_register,                  /* MR        */
 /*1D*/         &zz_divide_register,                    /* DR        */
 /*1E*/         &zz_add_logical_register,               /* ALR       */
 /*1F*/         &zz_subtract_logical_register,          /* SLR       */
 /*20*/         &zz_load_positive_float_long_reg,       /* LPDR      */
 /*21*/         &zz_load_negative_float_long_reg,       /* LNDR      */
 /*22*/         &zz_load_and_test_float_long_reg,       /* LTDR      */
 /*23*/         &zz_load_complement_float_long_reg,     /* LCDR      */
 /*24*/         &zz_halve_float_long_reg,               /* HDR       */
 /*25*/         &zz_round_float_long_reg,               /* LRDR      */
 /*26*/         &zz_multiply_float_ext_reg,             /* MXR       */
 /*27*/         &zz_multiply_float_long_to_ext_reg,     /* MXDR      */
 /*28*/         &zz_load_float_long_reg,                /* LDR       */
 /*29*/         &zz_compare_float_long_reg,             /* CDR       */
 /*2A*/         &zz_add_float_long_reg,                 /* ADR       */
 /*2B*/         &zz_subtract_float_long_reg,            /* SDR       */
 /*2C*/         &zz_multiply_float_long_reg,            /* MDR       */
 /*2D*/         &zz_divide_float_long_reg,              /* DDR       */
 /*2E*/         &zz_add_unnormal_float_long_reg,        /* AWR       */
 /*2F*/         &zz_subtract_unnormal_float_long_reg,   /* SWR       */
 /*30*/         &zz_load_positive_float_short_reg,      /* LPER      */
 /*31*/         &zz_load_negative_float_short_reg,      /* LNER      */
 /*32*/         &zz_load_and_test_float_short_reg,      /* LTER      */
 /*33*/         &zz_load_complement_float_short_reg,    /* LCER      */
 /*34*/         &zz_halve_float_short_reg,              /* HER       */
 /*35*/         &zz_round_float_short_reg,              /* LRER      */
 /*36*/         &zz_add_float_ext_reg,                  /* AXR       */
 /*37*/         &zz_subtract_float_ext_reg,             /* SXR       */
 /*38*/         &zz_load_float_short_reg,               /* LER       */
 /*39*/         &zz_compare_float_short_reg,            /* CER       */
 /*3A*/         &zz_add_float_short_reg,                /* AER       */
 /*3B*/         &zz_subtract_float_short_reg,           /* SER       */
 /*3C*/         &zz_multiply_float_short_to_long_reg,   /* MER       */
 /*3D*/         &zz_divide_float_short_reg,             /* DER       */
 /*3E*/         &zz_add_unnormal_float_short_reg,       /* AUR       */
 /*3F*/         &zz_subtract_unnormal_float_short_reg,  /* SUR       */
 /*40*/         &zz_store_halfword,                     /* STH       */
 /*41*/         &zz_load_address,                       /* LA        */
 /*42*/         &zz_store_character,                    /* STC       */
 /*43*/         &zz_insert_character,                   /* IC        */
 /*44*/         &zz_execute,                            /* EX        */
 /*45*/         &zz_branch_and_link,                    /* BAL       */
 /*46*/         &zz_branch_on_count,                    /* BCT       */
 /*47*/         &zz_branch_on_condition,                /* BC        */
 /*48*/         &zz_load_halfword,                      /* LH        */
 /*49*/         &zz_compare_halfword,                   /* CH        */
 /*4A*/         &zz_add_halfword,                       /* AH        */
 /*4B*/         &zz_subtract_halfword,                  /* SH        */
 /*4C*/         &zz_multiply_halfword,                  /* MH        */
 /*4D*/         &zz_branch_and_save,                    /* BAS       */
 /*4E*/         &zz_convert_to_decimal,                 /* CVD       */
 /*4F*/         &zz_convert_to_binary,                  /* CVB       */
 /*50*/         &zz_store,                              /* ST        */
 /*51*/         &zz_load_address_extended,              /* LAE       */
 /*52*/         &operation_exception,            
 /*53*/         &operation_exception,            
 /*54*/         &zz_and,                                /* N         */
 /*55*/         &zz_compare_logical,                    /* CL        */
 /*56*/         &zz_or,                                 /* O         */
 /*57*/         &zz_exclusive_or,                       /* X         */ 
 /*58*/         &zz_load,                               /* L         */
 /*59*/         &zz_compare,                            /* C         */
 /*5A*/         &zz_add,                                /* A         */
 /*5B*/         &zz_subtract,                           /* S         */
 /*5C*/         &zz_multiply,                           /* M         */
 /*5D*/         &zz_divide,                             /* D         */
 /*5E*/         &zz_add_logical,                        /* AL        */
 /*5F*/         &zz_subtract_logical,                   /* SL        */
 /*60*/         &zz_store_float_long,                   /* STD       */
 /*61*/         &operation_exception,            
 /*62*/         &operation_exception,            
 /*63*/         &operation_exception,            
 /*64*/         &operation_exception,            
 /*65*/         &operation_exception,            
 /*66*/         &operation_exception,            
 /*67*/         &zz_multiply_float_long_to_ext,         /* MXD       */
 /*68*/         &zz_load_float_long,                    /* LD        */
 /*69*/         &zz_compare_float_long,                 /* CD        */
 /*6A*/         &zz_add_float_long,                     /* AD        */
 /*6B*/         &zz_subtract_float_long,                /* SD        */
 /*6C*/         &zz_multiply_float_long,                /* MD        */
 /*6D*/         &zz_divide_float_long,                  /* DD        */
 /*6E*/         &zz_add_unnormal_float_long,            /* AW        */
 /*6F*/         &zz_subtract_unnormal_float_long,       /* SW        */
 /*70*/         &zz_store_float_short,                  /* STE       */
 /*71*/         &zz_multiply_single,                    /* MS        */
 /*72*/         &operation_exception,            
 /*73*/         &operation_exception,            
 /*74*/         &operation_exception,            
 /*75*/         &operation_exception,            
 /*76*/         &operation_exception,            
 /*77*/         &operation_exception,            
 /*78*/         &zz_load_float_short,                   /* LE        */
 /*79*/         &zz_compare_float_short,                /* CE        */
 /*7A*/         &zz_add_float_short,                    /* AE        */
 /*7B*/         &zz_subtract_float_short,               /* SE        */
 /*7C*/         &zz_multiply_float_short_to_long,       /* ME        */
 /*7D*/         &zz_divide_float_short,                 /* DE        */
 /*7E*/         &zz_add_unnormal_float_short,           /* AU        */
 /*7F*/         &zz_subtract_unnormal_float_short,      /* SU        */
 /*80*/         &zz_set_system_mask,                    /* SSM       */
 /*81*/         &operation_exception,            
 /*82*/         &zz_load_psw,                           /* LPSW      */
 /*83*/         &zz_diagnose,                           /* Diagnose  */
 /*84*/         &zz_branch_relative_on_index_high,      /* BRXH      */
 /*85*/         &zz_branch_relative_on_index_low_or_equal, /* BRXLE  */
 /*86*/         &zz_branch_on_index_high,               /* BXH       */
 /*87*/         &zz_branch_on_index_low_or_equal,       /* BXLE      */
 /*88*/         &zz_shift_right_single_logical,         /* SRL       */
 /*89*/         &zz_shift_left_single_logical,          /* SLL       */
 /*8A*/         &zz_shift_right_single,                 /* SRA       */
 /*8B*/         &zz_shift_left_single,                  /* SLA       */
 /*8C*/         &zz_shift_right_double_logical,         /* SRDL      */
 /*8D*/         &zz_shift_left_double_logical,          /* SLDL      */
 /*8E*/         &zz_shift_right_double,                 /* SRDA      */
 /*8F*/         &zz_shift_left_double,                  /* SLDA      */
 /*90*/         &zz_store_multiple,                     /* STM       */
 /*91*/         &zz_test_under_mask,                    /* TM        */
 /*92*/         &zz_move_immediate,                     /* MVI       */
 /*93*/         &zz_test_and_set,                       /* TS        */
 /*94*/         &zz_and_immediate,                      /* NI        */
 /*95*/         &zz_compare_logical_immediate,          /* CLI       */
 /*96*/         &zz_or_immediate,                       /* OI        */
 /*97*/         &zz_exclusive_or_immediate,             /* XI        */
 /*98*/         &zz_load_multiple,                      /* LM        */
 /*99*/         &zz_trace,                              /* TRACE     */
 /*9A*/         &zz_load_access_multiple,               /* LAM       */
 /*9B*/         &zz_store_access_multiple,              /* STAM      */
 /*9C*/         &zz_s370_startio,                       /* SIO/SIOF  */
 /*9D*/         &zz_s370_testio,                        /* TIO/CLRIO */
 /*9E*/         &zz_s370_haltio,                        /* HIO/HDV   */
 /*9F*/         &zz_s370_test_channel,                  /* TCH       */
 /*A0*/         &operation_exception,            
 /*A1*/         &operation_exception,            
 /*A2*/         &operation_exception,            
 /*A3*/         &operation_exception,            
 /*A4*/         &execute_a4xx,                          /* Vector    */
 /*A5*/         &execute_a5xx,                          /* Vector    */
 /*A6*/         &execute_a6xx,                          /* Vector    */
 /*A7*/         &execute_a7xx,
 /*A8*/         &zz_move_long_extended,                 /* MVCLE     */
 /*A9*/         &zz_compare_logical_long_extended,      /* CLCLE     */
 /*AA*/         &operation_exception,            
 /*AB*/         &operation_exception,            
 /*AC*/         &zz_store_then_and_system_mask,         /* STNSM     */
 /*AD*/         &zz_store_then_or_system_mask,          /* STOSM     */
 /*AE*/         &zz_signal_procesor,                    /* SIGP      */
 /*AF*/         &zz_monitor_call,                       /* MC        */
 /*B0*/         &operation_exception,            
 /*B1*/         &zz_load_real_address,                  /* LRA       */
 /*B2*/         &execute_b2xx,
 /*B3*/         &operation_exception,            
 /*B4*/         &operation_exception,            
 /*B5*/         &operation_exception,            
 /*B6*/         &zz_store_control,                      /* STCTL     */
 /*B7*/         &zz_load_control,                       /* LCTL      */
 /*B8*/         &operation_exception,            
 /*B9*/         &operation_exception,            
 /*BA*/         &zz_compare_and_swap,                   /* CS        */
 /*BB*/         &zz_compare_double_and_swap,            /* CDS       */
 /*BC*/         &operation_exception,            
 /*BD*/         &zz_compare_logical_characters_under_mask, /* CLM    */
 /*BE*/         &zz_store_characters_under_mask,        /* STCM      */
 /*BF*/         &zz_insert_characters_under_mask,       /* ICM       */
 /*C0*/         &operation_exception,            
 /*C1*/         &operation_exception,            
 /*C2*/         &operation_exception,            
 /*C3*/         &operation_exception,            
 /*C4*/         &operation_exception,            
 /*C5*/         &operation_exception,            
 /*C6*/         &operation_exception,            
 /*C7*/         &operation_exception,            
 /*C8*/         &operation_exception,            
 /*C9*/         &operation_exception,            
 /*CA*/         &operation_exception,            
 /*CB*/         &operation_exception,            
 /*CC*/         &operation_exception,            
 /*CD*/         &operation_exception,            
 /*CE*/         &operation_exception,            
 /*CF*/         &operation_exception,            
 /*D0*/         &operation_exception,            
 /*D1*/         &zz_move_numerics,                      /* MVN       */
 /*D2*/         &zz_move_character,                     /* MVC       */
 /*D3*/         &zz_move_zones,                         /* MVZ       */
 /*D4*/         &zz_and_character,                      /* NC        */
 /*D5*/         &zz_compare_logical_character,          /* CLC       */
 /*D6*/         &zz_or_character,                       /* OC        */
 /*D7*/         &zz_exclusive_or_character,             /* XC        */
 /*D8*/         &operation_exception,            
 /*D9*/         &zz_move_with_key,                      /* MVCK      */
 /*DA*/         &zz_move_to_primary,                    /* MVCP      */
 /*DB*/         &zz_move_to_secondary,                  /* MVCS      */
 /*DC*/         &zz_translate,                          /* TR        */
 /*DD*/         &zz_translate_and_test,                 /* TRT       */
 /*DE*/         &zz_edit_x_edit_and_mark,               /* ED        */
 /*DF*/         &zz_edit_x_edit_and_mark,               /* EDMK      */
 /*E0*/         &operation_exception,            
 /*E1*/         &operation_exception,            
 /*E2*/         &operation_exception,            
 /*E3*/         &operation_exception,            
 /*E4*/         &execute_e4xx,                          /* Vector    */
 /*E5*/         &execute_e5xx,            
 /*E6*/         &operation_exception,            
 /*E7*/         &operation_exception,            
 /*E8*/         &zz_move_inverse,                       /* MVCIN     */
 /*E9*/         &operation_exception,            
 /*EA*/         &operation_exception,            
 /*EB*/         &operation_exception,            
 /*EC*/         &operation_exception,            
 /*ED*/         &operation_exception,            
 /*EE*/         &zz_perform_locked_operation,           /* PLO       */
 /*EF*/         &operation_exception,            
 /*F0*/         &zz_shift_and_round_decimal,            /* SRP       */
 /*F1*/         &zz_move_with_offset,                   /* MVO       */
 /*F2*/         &zz_pack,                               /* PACK      */
 /*F3*/         &zz_unpack,                             /* UNPK      */
 /*F4*/         &operation_exception,            
 /*F5*/         &operation_exception,            
 /*F6*/         &operation_exception,            
 /*F7*/         &operation_exception,            
 /*F8*/         &zz_zero_and_add,                       /* ZAP       */
 /*F9*/         &zz_compare_decimal,                    /* CP        */
 /*FA*/         &zz_add_decimal,                        /* AP        */
 /*FB*/         &zz_subtract_decimal,                   /* SP        */
 /*FC*/         &zz_multiply_decimal,                   /* MP        */
 /*FD*/         &zz_divide_decimal,                     /* DP        */
 /*FE*/         &operation_exception,            
 /*FF*/         &operation_exception };


zz_func opcode_01xx[256] = {
 /*0100*/       &operation_exception,            
 /*0101*/       &zz_program_return,                     /* PR        */
 /*0102*/       &zz_update_tree,                        /* UPT       */
 /*0103*/       &operation_exception,            
 /*0104*/       &operation_exception,            
 /*0105*/       &operation_exception,            
 /*0106*/       &operation_exception,            
 /*0107*/       &zz_set_clock_programmable_field,       /* SCKPF     */
 /*0108*/       &operation_exception,            
 /*0109*/       &operation_exception,            
 /*010A*/       &operation_exception,            
 /*010B*/       &operation_exception,            
 /*010C*/       &operation_exception,            
 /*010D*/       &operation_exception,            
 /*010E*/       &operation_exception,            
 /*010F*/       &operation_exception,            
 /*0110*/       &operation_exception,            
 /*0111*/       &operation_exception,            
 /*0112*/       &operation_exception,            
 /*0113*/       &operation_exception,            
 /*0114*/       &operation_exception,            
 /*0115*/       &operation_exception,            
 /*0116*/       &operation_exception,            
 /*0117*/       &operation_exception,            
 /*0118*/       &operation_exception,            
 /*0119*/       &operation_exception,            
 /*011A*/       &operation_exception,            
 /*011B*/       &operation_exception,            
 /*011C*/       &operation_exception,            
 /*011D*/       &operation_exception,            
 /*011E*/       &operation_exception,            
 /*011F*/       &operation_exception,            
 /*0120*/       &operation_exception,            
 /*0121*/       &operation_exception,            
 /*0122*/       &operation_exception,            
 /*0123*/       &operation_exception,            
 /*0124*/       &operation_exception,            
 /*0125*/       &operation_exception,            
 /*0126*/       &operation_exception,            
 /*0127*/       &operation_exception,            
 /*0128*/       &operation_exception,            
 /*0129*/       &operation_exception,            
 /*012A*/       &operation_exception,            
 /*012B*/       &operation_exception,            
 /*012C*/       &operation_exception,            
 /*012D*/       &operation_exception,            
 /*012E*/       &operation_exception,            
 /*012F*/       &operation_exception,            
 /*0130*/       &operation_exception,            
 /*0131*/       &operation_exception,            
 /*0132*/       &operation_exception,            
 /*0133*/       &operation_exception,            
 /*0134*/       &operation_exception,            
 /*0135*/       &operation_exception,            
 /*0136*/       &operation_exception,            
 /*0137*/       &operation_exception,            
 /*0138*/       &operation_exception,            
 /*0139*/       &operation_exception,            
 /*013A*/       &operation_exception,            
 /*013B*/       &operation_exception,            
 /*013C*/       &operation_exception,            
 /*013D*/       &operation_exception,            
 /*013E*/       &operation_exception,            
 /*013F*/       &operation_exception,            
 /*0140*/       &operation_exception,            
 /*0141*/       &operation_exception,            
 /*0142*/       &operation_exception,            
 /*0143*/       &operation_exception,            
 /*0144*/       &operation_exception,            
 /*0145*/       &operation_exception,            
 /*0146*/       &operation_exception,            
 /*0147*/       &operation_exception,            
 /*0148*/       &operation_exception,            
 /*0149*/       &operation_exception,            
 /*014A*/       &operation_exception,            
 /*014B*/       &operation_exception,            
 /*014C*/       &operation_exception,            
 /*014D*/       &operation_exception,            
 /*014E*/       &operation_exception,            
 /*014F*/       &operation_exception,            
 /*0150*/       &operation_exception,            
 /*0151*/       &operation_exception,            
 /*0152*/       &operation_exception,            
 /*0153*/       &operation_exception,            
 /*0154*/       &operation_exception,            
 /*0155*/       &operation_exception,            
 /*0156*/       &operation_exception,            
 /*0157*/       &operation_exception,            
 /*0158*/       &operation_exception,            
 /*0159*/       &operation_exception,            
 /*015A*/       &operation_exception,            
 /*015B*/       &operation_exception,            
 /*015C*/       &operation_exception,            
 /*015D*/       &operation_exception,            
 /*015E*/       &operation_exception,            
 /*015F*/       &operation_exception,            
 /*0160*/       &operation_exception,            
 /*0161*/       &operation_exception,            
 /*0162*/       &operation_exception,            
 /*0163*/       &operation_exception,            
 /*0164*/       &operation_exception,            
 /*0165*/       &operation_exception,            
 /*0166*/       &operation_exception,            
 /*0167*/       &operation_exception,            
 /*0168*/       &operation_exception,            
 /*0169*/       &operation_exception,            
 /*016A*/       &operation_exception,            
 /*016B*/       &operation_exception,            
 /*016C*/       &operation_exception,            
 /*016D*/       &operation_exception,            
 /*016E*/       &operation_exception,            
 /*016F*/       &operation_exception,            
 /*0170*/       &operation_exception,            
 /*0171*/       &operation_exception,            
 /*0172*/       &operation_exception,            
 /*0173*/       &operation_exception,            
 /*0174*/       &operation_exception,            
 /*0175*/       &operation_exception,            
 /*0176*/       &operation_exception,            
 /*0177*/       &operation_exception,            
 /*0178*/       &operation_exception,            
 /*0179*/       &operation_exception,            
 /*017A*/       &operation_exception,            
 /*017B*/       &operation_exception,            
 /*017C*/       &operation_exception,            
 /*017D*/       &operation_exception,            
 /*017E*/       &operation_exception,            
 /*017F*/       &operation_exception,            
 /*0180*/       &operation_exception,            
 /*0181*/       &operation_exception,            
 /*0182*/       &operation_exception,            
 /*0183*/       &operation_exception,            
 /*0184*/       &operation_exception,            
 /*0185*/       &operation_exception,            
 /*0186*/       &operation_exception,            
 /*0187*/       &operation_exception,            
 /*0188*/       &operation_exception,            
 /*0189*/       &operation_exception,            
 /*018A*/       &operation_exception,            
 /*018B*/       &operation_exception,            
 /*018C*/       &operation_exception,            
 /*018D*/       &operation_exception,            
 /*018E*/       &operation_exception,            
 /*018F*/       &operation_exception,            
 /*0190*/       &operation_exception,            
 /*0191*/       &operation_exception,            
 /*0192*/       &operation_exception,            
 /*0193*/       &operation_exception,            
 /*0194*/       &operation_exception,            
 /*0195*/       &operation_exception,            
 /*0196*/       &operation_exception,            
 /*0197*/       &operation_exception,            
 /*0198*/       &operation_exception,            
 /*0199*/       &operation_exception,            
 /*019A*/       &operation_exception,            
 /*019B*/       &operation_exception,            
 /*019C*/       &operation_exception,            
 /*019D*/       &operation_exception,            
 /*019E*/       &operation_exception,            
 /*019F*/       &operation_exception,            
 /*01A0*/       &operation_exception,            
 /*01A1*/       &operation_exception,            
 /*01A2*/       &operation_exception,            
 /*01A3*/       &operation_exception,            
 /*01A4*/       &operation_exception,            
 /*01A5*/       &operation_exception,            
 /*01A6*/       &operation_exception,            
 /*01A7*/       &operation_exception,            
 /*01A8*/       &operation_exception,            
 /*01A9*/       &operation_exception,            
 /*01AA*/       &operation_exception,            
 /*01AB*/       &operation_exception,            
 /*01AC*/       &operation_exception,            
 /*01AD*/       &operation_exception,            
 /*01AE*/       &operation_exception,            
 /*01AF*/       &operation_exception,            
 /*01B0*/       &operation_exception,            
 /*01B1*/       &operation_exception,            
 /*0101*/       &operation_exception,            
 /*01B3*/       &operation_exception,            
 /*01B4*/       &operation_exception,            
 /*01B5*/       &operation_exception,            
 /*01B6*/       &operation_exception,            
 /*01B7*/       &operation_exception,            
 /*01B8*/       &operation_exception,            
 /*01B9*/       &operation_exception,            
 /*01BA*/       &operation_exception,            
 /*01BB*/       &operation_exception,            
 /*01BC*/       &operation_exception,            
 /*01BD*/       &operation_exception,            
 /*01BE*/       &operation_exception,            
 /*01BF*/       &operation_exception,            
 /*01C0*/       &operation_exception,            
 /*01C1*/       &operation_exception,            
 /*01C2*/       &operation_exception,            
 /*01C3*/       &operation_exception,            
 /*01C4*/       &operation_exception,            
 /*01C5*/       &operation_exception,            
 /*01C6*/       &operation_exception,            
 /*01C7*/       &operation_exception,            
 /*01C8*/       &operation_exception,            
 /*01C9*/       &operation_exception,            
 /*01CA*/       &operation_exception,            
 /*01CB*/       &operation_exception,            
 /*01CC*/       &operation_exception,            
 /*01CD*/       &operation_exception,            
 /*01CE*/       &operation_exception,            
 /*01CF*/       &operation_exception,            
 /*01D0*/       &operation_exception,            
 /*01D1*/       &operation_exception,            
 /*01D2*/       &operation_exception,            
 /*01D3*/       &operation_exception,            
 /*01D4*/       &operation_exception,            
 /*01D5*/       &operation_exception,            
 /*01D6*/       &operation_exception,            
 /*01D7*/       &operation_exception,            
 /*01D8*/       &operation_exception,            
 /*01D9*/       &operation_exception,            
 /*01DA*/       &operation_exception,            
 /*01DB*/       &operation_exception,            
 /*01DC*/       &operation_exception,            
 /*01DD*/       &operation_exception,            
 /*01DE*/       &operation_exception,            
 /*01DF*/       &operation_exception,            
 /*01E0*/       &operation_exception,            
 /*01E1*/       &operation_exception,            
 /*01E2*/       &operation_exception,            
 /*01E3*/       &operation_exception,            
 /*01E4*/       &operation_exception,            
 /*01E5*/       &operation_exception,            
 /*01E6*/       &operation_exception,            
 /*01E7*/       &operation_exception,            
 /*01E8*/       &operation_exception,            
 /*01E9*/       &operation_exception,            
 /*01EA*/       &operation_exception,            
 /*01EB*/       &operation_exception,            
 /*01EC*/       &operation_exception,            
 /*01ED*/       &operation_exception,            
 /*01EE*/       &operation_exception,            
 /*01EF*/       &operation_exception,            
 /*01F0*/       &operation_exception,            
 /*01F1*/       &operation_exception,            
 /*01F2*/       &operation_exception,            
 /*01F3*/       &operation_exception,            
 /*01F4*/       &operation_exception,            
 /*01F5*/       &operation_exception,            
 /*01F6*/       &operation_exception,            
 /*01F7*/       &operation_exception,            
 /*01F8*/       &operation_exception,            
 /*01F9*/       &operation_exception,            
 /*01FA*/       &operation_exception,            
 /*01FB*/       &operation_exception,            
 /*01FC*/       &operation_exception,            
 /*01FD*/       &operation_exception,            
 /*01FE*/       &operation_exception,            
 /*01FF*/       &operation_exception };


zz_func opcode_a7xx[16] = {
 /*A7x0*/       &zz_test_under_mask_high,               /* TMH       */
 /*A7x1*/       &zz_test_under_mask_low,                /* TML       */
 /*A7x2*/       &operation_exception,   
 /*A7x3*/       &operation_exception,  
 /*A7x4*/       &zz_branch_relative_on_condition,       /* BRC       */
 /*A7x5*/       &zz_branch_relative_and_save,           /* BRAS      */
 /*A7x6*/       &zz_branch_relative_on_count,           /* BRC       */
 /*A7x7*/       &operation_exception, 
 /*A7x8*/       &zz_load_halfword_immediate,            /* LHI       */
 /*A7x9*/       &operation_exception, 
 /*A7xA*/       &zz_add_halfword_immediate,             /* AHI       */
 /*A7xB*/       &operation_exception, 
 /*A7xC*/       &zz_multiply_halfword_immediate,        /* MHI       */
 /*A7xD*/       &operation_exception, 
 /*A7xE*/       &zz_compare_halfword_immediate,         /* CHI       */
 /*A7xF*/       &operation_exception };


zz_func opcode_b2xx[256] = {
 /*B200*/       &operation_exception,            
 /*B201*/       &operation_exception,            
 /*B202*/       &zz_store_cpu_id,                       /* STIDP     */
 /*B203*/       &zz_s370_store_channelid,               /* STIDC     */
 /*B204*/       &zz_set_clock,                          /* SCK       */
 /*B205*/       &zz_store_clock,                        /* STCK      */
 /*B206*/       &zz_set_clock_comparator,               /* SCKC      */
 /*B207*/       &zz_store_clock_comparator,             /* STCKC     */
 /*B208*/       &zz_set_cpu_timer,                      /* SPT       */
 /*B209*/       &zz_store_cpu_timer,                    /* STPT      */
 /*B20A*/       &zz_set_psw_key_from_address,           /* SPKA      */
 /*B20B*/       &zz_insert_psw_key,                     /* IPK       */
 /*B20C*/       &operation_exception,            
 /*B20D*/       &zz_purge_tlb,                          /* PTLB      */
 /*B20E*/       &operation_exception,            
 /*B20F*/       &operation_exception,            
 /*B210*/       &zz_set_prefix,                         /* SPX       */
 /*B211*/       &zz_store_prefix,                       /* STPX      */
 /*B212*/       &zz_store_cpu_address,                  /* STAP      */
 /*B213*/       &zz_reset_reference_bit,                /* RRB       */
 /*B214*/       &operation_exception,            
 /*B215*/       &operation_exception,            
 /*B216*/       &operation_exception,            
 /*B217*/       &operation_exception,            
 /*B218*/       &zz_program_call,                       /* PC        */
 /*B219*/       &zz_set_address_space_control_x,        /* SAC       */
 /*B21A*/       &zz_compare_and_form_codeword,          /* CFC       */
 /*B21B*/       &operation_exception,            
 /*B21C*/       &operation_exception,            
 /*B21D*/       &operation_exception,            
 /*B21E*/       &operation_exception,            
 /*B21F*/       &operation_exception,            
 /*B220*/       &zz_service_call,                       /* SERVC     */
 /*B221*/       &zz_invalidate_page_table_entry,        /* IPTE      */
 /*B222*/       &zz_insert_program_mask,                /* IPM       */
 /*B223*/       &zz_insert_virtual_storage_key,         /* IVSK      */
 /*B224*/       &zz_insert_address_space_control,       /* IAC       */
 /*B225*/       &zz_set_secondary_asn,                  /* SSAR      */
 /*B226*/       &zz_extract_primary_asn,                /* EPAR      */
 /*B227*/       &zz_extract_secondary_asn,              /* ESAR      */
 /*B228*/       &zz_program_transfer,                   /* PT        */
 /*B229*/       &zz_insert_storage_key_extended,        /* ISKE      */
 /*B22A*/       &zz_reset_reference_bit_extended,       /* RRBE      */
 /*B22B*/       &zz_set_storage_key_extended,           /* SSKE      */
 /*B22C*/       &zz_test_block,                         /* TB        */
 /*B22D*/       &zz_divide_float_ext_reg,               /* DXR       */
 /*B22E*/       &zz_page_in,                            /* PGIN      */
 /*B22F*/       &zz_page_out,                           /* PGOUT     */
 /*B230*/       &zz_clear_subchannel,                   /* CSCH      */
 /*B231*/       &zz_halt_subchannel,                    /* HSCH      */
 /*B232*/       &zz_modify_subchannel,                  /* MSCH      */
 /*B233*/       &zz_start_subchannel,                   /* SSCH      */
 /*B234*/       &zz_store_subchannel,                   /* STSCH     */
 /*B235*/       &zz_test_subchannel,                    /* TSCH      */
 /*B236*/       &zz_test_pending_interruption,          /* TPI       */
 /*B237*/       &operation_exception,
 /*B238*/       &zz_resume_subchannel,                  /* RSCH      */
 /*B239*/       &zz_store_channel_report_word,          /* STCRW     */
 /*B23A*/       &zz_store_channel_path_status,          /* STCPS     */
 /*B23B*/       &operation_exception,
 /*B23C*/       &zz_set_channel_monitor,                /* SCHM      */
 /*B23D*/       &operation_exception,
 /*B23E*/       &operation_exception,
 /*B23F*/       &operation_exception,
 /*B240*/       &zz_branch_and_stack,                   /* BAKR      */
 /*B241*/       &zz_checksum,                           /* CKSM      */
 /*B242*/       &operation_exception,            
 /*B243*/       &operation_exception,            
 /*B244*/       &operation_exception,            
 /*B245*/       &operation_exception,            
 /*B246*/       &zz_store_using_real_address,           /* STURA     */
 /*B247*/       &zz_modify_stacked_state,               /* MSTA      */
 /*B248*/       &zz_purge_alb,                          /* PALB      */
 /*B249*/       &zz_extract_stacked_registers,          /* EREG      */
 /*B24A*/       &zz_extract_stacked_state,              /* ESTA      */
 /*B24B*/       &zz_load_using_real_address,            /* LURA      */
 /*B24C*/       &zz_test_access,                        /* TAR       */
 /*B24D*/       &zz_copy_access,                        /* CPYA      */
 /*B24E*/       &zz_set_access_register,                /* SAR       */
 /*B24F*/       &zz_extract_access_register,            /* EAR       */
 /*B250*/       &zz_compare_and_swap_and_purge,         /* CSP       */
 /*B251*/       &operation_exception,            
 /*B252*/       &zz_multiply_single_register,           /* MSR       */
 /*B253*/       &operation_exception,            
 /*B254*/       &zz_move_page,                          /* MVPG      */
 /*B255*/       &zz_move_string,                        /* MVST      */
 /*B256*/       &operation_exception,            
 /*B257*/       &zz_compare_until_substring_equal,      /* CUSE      */
 /*B258*/       &zz_branch_in_subspace_group,           /* BSG       */
 /*B259*/       &zz_invalidate_expanded_storage_block_entry, /* IESBE*/
 /*B25A*/       &zz_branch_and_set_authority,           /* BSA       */
 /*B25B*/       &operation_exception,            
 /*B25C*/       &operation_exception,            
 /*B25D*/       &zz_compare_logical_string,             /* CLST      */
 /*B25E*/       &zz_search_string,                      /* SRST      */
 /*B25F*/       &operation_exception,            
 /*B260*/       &operation_exception,            
 /*B261*/       &operation_exception,            
 /*B262*/       &operation_exception,            
 /*B263*/       &zz_compression_call,			/* CMPSC     */
 /*B264*/       &operation_exception,            
 /*B265*/       &operation_exception,            
 /*B266*/       &operation_exception,            
 /*B267*/       &operation_exception,            
 /*B268*/       &operation_exception,            
 /*B269*/       &operation_exception,            
 /*B26A*/       &operation_exception,            
 /*B26B*/       &operation_exception,            
 /*B26C*/       &operation_exception,            
 /*B26D*/       &operation_exception,            
 /*B26E*/       &operation_exception,            
 /*B26F*/       &operation_exception,            
 /*B270*/       &operation_exception,            
 /*B271*/       &operation_exception,            
 /*B272*/       &operation_exception,            
 /*B273*/       &operation_exception,            
 /*B274*/       &operation_exception,            
 /*B275*/       &operation_exception,            
 /*B276*/       &operation_exception,            
 /*B277*/       &operation_exception,            
 /*B278*/       &zz_store_clock_extended,               /* STCKE     */
 /*B279*/       &zz_set_address_space_control_x,        /* SACF      */
 /*B27A*/       &operation_exception,            
 /*B27B*/       &operation_exception,            
 /*B27C*/       &operation_exception,            
 /*B27D*/       &operation_exception,            
 /*B27E*/       &operation_exception,            
 /*B27F*/       &operation_exception,            
 /*B280*/       &operation_exception,            
 /*B281*/       &operation_exception,            
 /*B282*/       &operation_exception,            
 /*B283*/       &operation_exception,            
 /*B284*/       &operation_exception,            
 /*B285*/       &operation_exception,            
 /*B286*/       &operation_exception,            
 /*B287*/       &operation_exception,            
 /*B288*/       &operation_exception,            
 /*B289*/       &operation_exception,            
 /*B28A*/       &operation_exception,            
 /*B28B*/       &operation_exception,            
 /*B28C*/       &operation_exception,            
 /*B28D*/       &operation_exception,            
 /*B28E*/       &operation_exception,            
 /*B28F*/       &operation_exception,            
 /*B290*/       &operation_exception,            
 /*B291*/       &operation_exception,            
 /*B292*/       &operation_exception,            
 /*B293*/       &operation_exception,            
 /*B294*/       &operation_exception,            
 /*B295*/       &operation_exception,            
 /*B296*/       &operation_exception,            
 /*B297*/       &operation_exception,            
 /*B298*/       &operation_exception,            
 /*B299*/       &operation_exception,            
 /*B29A*/       &operation_exception,            
 /*B29B*/       &operation_exception,            
 /*B29C*/       &operation_exception,            
 /*B29D*/       &operation_exception,            
 /*B29E*/       &operation_exception,            
 /*B29F*/       &operation_exception,            
 /*B2A0*/       &operation_exception,            
 /*B2A1*/       &operation_exception,            
 /*B2A2*/       &operation_exception,            
 /*B2A3*/       &operation_exception,            
 /*B2A4*/       &operation_exception,            
 /*B2A5*/       &operation_exception,            
 /*B2A6*/       &operation_exception,            
 /*B2A7*/       &operation_exception,            
 /*B2A8*/       &operation_exception,            
 /*B2A9*/       &operation_exception,            
 /*B2AA*/       &operation_exception,            
 /*B2AB*/       &operation_exception,            
 /*B2AC*/       &operation_exception,            
 /*B2AD*/       &operation_exception,            
 /*B2AE*/       &operation_exception,            
 /*B2AF*/       &operation_exception,            
 /*B2B0*/       &operation_exception,            
 /*B2B1*/       &operation_exception,            
 /*B2B2*/       &operation_exception,            
 /*B2B3*/       &operation_exception,            
 /*B2B4*/       &operation_exception,            
 /*B2B5*/       &operation_exception,            
 /*B2B6*/       &operation_exception,            
 /*B2B7*/       &operation_exception,            
 /*B2B8*/       &operation_exception,            
 /*B2B9*/       &operation_exception,            
 /*B2BA*/       &operation_exception,            
 /*B2BB*/       &operation_exception,            
 /*B2BC*/       &operation_exception,            
 /*B2BD*/       &operation_exception,            
 /*B2BE*/       &operation_exception,            
 /*B2BF*/       &operation_exception,            
 /*B2C0*/       &operation_exception,            
 /*B2C1*/       &operation_exception,            
 /*B2C2*/       &operation_exception,            
 /*B2C3*/       &operation_exception,            
 /*B2C4*/       &operation_exception,            
 /*B2C5*/       &operation_exception,            
 /*B2C6*/       &operation_exception,            
 /*B2C7*/       &operation_exception,            
 /*B2C8*/       &operation_exception,            
 /*B2C9*/       &operation_exception,            
 /*B2CA*/       &operation_exception,            
 /*B2CB*/       &operation_exception,            
 /*B2CC*/       &operation_exception,            
 /*B2CD*/       &operation_exception,            
 /*B2CE*/       &operation_exception,            
 /*B2CF*/       &operation_exception,            
 /*B2D0*/       &operation_exception,            
 /*B2D1*/       &operation_exception,            
 /*B2D2*/       &operation_exception,            
 /*B2D3*/       &operation_exception,            
 /*B2D4*/       &operation_exception,            
 /*B2D5*/       &operation_exception,            
 /*B2D6*/       &operation_exception,            
 /*B2D7*/       &operation_exception,            
 /*B2D8*/       &operation_exception,            
 /*B2D9*/       &operation_exception,            
 /*B2DA*/       &operation_exception,            
 /*B2DB*/       &operation_exception,            
 /*B2DC*/       &operation_exception,            
 /*B2DD*/       &operation_exception,            
 /*B2DE*/       &operation_exception,            
 /*B2DF*/       &operation_exception,            
 /*B2E0*/       &operation_exception,            
 /*B2E1*/       &operation_exception,            
 /*B2E2*/       &operation_exception,            
 /*B2E3*/       &operation_exception,            
 /*B2E4*/       &operation_exception,            
 /*B2E5*/       &operation_exception,            
 /*B2E6*/       &operation_exception,            
 /*B2E7*/       &operation_exception,            
 /*B2E8*/       &operation_exception,            
 /*B2E9*/       &operation_exception,            
 /*B2EA*/       &operation_exception,            
 /*B2EB*/       &operation_exception,            
 /*B2EC*/       &operation_exception,            
 /*B2ED*/       &operation_exception,            
 /*B2EE*/       &operation_exception,            
 /*B2EF*/       &operation_exception,            
 /*B2F0*/       &zz_inter_user_communication_vehicle,   /* IUCV      */
 /*B2F1*/       &operation_exception,            
 /*B2F2*/       &operation_exception,            
 /*B2F3*/       &operation_exception,            
 /*B2F4*/       &operation_exception,            
 /*B2F5*/       &operation_exception,            
 /*B2F6*/       &operation_exception,            
 /*B2F7*/       &operation_exception,            
 /*B2F8*/       &operation_exception,            
 /*B2F9*/       &operation_exception,            
 /*B2FA*/       &operation_exception,            
 /*B2FB*/       &operation_exception,            
 /*B2FC*/       &operation_exception,            
 /*B2FD*/       &operation_exception,            
 /*B2FE*/       &operation_exception,            
 /*B2FF*/       &operation_exception };


zz_func opcode_e5xx[256] = {
 /*E500*/       &zz_load_address_space_parameters,      /* LASP      */
 /*E501*/       &zz_test_protection,                    /* TPROT     */
 /*E502*/       &operation_exception,            
 /*E503*/       &operation_exception,
 /*E504*/       &zz_obtain_local_lock,                  /* Assist    */
 /*E505*/       &zz_release_local_lock,                 /* Assist    */
 /*E506*/       &zz_obtain_cms_lock,                    /* Assist    */
 /*E507*/       &zz_release_cms_lock,                   /* Assist    */
 /*E508*/       &operation_exception,            
 /*E509*/       &operation_exception,            
 /*E50A*/       &operation_exception,            
 /*E50B*/       &operation_exception,            
 /*E50C*/       &operation_exception,            
 /*E50D*/       &operation_exception,            
 /*E50E*/       &zz_move_with_source_key,               /* MVCSK     */
 /*E50F*/       &zz_move_with_destination_key,          /* MVCDK     */    
 /*E510*/       &operation_exception,            
 /*E511*/       &operation_exception,            
 /*E512*/       &operation_exception,            
 /*E513*/       &operation_exception,            
 /*E514*/       &operation_exception,            
 /*E515*/       &operation_exception,            
 /*E516*/       &operation_exception,            
 /*E517*/       &operation_exception,            
 /*E518*/       &operation_exception,            
 /*E519*/       &operation_exception,            
 /*E51A*/       &operation_exception,  
 /*E51B*/       &operation_exception,            
 /*E51C*/       &operation_exception,            
 /*E51D*/       &operation_exception,            
 /*E51E*/       &operation_exception,            
 /*E51F*/       &operation_exception,            
 /*E520*/       &operation_exception,            
 /*E521*/       &operation_exception,            
 /*E522*/       &operation_exception,       
 /*E523*/       &operation_exception,            
 /*E524*/       &operation_exception,            
 /*E525*/       &operation_exception,            
 /*E526*/       &operation_exception,            
 /*E527*/       &operation_exception,            
 /*E528*/       &operation_exception,            
 /*E529*/       &operation_exception,            
 /*E52A*/       &operation_exception,            
 /*E52B*/       &operation_exception,            
 /*E52C*/       &operation_exception,            
 /*E52D*/       &operation_exception,            
 /*E52E*/       &operation_exception,            
 /*E52F*/       &operation_exception,            
 /*E530*/       &operation_exception,            
 /*E531*/       &operation_exception,            
 /*E532*/       &operation_exception,         
 /*E533*/       &operation_exception,         
 /*E534*/       &operation_exception,        
 /*E535*/       &operation_exception,                
 /*E536*/       &operation_exception,     
 /*E537*/       &operation_exception,
 /*E538*/       &operation_exception,            
 /*E539*/       &operation_exception,   
 /*E53A*/       &operation_exception,  
 /*E53B*/       &operation_exception,
 /*E53C*/       &operation_exception,       
 /*E53D*/       &operation_exception,
 /*E53E*/       &operation_exception,
 /*E53F*/       &operation_exception,
 /*E540*/       &operation_exception,            
 /*E541*/       &operation_exception,            
 /*E542*/       &operation_exception,            
 /*E543*/       &operation_exception,            
 /*E544*/       &operation_exception,            
 /*E545*/       &operation_exception,            
 /*E546*/       &operation_exception,            
 /*E547*/       &operation_exception,            
 /*E548*/       &operation_exception,            
 /*E549*/       &operation_exception,            
 /*E54A*/       &operation_exception,            
 /*E54B*/       &operation_exception,            
 /*E54C*/       &operation_exception,            
 /*E54D*/       &operation_exception,                 
 /*E54E*/       &operation_exception,            
 /*E54F*/       &operation_exception,    
 /*E550*/       &operation_exception,            
 /*E551*/       &operation_exception,            
 /*E552*/       &operation_exception,  
 /*E553*/       &operation_exception,            
 /*E554*/       &operation_exception,            
 /*E555*/       &operation_exception,              
 /*E556*/       &operation_exception,            
 /*E557*/       &operation_exception, 
 /*E558*/       &operation_exception,            
 /*E559*/       &operation_exception,            
 /*E55A*/       &operation_exception,            
 /*E55B*/       &operation_exception,            
 /*E55C*/       &operation_exception,            
 /*E55D*/       &operation_exception,       
 /*E55E*/       &operation_exception,            
 /*E55F*/       &operation_exception,            
 /*E560*/       &operation_exception,            
 /*E561*/       &operation_exception,            
 /*E562*/       &operation_exception,            
 /*E563*/       &operation_exception,            
 /*E564*/       &operation_exception,            
 /*E565*/       &operation_exception,            
 /*E566*/       &operation_exception,            
 /*E567*/       &operation_exception,            
 /*E568*/       &operation_exception,            
 /*E569*/       &operation_exception,            
 /*E56A*/       &operation_exception,            
 /*E56B*/       &operation_exception,            
 /*E56C*/       &operation_exception,            
 /*E56D*/       &operation_exception,            
 /*E56E*/       &operation_exception,            
 /*E56F*/       &operation_exception,            
 /*E570*/       &operation_exception,            
 /*E571*/       &operation_exception,            
 /*E572*/       &operation_exception,            
 /*E573*/       &operation_exception,            
 /*E574*/       &operation_exception,            
 /*E575*/       &operation_exception,            
 /*E576*/       &operation_exception,            
 /*E577*/       &operation_exception,            
 /*E578*/       &operation_exception,            
 /*E579*/       &operation_exception,            
 /*E57A*/       &operation_exception,            
 /*E57B*/       &operation_exception,            
 /*E57C*/       &operation_exception,            
 /*E57D*/       &operation_exception,            
 /*E57E*/       &operation_exception,            
 /*E57F*/       &operation_exception,            
 /*E580*/       &operation_exception,            
 /*E581*/       &operation_exception,            
 /*E582*/       &operation_exception,            
 /*E583*/       &operation_exception,            
 /*E584*/       &operation_exception,            
 /*E585*/       &operation_exception,            
 /*E586*/       &operation_exception,            
 /*E587*/       &operation_exception,            
 /*E588*/       &operation_exception,            
 /*E589*/       &operation_exception,            
 /*E58A*/       &operation_exception,            
 /*E58B*/       &operation_exception,            
 /*E58C*/       &operation_exception,            
 /*E58D*/       &operation_exception,            
 /*E58E*/       &operation_exception,            
 /*E58F*/       &operation_exception,            
 /*E590*/       &operation_exception,            
 /*E591*/       &operation_exception,            
 /*E592*/       &operation_exception,            
 /*E593*/       &operation_exception,            
 /*E594*/       &operation_exception,            
 /*E595*/       &operation_exception,            
 /*E596*/       &operation_exception,            
 /*E597*/       &operation_exception,            
 /*E598*/       &operation_exception,            
 /*E599*/       &operation_exception,            
 /*E59A*/       &operation_exception,            
 /*E59B*/       &operation_exception,            
 /*E59C*/       &operation_exception,            
 /*E59D*/       &operation_exception,            
 /*E59E*/       &operation_exception,            
 /*E59F*/       &operation_exception,            
 /*E5A0*/       &operation_exception,            
 /*E5A1*/       &operation_exception,            
 /*E5A2*/       &operation_exception,            
 /*E5A3*/       &operation_exception,            
 /*E5A4*/       &operation_exception,            
 /*E5A5*/       &operation_exception,            
 /*E5A6*/       &operation_exception,            
 /*E5A7*/       &operation_exception,            
 /*E5A8*/       &operation_exception,            
 /*E5A9*/       &operation_exception,            
 /*E5AA*/       &operation_exception,            
 /*E5AB*/       &operation_exception,            
 /*E5AC*/       &operation_exception,            
 /*E5AD*/       &operation_exception,            
 /*E5AE*/       &operation_exception,            
 /*E5AF*/       &operation_exception,            
 /*E5B0*/       &operation_exception,            
 /*E5B1*/       &operation_exception,            
 /*E5E5*/       &operation_exception,            
 /*E5B3*/       &operation_exception,            
 /*E5B4*/       &operation_exception,            
 /*E5B5*/       &operation_exception,            
 /*E5B6*/       &operation_exception,            
 /*E5B7*/       &operation_exception,            
 /*E5B8*/       &operation_exception,            
 /*E5B9*/       &operation_exception,            
 /*E5BA*/       &operation_exception,            
 /*E5BB*/       &operation_exception,            
 /*E5BC*/       &operation_exception,            
 /*E5BD*/       &operation_exception,            
 /*E5BE*/       &operation_exception,            
 /*E5BF*/       &operation_exception,            
 /*E5C0*/       &operation_exception,            
 /*E5C1*/       &operation_exception,            
 /*E5C2*/       &operation_exception,            
 /*E5C3*/       &operation_exception,            
 /*E5C4*/       &operation_exception,            
 /*E5C5*/       &operation_exception,            
 /*E5C6*/       &operation_exception,            
 /*E5C7*/       &operation_exception,            
 /*E5C8*/       &operation_exception,            
 /*E5C9*/       &operation_exception,            
 /*E5CA*/       &operation_exception,            
 /*E5CB*/       &operation_exception,            
 /*E5CC*/       &operation_exception,            
 /*E5CD*/       &operation_exception,            
 /*E5CE*/       &operation_exception,            
 /*E5CF*/       &operation_exception,            
 /*E5D0*/       &operation_exception,            
 /*E5D1*/       &operation_exception,            
 /*E5D2*/       &operation_exception,            
 /*E5D3*/       &operation_exception,            
 /*E5D4*/       &operation_exception,            
 /*E5D5*/       &operation_exception,            
 /*E5D6*/       &operation_exception,            
 /*E5D7*/       &operation_exception,            
 /*E5D8*/       &operation_exception,            
 /*E5D9*/       &operation_exception,            
 /*E5DA*/       &operation_exception,            
 /*E5DB*/       &operation_exception,            
 /*E5DC*/       &operation_exception,            
 /*E5DD*/       &operation_exception,            
 /*E5DE*/       &operation_exception,            
 /*E5DF*/       &operation_exception,            
 /*E5E0*/       &operation_exception,            
 /*E5E1*/       &operation_exception,            
 /*E5E2*/       &operation_exception,            
 /*E5E3*/       &operation_exception,            
 /*E5E4*/       &operation_exception,            
 /*E5E5*/       &operation_exception,            
 /*E5E6*/       &operation_exception,            
 /*E5E7*/       &operation_exception,            
 /*E5E8*/       &operation_exception,            
 /*E5E9*/       &operation_exception,            
 /*E5EA*/       &operation_exception,            
 /*E5EB*/       &operation_exception,            
 /*E5EC*/       &operation_exception,            
 /*E5ED*/       &operation_exception,            
 /*E5EE*/       &operation_exception,            
 /*E5EF*/       &operation_exception,            
 /*E5F0*/       &operation_exception,            
 /*E5F1*/       &operation_exception,            
 /*E5F2*/       &operation_exception,            
 /*E5F3*/       &operation_exception,            
 /*E5F4*/       &operation_exception,            
 /*E5F5*/       &operation_exception,            
 /*E5F6*/       &operation_exception,            
 /*E5F7*/       &operation_exception,            
 /*E5F8*/       &operation_exception,            
 /*E5F9*/       &operation_exception,            
 /*E5FA*/       &operation_exception,            
 /*E5FB*/       &operation_exception,            
 /*E5FC*/       &operation_exception,            
 /*E5FD*/       &operation_exception,            
 /*E5FE*/       &operation_exception,            
 /*E5FF*/       &operation_exception };


#if defined(FEATURE_VECTOR_FACILITY)

zz_func opcode_a4xx[256] = {
 /*A400*/       &operation_exception,            
 /*A401*/       &operation_exception,                 
 /*A402*/       &operation_exception,                   
 /*A403*/       &operation_exception,            
 /*A404*/       &operation_exception,            
 /*A405*/       &operation_exception,            
 /*A406*/       &operation_exception,            
 /*A407*/       &operation_exception,  
 /*A408*/       &operation_exception,            
 /*A409*/       &operation_exception,            
 /*A40A*/       &operation_exception,            
 /*A40B*/       &operation_exception,            
 /*A40C*/       &operation_exception,            
 /*A40D*/       &operation_exception,            
 /*A40E*/       &operation_exception,            
 /*A40F*/       &operation_exception,            
 /*A410*/       &operation_exception,            
 /*A411*/       &operation_exception,            
 /*A412*/       &operation_exception,            
 /*A413*/       &operation_exception,            
 /*A414*/       &operation_exception,            
 /*A415*/       &operation_exception,            
 /*A416*/       &operation_exception,            
 /*A417*/       &operation_exception,            
 /*A418*/       &operation_exception,            
 /*A419*/       &operation_exception,            
 /*A41A*/       &operation_exception,            
 /*A41B*/       &operation_exception,            
 /*A41C*/       &operation_exception,            
 /*A41D*/       &operation_exception,            
 /*A41E*/       &operation_exception,            
 /*A41F*/       &operation_exception,            
 /*A420*/       &operation_exception,            
 /*A421*/       &operation_exception,            
 /*A422*/       &operation_exception,            
 /*A423*/       &operation_exception,            
 /*A424*/       &operation_exception,            
 /*A425*/       &operation_exception,            
 /*A426*/       &operation_exception,            
 /*A427*/       &operation_exception,            
 /*A428*/       &operation_exception,            
 /*A429*/       &operation_exception,            
 /*A42A*/       &operation_exception,            
 /*A42B*/       &operation_exception,            
 /*A42C*/       &operation_exception,            
 /*A42D*/       &operation_exception,            
 /*A42E*/       &operation_exception,            
 /*A42F*/       &operation_exception,            
 /*A430*/       &operation_exception,            
 /*A431*/       &operation_exception,            
 /*A432*/       &operation_exception,            
 /*A433*/       &operation_exception,            
 /*A434*/       &operation_exception,            
 /*A435*/       &operation_exception,            
 /*A436*/       &operation_exception,            
 /*A437*/       &operation_exception,            
 /*A438*/       &operation_exception,            
 /*A439*/       &operation_exception,            
 /*A43A*/       &operation_exception,            
 /*A43B*/       &operation_exception,            
 /*A43C*/       &operation_exception,            
 /*A43D*/       &operation_exception,            
 /*A43E*/       &operation_exception,            
 /*A43F*/       &operation_exception,            
 /*A440*/       &operation_exception,            
 /*A441*/       &operation_exception,            
 /*A442*/       &operation_exception,            
 /*A443*/       &operation_exception,            
 /*A444*/       &operation_exception,            
 /*A445*/       &operation_exception,            
 /*A446*/       &operation_exception,            
 /*A447*/       &operation_exception,            
 /*A448*/       &operation_exception,            
 /*A449*/       &operation_exception,            
 /*A44A*/       &operation_exception,            
 /*A44B*/       &operation_exception,            
 /*A44C*/       &operation_exception,            
 /*A44D*/       &operation_exception,            
 /*A44E*/       &operation_exception,            
 /*A44F*/       &operation_exception,            
 /*A450*/       &operation_exception,            
 /*A451*/       &operation_exception,            
 /*A452*/       &operation_exception,            
 /*A453*/       &operation_exception,            
 /*A454*/       &operation_exception,            
 /*A455*/       &operation_exception,            
 /*A456*/       &operation_exception,            
 /*A457*/       &operation_exception,            
 /*A458*/       &operation_exception,            
 /*A459*/       &operation_exception,            
 /*A45A*/       &operation_exception,            
 /*A45B*/       &operation_exception,            
 /*A45C*/       &operation_exception,            
 /*A45D*/       &operation_exception,            
 /*A45E*/       &operation_exception,            
 /*A45F*/       &operation_exception,            
 /*A460*/       &operation_exception,            
 /*A461*/       &operation_exception,            
 /*A462*/       &operation_exception,            
 /*A463*/       &operation_exception,            
 /*A464*/       &operation_exception,            
 /*A465*/       &operation_exception,            
 /*A466*/       &operation_exception,            
 /*A467*/       &operation_exception,            
 /*A468*/       &operation_exception,            
 /*A469*/       &operation_exception,            
 /*A46A*/       &operation_exception,            
 /*A46B*/       &operation_exception,            
 /*A46C*/       &operation_exception,            
 /*A46D*/       &operation_exception,            
 /*A46E*/       &operation_exception,            
 /*A46F*/       &operation_exception,            
 /*A470*/       &operation_exception,            
 /*A471*/       &operation_exception,            
 /*A472*/       &operation_exception,            
 /*A473*/       &operation_exception,            
 /*A474*/       &operation_exception,            
 /*A475*/       &operation_exception,            
 /*A476*/       &operation_exception,            
 /*A477*/       &operation_exception,            
 /*A478*/       &operation_exception,            
 /*A479*/       &operation_exception,            
 /*A47A*/       &operation_exception,            
 /*A47B*/       &operation_exception,            
 /*A47C*/       &operation_exception,            
 /*A47D*/       &operation_exception,            
 /*A47E*/       &operation_exception,            
 /*A47F*/       &operation_exception,            
 /*A480*/       &operation_exception,            
 /*A481*/       &operation_exception,            
 /*A482*/       &operation_exception,            
 /*A483*/       &operation_exception,            
 /*A484*/       &operation_exception,            
 /*A485*/       &operation_exception,            
 /*A486*/       &operation_exception,            
 /*A487*/       &operation_exception,            
 /*A488*/       &operation_exception,            
 /*A489*/       &operation_exception,            
 /*A48A*/       &operation_exception,            
 /*A48B*/       &operation_exception,            
 /*A48C*/       &operation_exception,            
 /*A48D*/       &operation_exception,            
 /*A48E*/       &operation_exception,            
 /*A48F*/       &operation_exception,            
 /*A490*/       &operation_exception,            
 /*A491*/       &operation_exception,            
 /*A492*/       &operation_exception,            
 /*A493*/       &operation_exception,            
 /*A494*/       &operation_exception,            
 /*A495*/       &operation_exception,            
 /*A496*/       &operation_exception,            
 /*A497*/       &operation_exception,            
 /*A498*/       &operation_exception,            
 /*A499*/       &operation_exception,            
 /*A49A*/       &operation_exception,            
 /*A49B*/       &operation_exception,            
 /*A49C*/       &operation_exception,            
 /*A49D*/       &operation_exception,            
 /*A49E*/       &operation_exception,            
 /*A49F*/       &operation_exception,            
 /*A4A0*/       &operation_exception,            
 /*A4A1*/       &operation_exception,            
 /*A4A2*/       &operation_exception,            
 /*A4A3*/       &operation_exception,            
 /*A4A4*/       &operation_exception,            
 /*A4A5*/       &operation_exception,            
 /*A4A6*/       &operation_exception,            
 /*A4A7*/       &operation_exception,            
 /*A4A8*/       &operation_exception,            
 /*A4A9*/       &operation_exception,            
 /*A4AA*/       &operation_exception,            
 /*A4AB*/       &operation_exception,            
 /*A4AC*/       &operation_exception,            
 /*A4AD*/       &operation_exception,            
 /*A4AE*/       &operation_exception,            
 /*A4AF*/       &operation_exception,            
 /*A4B0*/       &operation_exception,            
 /*A4B1*/       &operation_exception,            
 /*A401*/       &operation_exception,            
 /*A4B3*/       &operation_exception,            
 /*A4B4*/       &operation_exception,            
 /*A4B5*/       &operation_exception,            
 /*A4B6*/       &operation_exception,            
 /*A4B7*/       &operation_exception,            
 /*A4B8*/       &operation_exception,            
 /*A4B9*/       &operation_exception,            
 /*A4BA*/       &operation_exception,            
 /*A4BB*/       &operation_exception,            
 /*A4BC*/       &operation_exception,            
 /*A4BD*/       &operation_exception,            
 /*A4BE*/       &operation_exception,            
 /*A4BF*/       &operation_exception,            
 /*A4C0*/       &operation_exception,            
 /*A4C1*/       &operation_exception,            
 /*A4C2*/       &operation_exception,            
 /*A4C3*/       &operation_exception,            
 /*A4C4*/       &operation_exception,            
 /*A4C5*/       &operation_exception,            
 /*A4C6*/       &operation_exception,            
 /*A4C7*/       &operation_exception,            
 /*A4C8*/       &operation_exception,            
 /*A4C9*/       &operation_exception,            
 /*A4CA*/       &operation_exception,            
 /*A4CB*/       &operation_exception,            
 /*A4CC*/       &operation_exception,            
 /*A4CD*/       &operation_exception,            
 /*A4CE*/       &operation_exception,            
 /*A4CF*/       &operation_exception,            
 /*A4D0*/       &operation_exception,            
 /*A4D1*/       &operation_exception,            
 /*A4D2*/       &operation_exception,            
 /*A4D3*/       &operation_exception,            
 /*A4D4*/       &operation_exception,            
 /*A4D5*/       &operation_exception,            
 /*A4D6*/       &operation_exception,            
 /*A4D7*/       &operation_exception,            
 /*A4D8*/       &operation_exception,            
 /*A4D9*/       &operation_exception,            
 /*A4DA*/       &operation_exception,            
 /*A4DB*/       &operation_exception,            
 /*A4DC*/       &operation_exception,            
 /*A4DD*/       &operation_exception,            
 /*A4DE*/       &operation_exception,            
 /*A4DF*/       &operation_exception,            
 /*A4E0*/       &operation_exception,            
 /*A4E1*/       &operation_exception,            
 /*A4E2*/       &operation_exception,            
 /*A4E3*/       &operation_exception,            
 /*A4E4*/       &operation_exception,            
 /*A4E5*/       &operation_exception,            
 /*A4E6*/       &operation_exception,            
 /*A4E7*/       &operation_exception,            
 /*A4E8*/       &operation_exception,            
 /*A4E9*/       &operation_exception,            
 /*A4EA*/       &operation_exception,            
 /*A4EB*/       &operation_exception,            
 /*A4EC*/       &operation_exception,            
 /*A4ED*/       &operation_exception,            
 /*A4EE*/       &operation_exception,            
 /*A4EF*/       &operation_exception,            
 /*A4F0*/       &operation_exception,            
 /*A4F1*/       &operation_exception,            
 /*A4F2*/       &operation_exception,            
 /*A4F3*/       &operation_exception,            
 /*A4F4*/       &operation_exception,            
 /*A4F5*/       &operation_exception,            
 /*A4F6*/       &operation_exception,            
 /*A4F7*/       &operation_exception,            
 /*A4F8*/       &operation_exception,            
 /*A4F9*/       &operation_exception,            
 /*A4FA*/       &operation_exception,            
 /*A4FB*/       &operation_exception,            
 /*A4FC*/       &operation_exception,            
 /*A4FD*/       &operation_exception,            
 /*A4FE*/       &operation_exception,            
 /*A4FF*/       &operation_exception };


zz_func opcode_a5xx[256] = {
 /*A500*/       &operation_exception,            
 /*A501*/       &operation_exception,                 
 /*A502*/       &operation_exception,                   
 /*A503*/       &operation_exception,            
 /*A504*/       &operation_exception,            
 /*A505*/       &operation_exception,            
 /*A506*/       &operation_exception,            
 /*A507*/       &operation_exception,  
 /*A508*/       &operation_exception,            
 /*A509*/       &operation_exception,            
 /*A50A*/       &operation_exception,            
 /*A50B*/       &operation_exception,            
 /*A50C*/       &operation_exception,            
 /*A50D*/       &operation_exception,            
 /*A50E*/       &operation_exception,            
 /*A50F*/       &operation_exception,            
 /*A510*/       &operation_exception,            
 /*A511*/       &operation_exception,            
 /*A512*/       &operation_exception,            
 /*A513*/       &operation_exception,            
 /*A514*/       &operation_exception,            
 /*A515*/       &operation_exception,            
 /*A516*/       &operation_exception,            
 /*A517*/       &operation_exception,            
 /*A518*/       &operation_exception,            
 /*A519*/       &operation_exception,            
 /*A51A*/       &operation_exception,            
 /*A51B*/       &operation_exception,            
 /*A51C*/       &operation_exception,            
 /*A51D*/       &operation_exception,            
 /*A51E*/       &operation_exception,            
 /*A51F*/       &operation_exception,            
 /*A520*/       &operation_exception,            
 /*A521*/       &operation_exception,            
 /*A522*/       &operation_exception,            
 /*A523*/       &operation_exception,            
 /*A524*/       &operation_exception,            
 /*A525*/       &operation_exception,            
 /*A526*/       &operation_exception,            
 /*A527*/       &operation_exception,            
 /*A528*/       &operation_exception,            
 /*A529*/       &operation_exception,            
 /*A52A*/       &operation_exception,            
 /*A52B*/       &operation_exception,            
 /*A52C*/       &operation_exception,            
 /*A52D*/       &operation_exception,            
 /*A52E*/       &operation_exception,            
 /*A52F*/       &operation_exception,            
 /*A530*/       &operation_exception,            
 /*A531*/       &operation_exception,            
 /*A532*/       &operation_exception,            
 /*A533*/       &operation_exception,            
 /*A534*/       &operation_exception,            
 /*A535*/       &operation_exception,            
 /*A536*/       &operation_exception,            
 /*A537*/       &operation_exception,            
 /*A538*/       &operation_exception,            
 /*A539*/       &operation_exception,            
 /*A53A*/       &operation_exception,            
 /*A53B*/       &operation_exception,            
 /*A53C*/       &operation_exception,            
 /*A53D*/       &operation_exception,            
 /*A53E*/       &operation_exception,            
 /*A53F*/       &operation_exception,            
 /*A540*/       &operation_exception,            
 /*A541*/       &operation_exception,            
 /*A542*/       &operation_exception,            
 /*A543*/       &operation_exception,            
 /*A544*/       &operation_exception,            
 /*A545*/       &operation_exception,            
 /*A546*/       &operation_exception,            
 /*A547*/       &operation_exception,            
 /*A548*/       &operation_exception,            
 /*A549*/       &operation_exception,            
 /*A54A*/       &operation_exception,            
 /*A54B*/       &operation_exception,            
 /*A54C*/       &operation_exception,            
 /*A54D*/       &operation_exception,            
 /*A54E*/       &operation_exception,            
 /*A54F*/       &operation_exception,            
 /*A550*/       &operation_exception,            
 /*A551*/       &operation_exception,            
 /*A552*/       &operation_exception,            
 /*A553*/       &operation_exception,            
 /*A554*/       &operation_exception,            
 /*A555*/       &operation_exception,            
 /*A556*/       &operation_exception,            
 /*A557*/       &operation_exception,            
 /*A558*/       &operation_exception,            
 /*A559*/       &operation_exception,            
 /*A55A*/       &operation_exception,            
 /*A55B*/       &operation_exception,            
 /*A55C*/       &operation_exception,            
 /*A55D*/       &operation_exception,            
 /*A55E*/       &operation_exception,            
 /*A55F*/       &operation_exception,            
 /*A560*/       &operation_exception,            
 /*A561*/       &operation_exception,            
 /*A562*/       &operation_exception,            
 /*A563*/       &operation_exception,            
 /*A564*/       &operation_exception,            
 /*A565*/       &operation_exception,            
 /*A566*/       &operation_exception,            
 /*A567*/       &operation_exception,            
 /*A568*/       &operation_exception,            
 /*A569*/       &operation_exception,            
 /*A56A*/       &operation_exception,            
 /*A56B*/       &operation_exception,            
 /*A56C*/       &operation_exception,            
 /*A56D*/       &operation_exception,            
 /*A56E*/       &operation_exception,            
 /*A56F*/       &operation_exception,            
 /*A570*/       &operation_exception,            
 /*A571*/       &operation_exception,            
 /*A572*/       &operation_exception,            
 /*A573*/       &operation_exception,            
 /*A574*/       &operation_exception,            
 /*A575*/       &operation_exception,            
 /*A576*/       &operation_exception,            
 /*A577*/       &operation_exception,            
 /*A578*/       &operation_exception,            
 /*A579*/       &operation_exception,            
 /*A57A*/       &operation_exception,            
 /*A57B*/       &operation_exception,            
 /*A57C*/       &operation_exception,            
 /*A57D*/       &operation_exception,            
 /*A57E*/       &operation_exception,            
 /*A57F*/       &operation_exception,            
 /*A580*/       &operation_exception,            
 /*A581*/       &operation_exception,            
 /*A582*/       &operation_exception,            
 /*A583*/       &operation_exception,            
 /*A584*/       &operation_exception,            
 /*A585*/       &operation_exception,            
 /*A586*/       &operation_exception,            
 /*A587*/       &operation_exception,            
 /*A588*/       &operation_exception,            
 /*A589*/       &operation_exception,            
 /*A58A*/       &operation_exception,            
 /*A58B*/       &operation_exception,            
 /*A58C*/       &operation_exception,            
 /*A58D*/       &operation_exception,            
 /*A58E*/       &operation_exception,            
 /*A58F*/       &operation_exception,            
 /*A590*/       &operation_exception,            
 /*A591*/       &operation_exception,            
 /*A592*/       &operation_exception,            
 /*A593*/       &operation_exception,            
 /*A594*/       &operation_exception,            
 /*A595*/       &operation_exception,            
 /*A596*/       &operation_exception,            
 /*A597*/       &operation_exception,            
 /*A598*/       &operation_exception,            
 /*A599*/       &operation_exception,            
 /*A59A*/       &operation_exception,            
 /*A59B*/       &operation_exception,            
 /*A59C*/       &operation_exception,            
 /*A59D*/       &operation_exception,            
 /*A59E*/       &operation_exception,            
 /*A59F*/       &operation_exception,            
 /*A5A0*/       &operation_exception,            
 /*A5A1*/       &operation_exception,            
 /*A5A2*/       &operation_exception,            
 /*A5A3*/       &operation_exception,            
 /*A5A4*/       &operation_exception,            
 /*A5A5*/       &operation_exception,            
 /*A5A6*/       &operation_exception,            
 /*A5A7*/       &operation_exception,            
 /*A5A8*/       &operation_exception,            
 /*A5A9*/       &operation_exception,            
 /*A5AA*/       &operation_exception,            
 /*A5AB*/       &operation_exception,            
 /*A5AC*/       &operation_exception,            
 /*A5AD*/       &operation_exception,            
 /*A5AE*/       &operation_exception,            
 /*A5AF*/       &operation_exception,            
 /*A5B0*/       &operation_exception,            
 /*A5B1*/       &operation_exception,            
 /*A501*/       &operation_exception,            
 /*A5B3*/       &operation_exception,            
 /*A5B4*/       &operation_exception,            
 /*A5B5*/       &operation_exception,            
 /*A5B6*/       &operation_exception,            
 /*A5B7*/       &operation_exception,            
 /*A5B8*/       &operation_exception,            
 /*A5B9*/       &operation_exception,            
 /*A5BA*/       &operation_exception,            
 /*A5BB*/       &operation_exception,            
 /*A5BC*/       &operation_exception,            
 /*A5BD*/       &operation_exception,            
 /*A5BE*/       &operation_exception,            
 /*A5BF*/       &operation_exception,            
 /*A5C0*/       &operation_exception,            
 /*A5C1*/       &operation_exception,            
 /*A5C2*/       &operation_exception,            
 /*A5C3*/       &operation_exception,            
 /*A5C4*/       &operation_exception,            
 /*A5C5*/       &operation_exception,            
 /*A5C6*/       &operation_exception,            
 /*A5C7*/       &operation_exception,            
 /*A5C8*/       &operation_exception,            
 /*A5C9*/       &operation_exception,            
 /*A5CA*/       &operation_exception,            
 /*A5CB*/       &operation_exception,            
 /*A5CC*/       &operation_exception,            
 /*A5CD*/       &operation_exception,            
 /*A5CE*/       &operation_exception,            
 /*A5CF*/       &operation_exception,            
 /*A5D0*/       &operation_exception,            
 /*A5D1*/       &operation_exception,            
 /*A5D2*/       &operation_exception,            
 /*A5D3*/       &operation_exception,            
 /*A5D4*/       &operation_exception,            
 /*A5D5*/       &operation_exception,            
 /*A5D6*/       &operation_exception,            
 /*A5D7*/       &operation_exception,            
 /*A5D8*/       &operation_exception,            
 /*A5D9*/       &operation_exception,            
 /*A5DA*/       &operation_exception,            
 /*A5DB*/       &operation_exception,            
 /*A5DC*/       &operation_exception,            
 /*A5DD*/       &operation_exception,            
 /*A5DE*/       &operation_exception,            
 /*A5DF*/       &operation_exception,            
 /*A5E0*/       &operation_exception,            
 /*A5E1*/       &operation_exception,            
 /*A5E2*/       &operation_exception,            
 /*A5E3*/       &operation_exception,            
 /*A5E4*/       &operation_exception,            
 /*A5E5*/       &operation_exception,            
 /*A5E6*/       &operation_exception,            
 /*A5E7*/       &operation_exception,            
 /*A5E8*/       &operation_exception,            
 /*A5E9*/       &operation_exception,            
 /*A5EA*/       &operation_exception,            
 /*A5EB*/       &operation_exception,            
 /*A5EC*/       &operation_exception,            
 /*A5ED*/       &operation_exception,            
 /*A5EE*/       &operation_exception,            
 /*A5EF*/       &operation_exception,            
 /*A5F0*/       &operation_exception,            
 /*A5F1*/       &operation_exception,            
 /*A5F2*/       &operation_exception,            
 /*A5F3*/       &operation_exception,            
 /*A5F4*/       &operation_exception,            
 /*A5F5*/       &operation_exception,            
 /*A5F6*/       &operation_exception,            
 /*A5F7*/       &operation_exception,            
 /*A5F8*/       &operation_exception,            
 /*A5F9*/       &operation_exception,            
 /*A5FA*/       &operation_exception,            
 /*A5FB*/       &operation_exception,            
 /*A5FC*/       &operation_exception,            
 /*A5FD*/       &operation_exception,            
 /*A5FE*/       &operation_exception,            
 /*A5FF*/       &operation_exception };


zz_func opcode_a6xx[256] = {
 /*A600*/       &operation_exception,            
 /*A601*/       &operation_exception,                 
 /*A602*/       &operation_exception,                   
 /*A603*/       &operation_exception,            
 /*A604*/       &operation_exception,            
 /*A605*/       &operation_exception,            
 /*A606*/       &operation_exception,            
 /*A607*/       &operation_exception,  
 /*A608*/       &operation_exception,            
 /*A609*/       &operation_exception,            
 /*A60A*/       &operation_exception,            
 /*A60B*/       &operation_exception,            
 /*A60C*/       &operation_exception,            
 /*A60D*/       &operation_exception,            
 /*A60E*/       &operation_exception,            
 /*A60F*/       &operation_exception,            
 /*A610*/       &operation_exception,            
 /*A611*/       &operation_exception,            
 /*A612*/       &operation_exception,            
 /*A613*/       &operation_exception,            
 /*A614*/       &operation_exception,            
 /*A615*/       &operation_exception,            
 /*A616*/       &operation_exception,            
 /*A617*/       &operation_exception,            
 /*A618*/       &operation_exception,            
 /*A619*/       &operation_exception,            
 /*A61A*/       &operation_exception,            
 /*A61B*/       &operation_exception,            
 /*A61C*/       &operation_exception,            
 /*A61D*/       &operation_exception,            
 /*A61E*/       &operation_exception,            
 /*A61F*/       &operation_exception,            
 /*A620*/       &operation_exception,            
 /*A621*/       &operation_exception,            
 /*A622*/       &operation_exception,            
 /*A623*/       &operation_exception,            
 /*A624*/       &operation_exception,            
 /*A625*/       &operation_exception,            
 /*A626*/       &operation_exception,            
 /*A627*/       &operation_exception,            
 /*A628*/       &operation_exception,            
 /*A629*/       &operation_exception,            
 /*A62A*/       &operation_exception,            
 /*A62B*/       &operation_exception,            
 /*A62C*/       &operation_exception,            
 /*A62D*/       &operation_exception,            
 /*A62E*/       &operation_exception,            
 /*A62F*/       &operation_exception,            
 /*A630*/       &operation_exception,            
 /*A631*/       &operation_exception,            
 /*A632*/       &operation_exception,            
 /*A633*/       &operation_exception,            
 /*A634*/       &operation_exception,            
 /*A635*/       &operation_exception,            
 /*A636*/       &operation_exception,            
 /*A637*/       &operation_exception,            
 /*A638*/       &operation_exception,            
 /*A639*/       &operation_exception,            
 /*A63A*/       &operation_exception,            
 /*A63B*/       &operation_exception,            
 /*A63C*/       &operation_exception,            
 /*A63D*/       &operation_exception,            
 /*A63E*/       &operation_exception,            
 /*A63F*/       &operation_exception,            
 /*A640*/       &zz_v_test_vmr,                         /* VTVM      */
 /*A641*/       &zz_v_complement_vmr,                   /* VCVM      */
 /*A642*/       &zz_v_count_left_zeros_in_vmr,          /* VCZVM     */
 /*A643*/       &zz_v_count_ones_in_vmr,                /* VCOVM     */
 /*A644*/       &zz_v_extract_vct,                      /* VXVC      */
 /*A645*/       &operation_exception,            
 /*A646*/       &zz_v_extract_vector_modes,             /* VXVMM     */
 /*A647*/       &operation_exception,            
 /*A648*/       &zz_v_restore_vr,                       /* VRRS      */
 /*A649*/       &zz_v_save_changed_vr,                  /* VRSVC     */
 /*A64A*/       &zz_v_save_vr,                          /* VRSV      */
 /*A64B*/       &operation_exception,            
 /*A64C*/       &operation_exception,            
 /*A64D*/       &operation_exception,            
 /*A64E*/       &operation_exception,            
 /*A64F*/       &operation_exception,            
 /*A650*/       &operation_exception,            
 /*A651*/       &operation_exception,            
 /*A652*/       &operation_exception,            
 /*A653*/       &operation_exception,            
 /*A654*/       &operation_exception,            
 /*A655*/       &operation_exception,            
 /*A656*/       &operation_exception,            
 /*A657*/       &operation_exception,            
 /*A658*/       &operation_exception,            
 /*A659*/       &operation_exception,            
 /*A65A*/       &operation_exception,            
 /*A65B*/       &operation_exception,            
 /*A65C*/       &operation_exception,            
 /*A65D*/       &operation_exception,            
 /*A65E*/       &operation_exception,            
 /*A65F*/       &operation_exception,            
 /*A660*/       &operation_exception,            
 /*A661*/       &operation_exception,            
 /*A662*/       &operation_exception,            
 /*A663*/       &operation_exception,            
 /*A664*/       &operation_exception,            
 /*A665*/       &operation_exception,            
 /*A666*/       &operation_exception,            
 /*A667*/       &operation_exception,            
 /*A668*/       &operation_exception,            
 /*A669*/       &operation_exception,            
 /*A66A*/       &operation_exception,            
 /*A66B*/       &operation_exception,            
 /*A66C*/       &operation_exception,            
 /*A66D*/       &operation_exception,            
 /*A66E*/       &operation_exception,            
 /*A66F*/       &operation_exception,            
 /*A670*/       &operation_exception,            
 /*A671*/       &operation_exception,            
 /*A672*/       &operation_exception,            
 /*A673*/       &operation_exception,            
 /*A674*/       &operation_exception,            
 /*A675*/       &operation_exception,            
 /*A676*/       &operation_exception,            
 /*A677*/       &operation_exception,            
 /*A678*/       &operation_exception,            
 /*A679*/       &operation_exception,            
 /*A67A*/       &operation_exception,            
 /*A67B*/       &operation_exception,            
 /*A67C*/       &operation_exception,            
 /*A67D*/       &operation_exception,            
 /*A67E*/       &operation_exception,            
 /*A67F*/       &operation_exception,            
 /*A680*/       &zz_v_load_vmr,                         /* VLVM      */
 /*A681*/       &zz_v_load_vmr_complement,              /* VLCVM     */
 /*A682*/       &zz_v_store_vmr,                        /* VSTVM     */
 /*A683*/       &operation_exception,            
 /*A684*/       &zz_v_and_to_vmr,                       /* VNVM      */
 /*A685*/       &zz_v_or_to_vmr,                        /* VOVM      */
 /*A686*/       &zz_v_exclusive_or_to_vmr,              /* VXVM      */
 /*A687*/       &operation_exception,            
 /*A688*/       &operation_exception,            
 /*A689*/       &operation_exception,            
 /*A68A*/       &operation_exception,            
 /*A68B*/       &operation_exception,            
 /*A68C*/       &operation_exception,            
 /*A68D*/       &operation_exception,            
 /*A68E*/       &operation_exception,            
 /*A68F*/       &operation_exception,            
 /*A690*/       &operation_exception,            
 /*A691*/       &operation_exception,            
 /*A692*/       &operation_exception,            
 /*A693*/       &operation_exception,            
 /*A694*/       &operation_exception,            
 /*A695*/       &operation_exception,            
 /*A696*/       &operation_exception,            
 /*A697*/       &operation_exception,            
 /*A698*/       &operation_exception,            
 /*A699*/       &operation_exception,            
 /*A69A*/       &operation_exception,            
 /*A69B*/       &operation_exception,            
 /*A69C*/       &operation_exception,            
 /*A69D*/       &operation_exception,            
 /*A69E*/       &operation_exception,            
 /*A69F*/       &operation_exception,            
 /*A6A0*/       &operation_exception,            
 /*A6A1*/       &operation_exception,            
 /*A6A2*/       &operation_exception,            
 /*A6A3*/       &operation_exception,            
 /*A6A4*/       &operation_exception,            
 /*A6A5*/       &operation_exception,            
 /*A6A6*/       &operation_exception,            
 /*A6A7*/       &operation_exception,            
 /*A6A8*/       &operation_exception,            
 /*A6A9*/       &operation_exception,            
 /*A6AA*/       &operation_exception,            
 /*A6AB*/       &operation_exception,            
 /*A6AC*/       &operation_exception,            
 /*A6AD*/       &operation_exception,            
 /*A6AE*/       &operation_exception,            
 /*A6AF*/       &operation_exception,            
 /*A6B0*/       &operation_exception,            
 /*A6B1*/       &operation_exception,            
 /*A6B2*/       &operation_exception,            
 /*A6B3*/       &operation_exception,            
 /*A6B4*/       &operation_exception,            
 /*A6B5*/       &operation_exception,            
 /*A6B6*/       &operation_exception,            
 /*A6B7*/       &operation_exception,            
 /*A6B8*/       &operation_exception,            
 /*A6B9*/       &operation_exception,            
 /*A6BA*/       &operation_exception,            
 /*A6BB*/       &operation_exception,            
 /*A6BC*/       &operation_exception,            
 /*A6BD*/       &operation_exception,            
 /*A6BE*/       &operation_exception,            
 /*A6BF*/       &operation_exception,            
 /*A6C0*/       &zz_v_save_vsr,                         /* VSRSV     */
 /*A6C1*/       &zz_v_save_vmr,                         /* VMRSV     */
 /*A6C2*/       &zz_v_restore_vsr,                      /* VSRRS     */
 /*A6C3*/       &zz_v_restore_vmr,                      /* VMRRS     */
 /*A6C4*/       &zz_v_load_vct_from_address,            /* VLVCA     */
 /*A6C5*/       &zz_v_clear_vr,                         /* VRCL      */
 /*A6C6*/       &zz_v_set_vector_mask_mode,             /* VSVMM     */
 /*A6C7*/       &zz_v_load_vix_from_address,            /* VLVXA     */
 /*A6C8*/       &zz_v_store_vector_parameters,          /* VSTVP     */
 /*A6C9*/       &operation_exception,            
 /*A6CA*/       &zz_v_save_vac,                         /* VACSV     */
 /*A6CB*/       &zz_v_restore_vac,                      /* VACRS     */
 /*A6CC*/       &operation_exception,            
 /*A6CD*/       &operation_exception,            
 /*A6CE*/       &operation_exception,            
 /*A6CF*/       &operation_exception,            
 /*A6D0*/       &operation_exception,            
 /*A6D1*/       &operation_exception,            
 /*A6D2*/       &operation_exception,            
 /*A6D3*/       &operation_exception,            
 /*A6D4*/       &operation_exception,            
 /*A6D5*/       &operation_exception,            
 /*A6D6*/       &operation_exception,            
 /*A6D7*/       &operation_exception,            
 /*A6D8*/       &operation_exception,            
 /*A6D9*/       &operation_exception,            
 /*A6DA*/       &operation_exception,            
 /*A6DB*/       &operation_exception,            
 /*A6DC*/       &operation_exception,            
 /*A6DD*/       &operation_exception,            
 /*A6DE*/       &operation_exception,            
 /*A6DF*/       &operation_exception,            
 /*A6E0*/       &operation_exception,            
 /*A6E1*/       &operation_exception,            
 /*A6E2*/       &operation_exception,            
 /*A6E3*/       &operation_exception,            
 /*A6E4*/       &operation_exception,            
 /*A6E5*/       &operation_exception,            
 /*A6E6*/       &operation_exception,            
 /*A6E7*/       &operation_exception,            
 /*A6E8*/       &operation_exception,            
 /*A6E9*/       &operation_exception,            
 /*A6EA*/       &operation_exception,            
 /*A6EB*/       &operation_exception,            
 /*A6EC*/       &operation_exception,            
 /*A6ED*/       &operation_exception,            
 /*A6EE*/       &operation_exception,            
 /*A6EF*/       &operation_exception,            
 /*A6F0*/       &operation_exception,            
 /*A6F1*/       &operation_exception,            
 /*A6F2*/       &operation_exception,            
 /*A6F3*/       &operation_exception,            
 /*A6F4*/       &operation_exception,            
 /*A6F5*/       &operation_exception,            
 /*A6F6*/       &operation_exception,            
 /*A6F7*/       &operation_exception,            
 /*A6F8*/       &operation_exception,            
 /*A6F9*/       &operation_exception,            
 /*A6FA*/       &operation_exception,            
 /*A6FB*/       &operation_exception,            
 /*A6FC*/       &operation_exception,            
 /*A6FD*/       &operation_exception,            
 /*A6FE*/       &operation_exception,            
 /*A6FF*/       &operation_exception };


zz_func opcode_e4xx[256] = {
 /*E400*/       &operation_exception,            
 /*E401*/       &operation_exception,                 
 /*E402*/       &operation_exception,                   
 /*E403*/       &operation_exception,            
 /*E404*/       &operation_exception,            
 /*E405*/       &operation_exception,            
 /*E406*/       &operation_exception,            
 /*E407*/       &operation_exception,  
 /*E408*/       &operation_exception,            
 /*E409*/       &operation_exception,            
 /*E40A*/       &operation_exception,            
 /*E40B*/       &operation_exception,            
 /*E40C*/       &operation_exception,            
 /*E40D*/       &operation_exception,            
 /*E40E*/       &operation_exception,            
 /*E40F*/       &operation_exception,            
 /*E410*/       &operation_exception,            
 /*E411*/       &operation_exception,            
 /*E412*/       &operation_exception,            
 /*E413*/       &operation_exception,            
 /*E414*/       &operation_exception,            
 /*E415*/       &operation_exception,            
 /*E416*/       &operation_exception,            
 /*E417*/       &operation_exception,            
 /*E418*/       &operation_exception,            
 /*E419*/       &operation_exception,            
 /*E41A*/       &operation_exception,            
 /*E41B*/       &operation_exception,            
 /*E41C*/       &operation_exception,            
 /*E41D*/       &operation_exception,            
 /*E41E*/       &operation_exception,            
 /*E41F*/       &operation_exception,            
 /*E420*/       &operation_exception,            
 /*E421*/       &operation_exception,            
 /*E422*/       &operation_exception,            
 /*E423*/       &operation_exception,            
 /*E424*/       &operation_exception,            
 /*E425*/       &operation_exception,            
 /*E426*/       &operation_exception,            
 /*E427*/       &operation_exception,            
 /*E428*/       &operation_exception,            
 /*E429*/       &operation_exception,            
 /*E42A*/       &operation_exception,            
 /*E42B*/       &operation_exception,            
 /*E42C*/       &operation_exception,            
 /*E42D*/       &operation_exception,            
 /*E42E*/       &operation_exception,            
 /*E42F*/       &operation_exception,            
 /*E430*/       &operation_exception,            
 /*E431*/       &operation_exception,            
 /*E432*/       &operation_exception,            
 /*E433*/       &operation_exception,            
 /*E434*/       &operation_exception,            
 /*E435*/       &operation_exception,            
 /*E436*/       &operation_exception,            
 /*E437*/       &operation_exception,            
 /*E438*/       &operation_exception,            
 /*E439*/       &operation_exception,            
 /*E43A*/       &operation_exception,            
 /*E43B*/       &operation_exception,            
 /*E43C*/       &operation_exception,            
 /*E43D*/       &operation_exception,            
 /*E43E*/       &operation_exception,            
 /*E43F*/       &operation_exception,            
 /*E440*/       &operation_exception,            
 /*E441*/       &operation_exception,            
 /*E442*/       &operation_exception,            
 /*E443*/       &operation_exception,            
 /*E444*/       &operation_exception,            
 /*E445*/       &operation_exception,            
 /*E446*/       &operation_exception,            
 /*E447*/       &operation_exception,            
 /*E448*/       &operation_exception,            
 /*E449*/       &operation_exception,            
 /*E44A*/       &operation_exception,            
 /*E44B*/       &operation_exception,            
 /*E44C*/       &operation_exception,            
 /*E44D*/       &operation_exception,            
 /*E44E*/       &operation_exception,            
 /*E44F*/       &operation_exception,            
 /*E450*/       &operation_exception,            
 /*E451*/       &operation_exception,            
 /*E452*/       &operation_exception,            
 /*E453*/       &operation_exception,            
 /*E454*/       &operation_exception,            
 /*E455*/       &operation_exception,            
 /*E456*/       &operation_exception,            
 /*E457*/       &operation_exception,            
 /*E458*/       &operation_exception,            
 /*E459*/       &operation_exception,            
 /*E45A*/       &operation_exception,            
 /*E45B*/       &operation_exception,            
 /*E45C*/       &operation_exception,            
 /*E45D*/       &operation_exception,            
 /*E45E*/       &operation_exception,            
 /*E45F*/       &operation_exception,            
 /*E460*/       &operation_exception,            
 /*E461*/       &operation_exception,            
 /*E462*/       &operation_exception,            
 /*E463*/       &operation_exception,            
 /*E464*/       &operation_exception,            
 /*E465*/       &operation_exception,            
 /*E466*/       &operation_exception,            
 /*E467*/       &operation_exception,            
 /*E468*/       &operation_exception,            
 /*E469*/       &operation_exception,            
 /*E46A*/       &operation_exception,            
 /*E46B*/       &operation_exception,            
 /*E46C*/       &operation_exception,            
 /*E46D*/       &operation_exception,            
 /*E46E*/       &operation_exception,            
 /*E46F*/       &operation_exception,            
 /*E470*/       &operation_exception,            
 /*E471*/       &operation_exception,            
 /*E472*/       &operation_exception,            
 /*E473*/       &operation_exception,            
 /*E474*/       &operation_exception,            
 /*E475*/       &operation_exception,            
 /*E476*/       &operation_exception,            
 /*E477*/       &operation_exception,            
 /*E478*/       &operation_exception,            
 /*E479*/       &operation_exception,            
 /*E47A*/       &operation_exception,            
 /*E47B*/       &operation_exception,            
 /*E47C*/       &operation_exception,            
 /*E47D*/       &operation_exception,            
 /*E47E*/       &operation_exception,            
 /*E47F*/       &operation_exception,            
 /*E480*/       &operation_exception,            
 /*E481*/       &operation_exception,            
 /*E482*/       &operation_exception,            
 /*E483*/       &operation_exception,            
 /*E484*/       &operation_exception,            
 /*E485*/       &operation_exception,            
 /*E486*/       &operation_exception,            
 /*E487*/       &operation_exception,            
 /*E488*/       &operation_exception,            
 /*E489*/       &operation_exception,            
 /*E48A*/       &operation_exception,            
 /*E48B*/       &operation_exception,            
 /*E48C*/       &operation_exception,            
 /*E48D*/       &operation_exception,            
 /*E48E*/       &operation_exception,            
 /*E48F*/       &operation_exception,            
 /*E490*/       &operation_exception,            
 /*E491*/       &operation_exception,            
 /*E492*/       &operation_exception,            
 /*E493*/       &operation_exception,            
 /*E494*/       &operation_exception,            
 /*E495*/       &operation_exception,            
 /*E496*/       &operation_exception,            
 /*E497*/       &operation_exception,            
 /*E498*/       &operation_exception,            
 /*E499*/       &operation_exception,            
 /*E49A*/       &operation_exception,            
 /*E49B*/       &operation_exception,            
 /*E49C*/       &operation_exception,            
 /*E49D*/       &operation_exception,            
 /*E49E*/       &operation_exception,            
 /*E49F*/       &operation_exception,            
 /*E4A0*/       &operation_exception,            
 /*E4A1*/       &operation_exception,            
 /*E4A2*/       &operation_exception,            
 /*E4A3*/       &operation_exception,            
 /*E4A4*/       &operation_exception,            
 /*E4A5*/       &operation_exception,            
 /*E4A6*/       &operation_exception,            
 /*E4A7*/       &operation_exception,            
 /*E4A8*/       &operation_exception,            
 /*E4A9*/       &operation_exception,            
 /*E4AA*/       &operation_exception,            
 /*E4AB*/       &operation_exception,            
 /*E4AC*/       &operation_exception,            
 /*E4AD*/       &operation_exception,            
 /*E4AE*/       &operation_exception,            
 /*E4AF*/       &operation_exception,            
 /*E4B0*/       &operation_exception,            
 /*E4B1*/       &operation_exception,            
 /*E401*/       &operation_exception,            
 /*E4B3*/       &operation_exception,            
 /*E4B4*/       &operation_exception,            
 /*E4B5*/       &operation_exception,            
 /*E4B6*/       &operation_exception,            
 /*E4B7*/       &operation_exception,            
 /*E4B8*/       &operation_exception,            
 /*E4B9*/       &operation_exception,            
 /*E4BA*/       &operation_exception,            
 /*E4BB*/       &operation_exception,            
 /*E4BC*/       &operation_exception,            
 /*E4BD*/       &operation_exception,            
 /*E4BE*/       &operation_exception,            
 /*E4BF*/       &operation_exception,            
 /*E4C0*/       &operation_exception,            
 /*E4C1*/       &operation_exception,            
 /*E4C2*/       &operation_exception,            
 /*E4C3*/       &operation_exception,            
 /*E4C4*/       &operation_exception,            
 /*E4C5*/       &operation_exception,            
 /*E4C6*/       &operation_exception,            
 /*E4C7*/       &operation_exception,            
 /*E4C8*/       &operation_exception,            
 /*E4C9*/       &operation_exception,            
 /*E4CA*/       &operation_exception,            
 /*E4CB*/       &operation_exception,            
 /*E4CC*/       &operation_exception,            
 /*E4CD*/       &operation_exception,            
 /*E4CE*/       &operation_exception,            
 /*E4CF*/       &operation_exception,            
 /*E4D0*/       &operation_exception,            
 /*E4D1*/       &operation_exception,            
 /*E4D2*/       &operation_exception,            
 /*E4D3*/       &operation_exception,            
 /*E4D4*/       &operation_exception,            
 /*E4D5*/       &operation_exception,            
 /*E4D6*/       &operation_exception,            
 /*E4D7*/       &operation_exception,            
 /*E4D8*/       &operation_exception,            
 /*E4D9*/       &operation_exception,            
 /*E4DA*/       &operation_exception,            
 /*E4DB*/       &operation_exception,            
 /*E4DC*/       &operation_exception,            
 /*E4DD*/       &operation_exception,            
 /*E4DE*/       &operation_exception,            
 /*E4DF*/       &operation_exception,            
 /*E4E0*/       &operation_exception,            
 /*E4E1*/       &operation_exception,            
 /*E4E2*/       &operation_exception,            
 /*E4E3*/       &operation_exception,            
 /*E4E4*/       &operation_exception,            
 /*E4E5*/       &operation_exception,            
 /*E4E6*/       &operation_exception,            
 /*E4E7*/       &operation_exception,            
 /*E4E8*/       &operation_exception,            
 /*E4E9*/       &operation_exception,            
 /*E4EA*/       &operation_exception,            
 /*E4EB*/       &operation_exception,            
 /*E4EC*/       &operation_exception,            
 /*E4ED*/       &operation_exception,            
 /*E4EE*/       &operation_exception,            
 /*E4EF*/       &operation_exception,            
 /*E4F0*/       &operation_exception,            
 /*E4F1*/       &operation_exception,            
 /*E4F2*/       &operation_exception,            
 /*E4F3*/       &operation_exception,            
 /*E4F4*/       &operation_exception,            
 /*E4F5*/       &operation_exception,            
 /*E4F6*/       &operation_exception,            
 /*E4F7*/       &operation_exception,            
 /*E4F8*/       &operation_exception,            
 /*E4F9*/       &operation_exception,            
 /*E4FA*/       &operation_exception,            
 /*E4FB*/       &operation_exception,            
 /*E4FC*/       &operation_exception,            
 /*E4FD*/       &operation_exception,            
 /*E4FE*/       &operation_exception,            
 /*E4FF*/       &operation_exception };

#endif /*defined(FEATURE_VECTOR_FACILITY)*/


/* The following execute_xxxx routines can be optimized by the
   compiler to an indexed jump, leaving the stack frame untouched
   as the called routine has the same arguments, and the routine
   exits immediately after the call.                             *JJ */

void execute_01xx (BYTE inst[], int execflag, REGS *regs)
{
    opcode_01xx[inst[1]](inst, execflag, regs);
}


void execute_a7xx (BYTE inst[], int execflag, REGS *regs)
{
    opcode_a7xx[inst[1] & 0x0F](inst, execflag, regs);
}


void execute_b2xx (BYTE inst[], int execflag, REGS *regs)
{
    opcode_b2xx[inst[1]](inst, execflag, regs);
}


void execute_e5xx (BYTE inst[], int execflag, REGS *regs)
{
    opcode_e5xx[inst[1]](inst, execflag, regs);
}


#if defined(FEATURE_VECTOR_FACILITY)

void execute_a4xx (BYTE inst[], int execflag, REGS *regs)
{
    opcode_a4xx[inst[1]](inst, execflag, regs);
}


void execute_a5xx (BYTE inst[], int execflag, REGS *regs)
{
    opcode_a5xx[inst[1]](inst, execflag, regs);
}


void execute_a6xx (BYTE inst[], int execflag, REGS *regs)
{
    opcode_a6xx[inst[1]](inst, execflag, regs);
}


void execute_e4xx (BYTE inst[], int execflag, REGS *regs)
{
    opcode_e4xx[inst[1]](inst, execflag, regs);
}

#endif /*defined(FEATURE_VECTOR_FACILITY)*/


void operation_exception (BYTE inst[], int execflag, REGS *regs)
{
    if( !execflag )
    {
        regs->psw.ilc = (inst[0] < 0x40) ? 2 :
                        (inst[0] < 0xC0) ? 4 : 6;
        regs->psw.ia += regs->psw.ilc;
        regs->psw.ia &= ADDRESS_MAXWRAP(regs);
    }
    program_check(regs, PGM_OPERATION_EXCEPTION);
}


void dummy_instruction (BYTE inst[], int execflag, REGS *regs)
{
    logmsg("Dummy instruction: ");
    display_inst (regs, regs->inst);

    if( !execflag )
    {
        regs->psw.ilc = (inst[0] < 0x40) ? 2 :
                        (inst[0] < 0xC0) ? 4 : 6;
        regs->psw.ia += regs->psw.ilc;
        regs->psw.ia &= ADDRESS_MAXWRAP(regs);
    }
}
