################################################################################
#
# apotheca
#
################################################################################

# Built from this repository itself, as it stands (not from a download).
APOTHECA_VERSION = local
APOTHECA_SITE = $(BR2_EXTERNAL_APOTHECA_PATH)/..
APOTHECA_SITE_METHOD = local
# Only the program's own sources are copied in: never the image's own
# build folder (it is under build-image/, inside the repository).
APOTHECA_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS = \
	--exclude .git --exclude 'build*' --exclude dist --exclude docs --exclude os

# Apotheca's own code has no licence chosen yet (the owner's call); the code
# built into it from others carries its own (the files below). Its source is
# kept out of legal-info until the owner says otherwise.
APOTHECA_LICENSE = Apotheca (no licence chosen), MIT (Phazed engine), Apache-2.0 (msfa), MIT (signalsmith), OFL-1.1 (Routed Gothic)
APOTHECA_LICENSE_FILES = third_party/easycz/LICENSE third_party/msfa/LICENSE \
	third_party/signalsmith/LICENSE-stretch.txt third_party/signalsmith/LICENSE-linear.txt \
	assets/fonts/routed-gothic-LICENSE.txt
APOTHECA_REDISTRIBUTE = NO

APOTHECA_DEPENDENCIES = sdl2
# The version shown on screen is 0.N, N the commits plus one. git is read
# here, in the repository: the copy being built has no history.
APOTHECA_CHANGES := $(shell git -C $(BR2_EXTERNAL_APOTHECA_PATH) rev-list --count HEAD 2>/dev/null || echo 0)
APOTHECA_CONF_OPTS = \
	-DAPOTHECA_BUILD_TESTS=OFF \
	-DAPOTHECA_CHANGES=$(APOTHECA_CHANGES)

APOTHECA_LIB = $(TARGET_DIR)/usr/lib/apotheca

define APOTHECA_INSTALL_TARGET_CMDS
	mkdir -p $(APOTHECA_LIB)/chords $(APOTHECA_LIB)/licences
	$(INSTALL) -m 0755 $(@D)/apotheca $(APOTHECA_LIB)/apotheca
	$(INSTALL) -m 0644 $(@D)/assets/fonts/routed-gothic-narrow.ttf $(APOTHECA_LIB)/
	$(INSTALL) -m 0644 $(@D)/assets/chords/*.json $(APOTHECA_LIB)/chords/
	$(INSTALL) -m 0644 $(@D)/third_party/easycz/LICENSE $(APOTHECA_LIB)/licences/phazed-engine-LICENSE.txt
	$(INSTALL) -m 0644 $(@D)/third_party/easycz/THIRD_PARTY_LICENSES.md $(APOTHECA_LIB)/licences/phazed-engine-third-party.md
	$(INSTALL) -m 0644 $(@D)/third_party/msfa/LICENSE $(APOTHECA_LIB)/licences/fm6-core-msfa-LICENSE.txt
	$(INSTALL) -m 0644 $(@D)/third_party/signalsmith/LICENSE-stretch.txt $(APOTHECA_LIB)/licences/signalsmith-stretch-LICENSE.txt
	$(INSTALL) -m 0644 $(@D)/third_party/signalsmith/LICENSE-linear.txt $(APOTHECA_LIB)/licences/signalsmith-linear-LICENSE.txt
	$(INSTALL) -m 0644 $(@D)/assets/fonts/routed-gothic-LICENSE.txt $(APOTHECA_LIB)/licences/
	echo "0.$$(( $(APOTHECA_CHANGES) + 1 ))" > $(APOTHECA_LIB)/VERSION
endef

$(eval $(cmake-package))
