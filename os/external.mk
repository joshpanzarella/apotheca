# Apotheca's Buildroot external tree: our packages, and one hook on the kernel.
include $(sort $(wildcard $(BR2_EXTERNAL_APOTHECA_PATH)/package/*/*.mk))

# The panel's start-up sequence is a firmware file (panels/<compatible>.panel)
# that the generic MIPI panel driver asks for while it probes. The driver is
# built in, so it probes before the root file system is there: the files are
# built into the kernel (CONFIG_EXTRA_FIRMWARE, linux.fragment) from this
# folder, copied into the kernel tree after it is patched.
define APOTHECA_LINUX_PANEL_FIRMWARE
	mkdir -p $(LINUX_DIR)/firmware-apotheca
	cp -r $(BR2_EXTERNAL_APOTHECA_PATH)/board/rg40xxv/firmware/panels \
		$(LINUX_DIR)/firmware-apotheca/
endef
LINUX_POST_PATCH_HOOKS += APOTHECA_LINUX_PANEL_FIRMWARE

# The kernel's first program (board/rg40xxv/early/init.c): a small static
# /init in an initramfs built into the kernel (CONFIG_INITRAMFS_SOURCE,
# linux.fragment), which writes the kernel's messages to the card before it
# mounts the root. Built into the kernel's tree before every kernel build;
# the initramfs is made again only when the program changed, and the file's
# date is the build's fixed one, so the kernel is the same from one build to
# the next.
define APOTHECA_LINUX_EARLY_INIT
	mkdir -p $(LINUX_DIR)/apotheca-early
	cp $(BR2_EXTERNAL_APOTHECA_PATH)/board/rg40xxv/early/initramfs.list $(LINUX_DIR)/apotheca-early/initramfs.list
	$(TARGET_CC) -static -Os -s -Wall -Wextra -Werror -Wno-format-truncation \
		-o $(LINUX_DIR)/apotheca-early/init.new \
		$(BR2_EXTERNAL_APOTHECA_PATH)/board/rg40xxv/early/init.c
	cmp -s $(LINUX_DIR)/apotheca-early/init.new $(LINUX_DIR)/apotheca-early/init || \
		{ mv $(LINUX_DIR)/apotheca-early/init.new $(LINUX_DIR)/apotheca-early/init; \
		  rm -f $(LINUX_DIR)/usr/initramfs_data.cpio $(LINUX_DIR)/usr/initramfs_inc_data; }
	rm -f $(LINUX_DIR)/apotheca-early/init.new
	touch -d @$(SOURCE_DATE_EPOCH) $(LINUX_DIR)/apotheca-early/init $(LINUX_DIR)/apotheca-early/initramfs.list
endef
LINUX_PRE_BUILD_HOOKS += APOTHECA_LINUX_EARLY_INIT

# SDL2's "offscreen" video driver (no screen at all), so the desk test can
# have Apotheca draw a still under qemu (os/tools/qemu-test.py). Buildroot
# turns it off; the later flag wins.
SDL2_CONF_OPTS += --enable-video-offscreen
