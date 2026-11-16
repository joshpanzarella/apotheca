# FM-6's factory sounds

`rom1a.syx` is the first factory cartridge of the classic 1983 six-operator
FM synth (ROM1A: BRASS 1 to TAKE OFF), as one 32-voice sysex bank. `rom1b.syx`,
`rom2b.syx`, `rom3b.syx` and `rom4b.syx` are four more banks from the same
archive (added 2026-11-16), each its own folder on the device, ROM 1B to ROM 4B,
after ROM 1A. They are data, not code. `tools/fm6_rom.py` reads them and writes
`engine/src/fm6/roms.hpp`; nothing reads the files at play time.

Where it came from: Dave Benson's DX7 page, the archive `sysex.tar`
(https://homepages.abdn.ac.uk/d.j.benson/pages/dx7/syx/sysex.tar, fetched
2026-11-15), which says "the original factory patches are here as rom1a.syx -
rom4b.syx". The page calls the collection public domain patches.

- 4104 bytes: the bank's header (F0 43 00 09 20 00), 4096 bytes of 32 packed
  voices, its checksum, F7. **The checksum is right** (0x33).
- sha256 91416e81d0fad931f6c7b5dc5bcfd7b7c48f5340b3753b7d3bf99f439fdd104d.
- Every parameter is inside its range (the script checks).

The four banks added on 2026-11-16, from the same `sysex.tar` (fetched that day),
each 4104 bytes with the same header and **a right checksum**:

| File | Folder | First / last voice | sha256 |
| --- | --- | --- | --- |
| `rom1b.syx` | ROM 1B | PIANO 4 / BASS 4 | 15c817360f56f7bd0e269575291cbfac91a45f0ee6125479abfdc4d69a560b4b |
| `rom2b.syx` | ROM 2B | SYN-LEAD 2 / EXPLOSION | 1efb00f24e28df03470ebca19cadd63cb98b5f099b5e7502dd6f232ddb23af54 |
| `rom3b.syx` | ROM 3B | PIANO 2 / PLUCK BASS | 0f21e5f28f0b6c77ef5cef839bb244d8946b2ee2243e88e1ba19f46c4aa85e30 |
| `rom4b.syx` | ROM 4B | CLAV-E.PNO / EXPLOSION | d4eae13570569e27894a21593a9c591a474a1b945c13c3a0a933c42dee684c0b |

**Three values in ROM 3B are past their range** as the bank has them, and the
script holds each to its top (as the engine would): E.GRAND 1's OP6 R1 (127 to
99), HONKY TONK's KEY SYNC (3, a stray bit above it, to 1), 60-S ORGAN's OP1
FINE (100 to 99). Any other value out of range still stops the script.

Names are as ROM 1A's: the cartridge's ten letters with their gaps closed up
(`" ".join(name.split())`). Some names are in two banks (EXPLOSION, SHIMMER,
WATER GDN, ...); eight voices are a byte-for-byte copy of one before them,
ROM 3B's BANJO (1B's) and seven of ROM 4B (HARP-FLUTE, BELL-FLUTE, ORCHESTRA,
SHIMMER, WATER GDN, WASP STING, OCTAVE WAR, of 1A's or 2B's), kept so each bank
is whole.

The archive has three more banks, `rom2a.syx`, `rom3a.syx` and `rom4a.syx` (96
voices, ten of them copies of another bank's), **not taken**: the job asked
for the B sides. One line each in the script's `BANKS` would append them.

The copy in msfa's own repository (`app/src/main/res/raw/rom1a.syx`) is the
same but for voice 31, which there is "CHIME-STRG" in place of the
cartridge's "TRAIN", and its checksum is then wrong (0x33 kept, 0x53 due). So
that copy was edited, and this one is used.

Whether the factory sounds may be shipped is the owner's question (the plan,
`docs/plans/fm6-plan.md`): they are widely shared (every FM emulator offers
them) but they are the maker's work.
