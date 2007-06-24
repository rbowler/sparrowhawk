/* PARSER.H     (c) Copyright Nobody, 1999-2001                      */
/*              Simple parameter parser                              */

// $Id: parser.h,v 1.3 2006/12/08 09:43:29 jj Exp $
//
// $Log: parser.h,v $
// Revision 1.3  2006/12/08 09:43:29  jj
// Add CVS message log
//

#if !defined( _PARSER_H_ )
#define _PARSER_H_

#include "hercules.h"

#ifndef _PARSER_C_
#ifndef _HUTIL_DLL_
#define PAR_DLL_IMPORT DLL_IMPORT
#else   /* _HUTIL_DLL_ */
#define PAR_DLL_IMPORT extern
#endif  /* _HUTIL_DLL_ */
#else
#define PAR_DLL_IMPORT DLL_EXPORT
#endif

typedef struct _parser
{
    char *key;
    char *fmt;
} PARSER;

PAR_DLL_IMPORT int parser( PARSER *, char *, void * );

#endif /* !defined( _PARSER_H_ ) */
