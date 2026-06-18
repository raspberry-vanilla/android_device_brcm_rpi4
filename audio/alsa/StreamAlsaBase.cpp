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

#include <cmath>
#include <limits>

#define LOG_TAG "AHAL_StreamAlsaBase"
#include <Log.h>

#include <audio_utils/clock.h>
#include <error/expected_utils.h>
#include <media/AidlConversionCppNdk.h>

#include "UtilsAlsa.h"
#include "core-impl/StreamAlsaBase.h"

namespace aidl::android::hardware::audio::core {

StreamAlsaBase::StreamAlsaBase(StreamContext* context, const Metadata& metadata,
                               int readWriteRetries)
    : StreamCommonImpl(context, metadata),
      mBufferSizeFrames(getContext().getBufferSizeInFrames()),
      mFrameSizeBytes(getContext().getFrameSize()),
      mSampleRate(getContext().getSampleRate()),
      mIsInput(isInput(metadata)),
      mConfig(alsa::getPcmConfig(getContext(), mIsInput)),
      mReadWriteRetries(readWriteRetries) {}

StreamAlsaBase::~StreamAlsaBase() {
    cleanupWorker();
}

::android::status_t StreamAlsaBase::init(DriverCallbackInterface* /*callback*/) {
    return mConfig.has_value() ? ::android::OK : ::android::NO_INIT;
}

::android::status_t StreamAlsaBase::drain(StreamDescriptor::DrainMode) {
    if (!mIsInput) {
        static constexpr float kMicrosPerSecond = MICROS_PER_SECOND;
        const size_t delayUs = static_cast<size_t>(
                std::roundf(mBufferSizeFrames * kMicrosPerSecond / mSampleRate));
        usleep(delayUs);
    }
    return ::android::OK;
}

::android::status_t StreamAlsaBase::flush() {
    return ::android::OK;
}

::android::status_t StreamAlsaBase::pause() {
    return ::android::OK;
}

::android::status_t StreamAlsaBase::refinePosition(StreamDescriptor::Position* position) {
    if (mAlsaDeviceProxies.empty()) {
        LOG(WARNING) << __func__ << ": no opened devices";
        return ::android::NO_INIT;
    }
    // Since the proxy can only count transferred frames since its creation,
    // we override its counter value with ours and let it to correct for buffered frames.
    alsa::resetTransferredFrames(mAlsaDeviceProxies[0], position->frames);
    if (mIsInput) {
        if (int ret = proxy_get_capture_position(mAlsaDeviceProxies[0].get(), &position->frames,
                                                 &position->timeNs);
            ret != 0) {
            LOG(WARNING) << __func__ << ": failed to retrieve capture position: " << ret;
            return ::android::INVALID_OPERATION;
        }
    } else {
        uint64_t hwFrames;
        struct timespec timestamp;
        if (int ret = proxy_get_presentation_position(mAlsaDeviceProxies[0].get(), &hwFrames,
                                                      &timestamp);
            ret == 0) {
            if (hwFrames > std::numeric_limits<int64_t>::max()) {
                hwFrames -= std::numeric_limits<int64_t>::max();
            }
            position->frames = static_cast<int64_t>(hwFrames);
            position->timeNs = audio_utils_ns_from_timespec(&timestamp);
        } else {
            LOG(WARNING) << __func__ << ": failed to retrieve presentation position: " << ret;
            return ::android::INVALID_OPERATION;
        }
    }
    return ::android::OK;
}

ndk::ScopedAStatus StreamAlsaBase::setGain(float gain) {
    mGain = gain;
    return ndk::ScopedAStatus::ok();
}

::android::status_t StreamAlsaBase::openDeviceProxies(std::vector<alsa::DeviceProxy>* proxies) {
    for (const auto& device : getDeviceProfiles()) {
        if ((device.direction == PCM_OUT && mIsInput) ||
            (device.direction == PCM_IN && !mIsInput)) {
            continue;
        }
        alsa::DeviceProxy proxy;
        if (device.isExternal) {
            // Always ask alsa configure as required since the configuration should be supported
            // by the connected device. That is guaranteed by `setAudioPortConfig` and
            // `setAudioPatch`.
            proxy = alsa::openProxyForExternalDevice(
                    device, const_cast<struct pcm_config*>(&mConfig.value()),
                    true /*require_exact_match*/);
        } else {
            proxy = alsa::openProxyForAttachedDevice(
                    device, const_cast<struct pcm_config*>(&mConfig.value()), mBufferSizeFrames);
        }
        if (proxy.get() == nullptr) {
            return ::android::NO_INIT;
        }
        proxies->push_back(std::move(proxy));
    }
    if (proxies->empty()) {
        return ::android::NO_INIT;
    }
    return ::android::OK;
}

}  // namespace aidl::android::hardware::audio::core
