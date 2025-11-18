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

#include <algorithm>
#include <limits.h>
#include <unordered_set>

#define LOG_TAG "EffectReverb"

#include <audio_effects/effect_bassboost.h>
#include <audio_effects/effect_equalizer.h>
#include <audio_effects/effect_virtualizer.h>
#include <android-base/logging.h>
#include <fmq/AidlMessageQueue.h>
#include <Utils.h>

#include "EffectReverb.h"
#include "ReverbTypes.h"

using aidl::android::hardware::audio::effect::Descriptor;
using aidl::android::hardware::audio::effect::EffectReverb;
using aidl::android::hardware::audio::effect::getEffectImplUuidAuxEnvReverb;
using aidl::android::hardware::audio::effect::getEffectImplUuidAuxPresetReverb;
using aidl::android::hardware::audio::effect::getEffectImplUuidInsertEnvReverb;
using aidl::android::hardware::audio::effect::getEffectImplUuidInsertPresetReverb;
using aidl::android::hardware::audio::effect::IEffect;
using aidl::android::hardware::audio::effect::State;
using aidl::android::media::audio::common::AudioUuid;

bool isReverbUuidSupported(const AudioUuid* uuid) {
    return (*uuid == getEffectImplUuidAuxEnvReverb() ||
            *uuid == getEffectImplUuidAuxPresetReverb() ||
            *uuid == getEffectImplUuidInsertEnvReverb() ||
            *uuid == getEffectImplUuidInsertPresetReverb());
}

extern "C" binder_exception_t createEffect(const AudioUuid* uuid,
                                           std::shared_ptr<IEffect>* instanceSpp) {
    if (uuid == nullptr || !isReverbUuidSupported(uuid)) {
        LOG(ERROR) << __func__ << "uuid not supported";
        return EX_ILLEGAL_ARGUMENT;
    }
    if (instanceSpp) {
        *instanceSpp = ndk::SharedRefBase::make<EffectReverb>(*uuid);
        return EX_NONE;
    } else {
        LOG(ERROR) << __func__ << " invalid input parameter!";
        return EX_ILLEGAL_ARGUMENT;
    }
}

extern "C" binder_exception_t queryEffect(const AudioUuid* in_impl_uuid, Descriptor* _aidl_return) {
    if (*in_impl_uuid == getEffectImplUuidAuxEnvReverb()) {
        *_aidl_return = aidl::android::hardware::audio::effect::lvm::kAuxEnvReverbDesc;
    } else if (*in_impl_uuid == getEffectImplUuidInsertEnvReverb()) {
        *_aidl_return = aidl::android::hardware::audio::effect::lvm::kInsertEnvReverbDesc;
    } else if (*in_impl_uuid == getEffectImplUuidAuxPresetReverb()) {
        *_aidl_return = aidl::android::hardware::audio::effect::lvm::kAuxPresetReverbDesc;
    } else if (*in_impl_uuid == getEffectImplUuidInsertPresetReverb()) {
        *_aidl_return = aidl::android::hardware::audio::effect::lvm::kInsertPresetReverbDesc;
    } else {
        LOG(ERROR) << __func__ << "uuid not supported";
        return EX_ILLEGAL_ARGUMENT;
    }
    return EX_NONE;
}

namespace aidl::android::hardware::audio::effect {

EffectReverb::EffectReverb(const AudioUuid& uuid) {
    if (uuid == getEffectImplUuidAuxEnvReverb()) {
        mType = lvm::ReverbEffectType::AUX_ENV;
        mDescriptor = &lvm::kAuxEnvReverbDesc;
        mEffectName = &lvm::kAuxEnvReverbEffectName;
    } else if (uuid == getEffectImplUuidInsertEnvReverb()) {
        mType = lvm::ReverbEffectType::INSERT_ENV;
        mDescriptor = &lvm::kInsertEnvReverbDesc;
        mEffectName = &lvm::kInsertEnvReverbEffectName;
    } else if (uuid == getEffectImplUuidAuxPresetReverb()) {
        mType = lvm::ReverbEffectType::AUX_PRESET;
        mDescriptor = &lvm::kAuxPresetReverbDesc;
        mEffectName = &lvm::kAuxPresetReverbEffectName;
    } else if (uuid == getEffectImplUuidInsertPresetReverb()) {
        mType = lvm::ReverbEffectType::INSERT_PRESET;
        mDescriptor = &lvm::kInsertPresetReverbDesc;
        mEffectName = &lvm::kInsertPresetReverbEffectName;
    } else {
        LOG(ERROR) << __func__ << uuid.toString() << " not supported!";
    }
}

EffectReverb::~EffectReverb() {
    cleanUp();
}

ndk::ScopedAStatus EffectReverb::getDescriptor(Descriptor* _aidl_return) {
    RETURN_IF(!_aidl_return, EX_ILLEGAL_ARGUMENT, "Parameter:nullptr");
    *_aidl_return = *mDescriptor;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus EffectReverb::setParameterSpecific(const Parameter::Specific& specific) {
    LOG(VERBOSE) << __func__ << " specific " << specific.toString();
    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");

    auto tag = specific.getTag();
    switch (tag) {
        case Parameter::Specific::presetReverb:
            return setParameterPresetReverb(specific);
        case Parameter::Specific::environmentalReverb:
            return setParameterEnvironmentalReverb(specific);
        default:
            LOG(ERROR) << __func__ << " unsupported tag " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "specificParamNotSupported");
    }
}

ndk::ScopedAStatus EffectReverb::setParameterPresetReverb(const Parameter::Specific& specific) {
    auto& prParam = specific.get<Parameter::Specific::presetReverb>();
    RETURN_IF(!inRange(prParam, lvm::kPresetReverbRanges), EX_ILLEGAL_ARGUMENT, "outOfRange");
    auto tag = prParam.getTag();

    switch (tag) {
        case PresetReverb::preset: {
            RETURN_IF(mContext->setPresetReverbPreset(prParam.get<PresetReverb::preset>()) !=
                              RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setPresetFailed");
            return ndk::ScopedAStatus::ok();
        }
        default: {
            LOG(ERROR) << __func__ << " unsupported tag: " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "PresetReverbTagNotSupported");
        }
    }
}

ndk::ScopedAStatus EffectReverb::setParameterEnvironmentalReverb(
        const Parameter::Specific& specific) {
    auto& erParam = specific.get<Parameter::Specific::environmentalReverb>();
    RETURN_IF(!inRange(erParam, lvm::kEnvReverbRanges), EX_ILLEGAL_ARGUMENT, "outOfRange");
    auto tag = erParam.getTag();

    switch (tag) {
        case EnvironmentalReverb::roomLevelMb: {
            RETURN_IF(mContext->setEnvironmentalReverbRoomLevel(
                              erParam.get<EnvironmentalReverb::roomLevelMb>()) != RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setRoomLevelFailed");
            return ndk::ScopedAStatus::ok();
        }
        case EnvironmentalReverb::roomHfLevelMb: {
            RETURN_IF(
                    mContext->setEnvironmentalReverbRoomHfLevel(
                            erParam.get<EnvironmentalReverb::roomHfLevelMb>()) != RetCode::SUCCESS,
                    EX_ILLEGAL_ARGUMENT, "setRoomHfLevelFailed");
            return ndk::ScopedAStatus::ok();
        }
        case EnvironmentalReverb::decayTimeMs: {
            RETURN_IF(mContext->setEnvironmentalReverbDecayTime(
                              erParam.get<EnvironmentalReverb::decayTimeMs>()) != RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setDecayTimeFailed");
            return ndk::ScopedAStatus::ok();
        }
        case EnvironmentalReverb::decayHfRatioPm: {
            RETURN_IF(
                    mContext->setEnvironmentalReverbDecayHfRatio(
                            erParam.get<EnvironmentalReverb::decayHfRatioPm>()) != RetCode::SUCCESS,
                    EX_ILLEGAL_ARGUMENT, "setDecayHfRatioFailed");
            return ndk::ScopedAStatus::ok();
        }
        case EnvironmentalReverb::reflectionsLevelMb: {
            RETURN_IF(mContext->setReflectionsLevel(
                              erParam.get<EnvironmentalReverb::reflectionsLevelMb>()) !=
                              RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setReflectionsLevelFailed");
            return ndk::ScopedAStatus::ok();
        }
        case EnvironmentalReverb::reflectionsDelayMs: {
            RETURN_IF(mContext->setReflectionsDelay(
                              erParam.get<EnvironmentalReverb::reflectionsDelayMs>()) !=
                              RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setReflectionsDelayFailed");
            return ndk::ScopedAStatus::ok();
        }
        case EnvironmentalReverb::levelMb: {
            RETURN_IF(mContext->setEnvironmentalReverbLevel(
                              erParam.get<EnvironmentalReverb::levelMb>()) != RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setLevelFailed");
            return ndk::ScopedAStatus::ok();
        }
        case EnvironmentalReverb::delayMs: {
            RETURN_IF(mContext->setEnvironmentalReverbDelay(
                              erParam.get<EnvironmentalReverb::delayMs>()) != RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setDelayFailed");
            return ndk::ScopedAStatus::ok();
        }
        case EnvironmentalReverb::diffusionPm: {
            RETURN_IF(mContext->setEnvironmentalReverbDiffusion(
                              erParam.get<EnvironmentalReverb::diffusionPm>()) != RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setDiffusionFailed");
            return ndk::ScopedAStatus::ok();
        }
        case EnvironmentalReverb::densityPm: {
            RETURN_IF(mContext->setEnvironmentalReverbDensity(
                              erParam.get<EnvironmentalReverb::densityPm>()) != RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setDensityFailed");
            return ndk::ScopedAStatus::ok();
        }
        case EnvironmentalReverb::bypass: {
            RETURN_IF(mContext->setEnvironmentalReverbBypass(
                              erParam.get<EnvironmentalReverb::bypass>()) != RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setBypassFailed");
            return ndk::ScopedAStatus::ok();
        }
        default: {
            LOG(ERROR) << __func__ << " unsupported tag: " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                    EX_ILLEGAL_ARGUMENT, "EnvironmentalReverbTagNotSupported");
        }
    }
}

ndk::ScopedAStatus EffectReverb::getParameterSpecific(const Parameter::Id& id,
                                                      Parameter::Specific* specific) {
    RETURN_IF(!specific, EX_NULL_POINTER, "nullPtr");
    auto tag = id.getTag();

    switch (tag) {
        case Parameter::Id::presetReverbTag:
            return getParameterPresetReverb(id.get<Parameter::Id::presetReverbTag>(), specific);
        case Parameter::Id::environmentalReverbTag:
            return getParameterEnvironmentalReverb(id.get<Parameter::Id::environmentalReverbTag>(),
                                                   specific);
        default:
            LOG(ERROR) << __func__ << " unsupported tag: " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "wrongIdTag");
    }
}

ndk::ScopedAStatus EffectReverb::getParameterPresetReverb(const PresetReverb::Id& id,
                                                          Parameter::Specific* specific) {
    RETURN_IF(id.getTag() != PresetReverb::Id::commonTag, EX_ILLEGAL_ARGUMENT,
              "PresetReverbTagNotSupported");
    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");
    PresetReverb prParam;
    auto tag = id.get<PresetReverb::Id::commonTag>();
    switch (tag) {
        case PresetReverb::preset: {
            prParam.set<PresetReverb::preset>(mContext->getPresetReverbPreset());
            break;
        }
        default: {
            LOG(ERROR) << __func__ << " unsupported tag: " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "PresetReverbTagNotSupported");
        }
    }

    specific->set<Parameter::Specific::presetReverb>(prParam);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus EffectReverb::getParameterEnvironmentalReverb(const EnvironmentalReverb::Id& id,
                                                                 Parameter::Specific* specific) {
    RETURN_IF(id.getTag() != EnvironmentalReverb::Id::commonTag, EX_ILLEGAL_ARGUMENT,
              "EnvironmentalReverbTagNotSupported");
    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");
    EnvironmentalReverb erParam;

    auto tag = id.get<EnvironmentalReverb::Id::commonTag>();
    switch (tag) {
        case EnvironmentalReverb::roomLevelMb: {
            erParam.set<EnvironmentalReverb::roomLevelMb>(
                    mContext->getEnvironmentalReverbRoomLevel());
            break;
        }
        case EnvironmentalReverb::roomHfLevelMb: {
            erParam.set<EnvironmentalReverb::roomHfLevelMb>(
                    mContext->getEnvironmentalReverbRoomHfLevel());
            break;
        }
        case EnvironmentalReverb::decayTimeMs: {
            erParam.set<EnvironmentalReverb::decayTimeMs>(
                    mContext->getEnvironmentalReverbDecayTime());
            break;
        }
        case EnvironmentalReverb::decayHfRatioPm: {
            erParam.set<EnvironmentalReverb::decayHfRatioPm>(
                    mContext->getEnvironmentalReverbDecayHfRatio());
            break;
        }
        case EnvironmentalReverb::reflectionsLevelMb: {
            erParam.set<EnvironmentalReverb::reflectionsLevelMb>(mContext->getReflectionsLevel());
            break;
        }
        case EnvironmentalReverb::reflectionsDelayMs: {
            erParam.set<EnvironmentalReverb::reflectionsDelayMs>(mContext->getReflectionsDelay());
            break;
        }
        case EnvironmentalReverb::levelMb: {
            erParam.set<EnvironmentalReverb::levelMb>(mContext->getEnvironmentalReverbLevel());
            break;
        }
        case EnvironmentalReverb::delayMs: {
            erParam.set<EnvironmentalReverb::delayMs>(mContext->getEnvironmentalReverbDelay());
            break;
        }
        case EnvironmentalReverb::diffusionPm: {
            erParam.set<EnvironmentalReverb::diffusionPm>(
                    mContext->getEnvironmentalReverbDiffusion());
            break;
        }
        case EnvironmentalReverb::densityPm: {
            erParam.set<EnvironmentalReverb::densityPm>(mContext->getEnvironmentalReverbDensity());
            break;
        }
        case EnvironmentalReverb::bypass: {
            erParam.set<EnvironmentalReverb::bypass>(mContext->getEnvironmentalReverbBypass());
            break;
        }
        default: {
            LOG(ERROR) << __func__ << " unsupported tag: " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                    EX_ILLEGAL_ARGUMENT, "EnvironmentalReverbTagNotSupported");
        }
    }

    specific->set<Parameter::Specific::environmentalReverb>(erParam);
    return ndk::ScopedAStatus::ok();
}

std::shared_ptr<EffectContext> EffectReverb::createContext(const Parameter::Common& common) {
    if (mContext) {
        LOG(DEBUG) << __func__ << " context already exist";
    } else {
        mContext = std::make_shared<ReverbContext>(1 /* statusFmqDepth */, common, mType);
    }

    return mContext;
}

RetCode EffectReverb::releaseContext() {
    if (mContext) {
        mContext.reset();
    }
    return RetCode::SUCCESS;
}

// Processing method running in EffectWorker thread.
IEffect::Status EffectReverb::effectProcessImpl(float* in, float* out, int sampleToProcess) {
    IEffect::Status status = {EX_NULL_POINTER, 0, 0};
    RETURN_VALUE_IF(!mContext, status, "nullContext");
    return mContext->process(in, out, sampleToProcess);
}

}  // namespace aidl::android::hardware::audio::effect
