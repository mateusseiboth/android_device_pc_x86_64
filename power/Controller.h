// Ties the Power HAL's pieces together: Settings' choices (persist.sys.pc.power.*), Android's battery
// saver (Mode::LOW_POWER), the power source and the devices present. Everything runs under one lock,
// from the binder thread, the property watcher and the uevent watcher.
#pragma once

#include "ChargeLimit.h"
#include "DeviceTuning.h"
#include "Profiles.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace pc::power {

// Set by Settings (system_ext policy: pc_power_settings_prop), read here.
constexpr char kProfileProp[] = "persist.sys.pc.power.profile";
constexpr char kChargeLimitProp[] = "persist.sys.pc.power.charge_limit";

// Set here (vendor policy: vendor_pc_power_prop), read by Settings.
constexpr char kProfilesProp[] = "vendor.pc.power.profiles";
constexpr char kChargeLimitSupportedProp[] = "vendor.pc.power.charge_limit";
constexpr char kChargeLimitFixedProp[] = "vendor.pc.power.charge_limit_fixed";
// Only for this HAL: the device knobs hold battery values. It outlives a crash of the HAL, not a
// reboot, and tells the next run that the values it finds are not the kernel's.
constexpr char kBatteryTunedProp[] = "vendor.pc.power.battery_tuned";

class Controller {
  public:
    Controller();

    // Applies whatever changed since the last call: Settings' choices, the power source, knobs that
    // showed up. devicesChanged: devices or drivers came up, so their knobs get the current values.
    void update(bool devicesChanged);

    void setLowPower(bool enabled);

    // Whether battery saver has a profile to switch to on this PC.
    bool canSave();

  private:
    // Returns whether the profiles this PC can apply changed (a driver with a platform profile
    // came up), so the profile is applied again.
    bool updateCapabilities();
    void updateProfile(bool force);
    void updateDeviceTuning(bool force);
    void updateChargeLimit();

    std::mutex mutex_;
    bool lowPower_ = false;
    std::vector<Profile> profiles_;
    std::vector<std::unique_ptr<ChargeLimiter>> limiters_;
    std::optional<Profile> appliedProfile_;
    std::optional<bool> onBattery_;
    std::string appliedChargeLimit_;
    DeviceTuning tuning_;
};

}  // namespace pc::power
