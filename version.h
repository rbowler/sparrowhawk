/*  VERSION.H   (c) Copyright Roger Bowler, 1999-2003            */
/*      ESA/390 Emulator Version definition                  */

/*-------------------------------------------------------------------*/
/* Header file defining the Hercules version number.             */
/*-------------------------------------------------------------------*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if !defined(VERSION)
#warning No version specified
#define VERSION Unknown                 /* Unkown version number     */
#endif

#define HDL_VERS_HERCULES VERSION
#define HDL_SIZE_HERCULES sizeof(VERSION)

void display_version(FILE *f, char *prog);

#define HERCULES_COPYRIGHT \
       "(c)Copyright 1999-2003 by Roger Bowler, Jan Jaeger, and others"
