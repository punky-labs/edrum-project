#pragma once
#include <Arduino.h>
#include "esp_adc/adc_continuous.h"

// ===========================================================================
// AdcSampler — Layer 1 of the sensing pipeline (sampling/sensing rewrite Step 1)
// ===========================================================================
//
// Sole owner of ADC1 + the ESP-IDF `adc_continuous` DMA driver.
// Knows nothing about drums. This is the ONLY file in the firmware allowed to
// reference ADC / GPIO / DMA APIs.
//
// Responsibilities:
//   - configure adc_continuous for N channels at a requested per-channel rate
//     (12-bit width, ADC_ATTEN_DB_12)
//   - map the caller's GPIO numbers to ADC1 hardware channels and remember the
//     order they were configured in (the "slot" order)
//   - hand the newest completed DMA conversion bytes up to Layer 2, non-blocking
//   - decode one raw conversion result into (slot index, 12-bit value) so Layer 2
//     can demux without knowing anything about ADC channel numbering
//
// Layer 2 (SampleStream) owns the demux; Layer 1 just exposes enough for it.
// ---------------------------------------------------------------------------

class AdcSampler {
public:
    static constexpr uint8_t  kMaxChannels = 8;
    // Bytes per raw conversion result (SOC_ADC_DIGI_RESULT_BYTES == 4 on S3).
    static constexpr uint8_t  kSampleBytes = SOC_ADC_DIGI_RESULT_BYTES;
    // Number of ADC1 hardware channels. SOC_ADC_CHANNEL_NUM is a function-like
    // macro (SOC_ADC_CHANNEL_NUM(unit)); evaluate it once here.
    static constexpr uint8_t  kAdcChanCount = SOC_ADC_CHANNEL_NUM(0);

    AdcSampler() = default;
    ~AdcSampler();

    // Configure + start continuous sampling.
    //   channelGpios : GPIO numbers, in the order they become slots 0..numChannels-1
    //   numChannels  : 1..kMaxChannels
    //   perChannelHz : requested per-channel sample rate (aggregate = perCh * numCh)
    // Returns false if the driver rejected the config.
    bool begin(const uint8_t* channelGpios, uint8_t numChannels, uint32_t perChannelHz);

    // Stop + tear down the driver. Safe to call if never started.
    void stop();

    // Non-blocking: copy up to bufLenBytes of newly converted raw bytes into buf.
    // Returns the number of bytes written (0 if nothing new). The returned length
    // is always a multiple of kSampleBytes.
    uint32_t read(uint8_t* buf, uint32_t bufLenBytes);

    // Decode one raw conversion result (kSampleBytes long) into the slot index it
    // belongs to (0..numChannels-1) and its 12-bit value. Returns false if the
    // result's ADC channel is not one we configured (spurious / cross-unit).
    bool decode(const uint8_t* sample, uint8_t& slotOut, uint16_t& valueOut) const;

    uint8_t  numChannels()  const { return numChannels_; }
    uint32_t sampleRateHz() const { return perChannelHz_; }
    bool     isRunning()    const { return handle_ != nullptr; }

private:
    adc_continuous_handle_t handle_       = nullptr;
    uint8_t                 numChannels_  = 0;
    uint32_t                perChannelHz_ = 0;

    // Reverse map: ADC1 hardware channel -> configured slot index (0xFF = unused).
    uint8_t  adcChanToSlot_[kAdcChanCount] = {};
};
