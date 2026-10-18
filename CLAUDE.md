# esp32mac2: Macintosh LC on the M5Stack Tab5

A colour Mac: the Macintosh LC (68020 at 15.67 MHz, the V8 system ASIC, 640x480
in up to 256 colours or 512x384 in thousands, Egret, ADB, ASC sound, 2 to
10 MB). Bluetooth keyboard and mouse, the touch screen as a mouse, disk
images of any size in three drives, an LVGL setup screen and a web page.
ESP-IDF v5.5.4, C. The Mac II profile comes next (see Open items).

The sibling of `../esp32mac` (Plus, SE, Classic: 68000, black and white),
which runs on both boards (the MaTouch and the Tab5) and stays as it is:
this project borrows its disk driver, input and web converter, not its
machine core. The two flash different ROM partitions (`rom_plus`/`rom_se`/
`rom_classic` there, `rom_lc` here): switching a board between them needs
a full `flash`. The board side (tasks, LCD, touch, SD, WiFi, BLE, setup
screen, web page, console) and the CPU core are the shared library in
`../esp32emu`; its CLAUDE.md has the runtime and the board-level lessons.
Changes there need all its projects built, for both boards (this one for
the Tab5).

## Build, flash, debug

The Tab5 only (`/dev/cu.usbmodem3101`): `CMakeLists.txt` stops a build for
any other target. A 68020 at 15.67 MHz is an estimated five times the CPU
work of esp32mac's Plus, which already keeps the MaTouch's S3 busy, and only 2 MB of
Mac RAM would fit in its 8 MB of PSRAM; `../esp32mac` is the Mac for the
MaTouch. `sdkconfig.defaults` is still the siblings' base (written for the
S3); the Tab5's settings from `../esp32emu` go on top.

```sh
bash -c '. $IDF_PATH/export.sh >/dev/null 2>&1; \
  export IDF_PYTHON_ENV_PATH=/Users/robertlucencic/.espressif/tools/python/v5.5.4/venv; \
  export PATH=$IDF_PYTHON_ENV_PATH/bin:$PATH; \
  idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=$PWD/build-tab5/sdkconfig build && \
  idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=$PWD/build-tab5/sdkconfig -p /dev/cu.usbmodem3101 flash'
```

- `flash` also writes `roms/lc.rom` (not in git) to partition `rom_lc`;
  `app-flash` only the app. The boards are shared with the other projects:
  after another one, a full `flash` (partition table and ROM). NVS (WiFi,
  BLE pairings) is shared; this project's settings are namespace `mac2`,
  its drives `mac2disks`.
- `CONFIG_M68K_68020=y` (`sdkconfig.defaults`) makes `../esp32emu`'s
  Musashi a 68020; the ST and the compact Macs keep the 68000.
- `../esp32emu/tools/emu_console.py PORT log N | screenshot F.png | lcd F.png | send "cmd" ... | info | reset`
  with the IDF venv's Python (`-I`). `curl http://macintosh2.local/api/status`.
- Flashing restarts the board and ends the user's Mac session: say so first.

Console commands: the shared ones (`emu/console.h`) and the Mac's
(`main/main.c`): `k <hex>` tap a Mac key code, `K <hex> 0|1`, `m dx dy btn`
relative mouse, `P x y btn` pointer at a screen position (`p` is the
runtime's profiler), `R <ram MB>
[monitor 0|1]` restart with 2/4/6/10 MB and the 13" or 12" monitor, `n`
interrupt switch.

## Layout

| Path | What |
|---|---|
| `main/main.c` | The Mac as an `emu_machine_t`: memory, start, key map, touch, drives, status, console |
| `main/rom_store.c` | ROM from its flash partition, else a matching file in the SD card's `roms/` |
| `main/settings.c` | NVS `mac2`: `ram_mb`, `monitor`, `pram` (256 bytes) |
| `main/web/macfiles.js` | From `../esp32mac` (LGPL): Mac archives -> HFS disk images in the browser |
| `components/mac2/` | The Mac, platform independent (`include/mac.h`) |
| `components/mac2/host/` | Host build for testing on the Mac (see Testing) |
| `roms/` | `lc.rom` (copyrighted, not distributed) |
| `partitions.csv` | nvs, phy, factory 8 MB, rom_lc 512K (type 0x40), storage |

## The core (`components/mac2`)

Hardware behaviour from MAME's `maclc.cpp`, `v8.cpp`, `pseudovia.cpp`,
`ariel.cpp`, `asc.cpp` (BSD-3, R. Belmont; read, not copied), and the LC
ROM's own code for Egret.

- **Memory** (`mem.c`): RAM, ROM and VRAM with each 16-bit word in host
  order (byte n at index n^1). The V8 decodes A23-A0; in 32-bit mode
  (pseudo-VIA PB3 = 1) A31 set reaches nothing, in 24-bit mode it's
  dropped. RAM is placed by the pseudo-VIA's config register as MAME does
  (SIMMs at 0, the board's 2 MB after the configured SIMM size, and always
  at $800000), in 1 MB pages; the host buffer is SIMMs first, then the
  board's RAM, so the usual layout is `ram[a]` (`ram_window`, the fast
  path). ROM at $A00000 (and at 0 while the overlay is on; the first read
  at $A0xxxx ends it), VRAM $F40000-$FBFFFF (fast path too), I/O in 8 KB
  blocks at $F00000. Unmapped reads give 0, no bus errors (as MAME). The
  68020's misaligned words and longs go byte-wise.
- `mac.c`: time in CPU cycles (15.6672 MHz), "lines" of 704 cycles (one
  sound sample, 22.255 kHz), 370 a frame (60.15 Hz: VIA1 CA1). Each of
  Musashi's 68020 cycles counts as two (`CPU_SCALE`, see the lessons). Vertical
  blanking (pseudo-VIA slot IRQ bit 6) from line 338. Interrupts: VIA1 1,
  pseudo-VIA 2, SCC 4, NMI 7, the highest wins. VIA1 PA reads $D5 (the
  LC's ID bits).
- `via.c`: VIA1, from `../esp32mac` (CPU / 20 timers, the SR for Egret).
- `pvia.c`: pseudo-VIA $F26000 ($00 PB, $01 RAM config, $02 slot IRQs,
  $03 IFR, $10 video depth and monitor sense, $12/$13 enables).
- `egret.c`: Egret at the packet level (see the lessons for the handshake).
  Pseudo-commands: 01 autopoll, 02/08 read/write Egret memory (PRAM is
  at $0100-$01FF), 03/09 clock (host local time + offset), 07/0C PRAM,
  0A power off, 11 restart, 14/16 autopoll rate, 19/1A device list; others
  ($0E DFAC, $1B, $1C, ...) answer OK. Replies longer than wanted are fine:
  the ROM drops system session when it has its bytes.
- `adb.c`: keyboard at 2 (handler 2), mouse at 3, talk/listen/flush/reset,
  address changes; autopoll once a frame from `egret_frame()`.
- `video.c`: Ariel colour table ($F24000: address, data R-G-B, control) and
  the picture (1024-byte lines up to 8 bits, low bits filled with 1s as the
  V8 does; 16 bits direct). The signature changes on VRAM, table or mode
  writes.
- `asc.c`: the V8's ASC: FIFO A (1 KB), version $E8, status $804, level
  interrupt below half full; samples collected per frame, ring for the
  audio task.
- `chips.c`: SCC, SWIM (IWM mode), SCSI 5380 stubs from `../esp32mac`.
- `sony.c`: `../esp32mac`'s paravirtual disk driver over the ROM's `.Sony`
  (DRVR at $6C3E0), at $A0xxxx. Also takes uncompressed `.dmg` (UDIF:
  the data fork is the disk, found from the "koly" trailer); `.dmg` and CD
  images are locked. Empty drives answering "offline" aren't logged (the
  ROM polls them).
- `rom.c`: the LC ROM (350EACF0) and its patches (see the lessons).

## Lessons (each one cost a debugging session)

- **Musashi as a 68020** (`../esp32emu/components/musashi/m68kconf.h`):
  `CPU_TYPE_IS_010_LESS()` and friends only work with the 010 and EC020
  switches on too. With just the 020 on, the 68000's address errors stayed
  in and the ROM's misaligned-access test (`move.l d0,(a0,d1.w)`, $A46C78)
  failed. The fork's `assert(cpu_type == M68K_FIXED_CPU_TYPE)` compared the
  public type with the internal bit mask (true by chance for the 68000):
  removed.
- **ROM checksum** ($A46AFC `eor.l d4,d1` -> `eor.l d1,d1`): the patched
  copy fails it, and the LC then plays the death chimes and runs its serial
  Test Manager ("*APPLE*", "TM Version 2.0" at $A49Fxx, polling the SCC).
  D7 low word $0001 = ROM test.
- **No FPU**: the ROM looks for one with `moves.w $22000,d1` with SFC = 7
  (the coprocessor's CPU space). Musashi ignores function codes, so it read
  RAM, found "an FPU" (HWCfgFlags $B22 bit 12), and System 7 died with
  error 10 at its first `FNOP`. Patched to always clear the flag ($A03AB4).
- **Egret handshake, as the LC ROM does it** ($A14Cxx at boot, $A4A1xx
  later): command: system session (PB5) up first, then the SR to shift-out,
  the byte written, VIA full (PB4) up; Egret takes it (SR interrupt); VIA
  full down; per further byte the same; then SR in and system session down.
  Reply: transceiver session (PB3) low and an attention byte; the Mac
  raises system session; Egret gives a byte; the Mac raises VIA full, reads
  the SR, waits, checks PB3 (must still be low before the last byte, high
  after it), and drops VIA full to ask for the next one. Giving the next
  byte when VIA full rose instead ran one byte ahead and negated PB3 too
  early: the ROM retried its PRAM write forever.
- **PRAM is in Egret's memory**: the ROM reads and writes it with commands
  02/08 at 6805 addresses $0100-$01FF ("NuMc" at $010C means valid);
  without 02 it found no signature and reset PRAM at every start.
- **Mac OS 7.6 needs 32-bit addressing**: on the first start it sets PRAM
  $8A and restarts (a second boot chime is normal).
- **The ROM's ADB address resolution** moves each device to 15 and back
  about 50 times (~1.5 s) before turning autopoll on; it does finish.
- **The CPU's speed** (`CPU_SCALE` in `mac.c`): with Musashi's own 68020
  timing (the cache case, a 32-bit bus) the idle Finder ran 2.5 million
  instructions a second, more than a real LC (16-bit path to RAM, ROM and
  VRAM) and more than the Tab5 sustains (72% speed, core 1 at 100%).
  Counting each cycle twice halves it: 100% at ~70% load. No Speedometer at
  hand to calibrate against a real LC yet.
- **The console's `p` is the runtime's profiler**: the Mac's pointer
  command is `P` (esp32mac's `p x y btn` is shadowed the same way).
- **Video mode in slot PRAM**: the built-in video's bytes at PRAM $56-$5A
  (board $0029, mode $80 = 1 bit ... $83 = 8 bits, $5A = monitor sense 6).
  The defaults ask for 256 colours on the 13" monitor.

## Testing

- **Host build**: `make -C components/mac2/host`, then
  `build/mac_host roms/lc.rom [frames] [out.ppm] [ram_mb]`. Environment:
  `MAC_DISK1..3`, `MAC_PRAM=<file>`, `MAC_SCRIPT=<file>` (`<frame> key|type|
  mouse|pos|shot|insert|eject|peek|dis|free|units ...`), `MAC_STATUS=<n>`,
  `MAC_TRACE=<pc>[:<n>]`, `MAC_DIS=<addr>[:<n>]`, `MAC_PCHIST=<frames>`
  (hottest instructions), `MAC_REGS`, `MAC_IO=1|<hex block mask>` (I/O
  accesses), `MAC_EGRET` (Egret commands and handshake edges),
  `MAC_MONITOR=12`, `MAC_WAV=<file>`, `MAC_DEBUG=<port>` (the debugger).
  Runs ~40x real time.
- System 7.5.5 is the system for the LC (and the Mac II); 7.6.1 is for a
  later project, though it starts here too.
- Checked on the host (2026-10-18): System 7.5.5 and Mac OS 7.6.1 (an
  uncompressed `.dmg`) boot to the Finder in 256 colours, 4 MB; the Apple
  menu opens with a touch-style click, ADB mouse motion moves the pointer
  (with acceleration), the startup chime plays (~1.2 s).
- Measured on the Tab5 (2026-10-18): System 7.5.5 from the SD card, 4 MB,
  256 colours, idle desktop: 100% speed, core 1 ~70% (72% speed and core 1
  at 100% before `CPU_SCALE`); a double tap (`t` touches) opens a disk,
  `P x y 0` moves the pointer, Return (`k 24`, the ADB keyboard through
  Egret's autopoll) closes a dialog.
  Not checked by ear or hand yet: sound, Bluetooth, the Tab5 Keyboard.

## Open items

- The Mac II profile: NuBus video card (the 8•24's declaration ROM),
  VIA2, the original ASC, the 68881 (Musashi's FPU runs only for 030+ now),
  the Mac II's HMMU address translation.
- Sound volume (DFAC via Egret $0E) is ignored; FIFO B ignored (as on the V8).
- No SCSI devices, serial ports, floppy hardware (disk images instead), no
  PDS cards.

## Working with the user

- Expects changes built, flashed and checked on the board, then a short
  report of what was verified and what wasn't.
- Keep their private data (WiFi name, IP, SD card contents) out of
  anything meant for publishing. Test disks are copies in the scratchpad,
  never their originals.
