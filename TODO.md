# esp32mac2 roadmap

Proposed work in priority order. See [Development](README.md#development) for the current project.

1. **Verify millions of colours from the guest OS.** Boot a local System image
   with MODE32 on the Mac II and exercise the 8•24 through the Monitors control
   panel and a graphics app. Compare known colour patches with the packed RGB
   path in `components/mac2/jmfb.c`. Check the LC’s 512×384 Thousands mode as a
   separate Tab5 workflow.

2. **Check guest volume from the control panel.** Trace Egret/DFAC and ASC
   register writes in `components/mac2/egret.c` and `components/mac2/asc.c`.
   Use repeatable tones to check that mute and several levels change PCM
   amplitude, then confirm on the Tab5 speaker.

3. **Calibrate LC and II pacing.** Run the same Speedometer version and disk
   image under the same guest RAM and video conditions. Compare guest CPU and
   graphics scores with real-machine results and record Tab5 frame pacing
   before adjusting `CPU_SCALE`.
