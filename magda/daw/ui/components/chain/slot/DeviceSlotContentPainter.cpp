#include "slot/DeviceSlotContentPainter.hpp"

#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

bool skipsContentHeader(const DeviceSlotContentPaintState& state) {
    // Analysis devices (oscilloscope / spectrum) have no need for the
    // "manufacturer / name" subheader — the main header already names them.
    return state.traits.isAnalysis || state.traits.isFaust || state.traits.isFaustInstrument ||
           (state.traits.compiledPresentation != nullptr &&
            state.traits.compiledPresentation->layoutCellCount == 0);
}

void paintSeparators(juce::Graphics& g, juce::Rectangle<int> contentArea,
                     const DeviceSlotContentPaintState& state, int meterStripWidth,
                     int paginationHeight, int faustHeaderHeight) {
    if (state.collapsed)
        return;

    // Vertical separator before the meter strip — skip it when there's no strip
    // (meterStripWidth <= 0, e.g. post-FX analysis devices).
    if (meterStripWidth > 0) {
        const int lineX = contentArea.getRight() - meterStripWidth - 4;
        const int meterTop = contentArea.getY();
        g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER));
        g.drawVerticalLine(lineX, static_cast<float>(meterTop + 2),
                           static_cast<float>(contentArea.getBottom() - 2));
    }

    const auto left = static_cast<float>(contentArea.getX() + 2);
    const auto right = static_cast<float>(contentArea.getRight() - 2);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));

    // Rule under the pagination row. `paginationHeight` is 0 when the grid has
    // a single page: there is no row to separate, and drawing it anyway left a
    // line with an empty band above it.
    const bool runtimeFaust = state.traits.isFaust || state.traits.isFaustInstrument;
    if (paginationHeight > 0 && !state.traits.compiledPresentation &&
        (!state.internalDevice || runtimeFaust || !state.hasCustomUI)) {
        constexpr int paginationTopPadding = 2;
        constexpr int paginationBottomPadding = 4;
        const int paramGridTop = contentArea.getY() + (runtimeFaust ? faustHeaderHeight : 0);
        const int paginationBottom =
            paramGridTop + paginationTopPadding + paginationHeight + paginationBottomPadding;
        g.drawHorizontalLine(paginationBottom, left, right);
    }
}

bool paintLoadState(juce::Graphics& g, juce::Rectangle<int> contentArea,
                    magda::DeviceLoadState loadState) {
    if (loadState == magda::DeviceLoadState::Loading) {
        g.setColour(ActiveTheme::getSecondaryTextColour().withAlpha(0.6f));
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

}  // namespace

void paintDeviceSlotContent(juce::Graphics& g, juce::Rectangle<int> contentArea,
                            const DeviceSlotContentPaintState& state, int meterStripWidth,
                            int paginationHeight, int faustHeaderHeight) {
    paintSeparators(g, contentArea, state, meterStripWidth, paginationHeight, faustHeaderHeight);
    paintLoadState(g, contentArea, state.loadState);
}

DeviceSlotSubtitle deviceSlotSubtitle(const DeviceSlotContentPaintState& state) {
    // Analysis devices (oscilloscope / spectrum) and Faust patches carry their name alone.
    if (skipsContentHeader(state))
        return {};

    const auto dim = ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2);
    const auto shade = state.bypassed ? dim.withAlpha(0.5f) : dim;
    const auto& traits = state.traits;

    if ((traits.isStepSequencer || traits.isPolyStepSequencer) && state.stepRecording.active) {
        const int maxSteps = juce::jmax(1, state.stepRecording.maxSteps);
        const int position = juce::jlimit(0, maxSteps - 1, state.stepRecording.position);
        return {"STEP RECORDING " + juce::String(position + 1) + "/" + juce::String(maxSteps),
                ActiveTheme::getColour(ActiveTheme::STEP_RECORD)};
    }

    if (traits.isDrumGrid)
        return {"MAGDA / Drum Grid", shade};
    if (traits.isChordEngine)
        return {"MAGDA / Chord Engine", shade};
    if (traits.isArpeggiator)
        return {"MAGDA / Arpeggiator", shade};
    if (traits.isStrum)
        return {"MAGDA / Strum", shade};
    if (traits.isPolyStepSequencer)
        return {"MAGDA / Poly Sequencer", shade};
    if (traits.isStepSequencer)
        return {"MAGDA / Step Sequencer", shade};
    return {state.manufacturer, shade};
}

}  // namespace magda::daw::ui
