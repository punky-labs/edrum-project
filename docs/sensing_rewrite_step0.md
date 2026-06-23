# eDrum Sensing Rewrite — Step 0: Parameter Mapping & Design Spec

**Status:** Design (no code yet)
**Purpose:** The reference spec that Steps 1–3 execute against. Defines the new
sensing engine's parameters, their tier (Simple / Advanced / Internal), how each
surfaces (UI / SysEx / preset-only), and how it maps to the current schema.

**Source of truth for the engine:** Edrumulus (Volker Fischer, GPL-2.0). We adopt its
detection core; we do **not** adopt its UI or full flat parameter list verbatim.

---

## 0. Sample rate — RESOLVED (measured on hardware)

**Decision: lock the entire system to 8 kHz/channel via ESP32-S3 continuous DMA.**

The whole system targets ESP32-S3 (wired head unit now; wireless bolt-on later). The
RP2040 + MCP3008 was only a development rig and is **superseded** — its ~2 kHz
free-running rate is NOT a design constraint.

Benchmark results (bare XIAO ESP32-S3, `adc_continuous`):

| Config | Requested | Delivered/ch | Aggregate |
|--------|-----------|--------------|-----------|
| 4ch (satellite) | 8 kHz | 8011 Hz | 32 kHz |
| 4ch (satellite) | 16 kHz | 16024 Hz | 64 kHz |
| 4ch (satellite) | 20 kHz | 20159 Hz | 81 kHz |
| 8ch (head unit) | 8 kHz | 8011 Hz | 64 kHz |

(Naive `analogRead()` one-shot capped at ~33 kHz aggregate → only 4 kHz/ch at 8ch.
Continuous DMA blows past that and is what the engine will use.)

**Consequences — all positive:**
- `SAMPLE_RATE_HZ = 8000` is a real, enforced, deterministic constant (DMA-clocked).
- 8 kHz matches Edrumulus's design point → **band-pass coefficients port directly,
  no filter redesign.**
- Head unit and satellite run the **identical engine at the identical rate** — no
  per-hardware variant, no per-Fs coefficient sets. The earlier "different regimes"
  concern is gone.
- Headroom exists (4ch→20kHz, 8ch≥8kHz) if oversampling is ever wanted.
- Engine must use **continuous/DMA sampling**, not an `analogRead()` loop. (One-shot
  and continuous cannot share ADC1 in one run — a relevant gotcha for integration.)

---

## 1. Design decisions (locked)

| # | Decision | Choice |
|---|----------|--------|
| 1 | Simple-mode control surface | Propose ideal set from Roland/DWe norms (not a 1:1 copy of current knobs) |
| 2 | Units | **0–31 integer at UI/SysEx layer; real physical units (ADC counts, ms, dB) stored & computed internally.** Mirrors Edrumulus (`Epadsettings` holds 0–31, `initialize()` converts) |
| 3 | Scan-time coupling | **Option A (Direct) — LOCKED** (see §6) |
| 4 | Sample rate | **8 kHz/ch, ESP32-S3 continuous DMA (see §0) — RESOLVED** |

### Why 0–31-at-edge / real-units-internal

- The **struct** holds user-friendly integers: `velocity_threshold` 0–31, etc.
- `initialize()` converts to the linear/dB internal values the DSP needs.
- UI and SysEx speak 0–31 (Roland-feel, fits one 7-bit byte, no 14-bit splitting for
  most params); firmware converts once at init.
- **Simplifies SysEx:** params currently 14-bit-split (`SENS_HI/LO`, `SCAN_HI/LO`,
  `MASK_HI/LO`) collapse to single bytes.

---

## 2. The three tiers

| Tier | Who sees it | Where it lives | Count |
|------|-------------|----------------|-------|
| **Tier 1 — Simple** | Every user, Simple mode | App UI sliders + SysEx + preset | 8 |
| **Tier 2 — Advanced** | Power users, Advanced/dev mode | App UI (Advanced panels) + SysEx + preset | ~12 |
| **Tier 3 — Internal** | Nobody (constants / derived) | Firmware constants or preset-only | remainder |

Principle: **the preset carries the full set; the UI exposes a slice.** Selecting
"Roland PDX-8" loads all Tier 1+2+3 values; Simple mode shows only the 8 Tier-1 knobs.
Adjusting one Tier-1 knob changes that one value; the rest keep preset-tuned values.
(Exactly how Roland modules behave — pad type loads a deep factory profile; front-panel
knobs adjust a few offsets on top.)

---

## 3. Tier 1 — Simple mode (the Roland/DWe control surface)

Eight controls, benchmarked against Roland TD front-panel + DWe Control. Five already
exist in `PDrum`; three are new.

| # | UI label | Internal param (Edrumulus) | UI range | Maps to current | SysEx today | Notes |
|---|----------|----------------------------|----------|-----------------|-------------|-------|
| 1 | **Threshold** | `velocity_threshold` | 0–31 | `headThreshold` | `02 02` → shrink to 1 byte | Min level to register |
| 2 | **Sensitivity** | `velocity_sensitivity` | 0–31 | `headSensitivity` | `02 0B` → 1 byte | Dynamic range |
| 3 | **Scan time** | `scan_time_ms` | 0–31 (→ms) | `scantime` | `02 0C` → 1 byte | Peak-search window; see §6 |
| 4 | **Retrigger / Mask** | `mask_time_ms` | 0–31 (→ms) | `masktime` | `02 0D` → 1 byte | Now backed by decay model |
| 5 | **Curve** | `curve_type` | enum 0–5 | `curvetype` | `02 03` | Keep existing 6-curve enum |
| 6 | **Rim sensitivity** | `rim_shot_threshold` | 0–31 | `rimRatioThreshold` (partial) | `02 0E` | Dual-zone only |
| 7 | **Rim/zone threshold** | `rim_shot_threshold` (paired) | 0–31 | — | `02 0F` | Dual-zone only |
| 8 | **Crosstalk** | `cancellation` | 0–31 | — (xtalk group only) | `02 05` (group) | NEW: cancellation amount |

**Curve mapping** (your LUTs and Edrumulus share the RyoKosaka HelloDrum lineage):

| Your curve | Edrumulus | `curve_param` |
|------------|-----------|---------------|
| 0 Natural | LINEAR | 1.018 |
| 1 Expressive | EXP1 | 1.018 × 1.012 |
| 2 Sensitive | EXP2 | 1.018 × 1.017 |
| 3 Punchy | LOG1 | 1.018 × 0.995 |
| 4 Aggressive | LOG2 | 1.018 × 0.987 |
| 5 Custom | (reserved) | — |

Keep the LUT approach in firmware (avoids runtime `pow()`). Curves are compatible; only
the *input* changes (filtered power instead of raw amplitude).

---

## 4. Tier 2 — Advanced mode (power-user tier)

Exposed only in Advanced mode. Most users never touch these; they sit at preset values.

| UI label | Internal param | UI range | Drives |
|----------|----------------|----------|--------|
| Pre-scan time | `pre_scan_time_ms` | 0–31 (→ms) | First-peak search before main scan |
| First-peak diff | `first_peak_diff_thresh_db` | 0–31 (→dB) | First vs max peak discrimination |
| Decay seg 1 length | `decay_len1_ms` | 0–31 (→ms) | Decay envelope segment 1 |
| Decay seg 1 gradient | `decay_grad_fact1` | 0–31 (scaled) | Decay slope 1 |
| Decay seg 2 length | `decay_len2_ms` | 0–31 (→ms, scaled) | Decay envelope segment 2 |
| Decay seg 2 gradient | `decay_grad_fact2` | 0–31 (scaled) | Decay slope 2 |
| Decay seg 3 length | `decay_len3_ms` | 0–31 (→ms, scaled) | Decay envelope segment 3 |
| Decay seg 3 gradient | `decay_grad_fact3` | 0–31 (scaled) | Decay slope 3 |
| Decay shift | `decay_fact_db` | 0–31 (→dB) | Vertical shift of decay curve |
| Mask decay factor | `mask_time_decay_fact_db` | 0–31 (→dB) | Loud-after-soft rescue during mask |
| Decay est delay | `decay_est_delay_ms` | 0–31 (→ms) | When adaptive power est starts |
| Clip comp step | `clip_comp_ampmap_step` | 0–31 (scaled) | Overload reconstruction aggressiveness |

> **Range-mapping — DECISION LOCKED:** wide-range Tier-2 params (e.g. `decay_len2_ms`
> 27–600ms; `decay_grad_fact2` 90–700) are **stored as real units in the preset**, not
> forced onto 0–31. A 0–31 presentation layer is applied **only to the few params that
> map cleanly**. Principle: simple for the user, honest (real units) for the engine.
> Most Advanced users never sweep these; presets set them directly in real units.

---

## 5. Tier 3 — Internal (never user-facing)

- Band-pass filter coefficients (`bp_filt_a/b`) — **fixed constants (8 kHz design)**
- Rim band-pass coefficients — fixed; selected by `rim_use_low_freq_bp` (preset bool)
- Positional-sensing filter design — **defer entirely (out of scope v1)**
- Buffer lengths — derived in `initialize()`
- `decay_est_len_ms`, `decay_est_fact_db` — adaptive-decay internals; preset-only
- ADC noise scaling — hardware constant
- `rim_shot_boost`, `rim_shot_window_len_ms` — preset-only
- All `pos_*` params — out of scope v1

---

## 6. Scan-time coupling decision (deferred — both options documented)

Scan time is both a Tier-1 knob *and* an input the decay/first-peak machinery consumes.

**Option A — Direct (recommended).** User value feeds the engine as-is; Edrumulus's
`sched_init()` recomputes derived values on any change.
- *Pro:* no hidden math; matches reference engine; adaptive decay self-corrects.
- *Con:* a user could set a scan time that interacts oddly with preset decay tuning.

**Option B — Offset (Roland "Scan +3").** User knob is an offset on the preset value.
- *Pro:* insulates preset tuning.
- *Con:* more abstraction; displayed value ≠ actual value.

**DECISION: Option A (Direct) — LOCKED.** Simpler, matches reference engine, adaptive
decay tolerates it. User's scan value feeds the engine as-is.

---

## 7. Seed preset data (for Step 3) — from `parameters.cpp`

Real internal values from Edrumulus for your test hardware.

### Direct matches

**Roland PDX-8** (`PDX8`):
```
velocity_threshold=6, velocity_sensitivity=4, rim_shot_threshold=14
scan_time_ms=2.5, mask_time_ms=6, rim_shot_is_used=true
pos_sense_is_used=true (DEFER — out of scope v1)
```

**Roland CY-5** (`CY5`, rim switch):
```
is_rim_switch=true, velocity_threshold=6, velocity_sensitivity=4
rim_shot_threshold=12, rim_shot_boost=0, scan_time_ms=3.0
mask_time_ms=8.0, decay_fact_db=3.0, rim_shot_is_used=true
```

**Lemon 13" / HHC** (`LEHHS12C`):
```
is_rim_switch=true, velocity_threshold=18, velocity_sensitivity=6
rim_shot_threshold=25, scan_time_ms=4.0, decay_fact_db=5.0
decay_len2_ms=600, decay_grad_fact2=100, rim_shot_is_used=true
```

### Close cousins (adapt)

**Roland PD-7 ← `PD8`** (rubber, dual, rim switch):
```
is_rim_switch=true, velocity_sensitivity=3, rim_shot_threshold=22
mask_time_ms=7, scan_time_ms=1.3, decay_est_delay_ms=6.0, decay_fact_db=5.0
decay_len2_ms=30/grad2=600, decay_len3_ms=150/grad3=120, clip_comp_ampmap_step=0.4
```

**Roland KD-80 ← `KD8`** (kick, single):
```
velocity_sensitivity=2, curve_type=LOG2, scan_time_ms=3.0
mask_time_decay_fact_db=10.0, decay_grad_fact2=450, decay_len3_ms=500/grad3=45
```

**Lemon 15" Ride ← `CY8`:**
```
is_rim_switch=true, velocity_threshold=10, velocity_sensitivity=5
rim_shot_threshold=10, curve_type=LOG2, scan_time_ms=6.0
decay: len1=10/grad1=10, len2=100/grad2=200, len3=450/grad3=30
```

### Global preset defaults
```
velocity_threshold=8, velocity_sensitivity=9, mask_time_ms=6
scan_time_ms=2.5, pre_scan_time_ms=2.5
first_peak_diff_thresh_db=8.0, mask_time_decay_fact_db=15.0
decay_est_delay_ms=7.0, decay_est_len_ms=4.0, decay_est_fact_db=16.0, decay_fact_db=1.0
decay_len1=0/grad1=200, decay_len2=350/grad2=200, decay_len3=0/grad3=200
clip_comp_ampmap_step=0.08, curve_type=LINEAR
```

---

## 8. Scope boundaries for v1

**IN:** band-pass + square → power-domain detection; modelled 3-segment decay
retrigger mask; sample-based timing; overload/clip correction; first-peak vs max-peak;
Tier 1 + Tier 2 surfacing; preset-driven full config.

**OUT (defer):** positional sensing (all `pos_*`, `MultiHeadSensor`); multi-head-sensor
coupled pads; second rim (ride bell); adaptive `decay_est` tuning UI.

---

## 9. Stack-crossing checklist for Step 2

1. **Firmware** — new config struct (~20 live params), 0–31→internal conversion,
   the engine itself (Step 1)
2. **SysEx** (`docs/sysex_spec.md`) — collapse 14-bit params to 1 byte; add Tier-2
   commands (new `02 1x` range); extend full config dump (`02 07`) + preset export
   (`04 06`) layouts
3. **Python app** — config model, Simple/Advanced UI panels, SysEx encode/decode
4. **Presets** (`app/presets.json`) — richer schema; migrate existing 7; seed from §7

---

## 10. Open decisions

1. ~~Scan coupling~~ — **RESOLVED, §6.** Option A (Direct).
2. ~~0–31 mapping for wide-range Tier-2 params~~ — **RESOLVED, §4.** Stored as real
   units in preset; 0–31 presentation only for params that map cleanly.
3. ~~Sample rate~~ — **RESOLVED, §0.** 8 kHz, ESP32-S3 continuous DMA.
4. ~~Deterministic sampling~~ — **RESOLVED.** Continuous DMA is inherently fixed-rate.
5. ~~MCP3008 throughput~~ — **MOOT.** RP2040+MCP3008 rig superseded by ESP32-S3.

**All Step-0 open decisions are now closed. Step 1 (engine) and Step 2 (schema) are
unblocked.** See §11 (raw capture architecture) and §12 (onboarding tiers) for the
product-shaping decisions that emerged from the dev-data / USB-coexistence discussion.

---

## 11. Raw ADC capture architecture (dev scope + Learn-my-pad foundation)

**Problem this replaces:** the "USB MIDI vs CDC serial coexistence" issue on ESP32-S3.
**Resolution: they never need to run simultaneously in the shipping product.** Raw
ADC data is *captured fast, transferred slow* over SysEx — no serial, one connection.

**Key insight (the unlock):** the dev/calibration workflow is "hit the pad, *then
stop and look*." There is **no real-time requirement.** So this is a capture-then-
transfer problem, not a streaming problem — which sidesteps SysEx's poor sustained
throughput entirely.

**Mechanism:**
1. Firmware continuously fills the ring buffer at 8 kHz (already does).
2. On a capture command (SysEx) or a triggered hit, freeze a window (e.g. ~200ms =
   ~1600 samples/ch; longer for full decay-tail analysis — RAM allows it on S3).
3. Once capture is complete and the hit is over, transfer the frozen buffer out
   **chunked over SysEx `05 04`** at a robust pace — slow is fine, chunk + ack +
   retransmit on error. Engine can be idle during transfer (no detect-while-sending).
4. Desktop app reassembles chunks and either **plots** (dev scope) or **analyses**
   (Learn-my-pad).

**Why this is clean:**
- Reuses the single USB MIDI connection; retires the MIDI/serial coexistence problem.
- SysEx is bad at streaming but fine at slow reliable blob transfer.
- Captured data is true 8 kHz fidelity (what the engine sees), regardless of transfer
  speed — decoupled capture rate vs transfer rate.
- The existing RP2040 scope logic (`g_scopeSnap`, pre/post window in
  `main_rp2040.cpp`) is the same mechanism — move it onto SysEx transport, not invent.

**Two consumers, one mechanism:**
- **Dev scope** (developer-only): plots raw waveforms to design presets by eye.
  Produces the seed data for `presets.json` (§7). Never shipped to basic users.
- **Learn-my-pad** (user-facing, see §12): runs statistics on the same captured
  windows to auto-derive parameters.

**Touches:** protocol (`05 04` chunking format + ack/retransmit), firmware (capture
buffer + transfer state machine), app (reassembly + plot/analyse panels).

### 11a. On-device reduction — per-transfer, NOT baked into capture

The captured buffer is always **full 8 kHz fidelity** (cheap — it's just the ring
buffer). How much data is *reduced before transfer* is a **parameter of the transfer
request, chosen per consumer** — never baked into the capture itself. This keeps one
dumb full-fidelity capture serving both consumers with different needs.

**Why reduce at all:** a PyQt plot is only a few hundred px wide — it cannot render
1600 points/ch meaningfully (they smear below line width). The viewer wants the *shape
vs time* (attack, peak, decay envelope), which ~100–300 points conveys better and
clearer than the raw firehose. Reducing on-device also shrinks the SysEx payload by
the decimation factor (e.g. 16:1 → 16× less data → the "slow dribble" becomes quick).

**Two traps to avoid — use the right reduction method:**
- **Naive decimation (every Nth sample) lies.** Without anti-aliasing, HF content
  aliases into the downsampled trace as phantom wiggles, and you can skip the true
  peak sample entirely. Never just pick every Nth.
- **Plain averaging hides the peak — and the peak IS the velocity.** Averaging each
  bin smears a sharp transient down, understating the strike height.
- **Use min/max-per-bin** (send both the min and max sample of each group — how audio
  editors draw zoomed-out waveforms): preserves visual extent (true peak height and
  swing) at a fraction of the points. For drum hits, **max-per-bin** is often the most
  honest single-value reduction since positive peak excursion is what matters.

**Reduction strategy per consumer:**
- **Dev scope request** → reduce on-device to ~200 min/max points before SysEx. Light
  payload, readable graph, quick transfer.
- **Learn-my-pad request** → send **full (or near-full) fidelity — do NOT smooth.**
  Smoothing destroys the very measurements calibration depends on:
  - *Noise floor* needs real sample-to-sample variation (averaging suppresses it →
    underestimate).
  - *True peak / clipping detection* needs actual max samples (averaging hides a
    railed flat-top → missed clipping).
  - *Decay-tail analysis* needs the genuine envelope (heavy smoothing distorts it).
  This transfer is heavier but rare and non-interactive, so the slow dribble is fine.

**Rule of thumb:** smoothing is for *looking*, never for *measuring*. The graph can be
smoothed; the calibration cannot.

**Shipping vs dev:**
- *Shipping product:* USB MIDI only. Config app talks SysEx over MIDI; live hit
  feedback rides `05 03` SysEx; raw capture (if used by Learn-my-pad) rides `05 04`.
  No serial needed.
- *Dev bench:* CDC-serial builds (the `xiao_adc_bench` env pattern) remain available
  for low-level characterisation but never ship.

---

## 12. Onboarding tiers (Learn-my-pad + preset picker)

Three entry points, all converging on the same end state (a tuned per-pad config),
matched to how much the user knows about their hardware. **Keeps the deep parameter
machinery invisible to basic users — it's either preset-loaded or auto-derived.**

1. **"I have a known pad"** → pick from list → load preset. *Most users.* Ships
   essentially for free once §7 presets exist. **Primary onboarding flow.**
2. **"My pad is like an X"** → pick closest match → load preset → fine-tune. Covers
   near-matches and "behaves like a PDX-12" cases.
3. **"Learn my pad"** → guided calibration → derived starting profile → fine-tune.
   For *unknown / DIY* pads with no preset — important given the maker-community GTM.

**Learn-my-pad flow (Feature 2):**
- Prompt "hit the head softly a few times" → capture noise floor + low end of range.
- Prompt "hit the head hard a few times" → capture peak range; detect clipping/railing.
- (Dual-zone) "hit the rim" → capture rim-vs-head ratio characteristics.
- Derive: threshold (just above noise floor), sensitivity (mapped to observed peak
  range), sensible scan/decay starting point.
- Auto-derive threshold/sensitivity/range first (~80% of value); decay characteristics
  are harder (need ring-tail analysis) — start from a default decay profile, refine
  later. Uses the §11 capture mechanism as its data source.

**Sequencing:** ship the **preset picker (tiers 1–2) first** — nearly free once presets
exist. Add **guided calibration (tier 3)** as a fast-follow. Both are product/UX
features layered on the sensing engine, not part of the core engine rewrite.

---

## Appendix — superseded RP2040 notes (historical)

The original prototype ran XIAO RP2040 + MCP3008, free-running at ~2 kHz/ch
(unregulated, measured). This was a development convenience, not the target. All
sample-rate constraints tied to it (MCP3008 SPI ceiling, per-Fs filter sets) are moot
now that the system targets ESP32-S3 at a locked 8 kHz. Retained only for context.
