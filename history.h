// $Id: history.h,v 1.4 2006/12/08 09:43:26 jj Exp $
//
// $Log: history.h,v $
// Revision 1.4  2006/12/08 09:43:26  jj
// Add CVS message log
//

#ifndef HISTORY_H
#define HISTORY_H

extern int history_requested;
extern char *historyCmdLine;

int history_init();
int history_add(char *cmdline);
int history_show();
int history_absolute_line(int x);
int history_relative_line(int x);
int history_next(void);
int history_prev(void);
int history_remove(void);
#endif
