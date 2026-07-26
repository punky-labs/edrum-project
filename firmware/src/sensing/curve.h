#pragma once
#include <Arduino.h>

// Maps a raw ADC value through [threshold, sensRaw] -> [1,127] linearly,
// then reshapes via the selected curve (0=Natural...5=Custom, same enum
// as CURVE_NAMES in the app). Shared by PDrumTrigger (velocity curves) and
// HiHat (openness curve) so both stay byte-for-byte identical — no
// separate reimplementation to drift out of sync.
uint8_t applyCurve(int rawValue, int threshold, int sensRaw, uint8_t curveType);
