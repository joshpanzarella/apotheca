################################################################################
#
# rocknix-joypad
#
################################################################################

# The commit ROCKNIX pins (projects/ROCKNIX/packages/linux-drivers/rocknix-joypad).
ROCKNIX_JOYPAD_VERSION = ac1be0fa0c4e1f7feb9a900fc428d7ac6014029c
ROCKNIX_JOYPAD_SITE = https://github.com/ROCKNIX/rocknix-joypad.git
ROCKNIX_JOYPAD_SITE_METHOD = git
# The repository has no licence file; each source file says
# "SPDX-License-Identifier: GPL-2.0-or-later" at its top.
ROCKNIX_JOYPAD_LICENSE = GPL-2.0+
ROCKNIX_JOYPAD_LICENSE_FILES = rocknix-singleadc-joypad.c
# Only the single-ADC driver, as ROCKNIX builds it for the H700.
ROCKNIX_JOYPAD_MODULE_MAKE_OPTS = DEVICE=H700

$(eval $(kernel-module))
$(eval $(generic-package))
