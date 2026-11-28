#pragma once
// The look the WiFi pages share (the files page, the sound designer, the speed test and the door that
// asks for the number): the Shapetaker family's dark leather, brass edges, cream lettering, teal and
// purple. Each page is one piece of text with three marks in it, filled from here once:
//   @@LOOK@@   the shared style sheet (inside the page's own <style>)
//   @@BRAND@@  the header's emblem, the Shapetaker wordmark and APOTHECA
//   @@FONT@@   a script that loads the device's own lettering from /PIN/font.ttf (Transfer serves it,
//              kept by the browser), the page drawn in the browser's own letters until it comes
// Everything is inline: the page works with no internet (the handheld can be its own hotspot).

#include <string>

namespace apotheca {

// The page with its marks filled in.
std::string withWebLook(const char* page);

} // namespace apotheca
