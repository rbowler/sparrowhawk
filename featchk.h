/* FEATCHK.H	(c) Copyright Jan Jaeger, 2000-2001		     */
/*		Feature definition consistency checks		     */

/*-------------------------------------------------------------------*/
/* Perform various checks on feature combinations, and set	     */
/* additional flags to percolate certain features such as	     */
/* SIE down to lower architecture levels such that these	     */
/* can include emulation support				     */
/*-------------------------------------------------------------------*/

#if defined(FEATCHK_CHECK_ALL)

/* FEATURE_INTERPRETIVE_EXECUTION is related to host related issues
   _FEATURE_SIE is related to guest (emulation) related issues.  This
   because if FEATURE_INTERPRETIVE_EXECUTION is defined for say 390
   mode, then _FEATURE_SIE will also need to be in 370 in order to
   support 370 mode SIE emulation */
#if defined(FEATURE_INTERPRETIVE_EXECUTION)
 #define _FEATURE_SIE
 #if defined(FEATURE_ESAME)
  #define _FEATURE_ZSIE
 #endif
#endif

#if defined(FEATURE_2K_STORAGE_KEYS) \
 && defined(FEATURE_4K_STORAGE_KEYS)
 #define _FEATURE_2K_STORAGE_KEYS
#endif

#if defined(FEATURE_INTERVAL_TIMER)
 #define _FEATURE_INTERVAL_TIMER
#endif

#if defined(FEATURE_VECTOR_FACILITY)
 #define _FEATURE_VECTOR_FACILITY
#endif


#if defined(_FEATURE_SIE) && defined(FEATURE_STORAGE_KEY_ASSIST)
 #define _FEATURE_STORAGE_KEY_ASSIST
#endif

#if defined(FEATURE_CPU_RECONFIG)
 #define _FEATURE_CPU_RECONFIG
#endif

#undef _VSTORE_C_STATIC
#if !defined(OPTION_NO_INLINE_VSTORE)
 #define _VSTORE_C_STATIC static inline
#else
 #define _VSTORE_C_STATIC
#endif

#undef _DAT_C_STATIC
#if !defined(OPTION_NO_INLINE_DAT)
 #define _DAT_C_STATIC static inline
#else
 #define _DAT_C_STATIC
#endif

#else /*!defined(FEATCHK_CHECK_ALL)*/

#if !defined(FEATURE_2K_STORAGE_KEYS) \
 && !defined(FEATURE_4K_STORAGE_KEYS)
 #error Storage keys must be 2K or 4K
#endif

#if defined(FEATURE_EXPANDED_STORAGE) \
 && !defined(FEATURE_4K_STORAGE_KEYS)
 #error Expanded storage cannot be defined with 2K storage keys
#endif

#if defined(FEATURE_MOVE_PAGE_FACILITY_2) \
 && !defined(FEATURE_4K_STORAGE_KEYS)
 #error Move page facility cannot be defined with 2K storage keys
#endif

#if defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE) \
 && !defined(FEATURE_INTERPRETIVE_EXECUTION)
 #error MCDS only supported with SIE
#endif

#if defined(FEATURE_MULTIPLE_CONTROLLED_DATA_SPACE) \
 && !defined(FEATURE_STORAGE_KEY_ASSIST)
 #error MCDS requires storage key assist
#endif

#if defined(FEATURE_STORAGE_KEY_ASSIST) \
 && !defined(FEATURE_INTERPRETIVE_EXECUTION)
 #error Storage Key assist only supported with SIE
#endif

#if defined(FEATURE_ESAME) \
 && defined(FEATURE_VECTOR_FACILITY)
 #error Vector Facility not supported in ESAME mode
#endif

#if defined(FEATURE_BINARY_FLOATING_POINT) \
 && !defined(FEATURE_BASIC_FP_EXTENSIONS)
 #error Binary floating point requires basic FP extensions
#endif

#if !defined(FEATURE_BASIC_FP_EXTENSIONS)
 #if defined(FEATURE_BINARY_FLOATING_POINT) \
  || defined(FEATURE_HFP_EXTENSIONS) \
  || defined(FEATURE_FPS_EXTENSIONS)
  #error Floating point extensions require basic FP extensions
 #endif
#endif

#endif /*!defined(FEATALL_CHECKALL)*/

/* end of FEATCHK.H */
