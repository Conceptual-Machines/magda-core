#include "slot/DeviceSlotContentLayout.hpp"

#include <utility>

#include "drum_grid/DeviceSlotDrumGridBridge.hpp"
#include "layout/FaceplateBelowGeometry.hpp"
#include "params/ParamHostComponent.hpp"
#include "ui/debug/DebugSettings.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

void setVisibleIfPresent(juce::Component* component, bool shouldBeVisible) {
    if (component != nullptr)
        component->setVisible(shouldBeVisible);
}

bool isMidiUtility(const DeviceSlotTraits& traits) {
    return traits.isChordEngine || traits.isArpeggiator || traits.isStrum ||
           traits.isStepSequencer || traits.isPolyStepSequencer;
}

void layoutPluginPresetButton(juce::Rectangle<int> secondHeaderArea, const DeviceSlotTraits& traits,
                              bool pluginPresetsAvailable, juce::Component* pluginPresetsButton) {
    if (pluginPresetsButton == nullptr)
        return;

    const bool eligible = !isMidiUtility(traits);
    const bool show = eligible && pluginPresetsAvailable;
    if (!show) {
        pluginPresetsButton->setVisible(false);
        return;
    }

    const int btnWidth = juce::jmin(140, secondHeaderArea.getWidth() / 2);
    pluginPresetsButton->setBounds(secondHeaderArea.removeFromRight(btnWidth).reduced(2, 3));
    pluginPresetsButton->setVisible(true);
}

void layoutMeterStrip(juce::Rectangle<int>& contentArea, const DeviceSlotTraits& traits,
                      DeviceSlotContentFrameControls controls, int meterStripWidth) {
    // Width 0 = no meter strip (e.g. post-FX analysis devices): reserve nothing
    // so the body uses the full width.
    if (meterStripWidth <= 0) {
        setVisibleIfPresent(controls.levelMeter, false);
        setVisibleIfPresent(controls.midiNoteStrip, false);
        setVisibleIfPresent(controls.gainSlider, false);
        setVisibleIfPresent(controls.mixKnob, false);
        return;
    }

    auto stripBounds = contentArea.removeFromRight(meterStripWidth).reduced(1, 3);
    contentArea.removeFromRight(4);

    // Mix knob at the very top of the meter strip when the host wires one in.
    // The meter (and the overlaid gain slider) shrink to leave room. Visible
    // only when the device has a DryGain+WetGain wrapper pair — that decision
    // lives on the host, which sets the knob's visibility before relayout.
    constexpr int kMixKnobHeight = 18;
    constexpr int kMixKnobGap = 4;
    if (controls.mixKnob != nullptr && controls.mixKnob->isVisible() &&
        stripBounds.getHeight() > kMixKnobHeight + kMixKnobGap + 6) {
        auto knobSlot = stripBounds.removeFromTop(kMixKnobHeight);
        const int knobSize = juce::jmin(knobSlot.getWidth(), knobSlot.getHeight());
        controls.mixKnob->setBounds(knobSlot.withSizeKeepingCentre(knobSize, knobSize));
        stripBounds.removeFromTop(kMixKnobGap);
        controls.mixKnob->toFront(false);
    }

    const bool usesNoteStrip = isMidiUtility(traits);
    if (controls.levelMeter != nullptr) {
        controls.levelMeter->setBounds(stripBounds);
        controls.levelMeter->setVisible(!usesNoteStrip);
    }
    if (controls.midiNoteStrip != nullptr) {
        controls.midiNoteStrip->setBounds(stripBounds);
        controls.midiNoteStrip->setVisible(usesNoteStrip);
    }

    if (controls.gainSlider != nullptr) {
        // Analysis devices (oscilloscope / spectrum) are transparent passthroughs
        // and MIDI utilities emit no audio: neither has a volume control. (The
        // level meter still shows on analysis; MIDI utilities show the note
        // strip instead.)
        if (traits.isAnalysis || usesNoteStrip) {
            controls.gainSlider->setVisible(false);
        } else {
            controls.gainSlider->setBounds(stripBounds);
            controls.gainSlider->setVisible(true);
            controls.gainSlider->toFront(false);
        }
    }

    // No wet/dry mix on MIDI utilities — they have no audio to blend.
    if (usesNoteStrip)
        setVisibleIfPresent(controls.mixKnob, false);
}

void hideBodyControls(DeviceSlotContentFrameControls controls, bool collapsed) {
    setVisibleIfPresent(controls.paramGrid, false);
    setVisibleIfPresent(controls.gainLabel, false);
    setVisibleIfPresent(controls.pluginPresetsButton, false);
    setVisibleIfPresent(controls.gainSlider, false);
    setVisibleIfPresent(controls.magdaPresetButton, !collapsed);
    setVisibleIfPresent(controls.activeCustomUI, false);
    setVisibleIfPresent(controls.compiledPanel, false);
    setVisibleIfPresent(controls.faustHeader, false);
    setVisibleIfPresent(controls.faustCustomView, false);
    setVisibleIfPresent(controls.faustMeterPanel, false);
}

void showExpandedHeaderControls(const DeviceSlotTraits& traits, const magda::DeviceInfo& device,
                                bool internalDevice, DeviceSlotContentFrameControls controls) {
    setVisibleIfPresent(controls.modButton,
                        drum_grid_slot::shouldShowModButton(traits.isDrumGrid, device.deviceType));
    setVisibleIfPresent(controls.macroButton,
                        drum_grid_slot::shouldShowMacroButton(
                            traits.isDrumGrid, device.deviceType,
                            traits.isArpeggiator || traits.isStrum,
                            traits.isStepSequencer || traits.isPolyStepSequencer));
    setVisibleIfPresent(controls.uiButton, !internalDevice || traits.isAnalysis);
    setVisibleIfPresent(controls.powerButton, true);
    setVisibleIfPresent(controls.gainLabel, !isMidiUtility(traits) && !traits.isAnalysis);
}

void layoutParamGrid(ParamHostComponent* paramGrid, juce::Rectangle<int> area) {
    if (paramGrid == nullptr)
        return;

    auto labelFont =
        FontManager::getInstance().getUIFont(DebugSettings::getInstance().getParamLabelFontSize());
    auto valueFont =
        FontManager::getInstance().getUIFont(DebugSettings::getInstance().getParamValueFontSize());
    paramGrid->setBounds(area);
    paramGrid->setVisible(true);
    paramGrid->layoutContent(labelFont, valueFont);
}

// Floor for the Faust meter strip when the parameter grid leaves it nothing.
// Below this a vertical bar stops reading as a level.
constexpr int kMinFaustMeterHeight = 64;

int boundedBottomPanelHeight(int preferredHeight, int bodyHeight, int minFractionNumerator,
                             int minFractionDenominator) {
    jassert(minFractionNumerator >= 0);
    jassert(minFractionDenominator > 0);

    const int minScaledHeight = (bodyHeight * minFractionNumerator) / minFractionDenominator;
    return juce::jlimit(juce::jmin(preferredHeight, bodyHeight), bodyHeight,
                        juce::jmax(preferredHeight, minScaledHeight));
}

}  // namespace

bool prepareDeviceSlotContentFrame(juce::Rectangle<int>& contentArea,
                                   const DeviceSlotTraits& traits, const magda::DeviceInfo& device,
                                   bool collapsed, bool internalDevice, bool pluginPresetsAvailable,
                                   DeviceSlotContentFrameControls controls, int meterStripWidth,
                                   int contentHeaderHeight) {
    const bool skipContentHeader = traits.isAnalysis || traits.isFaust ||
                                   traits.isFaustInstrument ||
                                   (traits.compiledPresentation != nullptr &&
                                    traits.compiledPresentation->layoutCellCount == 0);

    if (!collapsed) {
        if (!skipContentHeader) {
            auto secondHeaderArea = contentArea.removeFromTop(contentHeaderHeight);
            layoutPluginPresetButton(secondHeaderArea, traits, pluginPresetsAvailable,
                                     controls.pluginPresetsButton);
        } else {
            setVisibleIfPresent(controls.pluginPresetsButton, false);
        }

        // MIDI devices emit no audio, so they get no peak meter / gain fader /
        // wet-dry mix. Note-strip utilities keep the strip for their MIDI
        // activity display (gain + mix hidden inside layoutMeterStrip); any other
        // MIDI device drops the strip entirely so the body uses the full width.
        const bool isMidiDevice =
            device.deviceType == magda::DeviceType::MIDI || traits.isChordEngine;
        const int effectiveMeterWidth =
            (isMidiDevice && !isMidiUtility(traits)) ? 0 : meterStripWidth;
        layoutMeterStrip(contentArea, traits, controls, effectiveMeterWidth);
    }

    contentArea.removeFromBottom(2);

    if (collapsed || device.loadState != magda::DeviceLoadState::Loaded) {
        hideBodyControls(controls, collapsed);
        return false;
    }

    showExpandedHeaderControls(traits, device, internalDevice, controls);
    return true;
}

void layoutDeviceSlotContentBody(juce::Rectangle<int> contentArea, const DeviceSlotTraits& traits,
                                 bool internalDevice, bool hasCustomUI,
                                 DeviceSlotContentBodyControls controls, int faustHeaderHeight) {
    const bool runtimeFaust = traits.isFaust || traits.isFaustInstrument;
    if (runtimeFaust && controls.faustHeader != nullptr) {
        controls.faustHeader->setBounds(contentArea.removeFromTop(faustHeaderHeight));
        controls.faustHeader->setVisible(true);

        if (controls.faustCustomView != nullptr) {
            const int bodyHeight = juce::jmax(0, contentArea.getHeight());
            const int customHeight =
                boundedBottomPanelHeight(controls.faustCustomViewPreferredHeight, bodyHeight, 1, 2);
            controls.faustCustomView->setBounds(contentArea.removeFromBottom(customHeight));
            controls.faustCustomView->setVisible(true);
        }

        // Meters sit under the grid and above any custom view, in a region of
        // their own: a level needs height, which a cell sized for a knob and
        // its label cannot give it.
        //
        // The strip takes the rows the grid leaves empty rather than a slice
        // of the body. The grid always fills its bounds with getRowCount()
        // rows whether or not the page has controls for them, so simply
        // carving off the bottom would shrink every knob to make room for
        // space that was already blank. Pinning the row height to what the
        // grid would have used with the whole body keeps the controls exactly
        // as they are without a meter.
        const bool wantsMeters =
            controls.faustMeterPanel != nullptr && controls.faustMeterPanelPreferredHeight > 0;
        if (wantsMeters && controls.paramGrid != nullptr) {
            auto& grid = *controls.paramGrid;
            const int rows = juce::jmax(1, grid.getRowCount());
            const int chrome = grid.getChromeHeight();
            const int rowHeight = juce::jmax(0, contentArea.getHeight() - chrome) / rows;
            const int usedRows = juce::jlimit(1, rows, grid.getUsedRowCount());
            const int gridHeight = chrome + usedRows * rowHeight;
            const int leftForMeters = contentArea.getHeight() - gridHeight;

            if (leftForMeters >= kMinFaustMeterHeight) {
                grid.setRowHeight(rowHeight);
                layoutParamGrid(controls.paramGrid, contentArea.removeFromTop(gridHeight));
                controls.faustMeterPanel->setBounds(contentArea);
                controls.faustMeterPanel->setVisible(true);
                return;
            }

            // A page that fills every row has nothing spare, so the strip
            // falls back to a fixed height and the grid divides what is left.
            grid.setRowHeight(0);
            const int meterHeight =
                juce::jmin(kMinFaustMeterHeight, juce::jmax(0, contentArea.getHeight()));
            controls.faustMeterPanel->setBounds(contentArea.removeFromBottom(meterHeight));
            controls.faustMeterPanel->setVisible(true);
            layoutParamGrid(controls.paramGrid, contentArea);
            return;
        }

        setVisibleIfPresent(controls.faustMeterPanel, false);
        if (controls.paramGrid != nullptr)
            controls.paramGrid->setRowHeight(0);
        layoutParamGrid(controls.paramGrid, contentArea);
        return;
    }

    if (controls.compiledPanel != nullptr) {
        if (controls.compiledPanelWantsFullBody) {
            // Panel wants the entire body — hide the param grid and let the
            // panel paint the whole content area. The EQ's collapse toggle
            // uses this path.
            controls.compiledPanel->setBounds(contentArea);
            controls.compiledPanel->setVisible(true);
            setVisibleIfPresent(controls.paramGrid, false);
            return;
        }

        controls.compiledPanel->setVisible(controls.compiledPanelShown);
        if (controls.compiledPanelBandRows > 0 && controls.paramGridShown &&
            controls.paramGrid != nullptr) {
            controls.paramGrid->setFaceplateBelowShown(controls.compiledPanelShown);
            if (controls.compiledPanelShown)
                controls.compiledPanel->setBounds(
                    FaceplateBelowGeometry::of(contentArea, true, controls.compiledPanelBandRows)
                        .faceplate);
            layoutParamGrid(controls.paramGrid, contentArea);
            return;
        }
        if (controls.compiledPanelStackedFraction > 0.0f && controls.paramGridShown) {
            if (controls.compiledPanelShown) {
                const int height = juce::roundToInt(static_cast<float>(contentArea.getHeight()) *
                                                    controls.compiledPanelStackedFraction);
                controls.compiledPanel->setBounds(
                    contentArea.removeFromBottom(height).reduced(14, 0).withTrimmedBottom(14));
            }
            layoutParamGrid(controls.paramGrid, contentArea);
            return;
        }
        if (controls.compiledPanelShown) {
            // Inset like the parameter grid: 14px at the outer edges, top and foot.
            const bool beside = controls.paramGridShown;
            const auto faceplate = beside ? contentArea.removeFromRight(controls.compiledPanelWidth)
                                          : std::exchange(contentArea, {});
            controls.compiledPanel->setBounds(faceplate.withTrimmedTop(10)
                                                  .withTrimmedRight(14)
                                                  .withTrimmedBottom(14)
                                                  .withTrimmedLeft(beside ? 0 : 14));
        }

        if (controls.paramGridShown)
            layoutParamGrid(controls.paramGrid, contentArea);
        else
            setVisibleIfPresent(controls.paramGrid, false);
        return;
    }

    if (internalDevice && hasCustomUI) {
        if (!drum_grid_slot::layoutDrumGridUI(controls.drumGridUI, contentArea) &&
            controls.activeCustomUI != nullptr) {
            controls.activeCustomUI->setBounds(contentArea.reduced(4));
            controls.activeCustomUI->setVisible(true);
        }

        setVisibleIfPresent(controls.paramGrid, false);
        return;
    }

    setVisibleIfPresent(controls.activeCustomUI, false);
    layoutParamGrid(controls.paramGrid, contentArea);
}

}  // namespace magda::daw::ui
