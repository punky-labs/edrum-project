# eDrum Debugging Method

**Audience:** Andrew + Claude (the whole dev team). This is *our* process, tuned to
how we actually work together — not a portable/generic guide. Portability is a future
concern.

**Why this exists:** A Stage 2a debugging session burned hours (and a lot of tokens)
on a phantom-hit bug whose real causes were (1) floating unplugged jacks acting as
antennas and (2) a stale pioarduino build cache silently flashing old binaries. Most
of the lost time came from **theorising from code-reading instead of observing the
actual system**, and from **changing code before confirming what was wrong**. This
doc is the discipline that prevents a repeat.

---

## The split: who does what

We have an unusual but powerful division of labour. Lean into it.

- **Andrew has the hardware and the eyes.** Only Andrew can flash, hit pads, read
  serial/telnet output, watch MidiView, observe the app. Andrew is the *instrument*.
- **Claude has the code and the reasoning.** Claude reads the codebase via MCP,
  reasons about cause, proposes the *smallest* change or the *next* observation.
- **The failure mode is Claude theorising in a vacuum** — reading code, forming a
  plausible story, changing something, and being wrong because the story was never
  checked against an observation only Andrew could make.

**Rule: Claude should bias toward "here's what to observe next" over "here's a fix to
try," until the cause is actually located.** A cheap observation from Andrew beats an
expensive guess from Claude almost every time.

---

## The loop (run this every time something breaks)

### 0. Confirm the binary is fresh — ALWAYS, FIRST
Before believing *any* test result: is the running binary actually the latest code?
- Check the **boot build stamp** (`[eDrum] Build stamp: <date> <time>`). It must be
  newer than the last edit.
- If behaviour is unchanged after a change, or weird/repeating: **full clean build**
  (`pio run -e <env> -t clean` then rebuild) BEFORE assuming the change was wrong.
- This is step zero because a stale binary makes *every* downstream conclusion false.
  It cost us hours once. Never skip it.

### 1. Reproduce — pin the exact trigger
- What is the precise, minimal action that causes it? ("hit pad" → which jack, which
  pad, how hard, how often.)
- Does it happen every time or intermittently? Intermittent → suspect timing, noise,
  hardware, or test-rig state, not logic.

### 2. Observe before theorising — instrument, don't guess
- **Get the actual data before forming a story.** The single highest-value habit.
- What is the real signal / message / value? Use the most direct observation available:
  - Raw ADC values (adc_diag firmware, MODE=1, clean serial).
  - Wire traffic (MidiView for MIDI/SysEx; app log `RX:`/`TX:` lines).
  - Boot output, error lines, measured rates.
- **Claude: if you're about to reason from code about what a value "must be," stop and
  ask Andrew to observe the actual value instead.** Code tells you what *should*
  happen; the instrument tells you what *does*.
- A blank/zero/garbage observation is itself decisive data (e.g. "graph always blank"
  meant the dump never fired — that ruled out a whole class of theory we'd been chasing).

### 3. Isolate — one variable at a time
- Narrow the problem to a single layer before changing anything.
- Prefer a **minimal standalone test** over poking the full system (the adc_diag
  firmware — sampler + serial only, no MIDI/detection/config — is the model: it proved
  the pipeline worked with everything else stripped away).
- Bisect the stack: is it the sampler? the demux? the engine? the transport? the app?
  Confirm each boundary before crossing it.
- **Change ONE thing per test.** If two things change and behaviour changes, you don't
  know which. We made this mistake; don't repeat it.

### 4. Locate the cause — state it explicitly
- Only after observing + isolating: state the actual cause in one sentence.
- **Distinguish "I observed this is the cause" from "I think this is the cause."**
  Claude should label confidence honestly. If it's still a hypothesis, the next step
  is another observation to confirm it — NOT a code change.
- Beware the test rig itself being the bug: floating jacks (electrical artifact) and
  stale cache (build artifact) were *both* test-rig problems, not code problems.

### 5. Fix — smallest change, then re-observe
- Make the *minimal* change that addresses the located cause.
- Re-run the observation from step 2 to confirm the fix actually worked — don't assume.
- **Don't fix more than one thing at once.** Don't "while I'm here" refactor.

### 6. Guard against regressions
- Did the fix break a working feature? (We re-broke the hit SysEx by gating it during
  anti-spam work.) Check the things near what you touched.
- If the fix revealed a constraint worth remembering, add it to project_state.md.

---

## Anti-patterns (the things that cost us)

- **Theorising from code across many turns without a single observation.** If Claude
  has proposed 2+ theories without Andrew observing anything new, STOP and instrument.
- **Changing code to test a hypothesis that an observation could test for free.**
- **Believing a test result without confirming the binary is fresh.**
- **Changing multiple things between tests.**
- **Treating the test rig as ground truth** (floating jacks, stale cache, a debug flag
  left on — `g_diagMode` suppressing all detection looked like "detection broken").
- **"While I'm here" scope creep** during a fix.
- **Ignoring the cheap signal** (the missing `[ADC] measured` line; the `raw=0`; the
  "serial RX wedges" — each was a loud clue dismissed too long).

---

## Token economy (a real constraint, since it's just us)

The expensive pattern is multi-turn theorising. The method *is* the token economy:
one good observation collapses several speculative turns into one. When in doubt,
the cheapest next move is almost always "Andrew, observe X" — not "Claude, read more
code and reason further."

---

## Quick checklist (the TL;DR to run)

1. Build stamp fresh? (clean build if unsure)
2. Reproduce: exact minimal trigger?
3. Observe the ACTUAL data (don't theorise yet)
4. Isolate to one layer (minimal test if possible)
5. State the cause — observed or hypothesis? (if hypothesis → observe again)
6. Smallest fix → re-observe to confirm
7. Check nothing nearby broke
