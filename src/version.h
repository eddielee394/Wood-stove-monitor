// Firmware version from `git describe` at build time (see tools/version.py),
// e.g. "v0.6.0", "v0.6.0-3-g8d07049", "v0.6.0-dirty".
#pragma once

namespace version {

const char* get();

}  // namespace version
