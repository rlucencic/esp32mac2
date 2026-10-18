<h1 align="center">esp32mac2</h1>

<p align="center">
  <b>A colour Macintosh LC on an M5Stack Tab5.</b><br>
  68020, 640×480 in 256 colours, up to 10 MB, System 7.5.5, disk images of any size, Bluetooth keyboard and mouse.
</p>

<p align="center">
  <img alt="ESP32-P4" src="https://img.shields.io/badge/ESP32--P4-M5Stack%20Tab5-e7352c">
  <img alt="ESP-IDF" src="https://img.shields.io/badge/ESP--IDF-v5.5-blue">
  <img alt="LVGL" src="https://img.shields.io/badge/LVGL-9.4-5e48e8">
  <img alt="68020" src="https://img.shields.io/badge/CPU-Musashi%2068020-555">
  <img alt="MIT licence" src="https://img.shields.io/badge/licence-MIT-green">
</p>

esp32mac2 turns an M5Stack Tab5 into a Macintosh LC, Apple's first low-cost
colour Mac: a 68020, the V8 chip's built-in colour video on a 13-inch
(640×480) or 12-inch (512×384) monitor, ADB keyboard and mouse through the
Egret microcontroller, and the Apple Sound Chip. A Bluetooth keyboard and
mouse (or your finger on the screen) work the Mac, and disk images on the
microSD card go into three drives. A setup screen on the touch panel, and a
web page on your network, handle everything else.

It's the colour sibling of [esp32mac](https://github.com/rlucencic/esp32mac)
(the compact black-and-white Macs: Plus, SE, Classic, on the same two
boards), and lives next to
[esp32ste](https://github.com/rlucencic/esp32ste) (Atari STE) and the other
machines on the shared runtime [esp32emu](https://github.com/rlucencic/esp32emu).
The Macintosh II is planned as a second profile.

## Features

**The machine**
- 68020 at 15.67 MHz ([Musashi](https://github.com/kstenerud/Musashi) core), no FPU (as a stock LC),
  at about a real LC's speed (its 16-bit path to memory is taken into account), full speed on the Tab5
- 2, 4, 6 or 10 MB of RAM (the board's 2 MB plus a SIMM pair)
- Built-in video: 640×480 in black and white, 4, 16 or 256 colours, or
  512×384 up to thousands of colours; 256 colours by default, the Monitors
  control panel changes it
- Egret: ADB keyboard and mouse, the clock (set from the internet),
  parameter RAM kept across power-offs
- Apple Sound Chip (the startup chime, system beeps and sound)
- System 7.5.5, tested on the Tab5 (Mac OS 7.6.1 starts too)

**Disks**
- **Three drives** for disk images of any size, through a fast replacement
  of the Mac's disk driver (from esp32mac)
- Raw images (`.dsk`, `.img`, `.hfv`), Disk Copy 4.2 images (`.image`),
  partitioned hard disk and CD images (`.hda`, `.iso`, `.toast`), and
  uncompressed `.dmg` images (read-only)
- Mac archives dropped on the web page (StuffIt, Compact Pro, BinHex,
  MacBinary) become disk images in your browser, as in esp32mac
- New blank disks from the setup screen or the web page

**Around it**
- Touch screen as mouse; setup screen (hold the top-right corner for 2 s);
  Bluetooth keyboard and mouse; web page at `http://macintosh2.local`; sound
  on the Tab5's speaker

## Hardware

An [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5): ESP32-P4 (RISC-V,
360 MHz), 16 MB flash, 32 MB PSRAM, a 5" 1280×720 touch screen (the Mac's
picture at 960×720), microSD, speaker; an ESP32-C6 for WiFi and Bluetooth.

The Makerfabs MaTouch ESP32-S3 7", the other board of the sibling projects,
isn't supported: its ESP32-S3 is too slow for a 68020, and only 2 MB of Mac
RAM would fit. [esp32mac](https://github.com/rlucencic/esp32mac) runs there.

## Getting started

1. [ESP-IDF](https://github.com/espressif/esp-idf) v5.5 and
   [esp32emu](https://github.com/rlucencic/esp32emu) checked out next to this
   project (`../esp32emu`).
2. Your own Macintosh LC ROM (512 KB, copyrighted, **not included**) as
   `roms/lc.rom` (see [roms/README.md](roms/README.md)).
3. Build and flash:

   ```sh
   idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=build-tab5/sdkconfig build
   idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=build-tab5/sdkconfig -p /dev/cu.usbmodem… flash
   ```

   Instead of flashing the ROM, you can copy it into a `roms` folder on the
   SD card.
4. On the setup screen: WiFi, then drop a system disk image on the web page
   and insert it into Drive 1, choose the memory and monitor, **Start**.

## How it works

- **`components/mac2`**: the LC, platform independent, with a desktop
  build (`components/mac2/host`) for testing: scripted keyboard and mouse,
  screenshots, sound recording, CPU and I/O traces.
- **`main`**: the Mac on the board: memory, ROM, settings, keyboard map,
  touch, drives and status.
- **[esp32emu](https://github.com/rlucencic/esp32emu)**: the board runtime and
  the CPU core, built as a 68020 here (`CONFIG_M68K_68020`).

The ROM is patched in memory, every patch checked against the expected
bytes: no ROM checksum test, no FPU (the ROM's probe needs a bus error the
CPU core doesn't make), and the `.Sony` floppy driver replaced by esp32mac's
paravirtual one. Egret is emulated at the level of its packets, with the
handshake the LC ROM itself uses. `CLAUDE.md` has the details.

## Limitations

- New. On the Tab5: System 7.5.5 starts and runs at full speed, in 256
  colours, and the touch screen works; sound, Bluetooth and the Tab5
  Keyboard haven't been tried by hand yet.
- No FPU, SCSI devices, serial ports, AppleTalk, printer or PDS cards.
- Disk images are presented through the floppy driver: copy-protected
  originals that talk to the floppy hardware directly don't work.

## Credits

- [Musashi](https://github.com/kstenerud/Musashi) by Karl Stenerud (MIT),
  via Matt Evans' [fork](https://github.com/evansm7/Musashi) for embedded use
- [MAME](https://www.mamedev.org)'s Mac LC drivers by R. Belmont (V8,
  pseudo-VIA, Ariel, ASC; hardware tests by Doug Brown), the reference for
  how the LC's chips behave
- Linux's `via-cuda.c` and QEMU's Cuda, for the Egret packets
- [Mini vMac](https://www.gryphel.com/c/minivmac/) and
  [umac](https://github.com/evansm7/umac), via esp32mac
- [LVGL](https://lvgl.io), [ESP-IDF](https://github.com/espressif/esp-idf) and NimBLE

## Licence

[MIT](LICENSE). `main/web/macfiles.js` (the archive converter, from
esp32mac) is under the GNU LGPL 2.1 or later.

Apple, Macintosh and Mac are trademarks of Apple Inc. This project contains
no Apple code; you need your own ROM image and system software.
