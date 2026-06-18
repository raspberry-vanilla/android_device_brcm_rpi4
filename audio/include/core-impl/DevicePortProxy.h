/*
 * Copyright 2023 The Android Open Source Project
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

#include <condition_variable>
#include <mutex>

#include <android-base/thread_annotations.h>
#include <audio_utils/resampler.h>

#include <aidl/android/hardware/bluetooth/audio/BluetoothAudioStatus.h>
#include <aidl/android/hardware/bluetooth/audio/SessionType.h>
#include <aidl/android/media/audio/common/AudioDeviceDescription.h>

#include "BluetoothAudioPort.h"

namespace android::bluetooth::audio::aidl {

class BluetoothSession;

class BluetoothAudioPortAidl : public BluetoothAudioPort {
  public:
    explicit BluetoothAudioPortAidl(std::optional<bool> supportsLowLatency);
    virtual ~BluetoothAudioPortAidl();

    bool registerPort(const ::aidl::android::media::audio::common::AudioDeviceDescription&
                              description) override EXCLUDES(mCvMutex);

    void unregisterPort() override;

    bool loadAudioConfig(
            ::aidl::android::hardware::bluetooth::audio::PcmConfiguration& audio_cfg) override;

    bool standby() override EXCLUDES(mCvMutex);
    bool start() override EXCLUDES(mCvMutex);
    bool suspend() override EXCLUDES(mCvMutex);
    void stop() override EXCLUDES(mCvMutex);

    bool getPresentationPosition(::aidl::android::hardware::bluetooth::audio::PresentationPosition&
                                         presentation_position) const override;

    bool updateSourceMetadata(const ::aidl::android::hardware::audio::common::SourceMetadata&
                                      sourceMetadata) const override;

    bool updateSinkMetadata(const ::aidl::android::hardware::audio::common::SinkMetadata&
                                    sinkMetadata) const override;

    /**
     * Return the current BluetoothStreamState
     */
    BluetoothStreamState getState() const override EXCLUDES(mCvMutex);

    bool setState(BluetoothStreamState state) override EXCLUDES(mCvMutex);

    bool isA2dp() const override;

    bool isHfp() const override;

    bool isLeAudio() const override;

    bool getPreferredDataIntervalUs(size_t& interval_us) const override;

    bool getRecommendedLatencyModes(
            std::vector<::aidl::android::hardware::bluetooth::audio::LatencyMode>* latencyModes)
            override EXCLUDES(mCvMutex);

    void setCallbacks(const std::shared_ptr<BluetoothAudioPortCallbacks>& callbacks) override
            EXCLUDES(mCvMutex);

    std::string getSessionNameForDebug() const override;

  protected:
    uint16_t mCookie;
    BluetoothStreamState mState GUARDED_BY(mCvMutex);
    // WR to support Mono: True if fetching Stereo and mixing into Mono
    bool mIsStereoToMono = false;
    uint32_t mResampleRatio = 0;
    using Resampler = std::unique_ptr<struct resampler_itfe, decltype(&release_resampler)>;
    Resampler mResampler = {nullptr, nullptr};
    std::shared_ptr<BluetoothAudioPortCallbacks> mCallbacks GUARDED_BY(mCvMutex);
    std::optional<bool> mSupportsLowLatency GUARDED_BY(mCvMutex);

    bool inUse() const;
    BluetoothSession* getSession() const EXCLUDES(mCvMutex);

    std::string debugMessage() const;

  private:
    // start()/suspend() report state change status via callback. Wait until kMaxWaitingTimeMs or a
    // state change after a call to start()/suspend() and analyse the returned status. Below mutex,
    // conditional variable serves this purpose.
    mutable std::mutex mCvMutex;
    std::condition_variable mInternalCv GUARDED_BY(mCvMutex);
    // do not call into BluetoothSession directly, use getSession() to ensure that 'mCvMutex'
    // is not taken, to avoid deadlocks with callbacks.
    std::unique_ptr<BluetoothSession> mSession;

    bool getRecommendedLatencyModes(
            std::vector<::aidl::android::hardware::bluetooth::audio::LatencyMode>* latency_modes,
            std::optional<bool>* supports_low_latency);
    // Check and initialize session type for |devices| If failed, this
    // BluetoothAudioPortAidl is not initialized and must be deleted.
    bool initSession(
            const ::aidl::android::media::audio::common::AudioDeviceDescription& description);

    bool condWaitState(std::unique_lock<std::mutex>* lock) REQUIRES(mCvMutex);

    void controlResultHandler(
            uint16_t cookie,
            const ::aidl::android::hardware::bluetooth::audio::BluetoothAudioStatus& status)
            EXCLUDES(mCvMutex);
    void lowLatencyAllowedHandler(uint16_t cookie, bool allowed) EXCLUDES(mCvMutex);
    void sessionChangedHandler(uint16_t cookie) EXCLUDES(mCvMutex);
};

class BluetoothAudioPortAidlOut : public BluetoothAudioPortAidl {
  public:
    BluetoothAudioPortAidlOut() : BluetoothAudioPortAidl(std::nullopt /*supportsLowLatency*/) {}
    bool loadAudioConfig(
            ::aidl::android::hardware::bluetooth::audio::PcmConfiguration& audio_cfg) override;

    // The audio data path to the Bluetooth stack (Software encoding)
    size_t writeData(const void* buffer, size_t bytes) const override;

    bool setLatencyMode(
            ::aidl::android::hardware::bluetooth::audio::LatencyMode latency_mode) override;
};

class BluetoothAudioPortAidlIn : public BluetoothAudioPortAidl {
  public:
    BluetoothAudioPortAidlIn() : BluetoothAudioPortAidl(false /*supportsLowLatency*/) {}
    // The audio data path from the Bluetooth stack (Software decoded)
    size_t readData(void* buffer, size_t bytes) const override;
};

}  // namespace android::bluetooth::audio::aidl
