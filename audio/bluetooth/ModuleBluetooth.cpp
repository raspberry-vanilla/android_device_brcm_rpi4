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

#define LOG_TAG "AHAL_ModuleBluetooth"

#include <Log.h>

#include "BluetoothAudioSession.h"
#include "core-impl/DevicePortProxy.h"
#include "core-impl/ModuleBluetooth.h"

using android::bluetooth::audio::aidl::BluetoothAudioPort;
using android::bluetooth::audio::aidl::BluetoothAudioPortAidlIn;
using android::bluetooth::audio::aidl::BluetoothAudioPortAidlOut;

// TODO(b/312265159) bluetooth audio should be in its own process
// Remove this and the shared_libs when that happens
extern "C" binder_status_t createIBluetoothAudioProviderFactory();

namespace aidl::android::hardware::audio::core {

ModuleBluetooth::ModuleBluetooth(std::unique_ptr<Module::Configuration>&& config)
    : ModuleBluetoothBase(std::move(config)) {
    // TODO(b/312265159) bluetooth audio should be in its own process
    // Remove this and the shared_libs when that happens
    binder_status_t status = createIBluetoothAudioProviderFactory();
    if (status != STATUS_OK) {
        LOG(ERROR) << __func__ << ": Failed to create bluetooth audio provider factory. Status: "
                   << ::android::statusToString(status);
    }
    if (!::aidl::android::hardware::bluetooth::audio::BluetoothAudioSession::IsAidlAvailable()) {
        LOG(ERROR) << __func__ << ": IBluetoothAudioProviderFactory AIDL service not available";
    }
}

std::shared_ptr<BluetoothAudioPort> ModuleBluetooth::createProxyInstance(bool isInput) {
    return isInput ? std::shared_ptr<BluetoothAudioPort>(
                             std::make_shared<BluetoothAudioPortAidlIn>())
                   : std::shared_ptr<BluetoothAudioPort>(
                             std::make_shared<BluetoothAudioPortAidlOut>());
}

}  // namespace aidl::android::hardware::audio::core
