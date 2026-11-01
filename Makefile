PS3DEV ?= /mnt/d/ps3dev-sdk
CC = $(PS3DEV)/ppu/bin/ppu-gcc
STRIP = $(PS3DEV)/ppu/bin/ppu-strip
SPRXLINKER = $(PS3DEV)/bin/sprxlinker
MAKE_SELF_NPDRM = $(PS3DEV)/bin/make_self_npdrm
SFO = $(PS3DEV)/bin/sfo
PKG = $(PS3DEV)/bin/pkg
PACKAGE_FINALIZE = $(PS3DEV)/bin/package_finalize

TITLE_ID = FMSX00001
CONTENT_ID = UP0001-FMSX00001_00-0000000000000000

INCLUDES = -Isrc -Iupstream/fMSX -Iupstream/EMULib -Iupstream/Z80 \
           -I$(PS3DEV)/ppu/include -I$(PS3DEV)/ppu/include/SDL2 \
           -I$(PS3DEV)/portlibs/ppu/include -I$(PS3DEV)/portlibs/ppu/include/SDL2 \
           -I$(PS3DEV)/portlibs/ppu/include/freetype2

DEFINES = -D__PS3__ -D__PSL1GHT__ -DFMSX -DMSB_FIRST -DBPP32 -DSOUND -DZLIB -DNOUNCRYPT

CFLAGS = -O3 $(DEFINES) $(INCLUDES) -fsigned-char -fno-strict-aliasing -Wno-unused-result -Wno-format-truncation

LDFLAGS = -L$(PS3DEV)/ppu/lib -L$(PS3DEV)/portlibs/ppu/lib
LDLIBS = src/SDL_PSL1GHTrender.o -lSDL2_ttf -lfreetype -lSDL2 -lgcm_sys -lio -laudio -lsysutil -lrsx -lrt -llv2 -lm -lz

OBJS = \
	build/MSX.o \
	build/V9938.o \
	build/I8251.o \
	build/Patch.o \
	build/Z80.o \
	build/EMULib.o \
	build/Sound.o \
	build/AY8910.o \
	build/YM2413.o \
	build/SCC.o \
	build/I8255.o \
	build/WD1793.o \
	build/Floppy.o \
	build/FDIDisk.o \
	build/SHA1.o \
	build/MCF.o \
	build/unzip.o \
	build/ioapi.o \
	build/ps3_main.o \
	build/ps3_font.o \
	build/ps3_vkbd.o \
	build/ps3_menu.o

all: MSX4PS3.pkg

build:
	mkdir -p build

build/MSX.o: upstream/fMSX/MSX.c src/ps3_compat.h | build
	$(CC) $(CFLAGS) -include src/ps3_compat.h -c $< -o $@

build/V9938.o: upstream/fMSX/V9938.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/I8251.o: upstream/fMSX/I8251.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/Patch.o: upstream/fMSX/Patch.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/Z80.o: upstream/Z80/Z80.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/EMULib.o: upstream/EMULib/EMULib.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/Sound.o: upstream/EMULib/Sound.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/AY8910.o: upstream/EMULib/AY8910.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/YM2413.o: upstream/EMULib/YM2413.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/SCC.o: upstream/EMULib/SCC.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/I8255.o: upstream/EMULib/I8255.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/WD1793.o: upstream/EMULib/WD1793.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/Floppy.o: upstream/EMULib/Floppy.c src/direct.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/FDIDisk.o: upstream/EMULib/FDIDisk.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/SHA1.o: upstream/EMULib/SHA1.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/MCF.o: upstream/EMULib/MCF.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/unzip.o: src/unzip.c src/unzip.h src/ioapi.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/ioapi.o: src/ioapi.c src/ioapi.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/ps3_main.o: src/ps3_main.c src/ps3_frontend.h src/ps3_btn_icons.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/ps3_font.o: src/ps3_font.c src/ps3_frontend.h src/ps3_btn_icons.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/ps3_vkbd.o: src/ps3_vkbd.c src/ps3_frontend.h src/ps3_btn_icons.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/ps3_menu.o: src/ps3_menu.c src/ps3_frontend.h src/ps3_btn_icons.h src/unzip.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/fMSX.elf: $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

MSX4PS3.pkg: build/fMSX.elf
	mkdir -p pkg/USRDIR
	cp -f build/fMSX.elf pkg/USRDIR/fMSX.elf
	$(STRIP) pkg/USRDIR/fMSX.elf
	$(SPRXLINKER) pkg/USRDIR/fMSX.elf
	$(MAKE_SELF_NPDRM) pkg/USRDIR/fMSX.elf pkg/USRDIR/EBOOT.BIN $(CONTENT_ID)
	rm -f pkg/USRDIR/fMSX.elf
	$(SFO) -f sfo.xml pkg/PARAM.SFO --title="MSX4PS3 v1.00" --appid="$(TITLE_ID)"
	$(PKG) --contentid=$(CONTENT_ID) pkg/ MSX4PS3.pkg
	cp -f MSX4PS3.pkg MSX4PS3.gnpdrm.pkg
	$(PACKAGE_FINALIZE) MSX4PS3.gnpdrm.pkg
	cp -f MSX4PS3.pkg fMSX_PS3.pkg
	cp -f MSX4PS3.gnpdrm.pkg fmsx_PS3.gnpdrm.pkg

clean:
	rm -rf build MSX4PS3.pkg MSX4PS3.gnpdrm.pkg fMSX_PS3.pkg fmsx_PS3.gnpdrm.pkg pkg/PARAM.SFO pkg/USRDIR/EBOOT.BIN
