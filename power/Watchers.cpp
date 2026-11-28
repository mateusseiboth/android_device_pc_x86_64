#include "Watchers.h"

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/unique_fd.h>

#include <linux/netlink.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/system_properties.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>

namespace pc::power {
namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

// Uevents come in bursts (ueventd's coldboot replays every device; plugging in AC sends a few):
// the Controller runs once the burst settles, and at the latest kMaxWait after its first event.
constexpr milliseconds kSettle{500};
constexpr milliseconds kMaxWait{3000};
constexpr int kReceiveBuffer = 256 * 1024;

std::string readSettings() {
    return android::base::GetProperty(kProfileProp, "") + "/" +
           android::base::GetProperty(kChargeLimitProp, "");
}

struct Pending {
    bool any = false;
    bool devices = false;
    Clock::time_point first;
    Clock::time_point last;

    void mark(bool devicesChanged) {
        const Clock::time_point now = Clock::now();
        if (!any) first = now;
        any = true;
        devices = devices || devicesChanged;
        last = now;
    }

    int pollTimeoutMs() const {
        if (!any) return -1;
        const Clock::time_point due = std::min(last + kSettle, first + kMaxWait);
        const auto left = std::chrono::duration_cast<milliseconds>(due - Clock::now()).count();
        return static_cast<int>(std::max<long long>(left, 0));
    }
};

// A uevent is "ACTION@DEVPATH" followed by NUL-separated KEY=VALUE fields.
void classify(const char* message, size_t length, Pending& pending) {
    std::string action;
    std::string subsystem;
    for (size_t at = strnlen(message, length) + 1; at < length;) {
        const std::string field(message + at, strnlen(message + at, length - at));
        at += field.size() + 1;
        if (field.rfind("ACTION=", 0) == 0) action = field.substr(7);
        if (field.rfind("SUBSYSTEM=", 0) == 0) subsystem = field.substr(10);
    }
    const bool devicesChanged = action == "add" || action == "bind";
    if (devicesChanged || subsystem == "power_supply") pending.mark(devicesChanged);
}

android::base::unique_fd openUeventSocket() {
    android::base::unique_fd fd(socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT));
    if (fd < 0) return fd;
    // A plain SO_RCVBUF: SO_RCVBUFFORCE would need CAP_NET_ADMIN. If coldboot overflows the
    // buffer anyway, ENOBUFS below counts as "something changed".
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &kReceiveBuffer, sizeof(kReceiveBuffer));
    sockaddr_nl address = {};
    address.nl_family = AF_NETLINK;
    address.nl_groups = 1;  // the kernel's uevents
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        return android::base::unique_fd();
    }
    return fd;
}

void receiveAll(int fd, Pending& pending) {
    char message[8192];
    while (true) {
        sockaddr_nl sender = {};
        socklen_t senderLength = sizeof(sender);
        const ssize_t length = recvfrom(fd, message, sizeof(message), MSG_DONTWAIT,
                                        reinterpret_cast<sockaddr*>(&sender), &senderLength);
        if (length < 0 && errno == ENOBUFS) {
            pending.mark(true);
            continue;
        }
        if (length < 0) return;
        // Only the kernel's own messages (port 0).
        if (sender.nl_pid != 0) continue;
        classify(message, static_cast<size_t>(length), pending);
    }
}

}  // namespace

void watchSettings(Controller& controller) {
    // The serial is taken before reading, so a change landing in between still wakes the wait.
    uint32_t serial = __system_property_area_serial();
    std::string applied = "\n";  // matches nothing: the first pass always updates
    while (true) {
        const std::string settings = readSettings();
        if (settings != applied) {
            applied = settings;
            controller.update(false);
        }
        // Wakes on any property change in the system; only ours lead anywhere.
        __system_property_wait(nullptr, serial, &serial, nullptr);
    }
}

void watchUevents(Controller& controller) {
    android::base::unique_fd fd = openUeventSocket();
    if (fd < 0) {
        PLOG(ERROR) << "no uevent socket: plugging and unplugging AC goes unnoticed";
        return;
    }
    Pending pending;
    while (true) {
        pollfd poll_fd = {fd.get(), POLLIN, 0};
        if (TEMP_FAILURE_RETRY(poll(&poll_fd, 1, pending.pollTimeoutMs())) > 0) {
            receiveAll(fd.get(), pending);
        }
        if (!pending.any || pending.pollTimeoutMs() > 0) continue;
        const bool devices = pending.devices;
        pending = Pending();
        controller.update(devices);
    }
}

}  // namespace pc::power
