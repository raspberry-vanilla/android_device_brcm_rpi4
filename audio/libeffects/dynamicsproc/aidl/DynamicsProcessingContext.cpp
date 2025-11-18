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

#define LOG_TAG "AHAL_DPLibEffectsContext"

#include "DynamicsProcessingContext.h"
#include "DynamicsProcessing.h"

#include <audio_utils/power.h>
#include <sys/param.h>
#include <functional>
#include <unordered_set>

namespace aidl::android::hardware::audio::effect {

DynamicsProcessingContext::DynamicsProcessingContext(int statusDepth,
                                                     const Parameter::Common& common)
    : EffectContext(statusDepth, common) {
    init();
}

RetCode DynamicsProcessingContext::enable() {
    if (mState != DYNAMICS_PROCESSING_STATE_INITIALIZED) {
        return RetCode::ERROR_EFFECT_LIB_ERROR;
    }
    mState = DYNAMICS_PROCESSING_STATE_ACTIVE;
    return RetCode::SUCCESS;
}

RetCode DynamicsProcessingContext::disable() {
    if (mState != DYNAMICS_PROCESSING_STATE_ACTIVE) {
        return RetCode::ERROR_EFFECT_LIB_ERROR;
    }
    mState = DYNAMICS_PROCESSING_STATE_INITIALIZED;
    return RetCode::SUCCESS;
}

RetCode DynamicsProcessingContext::reset() {
    if (mDpFreq != nullptr) {
        mDpFreq->reset();
    }
    mEngineInited = false;
    return RetCode::SUCCESS;
}

RetCode DynamicsProcessingContext::setCommon(const Parameter::Common& common) {
    if(auto ret = updateIOFrameSize(common); ret != RetCode::SUCCESS) {
        return ret;
    }
    mCommon = common;
    init();
    return RetCode::SUCCESS;
}

RetCode DynamicsProcessingContext::setVolumeStereo(const Parameter::VolumeStereo& volumeStereo) {
    dp_fx::DPChannel* leftChannel = mDpFreq->getChannel(0);
    dp_fx::DPChannel* rightChannel = mDpFreq->getChannel(1);
    if (leftChannel != nullptr) {
        leftChannel->setOutputGain(audio_utils_power_from_amplitude(volumeStereo.left));
    }
    if (rightChannel != nullptr) {
        rightChannel->setOutputGain(audio_utils_power_from_amplitude(volumeStereo.right));
    }
    return RetCode::SUCCESS;
}

Parameter::VolumeStereo DynamicsProcessingContext::getVolumeStereo() {
    return {1.0f, 1.0f};
}

void DynamicsProcessingContext::dpSetFreqDomainVariant_l(
        const DynamicsProcessing::EngineArchitecture& engine) {
    mDpFreq.reset(new dp_fx::DPFrequency());
    mDpFreq->init(mChannelCount, engine.preEqStage.inUse, engine.preEqStage.bandCount,
                  engine.mbcStage.inUse, engine.mbcStage.bandCount, engine.postEqStage.inUse,
                  engine.postEqStage.bandCount, engine.limiterInUse);

    int32_t sampleRate = mCommon.input.base.sampleRate;
    int32_t minBlockSize = (int32_t)dp_fx::DPFrequency::getMinBockSize();
    int32_t block = engine.preferredProcessingDurationMs * sampleRate / 1000.0f;
    LOG(VERBOSE) << __func__ << " sampleRate " << sampleRate << " block length "
                 << engine.preferredProcessingDurationMs << " ms (" << block << "samples)";
    if (block < minBlockSize) {
        block = minBlockSize;
    } else if (!powerof2(block)) {
        // find next highest power of 2.
        block = 1 << (32 - __builtin_clz(block));
    }
    mDpFreq->configure(block, block >> 1, sampleRate);
}

RetCode DynamicsProcessingContext::setEngineArchitecture(
        const DynamicsProcessing::EngineArchitecture& engineArchitecture) {
    if (!mEngineInited || mEngineArchitecture != engineArchitecture) {
        if (engineArchitecture.resolutionPreference ==
            DynamicsProcessing::ResolutionPreference::FAVOR_FREQUENCY_RESOLUTION) {
            dpSetFreqDomainVariant_l(engineArchitecture);
        } else {
            LOG(WARNING) << __func__ << toString(engineArchitecture.resolutionPreference)
                         << " not available now";
        }
        mEngineInited = true;
        mEngineArchitecture = engineArchitecture;
    }
    return RetCode::SUCCESS;
}

RetCode DynamicsProcessingContext::setPreEq(
        const std::vector<DynamicsProcessing::ChannelConfig>& channels) {
    return setDpChannels_l<dp_fx::DPEq>(channels, StageType::PREEQ);
}

RetCode DynamicsProcessingContext::setPostEq(
        const std::vector<DynamicsProcessing::ChannelConfig>& channels) {
    return setDpChannels_l<dp_fx::DPEq>(channels, StageType::POSTEQ);
}

RetCode DynamicsProcessingContext::setMbc(
        const std::vector<DynamicsProcessing::ChannelConfig>& channels) {
    return setDpChannels_l<dp_fx::DPMbc>(channels, StageType::MBC);
}

RetCode DynamicsProcessingContext::setPreEqBand(
        const std::vector<DynamicsProcessing::EqBandConfig>& bands) {
    RETURN_VALUE_IF(
            !validateBandConfig(bands, mChannelCount, mEngineArchitecture.preEqStage.bandCount),
            RetCode::ERROR_ILLEGAL_PARAMETER, "eqBandNotValid");
    return setBands_l<DynamicsProcessing::EqBandConfig>(bands, StageType::PREEQ);
}

RetCode DynamicsProcessingContext::setPostEqBand(
        const std::vector<DynamicsProcessing::EqBandConfig>& bands) {
    RETURN_VALUE_IF(
            !validateBandConfig(bands, mChannelCount, mEngineArchitecture.postEqStage.bandCount),
            RetCode::ERROR_ILLEGAL_PARAMETER, "eqBandNotValid");
    return setBands_l<DynamicsProcessing::EqBandConfig>(bands, StageType::POSTEQ);
}

RetCode DynamicsProcessingContext::setMbcBand(
        const std::vector<DynamicsProcessing::MbcBandConfig>& bands) {
    RETURN_VALUE_IF(
            !validateBandConfig(bands, mChannelCount, mEngineArchitecture.mbcStage.bandCount),
            RetCode::ERROR_ILLEGAL_PARAMETER, "eqBandNotValid");
    return setBands_l<DynamicsProcessing::MbcBandConfig>(bands, StageType::MBC);
}

RetCode DynamicsProcessingContext::setLimiter(
        const std::vector<DynamicsProcessing::LimiterConfig>& limiters) {
    RETURN_VALUE_IF(!validateLimiterConfig(limiters, mChannelCount),
                    RetCode::ERROR_ILLEGAL_PARAMETER, "limiterConfigNotValid");
    return setBands_l<DynamicsProcessing::LimiterConfig>(limiters, StageType::LIMITER);
}

RetCode DynamicsProcessingContext::setInputGain(
        const std::vector<DynamicsProcessing::InputGain>& inputGains) {
    RETURN_VALUE_IF(!validateInputGainConfig(inputGains, mChannelCount),
                    RetCode::ERROR_ILLEGAL_PARAMETER, "inputGainNotValid");
    return setBands_l<DynamicsProcessing::InputGain>(inputGains, StageType::INPUTGAIN);
}

DynamicsProcessing::EngineArchitecture DynamicsProcessingContext::getEngineArchitecture() {
    return mEngineArchitecture;
}

std::vector<DynamicsProcessing::ChannelConfig> DynamicsProcessingContext::getPreEq() {
    return getChannelConfig(StageType::PREEQ);
}

std::vector<DynamicsProcessing::ChannelConfig> DynamicsProcessingContext::getPostEq() {
    return getChannelConfig(StageType::POSTEQ);
}

std::vector<DynamicsProcessing::EqBandConfig> DynamicsProcessingContext::getPreEqBand() {
    return getEqBandConfigs(StageType::PREEQ);
}

std::vector<DynamicsProcessing::EqBandConfig> DynamicsProcessingContext::getPostEqBand() {
    return getEqBandConfigs(StageType::POSTEQ);
}

std::vector<DynamicsProcessing::ChannelConfig> DynamicsProcessingContext::getMbc() {
    return getChannelConfig(StageType::MBC);
}

std::vector<DynamicsProcessing::MbcBandConfig> DynamicsProcessingContext::getMbcBand() {
    std::vector<DynamicsProcessing::MbcBandConfig> bands;
    auto maxBand = mEngineArchitecture.mbcStage.bandCount;
    for (int32_t ch = 0; ch < mChannelCount; ch++) {
        auto mbc = getMbc_l(ch);
        if (!mbc) {
            continue;
        }
        for (int32_t bandId = 0; bandId < maxBand; bandId++) {
            auto band = mbc->getBand(bandId);
            if (!band) {
                continue;
            }
            bands.push_back({.channel = ch,
                             .band = bandId,
                             .enable = band->isEnabled(),
                             .cutoffFrequencyHz = band->getCutoffFrequency(),
                             .attackTimeMs = band->getAttackTime(),
                             .releaseTimeMs = band->getReleaseTime(),
                             .ratio = band->getRatio(),
                             .thresholdDb = band->getThreshold(),
                             .kneeWidthDb = band->getKneeWidth(),
                             .noiseGateThresholdDb = band->getNoiseGateThreshold(),
                             .expanderRatio = band->getExpanderRatio(),
                             .preGainDb = band->getPreGain(),
                             .postGainDb = band->getPostGain()});
        }
    }
    return bands;
}

std::vector<DynamicsProcessing::LimiterConfig> DynamicsProcessingContext::getLimiter() {
    std::vector<DynamicsProcessing::LimiterConfig> ret;
    for (int32_t ch = 0; ch < mChannelCount; ch++) {
        auto limiter = getLimiter_l(ch);
        if (!limiter) {
            continue;
        }
        ret.push_back({.channel = ch,
                       .enable = limiter->isEnabled(),
                       .linkGroup = static_cast<int32_t>(limiter->getLinkGroup()),
                       .attackTimeMs = limiter->getAttackTime(),
                       .releaseTimeMs = limiter->getReleaseTime(),
                       .ratio = limiter->getRatio(),
                       .thresholdDb = limiter->getThreshold(),
                       .postGainDb = limiter->getPostGain()});
    }
    return ret;
}

std::vector<DynamicsProcessing::InputGain> DynamicsProcessingContext::getInputGain() {
    std::vector<DynamicsProcessing::InputGain> ret;
    for (int32_t ch = 0; ch < mChannelCount; ch++) {
        auto channel = getChannel_l(ch);
        if (!channel) {
            continue;
        }
        ret.push_back({.channel = ch, .gainDb = channel->getInputGain()});
    }
    return ret;
}

IEffect::Status DynamicsProcessingContext::dpeProcess(float* in, float* out, int samples) {

    IEffect::Status status = {EX_NULL_POINTER, 0, 0};
    RETURN_VALUE_IF(!in, status, "nullInput");
    RETURN_VALUE_IF(!out, status, "nullOutput");
    status = {EX_ILLEGAL_STATE, 0, 0};

    RETURN_VALUE_IF(mState != DynamicsProcessingState::DYNAMICS_PROCESSING_STATE_ACTIVE, status,
                    "notInActiveState");
    RETURN_VALUE_IF(!mDpFreq, status, "engineNotInited");
    mDpFreq->processSamples(in, out, samples);
    return {STATUS_OK, samples, samples};
}

void DynamicsProcessingContext::init() {
    if (mState == DYNAMICS_PROCESSING_STATE_UNINITIALIZED) {
        mState = DYNAMICS_PROCESSING_STATE_INITIALIZED;
    }
    mChannelCount = static_cast<int>(::aidl::android::hardware::audio::common::getChannelCount(
            mCommon.input.base.channelMask));
}

dp_fx::DPChannel* DynamicsProcessingContext::getChannel_l(int channel) {
    RETURN_VALUE_IF(mDpFreq == nullptr, nullptr, "DPFreqNotInited");

    return mDpFreq->getChannel(channel);
}

dp_fx::DPEq* DynamicsProcessingContext::getPreEq_l(int ch) {
    auto channel = getChannel_l(ch);
    RETURN_VALUE_IF(channel == nullptr, nullptr, "ChannelNotExist");

    return channel->getPreEq();
}

dp_fx::DPEq* DynamicsProcessingContext::getPostEq_l(int ch) {
    auto channel = getChannel_l(ch);
    RETURN_VALUE_IF(channel == nullptr, nullptr, "ChannelNotExist");

    return channel->getPostEq();
}

dp_fx::DPMbc* DynamicsProcessingContext::getMbc_l(int ch) {
    auto channel = getChannel_l(ch);
    RETURN_VALUE_IF(channel == nullptr, nullptr, "ChannelNotExist");

    return channel->getMbc();
}

dp_fx::DPLimiter* DynamicsProcessingContext::getLimiter_l(int ch) {
    auto channel = getChannel_l(ch);
    RETURN_VALUE_IF(channel == nullptr, nullptr, "ChannelNotExist");

    return channel->getLimiter();
}

dp_fx::DPBandStage* DynamicsProcessingContext::getStageWithType_l(
        DynamicsProcessingContext::StageType type, int ch) {
    switch (type) {
        case StageType::PREEQ: {
            return getEqWithType_l(type, ch);
        }
        case StageType::POSTEQ: {
            return getEqWithType_l(type, ch);
        }
        case StageType::MBC: {
            return getMbc_l(ch);
        }
        case StageType::LIMITER:
            FALLTHROUGH_INTENDED;
        case StageType::INPUTGAIN: {
            return nullptr;
        }
    }
}

dp_fx::DPEq* DynamicsProcessingContext::getEqWithType_l(DynamicsProcessingContext::StageType type,
                                                        int ch) {
    switch (type) {
        case StageType::PREEQ: {
            return getPreEq_l(ch);
        }
        case StageType::POSTEQ: {
            return getPostEq_l(ch);
        }
        case StageType::MBC:
            FALLTHROUGH_INTENDED;
        case StageType::LIMITER:
            FALLTHROUGH_INTENDED;
        case StageType::INPUTGAIN: {
            return nullptr;
        }
    }
}

std::vector<DynamicsProcessing::ChannelConfig> DynamicsProcessingContext::getChannelConfig(
        StageType type) {
    std::vector<DynamicsProcessing::ChannelConfig> ret;

    for (int32_t ch = 0; ch < mChannelCount; ch++) {
        auto stage = getStageWithType_l(type, ch);
        if (!stage) {
            continue;
        }
        ret.push_back({.channel = ch, .enable = stage->isEnabled()});
    }
    return ret;
}

std::vector<DynamicsProcessing::EqBandConfig> DynamicsProcessingContext::getEqBandConfigs(
        StageType type) {
    std::vector<DynamicsProcessing::EqBandConfig> eqBands;

    auto maxBand = mEngineArchitecture.preEqStage.bandCount;
    for (int32_t ch = 0; ch < mChannelCount; ch++) {
        auto eq = getEqWithType_l(type, ch);
        if (!eq) {
            continue;
        }
        for (int32_t bandId = 0; bandId < maxBand; bandId++) {
            auto band = eq->getBand(bandId);
            if (!band) {
                continue;
            }
            eqBands.push_back({.channel = ch,
                               .band = bandId,
                               .enable = band->isEnabled(),
                               .cutoffFrequencyHz = band->getCutoffFrequency(),
                               .gainDb = band->getGain()});
        }
    }
    return eqBands;
}

template <typename T>
bool DynamicsProcessingContext::validateBandConfig(const std::vector<T>& bands, int maxChannel,
                                                   int maxBand) {
    std::map<int, float> freqs;
    for (auto band : bands) {
        if (!validateChannel(band.channel, maxChannel)) {
            LOG(ERROR) << __func__ << " " << band.toString() << " invalid, maxCh " << maxChannel;
            return false;
        }
        if (!validateBand(band.band, maxBand)) {
            LOG(ERROR) << __func__ << " " << band.toString() << " invalid, maxBand " << maxBand;
            return false;
        }
        if (freqs.find(band.band) != freqs.end()) {
            LOG(ERROR) << __func__ << " " << band.toString() << " found duplicate";
            return false;
        }
        freqs[band.band] = band.cutoffFrequencyHz;
    }
    return true;
}

bool DynamicsProcessingContext::validateLimiterConfig(
        const std::vector<DynamicsProcessing::LimiterConfig>& cfgs, int maxChannel) {
    for (auto cfg : cfgs) {
        if (!validateChannel(cfg.channel, maxChannel)) return false;
    }
    return true;
}

bool DynamicsProcessingContext::validateInputGainConfig(
        const std::vector<DynamicsProcessing::InputGain>& cfgs, int maxChannel) {
    for (auto cfg : cfgs) {
        if (!validateChannel(cfg.channel, maxChannel)) return false;
    }
    return true;
}

template <typename D>
RetCode DynamicsProcessingContext::setDpChannels_l(
        const std::vector<DynamicsProcessing::ChannelConfig>& channels, StageType type) {
    RetCode ret = RetCode::SUCCESS;
    std::unordered_set<int> channelSet;

    for (auto& it : channels) {
        if (0 != channelSet.count(it.channel)) {
            LOG(WARNING) << __func__ << " duplicated channel " << it.channel;
            ret = RetCode::ERROR_ILLEGAL_PARAMETER;
        } else {
            channelSet.insert(it.channel);
        }
        if (it.channel < 0 || it.channel >= mChannelCount) {
            LOG(WARNING) << __func__ << " skip illegal ChannelConfig " << it.toString() << " max "
                         << mChannelCount;
            ret = RetCode::ERROR_ILLEGAL_PARAMETER;
            continue;
        }
        auto dp = getStageWithType_l(type, it.channel);
        if (!dp) {
            LOG(WARNING) << __func__ << " channel " << it.channel << " not exist";
            ret = RetCode::ERROR_ILLEGAL_PARAMETER;
            continue;
        }
        if (dp->isEnabled() != it.enable) {
            dp->setEnabled(it.enable);
        }
    }
    return ret;
}

RetCode DynamicsProcessingContext::setDpChannelBand_l(const std::any& anyConfig, StageType type,
                                                      std::set<std::pair<int, int>>& chBandSet) {
    RETURN_VALUE_IF(!anyConfig.has_value(), RetCode::ERROR_ILLEGAL_PARAMETER, "bandInvalid");
    RetCode ret = RetCode::SUCCESS;
    std::pair<int, int> chBandKey;
    switch (type) {
        case StageType::PREEQ:
            FALLTHROUGH_INTENDED;
        case StageType::POSTEQ: {
            dp_fx::DPEq* dp;
            const auto& config = std::any_cast<DynamicsProcessing::EqBandConfig>(anyConfig);
            RETURN_VALUE_IF(
                    nullptr == (dp = getEqWithType_l(type, config.channel)) || !dp->isEnabled(),
                    RetCode::ERROR_ILLEGAL_PARAMETER, "dpEqNotExist");
            dp_fx::DPEqBand band;
            band.init(config.enable, config.cutoffFrequencyHz, config.gainDb);
            dp->setBand(config.band, band);
            chBandKey = {config.channel, config.band};
            break;
        }
        case StageType::MBC: {
            dp_fx::DPMbc* dp;
            const auto& config = std::any_cast<DynamicsProcessing::MbcBandConfig>(anyConfig);
            RETURN_VALUE_IF(nullptr == (dp = getMbc_l(config.channel)) || !dp->isEnabled(),
                            RetCode::ERROR_ILLEGAL_PARAMETER, "dpMbcNotExist");
            dp_fx::DPMbcBand band;
            band.init(config.enable, config.cutoffFrequencyHz, config.attackTimeMs,
                      config.releaseTimeMs, config.ratio, config.thresholdDb, config.kneeWidthDb,
                      config.noiseGateThresholdDb, config.expanderRatio, config.preGainDb,
                      config.postGainDb);
            dp->setBand(config.band, band);
            chBandKey = {config.channel, config.band};
            break;
        }
        case StageType::LIMITER: {
            dp_fx::DPChannel* dp;
            const auto& config = std::any_cast<DynamicsProcessing::LimiterConfig>(anyConfig);
            RETURN_VALUE_IF(nullptr == (dp = getChannel_l(config.channel)),
                            RetCode::ERROR_ILLEGAL_PARAMETER, "dpChNotExist");
            dp_fx::DPLimiter limiter;
            limiter.init(mEngineArchitecture.limiterInUse, config.enable, config.linkGroup,
                         config.attackTimeMs, config.releaseTimeMs, config.ratio,
                         config.thresholdDb, config.postGainDb);
            dp->setLimiter(limiter);
            chBandKey = {config.channel, 0};
            break;
        }
        case StageType::INPUTGAIN: {
            dp_fx::DPChannel* dp;
            const auto& config = std::any_cast<DynamicsProcessing::InputGain>(anyConfig);
            RETURN_VALUE_IF(nullptr == (dp = getChannel_l(config.channel)),
                            RetCode::ERROR_ILLEGAL_PARAMETER, "dpChNotExist");
            dp->setInputGain(config.gainDb);
            chBandKey = {config.channel, 0};
            break;
        }
    }
    RETURN_VALUE_IF(0 != chBandSet.count(chBandKey), RetCode::ERROR_ILLEGAL_PARAMETER,
                    "duplicatedBand");
    chBandSet.insert(chBandKey);
    return ret;
}

template <typename T /* BandConfig */>
RetCode DynamicsProcessingContext::setBands_l(const std::vector<T>& bands, StageType type) {
    RetCode ret = RetCode::SUCCESS;
    std::set<std::pair<int /* channel */, int /* band */>> bandSet;

    for (const auto& it : bands) {
        if (RetCode::SUCCESS != setDpChannelBand_l(std::make_any<T>(it), type, bandSet)) {
            LOG(WARNING) << __func__ << " skipping band " << it.toString();
            ret = RetCode::ERROR_ILLEGAL_PARAMETER;
            continue;
        }
    }
    return ret;
}

}  // namespace aidl::android::hardware::audio::effect
