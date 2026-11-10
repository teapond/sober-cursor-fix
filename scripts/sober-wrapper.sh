#!/bin/sh
# sober-wrapper.sh - launch Sober through wlproxy.
#
# Installed into ~/.var/app/org.vinegarhq.Sober/data/ by install.sh and used
# by the desktop entry:
#
#   flatpak run --command=<this script> org.vinegarhq.Sober
#
# It starts the proxy, points WAYLAND_DISPLAY at it, and then execs Sober.

set -u

SELF=$0
case "$SELF" in
    */*) ;;
    *) SELF=$(command -v -- "$SELF" 2>/dev/null || printf '%s' "$SELF") ;;
esac
DATA=$(CDPATH= cd -- "$(dirname -- "$SELF")" && pwd)

if [ -z "${XDG_RUNTIME_DIR:-}" ] || [ ! -x "$DATA/wlproxy" ]; then
    exec /app/bin/sober "$@"
fi

# Remember the real compositor socket before redirecting the app.
REAL="${SOBER_WL_REAL:-${WAYLAND_DISPLAY:-}}"
if [ -z "$REAL" ]; then
    # Pure X11 session: there is nothing to proxy.
    exec /app/bin/sober "$@"
fi

export SOBER_WL_REAL="$REAL"
export WAYLAND_DISPLAY=sober-wl-proxy
export WLPROXY_LOG="$DATA/wlproxy.log"

"$DATA/wlproxy" &
PROXY_PID=$!

# Wait up to ~10 s for the proxy socket.
i=0
while [ ! -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]; do
    sleep 0.05
    i=$((i + 1))
    if [ "$i" -gt 200 ]; then
        echo "sober-cursor-fix: proxy socket did not appear; launching without it" >&2
        kill "$PROXY_PID" 2>/dev/null || true
        export WAYLAND_DISPLAY="$REAL"
        exec /app/bin/sober "$@"
    fi
done

exec /app/bin/sober "$@"
