# Tests

How the tests are written and run, and the traps in them.
Moved here word for word from CLAUDE.md on 2026-11-24 (it had grown to 3,139 lines); CLAUDE.md is the index.

### Tests
- **The order of `a + f() + g()` is the compiler's.** GCC (our firmware's app, this desk) and clang
  (the port, the Mac) take the parts in other orders. A shared buffer passed in and also written by
  another call in the same expression is a bug on one of them: the designer page's `slotText` lost its
  "SLOT 1" name on GCC only (fixed 2026-11-24). Take such a value into a string on its own line first.
- **A test must not hang on the desk's ports, or on what a parallel test holds.** The WiFi page takes
  port 80 where it can (root, our firmware) and 8080 otherwise; a check of its address follows the form
  drawn (`textsize`). Run `ctest -j` and read the WHOLE failure list (`tail` cut it off once).
- **Mutation testing is the standard.** After a test passes, break the code it
  covers and see it fail. A surviving mutant usually means the fixture cannot
  reach the branch.
- **Do not compare audio sample for sample across separate notes** unless the
  engine is fresh; compare loudness, attack, or zero crossings. Let auditions
  end before listening.
- **An `Engine` is about 1.4 MB: a test keeps at most three on the stack
  at once** (`std::unique_ptr` for more). Six were 8.37 MB, over the main
  thread's 8 MB once MOD grew the Engine by a kilobyte, and `engine_test`
  crashed with no word, five runs in six (2026-11-14). `g++ -fstack-usage`
  on a test file says each function's frame.
- **Every test has a two-minute limit**, so a hang is a failure. A new ctest
  entry gets its `TIMEOUT 120` in CMakeLists.txt (launcher and words had none
  until 2026-11-14). **`effects_test` runs as two halves** (`--part 1`,
  `--part 2`; part 2 from `testFamiliesAndTypes`, so a test added at the end
  is in part 2; about 34 s each on a quiet desk, 2026-11-14). Move that line
  if one half grows much longer than the other.
- **Checksums of sound only hold on the machine's own maths library**
  (`tanh`, `pow`, `exp` differ in the last bit elsewhere, and compilers join
  a multiply and an add differently on ARM). `effects_test` checks which it is
  on (`mathsPrint`) and falls back to loudness and peak, saying so. To prove a
  change is bit-exact on another machine, run `--print` before and after and
  compare.
- **`effects_test --only testDuck testTape`** runs just those (a second or
  two, for trying a change); `ctest` runs them all. `through()` there hits a
  drum five times a second, lanes 1 and 8, so the checks for every effect
  reach DUCK; no other effect hears it.
- **The measuring tools are one copy, in `tests/measure.hpp`** (JX-3P plan,
  batch 0): a tool that reads a run of samples and knows no Effect or Synth
  goes there, not into a test program of its own. Moved out of `effects_test`
  unchanged: `--measure` and `--print` gave the same lines before and after
  (but for the block timings). The older copies in `engine_test`
  and `phazed_test` are other tools under the same names
  (interleaved buffers, int places) and were left. Batch 2 added `partNear`
  (one frequency through a window, any pitch), `spectrumOf` and `asideOf`
  (an oscillator's aliasing: the loudest part that is not a harmonic, under
  a frequency). **A band-limited wave's zero crossings are not its pitch near
  the top** (its ringing crosses too): read it through `bandOf` first.
  `ringOnOf` (2026-11-25, HALL) says how much a reverb's tail rings on the same
  notes: the spectrum's fine structure, second after second. **The loudest note
  against its neighbours in one window does not**: it reads 10 to 13 dB in any
  tail, moving or not.
- **A `bench` row that is the difference of two small figures times the two
  in turns** (many short rounds, each its best: `modsAlone`, `macroModsRun`).
  Timed one after the other, a busy desk moved the whole row more than the
  difference: 24 MODs read -0.04% in one run and +0.10% in the next.
- **Numbers measured from today's effects are written in `effects_test` as
  comments and checked** (`--measure` prints them). A new effect's row in the
  plan's testing table is checked with the same tools.
- When replacing a function by text range, bound it by **the next function in
  the file**, then count the functions before and after.
- **A test's temporary folder comes from `tempFolder("name")`**
  (`tests/temp_folder.hpp`): `$TMPDIR` or /tmp, `apotheca-name-XXXXXX`,
  deleted with all in it when the program ends normally (never before, so a
  second Session may open it again). A crash or a timeout keeps it (nothing
  is deleted from a signal handler); `APOTHECA_KEEP_TEMP=1` keeps them for a
  look. Links are deleted as links, never followed. **`ctest -R temp` fails a
  test source that says `mkdtemp(` or `"/tmp/apotheca`**, and a shell test
  whose `mktemp` has no `trap ... rm -rf` (2026-11-16: the tests had left
  33,000 folders, 16 GB, and filled the disk mid-build). A test that writes
  "../x" to prove a path is refused does it one folder down inside its own.

**A still is taken at an exact moment** (2026-11-28). In a still (`--shot`, `--texts`, `--smoke`) the App's clock
moves in whole 16 ms frames from the first pass, at most one a pass and never ahead of the real clock, and the picture
is taken when that clock reaches `--still-ms` (`app/main.cpp`, `stillClock`). So a hold that the still shows filling
draws the same bar on every run, however busy the desk. Before, the picture came at a real-time moment: on a busy
desk `fx-deleting`'s bar was a different width each run, and about one run in 24 its edge crossed the slot's "1",
which `textviews` read as text out of its box (seen in two full runs). Checked: 32 runs of `fx-deleting` under load,
four at a time, gave the same text file to the byte (they had given eight different ones).
