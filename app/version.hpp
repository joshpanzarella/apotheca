#pragma once
// Which Apotheca this is. The build gives both: the number is 0.N, where N
// counts the changes made to the program so far (one more with every build
// that is kept), and the day it was built. Shown on the SET screen, on the
// help card, on the WiFi page and at the top of the log.

#ifndef APOTHECA_VERSION
#define APOTHECA_VERSION "dev"
#endif
#ifndef APOTHECA_BUILT
#define APOTHECA_BUILT ""
#endif

namespace apotheca {
constexpr const char* kVersion = APOTHECA_VERSION;
constexpr const char* kBuilt = APOTHECA_BUILT;
} // namespace apotheca
