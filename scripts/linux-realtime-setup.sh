#!/usr/bin/env bash
#
# Lets your user give MAGDA's audio threads real-time priority on Linux.
#
# Usage: linux-realtime-setup.sh [--check] [--yes]
#   --check  report only, change nothing
#   --yes    don't ask before changing anything
#
# Prefers a group the system already grants real-time to (PipeWire's @pipewire, @audio, Arch's
# @realtime), and writes /etc/security/limits.d/95-magda-audio.conf for @audio only when none does.
# Safe to run again. Without it, MAGDA falls back to rtkit, which allows a lower priority.

set -euo pipefail

check_only=false
assume_yes=false
for arg in "$@"; do
    case "$arg" in
        --check) check_only=true ;;
        --yes) assume_yes=true ;;
        -h | --help) sed -n '3,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $arg" >&2; exit 2 ;;
    esac
done

limits_file=/etc/security/limits.d/95-magda-audio.conf
user=${SUDO_USER:-$(id -un)}

# Groups that a limits file grants an rtprio above 0, one per line.
granting_groups() {
    cat /etc/security/limits.conf /etc/security/limits.d/*.conf 2>/dev/null |
        awk '$1 ~ /^@/ && $3 == "rtprio" && $4 + 0 > 0 { sub(/^@/, "", $1); print $1 }' | sort -u
}

in_group() {
    id -nG "$user" | tr ' ' '\n' | grep -qx "$1"
}

rtkit_running() {
    command -v busctl >/dev/null &&
        busctl --system status org.freedesktop.RealtimeKit1 >/dev/null 2>&1
}

report() {
    echo "User:                 $user"
    echo "Real-time limit now:  $(ulimit -r) (MAGDA needs above 0 for priority 80)"
    local groups
    groups=$(granting_groups | tr '\n' ' ')
    echo "Groups granting it:   ${groups:-none}"
    local member=none
    for group in $(granting_groups); do
        in_group "$group" && member=$group && break
    done
    echo "You are in:           $member"
    if rtkit_running; then
        echo "rtkit:                running (MAGDA's fallback, priority up to 20)"
    else
        echo "rtkit:                not running"
    fi
}

report
echo

if [ "$(ulimit -r)" != 0 ]; then
    echo "Real-time priority is already available. Nothing to do."
    exit 0
fi

for group in $(granting_groups); do
    if in_group "$group"; then
        echo "You are already in @$group, which grants real-time priority."
        echo "Log out and back in (or reboot) for it to take effect, then run this again."
        exit 0
    fi
done

$check_only && exit 0

# Prefer a group the system already grants, in this order; otherwise set up @audio.
target=""
for preferred in pipewire audio realtime jackuser; do
    if granting_groups | grep -qx "$preferred" && getent group "$preferred" >/dev/null; then
        target=$preferred
        break
    fi
done

steps=()
if [ -z "$target" ]; then
    target=audio
    getent group audio >/dev/null || steps+=("create the group 'audio'")
    steps+=("write $limits_file granting @audio rtprio 95 and memlock unlimited")
fi
steps+=("add $user to '$target'")

echo "This will, using sudo:"
printf '  - %s\n' "${steps[@]}"
if ! $assume_yes; then
    read -r -p "Continue? [y/N] " answer
    [[ "$answer" =~ ^[Yy]$ ]] || { echo "Nothing changed."; exit 1; }
fi

if [ "$target" = audio ] && ! granting_groups | grep -qx audio; then
    getent group audio >/dev/null || sudo groupadd --system audio
    printf '%s\n' \
        "# Written by MAGDA's linux-realtime-setup.sh: real-time audio for the audio group." \
        "@audio   -  rtprio     95" \
        "@audio   -  memlock    unlimited" | sudo tee "$limits_file" >/dev/null
fi
sudo usermod -aG "$target" "$user"

echo
echo "Done. Log out and back in (or reboot), then check with: ulimit -r"
echo "It should print 95. MAGDA's log then says: render threads run SCHED_RR at priority 80"
