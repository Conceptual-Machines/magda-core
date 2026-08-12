#!/usr/bin/env bash
#
# Run a command, retrying it if it fails.
#
# Every third-party dependency this build fetches is a pinned archive on
# somebody else's server, and CMake downloads each one exactly once: if the
# request fails, FetchContent gives up and configure fails with it. There is no
# retry option to turn on, which makes a red build the default response to a bad
# few seconds on a CDN.
#
# It is not hypothetical. One afternoon took wasmtime on Linux, the ONNX Runtime
# on macOS and vcpkg's own bootstrap on Windows inside two minutes of each other,
# three jobs failing for the same reason and none of them for anything anybody
# had changed.
#
# So the download-bearing steps run through here. A second attempt fifteen
# seconds later costs nothing when the first one worked, and is almost always
# enough when it did not. A failure that is really about this repository fails
# the same way three times and still goes red, only later.
#
# Usage: scripts/ci-retry.sh <command> [args...]
#   CI_RETRY_ATTEMPTS  how many times to try   (default 3)
#   CI_RETRY_DELAY     seconds between tries   (default 15)

set -uo pipefail

attempts="${CI_RETRY_ATTEMPTS:-3}"
delay="${CI_RETRY_DELAY:-15}"

if [ "$#" -eq 0 ]; then
    echo "ci-retry: nothing to run" >&2
    exit 2
fi

attempt=1
while [ "$attempt" -le "$attempts" ]; do
    # Run it, then read the status, rather than testing it in an `if`. After
    # `if cmd; then ... fi` the status belongs to the `if` and not to cmd, so
    # reading $? there reports success for a command that failed, and this
    # script would swallow the very failures it exists to retry.
    "$@"
    status=$?

    if [ "$status" -eq 0 ]; then
        exit 0
    fi

    if [ "$attempt" -eq "$attempts" ]; then
        echo "ci-retry: all $attempts attempts failed (exit $status): $*" >&2
        exit "$status"
    fi

    echo "ci-retry: attempt $attempt of $attempts failed (exit $status), retrying in ${delay}s"
    sleep "$delay"
    attempt=$((attempt + 1))
done
