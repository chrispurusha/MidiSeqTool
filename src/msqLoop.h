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
#ifndef __MSQ_LOOP_H__
#define __MSQ_LOOP_H__

#include <stdbool.h>
#include <stdint.h>

// The learn-then-replay loop of design §8.1, with no plug-in code in it so it can be tested offline.

#define MSQ_MAX_NOTES    (1024)
#define MSQ_MAX_OUT      (1024)    // events one block can carry

typedef void (* tMsqEmit)(void * user, uint8_t status, uint8_t data1, uint8_t data2, uint32_t sampleOffset);

typedef struct {
    double  start;        // quarter notes from the start of the pass
    double  length;       // quarter notes; negative while the note is still held during learning
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
} tMsqNote;

typedef struct {
    uint32_t sampleOffset;
    uint8_t  status;
    uint8_t  data1;
    uint8_t  data2;
} tMsqEvent;

typedef struct {
    uint32_t  passBars;
    bool      ratchet;

    bool      playing;
    double    ppqStart;           // the block's first sample
    double    ppqPerSample;
    uint32_t  frames;
    double    expectedPpq;        // where the next block starts if nothing jumps

    double    origin;             // the bar line learning began at; negative until known
    double    passLength;         // quarter notes
    bool      learned;
    double    learnedFrom;        // nothing generated starts before here
    tMsqNote  notes[MSQ_MAX_NOTES];
    uint32_t  count;
    int16_t   open[16][128];      // the note being held while learning, or -1

    uint32_t  jumps;              // for the log: how often the position moved unexpectedly
    double    lastJumpBy;         // and by how much, the last time (quarter notes)
    uint8_t   through[16][128];   // sounding at the output: passed through
    uint8_t   generated[16][128]; // and played from the pass

    tMsqEvent out[MSQ_MAX_OUT];
    uint32_t  outCount;
    tMsqEmit  emit;
    void *    user;
} tMsqLoop;

void msq_loop_init(tMsqLoop * loop, tMsqEmit emit, void * user);
void msq_loop_reset(tMsqLoop * loop);
void msq_loop_block(tMsqLoop * loop, bool playing, double ppq, double tempo, double sampleRate, uint32_t frames,
                    double barPosition, int32_t numerator, int32_t denominator);
void msq_loop_note(tMsqLoop * loop, bool on, uint8_t channel, uint8_t note, uint8_t velocity, uint32_t sampleOffset);
void msq_loop_generate(tMsqLoop * loop);

#endif // __MSQ_LOOP_H__
