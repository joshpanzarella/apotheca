// THE MEMORY GUARD (2026-11-28; drums-and-samples.md, "The memory guard"). The handheld locks every page the
// program uses in its 1 GB, and past it the system kills the program with no word. So what the recordings take is
// counted (Session::recordingBytes: this session's copies and the engine's), what the system has left is read
// (memory.hpp: MemAvailable, less a margin), and a load or a copy that would not fit is refused and said: a pick
// changes nothing, a song or kit opened loads what fits and leaves the rest NOT LOADED.
//
// Here the system's figure is pretended: "a limit, less what the recordings take now", so loading and letting go
// moves it as the real one would. memory_test NAME... runs just those.

#include <SDL.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "lane_edit.hpp"
#include "memory.hpp"
#include "samples.hpp"
#include "song_file.hpp"
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

const long long kMargin = memory::kMargin;

std::vector<float> tone(int frames, float step = 0.03f) {
    std::vector<float> s(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) s[static_cast<std::size_t>(i)] = 0.4f * std::sin(step * static_cast<float>(i));
    return s;
}

// What loading a 16-bit 48 kHz file of `frames` frames and `sides` channels onto a lane at rest takes: the file's
// bytes and its sound as floats while it is read, then the lane's copy and the engine's.
long long loadOf(int frames, int sides) {
    const long long file = 44 + 2LL * frames * sides, clip = 4LL * frames * sides;
    return file + clip + clip + clip;
}
const long long kOneSecond = loadOf(48000, 1);   // 672,044
const long long kHeld = 2LL * 4 * 48000;         // a 1 s mono lane held: its copy and the engine's

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    long long limit = -1; // the pretended system: below 0, no figure at all
    explicit Rig(const std::string& folder) {
        app.session().useSampleFolder(folder);
        memory::pretend([this] { return limit < 0 ? -1 : limit - app.session().recordingBytes(); });
        frames(2);
    }
    ~Rig() { memory::pretend(nullptr); }
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            clock += 16;
            app.update(input, clock);
            std::vector<float> out(2 * 256);
            engine->render(out.data(), 256);
        }
    }
    Session& session() { return app.session(); }
    TrackData& kit() { return app.project().tracks[0]; }
    // Room for exactly `bytes` more, as things are now.
    void roomFor(long long bytes) { limit = kMargin + session().recordingBytes() + bytes; }
};

void writeMono(const std::string& folder, const std::string& name, int frames) { CHECK(writeWav(folder + "/" + name + ".wav", tone(frames), 48000)); }

// ---- the file's size, from its header --------------------------------------------------------------------------

void testTheFileSize() {
    const std::string folder = tempFolder("memory");
    writeMono(folder, "one", 48000);
    CHECK(writeWav(folder + "/two.wav", Clip(tone(48000), tone(48000, 0.05f)), 48000));
    WavSize w;
    CHECK(wavSize(folder + "/one.wav", w) && w.channels == 1 && w.frames == 48000u && w.fileFrames == 48000u && w.readBytes == 96044u);
    CHECK(w.clipBytes() == 192000u && w.readingBytes() == 96044u + 192000u);
    CHECK(wavSize(folder + "/two.wav", w) && w.channels == 2 && w.frames == 48000u && w.readBytes == 192044u && w.clipBytes() == 384000u); // stereo: twice
    CHECK(!wavSize(folder + "/none.wav", w) && w.channels == 0);
    {
        // Four channels are read mixed to one: one side's memory.
        std::string four = "RIFF";
        const auto put = [&](unsigned v, int n) { for (int i = 0; i < n; ++i) four += static_cast<char>((v >> (8 * i)) & 255); };
        const unsigned frames = 1000, data = frames * 4 * 2;
        put(36 + data, 4);
        four += "WAVEfmt ";
        put(16, 4), put(1, 2), put(4, 2), put(48000, 4), put(48000 * 8, 4), put(8, 2), put(16, 2);
        four += "data";
        put(data, 4);
        four += std::string(data, '\0');
        Clip read;
        CHECK(writeTextFile(folder + "/four.wav", four) && wavSize(folder + "/four.wav", w) && w.channels == 1 && w.frames == frames);
        CHECK(readWav(folder + "/four.wav", read) && !read.stereo() && read.size() == frames && w.clipBytes() == 4u * frames);
    }
    CHECK(writeTextFile(folder + "/junk.wav", "RIFF....WAVEnothing") && !wavSize(folder + "/junk.wav", w));
    // Another rate: as many frames as reading it makes.
    std::string bytes = wavBytes(tone(44100), 44100);
    CHECK(writeTextFile(folder + "/cd.wav", bytes));
    Clip read;
    CHECK(wavSize(folder + "/cd.wav", w) && readWav(folder + "/cd.wav", read) && w.frames == read.size() && w.fileFrames == 44100u);
    // A file longer than 20 s costs no more to read than its first 20 s (and a frame), and reads to the bit as it
    // did when the whole file was read.
    bytes = wavBytes(tone(48000 * 25), 48000);
    CHECK(writeTextFile(folder + "/long.wav", bytes));
    Clip whole;
    CHECK(wavSize(folder + "/long.wav", w) && w.frames == 48000u * 20u && w.readBytes == 44u + 2u * (48000u * 20u + 1u));
    CHECK(parseWav(bytes, whole) && readWav(folder + "/long.wav", read) && read.size() == 48000u * 20u && read.left() == whole.left());
    std::vector<float> mono, wholeMono;
    CHECK(parseWav(bytes, wholeMono) && readWav(folder + "/long.wav", mono) && mono == wholeMono);
    // One a little longer than 20 s too (the cut and its fade), and a short one, to the bit.
    for (int frames : {48000 * 20 + 1, 48000 * 20, 48000 * 20 - 1, 7}) {
        bytes = wavBytes(tone(frames), 48000);
        CHECK(writeTextFile(folder + "/edge.wav", bytes) && parseWav(bytes, whole) && readWav(folder + "/edge.wav", read) && read.left() == whole.left());
    }
}

// ---- the count ---------------------------------------------------------------------------------------------------

void testTheCountGoesUpAndDown() {
    const std::string folder = tempFolder("memory");
    writeMono(folder, "one", 48000);
    writeMono(folder, "two", 96000);
    CHECK(writeWav(folder + "/wide.wav", Clip(tone(48000), tone(48000, 0.05f)), 48000));
    Rig rig(folder);
    Session& s = rig.session();
    CHECK(s.recordingBytes() == 0); // the built-in drums are the program's
    s.setLaneSample(0, 2, "one");
    rig.frames(3);
    CHECK(s.recordingBytes() == kHeld); // the lane's copy and the engine's
    s.setLaneSample(0, 3, "one");
    rig.frames(3);
    CHECK(s.recordingBytes() == 2 * kHeld);
    s.setLaneSample(0, 3, "two"); // replaced: the old one's copies go, the engine's once it hands it back
    rig.frames(3);
    CHECK(s.recordingBytes() == 3 * kHeld && rig.engine->sampleBytes() == 3 * kHeld / 2);
    s.setLaneSample(0, 2, "wide"); // stereo: twice a mono file's
    rig.frames(3);
    CHECK(s.recordingBytes() == 4 * kHeld);
    s.setLaneSample(0, 2, "");
    rig.frames(3);
    CHECK(s.recordingBytes() == 2 * kHeld);
    // An EDIT that shapes makes a copy, and lets it go when it is undone; the song sent again counts the same.
    LaneEdit edit = rig.kit().laneEdit[3];
    edit.reverse = true;
    s.setLaneEdit(0, 3, edit);
    rig.frames(3);
    CHECK(s.recordingBytes() == 3 * kHeld);
    edit.reverse = false;
    s.setLaneEdit(0, 3, edit);
    rig.frames(3);
    CHECK(s.recordingBytes() == 2 * kHeld);
    s.applyProject();
    rig.frames(3);
    CHECK(s.recordingBytes() == 2 * kHeld);
    CHECK(s.deleteSample("two"));
    rig.frames(3);
    CHECK(s.recordingBytes() == 0 && rig.engine->sampleBytes() == 0);
}

// ---- a pick ------------------------------------------------------------------------------------------------------

void testAPickThatDoesNotFitChangesNothing() {
    const std::string folder = tempFolder("memory");
    writeMono(folder, "one", 48000);
    writeMono(folder, "two", 96000);
    CHECK(writeWav(folder + "/wide.wav", Clip(tone(48000), tone(48000, 0.05f)), 48000));
    Rig rig(folder);
    Session& s = rig.session();
    rig.roomFor(kOneSecond - 1);
    unsigned said = s.saidCount();
    s.setLaneSample(0, 2, "one");
    rig.frames(2);
    CHECK(rig.kit().laneSample[2][0] == 0 && !s.laneSound(0, 2).recording && !s.laneSound(0, 2).noMemory && s.recordingBytes() == 0);
    CHECK(s.saidCount() == said + 1 && std::string(s.saidText()) == Session::noMemoryWords(kOneSecond) && s.refusals() == 1u);
    CHECK(std::string(s.saidText()) == "NO MEMORY: NEEDS 1 MB");
    rig.roomFor(kOneSecond); // to the byte
    s.setLaneSample(0, 2, "one");
    rig.frames(2);
    CHECK(std::string(rig.kit().laneSample[2]) == "one" && s.laneSound(0, 2).recording && s.refusals() == 1u);
    // Stereo needs twice what mono does, its file's bytes too.
    rig.roomFor(loadOf(48000, 2) - 1);
    s.setLaneSample(0, 4, "wide");
    CHECK(rig.kit().laneSample[4][0] == 0 && s.refusals() == 2u);
    rig.roomFor(loadOf(48000, 2));
    s.setLaneSample(0, 4, "wide");
    CHECK(std::string(rig.kit().laneSample[4]) == "wide" && s.laneSound(0, 4).recording->stereo());
    // Another recording on a lane: what the lane alone holds is let go first, so it counts as room (not the
    // engine's copy, which goes a moment later).
    const Clip* was = s.laneSound(0, 2).recording.get(); // (not a share of it: that would hold it too)
    rig.roomFor(loadOf(96000, 1) - 192000 - 1);
    said = s.saidCount();
    s.setLaneSample(0, 2, "two");
    rig.frames(2);
    CHECK(std::string(rig.kit().laneSample[2]) == "one" && s.laneSound(0, 2).recording.get() == was && s.saidCount() == said + 1);
    // ...but not where something else still holds it (here this test; on the handheld a FIT under way): then it
    // is not freed with the lane's sound, and is no room.
    {
        const std::shared_ptr<const Clip> share = s.laneSound(0, 2).recording;
        rig.roomFor(loadOf(96000, 1) - 192000);
        s.setLaneSample(0, 2, "two");
        CHECK(std::string(rig.kit().laneSample[2]) == "one");
    }
    rig.roomFor(loadOf(96000, 1) - 192000);
    s.setLaneSample(0, 2, "two");
    CHECK(std::string(rig.kit().laneSample[2]) == "two" && s.laneSound(0, 2).recording->size() == 96000u);
    // With no figure to go by (not Linux, or unreadable) nothing is refused.
    rig.limit = -1;
    CHECK(memory::room() < 0);
    s.setLaneSample(0, 5, "two");
    CHECK(std::string(rig.kit().laneSample[5]) == "two" && s.refusals() == 4u);
}

void testAnEditAndAFitThatDoNotFit() {
    const std::string folder = tempFolder("memory");
    writeMono(folder, "one", 48000);
    Rig rig(folder);
    Session& s = rig.session();
    s.setLaneSample(0, 2, "one");
    rig.frames(2);
    // PITCH an octave down: a shape twice as long, its level (an edit off rest is guarded: installLane) and the
    // engine's copy of that.
    LaneEdit edit = rig.kit().laneEdit[2];
    edit.pitch = -12;
    const long long need = 3LL * (static_cast<long long>(48000.0 * 2.0 + 1.0) * 4);
    rig.roomFor(need - 1);
    unsigned said = s.saidCount();
    s.setLaneEdit(0, 2, edit);
    CHECK(rig.kit().laneEdit[2].pitch == 0 && s.laneSound(0, 2).shaped == s.laneSound(0, 2).source && s.saidCount() == said + 1);
    CHECK(std::string(s.saidText()) == Session::noMemoryWords(need));
    rig.roomFor(need);
    s.setLaneEdit(0, 2, edit);
    CHECK(rig.kit().laneEdit[2].pitch == -12 && s.laneSound(0, 2).shaped->size() > 90000u);
    edit.pitch = 0;
    s.setLaneEdit(0, 2, edit);
    rig.frames(2);
    // GAIN alone: the level made and the engine's copy.
    edit.gain = 40;
    rig.roomFor(2 * 192000 - 1);
    s.setLaneEdit(0, 2, edit);
    CHECK(rig.kit().laneEdit[2].gain == 0);
    rig.roomFor(2 * 192000);
    s.setLaneEdit(0, 2, edit);
    CHECK(rig.kit().laneEdit[2].gain == 40);
    // FIT, on the helper thread: refused once, said once, and the lane plays the file as it is meanwhile.
    rig.roomFor(1000);
    said = s.saidCount();
    s.setLaneFit(0, 2, 16);
    rig.frames(30);
    CHECK(s.laneSound(0, 2).noFit && !s.laneSound(0, 2).fitted && !s.laneSound(0, 2).waiting && !s.stretching());
    CHECK(s.saidCount() == said + 1 && std::string(s.saidText()).compare(0, 17, "NO MEMORY: NEEDS ") == 0);
    rig.limit = -1;
    rig.frames(5);
    CHECK(!s.laneSound(0, 2).fitted); // not asked again until it changes
    s.setLaneFit(0, 2, 8);
    s.finishStretching();
    CHECK(!s.laneSound(0, 2).noFit && s.laneSound(0, 2).fitted);
}

// ---- a song, a kit, a multisample, a bounce ---------------------------------------------------------------------

void testASongOpenLoadsWhatFits() {
    const std::string folder = tempFolder("memory");
    for (const char* name : {"a", "b", "c", "d"}) writeMono(folder, name, 48000);
    Rig rig(folder);
    Session& s = rig.session();
    for (int lane = 0; lane < 4; ++lane) std::snprintf(rig.kit().laneSample[lane], kSampleNameRoom, "%c", 'a' + lane);
    rig.limit = kMargin + kHeld + kOneSecond; // room for two: the second to the byte
    const unsigned said = s.saidCount();
    s.applyProject();
    for (int lane = 0; lane < 4; ++lane) {
        const Session::LaneSound& sound = s.laneSound(0, lane);
        CHECK((lane < 2) == (sound.recording != nullptr));
        CHECK((lane >= 2) == (sound.noMemory && sound.missing)); // NOT LOADED, as a missing file is shown
        CHECK(rig.kit().laneSample[lane][0] == 'a' + lane);      // the song keeps every name
    }
    CHECK(s.notLoaded() == 2 && s.saidCount() == said + 1 && std::string(s.saidText()) == "NO MEMORY: 2 NOT LOADED");
    CHECK(std::string(s.saidText()) == Session::notLoadedWords(2));
    // The lane NOT LOADED does not try again by itself, and the EDIT view and readyLane leave it be.
    rig.frames(3);
    s.readyLane(0, 3);
    CHECK(s.laneSound(0, 3).noMemory && s.notLoaded() == 2);
    // Opened again with the room: every one.
    rig.limit = -1;
    s.applyProject();
    CHECK(s.notLoaded() == 0 && s.laneSound(0, 3).recording);
}

void testASongSentAgainWaitsForTheOldOnes() {
    // The engine lets the old song's recordings go a few milliseconds after it is sent the new one (the audio
    // thread hands them back): a load refused meanwhile waits for them, once. Here the audio thread runs.
    const std::string folder = tempFolder("memory");
    for (const char* name : {"a", "b", "c", "d"}) writeMono(folder, name, 48000);
    Rig rig(folder);
    Session& s = rig.session();
    for (int lane = 0; lane < 4; ++lane) s.setLaneSample(0, lane, std::string(1, static_cast<char>('a' + lane)));
    rig.frames(3);
    CHECK(s.recordingBytes() == 4 * kHeld);
    rig.limit = kMargin + 3 * kHeld + kOneSecond; // all four, once the old engine copies have gone
    std::atomic<bool> playing{true};
    std::thread audio([&] {
        std::vector<float> out(2 * 128);
        while (playing.load()) rig.engine->render(out.data(), 128);
    });
    s.applyProject();
    playing.store(false);
    audio.join();
    CHECK(s.notLoaded() == 0 && s.refusals() == 0u);
}

void testAKitAndAMultisample() {
    const std::string folder = tempFolder("memory");
    for (const char* name : {"a", "b", "Piano C3", "Piano C4", "Piano C5"}) writeMono(folder, name, 48000);
    Rig rig(folder);
    Session& s = rig.session();
    // A kit put on: what fits, the rest NOT LOADED with its name, said once.
    TrackData kit = rig.kit();
    std::snprintf(kit.laneSample[0], kSampleNameRoom, "a");
    std::snprintf(kit.laneSample[1], kSampleNameRoom, "b");
    rig.roomFor(kOneSecond);
    unsigned said = s.saidCount();
    CHECK(s.putKit(0, kit));
    CHECK(std::string(rig.kit().laneSample[1]) == "b" && s.laneSound(0, 0).recording && s.laneSound(0, 1).noMemory);
    CHECK(s.saidCount() == said + 1 && std::string(s.saidText()) == Session::notLoadedWords(1));
    // The sampler: one recording picked that does not fit changes nothing.
    rig.limit = -1;
    CHECK(s.addTrack(TrackKind::Sampler));
    rig.frames(2);
    rig.roomFor(kOneSecond - 1);
    s.setTrackSample(1, "Piano C4");
    CHECK(rig.app.project().tracks[1].sample[0] == 0 && std::string(s.saidText()) == Session::noMemoryWords(kOneSecond));
    // A sampler's recording in a song opened that does not fit: NOT LOADED, its name kept.
    std::snprintf(rig.app.project().tracks[1].sample, kSampleNameRoom, "a");
    rig.limit = kMargin + s.recordingBytes() + 2 * kHeld; // (the kit's lane "a" again, then the sampler's: no room)
    s.applyProject();
    CHECK(s.laneSound(0, 0).recording && s.trackSound(1).noMemory && std::string(rig.app.project().tracks[1].sample) == "a");
    rig.limit = -1;
    s.setTrackSample(1, "");
    // A multisample: the picked one, then each other note asked as it is read (its bytes and its floats, moved
    // into the engine's zones): here room for one more.
    rig.roomFor(kOneSecond);
    said = s.saidCount();
    s.setTrackSample(1, "Piano C4");
    rig.frames(2);
    CHECK(std::string(rig.app.project().tracks[1].sample) == "Piano C4" && s.trackSound(1).zones == 2);
    CHECK(s.saidCount() == said + 1 && std::string(s.saidText()) == Session::notLoadedWords(1));
    CHECK(s.recordingBytes() >= kHeld + 2 * 192000); // (two notes in the engine's zones)
}

void testABounceStopsForWantOfMemory() {
    const std::string folder = tempFolder("memory");
    writeMono(folder, "a", 48000);
    Rig rig(folder);
    Session& s = rig.session();
    s.setLaneSample(0, 1, "a");
    rig.app.drums().setStep(1, 0, 1.f);
    rig.frames(2);
    // The bounce loads the song again into an engine of its own: a recording it cannot load would be silence.
    memory::pretend([] { return kMargin + 1000; }); // (a figure that does not move: read from the bounce's thread)
    CHECK(s.startBounce(Session::Bounce::ToSamples));
    s.finishBounce();
    CHECK(s.bounceNoMemory() && s.bounceFiles() == 0 && s.bounceSample().empty());
    rig.frames(2);
    CHECK(std::string(s.saidText()) == Session::kNoMemoryToBounce);
    memory::pretend(nullptr);
    CHECK(s.startBounce(Session::Bounce::ToSamples));
    s.finishBounce();
    CHECK(!s.bounceNoMemory() && s.bounceFiles() == 1);
}

// ---- the system's figure, and the words ------------------------------------------------------------------------

void testTheFigureAndTheWords() {
    memory::pretend(nullptr);
#if defined(__linux__)
    CHECK(memory::systemAvailable() > 0 && memory::available() > 0);
#endif
    memory::pretend([] { return 400 * memory::kMegabyte; });
    CHECK(memory::available() == 400 * memory::kMegabyte && memory::room() == 400 * memory::kMegabyte - kMargin);
    memory::pretend([] { return kMargin - 5; });
    CHECK(memory::room() == 0);
    memory::pretend([] { return -1LL; });
    CHECK(memory::room() == -1);
    CHECK(memory::megabytes(0) == 0 && memory::megabytes(1) == 1 && memory::megabytes(memory::kMegabyte) == 1 && memory::megabytes(memory::kMegabyte + 1) == 2);
    {
        const std::string folder = tempFolder("memory");
        writeMono(folder, "a", 48000);
        Rig rig(folder);
        rig.session().setLaneSample(0, 1, "a");
        rig.frames(2);
        CHECK(rig.session().memoryLine() == "RECORDINGS 1 MB"); // no figure: what is taken alone
        rig.limit = kMargin + 300 * memory::kMegabyte + rig.session().recordingBytes();
        CHECK(rig.session().memoryLine() == "RECORDINGS 1 MB, 300 MB FREE");
    }
    memory::pretend(nullptr);
    // Every line said fits the App's box at XL (LARGE: copybar_test's measure), at its longest on a handheld.
    Canvas canvas;
    if (!canvas.ok) return;
    for (const std::string& words : {Session::noMemoryWords(9999 * memory::kMegabyte), Session::notLoadedWords(96), std::string(Session::kNoMemoryToBounce)})
        CHECK(canvas.fonts->medium.width(words.c_str()) <= 248.f);
    // The kit's lane says NO MEMORY: over DRUM PLAYS, in the lettering the longer of the two is held to.
    CHECK(canvas.fonts->small.width("NO MEMORY:") <= canvas.fonts->small.width("DRUM PLAYS"));
}

} // namespace

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testTheFileSize", testTheFileSize}, {"testTheCountGoesUpAndDown", testTheCountGoesUpAndDown},
        {"testAPickThatDoesNotFitChangesNothing", testAPickThatDoesNotFitChangesNothing}, {"testAnEditAndAFitThatDoNotFit", testAnEditAndAFitThatDoNotFit},
        {"testASongOpenLoadsWhatFits", testASongOpenLoadsWhatFits}, {"testASongSentAgainWaitsForTheOldOnes", testASongSentAgainWaitsForTheOldOnes},
        {"testAKitAndAMultisample", testAKitAndAMultisample}, {"testABounceStopsForWantOfMemory", testABounceStopsForWantOfMemory},
        {"testTheFigureAndTheWords", testTheFigureAndTheWords},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("memory_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
