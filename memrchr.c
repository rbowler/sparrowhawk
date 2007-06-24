/************************************************************************/
/*                                                                      */
/*       memrchr          Right to Left memory scan                     */
/*                                                                      */
/************************************************************************/

// $Id: memrchr.c,v 1.7 2006/12/08 09:43:29 jj Exp $
//
// $Log: memrchr.c,v $
// Revision 1.7  2006/12/08 09:43:29  jj
// Add CVS message log
//

#include "hstdinc.h"

#define _MEMRCHR_C_
#define _HUTIL_DLL_

#include "hercules.h"

#if !defined( HAVE_MEMRCHR )

#include "memrchr.h"

DLL_EXPORT void *memrchr(const void *buf, int c, size_t num)
{
   unsigned char *pMem;
   if (num == 0)
   {
      return NULL;
   }
   for (pMem = (unsigned char *) buf + num - 1; pMem >= (unsigned char *) buf; pMem--)
   {
      if (*pMem == (unsigned char) c) break;
   }
   if (pMem >= (unsigned char *) buf)
   {
      return ((void *) pMem);
   }
   return NULL;
}

#endif // !defined(HAVE_MEMRCHR)
