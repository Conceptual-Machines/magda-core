// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom_ui/SequencerFaceplate.hpp"

#include <BinaryData.h>

#include "magda/sdk/sequencer/StepPattern.hpp"
#include "ui/components/chain/layout/NodeHeaderStyles.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui::sequencer_faceplate {

namespace {

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

bool isBlackKey(int noteNumber) {
    static const bool isBlack[] = {false, true,  false, true,  false, false,
                                   true,  false, true,  false, true,  false};
    return isBlack[((noteNumber % 12) + 12) % 12];
}

void drawButtonPair(juce::Graphics& g, juce::Rectangle<int> box, Glyph top, bool topEnabled,
                    Glyph bottom, bool bottomEnabled) {
    if (box.isEmpty())
        return;
    g.setColour(colour(ActiveTheme::DEVICE_WELL));
    g.fillRoundedRectangle(box.toFloat(), 4.0f);
    g.setColour(colour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 4.0f, 1.0f);

    const auto draw = [&g](juce::Rectangle<int> half, Glyph glyph, bool enabled) {
        g.setColour(colour(enabled ? ActiveTheme::DEVICE_TEXT : ActiveTheme::DEVICE_ICON_INACTIVE));
        const auto c = half.getCentre().toFloat();
        constexpr float r = 4.0f;
        juce::Path path;
        switch (glyph) {
            case Glyph::Up:
                path.addTriangle(c.x - r, c.y + r * 0.6f, c.x + r, c.y + r * 0.6f, c.x,
                                 c.y - r * 0.7f);
                g.fillPath(path);
                break;
            case Glyph::Down:
                path.addTriangle(c.x - r, c.y - r * 0.6f, c.x + r, c.y - r * 0.6f, c.x,
                                 c.y + r * 0.7f);
                g.fillPath(path);
                break;
            case Glyph::Plus:
                g.drawLine(c.x, c.y - r, c.x, c.y + r, 1.4f);
                [[fallthrough]];
            case Glyph::Minus:
                g.drawLine(c.x - r, c.y, c.x + r, c.y, 1.4f);
                break;
        }
    };
    draw(box.removeFromTop(box.getHeight() / 2), top, topEnabled);
    draw(box, bottom, bottomEnabled);
}

void drawGridFloor(juce::Graphics& g, juce::Rectangle<int> cells, int count, int rows,
                   const std::function<bool(int row)>& isDarkRow,
                   const std::function<bool(int step)>& isRest) {
    const float colW = static_cast<float>(cells.getWidth()) / static_cast<float>(count);
    const float rowH = static_cast<float>(cells.getHeight()) / static_cast<float>(rows);

    g.setColour(colour(ActiveTheme::DEVICE_GRAPH_BG));
    g.fillRect(cells);
    g.setColour(colour(ActiveTheme::DEVICE_WELL));
    for (int row = 0; row < rows; ++row)
        if (isDarkRow(row))
            g.fillRect(juce::Rectangle<float>(static_cast<float>(cells.getX()),
                                              cells.getY() + row * rowH,
                                              static_cast<float>(cells.getWidth()), rowH));
    for (int i = 0; i < count; ++i) {
        if (!isRest(i))
            continue;
        g.setColour(juce::Colours::black.withAlpha(0.28f));
        g.fillRect(juce::Rectangle<float>(cells.getX() + i * colW, static_cast<float>(cells.getY()),
                                          colW, static_cast<float>(cells.getHeight())));
    }

    g.setColour(colour(ActiveTheme::DEVICE_GRAPH_GRID));
    for (int row = 1; row < rows; ++row)
        g.drawHorizontalLine(juce::roundToInt(cells.getY() + row * rowH),
                             static_cast<float>(cells.getX()),
                             static_cast<float>(cells.getRight()));
    for (int i = 1; i < count; ++i) {
        g.setColour(colour(i % 4 == 0 ? ActiveTheme::DEVICE_LINE : ActiveTheme::DEVICE_GRAPH_GRID));
        g.drawVerticalLine(juce::roundToInt(cells.getX() + i * colW),
                           static_cast<float>(cells.getY()), static_cast<float>(cells.getBottom()));
    }
}

void drawPlayhead(juce::Graphics& g, juce::Rectangle<int> cells, int count, int playStep) {
    if (playStep < 0 || playStep >= count)
        return;
    const float x = cells.getX() + playStep * static_cast<float>(cells.getWidth()) / count;
    g.setColour(colour(ActiveTheme::DEVICE_GREEN));
    g.fillRect(juce::Rectangle<float>(x, static_cast<float>(cells.getY()), 1.5f,
                                      static_cast<float>(cells.getHeight())));
}

void drawNoteBar(juce::Graphics& g, juce::Rectangle<float> bar, int velocity, bool sounds) {
    const auto green = colour(ActiveTheme::DEVICE_GREEN);
    if (!sounds) {
        g.setColour(green.withAlpha(0.45f));
        g.drawRoundedRectangle(bar.reduced(0.5f), 2.5f, 1.0f);
        return;
    }
    g.setColour(green.withAlpha(0.45f + 0.55f * static_cast<float>(velocity) / 127.0f));
    g.fillRoundedRectangle(bar, 2.5f);
}

std::unique_ptr<magda::SvgButton> makeExpandButton() {
    auto button = std::make_unique<magda::SvgButton>("Expand", BinaryData::enter_fs_svg,
                                                     BinaryData::enter_fs_svgSize);
    node_header::applyDeviceIconStyle(*button, node_header::DeviceIcon::Toggle,
                                      juce::Colour(0xFFB3B3B3), ActiveTheme::DEVICE_BLUE, 18.0f);
    button->setTooltip("Expand into the arrangement");
    return button;
}

juce::Rectangle<int> expandButtonBounds(juce::Rectangle<int> ruler) {
    return ruler.withWidth(kGutterWidth - 8).removeFromRight(22).withSizeKeepingCentre(22, 18);
}

void drawNoteCell(juce::Graphics& g, juce::Rectangle<float> cell, const NoteCell& note) {
    const auto green = colour(ActiveTheme::DEVICE_GREEN);
    g.setColour(colour(ActiveTheme::DEVICE_FIELD));
    g.fillRoundedRectangle(cell, kCellRadius);
    if (note.playing) {
        g.setColour(green.withAlpha(0.14f));
        g.fillRoundedRectangle(cell, kCellRadius);
    }

    auto border = colour(ActiveTheme::DEVICE_FIELD_BORDER);
    float thickness = 1.0f;
    if (note.dropTarget) {
        border = colour(ActiveTheme::DEVICE_BLUE);
        thickness = 1.5f;
    } else if (note.selected) {
        border = green;
        thickness = 1.5f;
    }
    g.setColour(border);
    g.drawRoundedRectangle(cell.reduced(thickness * 0.5f), kCellRadius, thickness);

    g.setColour(colour(note.sounds ? ActiveTheme::DEVICE_VALUE_TEXT : ActiveTheme::DEVICE_DIM2));
    g.drawText(note.text, cell.toNearestInt(), juce::Justification::centred, false);
}

namespace {

const bool kIsBlack[] = {false, true,  false, true,  false, false,
                         true,  false, true,  false, true,  false};

struct KeyGeometry {
    float whiteW, blackW, blackH;
};

KeyGeometry keyGeometry(juce::Rectangle<float> area, int octaves) {
    const float whiteW = area.getWidth() / static_cast<float>(octaves * 7);
    return {whiteW, whiteW * 0.62f, area.getHeight() * 0.6f};
}

}  // namespace

void drawKeyboard(juce::Graphics& g, juce::Rectangle<float> area, int baseNote, int octaves,
                  const std::function<bool(int)>& isLit) {
    if (area.isEmpty())
        return;

    const auto [whiteW, blackW, blackH] = keyGeometry(area, octaves);
    const auto green = colour(ActiveTheme::DEVICE_GREEN);
    const int numNotes = octaves * 12;

    juce::Path outline;
    outline.addRoundedRectangle(area, 5.0f);
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(outline);

    g.setFont(FontManager::getInstance().getMonoFont(9.5f));
    int whiteIdx = 0;
    for (int n = 0; n < numNotes; ++n) {
        if (kIsBlack[n % 12])
            continue;
        const int note = baseNote + n;
        const juce::Rectangle<float> key{area.getX() + whiteIdx * whiteW, area.getY(), whiteW,
                                         area.getHeight()};
        const bool lit = isLit(note);
        g.setColour(lit ? green : kWhiteKey);
        g.fillRect(key);
        g.setColour(kBlackKey.withAlpha(0.35f));
        g.drawVerticalLine(juce::roundToInt(key.getRight()), key.getY(), key.getBottom());
        if (n % 12 == 0 || lit) {
            g.setColour(lit ? kWhiteKey : kBlackKey.withAlpha(0.6f));
            g.drawText(noteName(note), key.reduced(3.0f, 3.0f).toNearestInt(),
                       juce::Justification::bottomLeft, false);
        }
        ++whiteIdx;
    }

    whiteIdx = 0;
    for (int n = 0; n < numNotes; ++n) {
        if (!kIsBlack[n % 12]) {
            ++whiteIdx;
            continue;
        }
        const juce::Rectangle<float> key{area.getX() + whiteIdx * whiteW - blackW / 2.0f,
                                         area.getY(), blackW, blackH};
        g.setColour(isLit(baseNote + n) ? green : kBlackKey);
        g.fillRoundedRectangle(key.withTrimmedTop(-3.0f), 3.0f);
    }

    g.setColour(colour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.strokePath(outline, juce::PathStrokeType(1.0f));
}

int keyboardNoteAt(juce::Rectangle<float> area, int baseNote, int octaves,
                   juce::Point<float> point) {
    const auto [whiteW, blackW, blackH] = keyGeometry(area, octaves);
    const int numNotes = octaves * 12;

    // Black keys sit over the white ones, so they are hit first.
    int whiteIdx = 0;
    for (int n = 0; n < numNotes; ++n) {
        if (!kIsBlack[n % 12]) {
            ++whiteIdx;
            continue;
        }
        const juce::Rectangle<float> key{area.getX() + whiteIdx * whiteW - blackW / 2.0f,
                                         area.getY(), blackW, blackH};
        if (key.contains(point))
            return baseNote + n;
    }

    whiteIdx = 0;
    for (int n = 0; n < numNotes; ++n) {
        if (kIsBlack[n % 12])
            continue;
        const juce::Rectangle<float> key{area.getX() + whiteIdx * whiteW, area.getY(), whiteW,
                                         area.getHeight()};
        if (key.contains(point))
            return baseNote + n;
        ++whiteIdx;
    }
    return -1;
}

// =============================================================================
// NotePicker / DrumPicker
// =============================================================================

namespace {

void drawOctaveButton(juce::Graphics& g, juce::Rectangle<int> area, bool enabled, bool plus) {
    g.setColour(colour(ActiveTheme::DEVICE_WELL));
    g.fillRoundedRectangle(area.toFloat(), 4.0f);
    g.setColour(colour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 4.0f, 1.0f);
    g.setColour(colour(enabled ? ActiveTheme::DEVICE_TEXT : ActiveTheme::DEVICE_ICON_INACTIVE));
    const auto c = area.getCentre().toFloat();
    g.drawLine(c.x - 4.0f, c.y, c.x + 4.0f, c.y, 1.4f);
    if (plus)
        g.drawLine(c.x, c.y - 4.0f, c.x, c.y + 4.0f, 1.4f);
}

}  // namespace

NotePicker::NotePicker(int firstNote, std::function<bool(int)> isLit,
                       std::function<void(int)> onNote)
    : isLit_(std::move(isLit)),
      onNote_(std::move(onNote)),
      baseNote_(juce::jlimit(0, 128 - kOctaves * 12, firstNote / 12 * 12)) {
    setSize(kOctaves * 7 * 24 + 16, 120);
    startTimerHz(15);
}

void NotePicker::show(juce::Component& owner, juce::Rectangle<int> box,
                      std::unique_ptr<NotePicker> picker) {
    juce::CallOutBox::launchAsynchronously(std::move(picker), owner.localAreaToGlobal(box),
                                           nullptr);
}

void NotePicker::resized() {
    auto area = getLocalBounds().reduced(8);
    auto top = area.removeFromTop(22);
    upArea_ = top.removeFromRight(28);
    top.removeFromRight(4);
    downArea_ = top.removeFromRight(28);
    labelArea_ = top;
    area.removeFromTop(6);
    keysArea_ = area;
}

void NotePicker::paint(juce::Graphics& g) {
    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    g.setColour(colour(ActiveTheme::DEVICE_DIM));
    g.drawText(noteName(baseNote_) + " - " + noteName(baseNote_ + kOctaves * 12 - 1), labelArea_,
               juce::Justification::centredLeft, false);
    drawOctaveButton(g, downArea_, baseNote_ > 0, false);
    drawOctaveButton(g, upArea_, baseNote_ + (kOctaves + 1) * 12 <= 128, true);
    drawKeyboard(g, keysArea_.toFloat(), baseNote_, kOctaves, isLit_);
}

void NotePicker::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    if (downArea_.contains(pos) || upArea_.contains(pos)) {
        const int next = baseNote_ + (upArea_.contains(pos) ? 12 : -12);
        if (next >= 0 && next + kOctaves * 12 <= 128) {
            baseNote_ = next;
            repaint();
        }
        return;
    }
    const int note = keyboardNoteAt(keysArea_.toFloat(), baseNote_, kOctaves, pos.toFloat());
    if (note >= 0 && note <= 127 && onNote_) {
        onNote_(note);
        repaint();
    }
}

DrumPicker::DrumPicker(std::vector<Drum> drums, std::function<bool(int)> isLit,
                       std::function<void(int)> onNote)
    : drums_(std::move(drums)), isLit_(std::move(isLit)), onNote_(std::move(onNote)) {
    const int count = juce::jmax(1, static_cast<int>(drums_.size()));
    const int columns = juce::jmin(kColumns, count);
    const int rows = (count + kColumns - 1) / kColumns;
    setSize(16 + columns * kChipW + (columns - 1) * kGap, 16 + rows * kChipH + (rows - 1) * kGap);
    startTimerHz(15);
}

juce::Rectangle<int> DrumPicker::chipBounds(int index) const {
    return {8 + (index % kColumns) * (kChipW + kGap), 8 + (index / kColumns) * (kChipH + kGap),
            kChipW, kChipH};
}

void DrumPicker::paint(juce::Graphics& g) {
    const auto green = colour(ActiveTheme::DEVICE_GREEN);
    g.setFont(FontManager::getInstance().getUIFont(11.5f));
    for (int i = 0; i < static_cast<int>(drums_.size()); ++i) {
        const auto& drum = drums_[static_cast<size_t>(i)];
        const auto chip = chipBounds(i).toFloat();
        const bool lit = isLit_(drum.note);
        if (lit) {
            g.setColour(green.withAlpha(0.18f));
            g.fillRoundedRectangle(chip, kCellRadius);
            g.setColour(green.withAlpha(0.6f));
            g.drawRoundedRectangle(chip.reduced(0.5f), kCellRadius, 1.0f);
        } else {
            fillField(g, chip);
        }
        g.setColour(colour(lit ? ActiveTheme::DEVICE_VALUE_TEXT : ActiveTheme::DEVICE_DIM));
        g.drawText(drum.name, chip.reduced(6.0f, 0.0f).toNearestInt(), juce::Justification::centred,
                   true);
    }
}

void DrumPicker::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < static_cast<int>(drums_.size()); ++i) {
        if (!chipBounds(i).contains(e.getPosition()))
            continue;
        if (onNote_)
            onNote_(drums_[static_cast<size_t>(i)].note);
        repaint();
        return;
    }
}

// =============================================================================
// SettingsStrip
// =============================================================================

SettingsStrip::SettingsStrip() {
    static const char* rateNames[] = {"1/4.", "1/4",   "1/4T", "1/8.",  "1/8",
                                      "1/8T", "1/16.", "1/16", "1/16T", "1/32"};
    rate.setRange(0, 9, 1);
    rate.setValueFormatter(
        [](double v) { return juce::String(rateNames[juce::jlimit(0, 9, juce::roundToInt(v))]); });
    rate.setValueParser([](const juce::String&) { return 1.0; });

    steps.setRange(1, sdk::sequencer::kMaxSteps, 1);
    steps.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    steps.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });

    gate.setRange(0.05, 1.0, 0.01);
    gate.setValueFormatter(percent);
    gate.setValueParser(parsePercent);

    direction.setOptions({"Fwd", "Rev", "P-P", "Rnd"});
    direction.setSelectedIndex(0);

    for (auto [caption, control, width] :
         {std::tuple<const char*, juce::Component*, int>{"Rate", &rate, 46},
          {"Steps", &steps, 32},
          {"Gate", &gate, 42},
          {"Dir", &direction, direction.getPreferredWidth()}}) {
        items_.push_back({caption, control, width, {}});
        addAndMakeVisible(*control);
    }
    lookAndFeelChanged();
}

void SettingsStrip::addExtra(const juce::String& caption, juce::Component& control, int width) {
    items_.push_back({caption, &control, width, {}});
    addAndMakeVisible(control);
    resized();
}

void SettingsStrip::lookAndFeelChanged() {
    for (auto* slider : {&rate, &steps, &gate}) {
        styleTileValue(*slider);
        slider->getSlider().setFont(FontManager::getInstance().getUIFontMedium(12.0f));
        slider->getSlider().setJustification(juce::Justification::centred);
    }
    repaint();
}

void SettingsStrip::resized() {
    const auto font = FontManager::getInstance().getUIFont(11.5f);
    auto area = getLocalBounds();
    for (auto& item : items_) {
        const int captionW =
            juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, item.caption)) + 5;
        item.captionBounds = area.removeFromLeft(captionW);
        item.control->setBounds(area.removeFromLeft(item.width));
        area.removeFromLeft(12);
    }
}

void SettingsStrip::paint(juce::Graphics& g) {
    g.setFont(FontManager::getInstance().getUIFont(11.5f));
    for (const auto& item : items_) {
        g.setColour(colour(ActiveTheme::DEVICE_DIM));
        g.drawText(item.caption, item.captionBounds, juce::Justification::centredLeft, false);
        if (dynamic_cast<LinkableTextSlider*>(item.control) != nullptr)
            fillField(g, item.control->getBounds().toFloat());
    }
}

// =============================================================================
// ValueMenuItem
// =============================================================================

ValueMenuItem::ValueMenuItem(const juce::String& caption,
                             const std::function<void(LinkableTextSlider&)>& setUp)
    : juce::PopupMenu::CustomComponent(false), caption_(caption) {
    setUp(value_);
    addAndMakeVisible(value_);
}

void ValueMenuItem::getIdealSize(int& width, int& height) {
    width = 200;
    height = 24;
}

void ValueMenuItem::resized() {
    value_.setBounds(getLocalBounds().removeFromRight(64).reduced(4, 2));
}

void ValueMenuItem::paint(juce::Graphics& g) {
    g.setColour(findColour(juce::PopupMenu::textColourId));
    g.setFont(FontManager::getInstance().getUIFont(13.0f));
    g.drawText(caption_, getLocalBounds().withTrimmedLeft(12), juce::Justification::centredLeft,
               false);
}

}  // namespace magda::daw::ui::sequencer_faceplate
