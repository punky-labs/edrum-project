# Claude Code Prompt — Sensing Rewrite STAGE 1

> Paste this whole file as the task. It is scoped to **Stage 1 only**. Do not
> implement the band-pass / power-domain / decay model — that is Stage 2. The goal of
> Stage 1 is to prove the new **DMA → SampleStream → engine → MIDI** pipeline works
> end-to-end on a single pad, with the new layered architecture in place.

## Context — read these first
- `docs/sensing_rewrite_step0.md` — parameter/algorithm decisions (background)
- `docs/sensing_rewrite_step1_plan.md` — **the architecture spec this implements**
- `docs/project_state.md` — current hardware/firmware state

You are working in the firmware at `firmware/src/`. The active build environment is
`[env:xiao_esp32s3_head]` in `platformio.ini` (XIAO ESP32-S3, COM13, TinyUSB USB MIDI,
`ARDUINO_USB_MODE=0`). **Do not change any USB / TinyUSB / Serial setup** — that config
works and a MIDI-vs-CDC-serial question is deliberately out of scope.

## What Stage 1 delivers
A working 3-layer sampling/sensing pipeline, with a **SINGLE_PIEZO** engine using a
**simple time-domain peak detector** (NOT the Edrumulus band-pass — that is Stage 2).
This proves the plumbing: DMA continuous sampling at 8 kHz feeds a SampleStream, which
hands gapless per-channel blocks to a PDrum2Trigger, which detects hits and drives MIDI
exactly as today.

Success = the Roland KD-80 on jack 2 (head channel GPIO6) triggers cleanly via the new
pipeline, with sane velocities and no dropped-sample warnings at normal play.

---

## Architecture to build (per Step 1 plan §2)

Three layers, dependencies pointing downward only.

### Layer 1 — `firmware/src/sensing/sampling/AdcSampler.{h,cpp}`
Sole owner of ADC1 + the `adc_continuous` DMA driver (ESP-IDF v5 / arduino-esp32 3.x).
- `bool begin(const uint8_t* channelGpios, uint8_t numChannels, uint32_t perChannelHz)`
- `void stop()`
- A way to retrieve the newest completed DMA conversion frames (non-blocking). Use the
  ESP-IDF `adc_continuous` API directly (`adc_continuous_new_handle`,
  `adc_continuous_config`, `adc_continuous_read`, etc.). 12-bit width, `ADC_ATTEN_DB_12`.
- `uint8_t numChannels() const; uint32_t sampleRateHz() const;`
- Must configure all 8 channels: GPIO `{2,3,4,5,6,7,8,9}` (all ADC1 on XIAO S3).
- Knows nothing about drums. This is the ONLY file allowed to reference ADC/GPIO/DMA.
- Handle the `adc_continuous` result format: results are `adc_digi_output_data_t`
  carrying channel + value; demux is Layer 2's job, but Layer 1 must expose enough
  (channel id + value per sample) for Layer 2 to demux. Choose a clean small struct to
  hand frames up, or expose the raw read buffer + a documented decode helper.

### Layer 2 — `firmware/src/sensing/sampling/SampleStream.{h,cpp}`
Owns the ring buffer (move it here from the global `ring_buffer.h` model). Pulls frames
from AdcSampler, demuxes interleaved samples into per-channel ring storage, serves
gapless per-channel reads to multiple independent consumers.
- `void begin(AdcSampler* src)`
- `void pump()` — pull all available DMA frames into the ring buffer; the ONLY thing
  that advances the write head. Called once per `loop()`.
- A `Cursor` type = per-consumer read position + overrun detection.
- `uint16_t read(uint8_t channel, Cursor& cursor, uint16_t* dst, uint16_t maxSamples)`
  — copy up to `maxSamples` new samples for this channel since the cursor; advance the
  cursor; return count. If the ring wrapped past the cursor (consumer fell behind),
  reset the cursor to the oldest valid sample and set a flag the caller can check
  (`cursor.overran`) so the engine can reset its state. Do not return garbage.
- `uint16_t readWindow(uint8_t channel, uint32_t startIndex, uint16_t* dst, uint16_t count) const`
  — full-fidelity historical window grab (for the scope tool; see scope migration below).
- `uint32_t writeHead() const; uint8_t numChannels() const; uint32_t sampleRateHz() const;`
- Ring buffer sized generously: hold **at least 100 ms** of all-channel samples
  (8 ch × 8000 Hz × 0.1 s = 6400 samples/ch). Use a power-of-two depth ≥ 8192 per
  channel for cheap modulo. RAM is plentiful on the S3.

### Layer 3 — `firmware/src/sensing/pdrum2/PDrum2Trigger.{h,cpp}`
Replace the existing header-only stub with a real implementation. SINGLE_PIEZO only for
Stage 1, simple peak detector.
- `void initialize(uint32_t sampleRateHz)` — store Fs; precompute scan/mask sample
  counts from the ms config values. (No filters yet — Stage 2.)
- `void processBlock(const uint16_t* headBlock, const uint16_t* rimBlock, uint16_t n)`
  — run a simple time-domain peak detector over the head block:
  threshold crossing → scan for peak over scanTime → emit hit + velocity via the
  velocity curve → enter mask for maskTime. Sample-count based timing (NOT millis()).
  Ignore `rimBlock` for Stage 1 (SINGLE_PIEZO).
- Keep a velocity curve LUT or simple map (reuse the curve approach from the old
  PDrumTrigger `curve()`; a lookup or linear map is fine for Stage 1).
- Implement all `TriggerEngine` result getters/setters (see interface change below).
- `getTriggerSnap()` must return the global sample index at the threshold crossing, so
  the scope tool can grab the right window from SampleStream.

---

## Interface change — `firmware/src/sensing/TriggerEngine.h`
Replace the per-sample push with block processing:
- REMOVE: `virtual void sensing(int headVal, int rimVal, uint32_t ringHead = 0) = 0;`
- ADD:
  ```cpp
  virtual void initialize(uint32_t sampleRateHz) = 0;
  virtual void processBlock(const uint16_t* headBlock,
                            const uint16_t* rimBlock, uint16_t n) = 0;
  ```
- Keep ALL existing getters/setters unchanged.

## main_esp32s3.cpp rewrite (per Step 1 plan §5)
- In `setup()`: create one `AdcSampler` (8 ch, GPIOs {2..9}, 8000 Hz), one
  `SampleStream`, call `stream.begin(&sampler)`. Create `PDrum2Trigger` per input and
  call `initialize(stream.sampleRateHz())`. Keep `applyConfig()`.
- In `loop()`: call `stream.pump()` once. For each input, `stream.read()` the head and
  rim channels into local buffers (same count for both), call `processBlock()`, then
  the EXISTING hit/rim/choke → MIDI + SysEx-debug output (unchanged).
- DELETE `sampleADC()` (the analogRead loop) and the `[RATE]` temp measurement block.
- The hit/choke MIDI sending, `rawToMidi()`, SysEx debug (`05 03`), serial command
  handler — all UNCHANGED except where they read raw samples (see scope migration).
- Per-input channel map stays `kHeadCh={2,4,6,8,-1}`, `kRimCh={3,5,7,9,-1}` (these are
  GPIO numbers; map them to SampleStream channel indices 0..7 in frame order).

## Scope tool migration
`scopeDump()` currently reads the global `ringBuf[][]`. Change it to use
`stream.readWindow()` for the 100-pre/100-post window. The global `ringBuf`/`ringHead`/
`ringBufRead`/`ringBufWrite` and `ring_buffer.h` are removed in favour of SampleStream;
the ADC-dump serial command (`a`) and scope must read through SampleStream instead.
Keep the serial scope protocol output format identical (so the desktop ADC Scope tool
still parses it).

## platformio.ini
- In `[env:xiao_esp32s3_head]` `build_src_filter`, replace
  `+<sensing/pdrum/PDrumTrigger.cpp>` with the new files:
  `+<sensing/pdrum2/PDrum2Trigger.cpp>`, `+<sensing/sampling/AdcSampler.cpp>`,
  `+<sensing/sampling/SampleStream.cpp>`.
- **Retire the RP2040 env:** `[env:xiao_rp2040]` cannot run `adc_continuous`. Comment it
  out with a note that it is superseded (Step 0/Step 1). Do not try to keep it building.
- Leave `xiao_test` and `xiao_adc_bench` envs untouched.

## Do NOT do in Stage 1
- No band-pass / IIR / squaring / power-domain / decay model (Stage 2).
- No DUAL_PIEZO or PIEZO_SWITCH_CHOKE logic (Stages 3–4).
- No new config struct fields / SysEx / app changes (Step 2 of overall plan).
- No USB/TinyUSB/Serial changes.
- No dual-core / FreeRTOS task split — single-core, `pump()` in `loop()`.

## Acceptance criteria
1. `[env:xiao_esp32s3_head]` builds clean (0 errors).
2. On hardware: KD-80 (jack 2, head GPIO6) triggers via the new pipeline; velocities
   scale soft→hard; no continuous-fire/runaway; no dropped-sample (cursor overrun)
   warnings at normal playing speed.
3. The serial `s` config dump, `a` ADC dump, and `o`/scope still function and the
   desktop ADC Scope tool still parses scope output.
4. `AdcSampler.cpp` is the only file referencing ADC/GPIO/DMA APIs; `PDrum2Trigger.cpp`
   references none of them.

## Report back
- Confirm build status and the measured per-channel sample rate (print it once at boot
  from `AdcSampler`, e.g. the configured vs the driver-reported rate).
- Note any `adc_continuous` API quirks encountered (frame format, read sizing) so we can
  account for them in Stage 2.
