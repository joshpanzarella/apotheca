// Tests for a USB keyboard: the bytes it sends, the thread that reads them,
// and what the notes do (play the track, move the cursor, get recorded).
// The "keyboards" here are named pipes in a temporary folder, which the
// reader opens exactly as it opens /dev/snd/midiC1D0 on the handheld.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "midi.hpp"
#include "temp_folder.hpp"

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

// Wait for something another thread does, a second at most.
bool until(const std::function<bool()>& done) {
    for (int i = 0; i < 400; ++i) {
        if (done()) return true;
        usleep(5000);
    }
    return done();
}

// A made-up keyboard: a pipe the reader opens, and this end to send bytes down.
struct Keyboard {
    std::string path;
    int fd = -1;
    Keyboard(const std::string& folder, const char* name) : path(folder + "/" + name) { mkfifo(path.c_str(), 0600); }
    bool plugIn(const MidiInput* input = nullptr, int devices = 1) {
        // Opening for writing waits until the reader has opened its end.
        fd = open(path.c_str(), O_WRONLY);
        return fd >= 0 && (!input || until([&] { return input->devices() == devices; }));
    }
    void send(std::vector<unsigned char> bytes) { CHECK(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size())); }
    void on(int note, int velocity = 100) { send({0x90, static_cast<unsigned char>(note), static_cast<unsigned char>(velocity)}); }
    void off(int note) { send({0x80, static_cast<unsigned char>(note), 0}); }
    void pullOut() {
        unlink(path.c_str());
        if (fd >= 0) close(fd);
        fd = -1;
    }
    ~Keyboard() { pullOut(); }
};

std::vector<MidiEvent> parse(const std::vector<unsigned char>& bytes) {
    MidiParser parser;
    std::vector<MidiEvent> out;
    MidiEvent event;
    for (unsigned char b : bytes)
        if (parser.feed(b, event)) out.push_back(event);
    return out;
}
bool is(const MidiEvent& e, MidiEvent::Kind kind, int note) { return e.kind == kind && e.note == note; }

float peak(Engine& engine, int frames) {
    std::vector<float> out(2 * frames);
    engine.render(out.data(), frames);
    float p = 0.f;
    for (float s : out) p = std::fabs(s) > p ? std::fabs(s) : p;
    return p;
}

// ---- the bytes ----------------------------------------------------------------------------

void testTheBytes() {
    // A key down and up, on any channel.
    auto got = parse({0x90, 60, 127, 0x80, 60, 0});
    CHECK(got.size() == 2 && is(got[0], MidiEvent::NoteOn, 60) && std::fabs(got[0].velocity - 1.f) < 1e-6f && is(got[1], MidiEvent::NoteOff, 60));
    got = parse({0x95, 61, 64, 0x85, 61, 10});
    CHECK(got.size() == 2 && is(got[0], MidiEvent::NoteOn, 61) && std::fabs(got[0].velocity - 64.f / 127.f) < 1e-6f && is(got[1], MidiEvent::NoteOff, 61));
    // Most keyboards send "note on at no loudness" for a key let go.
    got = parse({0x90, 62, 90, 0x90, 62, 0});
    CHECK(got.size() == 2 && is(got[1], MidiEvent::NoteOff, 62));
    // "Running status": the first byte is left out when it would be the same again.
    got = parse({0x90, 60, 100, 64, 100, 67, 100, 60, 0});
    CHECK(got.size() == 4 && is(got[1], MidiEvent::NoteOn, 64) && is(got[2], MidiEvent::NoteOn, 67) && is(got[3], MidiEvent::NoteOff, 60));
    // The clock ticks in the middle of anything, and nothing is lost.
    got = parse({0x90, 0xF8, 60, 0xFE, 100, 0xF8, 64, 0xF8, 100});
    CHECK(got.size() == 2 && is(got[0], MidiEvent::NoteOn, 60) && is(got[1], MidiEvent::NoteOn, 64));
    // What is not a note is read past, whatever its length: a knob (3 bytes),
    // a sound change (2), pressure (2), the pitch wheel (3).
    got = parse({0xB0, 1, 60, 0xC0, 60, 0xD0, 60, 0xE0, 60, 60, 0x90, 70, 100});
    CHECK(got.size() == 1 && is(got[0], MidiEvent::NoteOn, 70));
    // Knob messages under running status do not turn into notes either.
    got = parse({0xB0, 60, 100, 61, 100, 0x90, 71, 100});
    CHECK(got.size() == 1 && is(got[0], MidiEvent::NoteOn, 71));
    got = parse({0xC0, 60, 61, 62, 63, 0x90, 72, 100});
    CHECK(got.size() == 1 && is(got[0], MidiEvent::NoteOn, 72));
    // A dump of a synth's memory ("system exclusive") is skipped whole, and
    // the bytes after it do not carry on the note before it.
    got = parse({0x90, 60, 100, 0xF0, 60, 100, 61, 100, 0xF7, 62, 100, 0x90, 63, 100});
    CHECK(got.size() == 2 && is(got[0], MidiEvent::NoteOn, 60) && is(got[1], MidiEvent::NoteOn, 63));
    // Bytes before any first byte mean nothing.
    got = parse({60, 100, 60, 100, 0x90, 65, 100});
    CHECK(got.size() == 1 && is(got[0], MidiEvent::NoteOn, 65));
    // Half a message is not a message.
    CHECK(parse({0x90, 60}).empty());

    // Which drum a key plays: C2 is lane 1, eight keys and round again.
    CHECK(midiDrumLane(36) == 0 && midiDrumLane(43) == 7 && midiDrumLane(44) == 0 && midiDrumLane(35) == 7 && midiDrumLane(0) == 4 && midiDrumLane(127) == 3);
}

// ---- the engine's second way in --------------------------------------------------------------

void testOnlyNotesComeInLive() {
    Engine engine;
    Command c;
    c.type = Command::Trigger;
    c.a = 0;
    c.value = 1.f;
    CHECK(engine.sendLive(c));
    CHECK(peak(engine, 2000) > 0.1f);
    for (Command::Type type : {Command::Play, Command::SetTempo, Command::SetStep, Command::Panic, Command::InstallSynth, Command::SetVolume}) {
        c.type = type;
        CHECK(!engine.sendLive(c));
    }
}

// ---- the thread that reads keyboards ------------------------------------------------------------

void testAKeyboardPlaysTheTrack() {
    const std::string folder = tempFolder("midi");
    // Other things live in /dev/snd too; they are left alone.
    // (This one even holds bytes that would be a note, if it were read.)
    {
        const int other = open((folder + "/pcmC0D0p").c_str(), O_CREAT | O_WRONLY, 0600);
        const unsigned char note[3] = {0x90, 99, 100};
        CHECK(write(other, note, 3) == 3);
        close(other);
    }
    Engine engine;
    engine.addSynthTrack(1);
    peak(engine, 100);
    MidiInput input;
    Keyboard keys(folder, "midiC1D0");
    input.start(&engine, folder);
    CHECK(input.running());
    CHECK(keys.plugIn(&input));
    CHECK(input.devices() == 1);
    MidiEvent event;

    // Nowhere to go yet: the screens still hear of it, the engine makes no sound.
    keys.on(36);
    CHECK(until([&] { return input.poll(event); }) && is(event, MidiEvent::NoteOn, 36));
    CHECK(peak(engine, 4000) == 0.f);
    keys.off(36);
    CHECK(until([&] { return input.poll(event); }) && is(event, MidiEvent::NoteOff, 36));

    // A drum track: C2 is the kick, and a soft key is a quieter hit.
    input.setTarget(0, MidiInput::Drums);
    keys.on(36, 127);
    CHECK(until([&] { return input.poll(event); }));
    const float loud = peak(engine, 4000);
    CHECK(loud > 0.2f);
    peak(engine, 96000);
    keys.on(36, 30);
    CHECK(until([&] { return input.poll(event); }));
    const float soft = peak(engine, 4000);
    CHECK(soft > 0.f && soft < 0.6f * loud);
    peak(engine, 96000);
    keys.off(36); // nothing to let go on a drum
    keys.off(36);
    CHECK(until([&] { return input.poll(event); }) && until([&] { return input.poll(event); }));
    CHECK(peak(engine, 4000) == 0.f);

    // A synth track: the key sounds while it is held, and stops when it is
    // let go, even if another track has been chosen in between.
    input.setTarget(1, MidiInput::Synth);
    keys.on(60);
    CHECK(until([&] { return input.poll(event); }));
    CHECK(peak(engine, 8000) > 0.02f);
    CHECK(peak(engine, 8000) > 0.02f); // held
    input.setTarget(0, MidiInput::Drums);
    keys.off(60);
    CHECK(until([&] { return input.poll(event); }) && is(event, MidiEvent::NoteOff, 60));
    peak(engine, 96000);
    CHECK(peak(engine, 8000) < 0.002f);

    // Pulled out with a key down: the key is let go.
    input.setTarget(1, MidiInput::Synth);
    keys.on(64);
    CHECK(until([&] { return input.poll(event); }));
    CHECK(peak(engine, 8000) > 0.02f);
    keys.pullOut();
    CHECK(until([&] { return input.devices() == 0; }));
    CHECK(until([&] { return input.poll(event); }) && is(event, MidiEvent::NoteOff, 64));
    peak(engine, 96000);
    CHECK(peak(engine, 8000) < 0.002f);

    // Another plugged in later is found without being asked for; two at once both play.
    Keyboard second(folder, "midiC2D0");
    CHECK(second.plugIn(&input, 1));
    Keyboard third(folder, "midiC3D1");
    CHECK(third.plugIn(&input, 2));
    second.on(60);
    third.on(72);
    CHECK(until([&] { return input.poll(event); }) && until([&] { return input.poll(event); }));
    CHECK(peak(engine, 8000) > 0.02f);
    // One pulled out while the other stays: the other still plays.
    second.pullOut();
    CHECK(until([&] { return input.devices() == 1; }));
    third.off(72);
    third.on(48);
    CHECK(until([&] { return input.poll(event); }) && until([&] { return input.poll(event); }));
    CHECK(is(event, MidiEvent::NoteOn, 48));

    input.stop();
    CHECK(!input.running() && input.devices() == 0);
    input.stop(); // twice is fine
}

// ---- through the app: the cursor and recording ----------------------------------------------------

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    std::string folder = tempFolder("midi");
    Keyboard keys{folder, "midiC1D0"};

    Rig() {
        app.session().startMidi(folder);
        keys.plugIn();
        until([&] { return app.session().midiKeyboards() == 1; });
    }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code) { key(code, true); key(code, false); }
    void frames(int count) { for (int i = 0; i < count; ++i) { clock += 16; app.update(input, clock); } }
    void render(int frames) { std::vector<float> out(2 * frames); engine->render(out.data(), frames); }
    // Frames until the screens have heard what the keyboard sent.
    bool heard(const std::function<bool()>& done) { return until([&] { frames(1); return done(); }); }
    Project& project() { return app.project(); }
    void addPhazed() {
        app.session().view = View::Song;
        app.song().moveCursorTo(0, project().trackCount());
        tap(SDLK_x);
        tap(SDLK_x);
        project().track = 1;
        app.session().view = View::Pattern;
        frames(1);
    }
};

int notesIn(const NotePattern& p) { int n = 0; for (const NoteEvent& e : p.notes) n += e.velocity > 0.f ? 1 : 0; return n; }

void testTheCursorFollowsTheKey() {
    Rig rig;
    rig.addPhazed();
    CHECK(rig.app.session().midiKeyboards() == 1);
    NoteScreen& notes = rig.app.notes();
    notes.moveCursorTo(60, 5);
    rig.keys.on(79);
    CHECK(rig.heard([&] { return notes.cursorPitch() == 79; }));
    CHECK(notes.cursorStep() == 5); // the key only; the step stays
    CHECK(notes.lowestKeyShown() <= 79 && notes.lowestKeyShown() + notes.visibleKeys() > 79); // and it is in view
    notes.moveCursorTo(60, 5);
    rig.keys.off(79);
    usleep(150000);
    rig.frames(3);
    CHECK(notes.cursorPitch() == 60); // letting go moves nothing
    // Nothing is written by playing with the song stopped, recording on or not.
    rig.app.session().setMidiRecording(true);
    rig.keys.on(50);
    CHECK(rig.heard([&] { return notes.cursorPitch() == 50; }));
    rig.keys.off(50);
    rig.frames(3);
    CHECK(rig.project().tracks[1].notes[0][0].empty());
    // While A has hold of a note the cursor stays with it.
    notes.moveCursorTo(60, 0);
    rig.key(SDLK_x, true);
    rig.keys.on(90);
    rig.keys.off(90);
    usleep(150000);
    rig.frames(3);
    CHECK(notes.cursorPitch() == 60);
    rig.key(SDLK_x, false);
}

void testRecordingASynth() {
    Rig rig;
    rig.addPhazed();
    Session& session = rig.app.session();
    const NotePattern& bar = rig.project().tracks[1].notes[0][0];
    rig.tap(SDLK_RETURN); // play this scene, round and round
    rig.render(2 * kStep + kStep / 2);
    rig.frames(1);
    CHECK(rig.project().playStep == 2);

    // Recording off: the key sounds (the keyboard's thread sees to that) and nothing is written.
    rig.keys.on(64);
    rig.keys.off(64);
    usleep(150000);
    rig.frames(3);
    CHECK(bar.empty());

    // On: the note lands on the step it was played in, one step long to begin with...
    session.setMidiRecording(true);
    rig.keys.on(67, 64);
    CHECK(rig.heard([&] { return !bar.empty(); }));
    int slot = bar.at(67, 2);
    CHECK(slot >= 0 && notesIn(bar) == 1);
    CHECK(bar.notes[slot].step == 2 && bar.notes[slot].length == 1.f && std::fabs(bar.notes[slot].velocity - 64.f / 127.f) < 1e-4f);
    // ...and as long as the key was held, once it is let go.
    rig.render(3 * kStep);
    rig.frames(1);
    CHECK(rig.project().playStep == 5 && bar.notes[slot].length == 1.f);
    rig.keys.off(67);
    CHECK(rig.heard([&] { return bar.notes[slot].length != 1.f; }));
    CHECK(bar.notes[slot].length == 3.f);
    // The engine has the length too: a bar played from the top sounds through
    // the note's third step, where a one-step note would have died away.
    {
        rig.tap(SDLK_RETURN);
        rig.render(96000);
        rig.tap(SDLK_RETURN);
        std::vector<float> out(2 * 5 * kStep);
        rig.engine->render(out.data(), 5 * kStep);
        float third = 0.f;
        for (int i = 2 * (4 * kStep + kStep / 2); i < 2 * 5 * kStep; ++i) third = std::fmax(third, std::fabs(out[i]));
        CHECK(third > 0.02f);
        rig.render(kStep / 2);
        rig.frames(1);
        CHECK(rig.project().playStep == 5);
    }
    // A chord: three keys at once are three notes.
    rig.keys.on(60);
    rig.keys.on(64);
    rig.keys.on(72);
    CHECK(rig.heard([&] { return notesIn(bar) == 4; }));
    CHECK(bar.at(60, 5) >= 0 && bar.at(64, 5) >= 0 && bar.at(72, 5) >= 0);
    rig.keys.off(60);
    rig.keys.off(64);
    rig.keys.off(72);
    rig.frames(1);
    // The same key on a step that already has it: that note again, not a second one.
    rig.keys.on(60, 127);
    CHECK(rig.heard([&] { const int s = bar.at(60, 5); return s >= 0 && bar.notes[s].velocity > 0.99f; }));
    CHECK(notesIn(bar) == 4);
    // Held past the end of the bar: it runs to the end of the bar and no further.
    rig.render(14 * kStep); // round to step 3 of the next pass
    rig.frames(1);
    CHECK(rig.project().playStep == 3);
    rig.keys.off(60);
    slot = bar.at(60, 5);
    CHECK(rig.heard([&] { return bar.notes[slot].length != 1.f; }));
    CHECK(bar.notes[slot].length == 11.f); // steps 6 to 16

    // The engine was told: the bar plays what was recorded when it comes round.
    rig.tap(SDLK_RETURN);
    rig.render(96000);
    Engine fresh;
    CHECK(rig.engine->trackPeak(1) >= 0.f); // (it has a peak to read)
    rig.tap(SDLK_RETURN);
    std::vector<float> out(2 * 16 * kStep);
    rig.engine->render(out.data(), 2 * kStep);     // steps 1 and 2: nothing recorded there
    float early = 0.f;
    for (int i = 0; i < 2 * 2 * kStep; ++i) early = std::fmax(early, std::fabs(out[i]));
    rig.engine->render(out.data(), 2 * kStep);     // steps 3 and 4: the first note
    float later = 0.f;
    for (int i = 0; i < 2 * 2 * kStep; ++i) later = std::fmax(later, std::fabs(out[i]));
    CHECK(early < 0.002f && later > 0.02f);

    // Stopped: nothing more is written, and a key still down when it stopped keeps the length it has.
    rig.tap(SDLK_RETURN);
    rig.frames(1);
    const int before = notesIn(bar);
    rig.keys.on(40);
    rig.keys.off(40);
    usleep(150000);
    rig.frames(3);
    CHECK(notesIn(bar) == before);
}

// NEXT SCENE (SELECT + Y) pressed while a key is held and being recorded: the playing goes to
// the new scene, and so does the note. It is as long as it was held there, where it is heard.
void testAKeyHeldThroughNextScene() {
    Rig rig;
    rig.addPhazed();
    rig.app.session().setMidiRecording(true);
    const TrackData& track = rig.project().tracks[1];
    rig.tap(SDLK_RETURN);
    rig.render(2 * kStep + kStep / 2);
    rig.frames(1);
    rig.keys.on(67, 64);
    CHECK(rig.heard([&] { return !track.notes[0][0].empty(); }));
    const int slot = track.notes[0][0].at(67, 2);
    CHECK(slot >= 0);
    rig.key(SDLK_BACKSPACE, true);
    rig.tap(SDLK_a);
    rig.key(SDLK_BACKSPACE, false);
    CHECK(rig.project().scene == 1 && track.notes[1][0].at(67, 2) == slot);
    rig.render(3 * kStep);
    rig.frames(1);
    CHECK(rig.project().playScene == 1 && rig.project().playStep == 5);
    rig.keys.off(67);
    CHECK(rig.heard([&] { return track.notes[1][0].notes[slot].length != 1.f; }));
    CHECK(track.notes[1][0].notes[slot].length == 3.f && track.notes[0][0].notes[slot].length == 1.f);
}

void testRecordingDrums() {
    Rig rig;
    Session& session = rig.app.session();
    session.view = View::Pattern;
    session.setMidiRecording(true);
    rig.frames(1);
    rig.tap(SDLK_RETURN);
    rig.render(6 * kStep + kStep / 2);
    rig.frames(1);
    CHECK(rig.project().playStep == 6);
    PatternScreen& drums = rig.app.drums();
    rig.keys.on(38, 90); // the third drum
    CHECK(rig.heard([&] { return drums.level(2, 6) > 0.f; }));
    CHECK(std::fabs(drums.level(2, 6) - 90.f / 127.f) < 1e-4f);
    for (int lane = 0; lane < kDrumLanes; ++lane)
        for (int step = 0; step < kSteps; ++step) CHECK((lane == 2 && step == 6) || drums.level(lane, step) == 0.f);
    // A very soft key is still a hit.
    rig.keys.on(36, 1);
    CHECK(rig.heard([&] { return drums.level(0, 6) > 0.f; }));
    CHECK(std::fabs(drums.level(0, 6) - 0.05f) < 1e-4f);
    // Recording off again: played, not written.
    session.setMidiRecording(false);
    rig.keys.on(41);
    usleep(150000);
    rig.frames(3);
    CHECK(drums.level(5, 6) == 0.f);
    // The engine was told: stopped and started, the recorded hit plays on its step.
    rig.tap(SDLK_RETURN);
    rig.render(96000);
    drums.setStep(0, 6, 0.f);
    rig.tap(SDLK_RETURN);
    std::vector<float> out(2 * 6 * kStep);
    rig.engine->render(out.data(), 6 * kStep);
    float early = 0.f;
    for (float s : out) early = std::fmax(early, std::fabs(s));
    rig.engine->render(out.data(), kStep);
    float hit = 0.f;
    for (int i = 0; i < 2 * kStep; ++i) hit = std::fmax(hit, std::fabs(out[i]));
    CHECK(early == 0.f && hit > 0.1f);
}

// Drums played into a busy cell send the one hit, not the cell. Sent whole for each hit, eight
// pads at once on an eight-bar cell full of hits were some sixteen thousand commands: the queue
// filled, and the song was stopped and sent again (and undo was lost with it).
void testRecordingDrumsIntoABusyCell() {
    Rig rig;
    Session& session = rig.app.session();
    session.view = View::Pattern;
    session.setMidiRecording(true);
    rig.frames(2);
    session.setCellBars(0, 0, 8);
    for (int bar = 0; bar < 8; ++bar)
        for (int lane = 0; lane < kDrumLanes; ++lane)
            for (int step = 0; step < kSteps; step += 2) rig.project().tracks[0].drums[0][bar].level[lane][step] = 0.8f;
    session.sendDrumPattern(0, 0);
    rig.render(512);
    rig.frames(40);
    rig.render(512);
    rig.frames(40);
    const unsigned dropped = rig.engine->dropped();
    rig.tap(SDLK_RETURN);
    rig.render(kStep + kStep / 2); // into step 1, which is empty on every lane
    rig.frames(1);
    CHECK(rig.project().playing && rig.project().playStep == 1);
    std::vector<unsigned char> pads;
    for (int note = 36; note < 36 + kDrumLanes; ++note) { pads.push_back(0x90); pads.push_back(static_cast<unsigned char>(note)); pads.push_back(100); }
    CHECK(write(rig.keys.fd, pads.data(), pads.size()) == static_cast<ssize_t>(pads.size()));
    usleep(100000); // all eight read before the next frame: one burst
    rig.frames(1);
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(rig.project().tracks[0].drums[0][0].level[lane][1] > 0.f);
    CHECK(rig.engine->dropped() == dropped); // nothing lost
    rig.render(512);
    rig.frames(1);
    rig.render(512);
    rig.frames(1);
    CHECK(session.resyncs() == 0);
    CHECK(rig.project().playing); // the take goes on
    CHECK(session.undoSteps() >= 1);
}

} // namespace

// ---- reading a MIDI file back -----------------------------------------------------------
// Just enough Standard MIDI to check what the bounce wrote: headers, tempo, track
// names, and notes (running status allowed, as any file may use it).
struct MidNote {
    long start = 0, length = 0;
    int pitch = 0, velocity = 0, channel = 0, track = 0;
};
struct MidFile {
    int format = -1, tracks = 0, ppq = 0, tempo = 0;
    std::vector<std::string> names;
    std::vector<MidNote> notes;
    std::vector<std::pair<long, int>> tempi; // (tick, microseconds per quarter) in file order
    std::vector<std::tuple<long, int, int>> sigs; // (tick, numerator, denominator) in file order
    bool ok = false;
};
MidFile readMidiFile(const std::string& bytes) {
    MidFile out;
    std::size_t at = 0;
    const auto take = [&](std::size_t n, std::string& s) {
        if (at + n > bytes.size()) return false;
        s.assign(bytes, at, n);
        at += n;
        return true;
    };
    const auto u16 = [&] {
        if (at + 2 > bytes.size()) return -1;
        const int v = (static_cast<unsigned char>(bytes[at]) << 8) | static_cast<unsigned char>(bytes[at + 1]);
        at += 2;
        return v;
    };
    const auto u32 = [&] {
        if (at + 4 > bytes.size()) return -1;
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) v = (v << 8) | static_cast<unsigned char>(bytes[at++]);
        return static_cast<int>(v);
    };
    const auto varlen = [&]() -> long {
        long v = 0;
        for (int i = 0; i < 4; ++i) {
            if (at >= bytes.size()) return -1;
            const unsigned char b = static_cast<unsigned char>(bytes[at++]);
            v = (v << 7) | (b & 0x7f);
            if (!(b & 0x80)) return v;
        }
        return -1;
    };
    std::string head;
    if (!take(4, head) || head != "MThd" || u32() != 6) return out;
    out.format = u16();
    out.tracks = u16();
    out.ppq = u16();
    if (out.format < 0 || out.tracks < 0 || out.ppq < 0) return out;
    for (int t = 0; t < out.tracks; ++t) {
        std::string tag;
        if (!take(4, tag) || tag != "MTrk") return out;
        const int length = u32();
        if (length < 0 || at + static_cast<std::size_t>(length) > bytes.size()) return out;
        const std::size_t end = at + static_cast<std::size_t>(length);
        long tick = 0;
        int running = 0;
        long pending[16][128];
        for (int c = 0; c < 16; ++c)
            for (int p = 0; p < 128; ++p) pending[c][p] = -1;
        int pendingVel[16][128] = {};
        while (at < end) {
            const long delta = varlen();
            if (delta < 0) return out;
            tick += delta;
            if (at >= end) return out;
            int status = static_cast<unsigned char>(bytes[at]);
            if (status < 0x80) {
                if (!running) return out;
                status = running;
            } else {
                ++at;
            }
            if (status == 0xff) {
                if (at + 1 > end) return out;
                const int type = static_cast<unsigned char>(bytes[at++]);
                const long len = varlen();
                if (len < 0 || at + static_cast<std::size_t>(len) > end) return out;
                if (type == 0x51 && len == 3) {
                    out.tempo = (static_cast<unsigned char>(bytes[at]) << 16) | (static_cast<unsigned char>(bytes[at + 1]) << 8) | static_cast<unsigned char>(bytes[at + 2]);
                    out.tempi.push_back({tick, out.tempo});
                } else if (type == 0x58 && len == 4) {
                    int denPow = static_cast<unsigned char>(bytes[at + 1]);
                    int den = 1;
                    for (int i = 0; i < denPow; ++i) den *= 2;
                    out.sigs.push_back({tick, static_cast<unsigned char>(bytes[at]), den});
                } else if (type == 0x03) {
                    if (static_cast<int>(out.names.size()) <= t) out.names.resize(t + 1);
                    out.names[t].assign(bytes, at, static_cast<std::size_t>(len));
                }
                at += static_cast<std::size_t>(len);
            } else if ((status & 0xf0) == 0x90 || (status & 0xf0) == 0x80) {
                running = status;
                if (at + 2 > end) return out;
                const int pitch = static_cast<unsigned char>(bytes[at++]);
                const int vel = static_cast<unsigned char>(bytes[at++]);
                const int channel = status & 0x0f;
                if (pitch < 0 || pitch > 127) return out;
                if ((status & 0xf0) == 0x90 && vel > 0) {
                    pending[channel][pitch] = tick;
                    pendingVel[channel][pitch] = vel;
                } else if (pending[channel][pitch] >= 0) {
                    MidNote n;
                    n.start = pending[channel][pitch];
                    n.length = tick - n.start;
                    n.pitch = pitch;
                    n.velocity = pendingVel[channel][pitch];
                    n.channel = channel;
                    n.track = t;
                    out.notes.push_back(n);
                    pending[channel][pitch] = -1;
                }
            } else if ((status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0) {
                running = status;
                if (at + 1 > end) return out;
                ++at;
            } else if (status >= 0x80) {
                running = status;
                if (at + 2 > end) return out;
                at += 2;
            } else {
                return out;
            }
        }
        at = end;
    }
    out.ok = true;
    return out;
}

void testMidiFileNotes() {
    Project song;
    song.tempo = 100.f;
    song.tracks[0].kind = TrackKind::Phazed;
    song.tracks[0].bars[0] = 1;
    NoteEvent& n = song.tracks[0].notes[0][0].notes[0];
    n.step = 4;
    n.pitch = 60;
    n.length = 2.f;
    n.velocity = 0.8f;
    song.repeats[0] = 2;
    const MidFile mid = readMidiFile(midiFileBytes(song, "song"));
    CHECK(mid.ok && mid.format == 1 && mid.ppq == 480 && mid.tempo == 600000);
    CHECK(mid.tracks == 2 && mid.names.size() == 2 && mid.names[1] == "PHAZED");
    CHECK(mid.notes.size() == 2); // scene 0 twice
    if (mid.notes.size() == 2) {
        CHECK(mid.notes[0].pitch == 60 && mid.notes[0].velocity == 102 && mid.notes[0].channel == 0);
        CHECK(mid.notes[0].start == 4 * 120 && mid.notes[0].length == 240);
        CHECK(mid.notes[1].start == (16 + 4) * 120 && mid.notes[1].length == 240);
    }
}

void testMidiFileDrumsLoopMute() {
    Project song;
    song.tracks[0].kind = TrackKind::Drums;
    song.tracks[0].bars[0] = 1;
    song.tracks[1].kind = TrackKind::Phazed; // two bars, empty: scene 0 is two bars, the drums go round
    song.tracks[1].bars[0] = 2;
    song.tracks[0].drums[0][0].level[0][0] = 1.f; // the kick, on the first step
    song.tracks[0].drums[0][0].level[5][4] = 0.5f;
    song.tracks[0].drums[0][0].nudge[5][4] = 0.5f;
    song.tracks[0].drums[0][0].fx[5][4].pitch = 2; // the tom, up two: 45 to 47
    song.tracks[0].drums[0][0].level[1][8] = 1.f;
    song.tracks[0].drums[0][0].fx[1][8].ratchet = 2; // the snare, twice
    song.tracks[1].notes[0][0].notes[0].velocity = 0.8f;
    song.tracks[1].notes[0][0].notes[0].pitch = 64;
    song.tracks[1].muted = true; // left out, as from a song bounce
    const MidFile mid = readMidiFile(midiFileBytes(song, "song"));
    CHECK(mid.ok && mid.tracks == 2); // the conductor and the drums only
    int kicks = 0, toms = 0, snares = 0;
    for (const MidNote& m : mid.notes) {
        CHECK(m.channel == 9);
        if (m.pitch == 36) {
            ++kicks;
            CHECK(m.velocity == 127 && (m.start == 0 || m.start == 16 * 120)); // both rounds of the cell
        } else if (m.pitch == 47) {
            ++toms;
            CHECK((m.start == 540 || m.start == 540 + 16 * 120) && m.velocity == 64); // both rounds
        } else if (m.pitch == 38) {
            ++snares;
        } else {
            CHECK(false); // nothing else: the muted track is out
        }
    }
    CHECK(kicks == 2 && toms == 2 && snares == 4); // ...each twice: the cell goes round
}

void testMidiFileChords() {
    Project song;
    song.tracks[0].kind = TrackKind::Phazed;
    ChordEvent& c = song.tracks[0].notes[0][0].chords[0];
    c.step = 0;
    c.length = 4.f;
    c.root = 48;
    c.count = 3;
    c.intervals[0] = 0;
    c.intervals[1] = 4;
    c.intervals[2] = 7;
    c.inversion = 1;
    c.velocity = 1.f;
    const MidFile mid = readMidiFile(midiFileBytes(song, "song"));
    CHECK(mid.ok && mid.notes.size() == 3);
    if (mid.notes.size() == 3) { // C major, first inversion: E G C
        CHECK(mid.notes[0].pitch == 52 && mid.notes[1].pitch == 55 && mid.notes[2].pitch == 60);
        for (const MidNote& m : mid.notes) CHECK(m.start == 0 && m.length == 480 && m.velocity == 127);
    }
}

void testMidiFileSceneTempo() {
    Project song;
    song.tempo = 120.f;
    song.tracks[0].kind = TrackKind::Phazed;
    song.tracks[0].notes[0][0].notes[0].velocity = 0.8f;
    song.tracks[0].notes[0][0].notes[0].pitch = 60;
    song.tracks[0].notes[1][0].notes[0].velocity = 0.8f;
    song.tracks[0].notes[1][0].notes[0].pitch = 64;
    song.sceneTempo[1] = 150.f;
    const MidFile mid = readMidiFile(midiFileBytes(song, "song"));
    CHECK(mid.ok && mid.tempi.size() == 2);
    if (mid.tempi.size() == 2) {
        CHECK(mid.tempi[0].first == 0 && mid.tempi[0].second == 500000);
        CHECK(mid.tempi[1].first == 16 * 120 && mid.tempi[1].second == 400000);
    }
}

void testMidiFileMeter() {
    Project song;
    song.tracks[0].kind = TrackKind::Drums;
    song.tracks[0].drums[0][0].level[0][0] = 1.f;
    song.tracks[0].drums[1][0].level[0][0] = 1.f;
    song.sceneMeter[0] = 12;
    const MidFile mid = readMidiFile(midiFileBytes(song, "song"));
    CHECK(mid.ok && mid.sigs.size() == 2);
    if (mid.sigs.size() == 2) { // 3/4, then back to 4/4
        CHECK(std::get<0>(mid.sigs[0]) == 0 && std::get<1>(mid.sigs[0]) == 3 && std::get<2>(mid.sigs[0]) == 4);
        CHECK(std::get<0>(mid.sigs[1]) == 12 * 120 && std::get<1>(mid.sigs[1]) == 4 && std::get<2>(mid.sigs[1]) == 4);
    }
    CHECK(mid.notes.size() == 2); // the kick in each scene, on its first step
    if (mid.notes.size() == 2) CHECK(mid.notes[1].start == 12 * 120);
    // A two-bar cell in 3/4: its second bar starts 12 steps in, and a hit past the 12th step of a bar
    // (never played) is not written (review 2026-11-23: bars were walked 16 steps long).
    Project two;
    two.tracks[0].kind = TrackKind::Drums;
    two.tracks[0].bars[0] = 2;
    two.sceneMeter[0] = 12;
    two.tracks[0].drums[0][0].level[0][14] = 1.f; // step 15 of bar 1: past the meter
    two.tracks[0].drums[0][1].level[1][0] = 1.f;  // bar 2's first step: the snare
    const MidFile m2 = readMidiFile(midiFileBytes(two, "song"));
    CHECK(m2.ok && m2.notes.size() == 1);
    if (m2.notes.size() == 1) CHECK(m2.notes[0].pitch == 38 && m2.notes[0].start == 12 * 120);
}

void testMidiBounceJob() {
    Rig rig;
    Session& session = rig.app.session();
    const std::string folder = tempFolder("bounces");
    session.useBounceFolder(folder);
    rig.project().tracks[0].kind = TrackKind::Drums;
    rig.project().tracks[0].drums[0][0].level[0][0] = 0.9f;
    CHECK(session.startBounce(Session::Bounce::MidiFile));
    session.finishBounce();
    CHECK(session.bounceFiles() == 1);
    const std::string last = session.bounceLast();
    CHECK(last.size() > 4 && last.compare(last.size() - 4, 4, ".mid") == 0);
    std::string bytes;
    CHECK(readTextFile(folder + "/" + last, bytes));
    const MidFile mid = readMidiFile(bytes);
    CHECK(mid.ok && mid.notes.size() == 1 && mid.notes[0].pitch == 36 && mid.notes[0].channel == 9);
}

int main() {
    testTheBytes();
    testOnlyNotesComeInLive();
    testAKeyboardPlaysTheTrack();
    testTheCursorFollowsTheKey();
    testRecordingASynth();
    testAKeyHeldThroughNextScene();
    testRecordingDrums();
    testRecordingDrumsIntoABusyCell();
    testMidiFileNotes();
    testMidiFileDrumsLoopMute();
    testMidiFileChords();
    testMidiFileSceneTempo();
    testMidiFileMeter();
    testMidiBounceJob();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("midi tests passed\n");
    return 0;
}
