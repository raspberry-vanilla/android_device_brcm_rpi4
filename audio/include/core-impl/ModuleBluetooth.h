/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "core-impl/ModuleBluetoothBase.h"

namespace aidl::android::hardware::audio::core {

class ModuleBluetooth final : public ModuleBluetoothBase {
  public:
    ModuleBluetooth(std::unique_ptr<Configuration>&& config);

  protected:
    std::shared_ptr<::android::bluetooth::audio::aidl::BluetoothAudioPort> createProxyInstance(
            bool isInput) override;
};

}  // namespace aidl::android::hardware::audio::core
