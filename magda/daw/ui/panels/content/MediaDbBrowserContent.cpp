#include "MediaDbBrowserContent.hpp"

#include <BinaryData.h>

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

// ---- Column IDs ---------------------------------------------------------
//
// TableHeaderComponent identifies columns by ID, not by index, so reorders
// don't change these. Defined as constants for the model's paintCell switch.

enum ColumnId {
    kColName = 1,
    kColFamily = 2,
    kColShape = 3,
    kColBpm = 4,
    kColKey = 5,
    kColDuration = 6,
};

// ---- Filter combo value tables -----------------------------------------
//
// Indexed by combo selectedId: 1 == "no filter" (sentinel), 2+ == the
// matching column value. Hard-coded so we don't depend on ComboBox's
// getItemText/getSelectedItemIndex timing inside onChange — which has
// produced "first selection doesn't take" behaviour in the past.

const std::vector<juce::String> kFamilies = {
    "",  // id=1 sentinel
    "drum", "bass", "lead", "pad", "keys", "guitar", "orchestral", "vocal", "fx",
};

const std::vector<juce::String> kShapes = {
    "",  // id=1 sentinel
    "one-shot",
    "loop",
    "sustained",
};

// Matches AudioFeatures.cpp::kPitchClasses (PathRules normalises flats → sharps).
const std::vector<juce::String> kKeys = {
    "",  // id=1 sentinel
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

std::optional<std::string> selectedString(const juce::ComboBox& cb,
                                          const std::vector<juce::String>& table) {
    const int id = cb.getSelectedId();
    if (id <= 1 || id - 1 >= static_cast<int>(table.size())) {
        return std::nullopt;
    }
    return table[static_cast<size_t>(id - 1)].toStdString();
}

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
// ResultsTableModel — paints one cell of the TableListBox per column
// ===========================================================================

class MediaDbBrowserContent::ResultsTableModel : public juce::TableListBoxModel {
  public:
    explicit ResultsTableModel(MediaDbBrowserContent& owner) : owner_(owner) {}

    int getNumRows() override {
        return static_cast<int>(owner_.results_.size());
    }

    void paintRowBackground(juce::Graphics& g, int /*rowNumber*/, int /*width*/, int /*height*/,
                            bool rowIsSelected) override {
        if (rowIsSelected) {
            g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE_HOVER));
        }
    }

    void paintCell(juce::Graphics& g, int row, int columnId, int width, int height,
                   bool /*rowIsSelected*/) override {
        if (row < 0 || row >= static_cast<int>(owner_.results_.size())) {
            return;
        }
        const auto& r = owner_.results_[static_cast<size_t>(row)];
        const auto cell = juce::Rectangle<int>(0, 0, width, height);

        const auto& font = FontManager::getInstance().getUIFont(11.0F);
        g.setFont(font);

        auto drawPill = [&](const juce::String& text, juce::Colour col) {
            if (text.isEmpty()) {
                return;
            }
            auto pill = cell.reduced(6, 4);
            g.setColour(col.withAlpha(0.15F));
            g.fillRoundedRectangle(pill.toFloat(), 3.0F);
            g.setColour(col);
            g.drawRoundedRectangle(pill.toFloat(), 3.0F, 1.0F);
            g.drawText(text, pill, juce::Justification::centred, true);
        };

        switch (columnId) {
            case kColName:
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
                g.drawText(juce::String(r.path.filename().string()), cell.reduced(6, 2),
                           juce::Justification::centredLeft, true);
                break;
            case kColFamily:
                drawPill(juce::String(r.family), DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
                break;
            case kColShape:
                drawPill(juce::String(r.shape), DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
                break;
            case kColBpm:
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                g.drawText(prettyBpm(r.bpm), cell.reduced(6, 2), juce::Justification::centredRight,
                           true);
                break;
            case kColKey:
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                g.drawText(prettyKey(r.keyRoot, r.keyScale), cell.reduced(6, 2),
                           juce::Justification::centredRight, true);
                break;
            case kColDuration:
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                g.drawText(prettyDuration(r.durationS), cell.reduced(6, 2),
                           juce::Justification::centredRight, true);
                break;
            default:
                break;
        }
    }

    void cellClicked(int row, int /*columnId*/, const juce::MouseEvent&) override {
        if (row < 0 || row >= static_cast<int>(owner_.results_.size())) {
            return;
        }
        const auto& r = owner_.results_[static_cast<size_t>(row)];
        if (owner_.onFileSelected) {
            owner_.onFileSelected(juce::File(juce::String(r.path.string())));
        }
    }

    // Kicks off an OS-level file drag for the selected rows and returns
    // juce::var() to tell ListBox NOT to also start its own internal drag.
    //
    // Why OS-level rather than JUCE-internal: the drop targets we care
    // about (TrackContentPanel, SessionView, Finder, plugin file slots)
    // are all juce::FileDragAndDropTarget, not juce::DragAndDropTarget —
    // they only respond to OS file drags. An internal drag would show
    // the snapshot but no target would accept the drop.
    //
    // Why deferred via callAsync: performExternalDragDropOfFiles enters
    // a modal native run loop and we're sitting inside ListBox's
    // mouseDrag callback right now. Bouncing through the message queue
    // gets us out of that nested call before the modal loop starts.
    juce::var getDragSourceDescription(const juce::SparseSet<int>& selectedRows) override {
        if (owner_.dragInProgress_ || selectedRows.isEmpty()) {
            return {};
        }
        juce::StringArray paths;
        for (int i = 0; i < selectedRows.size(); ++i) {
            const int row = selectedRows[i];
            if (row < 0 || row >= static_cast<int>(owner_.results_.size())) {
                continue;
            }
            paths.addIfNotAlreadyThere(
                juce::String(owner_.results_[static_cast<size_t>(row)].path.string()));
        }
        if (paths.isEmpty()) {
            return {};
        }
        owner_.dragInProgress_ = true;
        const juce::Component::SafePointer<MediaDbBrowserContent> src(&owner_);
        juce::MessageManager::callAsync([paths, src]() {
            if (src == nullptr) {
                return;
            }
            juce::DragAndDropContainer::performExternalDragDropOfFiles(paths,
                                                                       /*canMoveFiles=*/false, src);
            src->dragInProgress_ = false;
        });
        return {};  // suppress JUCE's internal drag — we handle it ourselves
    }

  private:
    MediaDbBrowserContent& owner_;
};

// ===========================================================================
// MediaDbBrowserContent
// ===========================================================================

MediaDbBrowserContent::MediaDbBrowserContent(bool isPopOutInstance)
    : isPopOutInstance_(isPopOutInstance) {
    // Style mirrors PluginBrowserContent's compact filter strip:
    // SmallComboBoxLookAndFeel on dropdowns + explicit theme colours on
    // each control. Labels and the index button share FileBrowserLookAndFeel
    // because it carries the matching theme-font overrides.
    auto& comboLnf = SmallComboBoxLookAndFeel::getInstance();
    auto& fbLnf = FileBrowserLookAndFeel::getInstance();
    const auto uiFont = FontManager::getInstance().getUIFont(11.0F);

    bpmLabel_.setText("bpm", juce::dontSendNotification);
    bpmLabel_.setFont(uiFont);
    bpmLabel_.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    bpmLabel_.setJustificationType(juce::Justification::centredRight);

    auto styleCombo = [&](juce::ComboBox& cb) {
        cb.setColour(juce::ComboBox::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        cb.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
        cb.setColour(juce::ComboBox::outlineColourId, DarkTheme::getBorderColour());
        cb.setLookAndFeel(&comboLnf);
    };

    // Family — strict closed-set dropdown (same shape as key/shape). The
    // tags free-text field below is for matching anything that doesn't fit
    // the predefined families.
    familyFilter_.addItem("family: all", 1);
    for (size_t i = 1; i < kFamilies.size(); ++i) {
        familyFilter_.addItem(kFamilies[i], static_cast<int>(i + 1));
    }
    familyFilter_.setSelectedId(1, juce::dontSendNotification);
    styleCombo(familyFilter_);
    familyFilter_.onChange = [this]() { runSearch(); };

    shapeFilter_.addItem("shape: any", 1);
    for (size_t i = 1; i < kShapes.size(); ++i) {
        shapeFilter_.addItem(kShapes[i], static_cast<int>(i + 1));
    }
    shapeFilter_.setSelectedId(1, juce::dontSendNotification);
    styleCombo(shapeFilter_);
    shapeFilter_.onChange = [this]() { runSearch(); };

    keyFilter_.addItem("key: any", 1);
    for (size_t i = 1; i < kKeys.size(); ++i) {
        keyFilter_.addItem(kKeys[i], static_cast<int>(i + 1));
    }
    keyFilter_.setSelectedId(1, juce::dontSendNotification);
    styleCombo(keyFilter_);
    keyFilter_.onChange = [this]() { runSearch(); };

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

    // Tags free-text filter — whitespace-separated tokens are AND-combined
    // via FTS5 MATCH against media_fts.tag_text. Updates on Enter / blur,
    // matching the BPM range editors so we don't query on every keystroke.
    tagsFilter_.setTextToShowWhenEmpty("tags (e.g. drum 808)", DarkTheme::getSecondaryTextColour());
    tagsFilter_.setColour(juce::TextEditor::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    tagsFilter_.setColour(juce::TextEditor::textColourId, DarkTheme::getTextColour());
    tagsFilter_.setColour(juce::TextEditor::outlineColourId, DarkTheme::getBorderColour());
    tagsFilter_.setFont(uiFont);
    tagsFilter_.onReturnKey = [this]() { runSearch(); };
    tagsFilter_.onFocusLost = [this]() { runSearch(); };

    // Pop-out button — opens this view in its own DocumentWindow. Hidden in
    // the pop-out instance to avoid recursive windows.
    if (!isPopOutInstance_) {
        popOutButton_ = std::make_unique<magda::SvgButton>(
            "Open in window", BinaryData::open_in_new_svg, BinaryData::open_in_new_svgSize);
        popOutButton_->setTooltip("Open in a separate window");
        popOutButton_->onClick = [this]() { openPopOutWindow(); };
        addAndMakeVisible(*popOutButton_);
    }

    addAndMakeVisible(familyFilter_);
    addAndMakeVisible(shapeFilter_);
    addAndMakeVisible(keyFilter_);
    addAndMakeVisible(bpmLabel_);
    addAndMakeVisible(bpmMinBox_);
    addAndMakeVisible(bpmMaxBox_);
    addAndMakeVisible(tonalOnly_);
    addAndMakeVisible(tagsFilter_);

    // Kind selection lives outside this component — see setKindFilter().
    // The search-bar file-type icons in MediaExplorerContent double as the
    // kind selector when library mode is active, so we don't duplicate them
    // here.

    // Results table — drag-out is wired via
    // ResultsTableModel::getDragSourceDescription (encodes the row paths
    // in the drag var) + MediaDbBrowserContent::shouldDropFilesWhenDraggedExternally
    // (decodes them and asks the OS to drop them as files when the drag
    // leaves the app window).
    resultsModel_ = std::make_unique<ResultsTableModel>(*this);
    resultsTable_.setModel(resultsModel_.get());
    resultsTable_.setRowHeight(28);
    resultsTable_.setMultipleSelectionEnabled(true);
    resultsTable_.setHeaderHeight(22);
    resultsTable_.setColour(juce::ListBox::backgroundColourId,
                            DarkTheme::getColour(DarkTheme::BACKGROUND));
    resultsTable_.setColour(juce::ListBox::outlineColourId, DarkTheme::getBorderColour());
    resultsTable_.setOutlineThickness(1);

    auto& header = resultsTable_.getHeader();
    header.setColour(juce::TableHeaderComponent::backgroundColourId,
                     DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05F));
    header.setColour(juce::TableHeaderComponent::textColourId,
                     DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    header.setColour(juce::TableHeaderComponent::outlineColourId, DarkTheme::getBorderColour());
    const int flags = juce::TableHeaderComponent::visible | juce::TableHeaderComponent::resizable |
                      juce::TableHeaderComponent::draggable;
    // (id, name, defaultWidth, minWidth, maxWidth, propertyFlags)
    header.addColumn("Name", kColName, 260, 80, -1, flags);
    header.addColumn("Family", kColFamily, 90, 50, -1, flags);
    header.addColumn("Shape", kColShape, 90, 50, -1, flags);
    header.addColumn("BPM", kColBpm, 60, 40, -1, flags);
    header.addColumn("Key", kColKey, 60, 40, -1, flags);
    header.addColumn("Duration", kColDuration, 70, 40, -1, flags);
    header.setStretchToFitActive(true);  // expand columns to fill width

    addAndMakeVisible(resultsTable_);

    // Empty state
    emptyState_.setText("No samples in your library yet.\nRight-click a folder in the browser "
                        "and choose \"Index this folder\".",
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
    resultsTable_.setModel(nullptr);
    familyFilter_.setLookAndFeel(nullptr);
    shapeFilter_.setLookAndFeel(nullptr);
    keyFilter_.setLookAndFeel(nullptr);
    bpmMinBox_.setLookAndFeel(nullptr);
    bpmMaxBox_.setLookAndFeel(nullptr);
    tonalOnly_.setLookAndFeel(nullptr);
}

void MediaDbBrowserContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE));
}

void MediaDbBrowserContent::resized() {
    auto bounds = getLocalBounds();

    // Two-row filter strip. Row 1: family / shape / key dropdowns + pop-out.
    // Row 2: BPM range + tonal. Kind selection is driven externally from the
    // search-bar icons (see MediaExplorerContent), so no third row.
    auto row1 = bounds.removeFromTop(28);
    row1.removeFromLeft(4);
    row1.removeFromRight(4);
    if (popOutButton_) {
        popOutButton_->setBounds(row1.removeFromRight(24).reduced(2));
        row1.removeFromRight(6);
    }
    familyFilter_.setBounds(row1.removeFromLeft(110).reduced(2));
    row1.removeFromLeft(6);
    shapeFilter_.setBounds(row1.removeFromLeft(100).reduced(2));
    row1.removeFromLeft(6);
    keyFilter_.setBounds(row1.removeFromLeft(90).reduced(2));

    auto row2 = bounds.removeFromTop(28);
    row2.removeFromLeft(4);
    row2.removeFromRight(4);
    bpmLabel_.setBounds(row2.removeFromLeft(30));
    bpmMinBox_.setBounds(row2.removeFromLeft(60).reduced(2));
    row2.removeFromLeft(4);
    bpmMaxBox_.setBounds(row2.removeFromLeft(60).reduced(2));
    row2.removeFromLeft(8);
    tonalOnly_.setBounds(row2.removeFromLeft(70).reduced(2));
    row2.removeFromLeft(8);
    // Tags fills the rest of the row — gives it room to breathe in
    // popped-out / wide panels and stays at minimum ~140px in narrow ones.
    tagsFilter_.setBounds(row2.reduced(2));

    bounds.removeFromTop(4);

    // Status strip at the bottom of the content area when indexing.
    if (statusLabel_.isVisible()) {
        statusLabel_.setBounds(bounds.removeFromBottom(18).reduced(8, 2));
    } else {
        statusLabel_.setBounds(bounds.getX() + 8, bounds.getBottom() - 18, bounds.getWidth() - 16,
                               18);
    }
    resultsTable_.setBounds(bounds);
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
    // Read filter values from a hard-coded table indexed by selectedId
    // instead of asking the ComboBox for the displayed text — getText() and
    // getItemText(getSelectedItemIndex()) have both been seen to lag the
    // selectedId update on first interaction, causing a stale or empty
    // filter on the first call to onChange.
    // Family / shape / key are strict closed-set dropdowns — read by
    // selectedId so onChange always sees a coherent value (getText() can
    // briefly lag).
    f.family = selectedString(familyFilter_, kFamilies);
    f.shape = selectedString(shapeFilter_, kShapes);
    f.keyRoot = selectedString(keyFilter_, kKeys);

    if (kindFilter_) {
        f.kind = *kindFilter_;
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
    const auto tagsText = tagsFilter_.getText().trim();
    if (tagsText.isNotEmpty()) {
        f.tags = tagsText.toStdString();
    }
    return f;
}

void MediaDbBrowserContent::runSearch() {
    auto& ctx = magda::media::MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        results_.clear();
        resultsTable_.updateContent();
        resultsTable_.repaint();
        emptyState_.setText("Failed to open the media database.", juce::dontSendNotification);
        emptyState_.setVisible(true);
        resultsTable_.setVisible(false);
        return;
    }

    // Lazy-load gate: ctx.textEncoder() / ctx.tokenizer() each trigger an
    // ONNX model load on first call (~480 MB on the text side). Filter-only
    // queries don't need them — only call the accessors when there's actual
    // search text to embed. Avoids a multi-second UI freeze when the user
    // just changes a categorical filter.
    const auto filters = currentFilters();
    const bool hasText = !queryText_.isEmpty();
    const std::optional<std::string> text =
        hasText ? std::optional<std::string>{queryText_.toStdString()} : std::nullopt;
    magda::media::ClapTextEncoder* textEnc = hasText ? ctx.textEncoder() : nullptr;
    magda::media::RobertaTokenizer* tok = hasText ? ctx.tokenizer() : nullptr;
    magda::media::MediaDbQuery query(ctx.db(), textEnc, tok);
    results_ = query.search(text, filters, /*limit=*/200);

    // updateContent() refreshes the row count but doesn't always invalidate
    // the paint region — explicit repaint() makes the new results show up
    // without needing an external paint trigger (window focus, scroll, etc).
    resultsTable_.updateContent();
    resultsTable_.repaint();

    const bool empty = results_.empty();
    resultsTable_.setVisible(!empty);
    emptyState_.setVisible(empty);
    if (empty) {
        emptyState_.setText(
            queryText_.isEmpty()
                ? "No samples in your library yet.\nRight-click a folder in the browser "
                  "and choose \"Index this folder\"."
                : "No results.",
            juce::dontSendNotification);
    }
}

void MediaDbBrowserContent::setKindFilter(std::optional<std::string> kind) {
    if (kindFilter_ == kind) {
        return;
    }
    kindFilter_ = std::move(kind);
    runSearch();
}

void MediaDbBrowserContent::visibilityChanged() {
    // Fired the first time the sidebar puts us on screen (and every subsequent
    // show/hide). Re-running the search here guarantees the table reflects the
    // current DB state regardless of indexing that happened off-screen — and
    // sidesteps the startup ordering where setVisible(true) → refresh() →
    // resized() could populate before the table had its final bounds.
    if (isVisible()) {
        runSearch();
    }
}

void MediaDbBrowserContent::startIndexing(const juce::File& dir) {
    if (indexing_ || !dir.isDirectory()) {
        return;  // Already running, or invalid target — ignore.
    }

    if (!indexPool_) {
        indexPool_ = std::make_unique<juce::ThreadPool>(1);
    }

    indexing_ = true;
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
            magda::media::MediaDatabase bgDb(magda::media::MediaDbContext::getInstance().dbPath());
            magda::media::MediaDbIndexer indexer(
                bgDb, magda::media::MediaDbContext::getInstance().audioEncoder());
            indexer.setProgress([self](int done, int total, const std::filesystem::path& current) {
                // Fired per-file on the indexer thread; marshal the text
                // update to the UI thread via callAsync.
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
            self->statusLabel_.setVisible(false);
            self->resized();
            self->runSearch();
        });
    });
}

// ===========================================================================
// PopOutWindow — DocumentWindow that hosts a detached MediaDbBrowserContent
// ===========================================================================
//
// Self-deleting on close (the JUCE-blessed pattern for floating windows).
// The owning MediaDbBrowserContent only keeps a SafePointer to it, so when
// the user clicks the OS close button the pointer goes null and a subsequent
// pop-out click spawns a fresh window.

class MediaDbBrowserContent::PopOutWindow : public juce::DocumentWindow {
  public:
    PopOutWindow()
        : juce::DocumentWindow("MAGDA — Sample Library",
                               DarkTheme::getColour(DarkTheme::BACKGROUND),
                               juce::DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        // setContentOwned takes ownership and deletes on window destruction.
        setContentOwned(new MediaDbBrowserContent(/*isPopOutInstance=*/true), true);
        centreWithSize(960, 640);
        setVisible(true);
    }

    void closeButtonPressed() override {
        delete this;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PopOutWindow)
};

void MediaDbBrowserContent::openPopOutWindow() {
    if (popOutWindow_ != nullptr) {
        // Window already exists — bring it to the front.
        popOutWindow_->toFront(true);
        return;
    }
    auto* w = new PopOutWindow();
    popOutWindow_ = w;  // SafePointer; auto-clears when w is deleted.
}

}  // namespace magda::daw::ui
