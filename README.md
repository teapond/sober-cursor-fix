# sober-cursor-fix

**Makes the normal mouse cursor visible in Sober on Wayland and removes
Roblox's own cursor.**

[Русская версия →](README.ru.md)

Sober hides your desktop cursor and draws its own. This tool fixes both: your
system cursor stays visible, and Roblox's duplicate cursor disappears.

## Quick start

```sh
git clone https://github.com/teapond/my-project.git
cd my-project
./install.sh
```

No `sudo`, no root. Everything is installed in your home folder.

Then open **Sober** from the application menu.

> Don't want to use git? On GitHub click **Code → Download ZIP**, unpack it,
> open a terminal in that folder and run `./install.sh`.

## What the installer does

1. Builds a tiny Wayland proxy (`wlproxy`).
2. Puts it and a launcher wrapper into Sober's data folder.
3. Creates transparent Roblox cursor textures from your installed `base.apk`.
4. Adds a menu entry that starts Sober through the wrapper.

If something is missing, the installer tells you exactly what to install and
which command to use for your distribution.

## Requirements

- Linux with a **Wayland** session (tested on KDE Plasma 6)
- **Sober** installed via Flatpak
- `cc`/`gcc` + `make` and `python3`
  (the installer prints the install command for your distro if they are
  missing)

## After installing

- Launch Sober from the **application menu**.
- If Sober was already running, close it and open it again.
- Launching with `flatpak run org.vinegarhq.Sober` from a terminal bypasses the
  fix. Use the menu entry or:

```sh
flatpak run --command="$HOME/.var/app/org.vinegarhq.Sober/data/sober-wrapper.sh" org.vinegarhq.Sober
```

## Uninstall

```sh
./uninstall.sh
```

or:

```sh
./install.sh --uninstall
```

This restores the stock menu entry and removes only the files the installer
created. Other files in `asset_overlay` are left untouched.

## If something goes wrong

<details>
<summary><b>Cursor is still hidden</b></summary>

Check the log:

```sh
tail -f ~/.var/app/org.vinegarhq.Sober/data/wlproxy.log
```

When the mouse enters the Sober window you should see lines like
`drop wl_pointer.set_cursor(NULL)`. If the log stays empty, make sure you
started Sober from the menu entry and not with a plain `flatpak run`.
</details>

<details>
<summary><b>I see two cursors</b></summary>

The cursor textures were not applied. Make sure Roblox is downloaded, run
`./install.sh` again and restart Sober.
</details>

<details>
<summary><b>Roblox updated</b></summary>

Run `./install.sh` again. It regenerates the cursor textures for the new
version.
</details>

<details>
<summary><b>I want the technical details</b></summary>

See [How it works](#how-it-works) below.
</details>

## How it works

### Cursor proxy

`wlproxy` listens on a private socket name (`sober-wl-proxy`) and connects to
the real compositor socket. The wrapper sets `WAYLAND_DISPLAY=sober-wl-proxy`
for Sober and `SOBER_WL_REAL=wayland-N` for the proxy.

The proxy tracks the object graph just enough to identify `wl_pointer` objects
(`wl_display.get_registry` → `wl_registry.bind` → `wl_seat.get_pointer`) and
removes `set_cursor` calls whose surface argument is `NULL`. It exits
automatically when its parent (Sober) dies.

Build it yourself with:

```sh
make static      # fully static binary
# or
make             # dynamic binary
```

### Cursor textures

`scripts/gen_overlay.py` opens `base.apk`, finds every
`assets/content/textures/Cursors/**.png` (plus
`assets/content/textures/ArrowCursor.png`), reads each PNG's dimensions, and
writes a fully transparent RGBA PNG of the same size into `asset_overlay`.
Sober prefers files from `asset_overlay` over the APK.

## Project layout

```
install.sh              installer
uninstall.sh            uninstaller
src/wlproxy.c           Wayland cursor proxy
scripts/sober-wrapper.sh  launcher used by the menu entry
scripts/gen_overlay.py  transparent cursor texture generator
```

## Files installed

```
~/.var/app/org.vinegarhq.Sober/data/
├── wlproxy                # proxy binary
├── sober-wrapper.sh       # launcher
├── wlproxy.log            # runtime log
└── sober/asset_overlay/   # transparent cursor textures
~/.local/share/applications/org.vinegarhq.Sober.desktop
```

## License

MIT — see [LICENSE](LICENSE).

---

MADE WITH AI
