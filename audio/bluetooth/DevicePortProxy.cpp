/*
 * Copyright 2023 The Android Open Source Project
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

#define LOG_TAG "AHAL_BluetoothAudioPort"

#include <Log.h>
#include <android-base/stringprintf.h>
#include <audio_utils/primitives.h>
#include <log/log.h>

#include "BluetoothAudioSessionControl.h"
#include "core-impl/DevicePortProxy.h"

using aidl::android::hardware::audio::common::SinkMetadata;
using aidl::android::hardware::audio::common::SourceMetadata;
using aidl::android::hardware::bluetooth::audio::AudioConfiguration;
using aidl::android::hardware::bluetooth::audio::BluetoothAudioSessionControl;
using aidl::android::hardware::bluetooth::audio::BluetoothAudioStatus;
using aidl::android::hardware::bluetooth::audio::ChannelMode;
using aidl::android::hardware::bluetooth::audio::LatencyMode;
using aidl::android::hardware::bluetooth::audio::PcmConfiguration;
using aidl::android::hardware::bluetooth::audio::PortStatusCallbacks;
using aidl::android::hardware::bluetooth::audio::PresentationPosition;
using aidl::android::hardware::bluetooth::audio::SessionType;
using aidl::android::media::audio::common::AudioDeviceDescription;
using aidl::android::media::audio::common::AudioDeviceType;
using android::base::StringPrintf;

namespace android::bluetooth::audio::aidl {

class BluetoothSession {
  public:
    explicit BluetoothSession(SessionType session_type) : mSessionType(session_type) {}

    uint16_t registerControlResultCback(const PortStatusCallbacks& cbacks) {
        return BluetoothAudioSessionControl::RegisterControlResultCback(mSessionType, cbacks);
    }

    void unregisterControlResultCback(uint16_t cookie) {
        BluetoothAudioSessionControl::UnregisterControlResultCback(mSessionType, cookie);
    }

    bool isSessionReady(bool is_primary_hal = true) const {
        return BluetoothAudioSessionControl::IsSessionReady(mSessionType, is_primary_hal);
    }

    const AudioConfiguration getAudioConfig() const {
        return BluetoothAudioSessionControl::GetAudioConfig(mSessionType);
    }

    std::vector<LatencyMode> getSupportedLatencyModes() const {
        return BluetoothAudioSessionControl::GetSupportedLatencyModes(mSessionType);
    }

    void setLatencyMode(LatencyMode latency_mode) const {
        BluetoothAudioSessionControl::SetLatencyMode(mSessionType, latency_mode);
    }

    bool startStream(bool low_latency) const {
        return BluetoothAudioSessionControl::StartStream(mSessionType, low_latency);
    }

    bool suspendStream() const { return BluetoothAudioSessionControl::SuspendStream(mSessionType); }

    void stopStream() const { BluetoothAudioSessionControl::StopStream(mSessionType); }

    size_t outWritePcmData(const void* buffer, size_t bytes) const {
        return BluetoothAudioSessionControl::OutWritePcmData(mSessionType, buffer, bytes);
    }

    size_t inReadPcmData(void* buffer, size_t bytes) const {
        return BluetoothAudioSessionControl::InReadPcmData(mSessionType, buffer, bytes);
    }

    bool getPresentationPosition(PresentationPosition& presentation_position) const {
        return BluetoothAudioSessionControl::GetPresentationPosition(mSessionType,
                                                                     presentation_position);
    }

    bool updateSourceMetadata(const SourceMetadata& source_metadata) const {
        return BluetoothAudioSessionControl::UpdateSourceMetadata(mSessionType, source_metadata);
    }

    bool updateSinkMetadata(const SinkMetadata& sink_metadata) const {
        return BluetoothAudioSessionControl::UpdateSinkMetadata(mSessionType, sink_metadata);
    }

    SessionType getSessionType() const { return mSessionType; }

    std::string toString() const {
        return ::aidl::android::hardware::bluetooth::audio::toString(mSessionType);
    }

    bool isA2dp() const {
        return mSessionType == SessionType::A2DP_SOFTWARE_ENCODING_DATAPATH ||
               mSessionType == SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH;
    }

    bool isHfp() const {
        switch (mSessionType) {
            case SessionType::HFP_SOFTWARE_ENCODING_DATAPATH:
            case SessionType::HFP_SOFTWARE_DECODING_DATAPATH:
            case SessionType::HFP_HARDWARE_OFFLOAD_DATAPATH:
                return true;
            default:
                return false;
        }
    }

    bool isLeAudio() const {
        return mSessionType == SessionType::LE_AUDIO_SOFTWARE_ENCODING_DATAPATH ||
               mSessionType == SessionType::LE_AUDIO_SOFTWARE_DECODING_DATAPATH ||
               mSessionType == SessionType::LE_AUDIO_HARDWARE_OFFLOAD_ENCODING_DATAPATH ||
               mSessionType == SessionType::LE_AUDIO_HARDWARE_OFFLOAD_DECODING_DATAPATH ||
               mSessionType == SessionType::LE_AUDIO_BROADCAST_SOFTWARE_ENCODING_DATAPATH ||
               mSessionType == SessionType::LE_AUDIO_BROADCAST_HARDWARE_OFFLOAD_ENCODING_DATAPATH;
    }

  private:
    const SessionType mSessionType;
};

namespace {

// The maximum time to wait in std::condition_variable::wait_for()
constexpr unsigned int kMaxWaitingTimeMs = 4500;
constexpr unsigned int kScoMixPortFixedSampleRate = 32000;

}  // namespace

BluetoothAudioPortAidl::BluetoothAudioPortAidl(std::optional<bool> supportsLowLatency)
    : mCookie(::aidl::android::hardware::bluetooth::audio::kObserversCookieUndefined),
      mState(BluetoothStreamState::DISABLED),
      mSupportsLowLatency(supportsLowLatency) {}

BluetoothAudioPortAidl::~BluetoothAudioPortAidl() {
    unregisterPort();
}

bool BluetoothAudioPortAidl::registerPort(const AudioDeviceDescription& description) {
    if (inUse()) {
        LOG(ERROR) << __func__ << debugMessage() << " already in use";
        return false;
    }

    if (!initSession(description)) return false;

    auto control_result_cb = [port = this](uint16_t cookie, bool start_resp,
                                           const BluetoothAudioStatus& status) {
        (void)start_resp;
        port->controlResultHandler(cookie, status);
    };
    auto session_changed_cb = [port = this](uint16_t cookie) {
        port->sessionChangedHandler(cookie);
    };
    auto low_latency_allowed_cb = [port = this](uint16_t cookie, bool allowed) {
        port->lowLatencyAllowedHandler(cookie, allowed);
    };

    PortStatusCallbacks cbacks = {
            .control_result_cb_ = control_result_cb,
            .session_changed_cb_ = session_changed_cb,
            .low_latency_mode_allowed_cb_ = low_latency_allowed_cb,
    };
    mCookie = getSession()->registerControlResultCback(cbacks);
    auto isOk = (mCookie != ::aidl::android::hardware::bluetooth::audio::kObserversCookieUndefined);
    if (isOk) {
        std::lock_guard guard(mCvMutex);
        mState = BluetoothStreamState::STANDBY;
    }
    LOG(DEBUG) << __func__ << debugMessage();
    return isOk;
}

bool BluetoothAudioPortAidl::initSession(const AudioDeviceDescription& description) {
    SessionType sessionType = SessionType::UNKNOWN;
    SessionType fallbackSessionType = SessionType::UNKNOWN;
    if (description.connection == AudioDeviceDescription::CONNECTION_BT_A2DP &&
        (description.type == AudioDeviceType::OUT_DEVICE ||
         description.type == AudioDeviceType::OUT_HEADPHONE ||
         description.type == AudioDeviceType::OUT_SPEAKER)) {
        LOG(VERBOSE) << __func__
                     << ": device=AUDIO_DEVICE_OUT_BLUETOOTH_A2DP (HEADPHONES/SPEAKER) ("
                     << description.toString() << ")";
        sessionType = SessionType::A2DP_SOFTWARE_ENCODING_DATAPATH;
        fallbackSessionType = SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH;
    } else if (description.connection == AudioDeviceDescription::CONNECTION_WIRELESS &&
               description.type == AudioDeviceType::OUT_HEARING_AID) {
        LOG(VERBOSE) << __func__ << ": device=AUDIO_DEVICE_OUT_HEARING_AID (MEDIA/VOICE) ("
                     << description.toString() << ")";
        sessionType = SessionType::HEARING_AID_SOFTWARE_ENCODING_DATAPATH;
    } else if (description.connection == AudioDeviceDescription::CONNECTION_BT_LE &&
               description.type == AudioDeviceType::OUT_HEADSET) {
        LOG(VERBOSE) << __func__ << ": device=AUDIO_DEVICE_OUT_BLE_HEADSET (MEDIA/VOICE) ("
                     << description.toString() << ")";
        sessionType = SessionType::LE_AUDIO_SOFTWARE_ENCODING_DATAPATH;
        fallbackSessionType = SessionType::LE_AUDIO_HARDWARE_OFFLOAD_ENCODING_DATAPATH;
    } else if (description.connection == AudioDeviceDescription::CONNECTION_BT_LE &&
               description.type == AudioDeviceType::OUT_SPEAKER) {
        LOG(VERBOSE) << __func__ << ": device=AUDIO_DEVICE_OUT_BLE_SPEAKER (MEDIA) ("
                     << description.toString() << ")";
        sessionType = SessionType::LE_AUDIO_SOFTWARE_ENCODING_DATAPATH;
        fallbackSessionType = SessionType::LE_AUDIO_HARDWARE_OFFLOAD_ENCODING_DATAPATH;
    } else if (description.connection == AudioDeviceDescription::CONNECTION_BT_LE &&
               description.type == AudioDeviceType::IN_HEADSET) {
        LOG(VERBOSE) << __func__ << ": device=AUDIO_DEVICE_IN_BLE_HEADSET (VOICE) ("
                     << description.toString() << ")";
        sessionType = SessionType::LE_AUDIO_SOFTWARE_DECODING_DATAPATH;
    } else if (description.connection == AudioDeviceDescription::CONNECTION_BT_LE &&
               description.type == AudioDeviceType::OUT_BROADCAST) {
        LOG(VERBOSE) << __func__ << ": device=AUDIO_DEVICE_OUT_BLE_BROADCAST (MEDIA) ("
                     << description.toString() << ")";
        sessionType = SessionType::LE_AUDIO_BROADCAST_SOFTWARE_ENCODING_DATAPATH;
    } else if (description.connection == AudioDeviceDescription::CONNECTION_BT_SCO &&
               description.type == AudioDeviceType::IN_HEADSET) {
        LOG(VERBOSE) << __func__ << ": device=AUDIO_DEVICE_IN_SCO_HEADSET (VOICE) ("
                     << description.toString() << ")";
        sessionType = SessionType::HFP_SOFTWARE_DECODING_DATAPATH;
    } else if (description.connection == AudioDeviceDescription::CONNECTION_BT_SCO &&
               (description.type == AudioDeviceType::OUT_DEVICE ||
                description.type == AudioDeviceType::OUT_HEADSET ||
                description.type == AudioDeviceType::OUT_CARKIT)) {
        LOG(VERBOSE) << __func__ << ": device=AUDIO_DEVICE_OUT_SCO (HEADSET/OUT_CARKIT) ("
                     << description.toString() << ")";
        sessionType = SessionType::HFP_SOFTWARE_ENCODING_DATAPATH;
    } else {
        LOG(ERROR) << __func__ << ": unknown device=" << description.toString();
        return false;
    }

    if (!BluetoothAudioSessionControl::IsSessionReady(sessionType)) {
        if (fallbackSessionType != SessionType::UNKNOWN) {
            LOG(WARNING) << __func__
                         << ": Retry fallback session_type=" << toString(fallbackSessionType)
                         << " for session_type=" << toString(sessionType);
            if (BluetoothAudioSessionControl::IsSessionReady(fallbackSessionType, false)) {
                mSession = std::make_unique<BluetoothSession>(fallbackSessionType);
                return true;
            } else {
                LOG(ERROR) << __func__
                           << ": fallback session_type=" << toString(fallbackSessionType)
                           << " is not ready";
            }
        }
        LOG(ERROR) << __func__ << ": device=" << description.toString()
                   << ", session_type=" << toString(sessionType) << " is not ready";
        return false;
    }
    mSession = std::make_unique<BluetoothSession>(sessionType);
    return true;
}

void BluetoothAudioPortAidl::unregisterPort() {
    if (!inUse()) {
        LOG(WARNING) << __func__ << debugMessage() << ": BluetoothAudioPortAidl is not in use";
        return;
    }
    getSession()->unregisterControlResultCback(mCookie);
    mCookie = ::aidl::android::hardware::bluetooth::audio::kObserversCookieUndefined;
    mSession.reset();
    LOG(VERBOSE) << __func__ << debugMessage() << " port unregistered";
}

void BluetoothAudioPortAidl::controlResultHandler(uint16_t cookie,
                                                  const BluetoothAudioStatus& status) {
    std::lock_guard guard(mCvMutex);
    if (!inUse()) {
        LOG(ERROR) << "control_result_cb: BluetoothAudioPortAidl is not in use";
        return;
    }
    if (mCookie != cookie) {
        LOG(ERROR) << "control_result_cb: proxy of device port is corrupted "
                   << "cookie=" << StringPrintf("%#hx", cookie) << ", expected "
                   << StringPrintf("%#hx", mCookie);
        return;
    }
    BluetoothStreamState previous_state = mState;
    ::android::base::LogSeverity severity = ::android::base::FATAL;
    switch (previous_state) {
        case BluetoothStreamState::STARTED:
            /* Only Suspend signal can be send in STARTED state*/
            if (status == BluetoothAudioStatus::RECONFIGURATION ||
                status == BluetoothAudioStatus::SUCCESS) {
                mState = BluetoothStreamState::STANDBY;
                severity = ::android::base::INFO;
            } else {
                severity = ::android::base::WARNING;
            }
            break;
        case BluetoothStreamState::STARTING:
            if (status == BluetoothAudioStatus::SUCCESS) {
                mState = BluetoothStreamState::STARTED;
                severity = ::android::base::INFO;
            } else {
                // Set to standby since the stack may be busy switching between outputs
                mState = BluetoothStreamState::STANDBY;
                severity = ::android::base::WARNING;
            }
            break;
        case BluetoothStreamState::SUSPENDING:
            if (status == BluetoothAudioStatus::SUCCESS) {
                mState = BluetoothStreamState::STANDBY;
                severity = ::android::base::INFO;
            } else {
                // Will fail if the headset is disconnecting, so set to disable
                // to wait for re-init again
                mState = BluetoothStreamState::DISABLED;
                severity = ::android::base::WARNING;
            }
            break;
        default:
            severity = ::android::base::ERROR;
    }
    if (previous_state != mState) {
        LOG(severity) << "control_result_cb" << debugMessage() << ", status=" << toString(status)
                      << ", " << previous_state << " -> " << mState;
    } else {
        LOG(severity) << "control_result_cb" << debugMessage() << ", status=" << toString(status)
                      << ", " << previous_state;
    }
    if (severity != ::android::base::ERROR) {
        mInternalCv.notify_all();
    }
}

void BluetoothAudioPortAidl::lowLatencyAllowedHandler(uint16_t cookie, bool allowed) {
    if (mCookie != cookie) {
        LOG(ERROR) << "low_latency_allowed_cb: proxy of device port (cookie="
                   << StringPrintf("%#hx", cookie) << ") is corrupted";
        return;
    }
    LOG(INFO) << "low_latency_allowed_cb:" << debugMessage() << ", allowed=" << allowed;
    std::vector<LatencyMode> latency_modes;
    if (!getRecommendedLatencyModes(&latency_modes)) return;
    std::shared_ptr<BluetoothAudioPortCallbacks> callbacks;
    {
        std::lock_guard guard(mCvMutex);
        callbacks = mCallbacks;
    }
    if (callbacks) {
        callbacks->onRecommendedLatencyModeChanged(latency_modes);
    }
}

void BluetoothAudioPortAidl::sessionChangedHandler(uint16_t cookie) {
    std::lock_guard guard(mCvMutex);
    if (!inUse()) {
        LOG(ERROR) << "session_changed_cb: BluetoothAudioPortAidl is not in use";
        return;
    }
    if (mCookie != cookie) {
        LOG(ERROR) << "session_changed_cb: proxy of device port is corrupted "
                   << "cookie=" << StringPrintf("%#hx", cookie) << ", expected "
                   << StringPrintf("%#hx", mCookie);
        return;
    }
    BluetoothStreamState previous_state = mState;
    mState = BluetoothStreamState::DISABLED;
    LOG(DEBUG) << "session_changed_cb" << debugMessage() << ", " << previous_state << " -> "
               << mState;
    mInternalCv.notify_all();
}

bool BluetoothAudioPortAidl::inUse() const {
    return (mCookie != ::aidl::android::hardware::bluetooth::audio::kObserversCookieUndefined);
}

BluetoothSession* BluetoothAudioPortAidl::getSession() const {
    return mSession.get();
}

bool BluetoothAudioPortAidl::getPreferredDataIntervalUs(size_t& interval_us) const {
    if (!inUse()) {
        LOG(ERROR) << __func__ << debugMessage() << ": BluetoothAudioPortAidl is not in use";
        return false;
    }

    const AudioConfiguration& hal_audio_cfg = getSession()->getAudioConfig();
    if (hal_audio_cfg.getTag() != AudioConfiguration::pcmConfig) {
        LOG(ERROR) << __func__ << debugMessage() << ": unsupported audio cfg tag";
        return false;
    }

    interval_us = hal_audio_cfg.get<AudioConfiguration::pcmConfig>().dataIntervalUs;
    return true;
}

bool BluetoothAudioPortAidl::getRecommendedLatencyModes(std::vector<LatencyMode>* latency_modes,
                                                        std::optional<bool>* supports_low_latency) {
    if (!inUse()) {
        LOG(ERROR) << __func__ << debugMessage() << ": BluetoothAudioPortAidl is not in use";
        return false;
    }
    *latency_modes = getSession()->getSupportedLatencyModes();
    LOG(INFO) << __func__ << debugMessage() << ": "
              << ::android::internal::ToString(*latency_modes);
    *supports_low_latency = std::find(latency_modes->begin(), latency_modes->end(),
                                      LatencyMode::LOW_LATENCY) != latency_modes->end();
    return true;
}

bool BluetoothAudioPortAidl::getRecommendedLatencyModes(std::vector<LatencyMode>* latency_modes) {
    std::optional<bool> supportsLowLatency;
    if (getRecommendedLatencyModes(latency_modes, &supportsLowLatency)) {
        std::lock_guard guard(mCvMutex);
        mSupportsLowLatency = supportsLowLatency;
        return true;
    }
    return false;
}

bool BluetoothAudioPortAidl::loadAudioConfig(PcmConfiguration& audio_cfg) {
    if (!inUse()) {
        LOG(ERROR) << __func__ << debugMessage() << ": BluetoothAudioPortAidl is not in use";
        return false;
    }

    const AudioConfiguration& hal_audio_cfg = getSession()->getAudioConfig();
    if (hal_audio_cfg.getTag() != AudioConfiguration::pcmConfig) {
        LOG(ERROR) << __func__ << debugMessage()
                   << ": unsupported audio cfg tag: " << toString(hal_audio_cfg.getTag());
        return false;
    }
    audio_cfg = hal_audio_cfg.get<AudioConfiguration::pcmConfig>();
    LOG(VERBOSE) << __func__ << debugMessage() << ", state=" << getState() << ", PcmConfig=["
                 << audio_cfg.toString() << "]";
    if (audio_cfg.channelMode == ChannelMode::UNKNOWN) {
        LOG(ERROR) << __func__ << debugMessage()
                   << ": unsupported channel mode: " << toString(audio_cfg.channelMode);
        return false;
    }

    if (mSession->isHfp()) {
        if (audio_cfg.sampleRateHz == kScoMixPortFixedSampleRate) {
            mResampler = {nullptr, nullptr};
            mResampleRatio = 0;
        } else {
            uint32_t resampleTargetRate = audio_cfg.sampleRateHz;
            switch (audio_cfg.sampleRateHz) {
                case 8000:
                case 16000:
                case 32000:
                    break;
                default:
                    LOG(FATAL) << __func__ << debugMessage()
                               << ": Resample target rate must be a divisor of "
                               << kScoMixPortFixedSampleRate << ", but is "
                               << audio_cfg.sampleRateHz;
            }
            mResampleRatio = kScoMixPortFixedSampleRate / resampleTargetRate;
            audio_cfg.sampleRateHz = kScoMixPortFixedSampleRate;

            int in_rate, out_rate;
            switch (mSession->getSessionType()) {
                case SessionType::HFP_SOFTWARE_ENCODING_DATAPATH:
                    in_rate = kScoMixPortFixedSampleRate;
                    out_rate = resampleTargetRate;
                    break;
                case SessionType::HFP_SOFTWARE_DECODING_DATAPATH:
                    in_rate = resampleTargetRate;
                    out_rate = kScoMixPortFixedSampleRate;
                    break;
                default:
                    LOG(ERROR) << __func__
                               << ": cannot create resampler for unsupported session type "
                               << toString(mSession->getSessionType());
                    return false;
            }
            struct resampler_itfe* resampler = nullptr;

            if (int rc = create_resampler(/* in_sample_rate= */ in_rate,
                                          /* outSampleRate= */ out_rate,
                                          /* channelCount= */ 1,
                                          /* quality= */ RESAMPLER_QUALITY_VOIP,
                                          /* provider= */ nullptr,
                                          /* resampler= */ &resampler);
                rc != 0) {
                LOG(ERROR) << __func__ << ": failed to create resampler, rc=" << rc;
                return false;
            }
            mResampler = {resampler, release_resampler};

            LOG(DEBUG) << __func__ << ": created resampler with in_rate=" << in_rate
                       << ", out_rate=" << out_rate << ", for session type "
                       << toString(mSession->getSessionType());
        }
    }
    return true;
}

bool BluetoothAudioPortAidlOut::loadAudioConfig(PcmConfiguration& audio_cfg) {
    if (!BluetoothAudioPortAidl::loadAudioConfig(audio_cfg)) return false;
    // WAR to support Mono / 16 bits per sample as the Bluetooth stack requires
    if (audio_cfg.channelMode == ChannelMode::MONO && audio_cfg.bitsPerSample == 16) {
        mIsStereoToMono = true;
        audio_cfg.channelMode = ChannelMode::STEREO;
        LOG(INFO) << __func__ << debugMessage()
                  << ": force channels = to be AUDIO_CHANNEL_OUT_STEREO";
    } else {
        mIsStereoToMono = false;
    }
    return true;
}

bool BluetoothAudioPortAidl::standby() {
    if (!inUse()) {
        LOG(ERROR) << __func__ << debugMessage() << ": BluetoothAudioPortAidl is not in use";
        return false;
    }
    std::lock_guard guard(mCvMutex);
    BluetoothStreamState previous_state = mState;
    LOG(VERBOSE) << __func__ << debugMessage() << ", state=" << mState << " request";
    if (mState == BluetoothStreamState::DISABLED) {
        mState = BluetoothStreamState::STANDBY;
        LOG(INFO) << __func__ << debugMessage() << ", " << previous_state << " -> " << mState;
        return true;
    }
    return false;
}

bool BluetoothAudioPortAidl::condWaitState(std::unique_lock<std::mutex>* lock) {
    const auto waitTime = std::chrono::milliseconds(kMaxWaitingTimeMs);
    const auto state = mState;
    if (state == BluetoothStreamState::STARTING || state == BluetoothStreamState::SUSPENDING) {
        LOG(DEBUG) << __func__ << debugMessage() << " waiting to change from " << state;
        mInternalCv.wait_for(*lock, waitTime, [this, state] {
            // No aliasing support in thread safety analysis, specify lock name explicitly.
            base::ScopedLockAssertion lock_assertion(mCvMutex);
            return mState != state;
        });
        const bool expected =
                mState == (state == BluetoothStreamState::STARTING ? BluetoothStreamState::STARTED
                                                                   : BluetoothStreamState::STANDBY);
        LOG(expected ? INFO : WARNING)
                << __func__ << debugMessage() << ", " << state << " -> " << mState;
        return expected;
    }
    LOG(ERROR) << __func__ << debugMessage() << " called to wait when in " << state;
    return false;
}

bool BluetoothAudioPortAidl::start() {
    if (!inUse()) {
        LOG(ERROR) << __func__ << debugMessage() << ": BluetoothAudioPortAidl is not in use";
        return false;
    }

    bool retval = false;
    {
        std::unique_lock lock(mCvMutex);
        base::ScopedLockAssertion lock_assertion(mCvMutex);
        LOG(VERBOSE) << __func__ << debugMessage() << ", state=" << mState
                     << ", mono=" << (mIsStereoToMono ? "true" : "false") << " request";
        if (mState == BluetoothStreamState::STARTED) {
            return true;  // nop, return
        } else if (mState == BluetoothStreamState::DISABLED) {
            return false; // avoid logspam when called from `transfer`
        } else if (mState == BluetoothStreamState::SUSPENDING ||
                   mState == BluetoothStreamState::STARTING) {
            /* If port is in transient state, give some time to respond */
            if (!condWaitState(&lock)) {
                LOG(ERROR) << __func__ << debugMessage() << ", state=" << mState << " failure";
                return false;
            }
        }
        if (mState == BluetoothStreamState::STARTED) {
            retval = true;
        } else if (mState == BluetoothStreamState::STANDBY) {
            if (!mSupportsLowLatency.has_value()) {
                std::vector<LatencyMode> latency_modes;
                getRecommendedLatencyModes(&latency_modes, &mSupportsLowLatency);
            }
            const bool low_latency = mSupportsLowLatency.value_or(false);
            mState = BluetoothStreamState::STARTING;
            lock.unlock();
            const bool startSuccess = mSession->startStream(low_latency);
            lock.lock();
            if (startSuccess && mState == BluetoothStreamState::STARTING) {
                retval = condWaitState(&lock);
            } else if (startSuccess && mState == BluetoothStreamState::STARTED) {
                retval = true;
            } else {
                // !startSuccess => no session instance
                const BluetoothStreamState newState = startSuccess ? BluetoothStreamState::STANDBY
                                                                   : BluetoothStreamState::DISABLED;
                LOG(ERROR) << __func__ << debugMessage() << ", startSuccess=" << startSuccess
                           << ", state=" << mState << " -> " << newState;
                mState = newState;
            }
        }
        if (retval) {
            LOG(INFO) << __func__ << debugMessage() << ", state=" << mState
                      << ", mono=" << (mIsStereoToMono ? "true" : "false") << " done";
            // Reload audio config.
            PcmConfiguration pcm_config;
            loadAudioConfig(pcm_config);
        } else {
            LOG(ERROR) << __func__ << debugMessage() << ", state=" << mState << " failure";
        }
    }
    return retval;  // false if any failure like timeout
}

bool BluetoothAudioPortAidl::suspend() {
    if (!inUse()) {
        LOG(ERROR) << __func__ << debugMessage() << ": BluetoothAudioPortAidl is not in use";
        return false;
    }

    bool retval = false;
    {
        std::unique_lock lock(mCvMutex);
        base::ScopedLockAssertion lock_assertion(mCvMutex);
        LOG(VERBOSE) << __func__ << debugMessage() << ", state=" << mState << " request";
        if (mState == BluetoothStreamState::STANDBY) {
            return true;  // nop, return
        } else if (mState == BluetoothStreamState::SUSPENDING ||
                   mState == BluetoothStreamState::STARTING) {
            /* If port is in transient state, give some time to respond */
            if (!condWaitState(&lock)) {
                LOG(ERROR) << __func__ << debugMessage() << ", state=" << mState << " failure";
                return false;
            }
        }
        if (mState == BluetoothStreamState::STANDBY) {
            retval = true;
        } else if (mState == BluetoothStreamState::STARTED) {
            mState = BluetoothStreamState::SUSPENDING;
            lock.unlock();
            const bool suspendSuccess = mSession->suspendStream();
            lock.lock();
            if (suspendSuccess && mState == BluetoothStreamState::SUSPENDING) {
                retval = condWaitState(&lock);
            } else if (suspendSuccess && mState == BluetoothStreamState::STANDBY) {
                retval = true;
            } else {
                LOG(ERROR) << __func__ << debugMessage() << ", suspendSuccess=" << suspendSuccess
                           << ", state=" << mState << " -> DISABLED";
                mState = BluetoothStreamState::DISABLED;
            }
        }
        if (retval) {
            LOG(INFO) << __func__ << debugMessage() << ", state=" << mState << " done";
        } else {
            LOG(ERROR) << __func__ << debugMessage() << ", state=" << mState << " failure";
        }
    }
    return retval;  // false if any failure like timeout
}

void BluetoothAudioPortAidl::stop() {
    if (!inUse()) {
        LOG(ERROR) << __func__ << debugMessage() << ": BluetoothAudioPortAidl is not in use";
        return;
    }
    std::unique_lock lock(mCvMutex);
    base::ScopedLockAssertion lock_assertion(mCvMutex);
    BluetoothStreamState previous_state = mState;
    LOG(VERBOSE) << __func__ << debugMessage() << ", state=" << mState << " request";
    if (mState != BluetoothStreamState::DISABLED) {
        lock.unlock();
        mSession->stopStream();
        lock.lock();
        mState = BluetoothStreamState::DISABLED;
        LOG(INFO) << __func__ << debugMessage() << ", " << previous_state << " -> " << mState;
    }
}

size_t BluetoothAudioPortAidlOut::writeData(const void* buffer, size_t bytes) const {
    if (!buffer) {
        LOG(ERROR) << __func__ << debugMessage() << ": bad input arg";
        return 0;
    }

    if (!inUse()) {
        LOG(ERROR) << __func__ << debugMessage() << ": BluetoothAudioPortAidl is not in use";
        return 0;
    }

    size_t multiplier = 1;
    std::vector<int16_t> src(static_cast<const int16_t*>(buffer),
                             static_cast<const int16_t*>(buffer) + bytes / 2);

    if (mIsStereoToMono) {
        // WAR to mix the stereo into Mono (16 bits per sample)
        std::vector<int16_t> dst(src.size() / 2);
        if (dst.empty()) {
            LOG(ERROR) << __func__ << debugMessage() << ": No frame to write";
            return 0;
        }
        downmix_to_mono_i16_from_stereo_i16(dst.data(), src.data(), dst.size());
        src = std::move(dst);

        multiplier *= 2;
    }

    if (mResampler) {
        // WAR to resample audio data from BT HAL to expected rate in mix port
        size_t in_frames = src.size();
        if (in_frames == 0) {
            LOG(ERROR) << __func__ << debugMessage() << ": No frame to write";
            return 0;
        }
        size_t out_frames = in_frames / mResampleRatio;
        std::vector<int16_t> dst(out_frames);
        int rc = mResampler->resample_from_input(mResampler.get(), src.data(), &in_frames,
                                                 dst.data(), &out_frames);
        if (rc) {
            LOG(ERROR) << __func__ << ": failed to resample, rc=" << rc;
        }
        dst.resize(out_frames);
        src = std::move(dst);

        multiplier *= mResampleRatio;
    }

    auto totalWrite = getSession()->outWritePcmData(src.data(), src.size() * 2);
    return totalWrite * multiplier;
}

bool BluetoothAudioPortAidlOut::setLatencyMode(
        ::aidl::android::hardware::bluetooth::audio::LatencyMode latency_mode) {
    if (!inUse()) {
        LOG(ERROR) << __func__ << debugMessage() << ": BluetoothAudioPortAidl is not in use";
        return false;
    }
    LOG(INFO) << __func__ << debugMessage() << ": " << toString(latency_mode);
    getSession()->setLatencyMode(latency_mode);
    return true;
}

size_t BluetoothAudioPortAidlIn::readData(void* buffer, size_t bytes) const {
    if (!buffer) {
        LOG(ERROR) << __func__ << debugMessage() << ": bad input arg";
        return 0;
    }

    if (!inUse()) {
        LOG(ERROR) << __func__ << debugMessage() << ": BluetoothAudioPortAidl is not in use";
        return 0;
    }

    if (!mResampler) {
        return getSession()->inReadPcmData(buffer, bytes);
    } else {
        std::vector<int16_t> input(bytes / mResampleRatio / 2);
        auto totalRead = getSession()->inReadPcmData(input.data(), input.size() * 2);

        size_t in_frames = totalRead / 2;
        if (in_frames == 0) return 0;

        size_t out_frames = in_frames * mResampleRatio;

        int rc = mResampler->resample_from_input(mResampler.get(), input.data(), &in_frames,
                                                 static_cast<int16_t*>(buffer), &out_frames);
        if (rc) {
            LOG(ERROR) << __func__ << ": failed to resample, rc=" << rc;
        }
        return out_frames * 2;
    }
}

bool BluetoothAudioPortAidl::getPresentationPosition(
        PresentationPosition& presentation_position) const {
    if (!inUse()) {
        LOG(ERROR) << __func__ << debugMessage() << ": BluetoothAudioPortAidl is not in use";
        return false;
    }
    bool retval = getSession()->getPresentationPosition(presentation_position);
    LOG(VERBOSE) << __func__ << debugMessage() << ", state=" << getState()
                 << presentation_position.toString();

    return retval;
}

bool BluetoothAudioPortAidl::updateSourceMetadata(const SourceMetadata& source_metadata) const {
    if (!inUse()) {
        LOG(ERROR) << __func__ << debugMessage() << ": BluetoothAudioPortAidl is not in use";
        return false;
    }
    LOG(DEBUG) << __func__ << debugMessage() << ", state=" << getState() << ", "
               << source_metadata.tracks.size() << " track(s)";
    if (source_metadata.tracks.size() == 0) return true;
    return getSession()->updateSourceMetadata(source_metadata);
}

bool BluetoothAudioPortAidl::updateSinkMetadata(const SinkMetadata& sink_metadata) const {
    if (!inUse()) {
        LOG(ERROR) << __func__ << debugMessage() << ": BluetoothAudioPortAidl is not in use";
        return false;
    }
    LOG(DEBUG) << __func__ << debugMessage() << ", state=" << getState() << ", "
               << sink_metadata.tracks.size() << " track(s)";
    if (sink_metadata.tracks.size() == 0) return true;
    return getSession()->updateSinkMetadata(sink_metadata);
}

BluetoothStreamState BluetoothAudioPortAidl::getState() const {
    std::lock_guard guard(mCvMutex);
    return mState;
}

bool BluetoothAudioPortAidl::setState(BluetoothStreamState state) {
    if (!inUse()) {
        LOG(ERROR) << __func__ << debugMessage() << ": BluetoothAudioPortAidl is not in use";
        return false;
    }
    std::lock_guard guard(mCvMutex);
    LOG(INFO) << __func__ << debugMessage() << ": " << mState << " -> " << state;
    mState = state;
    return true;
}

void BluetoothAudioPortAidl::setCallbacks(
        const std::shared_ptr<BluetoothAudioPortCallbacks>& callbacks) {
    std::lock_guard l(mCvMutex);
    mCallbacks = callbacks;
}

bool BluetoothAudioPortAidl::isA2dp() const {
    return mSession ? mSession->isA2dp() : false;
}

bool BluetoothAudioPortAidl::isHfp() const {
    return mSession ? mSession->isHfp() : false;
}

bool BluetoothAudioPortAidl::isLeAudio() const {
    return mSession ? mSession->isLeAudio() : false;
}

std::string BluetoothAudioPortAidl::debugMessage() const {
    const auto session_type_str = getSessionNameForDebug();
    return StringPrintf(": session_type=%s, cookie=%#hx", session_type_str.c_str(), mCookie);
}

std::string BluetoothAudioPortAidl::getSessionNameForDebug() const {
    return mSession ? mSession->toString() : toString(SessionType::UNKNOWN);
}

}  // namespace android::bluetooth::audio::aidl
