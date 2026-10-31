// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "audio/plugins/FaustPatchInfo.hpp"
#include "core/ChainNodePath.hpp"

namespace magda::daw::audio {
class IFaustEditorModel;
class MagdaDevice;
}  // namespace magda::daw::audio

namespace magda {
class SvgButton;
}

namespace magda::daw::ui {

class FaustCustomView;

/**
 * @brief The runtime Faust device's own controls, without a band of its own (#3205).
 *
 * Save / Load / Edit go in the slot's header, the patch name becomes the slot's title with its
 * credits in the tooltip, and the logo is a watermark behind the grid. This component is the
 * strip a compile error shows in, over the foot of the body. Parameters are the slot's grid.
 */
class FaustUI : public juce::Component {
  public:
    static constexpr int kErrorHeight = 16;

    /// The error strip's height: 0 unless the last load or edit left an error.
    int errorHeight() const;

    /// Save / Load / Edit, right to left, for the slot's header.
    std::vector<juce::Component*> headerControls();
    /// Called with the patch name and its credits whenever the loaded patch changes, and at once.
    void setOnIdentityChanged(std::function<void(const juce::String&, const juce::String&)> cb);
    void paintWatermark(juce::Graphics& g, juce::Rectangle<float> area) const;

    FaustUI();
    ~FaustUI() override;

    /// The Faust device rendering this slot, held open while bound. Null unbinds.
    void setDevice(std::shared_ptr<magda::daw::audio::MagdaDevice> device);
    const magda::daw::audio::MagdaDevice* boundDevice() const {
        return device_.get();
    }

    /// Path of the device this UI loads patches into.
    void setDevicePath(const ChainNodePath& path);

    void paint(juce::Graphics& g) override;
    void resized() override;
    // Label colours are cached by setColour, so they need re-applying when the
    // palette changes; paint-time roles look themselves up and self-heal.
    void lookAndFeelChanged() override;

  private:
    void showLoadMenu();
    void loadFromFile();
    void saveDspToFile();
    // The kind of the bound device, defaulting to Effect while unbound so the
    // menu-building helpers have something to answer with.
    magda::daw::audio::FaustPatchKind patchKind() const;
    // User Faust patch dir for one device kind (dataDir()/FaustEffects or
    // .../FaustInstruments), created on demand. Save target + Load default
    // location, so generated patches build a reusable library separate from
    // MAGDA .mps presets. Split by kind because an instrument patch is not
    // loadable into an effect: it synthesises against Faust's reserved
    // freq/gain/gate voice controls rather than processing an input.
    static juce::File userPatchDir(magda::daw::audio::FaustPatchKind kind);
    void showCodeEditor();
    // The custom view comes from the source's own `declare magda_view`, so
    // every load path resolves it the same way with nothing to pass in.
    bool tryLoad(const juce::String& name, const juce::String& source);

    void refreshNameLabel();
    void setError(const juce::String& error);

    std::shared_ptr<magda::daw::audio::MagdaDevice> device_;
    magda::daw::audio::IFaustEditorModel* plugin_ = nullptr;
    ChainNodePath devicePath_;

    std::unique_ptr<juce::Drawable> logo_;
    juce::String patchName_;
    juce::String patchTooltip_;
    std::function<void(const juce::String&, const juce::String&)> onIdentityChanged_;
    juce::Label errorLabel_;
    std::unique_ptr<magda::SvgButton> saveButton_;
    std::unique_ptr<magda::SvgButton> loadButton_;
    std::unique_ptr<magda::SvgButton> editButton_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    std::unique_ptr<class FaustCodeEditorWindow> editorWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustUI)
};

}  // namespace magda::daw::ui
