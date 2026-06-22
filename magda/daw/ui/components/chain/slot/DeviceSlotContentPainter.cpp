#include "slot/DeviceSlotContentPainter.hpp"

#include "drum_grid/DeviceSlotDrumGridBridge.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

bool skipsContentHeader(const DeviceSlotContentPaintState& state) {
    // Analysis devices (oscilloscope / spectrum) have no need for the
    // "manufacturer / name" subheader — the main header already names them.
    return state.traits.isAnalysis || state.traits.isFaust ||
           (state.traits.compiledPresentation != nullptr &&
            state.traits.compiledPresentation->layoutCellCount == 0);
}

void paintSeparators(juce::Graphics& g, juce::Rectangle<int> contentArea,
                     const DeviceSlotContentPaintState& state, int meterStripWidth,
                     int contentHeaderHeight, int paginationHeight, int faustHeaderHeight) {
    if (state.collapsed)
        return;

    const bool skipContentHeader = skipsContentHeader(state);
    // Vertical separator before the meter strip — skip it when there's no strip
    // (meterStripWidth <= 0, e.g. post-FX analysis devices).
    if (meterStripWidth > 0) {
        const int lineX = contentArea.getRight() - meterStripWidth - 4;
        const int meterTop = contentArea.getY() + (skipContentHeader ? 0 : contentHeaderHeight);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawVerticalLine(lineX, static_cast<float>(meterTop + 2),
                           static_cast<float>(contentArea.getBottom() - 2));
    }

    const float left = static_cast<float>(contentArea.getX() + 2);
    const float right = static_cast<float>(contentArea.getRight() - 2);
    const int headerBottom = contentArea.getY() + contentHeaderHeight;
    if (!skipContentHeader) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawHorizontalLine(headerBottom, left, right);
    }

    if (!state.traits.compiledPresentation &&
        (!state.internalDevice || state.traits.isFaust || !state.hasCustomUI)) {
        constexpr int paginationTopPadding = 2;
        constexpr int paginationBottomPadding = 4;
        const int paramGridTop =
            contentArea.getY() + (state.traits.isFaust ? faustHeaderHeight : contentHeaderHeight);
        const int paginationBottom =
            paramGridTop + paginationTopPadding + paginationHeight + paginationBottomPadding;
        g.drawHorizontalLine(paginationBottom, left, right);
    }
}

bool paintLoadState(juce::Graphics& g, juce::Rectangle<int> contentArea,
                    magda::DeviceLoadState loadState) {
    if (loadState == magda::DeviceLoadState::Loading) {
        g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.6f));
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.drawText("Loading...", contentArea, juce::Justification::centred);
        return true;
    }

    if (loadState == magda::DeviceLoadState::Failed) {
        g.setColour(juce::Colours::red.withAlpha(0.7f));
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.drawText("Failed to load", contentArea, juce::Justification::centred);
        return true;
    }

    return false;
}

void paintMidiUtilityHeader(juce::Graphics& g, juce::Rectangle<int> headerArea,
                            juce::Rectangle<int> textArea,
                            const DeviceSlotContentPaintState& state) {
    const auto textColour = state.bypassed ? DarkTheme::getSecondaryTextColour().withAlpha(0.5f)
                                           : DarkTheme::getSecondaryTextColour();
    g.setColour(textColour);

    if ((state.traits.isStepSequencer || state.traits.isPolyStepSequencer) &&
        state.stepRecording.active) {
        const int maxSteps = juce::jmax(1, state.stepRecording.maxSteps);
        const int displayPosition = juce::jlimit(0, maxSteps - 1, state.stepRecording.position);
        g.saveState();
        g.setColour(juce::Colour(0xFFCC3333).withAlpha(0.9f));
        g.fillRect(headerArea);
        g.setColour(juce::Colours::white);
        g.setFont(FontManager::getInstance().getMicrogrammaFont(9.0f));
        g.drawText("STEP RECORDING  " + juce::String(displayPosition + 1) + "/" +
                       juce::String(maxSteps),
                   textArea, juce::Justification::centredLeft);
        g.restoreState();
        return;
    }

    g.setFont(FontManager::getInstance().getMicrogrammaFont(9.0f));
    const juce::String label = state.traits.isChordEngine         ? "MAGDA Chord Engine"
                               : state.traits.isArpeggiator       ? "MAGDA Arpeggiator"
                               : state.traits.isPolyStepSequencer ? "MAGDA Poly Sequencer"
                                                                  : "MAGDA Step Sequencer";
    g.drawText(label, textArea, juce::Justification::centredLeft);
}

void paintTracktionHeader(juce::Graphics& g, juce::Rectangle<int> textArea,
                          const DeviceSlotContentPaintState& state) {
    const auto textColour = state.bypassed ? DarkTheme::getSecondaryTextColour().withAlpha(0.5f)
                                           : DarkTheme::getSecondaryTextColour();
    g.setColour(textColour);

    constexpr int logoSize = 14;
    auto logoBounds = textArea.removeFromLeft(logoSize).toFloat();
    logoBounds = logoBounds.withSizeKeepingCentre(logoSize, logoSize);
    state.tracktionLogo->drawWithin(g, logoBounds, juce::RectanglePlacement::centred,
                                    state.bypassed ? 0.3f : 0.6f);
    textArea.removeFromLeft(4);
    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    g.drawText("Tracktion / " + state.deviceName, textArea, juce::Justification::centredLeft);
}

void paintExternalHeader(juce::Graphics& g, juce::Rectangle<int> textArea,
                         const DeviceSlotContentPaintState& state) {
    const auto textColour = state.bypassed ? DarkTheme::getSecondaryTextColour().withAlpha(0.5f)
                                           : DarkTheme::getSecondaryTextColour();
    g.setColour(textColour);
    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    g.drawText(state.manufacturer + " / " + state.deviceName, textArea,
               juce::Justification::centredLeft);
}

}  // namespace

void paintDeviceSlotContent(juce::Graphics& g, juce::Rectangle<int> contentArea,
                            const DeviceSlotContentPaintState& state, int meterStripWidth,
                            int contentHeaderHeight, int paginationHeight, int faustHeaderHeight) {
    paintSeparators(g, contentArea, state, meterStripWidth, contentHeaderHeight, paginationHeight,
                    faustHeaderHeight);

    if (paintLoadState(g, contentArea, state.loadState))
        return;

    if (skipsContentHeader(state))
        return;

    auto headerArea = contentArea.removeFromTop(contentHeaderHeight);
    auto textArea = headerArea.withTrimmedLeft(6).withTrimmedRight(2);

    if (drum_grid_slot::paintContentHeader(g, state.traits.isDrumGrid, state.bypassed, textArea))
        return;

    if (state.traits.isChordEngine || state.traits.isArpeggiator || state.traits.isStepSequencer ||
        state.traits.isPolyStepSequencer) {
        paintMidiUtilityHeader(g, headerArea, textArea, state);
    } else if (state.traits.isTracktionDevice && state.tracktionLogo != nullptr) {
        paintTracktionHeader(g, textArea, state);
    } else {
        paintExternalHeader(g, textArea, state);
    }
}

}  // namespace magda::daw::ui
