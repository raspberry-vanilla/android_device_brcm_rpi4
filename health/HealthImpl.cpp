/*
 * Copyright (C) 2021 The Android Open Source Project
 * Copyright (C) 2023 KonstaKANG
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "HealthImpl.h"

using ::aidl::android::hardware::health::BatteryHealth;
using ::aidl::android::hardware::health::BatteryStatus;
using ::aidl::android::hardware::health::HealthInfo;

namespace aidl::android::hardware::health {

void HealthImpl::UpdateHealthInfo(HealthInfo* health_info) {
    health_info->chargerAcOnline = true;
    health_info->chargerUsbOnline = true;
    health_info->chargerWirelessOnline = false;
    health_info->chargerDockOnline = false;
    health_info->maxChargingCurrentMicroamps = 500000;
    health_info->maxChargingVoltageMicrovolts = 5000000;
    health_info->batteryStatus = BatteryStatus::FULL;
    health_info->batteryHealth = BatteryHealth::GOOD;
    health_info->batteryPresent = true;
    health_info->batteryLevel = 100;
    health_info->batteryVoltageMillivolts = 5000;
    health_info->batteryTemperatureTenthsCelsius = 250;
    health_info->batteryCurrentMicroamps = 500000;
    health_info->batteryCycleCount = 25;
    health_info->batteryFullChargeUah = 5000000;
    health_info->batteryChargeCounterUah = 5000000;
    health_info->batteryTechnology = "Li-ion";
    health_info->batteryCapacityLevel = BatteryCapacityLevel::FULL;
    health_info->batteryFullChargeDesignCapacityUah = 5000000;
}

ndk::ScopedAStatus HealthImpl::getChargeCounterUah(int32_t* out) {
    *out = 5000000;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus HealthImpl::getCurrentNowMicroamps(int32_t* out) {
    *out = 500000;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus HealthImpl::getCurrentAverageMicroamps(int32_t*) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus HealthImpl::getCapacity(int32_t* out) {
    *out = 100;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus HealthImpl::getChargeStatus(BatteryStatus* out) {
    *out = BatteryStatus::FULL;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus HealthImpl::getBatteryHealthData(BatteryHealthData* out) {
    out->batteryManufacturingDateSeconds = 1231006505;
    out->batteryFirstUsageSeconds = 1231469665;
    out->batteryStateOfHealth = 99;
    out->batterySerialNumber =
            "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";
    out->batteryPartStatus = BatteryPartStatus::ORIGINAL;
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::health
