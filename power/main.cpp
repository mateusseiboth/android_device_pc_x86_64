// android.hardware.power-service.pc: power profiles, device power saving on battery and the charge
// limit for PCs, behind Android's IPower (see README, "Power").
#include "Controller.h"
#include "Power.h"
#include "Watchers.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>

using aidl::android::hardware::power::impl::pc::Power;

int main() {
    auto controller = std::make_shared<pc::power::Controller>();
    // Before the service shows up, so Settings finds the capability properties already set.
    controller->update(true);
    std::thread(pc::power::watchSettings, std::ref(*controller)).detach();
    std::thread(pc::power::watchUevents, std::ref(*controller)).detach();

    ABinderProcess_setThreadPoolMaxThreadCount(0);
    std::shared_ptr<Power> power = ndk::SharedRefBase::make<Power>(controller);
    const std::string instance = std::string(Power::descriptor) + "/default";
    CHECK_EQ(AServiceManager_addService(power->asBinder().get(), instance.c_str()), STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // joinThreadPool does not return
}
