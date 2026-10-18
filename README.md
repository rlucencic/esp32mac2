<h1 align="center">esp32mac2</h1>

<p align="center">
  <b>A colour Macintosh LC or Macintosh II on an M5Stack Tab5.</b><br>
  68020, 640×480 in 256 colours, up to 10 MB, System 7.5.5, disk images of any size, Bluetooth keyboard and mouse.
</p>

<p align="center">
  <img alt="ESP32-P4" src="https://img.shields.io/badge/ESP32--P4-M5Stack%20Tab5-e7352c">
  <img alt="ESP-IDF" src="https://img.shields.io/badge/ESP--IDF-v5.5-blue">
  <img alt="LVGL" src="https://img.shields.io/badge/LVGL-9.4-5e48e8">
  <img alt="68020" src="https://img.shields.io/badge/CPU-Musashi%2068020-555">
  <img alt="MIT licence" src="https://img.shields.io/badge/licence-MIT-green">
</p>

esp32mac2 turns an M5Stack Tab5 into a colour Mac, chosen on its setup
screen: the **Macintosh LC**, Apple's first low-cost colour Mac (the V8
chip's built-in video, Egret, the Apple Sound Chip), or the **Macintosh
II**, the first colour Mac (a 68881 floating-point unit, two VIAs, NuBus
with Apple's Display Card 8•24, the original Apple Sound Chip). Both show
a 13-inch (640×480) or 12-inch (512×384) monitor. A Bluetooth keyboard and
mouse (or your finger on the screen) work the Mac, and disk images on the
microSD card go into three drives. A setup screen on the touch panel, and a
web page on your network, handle everything else.

It's the colour sibling of [esp32mac](https://github.com/rlucencic/esp32mac)
(the compact black-and-white Macs: Plus, SE, Classic, on the same two
boards), and lives next to
[esp32ste](https://github.com/rlucencic/esp32ste) (Atari STE) and the other
machines on the shared runtime [esp32emu](https://github.com/rlucencic/esp32emu).

## Features

**The machines**
- 68020 at 15.67 MHz ([Musashi](https://github.com/kstenerud/Musashi)
  core), about a real Mac's speed, full speed on the Tab5
- **Macintosh LC**: 2, 4, 6 or 10 MB; built-in video at 640×480 in black and
  white up to 256 colours, or 512×384 up to thousands; Egret for the ADB
  keyboard and mouse, the clock and parameter RAM; no FPU (as a stock LC)
- **Macintosh II**: 2, 4 or 8 MB; a 68881 FPU; the Macintosh Display Card
  8•24 in NuBus slot $9, driven by its own ROM (black and white up to 256
  colours); ADB and the clock through its VIAs
- 256 colours by default; the Monitors control panel changes it
- The startup chime, system beeps and sound through the Apple Sound Chip
- The clock is set from the internet; parameter RAM is kept across power-offs
- System 7.5.5, tested on the Tab5 on both machines (Mac OS 7.6.1 starts on
  the LC too)

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
2. Your own ROMs (copyrighted, **not included**; see
   [roms/README.md](roms/README.md)): the Macintosh LC's as `roms/lc.rom`,
   and for the Mac II its ROM as `roms/ii.rom` and the Display Card 8•24's
   as `roms/mdc824.rom`.
3. Build and flash:

   ```sh
   idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=build-tab5/sdkconfig build
   idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=build-tab5/sdkconfig -p /dev/cu.usbmodem… flash
   ```

   Instead of flashing the ROMs, you can copy them into a `roms` folder on
   the SD card.
4. On the setup screen: WiFi, then drop a system disk image on the web page
   and insert it into Drive 1, choose the model, memory and monitor,
   **Start**.

## How it works

- **`components/mac2`**: the LC and the II, platform independent (one file
  for each machine's map and wiring, the rest shared), with a desktop build
  (`components/mac2/host`) for testing: scripted keyboard and mouse,
  screenshots, sound recording, CPU and I/O traces.
- **`main`**: the Mac on the board: memory, ROMs, settings, keyboard map,
  touch, drives and status.
- **[esp32emu](https://github.com/rlucencic/esp32emu)**: the board runtime and
  the CPU core, built as a 68020 here (`CONFIG_M68K_68020`), with its FPU on
  for the Mac II.

The ROMs are patched in memory, every patch checked against the expected
bytes: no ROM checksum test, no FPU on the LC (its probe needs a bus error
the CPU core doesn't make), and the `.Sony` floppy driver replaced by
esp32mac's paravirtual one. Egret is emulated at the level of its packets,
with the handshake the LC ROM itself uses. On the Mac II the Display Card
8•24's own ROM finds the card, sets up its video and drives it, as on the
real thing. `CLAUDE.md` has the details.

## Limitations

- New. On the Tab5, both machines start System 7.5.5 and run it at full
  speed in 256 colours, and the touch screen works. Sound, Bluetooth and
  the Tab5 Keyboard haven't been tried by hand yet.
- No SCSI devices, serial ports, AppleTalk, printer, PDS cards or other
  NuBus cards; no 68851 PMMU on the II, so no virtual memory there. The LC
  has no FPU.
- Millions of colours on the Mac II's card needs 32-bit addressing, which
  the original II only gets with an extension (MODE32); not tried yet.
- Disk images are presented through the floppy driver: copy-protected
  originals that talk to the floppy hardware directly don't work.

## Credits

- [Musashi](https://github.com/kstenerud/Musashi) by Karl Stenerud (MIT),
  via Matt Evans' [fork](https://github.com/evansm7/Musashi) for embedded use
- [MAME](https://www.mamedev.org)'s Mac LC and Mac II drivers by R. Belmont
  (V8, pseudo-VIA, Ariel, ASC; hardware tests by Doug Brown) and the
  Display Card 4•8/8•24 by R. Belmont and Vas Crabb, the reference for how
  these machines' chips behave
- Linux's `via-cuda.c` and QEMU's Cuda, for the Egret packets
- [Mini vMac](https://www.gryphel.com/c/minivmac/) and
  [umac](https://github.com/evansm7/umac), via esp32mac
- [LVGL](https://lvgl.io), [ESP-IDF](https://github.com/espressif/esp-idf) and NimBLE

## Licence

[MIT](LICENSE). `main/web/macfiles.js` (the archive converter, from
esp32mac) is under the GNU LGPL 2.1 or later.

Apple, Macintosh and Mac are trademarks of Apple Inc. This project contains
no Apple code; you need your own ROM images and system software.
