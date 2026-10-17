#pragma once
// Tiny diagnostics log for field debugging (never called from the audio thread).
//   macOS:   ~/Library/Logs/Mangle/mangle.log   (also visible in Console.app)
//   Windows: %APPDATA%\Mangle\mangle.log
//   Linux:   ~/.config/Mangle/mangle.log
// MANGLE_LOG_FILE=<path> overrides the location (tests). Lines are timestamped; the file
// is rotated to mangle.old.log at ~1 MB. Writing is serialised by a mutex, so any
// message/background thread may log; the audio thread must not.

#include <juce_core/juce_core.h>

namespace mangle::log {

/** Where the log is written (see above). */
juce::File file();

/** Appends one timestamped line ("2026-09-26 15:44:02.123 [analysis] ..."). */
void write(const juce::String& category, const juce::String& message);

/** Plugin version + build id, e.g. "0.1.0 (076962f)". */
juce::String versionString();

} // namespace mangle::log
