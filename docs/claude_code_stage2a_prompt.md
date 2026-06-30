# Claude Code Prompt — Sensing Rewrite STAGE 2a

> Paste this whole file as the task. Scoped to **Stage 2a ONLY**: the core
> Edrumulus power-domain detection for a SINGLE_PIEZO pad (the KD-80), plus
> software spike cancellation, plus the coordinate-system fix. Do NOT implement
> clip/overload correction, adaptive decay estimation, DUAL_PIEZO, or
> PIEZO_SWITCH_CHOKE — those are Stages 2b/2c/2d.

## Context — read these first
- `docs/sensing_rewrite_stage2_plan.md` — **the plan this implements** (esp. §0 hardware
  caveat, §1 port/skip list, §2 architecture mapping, §3 coordinate fix, §5 staging)
- `docs/sensing_rewrite_step0.md` — parameter tiers & decisions
- `docs/project_state.md` — current state (Stage 1 complete, scope parked)

## Reference source — port FROM here (read these, do not modify them)
On this Windows machine:
`D:\Dev\E-drums\eDrumulus\edrumulus-src\edrumulus\`
- `edrumulus.h` — `FastWriteFIFO` class, `update_fifo()`, the band-pass coefficient
  constants (`bp_filt_a[4]`, `bp_filt_b[5]`, `x_filt_delay`), `SSensor` fields,
  `ADC_MAX_RANGE`/`ADC_MAX_NOISE_AMPL` (in edrumulus_hardware.h).
- `edrumulus.cpp` — `Pad::initialize()` (the parameter derivations + decay LUT build)
  and `Pad::process_sample()` (the detection state machine). Port the
  **single head sensor** path only (`number_head_sensors == 1`).
- `edrumulus_hardware.cpp` — `cancel_ADC_spikes()` (port this for spike cancellation).
- `edrumulus_parameters.cpp` — KD8 preset values (for the KD-80 default config).

> These are GPL-2.0. We are porting the algorithm into our own GPL-compatible engine;
> keep Volker Fischer's authorship credit in a comment where substantial logic is ported.

## What Stage 2a delivers
Replace the placeholder simple detector in `PDrum2Trigger` with the real Edrumulus
power-domain detector for SINGLE_PIEZO, validated on the KD-80. The point of this stage
is that the **hard-hit runaway is designed out by the decay model** — that is the
acceptance gate.

Active build env: `[env:xiao_esp32s3_head]` (COM13). Do NOT touch USB/TinyUSB/Serial
setup. Layers 1 (`AdcSampler`) and 2 (`SampleStream`) are DONE and must NOT change.

---

## Implementation

### A. Helpers (port verbatim into the engine or a small shared header under sensing/)
- `FastWriteFIFO` class (from edrumulus.h) — ring buffer with `operator[]` indexing.
- `update_fifo()` free function (from edrumulus.h).

### B. Spike cancellation (sensing/pdrum2/ or a small SpikeCancel.{h,cpp})
- Port `cancel_ADC_spikes()` from edrumulus_hardware.cpp. It operates on a float sample
  + overload flag, keeps a 5-deep state/sample history per channel, and zeroes 1–4
  sample spikes. Use **level 4** (matches Edrumulus ESP32 default; §0: current boards
  lack the RC front-end so we need this).
- Per-channel state: this engine instance only owns its head channel, so the history
  arrays collapse to single-channel (drop the `[pad_index][input_channel_index]`
  indexing — keep one set of prev-state/prev-sample members).
- NOTE: spike cancel adds latency = level samples (~4 @ level 4). Acceptable.

### C. PDrum2Trigger — the detection engine (sensing/pdrum2/PDrum2Trigger.{h,cpp})
Rewrite the SINGLE_PIEZO path to port the Edrumulus single-head-sensor detection.

**initialize(sampleRateHz):** port the relevant derivations from `Pad::initialize()`:
- `threshold` from `velocity_threshold` (the dB→linear-power formula).
- `scan_time`, `pre_scan_time`, `total_scan_time`, `mask_time` (ms→samples).
- Build the `decay[]` LUT from `decay_len1/2/3` + `decay_grad_fact1/2/3` (the 3-segment
  loop). `decay_fact`, `decay_mask_fact`.
- `first_peak_diff_thresh`.
- Velocity curve: `velocity_factor`, `velocity_exponent`, `velocity_offset` (the
  curve_param formula). Use these instead of the old LUT.
- `x_sq_hist_len = total_scan_time`; size the FIFOs accordingly.
- Constants: `ADC_MAX_RANGE=4096`, `ADC_MAX_NOISE_AMPL=8`,
  `ADC_noise_peak_velocity_scaling=1.0f/6.0f`, `x_filt_delay=5`.

**processBlock(headBlock, rimBlock, n, blockStartAbsIndex):** for each sample:
1. Spike-cancel the sample (§B).
2. Band-pass IIR (bp_filt_a/b) → `x_filt = filtered`, then `x_filt *= x_filt` (power).
3. Maintain `x_sq_hist` (FIFO of raw `input*input`) for peak picking.
4. Decay subtraction: `x_filt_decay = x_filt - decay_scaling*decay[...]` when
   `decay_back_cnt > 0` (clip at 0).
5. Mask-time rescue (`mask_back_cnt`/`decay_mask_fact`).
6. Threshold test → scan state machine → at scan end, find first peak (timing) and
   max peak (velocity) in `x_sq_hist`.
7. At mask end: arm decay (`decay_back_cnt = decay_len`, `decay_scaling = decay_fact *
   max_x_filt_val`).
8. On peak found: velocity = `velocity_factor * pow(peak_val * ADC_noise_peak_velocity_scaling,
   velocity_exponent) + velocity_offset`, clipped to 1..127. Set `hit_`, `velocity_`,
   `velocityRaw_` (= sqrt(peak_val) or peak_val as appropriate — match existing raw
   semantics used by `rawToMidi` in main).

**SKIP in 2a (explicitly do NOT port):** overload/clip correction (the
`amplification_mapping` / overload-history walk), adaptive decay power estimation
(`decay_pow_est_*`), positional sensing, rim/choke, multi-sensor. Leave clean TODO
markers where 2b/2c/2d will slot in.

### D. Coordinate fix (interface change)
- `TriggerEngine`: change `processBlock(head, rim, n)` →
  `processBlock(head, rim, n, uint32_t blockStartAbsIndex)`.
- Engine indexes detection in absolute sample space (blockStartAbsIndex + j).
  `getTriggerSnap()` returns a true distance from block end back to the crossing in
  absolute space (so `main`'s existing `blockEndIdx - getTriggerSnap()` is correct
  with no origin mismatch). Remove the old private `procIndex_` origin-relative scheme.

### E. main_esp32s3.cpp
- Pass the block-start absolute index into `processBlock`. The block start = the head
  cursor position BEFORE the read (capture it before `stream.read`).
- Everything else (MIDI out, `05 03` SysEx, scope arm — still guarded/parked) unchanged.

### F. Config (Config.h) — add Tier-2 real-unit fields
Add the fields listed in plan §4 to `InputConfig` with KD8-derived defaults so the
engine runs without app/SysEx changes:
`preScanTimeMs, firstPeakDiffThreshDb, decayLen1Ms, decayGradFact1, decayLen2Ms,
decayGradFact2, decayLen3Ms, decayGradFact3, decayFactDb, maskTimeDecayFactDb,
decayEstDelayMs, decayEstLenMs, decayEstFactDb, clipCompAmpmapStep`.
(decayEst* and clipComp* are stored now but UNUSED until 2b — store them anyway so the
struct is stable.) Use a fixed-point convention (document it). Add matching
`TriggerEngine` setters; wire them in `applyConfig()`. This changes the LittleFS blob —
note in output that `uploadfs` + config reset is expected.

KD8 defaults (from edrumulus_parameters.cpp, real units):
```
velocity_threshold=8 (global), velocity_sensitivity=2, curve=LOG2, scan_time_ms=3.0,
pre_scan_time_ms=2.5, mask_time_ms=6, mask_time_decay_fact_db=10.0,
first_peak_diff_thresh_db=8.0, decay_fact_db=1.0,
decay_len1_ms=0/grad1=200, decay_len2_ms=350/grad2=450, decay_len3_ms=500/grad3=45,
decay_est_delay_ms=7.0, decay_est_len_ms=4.0, decay_est_fact_db=16.0,
clip_comp_ampmap_step=0.08
```

### G. DC offset — try WITHOUT first
Do NOT port the 10000-sample DC estimator. The band-pass filter has no DC gain, so DC
bias is largely rejected. After flashing, if the KD-80 baseline `x_filt` sits clearly
off-zero / causes false triggers, add a light per-channel DC IIR (the
`dc_offset_iir_gamma` one-pole from edrumulus.cpp) and note it. Report which path was
needed.

---

## Do NOT do in 2a
- No clip/overload correction, no adaptive decay estimation (2b).
- No DUAL_PIEZO (2c), no PIEZO_SWITCH_CHOKE/choke (2d).
- No positional sensing, multi-sensor, second rim, cross-talk (out of scope / later).
- No changes to AdcSampler / SampleStream (Layers 1/2 are done).
- No USB/TinyUSB/Serial changes. No reviving the parked serial scope.

## Acceptance criteria
1. `[env:xiao_esp32s3_head]` builds clean (0 errors).
2. **Hard-hit runaway does NOT reproduce** on the KD-80 (the flat-topped fast-repeat
   bug). This is the primary gate — the decay-model mask must suppress it.
3. KD-80 velocity scales cleanly soft→hard; single hit = single MIDI note (no double
   triggers at normal dynamics).
4. No `[WARN] sample overrun` at normal play.
5. `AdcSampler`/`SampleStream` unchanged; spike cancel + detection live only in Layer 3
   (+ the small spike-cancel helper).

## Report back
- Build status; whether DC-offset path G was needed (and which).
- Subjective KD-80 feel: does the runaway reproduce under hard hits? Double-triggers?
- Any Edrumulus single-sensor logic that was ambiguous to port (so we can verify before
  2b builds on it).
- Confirm the `processBlock` coordinate change is consistent across TriggerEngine, the
  engine, and main.
