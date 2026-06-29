#include "AdcSampler.h"

// ===========================================================================
// AdcSampler implementation — ESP-IDF v5 / arduino-esp32 3.x `adc_continuous`.
// ===========================================================================
//
// adc_continuous quirks captured here (see report-back in the Step-1 prompt):
//   - Result format on the ESP32-S3 is TYPE2 (SOC_ADC_DIGI_RESULT_BYTES == 4).
//     Each result is an adc_digi_output_data_t; we read .type2.channel/.type2.data.
//   - adc_continuous_read() out_len is always a multiple of the 4-byte result
//     size, but NOT necessarily a multiple of numChannels — a read can end mid
//     conversion-set. Demux (Layer 2) must tolerate that.
//   - The driver delivers results in the configured pattern order (slot 0,1,..).
//   - sample_freq_hz is the AGGREGATE rate (per-channel * numChannels).
// ---------------------------------------------------------------------------

// DMA store/frame sizing. conv_frame_size must be a multiple of the per-conv
// byte size (4 on S3); max_store_buf_size must be a multiple of conv_frame_size.
// 4096-byte store ≈ 1024 conversions ≈ 16 ms at 8ch*8kHz — ample headroom
// between pump() calls; SampleStream's ring carries the real 100 ms buffer.
static constexpr uint32_t kConvFrameBytes = 1024;
static constexpr uint32_t kStoreBufBytes  = 4096;

// ESP32-S3: ADC1_CHANNEL_n is GPIO(n+1) for GPIO1..GPIO10. This is the one place
// in the firmware that encodes the GPIO<->ADC channel relationship.
static bool gpioToAdc1Channel(uint8_t gpio, adc_channel_t& chOut) {
    if (gpio < 1 || gpio > 10) return false;   // ADC1 spans GPIO1..GPIO10 on S3
    chOut = (adc_channel_t)(gpio - 1);
    return true;
}

AdcSampler::~AdcSampler() {
    stop();
}

bool AdcSampler::begin(const uint8_t* channelGpios, uint8_t numChannels, uint32_t perChannelHz) {
    if (handle_) stop();
    if (numChannels == 0 || numChannels > kMaxChannels) return false;

    numChannels_  = numChannels;
    perChannelHz_ = perChannelHz;
    for (uint8_t i = 0; i < kAdcChanCount; i++) adcChanToSlot_[i] = 0xFF;

    adc_continuous_handle_cfg_t handleCfg = {};
    handleCfg.max_store_buf_size = kStoreBufBytes;
    handleCfg.conv_frame_size    = kConvFrameBytes;
    if (adc_continuous_new_handle(&handleCfg, &handle_) != ESP_OK) {
        handle_ = nullptr;
        return false;
    }

    adc_digi_pattern_config_t pattern[kMaxChannels] = {};
    for (uint8_t i = 0; i < numChannels; i++) {
        adc_channel_t ch;
        if (!gpioToAdc1Channel(channelGpios[i], ch)) { stop(); return false; }
        pattern[i].atten     = ADC_ATTEN_DB_12;
        // NOTE: do NOT mask to 3 bits (& 0x7). ESP32-S3 ADC1 has 10 channels
        // (0..9, GPIO1..GPIO10); 3 bits only covers 0..7, so & 0x7 collides
        // ch8->0 and ch9->1 (e.g. jack-3 rim on GPIO9/ch8 would corrupt jack-0).
        // The pattern .channel field is a full byte; assign the channel directly.
        pattern[i].channel   = (uint8_t)ch;
        pattern[i].unit      = ADC_UNIT_1;
        pattern[i].bit_width = ADC_BITWIDTH_12;
        adcChanToSlot_[ch]   = i;   // remember slot order for demux
    }

    adc_continuous_config_t digCfg = {};
    digCfg.sample_freq_hz = perChannelHz * (uint32_t)numChannels;  // aggregate
    digCfg.conv_mode      = ADC_CONV_SINGLE_UNIT_1;
    digCfg.format         = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
    digCfg.pattern_num    = numChannels;
    digCfg.adc_pattern    = pattern;
    if (adc_continuous_config(handle_, &digCfg) != ESP_OK) { stop(); return false; }

    if (adc_continuous_start(handle_) != ESP_OK) { stop(); return false; }
    return true;
}

void AdcSampler::stop() {
    if (!handle_) return;
    adc_continuous_stop(handle_);
    adc_continuous_deinit(handle_);
    handle_ = nullptr;
}

uint32_t AdcSampler::read(uint8_t* buf, uint32_t bufLenBytes) {
    if (!handle_ || bufLenBytes < kSampleBytes) return 0;
    // Round request down to a whole number of conversion results.
    uint32_t req = bufLenBytes - (bufLenBytes % kSampleBytes);
    uint32_t outLen = 0;
    esp_err_t err = adc_continuous_read(handle_, buf, req, &outLen, 0 /* non-blocking */);
    if (err != ESP_OK) return 0;   // ESP_ERR_TIMEOUT when nothing new
    return outLen;
}

bool AdcSampler::decode(const uint8_t* sample, uint8_t& slotOut, uint16_t& valueOut) const {
    const adc_digi_output_data_t* p = (const adc_digi_output_data_t*)sample;
    uint32_t chan = p->type2.channel;
    if (chan >= kAdcChanCount) return false;
    uint8_t slot = adcChanToSlot_[chan];
    if (slot >= numChannels_) return false;     // not one of our configured slots
    slotOut  = slot;
    valueOut = (uint16_t)p->type2.data;         // 12-bit (0..4095)
    return true;
}
