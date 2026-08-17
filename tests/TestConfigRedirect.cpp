// Keep the test binaries away from the developer's real settings file.
//
// Config::save() persists the WHOLE singleton, and no test loads it, so a
// single save from a test process replaces every preference in
// ~/Library/MAGDA/config.json with defaults -- API keys included. That is not
// hypothetical: RemoteApiHost persists client grants by calling Config::save(),
// so simply constructing one in a test wrote defaults over the real file.
//
// This runs before main(), so the redirect is in place no matter which test
// executes first or how the binary is invoked.

#include <juce_core/juce_core.h>

#include <cstdlib>

namespace {

struct RedirectConfigFile {
    RedirectConfigFile() {
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("magda-test-config");
        dir.createDirectory();
        // A name per process, not a shared one: two test binaries running at
        // once (ctest does that) must never see each other's settings if a test
        // ever starts reading config back.
        const auto path = dir.getNonexistentChildFile("config", ".json").getFullPathName();

#if JUCE_WINDOWS
        _putenv_s("MAGDA_CONFIG_FILE", path.toRawUTF8());
#else
        setenv("MAGDA_CONFIG_FILE", path.toRawUTF8(), 1);
#endif
    }
};

// Namespace-scope, so it is constructed during static initialisation.
const RedirectConfigFile redirectConfigFile;

}  // namespace
