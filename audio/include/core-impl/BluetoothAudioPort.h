/*
 * Copyright 2025 The Android Open Source Project
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

#include <memory>
#include <vector>

#include <android-base/stringprintf.h>

#include <aidl/android/hardware/audio/common/SinkMetadata.h>
#include <aidl/android/hardware/audio/common/SourceMetadata.h>
#include <aidl/android/hardware/bluetooth/audio/LatencyMode.h>
#include <aidl/android/hardware/bluetooth/audio/PcmConfiguration.h>
#include <aidl/android/hardware/bluetooth/audio/PresentationPosition.h>
#include <aidl/android/media/audio/common/AudioDeviceDescription.h>

namespace android::bluetooth::audio::aidl {

enum class BluetoothStreamState : uint8_t {
    DISABLED = 0,  // This stream is closing or Bluetooth profiles (A2DP/LE) is disabled
    STANDBY,
    STARTING,
    STARTED,
    SUSPENDING,
    UNKNOWN,
};

inline std::ostream& operator<<(std::ostream& os, const BluetoothStreamState& state) {
    switch (state) {
        case BluetoothStreamState::DISABLED:
            return os << "DISABLED";
        case BluetoothStreamState::STANDBY:
            return os << "STANDBY";
        case BluetoothStreamState::STARTING:
            return os << "STARTING";
        case BluetoothStreamState::STARTED:
            return os << "STARTED";
        case BluetoothStreamState::SUSPENDING:
            return os << "SUSPENDING";
        case BluetoothStreamState::UNKNOWN:
            return os << "UNKNOWN";
        default:
            return os << android::base::StringPrintf("%#hhx", state);
    }
}

class BluetoothAudioPortCallbacks {
  public:
    virtual ~BluetoothAudioPortCallbacks() = default;
    virtual void onRecommendedLatencyModeChanged(
            const std::vector<::aidl::android::hardware::bluetooth::audio::LatencyMode>&) = 0;
};

/**
 * Proxy for Bluetooth Audio HW Module to communicate with Bluetooth Audio
 * Session Control. All methods are not thread safe, so users must acquire a
 * lock. Note: currently, getState() of DevicePortProxy is only used for
 * verbose logging, it is not locked, so the state may not be synchronized.
 */
class BluetoothAudioPort {
  public:
    virtual ~BluetoothAudioPort() = default;

    /**
     * Fetch output control / data path of BluetoothAudioPort and setup
     * callbacks into BluetoothAudioProvider. If registerPort() returns false, the audio
     * HAL must delete this BluetoothAudioPort and return EINVAL to caller
     */
    virtual bool registerPort(
            const ::aidl::android::media::audio::common::AudioDeviceDescription&) = 0;

    /**
     * Unregister this BluetoothAudioPort from BluetoothAudioSessionControl.
     * Audio HAL must delete this BluetoothAudioPort after calling this.
     */
    virtual void unregisterPort() = 0;

    /**
     * When the Audio framework / HAL tries to query audio config about format,
     * channel mask and sample rate, it uses this function to fetch from the
     * Bluetooth stack
     */
    virtual bool loadAudioConfig(
            ::aidl::android::hardware::bluetooth::audio::PcmConfiguration&) = 0;

    /**
     * When the Audio framework / HAL wants to change the stream state, it invokes
     * these 4 functions to control the Bluetooth stack (Audio Control Path).
     * Note: standby(), start() and suspend() will return true when there are no errors.

     * Called by Audio framework / HAL to change the state to stand by. When A2DP/LE profile is
     * disabled, the port is first set to STANDBY by calling suspend and then mState is set to
     * DISABLED. To reset the state back to STANDBY this method is called.
     */
    virtual bool standby() = 0;

    /**
     * Called by Audio framework / HAL to start the stream. Starts the BT session stream with
     * low latency when it is supported.
     */
    virtual bool start() = 0;

    /**
     * Called by Audio framework / HAL to suspend the stream
     */
    virtual bool suspend() = 0;

    /**
     * Called by Audio framework / HAL to stop the stream
     */
    virtual void stop() = 0;

    /**
     * Called by the Audio framework / HAL to fetch information about audio frames
     * presented to an external sink, or frames presented fror an internal sink
     */
    virtual bool getPresentationPosition(
            ::aidl::android::hardware::bluetooth::audio::PresentationPosition&) const = 0;

    /**
     * Called by the Audio framework / HAL when the metadata of the stream's
     * source has been changed.
     */
    virtual bool updateSourceMetadata(
            const ::aidl::android::hardware::audio::common::SourceMetadata&) const = 0;

    /**
     * Called by the Audio framework / HAL when the metadata of the stream's
     * sink has been changed.
     */
    virtual bool updateSinkMetadata(
            const ::aidl::android::hardware::audio::common::SinkMetadata&) const = 0;

    /**
     * Return the current BluetoothStreamState
     */
    virtual BluetoothStreamState getState() const = 0;

    /**
     * Set the current BluetoothStreamState
     */
    virtual bool setState(BluetoothStreamState) = 0;

    virtual bool isA2dp() const = 0;

    virtual bool isLeAudio() const = 0;

    virtual bool isHfp() const = 0;

    virtual bool getPreferredDataIntervalUs(size_t&) const = 0;

    virtual size_t writeData(const void*, size_t) const { return 0; }

    virtual size_t readData(void*, size_t) const { return 0; }

    virtual bool setLatencyMode(::aidl::android::hardware::bluetooth::audio::LatencyMode) {
        return false;
    }

    virtual bool getRecommendedLatencyModes(
            std::vector<::aidl::android::hardware::bluetooth::audio::LatencyMode>*) {
        return false;
    }

    virtual void setCallbacks(const std::shared_ptr<BluetoothAudioPortCallbacks>&) = 0;

    virtual std::string getSessionNameForDebug() const = 0;
};

}  // namespace android::bluetooth::audio::aidl
