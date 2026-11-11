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
// as it plays, and then three seconds more for echoes and tails to die away.
// Muted tracks are left out of a song bounce, as they are when it plays.
// Files are 48 kHz, 16-bit, stereo WAV, in the "bounces" folder beside the
// program; the WiFi page lists them for downloading.

#include <cmath>
#include <cstdio>
#include <ctime>

#include <sys/stat.h>

#include "instruments.hpp"
#include "session.hpp"
#include "song_file.hpp"

namespace apotheca {

namespace {

bool writeStereoWav(const std::string& path, const std::vector<float>& interleaved, int sampleRate) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const std::uint32_t frames = static_cast<std::uint32_t>(interleaved.size() / 2), bytes = frames * 4;
    const auto put32 = [&](std::uint32_t v) { std::fputc(v & 255, file); std::fputc((v >> 8) & 255, file); std::fputc((v >> 16) & 255, file); std::fputc((v >> 24) & 255, file); };
    const auto put16 = [&](std::uint32_t v) { std::fputc(v & 255, file); std::fputc((v >> 8) & 255, file); };
    std::fputs("RIFF", file);
    put32(36 + bytes);
    std::fputs("WAVEfmt ", file);
    put32(16);
    put16(1); // plain numbers
    put16(2); // two channels
    put32(static_cast<std::uint32_t>(sampleRate));
    put32(static_cast<std::uint32_t>(sampleRate) * 4);
    put16(4);
    put16(16);
    std::fputs("data", file);
    put32(bytes);
    for (float s : interleaved) {
        const float held = s != s ? 0.f : (s < -1.f ? -1.f : (s > 1.f ? 1.f : s));
        put16(static_cast<std::uint32_t>(static_cast<std::int16_t>(std::lround(held * 32767.f))) & 0xffffu);
    }
    const bool ok = std::ferror(file) == 0;
    return std::fclose(file) == 0 && ok;
}

} // namespace

// How many frames the song lasts, start to finish, once through.
long long songFrames(const Project& project, float sampleRate) {
    long long steps = 0;
    for (int scene = 0; scene < project.sceneCount(); ++scene) steps += static_cast<long long>(project.repeats[scene]) * project.sceneBars(scene) * project.steps[scene];
    const double perStep = sampleRate * 60.0 / (project.tempo > 0.f ? project.tempo : 120.f) / 4.0;
    return static_cast<long long>(std::llround(steps * perStep));
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
        const long long body = songFrames(song, rate), tail = static_cast<long long>(3 * rate);
        int made = 0;
        for (std::size_t n = 0; n < stems.size() && !job->cancel.load(); ++n) {
            // A second engine and a session of its own to load the song into it.
            std::unique_ptr<Engine> engine(new Engine);
            engine->prepare(rate);
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
            Command c;
            c.type = Command::Play;
            engine->send(c);
            std::vector<float> out(static_cast<std::size_t>(2 * (body + tail)));
            long long at = 0;
            const int block = 256;
            while (at < body + tail && !job->cancel.load()) {
                if (at < body && at + block > body) { // the song's last sample: stop, and let the tails ring
                    engine->render(out.data() + 2 * at, static_cast<int>(body - at));
                    at = body;
                }
                if (at == body) {
                    c.type = Command::Stop;
                    engine->send(c);
                }
                const int frames = static_cast<int>(body + tail - at < block ? body + tail - at : block);
                if (frames <= 0) break;
                engine->render(out.data() + 2 * at, frames);
                at += frames;
                job->progress.store((static_cast<float>(n) + static_cast<float>(at) / static_cast<float>(body + tail)) / static_cast<float>(stems.size()));
            }
            if (job->cancel.load()) break;
            std::string path = job->base;
            if (stems[n] >= 0) {
                char name[32];
                const TrackData& t = song.tracks[stems[n]];
                std::snprintf(name, sizeof name, " - %d %s", stems[n] + 1, t.name[0] ? t.name : instrumentName(t.kind));
                path += name;
            }
            path += ".wav";
            if (writeStereoWav(path, out, static_cast<int>(rate))) {
                ++made;
                job->last = path.substr(path.rfind('/') + 1);
            }
        }
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
int Session::bounceFiles() const { return bounce_ && bounce_->done.load() ? bounce_->files : 0; }
std::string Session::bounceLast() const { return bounce_ && bounce_->done.load() ? bounce_->last : std::string(); }

} // namespace apotheca
