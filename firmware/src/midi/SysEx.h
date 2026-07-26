#pragma once
#include <Arduino.h>

// SysEx framing
#define SYSEX_MFR_0      0x00
#define SYSEX_MFR_1      0x7D
#define SYSEX_DEV_HEAD   0x00   // head unit device ID
#define SYSEX_HEADER_LEN 5      // MFR0 MFR1 DEV_ID CMD_HI CMD_LO

// Firmware version reported in identify response
#define FW_VER_MAJ 0
#define FW_VER_MIN 1

// Category bytes
#define SYSEX_CAT_SYS    0x01
#define SYSEX_CAT_PAD    0x02
#define SYSEX_CAT_MIDI   0x03
#define SYSEX_CAT_PRESET 0x04
#define SYSEX_CAT_STATUS 0x05

// Category 01 — System
#define SYSEX_SYS_PING        0x01
#define SYSEX_SYS_PONG        0x02
#define SYSEX_SYS_IDENT_REQ   0x03
#define SYSEX_SYS_IDENT_RESP  0x04
#define SYSEX_SYS_RESET       0x05
#define SYSEX_SYS_SAVE        0x06
#define SYSEX_SYS_ACK         0x07

// Category 02 — Pad config
#define SYSEX_PAD_SET_TYPE    0x01
#define SYSEX_PAD_SET_THRESH  0x02
#define SYSEX_PAD_SET_CURVE   0x03
#define SYSEX_PAD_SET_RETRIG  0x04
#define SYSEX_PAD_SET_XTALK   0x05
#define SYSEX_PAD_GET         0x06
#define SYSEX_PAD_RESP        0x07
#define SYSEX_PAD_LINK        0x08
#define SYSEX_PAD_UNLINK      0x09
#define SYSEX_PAD_GET_STATUS  0x0A
#define SYSEX_PAD_SET_SENS        0x0B
#define SYSEX_PAD_SET_SCAN        0x0C
#define SYSEX_PAD_SET_MASK        0x0D
#define SYSEX_PAD_SET_RIM_SENS    0x0E   // repurposed: sets rimRatioThreshold (DUAL_PIEZO classify gate)
#define SYSEX_PAD_SET_RIM_THRESH  0x0F   // repurposed: sets chokeThreshold (PIEZO_SWITCH_CHOKE)
#define SYSEX_PAD_SET_CHOKE_EN    0x10   // chokeEnabled — byte already used by the app; handler added 2026-07

// ---- Added 2026-07: Secondary Trigger Behaviours v1 + Scan v3 tunables.
// These fields have existed in InputConfig/firmware since 2026-07-12 (telnet-`w`
// only until now) — this is the first SysEx exposure. See project_state.md,
// "SysEx Extension — Secondary Trigger Behaviours v1 + Scan v3" for the design.
#define SYSEX_PAD_SET_SCAN_MARGIN   0x11  // scanMargin (raw ADC), 14-bit
#define SYSEX_PAD_SET_SETTLE_WAIT   0x12  // settleWaitMs, 14-bit
#define SYSEX_PAD_SET_EMA_ALPHA     0x13  // emaAlpha (0-100), 1 byte
#define SYSEX_PAD_SET_RIM_GATE      0x14  // rimThreshold (raw ADC) — DUAL_PIEZO rim's own fire gate.
                                          // NOT the same field as SYSEX_PAD_SET_RIM_SENS (0x0E, which
                                          // is rimRatioThreshold) — deliberately distinct name to avoid
                                          // repeating the 0x0E/0x0F naming collision.
#define SYSEX_PAD_SET_RIM_SCALE     0x15  // rimSensitivity (raw ADC) — DUAL_PIEZO rim's own scaling bound
#define SYSEX_PAD_SET_RIM_CURVE     0x16  // rimCurve (enum, same values as velocityCurve), 1 byte
#define SYSEX_PAD_SET_XSTICK_NOTE   0x17  // crossStickNote (MIDI note), 1 byte
#define SYSEX_PAD_SET_XSTICK_CUTOFF 0x18  // crossStickCutoff — MIDI VELOCITY units (0-127), NOT raw ADC
#define SYSEX_PAD_SET_ALT_NOTE      0x19  // alternateNote (MIDI note), 1 byte
#define SYSEX_PAD_SET_ALT_MIN_VEL   0x1A  // minAltNoteVelocity (raw ADC), 14-bit
#define SYSEX_PAD_SET_CHOKE_HOLD    0x1B  // chokeHoldMs, 14-bit
#define SYSEX_PAD_SET_CHOKE_GRACE   0x1C  // chokeReleaseGraceMs, 14-bit
#define SYSEX_PAD_GET_EXT           0x1D  // [INPUT_ID] -> bundled GET for all 12 fields above
#define SYSEX_PAD_RESP_EXT          0x1E  // response payload, see SysEx.cpp for byte layout

// Category 03 — MIDI mapping
#define SYSEX_MIDI_SET_NOTE  0x01
#define SYSEX_MIDI_SET_Z2    0x02
#define SYSEX_MIDI_SET_CC    0x03
#define SYSEX_MIDI_GET       0x04
#define SYSEX_MIDI_RESP      0x05

// Category 04 — Preset management
#define SYSEX_PRE_LOAD    0x01
#define SYSEX_PRE_SAVE    0x02
#define SYSEX_PRE_LIST    0x03
#define SYSEX_PRE_LIST_R  0x04
#define SYSEX_PRE_DELETE  0x05
#define SYSEX_PRE_EXPORT  0x06

// Category 05 — Status / response
#define SYSEX_STAT_ACK       0x01
#define SYSEX_STAT_INP_ERR   0x02
#define SYSEX_STAT_HIT_DEBUG 0x03
#define SYSEX_STAT_HIHAT_DEBUG 0x04   // mirrors SYSEX_STAT_HIT_DEBUG (05 03) but for
                                       // continuous hi-hat position, not discrete hits

// Generic ack status values (payload byte 2 of 05 01)
#define SYSEX_ACK_OK      0x00
#define SYSEX_ACK_ERROR   0x01
#define SYSEX_ACK_UNKNOWN 0x02

// Input status values returned by 02 0A
#define SYSEX_INPUT_AVAIL    0x00
#define SYSEX_INPUT_ACTIVE   0x01
#define SYSEX_INPUT_RESERVED 0x02

// SysEx-safe sentinel for linkedInput == 0xFF (no link)
#define SYSEX_LINKED_NONE 0x7F

// Zone values used in 05 03 hit event
#define SYSEX_ZONE_HEAD 0x00
#define SYSEX_ZONE_RIM  0x01

// ---------------------------------------------------------------------------

// Receive a raw SysEx payload (without leading F0 and trailing F7) and
// dispatch to the appropriate handler.
void sysexParse(const uint8_t* data, size_t len);

// Construct and transmit a SysEx response.
// Stub: prints to Serial until USB MIDI send is wired up.
void sysexSendResponse(uint8_t deviceId, uint8_t cmdHigh, uint8_t cmdLow,
                       const uint8_t* payload, size_t payloadLen);


extern volatile bool g_save_requested;
extern volatile bool g_apply_requested;

// Pause/resume ADC DMA sampling around blocking LittleFS I/O in the preset
// handlers (defined in main_esp32s3.cpp). Prevents a filesystem-write stall from
// overflowing the ADC store buffer and wedging the sampler.
extern void adcSamplerPause();
extern void adcSamplerResume();
