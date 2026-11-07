# LC indexed reads and RGB555 conversion

Date: 2026-11-07. Reference: esp32mac2 `f329977`, esp32emu `07a399d`.
Follows the [Mac video survey](video-optimization-experiment-2026-11-06.md)
and [shared direct-scaling trial](../../esp32emu/docs/rgb565-direct-dispatch-experiment-2026-11-07.md).
Tab5 ESP32-P4 at 360 MHz, ESP-IDF 5.5.6 / GCC 14.2.0, converters at -O2,
32 MB / 200 MHz PSRAM, instructions and rodata in PSRAM.

Decision: retain the combined converter change for lower converter cost
and higher dirty-screen observation rates, with small observed CPU-frame
throughput tradeoffs. In one fresh run per build and mode, signature
increments rise 7.05% at 8 bits and 6.28% at RGB555; CPU-frame throughput
falls 0.18% and 0.33%, respectively. These are not panel-FPS or general
application-speed gains. The implementation remains C.

## Candidate

In `lc_render_line`, replace two byte reads and address XORs in the 8-bit
path with one native uint16_t read. VRAM words are already in host order:
the high byte supplies the first guest pixel and the low byte the second.
Supported widths are even (512 or 640), with no tail or extra source read.
Modes 5..7 retain the existing default 8-bit interpretation.

For RGB555, test once whether the entire source row is inside VRAM. If so,
convert without per-pixel bounds checks, using this equivalent expression:

```c
((p & 0x7FE0) << 1) | ((p & 0x0200) >> 4) | (p & 0x001F)
```

It drops the unused top bit, shifts red and green into place, duplicates
green's high bit into its new sixth bit, and preserves blue. Keep the
original per-pixel path for partial or out-of-range rows. This preserves
its exact bounds behavior, including a final word that starts below an
odd logical VRAM size. The machine's actual VRAM backing is word-aligned;
the new code needs no four-byte alignment. Source and output buffers
retain the existing uint16_t alignment requirement.

The combined converter grows from 522 to 590 P4 text bytes (+68), outside
IRAM. No table or allocation is added. No shared runtime source changes,
so this trial requires Mac2's P4 build rather than the shared 24-build
matrix. Mac2 is P4-only; no S3 runtime result is implied.

## Native correctness

Compile actual source snapshots for reference, paired-only, RGB555-only
and combined variants. ASan plus UBSan at -O2 passes 317,280 complete
row/output-canary comparisons per candidate. Coverage includes both
monitor widths, modes 0..15, invalid and valid scanlines, two source and
output alignment offsets, null VRAM, boundaries around row starts/ends,
and every 16-bit input word. Enumerating the first word covers all 65,536
8-bit index pairs, and complementary first/second words cover every
RGB555 value (including the unused top bit) in both pixel positions.

Native diskless LC and II ROM boots, both 13-inch and 12-inch monitors,
4 MB / 1,000 frames, produce byte-identical final pixels. Test source
snapshots and the modified production converter match. These checks do
not establish OS/application performance, input behavior or audible sound.

## Isolated P4 measurements

Four source variants run in the same minimal firmware. VRAM is in PSRAM;
palette and output are in internal RAM. Each timed pass draws four full
frames, 480 or 384 rows. Seven ABBA repetitions compare reference,
candidate, candidate, reference, with a volatile output checksum.
Interrupts stay enabled; LCD, guest CPU, network and audio are absent.

Median combined converter cycle-cost changes:

| Monitor/source | 1 bit | 2 bits | 4 bits | 8 bits | RGB555 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 13-inch / 640×480 | +0.004% | +0.032% | +0.062% | −23.341% | −37.323% |
| 12-inch / 512×384 | +0.096% | +0.093% | +0.116% | −23.093% | −39.214% |

Paired-only saves 23.338%/23.085% at 8 bits. RGB555-only saves
37.323%/39.215% at 16 bits. Combining them preserves both savings;
other-mode controls stay within 0.13%. Tiny control differences are not
claimed as gains or regressions. These numbers describe only conversion,
not whole-emulator FPS. The 13-inch RGB555 benchmark deliberately exercises
rows past 512 KB of VRAM through the original fallback; the real LC ROM
exposes thousands of colors on the 12-inch monitor.

## Full firmware

Candidate test and normal production P4 builds pass. Test builds use an
isolated disk-selection namespace; production source/build has the normal
namespace. Original guest disks are never mounted. For each build, start
the LC with 4 MB on the 13-inch monitor, then restart on the 12-inch monitor.
Inject the same continuously changing guest VRAM fill, initial registers
and masked interrupts: 8-bit output at 640×480, RGB555 at 512×384.
Read frame and screen-signature counters at three host-monotonic timestamps
about 30 seconds apart. No PC profiling or debugger connection is active
in those windows. Afterward, take three separate 32,000-sample core-0
profiles at 20 kHz (50 microseconds), resolved against the exact flashed ELF.

8-bit profiler-free results:

| Build | Emulated FPS, two windows | Signature increments/s |
| --- | --- | --- |
| Reference | 60.019, 59.953 | 5.559, 5.541 |
| Combined | 59.872, 59.887 | 5.957, 5.925 |

Pooled throughput is 59.986/59.880 FPS (−0.177%); signature increments
are 5.550/5.941 per second (+7.053%). Separate profiles put converter
residency at 11.9–12.9% for the reference and 10.1–10.6% for the candidate.
These are exclusive sample shares, not per-call timings. The signature
counts dirty-screen observations, not completed LCD frames or smoothness.
One fresh run per build does not establish broad statistical confidence.

A deterministic guest fills once, then stays in a self-loop. Complete
1280×720 LCD-buffer captures match byte-for-byte in 8-bit mode, checking
the actual converter, scaling, blending and output placement. This is not
a tearing or physical panel-timing test.

RGB555 profiler-free results:

| Build | Emulated FPS, two windows | Signature increments/s |
| --- | --- | --- |
| Reference | 60.114, 60.138 | 4.657, 4.649 |
| Combined | 59.968, 59.886 | 4.950, 4.941 |

Pooled throughput is 60.126/59.927 FPS (−0.331%); signature increments
are 4.653/4.946 per second (+6.282%). Separate converter sample shares
fall from 15.1–15.8% to 9.8–10.2%. Entire RGB555 LCD-buffer captures also
match byte-for-byte. Both full-firmware workloads show lower converter
residency and higher dirty-screen observation rates, with the reported
small CPU tradeoffs. These observations do not establish representative
application behavior or broad statistical confidence.

The production source and build have the normal disk namespace, no probes
and no temporary test configuration. The board is restored to the
uninstrumented candidate test app's ordinary II 4 MB / 13-inch ROM
disk-wait screen, without synthetic guest code or mounted guest disks.

## Next target

Profile the Mac3 LC II / LC III converter next. It has similar indexed
and RGB555 loops, but Sonora uses packed row strides and rejects whole
rows outside VRAM. Establish its own reference and boundary tests before
transferring these changes. Representative software on copied disks,
physical input and audio-by-ear coverage remain open.

Scratch fixtures, logs and firmware stay outside the repositories under
`/tmp/mac2-lc-video-2026-11-07`.
