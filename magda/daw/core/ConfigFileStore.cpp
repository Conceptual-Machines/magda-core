#include "ConfigFileStore.hpp"

namespace magda::ConfigFileStore {

namespace {

/// Move an unreadable file out of the way so the next save cannot overwrite
/// it. Returns the new location, or an invalid File if it could not be moved.
juce::File quarantine(const juce::File& file, const juce::String& stamp) {
    auto target = file.getSiblingFile(file.getFileName() + ".corrupt-" + stamp);
    // Never clobber an earlier quarantine from the same stamp.
    target = target.getNonexistentSibling();
    return file.moveFileTo(target) ? target : juce::File();
}

}  // namespace

ReadResult read(const juce::File& file, const juce::String& stamp) {
    ReadResult out;

    if (!file.existsAsFile()) {
        out.status = ReadStatus::NoFile;
        out.message = "no settings file at " + file.getFullPathName() + ", starting from defaults";
        return out;
    }

    const auto text = file.loadFileAsString();
    juce::var parsed;
    const auto result = juce::JSON::parse(text, parsed);

    juce::String problem;
    if (result.failed())
        problem = result.getErrorMessage().trim();
    else if (parsed.getDynamicObject() == nullptr)
        problem = "unexpected JSON root type";

    if (problem.isEmpty()) {
        out.status = ReadStatus::Loaded;
        out.parsed = std::move(parsed);
        out.message = "loaded " + file.getFullPathName();
        return out;
    }

    // The file holds settings this build could not read. Preserve it: moving it
    // aside keeps the contents recoverable AND leaves a clean slate, so the app
    // can still save. Only if the move fails do we have to refuse to write.
    const auto moved = quarantine(file, stamp);
    if (moved != juce::File()) {
        out.status = ReadStatus::Quarantined;
        out.quarantinedAs = moved;
        out.message = "could not read " + file.getFullPathName() + " (" + problem +
                      "); moved it to " + moved.getFileName() + " and started from defaults";
    } else {
        out.status = ReadStatus::Unreadable;
        out.message = "could not read " + file.getFullPathName() + " (" + problem +
                      ") and could not move it aside; settings will NOT be saved over it";
    }
    return out;
}

bool mayWrite(ReadStatus status) {
    // Everything except Unreadable: there, the file on disk still holds
    // settings that were never loaded, so writing would destroy them.
    return status != ReadStatus::Unreadable;
}

bool write(const juce::File& file, const juce::String& json) {
    // createDirectory() is a no-op that reports success when the directory is
    // already there.
    if (!file.getParentDirectory().createDirectory().wasOk())
        return false;

    // Keep the previous contents as the last known good copy before replacing
    // them.
    if (file.existsAsFile()) {
        auto backup = file.getSiblingFile(file.getFileName() + ".bak");
        backup.deleteFile();
        file.copyFileTo(backup);
    }

    // Write somewhere else and rename over the target, so a crash or a yanked
    // volume mid-write leaves the existing file intact rather than truncated.
    juce::TemporaryFile temp(file);
    if (!temp.getFile().replaceWithText(json))
        return false;

    return temp.overwriteTargetFileWithTemporary();
}

}  // namespace magda::ConfigFileStore
