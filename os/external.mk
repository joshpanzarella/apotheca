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

# SDL2's "offscreen" video driver (no screen at all), so the desk test can
# have Apotheca draw a still under qemu (os/tools/qemu-test.py). Buildroot
# turns it off; the later flag wins.
SDL2_CONF_OPTS += --enable-video-offscreen
