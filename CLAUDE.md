# esp32mac2: Macintosh LC and Macintosh II on the M5Stack Tab5

Colour Macs with a 68020 at 15.67 MHz, chosen on the setup screen:

- **Macintosh LC**: the V8 system ASIC, built-in video at 640x480 in up to
  256 colours or 512x384 in thousands, Egret (ADB, clock, PRAM), the V8's
  ASC, 2 to 10 MB.
- **Macintosh II**: a 68881, two VIAs (ADB through VIA1's shift register,
  the RTC), the original ASC, NuBus with the Macintosh Display Card 8•24 in
  slot $9 (its own ROM drives it), the 24-bit address translation, 2 to 8 MB.

Both run System 7.5.5. Bluetooth keyboard and mouse, the touch screen as a
touchpad, disk images of any size in three drives, an LVGL setup screen and a
web page. ESP-IDF v5.5.4, C.

The sibling of `../esp32mac` (Plus, SE, Classic: 68000, black and white),
which runs on both boards (the MaTouch and the Tab5) and stays as it is:
this project borrows its disk driver, input, RTC, ADB transceiver and web
converter, not its machine core. The two flash different ROM partitions
(`rom_plus`/`rom_se`/`rom_classic` there, `rom_lc`/`rom_ii`/`rom_card`
here): switching a board between them needs a full `flash`. The board side
(tasks, LCD, touch, SD, WiFi, BLE, setup screen, web page, console) and the
CPU core are the shared library in `../esp32emu`; its CLAUDE.md has the
runtime and the board-level lessons. Changes there need all its projects
built, for both boards (this one for the Tab5).

## Build, flash, debug

The Tab5 only (`/dev/cu.usbmodem3101`): `CMakeLists.txt` stops a build for
any other target. A 68020 at 15.67 MHz is an estimated five times the CPU
work of esp32mac's Plus, which already keeps the MaTouch's S3 busy, and only
2 MB of Mac RAM would fit in its 8 MB of PSRAM; `../esp32mac` is the Mac
for the MaTouch. `sdkconfig.defaults` is still the siblings' base (written
for the S3); the Tab5's settings from `../esp32emu` go on top.

```sh
bash -c '. $IDF_PATH/export.sh >/dev/null 2>&1; \
  export IDF_PYTHON_ENV_PATH=/Users/robertlucencic/.espressif/tools/python/v5.5.4/venv; \
  export PATH=$IDF_PYTHON_ENV_PATH/bin:$PATH; \
  idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=$PWD/build-tab5/sdkconfig build && \
  idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=$PWD/build-tab5/sdkconfig -p /dev/cu.usbmodem3101 flash'
```

- `flash` also writes `roms/lc.rom`, `roms/ii.rom` and `roms/mdc824.rom`
  (not in git; see `roms/README.md`) to partitions `rom_lc`, `rom_ii`,
  `rom_card`; `app-flash` only the app. The boards are shared with the
  other projects: after another one, a full `flash` (partition table and
  ROMs). NVS (WiFi, BLE pairings) is shared; this project's settings are
  namespace `mac2` (`model`, `ram_mb`, `monitor`, `pram` for the LC,
  `pram_ii`), its drives `mac2disks`.
- `CONFIG_M68K_68020=y` (`sdkconfig.defaults`) makes `../esp32emu`'s
  Musashi a 68020; the ST and the compact Macs keep the 68000.
  `m68k_set_fpu()` turns its FPU on for the Mac II.
- `../esp32emu/tools/emu_console.py PORT log N | screenshot F.png | lcd F.png | send "cmd" ... | info | reset`
  with the IDF venv's Python (`-I`). `curl http://macintosh2.local/api/status`.
- Flashing restarts the board and ends the user's Mac session: say so first.

Console commands: the shared ones (`emu/console.h`) and the Mac's
(`main/main.c`): `k <hex>` tap a Mac key code, `K <hex> 0|1`, `m dx dy btn`
relative mouse, `P x y btn` pointer at a screen position (`p` is the
runtime's profiler), `R <model 0|1> <ram MB> [monitor 0|1]` restart as the
LC (2/4/6/10 MB) or the II (2/4/8 MB) with the 13" or 12" monitor, `n`
interrupt switch. Touches on the Tab5: the picture is 960x720 at x=160
(a Mac pixel is 1.5 panel pixels); setup screen Start `t 1065 637`, Model
`t 440|795 251`. The Memory and Monitor rows follow the model
(`choice_names()`); the Monitor buttons' second line is the colours each
monitor gets: LC up to 256 at 640x480, 32,768 at 512x384; II 256 at both.

## Layout

| Path | What |
|---|---|
| `main/main.c` | The Mac as an `emu_machine_t`: memory, start, key map, mouse and touchpad (a count a pixel), drives, status, console |
| `main/rom_store.c` | ROMs from their flash partitions, else matching files in the SD card's `roms/` |
| `main/settings.c` | NVS `mac2` |
| `main/web/macfiles.js` | From `../esp32mac` (LGPL): Mac archives -> HFS disk images in the browser |
| `components/mac2/` | The Macs, platform independent (`include/mac.h`) |
| `components/mac2/host/` | Host build for testing on the Mac (see Testing) |
| `roms/` | `lc.rom`, `ii.rom`, `mdc824.rom` (copyrighted, not distributed) |
| `partitions.csv` | nvs, phy, factory 8 MB, rom_lc 512K, rom_ii 256K, rom_card 32K (type 0x40), storage |

## The core (`components/mac2`)

Hardware behaviour from MAME's `maclc.cpp`, `v8.cpp`, `pseudovia.cpp`,
`ariel.cpp`, `macii.cpp`, `nubus_48gc.cpp`, `asc.cpp` (BSD-3, R. Belmont,
Vas Crabb; read, not copied), and the ROMs' own code.

Shared:
- `mac.c`: time in CPU cycles (15.6672 MHz), "lines" of 704 cycles (one
  sound sample, 22.255 kHz), 370 a frame (60.15 Hz). Each of Musashi's
  68020 cycles counts as two (`CPU_SCALE`, see the lessons). Vertical
  blanking from line 338. Interrupts: VIA1 1, the LC's pseudo-VIA or the
  II's VIA2 2, SCC 4, NMI 7, the highest wins. The machine behind
  `model_ops_t` (`mac_internal.h`): slow paths, frame hooks, the picture.
- `mem.c`: the Musashi callbacks' fast paths, the same for both: RAM below
  `ram_window` is `ram[a]`, ROM at `rom_base` + `rom_window`, VRAM at
  `vram_base` + `vram_window` (the machines set them; a window of 0 sends
  that area to the slow path); RAM, ROM and VRAM with each 16-bit word in
  host order (byte n at index n^1). The 68020's misaligned words and longs
  go byte-wise; aligned longs reach the machine whole (the 8•24's
  registers).
- `via.c`: VIA instances (from `../esp32mac`): CPU / 20 timers, T1's PB7
  output, CA1/CA2/CB1/CB2 inputs on the PCR's edge, the SR for a device;
  the machine wires the ports (`via_wiring_t`, port B writes always
  reported).
- `adb.c`: keyboard at 2 (handler 2), mouse at 3, talk/listen/flush/reset,
  address changes, autopoll. `asc.c`: both ASCs (below); samples collected
  per frame, a DC blocker (the Macs' AC-coupled output), a ring for the
  audio task. `chips.c`: SCC, SWIM/IWM, SCSI 5380 stubs from `../esp32mac`.
- `sony.c`: `../esp32mac`'s paravirtual disk driver over the ROM's `.Sony`
  (LC $6C3E0, II $2D72C), at the ROM's 24-bit address. Also takes
  uncompressed `.dmg` (UDIF, found from the "koly" trailer: the disk is
  the data fork's chunks as the XML plist's "blkx" tables list them, raw
  or zeros, so a read-only UDRO that leaves out the zeros works; without
  tables the data fork in one piece; compressed ones are refused);
  `.dmg` and CD images are locked. Empty drives answering
  "offline" aren't logged (the ROM polls them).
- `rom.c`: the known ROMs (LC 350EACF0, II 9779D2C4) and their patches.

The LC (`lc.c`, `pvia.c`, `egret.c`, `video.c`):
- The V8 decodes A23-A0; in 32-bit mode (pseudo-VIA PB3 = 1) A31 set
  reaches nothing. RAM is placed by the pseudo-VIA's config register as
  MAME does (SIMMs at 0, the board's 2 MB after the configured SIMM size,
  and always at $800000), in 1 MB pages; the host buffer is SIMMs first,
  then the board's RAM, so the usual layout is `ram[a]`. ROM at $A00000
  (and at 0 while the overlay is on; the first read at $A0xxxx ends it),
  VRAM $F40000-$FBFFFF, I/O in 8 KB blocks at $F00000. Unmapped reads give
  0, no bus errors (as MAME). VIA1 PA reads $D5 (the LC's ID); the 60.15 Hz
  tick sets VIA1 CA1.
- `pvia.c`: pseudo-VIA $F26000 ($00 PB, $01 RAM config, $02 slot IRQs,
  $03 IFR, $10 video depth and monitor sense, $12/$13 enables).
- `egret.c`: Egret at the packet level (see the lessons for the handshake).
  Pseudo-commands: 01 autopoll, 02/08 read/write Egret memory (PRAM is
  at $0100-$01FF), 03/09 clock (host local time + offset), 07/0C PRAM,
  0A power off, 11 restart, 14/16 autopoll rate, 19/1A device list, 1B
  one-second ticks (type 3 packets: the ROM hands them to the one-second
  interrupt handler, Lvl1DT at $192); others ($0E DFAC, $1C, ...) answer
  OK. Replies longer than wanted are fine: the ROM drops system session
  when it has its bytes.
- `video.c`: Ariel colour table ($F24000: address, data R-G-B, control) and
  the picture (1024-byte lines up to 8 bits, low bits filled with 1s as the
  V8 does; 16 bits direct).
- The V8's ASC: FIFO A only (1 KB), version $E8, a level interrupt below
  half full (to the pseudo-VIA).

The Mac II (`ii.c`, `jmfb.c`, `adb_via.c`, `rtc.c`):
- Physical map: RAM at 0 (bank B where VIA2 PA6-PA7 put it: MAME's rules,
  which the original II's RAM sizing needs: all RAM at 0, bank A repeated
  after it but not with 4 MB, bank B at its place when beyond; only 1 MB
  when the place is past the end), ROM $40000000 (repeated), I/O
  $50000000 (VIA1, VIA2 +$2000, SCC +$4000, SCSI +$10000, ASC +$14000,
  IWM +$16000; every MB the same), NuBus slot s at $Fs000000. VIA2 PB3 low
  = 24-bit mode: the HMMU maps $800000 to the ROM, $s00000 to slot s's
  first MB, $F00000 to the I/O. The overlay (ROM at 0) is VIA1 PA4. Fast
  path windows switch with the mode.
- VIA1: PA reads $81 (the II's ID), PB0-PB2 the RTC (`rtc.c`, from
  `../esp32mac`), PB3-PB5 the ADB transceiver (`adb_via.c`, esp32mac's
  SE/Classic state machine over `adb.c`), CA1 the 60.15 Hz tick (from VIA2
  PB7, which the ROM runs off VIA2's T1), CA2 the one-second interrupt.
- VIA2: PA0-PA5 slot interrupts (active low, slot $9 = bit 0) and CA1 a
  fresh falling edge for each (as MAME), CB1 the ASC's interrupt, PB2 low
  power off.
- `jmfb.c`: the Macintosh Display Card 8•24: 1 MB VRAM at the slot base
  (packed 24-bit RGB when control bit 2: then through the slow path), JMFB
  registers at +$200000 (control with the monitor sense lines, base,
  stride), CRTC +$200100 (timing in half lines, beam status at $1C0, VBL
  interrupt off $13C bit 1, clear $148), RAMDAC +$200200 (colour address,
  data R-G-B, mode), clock synthesiser +$200300; the declaration ROM on
  byte lane 3 at the top of the slot ($FE0000-$FFFFFF). Registers take the
  value from whatever lanes the CPU drives. The monitor: 13" Hi-Res (sense
  6, 640x480) or 12" RGB (2, 512x384). VBL slot interrupt once a frame.
- The original ASC: FIFOs A and B (mono or stereo), the four-voice
  wavetable mode (the II's startup chime: 512-byte tables at $000, $200,
  $400, $600 written directly, 9.15 phase/increment at $810-$82F),
  version $00, the interrupt as a FIFO falls to half full, cleared by
  reading the status.
- The 68881: Musashi's FPU (the 68040's, from MAME) switched on for the
  020, with the 68881's transcendental functions added through libm (see
  `../esp32emu/components/musashi/README.md`).

## Lessons (each one cost a debugging session)

- **Musashi as a 68020** (`../esp32emu/components/musashi/m68kconf.h`):
  `CPU_TYPE_IS_010_LESS()` and friends only work with the 010 and EC020
  switches on too. With just the 020 on, the 68000's address errors stayed
  in and the LC ROM's misaligned-access test (`move.l d0,(a0,d1.w)`,
  $A46C78) failed. The fork's `assert(cpu_type == M68K_FIXED_CPU_TYPE)`
  compared the public type with the internal bit mask (true by chance for
  the 68000): removed.
- **ROM checksums** (ROM offsets: LC $46AFC, II $370A; `eor.l d4,d1` ->
  `eor.l d1,d1`): the patched copy fails them, and the LC then plays the death
  chimes and runs its serial Test Manager ("*APPLE*", "TM Version 2.0" at
  $A49Fxx, polling the SCC). D7 low word $0001 = ROM test.
- **The LC has no FPU**: its ROM looks for one with `moves.w $22000,d1`
  with SFC = 7 (the coprocessor's CPU space). Musashi ignores function
  codes, so it read RAM, found "an FPU" (HWCfgFlags $B22 bit 12), and
  System 7 died with error 10 at its first `FNOP`. Patched to always clear
  the flag ($A03AB4). The II's ROM assumes its 68881 (no probe); without
  the FPU System 7 dies the same way.
- **Musashi's FPU ran only for 68030 and up, and called `exit()`** on what
  it didn't do (every 68881 transcendental): `m68k_set_fpu()`, libm for
  the transcendentals, a log line instead of the exit (a board restart).
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
- **Egret: a command crossing a packet** (found in `../esp32mac3`). The
  ROM checks PB3 just before raising system session; Egret may raise
  transceiver session in between (a tick packet). The ROM then sends its
  first byte, sees PB3 asserted, drops system session and reads the
  packet, taking its own byte's SR interrupt as the attention byte.
  egret.c took the Mac's system session for "reading the packet" and gave
  bytes into a shift-out SR: the LC hung in the ROM's send loop ($A14850,
  Ticks 126) on the second boot of Mac OS 7.6.1 from fresh PRAM, at every
  RAM size. Now Egret takes that byte and counts it as the attention.
- **PRAM is in Egret's memory**: the ROM reads and writes it with commands
  02/08 at 6805 addresses $0100-$01FF ("NuMc" at $010C means valid);
  without 02 it found no signature and reset PRAM at every start.
- **The LC's clock stood still without Egret's tick packets**: the OS's
  one-second interrupt (the Time global, the menu bar clock) comes from
  them on Egret machines (enabled with $1B).
- **Mac OS 7.6 needs 32-bit addressing**: on the first start it sets PRAM
  $8A and restarts (a second boot chime is normal).
- **The ROM's ADB address resolution** moves each device to 15 and back
  about 50 times (~1.5 s) before turning autopoll on; it does finish.
- **The CPU's speed** (`CPU_SCALE` in `mac.c`): with Musashi's own 68020
  timing (the cache case, a 32-bit bus) the idle Finder ran 2.5 million
  instructions a second, more than a real LC (16-bit path to RAM, ROM and
  VRAM) and more than the Tab5 sustains (72% speed, core 1 at 100%).
  Counting each cycle twice halves it: 100% at ~65-70% load, both models.
  No Speedometer at hand to calibrate against a real LC or II yet.
- **The II's chime rode on a DC offset**: the three idle wavetable voices
  each add their table's first sample, and the chip switched off holds the
  last level. Real Macs' audio output is AC coupled: a DC blocker on the
  output (a step at the switch-off was a click until the level was held).
- **The console's `p` is the runtime's profiler**: the Mac's pointer
  command is `P` (esp32mac's `p x y btn` is shadowed the same way).
- **Video mode in slot PRAM**: the LC's built-in video at PRAM $56-$5A
  (board $0029, mode $80 = 1 bit ... $83 = 8 bits, $5A = monitor sense 6),
  the II's 8•24 in slot $9 at $46-$4C (board $0027, mode at $48, $A6 $A6,
  and $FF at $4C, without which the ROM reset the mode). The defaults ask
  for 256 colours. Millions of colours ($84) needs 32-bit addressing (the
  packed view is over 1 MB): the ROM refused it in 24-bit mode.
- **The ROM keeps that PRAM only while it matches the monitor**: with the
  12" monitor and the 13" values (LC sense $5A = 6, II $49/$4A = $A6), it
  reset the mode to $80, and the 12" monitor came up in black and white.
  `mac_pram_monitor()` (`egret.c`, from every `mac_init()`) writes the
  monitor's own values when they don't match: LC $A2 and sense 2, II $A2
  $A2. A depth the ROM can't give is reset to $80 the same way (how the
  setup screen's colour counts were checked: $84 = 16 bits kept on the LC
  at 512x384, reset at 640x480 and on the II at both).

## Testing

- **Host build**: `make -C components/mac2/host`, then
  `build/mac_host roms/lc.rom|roms/ii.rom [frames] [out.ppm] [ram_mb]`
  (`MAC_CARD=roms/mdc824.rom` for the II). Environment: `MAC_DISK1..3`,
  `MAC_PRAM=<file>`, `MAC_SCRIPT=<file>` (`<frame> key|type|mouse|pos|shot|
  insert|eject|peek|dis|free|units ...`), `MAC_STATUS=<n>`,
  `MAC_TRACE=<pc>[:<n>]`, `MAC_DIS=<addr>[:<n>]` (24-bit addresses),
  `MAC_PCHIST=[<from>:]<to>` (hottest instructions in those frames, and
  their total), `MAC_REGS`, `MAC_IO=1|<hex block mask>` (I/O accesses),
  `MAC_EGRET` (Egret commands and handshake edges), `MAC_MONITOR=12`,
  `MAC_WAV=<file>`, `MAC_DEBUG=<port>` (the debugger). Runs ~40-80x real
  time.
- System 7.5.5 is the system for the LC and the II; 7.6.1 is for a later
  project, though it starts on the LC too.
- Checked on the host (2026-10-18), both models, 4 MB: System 7.5.5 boots
  to the Finder in 256 colours (LC also Mac OS 7.6.1 from an uncompressed
  `.dmg`); the Apple menu opens with a touch-style click, ADB mouse motion
  moves the pointer (with acceleration), Return closes a dialog, the Time
  global advances a second a second, the startup chimes play (LC ~1.2 s
  FIFO, II ~1.5 s wavetable). The idle Finder: 1.27 (LC) and 1.26 (II)
  million instructions a second.
- Measured on the Tab5 (2026-10-18), System 7.5.5 from the SD card, 4 MB,
  256 colours, idle desktop: LC 100% speed, core 1 ~70%; II 100%, core 1
  ~64%. A double tap (`t` touches) opens a disk, `P x y 0` moves the
  pointer, Return (`k 24`) closes a dialog (both models' ADB), the II's
  menu bar clock shows the time. Not checked by ear or hand yet: sound,
  Bluetooth, the Tab5 Keyboard; not tried on the board: the 12" monitor,
  other colour depths through the Monitors control panel.
- Checked on the host (2026-10-18): the 12" monitor starts in 256 colours
  on both models (before the PRAM fix: black and white); the LC at 512x384
  in thousands (PRAM $58 = $84) draws its colours right.
- Checked on the Tab5 (2026-10-18): the setup screen's two-line Monitor
  buttons for the LC and the II (`docs/images/tab5-setup.png`).
- **Free disk space** (web page): each drive's HFS/MFS/HFS+ volume
  (`mac_disk_space()`, esp32emu's `diskspace`), counted in `sony_frame()`
  at insert and a second after the last write. Tab5 (2026-10-21):
  a 50 MB System 7.5.5 disk 32219136 of 52412416.
- **`.dmg` chunks** (2026-10-21): `hdiutil` images of an HFS+ volume with
  files, bare and behind an Apple partition map, read through
  `MAC_DUMP=1:<file>` (the disk as the Mac sees it): UDRO and UDRW equal
  to the raw (UDTO) image byte for byte, the free space the same; UDZO
  refused ("a compressed .dmg: hdiutil convert -format UDRO"). A UDRO
  used to be taken as its data fork, the zeros left out: a wrong disk.
- **Touchpad** (Tab5, 2026-10-21, the LC, System 7.5.5): slow drags of 150
  panel pixels move the pointer 104 across and 97 down (ADB with the Mac's
  acceleration); a tap on OK closed the not-shut-down dialog.

## Open items

- The II's other ROMs (v1 97851DB6, FDHD 97221136) aren't in the table yet.
- Millions of colours on the 8•24 (32-bit addressing, MODE32) untried.
- Sound volume (DFAC via Egret $0E, the ASC's volume register) is ignored;
  the V8 ignores FIFO B (as the real one).
- No SCSI devices, serial ports, floppy hardware (disk images instead), no
  PDS or other NuBus cards, no 68851 PMMU.
- `CPU_SCALE` is a guess for both models; a Speedometer run against real
  LC and II figures would settle it.
- **Clock** (the clock audit, 2026-10-21: `../esp32emu/CLAUDE.md`, To do):
  Egret's and the II's RTC are the board's time plus an offset the Mac sets
  (not saved): a clock set before the board's time is right is shifted by
  its next step. The Mac reads the clock at boot and then counts its
  one-second interrupt in emulated time (`ii.c`): it misses a later step and
  falls behind below real time.

## Working with the user

- Expects changes built, flashed and checked on the board, then a short
  report of what was verified and what wasn't.
- Keep their private data (WiFi name, IP, SD card contents) out of
  anything meant for publishing. Test disks are copies in the scratchpad,
  never their originals.
