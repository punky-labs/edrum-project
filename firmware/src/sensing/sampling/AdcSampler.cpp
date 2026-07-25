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
// At 8ch*8kHz = 64 kHz aggregate, one 4-byte conversion arrives every ~15.6 µs,
// so 1024 conversions (4096 bytes) ≈ 16 ms. The store buffer is the headroom the
// DMA has before pump() must drain it: if the main loop stalls longer than this,
// the buffer overflows and the sampler wedges (values freeze until reboot).
// Bumped 4096 -> 32768 (16 ms -> ~128 ms) so an unbracketed stall has generous
// margin; deliberate stalls (config save, LUT rebuild) are additionally bracketed
// by pause()/resume(). 32768 / 1024 = 32, so still a whole number of frames.
static constexpr uint32_t kConvFrameBytes = 1024;
static constexpr uint32_t kStoreBufBytes  = 32768;

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
    lastError_     = ESP_OK;
    lastErrorStep_ = "";
    if (numChannels == 0 || numChannels > kMaxChannels) {
        lastErrorStep_ = "numChannels range";
        return false;
    }

    // NOTE (2026-07-25): numChannels_/perChannelHz_ are NOT set here anymore —
    // moved to just before the final `return true` below. Previously they were
    // set this early and never rolled back on failure, so numChannels()/
    // sampleRateHz() (and thus the "[ADC] configured..." boot log) would report
    // the REQUESTED config even when begin() went on to fail — misleading, as seen
    // when begin() failed with 9ch but still logged "configured ... 9 ch".
    for (uint8_t i = 0; i < kAdcChanCount; i++) adcChanToSlot_[i] = 0xFF;

    adc_continuous_handle_cfg_t handleCfg = {};
    handleCfg.max_store_buf_size = kStoreBufBytes;
    handleCfg.conv_frame_size    = kConvFrameBytes;
    esp_err_t err = adc_continuous_new_handle(&handleCfg, &handle_);
    if (err != ESP_OK) {
        handle_ = nullptr;
        lastError_     = err;
        lastErrorStep_ = "new_handle";
        return false;
    }

    adc_digi_pattern_config_t pattern[kMaxChannels] = {};
    for (uint8_t i = 0; i < numChannels; i++) {
        adc_channel_t ch;
        if (!gpioToAdc1Channel(channelGpios[i], ch)) {
            lastError_     = ESP_ERR_INVALID_ARG;
            lastErrorStep_ = "gpio_map";
            stop();
            return false;
        }
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
    err = adc_continuous_config(handle_, &digCfg);
    if (err != ESP_OK) {
        lastError_     = err;
        lastErrorStep_ = "config";
        stop();
        return false;
    }

    err = adc_continuous_start(handle_);
    if (err != ESP_OK) {
        lastError_     = err;
        lastErrorStep_ = "start";
        stop();
        return false;
    }

    numChannels_  = numChannels;
    perChannelHz_ = perChannelHz;
    return true;
}

void AdcSampler::stop() {
    if (!handle_) return;
    adc_continuous_stop(handle_);
    adc_continuous_deinit(handle_);
    handle_ = nullptr;
}

void AdcSampler::pause() {
    if (!handle_) return;
    // Stop conversions but KEEP the handle/config — resume() restarts the same
    // driver. Much lighter than stop() (which deinits) so it's cheap to bracket.
    adc_continuous_stop(handle_);
}

void AdcSampler::resume() {
    if (!handle_) return;
    adc_continuous_start(handle_);
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
