// IPower for a PC. The one mode that does something is LOW_POWER (Android's battery saver), which
// switches to the saving profile; every other mode, the boosts and the hint sessions are reported as
// unsupported, so the framework does not send them.
#pragma once

#include "Controller.h"

#include <aidl/android/hardware/power/BnPower.h>
#include <aidl/android/hardware/power/SessionTag.h>

#include <memory>
#include <vector>

namespace aidl::android::hardware::power::impl::pc {

class Power : public BnPower {
  public:
    explicit Power(std::shared_ptr<::pc::power::Controller> controller);

    ndk::ScopedAStatus setMode(Mode type, bool enabled) override;
    ndk::ScopedAStatus isModeSupported(Mode type, bool* _aidl_return) override;
    ndk::ScopedAStatus setBoost(Boost type, int32_t durationMs) override;
    ndk::ScopedAStatus isBoostSupported(Boost type, bool* _aidl_return) override;
    ndk::ScopedAStatus createHintSession(int32_t tgid, int32_t uid,
                                         const std::vector<int32_t>& threadIds,
                                         int64_t durationNanos,
                                         std::shared_ptr<IPowerHintSession>* _aidl_return) override;
    ndk::ScopedAStatus getHintSessionPreferredRate(int64_t* _aidl_return) override;
    ndk::ScopedAStatus createHintSessionWithConfig(
            int32_t tgid, int32_t uid, const std::vector<int32_t>& threadIds,
            int64_t durationNanos, SessionTag tag, SessionConfig* config,
            std::shared_ptr<IPowerHintSession>* _aidl_return) override;
    ndk::ScopedAStatus getSessionChannel(int32_t tgid, int32_t uid,
                                         ChannelConfig* _aidl_return) override;
    ndk::ScopedAStatus closeSessionChannel(int32_t tgid, int32_t uid) override;
    ndk::ScopedAStatus getSupportInfo(SupportInfo* _aidl_return) override;
    ndk::ScopedAStatus getCpuHeadroom(const CpuHeadroomParams& params,
                                      CpuHeadroomResult* _aidl_return) override;
    ndk::ScopedAStatus getGpuHeadroom(const GpuHeadroomParams& params,
                                      GpuHeadroomResult* _aidl_return) override;
    ndk::ScopedAStatus sendCompositionData(const std::vector<CompositionData>& data) override;
    ndk::ScopedAStatus sendCompositionUpdate(const CompositionUpdate& update) override;

  private:
    std::shared_ptr<::pc::power::Controller> controller_;
};

}  // namespace aidl::android::hardware::power::impl::pc
