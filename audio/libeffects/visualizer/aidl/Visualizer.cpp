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

#define LOG_TAG "AHAL_VisualizerLibEffects"

#include <android-base/logging.h>
#include <system/audio_effects/effect_uuid.h>

#include "Visualizer.h"

using aidl::android::hardware::audio::effect::Descriptor;
using aidl::android::hardware::audio::effect::getEffectImplUuidVisualizer;
using aidl::android::hardware::audio::effect::getEffectTypeUuidVisualizer;
using aidl::android::hardware::audio::effect::IEffect;
using aidl::android::hardware::audio::effect::State;
using aidl::android::hardware::audio::effect::VisualizerImpl;
using aidl::android::media::audio::common::AudioUuid;

extern "C" binder_exception_t createEffect(const AudioUuid* in_impl_uuid,
                                           std::shared_ptr<IEffect>* instanceSpp) {
    if (!in_impl_uuid || *in_impl_uuid != getEffectImplUuidVisualizer()) {
        LOG(ERROR) << __func__ << "uuid not supported";
        return EX_ILLEGAL_ARGUMENT;
    }
    if (instanceSpp) {
        *instanceSpp = ndk::SharedRefBase::make<VisualizerImpl>();
        return EX_NONE;
    } else {
        LOG(ERROR) << __func__ << " invalid input parameter!";
        return EX_ILLEGAL_ARGUMENT;
    }
}

extern "C" binder_exception_t queryEffect(const AudioUuid* in_impl_uuid, Descriptor* _aidl_return) {
    if (!in_impl_uuid || *in_impl_uuid != getEffectImplUuidVisualizer()) {
        LOG(ERROR) << __func__ << "uuid not supported";
        return EX_ILLEGAL_ARGUMENT;
    }
    *_aidl_return = VisualizerImpl::kDescriptor;
    return EX_NONE;
}

namespace aidl::android::hardware::audio::effect {

const std::string VisualizerImpl::kEffectName = "Visualizer";
const std::vector<Range::VisualizerRange> VisualizerImpl::kRanges = {
        MAKE_RANGE(Visualizer, latencyMs, 0, VisualizerContext::kMaxLatencyMs),
        MAKE_RANGE(Visualizer, captureSamples, VisualizerContext::kMinCaptureBufSize,
                   VisualizerContext::kMaxCaptureBufSize),
        /* get only parameters, set invalid range (min > max) to indicate not support set */
        MAKE_RANGE(Visualizer, measurement, Visualizer::Measurement({.rms = 1, .peak = 1}),
                   Visualizer::Measurement({.rms = 0, .peak = 0})),
        MAKE_RANGE(Visualizer, captureSampleBuffer, std::vector<uint8_t>({1}),
                   std::vector<uint8_t>({0}))};
const Capability VisualizerImpl::kCapability = {
        .range = Range::make<Range::visualizer>(VisualizerImpl::kRanges)};
const Descriptor VisualizerImpl::kDescriptor = {
        .common = {.id = {.type = getEffectTypeUuidVisualizer(),
                          .uuid = getEffectImplUuidVisualizer(),
                          .proxy = std::nullopt},
                   .flags = {.type = Flags::Type::INSERT,
                             .insert = Flags::Insert::FIRST,
                             .volume = Flags::Volume::NONE},
                   .name = VisualizerImpl::kEffectName,
                   .implementor = "The Android Open Source Project"},
        .capability = VisualizerImpl::kCapability};

ndk::ScopedAStatus VisualizerImpl::getDescriptor(Descriptor* _aidl_return) {
    RETURN_IF(!_aidl_return, EX_ILLEGAL_ARGUMENT, "Parameter:nullptr");
    LOG(DEBUG) << __func__ << kDescriptor.toString();
    *_aidl_return = kDescriptor;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VisualizerImpl::setParameterSpecific(const Parameter::Specific& specific) {
    RETURN_IF(Parameter::Specific::visualizer != specific.getTag(), EX_ILLEGAL_ARGUMENT,
              "EffectNotSupported");
    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");

    auto& param = specific.get<Parameter::Specific::visualizer>();
    RETURN_IF(!inRange(param, kRanges), EX_ILLEGAL_ARGUMENT, "outOfRange");
    const auto tag = param.getTag();
    switch (tag) {
        case Visualizer::captureSamples: {
            RETURN_IF(mContext->setCaptureSamples(param.get<Visualizer::captureSamples>()) !=
                              RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setCaptureSizeFailed");
            return ndk::ScopedAStatus::ok();
        }
        case Visualizer::scalingMode: {
            RETURN_IF(mContext->setScalingMode(param.get<Visualizer::scalingMode>()) !=
                              RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setScalingModeFailed");
            return ndk::ScopedAStatus::ok();
        }
        case Visualizer::measurementMode: {
            RETURN_IF(mContext->setMeasurementMode(param.get<Visualizer::measurementMode>()) !=
                              RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setMeasurementModeFailed");
            return ndk::ScopedAStatus::ok();
        }
        case Visualizer::latencyMs: {
            RETURN_IF(mContext->setDownstreamLatency(param.get<Visualizer::latencyMs>()) !=
                              RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setLatencyFailed");
            return ndk::ScopedAStatus::ok();
        }
        default: {
            LOG(ERROR) << __func__ << " unsupported tag: " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                    EX_ILLEGAL_ARGUMENT, "VisualizerTagNotSupported");
        }
    }
}

ndk::ScopedAStatus VisualizerImpl::getParameterSpecific(const Parameter::Id& id,
                                                        Parameter::Specific* specific) {
    RETURN_IF(!specific, EX_NULL_POINTER, "nullPtr");
    auto tag = id.getTag();
    RETURN_IF(Parameter::Id::visualizerTag != tag, EX_ILLEGAL_ARGUMENT, "wrongIdTag");
    auto specificId = id.get<Parameter::Id::visualizerTag>();
    auto specificTag = specificId.getTag();
    switch (specificTag) {
        case Visualizer::Id::commonTag: {
            return getParameterVisualizer(specificId.get<Visualizer::Id::commonTag>(), specific);
        }
        default: {
            LOG(ERROR) << __func__ << " unsupported tag: " << toString(specificTag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "VisualizerTagNotSupported");
        }
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VisualizerImpl::getParameterVisualizer(const Visualizer::Tag& tag,
                                                          Parameter::Specific* specific) {
    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");

    Visualizer param;
    switch (tag) {
        case Visualizer::captureSamples: {
            param.set<Visualizer::captureSamples>(mContext->getCaptureSamples());
            break;
        }
        case Visualizer::scalingMode: {
            param.set<Visualizer::scalingMode>(mContext->getScalingMode());
            break;
        }
        case Visualizer::measurementMode: {
            param.set<Visualizer::measurementMode>(mContext->getMeasurementMode());
            break;
        }
        case Visualizer::measurement: {
            param.set<Visualizer::measurement>(mContext->getMeasure());
            break;
        }
        case Visualizer::captureSampleBuffer: {
            param.set<Visualizer::captureSampleBuffer>(mContext->capture());
            break;
        }
        case Visualizer::latencyMs: {
            param.set<Visualizer::latencyMs>(mContext->getDownstreamLatency());
            break;
        }
        default: {
            LOG(ERROR) << __func__ << " unsupported tag: " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
                    EX_ILLEGAL_ARGUMENT, "VisualizerTagNotSupported");
        }
    }

    specific->set<Parameter::Specific::visualizer>(param);
    return ndk::ScopedAStatus::ok();
}

std::shared_ptr<EffectContext> VisualizerImpl::createContext(const Parameter::Common& common) {
    if (mContext) {
        LOG(DEBUG) << __func__ << " context already exist";
        return mContext;
    }

    mContext = std::make_shared<VisualizerContext>(1 /* statusFmqDepth */, common);
    mContext->initParams(common);
    return mContext;
}

RetCode VisualizerImpl::releaseContext() {
    if (mContext) {
        mContext->disable();
        mContext->reset();
        mContext->resetBuffer();
    }
    return RetCode::SUCCESS;
}

// Processing method running in EffectWorker thread.
IEffect::Status VisualizerImpl::effectProcessImpl(float* in, float* out, int samples) {
    IEffect::Status status = {EX_NULL_POINTER, 0, 0};
    RETURN_VALUE_IF(!mContext, status, "nullContext");
    return mContext->process(in, out, samples);
}

}  // namespace aidl::android::hardware::audio::effect
