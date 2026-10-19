#include "compiled/DrumVoicePanel.hpp"

#include <cmath>
#include <limits>

#include "core/GestureRouter.hpp"
#include "plugins/compiled/MagdaCompiledPolyInstrument.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr float kHandleRadius = 4.5f;
constexpr float kHandleGrab = 9.0f;
constexpr float kTimeLabelHeight = 16.0f;
constexpr float kAmplitude = 0.92f;  ///< Full scale sits just inside the plot.

juce::Colour layerColour(size_t index) {
    static constexpr ColourRole kRoles[] = {ActiveTheme::DEVICE_BLUE, ActiveTheme::MIDI_KEY_ON,
                                            ActiveTheme::DEVICE_AMBER, ActiveTheme::DEVICE_GREEN};
    return ActiveTheme::getColour(kRoles[index % std::size(kRoles)]);
}

}  // namespace

DrumVoicePanel::DrumVoicePanel(const juce::String& pluginId)
    : sections_(drumVoiceSections(pluginId)) {
    style_ = resolveControlStyle({});
    preview_.onRendered = [this](const DrumHitPreview::Hit& hit) {
        hit_ = hit;
        repaint();
    };
    magda::Config::getInstance().addListener(this);
}

DrumVoicePanel::~DrumVoicePanel() {
    magda::Config::getInstance().removeListener(this);
}

void DrumVoicePanel::configChanged() {
    if (const auto style = resolveControlStyle(snapshot_.controlStyle); style != style_) {
        style_ = style;
        resized();
        repaint();
    }
}

void DrumVoicePanel::updateFromDevice(const magda::DeviceInfo& device) {
    snapshot_ = device;
    configChanged();
    // Held while a handle is dragged, so the axis does not rescale under the pointer.
    if (dragSection_ < 0)
        axisMs_ = axisMsFor();
    requestRender();
    repaint();
}

void DrumVoicePanel::bindDevice(std::shared_ptr<magda::daw::audio::MagdaDevice> device) {
    auto instrument = std::dynamic_pointer_cast<DrumHitPreview::Instrument>(std::move(device));
    if (instrument == instrument_)
        return;
    instrument_ = std::move(instrument);
    renderedFor_.clear();
    requestRender();
}

float DrumVoicePanel::value(int slot, float fallback) const {
    const auto* param = snapshot_.findParameterByIndex(slot);
    return param != nullptr ? param->currentValue : fallback;
}

void DrumVoicePanel::writeParameter(int slot, float newValue) {
    auto* param = snapshot_.findParameterByIndex(slot);
    if (param == nullptr)
        return;
    newValue = juce::jlimit(param->minValue, param->maxValue, newValue);
    param->currentValue = newValue;
    if (onParameterChanged_)
        onParameterChanged_(slot, newValue);
    requestRender();
    repaint();
}

void DrumVoicePanel::requestRender() {
    if (instrument_ == nullptr)
        return;
    std::vector<float> values;
    for (const auto& param : snapshot_.parameters) {
        if (param.paramIndex < 0)
            continue;
        if (param.paramIndex >= static_cast<int>(values.size()))
            values.resize(static_cast<size_t>(param.paramIndex) + 1, 0.0f);
        values[static_cast<size_t>(param.paramIndex)] = param.currentValue;
    }
    values.push_back(axisMs_);  // the length is part of what was rendered
    if (values == renderedFor_)
        return;
    renderedFor_ = values;
    values.pop_back();
    preview_.request(instrument_, std::move(values), axisMs_);
}

float DrumVoicePanel::axisMsFor() const {
    float longest = 0.0f;
    for (const auto& section : sections_)
        if (section.decaySlot >= 0)
            longest = std::max(longest, value(section.attackSlot) + value(section.decaySlot));
    return juce::jlimit(20.0f, 4000.0f, longest > 0.0f ? longest * 1.15f : 200.0f);
}

juce::Rectangle<float> DrumVoicePanel::plotArea() const {
    return geometry_.plot.toFloat().withTrimmedBottom(kTimeLabelHeight).reduced(6.0f, 4.0f);
}

float DrumVoicePanel::xForMs(float ms) const {
    const auto plot = plotArea();
    return plot.getX() + plot.getWidth() * juce::jlimit(0.0f, 1.0f, ms / axisMs_);
}

float DrumVoicePanel::envelopeAt(const DrumVoiceSection& section, float ms) const {
    // Linear attack to the peak, then the dsp's pow(1 - progress, 8^(-curve/50)) decay.
    const float attack = section.attackSlot >= 0 ? value(section.attackSlot) : 0.0f;
    const float decay = std::max(0.001f, value(section.decaySlot));
    if (ms < attack)
        return attack > 0.0f ? ms / attack : 1.0f;
    const float progress = (ms - attack) / decay;
    if (progress >= 1.0f)
        return 0.0f;
    const float curve = section.curveSlot >= 0 ? value(section.curveSlot) : 0.0f;
    return std::pow(1.0f - progress, std::pow(8.0f, -curve / 50.0f));
}

float DrumVoicePanel::envelopeHeight() const {
    // An envelope peaks where the hit does, so it traces the waveform's outline.
    float peak = 0.0f;
    for (const float sample : hit_.samples)
        peak = std::max(peak, std::abs(sample));
    return plotArea().getHeight() * 0.5f * kAmplitude * juce::jlimit(0.25f, 1.0f, peak);
}

juce::Point<float> DrumVoicePanel::handlePoint(const DrumVoiceSection& section,
                                               Handle handle) const {
    const auto plot = plotArea();
    const float attack = section.attackSlot >= 0 ? value(section.attackSlot) : 0.0f;
    const float halfHeight = envelopeHeight();
    if (handle == Handle::Attack)
        return {xForMs(attack), plot.getCentreY() - halfHeight};
    return {xForMs(attack + value(section.decaySlot)), plot.getCentreY()};
}

void DrumVoicePanel::paint(juce::Graphics& g) {
    auto& fonts = FontManager::getInstance();

    // Layer titles over their cell columns, in the colour their envelope is drawn in.
    g.setFont(fonts.getMonoFont(10.0f).withExtraKerningFactor(0.12f));
    for (size_t i = 0; i < sections_.size() && i < geometry_.titles.size(); ++i) {
        const bool layer = sections_[i].decaySlot >= 0;
        g.setColour(layer ? layerColour(i) : ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
        g.drawText(juce::String(sections_[i].title).toUpperCase(), geometry_.titles[i],
                   juce::Justification::centredLeft, false);
    }

    if (geometry_.plot.isEmpty())
        return;
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
    g.fillRoundedRectangle(geometry_.plot.toFloat(), 4.0f);

    const auto plot = plotArea();
    const float centre = plot.getCentreY();
    const float halfHeight = plot.getHeight() * 0.5f * kAmplitude;

    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_GRID));
    for (const float t : {0.25f, 0.5f, 0.75f})
        g.drawVerticalLine(juce::roundToInt(plot.getX() + t * plot.getWidth()), plot.getY(),
                           plot.getBottom());
    g.drawHorizontalLine(juce::roundToInt(centre), plot.getX(), plot.getRight());

    // The hit: per pixel column, its min-to-max body and a brighter RMS core.
    const auto& samples = hit_.samples;
    if (!samples.empty() && hit_.lengthMs > 0.0f) {
        const float samplesPerMs = static_cast<float>(samples.size()) / hit_.lengthMs;
        const auto text = ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT);
        const int columns = juce::roundToInt(plot.getWidth());
        for (int x = 0; x < columns; ++x) {
            const float msFrom = axisMs_ * static_cast<float>(x) / static_cast<float>(columns);
            const float msTo = axisMs_ * static_cast<float>(x + 1) / static_cast<float>(columns);
            const auto from = static_cast<size_t>(msFrom * samplesPerMs);
            const auto to = std::min(samples.size(), static_cast<size_t>(msTo * samplesPerMs) + 1);
            if (from >= to)
                continue;
            float low = 0.0f, high = 0.0f, power = 0.0f;
            for (size_t i = from; i < to; ++i) {
                low = std::min(low, samples[i]);
                high = std::max(high, samples[i]);
                power += samples[i] * samples[i];
            }
            const float rms = std::sqrt(power / static_cast<float>(to - from));
            const float px = plot.getX() + static_cast<float>(x);
            const auto y = [&](float v) {
                return centre - juce::jlimit(-1.0f, 1.0f, v) * halfHeight;
            };
            g.setColour(text.withAlpha(0.22f));
            g.drawVerticalLine(static_cast<int>(px), y(high), y(low) + 1.0f);
            g.setColour(text.withAlpha(0.45f));
            g.drawVerticalLine(static_cast<int>(px), y(rms), y(-rms) + 1.0f);
        }
    }

    // Each layer's envelope traces the hit's upper contour, with its handles.
    const float envelopeHalfHeight = envelopeHeight();
    for (size_t i = 0; i < sections_.size(); ++i) {
        const auto& section = sections_[i];
        if (section.decaySlot < 0)
            continue;
        const auto colour = layerColour(i);
        juce::Path path;
        constexpr int kSteps = 96;
        for (int step = 0; step <= kSteps; ++step) {
            const float ms = axisMs_ * static_cast<float>(step) / kSteps;
            const juce::Point<float> point{xForMs(ms),
                                           centre - envelopeAt(section, ms) * envelopeHalfHeight};
            if (step == 0)
                path.startNewSubPath(point);
            else
                path.lineTo(point);
        }
        g.setColour(colour.withAlpha(0.9f));
        g.strokePath(path, juce::PathStrokeType(1.5f));

        for (const auto handle : {Handle::Attack, Handle::Decay}) {
            if (handle == Handle::Attack && section.attackSlot < 0)
                continue;
            const bool active = dragSection_ == static_cast<int>(i) && dragHandle_ == handle;
            const float radius = active ? kHandleRadius + 1.5f : kHandleRadius;
            const auto point = handlePoint(section, handle);
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
            g.fillEllipse(juce::Rectangle<float>(radius * 2, radius * 2).withCentre(point));
            g.setColour(colour);
            g.drawEllipse(juce::Rectangle<float>(radius * 2, radius * 2).withCentre(point), 1.5f);
        }
    }

    g.setFont(fonts.getMonoFont(9.0f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
    const auto labels = geometry_.plot.toFloat().removeFromBottom(kTimeLabelHeight).reduced(8, 0);
    const auto ms = [](float v) {
        return v >= 1000.0f ? juce::String(v / 1000.0f, 2) + " s"
                            : juce::String(juce::roundToInt(v)) + " ms";
    };
    g.drawText("0", labels, juce::Justification::centredLeft, false);
    g.drawText(ms(axisMs_ * 0.5f), labels, juce::Justification::centred, false);
    g.drawText(ms(axisMs_), labels, juce::Justification::centredRight, false);
}

void DrumVoicePanel::resized() {
    geometry_ = DrumVoiceGeometry::of(getLocalBounds(), style_, sections_);
}

void DrumVoicePanel::mouseDown(const juce::MouseEvent& e) {
    dragSection_ = -1;
    dragHandle_ = Handle::None;
    for (size_t i = 0; i < sections_.size(); ++i) {
        const auto& section = sections_[i];
        if (section.decaySlot < 0)
            continue;
        for (const auto handle : {Handle::Attack, Handle::Decay}) {
            if (handle == Handle::Attack && section.attackSlot < 0)
                continue;
            if (e.position.getDistanceFrom(handlePoint(section, handle)) <= kHandleGrab) {
                dragSection_ = static_cast<int>(i);
                dragHandle_ = handle;
                repaint();
                return;
            }
        }
    }
}

void DrumVoicePanel::mouseDrag(const juce::MouseEvent& e) {
    if (dragSection_ < 0)
        return;
    const auto& section = sections_[static_cast<size_t>(dragSection_)];
    const auto plot = plotArea();
    const float ms =
        juce::jlimit(0.0f, axisMs_, (e.position.x - plot.getX()) / plot.getWidth() * axisMs_);
    if (dragHandle_ == Handle::Attack)
        writeParameter(section.attackSlot, ms);
    else
        writeParameter(section.decaySlot,
                       ms - (section.attackSlot >= 0 ? value(section.attackSlot) : 0.0f));
}

void DrumVoicePanel::mouseUp(const juce::MouseEvent&) {
    if (dragSection_ < 0)
        return;
    dragSection_ = -1;
    dragHandle_ = Handle::None;
    axisMs_ = axisMsFor();
    requestRender();
    repaint();
}

void DrumVoicePanel::mouseWheelMove(const juce::MouseEvent& e,
                                    const juce::MouseWheelDetails& wheel) {
    const auto gesture = magda::GestureRouter::getInstance().resolve(
        magda::GestureContext::CurveEditor, wheel, e.mods, e.getPosition());
    const auto plot = plotArea();
    if (!magda::isValueAdjustmentAction(gesture.type) || !plot.contains(e.position)) {
        juce::Component::mouseWheelMove(e, wheel);
        return;
    }

    // The layer whose curve passes nearest the pointer bends.
    const float ms = (e.position.x - plot.getX()) / plot.getWidth() * axisMs_;
    const float halfHeight = envelopeHeight();
    const DrumVoiceSection* nearest = nullptr;
    float best = std::numeric_limits<float>::max();
    for (const auto& section : sections_) {
        if (section.decaySlot < 0 || section.curveSlot < 0)
            continue;
        const float y = plot.getCentreY() - envelopeAt(section, ms) * halfHeight;
        if (const float distance = std::abs(y - e.position.y); distance < best) {
            best = distance;
            nearest = &section;
        }
    }
    if (nearest != nullptr)
        writeParameter(nearest->curveSlot, value(nearest->curveSlot) + gesture.magnitude * 60.0f);
}

namespace {

template <size_t Voice> const CompiledPresentationSpec& drumVoicePresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = kDrumVoicePluginIds[Voice],
        .layoutCellCount = 0,
        .layoutCellsPerRow = 0,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<DrumVoicePanel>(pluginId);
        },
        // The plot at its minimum beside the cell columns, plus the side strip.
        .preferredSlotWidth =
            DrumVoiceGeometry::kMinPlotWidth + 64 +
            DrumVoiceGeometry::columnsWidth(drumVoiceSections(kDrumVoicePluginIds[Voice])),
        .createLayout = []() -> std::unique_ptr<DeviceParamLayout> {
            return std::make_unique<DrumVoiceLayout>(kDrumVoicePluginIds[Voice]);
        },
    };
    return kSpec;
}

}  // namespace

const CompiledPresentationSpec& getMagdaKickPresentation() {
    return drumVoicePresentation<0>();
}
const CompiledPresentationSpec& getMagdaSnarePresentation() {
    return drumVoicePresentation<1>();
}
const CompiledPresentationSpec& getMagdaClapPresentation() {
    return drumVoicePresentation<2>();
}
const CompiledPresentationSpec& getMagdaHatPresentation() {
    return drumVoicePresentation<3>();
}
const CompiledPresentationSpec& getMagdaTomPresentation() {
    return drumVoicePresentation<4>();
}

}  // namespace magda::daw::ui
