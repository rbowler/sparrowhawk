/* ESA390.H	(c) Copyright Roger Bowler, 1994-2001		     */
/*		ESA/390 Data Areas				     */

/* Interpretive Execution - (c) Copyright Jan Jaeger, 1999-2001      */

/*-------------------------------------------------------------------*/
/* Header file containing ESA/390 structure definitions 	     */
/*-------------------------------------------------------------------*/

/* Platform-independent storage operand definitions */
typedef u_int8_t	BYTE;
typedef u_int8_t	HWORD[2];
typedef u_int8_t	FWORD[4];
typedef u_int8_t	DWORD[8];
typedef u_int16_t	U16;
typedef int16_t 	S16;
typedef u_int32_t	U32;
typedef int32_t 	S32;
typedef u_int64_t	U64;
typedef int64_t 	S64;

/* Internal-format PSW structure definition */
typedef struct _PSW {
	unsigned int
		prob:1, 		/* 1=Problem state	     */
		wait:1, 		/* 1=Wait state 	     */
		mach:1, 		/* 1=Machine check enabled   */
		ecmode:1,		/* 1=ECMODE, 0=BCMODE	     */
		sgmask:1,		/* Significance mask	     */
		eumask:1,		/* Exponent underflow mask   */
		domask:1,		/* Decimal overflow mask     */
		fomask:1,		/* Fixed-point overflow mask */
		armode:1,		/* Access-register mode      */
		space:1,		/* Secondary-space mode      */
		amode:1;		/* Addressing mode	     */
	BYTE	sysmask;		/* System mask		     */
	BYTE	pkey;			/* Bits 0-3=key, 4-7=zeroes  */
	BYTE	ilc;			/* Instruction length code   */
	BYTE	cc;			/* Condition code	     */
	U16	intcode;		/* Interruption code	     */
	U32	ia;			/* Instruction address	     */
    } PSW;

/* Bit definitions for ECMODE PSW system mask */
#define PSW_PERMODE	0x40		/* Program event recording   */
#define PSW_DATMODE	0x04		/* Dynamic addr translation  */
#define PSW_IOMASK	0x02		/* I/O interrupt mask	     */
#define PSW_EXTMASK	0x01		/* External interrupt mask   */

/* Macros for testing addressing mode */
#define REAL_MODE(p) \
	((p)->ecmode==0 || ((p)->sysmask & PSW_DATMODE)==0)
#define PRIMARY_SPACE_MODE(p) \
	((p)->space==0 && (p)->armode==0)
#define SECONDARY_SPACE_MODE(p) \
	((p)->space==1 && (p)->armode==0)
#define ACCESS_REGISTER_MODE(p) \
	((p)->space==0 && (p)->armode==1)
#define HOME_SPACE_MODE(p) \
	((p)->space==1 && (p)->armode==1)

/* Macro for converting a real address to an absolute address */
#define APPLY_PREFIXING(addr,pfx) \
	((((addr)&0x7FFFF000)==0)?((addr)&0xFFF)|pfx:\
	(((addr)&0x7FFFF000)==pfx)?(addr)&0xFFF:(addr))

/* Structure definition for translation-lookaside buffer entry */
typedef struct _TLBE {
	U32	std;			/* Segment table designation */
	U32	vaddr;			/* Virtual page address      */
	U32	pte;			/* Copy of page table entry  */
	BYTE	valid;			/* 1=TLB entry is valid      */
	BYTE	common; 		/* 1=Page in common segment  */
	BYTE	resv[2];		/* Padding for alignment     */
    } TLBE;

/* Bit definitions for control register 0 */
#define CR0_BMPX	0x80000000	/* Block multiplex ctl	S/370*/
#define CR0_SSM_SUPP	0x40000000	/* SSM suppression control   */
#define CR0_TOD_SYNC	0x20000000	/* TOD clock sync control    */
#define CR0_LOW_PROT	0x10000000	/* Low address protection    */
#define CR0_EXT_AUTH	0x08000000	/* Extraction auth control   */
#define CR0_SEC_SPACE	0x04000000	/* Secondary space control   */
#define CR0_FETCH_OVRD	0x02000000	/* Fetch protection override */
#define CR0_STORE_OVRD	0x01000000	/* Store protection override */
#define CR0_TRAN_FMT	0x00F80000	/* Translation format bits...*/
#define CR0_TRAN_ESA390 0x00B00000	/* ...1M/4K ESA/390 format   */
#define CR0_PAGE_SIZE	0x00C00000	/* Page size for S/370...    */
#define CR0_PAGE_SZ_2K	0x00400000	/* ...2K pages		     */
#define CR0_PAGE_SZ_4K	0x00800000	/* ...4K pages		     */
#define CR0_SEG_SIZE	0x00380000	/* Segment size for S/370... */
#define CR0_SEG_SZ_64K	0x00000000	/* ...64K segments	     */
#define CR0_SEG_SZ_1M	0x00100000	/* ...1M segments	     */
#define CR0_VOP 	0x00020000	/* Vector-Operation Control  */
#define CR0_ASF 	0x00010000	/* Addrspc function control  */
#define CR0_XM_MALFALT	0x00008000	/* Malfunction alert mask    */
#define CR0_XM_EMERSIG	0x00004000	/* Emergency signal mask     */
#define CR0_XM_EXTCALL	0x00002000	/* External call mask	     */
#define CR0_XM_TODSYNC	0x00001000	/* TOD clock sync mask	     */
#define CR0_XM_CLKC	0x00000800	/* Clock comparator mask     */
#define CR0_XM_PTIMER	0x00000400	/* CPU timer mask	     */
#define CR0_XM_SERVSIG	0x00000200	/* Service signal mask	     */
#define CR0_XM_ITIMER	0x00000080	/* Interval timer mask	S/370*/
#define CR0_XM_INTKEY	0x00000040	/* Interrupt key mask	     */
#define CR0_XM_EXTSIG	0x00000020	/* External signal mask S/370*/
#define CR0_PC_FAST	0x00000008	/* PCF instruction control   */

/* Bit definitions for control register 1 */
/* CR1 is the primary segment table descriptor */

/* Bit definitions for control register 2 */
#define CR2_DUCTO	0x7FFFFFC0	/* DUCT origin		     */
/* For S/370, CR2 contains channel masks for channels 0-31 */

/* Bit definitions for control register 3 */
#define CR3_KEYMASK	0xFFFF0000	/* PSW key mask 	     */
#define CR3_SASN	0x0000FFFF	/* Secondary ASN	     */

/* Bit definitions for control register 4 */
#define CR4_AX		0xFFFF0000	/* Authorization index	     */
#define CR4_PASN	0x0000FFFF	/* Primary ASN		     */

/* Bit definitions for control register 5 */
					/* When CR0_ASF=0:	     */
#define CR5_SSLINK	0x80000000	/* Subsystem-Linkage control */
#define CR5_LTO 	0x7FFFFF80	/* Linkage-Table origin      */
#define CR5_LTL 	0x0000007F	/* Linkage-Table length      */
					/* When CR0_ASF=1:	     */
#define CR5_PASTEO	0x7FFFFFC0	/* Primary-ASTE origin	     */

/* Bit definitions for control register 6 */
/* CR6 is the I/O interruption subclass mask */

/* Bit definitions for control register 7 */
/* CR7 is the secondary segment table descriptor */

/* Bit definitions for control register 8 */
#define CR8_EAX 	0xFFFF0000	/* Extended auth index	     */
#define CR8_MCMASK	0x0000FFFF	/* Monitor masks	     */

/* Bit definitions for PER */
#define CR9_SB          0x80000000      /* Successfull Branching     */
#define CR9_IF          0x40000000      /* Instruction Fetch         */
#define CR9_SA          0x20000000      /* Storage Alteration        */
#define CR9_GRA         0x10000000      /* General Register Alt.     */
#define CR9_STURA       0x08000000      /* Store using real addr     */
#define CR9_GRMASK      0x0000FFFF      /* GR mask bits              */

/* Bit definitions for control register 12 */
#define CR12_BRTRACE	0x80000000	/* Branch trace control      */
#define CR12_TRACEEA	0x7FFFFFFC	/* Trace entry address	     */
#define CR12_ASNTRACE	0x00000002	/* ASN trace control	     */
#define CR12_EXTRACE	0x00000001	/* Explicit trace control    */

/* Bit definitions for control register 13 */
/* CR13 is the home segment table descriptor */

/* Bit definitions for control register 14 */
#define CR14_CHKSTOP	0x80000000	/* Check-stop control	S/370*/
#define CR14_SYNCMCEL	0x40000000	/* Synchronous MCEL	S/370*/
#define CR14_IOEXTLOG	0x20000000	/* I/O extended logout	S/370*/
#define CR14_CHANRPT	0x10000000	/* Channel report mask	     */
#define CR14_RCVYRPT	0x08000000	/* Recovery report mask      */
#define CR14_DGRDRPT	0x04000000	/* Degradation report mask   */
#define CR14_XDMGRPT	0x02000000	/* External damage mask      */
#define CR14_WARNING	0x01000000	/* Warning mask 	     */
#define CR14_ASYNMCEL	0x00800000	/* Asynchronous MCEL	S/370*/
#define CR14_ASYNFIXL	0x00400000	/* Asynch fixed log	S/370*/
#define CR14_TODCTLOV	0x00200000	/* TOD clock control override*/
#define CR14_ASN_TRAN	0x00080000	/* ASN translation control   */
#define CR14_AFTO	0x0007FFFF	/* ASN first table origin    */

/* Bit definitions for control register 15 */
#define CR15_LSEA	0x7FFFFFF8	/* Linkage stack entry addr  */
#define CR15_MCEL	0x00FFFFF8	/* MCEL address 	S/370*/

/* Linkage table designation bit definitions */
#define LTD_SSLINK	0x80000000	/* Subsystem-Linkage control */
#define LTD_LTO 	0x7FFFFF80	/* Linkage-Table origin      */
#define LTD_LTL 	0x0000007F	/* Linkage-Table length      */

/* Segment table designation bit definitions (ESA/390 mode) */
#define STD_SSEVENT	0x80000000	/* Space switch event	     */
#define STD_STO 	0x7FFFF000	/* Segment table origin      */
#define STD_RESV	0x00000C00	/* Reserved bits - must be 0 */
#define STD_GROUP	0x00000200	/* Subspace group indicator  */
#define STD_PRIVATE	0x00000100	/* Private space indicator   */
#define STD_SAEVENT	0x00000080	/* Storage alteration event  */
#define STD_STL 	0x0000007F	/* Segment table length      */

/* Segment table designation bit definitions (S/370 mode) */
#define STD_370_STL	0xFF000000	/* 370 segment table length  */
#define STD_370_STO	0x00FFFFC0	/* 370 segment table origin  */
#define STD_370_SSEVENT 0x00000001	/* 370 space switch event    */

/* Segment table entry bit definitions (ESA/390 mode) */
#define SEGTAB_PTO	0x7FFFFFC0	/* Page table origin	     */
#define SEGTAB_INVALID	0x00000020	/* Invalid segment	     */
#define SEGTAB_COMMON	0x00000010	/* Common segment	     */
#define SEGTAB_PTL	0x0000000F	/* Page table length	     */
#define SEGTAB_RESV	0x80000000	/* Reserved bits - must be 0 */

/* Segment table entry bit definitions (S/370 mode) */
#define SEGTAB_370_PTL	0xF0000000	/* Page table length	     */
#define SEGTAB_370_PTO	0x00FFFFF8	/* Page table origin	     */
#define SEGTAB_370_PROT 0x00000004	/* Protected segment	     */
#define SEGTAB_370_CMN	0x00000002	/* Common segment	     */
#define SEGTAB_370_INVL 0x00000001	/* Invalid segment	     */
#define SEGTAB_370_RSV	0x0F000000	/* Reserved bits - must be 0 */

/* Page table entry bit definitions (ESA/390 mode) */
#define PAGETAB_PFRA	0x7FFFF000	/* Page frame real address   */
#define PAGETAB_INVALID 0x00000400	/* Invalid page 	     */
#define PAGETAB_PROT	0x00000200	/* Protected page	     */
#define PAGETAB_ESVALID 0x00000100	/* Valid in expanded storage */
#define PAGETAB_PGLOCK  0x00000001      /* Page lock (LKPG)          */
#define PAGETAB_RESV	0x80000900	/* Reserved bits - must be 0 */

/* Page table entry bit definitions (S/370 mode) */
#define PAGETAB_PFRA_4K 0xFFF0		/* Page frame real address   */
#define PAGETAB_INV_4K	0x0008		/* Invalid page 	     */
#define PAGETAB_EA_4K	0x0006		/* Extended physical address */
#define PAGETAB_PFRA_2K 0xFFF8		/* Page frame real address   */
#define PAGETAB_INV_2K	0x0004		/* Invalid page 	     */
#define PAGETAB_RSV_2K	0x0002		/* Reserved bit - must be 0  */

/* Access-list entry token special value definitions */
#define ALET_PRIMARY	0		/* Primary address-space     */
#define ALET_SECONDARY	1		/* Secondary address-space   */
#define ALET_HOME	2		/* Home address-space	     */

/* Access-list entry token bit definitions */
#define ALET_RESV	0xFE000000	/* Reserved bits - must be 0 */
#define ALET_PRI_LIST	0x01000000	/* Primary space access-list */
#define ALET_ALESN	0x00FF0000	/* ALE sequence number	     */
#define ALET_ALEN	0x0000FFFF	/* Access-list entry number  */

/* Access-list designation bit definitions */
#if FEATURE_ALD_FORMAT == 0
#define ALD_ALO 	0x7FFFFF80	/* Access-list origin (fmt0) */
#define ALD_ALL 	0x0000007F	/* Access-list length (fmt0) */
#define ALD_ALL_SHIFT	3		/* Length units are 2**3     */
#else
#define ALD_ALO 	0x7FFFFF00	/* Access-list origin (fmt1) */
#define ALD_ALL 	0x000000FF	/* Access-list length (fmt1) */
#define ALD_ALL_SHIFT	4		/* Length units are 2**4     */
#endif

/* Access-list entry bit definitions */
#define ALE0_INVALID	0x80000000	/* ALEN invalid 	     */
#define ALE0_FETCHONLY	0x02000000	/* Fetch only address space  */
#define ALE0_PRIVATE	0x01000000	/* Private address space     */
#define ALE0_ALESN	0x00FF0000	/* ALE sequence number	     */
#define ALE0_ALEAX	0x0000FFFF	/* ALE authorization index   */
#define ALE2_ASTE	0x7FFFFFC0	/* ASTE address 	     */
#define ALE3_ASTESN	0xFFFFFFFF	/* ASTE sequence number      */

/* Address-space number (ASN) bit definitions */
#define ASN_AFX 	0xFFC0		/* ASN first table index     */
#define ASN_ASX 	0x003F		/* ASN second table index    */

/* ASN first table entry bit definitions */
#define AFTE_INVALID	0x80000000	/* ASN invalid		     */
#define AFTE_ASTO_0	0x7FFFFFF0	/* ASTE origin (CR0_ASF=0)   */
#define AFTE_RESV_0	0x0000000F	/* Reserved bits (CR0_ASF=0) */
#define AFTE_ASTO_1	0x7FFFFFC0	/* ASTE origin (CR0_ASF=1)   */
#define AFTE_RESV_1	0x0000003F	/* Reserved bits (CR0_ASF=1) */

/* ASN second table entry bit definitions */
#define ASTE0_INVALID	0x80000000	/* ASX invalid		     */
#define ASTE0_ATO	0x7FFFFFFC	/* Authority-table origin    */
#define ASTE0_RESV	0x00000002	/* Reserved bits - must be 0 */
#define ASTE0_BASE	0x00000001	/* Base space of group	     */
#define ASTE1_AX	0xFFFF0000	/* Authorization index	     */
#define ASTE1_ATL	0x0000FFF0	/* Authority-table length    */
#define ASTE1_RESV	0x0000000F	/* Reserved bits - must be 0 */
/* ASTE word 2 is the segment table designation */
/* ASTE word 3 is the linkage-table designation */
/* ASTE word 4 is the access-list designation */
#define ASTE5_ASTESN	0xFFFFFFFF	/* ASTE sequence number      */
#define ASTE6_RESV	0xFFFFFFFF	/* Reserved bits - must be 0 */
/* ASTE word 7 is unused */

/* Authority table entry bit definitions */
#define ATE_PRIMARY	0x80		/* Primary authority bit     */
#define ATE_SECONDARY	0x40		/* Secondary authority bit   */

/* Dispatchable unit control table bit definitions */
#define DUCT0_BASTEO	0x7FFFFFC0	/* Base ASTE origin	     */
#define DUCT1_SA	0x80000000	/* Subspace active	     */
#define DUCT1_SSASTEO	0x7FFFFFC0	/* Subspace ASTE origin      */
/* DUCT word 2 is unused */
#define DUCT3_SSASTESN	0xFFFFFFFF	/* Subspace ASTE seq number  */
/* DUCT word 4 is the access-list designation */
/* DUCT word 5 is unused */
/* DUCT word 6 is unused */
/* DUCT word 7 is for control program use */
#define DUCT8_AMODE	0x80000000	/* Addressing mode	     */
#define DUCT8_IA	0x7FFFFFFF	/* Return address	     */
#define DUCT9_PKM	0xFFFF0000	/* PSW key mask 	     */
#define DUCT9_KEY	0x000000F0	/* PSW key		     */
#define DUCT9_RA	0x00000008	/* Reduced authority state   */
#define DUCT9_PROB	0x00000001	/* Problem state	     */
/* DUCT word 10 is unused */
#define DUCT11_TCBA	0x7FFFFFF8	/* Trap control block address*/
#define DUCT11_TE	0x00000001	/* Trap enabled 	     */
/* DUCT word 12 is unused */
/* DUCT word 13 is unused */
/* DUCT word 14 is unused */
/* DUCT word 15 is unused */

/* Linkage stack entry descriptor structure definition */
typedef struct _LSED {
	BYTE	uet;			/* U-bit and entry type      */
	BYTE	si;			/* Section identification    */
	HWORD	rfs;			/* Remaining free space      */
	HWORD	nes;			/* Next entry size	     */
	HWORD	resv;			/* Reserved bits - must be 0 */
    } LSED;

/* Linkage stack entry descriptor bit definitions */
#define LSED_UET_U	0x80		/* Unstack suppression bit   */
#define LSED_UET_ET	0x7F		/* Entry type...	     */
#define LSED_UET_HDR	0x01		/* ...header entry	     */
#define LSED_UET_TLR	0x02		/* ...trailer entry	     */
#define LSED_UET_BAKR	0x04		/* ...branch state entry     */
#define LSED_UET_PC	0x05		/* ...call state entry	     */

/* Linkage stack header entry bit definitions */
/* LSHE word 0 is reserved for control program use */
#define LSHE1_BVALID	0x80000000	/* Backward address is valid */
#define LSHE1_BSEA	0x7FFFFFF8	/* Backward stack entry addr */
#define LSHE1_RESV	0x00000007	/* Reserved bits - must be 0 */
/* LSHE words 2 and 3 contain a linkage stack entry descriptor */

/* Linkage stack trailer entry bit definitions */
/* LSTE word 0 is reserved for control program use */
#define LSTE1_FVALID	0x80000000	/* Forward address is valid  */
#define LSTE1_FSHA	0x7FFFFFF8	/* Forward section hdr addr  */
#define LSTE1_RESV	0x00000007	/* Reserved bits - must be 0 */
/* LSTE words 2 and 3 contain a linkage stack entry descriptor */

/* Linkage stack state entry definitions */
#define LSSE_SIZE	168		/* Size of linkage stack state
					   entry (incl. descriptor)  */

/* Program call number bit definitions */
#define PC_LX		0x000FFF00	/* Linkage index	     */
#define PC_EX		0x000000FF	/* Entry index		     */

/* Linkage table entry bit definitions */
#define LTE_INVALID	0x80000000	/* LX invalid		     */
#define LTE_ETO 	0x7FFFFFC0	/* Entry table origin	     */
#define LTE_ETL 	0x0000003F	/* Entry table length	     */

/* Entry table bit entry definitions */
#define ETE0_AKM	0xFFFF0000	/* Authorization key mask    */
#define ETE0_ASN	0x0000FFFF	/* Address space number      */
#define ETE1_AMODE	0x80000000	/* Addessing mode	     */
#define ETE1_EIA	0x7FFFFFFE	/* Instruction address	     */
#define ETE1_PROB	0x00000001	/* Problem state bit	     */
/* ETE word 2 is the entry parameter */
#define ETE3_EKM	0xFFFF0000	/* Entry key mask	     */
#define ETE4_T		0x80000000	/* 0=Basic PC, 1=Stacking PC */
#define ETE4_K		0x10000000	/* 1=Replace PSW key by EK   */
#define ETE4_M		0x08000000	/* 1=Replace PKM by EKM, 0=or*/
#define ETE4_E		0x04000000	/* 1=Replace EAX by EEAX     */
#define ETE4_C		0x02000000	/* 0=Primary mode, 1=AR mode */
#define ETE4_S		0x01000000	/* SASN:0=old PASN,1=new PASN*/
#define ETE4_EK 	0x00F00000	/* Entry key		     */
#define ETE4_EEAX	0x0000FFFF	/* Entry extended AX	     */
#define ETE5_ASTE	0x7FFFFFC0	/* ASTE address 	     */
/* ETE words 6 and 7 are unused */

/* SIGP order codes */
#define SIGP_SENSE	0x01		/* Sense		     */
#define SIGP_EXTCALL	0x02		/* External call	     */
#define SIGP_EMERGENCY	0x03		/* Emergency signal	     */
#define SIGP_START	0x04		/* Start		     */
#define SIGP_STOP	0x05		/* Stop 		     */
#define SIGP_RESTART	0x06		/* Restart		     */
#define SIGP_IPR	0x07		/* Initial program reset 370 */
#define SIGP_PR 	0x08		/* Program reset	 370 */
#define SIGP_STOPSTORE	0x09		/* Stop and store status     */
#define SIGP_IMPL	0x0A		/* Initial uprogram load 370 */
#define SIGP_INITRESET	0x0B		/* Initial CPU reset	     */
#define SIGP_RESET	0x0C		/* CPU reset		     */
#define SIGP_SETPREFIX	0x0D		/* Set prefix		     */
#define SIGP_STORE	0x0E		/* Store status at address   */
#define SIGP_STOREX	0x11		/* Store ext status at addr  */

/* SIGP status codes */
#define SIGP_STATUS_EQUIPMENT_CHECK		0x80000000
#define SIGP_STATUS_INCORRECT_STATE		0x00000200
#define SIGP_STATUS_INVALID_PARAMETER		0x00000100
#define SIGP_STATUS_EXTERNAL_CALL_PENDING	0x00000080
#define SIGP_STATUS_STOPPED			0x00000040
#define SIGP_STATUS_OPERATOR_INTERVENING	0x00000020
#define SIGP_STATUS_CHECK_STOP			0x00000010
#define SIGP_STATUS_INOPERATIVE 		0x00000004
#define SIGP_STATUS_INVALID_ORDER		0x00000002
#define SIGP_STATUS_RECEIVER_CHECK		0x00000001

/* Storage key bit definitions */
#define STORKEY_KEY	0xF0		/* Storage key		     */
#define STORKEY_FETCH	0x08		/* Fetch protect bit	     */
#define STORKEY_REF	0x04		/* Reference bit	     */
#define STORKEY_CHANGE	0x02		/* Change bit		     */
#define STORKEY_BADFRM	0x01		/* Unusable frame	     */

/* Prefixed storage area structure definition */
typedef struct _PSA {			/* Prefixed storage area     */
/*000*/ DWORD iplpsw;			/* IPL PSW, Restart new PSW  */
/*008*/ DWORD iplccw1;			/* IPL CCW1, Restart old PSW */
/*010*/ DWORD iplccw2;			/* IPL CCW2		     */
/*018*/ DWORD extold;			/* External old PSW	     */
/*020*/ DWORD svcold;			/* SVC old PSW		     */
/*028*/ DWORD pgmold;			/* Program check old PSW     */
/*030*/ DWORD mckold;			/* Machine check old PSW     */
/*038*/ DWORD iopold;			/* I/O old PSW		     */
/*040*/ DWORD csw;			/* Channel status word (S370)*/
/*048*/ FWORD caw;			/* Channel address word(S370)*/
/*04C*/ FWORD resv04C;			/* Reserved		     */
/*050*/ DWORD inttimer; 		/* Interval timer	     */
/*058*/ DWORD extnew;			/* External new PSW	     */
/*060*/ DWORD svcnew;			/* SVC new PSW		     */
/*068*/ DWORD pgmnew;			/* Program check new PSW     */
/*070*/ DWORD mcknew;			/* Machine check new PSW     */
/*078*/ DWORD iopnew;			/* I/O new PSW		     */
/*080*/ FWORD extparm;			/* External interrupt param  */
/*084*/ HWORD extcpad;			/* External interrupt CPU#   */
/*086*/ HWORD extint;			/* External interrupt code   */
/*088*/ FWORD svcint;			/* SVC interrupt code	     */
/*08C*/ FWORD pgmint;			/* Program interrupt code    */
/*090*/ FWORD tea;			/* Translation exception addr*/
/*094*/ HWORD monclass; 		/* Monitor class	     */
/*096*/ HWORD perint;			/* PER interrupt code	     */
/*098*/ FWORD peradr;			/* PER address		     */
/*09C*/ FWORD moncode;			/* Monitor code 	     */
/*0A0*/ BYTE  excarid;			/* Exception access id	     */
/*0A1*/ BYTE  perarid;			/* PER access id	     */
/*0A2*/ HWORD resv0A2;			/* Reserved		     */
/*0A4*/ FWORD resv0A4;			/* Reserved		     */
/*0A8*/ FWORD chanid;			/* Channel id (S370)	     */
/*0AC*/ FWORD ioelptr;			/* I/O extended logout (S370)*/
/*0B0*/ FWORD lcl;			/* Limited chan logout (S370)*/
/*0B4*/ FWORD resv0B0;			/* Reserved		     */
/*0B8*/ FWORD ioid;			/* I/O interrupt device id   */
/*0BC*/ FWORD ioparm;			/* I/O interrupt parameter   */
/*0C0*/ DWORD resv0C0;			/* Reserved		     */
/*0C8*/ DWORD resv0C8;			/* Reserved		     */
/*0D0*/ DWORD resv0D0;			/* Reserved		     */
/*0D8*/ DWORD storeptmr;		/* CPU timer save area	     */
/*0E0*/ DWORD storeclkc;		/* Clock comparator save area*/
/*0E8*/ DWORD mckint;			/* Machine check int code    */
/*0F0*/ FWORD resv0F0;			/* Reserved		     */
/*0F4*/ FWORD xdmgcode; 		/* External damage code      */
/*0F8*/ FWORD mcstorad; 		/* Failing storage address   */
/*0FC*/ FWORD resv0FC;			/* Reserved		     */
/*100*/ DWORD storepsw; 		/* Store status PSW save area*/
/*108*/ FWORD storepfx; 		/* Prefix register save area */
/*10C*/ FWORD resv10C;			/* Reserved		     */
/*110*/ DWORD resv110;			/* Reserved		     */
/*118*/ DWORD resv118;			/* Reserved		     */
/*120*/ FWORD storear[16];		/* Access register save area */
/*160*/ FWORD storefpr[8];		/* FP register save area     */
/*180*/ FWORD storegpr[16];		/* General register save area*/
/*1C0*/ FWORD storecr[16];		/* Control register save area*/
} PSA;

/* Bit settings for translation exception address */
#define TEA_SECADDR	0x80000000	/* Secondary address	     */
#define TEA_EFFADDR	0x7FFFF000	/* Effective address	     */
#define TEA_PROT_AP	0x00000004	/* Access-list/page protected*/
#define TEA_ST		0x00000003	/* Segment table indication..*/
#define TEA_ST_PRIMARY	0x00000000	/* ..primary segment table   */
#define TEA_ST_ARMODE	0x00000001	/* ..access register mode    */
#define TEA_ST_SECNDRY	0x00000002	/* ..secondary segment table */
#define TEA_ST_HOME	0x00000003	/* ..home segment table      */
#define TEA_SSEVENT	0x80000000	/* Space switch event bit    */
#define TEA_ASN 	0x0000FFFF	/* Address space number      */
#define TEA_PCN 	0x000FFFFF	/* Program call number	     */

/* Bit settings for machine check interruption code */
#define MCIC_SD  0x8000000000000000ULL	/* System damage	     */
#define MCIC_P	 0x4000000000000000ULL	/* Instruction proc damage   */
#define MCIC_SR  0x2000000000000000ULL	/* System recovery	     */
#define MCIC_CD  0x0800000000000000ULL	/* Timing facility damage    */
#define MCIC_ED  0x0400000000000000ULL	/* External damage	     */
#define MCIC_VF  0x0200000000000000ULL	/* Vector facility failure   */
#define MCIC_DG  0x0100000000000000ULL	/* Degradation		     */

#define MCIC_W	 0x0080000000000000ULL	/* Warning		     */
#define MCIC_CP  0x0040000000000000ULL	/* Channel report pending    */
#define MCIC_SP  0x0020000000000000ULL	/* Service processor damage  */
#define MCIC_CK  0x0010000000000000ULL	/* Channel subsystem damage  */
#define MCIC_VS  0x0004000000000000ULL	/* Vector facility source    */
#define MCIC_B	 0x0002000000000000ULL	/* Backed up		     */

#define MCIC_SE  0x0000800000000000ULL	/* Storage error uncorrected */
#define MCIC_SC  0x0000400000000000ULL	/* Storage error corrected   */
#define MCIC_KE  0x0000200000000000ULL	/* Storkey error uncorrected */
#define MCIC_DS  0x0000100000000000ULL	/* Storage degradation	     */
#define MCIC_WP  0x0000080000000000ULL	/* PSW-MWP validity	     */
#define MCIC_MS  0x0000040000000000ULL	/* PSW mask and key validity */
#define MCIC_PM  0x0000020000000000ULL	/* PSW pm and cc validity    */
#define MCIC_IA  0x0000010000000000ULL	/* PSW ia validity	     */

#define MCIC_FA  0x0000008000000000ULL	/* Failing stor addr validity*/
#define MCIC_EC  0x0000002000000000ULL	/* External damage code val. */
#define MCIC_FP  0x0000001000000000ULL	/* Floating point reg val.   */
#define MCIC_GR  0x0000000800000000ULL	/* General register validity */
#define MCIC_CR  0x0000000400000000ULL	/* Control register validity */
#define MCIC_ST  0x0000000100000000ULL	/* Storage logical validity  */

#define MCIC_IE  0x0000000080000000ULL	/* Indirect storage error    */
#define MCIC_AR  0x0000000040000000ULL	/* Access register validity  */
#define MCIC_DA  0x0000000020000000ULL	/* Delayed access exeption   */

#define MCIC_XF  0x0000000000100000ULL	/* Extended float reg val.   */
#define MCIC_AP  0x0000000000080000ULL	/* Ancillary report	     */
#define MCIC_CT  0x0000000000020000ULL	/* CPU timer validity	     */
#define MCIC_CC  0x0000000000010000ULL	/* Clock comparator validity */

/* Channel Report Word definitions */
#define CRW_SOL 	0x40000000	/* Solicited CRW	     */
#define CRW_OVER	0x20000000	/* Overflow, CRW's lost      */
#define CRW_CHAIN	0x10000000	/* More CRW's describe event */
#define CRW_RSC 	0x0F000000	/* Reporting resource mask   */
#define CRW_MONIT	0x02000000	/* Channel monitor is source */
#define CRW_SUBCH	0x03000000	/* Subchannel is source      */
#define CRW_CHPID	0x04000000	/* Channel path is source    */
#define CRW_CAF 	0x09000000	/* Configuration alert	     */
#define CRW_CSS 	0x0B000000	/* Channel subsys is source  */
#define CRW_AR		0x00800000	/* Ancillary report indicator*/
#define CRW_ERC 	0x003F0000	/* Error recovery code	     */
#define CRW_AVAIL	0x00010000	/* Available		     */
#define CRW_INIT	0x00020000	/* Initialized no parm. chg. */
#define CRW_TEMP	0x00030000	/* Temporary error	     */
#define CRW_ALERT	0x00040000	/* Installed, subch changed  */
#define CRW_TERM	0x00050000	/* Terminal		     */
#define CRW_PERM	0x00060000	/* Permanent error / not init*/
#define CRW_PERMI	0x00070000	/* Permanent, initialized    */
#define CRW_IPM 	0x00080000	/* PIM / PAM / CHPIDs changed*/
#define CRW_RSID	0x0000FFFF	/* Resource identifier	     */

/* Bit settings for channel id */
#define CHANNEL_TYPE	0xF0000000	/* Bits 0-3=Channel type...  */
#define CHANNEL_SEL	0x00000000	/* ...selector channel	     */
#define CHANNEL_MPX	0x10000000	/* ...byte multiplexor	     */
#define CHANNEL_BMX	0x20000000	/* ...block multiplexor      */
#define CHANNEL_MODEL	0x0FFF0000	/* Bits 4-15=Channel model   */
#define CHANNEL_MAXIOEL 0x0000FFFF	/* Bits 16-31=Max.IOEL length*/

/* Program interruption codes */
#define PGM_OPERATION_EXCEPTION 			0x0001
#define PGM_PRIVILEGED_OPERATION_EXCEPTION		0x0002
#define PGM_EXECUTE_EXCEPTION				0x0003
#define PGM_PROTECTION_EXCEPTION			0x0004
#define PGM_ADDRESSING_EXCEPTION			0x0005
#define PGM_SPECIFICATION_EXCEPTION			0x0006
#define PGM_DATA_EXCEPTION				0x0007
#define PGM_FIXED_POINT_OVERFLOW_EXCEPTION		0x0008
#define PGM_FIXED_POINT_DIVIDE_EXCEPTION		0x0009
#define PGM_DECIMAL_OVERFLOW_EXCEPTION			0x000A
#define PGM_DECIMAL_DIVIDE_EXCEPTION			0x000B
#define PGM_EXPONENT_OVERFLOW_EXCEPTION 		0x000C
#define PGM_EXPONENT_UNDERFLOW_EXCEPTION		0x000D
#define PGM_SIGNIFICANCE_EXCEPTION			0x000E
#define PGM_FLOATING_POINT_DIVIDE_EXCEPTION		0x000F
#define PGM_SEGMENT_TRANSLATION_EXCEPTION		0x0010
#define PGM_PAGE_TRANSLATION_EXCEPTION			0x0011
#define PGM_TRANSLATION_SPECIFICATION_EXCEPTION 	0x0012
#define PGM_SPECIAL_OPERATION_EXCEPTION 		0x0013
#define PGM_OPERAND_EXCEPTION				0x0015
#define PGM_TRACE_TABLE_EXCEPTION			0x0016
#define PGM_ASN_TRANSLATION_SPECIFICATION_EXCEPTION	0x0017
#define PGM_VECTOR_OPERATION_EXCEPTION			0x0019
#define PGM_SPACE_SWITCH_EVENT				0x001C
#define PGM_SQUARE_ROOT_EXCEPTION			0x001D
#define PGM_UNNORMALIZED_OPERAND_EXCEPTION		0x001E
#define PGM_PC_TRANSLATION_SPECIFICATION_EXCEPTION	0x001F
#define PGM_AFX_TRANSLATION_EXCEPTION			0x0020
#define PGM_ASX_TRANSLATION_EXCEPTION			0x0021
#define PGM_LX_TRANSLATION_EXCEPTION			0x0022
#define PGM_EX_TRANSLATION_EXCEPTION			0x0023
#define PGM_PRIMARY_AUTHORITY_EXCEPTION 		0x0024
#define PGM_SECONDARY_AUTHORITY_EXCEPTION		0x0025
#define PGM_ALET_SPECIFICATION_EXCEPTION		0x0028
#define PGM_ALEN_TRANSLATION_EXCEPTION			0x0029
#define PGM_ALE_SEQUENCE_EXCEPTION			0x002A
#define PGM_ASTE_VALIDITY_EXCEPTION			0x002B
#define PGM_ASTE_SEQUENCE_EXCEPTION			0x002C
#define PGM_EXTENDED_AUTHORITY_EXCEPTION		0x002D
#define PGM_STACK_FULL_EXCEPTION			0x0030
#define PGM_STACK_EMPTY_EXCEPTION			0x0031
#define PGM_STACK_SPECIFICATION_EXCEPTION		0x0032
#define PGM_STACK_TYPE_EXCEPTION			0x0033
#define PGM_STACK_OPERATION_EXCEPTION			0x0034
#define PGM_MONITOR_EVENT				0x0040
#define PGM_PER_EVENT					0x0080

/* External interrupt codes */
#define EXT_INTERRUPT_KEY_INTERRUPT			0x0040
#define EXT_INTERVAL_TIMER_INTERRUPT			0x0080
#define EXT_TOD_CLOCK_SYNC_CHECK_INTERRUPT		0x1003
#define EXT_CLOCK_COMPARATOR_INTERRUPT			0x1004
#define EXT_CPU_TIMER_INTERRUPT 			0x1005
#define EXT_MALFUNCTION_ALERT_INTERRUPT 		0x1200
#define EXT_EMERGENCY_SIGNAL_INTERRUPT			0x1201
#define EXT_EXTERNAL_CALL_INTERRUPT			0x1202
#define EXT_SERVICE_SIGNAL_INTERRUPT			0x2401

/* Macros for classifying CCW operation codes */
#define IS_CCW_WRITE(c) 	(((c)&0x03)==0x01)
#define IS_CCW_READ(c)		(((c)&0x03)==0x02)
#define IS_CCW_CONTROL(c)	(((c)&0x03)==0x03)
#define IS_CCW_NOP(c)           ((c)==0x03)
#define IS_CCW_SENSE(c) 	(((c)&0x0F)==0x04)
#define IS_CCW_TIC(c)		(((c)&0x0F)==0x08)
#define IS_CCW_RDBACK(c)	(((c)&0x0F)==0x0C)

/* Operation request block structure definition */
typedef struct _ORB {
	FWORD	intparm;		/* Interruption parameter    */
	BYTE	flag4;			/* Flag byte 4		     */
	BYTE	flag5;			/* Flag byte 5		     */
	BYTE	lpm;			/* Logical path mask	     */
	BYTE	flag7;			/* Flag byte 7		     */
	FWORD	ccwaddr;		/* CCW address		     */
    } ORB;

/* Bit definitions for ORB flag byte 4 */
#define ORB4_KEY	0xF0		/* Subchannel protection key */
#define ORB4_S		0x08		/* Suspend control	     */
#define ORB4_RESV	0x07		/* Reserved bits - must be 0 */

/* Bit definitions for ORB flag byte 5 */
#define ORB5_F		0x80		/* CCW format		     */
#define ORB5_P		0x40		/* Prefetch		     */
#define ORB5_I		0x20		/* Initial status interrupt  */
#define ORB5_A		0x10		/* Address limit checking    */
#define ORB5_U		0x08		/* Suppress susp interrupt   */
#define ORB5_RESV	0x07		/* Reserved bits - must be 0 */

/* Bit definitions for ORB flag byte 7 */
#define ORB7_L		0x80		/* Suppress incorrect length */
#define ORB7_RESV	0x7F		/* Reserved bits - must be 0 */

/* Path management control word structure definition */
typedef struct _PMCW {
	FWORD	intparm;		/* Interruption parameter    */
	BYTE	flag4;			/* Flag byte 4		     */
	BYTE	flag5;			/* Flag byte 5		     */
	HWORD	devnum; 		/* Device number	     */
	BYTE	lpm;			/* Logical path mask	     */
	BYTE	pnom;			/* Path not operational mask */
	BYTE	lpum;			/* Last path used mask	     */
	BYTE	pim;			/* Path installed mask	     */
	HWORD	mbi;			/* Measurement block index   */
	BYTE	pom;			/* Path operational mask     */
	BYTE	pam;			/* Path available mask	     */
	BYTE	chpid[8];		/* Channel path identifiers  */
	BYTE	flag24; 		/* Reserved byte - must be 0 */
	BYTE	flag25; 		/* Reserved byte - must be 0 */
	BYTE	flag26; 		/* Reserved byte - must be 0 */
	BYTE	flag27; 		/* Flag byte 27 	     */
    } PMCW;

/* Bit definitions for PMCW flag byte 4 */
#define PMCW4_ISC	0x38		/* Interruption subclass     */
#define PMCW4_RESV	0xC7		/* Reserved bits - must be 0 */

/* Bit definitions for PMCW flag byte 5 */
#define PMCW5_E 	0x80		/* Subchannel enabled	     */
#define PMCW5_LM	0x60		/* Limit mode...	     */
#define PMCW5_LM_NONE	0x00		/* ...no limit checking      */
#define PMCW5_LM_LOW	0x20		/* ...lower limit specified  */
#define PMCW5_LM_HIGH	0x40		/* ...upper limit specified  */
#define PMCW5_LM_RESV	0x60		/* ...reserved value	     */
#define PMCW5_MM	0x18		/* Measurement mode enable...*/
#define PMCW5_MM_MBU	0x10		/* ...meas.block.upd enabled */
#define PMCW5_MM_DCTM	0x08		/* Dev.conn.time.meas enabled*/
#define PMCW5_D 	0x04		/* Multipath mode enabled    */
#define PMCW5_T 	0x02		/* Timing facility available */
#define PMCW5_V 	0x01		/* Subchannel valid	     */

/* Bit definitions for PMCW flag byte 27 */
#define PMCW27_S	0x01		/* Concurrent sense mode     */
#define PMCW27_RESV	0xFE		/* Reserved bits - must be 0 */

/* Extended status word structure definition */
typedef struct _ESW {
	BYTE	scl0;			/* Subchannel logout byte 0  */
	BYTE	lpum;			/* Last path used mask	     */
	BYTE	scl2;			/* Subchannel logout byte 2  */
	BYTE	scl3;			/* Subchannel logout byte 3  */
	BYTE	erw0;			/* Extended report word byte0*/
	BYTE	erw1;			/* Extended report word byte1*/
	BYTE	erw2;			/* Extended report word byte2*/
	BYTE	erw3;			/* Extended report word byte3*/
	FWORD	failaddr;		/* Failing storage address   */
	FWORD	resv2;			/* Reserved word - must be 0 */
	FWORD	resv3;			/* Reserved word - must be 0 */
    } ESW;

/* Bit definitions for subchannel logout byte 0 */
#define SCL0_ESF	0x7F		/* Extended status flags...  */
#define SCL0_ESF_KEY	0x40		/* ...key check 	     */
#define SCL0_ESF_MBPGK	0x20		/* ...meas.block prog.check  */
#define SCL0_ESF_MBDCK	0x10		/* ...meas.block data check  */
#define SCL0_ESF_MBPTK	0x08		/* ...meas.block prot.check  */
#define SCL0_ESF_CCWCK	0x04		/* ...CCW check 	     */
#define SCL0_ESF_IDACK	0x02		/* ...IDAW check	     */

/* Bit definitions for subchannel logout byte 2 */
#define SCL2_R		0x80		/* Ancillary report bit      */
#define SCL2_FVF	0x7C		/* Field validity flags...   */
#define SCL2_FVF_LPUM	0x40		/* ...LPUM valid	     */
#define SCL2_FVF_TC	0x20		/* ...termination code valid */
#define SCL2_FVF_SC	0x10		/* ...sequence code valid    */
#define SCL2_FVF_USTAT	0x08		/* ...device status valid    */
#define SCL2_FVF_CCWAD	0x04		/* ...CCW address valid      */
#define SCL2_SA 	0x03		/* Storage access code...    */
#define SCL2_SA_UNK	0x00		/* ...access type unknown    */
#define SCL2_SA_RD	0x01		/* ...read		     */
#define SCL2_SA_WRT	0x02		/* ...write		     */
#define SCL2_SA_RDBK	0x03		/* ...read backward	     */

/* Bit definitions for subchannel logout byte 3 */
#define SCL3_TC 	0xC0		/* Termination code...	     */
#define SCL3_TC_HALT	0x00		/* ...halt signal issued     */
#define SCL3_TC_NORM	0x40		/* ...stop, stack, or normal */
#define SCL3_TC_CLEAR	0x80		/* ...clear signal issued    */
#define SCL3_TC_RESV	0xC0		/* ...reserved		     */
#define SCL3_D		0x20		/* Device status check	     */
#define SCL3_E		0x10		/* Secondary error	     */
#define SCL3_A		0x08		/* I/O error alert	     */
#define SCL3_SC 	0x07		/* Sequence code	     */

/* Bit definitions for extended report word byte 0 */
#define ERW0_A		0x10		/* Authorization check	     */
#define ERW0_P		0x08		/* Path verification required*/
#define ERW0_T		0x04		/* Channel path timeout      */
#define ERW0_F		0x02		/* Failing storage addr valid*/
#define ERW0_S		0x01		/* Concurrent sense	     */

/* Bit definitions for extended report word byte 1 */
#define ERW1_SCNT	0x3F		/* Concurrent sense count    */

/* Subchannel status word structure definition */
typedef struct _SCSW {
	BYTE	flag0;			/* Flag byte 0		     */
	BYTE	flag1;			/* Flag byte 1		     */
	BYTE	flag2;			/* Flag byte 2		     */
	BYTE	flag3;			/* Flag byte 3		     */
	FWORD	ccwaddr;		/* CCW address		     */
	BYTE	unitstat;		/* Device status	     */
	BYTE	chanstat;		/* Subchannel status	     */
	HWORD	count;			/* Residual byte count	     */
    } SCSW;

/* Bit definitions for SCSW flag byte 0 */
#define SCSW0_KEY	0xF0		/* Subchannel protection key */
#define SCSW0_S 	0x08		/* Suspend control	     */
#define SCSW0_L 	0x04		/* ESW format (logout stored)*/
#define SCSW0_CC	0x03		/* Deferred condition code...*/
#define SCSW0_CC_0	0x00		/* ...condition code 0	     */
#define SCSW0_CC_1	0x01		/* ...condition code 1	     */
#define SCSW0_CC_3	0x03		/* ...condition code 3	     */

/* Bit definitions for SCSW flag byte 1 */
#define SCSW1_F 	0x80		/* CCW format		     */
#define SCSW1_P 	0x40		/* Prefetch		     */
#define SCSW1_I 	0x20		/* Initial status interrupt  */
#define SCSW1_A 	0x10		/* Address limit checking    */
#define SCSW1_U 	0x08		/* Suppress susp interrupt   */
#define SCSW1_Z 	0x04		/* Zero condition code	     */
#define SCSW1_E 	0x02		/* Extended control	     */
#define SCSW1_N 	0x01		/* Path not operational      */

/* Bit definitions for SCSW flag byte 2 */
#define SCSW2_RESV	0x80		/* Reserved bit - must be 0  */
#define SCSW2_FC	0x70		/* Function control bits...  */
#define SCSW2_FC_START	0x40		/* ...start function	     */
#define SCSW2_FC_HALT	0x20		/* ...halt function	     */
#define SCSW2_FC_CLEAR	0x10		/* ...clear function	     */
#define SCSW2_AC	0x0F		/* Activity control bits...  */
#define SCSW2_AC_RESUM	0x08		/* ...resume pending	     */
#define SCSW2_AC_START	0x04		/* ...start pending	     */
#define SCSW2_AC_HALT	0x02		/* ...halt pending	     */
#define SCSW2_AC_CLEAR	0x01		/* ...clear pending	     */

/* Bit definitions for SCSW flag byte 3 */
#define SCSW3_AC	0xE0		/* Activity control bits...  */
#define SCSW3_AC_SCHAC	0x80		/* ...subchannel active      */
#define SCSW3_AC_DEVAC	0x40		/* ...device active	     */
#define SCSW3_AC_SUSP	0x20		/* ...suspended 	     */
#define SCSW3_SC	0x1F		/* Status control bits...    */
#define SCSW3_SC_ALERT	0x10		/* ...alert status	     */
#define SCSW3_SC_INTER	0x08		/* ...intermediate status    */
#define SCSW3_SC_PRI	0x04		/* ...primary status	     */
#define SCSW3_SC_SEC	0x02		/* ...secondary status	     */
#define SCSW3_SC_PEND	0x01		/* ...status pending	     */

/* CSW unit status flags */
#define CSW_ATTN	0x80		/* Attention		     */
#define CSW_SM		0x40		/* Status modifier	     */
#define CSW_CUE 	0x20		/* Control unit end	     */
#define CSW_BUSY	0x10		/* Busy 		     */
#define CSW_CE		0x08		/* Channel end		     */
#define CSW_DE		0x04		/* Device end		     */
#define CSW_UC		0x02		/* Unit check		     */
#define CSW_UX		0x01		/* Unit exception	     */

/* CSW channel status flags */
#define CSW_PCI 	0x80		/* Program control interrupt */
#define CSW_IL		0x40		/* Incorrect length	     */
#define CSW_PROGC	0x20		/* Program check	     */
#define CSW_PROTC	0x10		/* Protection check	     */
#define CSW_CDC 	0x08		/* Channel data check	     */
#define CSW_CCC 	0x04		/* Channel control check     */
#define CSW_ICC 	0x02		/* Interface control check   */
#define CSW_CHC 	0x01		/* Chaining check	     */

/* CCW flags */
#define CCW_FLAGS_CD	0x80		/* Chain data flag	     */
#define CCW_FLAGS_CC	0x40		/* Chain command flag	     */
#define CCW_FLAGS_SLI	0x20		/* Suppress incorrect length
					   indication flag	     */
#define CCW_FLAGS_SKIP	0x10		/* Skip flag		     */
#define CCW_FLAGS_PCI	0x08		/* Program controlled
					   interrupt flag	     */
#define CCW_FLAGS_IDA	0x04		/* Indirect data address flag*/
#define CCW_FLAGS_SUSP	0x02		/* Suspend flag 	     */
#define CCW_FLAGS_RESV	0x01		/* Reserved bit - must be 0  */

/* Device independent bit settings for sense byte 0 */
#define SENSE_CR	0x80		/* Command reject	     */
#define SENSE_IR	0x40		/* Intervention required     */
#define SENSE_BOC	0x20		/* Bus-out check	     */
#define SENSE_EC	0x10		/* Equipment check	     */
#define SENSE_DC	0x08		/* Data check		     */
#define SENSE_OR	0x04		/* Overrun		     */
#define SENSE_US	0x04		/* Unit specify 	     */
#define SENSE_CC	0x02		/* Control check	     */
#define SENSE_OC	0x01		/* Operation check	     */

/* Device dependent bit settings for sense byte 1 */
#define SENSE1_PER	0x80		/* Permanent Error	     */
#define SENSE1_ITF	0x40		/* Invalid Track Format      */
#define SENSE1_EOC	0x20		/* End of Cylinder	     */
#define SENSE1_MTO	0x10		/* Message to Operator	     */
#define SENSE1_NRF	0x08		/* No Record Found	     */
#define SENSE1_FP	0x04		/* File Protected	     */
#define SENSE1_WRI	0x02		/* Write Inhibited	     */
#define SENSE1_IE	0x01		/* Imprecise Ending	     */

/* Subchannel information block structure definition */
typedef struct _SCHIB {
	PMCW	pmcw;			/* Path management ctl word  */
	SCSW	scsw;			/* Subchannel status word    */
	BYTE	moddep[12];		/* Model dependent area      */
    } SCHIB;

/* Interruption response block structure definition */
typedef struct _IRB {
	SCSW	scsw;			/* Subchannel status word    */
	ESW	esw;			/* Extended status word      */
	BYTE	ecw[32];		/* Extended control word     */
    } IRB;

/* Measurement Block */
typedef struct _MBK {
	HWORD	srcount;		/* SSCH + RSCH count	     */
	HWORD	samplecnt;		/* Sample count 	     */
	FWORD	dct;			/* Device connect time	     */
	FWORD	fpt;			/* Function pending time     */
	FWORD	ddt;			/* Device disconnect time    */
	FWORD	cuqt;			/* Control unit queueing time*/
	FWORD	resv[3];		/* Reserved		     */
    } MBK;

/* Bit definitions for SCHM instruction */
#define CHM_GPR1_MBK	0xF0000000	/* Measurement Block Key     */
#define CHM_GPR1_M	0x00000002	/* Measurement mode control  */
#define CHM_GPR1_D	0x00000001	/* Block update Mode	     */
#define CHM_GPR1_RESV	0x0FFFFFFC	/* Reserved, must be zero    */

#define CHM_GPR2_MBO	0x7FFFFFE0	/* Measurement Block Origin  */
#define CHM_GPR2_RESV	0x8000001F	/* Reserved, must be zero    */

/* Definitions for PLO instruction */
#define PLO_GPR0_FC     0x000000FF      /* Function code mask        */
#define PLO_GPR0_T      0x00000100      /* Function test mask        */
#define PLO_GPR0_RESV   0xFFFFFE00      /* Reserved bits             */
#define PLO_CL                   0      /* Compare and load          */
#define PLO_CLG                  1      /* Compare and load          */
#define PLO_CS                   4      /* Compare and swap          */
#define PLO_CSG                  5      /* Compare and swap          */
#define PLO_DCS                  8      /* Double compare and swap   */
#define PLO_DCSG                 9      /* Double compare and swap   */
#define PLO_CSST                12      /* Compare and swap and store*/
#define PLO_CSSTG               13      /* Compare and swap and store*/
#define PLO_CSDST               16      /* C/S and double store      */
#define PLO_CSDSTG              17      /* C/S and double store      */
#define PLO_CSTST               20      /* C/S and triple store      */
#define PLO_CSTSTG              21      /* C/S and triple store      */

/* Bit definitions for the Vector Facility */
#define VSR_M    0x0001000000000000ULL	/* Vector mask mode bit      */
#define VSR_VCT  0x0000FFFF00000000ULL	/* Vector count              */
#define VSR_VIX  0x00000000FFFF0000ULL	/* Vector interruption index */
#define VSR_VIU  0x000000000000FF00ULL	/* Vector in-use bits        */
#define VSR_VIU0 0x0000000000008000ULL	/* Vector in-use bit vr0     */
#define VSR_VCH  0x00000000000000FFULL	/* Vector change bits        */
#define VSR_VCH0 0x0000000000000080ULL	/* Vector change bit vr0     */
#define VSR_RESV 0xFFFE000000000000ULL	/* Reserved bits             */

#define VAC_MASK 0x00FFFFFFFFFFFFFFULL	/* Vector Activity Count mask*/

/* SIE State Descriptor Block */
typedef struct _SIEBK {			/* SIE State Descriptor      */
/*00*/  BYTE  v;                        /* Intervention requests     */
#define SIE_V_WAIT      0x10            /* Wait/Run bit              */
#define SIE_V_EXTCALL   0x08            /* External call pending     */
#define SIE_V_STOP      0x04            /* SIE Stop control          */
#define SIE_V_IO        0x02            /* I/O Interrupt pending     */
#define SIE_V_EXT       0x01            /* EXT Interrupt pending     */
/*01*/  BYTE  s;                        /* State controls            */
#define SIE_S_T         0x80            /* Interval timer irpt pend  */
#define SIE_S_RETENTION 0x40            /* SIE State retained        */
#define SIE_S_EXP_TIMER 0x02            /* Expedite timer enabled    */
#define SIE_S_EXP_RUN   0x01            /* Expedite run enabled      */
/*02*/  BYTE  mx;                       /* Machine mode control      */
#define SIE_MX_XC       0x01            /* XC mode guest             */
/*03*/  BYTE  m;                        /* Mode controls             */
#define SIE_M_VCC       0x40            /* Vector change control     */
#define SIE_M_XA        0x20            /* XA mode guest             */
#define SIE_M_370       0x10            /* 370 mode guest            */
#define SIE_M_VR        0x08            /* V=R mode guest            */
#define SIE_M_ITMOF     0x04            /* Guest ival timer disabled */
#define SIE_M_GPE       0x02            /* Guest per enhancement     */
/*04*/  FWORD prefix;                   /* Guest prefix register     */
/*08*/  HWORD mso;                      /* Main Storage Origin       */
/*0A*/  HWORD mse;                      /* Main Storage Extent       */
/*0C*/  FWORD resv0cf;
/*10*/  FWORD gr14;                     /* Guest gr 14               */
/*14*/  FWORD gr15;                     /* Guest gr 15               */
/*18*/  DWORD psw;                      /* Guest PSW                 */
/*20*/  FWORD resv20f;
/*24*/  FWORD residue;                  /* Residue counter           */
/*28*/  DWORD cputimer;                 /* CPU timer                 */
/*30*/  DWORD clockcomp;                /* Clock comparator          */
/*38*/  DWORD epoch;                    /* Guest/Host epoch diff.    */
/*40*/  FWORD svc_ctl;                  /* SVC Controls              */
#define SIE_SVC0_ALL    0x80            /* Intercept all SVCs        */
#define SIE_SVC0_1N     0x40            /* Intercept SVC 1n          */
#define SIE_SVC0_2N     0x20            /* Intercept SVC 2n          */
#define SIE_SVC0_3N     0x10            /* Intercept SVC 3n          */
/*44*/  HWORD lctl_ctl;                 /* LCTL Control              */
#define SIE_LCTL0_CR0   0x80            /* Intercept LCTL 0          */
#define SIE_LCTL0_CR1   0x40            /* Intercept LCTL 1          */
#define SIE_LCTL0_CR2   0x20            /* Intercept LCTL 2          */
#define SIE_LCTL0_CR3   0x10            /* Intercept LCTL 3          */
#define SIE_LCTL0_CR4   0x08            /* Intercept LCTL 4          */
#define SIE_LCTL0_CR5   0x04            /* Intercept LCTL 5          */
#define SIE_LCTL0_CR6   0x02            /* Intercept LCTL 6          */
#define SIE_LCTL0_CR7   0x01            /* Intercept LCTL 7          */
#define SIE_LCTL1_CR8   0x80            /* Intercept LCTL 8          */
#define SIE_LCTL1_CR9   0x40            /* Intercept LCTL 9          */
#define SIE_LCTL1_CR10  0x20            /* Intercept LCTL 10         */
#define SIE_LCTL1_CR11  0x10            /* Intercept LCTL 11         */
#define SIE_LCTL1_CR12  0x08            /* Intercept LCTL 12         */
#define SIE_LCTL1_CR13  0x04            /* Intercept LCTL 13         */
#define SIE_LCTL1_CR14  0x02            /* Intercept LCTL 14         */
#define SIE_LCTL1_CR15  0x01            /* Intercept LCTL 15         */
/*46*/  HWORD cpuad;                    /* Virtual CPU address       */
/*48*/  DWORD ic;                       /* Interception Controls     */
#define SIE_IC0_OPEREX  0x80            /* Intercept operation exc.  */
#define SIE_IC0_PRIVOP  0x40            /* Intercept priv. op. exc.  */
#define SIE_IC0_PGMALL  0x20            /* Intercept program ints    */
#define SIE_IC0_TS1     0x08            /* Intercept TS cc1          */
#define SIE_IC0_CS1     0x04            /* Intercept CS cc1          */
#define SIE_IC0_CDS1    0x02            /* Intercept CDS cc1         */
#define SIE_IC0_IPTECSP 0x01            /* Intercept IPTE or CSP     */
#define SIE_IC1_LPSW    0x40            /* Intercept LPSW            */
#define SIE_IC1_PXLB    0x20            /* Intercept PTLB or PALB    */
#define SIE_IC1_SSM     0x10            /* Intercept SSM             */
#define SIE_IC1_BSA     0x08            /* Intercept BSA             */
#define SIE_IC1_STCTL   0x04            /* Intercept STCTL           */
#define SIE_IC1_STOSM   0x02            /* Intercept STOSM           */
#define SIE_IC1_STNSM   0x01            /* Intercept STNSM           */
#define SIE_IC2_STCK    0x80            /* Intercept STCK            */
#define SIE_IC2_ISKE    0x40            /* Intercept ISK/ISKE        */
#define SIE_IC2_SSKE    0x20            /* Intercept SSK/SSKE        */
#define SIE_IC2_RRBE    0x10            /* Intercept RRB/RRBE        */
#define SIE_IC2_PC      0x08            /* Intercept PC              */
#define SIE_IC2_PT      0x04            /* Intercept PT              */
#define SIE_IC2_TPROT   0x02            /* Intercept TPROT           */
#define SIE_IC2_LASP    0x01            /* Intercept LASP            */
#define SIE_IC3_VACSV   0x80            /* Intercept VACSV           */
#define SIE_IC3_SPT     0x40            /* Intercept SPT and STPT    */
#define SIE_IC3_SCKC    0x20            /* Intercept SCKC and STCKC  */
#define SIE_IC3_VACRS   0x10            /* Intercept VACRS           */
#define SIE_IC3_PR      0x08            /* Intercept PR              */
#define SIE_IC3_BAKR    0x04            /* Intercept BAKR            */
#define SIE_IC3_PGX     0x02            /* Intercept PGIN/PGOUT      */
/*50*/  BYTE  c;                        /* Interception Code         */
#define SIE_C_INST         4            /* Instruction interception  */
#define SIE_C_PGMINT       8            /* Program interruption      */
#define SIE_C_PGMINST     12            /* Program/instruction int   */
#define SIE_C_EXTREQ      16            /* External request          */
#define SIE_C_EXTINT      20            /* External interruption     */
#define SIE_C_IOREQ       24            /* I/O request               */
#define SIE_C_WAIT        28            /* Wait state                */
#define SIE_C_VALIDITY    32            /* Validity                  */
#define SIE_C_STOPREQ     40            /* Stop request              */
#define SIE_C_OPEREXC     44            /* Operation Exception       */
/*51*/  BYTE  f;                        /* Interception Status       */
#define SIE_F_IN        0x80            /* Intercept format 2        */
#define SIE_F_IF        0x02            /* Instruction fetch PER     */
#define SIE_F_EX        0x01            /* Icept for target of EX    */
/*52*/  HWORD lhcpu;                    /* Last Host CPU addr        */
/*54*/  HWORD todpf;                    /* TOD programmable field    */
/*56*/  HWORD ipa;                      /* Instruction parameter A   */
/*58*/  FWORD ipb;                      /* Instruction parameter B   */
/*5C*/  FWORD ipc;                      /* Instruction parameter C   */
/*60*/  FWORD rcpo;                     /* RCP area origin           */
#define SIE_RCPO0_SKA   0x80            /* Storage Key Assist        */
/*64*/  FWORD scao;                     /* SCA area origin           */
/*68*/  FWORD subchtabo;                /* Subchannel table origin   */
/*6C*/  FWORD resv6Cf;
/*70*/  HWORD tch_ctl;                  /* Test Channel control      */
/*72*/  HWORD resv72h;
/*74*/  FWORD iopassthru;               /* I/O passthru control      */
/*78*/  BYTE  xslim[3];                 /* Extended stor upper lim   */
/*7B*/  BYTE  resv7Bb;
/*7C*/  FWORD resv7Cf;
/*80*/  FWORD cr[16];                   /* Guest Control registers   */
/*C0*/  BYTE  ip[34];                   /* Interruption parameters   */
#define SIE_IP_PSA_OFFSET       0x40    /* Offset of the IP field 
                                           relative to the ipfields
                                           in the PSA                */
/*E2*/  BYTE  xso[3];                   /* Expanded storage origin   */
/*E5*/  BYTE  xsl[3];                   /* Expanded storage limit    */
/*E8*/  BYTE  resvE8b[24];
} SIEBK;

#define LKPG_GPR0_LOCKBIT       0x00000200
#define LKPG_GPR0_RESV          0x0000FD00

/* Bit definitions for STSI instruction */
#define STSI_GPR0_FC_MASK	0xF0000000
#define STSI_GPR0_FC_CURRENT	0x00000000
#define STSI_GPR0_FC_BASIC	0x10000000
#define STSI_GPR0_FC_LPAR	0x20000000
#define STSI_GPR0_FC_VM 	0x30000000
#define STSI_GPR0_SEL1_MASK	0x000000FF
#define STSI_GPR0_RESERVED	0x0FFFFF00

#define STSI_GPR1_SEL2_MASK	0x0000FFFF
#define STSI_GPR1_RESERVED	0xFFFF0000

/* Structure definitions for STSI instruction */
typedef struct _SYSIB111 {	/* Basic Machine Configuration	*/
	BYTE	resv1[4*8];	/* Reserved			*/
	BYTE	manufact[4*4];	/* Manufacturer 		*/
	BYTE	type[4*1];	/* Type 			*/
	BYTE	resv2[4*3];	/* Reserved			*/
	BYTE	model[4*4];	/* Model			*/
	BYTE	seqc[4*4];	/* Sequence Code		*/
	BYTE	plant[4*1];	/* Plant of manufacture 	*/
	}	SYSIB111;

typedef struct _SYSIB121 {	/* Basic Machine CPU		*/
	BYTE	resv1[4*20];	/* Reserved			*/
	BYTE	seqc[4*4];	/* Sequence Code		*/
	BYTE	plant[4*1];	/* Plant of manufacture 	*/
	BYTE	resv2[2*1];	/* Reserved			*/
	BYTE	cpuad[2*1];	/* CPU address			*/
	}	SYSIB121;

typedef struct _SYSIB122 {	/* Basic Machine CPUs		*/
	BYTE	resv1[4*8];	/* Reserved			*/
	BYTE	cap[4*1];	/* CPU capacity 		*/
	BYTE	totcpu[2*1];	/* Total CPU count		*/
	BYTE	confcpu[2*1];	/* Configured CPU count 	*/
	BYTE	sbcpu[2*1];	/* Standby CPU count		*/
	BYTE	resvcpu[2*1];	/* Reserved CPU count		*/
	BYTE	mpfact[2*MAX_CPU_ENGINES];	/* MP factors	*/
	}	SYSIB122;

typedef struct _SYSIB221 {	/* Logical partition CPU	*/
	BYTE	resv1[4*20];	/* Reserved			*/
	BYTE	seqc[4*4];	/* Logical CPU Sequence Code	*/
	BYTE	lcpuid[2*1];	/* Logical CPU ID		*/
	BYTE	cpuad[2*1];	/* CPU address			*/
	}	SYSIB221;

typedef struct _SYSIB222 {	/* Logical partition CPUs	*/
	BYTE	resv1[4*8];	/* Reserved			*/
	BYTE	lparnum[2*1];	/* LPAR number			*/
	BYTE	resv2[1*1];	/* Reserved			*/
	BYTE	lcpuc[1*1];	/* Logical CPU characteristics	*/
#define SYSIB222_LCPUC_DEDICATED	0x80
#define SYSIB222_LCPUC_SHARED		0x40
#define SYSIB222_LCPUC_CAPPED		0x20
	BYTE	totcpu[2*1];	/* Total CPU count		*/
	BYTE	confcpu[2*1];	/* Configured CPU count 	*/
	BYTE	sbcpu[2*1];	/* Standby CPU count		*/
	BYTE	resvcpu[2*1];	/* Reserved CPU count		*/
	BYTE	lparname[4*2];	/* LPAR name			*/
	BYTE	lparcaf[4*1];	/* LPAR capability adjustment	*/
	BYTE	resv3[4*4];	/* Reserved			*/
	BYTE	dedcpu[2*1];	/* Dedicated CPU count		*/
	BYTE	shrcpu[2*1];	/* Shared CPU count		*/
	}	SYSIB222;

typedef struct _SYSIB322 {	/* Virtual Machines CPUs	*/
	BYTE	resv1[4*7];	/* Reserved			*/
	BYTE	resv2[3*1];	/* Reserved			*/
	BYTE	dbct;		/* Four bit desc block count	*/
	BYTE	vmdb[4*16];	/* Virtual Machine desc block	*/
	}	SYSIB322;

typedef struct _SYSIBVMDB {	/* Virtual Machine Desc Block	*/
	BYTE	resv1[4*1];	/* Reserved			*/
	BYTE	totcpu[2*1];	/* Total CPU count		*/
	BYTE	confcpu[2*1];	/* Configured CPU count 	*/
	BYTE	sbcpu[2*1];	/* Standby CPU count		*/
	BYTE	resvcpu[2*1];	/* Reserved CPU count		*/
	BYTE	vmname[4*2];	/* VM userid			*/
	BYTE	vmcaf[4*1];	/* VM capability adjustment	*/
	BYTE	cpid[4*4];	/* Control Program ID		*/
	}	SYSIBVMDB;
