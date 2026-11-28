#!/vendor/bin/sh
# Turns Wi-Fi back on when a laptop's Wi-Fi key or switch turns its radio on (init.pc.rc, from boot).
# That key blocks the radio in hardware (rfkill "hard", which software cannot undo), and Android does
# not follow rfkill: Wi-Fi turned on while the radio was off fails, stays off, and nothing tries again
# once the key turns the radio back on. A Wi-Fi radio's hard block that lifts while Wi-Fi is meant to
# be on (wifi_on 1, or 2 for on in airplane mode) turns Wi-Fi on again. Checked every 2 seconds.
#   pc-radios.sh [SYSFS]   (default /sys)
rfkill=${1:-/sys}/class/rfkill

# 1 while any Wi-Fi radio is hard blocked, else 0.
read_wlan_block() {
    for radio in "$rfkill"/rfkill*; do
        read -r type < "$radio/type" || continue
        read -r hard < "$radio/hard" || continue
        [ "$type" = wlan ] && [ "$hard" = 1 ] && { echo 1; return; }
    done
    echo 0
}

resume_wifi() {
    case "$(/system/bin/settings get global wifi_on)" in
        1|2) /system/bin/cmd wifi set-wifi-enabled enabled ;;
    esac
}

previous=$(read_wlan_block 2>/dev/null)
while sleep 2; do
    current=$(read_wlan_block 2>/dev/null)
    [ "$previous$current" = 10 ] && resume_wifi
    previous=$current
done
