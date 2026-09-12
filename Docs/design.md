# MidiSeqTool - design

Living design note, started 2026-09-12. Code refers to it by section (`// §3.2`).

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
changed it).

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
