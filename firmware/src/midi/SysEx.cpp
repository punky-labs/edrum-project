#include "SysEx.h"
#include "BleMidi.h"
#include "../config/Config.h"
#include "../dev/DevLog.h"   // debug prints route through DevLog (telnet, dev-build)
#include <string.h>

// ---- 7-bit encode / decode -------------------------------------------------

static inline uint16_t decode14(uint8_t hi, uint8_t lo) {
    return ((uint16_t)(hi & 0x7F) << 7) | (lo & 0x7F);
}

static inline void encode14(uint16_t v, uint8_t* hi, uint8_t* lo) {
    *hi = (v >> 7) & 0x7F;
    *lo = v & 0x7F;
}

// ---- response helpers ------------------------------------------------------

void sysexSendResponse(uint8_t deviceId, uint8_t cmdHigh, uint8_t cmdLow,
                       const uint8_t* payload, size_t payloadLen) {
    // Framed message: F0 MFR0 MFR1 DEV CMD_HI CMD_LO [payload] F7
    // Max payload is the list-presets response: 1 + 16*(1+1+16) = 289 bytes -> 296 total
    const size_t msgLen = 7 + payloadLen;
    uint8_t buf[320];
    if (msgLen > sizeof(buf)) {
        if (!g_serialQuiet) DevLog.printf("[SysEx TX] ERROR: payload too large (%u bytes)\n", (unsigned)payloadLen);
        return;
    }
    buf[0] = 0xF0;
    buf[1] = SYSEX_MFR_0;
    buf[2] = SYSEX_MFR_1;
    buf[3] = deviceId;
    buf[4] = cmdHigh;
    buf[5] = cmdLow;
    if (payloadLen > 0 && payload != nullptr) {
        memcpy(buf + 6, payload, payloadLen);
    }
    buf[6 + payloadLen] = 0xF7;

    bleMidiSendSysEx(buf, msgLen);

    // Debug log (keep alongside USB send for now)
    if (!g_serialQuiet) {
        DevLog.printf("[SysEx TX] F0 00 7D %02X %02X %02X", deviceId, cmdHigh, cmdLow);
        for (size_t i = 0; i < payloadLen; i++) {
            DevLog.printf(" %02X", payload[i]);
        }
        DevLog.println(" F7");
    }
}

static void sendAck(uint8_t deviceId, uint8_t cmdHigh, uint8_t cmdLow, uint8_t status) {
    uint8_t buf[3] = { cmdHigh, cmdLow, status };
    sysexSendResponse(deviceId, SYSEX_CAT_STATUS, SYSEX_STAT_ACK, buf, 3);
}

// ---- category 01 — system --------------------------------------------------

static void handleSystem(uint8_t deviceId, uint8_t cmd,
                         const uint8_t* p, size_t pLen) {
    (void)pLen;
    switch (cmd) {
        case SYSEX_SYS_PING:
            sysexSendResponse(deviceId, SYSEX_CAT_SYS, SYSEX_SYS_PONG, nullptr, 0);
            break;

        case SYSEX_SYS_IDENT_REQ: {
            uint8_t buf[4] = { FW_VER_MAJ, FW_VER_MIN, SYSEX_DEV_HEAD, NUM_INPUTS };
            sysexSendResponse(deviceId, SYSEX_CAT_SYS, SYSEX_SYS_IDENT_RESP, buf, 4);
            break;
        }

        case SYSEX_SYS_RESET:
            configResetDefaults();
            sendAck(deviceId, SYSEX_CAT_SYS, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_SYS_SAVE:
            g_save_requested = true;
            // Ack sent after write completes in loop()
            break;

        default:
            sendAck(deviceId, SYSEX_CAT_SYS, cmd, SYSEX_ACK_UNKNOWN);
            break;
    }
}

// ---- category 02 — pad config ----------------------------------------------

static void handlePad(uint8_t deviceId, uint8_t cmd,
                      const uint8_t* p, size_t pLen) {
    switch (cmd) {
        case SYSEX_PAD_SET_TYPE:
            if (pLen < 2 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].padType = p[1];
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_THRESH:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].threshold = decode14(p[1], p[2]);
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_CURVE:
            if (pLen < 2 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].velocityCurve = p[1];
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_RETRIG:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].retriggerTime = decode14(p[1], p[2]);
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_XTALK:
            if (pLen < 2 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].crosstalkGroup = p[1];
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_GET: {
            if (pLen < 1 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            const InputConfig& c = g_inputs[p[0]];
            uint8_t thresh_hi, thresh_lo, retrig_hi, retrig_lo;
            uint8_t sens_hi, sens_lo, scan_hi, scan_lo, mask_hi, mask_lo;
            uint8_t rsens_hi, rsens_lo, rthresh_hi, rthresh_lo;
            encode14(c.threshold,       &thresh_hi,  &thresh_lo);
            encode14(c.retriggerTime,   &retrig_hi,  &retrig_lo);
            encode14(c.headSensitivity, &sens_hi,    &sens_lo);
            encode14(c.scanTime,        &scan_hi,    &scan_lo);
            encode14(c.maskTime,        &mask_hi,    &mask_lo);
            encode14(c.rimRatioThreshold, &rsens_hi,   &rsens_lo);
            encode14(c.chokeThreshold,    &rthresh_hi, &rthresh_lo);
            uint8_t buf[19] = {
                p[0],
                c.padType,
                thresh_hi, thresh_lo,
                c.velocityCurve,
                retrig_hi, retrig_lo,
                c.crosstalkGroup,
                sens_hi,    sens_lo,
                scan_hi,    scan_lo,
                mask_hi,    mask_lo,
                rsens_hi,   rsens_lo,    // rimRatioThreshold (was rimSensitivity slot)
                rthresh_hi, rthresh_lo,  // chokeThreshold (was rimThreshold slot)
                (uint8_t)(c.chokeEnabled ? 1 : 0)
            };
            sysexSendResponse(deviceId, SYSEX_CAT_PAD, SYSEX_PAD_RESP, buf, 19);
            break;
        }

        case SYSEX_PAD_LINK:
            if (pLen < 2 || p[0] >= NUM_INPUTS || p[1] >= NUM_INPUTS || p[0] == p[1]) {
                sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR);
                return;
            }
            g_inputs[p[0]].linkedInput = p[1];
            g_inputs[p[1]].linkedInput = p[0];
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_UNLINK: {
            if (pLen < 1 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            uint8_t linked = g_inputs[p[0]].linkedInput;
            if (linked < NUM_INPUTS) g_inputs[linked].linkedInput = 0xFF;
            g_inputs[p[0]].linkedInput = 0xFF;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;
        }

        case SYSEX_PAD_GET_STATUS: {
            if (pLen < 1 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            uint8_t id     = p[0];
            uint8_t linked = g_inputs[id].linkedInput;
            uint8_t status;
            // Reserved: this input is the secondary of a hardware dual-zone pair
            // (the primary holds padType 01 or 05)
            if (linked < NUM_INPUTS &&
                (g_inputs[linked].padType == 1 || g_inputs[linked].padType == 5)) {
                status = SYSEX_INPUT_RESERVED;
            } else if (g_inputs[id].padType != 0) {
                status = SYSEX_INPUT_ACTIVE;
            } else {
                status = SYSEX_INPUT_AVAIL;
            }
            uint8_t buf[2] = { id, status };
            sysexSendResponse(deviceId, SYSEX_CAT_PAD, SYSEX_PAD_GET_STATUS, buf, 2);
            break;
        }

        case SYSEX_PAD_SET_SENS:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].headSensitivity = decode14(p[1], p[2]);
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_SCAN:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].scanTime = decode14(p[1], p[2]);
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_MASK:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].maskTime = decode14(p[1], p[2]);
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_RIM_SENS:  // now: rim ratio threshold (DUAL_PIEZO)
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].rimRatioThreshold = decode14(p[1], p[2]);
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_RIM_THRESH:  // now: choke threshold (PIEZO_SWITCH_CHOKE)
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].chokeThreshold = decode14(p[1], p[2]);
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_CHOKE_EN:
            // Byte 0x10 was already defined and sent by the app's "Choke" checkbox;
            // this handler was missing, so the checkbox has been silently failing.
            if (pLen < 2 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].chokeEnabled = (p[1] != 0);
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        // ---- Secondary Trigger Behaviours v1 + Scan v3 tunables (2026-07-12 in
        // firmware, first exposed over SysEx here). All write directly into the
        // matching InputConfig field; applyConfig()/syncConfig() (main_esp32s3.cpp)
        // already picks these up unchanged, so no engine-side change is needed.

        case SYSEX_PAD_SET_SCAN_MARGIN:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].scanMargin = decode14(p[1], p[2]);
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_SETTLE_WAIT:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].settleWaitMs = decode14(p[1], p[2]);
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_EMA_ALPHA:
            if (pLen < 2 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].emaAlpha = p[1];
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_RIM_GATE:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].rimThreshold = decode14(p[1], p[2]);
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_RIM_SCALE:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].rimSensitivity = decode14(p[1], p[2]);
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_RIM_CURVE:
            if (pLen < 2 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].rimCurve = p[1];
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_XSTICK_NOTE:
            if (pLen < 2 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].crossStickNote = p[1];
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_XSTICK_CUTOFF:
            // MIDI velocity units (0-127), NOT raw ADC — single byte, no decode14.
            if (pLen < 2 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].crossStickCutoff = p[1];
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_ALT_NOTE:
            if (pLen < 2 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].alternateNote = p[1];
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_ALT_MIN_VEL:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].minAltNoteVelocity = decode14(p[1], p[2]);
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_CHOKE_HOLD:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].chokeHoldMs = decode14(p[1], p[2]);
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_CHOKE_GRACE:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].chokeReleaseGraceMs = decode14(p[1], p[2]);
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_GET_EXT: {
            // Bundled GET for all 12 fields above — mirrors the SYSEX_PAD_GET (0x06)
            // pattern rather than requiring 12 separate round-trips.
            if (pLen < 1 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            const InputConfig& c = g_inputs[p[0]];
            uint8_t scanm_hi, scanm_lo, settle_hi, settle_lo;
            uint8_t rimt_hi, rimt_lo, rims_hi, rims_lo;
            uint8_t altmin_hi, altmin_lo, chold_hi, chold_lo, cgrace_hi, cgrace_lo;
            encode14(c.scanMargin,          &scanm_hi,  &scanm_lo);
            encode14(c.settleWaitMs,        &settle_hi, &settle_lo);
            encode14(c.rimThreshold,        &rimt_hi,   &rimt_lo);
            encode14(c.rimSensitivity,      &rims_hi,   &rims_lo);
            encode14(c.minAltNoteVelocity,  &altmin_hi, &altmin_lo);
            encode14(c.chokeHoldMs,         &chold_hi,  &chold_lo);
            encode14(c.chokeReleaseGraceMs, &cgrace_hi, &cgrace_lo);
            uint8_t buf[20] = {
                p[0],
                scanm_hi, scanm_lo,
                settle_hi, settle_lo,
                (uint8_t)c.emaAlpha,
                rimt_hi, rimt_lo,
                rims_hi, rims_lo,
                c.rimCurve,
                c.crossStickNote,
                c.crossStickCutoff,
                c.alternateNote,
                altmin_hi, altmin_lo,
                chold_hi, chold_lo,
                cgrace_hi, cgrace_lo
            };
            sysexSendResponse(deviceId, SYSEX_CAT_PAD, SYSEX_PAD_RESP_EXT, buf, 20);
            break;
        }

        default:
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_UNKNOWN);
            break;
    }
}

// ---- category 03 — MIDI mapping --------------------------------------------

static void handleMidi(uint8_t deviceId, uint8_t cmd,
                       const uint8_t* p, size_t pLen) {
    switch (cmd) {
        case SYSEX_MIDI_SET_NOTE:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_MIDI, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].midiNote    = p[1];
            g_inputs[p[0]].midiChannel = p[2];
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_MIDI, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_MIDI_SET_Z2:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_MIDI, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].zone2MidiNote    = p[1];
            g_inputs[p[0]].zone2MidiChannel = p[2];
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_MIDI, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_MIDI_SET_CC:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_MIDI, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].ccNumber  = p[1];
            g_inputs[p[0]].ccChannel = p[2];
            g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_MIDI, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_MIDI_GET: {
            if (pLen < 1 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_MIDI, cmd, SYSEX_ACK_ERROR); return; }
            const InputConfig& c = g_inputs[p[0]];
            // Response: INPUT_ID MIDI_NOTE CH_1 MIDI_NOTE_2 CH_2 CC_NUM CC_CH
            uint8_t buf[7] = {
                p[0], c.midiNote, c.midiChannel,
                c.zone2MidiNote, c.zone2MidiChannel,
                c.ccNumber, c.ccChannel
            };
            sysexSendResponse(deviceId, SYSEX_CAT_MIDI, SYSEX_MIDI_RESP, buf, 7);
            break;
        }

        default:
            sendAck(deviceId, SYSEX_CAT_MIDI, cmd, SYSEX_ACK_UNKNOWN);
            break;
    }
}

// ---- category 04 — preset management --------------------------------------

static void handlePreset(uint8_t deviceId, uint8_t cmd,
                         const uint8_t* p, size_t pLen) {
    switch (cmd) {
        case SYSEX_PRE_LOAD: {
            if (pLen < 1) { sendAck(deviceId, SYSEX_CAT_PRESET, cmd, SYSEX_ACK_ERROR); return; }
            adcSamplerPause();
            bool ok = presetLoad(p[0]);
            adcSamplerResume();
            // BUG FIX: push the loaded config into the running engines. Every other
            // config-mutating handler sets this; presetLoad was missing it, so a loaded
            // preset didn't take effect until some later apply. loop() runs the apply
            // inside its own pause bracket.
            if (ok) g_apply_requested = true;
            sendAck(deviceId, SYSEX_CAT_PRESET, cmd, ok ? SYSEX_ACK_OK : SYSEX_ACK_ERROR);
            break;
        }

        case SYSEX_PRE_SAVE: {
            if (pLen < 2) { sendAck(deviceId, SYSEX_CAT_PRESET, cmd, SYSEX_ACK_ERROR); return; }
            uint8_t nameLen = p[1];
            if (pLen < (size_t)(2 + nameLen)) { sendAck(deviceId, SYSEX_CAT_PRESET, cmd, SYSEX_ACK_ERROR); return; }
            char name[PRESET_NAME_LEN + 1];
            uint8_t n = nameLen < PRESET_NAME_LEN ? nameLen : PRESET_NAME_LEN;
            memcpy(name, p + 2, n);
            name[n] = '\0';
            adcSamplerPause();
            bool ok = presetSave(p[0], name);
            adcSamplerResume();
            // No apply: presetSave writes current config out to a preset file; it does
            // not change g_inputs, so the running engines are unaffected.
            sendAck(deviceId, SYSEX_CAT_PRESET, cmd, ok ? SYSEX_ACK_OK : SYSEX_ACK_ERROR);
            break;
        }

        case SYSEX_PRE_LIST: {
            // Response: COUNT [PRESET_ID NAME_LEN NAME_BYTES...]...
            // Max payload: 1 + 16*(1+1+16) = 289 bytes
            uint8_t buf[1 + MAX_PRESETS * (1 + 1 + PRESET_NAME_LEN)];
            size_t pos = 1; // reserve buf[0] for count
            uint8_t count = 0;
            for (uint8_t i = 0; i < MAX_PRESETS; i++) {
                Preset pr;
                if (!presetRead(i, &pr)) continue;
                uint8_t nlen = (uint8_t)strlen(pr.name);
                buf[pos++] = i;
                buf[pos++] = nlen;
                memcpy(buf + pos, pr.name, nlen);
                pos += nlen;
                count++;
            }
            buf[0] = count;
            sysexSendResponse(deviceId, SYSEX_CAT_PRESET, SYSEX_PRE_LIST_R, buf, pos);
            break;
        }

        case SYSEX_PRE_DELETE: {
            if (pLen < 1) { sendAck(deviceId, SYSEX_CAT_PRESET, cmd, SYSEX_ACK_ERROR); return; }
            adcSamplerPause();
            bool ok = presetDelete(p[0]);
            adcSamplerResume();
            // No apply: deleting a preset file doesn't change g_inputs.
            sendAck(deviceId, SYSEX_CAT_PRESET, cmd, ok ? SYSEX_ACK_OK : SYSEX_ACK_ERROR);
            break;
        }

        case SYSEX_PRE_EXPORT: {
            if (pLen < 1) { sendAck(deviceId, SYSEX_CAT_PRESET, cmd, SYSEX_ACK_ERROR); return; }
            Preset pr;
            if (!presetRead(p[0], &pr)) {
                sendAck(deviceId, SYSEX_CAT_PRESET, cmd, SYSEX_ACK_ERROR);
                return;
            }
            // Record grew from 24 to 43 bytes/input on 2026-07 (append-only — the
            // first 24 bytes are byte-for-byte unchanged, so this is backward
            // compatible with anything that only reads the original fields).
            uint8_t buf[2 + PRESET_NAME_LEN + NUM_INPUTS * 43];
            uint8_t nlen = (uint8_t)strlen(pr.name);
            size_t pos = 0;
            buf[pos++] = p[0]; // preset ID
            buf[pos++] = nlen;
            memcpy(buf + pos, pr.name, nlen);
            pos += nlen;
            for (uint8_t i = 0; i < NUM_INPUTS; i++) {
                const InputConfig& c = pr.inputs[i];
                uint8_t thresh_hi, thresh_lo, retrig_hi, retrig_lo;
                uint8_t sens_hi, sens_lo, scan_hi, scan_lo, mask_hi, mask_lo;
                uint8_t rsens_hi, rsens_lo, rthresh_hi, rthresh_lo;
                encode14(c.threshold,       &thresh_hi,  &thresh_lo);
                encode14(c.retriggerTime,   &retrig_hi,  &retrig_lo);
                encode14(c.headSensitivity, &sens_hi,    &sens_lo);
                encode14(c.scanTime,        &scan_hi,    &scan_lo);
                encode14(c.maskTime,        &mask_hi,    &mask_lo);
                encode14(c.rimRatioThreshold, &rsens_hi,   &rsens_lo);
                encode14(c.chokeThreshold,    &rthresh_hi, &rthresh_lo);
                buf[pos++] = c.padType;
                buf[pos++] = thresh_hi;
                buf[pos++] = thresh_lo;
                buf[pos++] = c.velocityCurve;
                buf[pos++] = retrig_hi;
                buf[pos++] = retrig_lo;
                buf[pos++] = c.crosstalkGroup;
                buf[pos++] = sens_hi;
                buf[pos++] = sens_lo;
                buf[pos++] = scan_hi;
                buf[pos++] = scan_lo;
                buf[pos++] = mask_hi;
                buf[pos++] = mask_lo;
                buf[pos++] = rsens_hi;
                buf[pos++] = rsens_lo;
                buf[pos++] = rthresh_hi;
                buf[pos++] = rthresh_lo;
                buf[pos++] = c.midiNote;
                buf[pos++] = c.midiChannel;
                buf[pos++] = c.zone2MidiNote;
                buf[pos++] = c.zone2MidiChannel;
                buf[pos++] = c.ccNumber;
                buf[pos++] = c.ccChannel;
                buf[pos++] = (c.linkedInput == 0xFF) ? SYSEX_LINKED_NONE : c.linkedInput;

                // ---- Appended 2026-07: Secondary Trigger Behaviours v1 + Scan v3
                // (19 bytes, same field order/encoding as SYSEX_PAD_RESP_EXT minus
                // the leading INPUT_ID byte, which this record doesn't repeat).
                uint8_t scanm_hi, scanm_lo, settle_hi, settle_lo;
                uint8_t rimt_hi, rimt_lo, rims_hi, rims_lo;
                uint8_t altmin_hi, altmin_lo, chold_hi, chold_lo, cgrace_hi, cgrace_lo;
                encode14(c.scanMargin,          &scanm_hi,  &scanm_lo);
                encode14(c.settleWaitMs,        &settle_hi, &settle_lo);
                encode14(c.rimThreshold,        &rimt_hi,   &rimt_lo);
                encode14(c.rimSensitivity,      &rims_hi,   &rims_lo);
                encode14(c.minAltNoteVelocity,  &altmin_hi, &altmin_lo);
                encode14(c.chokeHoldMs,         &chold_hi,  &chold_lo);
                encode14(c.chokeReleaseGraceMs, &cgrace_hi, &cgrace_lo);
                buf[pos++] = scanm_hi;   buf[pos++] = scanm_lo;
                buf[pos++] = settle_hi;  buf[pos++] = settle_lo;
                buf[pos++] = (uint8_t)c.emaAlpha;
                buf[pos++] = rimt_hi;    buf[pos++] = rimt_lo;
                buf[pos++] = rims_hi;    buf[pos++] = rims_lo;
                buf[pos++] = c.rimCurve;
                buf[pos++] = c.crossStickNote;
                buf[pos++] = c.crossStickCutoff;
                buf[pos++] = c.alternateNote;
                buf[pos++] = altmin_hi;  buf[pos++] = altmin_lo;
                buf[pos++] = chold_hi;   buf[pos++] = chold_lo;
                buf[pos++] = cgrace_hi;  buf[pos++] = cgrace_lo;
            }
            sysexSendResponse(deviceId, SYSEX_CAT_PRESET, SYSEX_PRE_EXPORT, buf, pos);
            break;
        }

        default:
            sendAck(deviceId, SYSEX_CAT_PRESET, cmd, SYSEX_ACK_UNKNOWN);
            break;
    }
}

// ---- main dispatcher -------------------------------------------------------

void sysexParse(const uint8_t* data, size_t len) {
    if (!g_serialQuiet) DevLog.printf("[SysEx RX] len=%u first bytes: %02X %02X %02X %02X %02X\n",
        (unsigned)len,
        len>0?data[0]:0, len>1?data[1]:0,
        len>2?data[2]:0, len>3?data[3]:0,
        len>4?data[4]:0);
    
    if (len < SYSEX_HEADER_LEN) {
        DevLog.println("[SysEx] Message too short");
        return;
    }
    if (data[0] != SYSEX_MFR_0 || data[1] != SYSEX_MFR_1) {
        DevLog.println("[SysEx] Unknown manufacturer ID");
        return;
    }
    if (data[2] != SYSEX_DEV_HEAD) {
        if (!g_serialQuiet) DevLog.printf("[SysEx] Wrong device ID: %02X\n", data[2]);
        return;
    }

    uint8_t        deviceId   = data[2];
    uint8_t        cmdHigh    = data[3];
    uint8_t        cmdLow     = data[4];
    const uint8_t* payload    = data + SYSEX_HEADER_LEN;
    size_t         payloadLen = len - SYSEX_HEADER_LEN;

    switch (cmdHigh) {
        case SYSEX_CAT_SYS:    handleSystem(deviceId, cmdLow, payload, payloadLen); break;
        case SYSEX_CAT_PAD:    handlePad   (deviceId, cmdLow, payload, payloadLen); break;
        case SYSEX_CAT_MIDI:   handleMidi  (deviceId, cmdLow, payload, payloadLen); break;
        case SYSEX_CAT_PRESET: handlePreset(deviceId, cmdLow, payload, payloadLen); break;
        default:
            sendAck(deviceId, cmdHigh, cmdLow, SYSEX_ACK_UNKNOWN);
            break;
    }
}
