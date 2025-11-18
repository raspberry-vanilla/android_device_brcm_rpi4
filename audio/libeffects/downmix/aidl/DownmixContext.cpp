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

#define LOG_TAG "AHAL_DownmixContext"

#include <android-base/logging.h>

#include "DownmixContext.h"

using aidl::android::hardware::audio::common::getChannelCount;
using aidl::android::hardware::audio::effect::IEffect;
using aidl::android::media::audio::common::AudioChannelLayout;
using aidl::android::media::audio::common::AudioConfig;

namespace aidl::android::hardware::audio::effect {

namespace {

inline bool isChannelMaskValid(const AudioChannelLayout& channelMask) {
    if (channelMask.getTag() != AudioChannelLayout::layoutMask) return false;
    int chMask = channelMask.get<AudioChannelLayout::layoutMask>();
    // check against unsupported channels (up to FCC_26)
    constexpr uint32_t MAXIMUM_CHANNEL_MASK = AudioChannelLayout::LAYOUT_22POINT2 |
                                              AudioChannelLayout::CHANNEL_FRONT_WIDE_LEFT |
                                              AudioChannelLayout::CHANNEL_FRONT_WIDE_RIGHT;
    if (chMask & ~MAXIMUM_CHANNEL_MASK) {
        LOG(ERROR) << "Unsupported channels in " << (chMask & ~MAXIMUM_CHANNEL_MASK);
        return false;
    }
    return true;
}

inline bool isStereoChannelMask(const AudioChannelLayout& channelMask) {
    if (channelMask.getTag() != AudioChannelLayout::layoutMask) return false;

    return channelMask.get<AudioChannelLayout::layoutMask>() == AudioChannelLayout::LAYOUT_STEREO;
}

}  // namespace

bool DownmixContext::validateCommonConfig(const Parameter::Common& common) {
    const AudioConfig& input = common.input;
    const AudioConfig& output = common.output;
    if (input.base.sampleRate != output.base.sampleRate) {
        LOG(ERROR) << __func__ << ": SRC not supported, input: " << input.toString()
                   << " output: " << output.toString();
        return false;
    }

    if (!isStereoChannelMask(output.base.channelMask)) {
        LOG(ERROR) << __func__ << ": output should be stereo, not "
                   << output.base.channelMask.toString();
        return false;
    }

    if (!isChannelMaskValid(input.base.channelMask)) {
        LOG(ERROR) << __func__ << ": invalid input channel, " << input.base.channelMask.toString();
        return false;
    }

    return true;
}

DownmixContext::DownmixContext(int statusDepth, const Parameter::Common& common)
    : EffectContext(statusDepth, common) {
    mState = DOWNMIX_STATE_UNINITIALIZED;
    init_params(common);
}

DownmixContext::~DownmixContext() {
    mState = DOWNMIX_STATE_UNINITIALIZED;
}

RetCode DownmixContext::enable() {
    if (mState != DOWNMIX_STATE_INITIALIZED) {
        return RetCode::ERROR_EFFECT_LIB_ERROR;
    }
    mState = DOWNMIX_STATE_ACTIVE;
    return RetCode::SUCCESS;
}

RetCode DownmixContext::disable() {
    if (mState != DOWNMIX_STATE_ACTIVE) {
        return RetCode::ERROR_EFFECT_LIB_ERROR;
    }
    mState = DOWNMIX_STATE_INITIALIZED;
    return RetCode::SUCCESS;
}

IEffect::Status DownmixContext::downmixProcess(float* in, float* out, int samples) {
    IEffect::Status status = {EX_ILLEGAL_ARGUMENT, 0, 0};

    if (in == nullptr || out == nullptr ||
        getCommon().input.frameCount != getCommon().output.frameCount || getInputFrameSize() == 0) {
        return status;
    }

    status = {EX_ILLEGAL_STATE, 0, 0};
    if (mState == DOWNMIX_STATE_UNINITIALIZED) {
        LOG(ERROR) << __func__ << "Trying to use an uninitialized downmixer";
        return status;
    } else if (mState == DOWNMIX_STATE_INITIALIZED) {
        LOG(ERROR) << __func__ << "Trying to use a non-configured downmixer";
        return status;
    }

    bool accumulate = false;
    int frames = samples * sizeof(float) / getInputFrameSize();
    if (mType == Downmix::Type::STRIP) {
        while (frames) {
            if (accumulate) {
                out[0] = std::clamp(out[0] + in[0], -1.f, 1.f);
                out[1] = std::clamp(out[1] + in[1], -1.f, 1.f);
            } else {
                out[0] = in[0];
                out[1] = in[1];
            }
            in += mInputChannelCount;
            out += 2;
            frames--;
        }
    } else {
        int chMask = mChMask.get<AudioChannelLayout::layoutMask>();
        if (!mChannelMix.process(in, out, frames, accumulate, (audio_channel_mask_t)chMask)) {
            LOG(ERROR) << "Multichannel configuration " << mChMask.toString()
                       << " is not supported";
            return status;
        }
    }
    int producedSamples = (samples / mInputChannelCount) << 1;
    return {STATUS_OK, samples, producedSamples};
}

void DownmixContext::init_params(const Parameter::Common& common) {
    // when configuring the effect, do not allow a blank or unsupported channel mask
    AudioChannelLayout channelMask = common.input.base.channelMask;
    if (!isChannelMaskValid(channelMask)) {
        LOG(ERROR) << "Downmix_Configure error: input channel mask " << channelMask.toString()
                   << " not supported";
    } else {
        mType = Downmix::Type::FOLD;
        mChMask = channelMask;
        mState = DOWNMIX_STATE_INITIALIZED;
    }
}

}  // namespace aidl::android::hardware::audio::effect
