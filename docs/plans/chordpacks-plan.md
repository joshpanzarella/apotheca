# Chord packs: thirteen more (2026-11-17)

The owner: "let's take a look at the chord packs. I want to include a bunch more.
the ones on there now are a good start, but I think we need more chords."

**Built on branch `chordpacks`. Nobody has heard any of it**: the chords are
checked by arithmetic (every note against its quality, its scale and its name),
not by ear.

## How packs work today (found before starting)

- **A pack is a JSON file** in the form Shapetaker's Transmutation reads
  (schema 2). Apotheca reads `name`, `sourceTonic`, `scale.pitchClasses` and
  each chord's `root`, `intervals`, `quality` / `legacyName`
  (`app/chords.hpp`). Progressions, guidance, functions and tags are
  Transmutation's; Apotheca ignores them.
- **Where they came from**: a `chords` folder beside the program (the zip
  and our firmware copy `assets/chords/*.json` there), or `assets/chords`
  from a build folder. Plus "Plain C", built in. The boot line
  `chord packs: 7` was Plain C and the six files.
- **An update over WiFi brought no pack**: it replaces the program only
  (README: "chord packs still come from the zip").
- **A song stores each chord whole** (its root, notes and name) and the pack
  by NAME (`pack NAME`). So nothing is stored by a chord's place in a pack:
  chords may be added, moved or renamed freely; an old song keeps its chords.
  A pack's name is what a song finds it by: keep the names.
- **Picking**: hold Y on the roll; left / right the chord, up / down the
  inversion, L1 / R1 the pack (round and round), let go to place. The SET
  page's CHORD PACK row steps the same list. The strip shows seven tiles 88
  pixels across, scrolled about the one in hand, and "8 / 23". It is a tight
  row (always LARGE), so TEXT SIZE does not change it.
- **Limits**: Transmutation takes 6 notes a chord, 24 semitones, 128 chords,
  32 progressions of 64 parts. Apotheca takes 8 notes up to 48. A chord's name
  is 15 letters at most; a pack's 24 (the song keeps 31).

## What was missing, and the new packs

The six were all lush or open colours in major, dorian, lydian, mixolydian
and pentatonic: no jazz cadence, no blues, no minor key with a real dominant,
nothing dark, nothing for a stab. Thirteen packs, all in C as the six are (the
roll has no transpose, so one key keeps packs side by side in a song):

| Pack | Scale | Chords | Progressions | What it is for |
| --- | --- | --- | --- | --- |
| **Smoke** | major | 23 | 4 | jazz: ii-V-I with 9ths and 13ths, altered and b9 dominants, the tritone sub (Db9), secondary dominants, the backdoor (Fm6, Bb7), the passing C#dim7 |
| **Velvet** | dorian | 18 | 3 | neo-soul / R&B: minor 9ths and 11ths, maj9 and maj9#11, the Neapolitan Dbmaj7, an altered V |
| **Revival** | major | 18 | 4 | gospel: walk-ups through C#dim7 and F#dim7, F/G, the I13 to IV, the iii-VI-ii-V, the flat-six shout (Ab9) |
| **Ember** | phrygian | 17 | 3 | phrygian: Db over Cm, the Spanish turn down to C major, C7sus4b9 |
| **Elegy** | natural minor | 23 | 4 | minor and harmonic minor: G7b9, Bdim7, CmMaj7, the line cliche, the lament, the Neapolitan |
| **Horizon** | natural minor | 19 | 3 | cinematic: wide fifths and octaves, i-VI-III-VII, major IV, chromatic mediants (E, Abm, A) |
| **Haze** | major | 15 | 3 | ambient: close seconds, sus stacks, chords over a held C (G/C) |
| **Strobe** | natural minor | 15 | 3 | deep house stabs: close m7, m9, m11, maj7 and maj9, the dorian IV9 |
| **Delta** | mixolydian | 18 | 3 | blues: every chord a seventh, the twelve-bar (with its passing F#dim7), 9ths, 13ths, C7#9, a minor blues |
| **Skyline** | major | 22 | 4 | city pop: IVmaj7-V7-iii-vi, the night-drive turn (Fmaj7 E7 Am7 Gm7 C9), F/G, Fm6 |
| **Hearth** | major | 17 | 3 | folk: open triads voiced as a guitar holds them, sus and add9 shapes, Bb |
| **Hollow** | harmonic minor | 17 | 3 | dark: CmMaj7 and CmMaj9, augmented and diminished, minor chords a third and a tritone away |
| **Stone** | dorian | 15 | 3 | quartal: stacks of fourths (3, 4 and 5 high), the fourths-and-a-third m11, 9sus4 |

Modal: dorian (Moonlit), lydian (Luminous) and mixolydian (Tidal) were there
already; Ember adds phrygian, Velvet and Stone are dorian too.

Voices: 3 to 6 a chord (JAX-EP has six voices; nothing needs more). Within a
pack the chords run by their root's degree, so left / right climbs the scale.

## Owner's calls (taken at the picks; say if any is wrong)

1. **The packs are compiled into the program** (pick: yes). `app/chord_packs.inc`
   holds every `assets/chords/*.json`, written by `tools/chord_packs.py`, so an
   update over WiFi brings them. A file on the card still adds a pack, and one
   with a shipped pack's name takes its place (pick: the card's wins, so an
   edited copy can be tried). The zip and the firmware still copy the files.
   *Effect on the owner's handheld*: its card has the six old files, so after a
   WiFi update those six are the card's (the old names, next call) until the
   zip is copied again or they are deleted; the thirteen new ones are there.
2. **All in C** (pick: C, as the six). Other keys would be copies of the same
   files with another `sourceTonic`.
3. **Five chord names in the old packs corrected** (pick: yes). Their notes are
   unchanged. Three said less than they play (Weightless: Dsus4 plays D G A E,
   now Dsus4add9; Gsus4 plays an F, now G7sus4; Fsus2 plays a Bb, now
   Fsus2add11). Three did not fit their 88-pixel tile and were cut on the
   strip (Afterglow's Cmaj7add13 and Fmaj13(no5), now Cmaj13 and Fmaj13;
   Glass's C(open) and Cadd9(open), now C and Cadd9). Borrowed notes now say
   which they are (`outside`). Shapetaker's copies in its own repository are
   not changed.
4. **The names**: Smoke, Velvet, Revival, Ember, Elegy, Horizon, Haze, Strobe,
   Delta, Skyline, Hearth, Hollow, Stone.
5. **L1 / R1 still steps one pack a press** (pick: leave it). Twenty packs, and
   round both ways, so ten presses at most. Holding to repeat is the next thing
   if it is slow in the hand.
6. **The progressions are for Transmutation** (pick: leave them so). Apotheca
   does not lay one down; that would be its own feature.

## The checks (`chord_test`)

- `testEveryShippedChordIsWhatItSays`: each chord worked out again from its
  quality (a table of the notes each quality must have and may have; "(open)"
  is a voicing, "(no3)" and "(no5)" take a note out) and its root; every note
  in the pack's scale, or the chord says it is borrowed and lists exactly the
  notes outside; the name the strip shows is the root as the scale spells it
  (a flat second is Db, never C#) and the quality, or a slash chord over its
  root; 1 to 6 voices, the root at the bottom, two octaves at most; required
  notes in the chord; no id, name or chord twice; 7 to 24 chords; progressions
  name real chords. Ten planted faults in a good pack must each be caught.
- `testTheShippedPacksAreTheFiles`: the compiled-in packs are the files.
- `testACardPackTakesAShippedOnesPlace`.
- `testTheNamesFitTheStrip`: every chord name fits its tile, every pack name
  fits between L1 and R1 before the count, measured with the program's lettering.
- Still `chords-smoke`: SMOKE's tiles scrolled to its middle, on Db9.
