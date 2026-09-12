/*
 * MidiSeqTool - a MIDI plug-in that learns a part and plays it back evolving.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program. If
 * not, see <https://www.gnu.org/licenses/>.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "synthlibPlugin.h"
#include "synthlibLog.h"
#include "msqLoop.h"

// touch /tmp/midiseqtool-log, read /tmp/midiseqtool.log - see SynthLib's plugin/synthlibLog.h.
const char gSynthLibLogName[] = "midiseqtool";

#ifndef MSQ_VERSION_STRING
#define MSQ_VERSION_STRING    "0.1.0"
#endif

#ifndef MSQ_AU_VERSION
#define MSQ_AU_VERSION        (0x00000100)
#endif

// THESE MAY NEVER CHANGE: a saved project finds the plug-in by them.
static const uint8_t gProcessorUid[16]  = { 0x6F, 0x10, 0x25, 0x5E, 0x95, 0x58, 0x4F, 0xA9, 0xA7, 0xCC, 0xCB, 0x86, 0xBB, 0xDA, 0x0B, 0x3F };
static const uint8_t gControllerUid[16] = { 0xFA, 0x24, 0xD6, 0x2E, 0xCB, 0x89, 0x47, 0x45, 0x8B, 0x46, 0xB1, 0x95, 0xD1, 0x6C, 0x1B, 0x6C };

#define FOUR_CC(a, b, c, d)    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define MSQ_AU_TYPE            FOUR_CC('a', 'u', 'm', 'u')
#define MSQ_AU_SUBTYPE         FOUR_CC('M', 'S', 'e', 'q')
#define MSQ_AU_MANUFACTURER    FOUR_CC('C', 'P', 'u', 'r')
#define MSQ_AU_BUNDLE_ID       "com.chrispurusha.midiseqtool.au"

enum {
    kParamRatchet = 0,    // appended to, never inserted into: ids are what projects save
    kParamCount
};

static const tSynthLibParam gParams[kParamCount] = {
    { kParamRatchet, "Ratchet", "Ratchet", eSynthLibUnitBoolean, 0.0, 1.0, 1.0, 1, SYNTHLIB_MIDI_NONE, 0 },
};

typedef struct {
    tMsqLoop loop;
    double   sampleRate;
    double   ratchet;

    // FOR THE LOG ONLY (touch /tmp/midiseqtool-log): what the host gave, and what became of it.
    uint64_t notesIn;
    uint64_t sentOk;
    uint64_t sentRefused;
    uint32_t lastFlags;
    bool     wasLearned;
    double   wasOrigin;
    uint32_t loggedJumps;
    uint64_t framesSinceLog;
} tMsqPlugin;

static void emit(void * user, uint8_t status, uint8_t data1, uint8_t data2, uint32_t sampleOffset) {
    tMsqPlugin * m = (tMsqPlugin *)user;

    if (synthlib_plugin_midi_out(user, status, data1, data2, sampleOffset)) {
        m->sentOk++;
    } else {
        if (m->sentRefused == 0) {
            synthlib_log_line("MIDI OUT REFUSED - the host gave no output event list (or no event output bus is active)");
        }
        m->sentRefused++;
    }
}

static uint32_t transport_flags(const tSynthLibTransport * t) {
    return (t->valid ? 1u : 0u) | (t->playing ? 2u : 0u) | (t->musicTimeValid ? 4u : 0u) |
           (t->barPositionValid ? 8u : 0u) | (t->tempoValid ? 16u : 0u) | (t->timeSigValid ? 32u : 0u);
}

static void log_state(tMsqPlugin * m, const tSynthLibTransport * t, uint32_t frames) {
    uint32_t flags = transport_flags(t);

    if (flags != m->lastFlags) {
        synthlib_log_line("transport: %s%s%s%s%s%s ppq %.4f bar %.4f %.2f BPM %d/%d",
                          t->valid ? "valid " : "NO-CONTEXT ", t->playing ? "PLAYING " : "stopped ",
                          t->musicTimeValid ? "musicTime " : "NO-musicTime ", t->barPositionValid ? "bar " : "NO-bar ",
                          t->tempoValid ? "tempo " : "NO-tempo ", t->timeSigValid ? "timeSig" : "NO-timeSig",
                          t->projectTimeMusic, t->barPositionMusic, t->tempo, (int)t->timeSigNumerator,
                          (int)t->timeSigDenominator);
        m->lastFlags = flags;
    }

    if (m->loop.origin != m->wasOrigin) {
        if (m->loop.origin >= 0.0) {
            synthlib_log_line("learning from the bar line at ppq %.4f, a pass of %.2f quarter notes", m->loop.origin,
                              m->loop.passLength);
        }
        m->wasOrigin = m->loop.origin;
    }

    if (m->loop.learned != m->wasLearned) {
        synthlib_log_line(m->loop.learned ? "learned %u notes - playing them back with%s ratchets" : "forgotten (%u notes)",
                          m->loop.count, m->loop.ratchet ? "" : "out");
        m->wasLearned = m->loop.learned;
    }

    if (m->loop.jumps != m->loggedJumps) {
        synthlib_log_line("position jumped %u time(s) - last by %+.6f quarter notes", m->loop.jumps - m->loggedJumps,
                          m->loop.lastJumpBy);
        m->loggedJumps = m->loop.jumps;
    }
    m->framesSinceLog += frames;

    if ((double)m->framesSinceLog >= (2.0 * m->sampleRate)) {
        synthlib_log_line("notes in %llu, MIDI out sent %llu, refused %llu | ppq %.3f | %s, %u notes learned",
                          (unsigned long long)m->notesIn, (unsigned long long)m->sentOk,
                          (unsigned long long)m->sentRefused, t->projectTimeMusic,
                          m->loop.learned ? "playing back" : ((m->loop.origin >= 0.0) ? "learning" : "waiting"),
                          m->loop.count);
        m->framesSinceLog = 0;
    }
}

static void * msq_create(const tSynthLibPluginDesc * desc) {
    tMsqPlugin * m = (tMsqPlugin *)calloc(1, sizeof(tMsqPlugin));

    (void)desc;

    if (m == NULL) {
        return NULL;
    }
    msq_loop_init(&m->loop, emit, m);
    m->sampleRate = 48000.0;
    m->ratchet    = gParams[kParamRatchet].defaultNormalized;
    m->lastFlags  = 0xFFFFFFFFu;
    m->wasOrigin  = -1.0;
    synthlib_log_line("created - MidiSeqTool %s", MSQ_VERSION_STRING);
    return m;
}

static void msq_destroy(void * inst) {
    free(inst);
}

static void msq_prepare(void * inst, const tSynthLibSetup * setup) {
    ((tMsqPlugin *)inst)->sampleRate = setup->sampleRate;
}

static void msq_set_active(void * inst, bool active) {
    (void)active;
    msq_loop_reset(&((tMsqPlugin *)inst)->loop);
}

static void msq_block_begin(void * inst, uint32_t frames, const tSynthLibTransport * t) {
    tMsqPlugin * m          = (tMsqPlugin *)inst;
    int32_t      numerator  = t->timeSigValid ? t->timeSigNumerator : 4;
    int32_t      denominator = t->timeSigValid ? t->timeSigDenominator : 4;
    double       barLength  = ((numerator > 0) && (denominator > 0)) ? (((double)numerator * 4.0) / (double)denominator) : 4.0;
    double       bar        = t->barPositionValid ? t->barPositionMusic : (floor(t->projectTimeMusic / barLength) * barLength);

    m->loop.ratchet = (m->ratchet >= 0.5);
    msq_loop_block(&m->loop, t->valid && t->playing && t->musicTimeValid, t->projectTimeMusic,
                   t->tempoValid ? t->tempo : 120.0, (t->sampleRate > 0.0) ? t->sampleRate : m->sampleRate,
                   frames, bar, numerator, denominator);
    log_state(m, t, frames);
}

static uint8_t midi_velocity(float velocity) {
    long v = lroundf(velocity * 127.0f);

    return (uint8_t)((v < 1) ? 1 : ((v > 127) ? 127 : v));
}

static void msq_note_on(void * inst, uint8_t channel, uint8_t note, float velocity, uint32_t sampleOffset) {
    ((tMsqPlugin *)inst)->notesIn++;
    msq_loop_note(&((tMsqPlugin *)inst)->loop, true, channel, note, midi_velocity(velocity), sampleOffset);
}

static void msq_note_off(void * inst, uint8_t channel, uint8_t note, float velocity, uint32_t sampleOffset) {
    (void)velocity;
    msq_loop_note(&((tMsqPlugin *)inst)->loop, false, channel, note, 0, sampleOffset);
}

// Silence on the audio output - it exists so Live will host this where an instrument goes (design §5.2).
static void msq_process(void * inst, const float * const * in, uint32_t numIn, float ** out, uint32_t numOut,
                        uint32_t frames, const tSynthLibTransport * t) {
    (void)in;
    (void)numIn;
    (void)t;

    for (uint32_t ch = 0; (out != NULL) && (ch < numOut); ch++) {
        if (out[ch] != NULL) {
            memset(out[ch], 0, (size_t)frames * sizeof(float));
        }
    }
    msq_loop_generate(&((tMsqPlugin *)inst)->loop);
}

static void msq_set_param(void * inst, uint32_t id, double normalized) {
    if (id == kParamRatchet) {
        ((tMsqPlugin *)inst)->ratchet = normalized;
    }
}

static double msq_get_param(void * inst, uint32_t id) {
    return (id == kParamRatchet) ? ((tMsqPlugin *)inst)->ratchet : 0.0;
}

static bool msq_param_text(const tSynthLibPluginDesc * desc, void * inst, uint32_t id, double value, char * out, size_t len) {
    (void)desc;
    (void)inst;

    if (id != kParamRatchet) {
        return false;
    }
    snprintf(out, len, "%s", (value >= 0.5) ? "on" : "off");
    return true;
}

static const tSynthLibBus gOutputs[1] = { { "Out", 2, false, true } };

static const tSynthLibPluginDesc gDescriptor = {
    .name              = "MidiSeqTool",
    .vendor            = "Chris Purusha",
    .url               = "https://github.com/chrispurusha/MidiSeqTool",
    .email             = "",
    .version           = MSQ_VERSION_STRING,

    .isInstrument      = true,     // design §5.2 - Live takes a plug-in's MIDI output from its instrument slot
    .vst3SubCategory   = "Instrument",
    .outputs           = gOutputs,
    .numOutputs        = 1,
    .wantsMidiIn       = true,
    .wantsMidiOut      = true,
    .wantsTransport    = true,

    .vst3ProcessorUid  = gProcessorUid,
    .vst3ControllerUid = gControllerUid,
    .auType            = MSQ_AU_TYPE,
    .auSubType         = MSQ_AU_SUBTYPE,
    .auManufacturer    = MSQ_AU_MANUFACTURER,
    .auVersion         = MSQ_AU_VERSION,
    .auBundleId        = MSQ_AU_BUNDLE_ID,

    .params            = gParams,
    .numParams         = kParamCount,

    .cb = {
        .create     = msq_create,
        .destroy    = msq_destroy,
        .prepare    = msq_prepare,
        .setActive  = msq_set_active,
        .blockBegin = msq_block_begin,
        .process    = msq_process,
        .noteOn     = msq_note_on,
        .noteOff    = msq_note_off,
        .setParam   = msq_set_param,
        .getParam   = msq_get_param,
        .paramText  = msq_param_text,
    }
};

static const tSynthLibPluginSet gSet = {
    .variants = &gDescriptor,
    .count    = 1
};

const tSynthLibPluginSet * synthlib_plugin_variants(void) {
    return &gSet;
}
