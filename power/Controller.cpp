#include "Controller.h"

#include "PowerSupply.h"

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/strings.h>

#include <algorithm>
#include <utility>

namespace pc::power {
namespace {

using android::base::GetBoolProperty;
using android::base::GetProperty;

void setIfChanged(const char* name, const std::string& value) {
    if (GetProperty(name, "") != value) android::base::SetProperty(name, value);
}

// A fixed cap only when no control takes a percentage.
std::optional<int> fixedCapOf(const std::vector<std::unique_ptr<ChargeLimiter>>& limiters) {
    if (limiters.empty()) return std::nullopt;
    for (const std::unique_ptr<ChargeLimiter>& limiter : limiters) {
        if (!limiter->fixedCap()) return std::nullopt;
    }
    return limiters.front()->fixedCap();
}

}  // namespace

Controller::Controller() : tuning_(!GetBoolProperty(kBatteryTunedProp, false)) {}

void Controller::update(bool devicesChanged) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool profilesChanged = updateCapabilities();
    updateProfile(devicesChanged || profilesChanged);
    updateDeviceTuning(devicesChanged);
    updateChargeLimit();
}

void Controller::setLowPower(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG(INFO) << "battery saver " << (enabled ? "on" : "off");
    lowPower_ = enabled;
    updateProfile(false);
}

bool Controller::canSave() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::find(profiles_.begin(), profiles_.end(), Profile::kSaving) != profiles_.end();
}

bool Controller::updateCapabilities() {
    std::vector<Profile> profiles = availableProfiles();
    const bool profilesChanged = profiles != profiles_;
    profiles_ = std::move(profiles);
    limiters_ = findChargeLimiters();

    std::vector<std::string> names;
    for (Profile profile : profiles_) names.emplace_back(profileName(profile));
    setIfChanged(kProfilesProp, android::base::Join(names, ","));
    setIfChanged(kChargeLimitSupportedProp, limiters_.empty() ? "0" : "1");
    // A property cannot be removed; Settings reads 0 like an unset one.
    const std::optional<int> fixedCap = fixedCapOf(limiters_);
    if (fixedCap || !GetProperty(kChargeLimitFixedProp, "").empty()) {
        setIfChanged(kChargeLimitFixedProp, std::to_string(fixedCap.value_or(0)));
    }
    return profilesChanged;
}

void Controller::updateProfile(bool force) {
    const Profile target =
            lowPower_ ? Profile::kSaving : parseProfile(GetProperty(kProfileProp, ""));
    if (!force && appliedProfile_ == target) return;
    applyProfile(target);
    appliedProfile_ = target;
}

void Controller::updateDeviceTuning(bool force) {
    const bool onBattery = isOnBattery();
    // New devices on battery get the battery values too; on AC they keep the kernel's.
    const bool newDevicesOnBattery = force && onBattery;
    if (onBattery_ == onBattery && !newDevicesOnBattery) return;
    tuning_.apply(onBattery);
    onBattery_ = onBattery;
    setIfChanged(kBatteryTunedProp, onBattery ? "1" : "0");
}

void Controller::updateChargeLimit() {
    const std::string text = GetProperty(kChargeLimitProp, "");
    // Never chosen in Settings: the firmware keeps what it has, which another system on this PC
    // may have set.
    if (text.empty()) return;
    int limit = 0;
    if (!android::base::ParseInt(text, &limit, 0, 100)) return;
    // Applied again when a battery or a control shows up (a laptop driver loading late).
    std::string applied = text;
    for (const std::unique_ptr<ChargeLimiter>& limiter : limiters_) applied += "|" + limiter->path();
    if (applied == appliedChargeLimit_) return;
    LOG(INFO) << "charge limit " << limit << " on " << limiters_.size() << " control(s)";
    for (const std::unique_ptr<ChargeLimiter>& limiter : limiters_) limiter->apply(limit);
    appliedChargeLimit_ = applied;
}

}  // namespace pc::power
