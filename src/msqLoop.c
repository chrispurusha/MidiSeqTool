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
#include <string.h>

#include "msqLoop.h"

#define MSQ_RATCHET_GAP         (0.25)      // §8.1 - a 16th note after each note, in quarter notes
#define MSQ_RATCHET_VELOCITY    (0.75)
#define MSQ_NOTE_MIN            (1.0e-3)
#define MSQ_JUMP_TOLERANCE      (1.0e-4)    // quarter notes

#define MIDI_NOTE_ON            (0x90)
#define MIDI_NOTE_OFF           (0x80)

static void push(tMsqLoop * loop, uint32_t offset, uint8_t status, uint8_t data1, uint8_t data2) {
    if (loop->outCount < MSQ_MAX_OUT) {
        loop->out[loop->outCount++] = (tMsqEvent){ offset, status, data1, data2 };
    }
}

static bool is_note_off(const tMsqEvent * e) {
    return ((e->status & 0xF0) == MIDI_NOTE_OFF) || (((e->status & 0xF0) == MIDI_NOTE_ON) && (e->data2 == 0));
}

static bool goes_before(const tMsqEvent * a, const tMsqEvent * b) {
    if (a->sampleOffset != b->sampleOffset) {
        return a->sampleOffset < b->sampleOffset;
    }
    return is_note_off(a) && !is_note_off(b);    // §4.1 - a release before a restart at the same instant
}

// §4.2 - in time order, whatever order they were decided in.
static void flush(tMsqLoop * loop) {
    for (uint32_t i = 1; i < loop->outCount; i++) {
        tMsqEvent held = loop->out[i];
        uint32_t  j    = i;

        while ((j > 0) && goes_before(&held, &loop->out[j - 1])) {
            loop->out[j] = loop->out[j - 1];
            j--;
        }
        loop->out[j] = held;
    }

    for (uint32_t i = 0; i < loop->outCount; i++) {
        loop->emit(loop->user, loop->out[i].status, loop->out[i].data1, loop->out[i].data2, loop->out[i].sampleOffset);
    }
    loop->outCount = 0;
}

// §4.1
static void all_off(tMsqLoop * loop, uint32_t offset) {
    for (uint8_t channel = 0; channel < 16; channel++) {
        for (uint8_t note = 0; note < 128; note++) {
            if ((loop->through[channel][note] > 0) || (loop->generated[channel][note] > 0)) {
                push(loop, offset, (uint8_t)(MIDI_NOTE_OFF | channel), note, 0);
            }
        }
    }
    memset(loop->through, 0, sizeof(loop->through));
    memset(loop->generated, 0, sizeof(loop->generated));
}

static void forget(tMsqLoop * loop) {
    loop->origin      = -1.0;
    loop->learned     = false;
    loop->learnedFrom = 0.0;
    loop->count       = 0;
    memset(loop->open, 0xFF, sizeof(loop->open));
}

static void finish_learning(tMsqLoop * loop, double at) {
    for (uint32_t i = 0; i < loop->count; i++) {
        if (loop->notes[i].length < 0.0) {
            loop->notes[i].length = fmax(loop->passLength - loop->notes[i].start, MSQ_NOTE_MIN);
        }
    }
    memset(loop->open, 0xFF, sizeof(loop->open));
    loop->learned     = true;
    loop->learnedFrom = at;
}

static uint32_t offset_of(const tMsqLoop * loop, double at) {
    double offset = floor((at - loop->ppqStart) / loop->ppqPerSample);

    if (offset < 0.0) {
        return 0;
    }
    return (offset >= (double)loop->frames) ? ((loop->frames > 0) ? (loop->frames - 1) : 0) : (uint32_t)offset;
}

void msq_loop_init(tMsqLoop * loop, tMsqEmit emit, void * user) {
    memset(loop, 0, sizeof(*loop));
    loop->emit     = emit;
    loop->user     = user;
    loop->passBars = 4;
    loop->ratchet  = true;
    forget(loop);
}

void msq_loop_reset(tMsqLoop * loop) {
    memset(loop->through, 0, sizeof(loop->through));
    memset(loop->generated, 0, sizeof(loop->generated));
    loop->outCount = 0;
    loop->playing  = false;
    forget(loop);
}

void msq_loop_block(tMsqLoop * loop, bool playing, double ppq, double tempo, double sampleRate, uint32_t frames,
                    double barPosition, int32_t numerator, int32_t denominator) {
    double perSample = ((tempo > 0.0) && (sampleRate > 0.0)) ? ((tempo / 60.0) / sampleRate) : 0.0;

    if (loop->outCount > 0) {
        flush(loop);    // anything a block with no audio left behind
    }
    loop->frames = frames;

    if (!playing) {
        if (loop->playing) {    // stopped: release everything, and learn again next time
            all_off(loop, 0);
            forget(loop);
        }
        loop->playing      = false;
        loop->ppqStart     = ppq;
        loop->ppqPerSample = perSample;
        return;
    }

    // §4.3 - a loop brace or a moved playhead: release what is sounding; a pass half learned is lost.
    if (loop->playing && (fabs(ppq - loop->expectedPpq) > MSQ_JUMP_TOLERANCE)) {
        loop->jumps++;
        loop->lastJumpBy = ppq - loop->expectedPpq;
        all_off(loop, 0);

        if (loop->learned) {
            loop->learnedFrom = -INFINITY;
        } else {
            forget(loop);
        }
    }
    loop->playing      = true;
    loop->ppqStart     = ppq;
    loop->ppqPerSample = perSample;
    loop->expectedPpq  = ppq + (perSample * (double)frames);

    if (loop->origin < 0.0) {
        double barLength = ((numerator > 0) && (denominator > 0)) ? (((double)numerator * 4.0) / (double)denominator) : 4.0;

        loop->passLength = barLength * (double)loop->passBars;
        loop->origin     = ((ppq - barPosition) < MSQ_JUMP_TOLERANCE) ? barPosition : (barPosition + barLength);
    }
}

static void record(tMsqLoop * loop, bool on, uint8_t channel, uint8_t note, uint8_t velocity, double at) {
    int16_t held = loop->open[channel][note];
    double  rel  = at - loop->origin;

    if (held >= 0) {
        loop->notes[held].length = fmax(rel - loop->notes[held].start, MSQ_NOTE_MIN);
        loop->open[channel][note] = -1;
    }

    if (on && (loop->count < MSQ_MAX_NOTES)) {
        loop->notes[loop->count]  = (tMsqNote){ rel, -1.0, channel, note, velocity };
        loop->open[channel][note] = (int16_t)loop->count;
        loop->count++;
    }
}

void msq_loop_note(tMsqLoop * loop, bool on, uint8_t channel, uint8_t note, uint8_t velocity, uint32_t sampleOffset) {
    double at = loop->ppqStart + ((double)sampleOffset * loop->ppqPerSample);

    channel &= 0x0F;
    note    &= 0x7F;

    if (loop->playing && !loop->learned && (loop->origin >= 0.0) && (at >= (loop->origin + loop->passLength))) {
        finish_learning(loop, loop->origin + loop->passLength);
    }

    // §2.3 - once learned the input is ignored; only a note passed through before is still released.
    if (loop->playing && loop->learned) {
        if (!on && (loop->through[channel][note] > 0)) {
            loop->through[channel][note]--;
            push(loop, sampleOffset, (uint8_t)(MIDI_NOTE_OFF | channel), note, 0);
        }
        return;
    }

    if (on) {
        loop->through[channel][note]++;
        push(loop, sampleOffset, (uint8_t)(MIDI_NOTE_ON | channel), note, velocity);
    } else {
        if (loop->through[channel][note] > 0) {
            loop->through[channel][note]--;
        }
        push(loop, sampleOffset, (uint8_t)(MIDI_NOTE_OFF | channel), note, 0);
    }

    if (loop->playing && (loop->origin >= 0.0) && (at >= loop->origin)) {
        record(loop, on, channel, note, velocity, at);
    }
}

static void schedule(tMsqLoop * loop, double on, double length, const tMsqNote * n, uint8_t velocity, double end) {
    double off = on + length;

    if (on < loop->learnedFrom) {
        return;
    }

    if ((on >= loop->ppqStart) && (on < end)) {
        push(loop, offset_of(loop, on), (uint8_t)(MIDI_NOTE_ON | n->channel), n->note, velocity);
        loop->generated[n->channel][n->note]++;
    }

    if ((off >= loop->ppqStart) && (off < end) && (loop->generated[n->channel][n->note] > 0)) {
        push(loop, offset_of(loop, off), (uint8_t)(MIDI_NOTE_OFF | n->channel), n->note, 0);
        loop->generated[n->channel][n->note]--;
    }
}

// §8.1 - every learned note, and a ratchet after it.
void msq_loop_generate(tMsqLoop * loop) {
    double end = loop->ppqStart + ((double)loop->frames * loop->ppqPerSample);

    if (!loop->playing || (loop->ppqPerSample <= 0.0) || (loop->origin < 0.0)) {
        flush(loop);
        return;
    }

    if (!loop->learned) {
        if (end <= (loop->origin + loop->passLength)) {
            flush(loop);
            return;
        }
        finish_learning(loop, loop->origin + loop->passLength);
    }
    int64_t first = (int64_t)floor((loop->ppqStart - loop->origin) / loop->passLength) - 1;  // its notes may end here
    int64_t last  = (int64_t)floor((end - loop->origin) / loop->passLength);

    for (int64_t pass = first; pass <= last; pass++) {
        double base = loop->origin + ((double)pass * loop->passLength);

        for (uint32_t i = 0; i < loop->count; i++) {
            const tMsqNote * n      = &loop->notes[i];
            double           length = loop->ratchet ? fmin(n->length, MSQ_RATCHET_GAP * 0.9) : n->length;

            schedule(loop, base + n->start, length, n, n->velocity, end);

            if (loop->ratchet) {
                uint8_t velocity = (uint8_t)fmax(1.0, round((double)n->velocity * MSQ_RATCHET_VELOCITY));

                schedule(loop, base + n->start + MSQ_RATCHET_GAP, MSQ_RATCHET_GAP * 0.9, n, velocity, end);
            }
        }
    }
    flush(loop);
}
