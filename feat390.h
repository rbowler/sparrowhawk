/* FEAT390.H    (c) Copyright Jan Jaeger, 2000-2009                  */
/*              ESA/390 feature definitions                          */

// $Id: feat390.h,v 1.47 2009/01/03 10:58:58 jj Exp $
//
// $Log: feat390.h,v $
// Revision 1.47  2009/01/03 10:58:58  jj
// Fix storage reference
// Update path length to 1024
// Enable SCEDIO in ESA/390 mode
//
// Revision 1.46  2009/01/02 19:21:50  jj
// DVD-RAM IPL
// RAMSAVE
// SYSG Integrated 3270 console fixes
//
// Revision 1.45  2008/12/27 23:34:37  rbowler
// Integrated 3270 (SYSG) console send command
//
// Revision 1.44  2008/12/06 22:42:37  ivan
// Enable STFLE in ESA/390 Mode.
//
// Revision 1.43  2008/02/27 14:14:23  bernard
// Implemented feature_security_assist_extension_2
//
// Revision 1.42  2008/01/21 14:04:27  rbowler
// Activate ETF3 and ETF2/ETF3-Enhancements in ESA/390 mode
//
// Revision 1.41  2007/11/30 15:14:14  rbowler
// Permit String-Instruction facility to be activated in S/370 mode
//
// Revision 1.40  2007/11/18 22:27:07  rbowler
// Add Message-Security-Assist-Extension-1 facility to ESA/390
//
// Revision 1.39  2007/11/16 13:01:49  rbowler
// Add HFP-multiply-add/subtract facility to ESA/390
//
// Revision 1.38  2007/06/23 00:04:09  ivan
// Update copyright notices to include current year (2007)
//
// Revision 1.37  2007/04/25 12:10:27  rbowler
// Move LFAS,SFASR to IEEE-exception-simulation facility
//
// Revision 1.36  2006/12/08 09:43:21  jj
// Add CVS message log
//

/* This file MUST NOT contain #undef statements */
#if defined(OPTION_390_MODE)
#define _ARCH_390_NAME "ESA/390"

#define FEATURE_4K_STORAGE_KEYS
#define FEATURE_ACCESS_REGISTERS
#define FEATURE_ADDRESS_LIMIT_CHECKING
#define FEATURE_BASIC_FP_EXTENSIONS
#define FEATURE_BIMODAL_ADDRESSING
#define FEATURE_BINARY_FLOATING_POINT
#define FEATURE_BRANCH_AND_SET_AUTHORITY
#define FEATURE_BROADCASTED_PURGING
#define FEATURE_CANCEL_IO_FACILITY
#define FEATURE_CALLED_SPACE_IDENTIFICATION
#define FEATURE_CHANNEL_SUBSYSTEM
#define FEATURE_CHECKSUM_INSTRUCTION
#define FEATURE_CHSC
#define FEATURE_COMPARE_AND_MOVE_EXTENDED
#define FEATURE_COMPRESSION
#define FEATURE_CPU_RECONFIG
#define FEATURE_DUAL_ADDRESS_SPACE
#define FEATURE_EMULATE_VM
#define FEATURE_ETF2_ENHANCEMENT                                /*@ZA*/
#define FEATURE_ETF3_ENHANCEMENT                                /*@ZA*/
#define FEATURE_EXPANDED_STORAGE
#define FEATURE_EXPEDITED_SIE_SUBSET
#define FEATURE_EXTENDED_STORAGE_KEYS
#define FEATURE_EXTENDED_TOD_CLOCK
#define FEATURE_EXTENDED_TRANSLATION
#define FEATURE_EXTENDED_TRANSLATION_FACILITY_2
#define FEATURE_EXTENDED_TRANSLATION_FACILITY_3                 /*@ZA*/
#define FEATURE_EXTERNAL_INTERRUPT_ASSIST
#define FEATURE_FETCH_PROTECTION_OVERRIDE
#define FEATURE_FPS_ENHANCEMENT                                 /*DFP*/
#define FEATURE_FPS_EXTENSIONS
#define FEATURE_HERCULES_DIAGCALLS
#define FEATURE_HEXADECIMAL_FLOATING_POINT
#define FEATURE_HFP_EXTENSIONS
#define FEATURE_HFP_MULTIPLY_ADD_SUBTRACT
#define FEATURE_HYPERVISOR
#define FEATURE_IMMEDIATE_AND_RELATIVE
#define FEATURE_INCORRECT_LENGTH_INDICATION_SUPPRESSION
#define FEATURE_INTEGRATED_3270_CONSOLE
//#define FEATURE_INTEGRATED_ASCII_CONSOLE
#define FEATURE_INTERPRETIVE_EXECUTION
#define FEATURE_IO_ASSIST
#define FEATURE_LOCK_PAGE
#define FEATURE_LINKAGE_STACK
#define FEATURE_MESSAGE_SECURITY_ASSIST
#define FEATURE_MESSAGE_SECURITY_ASSIST_EXTENSION_1
#define FEATURE_MESSAGE_SECURITY_ASSIST_EXTENSION_2
#define FEATURE_MOVE_PAGE_FACILITY_2
#define FEATURE_MSSF_CALL
#define FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE
#define FEATURE_MVS_ASSIST
#define FEATURE_PAGE_PROTECTION
#define FEATURE_PERFORM_LOCKED_OPERATION
#define FEATURE_PER
#define FEATURE_PER2
#define FEATURE_PRIVATE_SPACE
#define FEATURE_PROTECTION_INTERCEPTION_CONTROL
#define FEATURE_QUEUED_DIRECT_IO
#define FEATURE_REGION_RELOCATE
#define FEATURE_RESUME_PROGRAM
#define FEATURE_S390_DAT
#define FEATURE_SCEDIO
#define FEATURE_SERVICE_PROCESSOR
#define FEATURE_SET_ADDRESS_SPACE_CONTROL_FAST
#define FEATURE_SQUARE_ROOT
#define FEATURE_STORAGE_KEY_ASSIST
#define FEATURE_STORAGE_PROTECTION_OVERRIDE
#define FEATURE_STORE_SYSTEM_INFORMATION
#define FEATURE_STRING_INSTRUCTION
#define FEATURE_SUBSPACE_GROUP
#define FEATURE_SUPPRESSION_ON_PROTECTION
#define FEATURE_SYSTEM_CONSOLE
#define FEATURE_TEST_BLOCK
#define FEATURE_TRACING
#define FEATURE_WAITSTATE_ASSIST
#define FEATURE_STORE_FACILITY_LIST_EXTENDED
// #define FEATURE_VECTOR_FACILITY

#endif /*defined(OPTION_390_MODE)*/
/* end of FEAT390.H */
