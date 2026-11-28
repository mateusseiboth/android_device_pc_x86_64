// Device power saving on battery: PCIe ASPM, USB autosuspend, SATA link power, HD Audio power save and
// PCI runtime PM. On AC every knob goes back to the value it had before the HAL changed it.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace pc::power {

class DeviceTuning {
  public:
    // valuesAreDefaults: the knobs still hold the kernel's values. False when an earlier run of the
    // HAL left battery values behind (it crashed on battery): those are not worth saving, and AC
    // then gets each knob's usual AC value instead.
    explicit DeviceTuning(bool valuesAreDefaults);

    // Applies the battery or the AC side to every knob present now, new devices included.
    void apply(bool onBattery);

  private:
    struct Knob {
        std::string path;
        std::vector<std::string> battery;  // first value the driver takes wins
        std::string ac;                    // only when the kernel's own value is unknown
    };

    static std::vector<Knob> knobs();
    static void addPciDevices(std::vector<Knob>& knobs);
    static void addUsbDevices(std::vector<Knob>& knobs);
    static void addSataHosts(std::vector<Knob>& knobs);
    void toBattery(const Knob& knob);
    void toAc(const Knob& knob);

    bool valuesAreDefaults_;
    std::map<std::string, std::string> saved_;  // path -> the value before the battery side
};

}  // namespace pc::power
