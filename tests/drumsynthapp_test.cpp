// DRUM SYNTH in the app (docs/plans/drumsynth-plan.md, batch 2): a lane's synth voice made and handed
// to the engine as a recording (beside a second engine handed the sound made by hand), in any block
// length; a recording or the drum taking it off; EDIT and FIT on it; a long voice made on the helper
// thread; the song file (a damaged line too); undo; the KIT page's SOUND column and VOICE view.
//
// NOBODY HAS HEARD ANY OF IT: the sound is held to drumsynth::make, which drumsynth_test measures.
//
// drumsynthapp_test NAME... runs just those.

#include <SDL.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apotheca/drum_synth.hpp"
#include "apotheca/engine.hpp"
#include "app.hpp"
#include "canvas.hpp"
#include "lane_edit.hpp"
#include "samples.hpp"
#include "song_file.hpp"
#include "stretch.hpp"
#include "temp_folder.hpp"
#include "ui.hpp"

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

const SDL_Keycode A = SDLK_x, B = SDLK_z, X = SDLK_s, Y = SDLK_a, Up = SDLK_UP, Down = SDLK_DOWN, Left = SDLK_LEFT, Right = SDLK_RIGHT, L1 = SDLK_q, R1 = SDLK_w;
constexpr float kRate = 48000.f;
constexpr int kStep = 6000; // 120 BPM at 48 kHz
constexpr int kLane = 5;

std::vector<float> made(int n) {
    std::vector<float> s(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) s[static_cast<std::size_t>(i)] = 0.5f * std::sin(0.031f * static_cast<float>(i));
    return s;
}

struct Rig {
    std::unique_ptr<Engine> engine{new Engine};
    Input input;
    App app{*engine};
    Uint32 clock = 1000;
    explicit Rig(const std::string& folder = "") {
        if (!folder.empty()) app.session().useSampleFolder(folder);
        frames(2);
    }
    void key(SDL_Keycode code, bool down) {
        SDL_Event event = {};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym = code;
        ButtonEvent press;
        if (input.handle(event, press)) app.onButton(press, input);
    }
    void tap(SDL_Keycode code, int times = 1) {
        for (int i = 0; i < times; ++i) {
            key(code, true);
            frames(1);
            key(code, false);
            frames(1);
        }
    }
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
    // Quiet first, then one hit on the lane, `frames` of it, in blocks of `block`.
    std::vector<float> hit(int lane, int frames, int block = 256) {
        std::vector<float> out(2 * 256);
        for (int i = 0; i < 1300; ++i) engine->render(out.data(), 256); // (7 s: the longest voice, 6 s, has rung out)
        Command c;
        c.type = Command::Trigger;
        c.track = 0;
        c.a = lane;
        c.value = 0.9f;
        engine->send(c);
        std::vector<float> all;
        std::vector<float> piece(static_cast<std::size_t>(2 * block));
        for (int done = 0; done < frames; done += block) {
            engine->render(piece.data(), block);
            all.insert(all.end(), piece.begin(), piece.end());
        }
        all.resize(static_cast<std::size_t>(2 * frames));
        return all;
    }
};

void handOver(Rig& rig, int lane, const std::vector<float>& sound) {
    std::unique_ptr<SampleData> data(new SampleData);
    data->data = sound;
    CHECK(rig.engine->setLaneSample(0, lane, std::move(data)));
    std::vector<float> out(2 * 256);
    rig.engine->render(out.data(), 256);
}

std::vector<float> handMade(const std::vector<float>& file, const LaneEdit& e) {
    const std::vector<float> shaped = makeShape(file, e, kRate);
    std::vector<float> out(shaped.size());
    makeLevel(shaped.data(), static_cast<int>(shaped.size()), e, kRate, out.data());
    return out;
}

float peakOf(const std::vector<float>& s) {
    float p = 0.f;
    for (float v : s) p = std::fabs(v) > p ? std::fabs(v) : p;
    return p;
}

drumsynth::Voice voiceOf(int model, int place = -1, int dial = 0) {
    drumsynth::Voice v = drumsynth::usualVoice(model);
    if (place >= 0) v.dial[place] = static_cast<std::uint8_t>(dial);
    return v;
}

struct Folder {
    std::string path = tempFolder("drumsynthapp");
    Folder() { CHECK(writeWav(path + "/hit.wav", made(24000), 48000)); }
};

// ---- the lane's sound ------------------------------------------------------------------------------

void testTheEnginePlaysIt() {
    const drumsynth::Voice v = voiceOf(drumsynth::kSnare, drumsynth::kTone, 70);
    const std::vector<float> want = drumsynth::make(v, kRate);
    Rig synth, byHand, plain;
    synth.session().setLaneSynth(0, kLane, v);
    CHECK(synth.kit().laneSynth[kLane] == v && synth.kit().laneSample[kLane][0] == 0);
    handOver(byHand, kLane, want);
    const int frames = 20000;
    const std::vector<float> heard = synth.hit(kLane, frames);
    CHECK(heard == byHand.hit(kLane, frames));
    CHECK(heard != plain.hit(kLane, frames)); // (not the built-in drum)
    CHECK(peakOf(heard) > 0.05f);
    // The lane's sound is the made one, as a recording's would be: its outline, its length, the source for EDIT.
    const Session::LaneSound& sound = synth.session().laneSound(0, kLane);
    CHECK(sound.recording && *sound.recording == want && sound.source == sound.recording && sound.shaped == sound.source);
    CHECK(std::fabs(sound.seconds - static_cast<float>(want.size()) / kRate) < 1e-6f && sound.peaks.size() == 512 && sound.sourcePeaks.size() == 512);
    CHECK(!sound.missing && !sound.waiting && !sound.making);
    // In any block length, the same to the bit.
    for (int block : {1, 7, 64, 333}) CHECK(synth.hit(kLane, 9000, block) == std::vector<float>(heard.begin(), heard.begin() + 2 * 9000));
    // A knob turned: made again.
    const drumsynth::Voice dark = voiceOf(drumsynth::kSnare, drumsynth::kTone, 10);
    synth.session().setLaneSynth(0, kLane, dark);
    handOver(byHand, kLane, drumsynth::make(dark, kRate));
    CHECK(synth.hit(kLane, frames) == byHand.hit(kLane, frames));
    // Held to its limits: a dial over 100, a place the model does not use.
    drumsynth::Voice wild = voiceOf(drumsynth::kClave);
    wild.dial[drumsynth::kTune] = 250;
    wild.dial[drumsynth::kTone] = 40;
    synth.session().setLaneSynth(0, kLane, wild);
    CHECK(synth.kit().laneSynth[kLane] == drumsynth::voiceHeld(wild) && synth.kit().laneSynth[kLane].dial[drumsynth::kTune] == 100);
    // Not on a synth track, not off the lanes.
    CHECK(synth.session().addTrack(TrackKind::Phazed));
    synth.session().setLaneSynth(1, 0, v);
    synth.session().setLaneSynth(0, kDrumLanes, v);
    synth.session().setLaneSynth(0, -1, v);
    CHECK(!synth.app.project().tracks[1].laneSynth[0].on());
}

// A lane is a recording, the built-in drum or a synth voice: one at a time.
void testOneOrTheOther() {
    Folder folder;
    Rig rig(folder.path), plain(folder.path);
    Session& session = rig.session();
    session.setLaneSample(0, kLane, "hit");
    plain.session().setLaneSample(0, kLane, "hit");
    session.setLaneSynth(0, kLane, voiceOf(drumsynth::kKick));
    CHECK(rig.kit().laneSample[kLane][0] == 0 && rig.kit().laneSynth[kLane].on());
    session.setLaneSample(0, kLane, "hit"); // a recording takes the voice off
    CHECK(!rig.kit().laneSynth[kLane].on() && std::strcmp(rig.kit().laneSample[kLane], "hit") == 0);
    CHECK(rig.hit(kLane, 20000) == plain.hit(kLane, 20000));
    session.setLaneSynth(0, kLane, voiceOf(drumsynth::kKick));
    session.setLaneSample(0, kLane, ""); // the drum back
    Rig drum;
    CHECK(!rig.kit().laneSynth[kLane].on() && rig.kit().laneSample[kLane][0] == 0);
    CHECK(rig.hit(kLane, 20000) == drum.hit(kLane, 20000));
    session.setLaneSynth(0, kLane, voiceOf(drumsynth::kKick));
    session.setLaneSynth(0, kLane, drumsynth::Voice()); // no voice: the drum back too
    CHECK(rig.hit(kLane, 20000) == drum.hit(kLane, 20000));
    // A voice set where a recording is takes its place; no voice where a recording is: the drum.
    session.setLaneSample(0, kLane, "hit");
    session.setLaneSynth(0, kLane, drumsynth::Voice());
    CHECK(rig.kit().laneSample[kLane][0] == 0 && rig.hit(kLane, 20000) == drum.hit(kLane, 20000));
    // The names that look for a recording pass a synth lane by.
    session.setLaneSynth(0, kLane, voiceOf(drumsynth::kRim));
    CHECK(session.findMissing("") == 0 && !session.laneSound(0, kLane).missing);
}

// EDIT and FIT take a synth voice as they take a recording.
void testEditAndFit() {
    const drumsynth::Voice v = voiceOf(drumsynth::kOpHat);
    const std::vector<float> voice = drumsynth::make(v, kRate);
    LaneEdit e;
    e.start = 5000;
    e.gain = -30;
    e.reverse = 1;
    e.fadeOut = 20;
    Rig rig, byHand;
    rig.session().setLaneSynth(0, kLane, v);
    rig.session().setLaneEdit(0, kLane, e);
    handOver(byHand, kLane, handMade(voice, e));
    CHECK(rig.hit(kLane, 30000) == byHand.hit(kLane, 30000));
    // A knob turned keeps EDIT (START too: a part of the sound, whatever its length).
    const drumsynth::Voice longer = voiceOf(drumsynth::kOpHat, drumsynth::kDecay, 70);
    rig.session().setLaneSynth(0, kLane, longer);
    CHECK(rig.session().laneSound(0, kLane).making); // (over a frame's work: the helper thread)
    rig.session().finishStretching();
    CHECK(rig.kit().laneEdit[kLane] == e);
    handOver(byHand, kLane, handMade(drumsynth::make(longer, kRate), e));
    CHECK(rig.hit(kLane, 30000) == byHand.hit(kLane, 30000));
    // Another model: START and END back to the whole, the rest of EDIT kept.
    rig.session().setLaneSynth(0, kLane, voiceOf(drumsynth::kClap));
    LaneEdit kept = e;
    kept.start = 0;
    CHECK(rig.kit().laneEdit[kLane] == kept);
    // FIT stretches it, as a recording (the built-in drum's FIT does nothing).
    Rig fitted, byHand2;
    fitted.session().setLaneSynth(0, kLane, v);
    fitted.session().setLaneFit(0, kLane, 4);
    fitted.session().finishStretching();
    fitted.frames(2);
    const Session::LaneSound& sound = fitted.session().laneSound(0, kLane);
    CHECK(sound.fitted && !sound.waiting && sound.fitted->size() == static_cast<std::size_t>(4 * kStep));
    handOver(byHand2, kLane, fitTo(voice, 4 * kStep, kRate));
    CHECK(fitted.hit(kLane, 20000) == byHand2.hit(kLane, 20000));
}

// A voice too costly for a frame is made on the helper thread; till then the lane plays what it had.
void testALongVoiceWaits() {
    const drumsynth::Voice longest = voiceOf(drumsynth::kCymbal, drumsynth::kDecay, 100);
    CHECK(drumsynth::work(longest, kRate) > drumsynth::kWorkInAFrame && drumsynth::work(voiceOf(drumsynth::kKick), kRate) < drumsynth::kWorkInAFrame);
    Rig rig, byHand, drum;
    rig.session().setLaneSynth(0, kLane, longest);
    const Session::LaneSound& sound = rig.session().laneSound(0, kLane);
    CHECK(sound.making && sound.shaping && sound.waiting && rig.session().stretching());
    CHECK(rig.hit(kLane, 9000) == drum.hit(kLane, 9000)); // meanwhile: what it had (the built-in drum)
    for (int i = 0; i < 5000 && rig.session().stretching(); ++i) {
        rig.frames(1);
        usleep(2000);
    }
    CHECK(!sound.making && !sound.shaping && !sound.waiting && sound.recording && sound.recording->size() == drumsynth::lengthOf(longest, kRate));
    handOver(byHand, kLane, drumsynth::make(longest, kRate));
    CHECK(rig.hit(kLane, 30000) == byHand.hit(kLane, 30000));
    // Moved on before the work came in: what came in is not taken.
    const drumsynth::Voice other = voiceOf(drumsynth::kCymbal, drumsynth::kDecay, 99);
    rig.session().setLaneSynth(0, kLane, other);
    rig.frames(1); // (the job starts)
    const drumsynth::Voice quick = voiceOf(drumsynth::kClHat);
    rig.session().setLaneSynth(0, kLane, quick); // made at once; the job's is for the voice before
    CHECK(!sound.making);
    rig.session().finishStretching();
    CHECK(*sound.recording == drumsynth::make(quick, kRate));
    // ...and a long one asked for again, then another long one: the last is what lands.
    rig.session().setLaneSynth(0, kLane, other);
    rig.frames(1);
    rig.session().setLaneSynth(0, kLane, longest);
    rig.session().finishStretching();
    CHECK(sound.recording->size() == drumsynth::lengthOf(longest, kRate) && *sound.recording == drumsynth::make(longest, kRate));
    handOver(byHand, kLane, drumsynth::make(longest, kRate));
    CHECK(rig.hit(kLane, 9000) == byHand.hit(kLane, 9000));
}

// ---- the song file and undo -------------------------------------------------------------------------

void testTheSongFile() {
    Rig rig;
    Session& session = rig.session();
    const std::string plain = songText(rig.app.project());
    CHECK(plain.find("lanesynth") == std::string::npos); // nothing written for no voice
    drumsynth::Voice kick = voiceOf(drumsynth::kKick);
    kick.dial[drumsynth::kTune] = 7;
    session.setLaneSynth(0, 0, kick);
    session.setLaneSynth(0, 6, voiceOf(drumsynth::kNoiseBurst));
    const std::string text = songText(rig.app.project());
    char line[96];
    std::snprintf(line, sizeof line, "\nlanesynth 1 kick 7 %d %d %d 0 50 %d %d\n", kick.dial[1], kick.dial[2], kick.dial[3], kick.dial[6], kick.dial[7]);
    CHECK(text.find(line) != std::string::npos);
    const drumsynth::Voice n = voiceOf(drumsynth::kNoiseBurst);
    std::snprintf(line, sizeof line, "\nlanesynth 7 noise %d %d %d %d 0 0 0 0\n", n.dial[0], n.dial[1], n.dial[2], n.dial[3]);
    CHECK(text.find(line) != std::string::npos);
    std::unique_ptr<Project> back(new Project);
    CHECK(parseSong(text, *back) && songText(*back) == text);
    CHECK(back->tracks[0].laneSynth[0] == kick && back->tracks[0].laneSynth[6] == n && !back->tracks[0].laneSynth[1].on());
    session.setLaneSynth(0, 0, drumsynth::Voice());
    session.setLaneSynth(0, 6, drumsynth::Voice());
    CHECK(songText(rig.app.project()) == plain);
    // By hand: each dial held as a double; a word nobody knows, a lane not there skipped; a number that
    // will not read ends the line there (the rest the model's usual); a place the model does not use 0.
    const std::size_t lanes = plain.find("\nlanes ");
    CHECK(lanes != std::string::npos);
    if (lanes == std::string::npos) return;
    const std::size_t after = plain.find('\n', lanes + 1) + 1;
    const auto with = [&](const std::string& lines) { return plain.substr(0, after) + lines + plain.substr(after); };
    std::unique_ptr<Project> odd(new Project);
    CHECK(parseSong(with("lanesynth 1 clave 1e300 -1e300 77 9 9 9 9 9\nlanesynth 2 tom 30 x 9\nlanesynth 3 bongo 1 2 3\nlanesynth 9 kick 1\nlanesynth 0 kick\n"
                         "lanesynth x kick\nlanesynth 4 rim nan\nlanesynth 5\nlanesynth 6 cowbell\nsample 7 0 hit\nlanesynth 7 zap 10\nlanesynth 8 snare 50\nsample 8 0 hit\n"),
                    *odd));
    const TrackData& t = odd->tracks[0];
    CHECK(t.laneSynth[0].model == drumsynth::kClave && t.laneSynth[0].dial[0] == 100 && t.laneSynth[0].dial[1] == 0 && t.laneSynth[0].dial[2] == 0 && t.laneSynth[0].dial[3] == 9);
    CHECK(t.laneSynth[0].dial[7] == 0); // (CLAVE has no place of its own: 0)
    drumsynth::Voice tom = voiceOf(drumsynth::kTom, drumsynth::kTune, 30);
    CHECK(t.laneSynth[1] == tom);
    CHECK(!t.laneSynth[2].on());
    CHECK(t.laneSynth[3] == voiceOf(drumsynth::kRim)); // ("nan" does not read: the usual voice)
    CHECK(!t.laneSynth[4].on() && t.laneSynth[5] == voiceOf(drumsynth::kCowbell));
    CHECK(t.laneSynth[6] == voiceOf(drumsynth::kZap, drumsynth::kTune, 10) && t.laneSample[6][0] == 0); // the later line wins
    CHECK(!t.laneSynth[7].on() && std::strcmp(t.laneSample[7], "hit") == 0);
    // Under a synth track it is not taken.
    CHECK(session.addTrack(TrackKind::Phazed));
    rig.frames(2);
    std::string synth = songText(rig.app.project());
    synth += "lanesynth 3 kick 1 2 3 4 5 6 7 8\n";
    std::unique_ptr<Project> none(new Project);
    CHECK(parseSong(synth, *none));
    for (int lane = 0; lane < kDrumLanes; ++lane) CHECK(!none->tracks[1].laneSynth[lane].on() && !none->tracks[0].laneSynth[lane].on());
}

// A song opened plays its voices (applyProject makes each lane's).
void testASongOpens() {
    Rig rig, byHand;
    const drumsynth::Voice v = voiceOf(drumsynth::kTom, drumsynth::kSweep, 90);
    rig.session().setLaneSynth(0, kLane, v);
    const std::string text = songText(rig.app.project());
    rig.session().setLaneSynth(0, kLane, drumsynth::Voice());
    CHECK(parseSong(text, rig.app.project()));
    rig.session().applyProject();
    rig.frames(2);
    handOver(byHand, kLane, drumsynth::make(v, kRate));
    CHECK(rig.hit(kLane, 20000) == byHand.hit(kLane, 20000));
}

void testUndo() {
    Rig rig, byHand;
    Session& session = rig.session();
    const drumsynth::Voice a = voiceOf(drumsynth::kKick), b = voiceOf(drumsynth::kKick, drumsynth::kDecay, 20), c = voiceOf(drumsynth::kClap);
    session.setLaneSynth(0, kLane, a);
    rig.frames(60);
    session.setLaneSynth(0, kLane, b); // a knob turned: a step
    rig.frames(60);
    CHECK(session.undo());
    rig.frames(4);
    CHECK(rig.kit().laneSynth[kLane] == a);
    handOver(byHand, kLane, drumsynth::make(a, kRate));
    CHECK(rig.hit(kLane, 20000) == byHand.hit(kLane, 20000));
    CHECK(session.redo());
    rig.frames(4);
    CHECK(rig.kit().laneSynth[kLane] == b);
    // Another model, with a cut: undo puts the model and the cut back together.
    LaneEdit cut;
    cut.start = 20000;
    session.setLaneEdit(0, kLane, cut);
    rig.frames(60);
    session.setLaneSynth(0, kLane, c);
    rig.frames(60);
    CHECK(rig.kit().laneEdit[kLane].atRest());
    CHECK(session.undo());
    rig.frames(4);
    CHECK(rig.kit().laneSynth[kLane] == b && rig.kit().laneEdit[kLane] == cut);
    handOver(byHand, kLane, handMade(drumsynth::make(b, kRate), cut));
    CHECK(rig.hit(kLane, 20000) == byHand.hit(kLane, 20000));
}

} // namespace

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testTheEnginePlaysIt", testTheEnginePlaysIt}, {"testOneOrTheOther", testOneOrTheOther}, {"testEditAndFit", testEditAndFit},
        {"testALongVoiceWaits", testALongVoiceWaits}, {"testTheSongFile", testTheSongFile}, {"testASongOpens", testASongOpens}, {"testUndo", testUndo},
    };
    for (const Test& test : tests) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("drumsynthapp_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
