# Mac II presentation after packed-write acceleration

Date: 2026-11-07. Current esp32mac2 `3a8188d`, shared runtime `da64601`.
The [packed-write experiment](packed-write-experiment-2026-11-07.md)
measured +57.2% emulated CPU-frame throughput, with fewer dirty-screen
signature increments. This follow-up examines what that counter means.
Tab5 ESP32-P4, 360 MHz, ESP-IDF 5.5.6 / GCC 14.2.0, 200 MHz PSRAM;
Mac II, 4 MB, 640×480, diskless packed-RGB fill, same guest registers and
card setup as the prior experiment. Mac2 is P4-only.

Finding: the faster guest causes more drawing work per presentation pass,
not a slower RGB converter. At matched write demand, pass frequency and
row work return to the reference values. Keep the packed-write change;
no renderer or presentation scheduling change is justified by this counter
alone. No further production code change is retained in this follow-up.

## Measurement

The shared presenter waits 40 ms, then polls the machine's screen signature.
For a changed screen it renders and hashes all 480 source rows. It then
renders selected source rows again to scale and draw changed output rows.
Its two-row cache avoids repeated conversion for adjacent scaled rows.
Consequently, the cost of a pass varies with how much source content has
changed. Signature increments count dirty-screen observations; they do
not count pixels updated or panel refreshes. EMUINFO itself also polls the
signature, so that proxy includes its occasional observations.

Temporary wrappers in the Mac2 main file count signature queries, changed
signatures, render-line calls and cumulative render-call microseconds.
A temporary `V` console command uses `emu_console_printf` to print those
counters, guest frame count
and device elapsed time without querying the screen signature. The shared
runtime and converter are unchanged. Both reference and fast firmware use
the same wrappers and isolated disk-selection namespace. The wrappers and
console command are removed from production source after building the
measurement apps; original guest disk images are never mounted.

After a fresh ROM boot and guest injection, record three counter snapshots
about 30 seconds apart, without PC sampling or a connected debugger. For
each interval, subtract 480 render calls per changed-signature pass to
estimate source rows rendered for drawing beyond the hash pass. Window
boundaries can split a pass, so these are interval estimates. They are not
counts of distinct pixels, output rows, completed panel frames or atomic
guest-frame snapshots. Elapsed render time includes interrupts and timer
instrumentation overhead. Separate three-capture core-0 PC profiles use
32,000 samples each at 25 microseconds and the exact flashed ELF.

A paced guest variant inserts NOPs between writes. It keeps the same
writes, colors and addresses, while reducing writes per wall-clock second.
Guest FPS cannot be compared directly across different instruction mixes.
Separate approximately ten-second calibration intervals briefly pause the
debugger at their endpoints and read D2 (completed full fills) and A0
(position within the fill); those intervals are not the counter windows.
Each complete fill is 327,680 packed CPU longword writes. Active elapsed
time excludes the initial stopped interval. Endpoint connection/pause
latency and host receipt timing limit calibration precision.

Scratch source wrappers, guest generators, logs and exact firmware images
are under `/tmp/mac2-present-2026-11-07`, outside the repositories.

## Results

Pooled counter intervals (about 60 seconds per row); separate write-rate
calibrations are about ten seconds:

| Workload | Writes/s | Changed-screen passes/s | Drawing source-row calls/s | Drawing source-row calls/pass | Render microseconds/call |
| --- | ---: | ---: | ---: | ---: | ---: |
| Reference, original guest | 354,501 | 12.649 | 552.46 | 43.68 | 42.14 |
| Fast, original guest | 553,670 | 10.302 | 841.21 | 81.66 | 41.96 |
| Fast, three NOPs per write | 354,867 | 12.659 | 555.27 | 43.86 | 41.88 |

The fast build does 56.2% more guest writes/s and 52.3% more source-row
conversions for drawing/s, despite 18.6% fewer changed-screen passes.
Drawing work per pass rises 87.0%. Converter time per call stays within
0.7% across the three cases. There is no measured converter slowdown that
would support the initial suspicion of a major PSRAM-contention penalty
inside that function.

With three NOPs per write, the fast build's write rate is just 0.103%
higher than the reference. Pass frequency differs by 0.083%, drawing row
calls/s by 0.509%, and converter time/call by −0.616%. These small residuals
are not claimed as improvements. The paced control strongly supports
higher changed-row work as the explanation for the lower pass counter.
It does not separately attribute every cycle of scaling, memory traffic,
PPA work or scheduling.

Three core-0 PC captures are consistent with that explanation:

| Workload | JMFB converter | Presenter body | memcpy | Idle |
| --- | ---: | ---: | ---: | ---: |
| Reference | 26.2–27.4% | 13.1–14.3% | 1.8–1.9% | 51.6–52.2% |
| Fast | 23.8–23.9% | 18.2–19.0% | 2.9% | 48.9–49.6% |
| Fast, paced | 25.8–28.0% | 13.1–14.0% | 1.7–1.9% | 50.4–52.4% |

Sample shares are exclusive residency, not per-call cost. The converter
is invoked fewer times overall because the always-full hash pass occurs
less frequently, while the presenter scales and copies more drawing rows.
Its 40 ms delay is after each pass, so increased work naturally lengthens
the time between subsequent dirty-screen observations.

Without wrappers, a repeat of the unpaced fast guest gives 51.514/51.457
emulated FPS and 10.416/10.445 signature increments/s. A five-NOP guest
lowers calibrated writes to about 288,939/s and raises the signature proxy
to 13.382/13.381/s. These additional controls show the same direction.
Instrumented reference/fast FPS is 32.553/51.044 pooled, about 0.7–1.0%
below the prior uninstrumented results. The wrappers have measurement cost;
use the prior experiment for the retained production throughput claim.
Paced guest FPS is deliberately omitted from the comparison table because
NOPs alter guest cycles per write.

## Outcome and next target

The earlier signature reduction is a pass-cadence observation, not evidence
of fewer source rows drawn per second. More source-row drawing work also
does not establish better panel smoothness, tear-free frames or lower input
latency. Those require different measurements and representative software.
OS/application tests on copied disks, physical input and audio-by-ear
coverage remain open.

The next graphics target is the shared RGB565 scaling/blending path in
`esp32emu/components/emu/emu.c`: `scale_row`, `blend565`, and the vertical
blend loop inside `present_task`. Measure those kernels and preserve exact
pixels, tap weights and unchanged-row skipping before changing scheduling.
Any retained shared runtime change needs the sibling build matrix and
board checks; this trial changes no shared runtime source. LC indexed and
RGB555 conversion remains a separate local candidate. No S3 performance
result is implied.

Both diagnostic P4 builds and the restored normal production build pass.
Production source is unchanged from the committed packed-write change;
the production ELF has the normal disk namespace and no probe command.
The board is restored to the uninstrumented candidate test app's normal
II ROM disk-wait screen, with no guest disks mounted and no synthetic code
running. Only this report and follow-up documentation remain changed.

The subsequent [shared half-weight row trial](../../esp32emu/docs/rgb565-half-blend-experiment-2026-11-07.md)
retains a separate averaging loop for uniform vertical weight 16. Its P4
kernel and full-firmware measurements are separate from this diagnostic
study; they include a small CPU-throughput tradeoff and no panel-FPS claim.

The later [exact 3:2 horizontal-scaling trial](../../esp32emu/docs/rgb565-3to2-experiment-2026-11-07.md)
rejects a geometry-selected row-function integration. Its isolated P4
kernel is faster and full LCD pixels match, but repeated CPU-frame
throughput falls 1.56% at 3:2 and 1.27% in a non-3:2 fallback control.
The shared runtime is restored to `784bba1`; only documentation is retained.
These measurements retain the same counter limitations as this
investigation. Direct conditional dispatch preserving the old general
loop, LC conversion and real-software tests remain candidates.
