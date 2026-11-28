#include "Power.h"

#include <utility>

namespace aidl::android::hardware::power::impl::pc {
namespace {

ndk::ScopedAStatus unsupported() {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}  // namespace

Power::Power(std::shared_ptr<::pc::power::Controller> controller)
    : controller_(std::move(controller)) {}

ndk::ScopedAStatus Power::setMode(Mode type, bool enabled) {
    if (type == Mode::LOW_POWER) controller_->setLowPower(enabled);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::isModeSupported(Mode type, bool* _aidl_return) {
    *_aidl_return = type == Mode::LOW_POWER && controller_->canSave();
    return ndk::ScopedAStatus::ok();
}

// A PC CPU ramps up on its own (intel_pstate/amd-pstate with HWP/CPPC): no boost to give.
ndk::ScopedAStatus Power::setBoost(Boost, int32_t) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::isBoostSupported(Boost, bool* _aidl_return) {
    *_aidl_return = false;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::createHintSession(int32_t, int32_t, const std::vector<int32_t>&, int64_t,
                                            std::shared_ptr<IPowerHintSession>* _aidl_return) {
    *_aidl_return = nullptr;
    return unsupported();
}

ndk::ScopedAStatus Power::getHintSessionPreferredRate(int64_t* _aidl_return) {
    *_aidl_return = -1;
    return unsupported();
}

ndk::ScopedAStatus Power::createHintSessionWithConfig(
        int32_t, int32_t, const std::vector<int32_t>&, int64_t, SessionTag, SessionConfig*,
        std::shared_ptr<IPowerHintSession>* _aidl_return) {
    *_aidl_return = nullptr;
    return unsupported();
}

ndk::ScopedAStatus Power::getSessionChannel(int32_t, int32_t, ChannelConfig*) {
    return unsupported();
}

ndk::ScopedAStatus Power::closeSessionChannel(int32_t, int32_t) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::getSupportInfo(SupportInfo* _aidl_return) {
    SupportInfo info;
    info.usesSessions = false;
    info.modes = controller_->canSave() ? 1LL << static_cast<int32_t>(Mode::LOW_POWER) : 0;
    info.boosts = 0;
    info.sessionHints = 0;
    info.sessionModes = 0;
    info.sessionTags = 0;
    info.compositionData.isSupported = false;
    info.headroom.isCpuSupported = false;
    info.headroom.isGpuSupported = false;
    *_aidl_return = info;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::getCpuHeadroom(const CpuHeadroomParams&, CpuHeadroomResult*) {
    return unsupported();
}

ndk::ScopedAStatus Power::getGpuHeadroom(const GpuHeadroomParams&, GpuHeadroomResult*) {
    return unsupported();
}

ndk::ScopedAStatus Power::sendCompositionData(const std::vector<CompositionData>&) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::sendCompositionUpdate(const CompositionUpdate&) {
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::power::impl::pc
