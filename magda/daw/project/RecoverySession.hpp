#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <vector>

namespace magda {

class RecoverySessionLock;

struct RecoveryEntry {
    juce::File directory;
    juce::File snapshot;
    juce::String build;
    juce::String version;
    juce::String name;
    juce::String originalFile;
    juce::String mediaDirectory;
    juce::Time created;
    juce::Time edited;
    juce::Time saved;
    int tracks = 0;
    int clips = 0;
    bool running = false;
    bool offered = false;
};

// A process owns one locked session directory. Only its immediately preceding
// session can trigger a launch offer; all other snapshots remain browsable.
class RecoverySession {
  public:
    RecoverySession(const juce::File& root, juce::String build);
    ~RecoverySession();

    const juce::File& root() const {
        return root_;
    }
    juce::File snapshot() const;
    RecoveryEntry startupCandidate() const;
    RecoveryEntry projectCandidate(const juce::File& project, bool includeOffered = false) const;
    std::vector<RecoveryEntry> entries(bool includeLive = false) const;

    bool write(RecoveryEntry entry, const std::function<bool(const juce::File&)>& writer);
    bool adopt(const RecoveryEntry& entry);
    bool markOffered(const RecoveryEntry& entry);
    bool discard(const RecoveryEntry& entry);
    void clear();
    void finish(const juce::File& copiedRoot = {});
    void pruneEmptySessions(juce::Time cutoff);

  private:
    static RecoveryEntry read(const juce::File& directory);
    static bool save(const RecoveryEntry& entry);
    bool belongsToStore(const RecoveryEntry& entry) const;

    juce::File root_;
    juce::File directory_;
    juce::File previous_;
    juce::String build_;
    std::unique_ptr<RecoverySessionLock> lock_;
    bool ready_ = false;
};

}  // namespace magda
