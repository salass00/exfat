CPU  := i386
OS   := aros
HOST := $(CPU)-$(OS)

CC     := $(HOST)-gcc
AR     := $(HOST)-ar
RANLIB := $(HOST)-ranlib
RM     := rm -f

CFLAGS  := -O2 -s -Wall -Werror -Wwrite-strings -std=gnu99 \
	-fno-builtin-printf -fno-builtin-fprintf -fno-builtin-fputs \
	-I../filesysbox/include -I. -I./libexfat -I./libdiskio \
	-I./amigaos_support/include -DID_EXFAT_DISK=0x46415458
LDFLAGS := -nostartfiles
LIBS    := -ldebug

LIBEXFAT   := libexfat.a
LIBDISKIO  := libdiskio.a
LIBSUPPORT := libamigaos_support.a

TARGET  := exfat-handler
VERSION := 53

LIBEXFAT_OBJS := \
	libexfat/amiga_io.o \
	libexfat/cluster.o \
	libexfat/amiga_log.o \
	libexfat/lookup.o \
	libexfat/mount.o \
	libexfat/node.o \
	libexfat/time.o \
	libexfat/utf.o \
	libexfat/utils.o \

LIBDISKIO_OBJS := \
	libdiskio/setup.o \
	libdiskio/cleanup.o \
	libdiskio/update.o \
	libdiskio/query.o \
	libdiskio/readbytes.o \
	libdiskio/writebytes.o \
	libdiskio/flushiocache.o \
	libdiskio/deviceio.o \
	libdiskio/cachedio.o \
	libdiskio/blockcache.o \
	libdiskio/memhandler.o \
	libdiskio/splay.o

LIBSUPPORT_OBJS := \
	amigaos_support/debugf.o \
	amigaos_support/malloc.o \
	amigaos_support/printf.o \
	amigaos_support/puts.o \
	amigaos_support/strdup.o

STARTOBJ := exfat-startup_amigaos.o

OBJS := fuse/main.o \
	mkfs/mkexfat.o \
	mkfs/vbr.o \
	mkfs/fat.o \
	mkfs/cbm.o \
	mkfs/uct.o \
	mkfs/rootdir.o \
	mkfs/uctc.o

.PHONY: all
all: $(TARGET)

$(LIBEXFAT): $(LIBEXFAT_OBJS)
	$(AR) -crv $@ $^
	$(RANLIB) $@

$(LIBDISKIO): $(LIBDISKIO_OBJS)
	$(AR) -crv $@ $^
	$(RANLIB) $@

$(LIBSUPPORT): $(LIBSUPPORT_OBJS)
	$(AR) -crv $@ $^
	$(RANLIB) $@

$(STARTOBJ): $(TARGET)_rev.h

$(TARGET): $(STARTOBJ) $(OBJS) $(LIBEXFAT) $(LIBDISKIO) $(LIBSUPPORT)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

.PHONY: clean
clean:
	$(RM) $(TARGET) $(LIBEXFAT) $(LIBDISKIO) $(LIBSUPPORT) *.o */*.o

.PHONY: dist-clean
dist-clean:
	$(RM) $(LIBEXFAT) $(LIBDISKIO) $(LIBSUPPORT) *.o */*.o

.PHONY: revision
revision:
	bumprev $(VERSION) $(TARGET)

