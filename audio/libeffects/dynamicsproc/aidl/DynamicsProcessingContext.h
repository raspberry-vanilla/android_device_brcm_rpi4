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

#pragma once

#include <audio_effects/effect_dynamicsprocessing.h>

#include "effect-impl/EffectContext.h"

#include <any>
#include <cstddef>
#include <dsp/DPBase.h>
#include <dsp/DPFrequency.h>

namespace aidl::android::hardware::audio::effect {

enum DynamicsProcessingState {
    DYNAMICS_PROCESSING_STATE_UNINITIALIZED,
    DYNAMICS_PROCESSING_STATE_INITIALIZED,
    DYNAMICS_PROCESSING_STATE_ACTIVE,
};

class DynamicsProcessingContext final : public EffectContext {
  public:
    DynamicsProcessingContext(int statusDepth, const Parameter::Common& common);
    ~DynamicsProcessingContext() = default;
    RetCode enable() override;
    RetCode disable() override;
    RetCode reset() override;

    // override EffectContext::setCommon to update mChannelCount
    RetCode setCommon(const Parameter::Common& common) override;
    RetCode setVolumeStereo(const Parameter::VolumeStereo& volumeStereo) override;
    Parameter::VolumeStereo getVolumeStereo() override;

    RetCode setEngineArchitecture(const DynamicsProcessing::EngineArchitecture& engineArchitecture);
    RetCode setPreEq(const std::vector<DynamicsProcessing::ChannelConfig>& eqChannels);
    RetCode setPostEq(const std::vector<DynamicsProcessing::ChannelConfig>& eqChannels);
    RetCode setPreEqBand(const std::vector<DynamicsProcessing::EqBandConfig>& eqBands);
    RetCode setPostEqBand(const std::vector<DynamicsProcessing::EqBandConfig>& eqBands);
    RetCode setMbc(const std::vector<DynamicsProcessing::ChannelConfig>& mbcChannels);
    RetCode setMbcBand(const std::vector<DynamicsProcessing::MbcBandConfig>& eqBands);
    RetCode setLimiter(const std::vector<DynamicsProcessing::LimiterConfig>& limiters);
    RetCode setInputGain(const std::vector<DynamicsProcessing::InputGain>& gain);

    DynamicsProcessing::EngineArchitecture getEngineArchitecture();
    std::vector<DynamicsProcessing::ChannelConfig> getPreEq();
    std::vector<DynamicsProcessing::ChannelConfig> getPostEq();
    std::vector<DynamicsProcessing::EqBandConfig> getPreEqBand();
    std::vector<DynamicsProcessing::EqBandConfig> getPostEqBand();
    std::vector<DynamicsProcessing::ChannelConfig> getMbc();
    std::vector<DynamicsProcessing::MbcBandConfig> getMbcBand();
    std::vector<DynamicsProcessing::LimiterConfig> getLimiter();
    std::vector<DynamicsProcessing::InputGain> getInputGain();

    IEffect::Status dpeProcess(float* in, float* out, int samples);

  private:
    static constexpr float kPreferredProcessingDurationMs = 10.0f;
    static constexpr int kBandCount = 5;
    int mChannelCount = 0;
    DynamicsProcessingState mState = DYNAMICS_PROCESSING_STATE_UNINITIALIZED;
    std::unique_ptr<dp_fx::DPFrequency> mDpFreq = nullptr;
    bool mEngineInited = false;
    DynamicsProcessing::EngineArchitecture mEngineArchitecture = {
            .resolutionPreference =
                    DynamicsProcessing::ResolutionPreference::FAVOR_FREQUENCY_RESOLUTION,
            .preferredProcessingDurationMs = kPreferredProcessingDurationMs,
            .preEqStage = {.inUse = true, .bandCount = kBandCount},
            .postEqStage = {.inUse = true, .bandCount = kBandCount},
            .mbcStage = {.inUse = true, .bandCount = kBandCount},
            .limiterInUse = true,
    };

    enum class StageType { PREEQ, POSTEQ, MBC, LIMITER, INPUTGAIN };

    void init();

    void dpSetFreqDomainVariant_l(const DynamicsProcessing::EngineArchitecture& engine);
    dp_fx::DPChannel* getChannel_l(int ch);
    dp_fx::DPEq* getPreEq_l(int ch);
    dp_fx::DPEq* getPostEq_l(int ch);
    dp_fx::DPMbc* getMbc_l(int ch);
    dp_fx::DPLimiter* getLimiter_l(int ch);
    dp_fx::DPBandStage* getStageWithType_l(StageType type, int ch);
    dp_fx::DPEq* getEqWithType_l(StageType type, int ch);
    template <typename D>
    RetCode setDpChannels_l(const std::vector<DynamicsProcessing::ChannelConfig>& channels,
                            StageType type);
    template <typename T /* BandConfig */>
    RetCode setBands_l(const std::vector<T>& bands, StageType type);
    RetCode setDpChannelBand_l(const std::any& anyConfig, StageType type,
                               std::set<std::pair<int, int>>& chBandSet);

    std::vector<DynamicsProcessing::EqBandConfig> getEqBandConfigs(StageType type);
    std::vector<DynamicsProcessing::ChannelConfig> getChannelConfig(StageType type);

    template <typename T /* BandConfig */>
    bool validateBandConfig(const std::vector<T>& bands, int maxChannel, int maxBand);
    bool validateLimiterConfig(const std::vector<DynamicsProcessing::LimiterConfig>& cfgs,
                               int maxChannel);
    bool validateInputGainConfig(const std::vector<DynamicsProcessing::InputGain>& cfgs,
                                 int maxChannel);

    inline bool validateChannel(int ch, int maxCh) { return ch >= 0 && ch < maxCh; }
    inline bool validateBand(int band, int maxBand) { return band >= 0 && band < maxBand; }
};

}  // namespace aidl::android::hardware::audio::effect