/*
 * Copyright (C) 2022 The Android Open Source Project
 * Copyright (C) 2025 KonstaKANG
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

#define LOG_TAG "android.hardware.tv.hdmi.connection-service.rpi"

#include "HdmiConnection.h"

#include <android-base/file.h>
#include <android-base/logging.h>

using android::base::ReadFileToString;
using ndk::ScopedAStatus;
using std::string;

namespace android {
namespace hardware {
namespace tv {
namespace hdmi {
namespace connection {
namespace implementation {

static const string drmCard = "card0";

HdmiConnection::HdmiConnection() {
    mCallback = nullptr;

    for (int i = 0; i < 2; i++) {
        mPortInfos.push_back(
                {.type = HdmiPortType::OUTPUT,
                 .portId = i,
                 .cecSupported = true,
                 .arcSupported = false,
                 .eArcSupported = false,
                 .physicalAddress = 0xFFFF});
        mHpdSignal.push_back(HpdSignal::HDMI_HPD_PHYSICAL);
    }
}

HdmiConnection::~HdmiConnection() {
    if (mCallback != nullptr) {
        mCallback = nullptr;
    }
}

ScopedAStatus HdmiConnection::getPortInfo(std::vector<HdmiPortInfo>* _aidl_return) {
    *_aidl_return = mPortInfos;
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiConnection::isConnected(int32_t portId, bool* _aidl_return) {
    if (portId != 0 && portId != 1) {
        *_aidl_return = false;
        return ScopedAStatus::ok();
    }

    bool connected = false;
    string hdmiStatusPath = "/sys/class/drm/" + drmCard + "-HDMI-A-" + to_string(portId + 1) + "/status";
    if (!access(hdmiStatusPath.c_str(), R_OK)) {
        string connectedValue;
        if (ReadFileToString(hdmiStatusPath, &connectedValue)) {
            connected = !connectedValue.compare("connected\n");
        }
    }
    LOG(INFO) << "portId: " << portId << ", connected: " << connected;

    *_aidl_return = connected;
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiConnection::setCallback(const std::shared_ptr<IHdmiConnectionCallback>& callback) {
    if (mCallback != nullptr) {
        mCallback = nullptr;
    }

    if (callback != nullptr) {
        mCallback = callback;
    }

    return ScopedAStatus::ok();
}

ScopedAStatus HdmiConnection::setHpdSignal(HpdSignal signal, int32_t portId) {
    if (portId != 0 && portId != 1) {
        return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    mHpdSignal.at(portId) = signal;
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiConnection::getHpdSignal(int32_t portId, HpdSignal* _aidl_return) {
    if (portId != 0 && portId != 1) {
        return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    *_aidl_return = mHpdSignal.at(portId);
    return ScopedAStatus::ok();
}

}  // namespace implementation
}  // namespace connection
}  // namespace hdmi
}  // namespace tv
}  // namespace hardware
}  // namespace android
