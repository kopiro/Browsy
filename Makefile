BIN     = Browsy
TOOLCHAIN ?= /opt/Retro68-build/toolchain
ARCH    ?= m68k-unknown-elf
CC      = $(TOOLCHAIN)/bin/$(ARCH)-gcc
LD      = $(TOOLCHAIN)/bin/$(ARCH)-g++
AS      = $(TOOLCHAIN)/bin/$(ARCH)-as
AR      = $(TOOLCHAIN)/bin/$(ARCH)-ar
REZ     = $(TOOLCHAIN)/bin/Rez
FROM_HEX= xxd -r -ps
CSRC    = $(wildcard src/*.c src/**/*.c)
INC     = $(wildcard src/*.h src/**/*.h)
OBJ     = $(CSRC:.c=.o)
CDEP    = $(CSRC:.c=.d)
SHAREDIR= Shared
DEP_DIR = dep
LIB_DIR = lib
DEPS    = http_parser cstreams
LIBS    = $(DEPS:%=$(LIB_DIR)/lib%.a)
LIBS_L  = $(DEPS:%=-l%)
CFLAGS  = -MMD
CFLAGS += -O3 -DNDEBUG -std=c11
CFLAGS += -Wno-multichar -Wno-attributes -Wno-stringop-overflow -Werror -fcommon
CFLAGS += -include src/retro68_compat.h
CFLAGS += -Isrc -Idep/http-parser -Idep/c-streams/src
LDFLAGS = -L$(LIB_DIR) $(LIBS_L) -Wl,-Map=linkmap.txt -Wl,-gc-sections
SFLAGS  =

RSRC_HEX=$(wildcard rsrc/*/*.hex)
RSRC_TXT=$(wildcard rsrc/*/*.txt)
RSRC_DAT=$(RSRC_HEX:.hex=.dat) $(RSRC_TXT:.txt=.dat)

MINI_VMAC_DIR ?= emulator
MINI_VMAC_APP = $(MINI_VMAC_DIR)/Mini vMac.app
MINI_VMAC_SYS_DISK ?= $(MINI_VMAC_DIR)/System6.img

DOCKER_IMAGE ?= retro68-nopie
DOCKER_TOOLCHAIN = /Retro68-build/toolchain
DOCKER_ARCH = m68k-apple-macos

ifndef V
	QUIET_CC   = @echo ' CC   ' $<;
	QUIET_AS   = @echo ' AS   ' $<;
	QUIET_LINK = @echo ' LINK ' $@;
	QUIET_APPL = @echo ' APPL ' $@;
	QUIET_RSRC = @echo ' RSRC ' $@;
	QUIET_DSK  = @echo ' DSK  ' $@;
	QUIET_RUN  = @echo ' RUN  ' $<;
endif

# Main

all: deps $(BIN).bin

-include $(CDEP)

$(BIN).bin: $(OBJ) $(LIBS) rsrc-rez
	$(QUIET_LINK)$(LD) -o $@ $(filter %.o,$^) $(filter %.a,$^) $(LDFLAGS)
	$(QUIET_APPL)$(REZ) -a -o $@ -t APPL -c WWW6 rsrc-rez

$(BIN).dsk: $(BIN).bin
	$(QUIET_DSK)dd if=/dev/zero of=$@ bs=1k count=800 2>/dev/null && \
	hformat -l $(BIN) $@ >/dev/null && \
	hmount $@ >/dev/null && \
	hcopy -m $< : >/dev/null && \
	if [ -f page.html ]; then hcopy -t page.html : && hattrib -t TEXT -c ttxt page.html; fi >/dev/null && \
	humount >/dev/null

%.o: %.c
	$(QUIET_CC)$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.s
	$(QUIET_AS)$(AS) $(SFLAGS) -o $@ $<

# Resources

rsrc: $(RSRC_DAT) rsrc-rez

rsrc/%.dat: rsrc/%.hex
	$(QUIET_RSRC)python3 -c "import sys,binascii,re;sys.stdout.buffer.write(binascii.unhexlify(re.sub(r'\s','',open(sys.argv[1]).read())))" $< > $@

rsrc/TEXT/%.dat: rsrc/TEXT/%.txt
	$(QUIET_RSRC)tr '\n' '\r' < $< > $@

# Generate a Rez source file from binary resource data
rsrc-rez: $(RSRC_DAT)
	@python3 -c "\
	import os, sys; \
	out = open('rsrc-rez', 'w'); \
	out.write('/* Auto-generated from rsrc/ */\n'); \
	[( \
		parts := f.split('/'), \
		rtype := parts[1], \
		rid := os.path.splitext(parts[2])[0], \
		data := open(f, 'rb').read(), \
		hexstr := data.hex(), \
		lines := [hexstr[i:i+64] for i in range(0, len(hexstr), 64)], \
		out.write('data \'%s\' (%s) {\n' % (rtype, rid)), \
		[out.write('    \$$\"%s\"\n' % ln) for ln in lines], \
		out.write('};\n'), \
	) for f in sorted(sys.argv[1:])]" $^

# Dependencies

deps: $(LIBS) $(LIB_DIR)

$(LIB_DIR)/libhttp_parser.a: $(DEP_DIR)/http-parser/libhttp_parser.a $(LIB_DIR)
	cp $< $@

$(LIB_DIR)/libcstreams.a: $(DEP_DIR)/c-streams/libcstreams.a $(LIB_DIR)
	cp $< $@

$(DEP_DIR)/http-parser/libhttp_parser.a: $(DEP_DIR)/http-parser
	cd $< && make package CC=$(CC) AR=$(AR)

$(DEP_DIR)/c-streams/libcstreams.a: $(DEP_DIR)/c-streams
	cd $< && $(CC) -O3 -DNDEBUG -Wno-multichar -Wno-implicit-function-declaration \
		-include $(CURDIR)/src/retro68_compat.h -c -o src/stream.o src/stream.c && \
	$(CC) -O3 -DNDEBUG -Wno-multichar -Wno-implicit-function-declaration \
		-Wno-incompatible-pointer-types \
		-include $(CURDIR)/src/retro68_compat.h -c -o src/filestream.o src/filestream.c && \
	$(CC) -O3 -DNDEBUG -Wno-multichar -Wno-implicit-function-declaration \
		-include $(CURDIR)/src/retro68_compat.h -I$(CURDIR)/src \
		-c -o src/tcpstream.o src/tcpstream.c && \
	$(AR) rcs libcstreams.a src/stream.o src/filestream.o src/tcpstream.o

$(DEP_DIR)/http-parser: | $(DEP_DIR)
	git clone https://github.com/joyent/http-parser $@

$(DEP_DIR)/c-streams: | $(DEP_DIR)
	git clone https://github.com/clehner/c-streams $@

$(DEP_DIR) $(LIB_DIR):
	mkdir -p $@

# Docker build

docker-build:
	docker run --rm --platform linux/amd64 \
		-v $(CURDIR):/root/build -w /root/build \
		$(DOCKER_IMAGE) \
		make all TOOLCHAIN=$(DOCKER_TOOLCHAIN) ARCH=$(DOCKER_ARCH)

docker-dsk:
	docker run --rm --platform linux/amd64 \
		-v $(CURDIR):/root/build -w /root/build \
		$(DOCKER_IMAGE) \
		make $(BIN).dsk TOOLCHAIN=$(DOCKER_TOOLCHAIN) ARCH=$(DOCKER_ARCH)

# Running

run: $(BIN).dsk
	$(QUIET_RUN)open "$(CURDIR)/$(MINI_VMAC_APP)" --args "$(CURDIR)/$(MINI_VMAC_SYS_DISK)" "$(CURDIR)/$(BIN).dsk"

share: $(SHAREDIR)/$(BIN)

$(SHAREDIR)/$(BIN): $(BIN).APPL
	cp $(BIN).APPL $(SHAREDIR)/$(BIN)
	@cp .rsrc/$(BIN).APPL $(SHAREDIR)/.rsrc/$(BIN)
	@cp .finf/$(BIN).APPL $(SHAREDIR)/.finf/$(BIN)

BASILISK_DIR = emulator/basilisk
BASILISK_APP = $(BASILISK_DIR)/BasiliskII.app
BASILISK_BIN = $(BASILISK_APP)/Contents/MacOS/BasiliskII
BASILISK_PREFS_SYS7 = $(BASILISK_DIR)/basilisk_ii_prefs_sys7
BASILISK_PREFS_SYS6 = $(BASILISK_DIR)/basilisk_ii_prefs_sys6
BASILISK_SHARED = emulator/shared

run-basilisk: run-sys7

run-sys7: $(BIN).bin $(BIN).dsk
	@mkdir -p "$(BASILISK_SHARED)"
	@cp $(BIN).bin "$(BASILISK_SHARED)/$(BIN).bin"
	"$(CURDIR)/$(BASILISK_BIN)" --config "$(CURDIR)/$(BASILISK_PREFS_SYS7)"

run-sys6: $(BIN).dsk
	"$(CURDIR)/$(MINI_VMAC_APP)/Contents/MacOS/minivmac" "$(CURDIR)/$(MINI_VMAC_SYS_DISK)" "$(CURDIR)/$(BIN).dsk"

# Misc

wc:
	@wc -l $(CSRC) $(INC) | sort -n

clean:
	rm -f $(BIN) $(BIN).dsk $(BIN).bin $(BIN).gdb \
		$(OBJ) $(CDEP) rsrc/*/*.dat rsrc-rez linkmap.txt $(LIBS)
	rm -rf .rsrc .finf

.PHONY: clean wc run run-basilisk run-sys6 docker-build docker-dsk
