#
# Copyright (C) 2021-2023 KonstaKANG
#
# SPDX-License-Identifier: Apache-2.0
#

PRODUCT_MAKEFILES := \
    $(LOCAL_DIR)/aosp_rpi4.mk \
    $(LOCAL_DIR)/aosp_rpi4_car.mk \
    $(LOCAL_DIR)/aosp_rpi4_tv.mk

COMMON_LUNCH_CHOICES := \
    aosp_rpi4-userdebug \
    aosp_rpi4_car-userdebug \
    aosp_rpi4_tv-userdebug
