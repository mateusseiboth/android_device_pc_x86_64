#!/system/bin/sh
# Sound at boot (init.pc.rc, post-fs, in the background). Android's audio HAL plays and sets levels on
# card 0 only, and a PC's sound card is often not card 0: a GPU's HDMI controller probes first (AMD
# laptops), or the PC speaker took it. Once the cards have settled, the first card with an analog codec
# becomes card 0 (a card keeps its number until its device is bound again), then its playback levels go up
# (pc-mixer: the kernel leaves them at silence).

log() { echo "pc-audio: $*" > /dev/kmsg; }

# A card that plays through speakers or a headphone jack: an HD Audio codec that is not HDMI or
# DisplayPort, or Intel's SOF.
is_analog() {
    for codec in "$1"/codec#*; do
        [ -f "$codec" ] && grep -m1 '^Codec:' "$codec" | grep -qviE 'hdmi|displayport' && return 0
    done
    case "$(cat "$1/id")" in sof*) return 0 ;; esac
    return 1
}

find_analog() {
    for card in /proc/asound/card[0-9]*; do
        is_analog "$card" && { echo "${card#/proc/asound/card}"; return; }
    done
}

# Codecs answer a few seconds after their controller: wait until nothing changed for 2 s (20 s at most).
wait_for_cards() {
    previous=- quiet=0
    for _ in $(seq 40); do
        now=$(ls /proc/asound/card*/codec#* /proc/asound/card*/id 2>/dev/null)
        [ "$now" = "$previous" ] && quiet=$((quiet + 1)) || quiet=0
        [ $quiet -ge 4 ] && return
        previous=$now
        sleep 0.5
    done
}

wait_for_card0() {
    for _ in $(seq 40); do
        [ -f /proc/asound/card0/id ] && ls /proc/asound/card0/codec#* >/dev/null 2>&1 && return
        sleep 0.25
    done
}

# Unbinds the devices of cards 0..N, binds card N's first (it gets 0), then the others.
move_to_card0() {
    analog_device=$(readlink -f "/sys/class/sound/card$1/device")
    analog_driver=$(readlink -f "/sys/class/sound/card$1/device/driver")
    others=
    for n in $(seq 0 "$1"); do
        device=$(readlink -f "/sys/class/sound/card$n/device")
        driver=$(readlink -f "/sys/class/sound/card$n/device/driver")
        [ -n "$driver" ] || continue
        echo "${device##*/}" > "$driver/unbind"
        [ "$n" = "$1" ] || others="$others $driver/bind:${device##*/}"
    done
    echo "${analog_device##*/}" > "$analog_driver/bind"
    wait_for_card0
    for other in $others; do
        echo "${other#*:}" > "${other%%:*}"
    done
}

# An HD Audio controller asks for its codec's driver by name while it probes. During coldboot nothing
# dynamic runs yet (the runtime APEX mounts meanwhile), so the request fails and the controller gives up
# without a card: probe those again, now that the module loader works.
probe_again() {
    driver=/sys/bus/pci/drivers/snd_hda_intel
    again=
    for device in "$driver"/0000:*; do
        [ -e "$device" ] || continue
        ls -d "$device"/sound/card* >/dev/null 2>&1 && continue
        echo "${device##*/}" > "$driver/unbind"
        echo "${device##*/}" > "$driver/bind"
        again="$again ${device##*/}"
    done
    [ -n "$again" ] && log "probed again:$again" && wait_for_cards
}

wait_for_cards
probe_again
card=$(find_analog)
[ -n "$card" ] || { log "no analog sound card"; exit 0; }
if [ "$card" != 0 ]; then
    log "card $card ($(cat /proc/asound/card$card/id)) becomes card 0"
    move_to_card0 "$card"
    wait_for_cards
    # The HAL keeps a card it failed to open: start it again (audioserver follows).
    [ "$(getprop init.svc.vendor.audio-hal-aidl)" = running ] && setprop ctl.restart vendor.audio-hal-aidl
fi
log "card 0 is $(cat /proc/asound/card0/id): $(/vendor/bin/pc-mixer 0 | tr '\n' ' ')"
exit 0
