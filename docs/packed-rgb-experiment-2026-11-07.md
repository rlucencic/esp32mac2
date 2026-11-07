# Mac II paired packed-RGB conversion

Date: 2026-11-07. Reference: esp32mac2 `700a357`, shared runtime `d6975bc`.
This follows the [mode-selection experiment](video-optimization-experiment-2026-11-06.md).
Tab5 ESP32-P4, 360 MHz, ESP-IDF 5.5.6 / GCC 14.2.0, machine code at -O2,
32 MB / 200 MHz PSRAM with instructions and rodata in PSRAM. Mac2 is P4-only.

Decision: retain for the rendering benefit, with a measured tradeoff in the
longer synthetic run: presentation-change detections rise about 21.9%,
while emulated CPU-frame throughput is about 0.47% lower. This is not an
emulated-CPU speed improvement or a general software-performance claim.

## Candidate

Convert two adjacent packed RGB pixels from three native 16-bit reads.
The emulator stores VRAM with each word in host order, so these words
contain R0:G0, B0:R1 and G1:B1. Extract both RGB565 results directly from
them, removing six individual byte loads and their XOR-one addressing.

JMFB base and stride are even, and each iteration advances six bytes.
The machine's VRAM already has native-word alignment; the implementation
needs two-byte alignment, with no new four-byte alignment requirement.
Check all six source bytes before the fast pair. At the VRAM boundary,
retain the old individual-pixel checks so a valid final pixel is rendered
even if its neighbor is outside VRAM. Indexed paths and their palette
lookups stay unchanged. Invalid mode values retain the direct-color
fallback and its existing addressing.

P4 `jmfb_render_line` grows from 546 to 668 bytes (+122), outside IRAM.
No table, BSS or allocation is added. This is portable C within the
emulator's existing host-order VRAM representation; no assembly is added.

## Correctness

153,453 complete reference/candidate scanline and buffer-canary comparisons
pass at -O2 under UBSan. Coverage includes:

- All 16,777,216 RGB colors in each of the two pixel positions, plus mixed
  adjacent colors from seeded VRAM.
- Both monitor sizes, all 16 mode values, valid and invalid scanlines,
  palettes, base/stride, convolution and disabled transfer.
- Explicit VRAM-end cases, including a valid first pixel with an invalid
  second pixel, plus fully out-of-range pairs.
- A VRAM buffer deliberately offset by two bytes from its allocation,
  confirming that four-byte alignment is unnecessary.

The reference and candidate native ROM boots also produce byte-identical
final pixels after 1,000 frames for both LC and II, 4 MB, without disks.
These checks do not establish System or application compatibility.

## Isolated P4 measurements

Use the same two-source firmware method as the prior experiment: source
VRAM in PSRAM, internal RGB565 output buffer consumed through a volatile
checksum, 12 full 640×480 frames per timed pass, seven ABBA repetitions
(reference/candidate/candidate/reference). The indexed fixture's stride is
640 bytes and the packed-RGB stride is 1,920. Cycle measurements include
interrupts; guest execution, LCD, sound and networking tasks are absent.

| Depth | Reference median cycles/frame | Candidate median cycles/frame | Median cost change | Range |
| --- | ---: | ---: | ---: | ---: |
| 1 bit | 3,275,921 | 3,274,362 | −0.049% | −0.236 to −0.046% |
| 2 bits | 3,744,852 | 3,744,354 | −0.013% | −0.113 to −0.012% |
| 4 bits | 3,144,105 | 3,143,286 | −0.027% | −0.335 to −0.025% |
| 8 bits | 4,794,572 | 4,798,825 | +0.089% | +0.088 to +0.633% |
| 24 bits | 10,283,455 | 6,159,018 | −40.108% | −40.108 to −40.106% |

The indexed differences are small; no indexed improvement is claimed.
The 24-bit figure measures converter cost, not whole-emulator FPS.

## Full firmware comparison

Use the prior diskless 640×480 24-bit synthetic guest fill, II with 4 MB,
32-bit addressing, RGB transfer control $46, stride register 240 and RAMDAC
mode register $1A. Five 65,536-longword blocks write through the packed
view, covering the visible frame. Guest interrupts are masked.

Freshly flash each build, boot the ROM, inject identical code and initial
registers, then collect three 32,000-sample core-0 captures at 20 kHz (50 microseconds).
Resolve each capture against the exact flashed ELF. Both hardware builds
use the same isolated `mac2vidtest` disk-selection namespace; original disk
images are never mounted. The temporary namespace change is not retained
in source, and the normal production configuration is separately built.

| Build | Converter samples, three captures | Presenter | Idle |
| --- | --- | --- | --- |
| Reference | 36.6%, 36.8%, 36.3% | 12.9–13.5% | 43.4–44.4% |
| Candidate | 26.7%, 27.6%, 27.8% | 13.2–13.8% | 52.2–53.8% |

Converter residency decreases consistently, with more sampled idle time.
The change in sample share is not the same quantity as the kernel-cycle
saving: presentation scheduling and redraw work vary in the full runtime.
No overall FPS gain is claimed. Short console windows show approximately
34.0 emulated FPS for the reference and 32.6–32.7 for the candidate.
The longer, profiler-free comparison below narrows this apparent difference.

Full P4 test and production builds pass. The production ELF is checked to
contain the normal `mac2disks` namespace. No shared runtime code changes.

## Profiler-free throughput and presentation proxy

The short console windows prompted a longer check. Freshly restart each
firmware and launch the same guest code; run the candidate first, followed
by the reference. Without periodic PC sampling, record EMUINFO frame and
screen-signature counters at three points about 30 seconds apart. Use
host monotonic elapsed time between received counter captures.

| Build | Emulated FPS, two windows | Screen-signature increments/second |
| --- | --- | --- |
| Reference | 32.977, 32.980 | 10.549, 10.549 |
| Candidate | 32.823, 32.821 | 12.837, 12.882 |

Pooled candidate frame throughput is 0.474% lower and signature-increment
rate is 21.899% higher. The latter is a presentation-change proxy: it
counts change detections used by the presenter and the status requests,
not panel refreshes or precisely measured displayed frames. Query schedules
are identical. Faster conversion can change redraw scheduling and memory
traffic; this comparison does not isolate the cause of the small CPU
throughput difference. There are two windows per build, not a broad
statistical or application benchmark. The large slowdown suggested by the
short captures does not reproduce, but the smaller observed tradeoff is
recorded rather than called a CPU gain.

## Limits and next target

The 24-bit guest CPU also spends substantial time translating addresses
and writing packed pixels through the II's slow memory path. Conversion
savings do not remove that cost. Short console FPS windows and periodic
samples are not a whole-emulator throughput benchmark. OS/software,
physical input and audio-by-ear coverage remain open.

The next bounded trial should measure the guest's packed writes separately
from rendering, preserving byte-lane behavior, unmapped lanes, VRAM bounds
and dirty tracking. LC RGB555 conversion remains a separate candidate.

Raw fixtures, matching test ELFs/binaries and captures are outside git in
`/tmp/mac2-rgb-2026-11-07`. Earlier STE/Acorn flash backups are preserved.

## Final board state

The board is back at the II's normal 640×480 disk-wait screen, visually
checked without HALT. It retains the isolated candidate test build, with
no guest disk mounted. The production configuration remains unchanged
and builds with the normal disk-selection namespace.

The next [packed-longword write experiment](packed-write-experiment-2026-11-07.md)
removes repeated address decoding on the guest CPU side; its measurements
use this paired-renderer commit as the reference.
