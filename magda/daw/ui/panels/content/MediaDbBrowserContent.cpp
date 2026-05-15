#include "MediaDbBrowserContent.hpp"

#include <filesystem>

#include "../../../media_db/MediaDatabase.hpp"
#include "../../../media_db/MediaDbContext.hpp"
#include "../../../media_db/MediaDbIndexer.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

juce::String prettyDuration(std::optional<double> seconds) {
    if (!seconds) {
        return "–";
    }
    const double s = *seconds;
    if (s < 1.0) {
        return juce::String(s, 2) + "s";
    }
    return juce::String(s, 1) + "s";
}

juce::String prettyBpm(std::optional<double> bpm) {
    if (!bpm) {
        return "–";
    }
    return juce::String(static_cast<int>(std::round(*bpm)));
}

juce::String prettyKey(const std::optional<std::string>& root,
                       const std::optional<std::string>& scale) {
    if (!root) {
        return "–";
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

        // Right-side meta strip: bpm · key · duration
        const auto& font = FontManager::getInstance().getUIFont(11.0F);
        g.setFont(font);
        const juce::String meta = prettyBpm(r.bpm) + " bpm · " + prettyKey(r.keyRoot, r.keyScale) +
                                  " · " + prettyDuration(r.durationS);
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
    // Filter strip
    auto styleLabel = [](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setFont(FontManager::getInstance().getUIFont(11.0F));
        l.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        l.setJustificationType(juce::Justification::centredRight);
    };
    styleLabel(familyLabel_, "family");
    styleLabel(shapeLabel_, "shape");
    styleLabel(bpmLabel_, "bpm");

    familyFilter_.addItem("all", 1);
    for (const auto* f :
         {"drum", "bass", "lead", "pad", "keys", "guitar", "orchestral", "vocal", "fx"}) {
        familyFilter_.addItem(f, familyFilter_.getNumItems() + 1);
    }
    familyFilter_.setSelectedId(1, juce::dontSendNotification);
    familyFilter_.onChange = [this]() { runSearch(); };

    shapeFilter_.addItem("any", 1);
    for (const auto* s : {"one-shot", "loop", "sustained"}) {
        shapeFilter_.addItem(s, shapeFilter_.getNumItems() + 1);
    }
    shapeFilter_.setSelectedId(1, juce::dontSendNotification);
    shapeFilter_.onChange = [this]() { runSearch(); };

    auto setupBpm = [this](juce::TextEditor& e, const juce::String& placeholder) {
        e.setTextToShowWhenEmpty(placeholder,
                                 DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.6F));
        e.setInputRestrictions(4, "0123456789.");
        e.onReturnKey = [this]() { runSearch(); };
        e.onFocusLost = [this]() { runSearch(); };
    };
    setupBpm(bpmMinBox_, "min");
    setupBpm(bpmMaxBox_, "max");

    tonalOnly_.onClick = [this]() { runSearch(); };

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
    emptyState_.setText("No samples indexed yet.\nClick \"Index folder…\" to add one.",
                        juce::dontSendNotification);
    emptyState_.setFont(FontManager::getInstance().getUIFont(13.0F));
    emptyState_.setJustificationType(juce::Justification::centred);
    emptyState_.setColour(juce::Label::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    emptyState_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(emptyState_);

    // First-time query if context is ready
    runSearch();
}

MediaDbBrowserContent::~MediaDbBrowserContent() {
    resultsList_.setModel(nullptr);
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
                                ? "No samples indexed yet.\nClick \"Index folder…\" to add one."
                                : "No results.",
                            juce::dontSendNotification);
    }
}

void MediaDbBrowserContent::onIndexFolderClicked() {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Choose a folder to index", juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    auto flags =
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    fileChooser_->launchAsync(flags, [this](const juce::FileChooser& fc) {
        const auto dir = fc.getResult();
        if (!dir.isDirectory()) {
            return;
        }

        auto& ctx = magda::media::MediaDbContext::getInstance();
        if (!ctx.ensureInitialized()) {
            return;
        }
        magda::media::MediaDbIndexer indexer(ctx.db(), ctx.audioEncoder());
        // Synchronous for F2; F3 moves this to a JUCE ThreadPool.
        indexer.indexDirectory(std::filesystem::path(dir.getFullPathName().toStdString()));
        runSearch();
    });
}

}  // namespace magda::daw::ui
