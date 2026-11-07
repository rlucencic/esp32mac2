# Mac II packed-RGB longword writes

Date: 2026-11-07. Reference: esp32mac2 `93c692d`, shared runtime `c8a05b5`.
This follows the [paired RGB renderer experiment](packed-rgb-experiment-2026-11-07.md).
Tab5 ESP32-P4, 360 MHz, ESP-IDF 5.5.6 / GCC 14.2.0, Mac2 at -O2,
32 MB / 200 MHz PSRAM, instructions and rodata in PSRAM. Mac2 is P4-only.

Decision: retain for the 57.2% gain in emulated CPU-frame throughput on
the RGB fill, with an observed presentation tradeoff: screen-change
signature increments fall 18.9%. Indexed throughput remains at 60 Hz.
This is not a claim of faster panel updates or general application speed.

## Candidate

The packed view accepts a CPU longword `$00RRGGBB` and stores three bytes.
Previously, the model decoded the address, split the longword into two
words, then four bytes, repeating address decoding and byte-lane mapping.
The new slot-9 helper translates an aligned packed longword once, ignores
the first lane, writes R/G/B in the existing host-word VRAM representation,
and marks VRAM dirty. Whole register writes still take their original path.
Non-RGB, unaligned and non-aperture accesses retain the byte fallback.

The final partial pixel preserves the reference's per-byte bounds. Writes
wholly beyond physical VRAM but within the packed aperture remain ignored
without setting the dirty flag. RGB writes remain valid when display
transfer is disabled. No shared source, table or allocation is added.
The P4 helper is 156 bytes and the II write32 function grows 20 bytes:
176 added text bytes, outside IRAM.

## Correctness

A source-inclusion fixture compiles the original and candidate II decoder,
write32/write16/write8 chain and JMFB code into the same native program.
At -O2 under UBSan, 5,345,760 writes match card/model state and dirty flags,
with full VRAM comparisons after every boundary/random case and every
64 writes in the exhaustive sweep. Cases cover both address modes, five
control values (including RGB with transfer disabled), every aligned word
in the two-megabyte tested address span, unaligned fallbacks, physical-VRAM
and aperture ends, register writes and other slots. In 24-bit mode, the
span also crosses into the next slot as the real decoder specifies.

Unrelated I/O callbacks are stubs in this fixture; it does not establish
other device behavior. Calls directly exercise the model's write32 handler;
CPU-level unaligned dispatch is not separately tested by this fixture.
A repeat using heap-backed VRAM under ASan plus UBSan also passes all
5,345,760 writes, checking the allocation boundaries.
Diskless native LC and II ROM boots, 4 MB and 1,000 frames each, finish
with byte-identical reference/candidate screenshots.

## P4 comparison

The full-firmware workload is the same diskless synthetic 68020 fill used
for the renderer experiment: Mac II, 4 MB, 640×480, 32-bit addresses, RGB
control $46, stride register 240, RAMDAC mode $1A, five blocks of 65,536
longwords through the packed view. Registers and guest code are identical;
interrupts are masked. The indexed control uses four such blocks and the
normal direct-VRAM path. These are guest memory-write workloads, not OS
or application benchmarks.

Each build uses the isolated `mac2vidtest` disk-selection namespace, so
original guest disk images are never mounted. Capture EMUINFO frame and
screen-signature counters over two approximately 30-second windows without
periodic PC sampling. Throughput uses host monotonic receipt times; screen
signature increments are a presentation-change proxy, not panel FPS.
Resolve subsequent core-1 profiles against the exact flashed ELF.

Three 32,000-sample core-1 profiles at 25 microseconds show:

| Symbol | Reference sample share | Candidate sample share |
| --- | ---: | ---: |
| Address decode | 12.3–12.7% | 2.8–2.9% |
| JMFB byte write | 10.7–10.8% | absent from top 25 |
| II byte write | 9.0–9.7% | absent from top 25 |
| II word write | 7.6–8.1% | absent from top 25 |
| II longword write | 4.2–4.4% | 6.1–6.3% |
| New packed helper | absent | 6.7–6.8% |
| Musashi execute | 27.7–28.3% | 41.6–42.1% |

Sample shares are exclusive residency, not per-call costs. Guest CPU
execution becomes the larger remaining target as repeated write decoding
is removed.

Final throughput uses a clean reference reflash after the candidate, with
the same completed ROM-boot wait before injection. An earlier reference
check (32.385/32.414 FPS) injected before that wait ended and is excluded
from the final table. It is preserved in scratch evidence.

| Workload/build | Emulated FPS, two windows | Signature increments/second |
| --- | --- | --- |
| RGB reference | 32.725, 32.845 | 12.850, 12.845 |
| RGB candidate | 51.554, 51.517 | 10.404, 10.436 |
| Indexed reference | 60.148, 60.144 | 10.319, 10.285 |
| Indexed candidate | 60.115, 60.180 | 10.280, 10.279 |

Pooled RGB throughput rises from 32.785 to 51.536 FPS (+57.194%). The
signature proxy falls from 12.848 to 10.420/s (−18.894%). Indexed pooled
throughput is 60.146/60.147 FPS; no indexed gain is claimed. Frame and
signature counters measure different work. The presenter increments the
signature when it observes a dirty screen; this does not count guest
frames or panel refreshes. EMUINFO itself also queries the signature.
The reason for the presentation reduction is not isolated by this trial.

Full test and production P4 builds pass. The production ELF contains only
the normal `mac2disks` namespace; only isolated test apps are flashed for
these diskless measurements. No S3 result applies to this P4-only project.
The board is restored to the candidate's normal II ROM disk-wait screen,
with the synthetic guest removed and no guest disks mounted.

OS/application compatibility, physical-input behavior and audio-by-ear
checks remain open. Next measure rendering/presentation under the faster
write workload and a representative disk-copy workload before making a
visible-performance claim. LC indexed/RGB555 conversion remains another
local target. Scratch fixtures, logs, ELFs and images stay outside the
repository in `/tmp/mac2-write-2026-11-07`.
