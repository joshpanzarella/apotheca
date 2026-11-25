#include "app.hpp"

#include "battery.hpp"
#include "redraw.hpp"
#include "song_file.hpp"
#include "version.hpp"

#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace apotheca {

namespace {
constexpr Uint32 kUpdateHoldMs = 1000; // A held this long accepts an update
constexpr Uint32 kHelpAfterMs = 400;   // with no MENU button: SELECT held this long shows the help card
constexpr Uint32 kPanicNoteMs = 4000; // how long the notice stays once the buttons are let go
constexpr Uint32 kBatteryLogEveryMs = 60000; // a line a minute, and at once when it moves
constexpr int kBatteryLogMove = 5;           // percent either way since the last line: a line at once
constexpr std::size_t kBatteryLogKeep = 3000; // lines kept: two days of minutes
constexpr long kBatteryLogTrimOver = 256 * 1024; // bytes over: cut back to the lines kept
}

App::App(Engine& engine)
    : project_(new Project), session_(engine, *project_), song_(session_), drums_(session_), notes_(session_), kit_(session_), fx_(session_), mods_(session_), sampler_(session_), settings_(session_),
      phazed_(session_, phazedSpec()), jaxep_(session_, jx3pSpec()), fm6_(session_, fm6Spec()), physic_(session_, physicSpec()), acid_(session_, acidSpec()), cloud_(session_, cloudSpec()), morph_(session_, morphSpec()) {}

// The one screen that is up: by the tab, and for PATTERN and DEVICE by the
// track's instrument.
Screen& App::screenNow() {
    const TrackKind kind = project_->current().kind;
    switch (session_.view) {
    case View::Song: return song_;
    case View::Pattern:
        if (isSynth(kind)) return notes_;
        return drums_;
    case View::Device:
        if (kind == TrackKind::Phazed) return phazed_;
        if (kind == TrackKind::Jx3p) return jaxep_;
        if (kind == TrackKind::Fm6) return fm6_;
        if (kind == TrackKind::Physic) return physic_;
        if (kind == TrackKind::Acid) return acid_;
        if (kind == TrackKind::Cloud) return cloud_;
        if (kind == TrackKind::Morph) return morph_;
        if (kind == TrackKind::Sampler) return sampler_;
        return kit_;
    case View::Effects: return fx_;
    case View::Mods: return mods_;
    case View::Settings: break;
    }
    return settings_;
}

void App::onButton(const ButtonEvent& event, const Input& input) {
    const Button b = event.button;
    // SELECT held with another button is a combo (undo, the tempo, the volume...), not the help card:
    // anything pressed while it is down puts the card away for the rest of that hold (no MENU: update()).
    if (event.down) selectUsed_ = b == Button::Select ? false : (selectUsed_ || input.held(Button::Select));
    // All four shoulder buttons at once: panic. Silence now, whatever screen
    // this is. It lasts as long as they are held.
    const bool shoulder = b == Button::L1 || b == Button::R1 || b == Button::L2 || b == Button::R2;
    if (shoulder && event.down) {
        const int down = input.held(Button::L1) + input.held(Button::R1) + input.held(Button::L2) + input.held(Button::R2);
        if (b == Button::R1) { // R1's press may copy or clear (a bar on the drum grid, a cell or a scene on SONG)
            barCopiesAtR1_ = session_.barCopies();
            sceneCopiesAtR1_ = session_.sceneCopies();
            cellCopiesAtR1_ = song_.cellCopies();
        }
        if (down == 1) {
            viewBeforeShoulders_ = session_.view; // the first of them: the triggers may turn the tabs on the way
            slicingBeforeShoulders_ = drums_.slicing(); // ...and L1 may open or shut the slices
            trackBeforeShoulders_ = session_.project.track; // ...or, on FX, L1 / R1 step the track
            sceneBeforeShoulders_ = session_.project.scene;  // ...or R2 off SONG takes the cursor's cell, its scene and bar 1 (G46)
            barBeforeShoulders_ = session_.project.bar;
            setRowBeforeShoulders_ = settings_.cursorRow();  // ...or, on SET, L1 / R1 jump a heading (G28)
            songBeforeShoulders_ = song_.visit();             // ...or the triggers open SONG, its cursor put on that cell (G46)
            recBeforeShoulders_ = session_.recording(); // ...or, on a kit lane, the sampler or CLOUD's RECORDING, R1 records
            typingBeforeShoulders_ = typingAName();     // ...or, on a synth's or the sampler's PRESET, R1 opens the name keys
        }
        if (down == 4) {
            panicking_ = true;
            panicNote_ = true;
            session_.view = viewBeforeShoulders_;
            if (drums_.slicing() != slicingBeforeShoulders_) drums_.setSlicing(slicingBeforeShoulders_);
            if (trackBeforeShoulders_ < session_.project.trackCount()) session_.project.track = trackBeforeShoulders_;
            session_.project.scene = sceneBeforeShoulders_;
            session_.project.bar = barBeforeShoulders_;
            if (settings_.cursorRow() != setRowBeforeShoulders_) settings_.goTo(setRowBeforeShoulders_);
            song_.putBack(songBeforeShoulders_);
            session_.wantSlices = -1;
            // A copy or a clear made by this panic's R1 (on the grids L1 then R1 copy the bar; on SONG R1 copies or
            // clears a cell or a scene) was on the way, not meant: it is taken back, the clipboard as it was just
            // before that R1 went down. (Not as it was when the first shoulder did: a paste since, L1 + A on a grid
            // or an R1 tapped on SONG, spent the copy, and it stays spent.)
            bool tookBack = false;
            if (session_.barCopies() != barCopiesAtR1_) session_.takeBackBarCopy(), tookBack = true;
            if (session_.sceneCopies() != sceneCopiesAtR1_) session_.takeBackSceneCopy(), tookBack = true;
            if (song_.cellCopies() != cellCopiesAtR1_) song_.takeBackCellCopy(), tookBack = true;
            if (tookBack) {
                saidSeen_ = session_.saidCount();
                undoSaidUntil_ = 0;
            }
            // A recording begun on the way (R1 first) is called off, its take thrown away: it would replace
            // the lane's or the track's sound when it ended. (One that was running is another thing: R1 first
            // ends it, and its take is kept at once, as any R1 keeps it.)
            const auto under = [](Session::Rec r) { return r == Session::Rec::Waiting || r == Session::Rec::Recording; };
            if (under(session_.recording()) && !under(recBeforeShoulders_)) session_.dropRecording();
            // The name keys opened on the way (R1 first on PRESET, perhaps on a tab R2 turned to) are closed, nothing
            // kept: on one of the player's own they open with its name typed in, and the next B would replace its
            // file. Keys that were up before the panic stay up, with what was typed.
            if (!typingBeforeShoulders_) closePresetKeys();
            song_.shouldersPressed();  // (L1 then R1 on a head was on its way here: no track is copied)
            drums_.shouldersPressed(); // L1 let go after this is no tap of L1
            notes_.shouldersPressed();
            session_.panic(true);
            return;
        }
        // L1 held and another shoulder pressed: L1 let go is then no tap (the slices, the zoom).
        if (b != Button::L1 && input.held(Button::L1)) drums_.shouldersPressed(), notes_.shouldersPressed();
        // With both triggers down the bumpers are on their way to a panic, not painting.
        if ((b == Button::L1 || b == Button::R1) && input.held(Button::L2) && input.held(Button::R2)) return;
    } else if (shoulder && panicking_) {
        panicking_ = false;
        panicNoteUntil_ = now_ + kPanicNoteMs;
        session_.panic(false);
    }
    // An update has arrived over WiFi and wants an answer. Nothing else
    // happens until it has one: hold A for a second to accept, hold B a second to delete it (a bar
    // fills for each, update()). Of A and B, the one pressed last is the one that fills; any other
    // press calls a delete off, as everywhere.
    if (session_.updateWaiting()) {
        if (b == Button::A) updateHeld_ = event.down, updateSince_ = 0, updateDeleting_ = updateDeleting_ && !event.down;
        else if (b == Button::B) updateDeleting_ = event.down, deleteSince_ = 0, updateHeld_ = updateHeld_ && !event.down;
        else if (event.down) updateDeleting_ = false;
        return;
    }
    updateHeld_ = false;
    updateDeleting_ = false;

    // The notice is up and the buttons are let go: the next press only puts
    // it away. It might have been an accident; that press must not also
    // place a hit or start the song.
    if (panicNote_ && !panicking_ && !shoulder && event.down) {
        panicNote_ = false;
        return;
    }

    // The button test (SETUP) has every button to itself.
    if (session_.view == View::Settings && (settings_.testing() || settings_.typing()) && !panicking_) {
        settings_.onButton(event, input);
        return;
    }

    // A name being typed (a new folder, a preset): every button is the keys', the tabs, SELECT and L3 too.
    if (typingAName() && !panicking_) {
        screenNow().onButton(event, input);
        return;
    }

    // L3 (the stick pressed in), from any screen: the SONG screen with MASTER under the cursor, so
    // hold A + up / down is the output's level at once (owner, 2026-11-17). Pressed again while the
    // cursor is still on MASTER: back to the screen it came from.
    if (b == Button::L3 && !panicking_) {
        if (!event.down) return;
        if (cameByL3_ && session_.view == View::Song && song_.cursorOnMaster()) {
            session_.view = viewBeforeL3_;
        } else {
            viewBeforeL3_ = session_.view;
            session_.view = View::Song;
            song_.moveToMaster();
        }
        cameByL3_ = session_.view == View::Song;
        selectAlone_ = false;
        return;
    }
    // SELECT tapped by itself, away from the SONG screen: go there. (On the
    // SONG screen the same tap opens the list of songs.) A second way to
    // SONG, for when L2 cannot be trusted.
    if (session_.view != View::Song) {
        if (b == Button::Select) {
            if (event.down) selectAlone_ = true;
            else if (selectAlone_) selectAlone_ = false, showTab(View::Song);
            return;
        }
        if (event.down) selectAlone_ = false;
    } else {
        selectAlone_ = false;
    }

    // SELECT held with L1 or R1: quieter or louder, from any screen. (The
    // handheld's own volume buttons belong to its firmware and may not
    // reach a running program.)
    // SELECT held with X: automation WRITE, on and off.
    if (b == Button::X && input.held(Button::Select) && !panicking_) {
        if (event.down) {
            session_.setWriting(!session_.writing());
            std::snprintf(undoSaid_, sizeof undoSaid_, "%s", session_.writing() ? "WRITE ON: MOVES ARE RECORDED" : "WRITE OFF");
            undoSaidUntil_ = now_ + 1500;
            selectAlone_ = false;
            song_.selectWasUsed();
        }
        return;
    }

    // SELECT held with B: undo. With A: redo. From any screen, playing or not.
    if ((b == Button::B || b == Button::A) && input.held(Button::Select) && !panicking_) {
        if (event.down) {
            const bool back = b == Button::B;
            const unsigned moves = session_.sceneMoves();
            const bool done = back ? session_.undo() : session_.redo();
            if (session_.sceneMoves() != moves) song_.scenesMoved(session_.sceneMoveFrom(), session_.sceneMoveBy()); // a NEXT SCENE taken back or put back
            if (done) std::snprintf(undoSaid_, sizeof undoSaid_, back ? "UNDONE  (%d MORE)" : "PUT BACK  (%d MORE)", back ? session_.undoSteps() : session_.redoSteps());
            else std::snprintf(undoSaid_, sizeof undoSaid_, "%s", back ? "NOTHING TO UNDO" : "NOTHING TO PUT BACK");
            undoSaidUntil_ = now_ + 1500;
            selectAlone_ = false;
            song_.selectWasUsed();
        }
        return;
    }

    // SELECT held with Y, on every tab: NEXT SCENE. The scene being worked on (on SONG the one under
    // the cursor) is copied into a new one right after it and the player is put there, on the same
    // screen with the cursor where it was (on SONG, on the copy), to carry on adding. One press,
    // nothing asked: SELECT + B takes it back. No screen is handed a SELECT-held Y (its own Y would
    // mute, CATCH or roll), and Y let go afterwards is not the screen's Y. (SELECT let go is not its
    // tap either: any button pressed while it is held has seen to that, above, and on SONG
    // selectWasUsed.) The one exception: in the list of songs SELECT + Y is the list's, save a rev.
    if (b == Button::Y && nextSceneY_ && !event.down) {
        nextSceneY_ = false;
        return;
    }
    if (b == Button::Y && input.held(Button::Select) && !(session_.view == View::Song && song_.listingSongs())) {
        if (event.down) nextSceneY_ = true;
        if (event.down && !panicking_) { // (not in a panic: all four shoulder buttons are down, and nothing is to change)
            const int to = session_.nextScene(sceneOfSelect());
            if (to >= 0) song_.scenesMoved(to - 1, 1); // the SONG screen's cursor goes with its scene
            if (to >= 0) std::snprintf(undoSaid_, sizeof undoSaid_, "SCENE %d: COPY OF %d", to + 1, to);
            else std::snprintf(undoSaid_, sizeof undoSaid_, "NO ROOM: %d SCENES", kMaxScenes);
            undoSaidUntil_ = now_ + 1500;
            song_.selectWasUsed();
        }
        return;
    }

    if ((b == Button::L1 || b == Button::R1) && input.held(Button::Select) && !panicking_) {
        if (event.down) {
            changeVolume(b == Button::R1 ? 0.1f : -0.1f);
            song_.selectWasUsed();
        }
        return;
    }

    // SELECT held with the D-pad: the tempo, from any screen. Up and down a
    // beat a minute, left and right ten.
    const bool direction = b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right;
    if (direction && input.held(Button::Select)) {
        if (event.down) {
            const float by = b == Button::Up ? 1.f : b == Button::Down ? -1.f : b == Button::Right ? 10.f : -10.f;
            session_.setTempo(project_->tempo + by);
            song_.selectWasUsed();
        }
        return;
    }

    // SELECT held with L2 or R2, on every tab: the scene a step shorter or longer (the scene NEXT SCENE
    // would copy). Any number of steps, up to every step of its longest cell; the steps past its end are
    // shaded on the grids.
    if ((b == Button::L2 || b == Button::R2) && input.held(Button::Select) && !panicking_) {
        if (event.down) {
            const int scene = sceneOfSelect();
            session_.setSceneSteps(scene, project_->sceneSteps(scene) + (b == Button::R2 ? 1 : -1));
            const int steps = project_->sceneSteps(scene), whole = project_->sceneBars(scene) * project_->meterOf(scene);
            if (steps == whole) std::snprintf(undoSaid_, sizeof undoSaid_, "SCENE %d: ALL %d STEPS", scene + 1, steps);
            else std::snprintf(undoSaid_, sizeof undoSaid_, "SCENE %d: %d STEPS OF %d", scene + 1, steps, whole);
            undoSaidUntil_ = now_ + 1500;
            selectAlone_ = false;
            song_.selectWasUsed();
        }
        return;
    }

    // The shoulder triggers walk along the tabs, round the ends (G35: SONG and SET are one press apart).
    if (event.button == Button::L2 || event.button == Button::R2) {
        // With A held on a grid they nudge what A has hold of, early or late.
        if (input.held(Button::A) && session_.view == View::Pattern && !panicking_) {
            screenNow().onButton(event, input);
            return;
        }
        // With Y held on the roll they halve or double the chord in hand (gesture audit G27). On the
        // drum grid Y held is the dice, and they stay swallowed there (below).
        if (input.held(Button::Y) && session_.view == View::Pattern && isSynth(project_->current().kind) && !panicking_) {
            notes_.onButton(event, input);
            const unsigned saidBefore = saidSeen_;
            takeSessionSaid();
            if (saidSeen_ != saidBefore) { // (the box covers the tiles: dropLengthSaid takes it away when the pick moves on)
                lengthSaidUntil_ = undoSaidUntil_;
                lengthSaidPick_[0] = notes_.handIndex(), lengthSaidPick_[1] = notes_.handInversion(), lengthSaidPick_[2] = session_.packIndex();
            }
            return;
        }
        // On the drum grid, R2 with R1 held turns R1's brush random. R2 by
        // itself is the next screen, at once, like everywhere else.
        const bool onDrums = session_.view == View::Pattern && !isSynth(project_->current().kind);
        if (event.button == Button::R2 && onDrums && !panicking_ && drums_.painting() && !input.held(Button::L2)) {
            drums_.onButton(event, input);
            return;
        }
        if (!event.down || input.held(Button::A) || input.held(Button::Y)) return;
        const int tabs = static_cast<int>(View::Settings) + 1;
        const int next = (static_cast<int>(session_.view) + (event.button == Button::R2 ? 1 : tabs - 1)) % tabs;
        showTab(static_cast<View>(next));
        return;
    }
    screenNow().onButton(event, input);
    takeSessionSaid();
}

// The tabs (gesture audit G46, 2026-11-25). SONG's cursor never followed the cell being worked on, and a tab
// off SONG showed that cell whatever the cursor was on: R2 x 3 with the cursor on track 5 could show track
// 2's effects. Now SONG opens on the cell being worked on, and leaving it by a tab takes the cell its cursor
// was moved to on this visit (track, scene, bar 1, as a tap of A), or nothing: a glance at SONG, the cell
// worked on (away and back: its bar is kept), a head, REPEATS, + TRACK or MASTER change nothing.
void App::showTab(View to) {
    if (session_.view == to) return;
    int track = 0, scene = 0;
    if (session_.view == View::Song && song_.movedToCell(&track, &scene) && (track != project_->track || scene != project_->scene)) {
        project_->track = track;
        project_->scene = scene;
        project_->bar = 0;
    }
    if (to == View::Song) song_.openOnWorkingCell();
    session_.view = to;
}

void App::closePresetKeys() {
    for (SynthScreen* synth : {&phazed_, &jaxep_, &fm6_, &physic_, &acid_, &cloud_, &morph_}) synth->closeKeys();
    sampler_.closeKeys();
}

// The scene SELECT + Y and SELECT + L2 / R2 act on: the one being worked on, or on SONG the one under
// the cursor (off the scenes there, on a head, + TRACK or MASTER: the one being worked on).
int App::sceneOfSelect() const {
    const int under = session_.view == View::Song ? song_.sceneUnderCursor() : -1;
    return under >= 0 ? under : project_->scene;
}

void App::takeSessionSaid() {
    if (session_.saidCount() == saidSeen_) return;
    saidSeen_ = session_.saidCount();
    std::snprintf(undoSaid_, sizeof undoSaid_, "%s", session_.saidText());
    undoSaidUntil_ = now_ + 1500;
}

// The box that says a chord's length (L2 / R2 with Y held on the roll, G27) is drawn over the middle three of the
// pack's tiles, the lit one among them: while the player is still choosing, it hid the chord a RIGHT would pick.
// So it goes once the pick moves on, by the D-pad or the stick: another chord, inversion or pack. Let go of Y, it
// stays its moment (the tiles are gone, and it says what landed: nothing moves the pick with Y up).
void App::dropLengthSaid() {
    if (lengthSaidUntil_ == 0) return;
    if (undoSaidUntil_ != lengthSaidUntil_ || now_ >= lengthSaidUntil_) { // something else said since, or it went
        lengthSaidUntil_ = 0;
        return;
    }
    if (notes_.handIndex() == lengthSaidPick_[0] && notes_.handInversion() == lengthSaidPick_[1] && session_.packIndex() == lengthSaidPick_[2]) return;
    undoSaidUntil_ = 0;
    lengthSaidUntil_ = 0;
}

void App::changeVolume(float by) {
    session_.setVolume(session_.volume() + by);
    volumeShownUntil_ = now_ + 1500;
    boxIsBrightness_ = false;
}

void App::changeBrightness(int by) {
    session_.setBrightness(session_.brightness() + by);
    volumeShownUntil_ = now_ + 1500;
    boxIsBrightness_ = true;
}

void App::volumeKey(bool up, const Input& input) {
    if (session_.screenHere() && input.held(Button::Menu)) {
        changeBrightness(up ? kBrightKeyStep : -kBrightKeyStep);
        menuUsed_ = true; // the card goes from this pass's update: the screen itself is what is judged
        return;
    }
    changeVolume(up ? 0.1f : -0.1f);
}

void App::logBattery(Uint32 nowMs) {
    if (batteryLog_.empty() || battery_ < 0) return; // nowhere to keep it, or no battery (a desk computer)
    const int moved = batteryLoggedPercent_ < 0 ? kBatteryLogMove : std::abs(battery_ - batteryLoggedPercent_);
    if (batteryLoggedAt_ != 0 && nowMs - batteryLoggedAt_ < kBatteryLogEveryMs && moved < kBatteryLogMove && charging_ == batteryLoggedCharging_) return;
    const long microvolts = batteryRoot_.empty() ? batteryMicrovolts() : batteryMicrovolts(batteryRoot_);
    char volts[16];
    if (microvolts < 0) std::snprintf(volts, sizeof volts, "?");
    else std::snprintf(volts, sizeof volts, "%.2fV", microvolts / 1000000.0);
    char line[64];
    std::snprintf(line, sizeof line, "[%.1f] %d%% %s %s\n", nowMs / 1000.f, battery_, volts, charging_ ? "charging" : "discharging");
    std::FILE* f = std::fopen(batteryLog_.c_str(), "a");
    if (!f) return;
    std::fwrite(line, 1, std::strlen(line), f);
    std::fflush(f);
    fsync(fileno(f)); // the point is the tail after a power cut: on the card at once
    const long size = std::ftell(f);
    std::fclose(f);
    batteryLoggedAt_ = nowMs ? nowMs : 1;
    batteryLoggedPercent_ = battery_;
    batteryLoggedCharging_ = charging_;
    if (size < kBatteryLogTrimOver) return;
    // Too long: back to the lines kept, whole or not at all.
    std::string text;
    if (!readTextFile(batteryLog_, text)) return;
    std::size_t lines = 0, cut = text.size();
    for (std::size_t i = text.size(); i > 0; --i) {
        if (text[i - 1] == '\n' && ++lines > kBatteryLogKeep) { cut = i; break; }
    }
    if (cut < text.size()) writeTextFile(batteryLog_, text.substr(cut));
}

void App::update(const Input& input, Uint32 nowMs) {
    now_ = nowMs;
    if (session_.view != View::Song || !song_.cursorOnMaster()) cameByL3_ = false; // (moved away: L3 goes to MASTER again)
    // The battery, looked at every ten seconds (and at once, the first time).
    if (batteryReadAt_ == 0 || nowMs - batteryReadAt_ >= 10000) {
        batteryReadAt_ = nowMs ? nowMs : 1;
        battery_ = batteryRoot_.empty() ? batteryPercent(&charging_) : batteryPercent(&charging_, batteryRoot_);
        logBattery(nowMs);
    }
    // The help card: there for as long as MENU is held. On a handheld whose
    // firmware keeps MENU for itself (it was left out of the controls setup),
    // SELECT held by itself for four tenths of a second does it instead; then
    // letting go of SELECT is not a tap. Another button pressed with SELECT (a combo: SEL + B, B, B
    // undoing) puts it away until SELECT is let go, so the box above the bar is seen (the volume,
    // UNDONE...), as MENU's card goes for MENU + a volume key.
    const bool busy = (session_.view == View::Settings && (settings_.testing() || settings_.typing())) || typingAName();
    const bool menu = input.has(Button::Menu);
    ui::gHelpHint = menu ? "MENU: HELP" : "HLD SEL: HELP"; // (busy: none, set after the screen's update below)
    ui::gUpdateReady = session_.updateReady();
    if (menu) {
        if (!input.held(Button::Menu)) menuUsed_ = false;
        help_ = input.held(Button::Menu) && !busy && !menuUsed_;
    } else if (!input.held(Button::Select)) {
        selectHeldSince_ = 0;
        help_ = false;
        selectUsed_ = false;
    } else if (selectUsed_) {
        help_ = false;
    } else if (selectHeldSince_ == 0) {
        selectHeldSince_ = nowMs;
    } else if (!help_ && nowMs - selectHeldSince_ >= kHelpAfterMs && !busy) {
        help_ = true;
        selectAlone_ = false;
        song_.selectWasUsed();
    }
    // While the help card is up: how hard the sound works (the engine's last second) and the whole
    // program (read once a second, from the second after it opened).
    if (help_) {
        if (!helpWas_) cpu_.restart();
        cpu_.read(nowMs);
        const float share = session_.engine.soundShare(), worst = session_.engine.soundWorst();
        load_[0] = share < 0.f ? -1 : static_cast<int>(std::lround(100.f * share));
        load_[1] = worst < 0.f ? -1 : static_cast<int>(std::lround(100.f * worst));
        load_[2] = cpu_.percent();
        if (pretending_)
            for (int i = 0; i < 3; ++i) load_[i] = pretend_[i];
        for (int& v : load_) v = v > 999 ? 999 : v; // (a call that took ten times its sound's time: still three figures)
    } else {
        load_[0] = load_[1] = load_[2] = -1;
    }
    helpWas_ = help_;
    if (panicNote_ && !panicking_ && nowMs >= panicNoteUntil_) panicNote_ = false;
    // B held for a whole second on the update notice: deleted (G40: a tap of B threw it away, while accepting took a hold).
    if (!input.held(Button::B)) updateDeleting_ = false; // (a let go the notice never saw)
    if (session_.updateWaiting() && updateDeleting_) {
        if (deleteSince_ == 0) deleteSince_ = nowMs;
        if (nowMs - deleteSince_ >= kUpdateHoldMs) {
            updateDeleting_ = false;
            session_.discardUpdate();
        }
    }
    // A held for a whole second on the update notice: accepted.
    if (session_.updateWaiting() && updateHeld_) {
        if (updateSince_ == 0) updateSince_ = nowMs;
        if (nowMs - updateSince_ >= kUpdateHoldMs) {
            updateHeld_ = false;
            updateSaid_ = session_.acceptUpdate() ? 1 : 2;
            updateSaidUntil_ = nowMs + 5000;
        }
    }
    if (updateSaid_ != 0 && nowMs >= updateSaidUntil_) updateSaid_ = 0;
    session_.browsingRecordings(browsingRecordings()); // (a visit to the list: one step of undo, or none)
    session_.update(nowMs);
    // X held hears a recording until it is let go. Let go, or another screen or track up while it
    // is held (the screen that asked would never hear of the let go): it stops.
    session_.keepHearing(input.held(Button::X), &screenNow());
    session_.keepModsHeard(input.held(Button::X), &screenNow()); // (X held on a knob's MOD while stopped: it ran)
    if (session_.resyncs() != resyncsSeen_) { // the engine fell behind and was sent the song again; that stops it
        resyncsSeen_ = session_.resyncs();
        std::snprintf(undoSaid_, sizeof undoSaid_, "SOUND FELL BEHIND AND WAS RESET: PRESS START");
        undoSaidUntil_ = nowMs + 4000;
    }
    session_.engine.setScopeTrack(project_->track);
    // An effect being tried on the FX screen (A held on a slot's line) is called off when another screen is up.
    if (&screenNow() != &fx_) fx_.callOff();
    // ...and so is a track being deleted on SONG (B held): L2 / R2 turn the tab without the screen seeing a press.
    if (&screenNow() != &song_) song_.callOffDelete();
    // ...and so is a hold of B on a preset, a kit, EDIT or VOICE, or in a list of recordings: back with B still held,
    // the bar would have been full at once (its file deleted, never seen filling).
    for (SynthScreen* synth : {&phazed_, &jaxep_, &fm6_, &physic_, &acid_, &cloud_, &morph_})
        if (&screenNow() != synth) synth->callOffDelete();
    if (&screenNow() != &sampler_) sampler_.callOffDelete();
    if (&screenNow() != &kit_) kit_.callOffDelete();
    screenNow().update(input, nowMs);
    takeSessionSaid(); // (what a screen's update said: the stick's TRANSPOSE)
    dropLengthSaid();
    // Where the card does not come up (the name keys, SET's keys and button test), the bar does not offer it.
    // Asked after the screen's update: the button test closes by itself there, and the bar drawn this pass
    // must be the one the redraw saw change (idle_test).
    if ((session_.view == View::Settings && (settings_.testing() || settings_.typing())) || typingAName()) ui::gHelpHint = "";
}

unsigned App::stirring() const {
    unsigned stirs = session_.stirring() | screenNow().stirring();
    if (project_->playing) stirs |= kStirPlaying;
    if (volumeShowing()) stirs |= kStirVolume;
    if (undoSaid()[0]) stirs |= kStirSaid;
    if (panicNote_ && !panicking_) stirs |= kStirPanicNote; // (while panicking the four buttons are held)
    if (updateSaid_ != 0) stirs |= kStirUpdateSaid;
    return stirs;
}

std::string App::loadWords(const int load[3]) {
    std::string words;
    char part[32];
    if (load[0] >= 0) {
        std::snprintf(part, sizeof part, "SOUND %d%%", load[0]);
        words += part;
    }
    // load[1], the worst call, is still measured but not shown (owner, 2026-11-17: not that helpful).
    if (load[2] >= 0) {
        std::snprintf(part, sizeof part, "%sCPU %d%%", words.empty() ? "" : "  ", load[2]);
        words += part;
    }
    return words;
}

void App::readouts(Readouts& out) const {
    const Engine& engine = session_.engine;
    unsigned heard = 0;
    const int scene = engine.playScene(&heard);
    const int place[6] = {engine.playStep(), engine.playBar(), engine.playRepeat(), scene * 4096 + static_cast<int>(heard % 4096u),
                          engine.silenced() ? 1 : 0, static_cast<int>(engine.songEnded())};
    for (int i = 0; i < 6; ++i) out.place[i] = place[i];
    out.scope.resize(ui::kScopeSamples);
    engine.readScope(out.scope.data(), ui::kScopeSamples);
    out.meters.clear();
    for (int track = kMasterTrack; track < kNumTracks; ++track)
        for (int slot = 0; slot < slotsOf(track); ++slot) out.meters.push_back(engine.effectMeter(track, slot));
    for (int i = 0; i < 3; ++i) out.load[i] = load_[i];
    float places[kGrainsShown]; // the track up's CLOUD grains (MAIN's picture draws them)
    const int grains = engine.grainPlaces(project_->track, places, kGrainsShown);
    out.grains.assign(places, places + (grains > 0 ? grains : 0));
    out.midi[0] = static_cast<int>(session_.midiHeard());
    out.midi[1] = session_.midiKeyboards();
}

void App::draw(SDL_Renderer* renderer, const Fonts& fonts) const {
    ui::useTheme(session_.theme()); // THEME: chosen on the SET page, in use from this frame
    screenNow().draw(renderer, fonts);

    if (now_ < volumeShownUntil_) {
        // The volume, for a moment after it is changed.
        using namespace ui;
        const int w = 260, h = 46, x = (kScreenW - w) / 2, y = kScreenH - kHintsH - h - 12;
        box(renderer, x, y, w, h, kPanel, kLine);
        char text[32];
        const float part = boxIsBrightness_ ? static_cast<float>(session_.brightness()) / 100.f : session_.volume();
        if (boxIsBrightness_) std::snprintf(text, sizeof text, "BRIGHTNESS  %d%%", session_.brightness());
        else std::snprintf(text, sizeof text, "VOLUME  %d%%", static_cast<int>(session_.volume() * 100.f + 0.5f));
        fonts.small.draw(renderer, x + 12.f, y + 9.f, text, kText);
        fill(renderer, x + 12, y + 28, w - 24, 8, kLine);
        fill(renderer, x + 12, y + 28, static_cast<int>((w - 24) * part), 8, kTeal);
    }
    // COUNT IN (easywins batch 2): the beats to go, in the box above the bar, for as long as it lasts (the
    // song is "playing" meanwhile, so every pass is drawn). It stands in for anything said there.
    char counting[24] = "";
    if (project_->playing && session_.engine.countingIn() > 0) std::snprintf(counting, sizeof counting, "COUNT IN  %d", session_.engine.countingIn());
    if (undoSaid()[0] || counting[0]) {
        using namespace ui;
        const char* said = counting[0] ? counting : undoSaid();
        const int w = 260, h = 34, x = (kScreenW - w) / 2, y = kScreenH - kHintsH - h - 12;
        box(renderer, x, y, w, h, kPanel, kTeal);
        // (Its longest at XL, "SCENE 10: 127 STEPS OF 128", is wider than the box: held at LARGE. Fonts::within.)
        const Font& saidFont = fonts.within(&Fonts::medium, said, w - 12.f);
        saidFont.drawCentered(renderer, kScreenW / 2.f, y + (h - saidFont.capHeight()) / 2.f, said, counting[0] ? kBrass : kText);
    }
    if (session_.updateWaiting() || updateSaid_ != 0) {
        using namespace ui;
        const int w = 470, h = 150, x = (kScreenW - w) / 2, y = (kScreenH - h) / 2;
        fill(renderer, 0, kTopBarH + 1, kScreenW, kScreenH - kTopBarH - 1, faded(kBg, 190));
        box(renderer, x, y, w, h, kPanel, kTeal);
        outline(renderer, x, y, w, h, kTeal, 2);
        if (updateSaid_ == 1) {
            fonts.large.draw(renderer, x + 18.f, y + 16.f, "UPDATE ACCEPTED", kTeal);
            fonts.medium.draw(renderer, x + 18.f, y + 46.f, "It will be used the next time Apotheca starts.", kText);
            fonts.small.draw(renderer, x + 18.f, y + 74.f, "The one running now is kept. If the new one does not", kDim);
            fonts.small.draw(renderer, x + 18.f, y + 90.f, "start, Apotheca goes back to this one by itself.", kDim);
            const char* now = updateNowWords(quitTurnsOff_);
            fonts.within(&Fonts::small, now, w - 36.f).draw(renderer, x + 18.f, y + 116.f, now, kText);
        } else if (updateSaid_ == 2) {
            fonts.large.draw(renderer, x + 18.f, y + 16.f, "COULD NOT KEEP THE UPDATE", kBrass);
            fonts.medium.draw(renderer, x + 18.f, y + 46.f, "Nothing was changed. Is the card full?", kText);
        } else {
            char size[64];
            std::snprintf(size, sizeof size, "A new Apotheca was sent over WiFi (%.1f MB).", static_cast<double>(session_.updateMegabytes()));
            fonts.large.draw(renderer, x + 18.f, y + 16.f, "AN UPDATE HAS ARRIVED", kTeal);
            fonts.medium.draw(renderer, x + 18.f, y + 46.f, size, kText);
            fonts.small.draw(renderer, x + 18.f, y + 72.f, "Only accept it if you sent it yourself.", kBrass);
            if (updateDeleting_) fonts.small.draw(renderer, x + 18.f, y + 96.f, kUpdateDeletingWords, kStop);
            else fonts.small.draw(renderer, x + 18.f, y + 96.f, kUpdateWords, kText);
            // The hold, filling: A's in teal, B's in red.
            fill(renderer, x + 18, y + 120, w - 36, 8, kLine);
            const Uint32 since = updateDeleting_ ? deleteSince_ : updateHeld_ ? updateSince_ : 0;
            if (since != 0) {
                const float part = static_cast<float>(now_ - since) / static_cast<float>(kUpdateHoldMs);
                fill(renderer, x + 18, y + 120, static_cast<int>((w - 36) * (part > 1.f ? 1.f : part)), 8, updateDeleting_ ? kStop : kTeal);
            }
        }
    } else if (panicNote_) {
        using namespace ui;
        const int w = 440, h = 124, x = (kScreenW - w) / 2, y = (kScreenH - h) / 2;
        fill(renderer, 0, kTopBarH + 1, kScreenW, kScreenH - kTopBarH - 1, faded(kBg, 170));
        box(renderer, x, y, w, h, kPanel, kBrass);
        outline(renderer, x, y, w, h, kBrass, 2);
        // Few words, large: it comes up when things are loud and someone is in a hurry.
        fonts.large.draw(renderer, x + 20.f, y + 18.f, "ALL SOUND STOPPED", kBrass);
        fonts.large.draw(renderer, x + 20.f, y + 54.f, "You held all four shoulder buttons.", kText);
        fonts.large.draw(renderer, x + 20.f, y + 84.f, panicking_ ? "Let go to carry on." : "Press any button to carry on.", kText);
    } else if (help_) {
        // What is true on every screen, so the bar at the bottom need not say it each time.
        using namespace ui;
        const int w = 560, h = 440, x = (kScreenW - w) / 2, y = kTopBarH + 4;
        fill(renderer, 0, kTopBarH + 1, kScreenW, kScreenH - kTopBarH - 1, faded(kBg, 215));
        box(renderer, x, y, w, h, kPanel, kTeal);
        outline(renderer, x, y, w, h, kTeal, 2);
        struct Line { const char* key; const char* does; };
        const Line everywhere[] = {
            {"X", "hear it (SONG: solo). Held: a step's effects, a knob's MOD"},
            {"STICK or D-PAD", "move: the stick for big moves, the D-pad for exact"},
            {"A", "place, open or pick. Held: a direction changes it"},
            {"B", "delete, empty, or go back. X held: empty the effects"}, // (gesture audit G11: no room for a chip)
            {"START", "play / stop"},
            {"L2  R2", "the screen before / the screen after"},
            {"SELECT, tapped", "the SONG screen. On SONG: the list of songs"},
        };
        const Line held[] = {
            {"B", "UNDO the last change (20 deep)"},
            {"A", "put it back again"},
            {"X", "WRITE on / off: record a knob's moves while playing"},
            {"L1  R1", "quieter / louder"},
            {"L2  R2", "the scene a step shorter / longer"},
            {"Y", "NEXT SCENE, a copy of this one to add to"},
            {"UP  DOWN  LEFT  RIGHT", "tempo: a beat a minute / ten at a time"}, // (one line: the card is full)
            {"START", quitWords(quitTurnsOff_)},
        };
        float ly = y + 14.f;
        const auto title = [&](const char* text) {
            fonts.small.draw(renderer, x + 18.f, ly, text, kDim);
            ly += 20.f;
        };
        const auto line = [&](const Line& l) {
            const float kw = keyWidth(fonts, l.key) + 8.f;
            fill(renderer, x + 18, static_cast<int>(ly) - 3, static_cast<int>(kw), 17, kText);
            drawKey(renderer, fonts, x + 22.f, ly, l.key, kBg);
            fonts.within(&Fonts::small, l.does, w - 150.f - 12.f).draw(renderer, x + 150.f, ly, l.does, kText); // (the card's longest line at XL: held at LARGE)
            ly += 22.f;
        };
        title("ON EVERY SCREEN");
        for (const Line& l : everywhere) line(l);
        ly += 6.f;
        title("WITH SEL HELD");
        for (const Line& l : held) line(l);
        ly += 6.f;
        title("STOP ALL SOUND");
        line({"L1 + L2 + R1 + R2", ""});
        fonts.small.draw(renderer, x + 150.f, ly - 22.f, "all four at once", kText);
        fonts.small.drawRight(renderer, x + w - 18.f, y + 14.f, "LET GO TO CLOSE", kDim);
        char version[48];
        std::snprintf(version, sizeof version, "APOTHECA %s", kVersion);
        fonts.small.drawRight(renderer, x + w - 18.f, y + h - 22.f, version, kDim);
        if (battery_ >= 0) {
            // The battery: a small cell that empties, and the number.
            char level[32];
            std::snprintf(level, sizeof level, "BATTERY %d%%", battery_);
            const float tw = fonts.small.width(level);
            const int bx = x + w - 18 - static_cast<int>(tw) - 34, by = y + h - 46;
            const SDL_Color color = battery_ <= 15 && !charging_ ? kStop : kText;
            outline(renderer, bx, by, 26, 12, color);
            fill(renderer, bx + 26, by + 3, 2, 6, color);
            fill(renderer, bx + 2, by + 2, 22 * battery_ / 100, 8, color);
            fonts.small.drawRight(renderer, x + w - 18.f, y + h - 44.f, level, color);
            // CHARGING before it on its line: the line above is how hard the program works.
            if (charging_) fonts.small.drawRight(renderer, static_cast<float>(bx) - 10.f, y + h - 44.f, "CHARGING", kDim);
        }
        // Above the battery: how hard the program works (owner, 2026-11-15). Held at LARGE where the
        // chosen size would reach the card's own words to its left.
        const std::string load = loadWords(load_);
        const float leftEnds = x + 150.f + fonts.within(&Fonts::small, held[7].does, w - 162.f).width(held[7].does); // (quit Apotheca, on its row)
        if (!load.empty()) fonts.within(&Fonts::small, load.c_str(), x + w - 18.f - leftEnds - kLoadClear).drawRight(renderer, x + w - 18.f, y + h - 64.f, load.c_str(), kText);
    }
}

} // namespace apotheca
