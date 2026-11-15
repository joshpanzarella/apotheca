# msfa: the FM core behind FM-6

The synth core of Google's "music-synthesizer-for-android" (msfa), Apache 2.0
(`LICENSE`). Copied from https://github.com/google/music-synthesizer-for-android,
commit f67d41d313b7dc85f6fb99e79e515cc9d208cfff (2017-09-12), folder
`app/src/main/jni`: only the files the voice needs (the note, the operator
kernels and their tables, the envelopes, the LFO). The app, the filter, the
MIDI glue and the ARM32 assembly were left behind.

Nothing here is taken from Dexed, which grew out of msfa: Dexed's own
changes are GPL, and none is copied.

Every file changed for Apotheca says so at its top; what and why is in
`third_party/CHANGES.md`. A fresh copy from the address above loses those.
