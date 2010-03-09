// $Id: history.h 4102 2006-12-08 09:43:35Z jj $
//
// $Log$

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
