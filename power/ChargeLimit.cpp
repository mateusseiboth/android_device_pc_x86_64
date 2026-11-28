#include "ChargeLimit.h"

#include "PowerSupply.h"
#include "Sysfs.h"

#include <android-base/parseint.h>

#include <algorithm>
#include <utility>

namespace pc::power {
namespace {

constexpr char kIdeapadDriver[] = "/sys/bus/platform/drivers/ideapad_acpi";

// Charging starts again this far below the limit, so a PC left plugged in does not top the battery
// up after every percent it loses (ThinkPads and Dells take a start threshold).
constexpr int kStartGap = 5;

// IdeaPad's conservation mode stops at 60% on most models (some at 80%); Samsung's battery life
// extender, the other Long_Life charge type, stops at 80%.
constexpr int kIdeapadCap = 60;
constexpr int kOtherLongLifeCap = 80;

bool isLimited(int limit) {
    return limit > 0 && limit < 100;
}

// charge_control_end_threshold (ThinkPad, ASUS, Dell, Huawei, LG, MSI, System76, Framework...),
// with charge_control_start_threshold where the firmware has one.
class ThresholdLimiter : public ChargeLimiter {
  public:
    explicit ThresholdLimiter(std::string battery) : battery_(std::move(battery)) {}

    void apply(int limit) const override {
        const bool limited = isLimited(limit);
        // Dell keeps the thresholds for its Custom charge type; Standard charges to 100%.
        const bool hasCustomType = contains(readChoices(battery_ + "/charge_types"), "Custom");
        if (hasCustomType && !limited) {
            writeValue(battery_ + "/charge_types", "Standard");
            return;
        }
        writeThresholds(limited ? std::max(limit - kStartGap, 0) : 0, limited ? limit : 100);
        if (hasCustomType) writeValue(battery_ + "/charge_types", "Custom");
    }

    std::optional<int> fixedCap() const override { return std::nullopt; }

    const std::string& path() const override { return battery_; }

  private:
    // The start must stay below the end: the end goes first when it rises, last when it drops.
    void writeThresholds(int start, int end) const {
        const std::string endPath = battery_ + "/charge_control_end_threshold";
        const std::string startPath = battery_ + "/charge_control_start_threshold";
        int currentEnd = 100;
        android::base::ParseInt(readValue(endPath), &currentEnd);
        const bool rising = end >= currentEnd;
        writeValue(rising ? endPath : startPath, std::to_string(rising ? end : start));
        writeValue(rising ? startPath : endPath, std::to_string(rising ? start : end));
    }

    std::string battery_;
};

// A charge_types Long_Life on the battery (IdeaPad on newer kernels, Samsung): a fixed cap.
class LongLifeLimiter : public ChargeLimiter {
  public:
    explicit LongLifeLimiter(std::string battery) : battery_(std::move(battery)) {}

    void apply(int limit) const override {
        writeValue(battery_ + "/charge_types", isLimited(limit) ? "Long_Life" : "Standard");
    }

    std::optional<int> fixedCap() const override {
        return exists(kIdeapadDriver) ? kIdeapadCap : kOtherLongLifeCap;
    }

    const std::string& path() const override { return battery_; }

  private:
    std::string battery_;
};

// IdeaPad's conservation_mode on the platform device: the same fixed cap through the older file.
class ConservationModeLimiter : public ChargeLimiter {
  public:
    explicit ConservationModeLimiter(std::string file) : file_(std::move(file)) {}

    void apply(int limit) const override { writeValue(file_, isLimited(limit) ? "1" : "0"); }

    std::optional<int> fixedCap() const override { return kIdeapadCap; }

    const std::string& path() const override { return file_; }

  private:
    std::string file_;
};

std::unique_ptr<ChargeLimiter> limiterForBattery(const std::string& battery) {
    if (exists(battery + "/charge_control_end_threshold")) {
        return std::make_unique<ThresholdLimiter>(battery);
    }
    if (contains(readChoices(battery + "/charge_types"), "Long_Life")) {
        return std::make_unique<LongLifeLimiter>(battery);
    }
    return nullptr;
}

void addConservationModes(std::vector<std::unique_ptr<ChargeLimiter>>& limiters) {
    for (const std::string& device : listDir(kIdeapadDriver)) {
        const std::string file = std::string(kIdeapadDriver) + "/" + device + "/conservation_mode";
        if (exists(file)) limiters.push_back(std::make_unique<ConservationModeLimiter>(file));
    }
}

}  // namespace

std::vector<std::unique_ptr<ChargeLimiter>> findChargeLimiters() {
    std::vector<std::unique_ptr<ChargeLimiter>> limiters;
    for (const std::string& battery : systemBatteries()) {
        std::unique_ptr<ChargeLimiter> limiter = limiterForBattery(battery);
        if (limiter) limiters.push_back(std::move(limiter));
    }
    // Kernels that give IdeaPads the Long_Life charge type keep conservation_mode only as a
    // deprecated copy of it; one control is enough.
    if (limiters.empty()) addConservationModes(limiters);
    return limiters;
}

}  // namespace pc::power
