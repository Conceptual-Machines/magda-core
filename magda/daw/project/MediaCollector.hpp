#pragma once

// clang-format off
#include <tracktion_engine/tracktion_engine.h>
// clang-format on

#include <atomic>
#include <functional>
#include <vector>

#include "../core/ClipTypes.hpp"

namespace magda {

/**
 * @brief "Collect files" — consolidate all externally-referenced audio into the
 *        project media folder so the project is self-contained (#1407).
 *
 * Imported audio (clip sources, sampler samples, drum-pad samples) references its
 * original file in place by absolute path; nothing copies it into the project. If
 * the originals move or are deleted, every reference breaks. Collect copies each
 * distinct external user file once into `<project>_Media/imported/`, repoints every
 * reference to the copy, dedupes, and marks the project dirty.
 *
 * The flow is split so I/O can run off the message thread:
 *   1. scan()  -- message thread: walk every reference, build the unique external
 *                 file set with destination filenames pre-assigned.
 *   2. copy()  -- background thread: copy each source into imported/ (cancellable,
 *                 reports per-file progress); marks each item copied/failed.
 *   3. apply() -- message thread: repoint references to the copies, invalidate
 *                 thumbnails, reload samplers, markDirty.
 */
class MediaCollector {
  public:
    // One distinct external source file and every reference that points at it.
    // References are deduped by source path: one copy serves all of them.
    struct Item {
        juce::File source;
        juce::File dest;  // assigned in scan(), created in copy()
        std::vector<ClipId> clipRefs;
        // Hold Plugin::Ptr (not raw) so the sampler stays alive between scan and
        // apply even if the edit is mutated in between.
        std::vector<tracktion::Plugin::Ptr> samplerRefs;
        bool copied = false;
    };

    struct Plan {
        std::vector<Item> items;
        int missingCount = 0;       // referenced files that no longer exist on disk
        int alreadyLocalCount = 0;  // references already under the media dir
        int factoryCount = 0;       // references to bundled/factory samples

        bool hasWork() const {
            return !items.empty();
        }
    };

    struct Summary {
        int collected = 0;  // files successfully copied + repointed
        int failed = 0;     // copy failures (reference left as-is)
        int missing = 0;
        int alreadyLocal = 0;
        int factory = 0;

        juce::String toMessage() const;
    };

    // Message thread. Walks clips + samplers + drum pads, returns the plan.
    static Plan scan();

    // Background thread. Copies each item's source into its dest, setting
    // item.copied. Calls onProgress(0..1) and stops early if cancel becomes true.
    static void copy(Plan& plan, const std::function<void(float)>& onProgress,
                     std::atomic<bool>& cancel);

    // Message thread. Repoints references of every copied item, invalidates
    // thumbnails, reloads samplers, marks the project dirty. Returns a summary.
    static Summary apply(const Plan& plan);

  private:
    // True iff `file` ships with the app (under a bundled resources root). Such
    // samples are left in place -- they travel with the install, not the project.
    static bool isFactoryFile(const juce::File& file);
};

}  // namespace magda
