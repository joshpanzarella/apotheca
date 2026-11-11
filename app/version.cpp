#include "version.hpp"

#ifndef APOTHECA_VERSION
#define APOTHECA_VERSION "dev"
#endif
#ifndef APOTHECA_BUILT
#define APOTHECA_BUILT ""
#endif

namespace apotheca {
const char* const kVersion = APOTHECA_VERSION;
const char* const kBuilt = APOTHECA_BUILT;
} // namespace apotheca
