# MSX4PS3 v1.00

MSX4PS3 is a high-definition MSX, MSX2, and MSX2+ emulator developed specifically for the Sony PlayStation 3 (compatible with Custom Firmware CFW and PS3HEN).

* Port and Architecture: theheroGAC
* Core Emulation Engine: fMSX Portable Core by Marat Fayzullin
* Open Source Firmware: C-BIOS Architecture by BouKiCHi and openMSX Team
* Platform: Sony PlayStation 3 (PSL1GHT SDK + RSX Hardware Acceleration)

---

## Key Features

* Native 720p and 1080p 60Hz HDMI progressive output with hardware-accelerated RSX blit.
* Complete hardware emulation of MSX1 (TMS9918), MSX2 (V9938), and MSX2+ (V9958).
* Full multi-chip audio subsystem running at 48000Hz:
  * AY-3-8910 Programmable Sound Generator (PSG)
  * Konami SCC and SCC+ 5-channel wavetable sound chip
  * Yamaha YM2413 (FM-PAC / MSX-MUSIC) 9-channel FM synthesis
* Modern UAE4ALL2-HD graphical user interface with TrueType font rendering.
* Dynamic on-screen Virtual Keyboard with authentic keycap positioning and dual-symbol Shift support.
* Unified Game Library browser scanning internal HDD and external FAT32 USB drives (usb000 through usb007).
* 10 independent Save State slots per game, featuring real-time graphical preview thumbnails.
* Native ZIP archive decompression with direct cache streaming.
* TV Safe Area margins compliant with CRT and LCD displays.
* Integrated legal C-BIOS firmware with automatic fallback and seamless OEM commercial BIOS support.

---

## Installation

1. Download `MSX4PS3.pkg` (or `MSX4PS3.gnpdrm.pkg`) from the GitHub Releases page and copy it to the root of a FAT32 USB storage drive.
2. Insert the USB drive into your PlayStation 3.
3. On the PS3 XMB, navigate to `Game` -> `Package Manager` -> `Install Package Files` -> `Standard`.
4. Select `MSX4PS3 v1.00` and confirm installation.
5. Launch `MSX4PS3` from the XMB Game column.

---

## Building from Source

### Prerequisites
* PSL1GHT v2 Toolchain (`ppu-gcc`, `ppu-strip`, `sprxlinker`, `make_self_npdrm`, `sfo`, `pkg`, `package_finalize`).
* Portlibs for PSL1GHT: `SDL2`, `SDL2_ttf`, `freetype`, `zlib`.

### Compilation
1. Clone this repository:
   ```bash
   git clone https://github.com/theheroGAC/MSX4PS3.git
   cd MSX4PS3
   ```
2. Build the packages using the automated script:
   ```bash
   ./build.sh
   ```
   Or directly via `make`:
   ```bash
   make -j$(nproc)
   ```
3. The build process generates `MSX4PS3.pkg` (retail/CFW) and `MSX4PS3.gnpdrm.pkg` (PS3HEN/NoDRM) in the project root directory.

---

## Directory Structure

MSX4PS3 creates and monitors the following standard directories on `/dev_hdd0/`:

* Application Home: `/dev_hdd0/game/FMSX00001/USRDIR/`
* Internal ROM Directory: `/dev_hdd0/fmsx/roms/`
* BIOS ROM Directory: `/dev_hdd0/fmsx/bios/`
* Save State Files: `/dev_hdd0/fmsx/states/`
* Battery-backed RAM Saves: `/dev_hdd0/fmsx/saves/`
* Configuration File: `/dev_hdd0/fmsx/fmsx_ps3.cfg`

### USB Storage Paths

External USB drives formatted in FAT32 are automatically scanned across ports `/dev_usb000` to `/dev_usb007`:

* `/dev_usb000/roms/msx/`
* `/dev_usb000/ROMS/msx/`
* `/dev_usb000/roms/`
* `/dev_usb000/msx/`
* `/dev_usb000/bios/`

---

## Supported Media Formats

* Cartridge ROMs: `.rom`, `.mx1`, `.mx2` (Supports Standard 16K/32K, MegaROM, ASCII 8K/16K, Konami, SCC)
* Floppy Disk Images: `.dsk` (Supports single-sided 360KB and double-sided 720KB 3.5" disks)
* Cassette Tape Images: `.cas` (Supports automatic tape loader)
* Compressed Archives: `.zip` (Direct ZIP inspection and extraction of MSX media)

---

## Controller Layout and Shortcuts

### In-Game Emulation

* D-Pad / Left Stick: MSX 8-Way Directional Pad / Cursor Keys
* Cross: Joystick Fire A / Spacebar
* Circle: Joystick Fire B / M key
* Square: MSX Function Key F1
* Triangle: MSX Function Key F2
* R1: MSX Return / Enter Key
* L1: MSX Escape / Stop Key
* Start or R2: Open In-Game Quick Menu
* Select or L2: Toggle On-Screen Virtual Keyboard
* L3: 3x Fast-Forward Speed Acceleration
* R3: Instant Quick Save State

### Menu Navigation

* L1 / R1: Cycle Menu Tabs (Library, Slots, Files, States, Machine, Display, Keyboard, Credits)
* D-Pad Up / Down: Move Selection
* D-Pad Left / Right: Page Up / Page Down or Adjust Sliders
* Cross: Confirm / Launch Game / Load State
* Square: Category Filter (in Library) / Save State (in States) / Eject (in Slots)
* Triangle: Rescan Drives (in Library) / Delete State (in States) / Import BIOS (in Machine)
* Circle: Resume MSX Session / Exit Menu

---

## BIOS Firmware Configuration

MSX4PS3 includes legal open-source C-BIOS firmware built into the application package, allowing immediate emulation of most MSX cartridge games without requiring copyrighted dumps.

To run disk games (.dsk), MSX-BASIC, or software requiring authentic system sub-ROMs, place original BIOS ROM dumps in `/dev_hdd0/fmsx/bios/` or `/dev_hdd0/game/FMSX00001/USRDIR/bios/`:

* MSX.ROM (32 KB) - MSX1 BIOS and BASIC
* MSX2.ROM (32 KB) - MSX2 BIOS and BASIC
* MSX2EXT.ROM (16 KB) - MSX2 Sub-ROM and Extended BASIC
* MSX2P.ROM (32 KB) - MSX2+ BIOS and BASIC
* MSX2PEXT.ROM (16 KB) - MSX2+ Sub-ROM
* DISK.ROM (16 KB) - WD1793 Disk Controller BIOS (Required for .dsk floppy disks)
* FMPAC.ROM (64 KB) - Yamaha YM2413 FM-PAC Firmware
* PAINTER.ROM (128 KB) - Kanji Font ROM

### One-Click USB Import

1. Create a `bios` folder on the root of a FAT32 USB drive and copy the ROM files inside.
2. Insert the USB drive into any PS3 USB port.
3. Launch MSX4PS3. If real BIOS files are not yet installed, the BIOS Notice dialog will automatically appear.
4. Press Triangle ("Copy from USB") to automatically copy all detected BIOS files to the internal hard drive.

---

## Display and Audio Options

* Aspect Ratio Modes: 4:3 Authentic MSX, 16:9 Full Screen, 1:1 Pixel Perfect.
* CRT Scanline Simulation: Off, Light (25%), Classic (50%).
* Sprite Limit: Authentic 4/8 sprites per line (authentic flicker) or Unlimited (no flicker).
* Texture Filtering: Bilinear Smooth or Sharp Nearest Neighbor.
* Color Palettes: Official MSX, V9938 Vibrant Studio, Cool Monochrome.
* Independent Sound Volume Sliders: Master Volume, PSG Volume, SCC Volume, FM-PAC Volume.
* Virtual Keyboard Themes: Original Grey, Warm Beige, Cool Blue, Dark Carbon.

---

## Credits and Licenses

* PlayStation 3 Port: theheroGAC
* fMSX Emulation Engine: Marat Fayzullin
* C-BIOS: BouKiCHi, Maarten ter Huurne, Manuel Bilderbeek (BSD 2-Clause License)
* openMSX Research & Tools: openMSX Team
* PSL1GHT Open Source PS3 SDK and PSL1GHT Contributors
