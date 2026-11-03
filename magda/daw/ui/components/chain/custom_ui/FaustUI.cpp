// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom_ui/FaustUI.hpp"

#include <BinaryData.h>

#include "audio/FaustResources.hpp"
#include "audio/faust/FaustModelEdits.hpp"
#include "audio/plugins/FaustParamPool.hpp"
#include "audio/plugins/IFaustEditorModel.hpp"
#include "audio/plugins/MagdaDevice.hpp"
#include "compiled/MagdaDriveCurveView.hpp"
#include "core/PresetManager.hpp"
#include "core/TrackManager.hpp"
#include "custom_ui/FaustCodeEditorWindow.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

FaustUI::FaustUI() {
    // First-touch registration of the built-in Faust custom views.
    // The function is defined in MagdaDriveCurveView.cpp; calling it
    // from here gives the linker a reason to keep that TU alive when
    // libmagda_daw_app is linked statically (a file-scope registrar
    // would be silently dropped because nothing else references the
    // TU's symbols). Idempotent — repeats just rewrite the same map
    // entries.
    static const bool builtInViewsRegistered = [] {
        registerBuiltInFaustCustomViews();
        return true;
    }();
    juce::ignoreUnused(builtInViewsRegistered);

    logo_ = juce::Drawable::createFromImageData(BinaryData::fausttextlogo_svg,
                                                BinaryData::fausttextlogo_svgSize);
    if (logo_)
        logo_->replaceColour(juce::Colour(0xFFD9D9D9), ActiveTheme::getSecondaryTextColour());
    if (logo_)
        ActiveTheme::applyToSvgIcon(*logo_);

    errorLabel_.setFont(FontManager::getInstance().getMonoFont(9.0f));
    errorLabel_.setColour(juce::Label::textColourId, juce::Colours::red);
    errorLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(errorLabel_);
}

FaustUI::~FaustUI() = default;

void FaustUI::setDevicePath(const ChainNodePath& path) {
    devicePath_ = path;
}

void FaustUI::setDevice(std::shared_ptr<magda::daw::audio::MagdaDevice> device) {
    device_ = std::move(device);
    plugin_ = dynamic_cast<magda::daw::audio::IFaustEditorModel*>(device_.get());
    boundSource_ = plugin_ != nullptr ? plugin_->getDspSource() : juce::String();
    refreshNameLabel();
}

namespace {

// Compose the patch's declared metadata into the name box's tooltip.
// Returns the bare name when the patch declares none of it, so a patch
// without metadata reads exactly as it did before.
juce::String describePatch(const juce::String& name,
                           const magda::daw::audio::FaustPatchInfo& info) {
    juce::String title = name;
    if (info.version.isNotEmpty())
        title << " " << info.version;
    if (info.isEmpty())
        return title;

    juce::StringArray lines;
    lines.add(title);
    if (info.author.isNotEmpty())
        lines.add("by " + info.author);
    if (info.license.isNotEmpty())
        lines.add(info.license);
    if (info.description.isNotEmpty()) {
        lines.add({});
        lines.add(info.description);
    }
    return lines.joinIntoString("\n");
}

}  // namespace

void FaustUI::refreshNameLabel() {
    patchName_ = plugin_ != nullptr ? plugin_->getDspName() : juce::String();
    patchTooltip_ =
        plugin_ != nullptr ? describePatch(patchName_, plugin_->getPatchInfo()) : juce::String();
    if (onIdentityChanged_)
        onIdentityChanged_(patchName_, patchTooltip_);
}

void FaustUI::setOnIdentityChanged(
    std::function<void(const juce::String&, const juce::String&)> cb) {
    onIdentityChanged_ = std::move(cb);
    refreshNameLabel();
}

int FaustUI::errorHeight() const {
    return errorLabel_.getText().isNotEmpty() ? kErrorHeight : 0;
}

void FaustUI::setError(const juce::String& error) {
    if (error == errorLabel_.getText())
        return;
    errorLabel_.setText(error, juce::dontSendNotification);
    errorLabel_.setTooltip(error);
    // The strip comes and goes with the error, so the slot re-carves.
    if (auto* parent = getParentComponent())
        parent->resized();
}

void FaustUI::paintWatermark(juce::Graphics& g, juce::Rectangle<float> area) const {
    if (logo_ == nullptr)
        return;
    const auto mark = area.withSizeKeepingCentre(juce::jmin(area.getWidth() * 0.5f, 220.0f),
                                                 juce::jmin(area.getHeight() * 0.5f, 60.0f));
    logo_->drawWithin(g, mark, juce::RectanglePlacement::centred, 0.06f);
}

bool FaustUI::tryLoad(const juce::String& name, const juce::String& source) {
    juce::String err;
    if (!faust_edits::loadSource(devicePath_, name, source, err)) {
        setError(err);
        return false;
    }
    setError({});
    // A .dsp replaces the source and nothing else, so no preset describes the device now.
    if (auto* live = magda::TrackManager::getInstance().getDeviceInChainByPath(devicePath_))
        live->presetName.clear();
    refreshNameLabel();

    // Surface any pool diagnostics (overflow / duplicate idx) in the
    // header's error label so silent failures don't slip through.
    if (plugin_ != nullptr) {
        const auto& diagnostics = plugin_->getLastRebindDiagnostics();
        if (!diagnostics.empty())
            setError(diagnostics.front());
    }
    return true;
}

namespace {

/**
 * @brief Where this kind's own .dsp files live, in the preset tree. Split by kind because an
 *        instrument patch synthesises against Faust's voice controls and can't load into an effect.
 */
juce::File patchLibraryDir(magda::daw::audio::FaustPatchKind kind) {
    return magda::PresetManager::getPresetsDirectory()
        .getChildFile("Faust Patches")
        .getChildFile(kind == magda::daw::audio::FaustPatchKind::Instrument ? "Instruments"
                                                                            : "Effects");
}

}  // namespace

void FaustUI::addPatchItems(juce::PopupMenu& menu) {
    const juce::Component::SafePointer<FaustUI> self(this);
    const auto kind = patchKind();
    const auto loaded = plugin_ != nullptr ? plugin_->getDspName() : juce::String();

    // Only this device kind's patches: an instrument cannot host an FX patch,
    // and vice versa. Grouped by the folder each .dsp was discovered in, which
    // getBundledStarterDsps sorts by, so each run of equal categories is one submenu.
    const auto starters = magda::daw::audio::getBundledStarterDsps(kind);
    juce::PopupMenu builtIn;
    juce::String currentCategory;
    juce::PopupMenu categoryMenu;
    auto flushCategory = [&builtIn, &categoryMenu, &currentCategory] {
        if (currentCategory.isNotEmpty()) {
            builtIn.addSubMenu(currentCategory.substring(0, 1).toUpperCase() +
                                   currentCategory.substring(1),
                               categoryMenu);
            categoryMenu = juce::PopupMenu();
        }
    };
    for (const auto& s : starters) {
        if (s.category != currentCategory) {
            flushCategory();
            currentCategory = s.category;
        }
        // A .dsp sitting at the root of the staged folder has no category and
        // is listed at the top level rather than under a submenu.
        auto& target = s.category.isEmpty() ? builtIn : categoryMenu;
        target.addItem(s.name, true, s.name == loaded, [self, name = s.name, source = s.source] {
            if (self != nullptr)
                self->tryLoad(name, source);
        });
    }
    flushCategory();

    juce::PopupMenu patches;
    if (!starters.empty())
        patches.addSubMenu("Built-in", builtIn);

    auto saved = patchLibraryDir(kind).findChildFiles(juce::File::findFiles, false, "*.dsp");
    saved.sort();
    if (!saved.isEmpty()) {
        juce::PopupMenu mine;
        for (const auto& file : saved) {
            const auto name = file.getFileNameWithoutExtension();
            mine.addItem(name, true, name == loaded, [self, name, file] {
                if (self != nullptr)
                    self->tryLoad(name, file.loadFileAsString());
            });
        }
        patches.addSubMenu("My Patches", mine);
    }

    patches.addSeparator();
    patches.addItem("Load .dsp File...", [self] {
        if (self != nullptr)
            self->loadFromFile();
    });
    patches.addItem("Save .dsp File...", [self] {
        if (self != nullptr)
            self->saveDspToFile();
    });

    menu.addSeparator();
    menu.addSubMenu("Patches", patches);
}

void FaustUI::loadFromFile() {
    fileChooser_ = std::make_unique<juce::FileChooser>("Choose a .dsp file",
                                                       patchLibraryDir(patchKind()), "*.dsp");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (!file.existsAsFile())
                return;
            tryLoad(file.getFileNameWithoutExtension(), file.loadFileAsString());
        });
}

void FaustUI::saveDspToFile() {
    if (plugin_ == nullptr)
        return;
    const auto source = plugin_->getDspSource();
    if (source.isEmpty())
        return;
    const auto kind = patchKind();
    auto name = plugin_->getDspName();
    if (name.isEmpty())
        name = kind == magda::daw::audio::FaustPatchKind::Instrument ? "instrument" : "effect";

    const auto dir = patchLibraryDir(kind);
    dir.createDirectory();
    fileChooser_ = std::make_unique<juce::FileChooser>("Save Faust DSP",
                                                       dir.getChildFile(name + ".dsp"), "*.dsp");
    fileChooser_->launchAsync(juce::FileBrowserComponent::saveMode |
                                  juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::warnAboutOverwriting,
                              [source](const juce::FileChooser& fc) {
                                  auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  if (!file.hasFileExtension("dsp"))
                                      file = file.withFileExtension("dsp");
                                  file.replaceWithText(source);
                              });
}

magda::daw::audio::FaustPatchKind FaustUI::patchKind() const {
    return plugin_ != nullptr ? plugin_->getPatchKind() : magda::daw::audio::FaustPatchKind::Effect;
}

void FaustUI::showCodeEditor() {
    if (plugin_ == nullptr)
        return;
    if (editorWindow_) {
        editorWindow_->setVisible(true);
        editorWindow_->toFront(true);
        return;
    }
    const auto title = juce::String::fromUTF8("Faust DSP \xe2\x80\x94 ") + plugin_->getDspName();
    const auto source = plugin_->getDspSource();
    editorWindow_ = std::make_unique<FaustCodeEditorWindow>(
        title, source, [this](const juce::String& src, juce::String& err) -> bool {
            if (plugin_ == nullptr)
                return false;
            auto editedName = plugin_->getDspName();
            if (editedName.isEmpty())
                editedName = "Custom";
            // No need to preserve the view across an in-place edit: it is read
            // back out of the edited source, so it survives as long as the
            // `declare magda_view` line does.
            if (!faust_edits::loadSource(devicePath_, editedName, src, err))
                return false;
            setError({});
            refreshNameLabel();
            return true;
        });
}

void FaustUI::paint(juce::Graphics& g) {
    g.setColour(ActiveTheme::getColour(ActiveTheme::BACKGROUND).withAlpha(0.92f));
    g.fillRect(getLocalBounds());
}

void FaustUI::resized() {
    errorLabel_.setBounds(getLocalBounds().reduced(8, 0));
}

void FaustUI::lookAndFeelChanged() {
    if (logo_) {
        logo_->replaceColour(juce::Colour(0xFFD9D9D9), ActiveTheme::getSecondaryTextColour());
        ActiveTheme::applyToSvgIcon(*logo_);
    }
}

}  // namespace magda::daw::ui
