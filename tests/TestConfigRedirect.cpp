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
        //
        // A UUID rather than getNonexistentChildFile(): that only checks whether
        // a candidate exists right now and does not reserve it, so two processes
        // starting together would both pick the same free name. Nothing writes
        // the file at this point, so there is no name to reserve -- uniqueness
        // has to come from the name itself.
        const auto path =
            dir.getChildFile("config-" + juce::Uuid().toDashedString() + ".json").getFullPathName();

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
