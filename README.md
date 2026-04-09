<p align="center">
  <img src="AetherBlock.jpg" alt="AetherBlock" width="200">
</p>

<h1 align="center">AetherBlock</h1>

<p align="center">
  A Nintendo Switch homebrew app for CFW management.<br>
  DNS blocking, firmware downloads, Atmosphere updates -- all without leaving your Switch.
</p>

<p align="center">
  <a href="https://github.com/hexbyt3/AetherBlock/releases/latest">
    <img src="https://img.shields.io/github/v/release/hexbyt3/AetherBlock?style=flat&label=Latest%20Release&color=green" alt="Latest Release">
  </a>
  <a href="https://github.com/hexbyt3/AetherBlock/releases/latest">
    <img src="https://img.shields.io/github/downloads/hexbyt3/AetherBlock/total?style=flat&label=Downloads&color=blue" alt="Total Downloads">
  </a>
  <a href="https://github.com/hexbyt3/AetherBlock/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/hexbyt3/AetherBlock?style=flat&color=yellow" alt="License">
  </a>
  <a href="https://github.com/hexbyt3/AetherBlock/stargazers">
    <img src="https://img.shields.io/github/stars/hexbyt3/AetherBlock?style=flat&color=orange" alt="Stars">
  </a>
  <a href="https://github.com/hexbyt3/AetherBlock/actions">
    <img src="https://img.shields.io/github/actions/workflow/status/hexbyt3/AetherBlock/build-release.yml?style=flat&label=Build" alt="Build Status">
  </a>
</p>

---
## Screenshots
![2026032121055300-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/08c684b0-bce7-4784-a28d-7597e550935f)
![2026032121060000-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/1cf5d21b-567f-40aa-a65d-410ae4acb56f)
![2026032121061300-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/bd216d12-f7ee-4778-8d5b-f92bd658dc3b)

NEW!  Firmware & CFW Updates
![2026032017353700-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/75e002d8-6b34-4622-90f9-0c736f4809d8)
![2026032017352900-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/9dca6861-56c5-4a5c-a01d-7c236c109e4c)




## Features

### DNS Hosts Manager
- Toggle Nintendo server blocks on/off with changes applied immediately (no reboot)
- Entries grouped by category: Telemetry, System Updates, Game Content, eShop
- Quick-apply profiles: Block All, Allow Game Updates, Telemetry Only, Allow All
- Built-in connectivity test that TCP pings each host on port 443
- Auto-detects which hosts file to use (emummc > sysmmc > default)
- Atomic file writes to prevent corruption

### Atmosphere Settings Manager
- Toggle verified `system_settings.ini` overrides from a UI (reboot required to apply)
- All settings sourced from Atmosphere's `settings_sd_kvs.cpp` -- nothing unverified
- Categories: Update Suppression, Network, Telemetry, Homebrew
- Preserves existing comments and manual edits in your INI file
- Safe for sysnand online play -- all overrides are local only

### Firmware Manager
- Download Nintendo firmware (HOS) directly from the Switch
- Browse available firmware versions and select the one you need
- Downloads and extracts firmware, then hands off to Daybreak for installation
- No PC required -- download, extract, and install all from the homebrew menu

### CFW Package Updater
- Downloads the latest [CFW4SysBots](https://github.com/hexbyt3/CFW4SysBots) package directly from GitHub
- Includes everything: Atmosphere, Hekate, sys-botbase, ldn_mitm, AetherBlock, and more
- Auto-detects your console type (Mariko/Erista) and downloads the right package
- Preserves your DNS hosts, system settings, sysmodules, and configs during update
- Mod-chipped users just reboot -- no jig, no RCM, no payload injection

## Controls

| Button | Action |
|--------|--------|
| Up/Down | Navigate entries |
| Left/Right | Page up/down |
| A | Toggle / Select / Confirm |
| X | Open profiles menu |
| Y | Save & reload DNS |
| L | Run server connectivity test |
| R | Atmosphere settings |
| ZL | Firmware Manager |
| ZR | CFW Package Updater |
| - | Seed default Nintendo entries |
| + | Quit (with unsaved changes check) |

## Installation

1. Download `AetherBlock.zip` from the [latest release](https://github.com/hexbyt3/AetherBlock/releases/latest)
2. Extract to the root of your SD card (places the NRO in `/switch/AetherBlock/`)
3. Make sure Daybreak is at `/switch/daybreak.nro` for firmware installation
4. Launch from the Homebrew Menu

Or grab the full CFW package from [CFW4SysBots](https://github.com/hexbyt3/CFW4SysBots) which includes AetherBlock, Daybreak, and everything else you need.

## Updating Your Switch

One guided flow updates both your CFW and firmware with a single reboot at the end. AetherBlock walks you from one step to the next automatically.

1. Open AetherBlock from the Homebrew Menu and press **ZR** to open the CFW Package Updater
2. Press **A** to check for the latest CFW4SysBots package, then **A** again to download and install it
3. When extraction finishes, press **A** — AetherBlock jumps straight to the Firmware Manager for you
4. Press **A** to fetch the Nintendo firmware list, pick the version you want, and press **A** to download
5. When extraction finishes, press **A** to launch Daybreak
6. In Daybreak: **Continue**, accept the default options, and let it install
7. When Daybreak finishes, tap **Reboot** — Atmosphere's reboot-to-payload kicks in and the Switch comes back up on the new CFW and firmware

That's it. No PC, no RCM jig, no manual file management.

### Why CFW First?

Atmosphere has to be updated before the reboot that loads the new firmware. New Atmosphere releases add support for new Nintendo firmware versions — if only the firmware gets installed and the old Atmosphere is still on the SD card at boot, it won't be able to launch CFW.

Extracting the CFW package just places files on the SD card; it doesn't touch the currently running system. The old Atmosphere keeps running in memory while you do everything else. The new files only matter at boot, which is why you can do CFW → firmware → one reboot back to back.

### How AetherBlock Handles Locked Files

A couple of Atmosphere files (`package3`, `stratosphere.romfs`, and sometimes `AetherBlock.nro` itself) are held open by the running CFW and can't be overwritten with a plain file write. AetherBlock handles this transparently:

1. During CFW extraction, it first tries a direct write, then a stash-and-rename, then a libnx direct-write path that bypasses stdio's share semantics. Most files land at that stage.
2. For anything that still refuses to budge, the new content is staged as `<file>.ab_new` and queued in `/config/AetherBlock/pending.txt`.
3. Right before the post-Daybreak reboot, AetherBlock tries to swap the staged files into place using a backup-rename-restore pattern so the real file is never in a missing state.
4. On the next launch after reboot, any sidecars that are still around get one more attempt — including a byte-for-byte content check so redundant `.ab_new` files get cleaned up automatically.

The net effect: you never have to manually rename anything, and stale sidecars don't accumulate on the SD card.

## Building

Requires [devkitPro](https://devkitpro.org/) with the following packages:

```bash
(dkp-)pacman -S switch-dev switch-sdl2 switch-sdl2_ttf switch-freetype switch-curl switch-mbedtls switch-zlib
make
```

## How It Works

**DNS Hosts:** Reads and modifies Atmosphere's hosts file at `/atmosphere/hosts/default.txt`. Entries prefixed with `;` are disabled. After saving, it calls Atmosphere's IPC command 65000 on `sfdnsres` to reload the hosts file in memory -- no reboot needed.

**System Settings:** Reads and modifies `/atmosphere/config/system_settings.ini`. Atmosphere parses this file at boot and overrides the corresponding system settings via its set:sys mitm service. Changes here require a reboot to take effect.

**Firmware Manager:** Fetches a firmware manifest from the CFW4SysBots repo, downloads the selected firmware ZIP, extracts it to `/firmware/`, then chain-loads Daybreak with the firmware path pre-set. Daybreak handles the actual firmware flash.

**CFW Package Updater:** Hits the GitHub API for the latest CFW4SysBots release, auto-selects the right package for your console (mod-chipped or unpatched), and downloads/extracts the full CFW bundle. Your hosts files, system settings, sysmodules, and AetherBlock config are preserved during extraction.

## Credits

- DNS reload mechanism via [DNS-MITM_Manager](https://github.com/znxDomain/DNS-MITM_Manager) by znxDomain
- Server list from [NintendoClients Wiki](https://github.com/kinnay/NintendoClients/wiki/Server-List)
- Download/extraction patterns adapted from [AIO-Switch-Updater](https://github.com/HamletDuFromage/aio-switch-updater)
- Settings verified against [Atmosphere](https://github.com/Atmosphere-NX/Atmosphere) source (`settings_sd_kvs.cpp`)
- Built with [libnx](https://github.com/switchbrew/libnx), [SDL2](https://www.libsdl.org/), [cJSON](https://github.com/DaveGamble/cJSON), and [libcurl](https://curl.se/libcurl/)

## License

MIT
