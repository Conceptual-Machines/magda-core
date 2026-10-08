#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

#include "core/ChainNodePath.hpp"
#include "core/Crossover.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui::multiband {

/** @brief "Low", "Mid", "High" and the like for band @p band of @p bands, low to high. */
juce::String bandName(int band, int bands);
/** @brief "180 Hz", "3.2 kHz". */
juce::String formatFrequency(float hz);

/** @brief A crossover drag: moves the model live and leaves one undo step at the end. */
class CrossoverEdit {
  public:
    explicit CrossoverEdit(magda::ChainNodePath rackPath) : rackPath_(std::move(rackPath)) {}

    void begin();
    void set(int index, magda::Crossover crossover);
    void end();

  private:
    magda::ChainNodePath rackPath_;
    std::optional<std::vector<magda::Crossover>> before_;
};

/**
 * @brief The multiband rack's faceplate: band regions, each band's response in its colour, and
 * the crossovers as amber lines that drag along the frequency axis.
 */
class CrossoverDisplay : public juce::Component, private magda::TrackManagerListener {
  public:
    explicit CrossoverDisplay(magda::ChainNodePath rackPath);
    ~CrossoverDisplay() override;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

  private:
    void tracksChanged() override {}
    void trackPropertyChanged(int trackId) override;

    juce::Rectangle<float> plotArea() const;
    float xForFrequency(float hz) const;
    float frequencyForX(float x) const;
    int crossoverAt(float x) const;

    magda::ChainNodePath rackPath_;
    CrossoverEdit edit_;
    int hovered_ = -1;
    int dragging_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrossoverDisplay)
};

/** @brief The rule between two band rows: the crossover's frequency, scrubbed, and its slope. */
class CrossoverDivider : public juce::Component, private magda::TrackManagerListener {
  public:
    static constexpr int kHeight = 22;

    CrossoverDivider(magda::ChainNodePath rackPath, int index);
    ~CrossoverDivider() override;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

  private:
    void tracksChanged() override {}
    void trackPropertyChanged(int trackId) override;

    std::optional<magda::Crossover> crossover() const;
    void layoutText();
    void showSlopeMenu();

    magda::ChainNodePath rackPath_;
    int index_;
    CrossoverEdit edit_;
    juce::Rectangle<int> frequencyArea_, slopeArea_;
    float dragStartHz_ = 0.0f;
    bool scrubbing_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrossoverDivider)
};

}  // namespace magda::daw::ui::multiband
