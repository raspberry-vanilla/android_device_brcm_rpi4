/*
 * Copyright (C) 2025 The Android Open Source Project
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

#define LOG_TAG "AHAL_OffloadStream"
#include <android-base/logging.h>
#include <audio_utils/clock.h>
#include <error/Result.h>
#include <utils/SystemClock.h>

#include "ApeHeader.h"
#include "core-impl/StreamOffloadStub.h"

using aidl::android::hardware::audio::common::SourceMetadata;
using aidl::android::media::audio::common::AudioDevice;
using aidl::android::media::audio::common::AudioOffloadInfo;
using aidl::android::media::audio::common::MicrophoneInfo;

namespace aidl::android::hardware::audio::core {

namespace offload {

std::string DspSimulatorLogic::init() {
    return "";
}

DspSimulatorLogic::Status DspSimulatorLogic::cycle() {
    std::vector<std::pair<int64_t, bool>> clipNotifies;
    // Simulate playback.
    const int64_t timeBeginNs = ::android::uptimeNanos();
    usleep(1000);
    const int64_t clipFramesPlayed =
            (::android::uptimeNanos() - timeBeginNs) * mSharedState.sampleRate / NANOS_PER_SECOND;
    const int64_t bufferFramesConsumed = clipFramesPlayed / 2;  // assume 1:2 compression ratio
    int64_t bufferFramesLeft = 0, bufferNotifyFrames = DspSimulatorState::kSkipBufferNotifyFrames;
    {
        std::lock_guard l(mSharedState.lock);
        mSharedState.bufferFramesLeft =
                mSharedState.bufferFramesLeft > bufferFramesConsumed
                        ? mSharedState.bufferFramesLeft - bufferFramesConsumed
                        : 0;
        int64_t framesPlayed = clipFramesPlayed;
        while (framesPlayed > 0 && !mSharedState.clipFramesLeft.empty()) {
            LOG(VERBOSE) << __func__ << ": clips: "
                         << ::android::internal::ToString(mSharedState.clipFramesLeft);
            const bool hasNextClip = mSharedState.clipFramesLeft.size() > 1;
            if (mSharedState.clipFramesLeft[0] > framesPlayed) {
                mSharedState.clipFramesLeft[0] -= framesPlayed;
                framesPlayed = 0;
                if (mSharedState.clipFramesLeft[0] <= mSharedState.earlyNotifyFrames) {
                    clipNotifies.emplace_back(mSharedState.clipFramesLeft[0], hasNextClip);
                }
            } else {
                clipNotifies.emplace_back(0 /*clipFramesLeft*/, hasNextClip);
                framesPlayed -= mSharedState.clipFramesLeft[0];
                mSharedState.clipFramesLeft.erase(mSharedState.clipFramesLeft.begin());
                if (!hasNextClip) {
                    // Since it's a simulation, the buffer consumption rate it not real,
                    // thus 'bufferFramesLeft' might still have something, need to erase it.
                    mSharedState.bufferFramesLeft = 0;
                }
            }
        }
        bufferFramesLeft = mSharedState.bufferFramesLeft;
        bufferNotifyFrames = mSharedState.bufferNotifyFrames;
        if (bufferFramesLeft <= bufferNotifyFrames) {
            // Suppress further notifications.
            mSharedState.bufferNotifyFrames = DspSimulatorState::kSkipBufferNotifyFrames;
        }
    }
    if (bufferFramesLeft <= bufferNotifyFrames) {
        LOG(DEBUG) << __func__ << ": sending onBufferStateChange: " << bufferFramesLeft;
        mSharedState.callback->onBufferStateChange(bufferFramesLeft);
    }
    for (const auto& notify : clipNotifies) {
        LOG(DEBUG) << __func__ << ": sending onClipStateChange: " << notify.first << ", "
                   << notify.second;
        mSharedState.callback->onClipStateChange(notify.first, notify.second);
    }
    return Status::CONTINUE;
}

}  // namespace offload

using offload::DspSimulatorState;

DriverOffloadStubImpl::DriverOffloadStubImpl(const StreamContext& context)
    : DriverStubImpl(context, 0 /*asyncSleepTimeUs*/),
      mBufferNotifyFrames(static_cast<int64_t>(context.getBufferSizeInFrames()) / 2),
      mState{context.getFormat().encoding, context.getSampleRate(),
             250 /*earlyNotifyMs*/ * context.getSampleRate() / MILLIS_PER_SECOND},
      mDspWorker(mState) {
    LOG_IF(FATAL, !mIsAsynchronous) << "The steam must be used in asynchronous mode";
}

::android::status_t DriverOffloadStubImpl::init(DriverCallbackInterface* callback) {
    RETURN_STATUS_IF_ERROR(DriverStubImpl::init(callback));
    if (!StreamOffloadStub::getSupportedEncodings().count(mState.formatEncoding)) {
        LOG(ERROR) << __func__ << ": encoded format \"" << mState.formatEncoding
                   << "\" is not supported";
        return ::android::NO_INIT;
    }
    mState.callback = callback;
    return ::android::OK;
}

::android::status_t DriverOffloadStubImpl::drain(StreamDescriptor::DrainMode drainMode) {
    RETURN_STATUS_IF_ERROR(DriverStubImpl::drain(drainMode));
    std::lock_guard l(mState.lock);
    if (!mState.clipFramesLeft.empty()) {
        // Cut playback of the current clip.
        mState.clipFramesLeft[0] = std::min(mState.earlyNotifyFrames * 2, mState.clipFramesLeft[0]);
        if (drainMode == StreamDescriptor::DrainMode::DRAIN_ALL) {
            // Make sure there are no clips after the current one.
            mState.clipFramesLeft.resize(1);
        }
    }
    mState.bufferNotifyFrames = DspSimulatorState::kSkipBufferNotifyFrames;
    return ::android::OK;
}

::android::status_t DriverOffloadStubImpl::flush() {
    RETURN_STATUS_IF_ERROR(DriverStubImpl::flush());
    mDspWorker.pause();
    {
        std::lock_guard l(mState.lock);
        mState.clipFramesLeft.clear();
        mState.bufferFramesLeft = 0;
        mState.bufferNotifyFrames = DspSimulatorState::kSkipBufferNotifyFrames;
    }
    return ::android::OK;
}

::android::status_t DriverOffloadStubImpl::pause() {
    RETURN_STATUS_IF_ERROR(DriverStubImpl::pause());
    mDspWorker.pause();
    {
        std::lock_guard l(mState.lock);
        mState.bufferNotifyFrames = DspSimulatorState::kSkipBufferNotifyFrames;
    }
    return ::android::OK;
}

::android::status_t DriverOffloadStubImpl::start() {
    RETURN_STATUS_IF_ERROR(DriverStubImpl::start());
    RETURN_STATUS_IF_ERROR(startWorkerIfNeeded());
    bool hasClips;  // Can be start after paused draining.
    {
        std::lock_guard l(mState.lock);
        hasClips = !mState.clipFramesLeft.empty();
        LOG(DEBUG) << __func__
                   << ": clipFramesLeft: " << ::android::internal::ToString(mState.clipFramesLeft);
        mState.bufferNotifyFrames = DspSimulatorState::kSkipBufferNotifyFrames;
    }
    if (hasClips) {
        mDspWorker.resume();
    }
    return ::android::OK;
}

::android::status_t DriverOffloadStubImpl::transfer(void* buffer, size_t frameCount,
                                                    size_t* actualFrameCount, int32_t* latencyMs) {
    RETURN_STATUS_IF_ERROR(
            DriverStubImpl::transfer(buffer, frameCount, actualFrameCount, latencyMs));
    RETURN_STATUS_IF_ERROR(startWorkerIfNeeded());
    // Scan the buffer for clip headers.
    *actualFrameCount = frameCount;
    while (buffer != nullptr && frameCount > 0) {
        ApeHeader* apeHeader = nullptr;
        void* prevBuffer = buffer;
        buffer = findApeHeader(prevBuffer, frameCount * mFrameSizeBytes, &apeHeader);
        if (buffer != nullptr && apeHeader != nullptr) {
            // Frame count does not include the size of the header data.
            const size_t headerSizeFrames =
                    (static_cast<uint8_t*>(buffer) - static_cast<uint8_t*>(prevBuffer)) /
                    mFrameSizeBytes;
            frameCount -= headerSizeFrames;
            *actualFrameCount = frameCount;
            // Stage the clip duration into the DSP worker's queue.
            const int64_t clipDurationFrames = getApeClipDurationFrames(apeHeader);
            const int32_t clipSampleRate = apeHeader->sampleRate;
            LOG(DEBUG) << __func__ << ": found APE clip of " << clipDurationFrames << " frames, "
                       << "sample rate: " << clipSampleRate;
            if (clipSampleRate == mState.sampleRate) {
                std::lock_guard l(mState.lock);
                mState.clipFramesLeft.push_back(clipDurationFrames);
            } else {
                LOG(ERROR) << __func__ << ": clip sample rate " << clipSampleRate
                           << " does not match stream sample rate " << mState.sampleRate;
            }
        } else {
            frameCount = 0;
        }
    }
    {
        std::lock_guard l(mState.lock);
        mState.bufferFramesLeft = *actualFrameCount;
        mState.bufferNotifyFrames = mBufferNotifyFrames;
    }
    mDspWorker.resume();
    return ::android::OK;
}

void DriverOffloadStubImpl::shutdown() {
    LOG(DEBUG) << __func__ << ": stopping the DSP simulator worker";
    mDspWorker.stop();
    DriverStubImpl::shutdown();
}

::android::status_t DriverOffloadStubImpl::startWorkerIfNeeded() {
    if (!mDspWorkerStarted) {
        // This is an "audio service thread," must have elevated priority.
        if (!mDspWorker.start("dsp_sim", ANDROID_PRIORITY_URGENT_AUDIO)) {
            return ::android::NO_INIT;
        }
        mDspWorkerStarted = true;
    }
    return ::android::OK;
}

// static
const std::set<std::string>& StreamOffloadStub::getSupportedEncodings() {
    static const std::set<std::string> kSupportedEncodings = {
            "audio/x-ape",
    };
    return kSupportedEncodings;
}

StreamOffloadStub::StreamOffloadStub(StreamContext* context, const Metadata& metadata)
    : StreamCommonImpl(context, metadata), DriverOffloadStubImpl(getContext()) {}

StreamOffloadStub::~StreamOffloadStub() {
    cleanupWorker();
}

StreamOutOffloadStub::StreamOutOffloadStub(StreamContext&& context,
                                           const SourceMetadata& sourceMetadata,
                                           const std::optional<AudioOffloadInfo>& offloadInfo)
    : StreamOut(std::move(context), offloadInfo),
      StreamOffloadStub(&mContextInstance, sourceMetadata) {}

}  // namespace aidl::android::hardware::audio::core
