#include "PowerSupply.h"

#include "Sysfs.h"

#include <algorithm>

namespace pc::power {
namespace {

std::vector<std::string> suppliesOfType(const std::string& type) {
    std::vector<std::string> supplies;
    for (const std::string& name : listDir(kPowerSupplies)) {
        const std::string path = std::string(kPowerSupplies) + "/" + name;
        if (readValue(path + "/type") == type && readValue(path + "/scope") != "Device") {
            supplies.push_back(path);
        }
    }
    return supplies;
}

bool isOnline(const std::string& supply) {
    return readValue(supply + "/online") == "1";
}

bool isDischarging(const std::string& battery) {
    return readValue(battery + "/status") == "Discharging";
}

}  // namespace

std::vector<std::string> systemBatteries() {
    return suppliesOfType("Battery");
}

bool isOnBattery() {
    const std::vector<std::string> batteries = systemBatteries();
    if (batteries.empty()) return false;
    const std::vector<std::string> mains = suppliesOfType("Mains");
    if (!mains.empty()) return std::none_of(mains.begin(), mains.end(), isOnline);
    // No AC adapter device at all (some laptops charge only through USB-C, whose supplies do not
    // say which way the power flows): the battery's own status tells.
    return std::any_of(batteries.begin(), batteries.end(), isDischarging);
}

}  // namespace pc::power
