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
#include "core/AppPaths.hpp"
#include "custom_ui/FaustCodeEditorWindow.hpp"
#include "ui/components/common/SvgButton.hpp"
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

    loadButton_ = std::make_unique<magda::SvgButton>("Load DSP", BinaryData::folderopen_svg,
                                                     BinaryData::folderopen_svgSize);
    loadButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    loadButton_->setIconPadding(1.5f);
    loadButton_->onClick = [this] { showLoadMenu(); };
    addChildComponent(*loadButton_);  // the slot's header shows it

    saveButton_ = std::make_unique<magda::SvgButton>("Save DSP", BinaryData::save_svg,
                                                     BinaryData::save_svgSize);
    saveButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    saveButton_->setIconPadding(3.0f);  // floppy glyph is denser; pad more to match Load/Edit
    saveButton_->onClick = [this] { saveDspToFile(); };
    addChildComponent(*saveButton_);  // the slot's header shows it

    editButton_ = std::make_unique<magda::SvgButton>("Edit DSP", BinaryData::code_blocks_svg,
                                                     BinaryData::code_blocks_svgSize);
    editButton_->setOriginalColor(juce::Colour(0xFFE3E3E3));
    loadButton_->setTooltip("Load a Faust patch");
    saveButton_->setTooltip("Save the patch's source");
    editButton_->setTooltip("Edit the patch's source");
    editButton_->setIconPadding(1.5f);
    editButton_->onClick = [this] { showCodeEditor(); };
    addChildComponent(*editButton_);  // the slot's header shows it
}

FaustUI::~FaustUI() = default;

void FaustUI::setDevicePath(const ChainNodePath& path) {
    devicePath_ = path;
}

void FaustUI::setDevice(std::shared_ptr<magda::daw::audio::MagdaDevice> device) {
    device_ = std::move(device);
    plugin_ = dynamic_cast<magda::daw::audio::IFaustEditorModel*>(device_.get());
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

std::vector<juce::Component*> FaustUI::headerControls() {
    return {editButton_.get(), loadButton_.get(), saveButton_.get()};
}

int FaustUI::errorHeight() const {
    return errorLabel_.getText().isNotEmpty() ? kErrorHeight : 0;
}

void FaustUI::setError(const juce::String& error) {
    if (error == errorLabel_.getText())
        return;
    setError(error);
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

void FaustUI::showLoadMenu() {
    if (plugin_ == nullptr)
        return;

    juce::PopupMenu menu;
    // Only this device kind's patches: an instrument cannot host an FX patch,
    // and vice versa. Grouped by the folder each .dsp was discovered in.
    // getBundledStarterDsps sorts by category, so each run of equal categories
    // is one submenu, and the ids stay in step with the vector index the
    // callback resolves.
    const auto kind = patchKind();
    auto starters = magda::daw::audio::getBundledStarterDsps(kind);
    int id = 1;
    juce::String currentCategory;
    juce::PopupMenu categoryMenu;
    auto flushCategory = [&menu, &categoryMenu, &currentCategory] {
        if (currentCategory.isNotEmpty()) {
            menu.addSubMenu(currentCategory.substring(0, 1).toUpperCase() +
                                currentCategory.substring(1),
                            categoryMenu);
            categoryMenu = juce::PopupMenu();
        }
    };
    for (const auto& s : starters) {
        // A .dsp sitting at the root of the staged folder has no category and
        // is listed at the top level rather than under a submenu.
        const auto& category = s.category;
        if (category != currentCategory) {
            flushCategory();
            currentCategory = category;
        }
        if (category.isEmpty())
            menu.addItem(id++, s.name);
        else
            categoryMenu.addItem(id++, s.name);
    }
    flushCategory();

    // User-saved patches from this kind's library (written by the save
    // button). Grouped in a submenu so the bundled starters stay tidy.
    auto savedFiles = userPatchDir(kind).findChildFiles(juce::File::findFiles, false, "*.dsp");
    savedFiles.sort();
    const int savedBaseId = id;
    if (!savedFiles.isEmpty()) {
        juce::PopupMenu savedMenu;
        for (const auto& f : savedFiles)
            savedMenu.addItem(id++, f.getFileNameWithoutExtension());
        menu.addSeparator();
        menu.addSubMenu(kind == magda::daw::audio::FaustPatchKind::Instrument ? "My Instruments"
                                                                              : "My Effects",
                        savedMenu);
    }

    menu.addSeparator();
    const int fromFileId = id;
    menu.addItem(fromFileId, "From file...");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(loadButton_.get()),
                       [this, starters, savedFiles, savedBaseId, fromFileId](int result) {
                           if (result <= 0)
                               return;
                           if (result == fromFileId) {
                               loadFromFile();
                               return;
                           }
                           if (result < savedBaseId) {
                               const int idx = result - 1;
                               if (idx >= 0 && idx < static_cast<int>(starters.size())) {
                                   const auto& s = starters[static_cast<size_t>(idx)];
                                   tryLoad(s.name, s.source);
                               }
                               return;
                           }
                           const int idx = result - savedBaseId;
                           if (idx >= 0 && idx < savedFiles.size()) {
                               const auto file = savedFiles[idx];
                               if (file.existsAsFile())
                                   tryLoad(file.getFileNameWithoutExtension(),
                                           file.loadFileAsString());
                           }
                       });
}

void FaustUI::loadFromFile() {
    fileChooser_ = std::make_unique<juce::FileChooser>("Choose a .dsp file",
                                                       userPatchDir(patchKind()), "*.dsp");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (!file.existsAsFile())
                return;
            tryLoad(file.getFileNameWithoutExtension(), file.loadFileAsString());
        });
}

magda::daw::audio::FaustPatchKind FaustUI::patchKind() const {
    return plugin_ != nullptr ? plugin_->getPatchKind() : magda::daw::audio::FaustPatchKind::Effect;
}

juce::File FaustUI::userPatchDir(magda::daw::audio::FaustPatchKind kind) {
    // Patches saved before the split all live in FaustEffects, instruments
    // included. They stay listed under FX rather than being moved: the folder
    // is the user's, and nothing about an unmoved file breaks.
    auto dir = magda::paths::dataDir().getChildFile(
        kind == magda::daw::audio::FaustPatchKind::Instrument ? "FaustInstruments"
                                                              : "FaustEffects");
    dir.createDirectory();
    return dir;
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

    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Save Faust DSP", userPatchDir(kind).getChildFile(name + ".dsp"), "*.dsp");
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
