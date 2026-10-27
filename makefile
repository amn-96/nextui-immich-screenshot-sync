PLATFORM ?= tg5040
PLATFORMS := tg5040 tg5050 h700
DOCKER ?= $(or $(shell command -v docker),$(HOME)/.orbstack/bin/docker)
# muos has no toolchain image of its own; it builds with tg5040's.
TOOLCHAIN ?= $(if $(filter muos,$(PLATFORM)),tg5040,$(PLATFORM))
IMAGE := ghcr.io/loveretro/$(TOOLCHAIN)-toolchain:latest
# CI runs inside a container whose workspace is not a host path, so it shares volumes instead of bind-mounting.
MOUNT ?= -v $(CURDIR):/work
WORKDIR ?= /work
RUN = $(DOCKER) run --rm $(1) $(MOUNT) -w $(WORKDIR) -e PLATFORM=$(PLATFORM) -e UNION_PLATFORM=$(PLATFORM) $(IMAGE)

# NextUI lists the pak by its folder name.
PAK_NAME := Immich Sync.pak
PAK := build/$(PAK_NAME)
PAK_PLATFORMS ?= $(PLATFORM)
# GitHub turns spaces in asset names into dots; must match release_filename in pak.json.
RELEASE_ZIP := Immich.Sync.pak.zip

# Brick firmware CA bundle predates current roots; ship Mozilla's.
CACERT := build/cacert.pem

.PHONY: all pak release muxapp shell clean test deploy logs

all:
	$(call RUN,) make -f build.mk

$(CACERT):
	mkdir -p build
	curl -fsSL -o $@ https://curl.se/ca/cacert.pem

pak: $(CACERT)
	rm -rf "$(PAK)"
	mkdir -p "$(PAK)"
	cp pak.json pak/launch.sh pak/immich-sync.sh pak/NotoEmoji.ttf pak/NotoEmoji-OFL.txt $(CACERT) "$(PAK)/"
	for p in $(PAK_PLATFORMS); do \
		$(MAKE) all PLATFORM=$$p && mkdir -p "$(PAK)/bin/$$p" && cp build/$$p/immich.elf "$(PAK)/bin/$$p/" || exit 1; \
	done

# Pak Store format: the zip holds the pak folder's contents, with a binary per platform.
release:
	$(MAKE) pak PAK_PLATFORMS="$(PLATFORMS)"
	rm -f build/$(RELEASE_ZIP)
	cd "$(PAK)" && zip -qr ../$(RELEASE_ZIP) .

MUXAPP_NAME := Immich Sync
MUXAPP := build/$(MUXAPP_NAME).muxapp
MUXAPP_STAGE := build/muxapp
MUXAPP_RES := vendor/NextUI/skeleton/SYSTEM/res

muxapp: $(CACERT)
	$(MAKE) all PLATFORM=muos
	rm -rf $(MUXAPP_STAGE) "$(MUXAPP)"
	mkdir -p "$(MUXAPP_STAGE)/$(MUXAPP_NAME)/.system/res"
	cp -r muxapp/mux_launch.sh muxapp/mux_lang.ini muxapp/glyph build/muos/immich.elf $(CACERT) pak/NotoEmoji.ttf pak/NotoEmoji-OFL.txt "$(MUXAPP_STAGE)/$(MUXAPP_NAME)/"
	cp $(MUXAPP_RES)/assets@[234]x.png $(MUXAPP_RES)/inputs@[234]x.png $(MUXAPP_RES)/font1.ttf "$(MUXAPP_STAGE)/$(MUXAPP_NAME)/.system/res/"
	chmod +x "$(MUXAPP_STAGE)/$(MUXAPP_NAME)/immich.elf" "$(MUXAPP_STAGE)/$(MUXAPP_NAME)/mux_launch.sh"
	cd $(MUXAPP_STAGE) && zip -qr "../$(MUXAPP_NAME).muxapp" "$(MUXAPP_NAME)"

shell:
	$(call RUN,-it) /bin/bash

clean:
	rm -rf build

test:
	$(call RUN,) make -f build.mk test

# Needs local/brick.env (BRICK_IP, BRICK_USER, BRICK_PWD); pushes local/prefs.txt only if the device has none.
# Brick firmware has no scp, so files go over ssh via tar/cat.
BRICK_ENV = . local/brick.env && export SSHPASS="$$BRICK_PWD" &&
SSH = sshpass -e ssh -o StrictHostKeyChecking=accept-new $$BRICK_USER@$$BRICK_IP
PREFS_REMOTE := /mnt/SDCARD/.userdata/shared/Immich/prefs.txt

deploy: pak
	$(BRICK_ENV) tar -C build -cf - "$(PAK_NAME)" | $(SSH) 'rm -rf /mnt/SDCARD/Tools/$(PLATFORM)/Immich.pak "/mnt/SDCARD/Tools/$(PLATFORM)/Immich Screenshot Sync.pak" "/mnt/SDCARD/Tools/$(PLATFORM)/$(PAK_NAME)" && tar -xf - -C /mnt/SDCARD/Tools/$(PLATFORM)'
	$(BRICK_ENV) $(SSH) 'mkdir -p $(dir $(PREFS_REMOTE)) && test -f $(PREFS_REMOTE) || cat > $(PREFS_REMOTE)' < local/prefs.txt

logs:
	$(BRICK_ENV) $(SSH) 'cat /mnt/SDCARD/.userdata/$(PLATFORM)/logs/Immich.txt'
