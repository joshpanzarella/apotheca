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
