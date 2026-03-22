<p align="center">
  <img src="AetherBlock.jpg" alt="AetherBlock" width="200">
</p>

<h1 align="center">AetherBlock</h1>

<p align="center">
  A Nintendo Switch homebrew app for managing Atmosphere's DNS MITM hosts file.<br>
  Toggle Nintendo server blocks on/off with a full-screen UI, no reboot required.
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
- Categories: Network (DNS MITM controls), Telemetry (error uploads), Homebrew (cheats, debug mode, bluetooth DB)
- Preserves existing comments and manual edits in your INI file
- Safe for sysnand online play -- all overrides are local only

### Atmosphere Release Checker
- Fetches the latest Atmosphere release from GitHub in a background thread
- Shows your current firmware and Atmosphere versions side by side
- Compares against the latest release and tells you if an update is available
- Extracts supported firmware version from release notes
- Scrollable release notes viewer

## Controls

| Button | Action |
|--------|--------|
| Up/Down | Navigate entries |
| Left/Right | Page up/down |
| A | Toggle selected entry |
| X | Open profiles menu |
| Y | Save & reload DNS |
| L | Run server connectivity test |
| R | Atmosphere settings |
| - | Seed default Nintendo entries |
| + | Quit (with unsaved changes check) |

**Settings Screen:** A Toggle | Y Save | X Release Checker | B Back

**Release Checker:** A Refresh | B Back | Up/Down scroll notes

## Installation

1. Download `AetherBlock.nro` from the [latest release](https://github.com/hexbyt3/AetherBlock/releases/latest)
2. Copy to `/switch/AetherBlock/AetherBlock.nro` on your SD card
3. Launch from the Homebrew Menu

## Building

Requires [devkitPro](https://devkitpro.org/) with the following packages:

```bash
(dkp-)pacman -S switch-dev switch-sdl2 switch-sdl2_ttf switch-freetype switch-curl switch-jansson switch-mbedtls switch-zlib
make
```

## How It Works

**DNS Hosts:** Reads and modifies Atmosphere's hosts file at `/atmosphere/hosts/default.txt`. Entries prefixed with `;` are disabled. After saving, it calls Atmosphere's IPC command 65000 on `sfdnsres` to reload the hosts file in memory -- no reboot needed.

**System Settings:** Reads and modifies `/atmosphere/config/system_settings.ini`. Atmosphere parses this file at boot and overrides the corresponding system settings via its set:sys mitm service. Changes here require a reboot to take effect.

**Release Checker:** Hits the GitHub API (`/repos/Atmosphere-NX/Atmosphere/releases/latest`) on a background thread and parses the JSON response with jansson. Reads local Atmosphere version via `splGetConfig` with config item 65000.

## Credits

- DNS reload mechanism via [DNS-MITM_Manager](https://github.com/znxDomain/DNS-MITM_Manager) by znxDomain
- Server list from [NintendoClients Wiki](https://github.com/kinnay/NintendoClients/wiki/Server-List)
- Settings verified against [Atmosphere](https://github.com/Atmosphere-NX/Atmosphere) source (`settings_sd_kvs.cpp`)
- Built with [libnx](https://github.com/switchbrew/libnx), [SDL2](https://www.libsdl.org/), [libcurl](https://curl.se/libcurl/), and [jansson](https://github.com/akheron/jansson)

## License

MIT
