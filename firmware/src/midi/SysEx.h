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

// ---------------------------------------------------------------------------

// Receive a raw SysEx payload (without leading F0 and trailing F7) and
// dispatch to the appropriate handler.
void sysexParse(const uint8_t* data, size_t len);

// Construct and transmit a SysEx response.
// Stub: prints to Serial until USB MIDI send is wired up.
void sysexSendResponse(uint8_t deviceId, uint8_t cmdHigh, uint8_t cmdLow,
                       const uint8_t* payload, size_t payloadLen);
