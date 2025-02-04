/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <aidl/android/hardware/tv/hdmi/cec/BnHdmiCec.h>

namespace android {
namespace hardware {
namespace tv {
namespace hdmi {
namespace cec {
namespace implementation {

using ::aidl::android::hardware::tv::hdmi::cec::Result;

class HdmiCecPort {
  public:
    HdmiCecPort(unsigned int portId);
    ~HdmiCecPort();
    Result init(const char* path);
    void release();

    unsigned int mPortId;
    int mCecFd;
    int mExitFd;
};

}  // namespace implementation
}  // namespace cec
}  // namespace hdmi
}  // namespace tv
}  // namespace hardware
}  // namespace android
