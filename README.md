# sober-cursor-fix

Keep the **native desktop cursor visible** in [Sober](https://github.com/VinegarHQ/Sober)
and hide Roblox's own in-game cursor.

> Unofficial community workaround. Not affiliated with Roblox or VinegarHQ.

## The problem

Sober renders Roblox through SDL. On Wayland, SDL hides the compositor cursor
with

```c
wl_pointer.set_cursor(serial, NULL, 0, 0)
```

and never asks the compositor for a cursor shape. The result is an invisible
pointer over the Sober window. On top of that, Roblox draws its own cursor from
PNG textures, so even if the host cursor were visible you would see two
cursors.

## The fix

This project does two independent things:

1. **`wlproxy`** — a tiny Wayland socket proxy that sits between Sober and the
   compositor. It parses only the client → server direction and drops
   `wl_pointer.set_cursor(..., NULL, ...)` requests. Everything else, including
   `SCM_RIGHTS` file descriptors, is forwarded verbatim. The compositor keeps
   drawing its normal cursor.
2. **Transparent cursor textures** — Roblox's cursor PNGs are replaced with
   fully transparent same-size PNGs through Sober's
   [`asset_overlay`](https://vinegarhq.org/Sober/Configuration/TipsAndTricks.html)
   mechanism, so only the compositor cursor remains.

## Requirements

- Linux with a Wayland session (tested on KDE Plasma 6; should work on any
  compositor)
- Sober installed via Flatpak (`org.vinegarhq.Sober`)
- A C compiler and `make` to build the proxy
- `python3` to generate the cursor overlay

## Install

```sh
git clone https://github.com/<your-username>/sober-cursor-fix.git
cd sober-cursor-fix
./scripts/install.sh
```

The installer is per-user and needs no root. It:

- builds `wlproxy` (static binary),
- installs it and a launcher wrapper into
  `~/.var/app/org.vinegarhq.Sober/data/`,
- reads the installed Roblox `base.apk` and writes transparent cursor textures
  into `~/.var/app/org.vinegarhq.Sober/data/sober/asset_overlay/`,
- installs a desktop entry in `~/.local/share/applications/` that launches Sober
  through the wrapper.

Then launch **Sober from the application menu** (close any running instance
first).

If Roblox has not been downloaded yet, launch Sober once and run the installer
again so the cursor overlay can be generated.

## Manual launch

Launching with plain `flatpak run org.vinegarhq.Sober` bypasses the wrapper. Use
either the menu entry or:

```sh
flatpak run --command="$HOME/.var/app/org.vinegarhq.Sober/data/sober-wrapper.sh" \
    org.vinegarhq.Sober
```

## Uninstall

```sh
./scripts/uninstall.sh
```

This restores the stock desktop entry and removes only the cursor textures that
were installed (other files in `asset_overlay` are left alone).

## How it works

### Cursor proxy

`wlproxy` listens on a private socket name (`sober-wl-proxy`) and connects to
the real compositor socket. The wrapper sets `WAYLAND_DISPLAY=sober-wl-proxy`
for Sober and `SOBER_WL_REAL=wayland-N` for the proxy.

The proxy tracks the object graph just enough to identify `wl_pointer` objects
(`wl_display.get_registry` → `wl_registry.bind` → `wl_seat.get_pointer`) and
removes `set_cursor` calls whose surface argument is `NULL`. It also exits
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

## Troubleshooting

- **Cursor is still hidden.** Check
  `~/.var/app/org.vinegarhq.Sober/data/wlproxy.log`; you should see
  `drop wl_pointer.set_cursor(NULL)` lines when the pointer enters the window.
  Set `WLPROXY_DEBUG=1` for verbose output.
- **Two cursors.** The overlay was not applied. Make sure the APK exists and
  re-run `./scripts/install.sh`, then restart Sober.
- **Roblox updated.** Cursor file names or sizes may change. Re-run
  `./scripts/install.sh` to regenerate the overlay.
- **Launched from a terminal.** `flatpak run org.vinegarhq.Sober` does not use
  the wrapper; see [Manual launch](#manual-launch).
- **X11 session.** Sober uses X11 there and this fix is a no-op; the wrapper
  simply starts Sober normally.
- **Multiple Sober instances.** The proxy uses a fixed socket name, so only one
  instance is supported.

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
