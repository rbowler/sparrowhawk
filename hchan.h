// $Id: hchan.h 4102 2006-12-08 09:43:35Z jj $
//
// $Log$

#ifndef __HCHAN_H__
#define __HCHAN_H__
/*
 * Hercules Generic Channel internal definitions
 * (c) Ivan Scott Warren 2004-2006
 *     based on work
 * (c) Roger Bowler, Jan Jaeger and Others 1999-2006
 * This code is covered by the QPL Licence
 */

static  int     hchan_init_exec(DEVBLK *,int,char **);
static  int     hchan_init_connect(DEVBLK *,int,char **);
static  int     hchan_init_int(DEVBLK *,int,char **);

#endif
