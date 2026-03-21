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
![2026032121055300-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/7609c91c-9802-45bb-8626-27a56e52dc98)
![2026032121060000-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/9307443d-f0c7-4eab-a054-1d89c52c40c5)
![2026032121061300-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/7ec86321-7131-48e2-97bc-68adfb80ae18)


## Features

- Changes take effect immediately via Atmosphere's IPC reload (no reboot)
- Entries grouped by category: Telemetry, System Updates, Game Content, eShop
- Color-coded status indicators (red/green)
- Atomic file writes (tmp + rename) to prevent corruption
- Auto-detects which hosts file to use (emummc > sysmmc > default)
- Confirmation dialog if you try to quit with unsaved changes
- Quick-apply profiles: block all, allow game updates, telemetry only, allow all
- Built-in connectivity test that TCP pings each host on port 443
- Toast notifications for save/reload/error feedback
- Scrollbar, alternating row colors, rounded UI elements

## Controls

| Button | Action |
|--------|--------|
| Up/Down | Navigate entries |
| Left/Right | Page up/down |
| A | Toggle selected entry |
| X | Open profiles menu |
| Y | Save & reload DNS |
| L | Run server connectivity test |
| - | Seed default Nintendo entries |
| + | Quit (with unsaved changes check) |

## Installation

1. Download `AetherBlock.nro` from the [latest release](https://github.com/hexbyt3/AetherBlock/releases/latest)
2. Copy to `/switch/AetherBlock/AetherBlock.nro` on your SD card
3. Launch from the Homebrew Menu

## Building

Requires [devkitPro](https://devkitpro.org/) with `switch-dev`, `switch-sdl2`, `switch-sdl2_ttf`, and `switch-freetype`.

```bash
(dkp-)pacman -S switch-dev switch-sdl2 switch-sdl2_ttf switch-freetype
make
```

## How It Works

AetherBlock reads and modifies Atmosphere's hosts file at `/atmosphere/hosts/default.txt`. Entries prefixed with `;` are disabled. Active entries redirect hostnames to `127.0.0.1`.

After saving, it calls Atmosphere's IPC command 65000 on `sfdnsres` to reload the hosts file in memory.

The connectivity test does non-blocking TCP connects on port 443, one host per frame so the UI stays responsive.

## Credits

- DNS reload mechanism via [DNS-MITM_Manager](https://github.com/znxDomain/DNS-MITM_Manager) by znxDomain
- Server list from [NintendoClients Wiki](https://github.com/kinnay/NintendoClients/wiki/Server-List)
- Built with [libnx](https://github.com/switchbrew/libnx) and [SDL2](https://www.libsdl.org/)

## License

MIT
