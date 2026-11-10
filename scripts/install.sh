#!/bin/sh
# install.sh - install the Sober native-cursor fix for the current user.
#
# Everything is installed under the user's home directory; no root is needed.

set -eu

APP_ID="org.vinegarhq.Sober"
DATA="${HOME}/.var/app/${APP_ID}/data"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

say()  { printf '%s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }

case "${1:-}" in
    -h|--help)
        cat <<EOF
Usage: $0

Installs the Sober native-cursor fix for the current user:
  * builds/copies the Wayland cursor proxy
  * installs the launcher wrapper
  * generates transparent Roblox cursor textures
  * installs a desktop entry that launches Sober through the wrapper

Uninstall with: scripts/uninstall.sh
EOF
        exit 0
        ;;
esac

command -v flatpak >/dev/null 2>&1 || die "flatpak not found"
flatpak info "$APP_ID" >/dev/null 2>&1 \
    || die "Sober ($APP_ID) is not installed via Flatpak"
[ -d "$DATA" ] \
    || die "Sober data directory not found: $DATA (launch Sober once first)"

# 1. Build the proxy if there is no prebuilt binary in the repo.
if [ ! -x "$ROOT/wlproxy" ]; then
    command -v make >/dev/null 2>&1 \
        || die "make and a C compiler are required to build wlproxy"
    say "Building wlproxy..."
    ( cd "$ROOT" && { make static 2>/dev/null || make; } )
fi

# 2. Install the proxy and wrapper into the app's data directory. Flatpak
#    mounts this directory inside the sandbox at the same path, so the app can
#    execute both files.
install -m 0755 "$ROOT/wlproxy" "$DATA/wlproxy"
install -m 0755 "$ROOT/scripts/sober-wrapper.sh" "$DATA/sober-wrapper.sh"
say "Installed proxy and launcher wrapper into $DATA"

# 3. Generate transparent cursor textures from the installed Roblox APK.
APK="$DATA/sober/packages/x86_64/com.roblox.client/base.apk"
if [ -f "$APK" ]; then
    command -v python3 >/dev/null 2>&1 \
        || die "python3 is required to generate the cursor overlay"
    python3 "$ROOT/scripts/gen_overlay.py" "$APK" "$DATA/sober/asset_overlay"
else
    warn "Roblox APK not found at:"
    warn "  $APK"
    warn "Launch Sober once so it downloads Roblox, then run this installer again."
fi

# 4. Install a desktop entry that launches Sober through the wrapper. It
#    shadows the Flatpak-exported entry and is removed by uninstall.sh.
mkdir -p "$APPS"
DESKTOP="$APPS/$APP_ID.desktop"
cat > "$DESKTOP" <<EOF
[Desktop Entry]
Type=Application
Name=Sober
GenericName=Roblox Player
Comment=Play, chat & explore on Roblox
Icon=$APP_ID
Keywords=roblox;vinegar;game;gaming;social;experience;launcher;
MimeType=x-scheme-handler/roblox;x-scheme-handler/roblox-player;
Categories=GNOME;GTK;Game;
Terminal=false
PrefersNonDefaultGPU=true
SingleMainWindow=true
Exec=/usr/bin/flatpak run --branch=stable --arch=x86_64 --command="$DATA/sober-wrapper.sh" --file-forwarding $APP_ID -- @@u %u @@
Actions=open-settings;
X-Flatpak-Tags=proprietary;
X-Flatpak=$APP_ID

[Desktop Action open-settings]
Name=Settings
Exec=/usr/bin/flatpak run --branch=stable --arch=x86_64 --command=sober $APP_ID config
EOF
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APPS" >/dev/null 2>&1 || true
fi
say "Installed desktop entry: $DESKTOP"

say ""
say "Done. Launch Sober from the application menu."
say "Close any running Sober instance first."
