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

#include <aidl/android/hardware/tv/hdmi/connection/BnHdmiConnection.h>
#include <aidl/android/hardware/tv/hdmi/connection/Result.h>
#include <algorithm>
#include <vector>

using namespace std;

namespace android {
namespace hardware {
namespace tv {
namespace hdmi {
namespace connection {
namespace implementation {

using ::aidl::android::hardware::tv::hdmi::connection::BnHdmiConnection;
using ::aidl::android::hardware::tv::hdmi::connection::HdmiPortInfo;
using ::aidl::android::hardware::tv::hdmi::connection::HdmiPortType;
using ::aidl::android::hardware::tv::hdmi::connection::HpdSignal;
using ::aidl::android::hardware::tv::hdmi::connection::IHdmiConnection;
using ::aidl::android::hardware::tv::hdmi::connection::IHdmiConnectionCallback;
using ::aidl::android::hardware::tv::hdmi::connection::Result;

struct HdmiConnection : public BnHdmiConnection {
    HdmiConnection();
    ~HdmiConnection();
    ::ndk::ScopedAStatus getPortInfo(std::vector<HdmiPortInfo>* _aidl_return) override;
    ::ndk::ScopedAStatus isConnected(int32_t portId, bool* _aidl_return) override;
    ::ndk::ScopedAStatus setCallback(
            const std::shared_ptr<IHdmiConnectionCallback>& callback) override;
    ::ndk::ScopedAStatus setHpdSignal(HpdSignal signal, int32_t portId) override;
    ::ndk::ScopedAStatus getHpdSignal(int32_t portId, HpdSignal* _aidl_return) override;

  private:
    std::vector<HdmiPortInfo> mPortInfos;
    std::vector<HpdSignal> mHpdSignal;

    std::shared_ptr<IHdmiConnectionCallback> mCallback;
};

}  // namespace implementation
}  // namespace connection
}  // namespace hdmi
}  // Namespace tv
}  // namespace hardware
}  // namespace android
