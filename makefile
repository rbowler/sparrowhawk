#
# Makefile for Hercules S/370 and ESA/390 emulator
#
# Syntax:
#	make ARCH=370
#	make ARCH=390
#

VERSION  = 1.41

CFLAGS	 = -O3 -Wall -fPIC -DVERSION=$(VERSION) -DARCH=$(ARCH)
#	   -march=pentium -malign-double -mwide-multiply
LFLAGS	 = -lpthread

EXEFILES = hercules dasdinit dasdisup dasdload dasdpdsu tapecopy

TARFILES = makefile *.c *.h hercules.cnf tapeconv.jcl dasdlist

HRC_OBJS = impl.o config.o panel.o ipl.o cpu.o assist.o \
	   dat.o decimal.o block.o stack.o xmem.o \
	   channel.o service.o ckddasd.o fbadasd.o \
	   tapedev.o cardrdr.o printer.o console.o \
	   diagnose.o

DIN_OBJS = dasdinit.o dasdutil.o

DIS_OBJS = dasdisup.o dasdutil.o

DLD_OBJS = dasdload.o dasdutil.o

DPU_OBJS = dasdpdsu.o dasdutil.o

TCY_OBJS = tapecopy.o

HEADERS  = hercules.h esa390.h

all:	   $(EXEFILES)

hercules:  $(HRC_OBJS)
	cc -o hercules $(HRC_OBJS) $(LFLAGS)

dasdinit:  $(DIN_OBJS)
	cc -o dasdinit $(DIN_OBJS)

dasdisup:  $(DIS_OBJS)
	cc -o dasdisup $(DIS_OBJS)

dasdload:  $(DLD_OBJS)
	cc -o dasdload $(DLD_OBJS)

dasdpdsu:  $(DPU_OBJS)
	cc -o dasdpdsu $(DPU_OBJS)

tapecopy:  $(TCY_OBJS)
	cc -o tapecopy $(TCY_OBJS)

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

diagnose.o: diagnose.c $(HEADERS)

dasdinit.o: dasdinit.c $(HEADERS) dasdblks.h makefile

dasdisup.o: dasdisup.c $(HEADERS) dasdblks.h makefile

dasdload.o: dasdload.c $(HEADERS) dasdblks.h makefile

dasdpdsu.o: dasdpdsu.c $(HEADERS) dasdblks.h makefile

dasdutil.o: dasdutil.c $(HEADERS) dasdblks.h

tapecopy.o: tapecopy.c $(HEADERS) makefile

clean:
	rm -f $(EXEFILES) *.o

tar:
	tar cvzf hercules-$(VERSION).tar.gz $(TARFILES)

