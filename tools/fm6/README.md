# FM-6's factory sounds

`rom1a.syx` is the first factory cartridge of the classic 1983 six-operator
FM synth (ROM1A: BRASS 1 to TAKE OFF), as one 32-voice sysex bank. It is data,
not code. `tools/fm6_rom.py` reads it and writes `engine/src/fm6/rom1a.hpp`;
nothing reads the file at play time.

Where it came from: Dave Benson's DX7 page, the archive `sysex.tar`
(https://homepages.abdn.ac.uk/d.j.benson/pages/dx7/syx/sysex.tar, fetched
2026-11-15), which says "the original factory patches are here as rom1a.syx -
rom4b.syx". The page calls the collection public domain patches.

- 4104 bytes: the bank's header (F0 43 00 09 20 00), 4096 bytes of 32 packed
  voices, its checksum, F7. **The checksum is right** (0x33).
- sha256 91416e81d0fad931f6c7b5dc5bcfd7b7c48f5340b3753b7d3bf99f439fdd104d.
- Every parameter is inside its range (the script checks).

The copy in msfa's own repository (`app/src/main/res/raw/rom1a.syx`) is the
same but for voice 31, which there is "CHIME-STRG" in place of the
cartridge's "TRAIN", and its checksum is then wrong (0x33 kept, 0x53 due). So
that copy was edited, and this one is used.

Whether the factory sounds may be shipped is the owner's question (the plan,
`docs/plans/fm6-plan.md`): they are widely shared (every FM emulator offers
them) but they are the maker's work.
