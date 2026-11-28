// The two threads that keep the Controller current. Neither returns.
#pragma once

#include "Controller.h"

namespace pc::power {

// Settings' choices: persist.sys.pc.power.profile and .charge_limit, also when the persistent
// properties load from /data after the HAL started.
void watchSettings(Controller& controller);

// Kernel uevents: AC plugged or unplugged (power_supply), and devices or drivers coming up.
void watchUevents(Controller& controller);

}  // namespace pc::power
