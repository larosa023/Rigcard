# Rigcard

Your PC build on one clean card: parts, photo, live stats and benchmarks. Rigcard detects your hardware by itself, so there's nothing to type in, and makes a card you can share as a link or an image (Discord, Reddit, friends).

Part of the larosa023 suite (Hangar, Foyer, Strata, Breaker, Ember…): same look, same theme files, and Hangar can install and live-update it.

## Install

Download from [Releases](https://github.com/larosa023/Rigcard/releases/latest):

| File | What it is |
|---|---|
| `Rigcard-Setup.exe` | Installer. Per-user, no admin. Installs to `%LOCALAPPDATA%\Programs\Rigcard` with a Start menu entry and an uninstaller |
| `Rigcard-portable.zip` | Portable: unzip anywhere. Your data stays in a `data` folder next to the exe |
| `Rigcard.exe` | The app on its own (~1.3 MB). Run it from anywhere |

Or install it from **Hangar**. Windows may show SmartScreen the first time because the exe isn't signed yet: **More info → Run anyway**.

## Using it

- **Card**: a live preview of your card. Pick a style (Midnight, Light, Neon, Aurora, Carbon, Sunset, Showcase, Mono, Paper, Ocean), change colors, font, layout and corners, choose which sections show, drop a photo of your build anywhere in the window, then drag it to position and scroll to zoom. Preview it wide, square, portrait or as a phone wallpaper.
- **Parts**: everything Windows can detect is filled in: CPU, GPU and VRAM, motherboard and BIOS, memory (size, speed, modules), drives, monitors, keyboard and mouse, Windows version. Rename anything; hide what you don't want shown. For the cooler, power supply, case, fans and peripherals, start typing to pick from popular parts, or type anything. **Rescan** shows what changed.
- **Benchmarks**: CPU single and multi-core, GPU compute, memory bandwidth and latency, storage read, write and 4K random. Every run is kept, with a trend line and the change since last time. Import 3DMark `.3dmark-result` files, CrystalDiskMark text, Geekbench links, or paste Cinebench / Time Spy / Port Royal scores, or type a score in.
- **Export**: PNG at Wide 1920×1080 (Discord, Reddit), Square 1080×1080, Portrait 1080×1350, Phone 1170×2532 and 4K, or **Copy image** to paste straight into a chat.

Keyboard: Ctrl+1–4 switch pages, Tab/Shift+Tab move between controls, Enter or Space activates, Ctrl+C copies the share link, Ctrl+S exports, F5 rescans.

## Sharing

**Copy share link** gives you a link to a page on **https://larosa023.github.io/Rigcard/** that shows your card (it adapts to phones).

- **Quick link** (no account): the whole card is packed into the link itself. Instant and private (the part after `#` never reaches any server), but it's a snapshot and has no photo.
- **Hosted link** (free, with your GitHub account): Settings → Sharing → *Create a token on GitHub* (only the `gist` permission), paste it, **Connect**. Rigcard stores the card in a *secret gist* on your account. The link never changes; editing your build in Rigcard updates the page within seconds, it includes your photo, and with **Live stats on the shared page** on, temperatures and power are pushed every minute while Rigcard runs (viewers see LIVE, or OFFLINE with "last seen"). **Stop sharing** deletes the gist.

**Privacy**: serial numbers, your IP address and Windows user name never appear on the card, exports or the share page unless you turn them on in Settings → Privacy, and you can hide your location. Your GitHub token is encrypted with Windows (DPAPI) and only sent to GitHub.

## Live stats and sensors

CPU and GPU temperature, clocks, load, power draw and uptime. Polling only runs while the window is open (or while live sharing is on); when Rigcard is minimized it uses no CPU.

- **GPU**: NVIDIA through NVML (part of the driver); other GPUs through Windows' own GPU telemetry (the numbers Task Manager shows). No admin needed.
- **CPU temperature and package power** need a signed kernel driver. Rigcard uses **PawnIO**, the same driver as Ember and LibreHardwareMonitor. Settings → Sensors → **Turn on CPU temperature** asks for admin once: it installs PawnIO if needed (via winget) and creates a scheduled task, so after that a tiny helper reads the sensor with no UAC prompts while Rigcard is open. The main window stays un-elevated so drag-and-drop keeps working. If HWiNFO is running with shared memory on, Rigcard can read it instead.

## Themes

Dark (default), Light, Windows (follows Windows light/dark), Midnight (pure black for OLED), Glass, plus Frost, Twilight, Forest, Sunset, Ocean, Rose, Graphite, Paper and Mist. Accent color (theme, Windows, swatches or any color), window background (solid, Mica, acrylic), text size, density, corner radius and animation speed. Themes use the suite's shared JSON format (see [Hangar's THEMES.md](https://github.com/larosa023/Hangar/blob/main/docs/THEMES.md)); drop files in `%LOCALAPPDATA%\Rigcard\themes`. Hangar can push a theme live.

Card styles are separate from the app theme: **Save**, **Export** and **Import** them as JSON files to share with others.

## Updates

Rigcard checks GitHub Releases on startup and every 3 hours (1–24, your choice), on the **Stable** or **Beta** channel. You get a small notice with the changelog and a one-click **Restart to update**, or let it install quietly when you're away. Downloads are verified against GitHub's SHA-256 digest and `SHA256SUMS.txt`; if the new version doesn't start within 20 seconds, the old one is put back and that version is skipped. It never updates during a benchmark. Settings → Updates → **Roll back** keeps the previous two versions.

When Hangar is installed, Rigcard also speaks the Hangar SDK protocol: Hangar sees its health pings, waits while a benchmark runs, live-updates it and restores the page you were on.

## Data

Everything lives in `%LOCALAPPDATA%\Rigcard` (or `data\` next to the exe in portable mode): `build.json`, `photo.jpg`, `benchmarks.json`, `settings.json`, `styles\`, `themes\` and `rigcard.log`.

## Build

```bash
./build.sh                        # Linux/macOS: needs Zig (pip install ziglang==0.13.0); makensis for the installer
cmake -S . -B build -A x64 && cmake --build build --config Release   # Windows, VS 2022
```

A release is published automatically when `RIGCARD_VERSION` in `src/common.h` changes on `main` (versions with a `-` are pre-releases on the Beta channel). The share site deploys from `web/` to GitHub Pages. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
