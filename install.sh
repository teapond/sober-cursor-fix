#!/bin/sh
# install.sh - install the Sober native-cursor fix for the current user.
#
# Everything is installed under your home directory. No root, no sudo.
#
# Usage:
#   ./install.sh              install
#   ./install.sh --uninstall  remove
#   ./install.sh --help       show help

set -eu

APP_ID="org.vinegarhq.Sober"
APP_NAME="Sober"
DATA="${HOME}/.var/app/${APP_ID}/data"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
DESKTOP="$APPS/$APP_ID.desktop"
APK="$DATA/sober/packages/x86_64/com.roblox.client/base.apk"
OVERLAY="$DATA/sober/asset_overlay"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    BOLD=$(printf '\033[1m')
    GREEN=$(printf '\033[32m')
    YELLOW=$(printf '\033[33m')
    RED=$(printf '\033[31m')
    RESET=$(printf '\033[0m')
else
    BOLD='' GREEN='' YELLOW='' RED='' RESET=''
fi

step() { printf '\n%s==> %s%s\n' "$BOLD" "$*" "$RESET"; }
info() { printf '    %s\n' "$*"; }
ok()   { printf '    %s[ok]%s %s\n' "$GREEN" "$RESET" "$*"; }
warn() { printf '    %s[!]%s  %s\n' "$YELLOW" "$RESET" "$*" >&2; }
die()  { printf '\n%s[error]%s %s\n' "$RED" "$RESET" "$*" >&2; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

install_hint() {
    if have apt-get; then
        echo "sudo apt install build-essential python3"
    elif have dnf; then
        echo "sudo dnf install gcc make python3"
    elif have pacman; then
        echo "sudo pacman -S base-devel python"
    elif have zypper; then
        echo "sudo zypper install gcc make python3"
    elif have apk; then
        echo "sudo apk add build-base python3"
    else
        echo "install a C compiler, make and python3 with your package manager"
    fi
}

usage() {
    cat <<EOF
Sober native-cursor fix

Usage:
  ./install.sh              install for the current user
  ./install.sh --uninstall  remove it again
  ./install.sh --help       show this help

The installer only writes inside your home directory and needs no root.
EOF
}

case "${1:-}" in
    -h|--help)
        usage
        exit 0
        ;;
    --uninstall)
        exec "$ROOT/uninstall.sh"
        ;;
    "")
        ;;
    *)
        die "Unknown option: $1. Try --help."
        ;;
esac

printf '%sSober native-cursor fix%s\n' "$BOLD" "$RESET"
printf 'Makes the normal cursor visible and hides Roblox'\''s own cursor.\n'

# ---------------------------------------------------------------------------
step "Checking your system"

have flatpak || die "Flatpak is not installed.
Install Sober first: https://sober.vinegarhq.org/"

flatpak info "$APP_ID" >/dev/null 2>&1 || die "Sober is not installed.
Install it first: https://sober.vinegarhq.org/"
ok "Sober is installed"

[ -d "$DATA" ] || die "Sober has not been started yet.
Open Sober once, close it, then run this installer again."
ok "Sober data folder found"

HAVE_APK=0
if [ -f "$APK" ]; then
    HAVE_APK=1
    ok "Roblox files found"
else
    warn "Roblox is not downloaded yet."
    warn "The cursor overlay will be skipped; run this installer again after"
    warn "opening Sober once."
fi

# ---------------------------------------------------------------------------
step "Building the cursor proxy"

if [ -x "$ROOT/wlproxy" ]; then
    ok "Using the prebuilt wlproxy"
else
    have make || die "make is not installed.
Install it with:
    $(install_hint)"

    if have cc; then
        CC_BIN=cc
    elif have gcc; then
        CC_BIN=gcc
    elif have clang; then
        CC_BIN=clang
    else
        die "No C compiler found.
Install one with:
    $(install_hint)"
    fi

    if ( cd "$ROOT" && make CC="$CC_BIN" static >/dev/null 2>&1 ); then
        ok "Built wlproxy (static)"
    elif ( cd "$ROOT" && make CC="$CC_BIN" >/dev/null 2>&1 ); then
        ok "Built wlproxy"
    else
        die "Build failed. Make sure a C compiler and make are installed."
    fi
fi

# ---------------------------------------------------------------------------
step "Installing the launcher"

install -m 0755 "$ROOT/wlproxy" "$DATA/wlproxy"
install -m 0755 "$ROOT/scripts/sober-wrapper.sh" "$DATA/sober-wrapper.sh"
ok "Installed into $DATA"

# ---------------------------------------------------------------------------
step "Preparing transparent Roblox cursor textures"

if [ "$HAVE_APK" = 1 ]; then
    have python3 || die "python3 is required to generate the cursor overlay.
Install it with:
    $(install_hint)"

    if python3 "$ROOT/scripts/gen_overlay.py" "$APK" "$OVERLAY" >/dev/null; then
        ok "Cursor textures installed"
    else
        die "Could not generate the cursor overlay."
    fi
else
    warn "Skipped (Roblox is not downloaded yet)."
fi

# ---------------------------------------------------------------------------
step "Adding the application menu entry"

mkdir -p "$APPS"
cat > "$DESKTOP" <<EOF
[Desktop Entry]
Type=Application
Name=$APP_NAME
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
if have update-desktop-database; then
    update-desktop-database "$APPS" >/dev/null 2>&1 || true
fi
ok "Menu entry installed"

# ---------------------------------------------------------------------------
printf '\n%sAll done!%s\n' "$GREEN" "$RESET"
info "Launch Sober from your application menu."

if flatpak ps 2>/dev/null | grep -q "$APP_ID"; then
    warn "Sober is running right now - close it and open it again."
fi

info "To remove everything later, run: ./uninstall.sh"
