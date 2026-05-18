// Media database browser content (issue #768 — Phase F2).
//
// Replaces the F1 placeholder when MediaExplorerContent's library icon is
// active. Composes:
//   - filter strip: family + shape + BPM range + tonal toggle + Index button
//   - results list: rows from MediaDbQuery (path + family/shape pills +
//     BPM + key + duration)
//   - empty state: a hint to index a folder when there are no rows yet
//
// Parent component (MediaExplorerContent) drives the search by calling
// setQueryText() whenever the shared search box changes. Selection fires
// onFileSelected so the parent's audio preview path can stay in one place.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <vector>

#include "../../../media_db/MediaDbQuery.hpp"
#include "../../components/common/SvgButton.hpp"

namespace magda::daw::ui {

class MediaDbBrowserContent : public juce::Component {
  public:
    // isPopOutInstance: true when this is the content of a detached pop-out
    // window (suppresses its own pop-out button and skips the empty-state hint
    // about indexing — the docked instance owns that path).
    explicit MediaDbBrowserContent(bool isPopOutInstance = false);
    ~MediaDbBrowserContent() override;

    // Push the current search box text. Triggers a query refresh.
    void setQueryText(const juce::String& text);

    // Re-run the current query. Useful after indexing finishes or filters change.
    void refresh();

    // Kick off a background scan of `dir` and update the status label as it
    // progresses. Called from the file-browser's folder-right-click menu — the
    // DB browser no longer has its own "Index folder" button.
    //
    // force: when true, the indexer re-derives every file's metadata
    // ignoring the skip-on-unchanged fast path. Used by the "Re-index"
    // menu item to refresh tags / family / features on already-indexed
    // content after a rules / algorithm change.
    void startIndexing(const juce::File& dir, bool force = false);

    // External kind selector hook. Pass "audio" / "clip" / "preset", or
    // nullopt to clear the filter. Re-runs the search.
    void setKindFilter(std::optional<std::string> kind);

    // Fired when the user clicks a result row.
    std::function<void(const juce::File&)> onFileSelected;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

  private:
    class ResultsTableModel;
    class PopOutWindow;

    void runSearch();
    void openPopOutWindow();
    magda::media::QueryFilters currentFilters() const;

    // Filter strip — two rows.
    // Row 1: family / shape / key dropdowns
    // Row 2: BPM range + tonal toggle + pop-out
    juce::ComboBox familyFilter_;
    juce::ComboBox shapeFilter_;
    juce::ComboBox keyFilter_;
    juce::Label bpmLabel_;
    juce::TextEditor bpmMinBox_;
    juce::TextEditor bpmMaxBox_;
    juce::ToggleButton tonalOnly_{"tonal"};
    juce::TextEditor tagsFilter_;  // free-text tag filter, AND-tokenised
    std::unique_ptr<magda::SvgButton> popOutButton_;

    // Externally-driven kind filter. The DB browser doesn't own a kind
    // selector of its own — MediaExplorerContent reuses the search-bar
    // file-type icons for that, calling setKindFilter() to push the choice
    // here. nullopt == "All kinds".
    std::optional<std::string> kindFilter_;

    // Results
    std::unique_ptr<ResultsTableModel> resultsModel_;
    juce::TableListBox resultsTable_;  // resizable, reorderable column header
    juce::Label emptyState_;
    juce::Label statusLabel_;  // "Indexing path/to/x.wav (N/M)" during a scan

    // State
    juce::String queryText_;
    std::vector<magda::media::QueryResult> results_;
    bool isPopOutInstance_ = false;

    juce::Component::SafePointer<PopOutWindow>
        popOutWindow_;  // tracked so re-clicks focus existing

    // Drag-in-flight gate: ListBox can call getDragSourceDescription
    // repeatedly during a single drag gesture as the drag-threshold is
    // probed. Without this flag we'd queue multiple
    // performExternalDragDropOfFiles invocations.
    bool dragInProgress_ = false;

    // Single-thread pool so indexing doesn't block the message thread. The
    // pool is created lazily on first index click so app startup pays nothing.
    std::unique_ptr<juce::ThreadPool> indexPool_;
    bool indexing_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MediaDbBrowserContent)
};

}  // namespace magda::daw::ui
