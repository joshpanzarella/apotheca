// GLIDE in the app: what the roll draws is what the engine plays, with the Session's own
// commands between them; and the song file. The gesture is in tests/app_test.cpp, the
// engine's side in tests/glide_test.cpp.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "chords.hpp"
#include "song_file.hpp"

using namespace apotheca;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

namespace {

const int kStep = 6000; // at 120 BPM, 48 kHz

// A synth that makes no sound and writes down what it is told.
struct Began {
    long frame;
    int pitch, from; // from -1: a plain note
    float seconds;
};
struct Spy : Instrument {
    std::vector<Began>* log;
    long frame = 0;
    explicit Spy(std::vector<Began>* to) : log(to) {}
    void prepare(float) override {}
    void selectPreset(int) override {}
    void setSetting(int, int) override {}
    void setMacro(int, float) override {}
    void noteOn(int note, float) override { log->push_back({frame, note, -1, 0.f}); }
    void noteOnFrom(int note, float, int from, float seconds) override { log->push_back({frame, note, from, seconds}); }
    void noteOff(int) override {}
    void allNotesOff() override {}
    void render(float* left, float* right, int frames) override {
        for (int i = 0; i < frames; ++i) left[i] = right[i] = 0.f;
        frame += frames;
    }
};

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    std::vector<Began> log;
    Spy* spy = nullptr;

    void frames(int count) { for (int i = 0; i < count; ++i) { clock += 16; app.update(input, clock); } }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void render(long audioFrames) {
        std::vector<float> out(2 * 1000);
        for (; audioFrames > 0; audioFrames -= 1000) engine->render(out.data(), audioFrames < 1000 ? static_cast<int>(audioFrames) : 1000);
    }
    Project& project() { return app.project(); }
    // The synth on track 2 taken out of the engine, and one that only listens put there.
    void listen() {
        log.reserve(100000);
        frames(2);
        render(2000);
        Command c;
        c.type = Command::RemoveInstrument;
        c.track = 1;
        CHECK(engine->send(c));
        render(1000);
        spy = new Spy(&log);
        c.type = Command::InstallSynth;
        c.pointer = spy;
        CHECK(engine->send(c));
        render(1000);
        engine->collectGarbage();
    }
};

std::uint32_t seed = 0x51ed270bu;
int roll(int outOf) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<int>((seed >> 12) % static_cast<std::uint32_t>(outOf));
}
bool about(double a, double b) { return std::fabs(a - b) <= 1e-6 * (std::fabs(b) > 1.0 ? std::fabs(b) : 1.0); }

// Songs made up at random on the roll's own data (notes, chords, ratchets, nudges, cells of
// one to three bars, a scene cut short), sent by the Session, played by the engine. For
// every note: the line the roll would draw (Project::glideLines) names the key the engine
// slid from and the time it took.
void testTheRollDrawsWhatTheEnginePlays() {
    Rig rig;
    CHECK(rig.app.session().addTrack(TrackKind::Qmod) && isSynth(rig.project().tracks[1].kind));
    rig.listen();
    Session& session = rig.app.session();
    int lines = 0, fromChords = 0, acrossTheEnd = 0, notesHeard = 0, spread = 0, fromSpread = 0;
    for (int song = 0; song < 60; ++song) {
        Project& project = rig.project();
        session.setCellBars(1, 0, 1 + roll(4));
        const int bars = project.tracks[1].bars[0]; // (one, two or four)
        session.setSceneSteps(0, project.sceneBars(0) * kSteps - (song % 4 == 3 ? 1 + roll(9) : 0)); // sometimes cut short
        const int sceneSteps = project.sceneSteps(0);
        bool chordKey[kMaxBars][128] = {};
        for (int bar = 0; bar < bars; ++bar) {
            NotePattern& p = project.tracks[1].notes[0][bar];
            p = NotePattern();
            if (roll(3) != 0) {
                p.chords[0] = makeChord(builtInPack().chords[static_cast<std::size_t>(roll(4))], 48 + roll(12), roll(16), 1.f, 0.7f, roll(3) - 1);
                p.chords[0].nudge = roll(3) == 0 ? static_cast<float>(roll(9) - 4) * 0.1f : 0.f;
                p.chords[0].ratchet = static_cast<std::uint8_t>(roll(3) == 0 ? 2 + roll(3) : 0);
                // Chord effects, half the time: strummed, an arpeggio, humanised. A chord is one moment
                // to a glide however its notes are spread, so nothing below changes for them.
                if (roll(2) == 0) p.chords[0].play = chordPlayOf(roll(kStrumKinds), roll(kOrderKinds), kAmountAt[roll(kAmountClicks)], kAmountAt[roll(kAmountClicks)], kAmountAt[roll(kAmountClicks)]);
                spread += p.chords[0].play.timed();
                int keys[kChordNotes];
                const int n = p.chords[0].pitches(keys);
                for (int k = 0; k < n; ++k) chordKey[bar][keys[k]] = true;
            }
            const int count = 2 + roll(12);
            for (int i = 0; i < count; ++i) {
                const int slot = roll(kMaxNotes);
                if (p.notes[slot].velocity > 0.f) continue;
                NoteEvent n;
                n.step = roll(4) == 0 ? roll(2) * 15 : roll(16);
                n.pitch = 62 + roll(24);
                n.length = 0.5f;
                n.velocity = 0.8f;
                n.nudge = roll(3) == 0 ? static_cast<float>(roll(9) - 4) * 0.1f : 0.f;
                n.ratchet = static_cast<std::uint8_t>(roll(3) == 0 ? 2 + roll(3) : 0);
                n.glide = static_cast<std::uint8_t>(roll(5) == 0 ? 0 : 1 + roll(5));
                bool clash = chordKey[bar][n.pitch];
                for (const NoteEvent& o : p.notes) clash = clash || (o.velocity > 0.f && o.pitch == n.pitch);
                if (!clash) p.notes[slot] = n;
            }
        }
        session.sendNotePattern(1, 0);
        rig.frames(2);
        rig.render(1000);
        std::vector<GlideLine> drawn(kMaxBars * kMaxNotes);
        project.glideLines(1, 0, drawn.data());
        rig.log.clear();

        const long began = rig.spy->frame;
        session.togglePlay(false);
        rig.frames(1);
        rig.render(static_cast<long>(3 * sceneSteps + 2) * kStep);
        session.togglePlay(false);
        rig.frames(1);
        rig.render(2000);
        CHECK(session.engine.dropped() == 0);
        for (int bar = 0; bar < bars && bar * kSteps < sceneSteps; ++bar) {
            const NotePattern& p = project.tracks[1].notes[0][bar];
            for (int slot = 0; slot < kMaxNotes; ++slot) {
                const NoteEvent& n = p.notes[slot];
                if (n.velocity <= 0.f || n.step >= project.barSteps(0, bar)) continue;
                const GlideLine& line = drawn[static_cast<std::size_t>(bar * kMaxNotes + slot)];
                const long at = began + std::lround((2.0 * sceneSteps + bar * kSteps + n.step + static_cast<double>(n.nudge)) * kStep);
                const Began* heard = nullptr;
                for (const Began& b : rig.log)
                    if (!heard && b.pitch == n.pitch && std::labs(b.frame - at) <= 2) heard = &b;
                CHECK(heard != nullptr);
                ++notesHeard;
                if (!heard) continue;
                const bool same = heard->from == line.from && (line.from < 0 || about(heard->seconds, line.steps * 0.125));
                CHECK(same);
                if (!same) std::printf("  song %d bar %d step %d key %d (nudge %.1f, x%d, glide %d): the engine from %d for %.4f s, the roll from %d for %.4f s\n", song, bar, n.step, n.pitch, n.nudge, n.ratchet, n.glide, heard->from, heard->seconds, line.from, line.steps * 0.125);
                if (n.glide == kGlideOff) CHECK(line.from < 0);
                lines += line.from >= 0;
                fromChords += line.from >= 0 && chordKey[bar][line.from];
                fromSpread += line.from >= 0 && chordKey[bar][line.from] && p.chords[0].play.timed();
                acrossTheEnd += line.from >= 0 && n.ratchet < 2 && n.glide == kGlideFull && bar * kSteps + n.step + n.nudge - line.steps < -0.01;
            }
        }
    }
    CHECK(lines > 400 && fromChords > 40 && acrossTheEnd > 3); // the songs did hold what this is for
    CHECK(spread > 20 && fromSpread > 10); // ...chords strummed and humanised among them, with glides out of them
    if (failures || std::getenv("GLIDE_PRINT")) std::printf("  the roll and the engine: %d lines, %d from a chord's key, %d across the loop's end, of %d notes\n", lines, fromChords, acrossTheEnd, notesHeard);
}

// A chord's keys are in the slots NotePattern::render gives them, and what follows slides from its top one.
void testAGlideFromAChord() {
    std::unique_ptr<Project> project(new Project);
    project->tracks[1].kind = TrackKind::Qmod;
    NotePattern& p = project->tracks[1].notes[0][0];
    p.chords[0] = makeChord(builtInPack().chords[0], 48, 2, 2.f, 0.7f, 0);
    int keys[kChordNotes];
    const int n = p.chords[0].pitches(keys);
    int top = 0;
    for (int k = 0; k < n; ++k) top = keys[k] > top ? keys[k] : top;
    p.notes[0] = NoteEvent{6, 80, 1.f, 0.f, 0.8f};
    p.notes[0].glide = kGlideFull;
    p.notes[5] = NoteEvent{0, 70, 1.f, 0.f, 0.8f};
    p.notes[5].glide = kGlideTwo;
    std::vector<GlideLine> lines(kMaxBars * kMaxNotes);
    project->glideLines(1, 0, lines.data());
    CHECK(n >= 3 && lines[0].from == top && lines[0].steps == 4.0);
    CHECK(lines[5].from == 80 && lines[5].steps == 2.0); // into step 1, from the loop's last note
    for (int i = 1; i < kMaxNotes; ++i) CHECK(i == 5 || lines[static_cast<std::size_t>(i)].from == -1); // a chord's own keys do not slide
    // Two bars: the second bar's lines are in its own rows of the answer.
    project->tracks[1].bars[0] = 2;
    project->tracks[1].notes[0][1].notes[3] = NoteEvent{0, 60, 1.f, 0.f, 0.8f};
    project->tracks[1].notes[0][1].notes[3].glide = kGlideHalf;
    project->glideLines(1, 0, lines.data());
    CHECK(lines[kMaxNotes + 3].from == 80 && lines[kMaxNotes + 3].steps == 0.5 && lines[5].from == 60 && lines[5].steps == 2.0);
    // A number in a note that no length has: half a step, as the engine takes it.
    p.notes[0].glide = 77;
    project->glideLines(1, 0, lines.data());
    CHECK(lines[0].from == top && lines[0].steps == 0.5);
}

// A track sent whole (a song opened, an undo, a cell made longer: Session::sendTrackImage) keeps
// each glide's length. Nothing here goes by sendNotePattern: the engine has only the whole track.
void testATrackSentWhole() {
    Rig rig;
    Session& session = rig.app.session();
    CHECK(session.addTrack(TrackKind::Qmod));
    rig.listen();
    Project& project = rig.project();
    CHECK(project.tracks[1].bars[0] == 1);
    NotePattern& p = project.tracks[1].notes[0][0];
    p = NotePattern();
    project.tracks[1].notes[0][1] = NotePattern();
    const int kinds[5] = {kGlideTwo, kGlideFull, kGlideQuarter, kGlideOne, kGlideHalf};
    const double seconds[5] = {0.25, 0.375, 0.03125, 0.125, 0.0625}; // (FULL: the three steps since the note before)
    p.notes[0] = NoteEvent{0, 60, 1.f, 0.f, 0.8f};
    for (int i = 0; i < 5; ++i) {
        p.notes[i + 1] = NoteEvent{3 + 3 * i, 62 + 2 * i, 1.f, 0.f, 0.8f};
        p.notes[i + 1].glide = static_cast<std::uint8_t>(kinds[i]);
    }
    session.setCellBars(1, 0, 2); // the second bar is filled from the first, and the track goes whole
    CHECK(project.tracks[1].bars[0] == 2 && project.tracks[1].notes[0][1].notes[3].glide == kGlideQuarter);
    rig.frames(2);
    rig.render(2000);
    rig.log.clear();
    session.togglePlay(false);
    rig.frames(1);
    rig.render(40L * kStep);
    session.togglePlay(false);
    rig.frames(1);
    rig.render(2000);
    CHECK(session.engine.dropped() == 0);
    int heard[5] = {0, 0, 0, 0, 0};
    for (const Began& b : rig.log)
        for (int i = 0; i < 5; ++i) {
            if (b.pitch != 62 + 2 * i) continue;
            CHECK(b.from == 60 + 2 * i && about(b.seconds, seconds[i]));
            ++heard[i];
        }
    for (int i = 0; i < 5; ++i) CHECK(heard[i] >= 2); // (both bars)
}

// The song file: a glide's length is the last number of its notefx line. 1 is what ON was.
void testTheSongFile() {
    Rig rig;
    CHECK(rig.app.session().addTrack(TrackKind::Qmod));
    Project& project = rig.project();
    NotePattern& p = project.tracks[1].notes[0][0];
    p = NotePattern();
    for (int kind = 0; kind < kGlideKinds; ++kind) {
        p.notes[kind] = NoteEvent{kind * 2, 60 + kind, 1.f, 0.f, 0.8f};
        p.notes[kind].glide = static_cast<std::uint8_t>(kind);
    }
    p.notes[4].ratchet = 3;
    const std::string text = songText(project);
    CHECK(text.find("notefx 1 60") == std::string::npos); // no glide, no ratchet: no line
    CHECK(text.find("\nnotefx 3 61 1 1\n") != std::string::npos && text.find("\nnotefx 5 62 1 2\n") != std::string::npos && text.find("\nnotefx 7 63 1 3\n") != std::string::npos);
    CHECK(text.find("\nnotefx 9 64 3 4\n") != std::string::npos && text.find("\nnotefx 11 65 1 5\n") != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && songText(*back) == text);
    for (int kind = 0; kind < kGlideKinds; ++kind) {
        const NotePattern& kept = back->tracks[1].notes[0][0];
        const int at = kept.at(60 + kind, kind * 2);
        CHECK(at >= 0 && kept.notes[at].glide == kind);
    }
    // An old song's line, and lines no version has written: on is on, and is half a step.
    std::string old = text;
    const auto swap = [&](const char* was, const char* now) {
        const std::size_t at = old.find(was);
        CHECK(at != std::string::npos);
        if (at != std::string::npos) old.replace(at, std::strlen(was), now);
    };
    swap("notefx 5 62 1 2", "notefx 5 62 1 9");   // a length from a later version
    swap("notefx 7 63 1 3", "notefx 7 63 1 0.5"); // was read as on
    swap("notefx 9 64 3 4", "notefx 9 64 3 -2");  // and this
    swap("notefx 11 65 1 5", "notefx 11 65 1 1e9");
    std::unique_ptr<Project> odd(new Project);
    CHECK(parseSong(old, *odd));
    const NotePattern& o = odd->tracks[1].notes[0][0];
    CHECK(o.notes[o.at(61, 2)].glide == kGlideHalf && o.notes[o.at(62, 4)].glide == kGlideHalf && o.notes[o.at(63, 6)].glide == kGlideHalf);
    CHECK(o.notes[o.at(64, 8)].glide == kGlideHalf && o.notes[o.at(64, 8)].ratchet == 3 && o.notes[o.at(65, 10)].glide == kGlideHalf);
}

// ---- the picture itself ----------------------------------------------------------------

// The screen drawn into memory, as the program draws it.
struct Picture {
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    Fonts fonts;
    bool lettering = false;
    Picture() {
        const std::vector<unsigned char> ttf = loadFontFile();
        lettering = renderer && fonts.small.load(renderer, ttf, 14.5f) && fonts.medium.load(renderer, ttf, 17.f) && fonts.large.load(renderer, ttf, 20.f);
    }
    ~Picture() {
        fonts = Fonts();
        if (renderer) SDL_DestroyRenderer(renderer);
        if (surface) SDL_FreeSurface(surface);
    }
    std::vector<Uint32> of(const App& app) {
        app.draw(renderer, fonts);
        SDL_RenderPresent(renderer);
        const Uint32* px = static_cast<const Uint32*>(surface->pixels);
        return std::vector<Uint32>(px, px + 640 * 480);
    }
};
// The pixels of a glide's lines: purple where the same notes with no glide have none. (A glide
// also leaves a purple corner on its own note: `corner` says where not to look, a note's block.)
struct Seen {
    int count = 0, top = 9999, bottom = -1, left = 9999, right = -1;
    int topX = 0, bottomX = 0; // where its highest and its lowest pixels are
};
const Uint32 kPurplePixel = 0xff000000u | (154u << 16) | (118u << 8) | 176u;
Seen lineOf(const std::vector<Uint32>& with, const std::vector<Uint32>& without, int fromY = 0, int toY = 480) {
    Seen seen;
    for (int y = fromY; y < toY; ++y)
        for (int x = 0; x < 640; ++x) {
            const std::size_t i = static_cast<std::size_t>(y * 640 + x);
            if (with[i] != kPurplePixel || without[i] == kPurplePixel) continue;
            ++seen.count;
            if (y < seen.top) seen.top = y, seen.topX = x;
            if (y >= seen.bottom) seen.bottom = y, seen.bottomX = x;
            seen.left = x < seen.left ? x : seen.left;
            seen.right = x > seen.right ? x : seen.right;
        }
    return seen;
}
const int kTop = 69, kRow = 21, kKey = 25; // the roll at two octaves: its first row, a step, a key (note_screen.cpp)

// What the roll shows of a glide, read off the screen: the line of a FULL glide from a note
// bars before is in every bar it crosses, the one its source is in too; a glide into the first
// step is drawn at the foot of the bar; a line ends at its note's own start, nudge and all,
// and leans from the key it slides from; and the strip says the length.
void testTheLinesOnTheScreen() {
    Rig rig;
    Picture picture;
    CHECK(picture.lettering);
    if (!picture.lettering) return;
    Session& session = rig.app.session();
    CHECK(session.addTrack(TrackKind::Qmod));
    Project& project = rig.project();
    project.track = 1;
    session.view = View::Pattern;
    CHECK(session.octavesShown() == 2);
    const auto shot = [&](int bar) {
        project.bar = bar;
        rig.app.notes().moveCursorTo(72, 0);
        rig.frames(1);
        rig.render(1000);
        return picture.of(rig.app);
    };
    const auto clear = [&] {
        for (int b = 0; b < kMaxBars; ++b) project.tracks[1].notes[0][b] = NotePattern();
    };
    // A slow line: a note in bar 1, the next in bar 3, FULL. 38 steps of line, seven keys to the right.
    session.setCellBars(1, 0, 4);
    CHECK(project.tracks[1].bars[0] == 4 && project.sceneSteps(0) == 64);
    clear();
    project.tracks[1].notes[0][0].notes[0] = NoteEvent{3, 60, 1.f, 0.f, 0.8f};
    project.tracks[1].notes[0][2].notes[0] = NoteEvent{9, 67, 1.f, 0.f, 0.8f};
    std::vector<Uint32> plain[4], drawn[4];
    for (int bar = 0; bar < 4; ++bar) plain[bar] = shot(bar);
    project.tracks[1].notes[0][2].notes[0].glide = kGlideFull;
    for (int bar = 0; bar < 4; ++bar) drawn[bar] = shot(bar);
    const int foot = kTop + 16 * kRow;
    const Seen first = lineOf(drawn[0], plain[0]), second = lineOf(drawn[1], plain[1]), third = lineOf(drawn[2], plain[2]), fourth = lineOf(drawn[3], plain[3]);
    // Bar 1: from its source's start (step 4) to the foot. Bar 2: top to foot. Bar 3: from the top to its note. Bar 4: nothing.
    CHECK(first.count > 100 && std::abs(first.top - (kTop + 3 * kRow)) <= 2 && first.bottom >= foot - 5 && first.bottom < foot);
    CHECK(second.count > 100 && second.top <= kTop + 2 && second.top >= kTop && second.bottom >= foot - 5 && second.bottom < foot);
    CHECK(third.count > 50 && third.top <= kTop + 2 && third.bottom <= kTop + 10 * kRow);
    CHECK(fourth.count == 0);
    // It leans: 7 keys over 38 steps, so 16 steps of it cross about three keys, to the right going down.
    CHECK(second.right - second.left > 2 * kKey && second.right - second.left < 4 * kKey && second.bottomX > second.topX + 2 * kKey);
    CHECK(first.left >= second.left - 3 * kKey && first.right <= second.left + 4 && third.left >= second.right - 4);
    // Its dark edge: beside the purple, pixels that were lighter are the background's colour now.
    int edge = 0, other = 0;
    for (std::size_t i = 0; i < plain[1].size(); ++i) {
        if (drawn[1][i] == plain[1][i] || drawn[1][i] == kPurplePixel) continue;
        ++(drawn[1][i] == (0xff000000u | (20u << 16) | (17u << 8) | 14u) ? edge : other);
    }
    CHECK(edge > second.count / 2 && other == 0);
    // ...and the purple is the middle of it: in each row one run, two or three across, not two with dark between.
    int rowsOfIt = 0, split = 0;
    for (int y = kTop; y < foot; ++y) {
        int count = 0, from = 640, to = -1;
        for (int x = 0; x < 640; ++x) {
            const std::size_t i = static_cast<std::size_t>(y * 640 + x);
            if (drawn[1][i] != kPurplePixel || plain[1][i] == kPurplePixel) continue;
            ++count, from = x < from ? x : from, to = x;
        }
        rowsOfIt += count > 0;
        split += count > 0 && (to - from + 1 != count || count > 3);
    }
    CHECK(rowsOfIt > 300 && split == 0);
    if (failures) std::printf("  the slow line: bar 1 %d px rows %d..%d, bar 2 %d px rows %d..%d x %d..%d, bar 3 %d px rows %d..%d, bar 4 %d px\n", first.count, first.top, first.bottom, second.count, second.top, second.bottom, second.left, second.right, third.count, third.top, third.bottom, fourth.count);

    // Into the first step from the loop's last note: at the foot of the bar, and nowhere else.
    session.setCellBars(1, 0, 1);
    CHECK(project.sceneSteps(0) == 16);
    clear();
    NotePattern& p = project.tracks[1].notes[0][0];
    p.notes[0] = NoteEvent{0, 64, 1.f, 0.f, 0.8f};
    p.notes[1] = NoteEvent{14, 70, 1.f, 0.f, 0.8f};
    const std::vector<Uint32> plainRound = shot(0);
    p.notes[0].glide = kGlideFull;
    const std::vector<Uint32> round = shot(0);
    const Seen wrap = lineOf(round, plainRound, kTop + kRow, 480), all = lineOf(round, plainRound);
    CHECK(wrap.count > 20 && std::abs(wrap.top - (kTop + 14 * kRow)) <= 2 && wrap.bottom >= foot - 5 && wrap.bottom < foot);
    CHECK(wrap.topX > wrap.bottomX + 2 * kKey); // from key 70, on its way left to 64
    CHECK(all.count - wrap.count < 80);          // (the corner on the note itself)

    // One step long, into a note played 0.4 of a step late: the line is from 0.6 before the step to 0.4 after it.
    clear();
    p.notes[0] = NoteEvent{2, 62, 1.f, 0.f, 0.8f};
    p.notes[1] = NoteEvent{8, 70, 1.f, 0.4f, 0.8f};
    const std::vector<Uint32> plainLate = shot(0);
    p.notes[1].glide = kGlideOne;
    const Seen late = lineOf(shot(0), plainLate);
    CHECK(late.count > 20 && std::abs(late.top - (kTop + std::lround(7.4 * kRow))) <= 2);
    CHECK(late.right - late.left > 7 * kKey - 14 && late.right - late.left < 8 * kKey + 14); // eight keys, centre to centre (and the corner)
    p.notes[1].nudge = 0.f;
    p.notes[1].glide = kGlideOff;
    const std::vector<Uint32> plainOnTime = shot(0);
    p.notes[1].glide = kGlideOne;
    CHECK(std::abs(lineOf(shot(0), plainOnTime).top - (kTop + 7 * kRow)) <= 2);

    // The strip with X held says the length: two lengths read differently there.
    const auto strip = [&](int kind) {
        p.notes[1].glide = static_cast<std::uint8_t>(kind);
        project.bar = 0;
        rig.app.notes().moveCursorTo(70, 8);
        rig.frames(2);
        rig.render(2000);
        const std::vector<Uint32> all = picture.of(rig.app);
        return std::vector<Uint32>(all.begin() + 640 * (foot + 4), all.begin() + 640 * 456);
    };
    const std::vector<Uint32> notHeld = strip(kGlideQuarter);
    rig.key(SDLK_s, true); // X, on the note
    rig.frames(2);
    const std::vector<Uint32> quarter = strip(kGlideQuarter), two = strip(kGlideTwo), full = strip(kGlideFull), again = strip(kGlideQuarter);
    CHECK(quarter != notHeld); // (it is the strip with X held: RATCHET, GLIDE, THROW, NUDGE)
    CHECK(quarter == again && quarter != two && two != full && quarter != full);
    rig.key(SDLK_s, false);
    rig.frames(2);

    // What the lines cost to draw (GLIDE_PRINT=1): 64 notes, each FULL, from one end of the roll to the other.
    // On the desk, 2026-11-12, this test's software renderer (1.6 ms a frame without the glides): the lines
    // add 0.2 to 0.4 ms; drawn a call a pixel they added 0.6 to 0.9 ms. The same pixels either way.
    if (std::getenv("GLIDE_PRINT")) {
        clear();
        for (int i = 0; i < kMaxNotes; ++i) p.notes[i] = NoteEvent{i % 16, i % 2 ? 60 + (i / 2) % 8 : 83 - (i / 2) % 8, 0.5f, i / 16 * 0.2f, 0.8f};
        double ms[2] = {0, 0};
        for (int glides = 0; glides < 2; ++glides) {
            for (int i = 0; i < kMaxNotes; ++i) p.notes[i].glide = static_cast<std::uint8_t>(glides ? kGlideFull : kGlideOff);
            shot(0);
            const Uint64 began = SDL_GetPerformanceCounter();
            for (int i = 0; i < 200; ++i) rig.app.draw(picture.renderer, picture.fonts);
            ms[glides] = static_cast<double>(SDL_GetPerformanceCounter() - began) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()) / 200.0;
        }
        std::printf("  a frame of the roll, 64 notes: %.3f ms; each with a FULL glide: %.3f ms (%d purple pixels)\n", ms[0], ms[1], lineOf(picture.of(rig.app), plainOnTime).count);
    }
}

} // namespace

int main(int, char**) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    testTheRollDrawsWhatTheEnginePlays();
    testAGlideFromAChord();
    testATrackSentWhole();
    testTheSongFile();
    testTheLinesOnTheScreen();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("glide in the app: tests passed\n");
    return 0;
}
