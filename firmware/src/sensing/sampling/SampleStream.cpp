#include "SampleStream.h"

void SampleStream::begin(AdcSampler* src) {
    src_       = src;
    writeHead_ = 0;
    lastSlot_  = -1;
}

void SampleStream::pump() {
    if (!src_) return;

    const uint8_t numCh = src_->numChannels();
    const uint8_t sb    = AdcSampler::kSampleBytes;

    // Drain everything the driver has buffered. Read buffer matches the driver's
    // conv_frame_size so a single read pulls a full frame at a time.
    static uint8_t buf[1024];
    uint32_t bytes;
    while ((bytes = src_->read(buf, sizeof(buf))) > 0) {
        for (uint32_t off = 0; off + sb <= bytes; off += sb) {
            uint8_t  slot;
            uint16_t value;
            if (!src_->decode(&buf[off], slot, value)) continue;  // skip spurious

            // Frame-boundary detection: results arrive in pattern (slot) order
            // 0,1,..,numCh-1,0,1,... A slot that is not strictly greater than the
            // previous one means a new conversion-set started, so the frame we
            // were assembling is complete — commit it by advancing the head.
            // Robust to a read ending mid-set and to an occasional dropped sample.
            if (slot <= lastSlot_) {
                writeHead_++;
            }
            ring_[slot][writeHead_ & kMask] = value;
            lastSlot_ = slot;
        }
        (void)numCh;
        if (bytes < sizeof(buf)) break;   // partial read => driver drained
    }
}

uint16_t SampleStream::read(uint8_t channel, Cursor& cursor, uint16_t* dst, uint16_t maxSamples) {
    cursor.overran = false;
    if (channel >= numChannels()) return 0;

    const uint32_t head = writeHead_;     // completed frames are [oldest, head)
    if (!cursor.initialized) {            // first use: start at the live head
        cursor.pos         = head;
        cursor.initialized = true;
    }
    if (maxSamples == 0) return 0;        // (cursor still initialized for alignment)

    const uint32_t oldest = (head > kDepth) ? head - kDepth : 0;
    if (cursor.pos < oldest) {            // consumer fell behind -> ring wrapped
        cursor.pos     = oldest;
        cursor.overran = true;
    }
    if (cursor.pos > head) cursor.pos = head;   // safety (never expected)

    uint32_t avail = head - cursor.pos;
    uint16_t n = (avail > maxSamples) ? maxSamples : (uint16_t)avail;
    for (uint16_t i = 0; i < n; i++) {
        dst[i] = ring_[channel][(cursor.pos + i) & kMask];
    }
    cursor.pos += n;
    return n;
}

uint16_t SampleStream::readWindow(uint8_t channel, uint32_t startIndex,
                                  uint16_t* dst, uint16_t count) const {
    if (channel >= numChannels()) return 0;

    const uint32_t head   = writeHead_;
    const uint32_t oldest = (head > kDepth) ? head - kDepth : 0;
    uint16_t valid = 0;
    for (uint16_t i = 0; i < count; i++) {
        uint32_t idx = startIndex + i;
        if (idx >= oldest && idx < head) {
            dst[i] = ring_[channel][idx & kMask];
            valid++;
        } else {
            dst[i] = 0;                  // outside the live window
        }
    }
    return valid;
}
