#include "DeviceTuning.h"

#include "Sysfs.h"

#include <android-base/logging.h>

#include <array>

namespace pc::power {
namespace {

constexpr char kPciDevices[] = "/sys/bus/pci/devices";
constexpr char kUsbDevices[] = "/sys/bus/usb/devices";
constexpr char kScsiHosts[] = "/sys/class/scsi_host";

// USB classes that stay awake: HID (keyboards, mice, touchpads) would lag on the first move after
// a pause; audio (headsets) clicks and drops the start of a sound; wireless controllers are the
// Bluetooth adapters that Bluetooth keyboards and mice go through.
constexpr std::array<const char*, 3> kAwakeUsbClasses = {"01", "03", "e0"};

bool isAwakeUsbClass(const std::string& usbClass) {
    for (const char* awake : kAwakeUsbClasses) {
        if (usbClass == awake) return true;
    }
    return false;
}

// A USB device stays awake when the device itself or any of its interfaces ("1-2:1.0" next to
// "1-2") has one of those classes.
bool mustStayAwake(const std::string& device, const std::vector<std::string>& entries) {
    if (isAwakeUsbClass(readValue(std::string(kUsbDevices) + "/" + device + "/bDeviceClass"))) {
        return true;
    }
    const std::string prefix = device + ":";
    for (const std::string& entry : entries) {
        if (entry.rfind(prefix, 0) != 0) continue;
        const std::string path = std::string(kUsbDevices) + "/" + entry + "/bInterfaceClass";
        if (isAwakeUsbClass(readValue(path))) return true;
    }
    return false;
}

}  // namespace

DeviceTuning::DeviceTuning(bool valuesAreDefaults) : valuesAreDefaults_(valuesAreDefaults) {}

void DeviceTuning::apply(bool onBattery) {
    LOG(INFO) << (onBattery ? "on battery: device power saving" : "on AC: device defaults");
    for (const Knob& knob : knobs()) {
        onBattery ? toBattery(knob) : toAc(knob);
    }
    if (onBattery) return;
    // Once AC has put every knob back, what the knobs hold are AC values again, worth saving.
    // Values of devices unplugged on battery go too: a new device on that port has its own.
    valuesAreDefaults_ = true;
    saved_.clear();
}

std::vector<DeviceTuning::Knob> DeviceTuning::knobs() {
    // ASPM's powersupersave adds the L1 substates; kernels or firmware without them take powersave.
    std::vector<Knob> knobs = {
            {"/sys/module/pcie_aspm/parameters/policy", {"powersupersave", "powersave"}, "default"},
            {"/sys/module/snd_hda_intel/parameters/power_save", {"1"}, "0"},
            {"/sys/module/snd_hda_intel/parameters/power_save_controller", {"Y"}, "N"},
    };
    addSataHosts(knobs);
    addPciDevices(knobs);
    addUsbDevices(knobs);
    return knobs;
}

void DeviceTuning::addSataHosts(std::vector<Knob>& knobs) {
    for (const std::string& host : listDir(kScsiHosts)) {
        knobs.push_back({std::string(kScsiHosts) + "/" + host + "/link_power_management_policy",
                         {"med_power_with_dipm"},
                         "max_performance"});
    }
}

// Every PCI device, GPUs and USB controllers included: a driver without runtime PM support just
// ignores auto. If a machine loses USB devices on battery, the USB controllers (class 0x0c03) are
// the first ones to leave out here.
void DeviceTuning::addPciDevices(std::vector<Knob>& knobs) {
    for (const std::string& device : listDir(kPciDevices)) {
        knobs.push_back({std::string(kPciDevices) + "/" + device + "/power/control", {"auto"}, "on"});
    }
}

void DeviceTuning::addUsbDevices(std::vector<Knob>& knobs) {
    const std::vector<std::string> entries = listDir(kUsbDevices);
    for (const std::string& entry : entries) {
        // Interfaces ("1-2:1.0") have no power control of their own.
        if (entry.find(':') != std::string::npos || mustStayAwake(entry, entries)) continue;
        knobs.push_back({std::string(kUsbDevices) + "/" + entry + "/power/control", {"auto"}, "on"});
    }
}

void DeviceTuning::toBattery(const Knob& knob) {
    if (!exists(knob.path)) return;
    if (valuesAreDefaults_ && saved_.count(knob.path) == 0) {
        saved_[knob.path] = readCurrentChoice(knob.path);
    }
    for (const std::string& value : knob.battery) {
        if (writeValue(knob.path, value)) return;
    }
}

void DeviceTuning::toAc(const Knob& knob) {
    auto saved = saved_.find(knob.path);
    if (saved != saved_.end()) {
        writeValue(knob.path, saved->second);
        saved_.erase(saved);
        return;
    }
    // Never changed by this run: the kernel's value stays, unless an earlier run changed it.
    if (!valuesAreDefaults_) writeValue(knob.path, knob.ac);
}

}  // namespace pc::power
