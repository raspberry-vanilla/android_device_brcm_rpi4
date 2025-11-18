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
#include <array>

#include <aidl/android/hardware/audio/effect/BnEffect.h>
#include <system/audio_effects/effect_uuid.h>

#include "effect-impl/EffectTypes.h"
#include "LVM.h"

namespace aidl::android::hardware::audio::effect {
namespace lvm {

constexpr inline size_t MAX_NUM_PRESETS = 10;
constexpr inline size_t MAX_NUM_BANDS = 5;
constexpr inline size_t MAX_CALL_SIZE = 256;
constexpr inline int BASS_BOOST_CUP_LOAD_ARM9E = 150;   // Expressed in 0.1 MIPS
constexpr inline int VIRTUALIZER_CUP_LOAD_ARM9E = 120;  // Expressed in 0.1 MIPS
constexpr inline int EQUALIZER_CUP_LOAD_ARM9E = 220;    // Expressed in 0.1 MIPS
constexpr inline int VOLUME_CUP_LOAD_ARM9E = 0;         // Expressed in 0.1 MIPS
constexpr inline int BUNDLE_MEM_USAGE = 25;             // Expressed in kB
constexpr inline int PRESET_CUSTOM = -1;

static const std::vector<Equalizer::BandFrequency> kEqBandFrequency = {{0, 30000, 120000},
                                                                       {1, 120001, 460000},
                                                                       {2, 460001, 1800000},
                                                                       {3, 1800001, 7000000},
                                                                       {4, 7000001, 20000000}};

/*
Frequencies in Hz
Note: If these frequencies change, please update LimitLevel values accordingly.
*/
constexpr inline std::array<uint16_t, MAX_NUM_BANDS> kPresetsFrequencies = {60, 230, 910, 3600,
                                                                            14000};

/* Q factor multiplied by 100 */
constexpr inline std::array<uint16_t, MAX_NUM_BANDS> kPresetsQFactors = {96, 96, 96, 96, 96};

constexpr inline std::array<std::array<int16_t, MAX_NUM_BANDS>, MAX_NUM_PRESETS> kSoftPresets = {
        {{3, 0, 0, 0, 3},    /* Normal Preset */
         {5, 3, -2, 4, 4},   /* Classical Preset */
         {6, 0, 2, 4, 1},    /* Dance Preset */
         {0, 0, 0, 0, 0},    /* Flat Preset */
         {3, 0, 0, 2, -1},   /* Folk Preset */
         {4, 1, 9, 3, 0},    /* Heavy Metal Preset */
         {5, 3, 0, 1, 3},    /* Hip Hop Preset */
         {4, 2, -2, 2, 5},   /* Jazz Preset */
         {-1, 2, 5, 1, -2},  /* Pop Preset */
         {5, 3, -1, 3, 5}}}; /* Rock Preset */

static const std::vector<Equalizer::Preset> kEqPresets = {
        {-1, "Custom"}, {0, "Normal"}, {1, "Classical"},   {2, "Dance"},
        {3, "Flat"},    {4, "Folk"},   {5, "Heavy Metal"}, {6, "Hip Hop"},
        {7, "Jazz"},    {8, "Pop"},    {9, "Rock"}};

const std::vector<Range::EqualizerRange> kEqRanges = {
        MAKE_RANGE(Equalizer, preset, 0, MAX_NUM_PRESETS - 1),
        MAKE_RANGE(Equalizer, bandLevels,
                   std::vector<Equalizer::BandLevel>{
                           Equalizer::BandLevel({.index = 0, .levelMb = -1500})},
                   std::vector<Equalizer::BandLevel>{
                           Equalizer::BandLevel({.index = MAX_NUM_BANDS - 1, .levelMb = 1500})}),
        /* capability definition */
        MAKE_RANGE(Equalizer, bandFrequencies, kEqBandFrequency, kEqBandFrequency),
        MAKE_RANGE(Equalizer, presets, kEqPresets, kEqPresets),
        /* get only parameters with range min > max */
        MAKE_RANGE(Equalizer, centerFreqMh, std::vector<int>({1}), std::vector<int>({}))};
static const Capability kEqCap = {.range = kEqRanges};
static const std::string kEqualizerEffectName = "EqualizerBundle";
static const Descriptor kEqualizerDesc = {
        .common = {.id = {.type = getEffectTypeUuidEqualizer(),
                          .uuid = getEffectImplUuidEqualizerBundle()},
                   .flags = {.type = Flags::Type::INSERT,
                             .insert = Flags::Insert::FIRST,
                             .volume = Flags::Volume::CTRL},
                   .name = kEqualizerEffectName,
                   .implementor = "NXP Software Ltd."},
        .capability = kEqCap};

static const int mMaxStrengthSupported = 1000;
static const std::vector<Range::BassBoostRange> kBassBoostRanges = {
        MAKE_RANGE(BassBoost, strengthPm, 0, mMaxStrengthSupported)};
static const Capability kBassBoostCap = {.range = kBassBoostRanges};
static const std::string kBassBoostEffectName = "Dynamic Bass Boost";
static const Descriptor kBassBoostDesc = {
        .common = {.id = {.type = getEffectTypeUuidBassBoost(),
                          .uuid = getEffectImplUuidBassBoostBundle()},
                   .flags = {.type = Flags::Type::INSERT,
                             .insert = Flags::Insert::FIRST,
                             .volume = Flags::Volume::CTRL,
                             .deviceIndication = true},
                   .cpuLoad = BASS_BOOST_CUP_LOAD_ARM9E,
                   .memoryUsage = BUNDLE_MEM_USAGE,
                   .name = kBassBoostEffectName,
                   .implementor = "NXP Software Ltd."},
        .capability = kBassBoostCap};

static const std::vector<Range::VirtualizerRange> kVirtualizerRanges = {
        MAKE_RANGE(Virtualizer, strengthPm, 0, mMaxStrengthSupported)};
static const Capability kVirtualizerCap = {.range = kVirtualizerRanges};
static const std::string kVirtualizerEffectName = "Virtualizer";

static const Descriptor kVirtualizerDesc = {
        .common = {.id = {.type = getEffectTypeUuidVirtualizer(),
                          .uuid = getEffectImplUuidVirtualizerBundle()},
                   .flags = {.type = Flags::Type::INSERT,
                             .insert = Flags::Insert::LAST,
                             .volume = Flags::Volume::CTRL,
                             .deviceIndication = true},
                   .cpuLoad = VIRTUALIZER_CUP_LOAD_ARM9E,
                   .memoryUsage = BUNDLE_MEM_USAGE,
                   .name = kVirtualizerEffectName,
                   .implementor = "NXP Software Ltd."},
        .capability = kVirtualizerCap};

static const std::vector<Range::VolumeRange> kVolumeRanges = {MAKE_RANGE(Volume, levelDb, -96, 0)};
static const Capability kVolumeCap = {.range = kVolumeRanges};
static const std::string kVolumeEffectName = "Volume";
static const Descriptor kVolumeDesc = {
        .common = {.id = {.type = getEffectTypeUuidVolume(),
                          .uuid = getEffectImplUuidVolumeBundle()},
                   .flags = {.type = Flags::Type::INSERT,
                             .insert = Flags::Insert::LAST,
                             .volume = Flags::Volume::CTRL},
                   .cpuLoad = VOLUME_CUP_LOAD_ARM9E,
                   .memoryUsage = BUNDLE_MEM_USAGE,
                   .name = kVolumeEffectName,
                   .implementor = "NXP Software Ltd."},
        .capability = kVolumeCap};

/* The following tables have been computed using the actual levels measured by the output of
 * white noise or pink noise (IEC268-1) for the EQ and BassBoost Effects. These are estimates of
 * the actual energy that 'could' be present in the given band.
 * If the frequency values in EQNB_5BandPresetsFrequencies change, these values might need to be
 * updated.
 */
constexpr inline std::array<float, MAX_NUM_BANDS> kBandEnergyCoefficient = {7.56, 9.69, 9.59, 7.37,
                                                                            2.88};

constexpr inline std::array<float, MAX_NUM_BANDS - 1> kBandEnergyCrossCoefficient = {126.0, 115.0,
                                                                                     125.0, 104.0};

constexpr inline std::array<float, MAX_NUM_BANDS> kBassBoostEnergyCrossCoefficient = {
        221.21, 208.10, 28.16, 0.0, 0.0};

constexpr inline float kBassBoostEnergyCoefficient = 9.00;

constexpr inline float kVirtualizerContribution = 1.9;

enum class BundleEffectType {
    BASS_BOOST,
    VIRTUALIZER,
    EQUALIZER,
    VOLUME,
};

inline std::ostream& operator<<(std::ostream& out, const BundleEffectType& type) {
    switch (type) {
        case BundleEffectType::BASS_BOOST:
            return out << "BASS_BOOST";
        case BundleEffectType::VIRTUALIZER:
            return out << "VIRTUALIZER";
        case BundleEffectType::EQUALIZER:
            return out << "EQUALIZER";
        case BundleEffectType::VOLUME:
            return out << "VOLUME";
    }
    return out << "EnumBundleEffectTypeError";
}

inline std::ostream& operator<<(std::ostream& out, const LVM_ReturnStatus_en& status) {
    switch (status) {
        case LVM_SUCCESS:
            return out << "LVM_SUCCESS";
        case LVM_ALIGNMENTERROR:
            return out << "LVM_ALIGNMENTERROR";
        case LVM_NULLADDRESS:
            return out << "LVM_NULLADDRESS";
        case LVM_OUTOFRANGE:
            return out << "LVM_OUTOFRANGE";
        case LVM_INVALIDNUMSAMPLES:
            return out << "LVM_INVALIDNUMSAMPLES";
        case LVM_WRONGAUDIOTIME:
            return out << "LVM_WRONGAUDIOTIME";
        case LVM_ALGORITHMDISABLED:
            return out << "LVM_ALGORITHMDISABLED";
        case LVM_ALGORITHMPSA:
            return out << "LVM_ALGORITHMPSA";
        case LVM_RETURNSTATUS_DUMMY:
            return out << "LVM_RETURNSTATUS_DUMMY";
    }
    return out << "EnumLvmRetStatusError";
}

#define GOTO_IF_LVM_ERROR(status, tag, log)                                       \
    do {                                                                          \
        LVM_ReturnStatus_en temp = (status);                                      \
        if (temp != LVM_SUCCESS) {                                                \
            LOG(ERROR) << __func__ << " return status: " << temp << " " << (log); \
            goto tag;                                                             \
        }                                                                         \
    } while (0)

}  // namespace lvm
}  // namespace aidl::android::hardware::audio::effect
