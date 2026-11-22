<p align="center">
  <img src="assets/icon.jpg" alt="NX-RaptureReader icon" width="160" />
</p>

<h1 align="center">NX-RaptureReader</h1>

<p align="center">
  A lightweight, OLED-native e-book, manga, comic, and PDF reader written in C++17 (<code>SDL2</code> + <code>FreeType2</code> + <code>zlib</code>) for <strong>Nintendo Switch</strong> and <strong>macOS / PC</strong>.
</p>

## Features

- **Formats:**
  - **EPUB:** Streaming ZIP reader, messy real-world path resolution (`%20`, `../`, `#fragment`), `toc.ncx` / EPUB3 `nav.xhtml` Table of Contents, `<em>`/`<strong>` styling, FreeType kerning, cover art extraction, and lazy per-chapter pagination with drift-free anchor bookmarks.
  - **CBZ (Manga & Comics):** `JPEG`, `PNG`, `BMP`, `GIF` (via vendored `stb_image.h`) and `WebP` (`libwebp`), `ComicInfo.xml` metadata, natural numeric page sorting, Right-to-Left (`RTL`) & Left-to-Right (`LTR`) reading directions, Fit Page / Fit Width / 1.5x / 2.0x zoom with pan, 1-page-ahead decode prefetch, and cover thumbnails.
  - **PDF:** Native Switch rendering via `switch-mupdf` (`ARMv8`) and macOS rendering via `CoreGraphics`, with sharp zoom re-rasterization, pan, page picker, and cover thumbnails.
- **OLED Black UI:** Pure `#000000` background with unlit background pixels completely off, `Mocha` & `Paper` alternate themes, software dark-room brightness dimmer (`30%`–`100%`), distraction-free HUD toggle, and `0° / 90° / 180° / 270°` screen rotation.
- **Console Ergonomics:** Configurable Switch idle/auto-sleep wake-lock (`3m / 5m / 10m / 15m / Sys`) ignoring Joy-Con analog drift, event-driven idle rendering, and persisted per-book progress (`.pos`) + global settings (`settings.cfg`).

## Controls

### Nintendo Switch (Joy-Con / Pro Controller & Touch)

| Context | Input | Action |
|---|---|---|
| **Library** | `D-Pad Up/Down` | Select book |
| | `A` / Tap row | Open book |
| | `X` / `Y` / `L3` / `R3` | Theme / Rotate / Brightness / Sleep timer |
| | `B` / `+` | Quit |
| **EPUB Reader** | `R` / `D-Right` / Tap right / Swipe left | Next page |
| | `L` / `D-Left` / Tap left / Swipe right | Previous page |
| | `D-Up` / `D-Down` | Previous / Next chapter |
| | `A` / Tap `[Ch X/Y]` footer | Open Table of Contents |
| | `ZR` / `ZL` | Font size +/- |
| | `-` / Tap center | Toggle HUD |
| **CBZ / PDF Reader** | `L` / `R` / `D-Left` / `D-Right` / Tap / Swipe | Turn page (respects `RTL` / `LTR` in CBZ) |
| | `D-Up` / `D-Down` | Pan vertically when zoomed, or jump `-/+ 10` pages |
| | `Touch drag` (when zoomed) | Pan across page |
| | `ZR` / `ZL` / Tap `[Fit]` | Zoom (`Fit Page` / `Fit Width` / `1.5x` / `2.0x`) |
| | `X` (in CBZ) | Toggle `RTL` (Manga) / `LTR` (Comic) |
| | `A` / Tap `[Page X/Y]` footer | Open Page Picker |
| **Shared** | `X` / `Y` / `L3` / `R3` / `B` | Theme (except CBZ) / Rotate / Dim / Sleep timer / Return to Library |

### PC / Mac

- **Library / TOC / Page Picker:** `Up`/`Down` select, `PgUp`/`PgDn` scroll, `Enter` open/jump, `Esc`/`Backspace` close or quit, `Q` quit
- **EPUB:** `Left`/`Right`/`Space` page, `Up`/`Down` chapter, `C`/`Tab`/`Enter` chapters, `+`/`-` font size, `L` line spacing, `M` margins
- **CBZ / PDF:** `Left`/`Right`/`Space` page, `Up`/`Down` pan (when zoomed) or `-/+ 10` pages, `+`/`-`/`Z` zoom, `D` toggle RTL/LTR (CBZ), `C`/`Tab`/`Enter` page picker
- **Global:** `T` theme, `B` brightness dimmer, `R` rotate, `S` sleep timer, `H` toggle HUD, `F` fullscreen

## Building

### PC / macOS

Requires `SDL2`, `FreeType2`, `zlib`, and `libwebp` (`brew install sdl2 freetype zlib webp` on macOS; `sudo apt install build-essential pkg-config libsdl2-dev libfreetype-dev zlib1g-dev libwebp-dev` on Debian/Ubuntu).

```bash
make test          # Compile with -Wall -Wextra and run unit tests
make pc            # Build build/NX-RaptureReader-PC
make run-pc        # Build and launch
make check-assets  # Verify assets/ + books/ match their romfs/ copies
```

> **PDF support** is available on Nintendo Switch (`switch-mupdf`) and macOS (`CoreGraphics`). On Linux/other desktops the app builds and runs, but `.pdf` files are listed but cannot be opened.

### Nintendo Switch (`.nro` via Docker)

Requires Docker (`devkitpro/devkita64` with `switch-sdl2`, `switch-freetype`, `switch-zlib`, `switch-libwebp`, `switch-mupdf`, and `libnx`):

```bash
make switch-image                  # Build/refresh cached Docker image
make switch                        # Produce build/NX-RaptureReader.nro
make nxlink SWITCH_IP=192.168.x.x  # Push over Wi-Fi to Homebrew Menu NetLoader
```

Copy `build/NX-RaptureReader.nro` to `sdmc:/switch/NX-RaptureReader/NX-RaptureReader.nro` and place your `.epub`, `.cbz`, or `.pdf` files in `sdmc:/switch/NX-RaptureReader/books/`.

## License

Copyright (C) 2026 xleo36

`NX-RaptureReader` is free software: you can redistribute it and/or modify it under the terms of the **GNU Affero General Public License** as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version (`AGPL-3.0-or-later`). See [LICENSE](LICENSE) for the full license text.

Official Nintendo Switch `.nro` builds statically link `switch-mupdf` (GNU AGPL-3.0). See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for third-party library, font (Lato under SIL OFL 1.1), and bundled code (`stb_image.h`) notices.
