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

#include <audio_effects/effect_loudnessenhancer.h>

#include "dsp/core/dynamic_range_compression.h"
#include "effect-impl/EffectContext.h"

namespace aidl::android::hardware::audio::effect {

enum LoudnessEnhancerState {
    LOUDNESS_ENHANCER_STATE_UNINITIALIZED,
    LOUDNESS_ENHANCER_STATE_INITIALIZED,
    LOUDNESS_ENHANCER_STATE_ACTIVE,
};

class LoudnessEnhancerContext final : public EffectContext {
  public:
    LoudnessEnhancerContext(int statusDepth, const Parameter::Common& common);
    ~LoudnessEnhancerContext() = default;

    RetCode enable() override;
    RetCode disable() override;

    RetCode setLeGain(int gainMb);
    int getLeGain() const { return mGain; }

    IEffect::Status process(float* in, float* out, int samples);

  private:
    LoudnessEnhancerState mState = LOUDNESS_ENHANCER_STATE_UNINITIALIZED;
    int mGain = LOUDNESS_ENHANCER_DEFAULT_TARGET_GAIN_MB;
    // In this implementation, there is no coupling between the compression on the left and right
    // channels
    std::unique_ptr<le_fx::AdaptiveDynamicRangeCompression> mCompressor;

    void init_params();
};
}  // namespace aidl::android::hardware::audio::effect
