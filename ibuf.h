/* IBUF.H       (c) Copyright Juergen Dobrinski, 1994-2000                */
/*              Instruction prefetch and buffering                        */

#if 0
/*-------------------------------------------------------------------*/
/* Assign compiled fragment                                          */
/*-------------------------------------------------------------------*/
static inline void ibuf_assign_fragment (REGS *regs, U32 ia)
{
FRAGENTRY *fragentry;
U32  abs;                            /* Absolute storage address  */
BYTE akey;                           /* Bits 0-3=key, 4-7=zeroes  */
#ifdef IBUF_SWITCH
U32 i;
BYTE ibufactive = 0;
#endif

  /* search compiled fragment in buffer  */
  debugmsg("assign fragment\n");
#ifdef IBUF_STAT
  regs->ibufassign++;
#endif

  /* Obtain current access key from PSW */
  akey = regs->psw.pkey;

  /* Program check if instruction address is odd */
  regs->instvalid = 0;
  if (ia & 0x01)
      program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);

  abs = logical_to_abs (ia, 0, regs, ACCTYPE_INSTFETCH, akey);
  regs->instvalid = 1;
  regs->iaabs = abs;

#ifdef IBUF_SWITCH
  if (regs->actpage)
     ibufactive = 1;
#endif

  regs->actpage = &regs->dict[abs & 
             (STORAGE_KEY_PAGEMASK & ((FRAG_BUFFER * FRAG_BYTESIZE) - 1))];

  fragentry = regs->dict[abs & ((FRAG_BUFFER * FRAG_BYTESIZE) - 1)];
  
  if (fragentry && abs == fragentry->iaabs)
  {
      if (*fragentry->valid)
      {
          regs->actentry = fragentry;
          regs->iaabs = 0;
#ifdef IBUF_STAT
          regs->ibufexecute++;
#endif
          /* execution mode */
#ifdef IBUF_SWITCH
          if (ibufactive)
              return;
          else
              longjmp(regs->progjmp, 0);
#endif
      }
  }

#ifdef IBUF_SWITCH
  {
      i = (abs >> FRAG_ADDRESSLENGTH) & (FRAG_BUFFER - 1);
      regs->icount[i]++;
      if (!(regs->icount[i] >> IBUF_ICOUNT_SHIFT))
      {
          debugmsg("count too low 1\n");
          /* interpreter mode */
          regs->actpage = NULL;
#ifdef IBUF_STAT
          regs->ibufinterpret++;
#endif
          if (ibufactive)
              longjmp(regs->progjmp, 0);
          else
              return;
      }
  }
#endif

  /* fragment not valid (invalidated or new */
  debugmsg("ibuf_assign end bef compile\n");
  ibuf_compile_frag (regs, ia);

  /* execution mode */
#ifdef IBUF_STAT
  regs->ibufexecute++;
#endif
  regs->iaabs = 0;
  if (ibufactive)
      return;
  else
      longjmp(regs->progjmp, 0);
}
#endif

#ifdef INLINE_GET
/*-------------------------------------------------------------------*/
/* Get compiled fragment                                             */
/*-------------------------------------------------------------------*/
static inline void ibuf_get_fragentry (REGS *regs, U32 ia)
{
FRAGENTRY *fragentry;
#ifdef IBUF_SWITCH

  debugmsg("get fragentry\n");
#ifdef IBUF_STAT
  regs->ibufget++;
#endif

  if (!regs->actpage)
  {
#ifdef IBUF_STAT
      regs->ibufexeassign++;
#endif
      ibuf_assign_fragment(regs, ia);
      return;
  }
#endif
  debugmsg("get bef actpage %p\n", regs->actpage);
  fragentry = regs->actpage[ia & STORAGE_KEY_BYTEMASK & 
                            (FRAG_BYTESIZE * FRAG_BUFFER - 1)];

  debugmsg("get aft actpage\n");

  if (fragentry && ia == fragentry->ia)
  {
#if 0
      if (*fragentry->valid)
      {
          regs->actentry = fragentry;
#ifdef IBUF_STAT
          regs->ibufexecute++;
#endif
          return;
      }
      debugmsg("ibuf_get end\n");
      regs->actpage = NULL;
#ifdef IBUF_STAT
      regs->ibufinterpret++;
#endif
      longjmp(regs->progjmp, 0);
#else
      regs->actentry = fragentry;
#ifdef IBUF_STAT
      regs->ibufexecute++;
#endif
      return;
#endif
  }

  /* fragment not valid (invalidated or new */
  debugmsg("ibuf_get end bef compile\n");
  ibuf_assign_fragment(regs, ia);
}
#endif
