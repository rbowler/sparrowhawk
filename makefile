#
# Makefile for Hercules S/370 and ESA/390 emulator
#
# Syntax:
#	make ARCH=370
#	make ARCH=390
#

VERSION  = 1.36

CFLAGS	 = -O3 -Wall -fPIC -DVERSION=$(VERSION) -DARCH=$(ARCH)
#	   -march=pentium -malign-double -mwide-multiply
LFLAGS	 = -lpthread

EXEFILES = hercules dasdinit dasdload tapecopy

TARFILES = makefile *.c *.h hercules.cnf tapeconv.jcl dasdlist

HRC_OBJS = impl.o config.o panel.o ipl.o cpu.o assist.o \
	   dat.o decimal.o block.o stack.o xmem.o \
	   channel.o service.o ckddasd.o fbadasd.o \
	   tapedev.o cardrdr.o printer.o console.o

DIN_OBJS = dasdinit.o

DLD_OBJS = dasdload.o

TCY_OBJS = tapecopy.o

HEADERS  = hercules.h esa390.h

all:	   $(EXEFILES)

hercules:  $(HRC_OBJS)
	cc $(LFLAGS) -o hercules $(HRC_OBJS)

dasdinit:  $(DIN_OBJS)
	cc -o dasdinit $(DIN_OBJS)

tapecopy:  $(TCY_OBJS)
	cc -o tapecopy $(TCY_OBJS)

dasdload:  $(DLD_OBJS)
	cc -o dasdload $(DLD_OBJS)

assist.o:  assist.c $(HEADERS)

cardrdr.o: cardrdr.c $(HEADERS)

config.o:  config.c $(HEADERS) makefile

console.o: console.c $(HEADERS) makefile

panel.o:   panel.c $(HEADERS)

printer.o: printer.c $(HEADERS)

cpu.o:	   cpu.c $(HEADERS)

dat.o:	   dat.c $(HEADERS)

decimal.o: decimal.c $(HEADERS)

stack.o:   stack.c $(HEADERS)

block.o:   block.c $(HEADERS)

xmem.o:    xmem.c $(HEADERS)

impl.o:    impl.c $(HEADERS) makefile

ipl.o:	   ipl.c $(HEADERS)

channel.o: channel.c $(HEADERS)

service.o: service.c $(HEADERS)

tapedev.o: tapedev.c $(HEADERS)

ckddasd.o: ckddasd.c $(HEADERS)

fbadasd.o: fbadasd.c $(HEADERS)

dasdinit.o: dasdinit.c $(HEADERS) makefile

tapecopy.o: tapecopy.c $(HEADERS) makefile

dasdload.o: dasdload.c $(HEADERS) makefile

clean:
	rm -f $(EXEFILES) *.o

tar:
	tar cvzf hercules-$(VERSION).tar.gz $(TARFILES)

