// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace magda {

/**
 * @brief The MAGDA Lisa mascot, as the splash and the About box show her.
 *
 * Her portrait sits in a soft light with its edge faded into the panel, and the
 * headphone ring pulses on the beat in the theme's accent. Only the ring
 * repaints, between the two cached layers so the soft edge still falls over it.
 * The beat follows the vblank clock, so it stays in step after the message
 * thread has been held up by startup work.
 */
class Mascot : public juce::Component {
  public:
    Mascot();

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    /** @brief One layer of the mascot, painted by its owner and cached when it is still. */
    class Layer : public juce::Component {
      public:
        Layer(std::function<void(juce::Graphics&)> paintLayer, bool still);
        void paint(juce::Graphics& g) override {
            paintLayer_(g);
        }

      private:
        std::function<void(juce::Graphics&)> paintLayer_;
    };

    juce::Path placed(juce::Path shape) const;
    void paintArt(juce::Graphics& g) const;
    void paintRing(juce::Graphics& g) const;
    void paintEdge(juce::Graphics& g) const;
    void pulse(double seconds);

    std::unique_ptr<juce::Drawable> art_;
    juce::Path silhouette_;
    juce::Path ring_;

    juce::AffineTransform placement_;
    juce::Path placedSilhouette_;
    juce::Path placedRing_;
    juce::Path edge_;
    int edgeBlur_ = 1;
    int ringGlow_ = 1;
    juce::Rectangle<int> ringArea_;
    float level_ = 0.0f;

    Layer artLayer_{[this](juce::Graphics& g) { paintArt(g); }, true};
    Layer ringLayer_{[this](juce::Graphics& g) { paintRing(g); }, false};
    Layer edgeLayer_{[this](juce::Graphics& g) { paintEdge(g); }, true};
    juce::VBlankAttachment vblank_{this, [this](double seconds) { pulse(seconds); }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Mascot)
};

}  // namespace magda
