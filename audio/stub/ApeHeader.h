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

#pragma once

#include <cstdint>

namespace aidl::android::hardware::audio::core {

// Simplified APE (Monkey Audio) header definition sufficient to figure out
// the basic parameters of the encoded file. Only supports the "current"
// versions of the header (>= 3980).

#pragma pack(push, 4)

// Only the beginning of the descriptor is needed to find the header which
// follows the descriptor.
struct ApeDescriptor {
    uint32_t signature;  // 'MAC ' or 'MACF'
    uint16_t version;
    uint16_t padding;
    uint32_t descriptorSizeBytes;
    uint32_t headerSizeBytes;
};

struct ApeHeader {
    uint16_t compressionLevel;
    uint16_t flags;
    uint32_t blocksPerFrame;   // "frames" are encoder frames, while "blocks" are audio frames
    uint32_t lastFrameBlocks;  // number of "blocks" in the last encoder "frame"
    uint32_t totalFrames;      // total number of encoder "frames"
    uint16_t bitsPerSample;
    uint16_t channelCount;
    uint32_t sampleRate;
};

#pragma pack(pop)

// Tries to find APE descriptor and header in the buffer. Returns the position
// after the header or nullptr if it was not found.
void* findApeHeader(void* buffer, size_t bufferSizeBytes, ApeHeader** header);

// Clip duration in audio frames ("blocks" in the APE terminology).
inline int64_t getApeClipDurationFrames(const ApeHeader* header) {
    return header->totalFrames != 0
                   ? (header->totalFrames - 1) * header->blocksPerFrame + header->lastFrameBlocks
                   : 0;
}

}  // namespace aidl::android::hardware::audio::core
