#pragma once
// Which Apotheca this is. The build gives both: the number is 0.N, where N
// counts the changes made to the program so far (one more with every build
// that is kept), and the day it was built. Shown on the SET screen, on the
// help card, on the WiFi page and at the top of the log.
//
// The values live in version.cpp, the only file the build hands them to, so
// a new number recompiles that one file and not the whole program.

namespace apotheca {
extern const char* const kVersion; // "0.111", or "dev" when the build did not say
extern const char* const kBuilt;   // "2026-11-11", or ""
} // namespace apotheca
