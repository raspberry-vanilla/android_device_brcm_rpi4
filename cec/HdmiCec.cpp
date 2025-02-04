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

#define LOG_TAG "android.hardware.tv.hdmi.cec-service.rpi"

#include <android-base/logging.h>
#include <android-base/properties.h>

#include <cutils/properties.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <poll.h>

#include "HdmiCec.h"

#define PROPERTY_CEC_DEVICE "persist.hdmi.cec_device"
#define PROPERTY_CEC_VERSION "ro.hdmi.cec_version"
#define PROPERTY_VENDOR_ID "ro.hdmi.vendor_id"

using android::base::GetProperty;
using ndk::ScopedAStatus;
using std::string;

namespace android {
namespace hardware {
namespace tv {
namespace hdmi {
namespace cec {
namespace implementation {

HdmiCec::HdmiCec() {
    mCecEnabled = false;
    mWakeupEnabled = false;
    mCecControlEnabled = false;
    mCallback = nullptr;

    Result result = init();
    if (result != Result::SUCCESS) {
        LOG(ERROR) << "Failed to init HDMI-CEC HAL";
    }
}

HdmiCec::~HdmiCec() {
    release();
}

ScopedAStatus HdmiCec::addLogicalAddress(CecLogicalAddress addr, Result* _aidl_return) {
    if (addr < CecLogicalAddress::TV || addr >= CecLogicalAddress::BROADCAST) {
        LOG(ERROR) << "Add logical address failed, Invalid address";
        *_aidl_return = Result::FAILURE_INVALID_ARGS;
        return ScopedAStatus::ok();
    }

    cec_log_addrs cecLogAddrs;
    int ret = ioctl(mHdmiCecPorts[0]->mCecFd, CEC_ADAP_G_LOG_ADDRS, &cecLogAddrs);
    if (ret) {
        LOG(ERROR) << "Add logical address failed, Error = " << strerror(errno);
        *_aidl_return = Result::FAILURE_BUSY;
        return ScopedAStatus::ok();
    }

    cecLogAddrs.cec_version = getCecVersion();
    cecLogAddrs.vendor_id = getVendorId();

    unsigned int logAddrType = CEC_LOG_ADDR_TYPE_UNREGISTERED;
    unsigned int allDevTypes = 0;
    unsigned int primDevType = 0xff;
    switch (addr) {
        case CecLogicalAddress::TV:
            primDevType = CEC_OP_PRIM_DEVTYPE_TV;
            logAddrType = CEC_LOG_ADDR_TYPE_TV;
            allDevTypes = CEC_OP_ALL_DEVTYPE_TV;
            break;
        case CecLogicalAddress::RECORDER_1:
        case CecLogicalAddress::RECORDER_2:
        case CecLogicalAddress::RECORDER_3:
            primDevType = CEC_OP_PRIM_DEVTYPE_RECORD;
            logAddrType = CEC_LOG_ADDR_TYPE_RECORD;
            allDevTypes = CEC_OP_ALL_DEVTYPE_RECORD;
            break;
        case CecLogicalAddress::TUNER_1:
        case CecLogicalAddress::TUNER_2:
        case CecLogicalAddress::TUNER_3:
        case CecLogicalAddress::TUNER_4:
            primDevType = CEC_OP_PRIM_DEVTYPE_TUNER;
            logAddrType = CEC_LOG_ADDR_TYPE_TUNER;
            allDevTypes = CEC_OP_ALL_DEVTYPE_TUNER;
            break;
        case CecLogicalAddress::PLAYBACK_1:
        case CecLogicalAddress::PLAYBACK_2:
        case CecLogicalAddress::PLAYBACK_3:
            primDevType = CEC_OP_PRIM_DEVTYPE_PLAYBACK;
            logAddrType = CEC_LOG_ADDR_TYPE_PLAYBACK;
            allDevTypes = CEC_OP_ALL_DEVTYPE_PLAYBACK;
            cecLogAddrs.flags |= CEC_LOG_ADDRS_FL_ALLOW_RC_PASSTHRU;
            break;
        case CecLogicalAddress::AUDIO_SYSTEM:
            primDevType = CEC_OP_PRIM_DEVTYPE_AUDIOSYSTEM;
            logAddrType = CEC_LOG_ADDR_TYPE_AUDIOSYSTEM;
            allDevTypes = CEC_OP_ALL_DEVTYPE_AUDIOSYSTEM;
            break;
        case CecLogicalAddress::FREE_USE:
            primDevType = CEC_OP_PRIM_DEVTYPE_PROCESSOR;
            logAddrType = CEC_LOG_ADDR_TYPE_SPECIFIC;
            allDevTypes = CEC_OP_ALL_DEVTYPE_SWITCH;
            break;
        case CecLogicalAddress::UNREGISTERED:
            cecLogAddrs.flags |= CEC_LOG_ADDRS_FL_ALLOW_UNREG_FALLBACK;
            break;
        case CecLogicalAddress::BACKUP_1:
        case CecLogicalAddress::BACKUP_2:
            break;
    }

    int logAddrIndex = cecLogAddrs.num_log_addrs;
    cecLogAddrs.num_log_addrs += 1;
    cecLogAddrs.log_addr[logAddrIndex] = static_cast<cec_logical_address_t>(addr);
    cecLogAddrs.log_addr_type[logAddrIndex] = logAddrType;
    cecLogAddrs.primary_device_type[logAddrIndex] = primDevType;
    cecLogAddrs.all_device_types[logAddrIndex] = allDevTypes;
    cecLogAddrs.features[logAddrIndex][0] = 0;
    cecLogAddrs.features[logAddrIndex][1] = 0;

    ret = ioctl(mHdmiCecPorts[0]->mCecFd, CEC_ADAP_S_LOG_ADDRS, &cecLogAddrs);
    if (ret) {
        LOG(ERROR) << "Add logical address failed for port " << mHdmiCecPorts[0]->mPortId
                   << ", Error = " << strerror(errno);
        *_aidl_return = Result::FAILURE_BUSY;
        return ScopedAStatus::ok();
    }

    *_aidl_return = Result::SUCCESS;
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCec::clearLogicalAddress() {
    cec_log_addrs cecLogAddrs;
    memset(&cecLogAddrs, 0, sizeof(cecLogAddrs));

    int ret = ioctl(mHdmiCecPorts[0]->mCecFd, CEC_ADAP_S_LOG_ADDRS, &cecLogAddrs);
    if (ret) {
        LOG(ERROR) << "Clear logical Address failed for port " << mHdmiCecPorts[0]->mPortId
                   << ", Error = " << strerror(errno);
    }

    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCec::enableAudioReturnChannel(int32_t portId __unused, bool enable __unused) {
    return ScopedAStatus::ok();
}

int32_t HdmiCec::getCecVersion() {
    return property_get_int32(PROPERTY_CEC_VERSION, CEC_OP_CEC_VERSION_1_4);
}

ScopedAStatus HdmiCec::getCecVersion(int32_t* _aidl_return) {
    *_aidl_return = getCecVersion();
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCec::getPhysicalAddress(int32_t* _aidl_return) {
    uint16_t addr;

    int ret = ioctl(mHdmiCecPorts[0]->mCecFd, CEC_ADAP_G_PHYS_ADDR, &addr);
    if (ret) {
        LOG(ERROR) << "Get physical address failed, Error = " << strerror(errno);
        return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::FAILURE_INVALID_STATE));
    }

    *_aidl_return = addr;
    return ScopedAStatus::ok();
}

uint32_t HdmiCec::getVendorId() {
    return property_get_int32(PROPERTY_VENDOR_ID, 0x000c03 /* HDMI LLC vendor ID */);
}

ScopedAStatus HdmiCec::getVendorId(int32_t* _aidl_return) {
    *_aidl_return = getVendorId();
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCec::sendMessage(const CecMessage& message, SendMessageResult* _aidl_return) {
    if (!mCecEnabled) {
        *_aidl_return = SendMessageResult::FAIL;
        return ScopedAStatus::ok();
    }

    cec_msg cecMsg;
    memset(&cecMsg, 0, sizeof(cec_msg));

    int initiator = static_cast<cec_logical_address_t>(message.initiator);
    int destination = static_cast<cec_logical_address_t>(message.destination);

    cecMsg.msg[0] = (initiator << 4) | destination;
    for (size_t i = 0; i < message.body.size(); ++i) {
        cecMsg.msg[i + 1] = message.body[i];
    }
    cecMsg.len = message.body.size() + 1;

    int ret = ioctl(mHdmiCecPorts[0]->mCecFd, CEC_TRANSMIT, &cecMsg);
    if (ret) {
        LOG(ERROR) << "Send message failed, Error = " << strerror(errno);
        *_aidl_return = SendMessageResult::FAIL;
        return ScopedAStatus::ok();
    }

    if (cecMsg.tx_status != CEC_TX_STATUS_OK) {
        LOG(ERROR) << "Send message tx_status = " << cecMsg.tx_status;
    }

    *_aidl_return = getSendMessageResult(cecMsg.tx_status);
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCec::setCallback(const std::shared_ptr<IHdmiCecCallback>& callback) {
    if (mCallback != nullptr) {
        mCallback = nullptr;
    }

    if (callback != nullptr) {
        mCallback = callback;
    }

    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCec::setLanguage(const std::string& language __unused) {
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCec::enableWakeupByOtp(bool value) {
    mWakeupEnabled = value;
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCec::enableCec(bool value) {
    mCecEnabled = value;
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCec::enableSystemCecControl(bool value) {
    mCecControlEnabled = value;
    return ScopedAStatus::ok();
}

// Initialise the cec file descriptor
Result HdmiCec::init() {
    string cecDevice = GetProperty(PROPERTY_CEC_DEVICE, "cec0");
    if (cecDevice != "cec0" && cecDevice != "cec1") {
        LOG(ERROR) << "Invalid CEC device " << cecDevice;
        return Result::FAILURE_NOT_SUPPORTED;
    }

    string devicePath = "/dev/" + cecDevice;
    int portId = stoi(cecDevice.substr(3));

    shared_ptr<HdmiCecPort> hdmiCecPort(new HdmiCecPort(portId));
    Result result = hdmiCecPort->init(devicePath.c_str());
    if (result != Result::SUCCESS) {
        return Result::FAILURE_NOT_SUPPORTED;
    }

    thread eventThread(&HdmiCec::event_thread, this, hdmiCecPort.get());
    mEventThreads.push_back(std::move(eventThread));
    mHdmiCecPorts.push_back(std::move(hdmiCecPort));
    LOG(INFO) << "Using CEC device " << devicePath;

    mCecEnabled = true;
    mWakeupEnabled = true;
    mCecControlEnabled = true;

    return Result::SUCCESS;
}

void HdmiCec::release() {
    mCecEnabled = false;
    mWakeupEnabled = false;
    mCecControlEnabled = false;

    for (thread& eventThread : mEventThreads) {
        if (eventThread.joinable()) {
            eventThread.join();
        }
    }
    setCallback(nullptr);
    mHdmiCecPorts.clear();
    mEventThreads.clear();
}

void HdmiCec::event_thread(HdmiCecPort* hdmiCecPort) {
    struct pollfd ufds[3] = {
            {hdmiCecPort->mCecFd, POLLIN, 0},
            {hdmiCecPort->mCecFd, POLLERR, 0},
            {hdmiCecPort->mExitFd, POLLIN, 0},
    };

    while (1) {
        ufds[0].revents = 0;
        ufds[1].revents = 0;
        ufds[2].revents = 0;

        int ret = poll(ufds, /* size(ufds) = */ 3, /* timeout = */ -1);

        if (ret <= 0) {
            continue;
        }

        if (ufds[2].revents == POLLIN) { /* Exit */
            break;
        }

        if (ufds[1].revents == POLLERR) { /* CEC Event */
            cec_event ev;
            ret = ioctl(hdmiCecPort->mCecFd, CEC_DQEVENT, &ev);

            if (ret) {
                LOG(ERROR) << "CEC_DQEVENT failed, Error = " << strerror(errno);
                continue;
            }

            if (!mCecEnabled) {
                continue;
            }

/*
            if (ev.event == CEC_EVENT_STATE_CHANGE) {
                if (mCallback != nullptr) {
                    HotplugEvent hotplugEvent{
                            .connected = (ev.state_change.phys_addr != CEC_PHYS_ADDR_INVALID),
                            .portId = hdmiCecPort->mPortId};
                    mCallback->onHotplugEvent(hotplugEvent);
                } else {
                    LOG(ERROR) << "No event callback for hotplug";
                }
            }
*/
        }

        if (ufds[0].revents == POLLIN) { /* CEC Driver */
            cec_msg msg = {};
            ret = ioctl(hdmiCecPort->mCecFd, CEC_RECEIVE, &msg);

            if (ret) {
                LOG(ERROR) << "CEC_RECEIVE failed, Error = " << strerror(errno);
                continue;
            }

            if (msg.rx_status != CEC_RX_STATUS_OK) {
                LOG(ERROR) << "msg rx_status = " << msg.rx_status;
                continue;
            }

            if (!mCecEnabled) {
                continue;
            }

            if (!mWakeupEnabled && isWakeupMessage(msg)) {
                LOG(DEBUG) << "Filter wakeup message";
                continue;
            }

            if (!mCecControlEnabled && !isTransferableInSleep(msg)) {
                LOG(DEBUG) << "Filter message in standby mode";
                continue;
            }

            if (mCallback != nullptr) {
                size_t length = std::min(msg.len - 1, (uint32_t)(CEC_MESSAGE_BODY_MAX_LENGTH - 1));
                CecMessage cecMessage{
                        .initiator = static_cast<CecLogicalAddress>(msg.msg[0] >> 4),
                        .destination = static_cast<CecLogicalAddress>(msg.msg[0] & 0xf),
                };
                cecMessage.body.resize(length);
                for (size_t i = 0; i < length; ++i) {
                    cecMessage.body[i] = static_cast<uint8_t>(msg.msg[i + 1]);
                }
                mCallback->onCecMessage(cecMessage);
            } else {
                LOG(ERROR) << "no event callback for message";
            }
        }
    }
}

int HdmiCec::getOpcode(cec_msg message) {
    return static_cast<uint8_t>(message.msg[1]);
}

bool HdmiCec::isWakeupMessage(cec_msg message) {
    int opcode = getOpcode(message);
    switch (opcode) {
        case CEC_MESSAGE_TEXT_VIEW_ON:
        case CEC_MESSAGE_IMAGE_VIEW_ON:
            return true;
        default:
            return false;
    }
}

bool HdmiCec::isTransferableInSleep(cec_msg message) {
    int opcode = getOpcode(message);
    switch (opcode) {
        case CEC_MESSAGE_ABORT:
        case CEC_MESSAGE_DEVICE_VENDOR_ID:
        case CEC_MESSAGE_GET_CEC_VERSION:
        case CEC_MESSAGE_GET_MENU_LANGUAGE:
        case CEC_MESSAGE_GIVE_DEVICE_POWER_STATUS:
        case CEC_MESSAGE_GIVE_DEVICE_VENDOR_ID:
        case CEC_MESSAGE_GIVE_OSD_NAME:
        case CEC_MESSAGE_GIVE_PHYSICAL_ADDRESS:
        case CEC_MESSAGE_REPORT_PHYSICAL_ADDRESS:
        case CEC_MESSAGE_REPORT_POWER_STATUS:
        case CEC_MESSAGE_SET_OSD_NAME:
        case CEC_MESSAGE_DECK_CONTROL:
        case CEC_MESSAGE_PLAY:
        case CEC_MESSAGE_IMAGE_VIEW_ON:
        case CEC_MESSAGE_TEXT_VIEW_ON:
        case CEC_MESSAGE_SYSTEM_AUDIO_MODE_REQUEST:
            return true;
        case CEC_MESSAGE_USER_CONTROL_PRESSED:
            return isPowerUICommand(message);
        default:
            return false;
    }
}

int HdmiCec::getFirstParam(cec_msg message) {
    return static_cast<uint8_t>(message.msg[2]);
}

bool HdmiCec::isPowerUICommand(cec_msg message) {
    int uiCommand = getFirstParam(message);
    switch (uiCommand) {
        case CEC_OP_UI_CMD_POWER:
        case CEC_OP_UI_CMD_DEVICE_ROOT_MENU:
        case CEC_OP_UI_CMD_POWER_ON_FUNCTION:
            return true;
        default:
            return false;
    }
}

SendMessageResult HdmiCec::getSendMessageResult(int tx_status) {
    switch (tx_status) {
        case CEC_TX_STATUS_OK:
            return SendMessageResult::SUCCESS;
        case CEC_TX_STATUS_ARB_LOST:
            return SendMessageResult::BUSY;
        case CEC_TX_STATUS_NACK:
            return SendMessageResult::NACK;
        default:
            return SendMessageResult::FAIL;
    }
}

}  // namespace implementation
}  // namespace cec
}  // namespace hdmi
}  // namespace tv
}  // namespace hardware
}  // namespace android
