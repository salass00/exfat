CC     = m68k-amigaos-gcc
STRIP  = m68k-amigaos-strip
AR     = m68k-amigaos-ar
RANLIB = m68k-amigaos-ranlib

TARGET  = exFATFileSystem
VERSION = 53

INCLUDES = -I. -I./libexfat -I./libdiskio -I./amigaos_support/include
DEFINES  = -DID_EXFAT_DISK=0x46415458
WARNINGS = -Werror -Wall -Wwrite-strings

CFLAGS  = -noixemul -std=gnu99 -O2 -g -fomit-frame-pointer \
          -fno-builtin-printf -fno-builtin-fprintf -fno-builtin-fputs \
          $(INCLUDES) $(DEFINES) $(WARNINGS)
LDFLAGS = -noixemul -g -nostartfiles
LIBS    = -ldebug

STRIPFLAGS = -R.comment

ARCH_000 = -mcpu=68000 -mtune=68000
ARCH_020 = -mcpu=68020 -mtune=68020-60
ARCH_060 = -mcpu=68060 -mtune=68060

LIBEXFAT   = libexfat.a
LIBDISKIO  = libdiskio.a
LIBSUPPORT = libamigaos_support.a

LIBEXFAT_SRCS = \
	libexfat/amiga_io.c \
	libexfat/cluster.c \
	libexfat/amiga_log.c \
	libexfat/lookup.c \
	libexfat/mount.c \
	libexfat/node.c \
	libexfat/time.c \
	libexfat/utf.c \
	libexfat/utils.c \

LIBDISKIO_SRCS = \
	libdiskio/setup.c \
	libdiskio/cleanup.c \
	libdiskio/update.c \
	libdiskio/query.c \
	libdiskio/readbytes.c \
	libdiskio/writebytes.c \
	libdiskio/flushiocache.c \
	libdiskio/deviceio.c \
	libdiskio/cachedio.c \
	libdiskio/blockcache.c \
	libdiskio/memhandler.c \
	libdiskio/splay.c

LIBSUPPORT_SRCS = \
	amigaos_support/debugf.c \
	amigaos_support/malloc.c \
	amigaos_support/printf.c \
	amigaos_support/puts.c \
	amigaos_support/strdup.c

SRCS = \
	exfat-startup_amigaos.c \
	fuse/main.c \
	mkfs/mkexfat.c \
	mkfs/vbr.c \
	mkfs/fat.c \
	mkfs/cbm.c \
	mkfs/uct.c \
	mkfs/rootdir.c \
	mkfs/uctc.c

LIBEXFAT_OBJS_000 = $(addprefix obj/68000/,$(LIBEXFAT_SRCS:.c=.o))
LIBDISKIO_OBJS_000 = $(addprefix obj/68000/,$(LIBDISKIO_SRCS:.c=.o))
LIBSUPPORT_OBJS_000 = $(addprefix obj/68000/,$(LIBSUPPORT_SRCS:.c=.o))
OBJS_000 = $(addprefix obj/68000/,$(SRCS:.c=.o))
DEPS_000 = $(LIBEXFAT_OBJS_000:.o=.d) \
           $(LIBDISKIO_OBJS_000:.o=.d) \
           $(LIBSUPPORT_OBJS_000:.o=.d) \
           $(OBJS_000:.o=.d)

LIBEXFAT_OBJS_020 = $(addprefix obj/68020/,$(LIBEXFAT_SRCS:.c=.o))
LIBDISKIO_OBJS_020 = $(addprefix obj/68020/,$(LIBDISKIO_SRCS:.c=.o))
LIBSUPPORT_OBJS_020 = $(addprefix obj/68020/,$(LIBSUPPORT_SRCS:.c=.o))
OBJS_020 = $(addprefix obj/68020/,$(SRCS:.c=.o))
DEPS_020 = $(LIBEXFAT_OBJS_020:.o=.d) \
           $(LIBDISKIO_OBJS_020:.o=.d) \
           $(LIBSUPPORT_OBJS_020:.o=.d) \
           $(OBJS_020:.o=.d)

LIBEXFAT_OBJS_060 = $(addprefix obj/68060/,$(LIBEXFAT_SRCS:.c=.o))
LIBDISKIO_OBJS_060 = $(addprefix obj/68060/,$(LIBDISKIO_SRCS:.c=.o))
LIBSUPPORT_OBJS_060 = $(addprefix obj/68060/,$(LIBSUPPORT_SRCS:.c=.o))
OBJS_060 = $(addprefix obj/68060/,$(SRCS:.c=.o))
DEPS_060 = $(LIBEXFAT_OBJS_060:.o=.d) \
           $(LIBDISKIO_OBJS_060:.o=.d) \
           $(LIBSUPPORT_OBJS_060:.o=.d) \
           $(OBJS_060:.o=.d)

.PHONY: all
all: bin/$(TARGET).000 bin/$(TARGET).020 bin/$(TARGET).060

-include $(DEPS_000)
-include $(DEPS_020)
-include $(DEPS_060)

obj/68000/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -MM -MP -MT $(@:.o=.d) -MT $@ -MF $(@:.o=.d) $(CFLAGS) $<
	$(CC) $(ARCH_000) $(CFLAGS) -c -o $@ $<

obj/68020/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -MM -MP -MT $(@:.o=.d) -MT $@ -MF $(@:.o=.d) $(CFLAGS) $<
	$(CC) $(ARCH_020) $(CFLAGS) -c -o $@ $<

obj/68060/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -MM -MP -MT $(@:.o=.d) -MT $@ -MF $(@:.o=.d) $(CFLAGS) $<
	$(CC) $(ARCH_060) $(CFLAGS) -c -o $@ $<

obj/%/amigaos_support/malloc.o: CFLAGS += -fno-builtin

bin/$(LIBEXFAT).000: $(LIBEXFAT_OBJS_000)
	@mkdir -p $(dir $@)
	$(AR) -crv $@ $^
	$(RANLIB) $@

bin/$(LIBDISKIO).000: $(LIBDISKIO_OBJS_000)
	@mkdir -p $(dir $@)
	$(AR) -crv $@ $^
	$(RANLIB) $@

bin/$(LIBSUPPORT).000: $(LIBSUPPORT_OBJS_000)
	@mkdir -p $(dir $@)
	$(AR) -crv $@ $^
	$(RANLIB) $@

bin/$(TARGET).000.debug: $(OBJS) bin/$(LIBEXFAT).000 bin/$(LIBDISKIO).000 bin/$(LIBSUPPORT).000
	@mkdir -p $(dir $@)
	$(CC) $(ARCH_000) $(LDFLAGS) -o $@ $^ $(LIBS)

bin/$(TARGET).020.debug: $(OBJS) bin/$(LIBEXFAT).020 bin/$(LIBDISKIO).020 bin/$(LIBSUPPORT).020
	@mkdir -p $(dir $@)
	$(CC) $(ARCH_020) $(LDFLAGS) -o $@ $^ $(LIBS)

bin/$(TARGET).060.debug: $(OBJS) bin/$(LIBEXFAT).060 bin/$(LIBDISKIO).060 bin/$(LIBSUPPORT).060
	@mkdir -p $(dir $@)
	$(CC) $(ARCH_060) $(LDFLAGS) -o $@ $^ $(LIBS)

bin/$(TARGET).000: bin/$(TARGET).000.debug
	$(STRIP) $(STRIPFLAGS) -o $@ $<

bin/$(TARGET).020: bin/$(TARGET).020.debug
	$(STRIP) $(STRIPFLAGS) -o $@ $<

bin/$(TARGET).060: bin/$(TARGET).060.debug
	$(STRIP) $(STRIPFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -rf bin obj

.PHONY: revision
revision:
	bumprev -e is $(VERSION) exFATFileSystem
	bumprev -e is $(VERSION) exfat-handler

