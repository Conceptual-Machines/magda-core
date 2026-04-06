#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ArpeggiatorUI.hpp"
#include "ChorusUI.hpp"
#include "CompressorUI.hpp"
#include "DelayUI.hpp"
#include "DrumGridUI.hpp"
#include "EqualiserUI.hpp"
#include "FilterUI.hpp"
#include "FourOscUI.hpp"
#include "ImpulseResponseUI.hpp"
#include "PhaserUI.hpp"
#include "PitchShiftUI.hpp"
#include "ReverbUI.hpp"
#include "SamplerUI.hpp"
#include "StepSequencerUI.hpp"
#include "ToneGeneratorUI.hpp"
#include "UtilityUI.hpp"
#include "audio/ArpeggiatorPlugin.hpp"
#include "audio/MidiChordEnginePlugin.hpp"
#include "audio/StepSequencerPlugin.hpp"
#include "core/DeviceInfo.hpp"
#include "core/SelectionManager.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/panels/content/ChordPanelContent.hpp"

namespace magda::daw::ui {

/**
 * @brief Manages all custom UI instances for a DeviceSlotComponent.
 *
 * This is a plain (non-JUCE-Component) manager class that owns the unique_ptrs
 * for all internal-device custom UIs.  DeviceSlotComponent owns one of these
 * and delegates creation / update to it.
 *
 * The manager calls parent->addAndMakeVisible() for whatever UI is created, so
 * the parent (DeviceSlotComponent) remains the JUCE owner of the child components.
 */
class DeviceCustomUIManager {
  public:
    // -------------------------------------------------------------------------
    // Callbacks provided by DeviceSlotComponent so the custom UIs can call back
    // -------------------------------------------------------------------------
    struct Callbacks {
        // Called when a parameter value changes (paramIndex, normalizedValue)
        std::function<void(int, float)> onParameterChanged;
        // Called when the layout needs updating (e.g. drum grid chains toggled)
        std::function<void()> onLayoutChanged;
        // Called when the mod panel should expand/select a mod
        std::function<void()> onExpandModPanel;
        // Called when the macro panel should expand/select a macro
        std::function<void()> onExpandMacroPanel;
        // Called to trigger a full updateParamModulation() on the parent
        std::function<void()> onParamModulationChanged;
        // Called to trigger updateModsPanel() on the parent
        std::function<void()> onUpdateModsPanel;
        // Called to trigger updateMacroPanel() on the parent
        std::function<void()> onUpdateMacroPanel;
        // Returns the current node path of the parent (queried at callback time, not capture time)
        std::function<magda::ChainNodePath()> getNodePath;
    };

    DeviceCustomUIManager() = default;
    ~DeviceCustomUIManager() = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /**
     * Create the appropriate custom UI for the given device and add it as a
     * visible child of @p parent.  Must be called once when the device slot is
     * constructed (or when updateFromDevice discovers a new internal device).
     */
    void create(const magda::DeviceInfo& device, juce::Component* parent,
                const Callbacks& callbacks);

    /**
     * Update the custom UI to reflect new device state (parameter values, etc.).
     */
    void update(const magda::DeviceInfo& device);

    /**
     * Read the FourOsc mod matrix from the plugin and push it to FourOscUI.
     */
    void readAndPushModMatrix(magda::DeviceId deviceId);

    // -------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------

    /** Returns the single active custom UI component, or nullptr if none. */
    juce::Component* getActiveUI() const;

    /**
     * Returns the linkable sliders for the currently active custom UI.
     * Returns an empty vector if no custom UI is active or the active UI has no linkable sliders.
     */
    std::vector<LinkableTextSlider*> getLinkableSliders() const;

    /**
     * Returns true if any custom UI has been created.
     * Used to decide whether to show the parameter grid or the custom UI.
     */
    bool hasAnyUI() const;

    /**
     * Returns true if any custom UI has been created, checking all known types.
     * Same as hasAnyUI() but explicit — kept for parity with old code patterns.
     */
    bool hasCustomUI() const {
        return hasAnyUI();
    }

    /** Preferred content width for layout calculations (matches old per-type if-chains). */
    int getPreferredContentWidth(int drumGridFallback = 0) const;

    // -------------------------------------------------------------------------
    // Accessors used outside createCustomUI / updateCustomUI
    // -------------------------------------------------------------------------

    // Plugin raw pointers (needed by DeviceSlotComponent::timerCallback and setNodePath)
    daw::audio::ArpeggiatorPlugin* getArpPlugin() const {
        return arpPlugin_;
    }
    daw::audio::StepSequencerPlugin* getStepSeqPlugin() const {
        return stepSeqPlugin_;
    }
    daw::audio::MidiChordEnginePlugin* getChordPlugin() const {
        return chordPlugin_;
    }

    // Allow timerCallback to write stepSeqPlugin_ after setNodePath resolution
    void setStepSeqPlugin(daw::audio::StepSequencerPlugin* p) {
        stepSeqPlugin_ = p;
    }

    // Tab index for FourOscUI persistence across rebuilds
    int getCustomUITabIndex() const;
    void setCustomUITabIndex(int index);

    // Pending tab index (set before fourOscUI_ is created, consumed in create())
    static constexpr int NO_PENDING_TAB = -1;
    int pendingCustomUITabIndex_ = NO_PENDING_TAB;

    // Direct accessors needed by DeviceSlotComponent for setNodePath() and getDrumPad*()
    DrumGridUI* getDrumGridUI() const {
        return drumGridUI_.get();
    }
    FourOscUI* getFourOscUI() const {
        return fourOscUI_.get();
    }
    ChordPanelContent* getChordEngineUI() const {
        return chordEngineUI_.get();
    }
    ArpeggiatorUI* getArpeggiatorUI() const {
        return arpeggiatorUI_.get();
    }
    StepSequencerUI* getStepSequencerUI() const {
        return stepSequencerUI_.get();
    }

  private:
    // Custom UI unique_ptrs
    std::unique_ptr<ToneGeneratorUI> toneGeneratorUI_;
    std::unique_ptr<SamplerUI> samplerUI_;
    std::unique_ptr<DrumGridUI> drumGridUI_;
    std::unique_ptr<FourOscUI> fourOscUI_;
    std::unique_ptr<EqualiserUI> eqUI_;
    std::unique_ptr<CompressorUI> compressorUI_;
    std::unique_ptr<ReverbUI> reverbUI_;
    std::unique_ptr<DelayUI> delayUI_;
    std::unique_ptr<ChorusUI> chorusUI_;
    std::unique_ptr<PhaserUI> phaserUI_;
    std::unique_ptr<FilterUI> filterUI_;
    std::unique_ptr<PitchShiftUI> pitchShiftUI_;
    std::unique_ptr<ImpulseResponseUI> impulseResponseUI_;
    std::unique_ptr<UtilityUI> utilityUI_;
    std::unique_ptr<ChordPanelContent> chordEngineUI_;
    std::unique_ptr<ArpeggiatorUI> arpeggiatorUI_;
    std::unique_ptr<StepSequencerUI> stepSequencerUI_;

    // Plugin raw pointers for timer polling / setNodePath updates
    daw::audio::ArpeggiatorPlugin* arpPlugin_ = nullptr;
    daw::audio::StepSequencerPlugin* stepSeqPlugin_ = nullptr;
    daw::audio::MidiChordEnginePlugin* chordPlugin_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceCustomUIManager)
};

}  // namespace magda::daw::ui
