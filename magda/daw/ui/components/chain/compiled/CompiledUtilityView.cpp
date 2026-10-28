// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compiled/CompiledUtilityView.hpp"

#include <cmath>

#include "compiled/CompiledUtilityLayout.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kEdgeGrabPx = 5.0f;
constexpr float kLabelRowPx = 14.0f;

juce::Rectangle<float> plotOf(juce::Rectangle<int> field) {
    return field.toFloat().reduced(8.0f, 6.0f).withTrimmedBottom(kLabelRowPx);
}

}  // namespace

CompiledUtilityView::CompiledUtilityView(juce::String /*pluginId*/) {
    static constexpr const char* kLabels[] = {"Mono", "Low mono", "Swap", "\xc3\x98 L",
                                              "\xc3\x98 R"};
    for (size_t i = 0; i < toggles_.size(); ++i) {
        auto& toggle = toggles_[i];
        toggle.setToggle(juce::String(juce::CharPointer_UTF8(kLabels[i])));
        toggle.setSelectedIndex(0);
        toggle.onChange = [this, slot = kToggleSlots[i]](int index) {
            writeParameter(slot, index == 1 ? 1.0f : 0.0f);
        };
        addAndMakeVisible(toggle);
    }
    style_ = resolveControlStyle(deviceSnapshot_.controlStyle);
    magda::Config::getInstance().addListener(this);
}

CompiledUtilityView::~CompiledUtilityView() {
    magda::Config::getInstance().removeListener(this);
}

void CompiledUtilityView::configChanged() {
    syncControlStyle();
}

void CompiledUtilityView::syncControlStyle() {
    if (const auto style = resolveControlStyle(deviceSnapshot_.controlStyle); style != style_) {
        style_ = style;
        resized();
        repaint();
    }
}

void CompiledUtilityView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    for (size_t i = 0; i < toggles_.size(); ++i)
        toggles_[i].setSelectedIndex(isOn(kToggleSlots[i]) ? 1 : 0);

    syncControlStyle();
    repaint();
}

void CompiledUtilityView::bindDevice(std::shared_ptr<magda::daw::audio::MagdaDevice> device) {
    compiledPlugin_ = std::dynamic_pointer_cast<Plugin>(std::move(device));
}

float CompiledUtilityView::value(int slot, float fallback) const {
    const auto* param = deviceSnapshot_.findParameterByIndex(slot);
    return param != nullptr ? param->currentValue : fallback;
}

bool CompiledUtilityView::isOn(int slot) const {
    return value(slot, 0.0f) >= 0.5f;
}

void CompiledUtilityView::writeParameter(int slot, float newValue) {
    // Through the model, so automation, undo and macro links see the edit (#2192).
    if (auto* param = deviceSnapshot_.findParameterByIndex(slot))
        param->currentValue = newValue;
    if (onParameterChanged_)
        onParameterChanged_(slot, newValue);
    repaint();
}

juce::Range<float> CompiledUtilityView::bandRange() const {
    const float size = isOn(Plugin::kMonoSlot)
                           ? 0.0f
                           : juce::jlimit(0.0f, 1.0f, value(Plugin::kWidthSlot, 100.0f) / 200.0f);
    const float pan = juce::jlimit(-1.0f, 1.0f, value(Plugin::kPanSlot, 0.0f));
    const float centre = 0.5f + 0.5f * pan * (1.0f - size);
    return {centre - size * 0.5f, centre + size * 0.5f};
}

CompiledUtilityView::Drag CompiledUtilityView::dragFor(juce::Point<float> position) const {
    if (!geometry_.field.contains(position.toInt()))
        return Drag::None;
    const auto plot = plotOf(geometry_.field);
    const auto band = bandRange();
    const float left = plot.getX() + band.getStart() * plot.getWidth();
    const float right = plot.getX() + band.getEnd() * plot.getWidth();
    const bool nearEdge =
        std::abs(position.x - left) <= kEdgeGrabPx || std::abs(position.x - right) <= kEdgeGrabPx;
    return nearEdge && !isOn(Plugin::kMonoSlot) ? Drag::Width : Drag::Pan;
}

void CompiledUtilityView::paint(juce::Graphics& g) {
    if (geometry_.field.isEmpty())
        return;

    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
    g.fillRoundedRectangle(geometry_.field.toFloat(), 4.0f);

    const auto plot = plotOf(geometry_.field);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_GRID));
    for (const float t : {0.25f, 0.5f, 0.75f}) {
        const float x = plot.getX() + t * plot.getWidth();
        g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
    }

    // Swapped, the picture says so: what was on the left now plays on the right.
    const bool swapped = isOn(Plugin::kSwapSlot);
    auto labels = geometry_.field.toFloat().reduced(8.0f, 4.0f).removeFromBottom(kLabelRowPx);
    g.setFont(FontManager::getInstance().getMonoFont(9.0f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
    g.drawText(swapped ? "R" : "L", labels, juce::Justification::centredLeft, false);
    g.drawText("C", labels, juce::Justification::centred, false);
    g.drawText(swapped ? "L" : "R", labels, juce::Justification::centredRight, false);

    const auto accent = ActiveTheme::getColour(ActiveTheme::ACCENT_POSITIVE);
    const auto range = bandRange();
    const float left = plot.getX() + range.getStart() * plot.getWidth();
    const float right = plot.getX() + range.getEnd() * plot.getWidth();
    const bool active = drag_ != Drag::None;
    if (right - left < 2.0f) {
        g.setColour(accent);
        g.fillRect(juce::Rectangle<float>(left - 1.0f, plot.getY(), 2.0f, plot.getHeight()));
    } else {
        const auto band = juce::Rectangle<float>(left, plot.getY(), right - left, plot.getHeight());
        g.setColour(accent.withAlpha(active ? 0.28f : 0.18f));
        g.fillRoundedRectangle(band, 3.0f);
        g.setColour(accent.withAlpha(0.9f));
        g.drawRoundedRectangle(band, 3.0f, 1.2f);
    }

    // Low mono folds the lows to the band's centre; the tick's height reads the crossover.
    if (isOn(Plugin::kLowMonoSlot) && !isOn(Plugin::kMonoSlot)) {
        const float freq = juce::jlimit(20.0f, 500.0f, value(Plugin::kLowMonoFreqSlot, 120.0f));
        const float reach = 0.25f + 0.75f * std::log(freq / 20.0f) / std::log(500.0f / 20.0f);
        const float centre = 0.5f * (left + right);
        const float height = plot.getHeight() * reach;
        g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION));
        g.fillRect(juce::Rectangle<float>(centre - 1.5f, plot.getBottom() - height, 3.0f, height));
    }
}

void CompiledUtilityView::resized() {
    geometry_ = UtilityGeometry::of(getLocalBounds(), style_);
    for (size_t i = 0; i < toggles_.size(); ++i)
        toggles_[i].setBounds(geometry_.toggles[i]);
}

void CompiledUtilityView::mouseMove(const juce::MouseEvent& e) {
    const auto drag = dragFor(e.position);
    setMouseCursor(drag == Drag::Width ? juce::MouseCursor::LeftRightResizeCursor
                   : drag == Drag::Pan ? juce::MouseCursor::DraggingHandCursor
                                       : juce::MouseCursor::NormalCursor);
}

void CompiledUtilityView::mouseDown(const juce::MouseEvent& e) {
    drag_ = onParameterChanged_ != nullptr ? dragFor(e.position) : Drag::None;
    dragStartPan_ = value(Plugin::kPanSlot, 0.0f);
    repaint();
}

void CompiledUtilityView::mouseDrag(const juce::MouseEvent& e) {
    const auto plot = plotOf(geometry_.field);
    if (drag_ == Drag::None || plot.getWidth() <= 0.0f)
        return;

    if (drag_ == Drag::Pan) {
        // Relative, so the band doesn't jump to the pointer and a full-width band still pans.
        const float delta =
            static_cast<float>(e.getDistanceFromDragStartX()) / (0.5f * plot.getWidth());
        writeParameter(Plugin::kPanSlot, juce::jlimit(-1.0f, 1.0f, dragStartPan_ + delta));
        return;
    }

    const auto range = bandRange();
    const float centre = plot.getX() + range.getStart() * plot.getWidth() +
                         0.5f * range.getLength() * plot.getWidth();
    const float halfSize = std::abs(e.position.x - centre) / plot.getWidth();
    writeParameter(Plugin::kWidthSlot, juce::jlimit(0.0f, 200.0f, halfSize * 2.0f * 200.0f));
}

void CompiledUtilityView::mouseUp(const juce::MouseEvent&) {
    drag_ = Drag::None;
    repaint();
}

void CompiledUtilityView::mouseDoubleClick(const juce::MouseEvent& e) {
    if (dragFor(e.position) == Drag::None || onParameterChanged_ == nullptr)
        return;
    for (const int slot : {Plugin::kPanSlot, Plugin::kWidthSlot})
        if (const auto* param = deviceSnapshot_.findParameterByIndex(slot))
            writeParameter(slot, param->defaultValue);
}

const CompiledPresentationSpec& getMagdaUtilityPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaUtilityCompiledPlugin::xmlTypeName,
        .layoutCellCount = 0,
        .layoutCellsPerRow = 0,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledUtilityView>(pluginId);
        },
        .preferredSlotWidth = 270,
        .ownsOutputGain = true,
        .createLayout = []() -> std::unique_ptr<DeviceParamLayout> {
            return std::make_unique<CompiledUtilityLayout>();
        },
    };
    return kSpec;
}

}  // namespace magda::daw::ui
