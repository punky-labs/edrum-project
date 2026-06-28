# eDrum Sensing Rewrite — Step 1: Implementation Plan & Architecture

**Status:** Plan (pre-implementation)
**Companion to:** `docs/sensing_rewrite_step0.md` (parameter mapping & decisions)
**Executes via:** staged Claude Code prompts (see §7)

This doc defines the *architecture* and *staging* for the PDrum2 sensing rewrite.
Step 0 defined the parameters and algorithm; this defines the code structure that
houses them and the order we build it.

---

## 1. Locked decisions (from discussion)

| Decision | Choice |
|----------|--------|
| Architecture | **Approach B** — 3 layers, sampling separated from sensing |
| Sampling | **`adc_continuous` DMA @ 8 kHz/ch** (measured feasible: 8ch→8011 Hz/ch) |
| Core model | **Single-core to start.** DMA decouples acquisition from loop timing. Dual-core deferred (and contained by the layering if ever needed) |
| Radio safety | Don't depend on per-core timing. Once ESP-NOW is live the radio contends a core; DMA + generous ring buffer makes sensing immune to that |
| Ring buffer | **Sized generously** (~50–100ms all-channel) for worst-case loop/radio stall. RAM is plentiful (S3) |
| PDrum (v1) | **Retired entirely.** No legacy to service — all dev. Clean replacement, no shim |
| Units | 0–31 at UI/SysEx; real units internal; convert in `initialize(Fs)` (Step 0 §1) |
| Runaway bug | **Designed out** by the decay-model retrigger mask — success criterion, not a separate patch |

---

## 2. Three-layer architecture

Dependencies point **downward only**. Layer 3 never includes ADC headers;
Layer 1 never mentions a drum. Layer 2 is the contract.

```
Layer 3  ENGINES (per jack)      PDrum2Trigger × NUM_INPUTS
         pure DSP: block in -> hit/choke/velocity out
              ▲ gapless per-channel sample blocks
Layer 2  SAMPLE STREAM            SampleStream (owns ring buffer, demux, cursors)
         serves engines + scope + learn-my-pad equally
              ▲ interleaved DMA frames
Layer 1  ADC SAMPLER              AdcSampler (owns adc_continuous, GPIO map, 8kHz)
         ONE instance. Hardware truth. Knows nothing about drums.
```

### Layer 1 — `AdcSampler` (`sensing/sampling/AdcSampler.{h,cpp}`)
Sole owner of the single ADC1 + DMA peripheral.
- `begin(const uint8_t* channelGpios, uint8_t numCh, uint32_t perChannelHz)`
- `stop()`
- `uint16_t readBlock(const adc_frame_t** out)` — newest completed DMA block,
  non-blocking, returns frame count (0 if nothing new)
- `numChannels()`, `sampleRateHz()`
- Owns: `adc_continuous_handle`, conversion-done callback, atten/width config,
  GPIO→ADC1-channel map, overrun detection.
- **The only file that knows** ADC2-is-forbidden, GPIO2=ADC1_CH1, etc.
- Swapping head unit (8ch) ↔ satellite (4ch) ↔ pin changes = **this file only**.

### Layer 2 — `SampleStream` (`sensing/sampling/SampleStream.{h,cpp}`)
Owns the ring buffer; pulls interleaved frames from AdcSampler; exposes per-channel
gapless views to any number of consumers, each with its own cursor.
- `begin(AdcSampler* src)`
- `pump()` — pull completed DMA blocks into ring buffer. **Only thing that advances
  the write head.** Called once per loop (single-core).
- `uint16_t read(uint8_t channel, Cursor& cursor, uint16_t* dst, uint16_t maxSamples)`
  — gapless per-consumer read; returns samples copied
- `uint16_t readWindow(uint8_t channel, uint32_t startIndex, uint16_t* dst, uint16_t count)`
  — full-fidelity window grab (**Step 0 §11 capture mechanism — scope & learn-my-pad**)
- `writeHead()`, `numChannels()`, `sampleRateHz()`
- `Cursor` = per-consumer read position + **overrun detection**: if the ring wraps
  past a cursor, report "lost N samples" so the engine resets filter state rather
  than processing garbage. (Producer/consumer discipline B requires.)
- **Scope and learn-my-pad are first-class consumers here**, equal to engines —
  not reaching through an engine. This is the payoff of B.

### Layer 3 — `PDrum2Trigger` (`sensing/pdrum2/PDrum2Trigger.{h,cpp}`)
Pure sensing. Receives a block of its channel's samples; runs Edrumulus-derived
detection. No knowledge of ADC/DMA/GPIO/where samples come from.
- `initialize(uint32_t sampleRateHz)` — build band-pass coeffs, decay LUTs, convert
  0–31 params → internal units (Step 0 §1). Fs-dependent work happens here.
- `processBlock(const uint16_t* headBlock, const uint16_t* rimBlock, uint16_t n)`
  — replaces the old per-sample `sensing(headVal, rimVal)`
- Existing result getters retained (`hasHit`, `getVelocity`, `hasChoke`, etc.)
- Houses: band-pass IIR → square (power domain) → 3-segment decay retrigger mask →
  clip/overload correction → first-peak vs max-peak → velocity curve LUT.

---

## 3. TriggerEngine interface change

The interface moves from per-sample push to block processing. This is the **only**
breaking change to `main_esp32s3.cpp`'s loop, and it's contained.

**Remove:** `virtual void sensing(int headVal, int rimVal, uint32_t ringHead)`
**Add:**
```cpp
virtual void initialize(uint32_t sampleRateHz) = 0;
virtual void processBlock(const uint16_t* headBlock,
                          const uint16_t* rimBlock, uint16_t n) = 0;
```
All result getters (`hasHit`, `getVelocity`, `hasChoke`, `clearChoke`, scope
`getTriggerSnap`, etc.) and all setters (`setHeadThreshold`, …) **retained as-is**.
New setters added for the Edrumulus params (see §4).

Since PDrum v1 is retired, no shim needed — `PDrumTrigger.{h,cpp}` and the
`sensing/pdrum/` dir are **deleted**.

---

## 4. Config struct extension (ripples into LittleFS + SysEx)

Current `InputConfig` (Config.h) already has the Stage-1 fields: `padType`,
`threshold`, `velocityCurve`, `headSensitivity`, `scanTime`, `maskTime`,
`rimRatioThreshold`, `chokeThreshold`, `chokeEnabled`. It does **not** have the
Edrumulus Tier-2 params. Those must be added.

**New fields needed** (Tier-2 / Advanced, real units stored — Step 0 §4 decision):
```
uint16_t preScanTimeMs;          // (×10 fixed-point or raw ms — pick in impl)
uint16_t firstPeakDiffThreshDb;  // dB ×10
uint16_t decayLen1Ms, decayGradFact1;
uint16_t decayLen2Ms, decayGradFact2;
uint16_t decayLen3Ms, decayGradFact3;
uint16_t decayFactDb;            // dB ×10
uint16_t maskTimeDecayFactDb;    // dB ×10
uint16_t decayEstDelayMs;
uint16_t clipCompAmpmapStep;     // ×100 fixed-point
```
(Exact fixed-point scaling decided in implementation; store real units, not 0–31.)

**Consequences (all expected, all in-scope):**
- **LittleFS blob size changes** → filesystem re-upload required (`uploadfs`);
  config resets to defaults. Known/correct (project_state.md notes this pattern).
- **SysEx** (`docs/sysex_spec.md`) → new param commands for Tier-2 fields; existing
  Tier-1 params that were 14-bit can shrink to 1 byte (Step 0 §1). **This is Step 2
  of the overall plan — NOT done in this Step-1 firmware pass.** For Step 1, the new
  fields get sensible compiled defaults so the engine runs; full SysEx/app plumbing
  follows in Step 2.

> **Staging note:** Step 1 (this doc) is the *firmware engine + sampling*. It needs
> the config fields to exist with defaults, but does NOT require the app/SysEx to
> expose them yet. Tune via serial `w` command + presets during Step 1; wire the app
> in Step 2.

---

## 5. main_esp32s3.cpp changes

Loop gets simpler. Sampling and detection both behind clean boundaries.

```cpp
// setup()
sampler.begin(kChannelGpios, 8, 8000);
stream.begin(&sampler);
for (i...) {
    triggers[i] = new PDrum2Trigger(kHeadCh[i], kRimCh[i]);
    triggers[i]->initialize(stream.sampleRateHz());
}
applyConfig();   // now also pushes Tier-2 setters

// loop()
stream.pump();                              // Layer 2: DMA → ring buffer
for (i...) {
    uint16_t headBuf[N], rimBuf[N];
    uint16_t n = stream.read(kHeadCh[i], headCursor[i], headBuf, N);
    stream.read(kRimCh[i], rimCursor[i], rimBuf, n);
    triggers[i]->processBlock(headBuf, rimBuf, n);
    // hit/rim/choke handling + MIDI + SysEx debug — UNCHANGED
}
```
**Removed:** `sampleADC()` (analogRead loop), the `[RATE]` temp block, the
`analogRead`-in-loop model.
**Scope tool:** `scopeDump()` changes from reading global `ringBuf[][]` to calling
`stream.readWindow()` — cleaner, and the ring buffer becomes Layer 2's private
property rather than a global.
**MIDI/SysEx/choke output paths:** untouched.

---

## 6. Pad-type handling inside processBlock

Edrumulus has no notion of your 3 pad types — grafting them onto its core is the
genuinely novel integration work.

- **SINGLE_PIEZO (KD-80):** band-pass→square→power detection on head only. No rim.
  *Simplest — build first.*
- **DUAL_PIEZO (PDX-8/12):** power detection on head; rim piezo also band-passed;
  ratio (`rimPeak*100/headPeak`) + first-peak-channel discrimination → head vs rim.
- **PIEZO_SWITCH_CHOKE (CY-5, PD-7, Lemon):** power detection on head; rim is the
  **switch monitor** — your existing sustained-signal choke logic (NOT band-passed;
  it's a switch, not a velocity sensor). Ports largely from current PDrum choke code.

So PDrum2's rim path is *part Edrumulus* (DUAL_PIEZO rim piezo) and *part existing
choke logic* (PIEZO_SWITCH_CHOKE switch). Keep choke logic conceptually separate
from the power-domain detection.

---

## 7. Staging sequence (one Claude Code pass per stage)

Bounded prompts, each validated on hardware before the next. Matches the
"finish before moving on" discipline and keeps each prompt reliable.

### Stage 1 — Pipeline skeleton + SINGLE_PIEZO, time-domain
**Goal:** prove DMA → SampleStream → PDrum2Trigger → MIDI end-to-end on the KD-80.
- Build Layer 1 (`AdcSampler`) + Layer 2 (`SampleStream`) + ring buffer.
- Change `TriggerEngine` interface (`initialize` + `processBlock`).
- `PDrum2Trigger` with **SINGLE_PIEZO only**, using a *simple* peak detector first
  (NOT the band-pass yet) — just to prove the plumbing and that blocks flow gaplessly.
- Wire `main` to the new loop. Delete `sampleADC()` + `[RATE]` block.
- **Success:** KD-80 triggers cleanly via the new pipeline; velocities sane; no
  dropped-sample warnings from the cursor at normal play.

### Stage 2 — The Edrumulus detection core (still SINGLE_PIEZO)
**Goal:** the real algorithm, proven on the simplest pad.
- Add band-pass IIR (8 kHz coeffs from Edrumulus) → square → power domain.
- Add 3-segment decay-model retrigger mask. **Runaway bug must not reproduce** —
  test with hard KD-80 hits per the project_state.md repro.
- Add clip/overload correction; first-peak vs max-peak.
- `initialize(Fs)` builds filters + converts params.
- **Success:** clean velocity across soft→hard; no runaway on hard hits; decay mask
  visibly suppresses retrigger (verify via scope window).

### Stage 3 — DUAL_PIEZO
- Run power detection on head + rim piezo; ratio + first-peak discrimination.
- **Success:** PDX-8 head vs rim discriminated reliably (per scope findings:
  10:1 hard head, 3.8:1 hard rim, soft-rim tiebreak via first-peak).

### Stage 4 — PIEZO_SWITCH_CHOKE
- Head = power detection; rim = switch monitor (existing choke logic ported).
- **Success:** CY-5 / PD-7 / Lemon hit velocity correct AND choke (note-off)
  detected on sustained switch; hit-induced switch transients ignored.

### Stage 5 — Cleanup + presets
- Delete `sensing/pdrum/`. Retire dead code.
- Seed presets from Step 0 §7 (PDX8, CY5, Lemon, KD8/PD8 cousins).
- **Success:** all four test jacks working; presets load; clean build both envs.

> Each stage = one Claude Code prompt, reviewed + flashed + tested before the next.
> Recommended model: **Opus** (subtle correctness rippling across files — DSP + DMA
> + interface change).

---

## 8. Success criteria (whole rewrite)

- DMA @ 8 kHz/ch, gapless, single-core, robust to loop stalls.
- Sampling (L1/L2) and sensing (L3) cleanly separable — algorithm can be rewritten
  without touching `AdcSampler.cpp`.
- All 3 pad types working on real test pads (KD-80, PDX-8, CY-5/PD-7/Lemon).
- **Hard-hit runaway designed out** by the decay mask (no `loopTimes` watchdog needed).
- Scope + (future) learn-my-pad consume Layer 2 via `readWindow()`.
- PDrum v1 fully removed.
- Both `[env:xiao_esp32s3_head]` and `[env:xiao_rp2040]` build clean — OR RP2040 env
  formally retired if it can't carry the new ADC model (decide in Stage 1; RP2040 has
  no `adc_continuous` equivalent, so it likely **drops to "legacy, unmaintained"**).

> **RP2040 note:** the new sampling layer is ESP32-S3 `adc_continuous`-specific. The
> RP2040 + MCP3008 path cannot run this engine (different ADC, ~2kHz). Per Step 0,
> RP2040 is superseded. Stage 1 should formally retire `[env:xiao_rp2040]` rather than
> try to keep it building against an interface it can't satisfy.

---

## 9. Open implementation details (decide during coding, not blocking)

1. Block size `N` for `processBlock` / ring buffer depth — size for worst-case loop
   latency under radio contention. Start ~50–100ms all-channel.
2. Fixed-point scaling for the new Tier-2 config fields (×10 / ×100) — pick per field.
3. Exact Edrumulus band-pass coefficients for 8 kHz — lift from his source; verify
   passband by scoping a known hit.
4. `pump()` placement — top of loop, single-core. Revisit only if dropped samples
   appear under load.
