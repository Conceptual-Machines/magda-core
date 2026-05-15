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
#include <vector>

#include "../../../media_db/MediaDbQuery.hpp"

namespace magda::daw::ui {

class MediaDbBrowserContent : public juce::Component {
  public:
    MediaDbBrowserContent();
    ~MediaDbBrowserContent() override;

    // Push the current search box text. Triggers a query refresh.
    void setQueryText(const juce::String& text);

    // Re-run the current query. Useful after indexing finishes or filters change.
    void refresh();

    // Fired when the user clicks a result row.
    std::function<void(const juce::File&)> onFileSelected;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    class ResultsListModel;

    void runSearch();
    void onIndexFolderClicked();
    magda::media::QueryFilters currentFilters() const;

    // Filter strip
    juce::Label familyLabel_;
    juce::ComboBox familyFilter_;
    juce::Label shapeLabel_;
    juce::ComboBox shapeFilter_;
    juce::Label bpmLabel_;
    juce::TextEditor bpmMinBox_;
    juce::TextEditor bpmMaxBox_;
    juce::ToggleButton tonalOnly_{"tonal"};
    juce::TextButton indexButton_{"Index folder…"};

    // Results
    std::unique_ptr<ResultsListModel> resultsModel_;
    juce::ListBox resultsList_;
    juce::Label emptyState_;

    // State
    juce::String queryText_;
    std::vector<magda::media::QueryResult> results_;
    std::unique_ptr<juce::FileChooser> fileChooser_;  // persisted for async callback

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MediaDbBrowserContent)
};

}  // namespace magda::daw::ui
