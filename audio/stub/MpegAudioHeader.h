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

#include <cstdint>
#include <optional>
#include <ostream>

namespace aidl::android::hardware::audio::core {

/*
 * This parser captures these fields to determine properties such as
 * the MPEG version, layer, bitrate, sample rate, frame length and frame size.
 *
 * Reference - https://www.datavoyage.com/mpgscript/mpeghdr.htm
 *           - https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html
 */

struct MpegFrame {
    // The total length of the MPEG audio frame in bytes, including the header
    int frameLengthBytes = 0;
    // The number of audio samples in the frame
    int frameSize = 0;
    bool isID3v2 = false;
    bool isID3v1 = false;
    // Last few bytes of an ID3 tag or frame that couldn't be processed in a single chunk
    size_t bytesPending = 0;
    int sampleRate = 0;
    int bitRate = 0;
};

inline std::ostream& operator<<(std::ostream& os, const MpegFrame& frame) {
    os << "MpegFrame{len=" << frame.frameLengthBytes << ", size=" << frame.frameSize
       << ", isID3v2=" << (frame.isID3v2 ? "true" : "false")
       << ", isID3v1=" << (frame.isID3v1 ? "true" : "false") << ", pending=" << frame.bytesPending
       << ", sampleRate=" << frame.sampleRate << "}";
    return os;
}

std::optional<MpegFrame> findMpegFrame(const uint8_t** currBuff, const uint8_t* endBuff);

}  // namespace aidl::android::hardware::audio::core
