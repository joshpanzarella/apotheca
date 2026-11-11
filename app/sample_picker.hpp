#pragma once
// The list of recordings, for picking one quickly. The kit's lanes and the
// sampler both open it: a tap of A on SOUND.
//
//   up / down      the next one. It goes onto the lane AT ONCE, so with the
//                  song playing it is heard in its place in the beat; with
//                  the song stopped it is played once.
//   left / right   eight at a time
//   L1 / R1        the folder before / after. Recordings kept in folders (one
//                  level inside the samples folder) are listed a folder at a
//                  time; the loose ones are a folder of their own, UNSORTED.
//                  Arriving in a folder puts its first recording on, as
//                  arriving on a row does.
//   A              keep the one showing
//   B              put back what was there before the list was opened
//   X              hear it again
//   hold Y         for a second: DELETE the recording under the cursor from the
//                  handheld (a bad catch, a take not wanted). Not the first row.
//
// The list knows nothing of lanes or samplers: it is given the names and
// three things it can do (use this one, play it, tell me about it).

#include <SDL.h>

#include <functional>
#include <string>
#include <vector>

#include "input.hpp"
#include "session.hpp"
#include "ui.hpp"

namespace apotheca {

class SamplePicker {
public:
    struct Jobs {
        std::function<void(const std::string&)> use;       // put this recording on ("" is the first row: none)
        std::function<void()> hear;                        // play what is on now
        std::function<const Session::LaneSound*()> sound;  // what is on now, for its picture; may be empty
        std::function<bool(const std::string&)> remove;    // delete this recording's file; true if it is gone
        std::function<const ui::Playhead*()> playhead;     // where the sound being heard has got to, for a line across its picture
    };
    static constexpr int kRows = 9, kPage = 8;

    // `first` names the row before the recordings ("THE BUILT-IN DRUM").
    void open(std::vector<std::string> names, const std::string& current, const char* title, const char* first, Jobs jobs);
    bool isOpen() const { return open_; }
    void onButton(const ButtonEvent& event, bool playing);
    void update(const Input& input, Uint32 nowMs, bool playing);
    void draw(SDL_Renderer* renderer, const Fonts& fonts) const;

    int cursor() const { return cursor_; }                             // 0 is the first row
    int count() const { return static_cast<int>(names_.size()) + 1; }  // the first row and the recordings of the folder showing
    int folders() const { return static_cast<int>(folders_.size()); }
    int folder() const { return folder_; }                             // which is showing, from 0
    const std::string& folderName() const { return folders_[static_cast<std::size_t>(folder_)]; } // "" is the loose ones

private:
    void moveTo(int row, bool playing);
    void showFolder(int index);          // list its recordings
    void turnFolder(int by, bool playing);

    bool open_ = false;
    std::vector<std::string> all_;      // every recording, as "name" or "folder/name"
    std::vector<std::string> folders_;  // the folders there are; "" (the loose ones) first if there are any
    int folder_ = 0;
    std::vector<std::string> names_;    // the recordings of the folder showing, by their whole names
    std::string original_, title_, first_;
    Jobs jobs_;
    int cursor_ = 0;
    float saved_ = 0.f;
    bool stickWas_ = false;
    Button repeating_ = Button::None;
    Uint32 repeatAt_ = 0, lastMs_ = 0;
    bool playing_ = false;
    bool deleting_ = false, deleteTimed_ = false; // Y is held on a recording
    Uint32 deleteSince_ = 0;
    float deletePart_ = 0.f;
public:
    static constexpr Uint32 kDeleteHoldMs = 1000;
    float deletePart() const { return deletePart_; }
};

} // namespace apotheca
