#!/bin/sh
# uninstall.sh - remove the Sober native-cursor fix for the current user.

set -eu

APP_ID="org.vinegarhq.Sober"
DATA="${HOME}/.var/app/${APP_ID}/data"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
DESKTOP="$APPS/$APP_ID.desktop"
OVERLAY="$DATA/sober/asset_overlay"
MANIFEST="$OVERLAY/.sober-cursor-fix.manifest"

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    BOLD=$(printf '\033[1m')
    GREEN=$(printf '\033[32m')
    RESET=$(printf '\033[0m')
else
    BOLD='' GREEN='' RESET=''
fi

step() { printf '\n%s==> %s%s\n' "$BOLD" "$*" "$RESET"; }
info() { printf '    %s\n' "$*"; }
ok()   { printf '    %s[ok]%s %s\n' "$GREEN" "$RESET" "$*"; }

have() { command -v "$1" >/dev/null 2>&1; }

printf '%sSober native-cursor fix - uninstall%s\n' "$BOLD" "$RESET"

step "Removing the application menu entry"
if [ -f "$DESKTOP" ]; then
    rm -f "$DESKTOP"
    if have update-desktop-database; then
        update-desktop-database "$APPS" >/dev/null 2>&1 || true
    fi
    ok "Menu entry removed"
else
    info "Nothing to remove"
fi

step "Removing the cursor textures"
if [ -f "$MANIFEST" ]; then
    while IFS= read -r rel; do
        [ -n "$rel" ] && rm -f "$OVERLAY/$rel"
    done < "$MANIFEST"
    rm -f "$MANIFEST"
    # Remove directories that became empty; leave other user overlays alone.
    find "$OVERLAY" -mindepth 1 -type d -empty -delete 2>/dev/null || true
    ok "Installed cursor textures removed"
else
    info "Nothing to remove"
fi

step "Removing the launcher and proxy"
rm -f "$DATA/sober-wrapper.sh" "$DATA/wlproxy" "$DATA/wlproxy.c" "$DATA/wlproxy.log"
ok "Launcher and proxy removed"

printf '\n%sDone!%s Sober is back to its stock launcher and cursor.\n' "$GREEN" "$RESET"
