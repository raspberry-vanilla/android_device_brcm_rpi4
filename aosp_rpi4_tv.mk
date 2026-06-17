#
# Copyright (C) 2021-2023 KonstaKANG
#
# SPDX-License-Identifier: Apache-2.0
#

DEVICE_PATH := device/brcm/rpi4

# Inherit device configuration
$(call inherit-product, $(DEVICE_PATH)/device.mk)

PRODUCT_AAPT_PREF_CONFIG := xhdpi
PRODUCT_CHARACTERISTICS := tv

$(call inherit-product, device/google/atv/products/atv_base.mk)
$(call enforce-product-packages-exist,com.android.ranging vendor_tracing_descriptors)

# Android TV
PRODUCT_PACKAGES += \
    DocumentsUI \
    LeanbackIME \
    TvProvision \
    TvSampleLeanbackLauncher \
    TvSettingsTwoPanel

# Bluetooth
PRODUCT_VENDOR_PROPERTIES += \
    bluetooth.device.class_of_device=34,4,36

# Boot animation
PRODUCT_COPY_FILES += \
    device/google/atv/products/bootanimations/bootanimation.zip:$(TARGET_COPY_OUT_SYSTEM)/media/bootanimation.zip

# Keylayout
PRODUCT_PACKAGES += \
    atv_generic_keylayout_rpi

$(call soong_config_set_bool,rpi_keylayout,use_atv_generic_keylayout_rpi,true)

# Overlays
PRODUCT_PACKAGES += \
    AndroidTvRpiOverlay \
    BluetoothRpiOverlay \
    SettingsProviderTvRpiOverlay \
    WifiRpiOverlay

# Device identifier. This must come after all inclusions.
PRODUCT_DEVICE := rpi4
PRODUCT_NAME := aosp_rpi4_tv
PRODUCT_BRAND := Raspberry
PRODUCT_MODEL := Pi 4
PRODUCT_MANUFACTURER := Raspberry
