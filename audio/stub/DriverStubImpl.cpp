/*
 * Copyright (C) 2024 The Android Open Source Project
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
#include <cstdlib>

#define LOG_TAG "AHAL_Stream"
#include <android-base/logging.h>
#include <audio_utils/clock.h>

#include "core-impl/DriverStubImpl.h"

namespace aidl::android::hardware::audio::core {

DriverStubImpl::DriverStubImpl(const StreamContext& context, int asyncSleepTimeUs)
    : mBufferSizeFrames(context.getBufferSizeInFrames()),
      mFrameSizeBytes(context.getFrameSize()),
      mSampleRate(context.getSampleRate()),
      mIsAsynchronous(!!context.getAsyncCallback()),
      mIsInput(context.isInput()),
      mMixPortHandle(context.getMixPortHandle()),
      mAsyncSleepTimeUs(asyncSleepTimeUs) {}

#define LOG_ENTRY()                                                                          \
    LOG(DEBUG) << "[" << (mIsInput ? "in" : "out") << "|ioHandle:" << mMixPortHandle << "] " \
               << __func__;

::android::status_t DriverStubImpl::init(DriverCallbackInterface* /*callback*/) {
    LOG_ENTRY();
    mIsInitialized = true;
    return ::android::OK;
}

::android::status_t DriverStubImpl::drain(StreamDescriptor::DrainMode) {
    LOG_ENTRY();
    if (!mIsInitialized) {
        LOG(FATAL) << __func__ << ": must not happen for an uninitialized driver";
    }
    if (!mIsInput) {
        if (!mIsAsynchronous) {
            static constexpr float kMicrosPerSecond = MICROS_PER_SECOND;
            const size_t delayUs = static_cast<size_t>(
                    std::roundf(mBufferSizeFrames * kMicrosPerSecond / mSampleRate));
            usleep(delayUs);
        } else if (mAsyncSleepTimeUs) {
            usleep(mAsyncSleepTimeUs);
        }
    }
    return ::android::OK;
}

::android::status_t DriverStubImpl::flush() {
    LOG_ENTRY();
    if (!mIsInitialized) {
        LOG(FATAL) << __func__ << ": must not happen for an uninitialized driver";
    }
    return ::android::OK;
}

::android::status_t DriverStubImpl::pause() {
    LOG_ENTRY();
    if (!mIsInitialized) {
        LOG(FATAL) << __func__ << ": must not happen for an uninitialized driver";
    }
    return ::android::OK;
}

::android::status_t DriverStubImpl::standby() {
    LOG_ENTRY();
    if (!mIsInitialized) {
        LOG(FATAL) << __func__ << ": must not happen for an uninitialized driver";
    }
    mIsStandby = true;
    return ::android::OK;
}

::android::status_t DriverStubImpl::start() {
    LOG_ENTRY();
    if (!mIsInitialized) {
        LOG(FATAL) << __func__ << ": must not happen for an uninitialized driver";
    }
    mIsStandby = false;
    mStartTimeNs = ::android::uptimeNanos();
    mFramesSinceStart = 0;
    return ::android::OK;
}

::android::status_t DriverStubImpl::transfer(void* buffer, size_t frameCount,
                                             size_t* actualFrameCount, int32_t*) {
    // No LOG_ENTRY as this is called very often.
    if (!mIsInitialized) {
        LOG(FATAL) << __func__ << ": must not happen for an uninitialized driver";
    }
    if (mIsStandby) {
        LOG(FATAL) << __func__ << ": must not happen while in standby";
    }
    *actualFrameCount = frameCount;
    if (mIsAsynchronous) {
        if (mAsyncSleepTimeUs) usleep(mAsyncSleepTimeUs);
    } else {
        mFramesSinceStart += *actualFrameCount;
        const long bufferDurationUs = (*actualFrameCount) * MICROS_PER_SECOND / mSampleRate;
        const auto totalDurationUs =
                (::android::uptimeNanos() - mStartTimeNs) / NANOS_PER_MICROSECOND;
        const long totalOffsetUs =
                mFramesSinceStart * MICROS_PER_SECOND / mSampleRate - totalDurationUs;
        LOG(VERBOSE) << __func__ << ": totalOffsetUs " << totalOffsetUs;
        if (totalOffsetUs > 0) {
            const long sleepTimeUs = std::min(totalOffsetUs, bufferDurationUs);
            LOG(VERBOSE) << __func__ << ": sleeping for " << sleepTimeUs << " us";
            usleep(sleepTimeUs);
        }
    }
    if (mIsInput) {
        uint8_t* byteBuffer = static_cast<uint8_t*>(buffer);
        for (size_t i = 0; i < frameCount * mFrameSizeBytes; ++i) {
            byteBuffer[i] = std::rand() % 255;
        }
    }
    return ::android::OK;
}

void DriverStubImpl::shutdown() {
    LOG_ENTRY();
    mIsInitialized = false;
}

}  // namespace aidl::android::hardware::audio::core
