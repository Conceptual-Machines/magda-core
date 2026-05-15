#include "MediaDbBrowserContent.hpp"

#include <filesystem>

#include "../../../media_db/MediaDatabase.hpp"
#include "../../../media_db/MediaDbContext.hpp"
#include "../../../media_db/MediaDbIndexer.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FileBrowserLookAndFeel.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {

juce::String prettyDuration(std::optional<double> seconds) {
    if (!seconds) {
        return "-";
    }
    const double s = *seconds;
    if (s < 1.0) {
        return juce::String(s, 2) + "s";
    }
    return juce::String(s, 1) + "s";
}

juce::String prettyBpm(std::optional<double> bpm) {
    if (!bpm) {
        return "-";
    }
    return juce::String(static_cast<int>(std::round(*bpm)));
}

juce::String prettyKey(const std::optional<std::string>& root,
                       const std::optional<std::string>& scale) {
    if (!root) {
        return "-";
    }
    juce::String out(*root);
    if (scale && !scale->empty()) {
        out += " " + juce::String(*scale).substring(0, 3);  // "maj" / "min"
    }
    return out;
}

}  // namespace

// ===========================================================================
// ResultsListModel — paints one DB result row
// ===========================================================================

class MediaDbBrowserContent::ResultsListModel : public juce::ListBoxModel {
  public:
    explicit ResultsListModel(MediaDbBrowserContent& owner) : owner_(owner) {}

    int getNumRows() override {
        return static_cast<int>(owner_.results_.size());
    }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height,
                          bool rowIsSelected) override {
        if (row < 0 || row >= static_cast<int>(owner_.results_.size())) {
            return;
        }
        const auto& r = owner_.results_[static_cast<size_t>(row)];

        if (rowIsSelected) {
            g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE_HOVER));
        }
        // Subtle row separator
        g.setColour(DarkTheme::getBorderColour().withAlpha(0.4F));
        g.fillRect(8, height - 1, width - 16, 1);

        auto bounds = juce::Rectangle<int>(8, 0, width - 16, height);

        // Right-side meta strip: bpm | key | duration. ASCII separator
        // because JUCE's TTF rendering eats non-Latin1 codepoints unless we
        // route the strings through CharPointer_UTF8::CharPointer_UTF8 ctors,
        // and "|" reads fine at 11pt.
        const auto& font = FontManager::getInstance().getUIFont(11.0F);
        g.setFont(font);
        const juce::String meta = prettyBpm(r.bpm) + " bpm | " + prettyKey(r.keyRoot, r.keyScale) +
                                  " | " + prettyDuration(r.durationS);
        const int metaW = font.getStringWidth(meta) + 8;
        auto metaArea = bounds.removeFromRight(metaW);
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        g.drawText(meta, metaArea, juce::Justification::centredRight, true);

        // Family + shape pills next to the right of the filename
        auto pillArea = bounds.removeFromRight(120);
        auto drawPill = [&](juce::Rectangle<int> r, const juce::String& text, juce::Colour col) {
            g.setColour(col.withAlpha(0.15F));
            g.fillRoundedRectangle(r.toFloat(), 3.0F);
            g.setColour(col);
            g.drawRoundedRectangle(r.toFloat(), 3.0F, 1.0F);
            g.drawText(text, r, juce::Justification::centred, true);
        };
        auto pillSlot = [&](juce::Rectangle<int> area, int w) {
            area = area.reduced(2, 4);
            area = area.removeFromLeft(w);
            return area;
        };
        if (!r.shape.empty()) {
            drawPill(pillSlot(pillArea.removeFromRight(60), 56), juce::String(r.shape),
                     DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        }
        if (!r.family.empty()) {
            drawPill(pillSlot(pillArea.removeFromRight(60), 56), juce::String(r.family),
                     DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        }

        // Filename (truncated middle)
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        g.drawText(juce::String(r.path.filename().string()), bounds.reduced(0, 2),
                   juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent&) override {
        if (row < 0 || row >= static_cast<int>(owner_.results_.size())) {
            return;
        }
        const auto& r = owner_.results_[static_cast<size_t>(row)];
        if (owner_.onFileSelected) {
            owner_.onFileSelected(juce::File(juce::String(r.path.string())));
        }
    }

  private:
    MediaDbBrowserContent& owner_;
};

// ===========================================================================
// MediaDbBrowserContent
// ===========================================================================

MediaDbBrowserContent::MediaDbBrowserContent() {
    // Style mirrors PluginBrowserContent's compact filter strip:
    // SmallComboBoxLookAndFeel on dropdowns + explicit theme colours on
    // each control. Labels and the index button share FileBrowserLookAndFeel
    // because it carries the matching theme-font overrides.
    auto& comboLnf = SmallComboBoxLookAndFeel::getInstance();
    auto& fbLnf = FileBrowserLookAndFeel::getInstance();
    const auto uiFont = FontManager::getInstance().getUIFont(11.0F);

    auto styleLabel = [&](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setFont(uiFont);
        l.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        l.setJustificationType(juce::Justification::centredRight);
    };
    styleLabel(familyLabel_, "family");
    styleLabel(shapeLabel_, "shape");
    styleLabel(bpmLabel_, "bpm");

    auto styleCombo = [&](juce::ComboBox& cb) {
        cb.setColour(juce::ComboBox::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        cb.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
        cb.setColour(juce::ComboBox::outlineColourId, DarkTheme::getBorderColour());
        cb.setLookAndFeel(&comboLnf);
    };

    familyFilter_.addItem("all", 1);
    for (const auto* f :
         {"drum", "bass", "lead", "pad", "keys", "guitar", "orchestral", "vocal", "fx"}) {
        familyFilter_.addItem(f, familyFilter_.getNumItems() + 1);
    }
    familyFilter_.setSelectedId(1, juce::dontSendNotification);
    styleCombo(familyFilter_);
    familyFilter_.onChange = [this]() { runSearch(); };

    shapeFilter_.addItem("any", 1);
    for (const auto* s : {"one-shot", "loop", "sustained"}) {
        shapeFilter_.addItem(s, shapeFilter_.getNumItems() + 1);
    }
    shapeFilter_.setSelectedId(1, juce::dontSendNotification);
    styleCombo(shapeFilter_);
    shapeFilter_.onChange = [this]() { runSearch(); };

    auto setupBpm = [&](juce::TextEditor& e, const juce::String& placeholder) {
        e.setTextToShowWhenEmpty(placeholder, DarkTheme::getSecondaryTextColour());
        e.setColour(juce::TextEditor::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        e.setColour(juce::TextEditor::textColourId, DarkTheme::getTextColour());
        e.setColour(juce::TextEditor::outlineColourId, DarkTheme::getBorderColour());
        e.setInputRestrictions(4, "0123456789.");
        e.setFont(uiFont);
        e.onReturnKey = [this]() { runSearch(); };
        e.onFocusLost = [this]() { runSearch(); };
    };
    setupBpm(bpmMinBox_, "min");
    setupBpm(bpmMaxBox_, "max");

    tonalOnly_.setLookAndFeel(&fbLnf);
    tonalOnly_.onClick = [this]() { runSearch(); };

    indexButton_.setLookAndFeel(&fbLnf);
    indexButton_.setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    indexButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    indexButton_.onClick = [this]() { onIndexFolderClicked(); };

    addAndMakeVisible(familyLabel_);
    addAndMakeVisible(familyFilter_);
    addAndMakeVisible(shapeLabel_);
    addAndMakeVisible(shapeFilter_);
    addAndMakeVisible(bpmLabel_);
    addAndMakeVisible(bpmMinBox_);
    addAndMakeVisible(bpmMaxBox_);
    addAndMakeVisible(tonalOnly_);
    addAndMakeVisible(indexButton_);

    // Results list
    resultsModel_ = std::make_unique<ResultsListModel>(*this);
    resultsList_.setModel(resultsModel_.get());
    resultsList_.setRowHeight(28);
    resultsList_.setColour(juce::ListBox::backgroundColourId,
                           DarkTheme::getColour(DarkTheme::BACKGROUND));
    resultsList_.setColour(juce::ListBox::outlineColourId, DarkTheme::getBorderColour());
    resultsList_.setOutlineThickness(1);
    addAndMakeVisible(resultsList_);

    // Empty state
    emptyState_.setText("No samples indexed yet.\nClick \"Index folder...\" to add one.",
                        juce::dontSendNotification);
    emptyState_.setFont(FontManager::getInstance().getUIFont(13.0F));
    emptyState_.setJustificationType(juce::Justification::centred);
    emptyState_.setColour(juce::Label::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    emptyState_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(emptyState_);

    // Indexing status. Hidden until a scan starts.
    statusLabel_.setFont(FontManager::getInstance().getUIFont(10.0F));
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setColour(juce::Label::textColourId,
                           DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    statusLabel_.setMinimumHorizontalScale(1.0F);  // truncate long paths, don't shrink the font
    statusLabel_.setInterceptsMouseClicks(false, false);
    statusLabel_.setVisible(false);
    addAndMakeVisible(statusLabel_);

    // Defer the first query until the library icon is actually clicked.
    // Opening SQLite + loading CLAP models can take seconds; doing it during
    // app startup would freeze the splash. setQueryText()/refresh() trigger
    // runSearch() lazily once the user enters library mode.
}

MediaDbBrowserContent::~MediaDbBrowserContent() {
    // Drain in-flight indexing jobs. removeAllJobs(true, ...) signals
    // cancellation and waits up to the timeout for the worker to exit.
    if (indexPool_) {
        indexPool_->removeAllJobs(true, 5000);
    }
    resultsList_.setModel(nullptr);
    familyFilter_.setLookAndFeel(nullptr);
    shapeFilter_.setLookAndFeel(nullptr);
    bpmMinBox_.setLookAndFeel(nullptr);
    bpmMaxBox_.setLookAndFeel(nullptr);
    tonalOnly_.setLookAndFeel(nullptr);
    indexButton_.setLookAndFeel(nullptr);
}

void MediaDbBrowserContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE));
}

void MediaDbBrowserContent::resized() {
    auto bounds = getLocalBounds();

    // Filter strip across the top.
    auto strip = bounds.removeFromTop(28);
    strip.removeFromLeft(4);
    strip.removeFromRight(4);
    indexButton_.setBounds(strip.removeFromRight(110).reduced(2));
    strip.removeFromRight(6);
    tonalOnly_.setBounds(strip.removeFromRight(70).reduced(2));
    strip.removeFromRight(6);
    bpmMaxBox_.setBounds(strip.removeFromRight(46).reduced(2));
    bpmMinBox_.setBounds(strip.removeFromRight(46).reduced(2));
    bpmLabel_.setBounds(strip.removeFromRight(28));
    strip.removeFromRight(6);
    shapeFilter_.setBounds(strip.removeFromRight(90).reduced(2));
    shapeLabel_.setBounds(strip.removeFromRight(40));
    strip.removeFromRight(6);
    familyFilter_.setBounds(strip.removeFromRight(100).reduced(2));
    familyLabel_.setBounds(strip.removeFromRight(44));

    bounds.removeFromTop(4);
    // Status strip lives at the bottom of the content area when indexing.
    // It shares the area with the results list, but on a separate row.
    if (statusLabel_.isVisible()) {
        statusLabel_.setBounds(bounds.removeFromBottom(18).reduced(8, 2));
    } else {
        statusLabel_.setBounds(bounds.getX() + 8, bounds.getBottom() - 18, bounds.getWidth() - 16,
                               18);
    }
    resultsList_.setBounds(bounds);
    emptyState_.setBounds(bounds);
}

void MediaDbBrowserContent::setQueryText(const juce::String& text) {
    if (queryText_ == text) {
        return;
    }
    queryText_ = text;
    runSearch();
}

void MediaDbBrowserContent::refresh() {
    runSearch();
}

magda::media::QueryFilters MediaDbBrowserContent::currentFilters() const {
    magda::media::QueryFilters f;
    if (familyFilter_.getSelectedId() > 1) {
        f.family = familyFilter_.getText().toStdString();
    }
    if (shapeFilter_.getSelectedId() > 1) {
        f.shape = shapeFilter_.getText().toStdString();
    }
    if (bpmMinBox_.getText().isNotEmpty()) {
        f.bpmMin = bpmMinBox_.getText().getDoubleValue();
    }
    if (bpmMaxBox_.getText().isNotEmpty()) {
        f.bpmMax = bpmMaxBox_.getText().getDoubleValue();
    }
    if (tonalOnly_.getToggleState()) {
        f.tonal = true;
    }
    return f;
}

void MediaDbBrowserContent::runSearch() {
    auto& ctx = magda::media::MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        results_.clear();
        resultsList_.updateContent();
        emptyState_.setText("Failed to open the media database.", juce::dontSendNotification);
        emptyState_.setVisible(true);
        resultsList_.setVisible(false);
        return;
    }

    magda::media::MediaDbQuery query(ctx.db(), ctx.textEncoder(), ctx.tokenizer());
    const auto filters = currentFilters();
    const std::optional<std::string> text =
        queryText_.isEmpty() ? std::nullopt : std::optional<std::string>{queryText_.toStdString()};
    results_ = query.search(text, filters, /*limit=*/200);

    resultsList_.updateContent();
    const bool empty = results_.empty();
    resultsList_.setVisible(!empty);
    emptyState_.setVisible(empty);
    if (empty) {
        emptyState_.setText(queryText_.isEmpty()
                                ? "No samples indexed yet.\nClick \"Index folder...\" to add one."
                                : "No results.",
                            juce::dontSendNotification);
    }
}

void MediaDbBrowserContent::onIndexFolderClicked() {
    if (indexing_) {
        return;  // Already running — ignore double clicks.
    }

    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Choose a folder to index", juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    auto flags =
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    fileChooser_->launchAsync(flags, [this](const juce::FileChooser& fc) {
        const auto dir = fc.getResult();
        if (!dir.isDirectory()) {
            return;
        }

        if (!indexPool_) {
            indexPool_ = std::make_unique<juce::ThreadPool>(1);
        }

        indexing_ = true;
        indexButton_.setEnabled(false);
        indexButton_.setButtonText("Indexing...");
        statusLabel_.setText("Preparing scan...", juce::dontSendNotification);
        statusLabel_.setVisible(true);
        resized();

        const auto path = std::filesystem::path(dir.getFullPathName().toStdString());
        const juce::Component::SafePointer<MediaDbBrowserContent> self(this);

        indexPool_->addJob([self, path]() {
            // Background thread. Open a fresh DB connection here so we don't
            // share the UI-thread MediaDbContext::db() handle across threads
            // — SQLite multi-thread mode requires one connection per thread.
            // WAL mode (set in schema) lets this writer coexist with the
            // UI's reader connection.
            try {
                magda::media::MediaDatabase bgDb(
                    magda::media::MediaDbContext::getInstance().dbPath());
                magda::media::MediaDbIndexer indexer(
                    bgDb, magda::media::MediaDbContext::getInstance().audioEncoder());
                indexer.setProgress(
                    [self](int done, int total, const std::filesystem::path& current) {
                        // Fired per-file on the indexer thread; marshal the
                        // text update to the UI thread via callAsync.
                        const auto status = juce::String("Indexing ") +
                                            juce::String(current.filename().string()) + " (" +
                                            juce::String(done) + "/" + juce::String(total) + ")";
                        juce::MessageManager::callAsync([self, status]() {
                            if (self != nullptr) {
                                self->statusLabel_.setText(status, juce::dontSendNotification);
                            }
                        });
                    });
                indexer.indexDirectory(path);
            } catch (const std::exception&) {
                // Swallow — bounce back to the UI either way.
            }

            juce::MessageManager::callAsync([self]() {
                if (self == nullptr) {
                    return;
                }
                self->indexing_ = false;
                self->indexButton_.setEnabled(true);
                self->indexButton_.setButtonText("Index folder...");
                self->statusLabel_.setVisible(false);
                self->resized();
                self->runSearch();
            });
        });
    });
}

}  // namespace magda::daw::ui
