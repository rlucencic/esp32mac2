# Macintosh LC / II video optimization tests

Date: 2026-11-06. Reference: esp32mac2 `8d4750e`, shared runtime `aa245fd`.
Tab5 ESP32-P4 at 360 MHz, ESP-IDF 5.5.6 / GCC 14.2.0, machine component
at -O2, 32 MB PSRAM at 200 MHz, instructions and rodata in PSRAM.
This project supports P4 only; no S3 build or performance claim.

Decision: retain the full switch-outside-loop C candidate, subject to the
workload limits below. The smaller 8-bit-only prefix trial is rejected.

## Isolation and baseline

A complete 16 MB STE flash backup was saved and its length/hash checked
before changing partitions and ROMs. The pre-existing Acorn backup was
preserved. Private ROMs remain outside git; flash backups and raw captures
are under `/tmp/mac2-video-2026-11-06`.

Both baseline and candidate hardware test builds temporarily use a fresh
`mac2vidtest` disk-selection namespace instead of `mac2disks`. This prevents
restoring original disk selections at startup. No guest disks are mounted;
no SD-card image is changed. The namespace adjustment is confined to test
builds; `main/main.c` is restored and a normal production build is checked.
All test ELFs are retained with their corresponding binaries.

Native and physical ROM boots use 4 MB RAM. Both models reach the disk-wait
screen. This is not System 7 or application compatibility coverage.

## Synthetic guest workloads

Inject code at guest RAM $020000 through the existing debugger, set
SR=$2700, D1=$12345678 and D2=$01020304. Repeatedly fill VRAM with longwords
of D1, add D2 after each store, and increment D2 after each complete fill.
Nested DBRA loops use 65,536 longwords per block. Disable guest interrupts;
no OS runs during the measurements.

- LC 8-bit: 640×480 / 13-inch monitor, V8 video register $F26010 = 3,
  two blocks write 512 KB at $F40000; scanline stride is 1,024 bytes.
- LC 16-bit: 512×384 / 12-inch monitor, video register = 4; same fill.
- II 8-bit: 640×480, force 32-bit addressing through VIA2 without powering
  off, transfer control $42, base zero, stride register 160, RAMDAC mode
  register $18. Four blocks write 1 MB at $F9000000. Initialize the CLUT
  with a grayscale ramp through the card's own registers.
- II 24-bit: same monitor, transfer control $46 enables RGB packing,
  stride register 240, RAMDAC mode register $1A. Five blocks write
  $00RRGGBB pixels through the packed view (983,040 stored RGB bytes),
  covering the 921,600-byte visible frame. Web status confirms millions
  of colors. This does not demonstrate OS/Monitors support for that mode.

Each scout samples 32,000 PCs per core at a 25 microsecond interval,
resolved against the exact flashed ELF. These are individual exploratory
bursts, not precise utilization measurements or cross-mode benchmarks.

| Workload | Converter, core 0 | Shared presenter | memcpy | Idle |
| --- | ---: | ---: | ---: | ---: |
| LC 8-bit | 11.0% | 41.2% | 8.4% | 34.0% |
| LC 16-bit | 14.9% | 37.0% | 8.4% | 35.9% |
| II 8-bit | 23.2% | 25.3% | 4.6% | 43.0% |
| II 24-bit | 40.6% | 12.9% | 1.9% | 40.4% |

LC and indexed II guest writes use the direct memory path. In the 24-bit
II scout, core 1 instead samples 12.6% `decode`, 10.8% `jmfb_write8`,
9.0% `write8`, 7.9% `write16` and 4.1% `write32`, in addition to Musashi.
Thus direct-color performance has both conversion and guest memory-path
costs. The LC presenter also has substantial cost outside the converter.

## Candidate and rejected first trial

`jmfb_render_line` originally selects the RAMDAC mode inside its pixel
loop. The retained candidate moves the switch outside, with one loop per
depth. Preserve palette addressing, endian conversion, base/stride,
convolution addressing, transfer-disable behavior, invalid-mode fallback
and per-read VRAM bounds checks. No lookup table or assembly is added.

The first trial added only an early 8-bit path. It passed correctness,
but isolated P4 costs changed −27.827% at 8 bits, +24.503% at 4 bits and
+10.314% at 24 bits. It was rejected. Function layout/register allocation
also matter; a special-case gain alone is insufficient.

## Isolated P4 comparison

A temporary firmware links both actual source versions at -O2, with seeded
1 MB VRAM in PSRAM and an internal RGB565 output buffer consumed through a
volatile checksum. Each pass renders 12 full 640×480 frames; seven ABBA
repetitions run reference/candidate/candidate/reference in one boot.
Indexed strides are 640 bytes in this kernel fixture, direct-color stride
is 1,920. Cycle-counter measurements include interrupts and exclude the
LCD, guest CPU, sound and networking tasks.

| Depth | Reference median cycles/frame | Candidate median cycles/frame | Median cost change | Range |
| --- | ---: | ---: | ---: | ---: |
| 1 bit | 3,473,744 | 3,273,818 | −5.755% | −5.926 to −5.754% |
| 2 bits | 4,367,785 | 3,742,676 | −14.312% | −14.381 to −14.311% |
| 4 bits | 3,762,428 | 3,142,031 | −16.489% | −16.705 to −16.489% |
| 8 bits | 6,643,734 | 4,797,636 | −27.787% | −27.788 to −27.510% |
| 24 bits | 11,855,325 | 10,283,082 | −13.262% | −13.262 to −13.260% |

P4 `jmfb_render_line` shrinks from 602 to 546 bytes (−56), outside IRAM.
No added table, BSS or explicit allocation. These are converter costs,
not full-emulator FPS improvements.

## Full firmware comparison

After the kernel benchmark, flash a fresh reference, boot the II with 4 MB
and no disks, inject each workload, and collect three consecutive core-0
captures per depth. Repeat after a fresh candidate flash with the same
settings and guest code. Each capture uses 32,000 samples at 25 microseconds.

| Workload / build | Converter samples, three captures | Shared presenter | Idle |
| --- | --- | --- | --- |
| II 8-bit reference | 23.2%, 23.4%, 23.2% | 25.0–25.4% | 41.9–42.5% |
| II 8-bit candidate | 17.9%, 17.8%, 18.6% | 24.1–25.8% | 46.7–48.0% |
| II 24-bit reference | 40.5%, 40.3%, 39.2% | 12.4–12.9% | 39.9–42.3% |
| II 24-bit candidate | 36.6%, 37.4%, 37.2% | 12.7–12.9% | 43.1–44.6% |

The converter's sample share decreases in both tested full-firmware modes.
Periodic samples and asynchronous presentation do not establish an exact
whole-emulator speedup, displayed update rate or System performance gain.
The short console FPS windows are not a throughput benchmark. The 24-bit
guest remains limited by its slow memory-write path on core 1; changing
conversion alone does not remove that bottleneck.

## Correctness checks

- 100,960 complete scanline/buffer-canary comparisons pass under UBSan at
  -O2: both monitor sizes, all 16 mode values, valid/invalid scanlines,
  all input byte pairs for 8-bit output, seeded palettes, base/stride,
  convolution, transfer disable and VRAM boundaries.
- Both native ROM boots run 1,000 frames and produce byte-identical final
  pixels against the reference, LC and II, 4 MB, without guest disks.
- Full P4 test builds and the normal production configuration build pass.
  No shared runtime code changes and no S3 target for this project.

## Next bounded trials

The II's 24-bit path remains a strong target: packed RGB-to-RGB565 reads
and the guest's multi-layer slow write path should be measured separately.
For LC, test paired source reads for indexed output and paired RGB555
conversion at 16 bits. Presenter work affects multiple emulators and needs
cross-project validation. Real software, audio by ear and physical input
coverage remain separate work.

## Final board state

The candidate's synthetic 24-bit output and post-reset II disk-wait screen
were visually checked. A cold guest reset ends the synthetic workload;
the II returns to 640×480 at about 60 emulated FPS without HALT, with no
guest disk mounted. The board retains the isolated test build; its
converter matches the production source, while disk selection uses the
test namespace. The production build is separately rebuilt and verified
to contain the normal `mac2disks` namespace. STE's full flash snapshot
remains available for restoration.
