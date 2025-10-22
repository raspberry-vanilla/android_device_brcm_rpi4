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

struct DspSimulatorState {
    static constexpr int64_t kSkipBufferNotifyFrames = -1;

    const std::string formatEncoding;
    const int sampleRate;
    const int64_t earlyNotifyFrames;
    DriverCallbackInterface* callback = nullptr;  // set before starting DSP worker
    std::mutex lock;
    std::vector<int64_t> clipFramesLeft GUARDED_BY(lock);
    int64_t bufferFramesLeft GUARDED_BY(lock) = 0;
    int64_t bufferNotifyFrames GUARDED_BY(lock) = kSkipBufferNotifyFrames;
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

class DriverOffloadStubImpl : public DriverStubImpl {
  public:
    explicit DriverOffloadStubImpl(const StreamContext& context);
    ::android::status_t init(DriverCallbackInterface* callback) override;
    ::android::status_t drain(StreamDescriptor::DrainMode drainMode) override;
    ::android::status_t flush() override;
    ::android::status_t pause() override;
    ::android::status_t start() override;
    ::android::status_t transfer(void* buffer, size_t frameCount, size_t* actualFrameCount,
                                 int32_t* latencyMs) override;
    void shutdown() override;

  private:
    ::android::status_t startWorkerIfNeeded();

    const int64_t mBufferNotifyFrames;
    offload::DspSimulatorState mState;
    offload::DspSimulatorWorker mDspWorker;
    bool mDspWorkerStarted = false;
};

class StreamOffloadStub : public StreamCommonImpl, public DriverOffloadStubImpl {
  public:
    static const std::set<std::string>& getSupportedEncodings();

    StreamOffloadStub(StreamContext* context, const Metadata& metadata);
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
