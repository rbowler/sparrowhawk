#
# Makefile for Hercules ESA/390 emulator
#

VERSION  = 1.29

CFLAGS	 = -O3 -Wall -fPIC -DVERSION=$(VERSION)
#	   -march=pentium -malign-double -mwide-multiply
LFLAGS	 = -lpthread

all:	   cpu ipl dasdinit

TARFILES = makefile *.c *.h hercules.cnf cpu ipl dasdinit \
	   gentape.jcl

ALL_OBJS = config.o panel.o cpu.o assist.o dat.o decimal.o \
	   block.o stack.o xmem.o \
	   channel.o service.o ckddasd.o fbadasd.o \
	   simtape.o cardrdr.o printer.o console.o

CPU_OBJS = cputest.o $(ALL_OBJS)

IPL_OBJS = ipl.o $(ALL_OBJS)

DIN_OBJS = dasdinit.o

HEADERS  = hercules.h esa390.h

cpu:	   $(CPU_OBJS)
	cc $(LFLAGS) -o cpu $(CPU_OBJS)

ipl:	   $(IPL_OBJS)
	cc $(LFLAGS) -o ipl $(IPL_OBJS)

dasdinit:  $(DIN_OBJS)
	cc -o dasdinit $(DIN_OBJS)

assist.o:  assist.c $(HEADERS)

cardrdr.o: cardrdr.c $(HEADERS)

config.o:  config.c $(HEADERS)

console.o: console.c $(HEADERS) makefile

panel.o:   panel.c $(HEADERS)

printer.o: printer.c $(HEADERS)

cpu.o:	   cpu.c $(HEADERS)

cputest.o: cputest.c $(HEADERS) makefile

dat.o:	   dat.c $(HEADERS)

decimal.o: decimal.c $(HEADERS)

stack.o:   stack.c $(HEADERS)

block.o:   block.c $(HEADERS)

xmem.o:    xmem.c $(HEADERS)

ipl.o:	   ipl.c $(HEADERS) makefile

channel.o: channel.c $(HEADERS)

service.o: service.c $(HEADERS)

simtape.o: simtape.c $(HEADERS)

ckddasd.o: ckddasd.c $(HEADERS)

fbadasd.o: fbadasd.c $(HEADERS)

dasdinit.o: dasdinit.c $(HEADERS) makefile

clean:
	rm -f cpu ipl dasdinit *.o

tar:
	tar cvzf hercules-$(VERSION).tar.gz $(TARFILES)

fdtar:
	tar cvf /dev/fd0 $(TARFILES)
