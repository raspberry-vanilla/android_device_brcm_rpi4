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
    health_info->batteryLevel = 100;
    health_info->batteryStatus = BatteryStatus::CHARGING;
    health_info->batteryHealth = BatteryHealth::GOOD;
}

ndk::ScopedAStatus HealthImpl::getChargeStatus(BatteryStatus* out) {
    *out = BatteryStatus::CHARGING;
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::health
