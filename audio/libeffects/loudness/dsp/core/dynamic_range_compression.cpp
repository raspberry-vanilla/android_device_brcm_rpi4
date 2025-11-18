/*
 * Copyright (C) 2013 The Android Open Source Project
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
//#define LOG_NDEBUG 0

#include <log/log.h>
#include "dsp/core/dynamic_range_compression.h"
#include <system/audio.h>

namespace le_fx {

AdaptiveDynamicRangeCompression::AdaptiveDynamicRangeCompression() {
  static constexpr float kTargetGain[] = {
      1.0f, 2.0f, 3.0f, 4.0f, 5.0f };
  static constexpr float kKneeThreshold[] = {
      -8.0f, -8.0f, -8.5f, -9.0f, -10.0f };
  target_gain_to_knee_threshold_.Initialize(
      &kTargetGain[0], &kKneeThreshold[0],
      std::size(kTargetGain));
}

bool AdaptiveDynamicRangeCompression::Initialize(
        float target_gain, float sampling_rate) {
  set_target_gain(target_gain);
  sampling_rate_ = sampling_rate;
  state_ = 0.0f;
  compressor_gain_ = 1.0f;
  if (kTauAttack > 0.0f) {
    const float taufs = kTauAttack * sampling_rate_;
    alpha_attack_ = std::exp(-1.0f / taufs);
  } else {
    alpha_attack_ = 0.0f;
  }
  if (kTauRelease > 0.0f) {
    const float taufs = kTauRelease * sampling_rate_;
    alpha_release_ = std::exp(-1.0f / taufs);
  } else {
    alpha_release_ = 0.0f;
  }
  return true;
}

// Instantiate Compress for supported channel counts.
#define INSTANTIATE_COMPRESS(CHANNEL_COUNT) \
case CHANNEL_COUNT: \
    if constexpr (CHANNEL_COUNT <= FCC_LIMIT) { \
        Compress(inputAmp, inverseScale, \
                reinterpret_cast<internal_array_t<float, CHANNEL_COUNT>*>(buffer), frameCount); \
        return; \
    } \
    break;

 void AdaptiveDynamicRangeCompression::Compress(size_t channelCount,
        float inputAmp, float inverseScale, float* buffer, size_t frameCount) {
    using android::audio_utils::intrinsics::internal_array_t;
    switch (channelCount) {
        INSTANTIATE_COMPRESS(1);
        INSTANTIATE_COMPRESS(2);
        INSTANTIATE_COMPRESS(3);
        INSTANTIATE_COMPRESS(4);
        INSTANTIATE_COMPRESS(5);
        INSTANTIATE_COMPRESS(6);
        INSTANTIATE_COMPRESS(7);
        INSTANTIATE_COMPRESS(8);
        INSTANTIATE_COMPRESS(9);
        INSTANTIATE_COMPRESS(10);
        INSTANTIATE_COMPRESS(11);
        INSTANTIATE_COMPRESS(12);
        INSTANTIATE_COMPRESS(13);
        INSTANTIATE_COMPRESS(14);
        INSTANTIATE_COMPRESS(15);
        INSTANTIATE_COMPRESS(16);
        INSTANTIATE_COMPRESS(17);
        INSTANTIATE_COMPRESS(18);
        INSTANTIATE_COMPRESS(19);
        INSTANTIATE_COMPRESS(20);
        INSTANTIATE_COMPRESS(21);
        INSTANTIATE_COMPRESS(22);
        INSTANTIATE_COMPRESS(23);
        INSTANTIATE_COMPRESS(24);
        INSTANTIATE_COMPRESS(25);
        INSTANTIATE_COMPRESS(26);
        INSTANTIATE_COMPRESS(27);
        INSTANTIATE_COMPRESS(28);
    }
    LOG_ALWAYS_FATAL("%s: channelCount: %zu not supported", __func__, channelCount);
}

}  // namespace le_fx

