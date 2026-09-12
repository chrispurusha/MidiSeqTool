# MidiSeqTool

A MIDI effect plug-in that listens to a MIDI part for a number of bars, passes it through unchanged the
first time, and on every pass after that plays it back **mutated** - fills, extra notes woven into the
groove, changes of feel, rising and falling intensity - steered by a handful of macro dials.

**Status: the first, simplest version.** It learns four bars, passes them through, then plays them
back with a ratchet after every note. VST3 only; in Ableton Live it is used through two-track routing
(see [`Docs/design.md`](Docs/design.md) §5). `tools/msqtest` checks it offline.

## Relationship to the sibling projects

One of a family that shares [SynthLib](https://github.com/chrispurusha/SynthLib) and its format-free
plug-in wrappers:

- **G2-Edit** - editor for the Nord G2, and the G2 Alike plug-in
- **SynthEdit** - generic multi-synth editor
- **EmuUtility** - E-mu EOS sampler utility
- **GenBridge** - bridges any CoreAudio device into a DAW
- **MidiSyncTool** - MIDI clock from the host's timeline; this project started from its skeleton

## Building

```
./do-plugin            # VST3 and Audio Unit, and installs them
./do-plugin vst3       # one format
./do-plugin --no-install
```

The VST3 half needs the VST3 SDK at `~/Documents/vst3sdk` (or `$VST3_SDK`); the Audio Unit needs none.

## Licence

GPLv3 - see [`LICENSE`](LICENSE). Third-party components and their redistribution obligations are in
[`THIRD_PARTY.md`](THIRD_PARTY.md).
