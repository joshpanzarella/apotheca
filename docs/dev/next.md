# Next, in the owner's order

The road map and what is open.
Moved here word for word from CLAUDE.md on 2026-11-24 (it had grown to 3,139 lines); CLAUDE.md is the index.

## Next, in the owner's order

1. The effects plan (`docs/plans/effects-plan.md`): all built and joined (0.452): the seven families and their 27 types.
2. Twin Analog became a JX-3P clone, **JAX-EP on the device** (`docs/plans/jx3p-plan.md`; branch `jx3p`: **batches 0 to 8c are done (2026-11-15)**: the engine, the app, the 32 presets, the tests off Twin, Twin's code out, the docs. Not joined to `main`, nobody has heard any of it, SPEED TEST has not run on the handheld. Left: batch 9, calibration from recordings, which waits on the owner's source; and the owner's open calls in the plan's section 8).
3. LFO and random stepper per track (`docs/plans/modulation-plan.md`): batch 0, the groundwork, is built (branch `mods`: the rule, `giveLane`, DETUNE mended, QMOD's early return, the cost row), and batch 1, the engine (the tick, `runMods`, the commands, X held while stopped); their review is mended (the plan's section 14); batch 2, the song, is built too (`TrackData::mods`, `Session::setMod`, the `mod` / `modsteps` words, undo, the TYPE carry: section 15); **batches 3 and 4 are built (2026-11-15, the owner's calls A to H taken; section 18): X held on a knob is its MOD, on DEVICE and FX**; **batch 5, the MOD tab after FX, is built (2026-11-15, section 20)**; next, batch 6 (the brass mark on the target's own bar). **Joined with `main` (2026-11-15)**, and the join's review mended (section 17: no defect, the bench's MOD rows timed in turns): FEEL, AT END, the mixer, the picker, JAX-EP and Twin's removal sit beside the MODs; nobody has heard a MOD.
4. **PHYSIC** (owner, 2026-11-14): a physical-model synth for plucked and struck sounds (strings, mallets, bells). Plan: `docs/plans/physic-plan.md`. **Built and joined: the engine (2026-11-15, 24 presets) and the app (2026-11-16). Batch 10 (BODY, PAIR, MOTOR, NOISE, BUZZ, a fifth page and 8 presets: 32) on branch `physic2`, 2026-11-16. Nobody has heard it, not run on the handheld.**
5. **An acid mono bass** (303-style: accent, slide; **ACID, built 2026-11-16, batches 0 to 5 of `docs/plans/acid-plan.md`: the engine, the app, 32 presets, README. Nobody has heard it, not run on the handheld; batch 6, calibration from recordings, waits on the owner's source**), then **a granular synth** (clouds from the recordings) (owner, 2026-11-14; **CLOUD, planned 2026-11-16 in `docs/plans/granular-plan.md`; its engine and its app built on branch `cloud` (batches 0 to 2; the pool 24, not 32), not heard, not run on the handheld; batch 3's 24 presets, batch 4's designer page and batch 5's review and README too; the owner's three open questions are in the plan's batch 5 section**).
7. **FEEL on the drum grid** (owner, 2026-11-14; `docs/plans/drum-feel-plan.md`): HUMAN TIME and HUMAN LEVEL for each lane, live, the main beats tighter. **Built on branch `feel` (2026-11-15), not heard by anyone**. **SWING built on branch `swing` (2026-11-17: batches 0 to 3 of the plan's SWING section), not joined, not heard**; the owner's seven calls are in the plan.
6. **The sample picker in columns** (owner, 2026-11-14; `docs/plans/picker-plan.md`): three columns as yazi, folders two levels deep, new folders and moving recordings on the device. The owner asked for it to go soon: after the JAX-EP's presets. **Batches 0 to 2 are built on branch `picker`** (two levels underneath, the three columns, + NEW FOLDER, carry with hold R1, hold B on an empty folder), not joined, not tried on the handheld; batch 3, the review and its mend, is done (2026-11-15).

8. **The efficiency pass, then FM-6** (owner, 2026-11-15, after SPEED TEST on the handheld: it is
   13 to 20 times slower than the desk, not 5, and the busiest song stutters even with helpers):
   a third helper timed beside two, undo that reads back only what changed (200 ms on the
   handheld), the busiest song profiled, nothing worked out on silence; then QMOD replaced by
   FM-6, a 6-operator FM synth on msfa (Apache 2.0, the core inside Dexed; never Dexed's GPL
   code), with the 32 sounds of the first factory cartridge. **The engine part is built (branch
   `fm6`, 2026-11-15; `docs/plans/fm6-plan.md`)**: `third_party/msfa` (changes in
   `third_party/CHANGES.md`, built with `-fwrapv`), `engine/src/fm6.cpp`, the 32 written by
   `tools/fm6_rom.py`, `fm6_test`. **The app side is built too (2026-11-15): FM-6 is in QMOD's
   place and QMOD is gone** (its code, its test, `third_party/shapetaker`, ROLL). Not heard,
   not run on the handheld; the owner's questions are in the plan's section 9.
9. **The RG505 on the road map** (owner, 2026-11-15: "could be the perfect handheld"; it stays on
   the RG40XXV for now). Unisoc T618 (2 x A75 + 6 x A55 at 2.0 GHz, about 3 to 4 times the
   speed on a big core), 960x544 OLED, a headphone jack, 5000 mAh, **Android 12 only**: a port
   through SDL2's Android side (sound, files, the WiFi page, which core the sound runs on).
   Keep the engine free of Linux-only calls where it costs nothing.
10. **Our own firmware for the RG40XXV, Apotheca and nothing else** (owner, 2026-11-16; `docs/plans/firmware-plan.md`): a Buildroot image on the mainline kernel with ROCKNIX's H700 patches. Batch 0 (ROCKNIX on a spare card, SPEED TEST there) is the owner's and has not happened. **Batch 1 is built on branch `firmware` (2026-11-17)**, tested on the desk only (section 8 of the plan): `os/` is a Buildroot external tree, `scripts/build-image.sh` writes `build-image/apotheca-rg40xxv.img.gz`, `os/tools/check-image.sh` and `os/tools/qemu-test.py` are its desk tests, `os/README.md` is the owner's page. ROCKNIX's kernel patches are vendored in `os/board/rg40xxv/patches/linux` (eight refreshed so they apply with no fuzz: the same tree to the byte; `os/board/rg40xxv/README.md` says where every file came from and its licence). **The image must stay reproducible** (the same `img.gz` from two builds): nothing made at build time may write the clock in. **Batch 2 is built too (2026-11-17, section 9)**: Apotheca starts at boot under `apotheca-keep` on SDL's KMSDRM through Mesa (panfrost; softpipe after two quick stops or with `gpu-off` on the card), `controls-rg40xxv.txt` numbered as SDL numbers the joypad, `S20sound` sets the mixer. **Buildroot's Mesa package is patched** (`os/patches-to-buildroot`: no LLVM on the card), and the first build takes about three hours (host LLVM and clang). Not run on a handheld. **Batch 4, power, is built too (2026-11-17, section 10)**: on the firmware SEL + START turns the handheld off (`app/power_off.hpp`: the system sets `APOTHECA_OFF_MARK` and `APOTHECA_IDLE_OFF_MINUTES`; the app writes the mark after `saveNow`; the port sets neither, so quitting there is unchanged), `apotheca-power` has the power key (tap: the screen off and the joypad held; held 2 s: off) and the battery (5%: off), and **every way off goes through `poweroff`, whose rcK runs `S95apotheca` first so Apotheca saves**. `os/tools/power-test.py` is its desk test. Suspend is opt-in and untried.
11. **A drum synth** (owner, 2026-11-17): a kit lane's sound made from knobs. **Batches 0 to 5 built** (`docs/plans/drumsynth-plan.md`: twelve models, the KIT page's SOUND column and VOICE view, 73 named voices, the designer page, the review); all joined to main (0.700). Not heard, not run on the handheld; the owner's twelve calls are with the owner.
12. **A wavetable synth, MORPH** (owner, 2026-11-17: "let's do the wavetable after that"; `docs/plans/wavetable-plan.md`): batches 0 to 5 built and joined to main (0.718): the rule, the tables and the voice, the app, the 32 presets, the designer page's picture, the review and the qemu bit check. Left: batch 6, a player's own table from a recording (the owner's call 7). Not heard, not run on the handheld; the owner's 17 calls are at the plan's picks.
13. **More scenes: 64** (owner, 2026-11-17: "would it be possible to push the max scenes past 16?"; `docs/plans/scenes-plan.md`): built on branch `scenes` (batches 0 to 5): a scene's patterns are made when something is first written there, in the app and in the engine, so the 48 more cost nothing until used. Not joined, not run on the handheld; the owner's four calls are at the plan's picks.

14. **BACKUP and RESTORE on the WiFi page** (owner, 2026-11-28: "for later"; "easy access to a tool that can do all
of that for them instead of doing it manually and missing something"). Not built, not planned yet. The idea: one
button saves the whole `apotheca` folder (songs, samples, presets, bounces, recordings, settings) to the computer as
one zip with the date in its name; RESTORE takes such a zip back, shows what it holds, and puts it back (all of it,
or only the songs or only the samples), never overwriting without saying so. It needs the memory guard's care (a
zip can be bigger than free memory: stream it, never hold it whole) and the same checks as an upload. It would
also make INSTALL.md's "back up the apotheca folder before a new card image" one click.

Open: FM-6's banks ROM2A, 3A and 4A (in the same archive; not taken: the job asked for the B sides), more presets, own WiFi hotspot, an automation
step-editor, a picture of FM-6's ALGORITHM (its wiring; msfa's table says each). **Twin Analog is gone** (owner, 2026-11-11: rethink it; 2026-11-13: it
becomes a faithful Roland JX-3P clone, which is what it was meant to be). The
clone is JAX-EP, built in batches 0 to 8c of `docs/plans/jx3p-plan.md` on
branch `jx3p`; each batch's "what it learned" is there. The owner's calls
(plan section 8) are taken at the recommended answers, each one constant in
`jx3p.hpp` ("THE OWNER'S CALLS") or one named place. The engine stays at
48 kHz: 44.1 would save about 8%, which the owner judged not worth it.
