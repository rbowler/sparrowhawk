#
# Makefile for Hercules S/370 and ESA/390 emulator
#
# This makefile will make executables for both architectures at the same
# time; the 370 program is hercules-370, and the 390 one is hercules-390.
#
#

VERSION  = 1.67

# Change this if you want to install the Hercules executables somewhere
#   besides /usr/bin. The $PREFIX (which defaults to nothing) can be
#   overridden in the make command line, as in "PREFIX=/foo make install"
#   (the directory is only used when installing).
DESTDIR  = $(PREFIX)/usr/bin

#CFLAGS	 = -O0 -Wall -DARCH=390
CFLAGS	 = -O2 -Wall -fPIC -malign-double -DARCH=390
#	   -march=pentium -malign-double -mwide-multiply
#CFL_370  = -O0 -Wall -DARCH=370
CFL_370  = -O2 -Wall -fPIC -malign-double -DARCH=370
#	   -march=pentium -malign-double -mwide-multiply
LFLAGS	 = -lpthread

# Uncomment these lines for NetBSD, with either the unproven-pthreads
#   or pth packages
#CFLAGS += -I/usr/pkg/pthreads/include -I/usr/pkg/include
#CFL_370	+= -I/usr/pkg/pthreads/include -I/usr/pkg/include
#LFLAGS += -L/usr/pkg/pthreads/lib -R/usr/pkg/pthreads
#LFLAGS += -L/usr/pkg/lib -R/usr/pkg/pthreads/lib

EXEFILES = hercules-370 hercules-390 \
	   dasdinit dasdisup dasdload dasdls dasdpdsu \
	   tapecopy tapemap tapesplit

TARFILES = makefile *.c *.h hercules.cnf tapeconv.jcl dasdlist \
	   obj370 obj390 html zzsa.cnf zzsacard.bin

HRC_370_OBJS = obj370/impl.o obj370/config.o obj370/panel.o \
	   obj370/ipl.o obj370/assist.o obj370/dat.o \
	   obj370/stack.o obj370/cpu.o \
           obj370/general.o obj370/control.o obj370/io.o \
	   obj370/decimal.o obj370/service.o obj370/opcode.o \
	   obj370/diagnose.o obj370/diagmssf.o obj370/vm.o \
	   obj370/channel.o obj370/ckddasd.o obj370/fbadasd.o \
	   obj370/tapedev.o obj370/cardrdr.o obj370/cardpch.o \
	   obj370/printer.o obj370/console.o obj370/external.o \
	   obj370/float.o obj370/ctcadpt.o obj370/trace.o \
	   obj370/machchk.o obj370/vector.o obj370/xstore.o \
	   obj370/cmpsc.o
           

HRC_390_OBJS = obj390/impl.o obj390/config.o obj390/panel.o \
	   obj390/ipl.o obj390/assist.o obj390/dat.o \
	   obj390/stack.o obj390/cpu.o \
           obj390/general.o obj390/control.o obj390/io.o \
	   obj390/decimal.o obj390/service.o obj390/opcode.o \
	   obj390/diagnose.o obj390/diagmssf.o obj390/vm.o \
	   obj390/channel.o obj390/ckddasd.o obj390/fbadasd.o \
	   obj390/tapedev.o obj390/cardrdr.o obj390/cardpch.o \
	   obj390/printer.o obj390/console.o obj390/external.o \
	   obj390/float.o obj390/ctcadpt.o obj390/trace.o \
	   obj390/machchk.o obj390/vector.o obj390/xstore.o \
	   obj390/cmpsc.o obj390/sie.o
           

DIN_OBJS = dasdinit.o dasdutil.o

DIS_OBJS = dasdisup.o dasdutil.o

DLD_OBJS = dasdload.o dasdutil.o

DLS_OBJS = dasdls.o dasdutil.o

DPU_OBJS = dasdpdsu.o dasdutil.o

TCY_OBJS = tapecopy.o

TMA_OBJS = tapemap.o

TSP_OBJS = tapesplit.o

HEADERS  = hercules.h esa390.h version.h opcode.h inline.h

all:	   $(EXEFILES)

hercules-370:  $(HRC_370_OBJS)
	$(CC) -o hercules-370 $(HRC_370_OBJS) $(LFLAGS)

hercules-390:  $(HRC_390_OBJS)
	$(CC) -o hercules-390 $(HRC_390_OBJS) $(LFLAGS)

$(HRC_370_OBJS): obj370/%.o: %.c $(HEADERS)
	$(CC) $(CFL_370) -o $@ -c $<

$(HRC_390_OBJS): obj390/%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ -c $<

dasdinit:  $(DIN_OBJS)
	$(CC) -o dasdinit $(DIN_OBJS)

dasdisup:  $(DIS_OBJS)
	$(CC) -o dasdisup $(DIS_OBJS)

dasdload:  $(DLD_OBJS)
	$(CC) -o dasdload $(DLD_OBJS)

dasdls:  $(DLS_OBJS)
	$(CC) -o dasdls $(DLS_OBJS)

dasdpdsu:  $(DPU_OBJS)
	$(CC) -o dasdpdsu $(DPU_OBJS)

tapecopy:  $(TCY_OBJS)
	$(CC) -o tapecopy $(TCY_OBJS)

tapemap:  $(TMA_OBJS)
	$(CC) -o tapemap $(TMA_OBJS)

tapesplit:  $(TSP_OBJS)
	$(CC) -o tapesplit $(TSP_OBJS)

dasdinit.o: dasdinit.c $(HEADERS) dasdblks.h

dasdisup.o: dasdisup.c $(HEADERS) dasdblks.h

dasdload.o: dasdload.c $(HEADERS) dasdblks.h

dasdls.o: dasdls.c $(HEADERS) dasdblks.h

dasdpdsu.o: dasdpdsu.c $(HEADERS) dasdblks.h

dasdutil.o: dasdutil.c $(HEADERS) dasdblks.h

tapecopy.o: tapecopy.c $(HEADERS)

tapemap.o: tapemap.c $(HEADERS)

tapesplit.o: tapesplit.c $(HEADERS)

clean:
	rm -rf $(EXEFILES) *.o obj370 obj390; mkdir obj370 obj390

tar:    clean
	(cd ..; tar cvzf hercules-$(VERSION).tar.gz hercules-$(VERSION))

install:  $(EXEFILES)
	cp $(EXEFILES) $(DESTDIR)
	cp dasdlist $(DESTDIR)
