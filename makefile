#
# Makefile for Hercules S/370 and ESA/390 emulator
#
# Syntax:
#	make ARCH=370
#	make ARCH=390
#

VERSION  = 1.33

CFLAGS	 = -O3 -Wall -fPIC -DVERSION=$(VERSION) -DARCH=$(ARCH)
#	   -march=pentium -malign-double -mwide-multiply
LFLAGS	 = -lpthread

EXEFILES = cpu ipl dasdinit tapecopy xmitconv

TARFILES = makefile *.c *.h hercules.cnf tapeconv.jcl dasdlist

ALL_OBJS = config.o panel.o cpu.o assist.o dat.o decimal.o \
	   block.o stack.o xmem.o \
	   channel.o service.o ckddasd.o fbadasd.o \
	   tapedev.o cardrdr.o printer.o console.o

CPU_OBJS = cputest.o $(ALL_OBJS)

IPL_OBJS = ipl.o $(ALL_OBJS)

DIN_OBJS = dasdinit.o

TCY_OBJS = tapecopy.o

XMT_OBJS = xmitconv.o

HEADERS  = hercules.h esa390.h

all:	   $(EXEFILES)

cpu:	   $(CPU_OBJS)
	cc $(LFLAGS) -o cpu $(CPU_OBJS)

ipl:	   $(IPL_OBJS)
	cc $(LFLAGS) -o ipl $(IPL_OBJS)

dasdinit:  $(DIN_OBJS)
	cc -o dasdinit $(DIN_OBJS)

tapecopy:  $(TCY_OBJS)
	cc -o tapecopy $(TCY_OBJS)

xmitconv:  $(XMT_OBJS)
	cc -o xmitconv $(XMT_OBJS)

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

tapedev.o: tapedev.c $(HEADERS)

ckddasd.o: ckddasd.c $(HEADERS)

fbadasd.o: fbadasd.c $(HEADERS)

dasdinit.o: dasdinit.c $(HEADERS) makefile

tapecopy.o: tapecopy.c $(HEADERS) makefile

xmitconv.o: xmitconv.c $(HEADERS) makefile

clean:
	rm -f $(EXEFILES) *.o

tar:
	tar cvzf hercules-$(VERSION).tar.gz $(TARFILES)

