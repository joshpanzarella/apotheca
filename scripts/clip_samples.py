#!/usr/bin/env python3
"""Recordings for the feature clips (scripts/make-clips.sh), made from nothing, into FOLDER:

  break.wav     a mono drum break, two bars at 120 BPM (kicks, snares, hats): uneven hits for SLICES AT THE HITS
  pingpong.wav  a stereo ping-pong, one bar at 120 BPM: a plucked note on each sixteenth, left, right, left...
                (MONO folds it to the middle, which is the point of the STEREO clip)

usage: clip_samples.py FOLDER            both
       clip_samples.py --stereo FOLDER   the ping-pong alone (the stills put a folder's first recording on a lane)
"""
import math
import os
import random
import struct
import sys
import wave

RATE = 48000
BEAT = 0.5  # 120 BPM


def write(path, left, right=None):
    with wave.open(path, "wb") as w:
        w.setnchannels(1 if right is None else 2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        frames = bytearray()
        for i in range(len(left)):
            frames += struct.pack("<h", max(-32767, min(32767, int(left[i] * 32767))))
            if right is not None:
                frames += struct.pack("<h", max(-32767, min(32767, int(right[i] * 32767))))
        w.writeframes(bytes(frames))


def drum_break():
    n = int(RATE * BEAT * 8)
    out = [0.0] * n
    rnd = random.Random(7)

    def add(at, length, voice):
        start = int(at * RATE)
        for i in range(int(length * RATE)):
            if start + i < n:
                out[start + i] += voice(i / RATE)

    kick = lambda t: 0.9 * math.sin(2 * math.pi * (50 * t + 90 * (1 - math.exp(-t * 30)) / 30)) * math.exp(-t * 9)
    snare = lambda t: (0.5 * (rnd.random() * 2 - 1) + 0.3 * math.sin(2 * math.pi * 190 * t)) * math.exp(-t * 18)
    hat = lambda t: 0.25 * (rnd.random() * 2 - 1) * math.exp(-t * 60)
    eighth = BEAT / 2
    for bar in range(2):
        b = bar * BEAT * 4
        for k in (0, 1.5, 2.75) if bar == 0 else (0, 1.5, 2.25, 3.5):
            add(b + k * BEAT, 0.4, kick)
        for s in (1, 3):
            add(b + s * BEAT, 0.3, snare)
        for e in range(8):
            add(b + e * eighth, 0.08, hat)
    return out


def ping_pong():
    n = int(RATE * BEAT * 4)
    left, right = [0.0] * n, [0.0] * n
    notes = [69, 72, 76, 79, 76, 72, 74, 77]
    for e in range(16):
        start = int(e * BEAT / 4 * RATE)
        f = 440 * 2 ** ((notes[e % 8] - 69) / 12)
        side = left if e % 2 == 0 else right
        for i in range(int(0.12 * RATE)):
            if start + i < n:
                t = i / RATE
                side[start + i] += 0.6 * math.exp(-t * 14) * (math.sin(2 * math.pi * f * t) + 0.3 * math.sin(4 * math.pi * f * t))
    return left, right


def main():
    args = sys.argv[1:]
    stereo_only = args[:1] == ["--stereo"]
    if stereo_only:
        args = args[1:]
    if len(args) != 1:
        sys.exit(__doc__)
    folder = args[0]
    os.makedirs(folder, exist_ok=True)
    if not stereo_only:
        write(os.path.join(folder, "break.wav"), drum_break())
    l, r = ping_pong()
    write(os.path.join(folder, "pingpong.wav"), l, r)


if __name__ == "__main__":
    main()
