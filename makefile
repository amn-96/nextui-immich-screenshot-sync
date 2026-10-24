PLATFORM := tg5040
DOCKER ?= $(or $(shell command -v docker),$(HOME)/.orbstack/bin/docker)
IMAGE := ghcr.io/loveretro/$(PLATFORM)-toolchain:latest
RUN = $(DOCKER) run --rm $(1) -v $(CURDIR):/work -w /work -e PLATFORM=$(PLATFORM) -e UNION_PLATFORM=$(PLATFORM) $(IMAGE)

ELF := build/$(PLATFORM)/immich.elf
# NextUI lists the pak by its folder name.
PAK_NAME := Immich Screenshot Sync.pak
PAK := build/$(PAK_NAME)

# Brick firmware CA bundle predates current roots; ship Mozilla's.
CACERT := build/cacert.pem

.PHONY: all pak shell clean test deploy logs

all:
	$(call RUN,) make -f build.mk

$(CACERT):
	mkdir -p build
	curl -fsSL -o $@ https://curl.se/ca/cacert.pem

pak: all $(CACERT)
	rm -rf "$(PAK)"
	mkdir -p "$(PAK)"
	cp pak/launch.sh pak/pak.json pak/immich-sync.sh $(ELF) $(CACERT) "$(PAK)/"

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
	$(BRICK_ENV) tar -C build -cf - "$(PAK_NAME)" | $(SSH) 'rm -rf /mnt/SDCARD/Tools/$(PLATFORM)/Immich.pak "/mnt/SDCARD/Tools/$(PLATFORM)/$(PAK_NAME)" && tar -xf - -C /mnt/SDCARD/Tools/$(PLATFORM)'
	$(BRICK_ENV) $(SSH) 'mkdir -p $(dir $(PREFS_REMOTE)) && test -f $(PREFS_REMOTE) || cat > $(PREFS_REMOTE)' < local/prefs.txt

logs:
	$(BRICK_ENV) $(SSH) 'cat /mnt/SDCARD/.userdata/$(PLATFORM)/logs/Immich.txt'
