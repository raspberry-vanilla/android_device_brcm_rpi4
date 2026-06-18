/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <atomic>
#include <optional>
#include <vector>

#include "Stream.h"
#include "alsa/UtilsAlsa.h"

namespace aidl::android::hardware::audio::core {

// Base class for implementations that use TinyAlsa.
// Contains code common to both the direct ALSA path (StreamAlsa) and the
// MonoPipe + I/O thread path (StreamAlsaMonoPipe).
// This class does not define a complete stream implementation and should never
// be used on its own. Derived classes must provide implementations for
// standby(), start(), transfer(), and shutdown().
class StreamAlsaBase : public StreamCommonImpl {
  public:
    StreamAlsaBase(StreamContext* context, const Metadata& metadata, int readWriteRetries);
    ~StreamAlsaBase();

    // Methods of 'DriverInterface'.
    ::android::status_t init(DriverCallbackInterface* callback) override;
    ::android::status_t drain(StreamDescriptor::DrainMode) override;
    ::android::status_t flush() override;
    ::android::status_t pause() override;
    ::android::status_t standby() override = 0;
    ::android::status_t start() override = 0;
    ::android::status_t transfer(void* buffer, size_t frameCount, size_t* actualFrameCount,
                                 int32_t* latencyMs) override = 0;
    ::android::status_t refinePosition(StreamDescriptor::Position* position) override;
    void shutdown() override = 0;
    ndk::ScopedAStatus setGain(float gain) override;

  protected:
    // Called from 'start' to initialize 'mAlsaDeviceProxies', the vector must be non-empty.
    virtual std::vector<alsa::DeviceProfile> getDeviceProfiles() = 0;

    // Helper used by subclass start() implementations to open ALSA device proxies.
    // Returns ::android::NO_INIT if no proxies could be opened, ::android::OK otherwise.
    ::android::status_t openDeviceProxies(std::vector<alsa::DeviceProxy>* proxies);

    const size_t mBufferSizeFrames;
    const size_t mFrameSizeBytes;
    const int mSampleRate;
    const bool mIsInput;
    const std::optional<struct pcm_config> mConfig;
    const int mReadWriteRetries;

    std::atomic<float> mGain = 1.0;

    // Opened ALSA device proxies. Populated by start(), cleared by standby()/shutdown().
    std::vector<alsa::DeviceProxy> mAlsaDeviceProxies;
};

}  // namespace aidl::android::hardware::audio::core
