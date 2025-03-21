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
#include <hardware/hdmi_cec.h>
#include <linux/cec.h>
#include <thread>
#include <vector>
#include "HdmiCecPort.h"

namespace android {
namespace hardware {
namespace tv {
namespace hdmi {
namespace cec {
namespace implementation {

using std::shared_ptr;
using std::thread;
using std::vector;

using ::aidl::android::hardware::tv::hdmi::cec::BnHdmiCec;
using ::aidl::android::hardware::tv::hdmi::cec::CecLogicalAddress;
using ::aidl::android::hardware::tv::hdmi::cec::CecMessage;
using ::aidl::android::hardware::tv::hdmi::cec::IHdmiCec;
using ::aidl::android::hardware::tv::hdmi::cec::IHdmiCecCallback;
using ::aidl::android::hardware::tv::hdmi::cec::Result;
using ::aidl::android::hardware::tv::hdmi::cec::SendMessageResult;

struct HdmiCec : public BnHdmiCec {
  public:
    HdmiCec();
    ~HdmiCec();
    ::ndk::ScopedAStatus addLogicalAddress(CecLogicalAddress addr, Result* _aidl_return) override;
    ::ndk::ScopedAStatus clearLogicalAddress() override;
    ::ndk::ScopedAStatus enableAudioReturnChannel(int32_t portId, bool enable) override;
    ::ndk::ScopedAStatus getCecVersion(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getPhysicalAddress(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getVendorId(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus sendMessage(const CecMessage& message,
                                     SendMessageResult* _aidl_return) override;
    ::ndk::ScopedAStatus setCallback(const std::shared_ptr<IHdmiCecCallback>& callback) override;
    ::ndk::ScopedAStatus setLanguage(const std::string& language) override;
    ::ndk::ScopedAStatus enableWakeupByOtp(bool value) override;
    ::ndk::ScopedAStatus enableCec(bool value) override;
    ::ndk::ScopedAStatus enableSystemCecControl(bool value) override;

    Result init();
    void release();

  private:
    void event_thread(HdmiCecPort* hdmiCecPort);
    static int getOpcode(cec_msg message);
    static int getFirstParam(cec_msg message);
    static bool isWakeupMessage(cec_msg message);
    static bool isTransferableInSleep(cec_msg message);
    static bool isPowerUICommand(cec_msg message);
    static SendMessageResult getSendMessageResult(int tx_status);

    vector<thread> mEventThreads;
    vector<shared_ptr<HdmiCecPort>> mHdmiCecPorts;

    // When set to false, all the CEC commands are discarded. True by default after initialization.
    bool mCecEnabled;
    /*
     * When set to false, HAL does not wake up the system upon receiving <Image View On> or
     * <Text View On>. True by default after initialization.
     */
    bool mWakeupEnabled;
    /*
     * Updated when system goes into or comes out of standby mode.
     * When set to true, Android system is handling CEC commands.
     * When set to false, microprocessor is handling CEC commands.
     * True by default after initialization.
     */
    bool mCecControlEnabled;

    std::shared_ptr<IHdmiCecCallback> mCallback;
};

}  // namespace implementation
}  // namespace cec
}  // namespace hdmi
}  // namespace tv
}  // namespace hardware
}  // namespace android
