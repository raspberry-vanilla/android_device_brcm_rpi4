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

#include "StreamAlsaBase.h"

namespace aidl::android::hardware::audio::core {

// Direct ALSA I/O path (no MonoPipe, no background I/O thread).
// Reads/writes directly to/from ALSA device proxies in the transfer() call.
// Used by StreamUsb.
class StreamAlsa : public StreamAlsaBase {
  public:
    StreamAlsa(StreamContext* context, const Metadata& metadata, int readWriteRetries);
    ~StreamAlsa();

    // Methods of 'DriverInterface'.
    ::android::status_t standby() override;
    ::android::status_t start() override;
    ::android::status_t transfer(void* buffer, size_t frameCount, size_t* actualFrameCount,
                                 int32_t* latencyMs) override;
    void shutdown() override;
};

}  // namespace aidl::android::hardware::audio::core