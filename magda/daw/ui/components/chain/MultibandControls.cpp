#include "MultibandControls.hpp"

#include <cmath>

#include "core/SelectionManager.hpp"
#include "core/TrackCommands.hpp"
#include "core/UndoManager.hpp"
#include "layout/DeviceShellPainter.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui::multiband {

namespace {

const magda::RackInfo* rackAt(const magda::ChainNodePath& rackPath) {
    return magda::TrackManager::getInstance().getRackByPath(rackPath);
}

float order(magda::CrossoverSlope slope) {
    return static_cast<float>(magda::slopeDbPerOctave(slope)) / 6.0f;
}

// A band's magnitude: the low-pass of its upper crossover times the high-passes below it. The
// allpasses that keep the bands in phase leave the magnitude alone.
float bandMagnitude(const std::vector<magda::Crossover>& crossovers, std::size_t band, float hz) {
    float magnitude = 1.0f;
    for (std::size_t j = 0; j < crossovers.size(); ++j) {
        const float ratio = std::pow(hz / crossovers[j].frequencyHz, order(crossovers[j].slope));
        if (j == band)
            magnitude *= 1.0f / (1.0f + ratio);
        else if (j < band)
            magnitude *= ratio / (1.0f + ratio);
    }
    return magnitude;
}

void commitCrossovers(const magda::ChainNodePath& rackPath,
                      std::vector<magda::Crossover> crossovers) {
    magda::RackPropertyPatch patch;
    patch.crossovers = std::move(crossovers);
    magda::UndoManager::getInstance().executeCommand(
        std::make_unique<magda::SetRackPropertiesByPathCommand>(rackPath, std::move(patch)));
}

}  // namespace

juce::String bandName(int band, int bands) {
    static const juce::StringArray kTwo{"Low", "High"};
    static const juce::StringArray kThree{"Low", "Mid", "High"};
    static const juce::StringArray kFour{"Low", "Low Mid", "High Mid", "High"};
    static const juce::StringArray kFive{"Low", "Low Mid", "Mid", "High Mid", "High"};
    switch (bands) {
        case 2:
            return kTwo[band];
        case 3:
            return kThree[band];
        case 4:
            return kFour[band];
        case 5:
            return kFive[band];
        default:
            return "Band " + juce::String(band + 1);
    }
}

juce::String formatFrequency(float hz) {
    if (hz >= 1000.0f)
        return juce::String(hz / 1000.0f, hz >= 10000.0f ? 1 : 2)
                   .trimCharactersAtEnd("0")
                   .trimCharactersAtEnd(".") +
               " kHz";
    return juce::String(juce::roundToInt(hz)) + " Hz";
}

void CrossoverEdit::begin() {
    if (const auto* rack = rackAt(rackPath_))
        before_ = rack->crossovers;
}

void CrossoverEdit::set(int index, magda::Crossover crossover) {
    magda::TrackManager::getInstance().setRackCrossover(rackPath_, index, crossover);
}

// The drag already moved the model, so it goes back to where it started for the command to take
// it forward again as one undoable step.
void CrossoverEdit::end() {
    const auto before = std::exchange(before_, std::nullopt);
    const auto* rack = rackAt(rackPath_);
    if (!before || rack == nullptr || rack->crossovers == *before)
        return;
    auto after = rack->crossovers;
    magda::TrackManager::getInstance().setRackCrossovers(rackPath_, *before);
    commitCrossovers(rackPath_, std::move(after));
}

CrossoverDisplay::CrossoverDisplay(magda::ChainNodePath rackPath)
    : rackPath_(rackPath), edit_(std::move(rackPath)) {
    magda::TrackManager::getInstance().addListener(this);
}

CrossoverDisplay::~CrossoverDisplay() {
    magda::TrackManager::getInstance().removeListener(this);
}

void CrossoverDisplay::trackPropertyChanged(int trackId) {
    if (trackId == rackPath_.trackId)
        repaint();
}

juce::Rectangle<float> CrossoverDisplay::plotArea() const {
    return getLocalBounds().toFloat().reduced(1.0f);
}

float CrossoverDisplay::xForFrequency(float hz) const {
    const auto plot = plotArea();
    const float position = std::log(hz / magda::kMinCrossoverHz) /
                           std::log(magda::kMaxCrossoverHz / magda::kMinCrossoverHz);
    return plot.getX() + position * plot.getWidth();
}

float CrossoverDisplay::frequencyForX(float x) const {
    const auto plot = plotArea();
    const float position = juce::jlimit(0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
    return magda::kMinCrossoverHz *
           std::pow(magda::kMaxCrossoverHz / magda::kMinCrossoverHz, position);
}

int CrossoverDisplay::crossoverAt(float x) const {
    const auto* rack = rackAt(rackPath_);
    if (rack == nullptr)
        return -1;
    for (std::size_t i = 0; i < rack->crossovers.size(); ++i)
        if (std::abs(xForFrequency(rack->crossovers[i].frequencyHz) - x) <= 5.0f)
            return static_cast<int>(i);
    return -1;
}

juce::Rectangle<float> CrossoverDisplay::bandLabel(std::size_t band) const {
    const auto* rack = rackAt(rackPath_);
    if (rack == nullptr || band > rack->crossovers.size())
        return {};
    const auto plot = plotArea();
    const auto& crossovers = rack->crossovers;
    const float left = band > 0 ? xForFrequency(crossovers[band - 1].frequencyHz) : plot.getX();
    const float right =
        band < crossovers.size() ? xForFrequency(crossovers[band].frequencyHz) : plot.getRight();
    const auto name = bandName(static_cast<int>(band), static_cast<int>(crossovers.size() + 1));
    const auto font = FontManager::getInstance().getMonoFont(10.0f).withExtraKerningFactor(0.08f);
    const float width = juce::GlyphArrangement::getStringWidth(font, name.toUpperCase()) + 8.0f;
    return juce::Rectangle<float>(width, 18.0f)
        .withCentre({(left + right) / 2.0f, plot.getY() + 14.0f});
}

int CrossoverDisplay::bandLabelAt(juce::Point<float> point) const {
    const auto* rack = rackAt(rackPath_);
    if (rack == nullptr)
        return -1;
    for (std::size_t band = 0; band <= rack->crossovers.size(); ++band)
        if (bandLabel(band).contains(point))
            return static_cast<int>(band);
    return -1;
}

void CrossoverDisplay::paint(juce::Graphics& g) {
    const auto* rack = rackAt(rackPath_);
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
    g.fillRoundedRectangle(bounds, 4.0f);
    if (rack == nullptr)
        return;

    const auto plot = plotArea();
    const auto& crossovers = rack->crossovers;
    const auto bands = crossovers.size() + 1;
    const auto edgeX = [&](std::size_t band, bool upper) {
        if (upper)
            return band < crossovers.size() ? xForFrequency(crossovers[band].frequencyHz)
                                            : plot.getRight();
        return band > 0 ? xForFrequency(crossovers[band - 1].frequencyHz) : plot.getX();
    };

    auto& fonts = FontManager::getInstance();
    const auto& selected = magda::SelectionManager::getInstance().getSelectedChainNode();
    for (std::size_t band = 0; band < bands; ++band) {
        const auto colour = device_shell::chainColour(static_cast<int>(band));
        const auto region = juce::Rectangle<float>::leftTopRightBottom(
            edgeX(band, false), plot.getY(), edgeX(band, true), plot.getBottom());
        const bool isSelected =
            band < rack->chains.size() && selected == rackPath_.withChain(rack->chains[band].id);
        g.setColour(colour.withAlpha(isSelected ? 0.16f : 0.08f));
        g.fillRect(region);
        const auto label = bandLabel(band);
        if (isSelected || static_cast<int>(band) == hoveredBand_) {
            g.setColour(colour.withAlpha(isSelected ? 0.25f : 0.12f));
            g.fillRoundedRectangle(label, 3.0f);
        }
        g.setColour(colour);
        g.setFont(fonts.getMonoFont(10.0f).withExtraKerningFactor(0.08f));
        g.drawText(bandName(static_cast<int>(band), static_cast<int>(bands)).toUpperCase(), label,
                   juce::Justification::centred, false);
    }

    // Frequency grid labels along the bottom.
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
    g.setFont(fonts.getMonoFont(9.0f));
    for (float hz : {50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f}) {
        const auto label = hz >= 1000.0f ? juce::String(juce::roundToInt(hz / 1000.0f)) + "k"
                                         : juce::String(juce::roundToInt(hz));
        g.drawText(label,
                   juce::Rectangle<float>(xForFrequency(hz) - 15.0f, plot.getBottom() - 14.0f,
                                          30.0f, 12.0f),
                   juce::Justification::centred, false);
    }

    const float top = plot.getY() + 32.0f;
    const float bottom = plot.getBottom() - 18.0f;
    for (std::size_t band = 0; band < bands; ++band) {
        juce::Path curve;
        bool started = false;
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f) {
            const float magnitude = bandMagnitude(crossovers, band, frequencyForX(x));
            if (magnitude < 0.002f) {
                started = false;
                continue;
            }
            const float y = bottom - magnitude * (bottom - top);
            if (!started)
                curve.startNewSubPath(x, y);
            else
                curve.lineTo(x, y);
            started = true;
        }
        g.setColour(device_shell::chainColour(static_cast<int>(band)));
        g.strokePath(curve, juce::PathStrokeType(1.5f));
    }

    const auto amber = ActiveTheme::getColour(ActiveTheme::DEVICE_AMBER);
    g.setFont(fonts.getMonoFont(10.5f));
    for (std::size_t i = 0; i < crossovers.size(); ++i) {
        const float x = xForFrequency(crossovers[i].frequencyHz);
        const bool active = static_cast<int>(i) == hovered_ || static_cast<int>(i) == dragging_;
        g.setColour(amber.withAlpha(active ? 1.0f : 0.75f));
        g.fillRect(juce::Rectangle<float>(x - (active ? 1.0f : 0.5f), plot.getY(),
                                          active ? 2.0f : 1.0f, plot.getHeight()));
        const auto text = formatFrequency(crossovers[i].frequencyHz);
        const float width =
            juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), text) + 10.0f;
        const auto label =
            juce::Rectangle<float>(x - width / 2.0f, plot.getY() + 4.0f, width, 18.0f)
                .constrainedWithin(plot);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD));
        g.fillRoundedRectangle(label, 3.0f);
        g.setColour(amber);
        g.drawText(text, label, juce::Justification::centred, false);
    }
}

void CrossoverDisplay::mouseMove(const juce::MouseEvent& e) {
    const int over = crossoverAt(e.position.x);
    const int band = over >= 0 ? -1 : bandLabelAt(e.position);
    if (over != hovered_ || band != hoveredBand_) {
        hovered_ = over;
        hoveredBand_ = band;
        setMouseCursor(over >= 0   ? juce::MouseCursor::LeftRightResizeCursor
                       : band >= 0 ? juce::MouseCursor::PointingHandCursor
                                   : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CrossoverDisplay::mouseExit(const juce::MouseEvent&) {
    if ((hovered_ >= 0 || hoveredBand_ >= 0) && dragging_ < 0) {
        hovered_ = hoveredBand_ = -1;
        repaint();
    }
}

void CrossoverDisplay::mouseDown(const juce::MouseEvent& e) {
    dragging_ = crossoverAt(e.position.x);
    if (dragging_ >= 0) {
        edit_.begin();
        return;
    }
    const int band = bandLabelAt(e.position);
    const auto* rack = rackAt(rackPath_);
    if (band >= 0 && rack != nullptr && band < static_cast<int>(rack->chains.size()))
        magda::SelectionManager::getInstance().selectChainNode(
            rackPath_.withChain(rack->chains[static_cast<std::size_t>(band)].id));
}

void CrossoverDisplay::mouseDrag(const juce::MouseEvent& e) {
    const auto* rack = rackAt(rackPath_);
    if (dragging_ < 0 || rack == nullptr || dragging_ >= static_cast<int>(rack->crossovers.size()))
        return;
    auto crossover = rack->crossovers[static_cast<std::size_t>(dragging_)];
    crossover.frequencyHz = frequencyForX(e.position.x);
    edit_.set(dragging_, crossover);
}

void CrossoverDisplay::mouseUp(const juce::MouseEvent&) {
    if (dragging_ >= 0)
        edit_.end();
    dragging_ = -1;
    repaint();
}

CrossoverDivider::CrossoverDivider(magda::ChainNodePath rackPath, int index)
    : rackPath_(rackPath), index_(index), edit_(std::move(rackPath)) {
    magda::TrackManager::getInstance().addListener(this);
}

CrossoverDivider::~CrossoverDivider() {
    magda::TrackManager::getInstance().removeListener(this);
}

void CrossoverDivider::trackPropertyChanged(int trackId) {
    if (trackId == rackPath_.trackId)
        repaint();
}

std::optional<magda::Crossover> CrossoverDivider::crossover() const {
    const auto* rack = rackAt(rackPath_);
    if (rack == nullptr || index_ >= static_cast<int>(rack->crossovers.size()))
        return std::nullopt;
    return rack->crossovers[static_cast<std::size_t>(index_)];
}

void CrossoverDivider::layoutText() {
    const auto value = crossover();
    if (!value)
        return;
    auto& fonts = FontManager::getInstance();
    const float frequencyWidth = juce::GlyphArrangement::getStringWidth(
        fonts.getMonoFont(11.0f).boldened(), formatFrequency(value->frequencyHz));
    const float slopeWidth = juce::GlyphArrangement::getStringWidth(
        fonts.getMonoFont(11.0f), juce::String(magda::slopeDbPerOctave(value->slope)) + " dB/oct");
    const int total = juce::roundToInt(frequencyWidth + slopeWidth) + 10;
    const int left = getWidth() / 2 - total / 2;
    frequencyArea_ = {left - 2, 0, juce::roundToInt(frequencyWidth) + 4, getHeight()};
    slopeArea_ = {frequencyArea_.getRight() + 6, 0, juce::roundToInt(slopeWidth) + 4, getHeight()};
}

void CrossoverDivider::paint(juce::Graphics& g) {
    const auto value = crossover();
    if (!value)
        return;
    layoutText();
    const int y = getHeight() / 2;
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
    g.fillRect(16, y, juce::jmax(0, frequencyArea_.getX() - 24), 1);
    g.fillRect(slopeArea_.getRight() + 8, y, juce::jmax(0, getWidth() - slopeArea_.getRight() - 24),
               1);

    auto& fonts = FontManager::getInstance();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_AMBER));
    g.setFont(fonts.getMonoFont(11.0f).boldened());
    g.drawText(formatFrequency(value->frequencyHz), frequencyArea_, juce::Justification::centred,
               false);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    g.setFont(fonts.getMonoFont(11.0f));
    g.drawText(juce::String(magda::slopeDbPerOctave(value->slope)) + " dB/oct", slopeArea_,
               juce::Justification::centred, false);
}

void CrossoverDivider::mouseMove(const juce::MouseEvent& e) {
    layoutText();
    setMouseCursor(frequencyArea_.contains(e.getPosition()) ? juce::MouseCursor::UpDownResizeCursor
                   : slopeArea_.contains(e.getPosition())   ? juce::MouseCursor::PointingHandCursor
                                                            : juce::MouseCursor::NormalCursor);
}

void CrossoverDivider::mouseDown(const juce::MouseEvent& e) {
    layoutText();
    const auto value = crossover();
    if (!value)
        return;
    if (slopeArea_.contains(e.getPosition())) {
        showSlopeMenu();
        return;
    }
    if (frequencyArea_.contains(e.getPosition())) {
        scrubbing_ = true;
        dragStartHz_ = value->frequencyHz;
        edit_.begin();
    }
}

// Forty pixels to the octave, up is higher.
void CrossoverDivider::mouseDrag(const juce::MouseEvent& e) {
    const auto value = crossover();
    if (!scrubbing_ || !value)
        return;
    auto moved = *value;
    moved.frequencyHz =
        dragStartHz_ * std::pow(2.0f, static_cast<float>(-e.getDistanceFromDragStartY()) / 40.0f);
    edit_.set(index_, moved);
}

void CrossoverDivider::mouseUp(const juce::MouseEvent&) {
    if (scrubbing_)
        edit_.end();
    scrubbing_ = false;
}

void CrossoverDivider::showSlopeMenu() {
    const auto value = crossover();
    if (!value)
        return;
    juce::PopupMenu menu;
    for (auto slope :
         {magda::CrossoverSlope::Db12, magda::CrossoverSlope::Db24, magda::CrossoverSlope::Db48})
        menu.addItem(static_cast<int>(slope) + 1,
                     juce::String(magda::slopeDbPerOctave(slope)) + " dB/oct", true,
                     slope == value->slope);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(
                           localAreaToGlobal(slopeArea_)),
                       [rackPath = rackPath_, index = index_](int result) {
                           const auto* rack = rackAt(rackPath);
                           if (result <= 0 || rack == nullptr ||
                               index >= static_cast<int>(rack->crossovers.size()))
                               return;
                           auto crossovers = rack->crossovers;
                           crossovers[static_cast<std::size_t>(index)].slope =
                               static_cast<magda::CrossoverSlope>(result - 1);
                           if (crossovers != rack->crossovers)
                               commitCrossovers(rackPath, std::move(crossovers));
                       });
}

}  // namespace magda::daw::ui::multiband
