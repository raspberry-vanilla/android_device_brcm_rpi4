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

#include <android-base/logging.h>
#include <array>
#include <cstddef>

#include "BundleTypes.h"
#include "effect-impl/EffectContext.h"

namespace aidl::android::hardware::audio::effect {

class BundleContext final : public EffectContext {
  public:
    BundleContext(int statusDepth, const Parameter::Common& common,
                  const lvm::BundleEffectType& type);
    ~BundleContext();

    RetCode init();
    void deInit();
    lvm::BundleEffectType getBundleType() const { return mType; }

    RetCode setCommon(const Parameter::Common& common) override;

    RetCode enable() override;
    RetCode enableOperatingMode();
    RetCode disable() override;
    RetCode disableOperatingMode();

    bool isDeviceSupportedBassBoost(
            const std::vector<aidl::android::media::audio::common::AudioDeviceDescription>&
                    devices);
    bool isDeviceSupportedVirtualizer(
            const std::vector<aidl::android::media::audio::common::AudioDeviceDescription>&
                    devices);
    bool isConfigSupportedVirtualizer(
            size_t channelCount,
            const aidl::android::media::audio::common::AudioDeviceDescription& device);

    RetCode setOutputDevice(
            const std::vector<aidl::android::media::audio::common::AudioDeviceDescription>& devices)
            override;

    RetCode setEqualizerPreset(const std::size_t presetIdx);
    int getEqualizerPreset() const { return mCurPresetIdx; }
    RetCode setEqualizerBandLevels(const std::vector<Equalizer::BandLevel>& bandLevels);
    std::vector<Equalizer::BandLevel> getEqualizerBandLevels() const;

    std::vector<int32_t> getEqualizerCenterFreqs();

    RetCode setBassBoostStrength(int strength);
    int getBassBoostStrength() const { return mBassStrengthSaved; }

    RetCode setVolumeLevel(float level);
    float getVolumeLevel() const;

    RetCode setVolumeMute(bool mute);
    int getVolumeMute() const { return mMuteEnabled; }

    RetCode setVirtualizerStrength(int strength);
    int getVirtualizerStrength() const { return mVirtStrengthSaved; }

    RetCode setForcedDevice(
            const ::aidl::android::media::audio::common::AudioDeviceDescription& device);
    aidl::android::media::audio::common::AudioDeviceDescription getForcedDevice() const {
        return mVirtualizerForcedDevice;
    }
    std::vector<Virtualizer::ChannelAngle> getSpeakerAngles(
            const Virtualizer::SpeakerAnglesPayload payload);

    RetCode setVolumeStereo(const Parameter::VolumeStereo& volumeStereo) override;
    Parameter::VolumeStereo getVolumeStereo() override { return {1.0f, 1.0f}; }

    IEffect::Status process(float* in, float* out, int samples);

    IEffect::Status processEffect(float* in, float* out, int sampleToProcess);

  private:
    const lvm::BundleEffectType mType;
    bool mEnabled = false;
    LVM_Handle_t mInstance;

    int mSamplesPerSecond = 0;
    int mSamplesToExitCountEq = 0;
    int mSamplesToExitCountBb = 0;
    int mSamplesToExitCountVirt = 0;
    int mFrameCount = 0;

    /* Bitmask whether drain is in progress due to disabling the effect.
       The corresponding bit to an effect is set by 1 << lvm_effect_en. */
    int mEffectInDrain = 0;

    /* Bitmask whether process() was called for a particular effect.
       The corresponding bit to an effect is set by 1 << lvm_effect_en. */
    int mEffectProcessCalled = 0;
    int mNumberEffectsEnabled = 0;
    int mNumberEffectsCalled = 0;
    bool mFirstVolume = true;
    // Bass
    bool mBassTempDisabled = false;
    int mBassStrengthSaved = 0;
    // Equalizer
    int mCurPresetIdx = lvm::PRESET_CUSTOM; /* Current preset being used */
    std::array<int, lvm::MAX_NUM_BANDS> mBandGainmB; /* band gain in millibels */
    // Virtualizer
    int mVirtStrengthSaved = 0; /* Conversion between Get/Set */
    bool mVirtualizerTempDisabled = false;
    ::aidl::android::media::audio::common::AudioDeviceDescription mVirtualizerForcedDevice;
    // Volume
    float mLevelSaveddB = 0; /* for when mute is set, level must be saved */
    float mVolumedB = 0;
    bool mMuteEnabled = false; /* Must store as mute = -96dB level */

    RetCode initControlParameter(LVM_ControlParams_t& params) const;
    void initHeadroomParameter(LVM_HeadroomParams_t& params) const;
    RetCode limitLevel();
    static float VolToDb(float vol);
    LVM_INT16 LVC_ToDB_s32Tos16(LVM_INT32 Lin_fix) const;
    RetCode updateControlParameter(const std::vector<Equalizer::BandLevel>& bandLevels);
    bool isBandLevelIndexInRange(const std::vector<Equalizer::BandLevel>& bandLevels) const;
    static LVM_EQNB_BandDef_t* getDefaultEqualizerBandDefs();
    static LVM_HeadroomBandDef_t* getDefaultEqualizerHeadroomBanDefs();
    RetCode applyCommonParameter(LVM_ControlParams_t& params) const;
};

}  // namespace aidl::android::hardware::audio::effect

