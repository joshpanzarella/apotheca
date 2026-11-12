// Bouncing: the song, or its tracks one by one, written out as sound files.
//
// A bounce is not recorded from the output as the song plays. It is worked
// out again from the beginning, by a second engine that nobody is listening
// to, as fast as the handheld can go. That has two consequences:
//   - the handheld can be used (and can go on playing) while it happens
//   - it does not have to keep up, so Twin Analog is bounced at its best
//     oversampling (x8) whatever the track is set to, even where that is
//     more than the handheld could play
//
// The song is bounced from its first scene to its last, each as many times
// as it plays, and then for as long as echoes and tails take to die away
// (BounceTail, session.hpp: until there has been no sound for four seconds,
// and never past the cap). Muted tracks are left out of a song bounce, as
// they are when it plays. The tracks of an EACH TRACK bounce are all made as
// long as the longest of them, so they line up.
// Files are 48 kHz, 16-bit, stereo WAV, in the "bounces" folder beside the
// program; the WiFi page lists them for downloading. A file is written as it
// is worked out (a long tail does not sit in memory), under a name ending
// ".part", and gets its own name only once it is whole.

#include <cmath>
#include <cstdio>
#include <ctime>

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

} // namespace

// How many frames the song lasts, start to finish, once through.
long long songFrames(const Project& project, float sampleRate) {
    long long steps = 0;
    for (int scene = 0; scene < project.sceneCount(); ++scene) steps += static_cast<long long>(project.repeats[scene]) * project.sceneSteps(scene);
    const double perStep = sampleRate * 60.0 / (project.tempo > 0.f ? project.tempo : 120.f) / 4.0;
    return static_cast<long long>(std::llround(steps * perStep));
}

// The cap is for what is broken, not for a long tail: it is past the longest any delay or reverb takes.
static_assert(kBounceCapSeconds > kLongestTailSeconds, "a bounce's cap must be longer than the longest tail a bus can hold");
static_assert(kBounceQuietSeconds > 8.f * 15.f / kMinTempo, "the quiet that ends a bounce must be longer than the longest gap between two echoes");

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

bool Session::startBounce(Bounce what) {
    if (bounce_ && !bounce_->done.load()) return false; // one at a time
    finishBounce();
    if (bounceFolder_.empty()) return false;
    mkdir(bounceFolder_.c_str(), 0755);
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
            // Not in real time, so nothing need be held back: Twin Analog at its best.
            for (int t = 0; t < copy->trackCount(); ++t)
                if (copy->tracks[t].kind == TrackKind::Twin) offline.setSynthSetting(t, twin::Oversample, 3);
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
            engine->send(c);
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
            // What is not yet known to belong in the file waits in `held` (four seconds of it at most).
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
