// Where the PC draws its power from, read from /sys/class/power_supply.
#pragma once

#include <string>
#include <vector>

namespace pc::power {

constexpr char kPowerSupplies[] = "/sys/class/power_supply";

// The PC's own batteries. A wireless mouse or a gamepad reports its battery here too, with scope
// Device; those are not the PC's.
std::vector<std::string> systemBatteries();

// True only when the PC runs on its battery. A desktop without a battery is always on AC.
bool isOnBattery();

}  // namespace pc::power
