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
#include <Log.h>
#include <audio_utils/clock.h>
#include <error/Result.h>
#include <utils/SystemClock.h>

#include "ApeHeader.h"
#include "MpegAudioHeader.h"
#include "core-impl/StreamOffloadStub.h"

using aidl::android::hardware::audio::common::SourceMetadata;
using aidl::android::media::audio::common::AudioDevice;
using aidl::android::media::audio::common::AudioFormatType;
using aidl::android::media::audio::common::AudioOffloadInfo;
using aidl::android::media::audio::common::FlushFromFrameAccuracy;
using aidl::android::media::audio::common::MicrophoneInfo;

namespace aidl::android::hardware::audio::core {

namespace offload {

static constexpr int32_t SAFE_FLUSH_FROM_MARGIN_IN_MS = 100;
static constexpr int32_t SAFE_DRAIN_METADATA_MARGIN_IN_MS = 10;

std::string DspSimulatorLogic::init() {
    return "";
}

DspSimulatorLogic::Status DspSimulatorLogic::cycle() {
    using DrainMode = StreamDescriptor::DrainMode;
    std::vector<std::pair<int64_t, bool>> clipNotifies;
    DrainMode emptyDrainNotify = DrainMode::DRAIN_UNSPECIFIED;
    // Simulate playback.
    const int64_t timeBeginNs = ::android::uptimeNanos();
    usleep(1000);
    const int64_t clipFramesPlayed =
            (::android::uptimeNanos() - timeBeginNs) * mSharedState.sampleRate / NANOS_PER_SECOND;

    int64_t bufferFramesConsumed = 0, bufferFramesLeft = 0,
            bufferNotifyFrames = DspSimulatorState::kSkipBufferNotifyFrames;
    bool isEmpty = false;
    {
        std::lock_guard l(mSharedState.lock);
        int64_t bytesPerSecond = 0;

        if (mSharedState.format.type == AudioFormatType::PCM) {
            bytesPerSecond = mSharedState.sampleRate;
        } else {
            bytesPerSecond = mSharedState.bitRatePerSecond / 8;

            if (bytesPerSecond <= 0) {
                bytesPerSecond = mSharedState.fallbackBitRatePerSecond / 8;
            }
            if (bytesPerSecond <= 0) {
                // Fallback to a 1:2 compression ratio
                bytesPerSecond = mSharedState.sampleRate / 2LL;
            }
        }

        bufferFramesConsumed = clipFramesPlayed * bytesPerSecond / mSharedState.sampleRate;

        if (mSharedState.clips.empty()) {
            isEmpty = true;
            emptyDrainNotify = mSharedState.draining;
            if (mSharedState.draining != DrainMode::DRAIN_EARLY_NOTIFY) {
                mSharedState.draining = DrainMode::DRAIN_UNSPECIFIED;
            }
        }
        mSharedState.bufferFramesLeft =
                mSharedState.bufferFramesLeft > bufferFramesConsumed
                        ? mSharedState.bufferFramesLeft - bufferFramesConsumed
                        : 0;
        int64_t framesPlayed = clipFramesPlayed;
        while (framesPlayed > 0 && !mSharedState.clips.empty()) {
            LOG(VERBOSE) << __func__ << ": " << mSharedState.clips.log();
            const bool hasNextClip = mSharedState.clips.hasNext();
            if (mSharedState.clips.currentFrames() > framesPlayed) {
                mSharedState.clips.updateCurrentFrames(-framesPlayed);
                mSharedState.mTotalFramesPlayed += framesPlayed;
                framesPlayed = 0;
                if (auto clipFramesLeft = mSharedState.clips.currentFrames();
                    mSharedState.draining == DrainMode::DRAIN_EARLY_NOTIFY &&
                    clipFramesLeft <= mSharedState.earlyNotifyFrames) {
                    clipNotifies.emplace_back(clipFramesLeft, hasNextClip);
                }
            } else {
                mSharedState.mTotalFramesPlayed += mSharedState.clips.currentFrames();
                if (mSharedState.format.type == AudioFormatType::PCM) {
                    // There is not enough data to be full played, set `framesPlayed` to 0 so that
                    // it can exit the while loop.
                    framesPlayed = 0;
                    mSharedState.clips.trimCurrentFrames(0);
                    clipNotifies.emplace_back(0 /*clipFramesLeft*/, false /*hasNextClip*/);
                } else {
                    clipNotifies.emplace_back(0 /*clipFramesLeft*/, hasNextClip);
                    framesPlayed -= mSharedState.clips.removeCurrent();
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
    if (!clipNotifies.empty()) {
        for (const auto& notify : clipNotifies) {
            LOG(DEBUG) << __func__ << ": sending onClipStateChange: " << notify.first << ", "
                       << notify.second;
            mSharedState.callback->onClipStateChange(notify.first, notify.second);
        }
    } else if (emptyDrainNotify != DrainMode::DRAIN_UNSPECIFIED) {
        bool alreadySent;
        {
            std::lock_guard l(mSharedState.lock);
            alreadySent = mSharedState.isDrainCompleteClipStateChangeSent;
            if (!alreadySent) mSharedState.isDrainCompleteClipStateChangeSent = true;
        }
        if (!alreadySent) {
            LOG(DEBUG) << __func__ << ": sending onClipStateChange with no clips for "
                       << toString(emptyDrainNotify);
            // Regardless of the drain mode, the buffer is now empty, so we signal completion (0
            // frames).
            mSharedState.callback->onClipStateChange(0 /*clipFramesLeft*/, false /*hasNextClip*/);
            std::lock_guard l(mSharedState.lock);
            mSharedState.draining = DrainMode::DRAIN_UNSPECIFIED;
        }
    } else if (isEmpty) {
        // Sleep when idle to avoid busy-waiting and reduce CPU usage.
        usleep(10000);
    }
    return Status::CONTINUE;
}

}  // namespace offload

using offload::DspSimulatorState;

DriverOffloadStubImpl::DriverOffloadStubImpl(const StreamContext& context,
                                             const std::optional<AudioOffloadInfo>& offloadInfo)
    : DriverStubImpl(context, 0 /*asyncSleepTimeUs*/),
      mBufferNotifyFrames(static_cast<int64_t>(context.getBufferSizeInFrames()) / 2),
      mSafeMarginForFlushFromFrames(offload::SAFE_FLUSH_FROM_MARGIN_IN_MS *
                                    context.getSampleRate() / MILLIS_PER_SECOND),
      mSafeMarginForDrainMetadataFrames(offload::SAFE_DRAIN_METADATA_MARGIN_IN_MS *
                                        context.getSampleRate() / MILLIS_PER_SECOND),
      mState{context.getFormat(), context.getSampleRate(),
             250 /*earlyNotifyMs*/ * context.getSampleRate() / MILLIS_PER_SECOND,
             offloadInfo.has_value() ? offloadInfo->bitRatePerSecond : 0},
      mDspWorker(mState) {
    LOG_IF(FATAL, !mIsAsynchronous) << "The steam must be used in asynchronous mode";

    if (mState.format.encoding == "audio/x-ape") {
        mTransferHandler = &DriverOffloadStubImpl::handleApeTransfer;
    } else if (mState.format.encoding == "audio/mpeg") {
        mTransferHandler = &DriverOffloadStubImpl::handleMpegTransfer;
    } else if (mState.format.type == AudioFormatType::PCM) {
        mTransferHandler = &DriverOffloadStubImpl::handlePcmTransfer;
        // For PCM offload, there is no way for the HAL to know where is the end of a clip.
        // In that case, it assumes there is only one clip.
        LOG_IF(FATAL, !mState.clips.add(0)) << "Cannot initialize for PCM offload";
    }
}

::android::status_t DriverOffloadStubImpl::init(DriverCallbackInterface* callback) {
    RETURN_STATUS_IF_ERROR(DriverStubImpl::init(callback));
    if (!StreamOffloadStub::getSupportedEncodings().count(mState.format.encoding) &&
        mState.format.type != AudioFormatType::PCM) {
        LOG(ERROR) << __func__ << ": encoded format \"" << mState.format.toString()
                   << "\" is not supported";
        return ::android::NO_INIT;
    }
    mState.callback = callback;
    return ::android::OK;
}

::android::status_t DriverOffloadStubImpl::drain(StreamDescriptor::DrainMode drainMode) {
    RETURN_STATUS_IF_ERROR(DriverStubImpl::drain(drainMode));
    std::lock_guard l(mState.lock);
    if (!mState.clips.empty()) {
        mState.clips.trimCurrentFrames(mState.earlyNotifyFrames * 2);
        if (drainMode == StreamDescriptor::DrainMode::DRAIN_ALL) {
            mState.clips.eraseAllNext();
        }
    }
    mState.bufferNotifyFrames = DspSimulatorState::kSkipBufferNotifyFrames;
    mState.draining = drainMode;
    mState.isDrainCompleteClipStateChangeSent = false;
    return ::android::OK;
}

::android::status_t DriverOffloadStubImpl::flush() {
    RETURN_STATUS_IF_ERROR(DriverStubImpl::flush());
    mDspWorker.pause();
    {
        std::lock_guard l(mState.lock);
        mState.clips.erase();
        mState.bufferFramesLeft = 0;
        mState.bufferNotifyFrames = DspSimulatorState::kSkipBufferNotifyFrames;
        mState.draining = StreamDescriptor::DrainMode::DRAIN_UNSPECIFIED;
        mState.isDrainCompleteClipStateChangeSent = false;
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
    bool shouldResume;
    {
        std::lock_guard l(mState.lock);
        bool hasClips = !mState.clips.empty();
        bool isDraining = mState.draining != StreamDescriptor::DrainMode::DRAIN_UNSPECIFIED;
        // Allow notifications if resuming into a drain with an empty buffer.
        if (isDraining && !hasClips) {
            mState.isDrainCompleteClipStateChangeSent = false;
        }
        LOG(DEBUG) << __func__ << ": " << mState.clips.log();
        mState.bufferNotifyFrames = DspSimulatorState::kSkipBufferNotifyFrames;
        shouldResume = hasClips || isDraining;
    }
    if (shouldResume) {
        mDspWorker.resume();
    }
    return ::android::OK;
}

::android::status_t DriverOffloadStubImpl::transfer(void* buffer, size_t frameCount,
                                                    size_t* actualFrameCount, int32_t* latencyMs) {
    RETURN_STATUS_IF_ERROR(
            DriverStubImpl::transfer(buffer, frameCount, actualFrameCount, latencyMs));
    RETURN_STATUS_IF_ERROR(startWorkerIfNeeded());

    if (mTransferHandler != nullptr) {
        const ::android::status_t status =
                (this->*mTransferHandler)(buffer, frameCount, actualFrameCount);
        if (status != ::android::OK) {
            return status;
        }
    } else {
        LOG(FATAL) << __func__ << ": unsupported format for offload: " << mState.format.toString();
        *actualFrameCount = 0;
    }

    {
        std::lock_guard l(mState.lock);
        mState.bufferFramesLeft += *actualFrameCount;
        mState.bufferNotifyFrames = mBufferNotifyFrames;
    }
    mDspWorker.resume();
    return ::android::OK;
}

::android::status_t DriverOffloadStubImpl::flushFromFrame(FlushFromFrameAccuracy accuracy,
                                                          int32_t position,
                                                          int32_t* flushFromPosition) {
    LOG(DEBUG) << __func__ << ": accuracy=" << toString(accuracy) << ", position=" << position;
    if ((accuracy != FlushFromFrameAccuracy::BEST_EFFORT &&
         accuracy != FlushFromFrameAccuracy::EXACT) ||
        position < 0 || flushFromPosition == nullptr) {
        *flushFromPosition = position;
        LOG(ERROR) << __func__ << ": invalid parameters, accuracy= " << toString(accuracy)
                   << ", position=" << position
                   << ", suggestedPosition is null:" << (flushFromPosition == nullptr);
        return ::android::BAD_VALUE;
    }
    if (mState.format.type != AudioFormatType::PCM) {
        // Currently only support flushFromFrame for PCM offload.
        LOG(ERROR) << __func__ << ": invalid as format is " << mState.format.toString();
        return ::android::INVALID_OPERATION;
    }
    {
        std::lock_guard l(mState.lock);
        const int64_t requestedPosition = position + mState.mLastReportedFrames;
        const int64_t safeFlushedPosition =
                mState.mTotalFramesPlayed +
                std::min((int64_t)mSafeMarginForFlushFromFrames, mState.clips.currentFrames());
        const int64_t totalWrittenFrames = mState.mTotalFramesPlayed + mState.clips.currentFrames();
        if (accuracy == FlushFromFrameAccuracy::BEST_EFFORT) {
            if (requestedPosition < safeFlushedPosition) {
                *flushFromPosition = safeFlushedPosition - mState.mLastReportedFrames;
                mState.clips.trimCurrentFrames(safeFlushedPosition - mState.mTotalFramesPlayed);
            } else if (requestedPosition > totalWrittenFrames) {
                *flushFromPosition = totalWrittenFrames - mState.mLastReportedFrames;
            } else {
                *flushFromPosition = requestedPosition - mState.mLastReportedFrames;
                mState.clips.trimCurrentFrames(requestedPosition - mState.mTotalFramesPlayed);
            }
        } else {  // accuracy == FlushFromFrameAccuracy::EXACT
            if (requestedPosition < safeFlushedPosition || requestedPosition > totalWrittenFrames) {
                *flushFromPosition = safeFlushedPosition - mState.mLastReportedFrames;
                return ::android::BAD_VALUE;
            } else {
                *flushFromPosition = requestedPosition - mState.mLastReportedFrames;
                mState.clips.trimCurrentFrames(requestedPosition - mState.mTotalFramesPlayed);
            }
        }
    }
    return ::android::OK;
}

::android::status_t DriverOffloadStubImpl::handleApeTransfer(void* buffer, size_t frameCount,
                                                             size_t* actualFrameCount) {
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
                if (!mState.clips.add(clipDurationFrames)) {
                    LOG(ERROR) << __func__
                               << ": not ready for the next clip, clips: " << mState.clips.log();
                    return ::android::INVALID_OPERATION;
                }
            } else {
                LOG(ERROR) << __func__ << ": clip sample rate " << clipSampleRate
                           << " does not match stream sample rate " << mState.sampleRate;
                return ::android::BAD_VALUE;
            }
        } else {
            frameCount = 0;
        }
    }
    return ::android::OK;
}

::android::status_t DriverOffloadStubImpl::handleMpegTransfer(void* buffer, size_t frameCount,
                                                              size_t* actualFrameCount) {
    const size_t bufferSizeBytes = frameCount * mFrameSizeBytes;
    const uint8_t* currentPtr = static_cast<const uint8_t*>(buffer);
    const uint8_t* const endPtr = currentPtr + bufferSizeBytes;
    const uint8_t* const beginPtr = currentPtr;

    // Track total audio payload bytes to distinguish from metadata.
    size_t totalAudioBytes = 0;

    if (mMpegFrameState.bytesPending > 0) {
        size_t processBytes =
                std::min(mMpegFrameState.bytesPending, static_cast<size_t>(endPtr - currentPtr));
        currentPtr += processBytes;
        mMpegFrameState.bytesPending -= processBytes;
        if (mMpegFrameState.isAudioFrame) {
            totalAudioBytes += processBytes;
            // Incremental Timing: Update duration based on ratio of bytes processed to fix time
            // jump.
            if (mMpegFrameState.totalFrameLengthBytes > 0) {
                std::lock_guard l(mState.lock);
                const int64_t processedFrames = mMpegFrameState.frameSize * (int64_t)processBytes /
                                                mMpegFrameState.totalFrameLengthBytes;
                mState.clips.updateLastFrames(processedFrames);
                LOG(DEBUG) << __func__ << ": added samples=" << processedFrames;
            }
        }
    }
    while (currentPtr < endPtr) {
        const uint8_t* const frameBeginning = currentPtr;
        std::optional<MpegFrame> frameOpt = findMpegFrame(&currentPtr, endPtr);
        if (!frameOpt.has_value()) {
            // Could not find a header in the input buffer.
            mMpegFrameState.bytesPending = 0;
            break;
        }

        const MpegFrame& frame = frameOpt.value();
        LOG(DEBUG) << __func__ << ": Found at offset " << frameBeginning - beginPtr << " " << frame;
        mMpegFrameState.isAudioFrame = !(frame.isID3v2 || frame.isID3v1);
        if (frame.isID3v1) {
            mMpegFrameState.clipEnded = true;
        } else {
            if (frame.sampleRate != mState.sampleRate) {
                LOG(ERROR) << __func__ << ": clip sample rate " << frame.sampleRate
                           << " does not match stream sample rate " << mState.sampleRate;
                return ::android::BAD_VALUE;
            }

            // Calculate physical bytes consumed in this iteration
            size_t bytesInThisBuffer = currentPtr - frameBeginning;
            if (mMpegFrameState.isAudioFrame) {
                totalAudioBytes += bytesInThisBuffer;
            }

            std::lock_guard l(mState.lock);
            if (frame.bitRate > 0) mState.bitRatePerSecond = frame.bitRate;
            if (frame.isID3v2 || mState.clips.empty() || mMpegFrameState.clipEnded) {
                if (!mState.clips.add(0)) {
                    LOG(ERROR) << __func__
                               << ": not ready for the next clip, clips: " << mState.clips.log();
                    return ::android::INVALID_OPERATION;
                }
                mMpegFrameState.clipEnded = false;
            }
            // Proportional timing fix: satisfy PauseAsync by adding time for bytes delivered only.
            if (mMpegFrameState.isAudioFrame && frame.frameLengthBytes > 0) {
                const int64_t processedFrames =
                        frame.frameSize * (int64_t)bytesInThisBuffer / frame.frameLengthBytes;
                mState.clips.updateLastFrames(processedFrames);
                LOG(DEBUG) << __func__ << ": added samples=" << processedFrames;
            }
        }
        if (currentPtr == endPtr) {
            mMpegFrameState.bytesPending = frame.bytesPending;
            mMpegFrameState.frameSize = frame.frameSize;
            mMpegFrameState.totalFrameLengthBytes = frame.frameLengthBytes;
            break;
        }
    }
    *actualFrameCount = static_cast<size_t>(currentPtr - beginPtr) / mFrameSizeBytes;

    // If totalAudioBytes > 0, we have mixed content, so we subtract metadata bytes.
    // If totalAudioBytes == 0 (Header Only), we SKIP subtraction to keep buffer non-empty.
    // The fallback bitrate in cycle() ensures it eventually drains.
    if (*actualFrameCount > totalAudioBytes) {
        size_t nonAudioBytes = *actualFrameCount - totalAudioBytes;
        if (totalAudioBytes > 0) {
            std::lock_guard l(mState.lock);

            // Calculate the anticipated buffer size once the caller adds the frames.
            int64_t projectedBufferLevel = mState.bufferFramesLeft + totalAudioBytes;

            // Ensure the buffer level does not dip below mBufferNotifyFrames after subtraction.
            // An instant drop triggers the worker thread to fire onTransferReady prematurely...
            const int64_t safeThreshold = mBufferNotifyFrames + mSafeMarginForDrainMetadataFrames;

            if (projectedBufferLevel < safeThreshold) {
                // Subtract only enough to perfectly land on the safeThreshold
                int64_t allowedSubtract =
                        (mState.bufferFramesLeft + *actualFrameCount) - safeThreshold;
                if (allowedSubtract > 0) {
                    mState.bufferFramesLeft -= allowedSubtract;
                }
            } else {
                // Safe to subtract all nonAudioBytes without triggering the race condition
                mState.bufferFramesLeft -= nonAudioBytes;
            }
        }
    }
    return ::android::OK;
}

::android::status_t DriverOffloadStubImpl::handlePcmTransfer(void* /*buffer*/, size_t frameCount,
                                                             size_t* actualFrameCount) {
    *actualFrameCount = frameCount;
    std::lock_guard l(mState.lock);
    mState.clips.updateLastFrames(frameCount);
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

::android::status_t DriverOffloadStubImpl::refinePosition(StreamDescriptor::Position* position) {
    std::lock_guard l(mState.lock);
    position->frames = mState.mTotalFramesPlayed;
    return ::android::OK;
}

// static
const std::set<std::string>& StreamOffloadStub::getSupportedEncodings() {
    static const std::set<std::string> kSupportedEncodings = {
            "audio/x-ape",
            "audio/mpeg",
    };
    return kSupportedEncodings;
}

StreamOffloadStub::StreamOffloadStub(StreamContext* context, const Metadata& metadata,
                                     const AudioOffloadInfo& offloadInfo)
    : StreamCommonImpl(context, metadata), DriverOffloadStubImpl(getContext(), offloadInfo) {}

StreamOffloadStub::~StreamOffloadStub() {
    cleanupWorker();
}

StreamOutOffloadStub::StreamOutOffloadStub(StreamContext&& context,
                                           const SourceMetadata& sourceMetadata,
                                           const std::optional<AudioOffloadInfo>& offloadInfo)
    : StreamOut(std::move(context), offloadInfo),
      StreamOffloadStub(&mContextInstance, sourceMetadata, offloadInfo.value()) {}

}  // namespace aidl::android::hardware::audio::core
