MidiSeqTool - TODO

One line per item, grouped by area. Measurements, reasoning and completed work go in findings.md.

ON HOLD since 2026-09-12 (design.md, top). When resuming: the listener first (design §9.2).

## First steps (design.md §8)
- SynthLib: the MIDI output (done for VST3) - push it, and pull it into the siblings; the AU half (a MIDI processor) later
- §8.1: learn 4 bars, pass them through, then replay them with a ratchet after every note; checked in Live through the two-track "MIDI From" routing (§5.2)

## Direction (design §9)
- Split the loop learner into a listener (rolling history, grid, density, key, loop length) and generators, before adding more generators
- Controller input: map CCs onto hidden parameters (IMidiMapping) - a SynthLib contract question
- Controller output for Live: a virtual CoreMIDI port of the plug-in's own, since Live drops VST3 CC output
- Generators to try: echo/continuation, call and response, fills, euclidean and Markov patterns from nothing
- Call and response (design §9.4): phrase end by rest or by bar count, an answer transformed from the call under Similarity/Length/Density/Register
- Melodic step generator (design §10): root + 12-note mask (presets, or learned), range, 303-style steps (gate/accent/slide/octave), Generate and Modify, seeded
