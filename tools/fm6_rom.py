#!/usr/bin/env python3
"""Makes engine/src/fm6/roms.hpp: FM-6's factory sounds (docs/plans/fm6-plan.md).

Reads the factory banks in tools/fm6 (rom1a.syx, then ROM 1B, 2B, 3B and 4B,
appended 2026-11-16), each one 32-voice sysex bank (where they came from:
tools/fm6/README.md), checks them, unpacks each voice into its 155 parameters
and writes them out as one table, a bank after another. A preset's place is in
songs: a bank is appended, never put in before another.

The checks, each a stop if it fails:
  - each file is the one the README names (its sha256);
  - a 32-voice bank's header (F0 43 00 09 20 00), 4096 bytes, its checksum
    (the low 7 bits of minus the sum of the 4096), F7;
  - every parameter, unpacked, inside its range.

The unpacking is the bank format's (published in the original's manual): each
voice is 128 bytes, six operators of 17 (OP6 first) and 26 of the voice's own,
with some pairs of small values sharing a byte. Unpacked, a voice is the 155
parameters in the order the original numbers them (0 to 125 the operators,
OP6 first, 21 each; 126 to 144 the voice's; 145 to 154 the name), which is the
order of FM-6's settings.

Run it again only to change the table: python3 tools/fm6_rom.py
"""
import hashlib
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "engine", "src", "fm6", "roms.hpp")
# The banks in their order on the device: the file, its folder, its sha256. APPENDED, never reordered.
BANKS = [
    ("rom1a.syx", "ROM 1A", "91416e81d0fad931f6c7b5dc5bcfd7b7c48f5340b3753b7d3bf99f439fdd104d"),
    ("rom1b.syx", "ROM 1B", "15c817360f56f7bd0e269575291cbfac91a45f0ee6125479abfdc4d69a560b4b"),
    ("rom2b.syx", "ROM 2B", "1efb00f24e28df03470ebca19cadd63cb98b5f099b5e7502dd6f232ddb23af54"),
    ("rom3b.syx", "ROM 3B", "0f21e5f28f0b6c77ef5cef839bb244d8946b2ee2243e88e1ba19f46c4aa85e30"),
    ("rom4b.syx", "ROM 4B", "d4eae13570569e27894a21593a9c591a474a1b945c13c3a0a933c42dee684c0b"),
]

# Each operator's 21, in order, with its top value.
OP_PARAMS = [("R1", 99), ("R2", 99), ("R3", 99), ("R4", 99), ("L1", 99), ("L2", 99), ("L3", 99), ("L4", 99),
             ("BREAK POINT", 99), ("LEFT DEPTH", 99), ("RIGHT DEPTH", 99), ("LEFT CURVE", 3), ("RIGHT CURVE", 3),
             ("RATE SCALING", 7), ("AMS", 3), ("VELOCITY", 7), ("OUTPUT LEVEL", 99), ("MODE", 1),
             ("COARSE", 31), ("FINE", 99), ("DETUNE", 14)]
VOICE_PARAMS = [("PR1", 99), ("PR2", 99), ("PR3", 99), ("PR4", 99), ("PL1", 99), ("PL2", 99), ("PL3", 99), ("PL4", 99),
                ("ALGORITHM", 31), ("FEEDBACK", 7), ("KEY SYNC", 1), ("LFO SPEED", 99), ("LFO DELAY", 99),
                ("LFO PMD", 99), ("LFO AMD", 99), ("LFO SYNC", 1), ("LFO WAVE", 5), ("PMS", 7), ("TRANSPOSE", 48)]
TOPS = [top for _ in range(6) for _, top in OP_PARAMS] + [top for _, top in VOICE_PARAMS]
assert len(TOPS) == 145


def unpack(b):
    """One voice's 128 bytes as its 145 parameters and its name."""
    p = []
    for op in range(6):
        o = b[op * 17:(op + 1) * 17]
        p += list(o[0:11])                      # R1-4, L1-4, BREAK POINT, LEFT DEPTH, RIGHT DEPTH
        p += [o[11] & 3, (o[11] >> 2) & 3]      # LEFT CURVE, RIGHT CURVE
        p += [o[12] & 7]                        # RATE SCALING
        p += [o[13] & 3, o[13] >> 2]            # AMS, VELOCITY
        p += [o[14]]                            # OUTPUT LEVEL
        p += [o[15] & 1, o[15] >> 1]            # MODE, COARSE
        p += [o[16], o[12] >> 3]                # FINE, DETUNE
    p += list(b[102:110])                       # the pitch envelope
    p += [b[110], b[111] & 7, b[111] >> 3]      # ALGORITHM, FEEDBACK, KEY SYNC
    p += list(b[112:116])                       # LFO SPEED, DELAY, PMD, AMD
    p += [b[116] & 1, (b[116] >> 1) & 7, b[116] >> 4]  # LFO SYNC, WAVE, PMS
    p += [b[117]]                               # TRANSPOSE
    name = bytes(b[118:128]).decode("ascii")
    return p, name


# The values past their range in the banks as they are, each held to its top (tools/fm6/README.md):
# (file, voice from 1, parameter, the value in the bank).
HELD = {
    ("rom3b.syx", 2, 0, 127),    # E.GRAND 1, OP6 R1: 127, held to 99
    ("rom3b.syx", 4, 136, 3),    # HONKY TONK, KEY SYNC: 3 (a stray bit above it), held to 1
    ("rom3b.syx", 15, 124, 100), # 60-S ORGAN, OP1 FINE: 100, held to 99
}


def read_bank(file, sha):
    """One bank's 32 voices, checked: (name, parameters) each."""
    data = open(os.path.join(HERE, "fm6", file), "rb").read()
    if hashlib.sha256(data).hexdigest() != sha:
        raise SystemExit("%s: not the file tools/fm6/README.md names (sha256)" % file)
    if len(data) != 4104 or data[:6] != bytes([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00]) or data[-1] != 0xF7:
        raise SystemExit("%s: not a 32-voice bank" % file)
    bank = data[6:6 + 4096]
    if (-sum(bank)) & 0x7F != data[6 + 4096]:
        raise SystemExit("%s: the bank's checksum is wrong" % file)
    voices = []
    for v in range(32):
        params, name = unpack(bank[v * 128:(v + 1) * 128])
        for i, (value, top) in enumerate(zip(params, TOPS)):
            if not 0 <= value <= top:
                # Three values of ROM 3B are past their range as the bank has them (README.md): each is
                # held to it, as the engine would hold it, and only those three are let through.
                if (file, v + 1, i, value) not in HELD:
                    raise SystemExit("%s, voice %d, parameter %d: %d is over %d" % (file, v + 1, i, value, top))
                params[i] = top
        if not all(32 <= ord(c) < 127 for c in name):
            raise SystemExit("%s, voice %d: a name that is not plain letters" % (file, v + 1))
        voices.append((" ".join(name.split()), params))  # gaps closed up: "BRASS   1" is BRASS 1 (owner, 2026-11-15)
    return voices


def main():
    voices = []
    for file, folder, sha in BANKS:
        voices += [(folder, name, params) for name, params in read_bank(file, sha)]

    lines = [
        "#pragma once",
        "// MADE BY tools/fm6_rom.py from the banks in tools/fm6: do not edit by hand.",
        "// FM-6's factory sounds: the first factory cartridge (ROM 1A), then ROM 1B, 2B,",
        "// 3B and 4B, each bank in its own order. Each is the 145 parameters in the",
        "// original's order (0 to 125 the operators, OP6 first, 21 each; 126 to 144 the",
        "// voice's): FM-6's settings 0 to 144. A preset's place is in songs: APPENDED.",
        "",
        "namespace apotheca {",
        "namespace fm6 {",
        "",
        "struct RomVoice {",
        "    const char* folder; // the bank: \"ROM 1A\"",
        "    const char* name;   // as the cartridge has it, its gaps closed up",
        "    signed char value[145];",
        "};",
        "",
        "constexpr int kRomVoices = %d;" % len(voices),
        "",
        "constexpr RomVoice kRom[kRomVoices] = {",
    ]
    for folder, name, params in voices:
        ops = ["/**/ " + ", ".join(str(x) for x in params[op * 21:(op + 1) * 21]) for op in range(6)]
        lines.append('    {"%s", "%s", {%s, /**/ %s}},' % (folder, name, ", ".join(ops)[5:], ", ".join(str(x) for x in params[126:])))
    lines += ["};", "", "} // namespace fm6", "} // namespace apotheca", ""]
    with open(OUT, "w") as f:
        f.write("\n".join(lines))
    print("wrote", os.path.relpath(OUT), "(%d voices)" % len(voices))


if __name__ == "__main__":
    main()
