/* IBUF.H       (c) Copyright Juergen Dobrinski, 1994-2000                */
/*              Instruction prefetch and buffering                        */

/*-------------------------------------------------------------------*/
/* Assign compiled fragment                                          */
/*-------------------------------------------------------------------*/
static inline void ibuf_assign_fragment (REGS *regs, U32 ia)
{
FRAG *frag;
FRAGENTRY *fragentry;
U32  abs;                            /* Absolute storage address  */
BYTE akey;                           /* Bits 0-3=key, 4-7=zeroes  */

#if 0
  if (regs->actentry)
  {
      if ((((FRAGENTRY*)(regs->actentry))->inst[0] == 0x0a) ||
          (((FRAGENTRY*)(regs->actentry))->inst[0] == 0x82))
          logmsg("ibuf_assign %2x\n", ia);
  }
#endif

  /* search compiled fragment in buffer  */

  regs->ibufsearch++;
  debugmsg("search buffer\n");

  /* Obtain current access key from PSW */
  akey = regs->psw.pkey;

  /* Program check if instruction address is odd */
  regs->instvalid = 0;
  if (ia & 0x01)
      program_interrupt (regs, PGM_SPECIFICATION_EXCEPTION);

  abs = logical_to_abs (ia, 0, regs, ACCTYPE_INSTFETCH, akey);
  regs->instvalid = 1;

  frag = FRAG_BUFFERADDRESS(regs, abs);  /* HASH */

  regs->actfrag = frag;

  
  if (frag->valid)
  {
      fragentry = &frag->entry[frag->dict[ia & (FRAG_BYTESIZE - 1)]];
      if (ia == fragentry->ia)
      {
          debugmsg("ibuf_get end\n");
#if 0
          if (regs->actentry)
          {
              if ((((FRAGENTRY*)(regs->actentry))->inst[0] == 0x0a) ||
                  (((FRAGENTRY*)(regs->actentry))->inst[0] == 0x82))
                   logmsg("ibuf_assign found %2x\n", fragentry->ia);
          }
#endif
          regs->actentry = fragentry;
          return;
      }
  }
  else
      regs->ibufcodechange++;


  /* fragment not valid (invalidated or new */
  debugmsg("ibuf_get end bef compile\n");
  ibuf_compile_frag (regs, ia);
}

/*-------------------------------------------------------------------*/
/* Get compiled fragment                                             */
/*-------------------------------------------------------------------*/
static inline void ibuf_get_fragentry (REGS *regs, U32 ia)
{
FRAG *frag;
FRAGENTRY *fragentry;

//  debugmsg("ibuf_get begin %4x %llu\n", ia, regs->instcount);

#if 0
  if (regs->actentry)
  {
      if ((((FRAGENTRY*)(regs->actentry))->inst[0] == 0x0a) ||
          (((FRAGENTRY*)(regs->actentry))->inst[0] == 0x82))
          logmsg("ibuf_get %2x\n", ia);
  }
#endif

  /* check if address is in current segment */

  frag = regs->actfrag;

  fragentry = &frag->entry[frag->dict[ia & (FRAG_BYTESIZE - 1)]];
  
  if (ia == fragentry->ia)
  {
//      debugmsg("ibuf_get end\n");
#if 0
      if (regs->actentry)
      {
          if ((((FRAGENTRY*)(regs->actentry))->inst[0] == 0x0a) ||
              (((FRAGENTRY*)(regs->actentry))->inst[0] == 0x82))
              logmsg("ibuf_get found %2x\n", fragentry->ia);
      }
#endif
      regs->actentry = fragentry;
      return;
  }

  /* search compiled fragment in buffer  */

  ibuf_assign_fragment(regs, ia);
}
