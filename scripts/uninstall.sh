#!/bin/sh
# uninstall.sh - remove the Sober native-cursor fix for the current user.

set -eu

APP_ID="org.vinegarhq.Sober"
DATA="${HOME}/.var/app/${APP_ID}/data"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
OVERLAY="$DATA/sober/asset_overlay"
MANIFEST="$OVERLAY/.sober-cursor-fix.manifest"

say() { printf '%s\n' "$*"; }

# 1. Restore the stock desktop entry (ours only shadows it).
rm -f "$APPS/$APP_ID.desktop"
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APPS" >/dev/null 2>&1 || true
fi

# 2. Remove only the cursor textures this tool installed, then prune empty
#    directories. Other user overlays are left untouched.
if [ -f "$MANIFEST" ]; then
    while IFS= read -r rel; do
        [ -n "$rel" ] && rm -f "$OVERLAY/$rel"
    done < "$MANIFEST"
    rm -f "$MANIFEST"
    find "$OVERLAY" -mindepth 1 -type d -empty -delete 2>/dev/null || true
fi

# 3. Remove the launcher and proxy.
rm -f "$DATA/sober-wrapper.sh" "$DATA/wlproxy" "$DATA/wlproxy.c" "$DATA/wlproxy.log"

say "Uninstalled. Sober now uses its stock launcher and Roblox's own cursor."
