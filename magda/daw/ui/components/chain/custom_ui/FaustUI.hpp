#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "core/ChainNodePath.hpp"

namespace magda::daw::audio {
class IFaustEditorModel;
}

namespace magda {
class SvgButton;
}

namespace magda::daw::ui {

class FaustCustomView;

/**
 * @brief Bespoke header strip shared by runtime Faust effects and instruments.
 *
 * This component renders the Faust-specific header — logo, DSP name
 * box, Load DSP icon, Edit code icon — and *only* that strip. The
 * device's parameter widgets are rendered by the standard
 * DeviceSlotComponent::paramGrid_, driven by the ParameterInfo that
 * FaustProcessor produces from the FaustPlugin pool. Sharing the
 * grid with every other device gives Faust mod/macro/automation/MIDI
 * Learn drag-and-drop for free.
 *
 * After a successful Load or Edit-recompile, FaustUI fires
 * onDspChanged so the host (DeviceSlotComponent) can trigger a
 * track-devices-changed rebuild — paramGrid_ then re-fetches
 * DeviceInfo.parameters from the (now refreshed) pool.
 *
 * Fixed-height layout: the host carves `kHeaderHeight` from the top
 * of the content area for this strip, and gives the rest to
 * paramGrid_.
 */
class FaustUI : public juce::Component {
  public:
    static constexpr int kHeaderHeight = 36;

    /// Extra row below the header, shown only when the loaded patch
    /// declares any of author / version / license / description.
    static constexpr int kInfoStripHeight = 16;

    /// Total height the host should carve for this component. Collapses to
    /// `kHeaderHeight` for patches that declare no metadata, so an
    /// unannotated .dsp gives up no grid space.
    int getDesiredHeight() const {
        return kHeaderHeight + (showInfoStrip_ ? kInfoStripHeight : 0);
    }

    FaustUI();
    ~FaustUI() override;

    void setPlugin(magda::daw::audio::IFaustEditorModel* plugin);

    /// Path of the device this UI is bound to. Used by the DSP-load
    /// flow to fire a track-devices-changed notification, which
    /// makes the standard paramGrid_ rebuild against the new pool
    /// state.
    void setDevicePath(const ChainNodePath& path);

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    void showLoadMenu();
    void loadFromFile();
    void saveDspToFile();
    // User Faust effects dir (dataDir()/FaustEffects), created on demand. Save
    // target + Load default location, so generated effects build a reusable
    // library separate from MAGDA .mps presets.
    static juce::File userEffectsDir();
    void showCodeEditor();
    // The custom view comes from the source's own `declare magda_view`, so
    // every load path resolves it the same way with nothing to pass in.
    bool tryLoad(const juce::String& name, const juce::String& source);

    void refreshNameLabel();

    magda::daw::audio::IFaustEditorModel* plugin_ = nullptr;
    ChainNodePath devicePath_;

    std::unique_ptr<juce::Drawable> logo_;
    juce::Rectangle<float> logoBounds_;
    juce::Rectangle<float> nameBorderBounds_;
    juce::Label nameLabel_;

    // Info strip state, refreshed whenever the loaded DSP changes.
    // `infoStripText_` is the one-line credit; the full description goes to
    // the name box tooltip, since it is prose and does not fit a 16px row.
    bool showInfoStrip_ = false;
    juce::String infoStripText_;
    juce::Label errorLabel_;
    std::unique_ptr<magda::SvgButton> saveButton_;
    std::unique_ptr<magda::SvgButton> loadButton_;
    std::unique_ptr<magda::SvgButton> editButton_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    std::unique_ptr<class FaustCodeEditorWindow> editorWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustUI)
};

}  // namespace magda::daw::ui
