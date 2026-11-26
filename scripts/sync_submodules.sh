#!/usr/bin/env bash
# SPDX-FileCopyrightText: Conceptual Machines
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Moves each submodule to the commit the superproject records, before a build. A checkout with
# commits of its own is left alone and reported. Our patches/ are lifted off a submodule for the
# checkout and put back after, since CMake applies them in-tree only at configure time.
set -u
cd "$(git rev-parse --show-toplevel)" || exit 1

status=0
patches=(patches/*.patch)

while read -r line; do
    flag=${line:0:1}
    path=$(echo "${line:1}" | awk '{print $2}')
    if [ "$flag" = "-" ]; then
        echo "📦 $path: initialising"
        git submodule update --init -- "$path" || status=1
        continue
    fi
    [ "$flag" = "+" ] || continue

    recorded=$(git ls-files -s -- "$path" | awk '{print $2}')
    if git -C "$path" cat-file -e "$recorded^{commit}" 2>/dev/null &&
        ! git -C "$path" merge-base --is-ancestor HEAD "$recorded"; then
        echo "⚠️  $path: has commits $recorded does not, left as it is"
        continue
    fi

    echo "📦 $path: moving to $recorded"
    lifted=()
    for patch in "${patches[@]}"; do
        if [ -f "$patch" ] &&
            git -C "$path" apply --reverse --check --ignore-whitespace "$PWD/$patch" 2>/dev/null; then
            git -C "$path" apply --reverse --ignore-whitespace "$PWD/$patch" && lifted+=("$patch")
        fi
    done
    git submodule update -- "$path" || status=1
    for patch in ${lifted[@]+"${lifted[@]}"}; do
        git -C "$path" apply --ignore-whitespace "$PWD/$patch" ||
            echo "⚠️  $path: $patch no longer applies"
    done
done < <(git submodule status)

exit $status
