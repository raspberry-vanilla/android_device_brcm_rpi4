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

#include <android-base/logging.h>
#include <unordered_map>

#include "ReverbTypes.h"
#include "effect-impl/EffectContext.h"

namespace aidl::android::hardware::audio::effect {

enum VolumeMode {
    VOLUME_OFF,
    VOLUME_FLAT,
    VOLUME_RAMP,
};

struct LPFPair {
    int roomHf;
    int lpf;
};

class ReverbContext final : public EffectContext {
  public:
    ReverbContext(int statusDepth, const Parameter::Common& common,
                  const lvm::ReverbEffectType& type)
        : EffectContext(statusDepth, common), mType(type) {
        LOG(DEBUG) << __func__ << type;
        init();
    }
    ~ReverbContext() override {
        LOG(DEBUG) << __func__;
        deInit();
    }

    RetCode init();
    void deInit();

    RetCode enable() override;
    RetCode disable() override;

    bool isAuxiliary();
    bool isPreset();

    RetCode setPresetReverbPreset(const PresetReverb::Presets& preset);
    PresetReverb::Presets getPresetReverbPreset() const { return mNextPreset; }

    RetCode setEnvironmentalReverbRoomLevel(int roomLevel);
    int getEnvironmentalReverbRoomLevel() const { return mRoomLevel; }
    RetCode setEnvironmentalReverbRoomHfLevel(int roomHfLevel);
    int getEnvironmentalReverbRoomHfLevel() const { return mRoomHfLevel; }
    RetCode setEnvironmentalReverbDecayTime(int decayTime);
    int getEnvironmentalReverbDecayTime() const { return mDecayTime; }
    RetCode setEnvironmentalReverbDecayHfRatio(int decayHfRatio);
    int getEnvironmentalReverbDecayHfRatio() const { return mDecayHfRatio; }
    RetCode setEnvironmentalReverbLevel(int level);
    int getEnvironmentalReverbLevel() const { return mLevel; }
    RetCode setEnvironmentalReverbDelay(int delay);
    int getEnvironmentalReverbDelay() const { return mDelay; }
    RetCode setEnvironmentalReverbDiffusion(int diffusion);
    int getEnvironmentalReverbDiffusion() const { return mDiffusion; }
    RetCode setEnvironmentalReverbDensity(int density);
    int getEnvironmentalReverbDensity() const { return mDensity; }
    RetCode setEnvironmentalReverbBypass(bool bypass);
    bool getEnvironmentalReverbBypass() const { return mBypass; }

    RetCode setVolumeStereo(const Parameter::VolumeStereo& volumeStereo) override;
    Parameter::VolumeStereo getVolumeStereo() override {
        if (isAuxiliary()) {
            return mVolumeStereo;
        }
        return {1.0f, 1.0f};
    }

    RetCode setReflectionsDelay(int delay) {
        mReflectionsDelayMs = delay;
        return RetCode::SUCCESS;
    }
    bool getReflectionsDelay() const { return mReflectionsDelayMs; }

    RetCode setReflectionsLevel(int level) {
        mReflectionsLevelMb = level;
        return RetCode::SUCCESS;
    }
    bool getReflectionsLevel() const { return mReflectionsLevelMb; }

    IEffect::Status process(float* in, float* out, int samples);

  private:
    static constexpr inline float kUnitVolume = 1;
    static constexpr inline float kSendLevel = 0.75f;
    static constexpr inline int kDefaultLevel = 0;
    static constexpr inline int kDefaultLPF = 23999;      /* Default low pass filter, in Hz */
    static constexpr inline int kDefaultHPF = 50;         /* Default high pass filter, in Hz */
    static constexpr inline int kDefaultDecayTime = 1490; /* Default Decay time, in ms */
    static constexpr inline int kDefaultDensity = 100;    /* Default Echo density */
    static constexpr inline int kDefaultDamping = 21;
    static constexpr inline int kDefaultRoomSize = 100;

    static inline const std::vector<LPFPair> kLPFMapping = {
            // Limit range to 50 for LVREV parameter range
            {-10000, 50}, {-5000, 50},  {-4000, 50},  {-3000, 158}, {-2000, 502}, {-1000, 1666},
            {-900, 1897}, {-800, 2169}, {-700, 2496}, {-600, 2895}, {-500, 3400}, {-400, 4066},
            {-300, 5011}, {-200, 6537}, {-100, 9826}, {-99, 9881},  {-98, 9937},  {-97, 9994},
            {-96, 10052}, {-95, 10111}, {-94, 10171}, {-93, 10231}, {-92, 10293}, {-91, 10356},
            {-90, 10419}, {-89, 10484}, {-88, 10549}, {-87, 10616}, {-86, 10684}, {-85, 10753},
            {-84, 10823}, {-83, 10895}, {-82, 10968}, {-81, 11042}, {-80, 11117}, {-79, 11194},
            {-78, 11272}, {-77, 11352}, {-76, 11433}, {-75, 11516}, {-74, 11600}, {-73, 11686},
            {-72, 11774}, {-71, 11864}, {-70, 11955}, {-69, 12049}, {-68, 12144}, {-67, 12242},
            {-66, 12341}, {-65, 12443}, {-64, 12548}, {-63, 12654}, {-62, 12763}, {-61, 12875},
            {-60, 12990}, {-59, 13107}, {-58, 13227}, {-57, 13351}, {-56, 13477}, {-55, 13607},
            {-54, 13741}, {-53, 13878}, {-52, 14019}, {-51, 14164}, {-50, 14313}, {-49, 14467},
            {-48, 14626}, {-47, 14789}, {-46, 14958}, {-45, 15132}, {-44, 15312}, {-43, 15498},
            {-42, 15691}, {-41, 15890}, {-40, 16097}, {-39, 16311}, {-38, 16534}, {-37, 16766},
            {-36, 17007}, {-35, 17259}, {-34, 17521}, {-33, 17795}, {-32, 18081}, {-31, 18381},
            {-30, 18696}, {-29, 19027}, {-28, 19375}, {-27, 19742}, {-26, 20129}, {-25, 20540},
            {-24, 20976}, {-23, 21439}, {-22, 21934}, {-21, 22463}, {-20, 23031}, {-19, 23643},
            {-18, 23999}};

    static inline const std::vector<int> kLevelMapping = {
            -12000, -4000, -3398, -3046, -2796, -2603, -2444, -2310, -2194, -2092, -2000, -1918,
            -1842,  -1773, -1708, -1648, -1592, -1540, -1490, -1443, -1398, -1356, -1316, -1277,
            -1240,  -1205, -1171, -1138, -1106, -1076, -1046, -1018, -990,  -963,  -938,  -912,
            -888,   -864,  -841,  -818,  -796,  -775,  -754,  -734,  -714,  -694,  -675,  -656,
            -638,   -620,  -603,  -585,  -568,  -552,  -536,  -520,  -504,  -489,  -474,  -459,
            -444,   -430,  -416,  -402,  -388,  -375,  -361,  -348,  -335,  -323,  -310,  -298,
            -286,   -274,  -262,  -250,  -239,  -228,  -216,  -205,  -194,  -184,  -173,  -162,
            -152,   -142,  -132,  -121,  -112,  -102,  -92,   -82,   -73,   -64,   -54,   -45,
            -36,    -27,   -18,   -9,    0};

    static inline std::unordered_map<PresetReverb::Presets, t_reverb_settings> mReverbPresets = {
            {PresetReverb::Presets::NONE, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
            {PresetReverb::Presets::SMALLROOM,
             {-400, -600, 1100, 830, -400, 5, 500, 10, 1000, 1000}},
            {PresetReverb::Presets::MEDIUMROOM,
             {-400, -600, 1300, 830, -1000, 20, -200, 20, 1000, 1000}},
            {PresetReverb::Presets::LARGEROOM,
             {-400, -600, 1500, 830, -1600, 5, -1000, 40, 1000, 1000}},
            {PresetReverb::Presets::MEDIUMHALL,
             {-400, -600, 1800, 700, -1300, 15, -800, 30, 1000, 1000}},
            {PresetReverb::Presets::LARGEHALL,
             {-400, -600, 1800, 700, -2000, 30, -1400, 60, 1000, 1000}},
            {PresetReverb::Presets::PLATE, {-400, -200, 1300, 900, 0, 2, 0, 10, 1000, 750}}};

    const lvm::ReverbEffectType mType;
    bool mEnabled = false;
    LVREV_Handle_t mInstance = LVM_NULL;

    int mRoomLevel = 0;
    int mRoomHfLevel = 0;
    int mDecayTime = 0;
    int mDecayHfRatio = 0;
    int mLevel = 0;
    int mDelay = 0;
    int mDiffusion = 0;
    int mDensity = 0;
    bool mBypass = 0;
    int mReflectionsLevelMb = 0;
    int mReflectionsDelayMs = 0;

    PresetReverb::Presets mPreset;
    PresetReverb::Presets mNextPreset;

    int mSamplesToExitCount;

    Parameter::VolumeStereo mVolume;
    Parameter::VolumeStereo mPrevVolume;
    VolumeMode volumeMode;

    void initControlParameter(LVREV_ControlParams_st& params);
    int16_t convertHfLevel(int hfLevel);
    int convertLevel(int level);
    void loadPreset();
};

}  // namespace aidl::android::hardware::audio::effect
