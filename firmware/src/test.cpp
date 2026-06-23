/*
 * test.cpp — ESP32-S3 bring-up / benchmark sketch
 *
 * Modes, selected by the defines below:
 *   BENCH_ADC_ONESHOT     -> analogRead() loop rate (Test 1)
 *   BENCH_ADC_CONTINUOUS  -> analogContinuous() DMA rate (Test 2)
 *   BENCH_MIDI            -> original USB-MIDI spike (preserved)
 *
 * IMPORTANT: one-shot and continuous CANNOT share ADC1 in the same run.
 * Calling analogRead() locks ADC1 into one-shot mode, after which
 * analogContinuous() aborts with "ADC1 is running in oneshot mode".
 * So the two ADC tests are SEPARATE builds — flash one, then the other.
 *
 * Build/run sequence for full picture:
 *   1) BENCH_ADC_ONESHOT active     -> pio run -e xiao_adc_bench -t upload, read
 *   2) BENCH_ADC_CONTINUOUS active  -> reflash, read
 *
 * Build with [env:xiao_adc_bench] (USB-CDC serial enabled).
 */

// ===== select ONE mode =====
// #define BENCH_ADC_ONESHOT
#define BENCH_ADC_CONTINUOUS
// #define BENCH_MIDI
// ============================


#if defined(BENCH_ADC_ONESHOT)
// ===========================================================================
//  TEST 1 — naive analogRead() loop. Counts full N-channel sweeps/sec.
//  Bare board, floating pins fine. ADC1 GPIOs 1-8.
// ===========================================================================
#include <Arduino.h>

static const uint8_t PINS8[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

static void benchAnalogRead(int nch) {
  Serial.printf("\n--- analogRead() loop, %d channels ---\n", nch);
  analogReadResolution(12);
  for (int i = 0; i < nch; i++) (void)analogRead(PINS8[i]);  // warm up

  const uint32_t test_ms = 1000;
  uint32_t sweeps = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < test_ms) {
    for (int ch = 0; ch < nch; ch++) {
      volatile int v = analogRead(PINS8[ch]);
      (void)v;
    }
    sweeps++;
  }
  uint32_t elapsed = millis() - t0;
  float per_ch = (float)sweeps * 1000.0f / (float)elapsed;
  Serial.printf("  per-channel rate:          %.0f Hz\n", per_ch);
  Serial.printf("  aggregate conversions/sec: %.0f Hz\n", per_ch * nch);
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("\n==== ADC ONE-SHOT (analogRead) Benchmark ====");
  benchAnalogRead(4);   // satellite config
  benchAnalogRead(8);   // head-unit config
  Serial.println("\n==== done ====");
}
void loop() {}


#elif defined(BENCH_ADC_CONTINUOUS)
// ===========================================================================
//  TEST 2 — analogContinuous() DMA mode. ONE configuration per boot.
//  Does NOT call analogRead() anywhere, so ADC1 stays free for continuous.
//
//  Edit CFG_NCH and CFG_HZ below to test a specific config, OR leave the
//  auto-sweep on (reboots itself through configs via a static index in NVS-less
//  RAM won't persist across reset, so we sweep within one boot using full
//  deinit between each — if a config aborts, reflash with a single fixed one).
// ===========================================================================
#include <Arduino.h>

static const uint8_t PINS8[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

volatile bool adc_conversion_done = false;
void ARDUINO_ISR_ATTR adcComplete() { adc_conversion_done = true; }

// Measure one continuous configuration cleanly. Returns delivered per-ch Hz.
static void benchContinuous(int nch, uint32_t requested_per_ch_hz) {
  Serial.printf("\n--- analogContinuous(), %d ch, request %lu Hz/ch ---\n",
                nch, (unsigned long)requested_per_ch_hz);

  uint8_t pins[8];
  for (int i = 0; i < nch; i++) pins[i] = PINS8[i];

  uint32_t requested_total = requested_per_ch_hz * (uint32_t)nch;

  analogContinuousSetWidth(12);
  analogContinuousSetAtten(ADC_11db);

  if (!analogContinuous(pins, nch, 1, requested_total, &adcComplete)) {
    Serial.println("  setup FAILED (rate out of range or driver state)");
    return;
  }
  analogContinuousStart();

  adc_continuous_data_t* result = nullptr;
  uint32_t total_samples = 0;
  const uint32_t test_ms = 1000;
  uint32_t t0 = millis();
  while (millis() - t0 < test_ms) {
    if (adc_conversion_done) {
      adc_conversion_done = false;
      if (analogContinuousRead(&result, 0)) total_samples += nch;
    }
  }
  uint32_t elapsed = millis() - t0;
  analogContinuousStop();
  analogContinuousDeinit();
  delay(50);  // let driver fully tear down before next config

  float per_ch = (float)total_samples * 1000.0f / (float)elapsed / (float)nch;
  Serial.printf("  delivered per-channel rate: %.0f Hz\n", per_ch);
  Serial.printf("  delivered aggregate:        %.0f Hz\n", per_ch * nch);
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("\n==== ADC CONTINUOUS (DMA) Benchmark ====");
  Serial.println(" NOTE: no analogRead() called — ADC1 free for continuous");

  // 4-channel (satellite)
  benchContinuous(4, 4000);
  benchContinuous(4, 8000);
  benchContinuous(4, 16000);
  benchContinuous(4, 20000);
  // 8-channel (head unit)
  benchContinuous(8, 4000);
  benchContinuous(8, 8000);

  Serial.println("\n==== done ====");
  Serial.println(" If configs after the first ABORT, the driver isn't fully");
  Serial.println(" resetting between runs — reflash testing ONE config:");
  Serial.println(" set a single benchContinuous() call in setup().");
}
void loop() {}


#elif defined(BENCH_MIDI)
// ===========================================================================
//  ORIGINAL USB-MIDI SPIKE (preserved). Build with [env:xiao_test].
// ===========================================================================
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

uint8_t current_note = 60;

void handleNoteOn(byte channel, byte pitch, byte velocity) {}
void handleNoteOff(byte channel, byte pitch, byte velocity) {}

void setup() {
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }
  usb_midi.setStringDescriptor("eDrum S3 MIDI");
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.turnThruOff();
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
}

void loop() {
  MIDI.read();
  MIDI.sendNoteOn(current_note, 127, 1);
  delay(500);
  MIDI.sendNoteOff(current_note, 0, 1);
  delay(500);
  current_note++;
  if (current_note > 72) current_note = 60;
}

#else
  #error "Select a mode at top of test.cpp"
#endif
