#pragma once
#include <stdint.h>

#define RING_BUF_SIZE 1024

extern uint16_t          ringBuf[8][RING_BUF_SIZE];
extern volatile uint32_t ringHead;

#ifdef ARDUINO_ARCH_RP2040
// ===========================================================================
// RP2040 — hardware spinlock implementation
// ===========================================================================
#include <hardware/sync.h>

extern spin_lock_t*      ringLock;

inline void ringBufInit() {
    uint lock_num = spin_lock_claim_unused(true);
    ringLock = spin_lock_init(lock_num);
    ringHead = 0;
}

inline void ringBufWrite(const uint16_t samples[8]) {
    uint32_t save = spin_lock_blocking(ringLock);
    uint32_t pos  = ringHead % RING_BUF_SIZE;
    for (int ch = 0; ch < 8; ch++) {
        ringBuf[ch][pos] = samples[ch];
    }
    ringHead++;
    spin_unlock(ringLock, save);
}

inline uint16_t ringBufRead(uint8_t channel, uint32_t index) {
    uint32_t save = spin_lock_blocking(ringLock);
    uint16_t val  = ringBuf[channel][index % RING_BUF_SIZE];
    spin_unlock(ringLock, save);
    return val;
}

#else
// ===========================================================================
// ESP32-S3 / FreeRTOS — portMUX critical section implementation
// ===========================================================================
#include <Arduino.h>

static portMUX_TYPE ringMux = portMUX_INITIALIZER_UNLOCKED;

inline void ringBufInit() {
    ringHead = 0;
}

inline void ringBufWrite(const uint16_t samples[8]) {
    taskENTER_CRITICAL(&ringMux);
    uint32_t pos  = ringHead % RING_BUF_SIZE;
    for (int ch = 0; ch < 8; ch++) {
        ringBuf[ch][pos] = samples[ch];
    }
    ringHead++;
    taskEXIT_CRITICAL(&ringMux);
}

inline uint16_t ringBufRead(uint8_t channel, uint32_t index) {
    taskENTER_CRITICAL(&ringMux);
    uint16_t val  = ringBuf[channel][index % RING_BUF_SIZE];
    taskEXIT_CRITICAL(&ringMux);
    return val;
}

#endif
