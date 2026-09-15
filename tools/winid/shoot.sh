#!/bin/sh
# Screenshot the desktop simulator by window id. See winid.c for why this
# does not use `screencapture -R`.
#
#   tools/winid/shoot.sh [output.png] [env]
#
# `env` is the PlatformIO environment to build and run -- `simulator` (the
# MQB product, the default) or `simulator_gauge` (the analog gauge). They
# are separate binaries and can be running at the same time, which is the
# point: one command each proves both products still render.
#
# Builds winid on first use, restores the interactive simulator binary if
# `pio test` has overwritten it, launches it if it is not already running,
# and captures the window.
set -e

out=${1:-sim.png}
env=${2:-simulator}
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
bin="$repo/.pio/build/$env/program"

[ -x "$here/winid" ] || clang -o "$here/winid" "$here/winid.c" \
    -framework CoreGraphics -framework CoreFoundation

pid=$(pgrep -f "$bin" || true)
if [ -z "$pid" ]; then
    # `pio test` overwrites this same path with the Unity runner, which
    # exits immediately -- so always rebuild before launching.
    (cd "$repo" && pio run -e "$env" >/dev/null)
    nohup "$bin" >"/tmp/mqb_$env.log" 2>&1 &
    sleep 3
    pid=$(pgrep -f "$bin")
fi

wid=$("$here/winid" "$pid" | head -1 | cut -d' ' -f1)
screencapture -x -o -l"$wid" "$out"
echo "$out"
