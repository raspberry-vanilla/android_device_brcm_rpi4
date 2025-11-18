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
#ifndef LE_FX_ENGINE_DSP_CORE_DYNAMIC_RANGE_COMPRESSION_H_

#include "dsp/core/interpolation.h"
#include <audio_utils/intrinsic_utils.h>

namespace le_fx {
namespace math {
// taken from common/core/math.h
// A fast approximation to log2(.)
inline float fast_log2(float val) {
    int* const exp_ptr = reinterpret_cast <int *> (&val);
    int x = *exp_ptr;
    const int log_2 = ((x >> 23) & 255) - 128;
    x &= ~(255 << 23);
    x += 127 << 23;
    *exp_ptr = x;
    val = ((-1.0f / 3) * val + 2) * val - 2.0f / 3;
    return static_cast<float>(val + log_2);
}

// A fast approximation to log(.)
inline float fast_log(float val) {
    return fast_log2(val) *
           0.693147180559945286226763982995180413126945495605468750f;
}

// An approximation of the exp(.) function using a 5-th order Taylor expansion.
// It's pretty accurate between +-0.1 and accurate to 10e-3 between +-1
//
// TODO(b/315070856)
// ExpApproximationViaTaylorExpansionOrder5() is only marginally faster using expf() itself
// Keeping as the 5th order expansion may be better for vector parallelization, if so desired.
//
// BM_math_ExpApproximationViaTaylorExpansionOrder5 2.11 ns 2.10 ns 332924668
// BM_math_expf_speccpu2017 2.91 ns 2.90 ns 241714501
// BM_math_exp_speccpu2017 4.06 ns 4.03 ns 173500928

template <typename T>
T ExpApproximationViaTaylorExpansionOrder5(T x) {
    const T x2 = x * x;
    const T x3 = x2 * x;
    const T x4 = x2 * x2;
    const T x5 = x3 * x2;
    // [sic] this is 1/6, why do we have such precision annotated with 'f' for float.
    return 1.0f + x + 0.5f * x2 +
           0.16666666666666665741480812812369549646973609924316406250f * x3 +
           0.0416666666666666643537020320309238741174340248107910156250f * x4 +
           0.008333333333333333217685101601546193705871701240539550781250f * x5;
}

}  // namespace math
}  // namespace le_fx

namespace le_fx {

// An adaptive dynamic range compression algorithm. The gain adaptation is made
// at the logarithmic domain and it is based on a Branching-Smooth compensated
// digital peak detector with different time constants for attack and release.
class AdaptiveDynamicRangeCompression {
 public:
    AdaptiveDynamicRangeCompression();
    AdaptiveDynamicRangeCompression(const AdaptiveDynamicRangeCompression&) = delete;
    AdaptiveDynamicRangeCompression& operator=(const AdaptiveDynamicRangeCompression&) = delete;

    // Initializes the compressor using prior information. It assumes that the
    // input signal is speech from high-quality recordings that is scaled and then
    // fed to the compressor. The compressor is tuned according to the target gain
    // that is expected to be applied.
    //
    // Target gain receives values between 0.0 and 10.0. The knee threshold is
    // reduced as the target gain increases in order to fit the increased range of
    // values.
    //
    // Values between 1.0 and 2.0 will only mildly affect your signal. Higher
    // values will reduce the dynamic range of the signal to the benefit of
    // increased loudness.
    //
    // If nothing is known regarding the input, a `target_gain` of 1.0f is a
    // relatively safe choice for many signals.
    bool Initialize(float target_gain, float sampling_rate);

  // in-place compression.
  void Compress(size_t channelCount,
          float inputAmp, float inverseScale, float* buffer, size_t frameCount);

  // Sets knee threshold (in decibel).
  void set_knee_threshold(float decibel) {
      // Converts to 1og-base
      knee_threshold_in_decibel_ = decibel;
      knee_threshold_ = 0.1151292546497023061569109358970308676362037658691406250f *
                        decibel + 10.39717719035538401328722102334722876548767089843750f;
  }

  // Sets knee threshold via the target gain using an experimentally derived
  // relationship.
  void set_target_gain(float target_gain) {
        const float decibel = target_gain_to_knee_threshold_.Interpolate(
                target_gain);
        // ALOGV("set_knee_threshold_via_target_gain: decibel =%.3fdB", decibel);
        set_knee_threshold(decibel);
  }

 private:

  // Templated Compress routine.
  template <typename V>
  void Compress(float inputAmp, float inverseScale, V* buffer, size_t frameCount) {
    for (size_t i = 0; i < frameCount; ++i) {
        auto v = android::audio_utils::intrinsics::vmul(buffer[i], inputAmp);
        const float max_abs_x = android::audio_utils::intrinsics::vmaxv(
                android::audio_utils::intrinsics::vabs(v));
        const float max_abs_x_dB = math::fast_log(std::max(max_abs_x, kMinLogAbsValue));
        // Subtract Threshold from log-encoded input to get the amount of overshoot
        const float overshoot = max_abs_x_dB - knee_threshold_;
        // Hard half-wave rectifier
        const float rect = std::max(overshoot, 0.0f);
        // Multiply rectified overshoot with slope
        const float cv = rect * kSlope;
        const float prev_state = state_;
        const float alpha = (cv <= state_) ? alpha_attack_ : alpha_release_;
        state_ = alpha * state_ + (1.0f - alpha) * cv;
        compressor_gain_ *= expf(state_ - prev_state);
        const auto x = android::audio_utils::intrinsics::vmul(v, compressor_gain_);
        v = android::audio_utils::intrinsics::vclamp(x, -kFixedPointLimit, kFixedPointLimit);
        buffer[i] = android::audio_utils::intrinsics::vmul(inverseScale, v);
    }
  }

  // The minimum accepted absolute input value to prevent numerical issues
  // when the input is close to zero.
  static constexpr float kMinLogAbsValue =
      0.032766999999999997517097227728299912996590137481689453125f;
  // Fixed-point arithmetic limits
  static constexpr float kFixedPointLimit = 32767.0f;
  static constexpr float kInverseFixedPointLimit = 1.0f / kFixedPointLimit;
  // The default knee threshold in decibel. The knee threshold defines when the
  // compressor is actually starting to compress the value of the input samples
  static constexpr float kDefaultKneeThresholdInDecibel = -8.0f;
  // The compression ratio is the reciprocal of the slope of the line segment
  // above the threshold (in the log-domain). The ratio controls the
  // effectiveness of the compression.
  static constexpr float kCompressionRatio = 7.0f;
  // The attack time of the envelope detector
  static constexpr float kTauAttack = 0.001f;
  // The release time of the envelope detector
  static constexpr float kTauRelease = 0.015f;

  static constexpr float kSlope = 1.0f / kCompressionRatio - 1.0f;
  float sampling_rate_;
  // the internal state of the envelope detector
  float state_;
  // the latest gain factor that was applied to the input signal
  float compressor_gain_;
  // attack constant for exponential dumping
  float alpha_attack_;
  // release constant for exponential dumping
  float alpha_release_;
  float knee_threshold_;
  float knee_threshold_in_decibel_;
  // This interpolator provides the function that relates target gain to knee
  // threshold.
  sigmod::InterpolatorLinear<float> target_gain_to_knee_threshold_;
};

}  // namespace le_fx

#endif  // LE_FX_ENGINE_DSP_CORE_DYNAMIC_RANGE_COMPRESSION_H_
