#pragma once
#include <Arduino.h>
#include "AdcSampler.h"

// ===========================================================================
// SampleStream — Layer 2 of the sensing pipeline.
// ===========================================================================
//
// Owns the ring buffer (replaces the old global ring_buffer.h model). Pulls
// interleaved conversion results from AdcSampler, demuxes them into per-channel
// ring storage, and serves gapless per-channel reads to any number of
// independent consumers (engines, scope, future learn-my-pad), each with its
// own Cursor.
//
// Single-core discipline: pump() is the ONLY thing that advances the write head
// and it is called once per loop(), so no locking is needed.
// ---------------------------------------------------------------------------

class SampleStream {
public:
    static constexpr uint8_t  kMaxChannels = AdcSampler::kMaxChannels;
    // Per-channel depth. >= 100 ms of all-channel samples; power of two for
    // cheap modulo. 8192 frames @ 8 kHz ≈ 1.02 s of history.
    static constexpr uint32_t kDepth = 8192;
    static constexpr uint32_t kMask  = kDepth - 1;

    // Per-consumer read position + overrun detection.
    struct Cursor {
        uint32_t pos         = 0;      // absolute frame index of next sample to read
        bool     overran     = false;  // set by read() if the ring wrapped past pos
        bool     initialized = false;  // first read() snaps pos to the live write head
    };

    void begin(AdcSampler* src);

    // Pull all available conversion results from the AdcSampler into the ring
    // buffer. The only thing that advances the write head. Call once per loop().
    void pump();

    // Copy up to maxSamples new samples for `channel` since `cursor`, advancing
    // the cursor. Returns the count copied. If the consumer fell behind and the
    // ring wrapped past its cursor, the cursor is reset to the oldest valid
    // sample and cursor.overran is set (so the engine can reset its state).
    uint16_t read(uint8_t channel, Cursor& cursor, uint16_t* dst, uint16_t maxSamples);

    // Full-fidelity historical window grab (scope / learn-my-pad). Copies `count`
    // samples for `channel` starting at absolute frame index `startIndex`; entries
    // that fall outside the live ring window are written as 0. Returns the number
    // of in-window (valid) samples copied.
    uint16_t readWindow(uint8_t channel, uint32_t startIndex, uint16_t* dst, uint16_t count) const;

    uint32_t writeHead()    const { return writeHead_; }
    uint8_t  numChannels()  const { return src_ ? src_->numChannels() : 0; }
    uint32_t sampleRateHz() const { return src_ ? src_->sampleRateHz() : 0; }

private:
    AdcSampler* src_       = nullptr;
    uint32_t    writeHead_ = 0;     // count of completed frames (index being assembled)
    int16_t     lastSlot_  = -1;    // slot of the previous demuxed sample (frame-boundary detect)

    uint16_t    ring_[kMaxChannels][kDepth] = {};
};
