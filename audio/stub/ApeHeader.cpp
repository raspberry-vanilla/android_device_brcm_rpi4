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

#define LOG_TAG "AHAL_OffloadStream"
#include <android-base/logging.h>

#include "ApeHeader.h"

namespace aidl::android::hardware::audio::core {

static constexpr uint32_t kApeSignature1 = 0x2043414d;  // 'MAC ';
static constexpr uint32_t kApeSignature2 = 0x4643414d;  // 'MACF';
static constexpr uint16_t kMinimumVersion = 3980;

void* findApeHeader(void* buffer, size_t bufferSizeBytes, ApeHeader** header) {
    auto advanceBy = [&](size_t bytes) -> void* {
        buffer = static_cast<uint8_t*>(buffer) + bytes;
        bufferSizeBytes -= bytes;
        return buffer;
    };

    while (bufferSizeBytes >= sizeof(ApeDescriptor) + sizeof(ApeHeader)) {
        ApeDescriptor* descPtr = static_cast<ApeDescriptor*>(buffer);
        if (descPtr->signature != kApeSignature1 && descPtr->signature != kApeSignature2) {
            advanceBy(sizeof(descPtr->signature));
            continue;
        }
        if (descPtr->version < kMinimumVersion) {
            LOG(ERROR) << __func__ << ": Unsupported APE version: " << descPtr->version
                       << ", minimum supported version: " << kMinimumVersion;
            // Older versions only have a header, which is of the size similar to the modern header.
            advanceBy(sizeof(ApeHeader));
            continue;
        }
        if (descPtr->descriptorSizeBytes > bufferSizeBytes) {
            LOG(ERROR) << __func__
                       << ": Invalid APE descriptor size: " << descPtr->descriptorSizeBytes
                       << ", overruns remaining buffer size: " << bufferSizeBytes;
            advanceBy(sizeof(ApeDescriptor));
            continue;
        }
        advanceBy(descPtr->descriptorSizeBytes);
        if (sizeof(ApeHeader) > bufferSizeBytes) {
            LOG(ERROR) << __func__ << ": APE header is incomplete, want: " << sizeof(ApeHeader)
                       << " bytes, have: " << bufferSizeBytes;
            return nullptr;
        }
        *header = static_cast<ApeHeader*>(buffer);
        return advanceBy(sizeof(ApeHeader));
    }
    return nullptr;
}

}  // namespace aidl::android::hardware::audio::core
