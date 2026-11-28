# The WiFi page and bouncing

The PIN, transfers, the designer page, missing recordings, renames, bounces.
Moved here word for word from CLAUDE.md on 2026-11-24 (it had grown to 3,139 lines); CLAUDE.md is the index.

- **The WiFi page is PIN-gated and checks every file.** A program update must
  be accepted on the device (hold A). **Refusing it is hold B a second too** (gesture audit G40,
  2026-11-25; `App::updateDeleting_`): a tap of B deleted the program sent, the one for-good delete that was
  not a hold, and a stray B meant sending it again from the desk. The bar fills red, and the line reads
  `kUpdateDeletingWords` (CANNOT BE UNDONE) while B is held. Of A and B the one pressed last fills; any
  other press calls B's off (A's is as it was). `transfer_test`. **Five wrong numbers lock it for a
  minute** (`Transfer::kWrongMost`, `lockoutMs`), counted for the whole server,
  every request refused with 429 meanwhile, the right number too. Only a path
  whose first part is all digits is a try (a browser asks for `/favicon.ico`
  by itself). **A right number does not begin the count again** (the owner's
  page asks with it all the time: a guesser beside it was never stopped); a
  minute with no wrong number does, and so does a fresh `start`. **An update,
  once accepted, makes the hints bar's end say RESTART TO UPDATE in brass until the program starts again** (owner,
  2026-11-14; `Session::updateReady`, `ui::gUpdateReady`): it takes the help
  words' place and is always drawn, the hints (and a strip's chips, the dice
  at the end too) giving way to it (`hintsEnd`). MENU still opens the help.
  `ui_test` (`testTheBarSaysAnUpdateWaits`) and `transfer_test` hold it;
  stills `update-ready`, `update-ready-drums`, `update-accepted`.
- **`Transfer::stop` never waits on a browser or a SPEED TEST** (review 2026-11-23: it joined the
  server's thread for minutes). It shuts the connection being answered (`serving_`, under
  `servingMutex_`), kills the speed test's process group (`benchGroup_`; started by `posix_spawn` in a
  group of its own, read with `poll`), and the drain after an answer is a second and 64 KB at most. Its
  sockets are close-on-exec. **A song sent is checked by its first line only** (`isSongText`): parsing it
  there read the app's preset lists under the app thread's feet. `transfer_test` `testStopNeverWaits`.
- **The chord packs are read again only when a pack is sent or deleted** (`Transfer::chordsReceived`),
  not for every file. **The WiFi page's address is asked of the system at most once a second**
  (`Session::transferAddress`), and at once after the server starts or stops (another PIN).
- **A song sent under the open song's name is refused** (409, "that song is
  open on the handheld", case not counted: a card may not tell them apart).
  The Session tells the server the name (`Transfer::setOpenSong`, from
  `update()` when it changes and when WiFi goes on). **`saveNow` holds
  `Transfer::songFiles()` while it writes**, as the server does.
- **A folder of recordings is deleted only on purpose** (owner, 2026-11-15: "Keep them"):
  one that loses its last recording, deleted or moved out, stays, on the page as on the
  handheld. `DELETE /PIN/folders/NAME` (`safeFolderPath`: one or two plain names) deletes an
  EMPTY one by `rmdir` (409 with anything in it, 404 for a link or one inside a link); the
  page offers DELETE FOLDER on a heading with no recording and no folder under it. Folders
  made for a request that is then refused are taken away again, and only those (`foldersMade`
  keeps what it made, `dropMade`): a folder that was there before is never touched.
- **The sound designer page knows three kinds of track** (2026-11-16): the spec's `"page"` is
  `"synth"`, `"sampler"` or `"kit"` (`Session::designSpec` / `samplerSpec` / `kitSpec`), and the state's `"spec"` number
  (`designSpecSerial_`) goes up whenever the spec's text changes, which is when the page asks for it
  again. A sampler's spec carries `"waves"`: a list of pictures, each a recording's 512 loudest points
  (`LaneSound::peaks`, made on the app thread when the recording is read; 0 to 1000) and the settings
  that mark it (`"marks"`, ids, kept `"least"` apart by `sampler::heldApart`, the screen's rule too),
  and `"recordings"`, read from the card on the app thread only while the page looks (at once, on
  `Transfer::received()`, then every 5 s). `sample NAME` takes only a name in that list.
  - **The KIT** (owner, 2026-11-16: "I think the kit could also be helpful"): a card a lane, built by
    `laneCard` round the same `wave()`. **A lane's setting is the id `lane * kKitStride + field`**
    (`designer.hpp`: fields 0 to 7 are `LaneEditRow`'s, then `KitField`: FIT, CHOKE, VOLUME, HUMAN
    TIME, HUMAN LEVEL); `"fields"` gives each once, every reading that does not hang on the lane with
    it (START and END are a time into the lane's own file: the state gives the handheld's eight
    readings a lane, `laneEditText`). `Session::serveKit` routes `set` by lane into the calls the
    handheld makes (`setLaneEdit`, `setLaneFit`, `setLaneChoke`, `setLaneLevel`, `setLaneFeel`);
    `lanesample L NAME` (only a name offered, or none: the drum), `hear L 1|0`, `normalize L`,
    `trim L`. A synth's or a sampler's asks do nothing to a kit, but `name`. **VOLUME is
    `Session::setLaneLevel`, which the drum grid now calls too.**
  - **The picture is the EDIT view's**: the lane's whole source (`LaneSound::sourcePeaks`, made on the
    app thread when the lane is read; `serveDesigner` readies every lane while the page looks), the
    part played lit by GAIN and the fades. The page's `guarded` and `levelAt` copy `lane_edit.cpp`'s
    `guarded` and `laneEditLevelAt` for that: **change them together** (tried against the C++ numbers
    in the browser, 2026-11-16). So an edit changes no spec; another sound does: the spec is made
    again when any lane's `LaneSound::version` moves (their sum), and its number goes up only when its
    text changes.
  - **A synth lane on the page (DRUM SYNTH, batch 4)**: the SOUND choice lists the twelve models after the
    built-in drum (`lanesynth L WORD`), a synth lane's card has its named voices (`lanevoice L N`) and its
    knobs. **A knob's id is `kKitKnobs + lane * 8 + place`** (designer.hpp: after every lane's fields, so
    the ids before stayed); its name is in the spec's wave (`"synth"`), its reading in the state. **A synth
    lane's picture, length and least gap are in the STATE (`lanes[].synth`), not the spec**: a knob turned
    changes the sound, and with the picture in the spec every turn sent a new spec and the page was built
    again under the slider being dragged. `design_test` `testKitSynthFromThePage`; the script was tried in
    Chromium with Playwright (2026-11-17: the choice, a named voice, a knob, a recording made a CLAP, the
    drum back, no script errors).
  - **PLAY held is `hearLaneHeld` with the Designer as its owner**, and `keepHearing` leaves that one
    to the page (X is not held for it): it is let go by `hear L 0`, by the page going away, or by
    another track on the handheld.
  - `design_test` `testSamplerFromThePage`, `testKitFromThePage`; `APOTHECA_DESIGN_SAMPLER=1` or
    `APOTHECA_DESIGN_KIT=1` with `APOTHECA_DESIGN_HOLD` holds a page open for a browser (the KIT's
    lanes are printed whenever one changes on the handheld).
- **The pages' layout and look (web redesign, owner, 2026-11-28: "laid out logically ... minimal vertical scroll
  ... more in the vein of the shapetaker family").** Every endpoint and every guard is as it was; only the layout,
  the look and three additions below changed.
  - **One look for every page** (`app/web_look.cpp`): each page is one raw string marked `page` with `@@LOOK@@`
    (the shared style sheet), `@@BRAND@@` (the emblem, the Shapetaker wordmark, APOTHECA) and `@@FONT@@` (the
    lettering's loader) in it, filled once by `withWebLook` (`pageWithLook` in transfer.cpp: a function's static,
    made once whichever thread asks first). The colours are the BROWN theme's surfaces with the Shapetaker
    accents: near-black leather (two SVG `feTurbulence` grains, under 1 KB, no picture), brass edges and screws
    on every plate, cream lettering, teal `#00ffb4` for "on" and the main key, purple for the loose recordings,
    MORPH's B and the UPDATE key, amber for a refusal. `--dim` lettering is 6:1 or more on its ground.
  - **The device's lettering** (Routed Gothic Narrow) is served at `/PIN/font.ttf`, whole and unchanged (the
    OFL: a subset or a WOFF would be a Modified Version, under the font's reserved name), **kept by the browser
    a year** (`Cache-Control: private, max-age=31536000, immutable`; every other answer stays `no-store`): a weak
    WiFi pays its 57 KB once. `Transfer::setLettering` takes the bytes before `start` (the server's thread only
    reads them); `Session::toggleTransfer` reads them once with `loadFontFile()`. None: 404, and the pages keep
    the browser's letters. It is for the page's own words: **a file's name, a size, a number typed or read
    (a setting's value, a picture's marks) are in the browser's plain letters** (`--data`), because the
    lettering's 1 reads as an I ("round I", "I6 B").
  - **The files page on a computer** (960 px wide and 560 high or more) **is one screen high**: the header
    (version, free space, SOUND DESIGNER, SPEED TEST, UPDATE APOTHECA), then RECORDINGS as the big plate on the
    left (the folders down its left side, each a place to drop on; the folder picked on the right; FIND shows
    every match in every folder, under its folder's name), and down the right SEND TO THE HANDHELD, SENT AND
    DONE (only when there is something), and SONGS / BOUNCES / CHORD PACKS as tabs of one plate (the last one
    looked at kept in `localStorage`). **Every list scrolls inside its own plate**: nothing is below the
    screen's foot (measured at 1440x900: BOUNCES was 221 px down with one folder open and 1,953 px with every
    folder open, UPDATE 944 and 2,676; now 0, the page does not scroll). The folder plan and the update are
    boxes over the page. On a phone it is one column: SEND first, then the recordings (the folders a row to
    swipe along), then the tabs.
  - **The recordings as folders down the left** replace the open-and-shut folders: CHOOSE ALL, ADD FILES and
    DELETE FOLDER are on the folder shown, dropping and dragging onto a folder work on its name in the list,
    and **+ NEW FOLDER** in that list names one there (as RECORDINGS GO IN's NEW FOLDER: made on the card when
    the first recording lands in it). Files sent are shown in the folder they go into; a move does not change
    the folder shown.
  - **The door**: a GET of the bare address, or of a wrong number with nothing after it, is answered with a
    page that asks for the number (still a 403, and a wrong number still counted). Anything else keeps the
    plain sentence. The door loads no lettering: asking for `/NUMBER/font.ttf` would be another try.
    `transfer_test` holds both.
  - **SPEED TEST has a page of its own** (`/PIN/speed`), which reads `/bench` as it comes and puts its parts in
    plates, the numbers in teal, three across on a computer; SAVE AS TEXT keeps what was printed (a second
    `/bench` would time it all again). `/bench` is as it was.
  - **The sound designer** is one screen high on a computer too: the picture (a recording, CLOUD's band,
    MORPH's tables) and the eight MACROS stay in view, and the handheld's pages, EFFECTS (the track's three
    slots, then SENDS AND MIXER, the buses, the master) and MODS are tabs (ALL PAGES: every one, two across).
    **A KIT is eight pads down the left** (number, sound, a small picture, a PLAY to hold) and the lane picked on
    the right: its sound, its picture, LANE, EDIT and VOICE side by side. Every lane's card is still built and
    kept up to date; only the one picked is shown (the ids `sound L`, `play L`, `normalize L`, `voice L` are as
    they were). The tab and the lane picked are kept in `localStorage`.
  - **A fault found on the way**: the KIT page did not draw at all with a built-in drum on any lane (every fresh
    kit): `laneCard` greyed FIT with `box.querySelector('select')`, and FIT is a slider, so it threw, and
    `look()` said "The handheld is not answering". Now `querySelector('select, input')`. A MOD's SHAPE, PHASE
    and STEPPER rows were never hidden either (`.fxkind`'s `display:flex` beat `hidden`): the shared look's
    `[hidden]{display:none!important}` hides them.
  - Sizes served (bytes, no compression; the server sends none): files page 42,252 before, 59,570 after (18,778
    gzipped, for comparison); designer 51,278, then 72,775; the speed test page 17,306 (before: plain text); the
    door 14,729; the lettering 57,532 once. The growth is the shared look (about 9 KB) and the wordmark (3.6 KB)
    on the designer.
  - Tried in Chromium with Playwright against a `Transfer` started by a small program with made-up content
    (forty-odd recordings in folders, songs, bounces, chord packs) and against `design_test`'s held pages (every
    kind), 2026-11-28: send into a folder, find, rename, move by the form and by dragging onto a folder,
    download, play, several moved into a NEW FOLDER and deleted, delete (asked once more), + NEW FOLDER, ADD
    FILES, DELETE FOLDER, a song sent, fetched and deleted, the three tabs, a folder's plan, the update (a WAV
    refused), a 17 MB upload under way (its percent) and saved, SPEED TEST's page to `done`; on the designer a
    macro, a tab, a setting, a held key heard and let go, a picture clicked, ALL PAGES, an effect put in a
    slot, a MOD made a STEPPER; on the KIT a pad picked, VOLUME, START dragged, a pad's PLAY and key 3 heard,
    a recording on a lane, NORMALIZE, a named voice. No script errors. Not tried: Safari, Firefox, a real
    phone, the handheld's own WiFi.
- **The page's own script has no test in `ctest`.** A change to it is tried
  in a browser (Playwright is installed) against a `Transfer` started by a
  small program, as the folder import was (`planFolders` can be called from
  `page.evaluate`). Say in the reply that it was, or was not.
- **The WiFi server's thread never touches the `Project`.** What it did to a
  recording goes on a list (`Transfer::takeChanges`), put there BEFORE
  `received()` goes up; `Session::update` reads it and calls
  `recordingChanged`. A rename changes the name only: reading the file again
  would cut the sound. It is no step of undo, and every step of undo follows
  it (`undoFollows`; songs-and-undo.md), as does a recording `findMissing` finds. A recording SENT (no old name) is read again on every
  lane and sampler track that names it, so a replaced file is heard at once.
- **A missing recording is looked for by base name, and taken only if there
  is exactly one** (`Session::findMissing`, run after a song or a rev opens,
  not from `applyProject`). It lists the samples folder once, and not at all
  when nothing is missing. Never guess between two.
- **A rename on the page rewrites the saved songs and revs** on the app
  thread, a few files a frame (`Session::followLater`, as a carry; `renameRecordings`): only the name on
  `sample` and `sound` lines, the rest byte for byte, the file's date kept.
  The open song's own file is left to the autosave. It holds
  `Transfer::songFiles()`, which the server holds while it writes or deletes
  a song: anything new that writes songs off the app thread takes it too.
- **MIDI export and an audio bounce play a cell's FILL on the scene's last time round** (2026-11-25;
  songs-and-undo.md "The FILL"). MIDI export gathers a scene once and repeats it; where a cell has a FILL
  on, it gathers the last time round again with it (`gatherMidiScene`'s `round`). An audio bounce plays the
  song through an engine of its own, which is sent the FILL with the rest (`fillapp_test` `testMidi`,
  `testBounce`).
- **A bounce runs until its tail ends** (owner, 2026-11-12; `BounceTail` in
  `session.hpp`, no engine and no file in it, tested with made-up tails).
  Sound is `kBounceFloor` (one step of a 16-bit file) or more; it is over
  after `kBounceQuietSeconds` (4) without any, and the file ends half a second
  after the last sound. **Four seconds must stay longer than the longest gap
  between two echoes** (3 s: a half note at 40 BPM), as the bus rest time
  must. The cap (`kBounceCapSeconds`, 480) must stay longer than
  `kLongestTailSeconds` in the engine; both are checked.
- **A bounce is written as it goes**, to `NAME.wav.part`, and renamed when it
  is whole: a tail of minutes never sits in memory, and a cancelled bounce
  leaves nothing: **an EACH TRACK cut short deletes the tracks it had finished** (2026-11-25, with hold B
  on SET's BOUNCE, G15: they were a part of the stems, never padded to one length). **Hold B's stop is
  `stopBounce`, which only sets the cancel** (review of G15, 2026-11-25): it called `cancelBounce`, which
  joins the thread on the app thread, and the thread cannot leave loading a track half-way (a new
  Engine, `applyProject` reading the samples from the card, `finishStretching` stretching every FIT
  lane; `padWav` too), so a stray bounce stopped at once froze the screen and the buttons until the load
  ended (this line said it "waits for that block": wrong). Now `bouncing()` stays true until the thread
  sets `done`, `bounceStopping()` meanwhile: the row's note says STOPPING IT: NOTHING IS KEPT, B's hint
  goes, and A starts nothing. The thread looks at the cancel once a block (256 frames) and after
  loading each track, before its file is begun. `cancelBounce` (closing down, `~Session`; a still) still
  stops and joins, and marks the job done (a pretended one has no thread to). One that is KILLED (power, a crash) leaves its `.wav.part`:
  `removeHalfBounces` takes those away when the program opens
  (`useBounceFolder`) and when a bounce starts, never while one is under way. The tracks of an EACH TRACK bounce are padded with silence
  to the longest. Its percentage counts the song's own playing only; in a
  tail `bounceTail()` gives the seconds so far and the row says that instead.
- **Do not pipe the app's output into `head`** when making a still; it dies
  before it saves.
- **BOUNCE: TRACK TO SAMPLES** (batch S3, 2026-11-25; sampling-gaps.md 4; `Session::Bounce::ToSamples`, appended, the
  row's fifth choice: the other four work as they did). **A loop of the track being worked on, into the samples
  folder**: the track alone (muted or not: the copy mutes every other), the scene being worked on played round
  (Play with the scene looping), **the first time round thrown away and the second kept**, so what rings past the
  scene's end is in its start, as when it loops; mono (L + R halved, as CATCH), 16-bit 48 kHz (`writeWav`). **1, 2 or
  4 whole bars** (`samplesBounceSteps`: the most the scene has within `kCatchLongest`, 20 s; a scene under a bar,
  all of it). **It leaves the cell's FILL out** (integration review, 2026-11-25; the copy turns an on FILL off): a loop
  is the plain cell. Kept in, the FILL played in the round kept only at REPEATS 1 or 2 (that round is the last), so
  the same cell bounced two ways; `testABounceIntoSamplesLeavesTheFill` pins REPEATS 1, 2 and 4. Claude's pick, 2026-11-25, open to the owner: the scene and not the song, since a recording is 20 s at
  most and FIT 64 steps; 1, 2 or 4 bars so a tap of A on FIT (G45, `laneFitFor`) fits it to its bars exactly (3 bars
  would be fitted to 4), at the scene's own tempo where it has one, which the bounce is made at (review fix,
  2026-11-25: the tap read the song's; `bounceSampleSteps` is still read only by the test). **Named after the track** (`bounces/bass 1`, the next free number, `freeSampleName`),
  chosen on the app thread when it starts, in `samples/bounces` where the list sees it: the app thread bumps
  `sampleFiles_` the frame after it is done (`BounceJob::seen`), so an open list reads the card again. It needs no
  `bounces` folder beside the program. **It is not put on a lane by itself** (SET has no lane): pick it, then a tap
  of A on FIT. The row's note: `A: this scene's loop, into recordings`, then `done: "bounces/bass 1" in recordings`.
  Stopped with hold B as any bounce (nothing written). `sample_test` `testBouncingIntoSamples`; `textsize` (the row).
- **A bounce is a TAKE** (owner, 2026-11-28: "a bounce could start its dice from a new number each time, so each
  bounce is a fresh take. It could also say which take it is in the file name ("song take 3"), so a favourite can be
  made again"). Before, a bounce's own engine started the chance dice at the same constant every time and every other
  roll was seeded from position and pass, so bouncing a song twice gave the same file.
  - **`Engine::setTake(n)`** (only before the first render, on the thread that renders: the bounce's own engine) starts
    the dice at `takeSeed(kDiceStart, n)` and mixes the take into every per-note seed the engine hands out:
    `chordSeed` (HUMAN TIME / LEVEL, LENGTHS, strum, an arpeggio's order), `glideSeed` (GLIDE FROM RANDOM),
    `strikeSeed` (every synth's `noteSeed`: PHYSIC, CLOUD, MORPH), `feelSeed` (both of FEEL's sites) and the MODs
    (`modOffsetAt`'s `take`, down to `modSeed`: RANDOM, DRIFT, a STEPPER's CHANCE and its SLEW's value before).
    **`takeSeed(seed, 0) == seed`**: take 0 is the live engine and every pinned render, to the bit (`effects_test
    --print`, `idle_test` untouched). A new roll in the engine goes through `takeSeed(..., take_)` too, or every take
    of it bounces the same (CLAUDE.md). The screens' own copies of the rules (glide lines, the MOD tab) stay take 0:
    they draw what plays live.
  - **Kept in the song**: `takes N`, the last take made, a song-own line after `scale` (`cutSongAt` knows it), written
    only when N > 0, so a song never bounced is written as before; held to 0..`kMostTakes`. Not a side file in the
    bounces folder (Claude's choice): the count then follows a rename, a copy sent over WiFi and SEL + Y's revs, and
    needs no second file to keep in step. It is no step of undo (undo puts back the song's own lines it names, one by
    one: the master's mix, AT END, the scale, REPEATS and the lengths; `takes` is not one), so undoing past a bounce
    never hands out its take again. Raised on the app thread when a bounce
    STARTS (a stopped one spends its take too: the next is a fresh number either way); the autosave writes it.
  - **Names**: THE SONG `<song> take 3.wav`, a track `<song> take 3 - 1 bass.wav` (EACH TRACK and THIS TRACK, all of
    one go the same take, so stems line up with the song's take). **A name already in the folder gets the time after
    the take** (`bounceNameTaken`: `<song> take 3.wav` for THE SONG, anything starting `<song> take 3 - ` for tracks),
    so a remade take never writes over the first (a song of the same name, or the song changed since). **MIDI FILE has
    no take** (no chance in it, `midiFileBytes`): its name is as before (`<song> 261128-141500.mid`) and no take is
    spent. **TRACK TO SAMPLES** takes one too, after the number: `bounces/bass 1 take 3` (`freeSampleName`'s `after`);
    the number counts the track's loops whatever their take (a number any `bass 1` or `bass 1 take ...` has is taken),
    so it reads 1, 2, 3 and not 1, 1, 1.
  - **The gesture: hold A + UP / DOWN on SET's BOUNCE row picks the take** (`Session::pickBounceTake`; UP a later one,
    stopping at 1 and at the next new one; the stick's up and down too, a held D-pad at the list's pace), as NETWORK's
    hold A + UP / DOWN picks a network. The press calls A's tap off (G15), so the let go bounces nothing; a tap of A
    then makes the take picked (`startBounce(what, take)`; 0 or past the last is a new one). The row reads `THE SONG
    TAKE 3`, its note `A: that take again` while an earlier one is picked, else (once there is an earlier take) `hold A +
    up / down: earlier takes`; with A held the hints are `UP / DOWN take`. (A `HLD-A take` hint was tried and ran
    the bar 31.6 px over at XL with L1 / R1 and SEL: `app_test`'s every-row check.) **A bounce started, or another song (`applyProject`), puts the pick back on the new one.** Not on MIDI
    FILE (no take: up / down move the cursor). Claude's pick, open to the owner.
  - Tests: `app_test` `testBounceTakes` (names, the count, take 1 again the same to the bit, a new take other hits, MIDI,
    TRACK TO SAMPLES, the song file and a save and an open, the gesture), `engine_test` `testChance` (the dice),
    `chordfx_test` `testTheNumbers` (`takeSeed`) and `testTheSameEveryTime`, `feel_test` `testTakes`, `glide_test`
    `testRandomIsRolledForItsPlace`, `physic_test` `testSeeds`, `mod_test` `testTakes` and `testTheSameInAnyBlocks`
    (a song with no roll is the same in every take). Still `bounce-take`.
