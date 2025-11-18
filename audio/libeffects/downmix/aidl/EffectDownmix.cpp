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

#define ATRACE_TAG ATRACE_TAG_AUDIO
#define LOG_TAG "AHAL_DownmixImpl"

#include <android-base/logging.h>
#include <system/audio_effects/effect_uuid.h>
#include <utils/Trace.h>

#include "EffectDownmix.h"

using aidl::android::hardware::audio::effect::Descriptor;
using aidl::android::hardware::audio::effect::DownmixImpl;
using aidl::android::hardware::audio::effect::getEffectImplUuidDownmix;
using aidl::android::hardware::audio::effect::getEffectTypeUuidDownmix;
using aidl::android::hardware::audio::effect::IEffect;
using aidl::android::media::audio::common::AudioUuid;

extern "C" binder_exception_t createEffect(const AudioUuid* in_impl_uuid,
                                           std::shared_ptr<IEffect>* instanceSpp) {
    if (!in_impl_uuid || *in_impl_uuid != getEffectImplUuidDownmix()) {
        LOG(ERROR) << __func__ << "uuid not supported";
        return EX_ILLEGAL_ARGUMENT;
    }
    if (instanceSpp) {
        *instanceSpp = ndk::SharedRefBase::make<DownmixImpl>();
        return EX_NONE;
    } else {
        LOG(ERROR) << __func__ << " invalid input parameter!";
        return EX_ILLEGAL_ARGUMENT;
    }
}

extern "C" binder_exception_t queryEffect(const AudioUuid* in_impl_uuid, Descriptor* _aidl_return) {
    if (!in_impl_uuid || *in_impl_uuid != getEffectImplUuidDownmix()) {
        LOG(ERROR) << __func__ << "uuid not supported";
        return EX_ILLEGAL_ARGUMENT;
    }
    *_aidl_return = DownmixImpl::kDescriptor;
    return EX_NONE;
}

namespace aidl::android::hardware::audio::effect {

const std::string DownmixImpl::kEffectName = "Multichannel Downmix To Stereo";
const Descriptor DownmixImpl::kDescriptor = {
        .common = {.id = {.type = getEffectTypeUuidDownmix(),
                          .uuid = getEffectImplUuidDownmix(),
                          .proxy = std::nullopt},
                   .flags = {.type = Flags::Type::INSERT, .insert = Flags::Insert::FIRST},
                   .name = DownmixImpl::kEffectName,
                   .implementor = "The Android Open Source Project"}};

ndk::ScopedAStatus DownmixImpl::getDescriptor(Descriptor* _aidl_return) {
    RETURN_IF(!_aidl_return, EX_ILLEGAL_ARGUMENT, "Parameter:nullptr");
    *_aidl_return = kDescriptor;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus DownmixImpl::setParameterSpecific(const Parameter::Specific& specific) {
    RETURN_IF(Parameter::Specific::downmix != specific.getTag(), EX_ILLEGAL_ARGUMENT,
              "EffectNotSupported");
    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");

    auto& dmParam = specific.get<Parameter::Specific::downmix>();
    auto tag = dmParam.getTag();

    switch (tag) {
        case Downmix::type: {
            RETURN_IF(mContext->setDmType(dmParam.get<Downmix::type>()) != RetCode::SUCCESS,
                      EX_ILLEGAL_ARGUMENT, "setTypeFailed");
            return ndk::ScopedAStatus::ok();
        }
        default: {
            LOG(ERROR) << __func__ << " unsupported tag: " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "DownmixTagNotSupported");
        }
    }
}

ndk::ScopedAStatus DownmixImpl::getParameterSpecific(const Parameter::Id& id,
                                                     Parameter::Specific* specific) {
    RETURN_IF(!specific, EX_NULL_POINTER, "nullPtr");
    auto tag = id.getTag();
    RETURN_IF(Parameter::Id::downmixTag != tag, EX_ILLEGAL_ARGUMENT, "wrongIdTag");
    auto dmId = id.get<Parameter::Id::downmixTag>();
    auto dmIdTag = dmId.getTag();
    switch (dmIdTag) {
        case Downmix::Id::commonTag:
            return getParameterDownmix(dmId.get<Downmix::Id::commonTag>(), specific);
        default:
            LOG(ERROR) << __func__ << " unsupported tag: " << toString(dmIdTag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "DownmixTagNotSupported");
    }
}

ndk::ScopedAStatus DownmixImpl::getParameterDownmix(const Downmix::Tag& tag,
                                                    Parameter::Specific* specific) {
    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");

    Downmix dmParam;
    switch (tag) {
        case Downmix::type: {
            dmParam.set<Downmix::type>(mContext->getDmType());
            break;
        }
        default: {
            LOG(ERROR) << __func__ << " unsupported tag: " << toString(tag);
            return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                    "DownmixTagNotSupported");
        }
    }

    specific->set<Parameter::Specific::downmix>(dmParam);
    return ndk::ScopedAStatus::ok();
}

std::shared_ptr<EffectContext> DownmixImpl::createContext(const Parameter::Common& common) {
    if (mContext) {
        LOG(DEBUG) << __func__ << " context already exist";
        return mContext;
    }

    if (!DownmixContext::validateCommonConfig(common)) return nullptr;

    mContext = std::make_shared<DownmixContext>(1 /* statusFmqDepth */, common);
    return mContext;
}

RetCode DownmixImpl::releaseContext() {
    if (mContext) {
        mContext.reset();
    }
    return RetCode::SUCCESS;
}

void DownmixImpl::process() {
    ATRACE_NAME("Downmix::process");
    /**
     * wait for the EventFlag without lock, it's ok because the mEfGroup pointer will not change
     * in the life cycle of workerThread (threadLoop).
     */
    uint32_t efState = 0;
    if (!mEventFlag ||
        ::android::OK != mEventFlag->wait(mDataMqNotEmptyEf, &efState, 0 /* no timeout */,
                                          true /* retry */) ||
        !(efState & mDataMqNotEmptyEf)) {
        LOG(ERROR) << getEffectName() << __func__ << ": StatusEventFlag invalid";
    }

    {
        std::lock_guard lg(mImplMutex);
        RETURN_VALUE_IF(!mImplContext, void(), "nullContext");
        auto statusMQ = mImplContext->getStatusFmq();
        auto inputMQ = mImplContext->getInputDataFmq();
        auto outputMQ = mImplContext->getOutputDataFmq();
        auto buffer = mImplContext->getWorkBuffer();
        if (!inputMQ || !outputMQ) {
            return;
        }

        const auto availableToRead = inputMQ->availableToRead();
        const auto availableToWrite = outputMQ->availableToWrite() *
                                      mImplContext->getInputFrameSize() /
                                      mImplContext->getOutputFrameSize();
        assert(mImplContext->getWorkBufferSize() >=
               std::max(availableToRead(), availableToWrite));
        auto processSamples = std::min(availableToRead, availableToWrite);
        if (processSamples) {
            inputMQ->read(buffer, processSamples);
            IEffect::Status status = effectProcessImpl(buffer, buffer, processSamples);
            outputMQ->write(buffer, status.fmqProduced);
            statusMQ->writeBlocking(&status, 1);
        }
    }
}

// Processing method running in EffectWorker thread.
IEffect::Status DownmixImpl::effectProcessImpl(float* in, float* out, int sampleToProcess) {
    if (!mContext) {
        LOG(ERROR) << __func__ << " nullContext";
        return {EX_NULL_POINTER, 0, 0};
    }
    return mContext->downmixProcess(in, out, sampleToProcess);
}

}  // namespace aidl::android::hardware::audio::effect
