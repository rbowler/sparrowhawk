/*--------------------------------------------------------------*/
/* This module implements the compression call instruction of   */
/* S/390 architecture, described in the manual SA22-7208-01:    */
/* Data Compression.                                            */
/*                                                              */
/* This instruction is under construction, but if you need it,  */
/* you can #define FEATURE_CMPSC in hercules.h. Then you get a  */
/* copy function. Compress and expand just copies the data. So  */
/* compressed on hercules will also expand ;-).                 */
/*                                                              */
/*                      2000 (c) Copyright Bernard van der Helm */
/*--------------------------------------------------------------*/

#include "hercules.h"

/* Comment next four lines if you want to try the code that is
   still under construction */
#ifdef FEATURE_CMPSC
#define FEATURE_CMPSC_COPY
#undef FEATURE_CMPSC
#endif /* FEATURE_CMPSC */

/*--------------------------------------------------------------*/
/* Constants                                                    */
/*--------------------------------------------------------------*/
#define CMPDEBUG
#define CMPEND_DESTINATION	1
#define CMPEND_SOURCE		2
#define CMPFOUND		3
#define CMPMATCH		4
#define CMPMORE_260_CHILDREN    5
#define CMPNO_MATCH		6
#define CMPNOT_FOUND		7
#define CMPPROCESSMAX 		2048

/*--------------------------------------------------------------*/
/* Compression Character Entry macro's                          */
/*--------------------------------------------------------------*/
#define CMPCCE_cct(cce)  ((BYTE)((((BYTE*)&(cce))[0])>>5))
#define CMPCCE_x1(cce)   ((((BYTE*)&(cce))[0])&0x10)
#define CMPCCE_x2(cce)   ((((BYTE*)&(cce))[0])&0x08)
#define CMPCCE_x3(cce)   ((((BYTE*)&(cce))[0])&0x04)
#define CMPCCE_x4(cce)   ((((BYTE*)&(cce))[0])&0x02)
#define CMPCCE_x5(cce)   ((((BYTE*)&(cce))[0])&0x01)
#define CMPCCE_y1(cce)   ((((BYTE*)&(cce))[1])&0x80)
#define CMPCCE_y2(cce)   ((((BYTE*)&(cce))[1])&0x40)
#define CMPCCE_d(cce)    ((((BYTE*)&(cce))[1])&0x20)
#define CMPCCE_act(cce)  ((BYTE)((((BYTE*)&(cce))[1])>>5))
#define CMPCCE_cptr(cce) ((U16)((((((BYTE*)&(cce))[1])&0x1F)<<8)|(((BYTE*)&(cce))[2])))
#define CMPCCE_prt(cce) \
	logmsg("Character Entry\n"); \
        logmsg("cct     : %d\n", CMPCCE_cct(cce)); \
        logmsg("x1      : %s\n", CMPCCE_x1(cce) ? "True" : "False"); \
        logmsg("x2      : %s\n", CMPCCE_x2(cce) ? "True" : "False"); \
        logmsg("x3      : %s\n", CMPCCE_x3(cce) ? "True" : "False"); \
        logmsg("x4      : %s\n", CMPCCE_x4(cce) ? "True" : "False"); \
        logmsg("x5      : %s\n", CMPCCE_x5(cce) ? "True" : "False"); \
        logmsg("y1      : %s\n", CMPCCE_y1(cce) ? "True" : "False"); \
        logmsg("y2      : %s\n", CMPCCE_y2(cce) ? "True" : "False"); \
        logmsg("d       : %s\n", CMPCCE_d(cce) ? "True" : "False"); \
        logmsg("act     : %d\n", CMPCCE_act(cce)); \
        logmsg("cptr    : %08X\n", CMPCCE_cptr(cce));

/*--------------------------------------------------------------*/
/* Format-0 Sibling Descriptors macro's                         */
/*--------------------------------------------------------------*/
#define CMPSD0_sct(sd0) ((BYTE)((((BYTE*)&(sd0))[0])>>5))
#define CMPSD0_y1(sd0)  ((((BYTE*)&(sd0))[0])&0x10)
#define CMPSD0_y2(sd0)  ((((BYTE*)&(sd0))[0])&0x08)
#define CMPSD0_y3(sd0)  ((((BYTE*)&(sd0))[0])&0x04)
#define CMPSD0_y4(sd0)  ((((BYTE*)&(sd0))[0])&0x02)
#define CMPSD0_y5(sd0)  ((((BYTE*)&(sd0))[0])&0x01)
#define CMPSD0_prt(sd0) \
	logmsg("Format-0 Sibling Descriptor\n"); \
	logmsg("sct     : %d\n", CMPSD0_sct(sd0)); \
        logmsg("y1      : %s\n", CMPSD0_y1(sd0) ? "True" : "False"); \
        logmsg("y2      : %s\n", CMPSD0_y2(sd0) ? "True" : "False"); \
        logmsg("y3      : %s\n", CMPSD0_y3(sd0) ? "True" : "False"); \
        logmsg("y4      : %s\n", CMPSD0_y4(sd0) ? "True" : "False"); \
        logmsg("y5      : %s\n", CMPSD0_y5(sd0) ? "True" : "False");

/*--------------------------------------------------------------*/
/* Format-1 Sibling Descriptors macro's                         */
/*--------------------------------------------------------------*/
#define CMPSD1_sct(sd1) ((BYTE)(((BYTE*)&(sd1))[0]>>4))
#define CMPSD1_y1(sd1)  ((((BYTE*)&(sd1))[0])&0x08)
#define CMPSD1_y2(sd1)  ((((BYTE*)&(sd1))[0])&0x04)
#define CMPSD1_y3(sd1)  ((((BYTE*)&(sd1))[0])&0x02)
#define CMPSD1_y4(sd1)  ((((BYTE*)&(sd1))[0])&0x01)
#define CMPSD1_y5(sd1)  ((((BYTE*)&(sd1))[1])&0x80)
#define CMPSD1_y6(sd1)  ((((BYTE*)&(sd1))[1])&0x40)
#define CMPSD1_y7(sd1)  ((((BYTE*)&(sd1))[1])&0x20)
#define CMPSD1_y8(sd1)  ((((BYTE*)&(sd1))[1])&0x10)
#define CMPSD1_y9(sd1)  ((((BYTE*)&(sd1))[1])&0x08)
#define CMPSD1_y10(sd1) ((((BYTE*)&(sd1))[1])&0x04)
#define CMPSD1_y11(sd1) ((((BYTE*)&(sd1))[1])&0x02)
#define CMPSD1_y12(sd1) ((((BYTE*)&(sd1))[1])&0x01)
#define CMPSD1_prt(sd0) \
        logmsg("Format-1 Sibling Descriptor\n"); \
        logmsg("sct     : %d\n", CMPSD1_sct(sd1)); \
        logmsg("y1      : %s\n", CMPSD1_y1(sd1) ? "True" : "False"); \
        logmsg("y2      : %s\n", CMPSD1_y2(sd1) ? "True" : "False"); \
        logmsg("y3      : %s\n", CMPSD1_y3(sd1) ? "True" : "False"); \
        logmsg("y4      : %s\n", CMPSD1_y4(sd1) ? "True" : "False"); \
        logmsg("y5      : %s\n", CMPSD1_y5(sd1) ? "True" : "False"); \
        logmsg("y6      : %s\n", CMPSD1_y6(sd1) ? "True" : "False"); \
        logmsg("y7      : %s\n", CMPSD1_y7(sd1) ? "True" : "False"); \
        logmsg("y8      : %s\n", CMPSD1_y8(sd1) ? "True" : "False"); \
        logmsg("y9      : %s\n", CMPSD1_y9(sd1) ? "True" : "False"); \
        logmsg("y10     : %s\n", CMPSD1_y10(sd1) ? "True" : "False"); \
        logmsg("y11     : %s\n", CMPSD1_y11(sd1) ? "True" : "False"); \
        logmsg("y12     : %s\n", CMPSD1_y12(sd1) ? "True" : "False");

/*--------------------------------------------------------------*/
/* Expansion Character Entry macro's                            */
/*--------------------------------------------------------------*/
#define CMPECE_psl(ece)  ((BYTE)((((BYTE*)&(ece))[0])>>5))
#define CMPECE_csl(ece)  ((BYTE)((((BYTE*)&(ece))[0])&0x07))
#define CMPECE_pptr(ece) ((U16)((((((BYTE*)&(ece))[0])&0x1F)<<8)|(((BYTE*)(ece))[1])))
#define CMPECE_ofst(ece) ((BYTE)(((BYTE*)&(ece))[7]))
#define CMPECE_prt(ece) \
	logmsg("Expansion Character Entry\n"); \
 	logmsg("psl     : %d\n", CMPECE_psl(ece)); \
        logmsg("csl     : %d\n", CMPECE_csl(ece)); \
        logmsg("pptr    : %d\n", CMPECE_pptr(ece)); \
        logmsg("ofst    : %d\n", CMPECE_ofst(ece));

/*--------------------------------------------------------------*/
/* RRE macro's                                                  */
/*--------------------------------------------------------------*/
#define CMPRRE_r1(inst) ((BYTE)(((BYTE*)(inst))[3]>>4))
#define CMPRRE_r2(inst) ((BYTE)(((BYTE*)(inst))[3]&0x0F))
#define CMPRRE_prt(inst) \
	logmsg("CMPSC instruction\n"); \
        logmsg("r1      : %d\n", CMPRRE_r1(inst)); \
	logmsg("address : %08X\n", CMP_addr(CMPRRE_r1(inst), regs)); \
        logmsg("length  : %08X\n", regs->gpr[CMPRRE_r1(inst) + 1]); \
        logmsg("r2      : %d\n", CMPRRE_r2(inst)); \
        logmsg("address : %08X\n", CMP_addr(CMPRRE_r2(inst), regs)); \
        logmsg("length  : %08X\n", regs->gpr[CMPRRE_r2(inst) + 1]);

/*--------------------------------------------------------------*/
/* Register macro                                               */
/*--------------------------------------------------------------*/
#define CMP_addr(r, regs) ((U32)(regs)->gpr[(r)]&((regs)->psw.amode?0x7FFFFFFF:0x00FFFFFF))

/*--------------------------------------------------------------*/
/* GR0 macro's                                                  */
/*--------------------------------------------------------------*/
#define CMPGR0_st(regs)   ((regs)->gpr[0]&0x00010000)
#define CMPGR0_cdss(regs) ((BYTE)(((regs)->gpr[0]&0x0000F000)>>12))
#define CMPGR0_f1(regs)   ((regs)->gpr[0]&0x00000200)
#define CMPGR0_e(regs)    ((regs)->gpr[0]&0x00000100)
#define CMPGR0_prt(regs) \
	logmsg("General purpose register 0: %08X\n", regs->gpr[0]); \
        logmsg("st      : %d\n", CMPGR0_st(regs)); \
        logmsg("cdss    : %d\n", CMPGR0_cdss(regs)); \
        logmsg("f1      : %s\n", CMPGR0_f1(regs) ? "True" : "False"); \
        logmsg("e       : %s\n", CMPGR0_st(regs) ? "True" : "False");

/*--------------------------------------------------------------*/
/* GR1 macro's                                                  */
/*--------------------------------------------------------------*/
#define CMPGR1_dictor(regs)      ((U16)((regs)->gpr[1])&((regs)->psw.amode?0x7FFFF000:0x00FFF000))
#define CMPGR1_sttoff(regs)      ((U16)((regs)->gpr[1]&0x00000FF8)>>3)
#define CMPGR1_cbn(regs)         ((BYTE)((regs)->gpr[1]&0x00000007))
#define CMPGR1_setcbn(regs, cbn) ((regs)->gpr[1]=((regs)->gpr[1]&0xFFFFFFF8)|(((U32)cbn)&0x00000007))
#define CMPGR1_prt(regs) \
	logmsg("General purpose register 1: %08X\n", regs->gpr[1]); \
        logmsg("dictor  : %08X\n", CMPGR1_dictor(regs)); \
        logmsg("sttoff  : %d\n", CMPGR1_sttoff(regs)); \
        logmsg("cbn     : %d\n", CMPGR1_cbn(regs));

/*--------------------------------------------------------------*/
/* handy macro's                                                */
/*--------------------------------------------------------------*/
#define CMPEVEN(i) ((i)&0x02)
#ifdef CMPDEBUG
#define CMPGETCH(r2, regs, ch) \
	if (!regs->gpr[r2 + 1]) \
          return (CMPEND_SOURCE); \
  	ch = vfetchb (regs->gpr[r2], r2, regs); \
        logmsg("  character %02X read\n", ch);
#else
#define CMPGETCH(r2, regs, ch) \
        if (!regs->gpr[r2 + 1]) \
          return (CMPEND_SOURCE); \
        ch = vfetchb (regs->gpr[r2], r2, regs);
#endif /* CMPDEBUG */
#define CMPODD(i)  ((i)&0x01)

#ifdef FEATURE_CMPSC

/*--------------------------------------------------------------*/
/* Function proto types                                         */
/*--------------------------------------------------------------*/
int cmpcheck_extension_characters (int r1, int r2, REGS * regs, U64 * child_entry);
void cmpcompress (int r1, int r2, REGS * regs);
void cmpexpand (int r1, int r2, REGS * regs);
int cmpget_indexsymbol (int r1, int r2, REGS * regs, U32 * indexsymbol);
int cmpsearch_character_entry (int r1, int r2, REGS * regs, U64 parent_entry, U32 * child_pointer, U64 * child_entry);
int cmpsearch_child (int r1, int r2, REGS * regs, U64 parent_entry, U32 * child_pointer, U64 * child_entry);
int cmpsearch_sibling_descriptors (int r1, int r2, REGS * regs, U64 parent_entry, U32 * child_pointer, U64 * child_entry);
void cmpstore_indexsymbol (int r1, REGS * regs, U32 indexsymbol);
void cmpsymbol_translate (int r1, int r2, REGS * regs);

/*--------------------------------------------------------------*/
/* cmpcheck_extension_characters                                */
/*--------------------------------------------------------------*/
int
cmpcheck_extension_characters (int r1, int r2, REGS * regs, U64 * child_entry)
{
  int act = CMPCCE_act (*child_entry);
  BYTE ch;
  int index;

#ifdef CMPDEBUG
  logmsg ("cmpcheck_extension_characters\n");
#endif

  /* Now check all aditional extension characters */
  for (index = 0; index < act; index++)
    {
      CMPGETCH (r2, regs, ch);
      if (ch != ((BYTE *) & child_entry)[3 + index])
	{
#ifdef CMPDEBUG
	  logmsg ("  No match\n");
#endif
	  return (CMPNO_MATCH);
	}
    }

  /* We matched all additional extension characters (can be 0) */
#ifdef CMPDEBUG
  logmsg ("  Match\n");
#endif
  return (CMPMATCH);
}

/*--------------------------------------------------------------*/
/* cmpcompress                                                  */
/*--------------------------------------------------------------*/
void
cmpcompress (int r1, int r2, REGS * regs)
{
  U32 indexsymbol;
  int xlated = 0;

#ifdef CMPDEBUG
  logmsg ("cmpcompress\n");
#endif

  /* Try to process the model dependent maximum */
  while (xlated++ < CMPPROCESSMAX)
    {
      switch (cmpget_indexsymbol (r1, r2, regs, &indexsymbol))
	{
	case CMPFOUND:
	  /* Indexsymbol found, store it */
	  cmpstore_indexsymbol (r1, regs, indexsymbol);
	  break;

	case CMPMORE_260_CHILDREN:
	  /* More than 260 children found */
	  program_check (regs, PGM_DATA_EXCEPTION);
	  return;

	case CMPEND_SOURCE:
	  /* We have reached the end of the source */
	  regs->psw.cc = 0;
	  return;

	case CMPEND_DESTINATION:
	  /* We have reached the end of the destination */
	  regs->psw.cc = 1;
	  return;
	}
    }

  /* Reached model dependent CPU processing amount */
  regs->psw.cc = 3;
}

/*--------------------------------------------------------------*/
/* cmpexpand                                                    */
/*--------------------------------------------------------------*/
void
cmpexpand (int r1, int r2, REGS * regs)
{
#ifdef CMPDEBUG
  logmsg ("cmpexpand\n");
#endif

  logmsg ("CMPSC expand not implemented yet\n");
}

/*--------------------------------------------------------------*/
/* cmpget_indexsymbol                                           */
/*--------------------------------------------------------------*/
int
cmpget_indexsymbol (int r1, int r2, REGS * regs, U32 * indexsymbol)
{
  int act;
  BYTE ch;
  U64 child_entry;
  U32 child_pointer;
  U32 dictionary = CMPGR1_dictor (regs);
  U64 parent_entry;

#ifdef CMPDEBUG
  logmsg ("cmpget_indexsymbol\n");
#endif

  /* First check if we can write an indexsymbol */
  if (((CMPGR1_cbn (regs) + CMPGR0_cdss (regs) + 1) / 8) > regs->gpr[r1 + 1])
    {
#ifdef CMPDEBUG
      logmsg ("  End of destination\n");
#endif
      return (CMPEND_DESTINATION);
    }

  /* Get the alphabet entry */
  CMPGETCH (r2, regs, ch);
  parent_entry = vfetch8 (dictionary + ch * 8, 1, regs);

  /* We always match the alpabet entry, so set last match */
  regs->gpr[r2]++;
  regs->gpr[r2 + 1]--;
  *indexsymbol = ch;

  while (1)
    {
      switch (cmpsearch_child (r1, r2, regs, parent_entry, &child_pointer, &child_entry))
	{
	case CMPFOUND:
	  /* Set last match */
	  act = CMPCCE_act (child_entry);
	  regs->gpr[r2] += act;
	  regs->gpr[r2 + 1] += act;
	  *indexsymbol = child_pointer;

	  /* Now look for grand children */
	  parent_entry = child_entry;

	  break;

	case CMPEND_SOURCE:
	  /* Registers ??? */
	  return (CMPEND_SOURCE);

	case CMPNOT_FOUND:
	  /* Return last match */
	  return (CMPFOUND);

	case CMPMORE_260_CHILDREN:
	  /* Trying to read child number 261 */
	  return (CMPMORE_260_CHILDREN);
	}
    }
}

/*--------------------------------------------------------------*/
/* cmpsc                                                        */
/*--------------------------------------------------------------*/
void
zz_compression_call (BYTE inst[], int execflag, REGS * regs)
{
  int cdss = CMPGR0_cdss (regs);
  int r1 = CMPRRE_r1 (inst);
  int r2 = CMPRRE_r2 (inst);

#ifdef CMPDEBUG
  CMPRRE_prt (inst);
  CMPGR0_prt (regs);
  CMPGR1_prt (regs);
#endif

  /* Check the registers on even-odd pairs and
     check if compression-data symbol size is valid */
  if (CMPODD (r1) || CMPODD (r2) || !cdss || cdss > 5)
    {
      program_check (regs, PGM_SPECIFICATION_EXCEPTION);
      return;
    }

  /* Now go to the requested function */
  if (CMPGR0_e (regs))
    cmpexpand (r1, r2, regs);
  else if (CMPGR0_st (regs))
    cmpsymbol_translate (r1, r2, regs);
  else
    cmpcompress (r1, r2, regs);
}

/*--------------------------------------------------------------*/
/* cmpsearch_character_entry                                    */
/*--------------------------------------------------------------*/
int
cmpsearch_character_entry (int r1, int r2, REGS * regs, U64 parent_entry, U32 * child_pointer, U64 * child_entry)
{
  int act = CMPCCE_act (parent_entry);
  int cct = CMPCCE_cct (parent_entry);
  BYTE ch;
  U32 cptr = CMPCCE_cptr (parent_entry);
  int d = CMPCCE_d (parent_entry);
  U32 dictionary = CMPGR1_dictor (regs);
  int index;

#ifdef CMPDEBUG
  logmsg ("cmpsearch_character_entry\n");
#endif

  /* Get the next character */
  CMPGETCH (r2, regs, ch);

  /* Determine number of children */
  if ((d && cct == 5) || (!d && cct == 6))
    cct--;

  /* Check all children */
  for (index = 0; index < cct; index++)
    {
      if (ch == ((BYTE *) & parent_entry)[3 + index + act])
	{
	  /* We found a child, check additional extension characters */
	  *child_entry = vfetch8 (dictionary + cptr + index * 8, 1, regs);
	  switch (cmpcheck_extension_characters (r1, r2, regs, child_entry))
	    {
	    case CMPMATCH:
	      *child_pointer = cptr + index * 8;
	      return (CMPFOUND);

	    case CMPEND_SOURCE:
	      return (CMPEND_SOURCE);
	    }
	}
    }
  return (CMPNOT_FOUND);
}

/*--------------------------------------------------------------*/
/* cmpsearch_child                                              */
/*--------------------------------------------------------------*/
int
cmpsearch_child (int r1, int r2, REGS * regs, U64 parent_entry, U32 * child_pointer, U64 * child_entry)
{
  int rc;

#ifdef CMPDEBUG
  logmsg ("cmpsearch_child\n");
#endif

  rc = cmpsearch_character_entry (r1, r2, regs, parent_entry, child_pointer, child_entry);
  if (rc == CMPNOT_FOUND)
    return (cmpsearch_sibling_descriptors (r1, r2, regs, parent_entry, child_pointer, child_entry));
  return (rc);
}

/*--------------------------------------------------------------*/
/* cmpsearch_sibling_descriptors                                */
/*--------------------------------------------------------------*/
int
cmpsearch_sibling_descriptors (int r1, int r2, REGS * regs, U64 parent_entry, U32 * child_pointer, U64 * child_entry)
{
  int cct = CMPCCE_cct (parent_entry);
  BYTE ch;
  int children_searched;
  int cptr = CMPCCE_cptr (parent_entry);
  int d = CMPCCE_d (parent_entry);
  U32 dictionary = CMPGR1_dictor (regs);
  U32 dictionary_size = 2048 << CMPGR0_cdss (regs);
  int f1 = CMPGR0_f1 (regs);
  int index;
  int more_siblings = 1;
  int sct;
  U32 sibling_address;
  U64 sibling_descriptor[2];

#ifdef CMPDEBUG
  logmsg ("cmpsearch_sibling_descriptors\n");
#endif

  /* Are there sibling characters */
  if ((!d && cct != 6) || (d && cct != 5))
    return (CMPNOT_FOUND);

  /* Set already searched children in character entry */
  if (d)
    children_searched = 4;
  else
    children_searched = 5;

  /* Calculate the address */
  sibling_address = dictionary + cptr + cct * 8;

  while (more_siblings)
    {
      more_siblings = 0;

      /* Get the format-0 sibling descriptor */
      sibling_descriptor[0] = vfetch8 (sibling_address, 1, regs);

      /* Check and get the format-1 entry from the expansion dictionary */
      if (f1)
	{
	  sibling_descriptor[1] = vfetch8 (sibling_address + dictionary_size, 1, regs);
	  sct = CMPSD1_sct (*sibling_descriptor);
	  if (sct == 15)
	    {
	      sct = 14;
	      more_siblings = 1;
	    }
	}
      else
	{
	  sct = CMPSD0_sct (*sibling_descriptor);
	  if (!sct)
	    {
	      sct = 7;
	      more_siblings = 1;
	    }
	}

      /* Is this child 261 */
      if (children_searched++ == 260)
	return (CMPMORE_260_CHILDREN);

      /* Get the next character */
      CMPGETCH (r2, regs, ch);

      /* Compare read character */
      for (index = 0; index < sct; index++)
	{
	  if (ch == ((BYTE *) sibling_descriptor)[2 + index])
	    {
	      /* We found a child, check additional extension characters */
	      *child_entry = vfetch8 (sibling_address + (index + 1) * 8, 1, regs);
	      switch (cmpcheck_extension_characters (r1, r2, regs, child_entry))
		{
		case CMPMATCH:
		  *child_pointer = cptr + index * 8;
		  return (CMPFOUND);

		case CMPEND_SOURCE:
		  return (CMPEND_SOURCE);
		}
	    }
	}
      if (more_siblings)
	sibling_address += sct * 8;
    }
  return (CMPNOT_FOUND);
}

/*--------------------------------------------------------------*/
/* cmpstore_indexsymbol                                         */
/*--------------------------------------------------------------*/
void
cmpstore_indexsymbol (int r1, REGS * regs, U32 indexsymbol)
{
  BYTE cbn = CMPGR1_cbn (regs);
  int increment;
  U16 symbolsize = CMPGR0_cdss (regs) + 1;
  U32 work;

#ifdef CMPDEBUG
  logmsg ("cmpstore_indexsymbol %X\n", indexsymbol);
#endif

  /* Calculate the increment in storage */
  increment = (cbn + symbolsize) / 8;

  /* Get the storage */
  vfetchc (&work, increment, regs->gpr[r1], r1, regs);

  /* Clear the bits */
  work &= ~((0xFFFFFFFF << (32 - symbolsize)) >> cbn);

  /* Set the bits */
  work |= (indexsymbol << (32 - symbolsize - cbn));

  /* Set the storage */
  vstorec (&work, increment, regs->gpr[r1], r1, regs);

  /* Set the new address and length */
  regs->gpr[r1] += increment;
  regs->gpr[r1 + 1] -= increment;

  /* Calculate and set the new Compressed-data Bit Number */
  cbn = (cbn + symbolsize) % symbolsize;
  CMPGR1_setcbn (regs, cbn);
}

/*--------------------------------------------------------------*/
/* cmpsymbol_translate                                          */
/*--------------------------------------------------------------*/
void
cmpsymbol_translate (int r1, int r2, REGS * regs)
{
#ifdef CMPDEBUG
  logmsg ("cmpsymbol_translate\n");
#endif

  logmsg ("CMPSC symbol translation not implemented yet\n");
}

#endif /* FEATURE_CMPSC */

#ifdef FEATURE_CMPSC_COPY
/*--------------------------------------------------------------*/
/* cmpsc                                                        */
/*                                                              */
/* This is the routine you can use if you desperately need a    */
/* CMPSC instruction. It just copies the data from the source   */
/* to the destination. So compress and expand on hercules will  */
/* work fine ;-). As you can see I'm still working on the real  */
/* implementation.                                              */
/*--------------------------------------------------------------*/
void
cmpsc (BYTE inst[], REGS * regs)
{
  BYTE cdss = CMPGR0_cdss (regs);
  U32 length;
  int r1 = CMPRRE_r1 (inst);
  U32 dst_addr = CMP_addr (r1, regs);
  int r2 = CMPRRE_r2 (inst);
  U32 src_addr = CMP_addr (r2, regs);

#ifdef CMPDEBUG
  CMPRRE_prt (inst);
  CMPGR0_prt (regs);
  CMPGR1_prt (regs);
#endif

  /* Check if R1 and R2 are even-odd pairs and if compressed-data symbol
     size is valid */
  if (CMPODD (r1) || CMPODD (r2) || !cdss || cdss > 5)
    {
      program_check (regs, PGM_SPECIFICATION_EXCEPTION);
      return;
    }

  /* How many bytes do we have to copy */
  if (regs->gpr[r1 + 1] < regs->gpr[r2 + 1])
    {
      /* End of first operand reached and end of second operand not
         reached */
      length = regs->gpr[r1 + 1];

      /* Set CC code */
      regs->psw.cc = 1;
    }
  else
    {
      /* End of second operand reached */
      length = regs->gpr[r2 + 1];

      /* Set CC code */
      regs->psw.cc = 0;
    }

  /* We copy maximum 256 bytes a time */
  if (length > 256)
    {
      /* CPU-determined amount of data processed */
      length = 256;

      /* Set CC code */
      regs->psw.cc = 3;
    }

  /* Now copy the data */
  move_chars (dst_addr, r1, regs->psw.pkey,
	      src_addr, r2, regs->psw.pkey, length - 1, regs);

  /* Adjust length and address */
  regs->gpr[r1] += length;
  regs->gpr[r1 + 1] -= length;
  regs->gpr[r2] += length;
  regs->gpr[r2 + 1] -= length;
}
#endif /* FEATURE_CMPSC_JUST_COPY */
