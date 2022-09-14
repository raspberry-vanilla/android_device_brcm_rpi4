#
# Copyright (C) 2021-2022 KonstaKANG
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit device configuration
$(call inherit-product, device/brcm/rpi4/device.mk)

# Device identifier. This must come after all inclusions.
PRODUCT_DEVICE := rpi4
PRODUCT_NAME := aosp_rpi4
PRODUCT_BRAND := Raspberry
PRODUCT_MODEL := Raspberry Pi 4
PRODUCT_MANUFACTURER := Raspberry
PRODUCT_RELEASE_NAME := Raspberry Pi 4
