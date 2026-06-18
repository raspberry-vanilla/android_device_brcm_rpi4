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

#include <limits>

#define LOG_TAG "AHAL_StreamAlsa"
#include <Log.h>

#include "UtilsAlsa.h"
#include "core-impl/StreamAlsa.h"

namespace aidl::android::hardware::audio::core {

StreamAlsa::StreamAlsa(StreamContext* context, const Metadata& metadata, int readWriteRetries)
    : StreamAlsaBase(context, metadata, readWriteRetries) {}

StreamAlsa::~StreamAlsa() {
    cleanupWorker();
}

::android::status_t StreamAlsa::standby() {
    mAlsaDeviceProxies.clear();
    return ::android::OK;
}

::android::status_t StreamAlsa::start() {
    if (!mAlsaDeviceProxies.empty()) {
        // This is a resume after a pause.
        return ::android::OK;
    }
    return openDeviceProxies(&mAlsaDeviceProxies);
}

::android::status_t StreamAlsa::transfer(void* buffer, size_t frameCount, size_t* actualFrameCount,
                                         int32_t* latencyMs) {
    if (mAlsaDeviceProxies.empty()) {
        LOG(FATAL) << __func__ << ": no opened devices";
        return ::android::NO_INIT;
    }
    const size_t bytesToTransfer = frameCount * mFrameSizeBytes;
    unsigned maxLatency = 0;
    if (mIsInput) {
        if (int ret = proxy_read_with_retries(mAlsaDeviceProxies[0].get(), buffer, bytesToTransfer,
                                              mReadWriteRetries);
            ret != 0) {
            LOG(WARNING) << __func__ << ": read failed: " << ret;
            return ::android::INVALID_OPERATION;
        }
        maxLatency = proxy_get_latency(mAlsaDeviceProxies[0].get());
    } else {
        alsa::applyGain(buffer, mGain, bytesToTransfer, mConfig.value().format, mConfig->channels);
        for (size_t i = 0; i < mAlsaDeviceProxies.size(); ++i) {
            if (int ret = proxy_write_with_retries(mAlsaDeviceProxies[i].get(), buffer,
                                                   bytesToTransfer, mReadWriteRetries);
                ret != 0) {
                LOG(WARNING) << __func__ << ": write failed for device " << i << ": " << ret;
            }
            maxLatency = std::max(maxLatency, proxy_get_latency(mAlsaDeviceProxies[i].get()));
        }
    }
    *actualFrameCount = frameCount;
    maxLatency = std::min(maxLatency, static_cast<unsigned>(std::numeric_limits<int32_t>::max()));
    *latencyMs = maxLatency;
    return ::android::OK;
}

void StreamAlsa::shutdown() {
    mAlsaDeviceProxies.clear();
}

}  // namespace aidl::android::hardware::audio::core