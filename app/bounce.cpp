// Bouncing: the song, or its tracks one by one, written out as sound files.
//
// A bounce is not recorded from the output as the song plays. It is worked
// out again from the beginning, by a second engine that nobody is listening
// to, as fast as the handheld can go. So the handheld can be used (and can go
// on playing) while it happens. (No synth has a quality to turn up for it:
// a bounce is the song as it plays, to the sample.)
//
// The song is bounced from its first scene to its last, each as many times
// as it plays, and then for as long as echoes and tails take to die away
// (BounceTail, session.hpp: until there has been no sound for seven seconds,
// and never past the cap). Muted tracks are left out of a song bounce, as
// they are when it plays. The tracks of an EACH TRACK bounce are all made as
// long as the longest of them, so they line up.
// Files are 48 kHz, 16-bit, stereo WAV, in the "bounces" folder beside the
// program; the WiFi page lists them for downloading. A file is written as it
// is worked out (a long tail does not sit in memory), under a name ending
// ".part", and gets its own name only once it is whole.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

#include "instruments.hpp"
#include "session.hpp"
#include "song_file.hpp"

namespace apotheca {

namespace {

void put16(std::FILE* file, std::uint32_t v) {
    std::fputc(static_cast<int>(v & 255), file);
    std::fputc(static_cast<int>((v >> 8) & 255), file);
}
void put32(std::FILE* file, std::uint32_t v) {
    put16(file, v & 0xffffu);
    put16(file, v >> 16);
}
// The 44 bytes a WAV file starts with, for `frames` of 16-bit stereo.
void putHeader(std::FILE* file, std::uint32_t frames, int sampleRate) {
    const std::uint32_t bytes = frames * 4;
    std::fputs("RIFF", file);
    put32(file, 36 + bytes);
    std::fputs("WAVEfmt ", file);
    put32(file, 16);
    put16(file, 1); // plain numbers
    put16(file, 2); // two channels
    put32(file, static_cast<std::uint32_t>(sampleRate));
    put32(file, static_cast<std::uint32_t>(sampleRate) * 4);
    put16(file, 4);
    put16(file, 16);
    std::fputs("data", file);
    put32(file, bytes);
}

// ---- a MIDI file -------------------------------------------------------------------
// The song as a Standard MIDI file (Type 1): every scene as many times as it plays, one track per
// track, at the song's tempo. Notes and chords as written (ratchets struck out, nudges kept); drums
// as General MIDI on channel 10. Muted tracks are left out, as from a song bounce. What has no MIDI
// in it is left out: chance, swing and feel, strum and humanizing, glide, step effects but pitch
// and ratchet (drums), and automation.

constexpr int kMidiPpq = 480; // ticks in a beat; a step is 120 of them (a tenth of one, 12)
constexpr int kMidiTicksPerStep = kMidiPpq / 4;
constexpr int kMidiDrums[kDrumLanes] = {36, 38, 42, 46, 39, 45, 37, 49}; // GM: kick snare cl-hat op-hat clap low-tom rim crash-1

struct MidiNote {
    long start = 0, length = 0; // in ticks
    int pitch = 60, velocity = 100;
};

// One note (or chord key, or drum piece) struck out as the engine strikes it: a ratchet's pieces
// evenly inside its first step, each let go just before the next (engine.cpp).
void addMidiNote(std::vector<MidiNote>& out, long songStep, float length, float nudge, int pitch, float velocity, int ratchet) {
    if (length < kShortestNote || pitch < 0 || pitch > 127) return;
    const int vel = static_cast<int>(std::lround(velocity * 127.f));
    if (vel < 1) return;
    const int hits = ratchet >= 2 ? (ratchet > kMaxRatchet ? kMaxRatchet : ratchet) : 1;
    const double apart = 1.0 / static_cast<double>(hits);
    for (int k = 0; k < hits; ++k) {
        const double left = static_cast<double>(length) - static_cast<double>(k) * apart;
        if (left < kShortestNote) break;
        const double len = k + 1 < hits && left > apart * 0.85 ? apart * 0.85 : left;
        long tick = std::llround((static_cast<double>(songStep) + static_cast<double>(nudge) + static_cast<double>(k) * apart) * kMidiTicksPerStep);
        if (tick < 0) tick = 0; // a nudge before the song's first step starts at the top, as it plays
        MidiNote n;
        n.start = tick;
        n.length = std::max<long>(1, std::llround(len * kMidiTicksPerStep));
        n.pitch = pitch;
        n.velocity = vel > 127 ? 127 : vel;
        out.push_back(n);
    }
}

// One track's notes in one scene, in the scene's own ticks. A shorter cell goes round under a
// longer scene, bar by bar, as it plays (slotOf, engine.cpp).
void gatherMidiScene(const Project& song, int track, int scene, std::vector<MidiNote>& out) {
    const TrackData& t = song.tracks[track];
    const int steps = song.sceneSteps(scene), meter = song.meterOf(scene);
    const int bars = t.bars[scene] < 1 ? 1 : (t.bars[scene] > kMaxBars ? kMaxBars : t.bars[scene]);
    // A bar is the scene's METER of steps, as the engine plays it (a bar of 12 is 3/4): its steps past
    // the meter are never played (review 2026-11-23: they were written, and the bars after slid).
    for (int i = 0; i < steps; ++i) {
        const int cellBar = (i / meter) % bars, st = i % meter;
        if (t.kind == TrackKind::Drums) {
            const DrumPattern& bar = t.drums[scene][cellBar];
            for (int lane = 0; lane < kDrumLanes; ++lane) {
                if (!(bar.level[lane][st] > 0.f)) continue;
                const StepFx& fx = bar.fx[lane][st];
                const int pitch = kMidiDrums[lane] + fx.pitch;
                if (pitch < 0 || pitch > 127) continue;
                const int hits = fx.hits();
                for (int k = 0; k < hits; ++k) { // across its step, evenly, as it plays
                    long tick = std::llround((static_cast<double>(i) + static_cast<double>(bar.nudge[lane][st]) + static_cast<double>(k) / hits) * kMidiTicksPerStep);
                    if (tick < 0) tick = 0;
                    MidiNote n;
                    n.start = tick;
                    n.length = hits > 1 ? std::max<long>(1, std::llround(kMidiTicksPerStep / static_cast<double>(hits) * 0.85)) : 12;
                    n.pitch = pitch;
                    n.velocity = static_cast<int>(std::lround(bar.level[lane][st] * 127.f));
                    if (n.velocity >= 1) {
                        if (n.velocity > 127) n.velocity = 127;
                        out.push_back(n);
                    }
                }
            }
        } else {
            const NotePattern& bar = t.notes[scene][cellBar];
            for (const NoteEvent& e : bar.notes) {
                if (e.velocity > 0.f && e.step == st) addMidiNote(out, i, e.length, e.nudge, e.pitch, e.velocity, e.ratchet);
            }
            for (const ChordEvent& c : bar.chords) {
                if (c.velocity <= 0.f || c.step != st) continue;
                int keys[kChordNotes];
                const int n = c.pitches(keys);
                for (int k = 0; k < n; ++k) addMidiNote(out, i, c.length, c.nudge, keys[k], c.velocity, c.ratchet);
            }
        }
    }
}

void putVarLen(std::string& s, unsigned v) {
    unsigned char tmp[5];
    int n = 0;
    tmp[n++] = static_cast<unsigned char>(v & 0x7f);
    while ((v >>= 7) != 0) tmp[n++] = static_cast<unsigned char>(0x80 | (v & 0x7f));
    while (n > 0) s += static_cast<char>(tmp[--n]);
}

void put16be(std::string& s, unsigned v) {
    s += static_cast<char>((v >> 8) & 255);
    s += static_cast<char>(v & 255);
}

void put32be(std::string& s, unsigned v) {
    put16be(s, v >> 16);
    put16be(s, v & 0xffffu);
}

// One track's bytes: its name, then its notes (an ending first where one begins: no stuck keys).
std::string midiTrackBytes(const char* name, int channel, const std::vector<MidiNote>& notes) {
    struct Hit {
        long tick;
        bool on;
        int pitch, velocity;
    };
    std::vector<Hit> hits;
    for (const MidiNote& n : notes) {
        hits.push_back({n.start, true, n.pitch, n.velocity});
        hits.push_back({n.start + n.length, false, n.pitch, 0});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        return a.tick != b.tick ? a.tick < b.tick : (!a.on && b.on);
    });
    std::string t;
    t += '\0';
    t += static_cast<char>(0xff);
    t += '\x03';
    const std::string label = name ? name : "";
    putVarLen(t, static_cast<unsigned>(label.size()));
    t += label;
    long at = 0;
    for (const Hit& h : hits) {
        putVarLen(t, static_cast<unsigned>(h.tick - at));
        at = h.tick;
        t += static_cast<char>((h.on ? 0x90 : 0x80) | channel);
        t += static_cast<char>(h.pitch);
        t += static_cast<char>(h.on ? h.velocity : 0);
    }
    putVarLen(t, 0);
    t += static_cast<char>(0xff);
    t += '\x2f';
    t += '\0';
    std::string out = "MTrk";
    put32be(out, static_cast<unsigned>(t.size()));
    return out + t;
}

} // namespace

std::string midiFileBytes(const Project& song, const std::string& title) {
    const float tempo = song.tempo > 0.f ? song.tempo : 120.f;
    const auto mpqOf = [](float bpm) { return static_cast<unsigned>(std::llround(60000000.0 / static_cast<double>(bpm))); };
    const int scenes = song.sceneCount();
    std::string file = "MThd";
    put32be(file, 6);
    put16be(file, 1); // Type 1: a conductor track and one track per track
    // The tracks that play: drums on channel 10, the rest from channel 1 up, skipping it.
    struct TrackOut {
        int track, channel;
    };
    std::vector<TrackOut> tracks;
    int channel = 0;
    for (int t = 0; t < song.trackCount(); ++t) {
        if (song.tracks[t].muted) continue;
        if (song.tracks[t].kind == TrackKind::Drums) {
            tracks.push_back({t, 9});
            continue;
        }
        if (channel == 9) ++channel;
        tracks.push_back({t, channel++});
    }
    put16be(file, static_cast<unsigned>(tracks.size() + 1));
    put16be(file, kMidiPpq);
    std::string conductor;
    conductor += '\0';
    conductor += static_cast<char>(0xff);
    conductor += '\x03';
    putVarLen(conductor, static_cast<unsigned>(title.size()));
    conductor += title;
    long conductorAt = 0; // time signatures (a bar of N steps is N sixteenths) and tempos, each where it starts
    int lastNum = -1;
    unsigned lastMpq = 0;
    bool firstScene = true;
    long sceneBase = 0;
    for (int s = 0; s < scenes; ++s) {
        const int num = song.meterOf(s);
        const unsigned mpq = mpqOf(song.sceneTempo[s] > 0.f ? song.sceneTempo[s] : tempo);
        const long tick = sceneBase * kMidiTicksPerStep;
        if (firstScene || num != lastNum) {
            putVarLen(conductor, static_cast<unsigned>(tick - conductorAt));
            conductorAt = tick;
            int top = num, den = 16; // ...as music counts it: 12 steps are 3/4, 16 are 4/4
            while (top % 2 == 0 && den > 4) {
                top /= 2;
                den /= 2;
            }
            int denPow = 0;
            for (int d = den; d > 1; d /= 2) ++denPow;
            conductor += static_cast<char>(0xff);
            conductor += '\x58';
            conductor += '\x04';
            conductor += static_cast<char>(top);
            conductor += static_cast<char>(denPow);
            conductor += '\x18';
            conductor += '\x08';
            lastNum = num;
        }
        if (firstScene || mpq != lastMpq) {
            putVarLen(conductor, static_cast<unsigned>(tick - conductorAt));
            conductorAt = tick;
            conductor += static_cast<char>(0xff);
            conductor += '\x51';
            conductor += '\x03';
            conductor += static_cast<char>((mpq >> 16) & 255);
            conductor += static_cast<char>((mpq >> 8) & 255);
            conductor += static_cast<char>(mpq & 255);
            lastMpq = mpq;
        }
        firstScene = false;
        sceneBase += static_cast<long>(song.repeats[s]) * song.sceneSteps(s);
    }
    putVarLen(conductor, 0);
    conductor += static_cast<char>(0xff);
    conductor += '\x2f';
    conductor += '\0';
    file += "MTrk";
    put32be(file, static_cast<unsigned>(conductor.size()));
    file += conductor;
    for (const TrackOut& o : tracks) {
        std::vector<MidiNote> notes;
        long base = 0; // this scene's first step, in song steps
        for (int s = 0; s < scenes; ++s) {
            std::vector<MidiNote> scene;
            gatherMidiScene(song, o.track, s, scene);
            const long steps = song.sceneSteps(s);
            for (int rep = 0; rep < song.repeats[s]; ++rep) {
                for (MidiNote n : scene) {
                    n.start += (base + static_cast<long>(rep) * steps) * kMidiTicksPerStep;
                    notes.push_back(n);
                }
            }
            base += static_cast<long>(song.repeats[s]) * steps;
        }
        const TrackData& t = song.tracks[o.track];
        file += midiTrackBytes(t.name[0] ? t.name : instrumentName(t.kind), o.channel, notes);
    }
    return file;
}

namespace {

// A stereo WAV written a piece at a time. It is "NAME.part" until close(),
// which gives it its name; one that is dropped part-way leaves nothing.
class WavFile {
public:
    ~WavFile() { drop(); }
    bool open(const std::string& path, int sampleRate) {
        path_ = path;
        rate_ = sampleRate;
        frames_ = 0;
        file_ = std::fopen((path + ".part").c_str(), "wb");
        if (file_) putHeader(file_, 0, rate_); // the length is written again at the end, when it is known
        return file_ != nullptr;
    }
    void write(const float* interleaved, long long frames) {
        if (!file_) return;
        for (long long i = 0; i < 2 * frames; ++i) {
            const float s = interleaved[i];
            const float held = s != s ? 0.f : (s < -1.f ? -1.f : (s > 1.f ? 1.f : s));
            put16(file_, static_cast<std::uint32_t>(static_cast<std::int16_t>(std::lround(held * 32767.f))) & 0xffffu);
        }
        frames_ += frames;
    }
    long long frames() const { return frames_; }
    bool close() {
        if (!file_) return false;
        bool ok = std::fseek(file_, 0, SEEK_SET) == 0;
        if (ok) putHeader(file_, static_cast<std::uint32_t>(frames_), rate_);
        ok = ok && std::ferror(file_) == 0;
        ok = std::fclose(file_) == 0 && ok;
        file_ = nullptr;
        if (ok) ok = std::rename((path_ + ".part").c_str(), path_.c_str()) == 0;
        if (!ok) std::remove((path_ + ".part").c_str());
        return ok;
    }
    void drop() {
        if (!file_) return;
        std::fclose(file_);
        file_ = nullptr;
        std::remove((path_ + ".part").c_str());
    }

private:
    std::FILE* file_ = nullptr;
    std::string path_;
    int rate_ = 48000;
    long long frames_ = 0;
};

// Silence on the end of a finished file, to make it `frames` long (if it is shorter).
bool padWav(const std::string& path, long long had, long long frames, int sampleRate) {
    if (frames <= had) return true;
    std::FILE* file = std::fopen(path.c_str(), "r+b");
    if (!file) return false;
    bool ok = std::fseek(file, 0, SEEK_END) == 0;
    for (long long i = had; ok && i < frames; ++i) put32(file, 0);
    ok = ok && std::fseek(file, 0, SEEK_SET) == 0;
    if (ok) putHeader(file, static_cast<std::uint32_t>(frames), sampleRate);
    ok = ok && std::ferror(file) == 0;
    return std::fclose(file) == 0 && ok;
}

// What a bounce that was killed part-way left behind (the power went, the program was killed):
// its "NAME.wav.part". Nothing lists one and nothing else removes one, and with a tail of
// minutes it can be 90 MB. Only called when no bounce is under way: one that is has its own.
void removeHalfBounces(const std::string& folder) {
    static const char* endings[2] = {".wav.part", ".mid.part"};
    std::vector<std::string> names;
    if (DIR* dir = opendir(folder.c_str())) {
        while (const dirent* e = readdir(dir)) {
            const std::string n = e->d_name;
            for (const char* ending : endings)
                if (n.size() > std::strlen(ending) && n.compare(n.size() - std::strlen(ending), std::strlen(ending), ending) == 0) names.push_back(n);
        }
        closedir(dir);
    }
    for (const std::string& n : names) std::remove((folder + "/" + n).c_str());
}

} // namespace

// How many frames the song lasts, start to finish, once through (each scene at its own tempo, or the song's).
long long songFrames(const Project& project, float sampleRate) {
    const float songTempo = project.tempo > 0.f ? project.tempo : 120.f;
    double frames = 0.0;
    for (int scene = 0; scene < project.sceneCount(); ++scene) {
        const float tempo = project.sceneTempo[scene] > 0.f ? project.sceneTempo[scene] : songTempo;
        frames += static_cast<long long>(project.repeats[scene]) * project.sceneSteps(scene) * sampleRate * 60.0 / tempo / 4.0;
    }
    return static_cast<long long>(std::llround(frames));
}

// The cap is for what is broken and for effects in a row, not for a long tail: it is past the longest any one delay or reverb takes.
static_assert(kBounceCapSeconds > kLongestTailSeconds, "a bounce's cap must be longer than the longest tail a bus can hold");
static_assert(kBounceQuietSeconds > kLongestEchoGapSeconds + 0.5f, "the quiet that ends a bounce must be longer than the longest gap between two echoes");

BounceTail::BounceTail(float sampleRate, float capSeconds) {
    const double rate = sampleRate > 0.f ? sampleRate : 48000.0;
    cap_ = static_cast<long long>(std::llround((capSeconds > 0.f ? capSeconds : 0.f) * rate));
    fade_ = static_cast<long long>(std::llround(kBounceFadeSeconds * rate));
    if (fade_ > cap_) fade_ = cap_;
    quiet_ = static_cast<long long>(std::llround(kBounceQuietSeconds * rate));
    after_ = static_cast<long long>(std::llround(kBounceAfterSeconds * rate));
}

bool BounceTail::take(float* block, int frames) {
    for (int i = 0; i < frames; ++i) {
        const long long at = seen_ + i;
        float& left = block[2 * i];
        float& right = block[2 * i + 1];
        if (at >= cap_) {
            left = right = 0.f;
        } else if (at >= cap_ - fade_) {
            // The last stretch before the cap: down in a straight line, to nothing on the cap's own frame.
            const float gain = static_cast<float>(cap_ - at) / static_cast<float>(fade_ + 1);
            left *= gain;
            right *= gain;
        }
        if (std::fabs(left) >= kBounceFloor || std::fabs(right) >= kBounceFloor) lastSound_ = at + 1; // (not-a-number is not sound: it is written as silence)
    }
    seen_ += frames;
    return seen_ < cap_ && seen_ - lastSound_ < quiet_;
}

long long BounceTail::keep() const {
    const long long most = seen_ < cap_ ? seen_ : cap_;
    return lastSound_ + after_ < most ? lastSound_ + after_ : most;
}

void Session::useBounceFolder(const std::string& folder) {
    bounceFolder_ = folder;
    if (!folder.empty() && !bouncing()) removeHalfBounces(folder);
}

bool Session::startBounce(Bounce what) {
    if (bounce_ && !bounce_->done.load()) return false; // one at a time
    finishBounce();
    if (bounceFolder_.empty()) return false;
    mkdir(bounceFolder_.c_str(), 0755);
    removeHalfBounces(bounceFolder_);
    std::shared_ptr<BounceJob> job(new BounceJob);
    job->song.reset(new Project(project)); // as the song is this moment
    job->what = what;
    job->track = project.track;
    job->sampleFolder = sampleFolder_;
    job->cap = bounceCap_;
    char when[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(when, sizeof when, "%y%m%d-%H%M%S", std::localtime(&now));
    job->base = bounceFolder_ + "/" + (songName_.empty() ? std::string("song") : songName_) + " " + when;
    bounce_ = job;
    job->thread = std::thread([job] {
        Project& song = *job->song;
        if (job->what == Bounce::MidiFile) {
            // No engine: the song walked straight into a MIDI file, under a ".part" name until whole.
            const std::string path = job->base + ".mid";
            const std::string bytes = midiFileBytes(song, job->base.substr(job->base.rfind('/') + 1));
            bool ok = !job->cancel.load();
            if (ok) {
                std::FILE* file = std::fopen((path + ".part").c_str(), "wb");
                ok = file != nullptr;
                if (ok) {
                    ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
                    ok = std::fclose(file) == 0 && ok;
                }
                if (ok) ok = std::rename((path + ".part").c_str(), path.c_str()) == 0;
                if (!ok) std::remove((path + ".part").c_str());
            }
            job->files = ok ? 1 : 0;
            if (ok) job->last = path.substr(path.rfind('/') + 1);
            job->progress.store(1.f);
            job->tail.store(-1.f);
            job->done.store(true);
            return;
        }
        song.playing = false;
        for (TrackData& t : song.tracks) t.solo = false;
        // Which renders to make: the whole song, or one for each track asked for.
        std::vector<int> stems;
        if (job->what == Bounce::Song) stems.push_back(-1);
        for (int t = 0; t < song.trackCount(); ++t)
            if (job->what == Bounce::EachTrack || (job->what == Bounce::ThisTrack && t == job->track)) stems.push_back(t);
        const float rate = 48000.f;
        const long long body = songFrames(song, rate);
        int made = 0;
        std::vector<std::string> paths; // the files made, and how long each is
        std::vector<long long> lengths;
        for (std::size_t n = 0; n < stems.size() && !job->cancel.load(); ++n) {
            // A second engine and a session of its own to load the song into it.
            std::unique_ptr<Engine> engine(new Engine);
            engine->prepare(rate);
            engine->setFlushDenormals(true); // as the program plays it
            engine->useWorkers(2); // a bounce is quicker for it too
            std::unique_ptr<Project> copy(new Project(song));
            if (stems[n] >= 0)
                for (int t = 0; t < kNumTracks; ++t) copy->tracks[t].muted = t != stems[n]; // a stem: that track alone, muted or not
            Session offline(*engine, *copy);
            if (!job->sampleFolder.empty()) offline.useSampleFolder(job->sampleFolder);
            offline.applyProject();
            offline.finishStretching();
            // A moment of nothing first, thrown away: the engine takes everything in (mutes fade
            // over a few milliseconds, and must have finished before the first note).
            {
                std::vector<float> settle(2 * 1024);
                engine->render(settle.data(), 1024);
            }
            std::string path = job->base;
            if (stems[n] >= 0) {
                char name[32];
                const TrackData& t = song.tracks[stems[n]];
                std::snprintf(name, sizeof name, " - %d %s", stems[n] + 1, t.name[0] ? t.name : instrumentName(t.kind));
                path += name;
            }
            path += ".wav";
            WavFile file;
            if (!file.open(path, static_cast<int>(rate))) continue;
            Command c;
            c.type = Command::Play;
            c.c = 1; // once through: the song's last step begins nothing early that belongs to its first
            engine->send(c);
            c.c = 0;
            const int block = 256;
            std::vector<float> out(static_cast<std::size_t>(2 * block));
            // The song itself: its length is known, and this is what the percentage counts.
            for (long long at = 0; at < body && !job->cancel.load();) {
                const int frames = static_cast<int>(body - at < block ? body - at : block);
                engine->render(out.data(), frames);
                file.write(out.data(), frames);
                at += frames;
                job->progress.store((static_cast<float>(n) + static_cast<float>(at) / static_cast<float>(body)) / static_cast<float>(stems.size()));
            }
            // The song's last sample has gone by: stop, and let the tails ring for as long as they do.
            // What is not yet known to belong in the file waits in `held` (seven seconds of it at most).
            c.type = Command::Stop;
            engine->send(c);
            BounceTail tail(rate, job->cap);
            std::vector<float> held;
            long long kept = 0;
            for (bool more = true; more && !job->cancel.load();) {
                engine->render(out.data(), block);
                more = tail.take(out.data(), block);
                held.insert(held.end(), out.begin(), out.end());
                const long long keep = tail.keep();
                if (keep > kept) {
                    file.write(held.data(), keep - kept);
                    held.erase(held.begin(), held.begin() + 2 * (keep - kept));
                    kept = keep;
                }
                job->tail.store(static_cast<float>(tail.seen()) / rate);
            }
            job->tail.store(-1.f);
            if (job->cancel.load()) break; // (the half-written file goes with `file`)
            const long long length = file.frames();
            if (file.close()) {
                ++made;
                paths.push_back(path);
                lengths.push_back(length);
                job->last = path.substr(path.rfind('/') + 1);
            }
        }
        // Tracks bounced together are all as long as the longest of them.
        long long longest = 0;
        for (long long length : lengths) longest = length > longest ? length : longest;
        for (std::size_t i = 0; i < paths.size() && !job->cancel.load(); ++i) padWav(paths[i], lengths[i], longest, static_cast<int>(rate));
        job->files = made;
        job->progress.store(1.f);
        job->done.store(true);
    });
    return true;
}

void Session::finishBounce() {
    if (!bounce_) return;
    if (bounce_->thread.joinable()) bounce_->thread.join();
}

void Session::cancelBounce() {
    if (!bounce_) return;
    bounce_->cancel.store(true);
    finishBounce();
}

bool Session::bouncing() const { return bounce_ && !bounce_->done.load(); }
float Session::bounceProgress() const { return bounce_ ? bounce_->progress.load() : 0.f; }
float Session::bounceTail() const { return bounce_ ? bounce_->tail.load() : -1.f; }
void Session::pretendBounce(float progress, float tailSeconds) {
    cancelBounce();
    bounce_.reset(new BounceJob);
    bounce_->progress.store(progress);
    bounce_->tail.store(tailSeconds);
}
int Session::bounceFiles() const { return bounce_ && bounce_->done.load() ? bounce_->files : 0; }
std::string Session::bounceLast() const { return bounce_ && bounce_->done.load() ? bounce_->last : std::string(); }

} // namespace apotheca
