#include "SysEx.h"
#include "BleMidi.h"
#include "../config/Config.h"
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
        Serial.printf("[SysEx TX] ERROR: payload too large (%u bytes)\n", (unsigned)payloadLen);
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
    Serial.printf("[SysEx TX] F0 00 7D %02X %02X %02X", deviceId, cmdHigh, cmdLow);
    for (size_t i = 0; i < payloadLen; i++) {
        Serial.printf(" %02X", payload[i]);
    }
    Serial.println(" F7");
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
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_THRESH:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].threshold = decode14(p[1], p[2]);
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_CURVE:
            if (pLen < 2 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].velocityCurve = p[1];
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_RETRIG:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].retriggerTime = decode14(p[1], p[2]);
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_XTALK:
            if (pLen < 2 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].crosstalkGroup = p[1];
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
            encode14(c.rimSensitivity,  &rsens_hi,   &rsens_lo);
            encode14(c.rimThreshold,    &rthresh_hi, &rthresh_lo);
            uint8_t buf[18] = {
                p[0],
                c.padType,
                thresh_hi, thresh_lo,
                c.velocityCurve,
                retrig_hi, retrig_lo,
                c.crosstalkGroup,
                sens_hi,    sens_lo,
                scan_hi,    scan_lo,
                mask_hi,    mask_lo,
                rsens_hi,   rsens_lo,
                rthresh_hi, rthresh_lo
            };
            sysexSendResponse(deviceId, SYSEX_CAT_PAD, SYSEX_PAD_RESP, buf, 18);
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
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_SCAN:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].scanTime = decode14(p[1], p[2]);
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_MASK:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].maskTime = decode14(p[1], p[2]);
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_RIM_SENS:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].rimSensitivity = decode14(p[1], p[2]);
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_PAD_SET_RIM_THRESH:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].rimThreshold = decode14(p[1], p[2]);
            sendAck(deviceId, SYSEX_CAT_PAD, cmd, SYSEX_ACK_OK);
            break;

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
            sendAck(deviceId, SYSEX_CAT_MIDI, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_MIDI_SET_Z2:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_MIDI, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].zone2MidiNote    = p[1];
            g_inputs[p[0]].zone2MidiChannel = p[2];
            sendAck(deviceId, SYSEX_CAT_MIDI, cmd, SYSEX_ACK_OK);
            break;

        case SYSEX_MIDI_SET_CC:
            if (pLen < 3 || p[0] >= NUM_INPUTS) { sendAck(deviceId, SYSEX_CAT_MIDI, cmd, SYSEX_ACK_ERROR); return; }
            g_inputs[p[0]].ccNumber  = p[1];
            g_inputs[p[0]].ccChannel = p[2];
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
            bool ok = presetLoad(p[0]);
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
            bool ok = presetSave(p[0], name);
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
            bool ok = presetDelete(p[0]);
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
            uint8_t buf[2 + PRESET_NAME_LEN + NUM_INPUTS * 24];
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
                encode14(c.rimSensitivity,  &rsens_hi,   &rsens_lo);
                encode14(c.rimThreshold,    &rthresh_hi, &rthresh_lo);
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
    Serial.printf("[SysEx RX] len=%u first bytes: %02X %02X %02X %02X %02X\n",
        (unsigned)len,
        len>0?data[0]:0, len>1?data[1]:0,
        len>2?data[2]:0, len>3?data[3]:0,
        len>4?data[4]:0);
    
    if (len < SYSEX_HEADER_LEN) {
        Serial.println("[SysEx] Message too short");
        return;
    }
    if (data[0] != SYSEX_MFR_0 || data[1] != SYSEX_MFR_1) {
        Serial.println("[SysEx] Unknown manufacturer ID");
        return;
    }
    if (data[2] != SYSEX_DEV_HEAD) {
        Serial.printf("[SysEx] Wrong device ID: %02X\n", data[2]);
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
