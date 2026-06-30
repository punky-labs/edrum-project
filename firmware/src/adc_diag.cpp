// ===========================================================================
// adc_diag.cpp — minimal ADC diagnostic firmware (sensing rewrite bring-up)
// ===========================================================================
//
// PURPOSE: a deliberately minimal firmware to observe the RAW ADC signal with a
// KNOWN-GOOD, reliable bidirectional serial link. NO USB MIDI, NO detection, NO
// config, NO SysEx. Just: sample the ADC and print values.
//
// WHY: the main head firmware uses ARDUINO_USB_MODE=0 (TinyUSB owns USB for MIDI),
// under which the USB-CDC serial RX path is unreliable and wedges. This firmware
// uses the SAME USB config as the working `xiao_adc_bench` env
// (ARDUINO_USB_MODE=1 + CDC_ON_BOOT=1) — native USB-CDC serial, no MIDI — so the
// serial monitor is rock-solid in BOTH directions.
//
// BUILD: env [env:xiao_adc_diag]. Flash, open monitor @115200. No input needed —
// it auto-streams. Optional single-key commands (RX works in this config):
//   space = pause/resume stream     r = print raw decode debug for 1 second
//
// INCREMENTAL BRING-UP — this is STAGE D1:
//   D1 (this): AdcSampler (Layer 1) ONLY. Read raw bytes, decode, print per-slot
//              values. Proves the DMA sampler + serial both work in isolation.
//   D2 (next): add SampleStream (Layer 2), print via gapless reads. Proves demux.
//   D3 (next): add band-pass filter only, print x_filt. Proves the DSP front-end.
//   ...each layer confirmed before the next is added.
// ===========================================================================

#include <Arduino.h>
#include "sensing/sampling/AdcSampler.h"

// All 8 ADC1 channels (head unit), in frame order — same map as the head firmware.
// SampleStream slot index == position here; KD-80 head = GPIO6 = slot 4.
static const uint8_t kChannelGpios[8] = { 2, 3, 4, 5, 6, 7, 8, 9 };
static const uint8_t kNumCh = 8;

static AdcSampler sampler;

// Latest decoded value per slot (updated as raw results stream in).
static uint16_t latest[AdcSampler::kMaxChannels] = {0};

static bool     g_paused   = false;
static uint32_t g_frames   = 0;   // completed conversion-sets seen (rough)

void setup() {
    Serial.begin(115200);
    // With CDC_ON_BOOT=1 the port enumerates quickly, but give a moment so the
    // first prints aren't lost before the monitor attaches.
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0 < 3000)) { delay(10); }
    delay(500);

    Serial.println();
    Serial.println("=====================================================");
    Serial.println("[adc_diag] D1 — AdcSampler (Layer 1) raw read test");
    Serial.println("[adc_diag] USB-CDC serial (MODE=1), no MIDI, no detection");
    Serial.println("=====================================================");

    if (!sampler.begin(kChannelGpios, kNumCh, 8000)) {
        Serial.println("[adc_diag] ERROR: sampler.begin() FAILED");
    } else {
        Serial.printf("[adc_diag] sampler started: %u ch @ %lu Hz/ch (%lu Hz agg)\n",
                      sampler.numChannels(),
                      (unsigned long)sampler.sampleRateHz(),
                      (unsigned long)sampler.sampleRateHz() * sampler.numChannels());
        Serial.printf("[adc_diag] kSampleBytes=%u kAdcChanCount=%u\n",
                      (unsigned)AdcSampler::kSampleBytes,
                      (unsigned)AdcSampler::kAdcChanCount);
    }
    Serial.println("[adc_diag] streaming per-slot latest values every 100ms...");
    Serial.println("[adc_diag] columns: slot0..slot7  (KD-80 head = slot4, GPIO6)");
    Serial.println();
}

void loop() {
    // --- Drain raw conversion bytes from Layer 1, decode, track latest per slot ---
    static uint8_t buf[1024];
    uint32_t bytes = sampler.read(buf, sizeof(buf));
    const uint8_t sb = AdcSampler::kSampleBytes;
    for (uint32_t off = 0; off + sb <= bytes; off += sb) {
        uint8_t  slot;
        uint16_t value;
        if (sampler.decode(&buf[off], slot, value)) {
            if (slot < AdcSampler::kMaxChannels) latest[slot] = value;
            if (slot == 0) g_frames++;   // rough conversion-set counter
        }
    }

    // --- Optional single-key control (RX is reliable in this USB config) ---
    if (Serial.available()) {
        char c = Serial.read();
        if (c == ' ') {
            g_paused = !g_paused;
            Serial.println(g_paused ? "[adc_diag] PAUSED" : "[adc_diag] RESUMED");
        }
    }

    // --- Print latest per-slot values at 100ms cadence ---
    static uint32_t lastPrint = 0;
    if (!g_paused && millis() - lastPrint >= 100) {
        lastPrint = millis();
        Serial.printf("[ADC] %4u %4u %4u %4u %4u %4u %4u %4u   (frames~%lu)\n",
            latest[0], latest[1], latest[2], latest[3],
            latest[4], latest[5], latest[6], latest[7],
            (unsigned long)g_frames);
    }
}
