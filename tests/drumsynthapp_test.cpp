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
#include "drum_voices.hpp"
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
    void stick(int x, int y) {
        for (int axis = 0; axis < 2; ++axis) {
            SDL_Event event = {};
            event.type = SDL_CONTROLLERAXISMOTION;
            event.caxis.axis = static_cast<Uint8>(axis == 0 ? SDL_CONTROLLER_AXIS_LEFTX : SDL_CONTROLLER_AXIS_LEFTY);
            event.caxis.value = static_cast<Sint16>((axis == 0 ? x : y) * 32767);
            ButtonEvent press;
            if (input.handle(event, press)) app.onButton(press, input);
        }
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
        rig.session().readyLane(0, kLane); // (as the designer page and the VOICE view ask every frame: it must still land)
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

// ---- the KIT page ------------------------------------------------------------------------------------

void testTheSoundColumn() {
    Folder folder;
    Rig rig(folder.path);
    Session& session = rig.session();
    session.view = View::Device;
    rig.frames(2);
    KitScreen& kit = rig.app.kit();
    kit.goTo(kLane, KitScreen::kSound);
    // Hold A + down: from the built-in drum, the twelve models at their usual knobs; up, the recordings.
    rig.key(A, true);
    rig.frames(1);
    rig.tap(Down);
    CHECK(rig.kit().laneSynth[kLane] == voiceOf(drumsynth::kKick));
    rig.tap(Down);
    CHECK(rig.kit().laneSynth[kLane] == voiceOf(drumsynth::kSnare));
    rig.tap(Down, drumsynth::kModels - 3);
    CHECK(rig.kit().laneSynth[kLane] == voiceOf(drumsynth::kNoiseBurst));
    rig.tap(Down); // the last: the end stops
    CHECK(rig.kit().laneSynth[kLane] == voiceOf(drumsynth::kNoiseBurst));
    rig.tap(Up, drumsynth::kModels - 1);
    CHECK(!rig.kit().laneSynth[kLane].on() && rig.kit().laneSample[kLane][0] == 0); // the drum
    rig.tap(Up);
    CHECK(!rig.kit().laneSynth[kLane].on() && std::strcmp(rig.kit().laneSample[kLane], "hit") == 0);
    rig.tap(Up); // the last recording: the end stops
    CHECK(std::strcmp(rig.kit().laneSample[kLane], "hit") == 0);
    rig.tap(Down, 4);
    CHECK(rig.kit().laneSynth[kLane].model == drumsynth::kTom && rig.kit().laneSample[kLane][0] == 0);
    rig.key(A, false);
    rig.frames(1);
    CHECK(!kit.voicing() && !kit.picker().isOpen()); // (A let go after a step: no tap)
    // B: the drum back.
    rig.tap(B);
    CHECK(!rig.kit().laneSynth[kLane].on());
    // A tap on a lane with a recording or the drum: the list; on a synth lane: its knobs.
    rig.tap(A);
    CHECK(kit.picker().isOpen() && !kit.voicing());
    rig.tap(B); // (the list closes, as it was)
    CHECK(!kit.picker().isOpen());
    session.setLaneSynth(0, kLane, voiceOf(drumsynth::kRim));
    rig.tap(A);
    CHECK(kit.voicing() && !kit.picker().isOpen() && kit.voicePlace() == KitScreen::kVoiceModel);
    // The KIT page's bar holds every hint, on a synth lane, A held and not.
    rig.tap(B);
    CHECK(!kit.voicing());
    for (int held = 0; held < 2; ++held) {
        if (held) rig.key(A, true);
        rig.frames(1);
        Canvas canvas;
        rig.app.draw(canvas.r, *canvas.fonts);
        CHECK(ui::gHintsOver == 0);
    }
    rig.key(A, false);
    rig.frames(1);
}

void testTheVoiceView() {
    Rig rig;
    Session& session = rig.session();
    session.view = View::Device;
    session.setLaneSynth(0, 2, voiceOf(drumsynth::kKick));
    session.setLaneSynth(0, kLane, voiceOf(drumsynth::kClap));
    rig.frames(2);
    KitScreen& kit = rig.app.kit();
    kit.goTo(2, KitScreen::kSound);
    rig.tap(A);
    CHECK(kit.voicing() && kit.voicePlace() == KitScreen::kVoiceModel);
    // Down: the first knob; hold A + up / down turns it a step.
    rig.tap(Down);
    CHECK(kit.voicePlace() == drumsynth::kTune);
    const int tune = rig.kit().laneSynth[2].dial[drumsynth::kTune];
    rig.key(A, true);
    rig.frames(1);
    rig.tap(Up, 2);
    rig.tap(Down);
    CHECK(rig.kit().laneSynth[2].dial[drumsynth::kTune] == tune + 1);
    // The stick turns it through its range in about a second.
    rig.stick(0, -1);
    rig.frames(70);
    rig.stick(0, 0);
    CHECK(rig.kit().laneSynth[2].dial[drumsynth::kTune] == drumsynth::kDialMost);
    rig.key(A, false);
    rig.frames(1);
    // B: its usual value; B again (there already): back to the kit.
    rig.tap(B);
    CHECK(rig.kit().laneSynth[2].dial[drumsynth::kTune] == tune && kit.voicing());
    rig.tap(B);
    CHECK(!kit.voicing());
    rig.tap(A);
    CHECK(kit.voicing() && kit.voicePlace() == KitScreen::kVoiceModel); // (opened on MODEL)
    // Along a row, between the rows; the ends stop.
    rig.tap(Down);
    rig.tap(Right, 5);
    CHECK(kit.voicePlace() == drumsynth::kSweep - 2); // (KICK's first row: TUNE, DECAY, TONE, SNAP)
    rig.tap(Down);
    CHECK(kit.voicePlace() == drumsynth::kOwn);
    rig.tap(Down);
    CHECK(kit.voicePlace() == drumsynth::kOwn);
    rig.tap(Up, 2);
    CHECK(kit.voicePlace() == KitScreen::kVoiceModel);
    // Hold A on MODEL: the model after, at its usual knobs.
    rig.key(A, true);
    rig.tap(Up);
    rig.key(A, false);
    rig.frames(1);
    CHECK(rig.kit().laneSynth[2] == voiceOf(drumsynth::kSnare));
    // Y: ROLL, the model kept, every shown knob in 25..75, another voice.
    const drumsynth::Voice before = rig.kit().laneSynth[2];
    rig.tap(Y);
    const drumsynth::Voice rolled = rig.kit().laneSynth[2];
    CHECK(rolled.model == drumsynth::kSnare && rolled != before);
    for (int p = 0; p < drumsynth::kPlaces; ++p)
        if (drumsynth::shown(rolled.model, p)) CHECK(rolled.dial[p] >= KitScreen::kRollLow && rolled.dial[p] <= KitScreen::kRollHigh);
    rig.tap(Y);
    CHECK(rig.kit().laneSynth[2] != rolled); // (another roll)
    // Twenty rolls spread over the whole of 25..75: neither end piled up, both halves reached.
    int top = 0, low = 0, high = 0, dials = 0;
    for (int i = 0; i < 20; ++i) {
        rig.tap(Y);
        const drumsynth::Voice v = rig.kit().laneSynth[2];
        for (int p = 0; p < drumsynth::kPlaces; ++p)
            if (drumsynth::shown(v.model, p)) {
                ++dials;
                top += v.dial[p] == KitScreen::kRollHigh || v.dial[p] == KitScreen::kRollLow ? 1 : 0;
                low += v.dial[p] < 50 ? 1 : 0;
                high += v.dial[p] > 50 ? 1 : 0;
            }
    }
    CHECK(top * 10 < dials && low * 4 > dials && high * 4 > dials);
    // Hold B: the bar fills, and at a second every knob is the model's usual; the view stays.
    rig.tap(Down);
    rig.key(B, true);
    rig.frames(30);
    CHECK(kit.deletePart() > 0.3f && rig.kit().laneSynth[2] != voiceOf(drumsynth::kSnare));
    rig.frames(40);
    CHECK(rig.kit().laneSynth[2] == voiceOf(drumsynth::kSnare) && kit.deletePart() == 0.f);
    rig.key(B, false);
    rig.frames(2);
    CHECK(kit.voicing());
    // R1 / L1: the synth lane after / before; the ends stop.
    rig.tap(R1);
    CHECK(kit.cursorLane() == kLane && kit.voicing());
    rig.tap(R1);
    CHECK(kit.cursorLane() == kLane);
    // On CLAP the empty places are passed by: from TUNE down is SPREAD's row (DRIVE).
    kit.openVoice(drumsynth::kTune);
    rig.tap(Down);
    CHECK(kit.voicePlace() == drumsynth::kDrive);
    rig.tap(Left);
    CHECK(kit.voicePlace() == drumsynth::kDrive);
    rig.tap(L1);
    CHECK(kit.cursorLane() == 2);
    // The bar holds every hint, on every box, A held and not.
    for (int place = KitScreen::kVoiceModel; place < drumsynth::kPlaces; ++place) {
        kit.openVoice(place);
        for (int held = 0; held < 2; ++held) {
            if (held) rig.key(A, true);
            rig.frames(1);
            Canvas canvas;
            rig.app.draw(canvas.r, *canvas.fonts);
            CHECK(ui::gHintsOver == 0);
            if (held) rig.key(A, false);
            rig.frames(1);
        }
    }
    // The voice taken off (a recording picked, an undo): back to the kit.
    session.setLaneSample(0, 2, "");
    rig.frames(2);
    CHECK(!kit.voicing());
}

// ---- the named voices (batch 3) ------------------------------------------------------------------------

// What a sound is like, in three numbers: how long it lasts (to 40 dB under its peak), how bright it is
// (its first difference's level over its own), and how its peak stands over its level (crest).
struct Like {
    double seconds = 0, bright = 0, crest = 0;
};
Like likeOf(const std::vector<float>& s) {
    Like l;
    const float floor = peakOf(s) * 0.01f;
    std::size_t last = 0;
    double e = 0, d = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (std::fabs(s[i]) > floor) last = i;
        e += static_cast<double>(s[i]) * s[i];
        if (i) d += (static_cast<double>(s[i]) - s[i - 1]) * (static_cast<double>(s[i]) - s[i - 1]);
    }
    l.seconds = static_cast<double>(last + 1) / kRate;
    l.bright = e > 0 ? std::sqrt(d / e) : 0;
    l.crest = e > 0 ? peakOf(s) / std::sqrt(e / static_cast<double>(last + 1)) : 0;
    return l;
}

bool gMeasure = false;

void testTheNamedVoices() {
    const DrumVoiceData& data = drumVoices();
    for (const std::string& p : data.problems) std::printf("  drumvoices.inc %s\n", p.c_str());
    CHECK(data.problems.empty());
    int all = 0;
    for (int m = drumsynth::kNone + 1; m < drumsynth::kModels; ++m) {
        const int count = voiceCount(m);
        all += count - 1;
        CHECK(count >= 6); // CLASSIC and five more at least
        CHECK(voicePlaceOf(drumsynth::usualVoice(m)) == 0 && std::strcmp(voiceName(m, 0), kUsualVoiceName) == 0);
        std::vector<Like> likes;
        for (int place = 0; place < count; ++place) {
            const drumsynth::Voice v = voiceAt(m, place);
            CHECK(v.model == m && drumsynth::voiceHeld(v) == v && voicePlaceOf(v) == place);
            CHECK(std::strlen(voiceName(m, place)) <= kVoiceNameMost);
            const std::vector<float> s = drumsynth::make(v, kRate);
            CHECK(!s.empty() && s.back() == 0.f && peakOf(s) <= drumsynth::kPeak && peakOf(s) > 0.99f * drumsynth::kPeak);
            likes.push_back(likeOf(s));
            if (gMeasure) std::printf("  %-7s %-12s %5.2f s  bright %.3f  crest %5.2f\n", drumsynth::modelName(m), voiceName(m, place), likes.back().seconds, likes.back().bright, likes.back().crest);
        }
        // Not flat: every two voices of a model are apart in at least one of the three, plainly.
        for (int a = 0; a < count; ++a)
            for (int b = a + 1; b < count; ++b) {
                const Like& x = likes[static_cast<std::size_t>(a)];
                const Like& y = likes[static_cast<std::size_t>(b)];
                const auto apart = [](double p, double q, double by) { return p > q * by || q > p * by; };
                const bool differ = apart(x.seconds, y.seconds, 1.3) || apart(x.bright, y.bright, 1.2) || apart(x.crest, y.crest, 1.3);
                if (!differ) std::printf("  %s: %s and %s are alike\n", drumsynth::modelName(m), voiceName(m, a), voiceName(m, b));
                CHECK(differ);
            }
        // ...and the whole list reaches far: its longest over its shortest, or its brightest over its darkest.
        double lo = 1e9, hi = 0, dark = 1e9, light = 0;
        for (const Like& l : likes) lo = std::min(lo, l.seconds), hi = std::max(hi, l.seconds), dark = std::min(dark, l.bright), light = std::max(light, l.bright);
        CHECK(hi > 3.0 * lo || light > 2.0 * dark);
    }
    if (gMeasure) std::printf("  %d named voices\n", all);
    CHECK(voiceCount(drumsynth::kNone) == 0 && voiceCount(drumsynth::kModels) == 0 && !voiceAt(drumsynth::kKick, 99).on() && std::strcmp(voiceName(drumsynth::kKick, -1), "") == 0);
    drumsynth::Voice turned = drumsynth::usualVoice(drumsynth::kKick);
    turned.dial[drumsynth::kTune] = 3;
    CHECK(voicePlaceOf(turned) == -1 && voicePlaceOf(drumsynth::Voice()) == -1);
    // The reader: what it takes, and every line it cannot.
    const DrumVoiceData good = readDrumVoices("# a note\nvoice kick DEEP ONE   # after\nset TUNE 3  BEND 99\n\nvoice clap WIDE\nset TONE 100\n");
    CHECK(good.problems.empty() && good.voices[drumsynth::kKick].size() == 1 && good.voices[drumsynth::kClap].size() == 1);
    if (good.voices[drumsynth::kKick].size() == 1) {
        drumsynth::Voice want = drumsynth::usualVoice(drumsynth::kKick);
        want.dial[drumsynth::kTune] = 3;
        want.dial[drumsynth::kOwn] = 99;
        CHECK(good.voices[drumsynth::kKick][0].name == "DEEP ONE" && good.voices[drumsynth::kKick][0].voice == want);
    }
    const char* bad[] = {
        "set TUNE 3\n",                              // no voice
        "voice bongo LOW\n",                         // no such model
        "voice kick low\nset TUNE 2\n",               // not capitals (each with a set: not the usual voice either)
        "voice kick A NAME FAR TOO LONG\nset TUNE 2\n", // too long
        "voice kick CLASSIC\nset TUNE 1\n",           // the usual's name
        "voice kick ONE\nset TUNE 2\nvoice kick ONE\nset TUNE 4\n", // twice
        "voice kick ONE\nset SPREAD 2\n",            // not KICK's
        "voice clave ONE\nset TONE 2\n",             // CLAVE has no TONE
        "voice kick ONE\nset TUNE 101\n",            // over 100
        "voice kick ONE\nset TUNE x\n",              // not a number
        "voice kick ONE\nset TUNE\n",                // no number
        "voice kick ONE\n",                          // the usual voice
        "voice kick ONE\nset TUNE 2\nvoice kick TWO\nset TUNE 2\n", // another's
        "voice kick ONE\nset TUNE 2\nplay it\n",     // not understood
    };
    for (const char* text : bad) {
        const DrumVoiceData d = readDrumVoices(text);
        if (d.problems.empty()) std::printf("  not a problem: %s", text);
        CHECK(!d.problems.empty());
    }
}

void testPickingAVoice() {
    Rig rig;
    rig.session().view = View::Device;
    rig.session().setLaneSynth(0, 2, voiceOf(drumsynth::kKick));
    rig.frames(2);
    KitScreen& kit = rig.app.kit();
    kit.goTo(2, KitScreen::kSound);
    rig.tap(A);
    CHECK(kit.voicing() && kit.voicePlace() == KitScreen::kVoiceModel);
    const int count = voiceCount(drumsynth::kKick);
    rig.key(A, true);
    rig.frames(1);
    rig.tap(Right);
    CHECK(rig.kit().laneSynth[2] == voiceAt(drumsynth::kKick, 1));
    rig.tap(Right, count + 2); // the last: the end stops
    CHECK(rig.kit().laneSynth[2] == voiceAt(drumsynth::kKick, count - 1));
    rig.tap(Left, 2);
    CHECK(rig.kit().laneSynth[2] == voiceAt(drumsynth::kKick, count - 3));
    rig.key(A, false);
    rig.frames(1);
    // A knob turned: no voice's dials. LEFT / RIGHT go on from the voice last picked.
    rig.tap(Down);
    rig.key(A, true);
    rig.tap(Up);
    rig.key(A, false);
    rig.frames(1);
    CHECK(voicePlaceOf(rig.kit().laneSynth[2]) == -1);
    rig.tap(Up); // MODEL
    rig.key(A, true);
    rig.tap(Right);
    CHECK(rig.kit().laneSynth[2] == voiceAt(drumsynth::kKick, count - 2));
    rig.tap(Left, count + 2);
    CHECK(rig.kit().laneSynth[2] == drumsynth::usualVoice(drumsynth::kKick)); // CLASSIC, the first
    // Another model: its list from CLASSIC.
    rig.tap(Up);
    CHECK(rig.kit().laneSynth[2] == drumsynth::usualVoice(drumsynth::kSnare));
    rig.tap(Right);
    CHECK(rig.kit().laneSynth[2] == voiceAt(drumsynth::kSnare, 1));
    // Sideways with A held does nothing on a knob.
    rig.key(A, false);
    rig.frames(1);
    rig.tap(Down);
    const drumsynth::Voice before = rig.kit().laneSynth[2];
    rig.key(A, true);
    rig.tap(Right);
    rig.key(A, false);
    rig.frames(1);
    CHECK(rig.kit().laneSynth[2] == before);
    // The voice's name, in brass, after the model; SYNTH when turned by hand.
    rig.tap(Up);
    {
        Canvas canvas;
        rig.app.draw(canvas.r, *canvas.fonts);
        CHECK(ui::gHintsOver == 0);
    }
    // One step of undo takes a pick back.
    rig.frames(60);
    rig.key(A, true);
    rig.tap(Right);
    rig.key(A, false);
    rig.frames(60);
    CHECK(rig.kit().laneSynth[2] == voiceAt(drumsynth::kSnare, 2));
    CHECK(rig.session().undo());
    rig.frames(4);
    CHECK(rig.kit().laneSynth[2] == voiceAt(drumsynth::kSnare, 1));
    // Opened on another lane whose dials are its third voice, then turned: RIGHT goes on from that voice.
    rig.session().setLaneSynth(0, 5, voiceAt(drumsynth::kZap, 3));
    kit.goTo(5, KitScreen::kSound);
    kit.openVoice(drumsynth::kTune);
    rig.key(A, true);
    rig.tap(Up);
    rig.key(A, false);
    rig.frames(1);
    rig.tap(Up); // MODEL
    rig.key(A, true);
    rig.tap(Right);
    rig.key(A, false);
    rig.frames(1);
    CHECK(rig.kit().laneSynth[5] == voiceAt(drumsynth::kZap, 4));
}

} // namespace

// A knob turned is heard ONCE, when the turn is done (owner, 2026-11-17): not while A is held, not
// before kHearAfterMs, once after; X hears at once; a turn with the song playing owes nothing.
void testHeardWhenTheTurnIsDone() {
    Rig rig;
    Session& session = rig.session();
    session.view = View::Device;
    session.setLaneSynth(0, 2, voiceOf(drumsynth::kClap));
    rig.frames(2);
    KitScreen& kit = rig.app.kit();
    kit.goTo(2, KitScreen::kSound);
    rig.tap(A);
    rig.tap(Down);
    CHECK(kit.voicing() && kit.voicePlace() == drumsynth::kTune);
    const int before = kit.hears();
    rig.key(A, true);
    rig.frames(1);
    rig.tap(Up, 3);
    rig.frames(40); // (640 ms, A still held)
    CHECK(kit.hears() == before && kit.hearOwed());
    rig.key(A, false);
    rig.frames(1);
    CHECK(kit.hears() <= before + 1); // (let go: heard once kHearAfterMs has passed and the sound is made)
    rig.frames(60);
    CHECK(kit.hears() == before + 1 && !kit.hearOwed());
    rig.frames(60);
    CHECK(kit.hears() == before + 1); // once
    // Quickly: let go under kHearAfterMs after the last step, heard only once the wait is over.
    rig.key(A, true);
    rig.frames(1);
    rig.key(Up, true);
    rig.frames(1);
    rig.key(Up, false);
    rig.key(A, false);
    rig.frames(2); // (about 50 ms)
    CHECK(kit.hears() == before + 1 && kit.hearOwed());
    rig.frames(60);
    CHECK(kit.hears() == before + 2);
    // X hears at once.
    rig.tap(X);
    CHECK(kit.hears() == before + 3);
    // A turn owed, then Play before it is heard: the song is heard instead.
    rig.key(A, true);
    rig.frames(1);
    rig.tap(Up);
    rig.key(A, false);
    session.togglePlay(false);
    rig.frames(60);
    CHECK(kit.hears() == before + 3 && !kit.hearOwed());
    session.togglePlay(false);
    rig.frames(2);
    // Playing: a turn owes nothing.
    session.togglePlay(false);
    rig.frames(2);
    rig.key(A, true);
    rig.frames(1);
    rig.tap(Up);
    rig.key(A, false);
    rig.frames(60);
    CHECK(kit.hears() == before + 3 && !kit.hearOwed());
    session.togglePlay(false);
    rig.frames(2);
    // Left the view with a turn owed: not heard.
    rig.key(A, true);
    rig.frames(1);
    rig.tap(Up);
    rig.key(A, false);
    rig.frames(1);
    rig.tap(B); // (back: TUNE is not at its usual value, so first B gives it its usual...)
    rig.tap(B); // (...and the second goes back to the kit)
    rig.frames(60);
    CHECK(!kit.voicing() && !kit.hearOwed());
}

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    struct Test {
        const char* name;
        void (*run)();
    };
    const Test tests[] = {
        {"testTheEnginePlaysIt", testTheEnginePlaysIt}, {"testOneOrTheOther", testOneOrTheOther}, {"testEditAndFit", testEditAndFit},
        {"testALongVoiceWaits", testALongVoiceWaits}, {"testTheSongFile", testTheSongFile}, {"testASongOpens", testASongOpens}, {"testUndo", testUndo},
        {"testTheSoundColumn", testTheSoundColumn}, {"testTheVoiceView", testTheVoiceView}, {"testTheNamedVoices", testTheNamedVoices},
        {"testPickingAVoice", testPickingAVoice}, {"testHeardWhenTheTurnIsDone", testHeardWhenTheTurnIsDone},
    };
    int named = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--measure") == 0) gMeasure = true;
        else ++named;
    }
    for (const Test& test : tests) {
        bool wanted = named == 0;
        for (int i = 1; i < argc; ++i) wanted = wanted || std::strcmp(argv[i], test.name) == 0;
        if (!wanted) continue;
        const int before = failures;
        test.run();
        if (failures != before) std::printf("  (in %s)\n", test.name);
    }
    if (failures == 0) std::printf("drumsynthapp_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
