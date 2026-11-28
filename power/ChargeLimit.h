// The charge limit ("Protect the battery"): stop charging below 100% to make the battery last longer.
// Each kind of firmware control is one ChargeLimiter; findChargeLimiters() picks the ones this PC has.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pc::power {

class ChargeLimiter {
  public:
    virtual ~ChargeLimiter() = default;

    // limit: a percentage, or 0 to charge to 100% again.
    virtual void apply(int limit) const = 0;

    // The cap when the firmware only has a fixed one, whatever percentage is asked for.
    virtual std::optional<int> fixedCap() const = 0;

    // The sysfs file it drives, to tell when the set of controls changes.
    virtual const std::string& path() const = 0;
};

std::vector<std::unique_ptr<ChargeLimiter>> findChargeLimiters();

}  // namespace pc::power
