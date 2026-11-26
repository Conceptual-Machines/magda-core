#!/usr/bin/env bash
# SPDX-FileCopyrightText: Conceptual Machines
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Refuses a commit that moves a submodule back to an older commit, the mark of a stale checkout
# staged by accident. MAGDA_ALLOW_SUBMODULE_REWIND=1 lets a deliberate downgrade through.
set -u
[ "${MAGDA_ALLOW_SUBMODULE_REWIND:-0}" = "1" ] && exit 0

status=0
while read -r _ newmode old new _ path; do
    [ "$newmode" = "160000" ] || continue
    [ "$old" != "$new" ] || continue
    git -C "$path" cat-file -e "$old^{commit}" 2>/dev/null || continue
    if git -C "$path" merge-base --is-ancestor "$new" "$old"; then
        echo "❌ $path goes back from ${old:0:10} to ${new:0:10}."
        echo "   Run scripts/sync_submodules.sh and unstage it, or set MAGDA_ALLOW_SUBMODULE_REWIND=1."
        status=1
    fi
done < <(git diff --cached --raw --no-abbrev | sed 's/^://')

exit $status
