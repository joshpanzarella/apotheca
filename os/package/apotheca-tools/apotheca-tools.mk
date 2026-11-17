################################################################################
#
# apotheca-tools
#
################################################################################

APOTHECA_TOOLS_VERSION = local
APOTHECA_TOOLS_SITE = $(BR2_EXTERNAL_APOTHECA_PATH)/package/apotheca-tools/src
APOTHECA_TOOLS_SITE_METHOD = local
APOTHECA_TOOLS_LICENSE = Apotheca (no licence chosen)
APOTHECA_TOOLS_REDISTRIBUTE = NO
APOTHECA_TOOLS_DEPENDENCIES = sdl2 host-pkgconf

define APOTHECA_TOOLS_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -o $(@D)/padprobe $(@D)/padprobe.c \
		`$(PKG_CONFIG_HOST_BINARY) --cflags --libs sdl2`
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -o $(@D)/fakepad $(@D)/fakepad.c
endef

define APOTHECA_TOOLS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/padprobe $(TARGET_DIR)/usr/libexec/apotheca/padprobe
	$(INSTALL) -D -m 0755 $(@D)/fakepad $(TARGET_DIR)/usr/libexec/apotheca/fakepad
endef

$(eval $(generic-package))
