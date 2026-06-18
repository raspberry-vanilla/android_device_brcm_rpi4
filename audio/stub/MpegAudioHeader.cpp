/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "MpegAudioHeader.h"
#define LOG_TAG "AHAL_OffloadStream"
#include <android-base/logging.h>
#include <log/log.h>
#include <climits>

namespace aidl::android::hardware::audio::core {

enum class Version { MPEG_2_5 = 0, RESERVED = 1, MPEG_2 = 2, MPEG_1 = 3 };

enum class Layer { RESERVED = 0, LAYER_3 = 1, LAYER_2 = 2, LAYER_1 = 3 };

// Bitrates in kilo bits per second
const int kBitrateTable[16][5] = {
        // V1,L1, V1,L2, V1,L3, V2,L1, V2,L2&L3
        {0, 0, 0, 0, 0},  // Invalid
        {32, 32, 32, 32, 8},       {64, 48, 40, 48, 16},      {96, 56, 48, 56, 24},
        {128, 64, 56, 64, 32},     {160, 80, 64, 80, 40},     {192, 96, 80, 96, 48},
        {224, 112, 96, 112, 56},   {256, 128, 112, 128, 64},  {288, 160, 128, 144, 80},
        {320, 192, 160, 160, 96},  {352, 224, 192, 176, 112}, {384, 256, 224, 192, 128},
        {416, 320, 256, 224, 144}, {448, 384, 320, 256, 160}, {0, 0, 0, 0, 0}  // Invalid
};

const int kSampleRateTable[4][3] = {
        // MPEG1, MPEG2, MPEG2.5
        {44100, 22050, 11025},
        {48000, 24000, 12000},
        {32000, 16000, 8000},
        {0, 0, 0}  // Invalid
};

constexpr int kLayer1SamplesPerMpegFrame = 384;
constexpr int kLayer2SamplesPerMpegFrame = 1152;
constexpr int kID3v2TagHeaderSize = 10;
constexpr int kID3v1TagSize = 128;
constexpr int kID3TagIdentifierSize = 3;
constexpr int kFrameHeaderSize = 4;

constexpr uint8_t kSyncByte = 0xFF;
constexpr uint8_t kId3v2VersionByteMax = 0xFF;
constexpr uint8_t kId3v2SizeByteMax = 0x80;
constexpr uint8_t kSyncByteMask = 0xE0;

constexpr int kPaddingBitShift = 1;
constexpr int kLayerDescShift = 1;
constexpr int kSampleRateIndexShift = 2;
constexpr int kVersionIdShift = 3;
constexpr int kBitrateIndexShift = 4;
constexpr int kChannelIdxShift = 6;

constexpr uint8_t kPaddingBitMask = 0x01;
constexpr uint8_t kLayerDescMask = 0x03;
constexpr uint8_t kSampleRateIndexMask = 0x03;
constexpr uint8_t kVersionIdMask = 0x03;
constexpr uint8_t kBitrateIndexMask = 0x0F;
constexpr uint8_t kChannelIdxMask = 0x03;

constexpr int kMonoChannelIdx = 3;

bool findID3v1Tag(const uint8_t** currBuff, const uint8_t* endBuff, MpegFrame& frame) {
    frame.bytesPending = 0;
    if ((*currBuff) + kID3TagIdentifierSize > endBuff) {
        frame.bytesPending = (*currBuff + kID3TagIdentifierSize) - endBuff;
        return false;
    }
    // ID3v1 header must start with TAG
    if ((*currBuff) + kID3TagIdentifierSize <= endBuff) {
        if (std::memcmp(*currBuff, "TAG", kID3TagIdentifierSize) == 0) {
            frame.isID3v1 = true;
            if ((*currBuff) + kID3v1TagSize > endBuff) {
                frame.bytesPending = (*currBuff + kID3v1TagSize) - endBuff;
            }
            *currBuff = std::min(*currBuff + kID3v1TagSize, endBuff);
            return true;
        }
    }
    return false;
}

bool findID3v2Tag(const uint8_t** currBuff, const uint8_t* endBuff, MpegFrame& frame) {
    frame.bytesPending = 0;
    if ((*currBuff) + kID3v2TagHeaderSize > endBuff) {
        frame.bytesPending = (*currBuff + kID3v2TagHeaderSize) - endBuff;
        return false;
    }
    // An ID3v2 tag can be detected with the following pattern:
    // I D 3 yy yy xx zz zz zz zz
    // Where yy is less than $FF and zz is less than $80.
    if (std::memcmp(*currBuff, "ID3", kID3TagIdentifierSize) == 0 &&
        (*currBuff)[3] < kId3v2VersionByteMax /*yy*/ &&
        (*currBuff)[4] < kId3v2VersionByteMax /*yy*/ && (*currBuff)[6] < kId3v2SizeByteMax /*zz*/ &&
        (*currBuff)[7] < kId3v2SizeByteMax /*zz*/ && (*currBuff)[8] < kId3v2SizeByteMax /*zz*/ &&
        (*currBuff)[9] < kId3v2SizeByteMax) /*zz*/ {
        frame.isID3v2 = true;
        size_t dataSize = ((*currBuff)[6] << 21) | ((*currBuff)[7] << 14) | ((*currBuff)[8] << 7) |
                          (*currBuff)[9];
        size_t totalSize = dataSize + kID3v2TagHeaderSize;
        if (*currBuff + totalSize > endBuff) {
            frame.bytesPending = (*currBuff + totalSize) - endBuff;
        }
        *currBuff = std::min(*currBuff + totalSize, endBuff);
        return true;
    }
    return false;
}

std::optional<int> getBitrate(Version version, Layer layer, size_t bitrateIndex) {
    if (bitrateIndex >= std::size(kBitrateTable)) {
        LOG(ERROR) << "Invalid bitrate index: " << bitrateIndex;
        return std::nullopt;
    }

    int bitrateColumn = 0;
    if (version == Version::MPEG_1) {
        if (layer == Layer::LAYER_1)
            bitrateColumn = 0;
        else if (layer == Layer::LAYER_2)
            bitrateColumn = 1;
        else
            bitrateColumn = 2;
    } else {  // MPEG_2 or MPEG_2_5
        if (layer == Layer::LAYER_1)
            bitrateColumn = 3;
        else
            bitrateColumn = 4;
    }

    const int bitrateInKbps = kBitrateTable[bitrateIndex][bitrateColumn];
    if (bitrateInKbps == 0) {
        LOG(WARNING) << "Invalid frame: bitrate is 0";
        return std::nullopt;
    }

    return bitrateInKbps;
}

std::optional<int> getSampleRate(Version version, size_t sampleRateIndex) {
    if (sampleRateIndex >= std::size(kSampleRateTable)) {
        LOG(ERROR) << "Invalid sample rate index: " << sampleRateIndex;
        return std::nullopt;
    }

    int versionColumn = 0;
    if (version == Version::MPEG_1)
        versionColumn = 0;
    else if (version == Version::MPEG_2)
        versionColumn = 1;
    else
        versionColumn = 2;  // MPEG_2_5

    const int sampleRate = kSampleRateTable[sampleRateIndex][versionColumn];
    if (sampleRate == 0) {
        LOG(WARNING) << "Invalid frame: sampling frequency is 0";
        return std::nullopt;
    }

    return sampleRate;
}

std::optional<MpegFrame> findMpegFrame(const uint8_t** currBuff, const uint8_t* endBuff) {
    MpegFrame frame;
    bool id3TagFound =
            findID3v2Tag(currBuff, endBuff, frame) || findID3v1Tag(currBuff, endBuff, frame);
    if (id3TagFound && *currBuff == endBuff) {
        return frame;
    }
    while ((*currBuff) + kFrameHeaderSize <= endBuff) {
        // Sync word of 11 set bits
        if ((*currBuff)[0] != kSyncByte || ((*currBuff)[1] & kSyncByteMask) != kSyncByteMask) {
            (*currBuff)++;
            continue;
        }
        const Version version =
                static_cast<Version>((((*currBuff)[1] >> kVersionIdShift) & kVersionIdMask));
        const Layer layer =
                static_cast<Layer>(((*currBuff)[1] >> kLayerDescShift) & kLayerDescMask);
        const size_t bitrateIndex = ((*currBuff)[2] >> kBitrateIndexShift) & kBitrateIndexMask;
        const size_t sampleRateIndex =
                ((*currBuff)[2] >> kSampleRateIndexShift) & kSampleRateIndexMask;

        if (version != Version::RESERVED && layer != Layer::RESERVED) {
            // Get the bitrate and sample rate
            auto bitrateOpt = getBitrate(version, layer, bitrateIndex);
            auto sampleRateOpt = getSampleRate(version, sampleRateIndex);

            if (bitrateOpt && sampleRateOpt) {
                const int bitrateInKbps = bitrateOpt.value();
                const int sampleRate = sampleRateOpt.value();
                const int paddingBit = ((*currBuff)[2] >> kPaddingBitShift) & kPaddingBitMask;
                const int channelIdx = ((*currBuff)[3] >> kChannelIdxShift) & kChannelIdxMask;
                frame.sampleRate = sampleRate;
                frame.bitRate = bitrateInKbps * 1000;

                [[maybe_unused]] int channelCount = (channelIdx == kMonoChannelIdx) ? 1 : 2;
                // Compute frameSize and frameLength
                if (layer == Layer::LAYER_1) {
                    frame.frameSize = kLayer1SamplesPerMpegFrame;
                    // For Layer 1, frame length is calculated based on 4-byte "slots"
                    frame.frameLengthBytes = ((kLayer1SamplesPerMpegFrame / sizeof(uint32_t)) *
                                                      (bitrateInKbps * 1000 /* convert to bps */) /
                                                      (sampleRate * CHAR_BIT) +
                                              paddingBit) *
                                             sizeof(uint32_t);
                } else {
                    frame.frameSize = kLayer2SamplesPerMpegFrame;
                    // For Layer 2 & 3, frame length is calculated directly in bytes.
                    frame.frameLengthBytes = (kLayer2SamplesPerMpegFrame *
                                              (bitrateInKbps * 1000 /* convert to bps */) /
                                              (sampleRate * CHAR_BIT)) +
                                             paddingBit;
                }

                if (*currBuff + frame.frameLengthBytes > endBuff) {
                    // This buffer only has partial frame data.
                    // The second part of this frame will be available in the next call. Consume
                    // all the bytes in this call. The findMpegFrame call for the next frame
                    // will consume the second half of the frame.
                    frame.bytesPending = (*currBuff + frame.frameLengthBytes) - endBuff;
                }
                *currBuff = std::min(*currBuff + frame.frameLengthBytes, endBuff);
                return frame;
            }
        }
        (*currBuff)++;
    }
    return std::nullopt;
}

}  // namespace  aidl::android::hardware::audio::core
