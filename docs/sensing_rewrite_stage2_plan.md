# eDrum Sensing Rewrite — Stage 2: The Edrumulus Detection Core

**Status:** Plan (pre-implementation)
**Companion to:** `sensing_rewrite_step0.md` (params/decisions),
`sensing_rewrite_step1_plan.md` (architecture), `project_state.md` (current state)
**Reference source (on Windows dev machine):**
`D:\Dev\E-drums\eDrumulus\edrumulus-src\edrumulus\`
- `edrumulus.cpp` — `Pad::initialize()` (~line 408+) and `Pad::process_sample()` (~line 612+)
- `edrumulus.h` — `Epadsettings`, `SSensor`, filter coefficients (the constants block)
- `edrumulus_parameters.cpp` — `apply_preset_pad_settings()` (exact per-pad values)

Stage 1 proved the pipeline (DMA→SampleStream→engine→MIDI) with a placeholder
detector. Stage 2 replaces that placeholder with the real power-domain engine —
**this is where the hard-hit runaway gets designed out and real tuning begins.**

---

## 0. HARDWARE CAVEAT — tuning on current boards is PROVISIONAL

The current test hardware does **not** have the RC anti-alias/clamp front-end (1k series
+ 22nF cap per channel) — those PCBs are in manufacturing. So Stage 2 is developed and
tuned against the **raw, unfiltered** front-end, which is noisier (more HF hash, more
ring, more spike potential reaching the ADC, possible aliasing above 4 kHz Nyquist).

Consequences baked into this plan:
- **Software spike cancellation stays IN** (see §1) — it stands in for the missing RC.
- **The band-pass detection filter does more work** and cannot undo pre-sampling
  aliasing — expect the current board to be twitchier than production.
- **Per-pad DSP tuning done now is PROVISIONAL.** The algorithm and code structure port
  unchanged, but tuned threshold/sensitivity/decay values are front-end-specific and
  will likely need re-validation on the RC boards. (This is exactly why Step 0 split
  "portable engine" from "hardware-specific preset values.")
- DC-offset reasoning is unaffected (RC was about HF, not DC).

---

## 1. What we port, and what we deliberately skip

The reference `process_sample()` is large, but most of its bulk is **out of scope**
(Step 0 §8). For a single head sensor pad, the active path is much smaller.

**PORT (the core robustness path, per head channel):**
1. **Band-pass IIR filter** → square → power domain. Coefficients `bp_filt_a/b` from
   `edrumulus.h` port **directly** (designed for 8 kHz = our locked Fs).
2. **3-segment decay model** retrigger mask: `x_filt_decay = x_filt - decay_curve`.
   The `decay[]` LUT built in `initialize()` from `decay_len1/2/3` + `decay_grad_fact1/2/3`.
   **This is the runaway fix.**
3. **Mask-time decay-factor rescue** (`mask_back_cnt`/`decay_mask_fact`) — lets a loud
   hit shortly after a soft hit still register.
4. **Scan / pre-scan + first-peak vs max-peak** separation (first peak for timing,
   max peak for velocity, `first_peak_diff_thresh`).
5. **Overload / clip correction** (`amplification_mapping`, the overload-history walk).
6. **Adaptive decay power estimation** (`decay_pow_est_*`) — tunes the decay scaling
   to the actual ring of this hit.
7. **Velocity curve**: `velocity_factor * pow(peak, velocity_exponent) + velocity_offset`,
   computed in `initialize()`. Our existing curve LUTs are the same lineage; either
   reuse the LUTs or port the continuous formula (prefer the formula — it's what the
   preset `curve_type` values expect).
8. **Software spike cancellation** — KEEP IN for current (RC-less) hardware. Port
   Edrumulus's `cancel_ADC_spikes` approach (level ~4 on ESP32). Re-evaluate once the
   RC boards arrive; it may become reducible/removable then.

**SKIP (defer / not needed):**
- **Positional sensing** — all `pos_*`, `lp_filt_*`, `x_low_hist`, the `get_pos_*`
  geometry. Out of scope v1.
- **Multiple head sensor coupling** — the `number_head_sensors > 1` path, the entire
  triangulation block, `coupled_pad_idx_*`. We have one head sensor per jack.
- **Second rim** (ride bell), `use_second_rim`, `x_sec_rim_switch_hist`.
- **DC offset estimation loop** — Edrumulus does a 10000-sample startup DC estimate
  (`dc_offset_est_len`). The band-pass filter has no DC gain, so DC bias is largely
  rejected by the filter itself — likely we can skip the explicit DC estimator.
  Validate on hardware (decide in Stage 2a).
- **Cross-talk cancellation** (the `cancel_*` block in `Edrumulus::process()`) — this
  is multi-pad (one pad's hit suppressing a neighbour's bleed). Useful eventually
  (it's our Tier-1 "Crosstalk" param) but defer to a later stage; single-pad testing
  doesn't need it.

---

## 2. Where the algorithm lives in our architecture

The reference bundles sampling + DSP in `Pad`. We keep our clean separation:
**all of the above goes inside `PDrum2Trigger::processBlock()` (Layer 3).** The engine
receives gapless `headBlock`/`rimBlock` from SampleStream and runs the per-sample loop
internally. No ADC/DMA knowledge enters Layer 3.

**Per-sample state** (the reference `SSensor` fields we need) becomes private members
of `PDrum2Trigger`: `bp_filt_hist_x/y`, `x_sq_hist` (FIFO), `overload_hist` (FIFO),
`decay_back_cnt`, `mask_back_cnt`, `scan_time_cnt`, `max_x_filt_val`,
`max_mask_x_filt_val`, `first_peak_val`, `peak_val`, `decay_scaling`,
`decay_pow_est_*`, `was_above_threshold`, `was_peak_found`, `is_overloaded_state`.

**Derived-at-init state** (from `initialize()`): `threshold`, `scan_time`,
`pre_scan_time`, `total_scan_time`, `mask_time`, `decay[]` LUT, `decay_len*`,
`decay_fact`, `decay_mask_fact`, `first_peak_diff_thresh`, `velocity_factor/exponent/offset`,
`amplification_mapping[]`, `decay_est_*`.

Reuse the reference helpers `update_fifo()` and the `FastWriteFIFO` class (port them
into the engine or a small shared header). `FastWriteFIFO` is a clean ring with
`operator[]` indexing — port verbatim.

**Constants:** `ADC_MAX_RANGE = 4096`, `ADC_MAX_NOISE_AMPL ≈ 8` (12-bit S3 ADC, same as
Teensy). `ADC_noise_peak_velocity_scaling = 1.0/6.0` (the ESP32 TODO value — start here,
tune on hardware). `x_filt_delay = 5`.

---

## 3. The coordinate fix (carry forward from Stage 1)

Stage 1 deferred a real bug: `getTriggerSnap()` returns a distance in the engine's
private `procIndex_` space, but `main` subtracts it from a SampleStream-absolute index
— different origins. **Stage 2 fixes this properly** since `processBlock` is being
rewritten anyway:

- Change the interface: `processBlock(head, rim, n, blockStartAbsIndex)` — pass the
  SampleStream-absolute index of the block's first sample.
- The engine indexes everything in absolute space; `getTriggerSnap()` returns a true
  absolute-relative distance the caller can map.
- This makes the scope window centre correctly **if/when** we revive it — but note the
  serial scope itself stays PARKED (USB coexistence). The coordinate fix is still worth
  doing now for correctness and for the future SysEx-capture path.

---

## 4. Config / parameter additions

The `InputConfig` struct (Config.h) currently lacks the Edrumulus Tier-2 params. Stage 2
adds them as real-unit fields (Step 0 §4 decision: real units stored, 0–31 only at UI).
Add to `InputConfig` (fixed-point scaling per field, chosen in impl):
```
preScanTimeMs, firstPeakDiffThreshDb, decayLen1Ms, decayGradFact1,
decayLen2Ms, decayGradFact2, decayLen3Ms, decayGradFact3, decayFactDb,
maskTimeDecayFactDb, decayEstDelayMs, decayEstLenMs, decayEstFactDb,
clipCompAmpmapStep
```
**Consequence:** LittleFS blob size changes → `uploadfs` required, config resets to
defaults (known pattern). New `TriggerEngine` setters for each. SysEx plumbing for these
is **Step 2 of the overall plan** — NOT required for Stage 2 firmware; compile sensible
defaults and tune via serial `w` + presets. (Serial `w` works fine; it's only the
scope's bidirectional-heavy use that hit the USB issue. Single short commands are OK.)

---

## 5. Staging (one Claude Code pass per stage, KD-80 first)

### Stage 2a — Band-pass + power domain + decay model + spike cancel (SINGLE_PIEZO, KD-80)
The heart of it. Port the head-channel detection path:
- `FastWriteFIFO` + `update_fifo` helpers.
- Software spike cancellation (current RC-less hardware needs it).
- Band-pass IIR (coeffs from edrumulus.h) → square → `x_filt`.
- `initialize()`: build `decay[]` LUT, compute `threshold`, scan/mask sample counts,
  velocity curve params, from the (real-unit) config.
- The threshold/scan/first-peak/max-peak state machine.
- 3-segment decay subtraction + mask-time rescue.
- Velocity via the curve formula.
- Fix the coordinate/`processBlock(..., blockStartAbsIndex)` interface.
- **DECIDE: DC offset** — try WITHOUT explicit DC estimation first (BPF rejects DC);
  add a light DC step only if the KD-80 baseline sits off-zero enough to matter.
- **Success:** KD-80 clean velocity soft→hard; **hard-hit runaway does NOT reproduce**
  (test per project_state.md repro); decay mask visibly suppresses retrigger.
- **Explicitly skip:** clip correction, adaptive decay-est, rim, choke (next stages).

### Stage 2b — Clip/overload correction + adaptive decay estimation (still KD-80)
- Overload history, the neighbour-walk clip compensation (`amplification_mapping`).
- `decay_pow_est_*` adaptive decay scaling.
- **Success:** very hard KD-80 hits keep dynamics (don't saturate flat); decay scaling
  adapts to actual ring (verify no late-tail retrigger on hard hits).

### Stage 2c — DUAL_PIEZO (PDX-8)
- Rim piezo band-passed (rim BPF coeffs), `rim_metric = rim_max_pow / peak_val`,
  `rim_shot_treshold` comparison → head vs rim. Port the non-switch rim path.
- **Success:** PDX-8 head vs rim discriminated reliably.

### Stage 2d — PIEZO_SWITCH_CHOKE (CY-5, PD-7, Lemon)
- Rim = switch monitor: `rim_switch_on_cnt`, the `>=2 neighbour` crosstalk guard,
  choke-on/choke-off via `rim_switch_on_cnt_thresh`. Port the `get_is_rim_switch()` path.
- **Success:** CY-5/PD-7/Lemon velocity correct AND choke (note-off) detected on
  sustained switch; hit-induced switch transients ignored.

### Stage 2e — Presets + cleanup
- Seed presets from `edrumulus_parameters.cpp` (exact values in §6) into `presets.json`
  schema (KD8, PDX8, CY5, PD8→PD-7, CY8→Lemon ride).
- **Success:** all four jacks working; presets load; clean build.

> Each stage: review → flash → test before the next. Model: **Opus** (DSP correctness
> rippling across files). Commit per stage.

---

## 6. Exact seed preset values (from edrumulus_parameters.cpp)

Real units, ready to populate Stage 2e. (Unlisted fields = global preset defaults below.)

**Global defaults (`apply_preset_pad_settings` base):**
```
velocity_threshold=8, velocity_sensitivity=9, mask_time_ms=6
rim_shot_treshold=12, rim_shot_boost=15, cancellation=0, curve=LINEAR
first_peak_diff_thresh_db=8.0, mask_time_decay_fact_db=15.0
scan_time_ms=2.5, pre_scan_time_ms=2.5
decay_est_delay_ms=7.0, decay_est_len_ms=4.0, decay_est_fact_db=16.0, decay_fact_db=1.0
decay_len1_ms=0/grad1=200, decay_len2_ms=350/grad2=200, decay_len3_ms=0/grad3=200
clip_comp_ampmap_step=0.08, rim_shot_window_len_ms=3.5
```

**KD8 (→ KD-80, kick, SINGLE_PIEZO):**
```
velocity_sensitivity=2, curve=LOG2, scan_time_ms=3.0
mask_time_decay_fact_db=10.0, decay_grad_fact2=450, decay_len3_ms=500, decay_grad_fact3=45
```

**PDX8 (→ PDX-8, DUAL_PIEZO):**
```
velocity_threshold=6, velocity_sensitivity=4, rim_shot_treshold=14
rim_shot_is_used=true  (pos_sense_is_used=true — IGNORE, out of scope)
```

**CY5 (→ CY-5, PIEZO_SWITCH_CHOKE):**
```
is_rim_switch=true, velocity_threshold=6, velocity_sensitivity=4
scan_time_ms=3.0, mask_time_ms=8.0, decay_fact_db=3.0, rim_shot_is_used=true
```

**PD8 (→ PD-7, PIEZO_SWITCH_CHOKE):**
```
is_rim_switch=true, velocity_sensitivity=3, rim_shot_treshold=16
mask_time_ms=7, scan_time_ms=1.3, decay_est_delay_ms=6.0, decay_fact_db=5.0
decay_len2_ms=30/grad2=600, decay_len3_ms=150/grad3=120, clip_comp_ampmap_step=0.4
rim_shot_is_used=true
```

**CY8 (→ Lemon 15" ride, PIEZO_SWITCH_CHOKE — closest cousin):**
```
is_rim_switch=true, velocity_threshold=13, velocity_sensitivity=8
rim_shot_treshold=30, curve=LOG2, scan_time_ms=6.0
decay_len1_ms=10/grad1=10, decay_len2_ms=100/grad2=200, decay_len3_ms=450/grad3=30
rim_shot_is_used=true
```

---

## 7. Success criteria (whole of Stage 2)

- Real Edrumulus power-domain detection running on all three pad types.
- **Hard-hit runaway designed out** by the decay mask — no watchdog band-aid.
- Clip correction keeps hard-hit dynamics; adaptive decay-est tames ring tails.
- Velocity tracks the curve formula; presets reproduce Edrumulus's tuned feel.
- `processBlock` coordinate fix landed (absolute block-start index).
- Sampling layer (L1/L2) untouched — proof the L2/L3 separation holds.
- Clean build of `[env:xiao_esp32s3_head]`.
- Tuning treated as PROVISIONAL pending RC-equipped boards (§0).

---

## 8. Deferred to later (post-Stage-2)

- Cross-talk cancellation (multi-pad, our "Crosstalk" Tier-1 param).
- Scope via SysEx capture (Step 0 §11) — replaces the parked serial scope.
- SysEx/app plumbing for the new Tier-2 params (Step 2 of overall plan).
- Hi-hat controller (the `process_control_sample` path — input 4).
- Positional sensing, multi-sensor, second rim.
- Re-validate/re-tune all per-pad DSP params on RC-equipped boards (§0).
