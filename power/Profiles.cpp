#include "Profiles.h"

#include "Sysfs.h"

#include <android-base/logging.h>
#include <android-base/strings.h>

#include <array>

namespace pc::power {
namespace {

constexpr char kCpuPolicies[] = "/sys/devices/system/cpu/cpufreq";
constexpr char kPlatformProfile[] = "/sys/firmware/acpi/platform_profile";
constexpr char kPlatformProfileChoices[] = "/sys/firmware/acpi/platform_profile_choices";

// What each profile asks of each knob, best match first: machines name the same idea differently
// (ThinkPads say low-power, ASUS says quiet, Dell and HP say cool).
struct ProfileValues {
    Profile profile;
    const char* name;
    std::vector<std::string> epp;
    std::vector<std::string> platform;
};

const std::array<ProfileValues, 3>& profileTable() {
    static const std::array<ProfileValues, 3> table = {{
            {Profile::kBalanced, "balanced", {"balance_performance", "default"}, {"balanced"}},
            {Profile::kSaving, "saving", {"power", "balance_power"}, {"low-power", "quiet", "cool"}},
            {Profile::kPerformance, "performance", {"performance"},
             {"performance", "balanced-performance"}},
    }};
    return table;
}

const ProfileValues& valuesOf(Profile profile) {
    for (const ProfileValues& values : profileTable()) {
        if (values.profile == profile) return values;
    }
    return profileTable()[0];
}

std::vector<std::string> cpuPolicies() {
    std::vector<std::string> policies;
    for (const std::string& name : listDir(kCpuPolicies)) {
        if (android::base::StartsWith(name, "policy")) {
            policies.push_back(std::string(kCpuPolicies) + "/" + name);
        }
    }
    return policies;
}

std::string eppChoice(const std::string& policy, const ProfileValues& values) {
    return firstOffered(values.epp,
                        readChoices(policy + "/energy_performance_available_preferences"));
}

std::string platformChoice(const ProfileValues& values) {
    return firstOffered(values.platform, readChoices(kPlatformProfileChoices));
}

bool canApply(const ProfileValues& values) {
    if (!platformChoice(values).empty()) return true;
    for (const std::string& policy : cpuPolicies()) {
        if (!eppChoice(policy, values).empty()) return true;
    }
    return false;
}

}  // namespace

const char* profileName(Profile profile) {
    return valuesOf(profile).name;
}

Profile parseProfile(const std::string& name) {
    for (const ProfileValues& values : profileTable()) {
        if (name == values.name) return values.profile;
    }
    return Profile::kBalanced;
}

std::vector<Profile> availableProfiles() {
    std::vector<Profile> profiles;
    for (const ProfileValues& values : profileTable()) {
        if (canApply(values)) profiles.push_back(values.profile);
    }
    return profiles;
}

void applyProfile(Profile profile) {
    const ProfileValues& values = valuesOf(profile);
    LOG(INFO) << "profile " << values.name;
    // intel_pstate refuses an EPP other than performance while the CPU runs the performance
    // governor; that refusal is logged and the platform profile still applies.
    for (const std::string& policy : cpuPolicies()) {
        const std::string epp = eppChoice(policy, values);
        if (!epp.empty()) writeValue(policy + "/energy_performance_preference", epp);
    }
    const std::string platform = platformChoice(values);
    if (!platform.empty()) writeValue(kPlatformProfile, platform);
}

}  // namespace pc::power
