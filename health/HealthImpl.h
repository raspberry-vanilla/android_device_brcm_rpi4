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

#pragma once

#include <health-impl/Health.h>

using ::aidl::android::hardware::health::Health;

namespace aidl::android::hardware::health {

class HealthImpl : public Health {
public:
    using Health::Health;
    virtual ~HealthImpl() {}

    ndk::ScopedAStatus getChargeCounterUah(int32_t* out) override;
    ndk::ScopedAStatus getCurrentNowMicroamps(int32_t* out) override;
    ndk::ScopedAStatus getCurrentAverageMicroamps(int32_t* out) override;
    ndk::ScopedAStatus getCapacity(int32_t* out) override;
    ndk::ScopedAStatus getChargeStatus(BatteryStatus* out) override;
    ndk::ScopedAStatus getBatteryHealthData(BatteryHealthData* out) override;

protected:
    void UpdateHealthInfo(HealthInfo* health_info) override;
};

}  // namespace aidl::android::hardware::health
