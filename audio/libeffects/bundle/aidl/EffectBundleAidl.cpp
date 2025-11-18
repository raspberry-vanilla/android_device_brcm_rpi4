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

#include <algorithm>
#include <limits.h>
#include <unordered_set>
#define LOG_TAG "EffectBundleAidl"

#include <audio_effects/effect_bassboost.h>
#include <audio_effects/effect_equalizer.h>
#include <audio_effects/effect_virtualizer.h>
#include <android-base/logging.h>
#include <fmq/AidlMessageQueue.h>
#include <LVM.h>
#include <Utils.h>

#include "BundleTypes.h"
#include "EffectBundleAidl.h"

using aidl::android::hardware::audio::effect::getEffectImplUuidBassBoostBundle;
using aidl::android::hardware::audio::effect::Descriptor;
using aidl::android::hardware::audio::effect::EffectBundleAidl;
using aidl::android::hardware::audio::effect::getEffectImplUuidEqualizerBundle;
using aidl::android::hardware::audio::effect::IEffect;
using aidl::android::hardware::audio::effect::State;
using aidl::android::hardware::audio::effect::getEffectImplUuidVirtualizerBundle;
using aidl::android::hardware::audio::effect::getEffectImplUuidVolumeBundle;
using aidl::android::media::audio::common::AudioUuid;

bool isUuidSupported(const AudioUuid* uuid) {
    return (*uuid == getEffectImplUuidBassBoostBundle() ||
            *uuid == getEffectImplUuidEqualizerBundle() ||
            *uuid == getEffectImplUuidVirtualizerBundle() ||
            *uuid == getEffectImplUuidVolumeBundle());
}

extern "C" binder_exception_t createEffect(const AudioUuid* uuid,
                                           std::shared_ptr<IEffect>* instanceSpp) {
    if (uuid == nullptr || !isUuidSupported(uuid)) {
        LOG(ERROR) << __func__ << "uuid not supported";
        return EX_ILLEGAL_ARGUMENT;
    }
    if (instanceSpp) {
        *instanceSpp = ndk::SharedRefBase::make<EffectBundleAidl>(*uuid);
        return EX_NONE;
    } else {
        LOG(ERROR) << __func__ << " invalid input parameter!";
        return EX_ILLEGAL_ARGUMENT;
    }
}

extern "C" binder_exception_t queryEffect(const AudioUuid* in_impl_uuid, Descriptor* _aidl_return) {
    if (!in_impl_uuid || !isUuidSupported(in_impl_uuid)) {
        LOG(ERROR) << __func__ << "uuid not supported";
        return EX_ILLEGAL_ARGUMENT;
    }
    if (*in_impl_uuid == getEffectImplUuidEqualizerBundle()) {
        *_aidl_return = aidl::android::hardware::audio::effect::lvm::kEqualizerDesc;
    } else if (*in_impl_uuid == getEffectImplUuidBassBoostBundle()) {
        *_aidl_return = aidl::android::hardware::audio::effect::lvm:: kBassBoostDesc;
    } else if (*in_impl_uuid == getEffectImplUuidVirtualizerBundle()) {
        *_aidl_return = aidl::android::hardware::audio::effect::lvm::kVirtualizerDesc;
    } else if (*in_impl_uuid == getEffectImplUuidVolumeBundle()) {
        *_aidl_return = aidl::android::hardware::audio::effect::lvm::kVolumeDesc;
    }
    return EX_NONE;
}

namespace aidl::android::hardware::audio::effect {

EffectBundleAidl::EffectBundleAidl(const AudioUuid& uuid) {
    if (uuid == getEffectImplUuidEqualizerBundle()) {
        mType = lvm::BundleEffectType::EQUALIZER;
        mDescriptor = &lvm::kEqualizerDesc;
        mEffectName = &lvm::kEqualizerEffectName;
    } else if (uuid == getEffectImplUuidBassBoostBundle()) {
        mType = lvm::BundleEffectType::BASS_BOOST;
        mDescriptor = &lvm::kBassBoostDesc;
        mEffectName = &lvm::kBassBoostEffectName;
    } else if (uuid == getEffectImplUuidVirtualizerBundle()) {
        mType = lvm::BundleEffectType::VIRTUALIZER;
        mDescriptor = &lvm::kVirtualizerDesc;
        mEffectName = &lvm::kVirtualizerEffectName;
    } else if (uuid == getEffectImplUuidVolumeBundle()) {
        mType = lvm::BundleEffectType::VOLUME;
        mDescriptor = &lvm::kVolumeDesc;
        mEffectName = &lvm::kVolumeEffectName;
    } else {
        LOG(ERROR) << __func__ << uuid.toString() << " not supported!";
    }
}

EffectBundleAidl::~EffectBundleAidl() {
    cleanUp();
}

ndk::ScopedAStatus EffectBundleAidl::getDescriptor(Descriptor* _aidl_return) {
    RETURN_IF(!_aidl_return, EX_ILLEGAL_ARGUMENT, "Parameter:nullptr");
    *_aidl_return = *mDescriptor;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus EffectBundleAidl::setParameterCommon(const Parameter& param) {
    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");

    auto tag = param.getTag();
    switch (tag) {
        case Parameter::common:
            RETURN_IF(mContext->setCommon(param.get<Parameter::common>()) != RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setCommFailed");
            break;
        case Parameter::deviceDescription:
            RETURN_IF(mContext->setOutputDevice(param.get<Parameter::deviceDescription>()) !=
                              RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setDeviceFailed");
            break;
        case Parameter::mode:
            RETURN_IF(mContext->setAudioMode(param.get<Parameter::mode>()) != RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setModeFailed");
            break;
        case Parameter::source:
            RETURN_IF(mContext->setAudioSource(param.get<Parameter::source>()) != RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setSourceFailed");
            break;
        case Parameter::volumeStereo:
            RETURN_IF(mContext->setVolumeStereo(param.get<Parameter::volumeStereo>()) !=
                              RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setVolumeStereoFailed");
            break;
        default: {
            LOG(ERROR) << __func__ << " unsupportedParameterTag " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "commonParamNotSupported");
        }
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus EffectBundleAidl::setParameterSpecific(const Parameter::Specific& specific) {
    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");

    auto tag = specific.getTag();
    switch (tag) {
        case Parameter::Specific::equalizer:
            return setParameterEqualizer(specific);
        case Parameter::Specific::bassBoost:
            return setParameterBassBoost(specific);
        case Parameter::Specific::virtualizer:
            return setParameterVirtualizer(specific);
        case Parameter::Specific::volume:
            return setParameterVolume(specific);
        default:
            LOG(ERROR) << __func__ << " unsupported tag " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "specificParamNotSupported");
    }
}

ndk::ScopedAStatus EffectBundleAidl::setParameterEqualizer(const Parameter::Specific& specific) {
    auto& eq = specific.get<Parameter::Specific::equalizer>();
    RETURN_IF(!inRange(eq, lvm::kEqRanges), EX_ILLEGAL_ARGUMENT, "outOfRange");
    auto eqTag = eq.getTag();
    switch (eqTag) {
        case Equalizer::preset:
            RETURN_IF(mContext->setEqualizerPreset(eq.get<Equalizer::preset>()) != RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setBandLevelsFailed");
            return ndk::ScopedAStatus::ok();
        case Equalizer::bandLevels:
            RETURN_IF(mContext->setEqualizerBandLevels(eq.get<Equalizer::bandLevels>()) !=
                              RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setBandLevelsFailed");
            return ndk::ScopedAStatus::ok();
        default:
            LOG(ERROR) << __func__ << " unsupported parameter " << specific.toString();
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "eqTagNotSupported");
    }
}

ndk::ScopedAStatus EffectBundleAidl::setParameterBassBoost(const Parameter::Specific& specific) {
    auto& bb = specific.get<Parameter::Specific::bassBoost>();
    RETURN_IF(!inRange(bb, lvm::kBassBoostRanges), EX_ILLEGAL_ARGUMENT, "outOfRange");
    auto bbTag = bb.getTag();
    switch (bbTag) {
        case BassBoost::strengthPm: {
            RETURN_IF(mContext->setBassBoostStrength(bb.get<BassBoost::strengthPm>()) !=
                              RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setStrengthFailed");
            return ndk::ScopedAStatus::ok();
        }
        default:
            LOG(ERROR) << __func__ << " unsupported parameter " << specific.toString();
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "bbTagNotSupported");
    }
}

ndk::ScopedAStatus EffectBundleAidl::setParameterVirtualizer(const Parameter::Specific& specific) {
    auto& vr = specific.get<Parameter::Specific::virtualizer>();
    RETURN_IF(!inRange(vr, lvm::kVirtualizerRanges), EX_ILLEGAL_ARGUMENT, "outOfRange");
    auto vrTag = vr.getTag();
    switch (vrTag) {
        case Virtualizer::strengthPm: {
            RETURN_IF(mContext->setVirtualizerStrength(vr.get<Virtualizer::strengthPm>()) !=
                              RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setStrengthFailed");
            return ndk::ScopedAStatus::ok();
        }
        case Virtualizer::device: {
            RETURN_IF(mContext->setForcedDevice(vr.get<Virtualizer::device>()) != RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setDeviceFailed");
            return ndk::ScopedAStatus::ok();
        }
        case Virtualizer::speakerAngles:
            FALLTHROUGH_INTENDED;
        case Virtualizer::vendor: {
            LOG(ERROR) << __func__ << " unsupported tag: " << toString(vrTag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "VirtualizerTagNotSupported");
        }
    }
}

ndk::ScopedAStatus EffectBundleAidl::setParameterVolume(const Parameter::Specific& specific) {
    auto& vol = specific.get<Parameter::Specific::volume>();
    RETURN_IF(!inRange(vol, lvm::kVolumeRanges), EX_ILLEGAL_ARGUMENT, "outOfRange");
    auto volTag = vol.getTag();
    switch (volTag) {
        case Volume::levelDb: {
            RETURN_IF(mContext->setVolumeLevel(vol.get<Volume::levelDb>()) != RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setLevelFailed");
            return ndk::ScopedAStatus::ok();
        }
        case Volume::mute:
            RETURN_IF(mContext->setVolumeMute(vol.get<Volume::mute>()) != RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setMuteFailed");
            return ndk::ScopedAStatus::ok();
        default:
            LOG(ERROR) << __func__ << " unsupported parameter " << specific.toString();
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "volTagNotSupported");
    }
}

ndk::ScopedAStatus EffectBundleAidl::getParameterSpecific(const Parameter::Id& id,
                                                          Parameter::Specific* specific) {
    RETURN_IF(!specific, EX_NULL_POINTER, "nullPtr");
    auto tag = id.getTag();

    switch (tag) {
        case Parameter::Id::equalizerTag:
            return getParameterEqualizer(id.get<Parameter::Id::equalizerTag>(), specific);
        case Parameter::Id::bassBoostTag:
            return getParameterBassBoost(id.get<Parameter::Id::bassBoostTag>(), specific);
        case Parameter::Id::virtualizerTag:
            return getParameterVirtualizer(id.get<Parameter::Id::virtualizerTag>(), specific);
        case Parameter::Id::volumeTag:
            return getParameterVolume(id.get<Parameter::Id::volumeTag>(), specific);
        default:
            LOG(ERROR) << __func__ << " unsupported tag: " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "wrongIdTag");
    }
}

ndk::ScopedAStatus EffectBundleAidl::getParameterEqualizer(const Equalizer::Id& id,
                                                           Parameter::Specific* specific) {
    RETURN_IF(id.getTag() != Equalizer::Id::commonTag, EX_ILLEGAL_ARGUMENT,
              "EqualizerTagNotSupported");
    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");
    Equalizer eqParam;

    auto tag = id.get<Equalizer::Id::commonTag>();
    switch (tag) {
        case Equalizer::bandLevels: {
            eqParam.set<Equalizer::bandLevels>(mContext->getEqualizerBandLevels());
            break;
        }
        case Equalizer::preset: {
            eqParam.set<Equalizer::preset>(mContext->getEqualizerPreset());
            break;
        }
        case Equalizer::bandFrequencies: {
            eqParam.set<Equalizer::bandFrequencies>(lvm::kEqBandFrequency);
            break;
        }
        case Equalizer::presets: {
            eqParam.set<Equalizer::presets>(lvm::kEqPresets);
            break;
        }
        case Equalizer::centerFreqMh: {
            eqParam.set<Equalizer::centerFreqMh>(mContext->getEqualizerCenterFreqs());
            break;
        }
        case Equalizer::vendor: {
            LOG(ERROR) << __func__ << " not handled tag: " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                    EX_ILLEGAL_ARGUMENT, "unsupportedTag");
        }
    }

    specific->set<Parameter::Specific::equalizer>(eqParam);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus EffectBundleAidl::getParameterBassBoost(const BassBoost::Id& id,
                                                           Parameter::Specific* specific) {
    RETURN_IF(id.getTag() != BassBoost::Id::commonTag, EX_ILLEGAL_ARGUMENT,
              "BassBoostTagNotSupported");
    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");
    BassBoost bbParam;

    auto tag = id.get<BassBoost::Id::commonTag>();
    switch (tag) {
        case BassBoost::strengthPm: {
            bbParam.set<BassBoost::strengthPm>(mContext->getBassBoostStrength());
            break;
        }
        default: {
            LOG(ERROR) << __func__ << " not handled tag: " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "BassBoostTagNotSupported");
        }
    }

    specific->set<Parameter::Specific::bassBoost>(bbParam);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus EffectBundleAidl::getParameterVolume(const Volume::Id& id,
                                                        Parameter::Specific* specific) {
    RETURN_IF(id.getTag() != Volume::Id::commonTag, EX_ILLEGAL_ARGUMENT, "VolumeTagNotSupported");

    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");
    Volume volParam;

    auto tag = id.get<Volume::Id::commonTag>();
    switch (tag) {
        case Volume::levelDb: {
            volParam.set<Volume::levelDb>(static_cast<int>(mContext->getVolumeLevel()));
            break;
        }
        case Volume::mute: {
            volParam.set<Volume::mute>(mContext->getVolumeMute());
            break;
        }
        default: {
            LOG(ERROR) << __func__ << " not handled tag: " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "VolumeTagNotSupported");
        }
    }

    specific->set<Parameter::Specific::volume>(volParam);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus EffectBundleAidl::getParameterVirtualizer(const Virtualizer::Id& id,
                                                             Parameter::Specific* specific) {
    RETURN_IF((id.getTag() != Virtualizer::Id::commonTag) &&
                      (id.getTag() != Virtualizer::Id::speakerAnglesPayload),
              EX_ILLEGAL_ARGUMENT, "VirtualizerTagNotSupported");

    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");
    Virtualizer vrParam;

    if (id.getTag() == Virtualizer::Id::speakerAnglesPayload) {
        auto angles = mContext->getSpeakerAngles(id.get<Virtualizer::Id::speakerAnglesPayload>());
        RETURN_IF(angles.size() == 0, EX_ILLEGAL_ARGUMENT, "getSpeakerAnglesFailed");
        Virtualizer param = Virtualizer::make<Virtualizer::speakerAngles>(angles);
        specific->set<Parameter::Specific::virtualizer>(param);
        return ndk::ScopedAStatus::ok();
    }

    auto tag = id.get<Virtualizer::Id::commonTag>();
    switch (tag) {
        case Virtualizer::strengthPm: {
            vrParam.set<Virtualizer::strengthPm>(mContext->getVirtualizerStrength());
            break;
        }
        case Virtualizer::device: {
            vrParam.set<Virtualizer::device>(mContext->getForcedDevice());
            break;
        }
        case Virtualizer::speakerAngles:
            FALLTHROUGH_INTENDED;
        case Virtualizer::vendor: {
            LOG(ERROR) << __func__ << " unsupported tag: " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "VirtualizerTagNotSupported");
        }
    }

    specific->set<Parameter::Specific::virtualizer>(vrParam);
    return ndk::ScopedAStatus::ok();
}

std::shared_ptr<EffectContext> EffectBundleAidl::createContext(const Parameter::Common& common) {
    if (mContext) {
        LOG(DEBUG) << __func__ << " context already exist";
    } else {
        // GlobalSession is a singleton
        mContext = GlobalSession::getGlobalSession().createSession(mType, 1 /* statusFmqDepth */,
                                                                   common);
    }

    return mContext;
}

RetCode EffectBundleAidl::releaseContext() {
    if (mContext) {
        GlobalSession::getGlobalSession().releaseSession(mType, mContext->getSessionId());
        mContext.reset();
    }
    return RetCode::SUCCESS;
}

// Processing method running in EffectWorker thread.
IEffect::Status EffectBundleAidl::effectProcessImpl(float* in, float* out, int sampleToProcess) {
    IEffect::Status status = {EX_NULL_POINTER, 0, 0};
    RETURN_VALUE_IF(!mContext, status, "nullContext");
    return mContext->process(in, out, sampleToProcess);
}

}  // namespace aidl::android::hardware::audio::effect
