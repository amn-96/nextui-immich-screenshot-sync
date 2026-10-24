# Runs inside the toolchain container.
NEXTUI := vendor/NextUI/workspace
COMMON := $(NEXTUI)/all/common
PLAT := $(NEXTUI)/$(PLATFORM)/platform
BUILD := build/$(PLATFORM)
LIBWEBP := $(BUILD)/libwebp
MSETTINGS := $(BUILD)/msettings

include $(PLAT)/makefile.env

CC := $(CROSS_COMPILE)gcc
SOURCE := $(wildcard src/*.c) vendor/cJSON/cJSON.c \
	$(addprefix $(COMMON)/,utils.c api.c config.c scaler.c) $(PLAT)/platform.c
CFLAGS += $(OPT) -I$(COMMON) -I$(PLAT) -Ivendor/cJSON -Ivendor/libwebp/src -I$(MSETTINGS)/include \
	-DPLATFORM=\"$(PLATFORM)\" -std=gnu99
LDFLAGS += -L$(MSETTINGS)/lib $(LIBWEBP)/libwebpdecoder.a -lmsettings

$(BUILD)/immich.elf: $(SOURCE) $(wildcard src/*.h) $(LIBWEBP)/libwebpdecoder.a $(MSETTINGS)/lib/libmsettings.so
	$(CC) $(SOURCE) -o $@ $(CFLAGS) $(LDFLAGS)

$(LIBWEBP)/libwebpdecoder.a:
	cmake -S vendor/libwebp -B $(LIBWEBP) -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_COMPILER=$(CC) -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
		-DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF \
		-DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF \
		-DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF \
		-DWEBP_BUILD_LIBWEBPMUX=OFF -DWEBP_BUILD_FUZZTEST=OFF -DBUILD_SHARED_LIBS=OFF
	cmake --build $(LIBWEBP) --target webpdecoder -j

$(MSETTINGS)/lib/libmsettings.so: $(NEXTUI)/$(PLATFORM)/libmsettings/msettings.c $(COMMON)/displaycal.c
	mkdir -p $(dir $@) $(MSETTINGS)/include
	cp $(NEXTUI)/$(PLATFORM)/libmsettings/msettings.h $(MSETTINGS)/include/
	$(CC) -shared -fpic -Werror -I$(COMMON) -o $@ $^ -ltinyalsa -ldl -lrt -lm

# Host tests: native gcc, fake curl on PATH, no network.
.PHONY: test
test:
	gcc -std=gnu99 -pthread -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Ivendor/cJSON -Isrc tests/test_main.c vendor/cJSON/cJSON.c -o $(BUILD)/test_main
	$(BUILD)/test_main
