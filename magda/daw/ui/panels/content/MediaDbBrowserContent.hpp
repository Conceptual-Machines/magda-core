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

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "../../../media_db/MediaDatabase.hpp"
#include "../../../media_db/MediaDbIndexer.hpp"
#include "../../../media_db/MediaDbQuery.hpp"
#include "../../components/common/SvgButton.hpp"

namespace magda::daw::ui {

class MediaDbBrowserContent : public juce::Component, private juce::Timer {
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

    // Kick off a background scan of `dir` and update the status label as
    // it progresses. Called from the file-browser's folder-right-click
    // menu. Mode controls how existing rows are treated — see
    // MediaDbIndexer::Mode for the three semantics (Incremental, OnlyNew,
    // ForceAll).
    void startIndexing(const juce::File& dir, magda::media::MediaDbIndexer::Mode mode =
                                                  magda::media::MediaDbIndexer::Mode::Incremental);
    void requestStopIndexing();

    // External kind selector hook. Pass "audio" / "clip" / "preset", or
    // nullopt to clear the filter. Re-runs the search.
    void setKindFilter(std::optional<std::string> kind);

    // Fired when the user clicks a result row OR selects a different row
    // via the keyboard. The parent uses this to drive audition / autoplay,
    // so keyboard nav (UP/DOWN/PgUp/PgDn/Home/End) needs to fire it too —
    // not only mouse clicks. See ResultsTable::keyPressed.
    std::function<void(const juce::File&)> onFileSelected;

    // Keyboard transport hooks for the results list (issue #1339):
    //   LEFT  arrow → onPreviewStopRequest  (stop the current preview)
    //   RIGHT arrow → onPreviewReplayRequest (restart preview from 0)
    // Parent (MediaExplorerContent) owns the audio transport, so these
    // bubble out instead of being handled here.
    std::function<void()> onPreviewStopRequest;
    std::function<void()> onPreviewReplayRequest;

    // Indexing-progress callbacks. Empty string in onIndexingStatus means
    // "no indexing in progress" — the parent component uses these to
    // surface scan status in shared UI (e.g. the preview area) so it's
    // visible regardless of which browser mode the user is on.
    std::function<void(const juce::String&)> onIndexingStatus;
    std::function<void(bool)> onIndexingActiveChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

  private:
    class ResultsTableModel;
    class PopOutWindow;

    // TableListBox subclass that remembers the selection before JUCE's
    // RowComp collapses it. Pill-click bulk semantics need that snapshot
    // because cellClicked fires after selection handling.
    class ResultsTable : public juce::TableListBox {
      public:
        void mouseDown(const juce::MouseEvent& e) override {
            preClickSelection_ = getSelectedRows();
            juce::TableListBox::mouseDown(e);
        }

        void selectedRowsChanged(int row) override {
            preClickSelection_ = lastKnownSelection_;
            lastKnownSelection_ = getSelectedRows();
            juce::TableListBox::selectedRowsChanged(row);
        }

        // Keyboard handling for issue #1339 — make UP/DOWN navigation
        // audition the highlighted row (the model's cellClicked path only
        // fires on mouse) and bind LEFT/RIGHT to stop / replay-from-zero.
        // We intercept *after* the base class moves the selection so we
        // can read the post-move row, and only fire when the row actually
        // changed (avoids re-trigger when arrowing past the list edge).
        bool keyPressed(const juce::KeyPress& key) override;

        void syncSelectionSnapshot() {
            lastKnownSelection_ = getSelectedRows();
            preClickSelection_ = lastKnownSelection_;
        }

        juce::SparseSet<int> preClickSelection_;

        // Wired from MediaDbBrowserContent ctor. Fired with the new row
        // index after a keyboard nav key changes the selection.
        std::function<void(int row)> onKeyboardRowSelected;
        // Wired from MediaDbBrowserContent ctor; forwarded to the parent
        // browser's transport via the public onPreviewStop/Replay hooks.
        std::function<void()> onStopRequest;
        std::function<void()> onReplayRequest;

      private:
        juce::SparseSet<int> lastKnownSelection_;
    };

    void runSearch();
    // New-query entry points: resets pagination to the initial page and
    // clears similar-sounds mode before calling runSearch().
    void restartSearch();
    // Enter similar-sounds mode: re-run the query as a cosine search
    // around the given file's audio embedding instead of text / FTS.
    // Cleared by the next restartSearch().
    void findSimilarTo(std::int64_t seedFileId, const juce::String& seedName);
    void startAnalyzingFileIds(std::vector<std::int64_t> fileIds);
    void showEditRowDialog(std::int64_t fileId);
    void showBulkEditRowsDialog(std::vector<std::int64_t> fileIds);
    void resetRowsToDetected(std::vector<std::int64_t> fileIds);
    void saveMatchingClipValuesToLibrary(std::int64_t fileId);
    void recoverMissingFile(std::int64_t fileId);
    // When `fileIds` has more than one entry the family/shape choice is
    // applied to all of them; the `current*` argument is the clicked row's
    // value and only drives the tick mark next to that value in the menu.
    void showFamilyMenuForRow(std::vector<std::int64_t> fileIds, const juce::String& currentFamily,
                              juce::Point<int> screenPosition);
    void showShapeMenuForRow(std::vector<std::int64_t> fileIds, const juce::String& currentShape,
                             juce::Point<int> screenPosition);
    void deleteFileIdsWithConfirmation(std::vector<std::int64_t> fileIds);
    void removeDuplicateFilePathsWithConfirmation();
    void startIndexingWithOptions(const juce::File& dir, magda::media::MediaDbIndexer::Mode mode,
                                  magda::media::MediaDbIndexer::ScanTagOptions tagOptions);
    // Body of the indexing flow. Split out from startIndexingWithOptions so
    // the "Sample Analyzer not installed" warning can gate it on the user's
    // choice (Continue / Cancel) via an async alert callback.
    void runIndexing(const juce::File& dir, magda::media::MediaDbIndexer::Mode mode,
                     magda::media::MediaDbIndexer::ScanTagOptions tagOptions);
    void openPopOutWindow();
    magda::media::QueryFilters currentFilters() const;
    magda::media::QuerySort currentSort() const;
    void setSortOrder(int columnId, bool ascending);
    void timerCallback() override;

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
    ResultsTable resultsTable_;  // resizable, reorderable column header
    juce::Label emptyState_;
    juce::Label statusLabel_;  // similar-mode status only; indexing status lives in parent strip
    std::unique_ptr<juce::ArrowButton> prevPageBtn_;
    std::unique_ptr<juce::ArrowButton> nextPageBtn_;
    juce::Label pageLabel_;  // "Page N"

    // State
    juce::String queryText_;
    std::vector<magda::media::QueryResult> results_;
    bool isPopOutInstance_ = false;
    std::uint64_t observedMediaRevision_ = 0;
    magda::media::QuerySort sort_;

    // Pagination. Fixed page size; currentPage_ is 0-based. Prev / Next
    // buttons in the footer step the page index and re-run the same query
    // with a different OFFSET. Reset to 0 whenever the query / filters
    // change (restartSearch).
    static constexpr int kPageSize = 100;
    int currentPage_ = 0;

    // Similar-sounds mode. When set, runSearch dispatches to
    // MediaDbQuery::similarTo using the seed file's audio embedding
    // instead of text/FTS. Any new query/filter clears this via
    // restartSearch so the user goes back to normal browse.
    std::optional<std::int64_t> similarToFileId_;
    juce::String similarToFileName_;  // displayed in the status strip

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
    std::shared_ptr<std::atomic_bool> indexCancel_;

    // Single-thread pool for text-search queries. Text search triggers the
    // ONNX text encoder which costs multi-second on first load + a few
    // hundred ms per query — putting it on a worker keeps the UI fluid.
    // Filter-only queries stay synchronous (cheap SQL). searchGeneration_
    // drops stale results: an outdated worker callback finds its
    // generation has been bumped by a newer query and bails before
    // touching the table.
    std::unique_ptr<juce::ThreadPool> searchPool_;
    std::atomic<int> searchGeneration_{0};
    // Set true on teardown so queued / not-yet-started search jobs bail
    // immediately instead of making the destructor's drain wait on them.
    // shared_ptr (not a bare member) so a captured copy outlives the
    // component. The in-flight ONNX text-encoder load is uninterruptible, so
    // the destructor still drains unbounded — this just keeps the common case
    // (queued query behind a running one) from blocking the close.
    std::shared_ptr<std::atomic_bool> searchCancel_ = std::make_shared<std::atomic_bool>(false);

    // Worker-thread-owned SQLite connection. Built lazily on the first
    // search and reused across queries — opening a fresh connection per
    // query costs a sqlite3_open + schema check. Touched only by the
    // single search-pool worker; the dtor drains the pool before the
    // unique_ptr is destroyed so there's no race.
    std::unique_ptr<magda::media::MediaDatabase> searchDb_;
    void applySearchResultsToUi();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MediaDbBrowserContent)
};

}  // namespace magda::daw::ui
