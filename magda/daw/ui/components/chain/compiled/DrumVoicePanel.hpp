// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "compiled/CompiledPluginPresentation.hpp"
#include "compiled/DrumHitPreview.hpp"
#include "compiled/DrumVoiceLayout.hpp"
#include "core/Config.hpp"

namespace magda::daw::ui {

/**
 * @brief A drum voice's faceplate under its cells: one rendered hit, with each layer's envelope
 *        drawn over it on the same time axis.
 *
 * Drag a layer's peak or end handle to set its attack or decay; scroll near a layer's curve to
 * bend it. The section titles above the cells are painted here, in their layer's colour.
 */
class DrumVoicePanel final : public juce::Component,
                             public CompiledDevicePanel,
                             private magda::ConfigListener {
  public:
    explicit DrumVoicePanel(const juce::String& pluginId);
    ~DrumVoicePanel() override;

    juce::Component& component() override {
        return *this;
    }
    void updateFromDevice(const magda::DeviceInfo& device) override;
    void bindDevice(std::shared_ptr<magda::daw::audio::MagdaDevice> device) override;
    void setOnParameterChanged(std::function<void(int, float)> cb) override {
        onParameterChanged_ = std::move(cb);
    }
    int preferredHeight() const override {
        return 240;
    }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  private:
    enum class Handle { None, Attack, Decay };

    void configChanged() override;
    float value(int slot, float fallback = 0.0f) const;
    void writeParameter(int slot, float value);
    void requestRender();
    /// Long enough for the slowest layer to finish, so every envelope fits the axis.
    float axisMsFor() const;
    juce::Rectangle<float> plotArea() const;
    float xForMs(float ms) const;
    juce::Point<float> handlePoint(const DrumVoiceSection& section, Handle handle) const;
    float envelopeAt(const DrumVoiceSection& section, float ms) const;
    /// Half-height of an envelope's peak, at the rendered hit's peak level.
    float envelopeHeight() const;

    std::vector<DrumVoiceSection> sections_;
    magda::DeviceInfo snapshot_;
    std::shared_ptr<DrumHitPreview::Instrument> instrument_;
    std::function<void(int, float)> onParameterChanged_;
    ParamControlStyle style_ = ParamControlStyle::Text;
    DrumVoiceGeometry geometry_;

    DrumHitPreview preview_;
    DrumHitPreview::Hit hit_;
    std::vector<float> renderedFor_;
    float axisMs_ = 100.0f;

    int dragSection_ = -1;
    Handle dragHandle_ = Handle::None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumVoicePanel)
};

}  // namespace magda::daw::ui
