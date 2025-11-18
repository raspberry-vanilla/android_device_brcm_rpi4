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
#include <functional>
#include <map>
#include <memory>

#include <aidl/android/hardware/audio/effect/BnEffect.h>
#include <android-base/logging.h>

#include "effect-impl/EffectImpl.h"

#include "BundleContext.h"
#include "BundleTypes.h"
#include "GlobalSession.h"

namespace aidl::android::hardware::audio::effect {

class EffectBundleAidl final : public EffectImpl {
  public:
    explicit EffectBundleAidl(const AudioUuid& uuid);
    ~EffectBundleAidl() override;

    ndk::ScopedAStatus getDescriptor(Descriptor* _aidl_return) override;
    ndk::ScopedAStatus setParameterCommon(const Parameter& param) REQUIRES(mImplMutex) override;
    ndk::ScopedAStatus setParameterSpecific(const Parameter::Specific& specific)
            REQUIRES(mImplMutex) override;
    ndk::ScopedAStatus getParameterSpecific(const Parameter::Id& id, Parameter::Specific* specific)
            REQUIRES(mImplMutex) override;

    std::shared_ptr<EffectContext> createContext(const Parameter::Common& common)
            REQUIRES(mImplMutex) override;
    RetCode releaseContext() REQUIRES(mImplMutex) override;

    IEffect::Status effectProcessImpl(float* in, float* out, int samples)
            REQUIRES(mImplMutex) override;

    std::string getEffectName() override { return *mEffectName; }

  private:
    std::shared_ptr<BundleContext> mContext GUARDED_BY(mImplMutex);
    const Descriptor* mDescriptor;
    const std::string* mEffectName;
    lvm::BundleEffectType mType = lvm::BundleEffectType::EQUALIZER;

    IEffect::Status status(binder_status_t status, size_t consumed, size_t produced);

    ndk::ScopedAStatus setParameterBassBoost(const Parameter::Specific& specific)
            REQUIRES(mImplMutex);
    ndk::ScopedAStatus getParameterBassBoost(const BassBoost::Id& id, Parameter::Specific* specific)
            REQUIRES(mImplMutex);

    ndk::ScopedAStatus setParameterEqualizer(const Parameter::Specific& specific)
            REQUIRES(mImplMutex);
    ndk::ScopedAStatus getParameterEqualizer(const Equalizer::Id& id, Parameter::Specific* specific)
            REQUIRES(mImplMutex);
    ndk::ScopedAStatus setParameterVolume(const Parameter::Specific& specific) REQUIRES(mImplMutex);
    ndk::ScopedAStatus getParameterVolume(const Volume::Id& id, Parameter::Specific* specific)
            REQUIRES(mImplMutex);
    ndk::ScopedAStatus setParameterVirtualizer(const Parameter::Specific& specific)
            REQUIRES(mImplMutex);
    ndk::ScopedAStatus getParameterVirtualizer(const Virtualizer::Id& id,
                                               Parameter::Specific* specific) REQUIRES(mImplMutex);
};

}  // namespace aidl::android::hardware::audio::effect
