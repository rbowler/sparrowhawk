#
# Makefile for Hercules ESA/390 emulator
#

VERSION  = 1.18

CFLAGS	 = -O -Wall -fPIC -DVERSION=$(VERSION)
LFLAGS	 = -lpthread

all:	   cpu ipl

TARFILES = makefile *.c *.h hercules.cnf cpu ipl

ALL_OBJS = config.o panel.o cpu.o dat.o channel.o service.o \
	   simtape.o loc3270.o cardrdr.o printer.o \
	   ckddasd.o fbadasd.o

CPU_OBJS = cputest.o $(ALL_OBJS)

IPL_OBJS = ipl.o $(ALL_OBJS)

HEADERS  = hercules.h esa390.h

cpu:	   $(CPU_OBJS)
	cc $(LFLAGS) -o cpu $(CPU_OBJS)

ipl:	   $(IPL_OBJS)
	cc $(LFLAGS) -o ipl $(IPL_OBJS)

cardrdr.o: cardrdr.c $(HEADERS)

config.o:  config.c $(HEADERS)

panel.o:   panel.c $(HEADERS)

printer.o: printer.c $(HEADERS)

cpu.o:	   cpu.c $(HEADERS)

cputest.o: cputest.c $(HEADERS)

dat.o:	   dat.c $(HEADERS)

ipl.o:	   ipl.c $(HEADERS)

channel.o: channel.c $(HEADERS)

service.o: service.c $(HEADERS)

simtape.o: simtape.c $(HEADERS)

loc3270.o: loc3270.c $(HEADERS)

ckddasd.o: ckddasd.c $(HEADERS)

fbadasd.o: fbadasd.c $(HEADERS)

clean:
	rm -f cpu ipl *.o

tar:
	tar cvzf hercules-$(VERSION).tar.gz $(TARFILES)

fdtar:
	tar cvf /dev/fd0 $(TARFILES)
