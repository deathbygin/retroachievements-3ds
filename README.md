# RetroAchievements for Nintendo 3DS

A graphical frontend homebrew application for the Nintendo 3DS (`.3dsx`) that connects to the official [RetroAchievements](https://retroachievements.org/) Web API.

---

## Features

- **Dual-Screen Layout:**
  - **Top Screen (400x240):** Displays user profile stats (rank, hardcore/softcore points, avatar, rich presence activity) and selected achievement details with live badge art.
  - **Bottom Screen (320x240):** Interactive, scrollable lists for recent games and achievement sets with off-screen culling.
- **HTTPS & TLS Connectivity:**
  - Built-in multi-root CA bundle supporting Modern Google Trust Services / GlobalSign ECC certificates used by RetroAchievements.
  - Non-blocking Wi-Fi preflight status checks.
- **High-Performance Asset Pipeline:**
  - LRU linear RAM texture cache with 8x8 PICA200 Morton swizzling.
  - Background asynchronous image downloader thread for badge art, game icons, and user avatars.
  - Persistent on-device SD card caching (`sdmc:/3ds/retroachievements/cache/`).
- **Secure Configuration:**
  - Simple INI configuration (`sdmc:/3ds/retroachievements/config.ini`) with auto-generated template on first boot.
  - Volatile memory zeroing for API keys upon teardown.

---

## Configuration

Place your credentials in `sdmc:/3ds/retroachievements/config.ini`:

```ini
[retroachievements]
username = YOUR_USERNAME
web_api_key = YOUR_WEB_API_KEY
sound_enabled = 1
preferred_screen = 0
```

> **Note:** Obtain your Web API key under **Settings > Keys** on [RetroAchievements.org](https://retroachievements.org/controlpanel.php).

---

## Controls

| Button | Action |
| :--- | :--- |
| **D-Pad Up / Down** | Scroll through achievement / game list |
| **Circle Pad Up / Down** | Smooth list scroll |
| **A** | Sync profile summary / Refresh data |
| **X** | Toggle between Recent Games and Game Achievement View |
| **START** | Clean exit to Homebrew Launcher |

---

## Building from Source

### Prerequisites
- [devkitPro](https://devkitpro.org/) with `devkitARM`
- `libctru` (2.7.0+)
- `citro3d` (1.7.1+)
- `citro2d` (1.7.0+)
- `3ds-curl`
- `3ds-mbedtls`
- `3ds-jansson`
- `3ds-libpng`
- `3ds-zlib`

### Compilation
Open a terminal in the project root:

```bash
make
```

The compiled binaries will be output in the root directory:
- `RetroAchievements.3dsx`
- `RetroAchievements.smdh`

---

## License

This project is licensed under the MIT License.

