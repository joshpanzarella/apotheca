#!/usr/bin/env python3
"""Makes engine/src/fm6/rom1a.hpp: FM-6's 32 factory sounds (docs/plans/fm6-plan.md).

Reads tools/fm6/rom1a.syx, the first factory cartridge as one 32-voice sysex
bank (where it came from: tools/fm6/README.md), checks it, unpacks each voice
into its 155 parameters and writes them out as a table.

The checks, each a stop if it fails:
  - the file is the one the README names (its sha256);
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
SYX = os.path.join(HERE, "fm6", "rom1a.syx")
OUT = os.path.join(HERE, "..", "engine", "src", "fm6", "rom1a.hpp")
SHA256 = "91416e81d0fad931f6c7b5dc5bcfd7b7c48f5340b3753b7d3bf99f439fdd104d"

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


def main():
    data = open(SYX, "rb").read()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise SystemExit("not the file tools/fm6/README.md names (sha256)")
    if len(data) != 4104 or data[:6] != bytes([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00]) or data[-1] != 0xF7:
        raise SystemExit("not a 32-voice bank")
    bank = data[6:6 + 4096]
    if (-sum(bank)) & 0x7F != data[6 + 4096]:
        raise SystemExit("the bank's checksum is wrong")
    voices = []
    for v in range(32):
        params, name = unpack(bank[v * 128:(v + 1) * 128])
        for i, (value, top) in enumerate(zip(params, TOPS)):
            if not 0 <= value <= top:
                raise SystemExit("voice %d, parameter %d: %d is over %d" % (v + 1, i, value, top))
        if not all(32 <= ord(c) < 127 for c in name):
            raise SystemExit("voice %d: a name that is not plain letters" % (v + 1))
        voices.append((name.rstrip(" "), params))

    lines = [
        "#pragma once",
        "// MADE BY tools/fm6_rom.py from tools/fm6/rom1a.syx: do not edit by hand.",
        "// FM-6's 32 factory sounds, the first factory cartridge in its own order. Each",
        "// is the 145 parameters in the original's order (0 to 125 the operators, OP6",
        "// first, 21 each; 126 to 144 the voice's): FM-6's settings 0 to 144.",
        "",
        "namespace apotheca {",
        "namespace fm6 {",
        "",
        "struct RomVoice {",
        "    const char* name; // as the cartridge has it, the spaces at its end taken off",
        "    signed char value[145];",
        "};",
        "",
        "constexpr int kRomVoices = 32;",
        "",
        "constexpr RomVoice kRom1a[kRomVoices] = {",
    ]
    for name, params in voices:
        ops = ["/**/ " + ", ".join(str(x) for x in params[op * 21:(op + 1) * 21]) for op in range(6)]
        lines.append('    {"%s", {%s, /**/ %s}},' % (name, ", ".join(ops)[5:], ", ".join(str(x) for x in params[126:])))
    lines += ["};", "", "} // namespace fm6", "} // namespace apotheca", ""]
    with open(OUT, "w") as f:
        f.write("\n".join(lines))
    print("wrote", os.path.relpath(OUT), "(%d voices)" % len(voices))


if __name__ == "__main__":
    main()
