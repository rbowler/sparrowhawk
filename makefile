#
# Makefile for Hercules S/370 and ESA/390 emulator
#
# This makefile will make executables for both architectures at the same
# time; the 370 program is hercules-370, and the 390 one is hercules-390.
#
#

VERSION  = 1.50

CFLAGS	 = -O3 -Wall -fPIC -DVERSION=$(VERSION) -DARCH=390
#	   -march=pentium -malign-double -mwide-multiply
CFL_370  = -O3 -Wall -fPIC -DVERSION=$(VERSION) -DARCH=370
#	   -march=pentium -malign-double -mwide-multiply
LFLAGS	 = -lpthread

EXEFILES = hercules-370 hercules-390 \
	   dasdinit dasdisup dasdload dasdpdsu tapecopy

TARFILES = makefile *.c *.h hercules.cnf tapeconv.jcl dasdlist \
	   herc*.htm obj370 obj390

HRC_370_OBJS = obj370/impl.o obj370/config.o obj370/panel.o \
	   obj370/ipl.o obj370/cpu.o obj370/assist.o obj370/dat.o \
	   obj370/block.o obj370/stack.o obj370/xmem.o obj370/sort.o \
	   obj370/decimal.o obj370/diagnose.o obj370/service.o \
	   obj370/channel.o obj370/ckddasd.o obj370/fbadasd.o \
	   obj370/tapedev.o obj370/cardrdr.o obj370/cardpch.o \
	   obj370/printer.o obj370/console.o obj370/external.o

HRC_390_OBJS = obj390/impl.o obj390/config.o obj390/panel.o \
	   obj390/ipl.o obj390/cpu.o obj390/assist.o obj390/dat.o \
	   obj390/block.o obj390/stack.o obj390/xmem.o obj390/sort.o \
	   obj390/decimal.o obj390/diagnose.o obj390/service.o \
	   obj390/channel.o obj390/ckddasd.o obj390/fbadasd.o \
	   obj390/tapedev.o obj390/cardrdr.o obj390/cardpch.o \
	   obj390/printer.o obj390/console.o obj390/external.o

DIN_OBJS = dasdinit.o dasdutil.o

DIS_OBJS = dasdisup.o dasdutil.o

DLD_OBJS = dasdload.o dasdutil.o

DPU_OBJS = dasdpdsu.o dasdutil.o

TCY_OBJS = tapecopy.o

HEADERS  = hercules.h esa390.h

all:	   $(EXEFILES)

hercules-370:  $(HRC_370_OBJS)
	$(CC) -o hercules-370 $(HRC_370_OBJS) $(LFLAGS)

hercules-390:  $(HRC_390_OBJS)
	$(CC) -o hercules-390 $(HRC_390_OBJS) $(LFLAGS)

$(HRC_370_OBJS): obj370/%.o: %.c $(HEADERS) makefile
	$(CC) $(CFL_370) -o $@ -c $<

$(HRC_390_OBJS): obj390/%.o: %.c $(HEADERS) makefile
	$(CC) $(CFLAGS) -o $@ -c $<

dasdinit:  $(DIN_OBJS)
	$(CC) -o dasdinit $(DIN_OBJS)

dasdisup:  $(DIS_OBJS)
	$(CC) -o dasdisup $(DIS_OBJS)

dasdload:  $(DLD_OBJS)
	$(CC) -o dasdload $(DLD_OBJS)

dasdpdsu:  $(DPU_OBJS)
	$(CC) -o dasdpdsu $(DPU_OBJS)

tapecopy:  $(TCY_OBJS)
	$(CC) -o tapecopy $(TCY_OBJS)

dasdinit.o: dasdinit.c $(HEADERS) dasdblks.h makefile

dasdisup.o: dasdisup.c $(HEADERS) dasdblks.h makefile

dasdload.o: dasdload.c $(HEADERS) dasdblks.h makefile

dasdpdsu.o: dasdpdsu.c $(HEADERS) dasdblks.h makefile

dasdutil.o: dasdutil.c $(HEADERS) dasdblks.h

tapecopy.o: tapecopy.c $(HEADERS) makefile

clean:
	rm -f $(EXEFILES) *.o obj370/*.o obj390/*.o

tar:
	tar cvzf hercules-$(VERSION).tar.gz --exclude \*.o $(TARFILES)
