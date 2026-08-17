#pragma once

#include <juce_core/juce_core.h>

namespace magda {

/**
 * @brief Reading and writing the settings file, separated from its contents.
 *
 * A settings file that fails to parse used to leave the in-memory Config at its
 * defaults with nothing recording the failure, so the next save serialised
 * those defaults straight over the user's file and every preference was lost
 * (issue #2104). The rules that prevent that are here rather than in Config so
 * they can be tested against a real file without touching the user's own.
 */
namespace ConfigFileStore {

/// What reading the settings file produced.
enum class ReadStatus {
    /// Parsed an existing file. Its contents are the user's settings.
    Loaded,
    /// Nothing on disk yet: a first run. Defaults ARE the correct state.
    NoFile,
    /// Unreadable, and moved aside. The contents are preserved under a new
    /// name, so writing a fresh file over the (now absent) original is safe.
    Quarantined,
    /// Unreadable and could NOT be moved aside. The file still holds settings
    /// this build failed to read, so it must not be overwritten.
    Unreadable,
};

struct ReadResult {
    ReadStatus status = ReadStatus::NoFile;
    /// The parsed root. Only meaningful when status == Loaded.
    juce::var parsed;
    /// One line describing the outcome, for the log.
    juce::String message;
    /// Where an unreadable file was moved. Set only when status == Quarantined.
    juce::File quarantinedAs;
};

/**
 * Read and parse the settings file.
 *
 * An unreadable file is moved aside to `<name>.corrupt-<stamp>` rather than
 * left in place to be overwritten by the next save, so its contents stay
 * recoverable. If that move fails the file is left exactly as it is and the
 * status says so.
 *
 * @param file  The settings file to read
 * @param stamp Suffix for the quarantine name; the caller supplies it so the
 *              result is deterministic under test
 */
ReadResult read(const juce::File& file, const juce::String& stamp);

/**
 * True when a save is allowed to write the settings file, given how the last
 * read went. False only for ReadStatus::Unreadable, where the file still holds
 * settings that were not loaded and writing would destroy them.
 */
bool mayWrite(ReadStatus status);

/**
 * Write the settings file, atomically, keeping the previous contents as
 * `<name>.bak`.
 *
 * The write goes to a temporary file in the same directory and is renamed over
 * the target, so an interrupted write cannot truncate the live file -- the
 * failure mode that produced the unreadable file in the first place.
 *
 * @return true if the file was written
 */
bool write(const juce::File& file, const juce::String& json);

}  // namespace ConfigFileStore

}  // namespace magda
