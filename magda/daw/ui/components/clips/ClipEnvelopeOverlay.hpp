// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <optional>

#include "core/ClipSourceMap.hpp"
#include "ui/components/waveform/GainEnvelopeEditor.hpp"

namespace magda {

class TrackContentPanel;

/**
 * @brief An arrangement clip's gain envelope, drawn and edited on its body (#3064).
 *
 * x is clip-local beats. Each pixel reads its source second through ClipSourceMap, so a point
 * shows once in every loop pass and sits on the sound through reverse and warp.
 */
class ClipEnvelopeOverlay : private daw::ui::GainEnvelopeHost {
  public:
    struct Hooks {
        std::function<bool(juce::Point<int>)> clipKeepsPress;  // in the clip's own pixels
        std::function<void()> pressed;
        std::function<void()> showContextMenu;
    };

    ClipEnvelopeOverlay(ClipId clipId, juce::Component& clip, TrackContentPanel* panel,
                        Hooks hooks);
    ~ClipEnvelopeOverlay() override;

    /// Over @p body, in the clip's pixels; re-reads the clip and its points.
    void layout(juce::Rectangle<int> body);

  private:
    double pixelsPerX() const override;
    double xToPixel(double x) const override;
    double pixelToX(double px) const override;
    double sourceAt(double x) const override;
    std::vector<double> xsOf(double sourceSeconds) const override;
    std::pair<double, double> xRange() const override;
    bool snapsToGrid() const override;
    double snapToGrid(double x) const override;
    double gridSpacing() const override;
    bool keepsPress(int x, int y) const override;
    void envelopePressed() override;
    bool showContextMenu() override;
    bool showsLevelGuides() const override {
        return false;
    }

    const ClipInfo* clip() const;

    ClipId clipId_;
    juce::Component& clipComponent_;
    TrackContentPanel* panel_;
    Hooks hooks_;
    std::optional<ClipSourceMap> map_;
    double lengthBeats_ = 0.0;
    std::unique_ptr<daw::ui::GainEnvelopeEditor> editor_;
};

}  // namespace magda
