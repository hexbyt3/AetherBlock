<p align="center">
  <img src="AetherBlock.jpg" alt="AetherBlock" width="200">
</p>

<h1 align="center">AetherBlock</h1>

<p align="center">
  A Nintendo Switch homebrew app for managing Atmosphere's DNS MITM hosts file.<br>
  Toggle Nintendo server blocks on/off with a polished full-screen UI — no reboot required.
</p>

---
## Screenshots
![2026032121055300-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/7609c91c-9802-45bb-8626-27a56e52dc98)
![2026032121060000-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/9307443d-f0c7-4eab-a054-1d89c52c40c5)
![2026032121061300-DB1426D1DFD034027CECDE9C2DD914B8](https://github.com/user-attachments/assets/7ec86321-7131-48e2-97bc-68adfb80ae18)


## Features

### Core
- **Live DNS Reload** — Changes take effect immediately via Atmosphere's IPC reload (no reboot needed)
- **Category View** — Entries grouped by type: Telemetry, System Updates, Game Content, eShop
- **Color-Coded Status** — Red = blocked, Green = allowed at a glance
- **Atomic Save** — File writes use tmp+rename to prevent corruption
- **Auto-Detection** — Respects Atmosphere's hosts file priority (emummc > sysmmc > default)
- **Unsaved Changes Protection** — Confirmation dialog prevents accidental data loss

### Profiles
Quick-apply preset blocking strategies:
- **Block All** — Block all known Nintendo servers
- **Allow Game Updates** — Block firmware & telemetry only, allow game updates through
- **Telemetry Only** — Only block analytics/error reporting
- **Allow All** — Disable all blocks

### Server Connectivity Test
Verify your DNS blocks are actually working:
- **TCP Ping Test** — Tests each blocked hostname via port 443
- **Latency Measurement** — Shows connection time in ms for reachable hosts
- **Color-Coded Results** — Green = blocked (good), Red = reachable (warning)
- **Real-Time Progress** — Visual progress bar with auto-scrolling results
- **Re-Test** — Run the test again anytime with a single button press

### UI
- **Toast Notifications** — Color-coded feedback (success, warning, error, info)
- **Scrollbar** — Visual thumb indicator for long lists
- **Rounded UI Elements** — Modern, polished appearance
- **Button Hint Pills** — Styled control hints in the bottom bar
- **Alternating Row Backgrounds** — Easy-to-read entry lists

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

Requires [devkitPro](https://devkitpro.org/) with the following packages:
- `switch-dev`
- `switch-sdl2`
- `switch-sdl2_ttf`
- `switch-freetype`

```bash
# Install dependencies
(dkp-)pacman -S switch-dev switch-sdl2 switch-sdl2_ttf switch-freetype

# Build
make
```

## How It Works

AetherBlock reads and modifies Atmosphere's hosts file at `/atmosphere/hosts/default.txt`. Entries prefixed with `;` are treated as disabled (host resolves normally). Active entries redirect hostnames to `127.0.0.1` (blocked).

After saving, the app calls Atmosphere's custom IPC command 65000 on the `sfdnsres` service to reload the hosts file in memory — no reboot required.

The connectivity test performs non-blocking TCP connections on port 443 to each enabled host, one per frame, to verify blocks are effective without freezing the UI.

## Credits

- DNS reload mechanism discovered via [DNS-MITM_Manager](https://github.com/znxDomain/DNS-MITM_Manager) by znxDomain
- Server list sourced from [NintendoClients Wiki](https://github.com/kinnay/NintendoClients/wiki/Server-List)
- Built with [libnx](https://github.com/switchbrew/libnx) and [SDL2](https://www.libsdl.org/)

## License

MIT
