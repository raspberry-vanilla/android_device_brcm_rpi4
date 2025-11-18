/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <audio_effects/effect_dynamicsprocessing.h>
#include <system/audio_effects/effect_visualizer.h>

#include "effect-impl/EffectContext.h"

namespace aidl::android::hardware::audio::effect {

class VisualizerContext final : public EffectContext {
  public:
    // need align the min/max capture size to VISUALIZER_CAPTURE_SIZE_MIN and
    // VISUALIZER_CAPTURE_SIZE_MAX because of limitation in audio_utils fixedfft.
    static constexpr int32_t kMinCaptureBufSize = VISUALIZER_CAPTURE_SIZE_MIN;
    static constexpr int32_t kMaxCaptureBufSize = VISUALIZER_CAPTURE_SIZE_MAX;
    static constexpr uint32_t kMaxLatencyMs = 3000;  // 3 seconds of latency for audio pipeline

    VisualizerContext(int statusDepth, const Parameter::Common& common);
    ~VisualizerContext();

    RetCode initParams(const Parameter::Common& common);

    RetCode enable() override;
    RetCode disable() override;
    // keep all parameters and reset buffer.
    RetCode reset() override;

    RetCode setCaptureSamples(int32_t captureSize);
    int32_t getCaptureSamples();
    RetCode setMeasurementMode(Visualizer::MeasurementMode mode);
    Visualizer::MeasurementMode getMeasurementMode();
    RetCode setScalingMode(Visualizer::ScalingMode mode);
    Visualizer::ScalingMode getScalingMode();
    RetCode setDownstreamLatency(int latency);
    int getDownstreamLatency();

    IEffect::Status process(float* in, float* out, int samples);
    // Gets the current measurements, measured by process() and consumed by getParameter()
    Visualizer::Measurement getMeasure();
    // Gets the latest PCM capture, data captured by process() and consumed by getParameter()
    std::vector<uint8_t> capture();

    struct BufferStats {
        bool mIsValid;
        uint16_t mPeakU16; // the positive peak of the absolute value of the samples in a buffer
        float mRmsSquared; // the average square of the samples in a buffer
    };

    enum State {
        UNINITIALIZED,
        INITIALIZED,
        ACTIVE,
    };

  private:
    // maximum time since last capture buffer update before resetting capture buffer. This means
    // that the framework has stopped playing audio and we must start returning silence
    static const uint32_t kMaxStallTimeMs = 1000;
    // discard measurements older than this number of ms
    static const uint32_t kDiscardMeasurementsTimeMs = 2000;
    // maximum number of buffers for which we keep track of the measurements
    // note: buffer index is stored in uint8_t
    static const uint32_t kMeasurementWindowMaxSizeInBuffers = 25;

    Parameter::Common mCommon;
    State mState = State::UNINITIALIZED;
    uint32_t mCaptureIdx = 0;
    uint32_t mLastCaptureIdx = 0;
    Visualizer::ScalingMode mScalingMode = Visualizer::ScalingMode::NORMALIZED;
    struct timespec mBufferUpdateTime;
    // capture buf with 8 bits mono PCM samples
    std::array<uint8_t, kMaxCaptureBufSize> mCaptureBuf;
    uint32_t mDownstreamLatency = 0;
    int32_t mCaptureSamples = kMaxCaptureBufSize;

    // to avoid recomputing it every time a buffer is processed
    uint8_t mChannelCount = 0;
    Visualizer::MeasurementMode mMeasurementMode =
            Visualizer::MeasurementMode::NONE;
    uint8_t mMeasurementWindowSizeInBuffers = kMeasurementWindowMaxSizeInBuffers;
    uint8_t mMeasurementBufferIdx = 0;
    std::array<BufferStats, kMeasurementWindowMaxSizeInBuffers> mPastMeasurements;
    void init_params();

    uint32_t getDeltaTimeMsFromUpdatedTime_l();
};
}  // namespace aidl::android::hardware::audio::effect
