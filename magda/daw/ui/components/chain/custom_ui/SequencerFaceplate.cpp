#include "custom_ui/SequencerFaceplate.hpp"

#include "magda/sdk/sequencer/StepPattern.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui::sequencer_faceplate {

namespace {

constexpr int kTileGap = 6;
constexpr int kMinTileH = 36;
constexpr int kMaxTileH = 46;
constexpr int kCaptionH = 14;
constexpr float kCellRadius = 4.0f;

juce::Colour colour(magda::ColourRole role) {
    return ActiveTheme::getColour(role);
}

void fillField(juce::Graphics& g, juce::Rectangle<float> cell) {
    g.setColour(colour(ActiveTheme::DEVICE_FIELD));
    g.fillRoundedRectangle(cell, kCellRadius);
    g.setColour(colour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(cell.reduced(0.5f), kCellRadius, 1.0f);
}

juce::String percent(double v) {
    return juce::String(juce::roundToInt(v * 100.0)) + "%";
}

double parsePercent(const juce::String& text) {
    return text.trimCharactersAtEnd("%").trim().getDoubleValue() / 100.0;
}

}  // namespace

void styleTileValue(LinkableTextSlider& slider) {
    auto& inner = slider.getSlider();
    inner.setDrawFrame(false);
    inner.setShowFillIndicator(false);
    inner.setBackgroundColour(juce::Colours::transparentBlack);
    inner.setFont(FontManager::getInstance().getUIFontMedium(15.0f));
    inner.setTextColour(colour(ActiveTheme::DEVICE_VALUE_TEXT));
    inner.setJustification(juce::Justification::centredLeft);
}

juce::String noteName(int noteNumber) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (noteNumber < 0 || noteNumber > 127)
        return "-";
    return juce::String(names[noteNumber % 12]) + juce::String(noteNumber / 12 - 2);
}

int stepAt(int x, juce::Rectangle<int> area, int numSteps) {
    if (numSteps <= 0 || area.getWidth() <= 0)
        return -1;
    const int relX = x - area.getX();
    if (relX < 0 || relX >= area.getWidth())
        return -1;
    return relX * numSteps / area.getWidth();
}

juce::Rectangle<float> cellBounds(juce::Rectangle<int> area, int step, int numSteps) {
    const float colW = static_cast<float>(area.getWidth()) / static_cast<float>(numSteps);
    // The gap narrows with the columns, so 32 steps still read as cells.
    const float gap = juce::jlimit(1.0f, 5.0f, colW * 0.12f);
    return {area.getX() + step * colW + gap * 0.5f, static_cast<float>(area.getY()), colW - gap,
            static_cast<float>(area.getHeight())};
}

void drawRowLabel(juce::Graphics& g, juce::Rectangle<int> gutter, const juce::String& text) {
    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    g.setColour(colour(ActiveTheme::DEVICE_DIM));
    g.drawText(text, gutter.withTrimmedRight(8), juce::Justification::centredRight, true);
}

void drawRuler(juce::Graphics& g, juce::Rectangle<int> columns, int numSteps, int playStep) {
    g.setFont(FontManager::getInstance().getMonoFont(10.5f));
    for (int i = 0; i < numSteps; ++i) {
        const bool playing = i == playStep;
        if (i % 4 != 0 && !playing)
            continue;
        const auto cell = cellBounds(columns, i, numSteps);
        g.setColour(colour(playing ? ActiveTheme::DEVICE_GREEN : ActiveTheme::DEVICE_DIM));
        g.drawText(juce::String(i + 1), cell.withWidth(28.0f).toNearestInt(),
                   juce::Justification::centredLeft, false);
    }
}

void drawToggleRow(juce::Graphics& g, juce::Rectangle<int> columns, int numSteps,
                   magda::ColourRole role, const std::function<bool(int)>& isOn) {
    const auto lit = colour(role);
    for (int i = 0; i < numSteps; ++i) {
        const auto cell = cellBounds(columns, i, numSteps);
        if (!isOn(i)) {
            fillField(g, cell);
            continue;
        }
        g.setColour(lit.withAlpha(0.16f));
        g.fillRoundedRectangle(cell, kCellRadius);
        g.setColour(lit.withAlpha(0.4f));
        g.drawRoundedRectangle(cell.reduced(0.5f), kCellRadius, 1.0f);
        g.setColour(lit);
        g.fillRoundedRectangle(cell.reduced(juce::jmin(7.0f, cell.getWidth() * 0.12f), 0.0f)
                                   .withSizeKeepingCentre(cell.getWidth() * 0.76f, 3.0f),
                               1.5f);
    }
}

void drawBarRow(juce::Graphics& g, juce::Rectangle<int> columns, int numSteps,
                magda::ColourRole role, const std::function<float(int)>& valueOf) {
    const auto lit = colour(role);
    for (int i = 0; i < numSteps; ++i) {
        const auto cell = cellBounds(columns, i, numSteps);
        g.setColour(colour(ActiveTheme::DEVICE_WELL));
        g.fillRoundedRectangle(cell, kCellRadius);
        g.setColour(colour(ActiveTheme::DEVICE_FIELD_BORDER));
        g.drawRoundedRectangle(cell.reduced(0.5f), kCellRadius, 1.0f);

        const auto inner = cell.reduced(3.0f);
        const float value = juce::jlimit(0.0f, 1.0f, valueOf(i));
        auto bar = inner.withTrimmedTop(inner.getHeight() * (1.0f - value));
        if (bar.getHeight() > 2.0f) {
            g.setColour(lit.withAlpha(0.32f));
            g.fillRoundedRectangle(bar, 2.0f);
        }
        g.setColour(lit);
        g.fillRoundedRectangle(bar.withHeight(2.0f), 1.0f);
    }
}

// =============================================================================
// SequencerControlPanel
// =============================================================================

SequencerControlPanel::SequencerControlPanel() {
    static const char* rateNames[] = {"1/4.", "1/4",   "1/4T", "1/8.",  "1/8",
                                      "1/8T", "1/16.", "1/16", "1/16T", "1/32"};
    rate.setRange(0, 9, 1);
    rate.setValueFormatter(
        [](double v) { return juce::String(rateNames[juce::jlimit(0, 9, juce::roundToInt(v))]); });
    rate.setValueParser([](const juce::String&) { return 1.0; });

    steps.setRange(1, sdk::sequencer::kMaxSteps, 1);
    steps.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    steps.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });

    for (auto* slider : {&swing, &quantize}) {
        slider->setRange(0.0, 1.0, 0.01);
        slider->setValueFormatter(percent);
        slider->setValueParser(parsePercent);
    }
    gate.setRange(0.05, 1.0, 0.01);
    gate.setValueFormatter(percent);
    gate.setValueParser(parsePercent);

    subdivision.setRange(16, 512, 16);
    subdivision.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    subdivision.setValueParser([](const juce::String& t) { return t.trim().getDoubleValue(); });

    bendAmount.setRange(0.0, 1.0, 0.01);
    bendAmount.setValueFormatter(percent);
    bendAmount.setValueParser(parsePercent);
    bendCycles.setRange(1.0, sdk::sequencer::kMaxTimeBendCycles, 1.0);
    bendCycles.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)) + "x"; });
    bendCycles.setValueParser(
        [](const juce::String& t) { return t.trimCharactersAtEnd("x").getDoubleValue(); });

    direction.addItemList({"Forward", "Reverse", "Ping-Pong", "Random"}, 1);
    direction.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    direction.setJustificationType(juce::Justification::centredLeft);

    addTile("Rate", rate);
    addTile("Steps", steps);
    addTile("Swing", swing);
    addTile("Gate", gate);
    addTile("Direction", direction, 2);

    for (auto [caption, slider] :
         {std::pair{"Bend", &bendAmount}, std::pair{"Cycles", &bendCycles},
          std::pair{"Quantize", &quantize}, std::pair{"Sub", &subdivision}}) {
        bendTiles_.push_back({caption, slider, 1, {}});
        addAndMakeVisible(*slider);
    }

    // The bend's amount and cycles are slots, so they sit in tiles the links can reach.
    timeBend.setShowsAmountAndCycles(false);
    addAndMakeVisible(timeBend);

    styleControls();
}

SequencerControlPanel::~SequencerControlPanel() {
    direction.setLookAndFeel(nullptr);
}

void SequencerControlPanel::addTile(const juce::String& caption, juce::Component& control,
                                    int span) {
    tiles_.push_back({caption, &control, span, {}});
    addAndMakeVisible(control);
}

void SequencerControlPanel::addExtraTile(const juce::String& caption, juce::Component& control,
                                         int span) {
    addTile(caption, control, span);
    resized();
}

void SequencerControlPanel::styleControls() {
    for (auto* slider :
         {&rate, &steps, &swing, &gate, &quantize, &subdivision, &bendAmount, &bendCycles})
        styleTileValue(*slider);
    direction.setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    direction.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    direction.setColour(juce::ComboBox::textColourId, colour(ActiveTheme::DEVICE_VALUE_TEXT));
    direction.setColour(juce::ComboBox::arrowColourId, colour(ActiveTheme::DEVICE_DIM));
}

void SequencerControlPanel::lookAndFeelChanged() {
    styleControls();
    repaint();
}

int SequencerControlPanel::preferredWidth() const {
    return kTilesWidth + (showsTimeBend_ ? kColumnGap + kBendWidth : 0);
}

void SequencerControlPanel::setShowsTimeBend(bool shows) {
    if (shows == showsTimeBend_)
        return;
    showsTimeBend_ = shows;
    for (auto& tile : bendTiles_)
        tile.control->setVisible(shows);
    timeBend.setVisible(shows);
    resized();
    repaint();
}

void SequencerControlPanel::resized() {
    auto area = getLocalBounds();
    const int tileH = juce::jlimit(kMinTileH, kMaxTileH, area.getHeight() / 5);

    // Tiles flow across @p columns of @p column, a wide one wrapping whole to the next row.
    const auto flow = [&](std::vector<Tile>& tiles, juce::Rectangle<int>& column, int columns) {
        const int colW = (column.getWidth() - (columns - 1) * kTileGap) / columns;
        int col = 0, row = 0;
        for (auto& tile : tiles) {
            if (col + tile.span > columns) {
                col = 0;
                ++row;
            }
            tile.bounds = {column.getX() + col * (colW + kTileGap),
                           column.getY() + row * (tileH + kTileGap),
                           tile.span * colW + (tile.span - 1) * kTileGap, tileH};
            auto inner = tile.bounds.reduced(9, 5);
            inner.removeFromTop(kCaptionH);
            tile.control->setBounds(inner);
            col += tile.span;
        }
        column.removeFromTop((row + 1) * (tileH + kTileGap));
    };

    auto tiles = area.removeFromLeft(kTilesWidth);
    flow(tiles_, tiles, 2);

    area.removeFromLeft(kColumnGap);
    dividerX_ = area.getX() - kColumnGap / 2;
    if (!showsTimeBend_)
        return;

    bendHeader_ = area.removeFromTop(16);
    area.removeFromTop(4);
    flow(bendTiles_, area, 4);
    area.removeFromTop(kTileGap);
    timeBend.setBounds(area);
}

void SequencerControlPanel::paint(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();
    g.setFont(fonts.getUIFont(12.0f));
    for (const auto* tiles : {&tiles_, &bendTiles_}) {
        if (tiles == &bendTiles_ && !showsTimeBend_)
            break;
        for (const auto& tile : *tiles) {
            fillField(g, tile.bounds.toFloat());
            g.setColour(colour(ActiveTheme::DEVICE_DIM));
            g.drawText(tile.caption, tile.bounds.reduced(9, 5).removeFromTop(kCaptionH),
                       juce::Justification::centredLeft, true);
        }
    }

    if (!showsTimeBend_)
        return;
    g.setColour(colour(ActiveTheme::DEVICE_LINE));
    g.drawVerticalLine(dividerX_, 0.0f, static_cast<float>(getHeight()));
    g.setColour(colour(ActiveTheme::DEVICE_DIM));
    g.setFont(fonts.getMonoFont(10.0f).withExtraKerningFactor(0.12f));
    g.drawText("TIME BEND", bendHeader_, juce::Justification::centredLeft, false);
}

}  // namespace magda::daw::ui::sequencer_faceplate
