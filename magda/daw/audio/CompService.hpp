#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

#include "core/ClipTypes.hpp"

namespace magda {

/**
 * @brief Native comping for loop-record takes.
 *
 * Tracktion's own comp manager can't be used here: it resolves takes only via
 * ProjectItemID, and our recorded takes are direct file references. So MAGDA
 * assembles the comp itself - it edits the per-clip comp section list, renders
 * a stitched composite WAV from the take files (equal-power crossfades at the
 * section boundaries) on a background thread, and points the clip's source at
 * the render. The comp section list lives on the clip (AudioClipModel::comp)
 * and is persisted; the render is regenerated on demand.
 */
class CompService {
  public:
    static CompService& getInstance();

    /**
     * @brief Assign the comp region [startSeconds, endSeconds) to a take, then
     * re-render. Seeds a full-length base section from the active take on first
     * use. No-op for non-audio clips or clips with fewer than two takes.
     */
    void setSection(ClipId clipId, double startSeconds, double endSeconds, int takeIndex);

    /** Drop the comp and revert the clip to its active take. */
    void clearComp(ClipId clipId);

    /** Re-render the clip's existing comp (e.g. after project load). */
    void renderComp(ClipId clipId);

  private:
    CompService();
    ~CompService();

    std::unique_ptr<juce::ThreadPool> pool_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompService)
};

}  // namespace magda
