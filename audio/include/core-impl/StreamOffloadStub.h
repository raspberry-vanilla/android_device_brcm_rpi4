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

#pragma once

#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "core-impl/DriverStubImpl.h"
#include "core-impl/Stream.h"

namespace aidl::android::hardware::audio::core {

namespace offload {

class DspClipState {
  public:
    static constexpr int64_t kError = -1;
    static constexpr size_t kClipCountLimit = 2;

    bool add(int64_t frames) {
        if (mFrames.size() < kClipCountLimit) {
            mFrames.push_back(frames);
            return true;
        }
        return false;
    }
    int64_t currentFrames() const { return !empty() ? mFrames[0] : kError; }
    bool empty() const { return mFrames.empty(); }
    bool hasNext() const { return mFrames.size() > 1; }
    void erase() { mFrames.clear(); }
    void eraseAllNext() {
        if (!empty()) mFrames.resize(1);
    }
    int64_t removeCurrent() {
        if (empty()) return kError;
        const int64_t result = mFrames[0];
        mFrames.erase(mFrames.begin());
        return result;
    }
    int64_t trimCurrentFrames(int64_t frames) {
        if (!empty()) {
            if (mFrames[0] > frames) mFrames[0] = frames;
            return mFrames[0];
        }
        return kError;
    }
    int64_t updateCurrentFrames(int64_t delta) {
        return !empty() ? updateFrames(0, delta) : kError;
    }
    int64_t updateLastFrames(int64_t delta) {
        return !empty() ? updateFrames(mFrames.size() - 1, delta) : kError;
    }

    std::string log() const { return ::android::internal::ToString(mFrames); }

  private:
    int64_t updateFrames(size_t index, int64_t delta) {
        return delta >= 0 || mFrames[index] >= -delta ? mFrames[index] += delta : kError;
    }
    std::vector<int64_t> mFrames;
};

struct DspSimulatorState {
    static constexpr int64_t kSkipBufferNotifyFrames = -1;

    const ::aidl::android::media::audio::common::AudioFormatDescription format;
    const int sampleRate;
    const int64_t earlyNotifyFrames;
    const int fallbackBitRatePerSecond;           // Fallback from AudioOffloadInfo
    DriverCallbackInterface* callback = nullptr;  // set before starting DSP worker
    std::mutex lock;
    DspClipState clips GUARDED_BY(lock);
    int bitRatePerSecond GUARDED_BY(lock) = 0;
    int64_t bufferFramesLeft GUARDED_BY(lock) = 0;
    int64_t bufferNotifyFrames GUARDED_BY(lock) = kSkipBufferNotifyFrames;
    StreamDescriptor::DrainMode draining GUARDED_BY(lock) =
            StreamDescriptor::DrainMode::DRAIN_UNSPECIFIED;
    int64_t mTotalFramesPlayed GUARDED_BY(lock) = 0;
    int64_t mLastReportedFrames GUARDED_BY(lock) = 0;
    bool isDrainCompleteClipStateChangeSent GUARDED_BY(lock) = false;
};

class DspSimulatorLogic : public ::android::hardware::audio::common::StreamLogic {
  protected:
    explicit DspSimulatorLogic(DspSimulatorState& sharedState) : mSharedState(sharedState) {}
    std::string init() override;
    Status cycle() override;

  private:
    DspSimulatorState& mSharedState;
};

class DspSimulatorWorker
    : public ::android::hardware::audio::common::StreamWorker<DspSimulatorLogic> {
  public:
    explicit DspSimulatorWorker(DspSimulatorState& sharedState)
        : ::android::hardware::audio::common::StreamWorker<DspSimulatorLogic>(sharedState) {}
};

}  // namespace offload

struct MpegFrameState {
    bool clipEnded = false;
    size_t bytesPending = 0;

    // True if the frame contains actual audio samples.
    // False if it is a metadata/ID3 tag.
    bool isAudioFrame = false;
    int frameSize = 0;
    size_t totalFrameLengthBytes = 0;
};

class DriverOffloadStubImpl : public DriverStubImpl {
  public:
    explicit DriverOffloadStubImpl(
            const StreamContext& context,
            const std::optional<::aidl::android::media::audio::common::AudioOffloadInfo>&
                    offloadInfo);
    ::android::status_t init(DriverCallbackInterface* callback) override;
    ::android::status_t drain(StreamDescriptor::DrainMode drainMode) override;
    ::android::status_t flush() override;
    ::android::status_t pause() override;
    ::android::status_t start() override;
    ::android::status_t transfer(void* buffer, size_t frameCount, size_t* actualFrameCount,
                                 int32_t* latencyMs) override;
    ::android::status_t flushFromFrame(
            ::aidl::android::media::audio::common::FlushFromFrameAccuracy accuracy,
            int32_t position, int32_t* flushFromPosition) override;
    void shutdown() override;

    ::android::status_t refinePosition(StreamDescriptor::Position* position) override;

  private:
    ::android::status_t startWorkerIfNeeded();
    ::android::status_t handleApeTransfer(void* buffer, size_t frameCount,
                                          size_t* actualFrameCount);
    ::android::status_t handleMpegTransfer(void* buffer, size_t frameCount,
                                           size_t* actualFrameCount);
    ::android::status_t handlePcmTransfer(void* buffer, size_t frameCount,
                                          size_t* actualFrameCount);
    ::android::status_t (DriverOffloadStubImpl::*mTransferHandler)(void*, size_t, size_t*);

    const int64_t mBufferNotifyFrames;
    const int32_t mSafeMarginForFlushFromFrames;
    const int32_t mSafeMarginForDrainMetadataFrames;
    offload::DspSimulatorState mState;
    offload::DspSimulatorWorker mDspWorker;
    bool mDspWorkerStarted = false;
    MpegFrameState mMpegFrameState;
};

class StreamOffloadStub : public StreamCommonImpl, public DriverOffloadStubImpl {
  public:
    static const std::set<std::string>& getSupportedEncodings();

    StreamOffloadStub(StreamContext* context, const Metadata& metadata,
                      const ::aidl::android::media::audio::common::AudioOffloadInfo& offloadInfo);
    ~StreamOffloadStub();
};

class StreamOutOffloadStub final : public StreamOut, public StreamOffloadStub {
  public:
    friend class ndk::SharedRefBase;
    StreamOutOffloadStub(
            StreamContext&& context,
            const ::aidl::android::hardware::audio::common::SourceMetadata& sourceMetadata,
            const std::optional<::aidl::android::media::audio::common::AudioOffloadInfo>&
                    offloadInfo);

  private:
    void onClose(StreamDescriptor::State) override { defaultOnClose(); }
};

}  // namespace aidl::android::hardware::audio::core
