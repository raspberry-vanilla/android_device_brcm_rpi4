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

#include <cstddef>

#define LOG_TAG "ReverbContext"
#include <android-base/logging.h>
#include <Utils.h>
#include <audio_utils/primitives.h>

#include "ReverbContext.h"
#include "VectorArithmetic.h"
#include "math.h"

namespace aidl::android::hardware::audio::effect {

using aidl::android::media::audio::common::AudioDeviceDescription;
using aidl::android::media::audio::common::AudioDeviceType;

#define GOTO_IF_LVREV_ERROR(status, tag, log)                                     \
    do {                                                                          \
        LVREV_ReturnStatus_en temp = (status);                                    \
        if (temp != LVREV_SUCCESS) {                                              \
            LOG(ERROR) << __func__ << " return status: " << temp << " " << (log); \
            goto tag;                                                             \
        }                                                                         \
    } while (0)

RetCode ReverbContext::init() {
    if (isPreset()) {
        // force reloading preset at first call to process()
        mPreset = PresetReverb::Presets::NONE;
        mNextPreset = PresetReverb::Presets::NONE;
    }

    mVolume.left = kUnitVolume;
    mVolume.right = kUnitVolume;
    mPrevVolume.left = kUnitVolume;
    mPrevVolume.right = kUnitVolume;
    volumeMode = VOLUME_FLAT;

    mSamplesToExitCount = kDefaultDecayTime * mCommon.input.base.sampleRate / 1000;

    /* Saved strength is used to return the exact strength that was used in the set to the get
     * because we map the original strength range of 0:1000 to 1:15, and this will avoid
     * quantisation like effect when returning
     */
    mRoomLevel = lvm::kMinLevel;
    mRoomHfLevel = 0;
    mEnabled = LVM_FALSE;
    mDecayTime = kDefaultDecayTime;
    mDecayHfRatio = kDefaultDamping * 20;
    mDensity = kDefaultRoomSize * 10;
    mDiffusion = kDefaultDensity * 10;
    mLevel = lvm::kMinLevel;

    // allocate lvm reverb instance
    LVREV_ReturnStatus_en status = LVREV_SUCCESS;
    LVREV_InstanceParams_st params = {
            .MaxBlockSize = lvm::kMaxCallSize,
            // Max format, could be mono during process
            .SourceFormat = LVM_STEREO,
            .NumDelays = LVREV_DELAYLINES_4,
    };
    /* Init sets the instance handle */
    status = LVREV_GetInstanceHandle(&mInstance, &params);
    GOTO_IF_LVREV_ERROR(status, deinit, "LVREV_GetInstanceHandleFailed");

    // set control
    LVREV_ControlParams_st controlParams;
    initControlParameter(controlParams);
    status = LVREV_SetControlParameters(mInstance, &controlParams);
    GOTO_IF_LVREV_ERROR(status, deinit, "LVREV_SetControlParametersFailed");

    return RetCode::SUCCESS;

deinit:
    deInit();
    return RetCode::ERROR_EFFECT_LIB_ERROR;
}

void ReverbContext::deInit() {
    if (mInstance) {
        LVREV_FreeInstance(mInstance);
        mInstance = nullptr;
    }
}

RetCode ReverbContext::enable() {
    if (mEnabled) return RetCode::ERROR_ILLEGAL_PARAMETER;
    mEnabled = true;
    mSamplesToExitCount = (mDecayTime * mCommon.input.base.sampleRate) / 1000;
    // force no volume ramp for first buffer processed after enabling the effect
    volumeMode = VOLUME_FLAT;
    return RetCode::SUCCESS;
}

RetCode ReverbContext::disable() {
    if (!mEnabled) return RetCode::ERROR_ILLEGAL_PARAMETER;
    mEnabled = false;
    return RetCode::SUCCESS;
}

bool ReverbContext::isAuxiliary() {
    return (mType == lvm::ReverbEffectType::AUX_ENV || mType == lvm::ReverbEffectType::AUX_PRESET);
}

bool ReverbContext::isPreset() {
    return (mType == lvm::ReverbEffectType::AUX_PRESET ||
            mType == lvm::ReverbEffectType::INSERT_PRESET);
}

RetCode ReverbContext::setVolumeStereo(const Parameter::VolumeStereo& volume) {
    if (volumeMode == VOLUME_OFF) {
        // force no volume ramp for first buffer processed after getting volume control
        volumeMode = VOLUME_FLAT;
    }
    mVolumeStereo = volume;
    return RetCode::SUCCESS;
}

RetCode ReverbContext::setPresetReverbPreset(const PresetReverb::Presets& preset) {
    mNextPreset = preset;
    return RetCode::SUCCESS;
}

RetCode ReverbContext::setEnvironmentalReverbRoomLevel(int roomLevel) {
    // Update Control Parameter
    LVREV_ControlParams_st params;
    RETURN_VALUE_IF(LVREV_SUCCESS != LVREV_GetControlParameters(mInstance, &params),
                    RetCode::ERROR_EFFECT_LIB_ERROR, " getControlParamFailed");

    // Sum of room and reverb level controls
    // needs to subtract max levels for both room level and reverb level
    int combinedLevel = (roomLevel + mLevel) - lvm::kMaxReverbLevel;
    params.Level = convertLevel(combinedLevel);

    RETURN_VALUE_IF(LVREV_SUCCESS != LVREV_SetControlParameters(mInstance, &params),
                    RetCode::ERROR_EFFECT_LIB_ERROR, " setControlParamFailed");
    mRoomLevel = roomLevel;
    return RetCode::SUCCESS;
}

RetCode ReverbContext::setEnvironmentalReverbRoomHfLevel(int roomHfLevel) {
    // Update Control Parameter
    LVREV_ControlParams_st params;
    RETURN_VALUE_IF(LVREV_SUCCESS != LVREV_GetControlParameters(mInstance, &params),
                    RetCode::ERROR_EFFECT_LIB_ERROR, " getControlParamFailed");

    params.LPF = convertHfLevel(roomHfLevel);

    RETURN_VALUE_IF(LVREV_SUCCESS != LVREV_SetControlParameters(mInstance, &params),
                    RetCode::ERROR_EFFECT_LIB_ERROR, " setControlParamFailed");
    mRoomHfLevel = roomHfLevel;
    return RetCode::SUCCESS;
}

RetCode ReverbContext::setEnvironmentalReverbDecayTime(int decayTime) {
    int time = decayTime;
    if (time > lvm::kMaxT60) {
        time = lvm::kMaxT60;
    }

    // Update Control Parameter
    LVREV_ControlParams_st params;
    RETURN_VALUE_IF(LVREV_SUCCESS != LVREV_GetControlParameters(mInstance, &params),
                    RetCode::ERROR_EFFECT_LIB_ERROR, " getControlParamFailed");

    params.T60 = (LVM_UINT16)time;
    mSamplesToExitCount = (params.T60 * mCommon.input.base.sampleRate) / 1000;

    RETURN_VALUE_IF(LVREV_SUCCESS != LVREV_SetControlParameters(mInstance, &params),
                    RetCode::ERROR_EFFECT_LIB_ERROR, " setControlParamFailed");

    mDecayTime = time;
    return RetCode::SUCCESS;
}

RetCode ReverbContext::setEnvironmentalReverbDecayHfRatio(int decayHfRatio) {
    // Update Control Parameter
    LVREV_ControlParams_st params;
    RETURN_VALUE_IF(LVREV_SUCCESS != LVREV_GetControlParameters(mInstance, &params),
                    RetCode::ERROR_EFFECT_LIB_ERROR, " getControlParamFailed");

    params.Damping = (LVM_INT16)(decayHfRatio / 20);

    RETURN_VALUE_IF(LVREV_SUCCESS != LVREV_SetControlParameters(mInstance, &params),
                    RetCode::ERROR_EFFECT_LIB_ERROR, " setControlParamFailed");
    mDecayHfRatio = decayHfRatio;
    return RetCode::SUCCESS;
}

RetCode ReverbContext::setEnvironmentalReverbLevel(int level) {
    // Update Control Parameter
    LVREV_ControlParams_st params;
    RETURN_VALUE_IF(LVREV_SUCCESS != LVREV_GetControlParameters(mInstance, &params),
                    RetCode::ERROR_EFFECT_LIB_ERROR, " getControlParamFailed");

    // Sum of room and reverb level controls
    // needs to subtract max levels for both room level and level
    int combinedLevel = (level + mRoomLevel) - lvm::kMaxReverbLevel;
    params.Level = convertLevel(combinedLevel);

    RETURN_VALUE_IF(LVREV_SUCCESS != LVREV_SetControlParameters(mInstance, &params),
                    RetCode::ERROR_EFFECT_LIB_ERROR, " setControlParamFailed");

    mLevel = level;
    return RetCode::SUCCESS;
}

RetCode ReverbContext::setEnvironmentalReverbDelay(int delay) {
    mDelay = delay;
    return RetCode::SUCCESS;
}

RetCode ReverbContext::setEnvironmentalReverbDiffusion(int diffusion) {
    // Update Control Parameter
    LVREV_ControlParams_st params;
    RETURN_VALUE_IF(LVREV_SUCCESS != LVREV_GetControlParameters(mInstance, &params),
                    RetCode::ERROR_EFFECT_LIB_ERROR, " getControlParamFailed");

    params.Density = (LVM_INT16)(diffusion / 10);

    RETURN_VALUE_IF(LVREV_SUCCESS != LVREV_SetControlParameters(mInstance, &params),
                    RetCode::ERROR_EFFECT_LIB_ERROR, " setControlParamFailed");

    mDiffusion = diffusion;
    return RetCode::SUCCESS;
}

RetCode ReverbContext::setEnvironmentalReverbDensity(int density) {
    // Update Control Parameter
    LVREV_ControlParams_st params;
    RETURN_VALUE_IF(LVREV_SUCCESS != LVREV_GetControlParameters(mInstance, &params),
                    RetCode::ERROR_EFFECT_LIB_ERROR, " getControlParamFailed");

    params.RoomSize = (LVM_INT16)(((density * 99) / 1000) + 1);

    RETURN_VALUE_IF(LVREV_SUCCESS != LVREV_SetControlParameters(mInstance, &params),
                    RetCode::ERROR_EFFECT_LIB_ERROR, " setControlParamFailed");

    mDensity = density;
    return RetCode::SUCCESS;
}

RetCode ReverbContext::setEnvironmentalReverbBypass(bool bypass) {
    mBypass = bypass;
    return RetCode::SUCCESS;
}

void ReverbContext::loadPreset() {
    // TODO: add delay when early reflections are implemented
    mPreset = mNextPreset;

    if (mPreset != PresetReverb::Presets::NONE) {
        const t_reverb_settings preset = mReverbPresets[mPreset];
        setEnvironmentalReverbRoomLevel(preset.roomLevel);
        setEnvironmentalReverbRoomHfLevel(preset.roomHFLevel);
        setEnvironmentalReverbDecayTime(preset.decayTime);
        setEnvironmentalReverbDecayHfRatio(preset.decayHFRatio);
        setEnvironmentalReverbLevel(preset.reverbLevel);
        // reverbDelay
        setEnvironmentalReverbDiffusion(preset.diffusion);
        setEnvironmentalReverbDensity(preset.density);
    }
}

void ReverbContext::initControlParameter(LVREV_ControlParams_st& params) {
    /* Set the initial process parameters */
    /* General parameters */
    params.OperatingMode = LVM_MODE_ON;
    params.SampleRate = LVM_FS_44100;
    params.SourceFormat = (::aidl::android::hardware::audio::common::getChannelCount(
                                   mCommon.input.base.channelMask) == 1
                                   ? LVM_MONO
                                   : LVM_STEREO);

    if (!isAuxiliary() && params.SourceFormat == LVM_MONO) {
        params.SourceFormat = LVM_STEREO;
    }

    /* Reverb parameters */
    params.Level = kDefaultLevel;
    params.LPF = kDefaultLPF;
    params.HPF = kDefaultHPF;
    params.T60 = kDefaultDecayTime;
    params.Density = kDefaultDensity;
    params.Damping = kDefaultDamping;
    params.RoomSize = kDefaultRoomSize;
}

/*
 * Convert level from OpenSL ES format to LVM format
 *
 *  @param level : level to be applied
 */

int ReverbContext::convertLevel(int level) {
    for (std::size_t i = 0; i < kLevelMapping.size(); i++) {
        if (level <= kLevelMapping[i]) {
            return i;
        }
    }
    return kDefaultLevel;
}

/*
 * Convert level HF from OpenSL ES format to LVM format
 *
 * @param hfLevel : level to be applied
 */

int16_t ReverbContext::convertHfLevel(int hfLevel) {
    for (auto lpfPair : kLPFMapping) {
        if (hfLevel <= lpfPair.roomHf) {
            return lpfPair.lpf;
        }
    }
    return kDefaultLPF;
}

IEffect::Status ReverbContext::process(float* in, float* out, int samples) {
    IEffect::Status status = {EX_NULL_POINTER, 0, 0};
    RETURN_VALUE_IF(!in, status, "nullInput");
    RETURN_VALUE_IF(!out, status, "nullOutput");
    status = {EX_ILLEGAL_STATE, 0, 0};
    int64_t inputFrameCount = getCommon().input.frameCount;
    int64_t outputFrameCount = getCommon().output.frameCount;
    RETURN_VALUE_IF(inputFrameCount != outputFrameCount, status, "FrameCountMismatch");
    RETURN_VALUE_IF(0 == getInputFrameSize(), status, "zeroFrameSize");

    int channels = ::aidl::android::hardware::audio::common::getChannelCount(
            mCommon.input.base.channelMask);
    int outChannels = ::aidl::android::hardware::audio::common::getChannelCount(
            mCommon.output.base.channelMask);
    int frameCount = mCommon.input.frameCount;

    if (mBypass) {
        if (isAuxiliary()) {
            memset(out, 0, getOutputFrameSize() * frameCount);
        } else {
            memcpy_to_float_from_float_with_clamping(out, in, samples, 1);
        }
        return {STATUS_OK, samples, outChannels * frameCount};
    }

    // Reverb only effects the stereo channels in multichannel source.
    if (channels < 1 || channels > LVM_MAX_CHANNELS) {
        LOG(ERROR) << __func__ << " process invalid PCM channels " << channels;
        return status;
    }

    std::vector<float> inputSamples;
    std::vector<float> outputSamples(frameCount * FCC_2);

    if (isPreset() && mNextPreset != mPreset) {
        loadPreset();
    }

    if (isAuxiliary()) {
        inputSamples.resize(samples);
        inputSamples.assign(in, in + samples);
    } else {
        // Resizing to stereo is required to duplicate mono input
        inputSamples.resize(frameCount * FCC_2);
        if (channels >= FCC_2) {
            for (int i = 0; i < frameCount; i++) {
                inputSamples[FCC_2 * i] = in[channels * i] * kSendLevel;
                inputSamples[FCC_2 * i + 1] = in[channels * i + 1] * kSendLevel;
            }
        } else {
            for (int i = 0; i < frameCount; i++) {
                inputSamples[FCC_2 * i] = inputSamples[FCC_2 * i + 1] = in[i] * kSendLevel;
            }
        }
    }

    if (isPreset() && mPreset == PresetReverb::Presets::NONE) {
        std::fill(outputSamples.begin(), outputSamples.end(), 0);  // always stereo here
    } else {
        if (!mEnabled && mSamplesToExitCount > 0) {
            std::fill(outputSamples.begin(), outputSamples.end(), 0);
        }
        int inputBufferIndex = 0;
        int outputBufferIndex = 0;

        // LVREV library supports max of int16_t frames at a time
        constexpr int kMaxBlockFrames = std::numeric_limits<int16_t>::max();
        const auto inputFrameSize = getInputFrameSize();
        const auto outputFrameSize = getOutputFrameSize();

        /* Process the samples, producing a stereo output */
        for (int fc = frameCount; fc > 0;) {
            int processFrames = std::min(fc, kMaxBlockFrames);
            LVREV_ReturnStatus_en lvrevStatus =
                    LVREV_Process(mInstance,                            /* Instance handle */
                                  inputSamples.data() + inputBufferIndex,   /* Input buffer */
                                  outputSamples.data() + outputBufferIndex, /* Output buffer */
                                  processFrames); /* Number of samples to process */
            if (lvrevStatus != LVREV_SUCCESS) {
                LOG(ERROR) << __func__ << " LVREV_Process error: " << lvrevStatus;
                return {EX_UNSUPPORTED_OPERATION, 0, 0};
            }

            fc -= processFrames;

            inputBufferIndex += processFrames * inputFrameSize / sizeof(float);
            outputBufferIndex += processFrames * outputFrameSize / sizeof(float);
        }
    }
    // Convert to 16 bits
    if (isAuxiliary()) {
        // nothing to do here
    } else {
        if (channels >= FCC_2) {
            for (int i = 0; i < frameCount; i++) {
                // Mix with dry input
                outputSamples[FCC_2 * i] += in[channels * i];
                outputSamples[FCC_2 * i + 1] += in[channels * i + 1];
            }
        } else {
            for (int i = 0; i < frameCount; i++) {
                // Mix with dry input
                outputSamples[FCC_2 * i] += in[i];
                outputSamples[FCC_2 * i + 1] += in[i];
            }
        }

        // apply volume with ramp if needed
        if (mVolume != mPrevVolume && volumeMode == VOLUME_RAMP) {
            float vl = mPrevVolume.left;
            float incl = (mVolume.left - vl) / frameCount;
            float vr = mPrevVolume.right;
            float incr = (mVolume.right - vr) / frameCount;

            for (int i = 0; i < frameCount; i++) {
                outputSamples[FCC_2 * i] *= vl;
                outputSamples[FCC_2 * i + 1] *= vr;

                vl += incl;
                vr += incr;
            }
            mPrevVolume = mVolume;
        } else if (volumeMode != VOLUME_OFF) {
            if (mVolume.left != kUnitVolume || mVolume.right != kUnitVolume) {
                for (int i = 0; i < frameCount; i++) {
                    outputSamples[FCC_2 * i] *= mVolume.left;
                    outputSamples[FCC_2 * i + 1] *= mVolume.right;
                }
            }
            mPrevVolume = mVolume;
            volumeMode = VOLUME_RAMP;
        }
    }

    if (outChannels > 2) {
        for (int i = 0; i < frameCount; i++) {
            out[outChannels * i] = outputSamples[FCC_2 * i];
            out[outChannels * i + 1] = outputSamples[FCC_2 * i + 1];
        }
        if (!isAuxiliary()) {
            for (int i = 0; i < frameCount; i++) {
                // channels and outChannels are expected to be same.
                for (int j = FCC_2; j < outChannels; j++) {
                    out[outChannels * i + j] = in[outChannels * i + j];
                }
            }
        }
    } else {
        if (outChannels == FCC_1) {
            From2iToMono_Float(outputSamples.data(), out, frameCount);
        } else {
            for (int i = 0; i < frameCount * FCC_2; i++) {
                out[i] = outputSamples[i];
            }
        }
    }

    if (!mEnabled && mSamplesToExitCount > 0) {
        // signed - unsigned will trigger integer overflow if result becomes negative.
        mSamplesToExitCount -= samples;
    }

    return {STATUS_OK, samples, outChannels * frameCount};
}

}  // namespace aidl::android::hardware::audio::effect
