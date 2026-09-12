MidiSeqTool - FINDINGS

The history: completed work, what was learned in a host, and the traps that cost real time.


2026-09-12  STARTED: THE HOSTING QUESTION, SYNTHLIB MIDI OUT, THE FIRST LOOP
------------------------------------------------------------------------------------------------------
LIVE CANNOT LOAD A THIRD-PARTY MIDI EFFECT, VST3 or AU - its MIDI-effect slot is for its own devices
and Max for Live. What it does support, and documents, is taking a plug-in's MIDI output from another
track ("MIDI From"), so the plug-in is built as an instrument with a silent audio output and a MIDI
output. AU MIDI out does not reach Live at all. Design §5.

SYNTHLIB HAD NO MIDI OUTPUT. Added: a wantsMidiOut descriptor flag, a "MIDI Out" event bus on the VST3,
and synthlib_plugin_midi_out(), which adds straight to the host's output list for the length of the
process() call it is made in - notes, pressure, controllers and pitch bend. The Audio Unit returns
false until it becomes a MIDI processor.

THE FIRST LOOP (design §8.1) passes its offline check. The test host's own first run failed on its own
unsorted input - a note-on listed after an earlier note's note-off was held back to that note-off's
block - which is worth remembering: feed a plug-in in time order, or the host is the bug.


2026-09-12  IT WORKS IN LIVE 12
------------------------------------------------------------------------------------------------------
The owner heard the ratchets from bar 5, through the two-track routing. Three tries to get there, and
the log (touch /tmp/midiseqtool-log) is what separated them:

- FIRST TRY: the transport was stopped at bar 5 beat 1 - just as playback would have started - and a
  stop forgets the part. Four silent-looking bars read as "not working".
- SECOND TRY, THE REAL TRAP: Track 2's SECOND "MIDI From" chooser was on Pre/Post FX, so it heard the
  clip, not the plug-in. Proved by switching Track 1 to an empty clip: the plug-in's "sent" count went
  on rising at the same rate (it plays from what it learned) while Track 2 fell silent. With the chooser
  on MidiSeqTool, it worked.
- THE RATCHET MOVED FROM A 32nd TO A 16th on the way: 71 ms at 105 BPM reads as a flam on drums, not a
  repeat.

Worth keeping for anything mutating CCs later: Live reportedly forwards a VST3's notes but not its CCs
or pitch bend. Design §5.

