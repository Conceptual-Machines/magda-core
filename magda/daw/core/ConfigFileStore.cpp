#include "ConfigFileStore.hpp"

namespace magda::ConfigFileStore {

bool write(const juce::File& file, const juce::String& json) {
    // createDirectory() is a no-op that reports success when the directory is
    // already there.
    if (!file.getParentDirectory().createDirectory().wasOk())
        return false;

    // Write somewhere else and rename over the target. juce::TemporaryFile puts
    // the temporary beside the target so the rename stays within one volume,
    // which is what makes it atomic.
    juce::TemporaryFile temp(file);
    if (!temp.getFile().replaceWithText(json))
        return false;

    return temp.overwriteTargetFileWithTemporary();
}

}  // namespace magda::ConfigFileStore
