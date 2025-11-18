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

#include <algorithm>
#include <memory>
#include <unordered_map>

#include <android-base/logging.h>

#include "BundleContext.h"
#include "BundleTypes.h"

namespace aidl::android::hardware::audio::effect {

/**
 * @brief Maintain all effect bundle sessions.
 *
 * Sessions are identified with the session ID, maximum of MAX_BUNDLE_SESSIONS is supported by the
 * bundle implementation.
 */
class GlobalSession {
  public:
    static GlobalSession& getGlobalSession() {
        static GlobalSession instance;
        return instance;
    }

    static bool findBundleTypeInList(std::vector<std::shared_ptr<BundleContext>>& list,
                                     const lvm::BundleEffectType& type, bool remove = false) {
        auto itor = std::find_if(list.begin(), list.end(),
                                  [type](const std::shared_ptr<BundleContext>& bundle) {
                                      return bundle ? bundle->getBundleType() == type : false;
                                  });
        if (itor == list.end()) {
            return false;
        }
        if (remove && *itor) {
            (*itor)->deInit();
            list.erase(itor);
        }
        return true;
    }

    /**
     * Create a certain type of BundleContext in shared_ptr container, each session must not have
     * more than one session for each type.
     */
    std::shared_ptr<BundleContext> createSession(const lvm::BundleEffectType& type, int statusDepth,
                                                 const Parameter::Common& common) {
        int sessionId = common.session;
        LOG(VERBOSE) << __func__ << type << " with sessionId " << sessionId;
        if (mSessionMap.count(sessionId) == 0 && mSessionMap.size() >= MAX_BUNDLE_SESSIONS) {
            LOG(ERROR) << __func__ << " exceed max bundle session";
            return nullptr;
        }

        if (mSessionMap.count(sessionId)) {
            if (findBundleTypeInList(mSessionMap[sessionId], type)) {
                LOG(ERROR) << __func__ << type << " already exist in session " << sessionId;
                return nullptr;
            }
        }

        auto& list = mSessionMap[sessionId];
        auto context = std::make_shared<BundleContext>(statusDepth, common, type);
        RETURN_VALUE_IF(!context, nullptr, "failedToCreateContext");

        RetCode ret = context->init();
        if (RetCode::SUCCESS != ret) {
            LOG(ERROR) << __func__ << " context init ret " << ret;
            return nullptr;
        }
        list.push_back(context);
        return context;
    }

    void releaseSession(const lvm::BundleEffectType& type, int sessionId) {
        LOG(VERBOSE) << __func__ << type << " sessionId " << sessionId;
        if (mSessionMap.count(sessionId)) {
            auto& list = mSessionMap[sessionId];
            if (!findBundleTypeInList(list, type, true /* remove */)) {
                LOG(ERROR) << __func__ << " can't find " << type << "in session " << sessionId;
                return;
            }
            if (list.size() == 0) {
                mSessionMap.erase(sessionId);
            }
        }
    }

  private:
    // Max session number supported.
    static constexpr int MAX_BUNDLE_SESSIONS = 32;
    std::unordered_map<int /* session ID */, std::vector<std::shared_ptr<BundleContext>>>
            mSessionMap;
};
}  // namespace aidl::android::hardware::audio::effect
