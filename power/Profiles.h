// Power profiles: the CPU's energy/performance preference (intel_pstate and amd-pstate EPP) and the
// firmware's ACPI platform profile, set together.
#pragma once

#include <string>
#include <vector>

namespace pc::power {

enum class Profile { kBalanced, kSaving, kPerformance };

// The name Settings stores in persist.sys.pc.power.profile.
const char* profileName(Profile profile);

// Unknown or empty names are the balanced profile.
Profile parseProfile(const std::string& name);

// The profiles at least one knob of this machine can apply, in Settings' order; empty when the
// machine has neither EPP nor a platform profile.
std::vector<Profile> availableProfiles();

void applyProfile(Profile profile);

}  // namespace pc::power
