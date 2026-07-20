#pragma once

#include <juce_events/juce_events.h>

#include <functional>

namespace magda {

// Polls a single file's modification time and fires a callback when it
// changes. JUCE ships no native filesystem watcher, and theme edits are
// infrequent and user-driven, so a lightweight ~1s poll comfortably meets the
// "updates within ~1s" bar for #88 hot-reload without a background thread.
//
// Message-thread only: the timer callback runs there, which is exactly where
// palette swaps and repaints must happen.
class ThemeFileWatcher : private juce::Timer {
  public:
    explicit ThemeFileWatcher(std::function<void()> onChanged) : onChanged_(std::move(onChanged)) {}

    ~ThemeFileWatcher() override {
        stopTimer();
    }

    // Begins watching `file`. The current mtime is taken as the baseline, so
    // only edits made after this call fire the callback.
    void watch(const juce::File& file) {
        file_ = file;
        lastModified_ = file.getLastModificationTime();
        startTimer(kPollMs);
    }

    void stop() {
        stopTimer();
        file_ = juce::File();
    }

  private:
    static constexpr int kPollMs = 1000;

    void timerCallback() override {
        if (file_ == juce::File() || !file_.existsAsFile())
            return;

        const auto modified = file_.getLastModificationTime();
        if (modified != lastModified_) {
            lastModified_ = modified;
            if (onChanged_)
                onChanged_();
        }
    }

    std::function<void()> onChanged_;
    juce::File file_;
    juce::Time lastModified_;
};

}  // namespace magda
