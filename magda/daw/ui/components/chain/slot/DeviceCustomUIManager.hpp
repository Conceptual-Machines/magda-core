#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "core/DeviceInfo.hpp"
#include "core/DeviceUiContext.hpp"
#include "core/SelectionManager.hpp"

namespace magda::daw::audio {
class ArpeggiatorPlugin;
class MidiChordEnginePlugin;
class MidiStrumPlugin;
class OscilloscopePlugin;
class PolyStepSequencerPlugin;
class SpectrumAnalyzerPlugin;
class StepSequencerPlugin;
}  // namespace magda::daw::audio

namespace magda::daw::ui {

class ArpeggiatorUI;
class ChordPanelContent;
class ChorusUI;
class CompressorUI;
class DelayUI;
class DrumGridUI;
class EqualiserUI;
class FaustUI;
class FaustInstrumentTabbedUI;
class FilterUI;
class FourOscUI;
class ImpulseResponseUI;
class LevelsUI;
class LinkableTextSlider;
class OscilloscopeUI;
class OscilloscopeTelemetrySource;
class PhaserUI;
class PolyStepSequencerUI;
class PolySynthUI;
class FMUI;
class MateriaUI;
class HaloUI;
class NimbusUI;
class DrumVoiceUI;
class StruckInstrumentUI;
class SpectrumAnalyzerUI;
class SpectrumTelemetrySource;
class PitchShiftUI;
class ReverbUI;
class SamplerUI;
class StepSequencerUI;
class StrumUI;
class ToneGeneratorUI;
class LevelsTelemetrySource;
class NimbusTelemetrySource;

}  // namespace magda::daw::ui

namespace magda {
class MidiNoteStrip;
}

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
        // Optional live-plugin resolver for embedded device contexts that do
        // not have an AudioBridge-resolvable ChainNodePath, such as DrumGrid pad chains.
        std::function<tracktion::engine::Plugin::Ptr()> getLivePlugin;
        // Optional stable UI context. If omitted, DeviceCustomUIManager creates
        // a BasicDeviceUiContext so migrations can adopt the context gradually.
        std::shared_ptr<magda::DeviceUiContext> deviceUiContext;
    };

    DeviceCustomUIManager();
    ~DeviceCustomUIManager();

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
     * Push cached parameter values into lightweight controls without heavier
     * plugin-state reads such as waveforms or drum-pad chain snapshots.
     */
    void refreshParameterValues(const magda::DeviceInfo& device);

    /**
     * Read the FourOsc mod matrix from the plugin and push it to FourOscUI.
     */
    void readAndPushModMatrix(magda::DeviceId deviceId);

    /**
     * Set the chain path of the device this custom UI is bound to. Once set,
     * internal plugin lookups (FourOsc, Sampler, Faust, etc.) go through the
     * path rather than a bare DeviceId — required for section-scoped ids.
     * Callable repeatedly; latest value wins.
     *
     * Also re-binds the analyzer UIs (oscilloscope / spectrum): create() runs
     * before the slot knows its path, so those UIs are built while devicePath_
     * is still invalid and would otherwise never resolve their plugin.
     */
    void setDevicePath(const magda::ChainNodePath& path);

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

    std::shared_ptr<magda::DeviceUiContext> getDeviceUiContext() const {
        return deviceUiContext_;
    }

    // MIDI utility binding refreshes. Concrete plugin pointers stay inside the
    // manager so slot/header code does not depend on plugin lifetime.
    void bindStepSequencerPlugin(daw::audio::StepSequencerPlugin* p) {
        stepSeqPlugin_ = p;
    }
    void bindPolyStepSequencerPlugin(daw::audio::PolyStepSequencerPlugin* p) {
        polyStepSeqPlugin_ = p;
    }
    void bindStrumPlugin(daw::audio::MidiStrumPlugin* p) {
        strumPlugin_ = p;
    }

    // Tab index for FourOscUI persistence across rebuilds
    int getCustomUITabIndex() const;
    void setCustomUITabIndex(int index);

    // Re-resolve path-bound plugin pointers for UIs that depend on a live
    // Tracktion plugin. Safe to call before the plugin exists.
    void refreshLivePluginBindings();

    // Invalidate the current UI context and clear live plugin bindings before
    // the owning slot is destroyed, removed, or rebound to another plugin.
    void detachFromLivePlugin();

    bool randomizeSequencerPattern(bool polyphonic);
    std::optional<bool> toggleSequencerStepRecording(bool polyphonic);
    void copySequencerPatternToClipboard(bool polyphonic);
    bool handleSequencerPatternExternalDrag(bool polyphonic, juce::Component* exportButton,
                                            juce::Component* dragOwner,
                                            const juce::MouseEvent& event);
    bool getSequencerStepRecordingState(bool polyphonic, int& position, int& maxSteps) const;
    void refreshArpeggiatorMidiActivity(magda::MidiNoteStrip& strip, int& lastNote) const;
    void refreshStrumMidiActivity(magda::MidiNoteStrip& strip, int& lastNote) const;
    void refreshStepSequencerMidiActivity(magda::MidiNoteStrip& strip, int& lastNote) const;
    void refreshPolyStepSequencerMidiActivity(magda::MidiNoteStrip& strip, int& lastNote) const;
    void refreshChordEngineMidiActivity(magda::MidiNoteStrip& strip,
                                        std::array<int, 32>& lastChordNotes,
                                        int& lastChordCount) const;

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
    FaustInstrumentTabbedUI* getFaustInstrumentUI() const {
        return faustInstrumentUI_.get();
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
    StrumUI* getStrumUI() const {
        return strumUI_.get();
    }
    PolyStepSequencerUI* getPolyStepSequencerUI() const {
        return polyStepSequencerUI_.get();
    }

  private:
    // (Re-)resolve the live plugin for the oscilloscope / spectrum analyzer UIs
    // from the current devicePath_ and hand it to them. Safe to call before the
    // path or plugin exists (it simply binds nothing).
    void bindAnalyzerPlugins();
    tracktion::engine::Plugin::Ptr getLivePlugin() const;
    void createToneGeneratorUI(const magda::DeviceInfo& device, juce::Component& parent,
                               const Callbacks& callbacks);
    bool createSamplerUI(const magda::DeviceInfo& device, juce::Component& parent,
                         const Callbacks& callbacks);
    bool createDrumGridUI(const magda::DeviceInfo& device, juce::Component& parent,
                          const Callbacks& callbacks);
    bool createAnalyzerUI(const magda::DeviceInfo& device, juce::Component& parent);
    bool createMidiUtilityUI(const magda::DeviceInfo& device, juce::Component& parent);
    bool createFourOscUI(const magda::DeviceInfo& device, juce::Component& parent,
                         const Callbacks& callbacks);
    bool createCustomInstrumentUI(const magda::DeviceInfo& device, juce::Component& parent,
                                  const Callbacks& callbacks);
    bool createSimpleEffectUI(const magda::DeviceInfo& device, juce::Component& parent,
                              const Callbacks& callbacks);
    bool createImpulseResponseUI(const magda::DeviceInfo& device, juce::Component& parent,
                                 const Callbacks& callbacks);
    juce::var executeCustomUiCommand(const juce::Identifier& command, const juce::var& arguments);
    juce::var executeSamplerCommand(const juce::Identifier& command, const juce::var& arguments);
    bool executeImpulseResponseLoadCommand(const juce::var& arguments);
    juce::var executeSequencerCommand(const juce::Identifier& command, const juce::var& arguments,
                                      bool polyphonic);

    // Path of the device this manager is bound to. Used by every internal
    // plugin lookup; the bare device.id is no longer sufficient under
    // section-scoped device ids.
    magda::ChainNodePath devicePath_;
    std::shared_ptr<magda::DeviceUiContext> deviceUiContext_;
    std::function<tracktion::engine::Plugin::Ptr()> livePluginProvider_;
    tracktion::engine::Plugin* telemetryPlugin_ = nullptr;
    std::shared_ptr<OscilloscopeTelemetrySource> oscilloscopeTelemetry_;
    std::shared_ptr<SpectrumTelemetrySource> spectrumTelemetry_;
    std::shared_ptr<LevelsTelemetrySource> levelsTelemetry_;
    std::shared_ptr<NimbusTelemetrySource> nimbusTelemetry_;

    // Custom UI unique_ptrs
    std::unique_ptr<ToneGeneratorUI> toneGeneratorUI_;
    std::unique_ptr<SamplerUI> samplerUI_;
    std::unique_ptr<DrumGridUI> drumGridUI_;
    std::unique_ptr<FourOscUI> fourOscUI_;
    std::unique_ptr<FaustInstrumentTabbedUI> faustInstrumentUI_;
    std::unique_ptr<PolySynthUI> polySynthUI_;
    std::unique_ptr<FMUI> fmUI_;
    std::unique_ptr<MateriaUI> materiaUI_;
    std::unique_ptr<HaloUI> haloUI_;
    std::unique_ptr<NimbusUI> nimbusUI_;
    std::unique_ptr<DrumVoiceUI> drumVoiceUI_;
    std::unique_ptr<StruckInstrumentUI> struckUI_;
    std::unique_ptr<EqualiserUI> eqUI_;
    std::unique_ptr<CompressorUI> compressorUI_;
    std::unique_ptr<ReverbUI> reverbUI_;
    std::unique_ptr<DelayUI> delayUI_;
    std::unique_ptr<ChorusUI> chorusUI_;
    std::unique_ptr<PhaserUI> phaserUI_;
    std::unique_ptr<FilterUI> filterUI_;
    std::unique_ptr<PitchShiftUI> pitchShiftUI_;
    std::unique_ptr<ImpulseResponseUI> impulseResponseUI_;
    std::unique_ptr<FaustUI> faustUI_;
    std::unique_ptr<ChordPanelContent> chordEngineUI_;
    std::unique_ptr<ArpeggiatorUI> arpeggiatorUI_;
    std::unique_ptr<StrumUI> strumUI_;
    std::unique_ptr<StepSequencerUI> stepSequencerUI_;
    std::unique_ptr<PolyStepSequencerUI> polyStepSequencerUI_;
    std::unique_ptr<OscilloscopeUI> oscilloscopeUI_;
    std::unique_ptr<SpectrumAnalyzerUI> spectrumAnalyzerUI_;
    std::unique_ptr<LevelsUI> levelsUI_;

    // Plugin raw pointers for timer polling / setNodePath updates
    daw::audio::ArpeggiatorPlugin* arpPlugin_ = nullptr;
    daw::audio::MidiStrumPlugin* strumPlugin_ = nullptr;
    daw::audio::StepSequencerPlugin* stepSeqPlugin_ = nullptr;
    daw::audio::PolyStepSequencerPlugin* polyStepSeqPlugin_ = nullptr;
    daw::audio::MidiChordEnginePlugin* chordPlugin_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DeviceCustomUIManager)
};

}  // namespace magda::daw::ui
