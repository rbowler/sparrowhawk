#
# Makefile for Hercules S/370 and ESA/390 emulator
#
# This makefile will make executables for both architectures at the same
# time; the 370 program is hercules-370, and the 390 one is hercules-390.
#
#

VERSION  = 1.71

# Change this if you want to install the Hercules executables somewhere
#   besides /usr/bin. The $PREFIX (which defaults to nothing) can be
#   overridden in the make command line, as in "PREFIX=/foo make install"
#   (the directory is only used when installing).
DESTDIR  = $(PREFIX)/usr/bin

# Standard flags for all architectures
CFLAGS	 = -O2 -Wall -fPIC -DARCH=390
CFL_370  = -O2 -Wall -fPIC -DARCH=370
LFLAGS	 = -lpthread -lz

# Add default flags for Pentium compilations
ifndef HOST_ARCH
CFLAGS	 += -malign-double -march=pentium
CFL_370  += -malign-double -march=pentium
endif

# Handle host architecture if specified
ifeq ($(HOST_ARCH),i386)
CFLAGS	 += -malign-double
CFL_370	 += -malign-double
endif
ifeq ($(HOST_ARCH),i586)
CFLAGS	 += -malign-double -march=pentium
CFL_370  += -malign-double -march=pentium
endif
ifeq ($(HOST_ARCH),i686)
CFLAGS	 += -malign-double -march=pentiumpro
CFL_370  += -malign-double -march=pentiumpro
endif

# Reverse the comments below to disable Compressed CKD Dasd support
#CFLAGS	+= -DNO_CCKD
#CFL_370	+= -DNO_CCKD

# Uncomment these lines to enable Compressed CKD bzip2 compression
#CFLAGS	+= -DCCKD_BZIP2
#CFL_370	+= -DCCKD_BZIP2
#LFLAGS	+= -lbz2

# Uncomment these lines to enable HET bzip2 compression
#CFLAGS	+= -DHET_BZIP2
#CFL_370	+= -DHET_BZIP2
#LFLAGS	+= -lbz2

# Uncomment these lines for NetBSD, with either the unproven-pthreads
#   or pth packages
#CFLAGS  += -I/usr/pkg/pthreads/include -I/usr/pkg/include
#CFL_370  += -I/usr/pkg/pthreads/include -I/usr/pkg/include
#LFLAGS	 += -L/usr/pkg/pthreads/lib -R/usr/pkg/pthreads
#LFLAGS	 += -L/usr/pkg/lib -R/usr/pkg/pthreads/lib

EXEFILES = hercules-370 hercules-390 \
	   dasdinit dasdisup dasdload dasdls dasdpdsu \
	   tapecopy tapemap tapesplit \
	   cckd2ckd cckdcdsk ckd2cckd cckdcomp \
	   hetget hetinit hetmap hetupd

TARFILES = makefile *.c *.h hercules.cnf tapeconv.jcl dasdlist \
	   obj370 obj390 html zzsa.cnf zzsacard.bin \
	   cckddump.hla

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
	   obj370/cmpsc.o obj370/ibuf.o \
	   obj370/cckddasd.o obj370/cckdcdsk.o \
	   obj370/parser.o obj370/hetlib.o
           

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
	   obj390/cmpsc.o obj390/sie.o obj390/ibuf.o \
	   obj390/cckddasd.o obj390/cckdcdsk.o \
	   obj390/parser.o obj390/hetlib.o
           

DIN_OBJS = dasdinit.o dasdutil.o

DIS_OBJS = dasdisup.o dasdutil.o

DLD_OBJS = dasdload.o dasdutil.o

DLS_OBJS = dasdls.o dasdutil.o

DPU_OBJS = dasdpdsu.o dasdutil.o

TCY_OBJS = tapecopy.o

TLS_OBJS = tapelist.o

TMA_OBJS = tapemap.o

TSP_OBJS = tapesplit.o

CC2C_OBJ = cckd2ckd.o

CCHK_OBJ = cckdcdsk.o

C2CC_OBJ = ckd2cckd.o

COMP_OBJ = cckdcomp.o obj390/cckdcdsk.o

HGT_OBJS = hetget.o hetlib.o sllib.o

HIN_OBJS = hetinit.o hetlib.o sllib.o

HMA_OBJS = hetmap.o hetlib.o sllib.o

HUP_OBJS = hetupd.o hetlib.o sllib.o

HEADERS  = hercules.h esa390.h version.h opcode.h inline.h ibuf.h hetlib.h

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

tapelist:  $(TLS_OBJS)
	$(CC) -o tapemap $(TLS_OBJS)

tapemap:  $(TMA_OBJS)
	$(CC) -o tapemap $(TMA_OBJS)

tapesplit:  $(TSP_OBJS)
	$(CC) -o tapesplit $(TSP_OBJS)

hetget:  $(HGT_OBJS)
	$(CC) -o hetget $(HGT_OBJS) $(LFLAGS)

hetinit:  $(HIN_OBJS)
	$(CC) -o hetinit $(HGT_OBJS) $(LFLAGS)

hetmap:  $(HMA_OBJS)
	$(CC) -o hetmap $(HMA_OBJS) $(LFLAGS)

hetupd:  $(HUP_OBJS)
	$(CC) -o hetupd $(HUP_OBJS) $(LFLAGS)

dasdinit.o: dasdinit.c $(HEADERS) dasdblks.h

dasdisup.o: dasdisup.c $(HEADERS) dasdblks.h

dasdload.o: dasdload.c $(HEADERS) dasdblks.h

dasdls.o: dasdls.c $(HEADERS) dasdblks.h

dasdpdsu.o: dasdpdsu.c $(HEADERS) dasdblks.h

dasdutil.o: dasdutil.c $(HEADERS) dasdblks.h

tapecopy.o: tapecopy.c $(HEADERS)

tapelist.o: tapelist.c $(HEADERS)

tapemap.o: tapemap.c $(HEADERS)

tapesplit.o: tapesplit.c $(HEADERS)

hetget.o: hetget.c hetlib.h sllib.h

hetinit.o: hetinit.c hetlib.h sllib.h

hetmap.o: hetmap.c hetlib.h sllib.h

hetupd.o: hetupd.c hetlib.h sllib.h

cckd:      cckd2ckd cckdcdsk ckd2cckd cckd2comp

cckd2ckd:  $(CC2C_OBJ)
	$(CC) -o cckd2ckd $(CC2C_OBJ) $(LFLAGS)

cckdcdsk:  $(CCHK_OBJ)
	$(CC) -o cckdcdsk $(CCHK_OBJ) $(LFLAGS)

ckd2cckd:  $(C2CC_OBJ)
	$(CC) -o ckd2cckd $(C2CC_OBJ) $(LFLAGS)

cckdcomp:  $(COMP_OBJ)
	$(CC) -o cckdcomp $(COMP_OBJ) $(LFLAGS)

$(CCHK_OBJ): %.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -DCCKD_CHKDSK_MAIN -o $@ -c $<


clean:
	rm -rf $(EXEFILES) *.o obj370 obj390; mkdir obj370 obj390

tar:    clean
	(cd ..; tar cvzf hercules-$(VERSION).tar.gz hercules-$(VERSION))

install:  $(EXEFILES)
	cp $(EXEFILES) $(DESTDIR)
	cp dasdlist $(DESTDIR)
