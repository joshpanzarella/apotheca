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
    int lines = 0, fromChords = 0, acrossTheEnd = 0, notesHeard = 0;
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
                acrossTheEnd += line.from >= 0 && n.ratchet < 2 && n.glide == kGlideFull && bar * kSteps + n.step + n.nudge - line.steps < -0.01;
            }
        }
    }
    CHECK(lines > 400 && fromChords > 40 && acrossTheEnd > 3); // the songs did hold what this is for
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

} // namespace

int main(int, char**) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    testTheRollDrawsWhatTheEnginePlays();
    testAGlideFromAChord();
    testTheSongFile();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("glide in the app: tests passed\n");
    return 0;
}
