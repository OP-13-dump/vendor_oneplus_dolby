/*
 * Copyright (C) 2026 The LineageOS Project
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

#include <aidl/android/hardware/audio/effect/BnEffect.h>
#include <fmq/AidlMessageQueue.h>
#include <hardware/audio_effect.h>

#include <memory>
#include <vector>

#include "effect-impl/EffectImpl.h"

namespace aidl::android::hardware::audio::effect {

/*
 * Dolby ships DAP only as a pre-AIDL effect library: libswdap.so exports the
 * legacy AELI (audio_effect_library_t) symbol and nothing else, so the AIDL
 * effect factory - which dlsyms createEffect/queryEffect/destroyEffect - cannot
 * load it directly. This library is that missing adapter: it presents the AIDL
 * entry points and forwards everything to the legacy handle underneath.
 *
 * The framework hands vendor effects their parameters as an untouched
 * effect_param_t blob (see AidlConversionVendorExtension), which is exactly what
 * the legacy EFFECT_CMD_SET_PARAM/GET_PARAM commands consume, so the control
 * protocol LunarisDolby speaks survives the round trip byte for byte.
 */

// Owns the dlopen'd libswdap.so for the lifetime of the process.
class DolbyDapLibrary {
  public:
    static DolbyDapLibrary& get();

    bool valid() const { return mLib != nullptr; }
    audio_effect_library_t* lib() const { return mLib; }

    // Legacy descriptor as reported by the blob, translated to AIDL.
    bool getDescriptor(Descriptor* desc);

  private:
    DolbyDapLibrary();

    void* mHandle = nullptr;
    audio_effect_library_t* mLib = nullptr;
};

class DolbyDapContext final : public EffectContext {
  public:
    DolbyDapContext(int statusDepth, const Parameter::Common& common);
    ~DolbyDapContext() override;

    bool initialized() const { return mHandle != nullptr; }

    RetCode setParams(const std::vector<uint8_t>& params);
    std::vector<uint8_t> getParams(const std::vector<uint8_t>& id);

    RetCode enable() override;
    RetCode disable() override;
    RetCode reset() override;
    RetCode setCommon(const Parameter::Common& common) override;

    void process(float* in, float* out, int samples);

  private:
    // Pushes the current Parameter::Common down as a legacy effect_config_t.
    RetCode applyConfig(const Parameter::Common& common);
    int sendCommand(uint32_t cmd, uint32_t inSize, void* inData, uint32_t* outSize,
                    void* outData) const;

    effect_handle_t mHandle = nullptr;
    int mChannelCount = 2;
    // Replayed when the framework asks for a parameter we have no reply for.
    std::vector<uint8_t> mLastParams;
};

class DolbyDapEffect final : public EffectImpl {
  public:
    static const std::string kEffectName;

    DolbyDapEffect() = default;
    ~DolbyDapEffect() override;

    ndk::ScopedAStatus getDescriptor(Descriptor* _aidl_return) override;
    ndk::ScopedAStatus setParameterSpecific(const Parameter::Specific& specific)
            REQUIRES(mImplMutex) override;
    ndk::ScopedAStatus getParameterSpecific(const Parameter::Id& id, Parameter::Specific* specific)
            REQUIRES(mImplMutex) override;

    std::shared_ptr<EffectContext> createContext(const Parameter::Common& common)
            REQUIRES(mImplMutex) override;
    RetCode releaseContext() REQUIRES(mImplMutex) override;

    std::string getEffectName() override { return kEffectName; }
    IEffect::Status effectProcessImpl(float* in, float* out, int samples)
            REQUIRES(mImplMutex) override;

  private:
    std::shared_ptr<DolbyDapContext> mContext GUARDED_BY(mImplMutex);
};

}  // namespace aidl::android::hardware::audio::effect
