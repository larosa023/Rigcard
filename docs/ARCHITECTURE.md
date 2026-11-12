# Rigcard architecture

## Stack

**Native C++17, Win32 + Direct2D/DirectWrite/WIC, cross-compiled with Zig** — the same stack and toolchain as Hangar, Foyer, Strata and Breaker. One ~1.3 MB exe, no runtime, opens instantly, ~20–35 MB of RAM, no CPU when idle (timers only run while something animates; sensor polling only while the window is visible or live sharing is on).

Why not .NET like Ember? Ember is built on LibreHardwareMonitor, which is the main reason to pick .NET. Rigcard only needs CPU temperature and package power from the driver, which is two register reads, so it talks to the same **PawnIO** driver directly (using the same signed PawnIO modules, see `res/pawnio`) and keeps the suite's lightweight native footprint.

The card renderer (`cardrender.cpp`) is one Direct2D code path for both the live preview and PNG export (a WIC bitmap render target), so exports match the preview exactly. CI runs it under Wine to render sample cards on every build.

## Where the data comes from

| | Source |
|---|---|
| CPU, cores, base clock | WMI `Win32_Processor`, CPUID |
| GPU, VRAM, driver | DXGI (exact VRAM, >4 GB) + WMI `Win32_VideoController` |
| Motherboard, BIOS | WMI `Win32_BaseBoard`, `Win32_BIOS` |
| Memory | WMI `Win32_PhysicalMemory` (capacity, configured speed, DDR type, modules, part number) |
| Drives | `MSFT_PhysicalDisk` (NVMe/SSD/HDD), fallback `Win32_DiskDrive` |
| Monitors | Display config API (friendly name, resolution, refresh rate) |
| Keyboard, mouse | HID product strings |
| Windows | Registry (with the Windows 11 fix for `ProductName`) |
| CPU temp, CPU power | PawnIO: AMD Zen `THM_TCON_CUR_TMP` + `MSR_PKG_ENERGY_STAT`; Intel `IA32_PACKAGE_THERM_STATUS`/TjMax + RAPL. Via the elevated helper (scheduled task) unless Rigcard itself runs as admin; HWiNFO shared memory as a fallback |
| GPU temp/power/clock/load | NVML (NVIDIA); D3DKMT adapter perf data (any WDDM 2.4+ GPU); PDH GPU-engine counters for load |
| CPU load and clock | PDH `% Processor Utility` / `% Processor Performance` × base clock |

## Sharing

- Site: `web/` (static HTML/CSS/JS) on GitHub Pages, deployed by `.github/workflows/pages.yml`. Free, no server to run.
- Quick link `#d=…`: compact card JSON, raw DEFLATE (`src/deflate.h`), base64url. The page inflates it with the browser's `DecompressionStream`. ~1.3 KB, fits in a Discord message.
- Hosted link `#g=<gist id>`: a secret gist on your account holding `rigcard.json` (the card), `photo.txt` (a JPEG data URL) and `live.json` (live stats). Rigcard creates it once, then PATCHes it when the build changes (debounced) and pushes `live.json` every 30 s–5 min while live sharing is on. The page loads the gist through the GitHub API and polls the raw `live.json`.
- Everything shared is built by `share::CardJson`, which applies the privacy settings.

## Updates and Hangar

- `selfupdate.cpp` is Hangar's updater model: releases from Hangar's shared release index (falls back to the GitHub API), SHA-256 checked against GitHub's digest and `SHA256SUMS.txt`, swap the exe, start the new one with `--after-update`, wait for its "healthy" signal, roll back otherwise. Keeps the previous two versions for manual rollback. Refuses to apply during a benchmark.
- `hangar_sdk.cpp` implements Hangar's IPC protocol v1 (`docs/IPC.md` in Hangar): pipe `\\.\pipe\larosa023.rigcard.<session>`, run file `%LOCALAPPDATA%\Hangar\run\rigcard.json`, `health` / `busy` / `idle` / `restored` messages, and `status`, `prepare-update`, `restart`, `cancel-update`, `apply-theme`, `apply-settings`, `export-settings` requests. It has no Rigcard-specific code beyond the app id, so other C++ apps can reuse it.

## Source map

```
src/main.cpp          command line, single instance
src/ui.cpp            window, widgets (buttons, toggles, sliders, text fields, menus), input, background results
src/page_*.cpp        Card (preview + Customize panel), Parts, Benchmarks, Settings
src/cardrender.cpp    the card (layouts for wide, square, portrait, phone)
src/card.cpp          build model, card styles, JSON
src/hardware.cpp      detection + rescan merge
src/sensors.cpp       live stats, PawnIO, elevated helper, NVML, D3DKMT, HWiNFO
src/bench.cpp         benchmarks, imports, history
src/share.cpp         share JSON, quick links, gist publishing
src/image.cpp         WIC: load (EXIF rotation), blur, JPEG/PNG, clipboard
src/selfupdate.cpp    self-updater      src/releases.cpp  GitHub releases
src/hangar_sdk.cpp    Hangar IPC        src/partsdb.cpp   parts picker catalog
web/                  share-page viewer installer/  NSIS installer
```
