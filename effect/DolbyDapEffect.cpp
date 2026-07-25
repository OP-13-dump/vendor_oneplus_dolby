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

#include <cstring>
#include <dlfcn.h>

#include <aidl/android/hardware/audio/effect/DefaultExtension.h>
#define LOG_TAG "AHAL_DolbyDapEffect"
#include <Utils.h>
#include <android-base/logging.h>
#include <system/audio.h>
#include <system/audio_effect.h>
#include <system/audio_effects/effect_uuid.h>

#include "DolbyDapEffect.h"

using aidl::android::hardware::audio::effect::DefaultExtension;
using aidl::android::hardware::audio::effect::Descriptor;
using aidl::android::hardware::audio::effect::DolbyDapEffect;
using aidl::android::hardware::audio::effect::DolbyDapLibrary;
using aidl::android::hardware::audio::effect::getEffectImplUuidDolbyDap;
using aidl::android::hardware::audio::effect::IEffect;
using aidl::android::hardware::audio::effect::VendorExtension;
using aidl::android::media::audio::common::AudioUuid;

extern "C" binder_exception_t createEffect(const AudioUuid* in_impl_uuid,
                                           std::shared_ptr<IEffect>* instanceSpp) {
    if (!in_impl_uuid || *in_impl_uuid != getEffectImplUuidDolbyDap()) {
        LOG(ERROR) << __func__ << " uuid not supported";
        return EX_ILLEGAL_ARGUMENT;
    }
    if (!DolbyDapLibrary::get().valid()) {
        LOG(ERROR) << __func__ << " libswdap.so unavailable";
        return EX_UNSUPPORTED_OPERATION;
    }
    if (!instanceSpp) {
        LOG(ERROR) << __func__ << " invalid input parameter!";
        return EX_ILLEGAL_ARGUMENT;
    }
    *instanceSpp = ndk::SharedRefBase::make<DolbyDapEffect>();
    LOG(DEBUG) << __func__ << " instance " << instanceSpp->get() << " created";
    return EX_NONE;
}

extern "C" binder_exception_t queryEffect(const AudioUuid* in_impl_uuid, Descriptor* _aidl_return) {
    if (!in_impl_uuid || *in_impl_uuid != getEffectImplUuidDolbyDap()) {
        LOG(ERROR) << __func__ << " uuid not supported";
        return EX_ILLEGAL_ARGUMENT;
    }
    if (!_aidl_return || !DolbyDapLibrary::get().getDescriptor(_aidl_return)) {
        LOG(ERROR) << __func__ << " no descriptor from libswdap.so";
        return EX_UNSUPPORTED_OPERATION;
    }
    return EX_NONE;
}

namespace aidl::android::hardware::audio::effect {

namespace {

constexpr size_t kHeaderSize = sizeof(effect_param_t);

// effect_param_t pads the parameter block up to a 32-bit boundary before the value.
inline size_t paddedSize(uint32_t size) {
    return ((size - 1) / sizeof(int32_t) + 1) * sizeof(int32_t);
}

const char* kDapLibPaths[] = {
        "/vendor/lib64/soundfx/libswdap.so",
        "/vendor/lib/soundfx/libswdap.so",
};

AudioUuid legacyUuidToAidl(const effect_uuid_t& uuid) {
    AudioUuid out;
    out.timeLow = static_cast<int32_t>(uuid.timeLow);
    out.timeMid = static_cast<int32_t>(uuid.timeMid);
    out.timeHiAndVersion = static_cast<int32_t>(uuid.timeHiAndVersion);
    out.clockSeq = static_cast<int32_t>(uuid.clockSeq);
    out.node = std::vector<uint8_t>(std::begin(uuid.node), std::end(uuid.node));
    return out;
}

Flags legacyFlagsToAidl(uint32_t legacy) {
    Flags flags;

    switch (legacy & EFFECT_FLAG_TYPE_MASK) {
        case EFFECT_FLAG_TYPE_AUXILIARY:
            flags.type = Flags::Type::AUXILIARY;
            break;
        case EFFECT_FLAG_TYPE_REPLACE:
            flags.type = Flags::Type::REPLACE;
            break;
        case EFFECT_FLAG_TYPE_PRE_PROC:
            flags.type = Flags::Type::PRE_PROC;
            break;
        case EFFECT_FLAG_TYPE_POST_PROC:
            flags.type = Flags::Type::POST_PROC;
            break;
        default:
            flags.type = Flags::Type::INSERT;
            break;
    }

    switch (legacy & EFFECT_FLAG_INSERT_MASK) {
        case EFFECT_FLAG_INSERT_FIRST:
            flags.insert = Flags::Insert::FIRST;
            break;
        case EFFECT_FLAG_INSERT_LAST:
            flags.insert = Flags::Insert::LAST;
            break;
        case EFFECT_FLAG_INSERT_EXCLUSIVE:
            flags.insert = Flags::Insert::EXCLUSIVE;
            break;
        default:
            flags.insert = Flags::Insert::ANY;
            break;
    }

    switch (legacy & EFFECT_FLAG_VOLUME_MASK) {
        case EFFECT_FLAG_VOLUME_CTRL:
            flags.volume = Flags::Volume::CTRL;
            break;
        case EFFECT_FLAG_VOLUME_IND:
            flags.volume = Flags::Volume::IND;
            break;
        default:
            flags.volume = Flags::Volume::NONE;
            break;
    }

    switch (legacy & EFFECT_FLAG_HW_ACC_MASK) {
        case EFFECT_FLAG_HW_ACC_SIMPLE:
            flags.hwAcceleratorMode = Flags::HardwareAccelerator::SIMPLE;
            break;
        case EFFECT_FLAG_HW_ACC_TUNNEL:
            flags.hwAcceleratorMode = Flags::HardwareAccelerator::TUNNEL;
            break;
        default:
            flags.hwAcceleratorMode = Flags::HardwareAccelerator::NONE;
            break;
    }

    flags.offloadIndication = (legacy & EFFECT_FLAG_OFFLOAD_SUPPORTED) != 0;
    flags.deviceIndication = (legacy & EFFECT_FLAG_DEVICE_IND) != 0;
    flags.audioModeIndication = (legacy & EFFECT_FLAG_AUDIO_MODE_IND) != 0;
    flags.audioSourceIndication = (legacy & EFFECT_FLAG_AUDIO_SOURCE_IND) != 0;
    flags.bypass = (legacy & EFFECT_FLAG_NO_PROCESS) != 0;

    return flags;
}

effect_uuid_t aidlUuidToLegacy(const AudioUuid& uuid) {
    effect_uuid_t out;
    out.timeLow = static_cast<uint32_t>(uuid.timeLow);
    out.timeMid = static_cast<uint16_t>(uuid.timeMid);
    out.timeHiAndVersion = static_cast<uint16_t>(uuid.timeHiAndVersion);
    out.clockSeq = static_cast<uint16_t>(uuid.clockSeq);
    for (size_t i = 0; i < sizeof(out.node) && i < uuid.node.size(); i++) {
        out.node[i] = uuid.node[i];
    }
    return out;
}

}  // namespace

DolbyDapLibrary& DolbyDapLibrary::get() {
    static DolbyDapLibrary instance;
    return instance;
}

DolbyDapLibrary::DolbyDapLibrary() {
    for (const char* path : kDapLibPaths) {
        if (access(path, R_OK) != 0) {
            continue;
        }
        mHandle = dlopen(path, RTLD_NOW);
        if (mHandle == nullptr) {
            LOG(WARNING) << __func__ << " dlopen " << path << " failed: " << dlerror();
            continue;
        }

        auto* lib = static_cast<audio_effect_library_t*>(
                dlsym(mHandle, AUDIO_EFFECT_LIBRARY_INFO_SYM_AS_STR));
        if (lib == nullptr) {
            LOG(ERROR) << __func__ << " no " << AUDIO_EFFECT_LIBRARY_INFO_SYM_AS_STR << " in "
                       << path;
            dlclose(mHandle);
            mHandle = nullptr;
            continue;
        }
        if (lib->tag != AUDIO_EFFECT_LIBRARY_TAG) {
            LOG(ERROR) << __func__ << " bad library tag " << std::hex << lib->tag << " in " << path;
            dlclose(mHandle);
            mHandle = nullptr;
            continue;
        }

        mLib = lib;
        LOG(INFO) << __func__ << " loaded DAP engine from " << path;
        return;
    }
    LOG(WARNING) << __func__ << " libswdap.so not found";
}

bool DolbyDapLibrary::getDescriptor(Descriptor* desc) {
    if (!valid() || mLib->get_descriptor == nullptr || desc == nullptr) {
        return false;
    }

    const effect_uuid_t implUuid = aidlUuidToLegacy(getEffectImplUuidDolbyDap());
    effect_descriptor_t legacy = {};
    if (mLib->get_descriptor(&implUuid, &legacy) != 0) {
        LOG(ERROR) << __func__ << " get_descriptor failed";
        return false;
    }

    desc->common.id.type = legacyUuidToAidl(legacy.type);
    desc->common.id.uuid = legacyUuidToAidl(legacy.uuid);
    desc->common.id.proxy = std::nullopt;
    desc->common.flags = legacyFlagsToAidl(legacy.flags);
    desc->common.cpuLoad = legacy.cpuLoad;
    desc->common.memoryUsage = legacy.memoryUsage;
    desc->common.name = std::string(legacy.name, strnlen(legacy.name, sizeof(legacy.name)));
    desc->common.implementor =
            std::string(legacy.implementor, strnlen(legacy.implementor, sizeof(legacy.implementor)));
    return true;
}

DolbyDapContext::DolbyDapContext(int statusDepth, const Parameter::Common& common)
    : EffectContext(statusDepth, common) {
    auto& library = DolbyDapLibrary::get();
    if (!library.valid() || library.lib()->create_effect == nullptr) {
        LOG(ERROR) << __func__ << " DAP engine unavailable";
        return;
    }

    const effect_uuid_t implUuid = aidlUuidToLegacy(getEffectImplUuidDolbyDap());
    const int32_t sessionId = common.session;
    const int32_t ioId = common.ioHandle;

    if (library.lib()->create_effect(&implUuid, sessionId, ioId, &mHandle) != 0 ||
        mHandle == nullptr) {
        LOG(ERROR) << __func__ << " create_effect failed for session " << sessionId;
        mHandle = nullptr;
        return;
    }

    if (applyConfig(common) != RetCode::SUCCESS) {
        LOG(WARNING) << __func__ << " initial EFFECT_CMD_SET_CONFIG failed";
    }
}

DolbyDapContext::~DolbyDapContext() {
    auto& library = DolbyDapLibrary::get();
    if (mHandle != nullptr && library.valid() && library.lib()->release_effect != nullptr) {
        library.lib()->release_effect(mHandle);
    }
    mHandle = nullptr;
}

int DolbyDapContext::sendCommand(uint32_t cmd, uint32_t inSize, void* inData, uint32_t* outSize,
                                 void* outData) const {
    if (mHandle == nullptr || (*mHandle)->command == nullptr) {
        return -ENODEV;
    }
    return (*mHandle)->command(mHandle, cmd, inSize, inData, outSize, outData);
}

RetCode DolbyDapContext::applyConfig(const Parameter::Common& common) {
    if (mHandle == nullptr) {
        return RetCode::ERROR_EFFECT_LIB_ERROR;
    }

    mChannelCount = ::aidl::android::hardware::audio::common::getChannelCount(
            common.input.base.channelMask);
    if (mChannelCount <= 0) {
        mChannelCount = 2;
    }

    // EffectImpl always hands us deinterleaved-by-frame float PCM, so pin the
    // legacy config to float rather than whatever the blob would default to.
    effect_config_t config = {};
    config.inputCfg.buffer.frameCount = 0;
    config.inputCfg.samplingRate = static_cast<uint32_t>(common.input.base.sampleRate);
    config.inputCfg.channels = audio_channel_out_mask_from_count(mChannelCount);
    config.inputCfg.format = AUDIO_FORMAT_PCM_FLOAT;
    config.inputCfg.accessMode = EFFECT_BUFFER_ACCESS_READ;
    config.inputCfg.mask = EFFECT_CONFIG_ALL;

    config.outputCfg = config.inputCfg;
    config.outputCfg.samplingRate = static_cast<uint32_t>(common.output.base.sampleRate);
    config.outputCfg.accessMode = EFFECT_BUFFER_ACCESS_WRITE;

    int reply = 0;
    uint32_t replySize = sizeof(reply);
    const int ret = sendCommand(EFFECT_CMD_SET_CONFIG, sizeof(config), &config, &replySize, &reply);
    if (ret != 0 || reply != 0) {
        LOG(ERROR) << __func__ << " failed ret=" << ret << " reply=" << reply;
        return RetCode::ERROR_EFFECT_LIB_ERROR;
    }
    return RetCode::SUCCESS;
}

RetCode DolbyDapContext::setCommon(const Parameter::Common& common) {
    const RetCode ret = EffectContext::setCommon(common);
    if (ret != RetCode::SUCCESS) {
        return ret;
    }
    return applyConfig(common);
}

RetCode DolbyDapContext::setParams(const std::vector<uint8_t>& params) {
    if (params.size() < kHeaderSize) {
        LOG(ERROR) << __func__ << " too small " << params.size();
        return RetCode::ERROR_ILLEGAL_PARAMETER;
    }
    if (mHandle == nullptr) {
        return RetCode::ERROR_EFFECT_LIB_ERROR;
    }

    // The payload already is an effect_param_t, so hand it to the blob verbatim.
    int reply = 0;
    uint32_t replySize = sizeof(reply);
    const int ret = sendCommand(EFFECT_CMD_SET_PARAM, static_cast<uint32_t>(params.size()),
                                const_cast<uint8_t*>(params.data()), &replySize, &reply);
    if (ret != 0 || reply != 0) {
        LOG(ERROR) << __func__ << " EFFECT_CMD_SET_PARAM ret=" << ret << " reply=" << reply;
        return RetCode::ERROR_ILLEGAL_PARAMETER;
    }

    mLastParams = params;
    return RetCode::SUCCESS;
}

std::vector<uint8_t> DolbyDapContext::getParams(const std::vector<uint8_t>& id) {
    if (mHandle == nullptr || id.size() < kHeaderSize) {
        return mLastParams;
    }

    const auto* request = reinterpret_cast<const effect_param_t*>(id.data());
    const uint32_t psize = request->psize;
    uint32_t vsize = request->vsize;
    if (psize < sizeof(int32_t)) {
        LOG(ERROR) << __func__ << " psize too small " << psize;
        return mLastParams;
    }
    // Callers that only send an id leave no room for the value; give the blob
    // a word to write into so a plain get still works.
    if (vsize == 0) {
        vsize = sizeof(int32_t);
    }

    const size_t voffset = paddedSize(psize);
    std::vector<uint8_t> reply(kHeaderSize + voffset + vsize, 0);
    std::memcpy(reply.data(), id.data(), std::min(id.size(), kHeaderSize + voffset));

    auto* header = reinterpret_cast<effect_param_t*>(reply.data());
    header->status = 0;
    header->psize = psize;
    header->vsize = vsize;

    uint32_t replySize = static_cast<uint32_t>(reply.size());
    const int ret = sendCommand(EFFECT_CMD_GET_PARAM, static_cast<uint32_t>(kHeaderSize + voffset),
                                reply.data(), &replySize, reply.data());
    if (ret != 0 || replySize == 0) {
        LOG(ERROR) << __func__ << " EFFECT_CMD_GET_PARAM ret=" << ret;
        return mLastParams;
    }

    reply.resize(replySize);
    return reply;
}

RetCode DolbyDapContext::enable() {
    int reply = 0;
    uint32_t replySize = sizeof(reply);
    if (sendCommand(EFFECT_CMD_ENABLE, 0, nullptr, &replySize, &reply) != 0) {
        return RetCode::ERROR_EFFECT_LIB_ERROR;
    }
    return RetCode::SUCCESS;
}

RetCode DolbyDapContext::disable() {
    int reply = 0;
    uint32_t replySize = sizeof(reply);
    if (sendCommand(EFFECT_CMD_DISABLE, 0, nullptr, &replySize, &reply) != 0) {
        return RetCode::ERROR_EFFECT_LIB_ERROR;
    }
    return RetCode::SUCCESS;
}

RetCode DolbyDapContext::reset() {
    if (sendCommand(EFFECT_CMD_RESET, 0, nullptr, nullptr, nullptr) != 0) {
        return RetCode::ERROR_EFFECT_LIB_ERROR;
    }
    return RetCode::SUCCESS;
}

void DolbyDapContext::process(float* in, float* out, int samples) {
    if (mHandle == nullptr || (*mHandle)->process == nullptr || mChannelCount <= 0) {
        if (in != out) {
            std::memcpy(out, in, samples * sizeof(float));
        }
        return;
    }

    audio_buffer_t inBuffer = {};
    audio_buffer_t outBuffer = {};
    inBuffer.frameCount = samples / mChannelCount;
    inBuffer.f32 = in;
    outBuffer.frameCount = inBuffer.frameCount;
    outBuffer.f32 = out;

    const int ret = (*mHandle)->process(mHandle, &inBuffer, &outBuffer);
    // -ENODATA just means the effect is disabled and asked us to pass through.
    if (ret != 0 && in != out) {
        std::memcpy(out, in, samples * sizeof(float));
    }
}

const std::string DolbyDapEffect::kEffectName = "DAP";

DolbyDapEffect::~DolbyDapEffect() {
    cleanUp();
}

ndk::ScopedAStatus DolbyDapEffect::getDescriptor(Descriptor* _aidl_return) {
    RETURN_IF(!_aidl_return, EX_ILLEGAL_ARGUMENT, "nullDescriptor");
    RETURN_IF(!DolbyDapLibrary::get().getDescriptor(_aidl_return), EX_UNSUPPORTED_OPERATION,
              "noDescriptor");
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus DolbyDapEffect::setParameterSpecific(const Parameter::Specific& specific) {
    RETURN_IF(Parameter::Specific::vendorEffect != specific.getTag(), EX_ILLEGAL_ARGUMENT,
              "EffectNotSupported");
    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");

    auto& vendorEffect = specific.get<Parameter::Specific::vendorEffect>();
    std::optional<DefaultExtension> defaultExt;
    RETURN_IF(STATUS_OK != vendorEffect.extension.getParcelable(&defaultExt), EX_ILLEGAL_ARGUMENT,
              "getParcelableFailed");
    RETURN_IF(!defaultExt.has_value(), EX_ILLEGAL_ARGUMENT, "parcelableNull");
    RETURN_IF(mContext->setParams(defaultExt->bytes) != RetCode::SUCCESS, EX_ILLEGAL_ARGUMENT,
              "paramNotSupported");

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus DolbyDapEffect::getParameterSpecific(const Parameter::Id& id,
                                                        Parameter::Specific* specific) {
    RETURN_IF(Parameter::Id::vendorEffectTag != id.getTag(), EX_ILLEGAL_ARGUMENT, "wrongIdTag");
    RETURN_IF(!mContext, EX_NULL_POINTER, "nullContext");

    auto extensionId = id.get<Parameter::Id::vendorEffectTag>();
    std::optional<DefaultExtension> defaultIdExt;
    RETURN_IF(STATUS_OK != extensionId.extension.getParcelable(&defaultIdExt), EX_ILLEGAL_ARGUMENT,
              "getIdParcelableFailed");
    RETURN_IF(!defaultIdExt.has_value(), EX_ILLEGAL_ARGUMENT, "parcelableIdNull");

    VendorExtension extension;
    DefaultExtension defaultExt;
    defaultExt.bytes = mContext->getParams(defaultIdExt->bytes);
    RETURN_IF(STATUS_OK != extension.extension.setParcelable(defaultExt), EX_ILLEGAL_ARGUMENT,
              "setParcelableFailed");
    specific->set<Parameter::Specific::vendorEffect>(extension);
    return ndk::ScopedAStatus::ok();
}

std::shared_ptr<EffectContext> DolbyDapEffect::createContext(const Parameter::Common& common) {
    if (mContext) {
        LOG(DEBUG) << __func__ << " context already exists";
        return mContext;
    }

    auto context = std::make_shared<DolbyDapContext>(1 /* statusDepth */, common);
    if (!context->initialized()) {
        LOG(ERROR) << __func__ << " failed to create DAP instance";
        return nullptr;
    }

    mContext = std::move(context);
    return mContext;
}

RetCode DolbyDapEffect::releaseContext() {
    if (mContext) {
        mContext.reset();
    }
    return RetCode::SUCCESS;
}

IEffect::Status DolbyDapEffect::effectProcessImpl(float* in, float* out, int samples) {
    if (mContext) {
        mContext->process(in, out, samples);
    } else if (in != out) {
        std::memcpy(out, in, samples * sizeof(float));
    }
    return {STATUS_OK, samples, samples};
}

}  // namespace aidl::android::hardware::audio::effect
