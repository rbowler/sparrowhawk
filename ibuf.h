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

  
  if (frag->valid && (frag->minabs == (abs & FRAG_BYTEMASK)))
  {
      fragentry = &frag->entry[frag->dict[ia & (FRAG_BYTESIZE - 1)]];
      if (ia == fragentry->ia)
      {
          debugmsg("ibuf_get end\n");
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

  /* check if address is in current segment */

  frag = regs->actfrag;

  fragentry = &frag->entry[frag->dict[ia & (FRAG_BYTESIZE - 1)]];
  
  if (ia == fragentry->ia)
  {
      regs->actentry = fragentry;
      return;
  }

  /* search compiled fragment in buffer  */

  ibuf_assign_fragment(regs, ia);
}
