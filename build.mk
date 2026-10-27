# Runs inside the toolchain container.
NEXTUI := vendor/NextUI/workspace
COMMON := $(NEXTUI)/all/common
PLAT := $(NEXTUI)/$(PLATFORM)/platform
ifeq ($(PLATFORM),muos)
PLAT := platform/muos
endif
BUILD := build/$(PLATFORM)
LIBWEBP := $(BUILD)/libwebp
MSETTINGS := $(BUILD)/msettings
SDLTTF := $(BUILD)/sdl_ttf

include $(PLAT)/makefile.env

CC := $(CROSS_COMPILE)gcc
SOURCE := $(wildcard src/*.c) vendor/cJSON/cJSON.c \
	$(addprefix $(COMMON)/,utils.c api.c config.c scaler.c) $(PLAT)/platform.c
# Toolchain SDL_ttf is 2.0.15 (UCS-2 only); link the vendored 2.22 for NextUI's code too.
LDFLAGS := $(filter-out -l$(SDL)_ttf,$(LDFLAGS))
# SDL_ttf.h includes SDL.h/begin_code.h unprefixed; h700's pkg-config SDL2 include dir doesn't exist.
CFLAGS := -I$(SDLTTF)/include $(CFLAGS) -I$(PREFIX_LOCAL)/include/SDL2
CFLAGS += $(OPT) -I$(COMMON) -I$(PLAT) -Ivendor/cJSON -Ivendor/libwebp/src -I$(MSETTINGS)/include \
	-DPLATFORM=\"$(PLATFORM)\" -std=gnu99
# BaseOS (H700) ships no libjpeg; the other platforms decode JPEG through the device's SDL2_image.
# STICKS_FROM_PLATFORM: only h700 reports which stick it has; see lstick_seen in src/grid.c.
ifeq ($(PLATFORM),h700)
CFLAGS += -DBUNDLED_JPEG -DSTICKS_FROM_PLATFORM -Ivendor/stb
endif
LDFLAGS += $(LIBWEBP)/libwebpdecoder.a $(SDLTTF)/libSDL2_ttf.a $(SDLTTF)/external/freetype/libfreetype.a
ifeq ($(PLATFORM),muos)
# msettings is a stub compiled straight into the ELF; muOS owns brightness and volume.
SOURCE += $(PLAT)/msettings.c
CFLAGS += -DSCREENSHOTS_DIR=\"/run/muos/storage/screenshot\" -DDOWNLOADS_DIR=\"/mnt/mmc/Pictures/Immich\" \
	-DNO_DAEMON -DSTICKS_FROM_PLATFORM -DFOLLOW_OS_THEME
MSETTINGS_LIB :=
else
LDFLAGS += -L$(MSETTINGS)/lib -lmsettings
MSETTINGS_LIB := $(MSETTINGS)/lib/libmsettings.so
endif

$(BUILD)/immich.elf: $(SOURCE) $(wildcard src/*.h) $(LIBWEBP)/libwebpdecoder.a $(SDLTTF)/libSDL2_ttf.a $(MSETTINGS_LIB)
	$(CC) $(SOURCE) -o $@ $(CFLAGS) $(LDFLAGS)

$(LIBWEBP)/libwebpdecoder.a:
	cmake -S vendor/libwebp -B $(LIBWEBP) -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_COMPILER=$(CC) -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
		-DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF \
		-DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF \
		-DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF \
		-DWEBP_BUILD_LIBWEBPMUX=OFF -DWEBP_BUILD_FUZZTEST=OFF -DBUILD_SHARED_LIBS=OFF
	cmake --build $(LIBWEBP) --target webpdecoder -j

$(SDLTTF)/libSDL2_ttf.a:
	cmake -S vendor/SDL_ttf -B $(SDLTTF) -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_COMPILER=$(CC) -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
		-DBUILD_SHARED_LIBS=OFF -DSDL2TTF_VENDORED=ON -DSDL2TTF_HARFBUZZ=OFF -DSDL2TTF_SAMPLES=OFF \
		-DSDL2TTF_INSTALL=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
	cmake --build $(SDLTTF) -j
	mkdir -p $(SDLTTF)/include/SDL2 && cp vendor/SDL_ttf/SDL_ttf.h $(SDLTTF)/include/SDL2/

$(MSETTINGS)/lib/libmsettings.so: $(NEXTUI)/$(PLATFORM)/libmsettings/msettings.c $(COMMON)/displaycal.c
	mkdir -p $(dir $@) $(MSETTINGS)/include
	cp $(NEXTUI)/$(PLATFORM)/libmsettings/msettings.h $(MSETTINGS)/include/
	$(CC) -shared -fpic -Werror -I$(COMMON) -o $@ $^ -ltinyalsa -ldl -lrt -lm

# Host tests: native gcc, fake curl on PATH, no network.
.PHONY: test
test:
	mkdir -p $(BUILD)
	gcc -std=gnu99 -pthread -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Ivendor/cJSON -Isrc tests/test_main.c vendor/cJSON/cJSON.c -o $(BUILD)/test_main
	$(BUILD)/test_main
