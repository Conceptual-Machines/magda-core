#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <vector>

#include "plugins/sampler/SamplerZones.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief The sampler's zones on a key × velocity grid, with the selected zone's settings above
 *        it (#3098).
 *
 * A zone's edges drag its key and velocity ranges and its body moves it; an edit is handed out
 * when the drag ends. The fields edit the selected zone as they change.
 */
class SamplerZoneEditor : public juce::Component {
  public:
    using Zone = daw::audio::SamplerZoneSettings;

    SamplerZoneEditor();

    void setZones(const std::vector<Zone>& zones);
    void setSelected(int index);
    int selected() const {
        return selected_;
    }

    /// The key under @p x, in this component's coordinates.
    int keyAt(float x) const;
    bool gridContains(juce::Point<int> position) const {
        return grid_.contains(position);
    }

    std::function<void(int index)> onSelect;
    std::function<void(int index, const Zone& zone)> onEdit;
    std::function<void(int index)> onRemove;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

  private:
    enum class Grab { None, Body, LowKey, HighKey, LowVelocity, HighVelocity };

    juce::Rectangle<float> zoneBounds(const Zone& zone) const;
    float xForKey(int key) const;
    float yForVelocity(int velocity) const;
    int velocityAt(float y) const;
    /// The zone and its part under @p position, the selected zone first.
    std::pair<int, Grab> hit(juce::Point<float> position) const;
    void edit(const Zone& zone);
    void refreshFields();
    void showZoneMenu(int index);

    std::vector<Zone> zones_;
    int selected_ = -1;
    Grab grab_ = Grab::None;
    Zone dragStart_;
    juce::Point<float> dragOrigin_;

    juce::Rectangle<int> grid_, keyboard_;
    std::vector<juce::Rectangle<int>> tiles_;

    // Root, key range, velocity range, tune and gain.
    enum Field { kRoot, kLowKey, kHighKey, kLowVelocity, kHighVelocity, kTune, kGain };
    static constexpr int kFieldCount = 7;
    std::array<TextSlider, kFieldCount> fields_;
    std::array<juce::Label, kFieldCount> labels_;
    juce::Label fileLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerZoneEditor)
};

}  // namespace magda::daw::ui
