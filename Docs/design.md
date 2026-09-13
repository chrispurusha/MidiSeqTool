# MidiSeqTool - design

Living design note, started 2026-09-12. Code refers to it by section (`// §3.2`).

**ON HOLD since 2026-09-12**, by the owner's choice: the basic idea is proven in Live (§8.1, findings), and
§9-§12 are ideas recorded for when work resumes. The next step then would be the listener (§9.2).
§12 is the route to having it inline on a MIDI track in Live: a Max for Live device sharing the C core.

---

## 1. The idea

**1.1** A MIDI effect on a MIDI track. It LEARNS the part for N bars (a parameter), passing it straight
through, then on every later pass plays its own version of what it learned, mutated under macro dials.

**1.2** The first target is drums - the clearest case for fills and groove - with melodic material later.

## 2. Learning

**2.1** The host's transport gives tempo, bar position and time signature (`tSynthLibTransport`). A pass
is N bars from a bar line; learning starts at the first bar line after the transport starts.

**2.2** What is kept: every note on a grid - its step, note number, velocity, offset from the step (the
player's own timing) and length. A 16th-note grid first.

**2.3** Playback plays FROM that picture, never by echoing the input: once learned, what comes in is
ignored until the part is relearned (a Relearn control, or automatically when the input changes -
open question §7).

## 3. Mutation - the macro dials

**3.1 Density** - add or remove hits, from what the pattern already does: ghost notes on the most-used
note, the least-used ones dropped first.

**3.2 Fill** - on the last beat or bar of every 4th or 8th pass: rolls and flams on the snare- and
tom-like notes, velocity rising into the downbeat.

**3.3 Groove** - swing, timing looseness, accent shape.

**3.4 Intensity** - one dial moving density, velocity and fills together.

**3.5 Mutation / Evolve** - how much changes per pass, and whether changes carry forward and drift.

**3.6 Lock / seed** - randomness is seeded, so a variation you like can be kept and a saved project
plays the same way twice.

**3.7 Later, melodic** - the scale detected from what was learned; passing notes, octave jumps,
rhythmic displacement.

## 4. Getting the details right from the start

**4.1 No hung notes.** Every note-on it sends gets its note-off - including a note cut short by a
replaced bar, a transport stop, a loop jump or the plug-in being bypassed.

**4.2 Sample-accurate.** Each event carries its offset within the block (a lesson from GenBridge), and a
note decided in one block can fall due in a later one.

**4.3 Transport jumps and loops** reset or re-sync the pass.

**4.4 Deterministic** - seeded randomness (§3.6).

## 5. Hosts

**5.1 Ableton Live cannot load a third-party MIDI EFFECT** - VST3 or AU. Its MIDI-effect slot takes
only its own devices and Max for Live ones: "the only MIDI effects you can put in a track before an
instrument are the internal ones" (JUCE forum, still true at Live 11.1; nothing found saying Live 12
changed it). The one inline route is a Max for Live device around the same C core (§12).

**5.2 What Live does support - and documents** ("Accessing the MIDI output of a VST plug-in", Ableton
help): the plug-in sits on a track of its own, and a SECOND track takes "MIDI From" that track,
chooses the plug-in as its input channel, and monitors In; the instrument - a Drum Rack, or an External
Instrument out to hardware - goes on the second track. So MidiSeqTool is loaded where an instrument
would be (MIDI in, an audio output it leaves silent, MIDI out), and its MIDI comes out one track along.
Live merges MIDI channels when routing between tracks - fine for one part.

**THE SECOND CHOOSER IS THE TRAP.** Under "MIDI From" there are two: the track, and below it what of that
track. It must name the plug-in. "Pre FX" or "Post FX" hands over the clip itself, which sounds exactly
like a plug-in doing nothing (confirmed 2026-09-12 - see findings).

**Live reportedly passes a VST3's NOTES on but not its controllers or pitch bend** (LegacyMIDICCOutEvent,
which Reaper and Cubase honour). Anything that mutates CCs will need checking in Live on its own.

**5.3 AU MIDI out is not available in Live at all**; the VST3 is the Live build. The Audio Unit is still
worth having for Logic, which hosts AU MIDI processors (`aumi`) natively.

**5.4 Other hosts** (Bitwig, Reaper, Cubase) take a VST3's event output directly; not all hosts
collect plug-in output events, so each is a check, not an assumption.

**5.5** JUCE is worth reading for how it declares a MIDI effect in each format, and must not be copied
from: JUCE 8 is AGPLv3 (see `THIRD_PARTY.md`).

Sources: https://forum.juce.com/t/midi-effect-for-ableton/32455 ,
https://help.ableton.com/hc/en-us/articles/209070189-Accessing-the-MIDI-output-of-a-VST-plug-in ,
https://forum.ableton.com/viewtopic.php?t=229629 , and the VST3 SDK's own headers
(`ivstevents.h`, `ivstaudioprocessor.h`) for the event output bus.

## 9. Where it is going - a live generative instrument

**9.1 The owner's direction** (2026-09-12): if the loop idea works, the real use is LIVE - played into,
it reacts to the notes and controllers it is given and generates from them; and, since that is open
ended, it may also generate ideas from scratch. The learn-a-loop mode of §2 and §8 is the first case
of that, not the whole of it.

**9.2 Listener and generators.** The shape this suggests:

- A LISTENER that models what is coming in, continuously rather than one fixed pass: a rolling
  history of notes, the grid they sit on, density and velocity, the key or scale in use, and - when
  the input does loop - its length (§2, and the loop detection still to build).
- GENERATORS that read the listener and produce: the ratchet of §8.1; echoes and continuations of what
  was just played; answers (call and response); fills (§3.2); and patterns from nothing - euclidean
  rhythms, or a Markov model of what has been heard - for when there is no input to follow.
- The macro dials (§3) choose and blend the generators rather than tuning one.

**9.3 What it asks of the plumbing.**

- CONTROLLER INPUT: a VST3 host delivers CCs as PARAMETER changes, through IMidiMapping - there are no
  CC input events. Hearing arbitrary controllers means mapping them onto (hidden) parameters, the way
  G2 Alike maps the mod wheel, aftertouch and bend. A SynthLib question: the contract maps a fixed set.
- CONTROLLER OUTPUT: Live forwards a VST3's notes but not its CCs or bend (§5). Generated controllers
  for Live would need another route - a virtual CoreMIDI port of the plug-in's own, which a Live track
  can take as MIDI input; MidiSyncTool already schedules MIDI to a port against the host's timeline.
- LATENCY: an answer has to land in time - on the host's grid, scheduled ahead where it can be,
  in the same block as the note that prompted it where it cannot.
- DETERMINISM stays (§3.6): a seed, so a performance can be re-run.

**9.4 Call and response - the first live function** (owner, 2026-09-12): it hears a phrase, and answers
it.

- WHEN A CALL ENDS - two ways, a setting: a rest longer than a threshold (in beats), for free playing;
  or on the grid, the call filling N bars and the answer the next N, for a groove.
- WHAT THE ANSWER IS MADE OF - the call, transformed: its rhythm kept or varied, its contour echoed,
  inverted or turned to resolve (a question answered by a phrase that comes home to the tonic of the
  key the listener has inferred), in the same register or moved, denser or sparser.
- THE DIALS - Similarity (echo at one end, contrast at the other), Length, Density, Register; seeded,
  so an answer that worked can be kept.
- AND WHEN THE PLAYER STOPS, it can go on answering itself - which is §9.2's "from scratch" arriving by
  another door.
- It needs the listener's key and grid (§9.2), and notes only - so Live's two-track routing carries it.

**9.5 Not now.** Recorded so the next steps do not paint it into a corner: the loop learner should
become one listener mode, not the structure everything else hangs off.

## 10. Melodic generation - a 303-style step generator

**10.1 The model** (owner, 2026-09-12: "something very much like Roland's TB-303 software generator").
Roland's TB-303 plug-in has two randomise functions working from settings for Pitch, Gate, Accent and
Slide inside a definable scale and key range: GENERATE (a new pattern) and MODIFY (a variation of the
current one), with play modes Forward, Reverse, Forward-and-Reverse, Invert and Random. The same shape
here, as one of §9.2's generators.

**10.2 What the notes may be.**

- Root note, and a 12-note ENABLE mask - which pitch classes are allowed. Scale presets fill the mask
  (major, the minors, the modes, pentatonics, blues, chromatic); any key can then be switched by hand.
- Or LEARNED: the mask and root taken from what the listener has heard (§9.2), for playing along.
- Range: a lowest note and a number of octaves (the Roland reaches six); weighting towards the root
  and fifth, and a leap-versus-step control for how far one note jumps from the last.

**10.3 A step** carries a pitch, an octave shift, a gate (a rest when off), an accent and a slide. The
pattern has a length in steps (16 to start) and a rate (16ths to start).

**10.4 The dials** - probability of a gate (density), of an accent, of a slide, of an octave jump; the
leap control; GENERATE and MODIFY (how many steps a Modify touches); a seed, so a pattern can be kept.

**10.5 Out as MIDI.** Accent is velocity. A slide is LEGATO - the next note-on before this note-off - so a
synth set to legato portamento glides; the plug-in cannot make a synth glide on its own. Notes only,
which Live's two-track routing carries (§5).

**10.6 Controls before a panel.** Every setting above is a host parameter first - the 12-note mask as
12 switches works in a host's generic panel - and the panel (§7 of the open questions) comes when there
is something worth drawing: a keyboard strip for the mask, the step grid, the dials.

Source: https://articles.roland.com/mastering-the-tb-303-sequencer-in-roland-cloud/ , and the product
descriptions linked from the conversation of 2026-09-12.

## 11. Prior art - algorithmic approaches (surveyed 2026-09-12)

**11.1 Rule-based and probabilistic** - cheap, predictable, proven in hardware.

- Euclidean rhythms: k hits spread as evenly as possible over n steps - a vast set of world rhythms
  from one rule. Good for patterns from nothing.
- Drum-pattern maps: Mutable Instruments' Grids interpolates across a map of real patterns with an X/Y
  position and a density per instrument - close to §3's Density, Fill and Intensity.
- The shift-register "Turing Machine" (Music Thing Modular): a locked loop mutated by a probability
  knob - the Evolve dial of §3.5.
- Markov chains, L-systems, cellular automata.

**11.2 Learning the player live** - the family for call and response (§9.4).

- The Continuator (Pachet, Sony CSL, early 2000s): a variable-order Markov model built from the player's
  phrases as they play, answering in their style.
- OMax and Somax2 (IRCAM): a factor oracle learns a performer's material live and recombines it,
  Somax2 steered by what is being played.
- No dataset, no GPU, microseconds per decision, and what they do can be read - they fit the listener.

**11.3 Neural symbolic models.**

- Magenta (Google): MusicVAE (a latent space to morph through - a macro dial), GrooVAE (a quantised drum
  part made human), Music Transformer. Apache 2.0, compatible with this project's GPLv3.
- The Anticipatory Music Transformer (Stanford, 2023): generation conditioned on events still to come -
  infilling and accompaniment.
- Real-time jamming, 2025-26: ReaLJam (a transformer tuned by reinforcement learning for live jamming,
  generating ahead to hide latency); language-model jamming for live accompaniment; latent-diffusion
  accompaniment in Max/MSP; SongDriver (2022), accompaniment with no latency from the model itself.
- Google's live music models (Magenta RealTime / Lyria, 2025) are real-time but AUDIO, not MIDI.
- The costs: weights, on-device inference (Core ML on a Mac), training data, latency management, and
  licences that differ model to model.

**11.4 What would suit this plug-in.** Drums: Grids-style maps and Turing-Machine mutation, Euclidean
patterns from nothing. Melody: §10's constrained random. Call and response: a Continuator or factor
oracle learned from the player - the core of the listener. A small neural model through Core ML later,
if at all, once the rest works.

Sources: https://arxiv.org/html/2604.07612 , https://arxiv.org/pdf/2606.11886 ,
https://www.researchgate.net/publication/391152101 , https://arxiv.org/pdf/2209.06054 ,
https://gclef-cmu.org/static/pdfs/2025magentart.pdf ; the rest from general knowledge, unchecked.

## 12. Inline in Live - a Max for Live device (added 2026-09-12)

**12.1 Why.** Live's MIDI-effect section takes only its own devices and Max for Live ones (§5.1). No
plug-in registration changes that, whether instrument, effect or `aumi`, and a plug-in's MIDI output never passes down its
own track's chain. A Max for Live MIDI Effect device (`.amxd`) is the only way to have MidiSeqTool on
the MIDI track itself, before the instrument, with no second track. The owner has Live 12 Suite
(12.4.5, which includes Max for Live) and standalone Max 9.1.4.

**12.2 Shape.** The logic stays in C and is shared. `msqLoop.c`, and later the listener and
generators, is compiled twice: into the VST3, and into a Max external (`.mxo`). The external is a thin
wrapper: notes in, transport position in, notes out. The device routes `[midiin]` through the external
to `[midiout]`. Its controls are `live.dial` / `live.menu` objects, which Live automates and saves with
the set, so no SynthLib GUI code is needed.

**12.3 What it gains over the plug-in.**

- It sits inline on the MIDI track.
- The Live API: `live.path` / `live.observer` read the playing clip's `loop_start`, `loop_end` and
  `length`. That is the loop length §8.2 wanted, read from Live instead of assumed. Tempo and song
  position come from `[transport]` / `[plugsync~]`.
- `[midiout]` in a MIDI effect passes any MIDI to the next device, CCs and pitch bend included. That
  avoids the notes-only limit Live puts on a VST3's output (§5.2), but it needs checking.

**12.4 What it costs.**

- Timing: Max for Live runs Max's scheduler inside Live's audio processing, so events land on
  signal-vector boundaries (typically 64 samples, about 1.5 ms at 44.1 kHz), not on the exact sample
  offsets of §4.2. Fine for sequencing; to be checked by ear.
- Everyone who uses the device needs Max for Live (Suite, or the add-on).
- Freezing the `.amxd` can bundle the external, but only for the platforms it was built for.
- Two front ends to keep: the plug-in's and the device's.

**12.5 Build.** The Max SDK (Cycling '74, on GitHub) is its C API; the Min-DevKit C++ layer is not
needed. The licence is believed to be MIT; confirm it before use and record it in `THIRD_PARTY.md`. A
`do-max` script, like `do-plugin`, would build a universal `.mxo`, ad-hoc sign it, and install it
where Max finds it. Which user folder Live's bundled Max searches (`~/Documents/Max 9/Packages/...`, or
beside the device) is still to be confirmed.

**12.6 First step when resumed.** A pass-through external (notes in, notes out) in a MIDI Effect
device, plus a `live.observer` reading the playing clip's loop length. That proves the build, the
load, the inline placement and the loop-length read before any of `msqLoop.c` goes in. Then
`msqLoop.c` inside it, taking its pass length from Live instead of the fixed 4 bars.

Sources: general knowledge of Max and Max for Live, unchecked. The licence, the scheduler granularity
and the CC pass-through are the claims to verify first.

## 6. What SynthLib needs

**6.1** The contract (`SynthLib/plugin/synthlibPlugin.h`) takes MIDI in (`noteOn`, `noteOff`) but a plug-in
cannot send any out. Needed: a call a plug-in makes during `process()` to emit an event at a sample
offset, and a descriptor flag asking for a MIDI output.

**6.2** VST3: an event OUTPUT bus, and the buffered events copied into the host's output event list at
the end of each block.

**6.3** Audio Unit: a MIDI processor (`aumi`), and the host's MIDI output callback called with the
block's events.

**6.4** A SynthLib change: made in one checkout, pushed by the owner, pulled everywhere.

## 7. Open questions

- Relearn: a button, or automatic when the incoming part changes?
- Pass length and grid resolution as parameters, or fixed at first?
- An editor panel, or the host's generic parameters, to begin with?
- How drum roles (kick, snare, hats, toms) are recognised: a General MIDI map, or learned from position
  and frequency of use?

## 8. Milestones

**8.1 First, something very simple** (owner, 2026-09-12):

- A fixed 4-bar pass (working out the loop's real length comes later).
- The first pass is LEARNED and passed straight through.
- Every later pass is played from what was learned - not echoed from the input, which is ignored from
  then on - with the simplest possible evolution: each note gets a repeat (a ratchet) a 16th after it,
  at 75% of its velocity (a 32nd first; too tight to hear as a repeat on drums).
- Positions count from the bar line where learning began, so a loop brace in the host keeps it in step.
- Two-track routing in Live (§5.2); the VST3 only.

**8.2 Then** - the loop's length found rather than assumed; Density, Fill and Swing (§3); seeded
randomness; an Audio Unit for Logic.

**8.3 Checking** - offline first, with a small test host that feeds a 4-bar part and prints what comes
out (no hung notes, every ratchet where it should be); then by ear in Live.
