#!/bin/sh
# Builds the wake relay for this machine into dist/native/<platform>-<arch>/,
# named the way Node and Bun name the platform, so the plugin can find it.
# Usage: build-native.sh [output directory]   (default dist/native/<platform>-<arch>)
set -eu
cd "$(dirname "$0")/.."
out=${1:-dist/native/$(node -p 'process.platform + "-" + process.arch')}
mkdir -p "$out"
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -o "$out/nap-relay" native/nap-relay.c native/wake-screen.c -lutil
