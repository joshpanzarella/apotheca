// CLIPS (desk only): a script of presses played against the app on a virtual clock, every frame kept with the key
// band under it, the sound beside it (clip_record.hpp; docs/dev/videos.md).
#include "clip_record.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include "app.hpp"
#include "ui.hpp"

namespace apotheca {

namespace {

constexpr int kFps = 30;
constexpr int kPassesPerFrame = 2; // the app is moved on 60 times a second, as main.cpp's passes
constexpr int kBandH = 80;         // under the 640x480 screen: 160 pixels once the video is doubled
constexpr Uint32 kStartMs = 1000;  // the virtual clock at the script's 0 (0 is "a press with no time": input.hpp)

std::string trimmed(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

// A button by the name the screen gives it (buttonLabel): A, B, X, Y, UP, L1, START, SEL...
Button buttonCalled(const std::string& word) {
    for (int i = 0; i < static_cast<int>(Button::Count); ++i)
        if (word == buttonLabel(static_cast<Button>(i))) return static_cast<Button>(i);
    return Button::None;
}

// The keyboard stand-in for each button (input.cpp's fromKey, the other way).
SDL_Keycode keyFor(Button b) {
    switch (b) {
    case Button::Up: return SDLK_UP;
    case Button::Down: return SDLK_DOWN;
    case Button::Left: return SDLK_LEFT;
    case Button::Right: return SDLK_RIGHT;
    case Button::A: return SDLK_x;
    case Button::B: return SDLK_z;
    case Button::X: return SDLK_s;
    case Button::Y: return SDLK_a;
    case Button::L1: return SDLK_q;
    case Button::R1: return SDLK_w;
    case Button::L2: return SDLK_1;
    case Button::R2: return SDLK_2;
    case Button::L3: return SDLK_3;
    case Button::Start: return SDLK_RETURN;
    case Button::Select: return SDLK_BACKSPACE;
    case Button::Menu: return SDLK_TAB;
    default: return SDLK_UNKNOWN;
    }
}

const char* stickWord(float x, float y) {
    if (x == 0.f && y == 0.f) return "";
    if (std::fabs(x) >= std::fabs(y)) return x < 0.f ? "LEFT" : "RIGHT";
    return y < 0.f ? "UP" : "DOWN";
}

void writeLe(std::FILE* f, std::uint32_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) std::fputc(static_cast<int>((v >> (8 * i)) & 0xff), f);
}

// A 16-bit stereo WAV's header, for `frames` frames.
void wavHeader(std::FILE* f, int rate, std::uint32_t frames) {
    const std::uint32_t data = frames * 4;
    std::fseek(f, 0, SEEK_SET);
    std::fwrite("RIFF", 1, 4, f);
    writeLe(f, 36 + data, 4);
    std::fwrite("WAVEfmt ", 1, 8, f);
    writeLe(f, 16, 4);
    writeLe(f, 1, 2); // PCM
    writeLe(f, 2, 2);
    writeLe(f, static_cast<std::uint32_t>(rate), 4);
    writeLe(f, static_cast<std::uint32_t>(rate) * 4, 4);
    writeLe(f, 4, 2);
    writeLe(f, 16, 2);
    std::fwrite("data", 1, 4, f);
    writeLe(f, data, 4);
}

std::string srtTime(double s) {
    if (s < 0.0) s = 0.0;
    const long ms = static_cast<long>(std::lround(s * 1000.0));
    char out[32];
    std::snprintf(out, sizeof out, "%02ld:%02ld:%02ld,%03ld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000);
    return out;
}

// The band under the screen: the clip's caption on the left, the combo's chips after it, in the theme's colours
// (the band in the hint bar's panel, the chips cream as its chips are). Drawn at the top of the renderer, 640 x 80.
void drawBand(SDL_Renderer* r, const Fonts& band, const KeyBand& keys, double seconds) {
    using namespace ui;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    fill(r, 0, 0, kScreenW, kBandH, kBg);
    fill(r, 0, 0, kScreenW, 2, kLine);
    float x = 16.f;
    if (!keys.caption().empty()) {
        const Font& f = band.medium;
        f.draw(r, x, (kBandH - f.capHeight()) / 2.f, keys.caption().c_str(), kDim);
        x += f.width(keys.caption().c_str()) + 28.f;
    }
    x = std::max(x, 250.f);
    const float strength = keys.strength(seconds);
    if (strength <= 0.f || keys.chips().empty()) return;
    const int alpha = static_cast<int>(std::lround(255.f * strength));
    const int chipH = static_cast<int>(band.small.capHeight()) + 20;
    const int chipY = (kBandH - chipH) / 2;
    const float textY = chipY + (chipH - band.small.capHeight()) / 2.f;
    bool first = true;
    for (const KeyBand::Chip& chip : keys.chips()) {
        if (!first) {
            band.medium.draw(r, x, (kBandH - band.medium.capHeight()) / 2.f, "+", faded(kDim, alpha));
            x += band.medium.width("+") + 12.f;
        }
        first = false;
        const int w = static_cast<int>(keyWidth(band, chip.key.c_str())) + 20;
        if (x + w > kScreenW - 8) break;
        fill(r, static_cast<int>(x), chipY, w, chipH, faded(chip.lit ? kText : kLine, alpha));
        drawKey(r, band, x + 10.f, textY, chip.key.c_str(), faded(chip.lit ? kBg : kDim, alpha));
        x += w + 12.f;
    }
}

} // namespace

// ---- the script ----------------------------------------------------------------------------

bool ClipScript::parse(const std::string& text, std::string& error) {
    *this = ClipScript();
    std::istringstream lines(text);
    std::string raw;
    int number = 0, order = 0;
    const auto add = [&](Event e) {
        e.order = order++;
        events.push_back(e);
    };
    const auto fail = [&](const char* why) {
        error = "line " + std::to_string(number) + ": " + why + ": " + trimmed(raw);
        return false;
    };
    while (std::getline(lines, raw)) {
        ++number;
        std::string line = raw;
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        line = trimmed(line);
        if (line.empty()) continue;
        std::istringstream words(line);
        std::string first;
        words >> first;
        const auto rest = [&]() {
            std::string r;
            std::getline(words, r);
            return trimmed(r);
        };
        if (first == "title") { title = rest(); continue; }
        if (first == "about") { about = rest(); continue; }
        if (first == "caption") { caption = rest(); continue; }
        if (first == "run") continue; // the program's own options, for scripts/make-clips.sh
        if (first == "from") { if (!(words >> from)) return fail("from needs seconds"); continue; }
        if (first == "length") { if (!(words >> length)) return fail("length needs seconds"); continue; }
        char* end = nullptr;
        const double at = std::strtod(first.c_str(), &end);
        if (end == first.c_str() || *end) return fail("not a time or a word this reads");
        std::string what;
        words >> what;
        Event e;
        e.at = at;
        if (what == "say" || what == "caption") {
            e.kind = what == "say" ? Event::Say : Event::Caption;
            e.text = rest();
            add(e);
            continue;
        }
        std::string name;
        words >> name;
        if (what == "stick") {
            double seconds = 0.0, amount = 1.0;
            if (!(words >> seconds)) return fail("stick needs a direction and seconds");
            if (!(words >> amount)) amount = 1.0;
            const float a = static_cast<float>(amount);
            e.kind = Event::Stick;
            if (name == "LEFT") e.x = -a;
            else if (name == "RIGHT") e.x = a;
            else if (name == "UP") e.y = -a;
            else if (name == "DOWN") e.y = a;
            else return fail("the stick goes UP, DOWN, LEFT or RIGHT");
            add(e);
            Event back = e;
            back.at = at + seconds, back.x = 0.f, back.y = 0.f;
            add(back);
            continue;
        }
        e.button = buttonCalled(name);
        if (e.button == Button::None) return fail("no such button");
        if (what == "down" || what == "up") {
            e.kind = what == "down" ? Event::Down : Event::Up;
            std::string held;
            if (words >> held) e.hold = held == "hold";
            add(e);
        } else if (what == "tap") {
            int times = 1;
            double gap = 0.25;
            if (words >> times) words >> gap;
            for (int i = 0; i < times; ++i) {
                Event down = e, up = e;
                down.kind = Event::Down, down.at = at + i * gap;
                up.kind = Event::Up, up.at = down.at + 0.1;
                add(down), add(up);
            }
        } else if (what == "hold" || what == "press") {
            double seconds = 0.0;
            if (!(words >> seconds)) return fail("hold and press need seconds");
            Event down = e, up = e;
            down.kind = Event::Down, down.hold = what == "hold";
            up.kind = Event::Up, up.at = at + seconds;
            add(down), add(up);
        } else {
            return fail("not tap, hold, press, down, up, stick, say or caption");
        }
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
    if (length <= from) return fail("length is not after from");
    return true;
}

// ---- the band ------------------------------------------------------------------------------

void KeyBand::press(const std::string& key, bool hold, double at) {
    (void)at;
    bool anyLit = false;
    for (const Chip& c : chips_) anyLit = anyLit || c.lit;
    if (!anyLit) chips_.clear(); // a new combo
    const std::string shown = hold ? "HOLD " + key : key;
    for (Chip& c : chips_)
        if (c.key == key || c.key == "HOLD " + key) {
            c.key = shown, c.lit = true;
            return;
        }
    chips_.push_back(Chip{shown, true});
}

void KeyBand::letGo(const std::string& key, double at) {
    bool anyLit = false;
    for (Chip& c : chips_) {
        if (c.key == key || c.key == "HOLD " + key) c.lit = false;
        anyLit = anyLit || c.lit;
    }
    if (!anyLit) allUpAt_ = at;
}

void KeyBand::apply(const ClipScript::Event& e) {
    switch (e.kind) {
    case ClipScript::Event::Down: press(buttonLabel(e.button), e.hold, e.at); break;
    case ClipScript::Event::Up: letGo(buttonLabel(e.button), e.at); break;
    case ClipScript::Event::Stick: {
        const std::string word = stickWord(e.x, e.y);
        if (!stick_.empty() && stick_ != "STICK " + word) letGo(stick_, e.at);
        stick_ = word.empty() ? std::string() : "STICK " + word;
        if (!stick_.empty()) press(stick_, false, e.at);
        break;
    }
    case ClipScript::Event::Caption: caption_ = e.text; break;
    case ClipScript::Event::Say: break;
    }
}

float KeyBand::strength(double seconds) const {
    if (chips_.empty()) return 0.f;
    for (const Chip& c : chips_)
        if (c.lit) return 1.f;
    const double gone = (seconds - allUpAt_) / kFadeSeconds;
    return gone >= 1.0 ? 0.f : gone <= 0.0 ? 1.f : static_cast<float>(1.0 - gone);
}

std::string KeyBand::line() const {
    std::string out;
    for (const Chip& c : chips_) out += (out.empty() ? "" : " + ") + c.key;
    return out;
}

// ---- the recording -------------------------------------------------------------------------

int recordClip(App& app, Input& input, SDL_Renderer* renderer, const Fonts& fonts, const std::vector<unsigned char>& ttf,
               const ClipOptions& options) {
    using ui::kScreenW;
    using ui::kScreenH;
    std::ifstream in(options.script);
    if (!in) {
        std::fprintf(stderr, "record: cannot read %s\n", options.script);
        return 1;
    }
    std::stringstream text;
    text << in.rdbuf();
    ClipScript script;
    std::string error;
    if (!script.parse(text.str(), error)) {
        std::fprintf(stderr, "record: %s: %s\n", options.script, error.c_str());
        return 1;
    }
    const std::string out = options.out;
    mkdir(out.c_str(), 0755);
    std::FILE* frames = std::fopen((out + "/frames.rgb").c_str(), "wb");
    std::FILE* wav = std::fopen((out + "/sound.wav").c_str(), "wb");
    std::FILE* presses = std::fopen((out + "/presses.txt").c_str(), "w");
    if (!frames || !wav || !presses) {
        std::fprintf(stderr, "record: cannot write in %s\n", options.out);
        if (frames) std::fclose(frames);
        if (wav) std::fclose(wav);
        if (presses) std::fclose(presses);
        return 1;
    }
    wavHeader(wav, options.rate, 0);
    // A bounce from SET (its own clip) is written beside the recording, never in the desk's own folders.
    app.session().useBounceFolder(out + "/bounces");

    // The band's lettering: the app's own font, a size up from XL so the chips read on a phone.
    Fonts band;
    band.small.load(renderer, ttf, 20.f);
    band.medium.load(renderer, ttf, 22.f);
    band.large.load(renderer, ttf, 22.f);

    KeyBand keys;
    keys.apply(ClipScript::Event{ClipScript::Event::Caption, 0.0, Button::None, false, 0.f, 0.f, script.caption, -1});
    const long firstPass = std::lround(script.from * kFps) * kPassesPerFrame;
    const long endPass = std::lround(script.length * kFps) * kPassesPerFrame;
    const int passFrames = options.rate / (kFps * kPassesPerFrame);
    std::vector<float> block(static_cast<std::size_t>(passFrames) * 2);
    std::vector<unsigned char> picture(static_cast<std::size_t>(kScreenW) * (kScreenH + kBandH) * 3);
    std::vector<std::int16_t> pcm;
    std::size_t next = 0;
    long framesKept = 0, samplesKept = 0;
    for (long pass = 0; pass < endPass; ++pass) {
        const double seconds = static_cast<double>(pass) / (kFps * kPassesPerFrame);
        const Uint32 now = kStartMs + static_cast<Uint32>(std::lround(seconds * 1000.0));
        // What a still's setup pushed (stills.cpp's taps), as main.cpp reads it.
        SDL_Event event;
        ButtonEvent press;
        while (SDL_PollEvent(&event))
            if (input.handle(event, press)) do app.onButton(press, input); while (input.pending(press));
        // This pass's presses, stamped with the virtual clock.
        while (next < script.events.size() && script.events[next].at <= seconds + 1e-9) {
            const ClipScript::Event& e = script.events[next++];
            keys.apply(e);
            if (e.kind == ClipScript::Event::Down || e.kind == ClipScript::Event::Up) {
                SDL_Event key = {};
                key.type = e.kind == ClipScript::Event::Down ? SDL_KEYDOWN : SDL_KEYUP;
                key.key.timestamp = now;
                key.key.keysym.sym = keyFor(e.button);
                if (input.handle(key, press)) {
                    do {
                        std::fprintf(presses, "%u %s %s\n", now - kStartMs, buttonLabel(press.button), press.down ? "down" : "up");
                        app.onButton(press, input);
                    } while (input.pending(press));
                }
            } else if (e.kind == ClipScript::Event::Stick) {
                SDL_Event axis = {};
                axis.type = SDL_CONTROLLERAXISMOTION;
                axis.caxis.timestamp = now;
                for (int which = 0; which < 2; ++which) {
                    axis.caxis.axis = which == 0 ? SDL_CONTROLLER_AXIS_LEFTX : SDL_CONTROLLER_AXIS_LEFTY;
                    axis.caxis.value = static_cast<Sint16>(std::lround((which == 0 ? e.x : e.y) * 32767.f));
                    input.handle(axis, press);
                }
                std::fprintf(presses, "%u STICK %s\n", now - kStartMs, *stickWord(e.x, e.y) ? stickWord(e.x, e.y) : "LET GO");
            }
        }
        app.update(input, now);
        // The pass's sound, through main.cpp's own callback, in two blocks about the card's size.
        const bool kept = pass >= firstPass;
        for (int half = 0; half < 2; ++half) {
            const int count = passFrames / 2 + (half ? passFrames % 2 : 0);
            options.callback(options.userdata, reinterpret_cast<Uint8*>(block.data()), count * 2 * static_cast<int>(sizeof(float)));
            pcm.clear();
            float s;
            while (options.tap->read(s)) {
                const float c = s > 1.f ? 1.f : s < -1.f ? -1.f : s;
                pcm.push_back(static_cast<std::int16_t>(std::lround(c * 32767.f)));
            }
            if (kept) {
                for (std::int16_t v : pcm) writeLe(wav, static_cast<std::uint16_t>(v), 2);
                samplesKept += static_cast<long>(pcm.size() / 2);
            }
        }
        if (pass % kPassesPerFrame != 0 || !kept) continue;
        // The frame: the screen as the app draws it, then the band under it.
        app.draw(renderer, fonts);
        SDL_Rect screen = {0, 0, kScreenW, kScreenH};
        SDL_RenderReadPixels(renderer, &screen, SDL_PIXELFORMAT_RGB24, picture.data(), kScreenW * 3);
        ui::useTheme(app.session().theme());
        drawBand(renderer, band, keys, seconds);
        SDL_Rect strip = {0, 0, kScreenW, kBandH};
        SDL_RenderReadPixels(renderer, &strip, SDL_PIXELFORMAT_RGB24, picture.data() + static_cast<std::size_t>(kScreenW) * kScreenH * 3, kScreenW * 3);
        std::fwrite(picture.data(), 1, picture.size(), frames);
        SDL_RenderPresent(renderer);
        ++framesKept;
    }
    wavHeader(wav, options.rate, static_cast<std::uint32_t>(samplesKept));
    std::fclose(wav);
    std::fclose(frames);
    std::fclose(presses);
    band.unload();

    // The narration: each "say" until the next (at most six seconds), as subtitles and as a page.
    std::FILE* srt = std::fopen((out + "/clip.srt").c_str(), "w");
    std::FILE* md = std::fopen((out + "/clip.md").c_str(), "w");
    if (md) std::fprintf(md, "# %s\n\n%s\n\n## Narration\n\n", script.title.c_str(), script.about.c_str());
    int said = 0;
    for (std::size_t i = 0; i < script.events.size(); ++i) {
        const ClipScript::Event& e = script.events[i];
        if (e.kind != ClipScript::Event::Say || e.at < script.from || e.at >= script.length) continue;
        double until = std::min(script.length, e.at + 6.0);
        for (std::size_t j = i + 1; j < script.events.size(); ++j)
            if (script.events[j].kind == ClipScript::Event::Say) {
                until = std::min(until, script.events[j].at);
                break;
            }
        const double a = e.at - script.from, b = until - script.from;
        if (srt) std::fprintf(srt, "%d\n%s --> %s\n%s\n\n", ++said, srtTime(a).c_str(), srtTime(b).c_str(), e.text.c_str());
        const long whole = static_cast<long>(a);
        if (md) std::fprintf(md, "- `%ld:%02ld` %s\n", whole / 60, whole % 60, e.text.c_str());
    }
    if (srt) std::fclose(srt);
    if (md) std::fclose(md);
    if (std::FILE* info = std::fopen((out + "/clip.txt").c_str(), "w")) {
        std::fprintf(info, "frames %ld\nsamples %ld\nrate %d\nfps %d\nwidth %d\nheight %d\ndropped %ld\n", framesKept, samplesKept, options.rate, kFps,
                     kScreenW, kScreenH + kBandH, options.tap->dropped());
        std::fclose(info);
    }
    std::printf("record: %ld frames, %ld samples at %d Hz, in %s\n", framesKept, samplesKept, options.rate, options.out);
    return options.tap->dropped() == 0 ? 0 : 1;
}

} // namespace apotheca
