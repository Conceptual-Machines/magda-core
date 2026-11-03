// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "audio/plugins/FaustPatchInfo.hpp"
#include "core/ChainNodePath.hpp"

namespace magda::daw::audio {
class IFaustEditorModel;
class MagdaDevice;
}  // namespace magda::daw::audio

namespace magda::daw::ui {

class FaustCustomView;

/**
 * @brief The runtime Faust device's own controls, without a band of its own (#3205).
 *
 * The patch name becomes the slot's title with its credits in the tooltip, and the logo is a
 * watermark behind the grid. This component is the strip a compile error shows in, over the foot of
 * the body. Parameters are the slot's grid. A .dsp holds source only and sits in the preset box's
 * Patches submenu, apart from the MAGDA presets that also hold values, mods and macros (#3213).
 */
class FaustUI : public juce::Component {
  public:
    static constexpr int kErrorHeight = 16;

    /// The error strip's height: 0 unless the last load or edit left an error.
    int errorHeight() const;
    /** @brief Shows @p error in the strip; an empty one takes the strip away. */
    void setError(const juce::String& error);

    /** @brief Adds the Patches submenu: bundled and saved .dsp files, and file load and save. */
    void addPatchItems(juce::PopupMenu& menu);
    /** @brief Opens the patch's source in the code editor. */
    void showCodeEditor();
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
    /// The patch source this UI last bound, since a preset can swap it on the same device.
    const juce::String& boundSource() const {
        return boundSource_;
    }

    /// Path of the device this UI loads patches into.
    void setDevicePath(const ChainNodePath& path);

    void paint(juce::Graphics& g) override;
    void resized() override;
    // Label colours are cached by setColour, so they need re-applying when the
    // palette changes; paint-time roles look themselves up and self-heal.
    void lookAndFeelChanged() override;

  private:
    void loadFromFile();
    void saveDspToFile();
    // The kind of the bound device, defaulting to Effect while unbound so the
    // menu-building helpers have something to answer with.
    magda::daw::audio::FaustPatchKind patchKind() const;
    // The custom view comes from the source's own `declare magda_view`, so
    // every load path resolves it the same way with nothing to pass in.
    bool tryLoad(const juce::String& name, const juce::String& source);

    void refreshNameLabel();

    std::shared_ptr<magda::daw::audio::MagdaDevice> device_;
    magda::daw::audio::IFaustEditorModel* plugin_ = nullptr;
    juce::String boundSource_;
    ChainNodePath devicePath_;

    std::unique_ptr<juce::Drawable> logo_;
    juce::String patchName_;
    juce::String patchTooltip_;
    std::function<void(const juce::String&, const juce::String&)> onIdentityChanged_;
    juce::Label errorLabel_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    std::unique_ptr<class FaustCodeEditorWindow> editorWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustUI)
};

}  // namespace magda::daw::ui
