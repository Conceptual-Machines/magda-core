#include "custom_ui/FaustUI.hpp"

#include <BinaryData.h>
#include <tracktion_engine/tracktion_engine.h>

#include "audio/AudioBridge.hpp"
#include "audio/FaustResources.hpp"
#include "audio/plugin_manager/PluginManager.hpp"
#include "audio/plugins/FaustParamPool.hpp"
#include "audio/plugins/IFaustEditorModel.hpp"
#include "compiled/MagdaDriveCurveView.hpp"
#include "core/AppPaths.hpp"
#include "core/TrackManager.hpp"
#include "custom_ui/FaustCodeEditorWindow.hpp"
#include "engine/AudioEngine.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace te = tracktion::engine;

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
        logo_->replaceColour(juce::Colour(0xFFD9D9D9), DarkTheme::getSecondaryTextColour());
    if (logo_)
        DarkTheme::applyToSvgIcon(*logo_);

    nameLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(nameLabel_);

    errorLabel_.setFont(FontManager::getInstance().getMonoFont(9.0f));
    errorLabel_.setColour(juce::Label::textColourId, juce::Colours::red);
    errorLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(errorLabel_);

    loadButton_ = std::make_unique<magda::SvgButton>("Load DSP", BinaryData::folderopen_svg,
                                                     BinaryData::folderopen_svgSize);
    loadButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    loadButton_->setIconPadding(1.5f);
    loadButton_->onClick = [this] { showLoadMenu(); };
    addAndMakeVisible(*loadButton_);

    saveButton_ = std::make_unique<magda::SvgButton>("Save DSP", BinaryData::save_svg,
                                                     BinaryData::save_svgSize);
    saveButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    saveButton_->setIconPadding(3.0f);  // floppy glyph is denser; pad more to match Load/Edit
    saveButton_->onClick = [this] { saveDspToFile(); };
    addAndMakeVisible(*saveButton_);

    editButton_ = std::make_unique<magda::SvgButton>("Edit DSP", BinaryData::code_blocks_svg,
                                                     BinaryData::code_blocks_svgSize);
    editButton_->setOriginalColor(juce::Colour(0xFFE3E3E3));
    editButton_->setIconPadding(1.5f);
    editButton_->onClick = [this] { showCodeEditor(); };
    addAndMakeVisible(*editButton_);
}

FaustUI::~FaustUI() = default;

void FaustUI::setDevicePath(const ChainNodePath& path) {
    devicePath_ = path;
    DBG("[FaustUI] setDevicePath trackId=" << path.trackId
                                           << " topLevelDevice=" << (int)path.topLevelDeviceId
                                           << " steps=" << static_cast<int>(path.steps.size()));
}

void FaustUI::setPlugin(magda::daw::audio::IFaustEditorModel* plugin) {
    plugin_ = plugin;
    DBG("[FaustUI] setPlugin: " << (plugin ? "ok" : "NULL"));
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

// One-line credit for the meta row. Description is deliberately left out
// It is prose and belongs in the tooltip, not a 12px row.
juce::String creditLine(const magda::daw::audio::FaustPatchInfo& info) {
    juce::StringArray parts;
    if (info.author.isNotEmpty())
        parts.add(info.author);
    if (info.version.isNotEmpty())
        parts.add("v" + info.version);
    if (info.license.isNotEmpty())
        parts.add(info.license);
    return parts.joinIntoString("  |  ");
}

}  // namespace

void FaustUI::refreshNameLabel() {
    const bool wasShowingMetaRow = showMetaRow_;

    if (plugin_ == nullptr) {
        nameLabel_.setText({}, juce::dontSendNotification);
        nameLabel_.setTooltip({});
        showMetaRow_ = false;
        metaText_ = {};
    } else {
        const auto name = plugin_->getDspName();
        const auto info = plugin_->getPatchInfo();
        nameLabel_.setText(name, juce::dontSendNotification);
        nameLabel_.setTooltip(describePatch(name, info));
        metaText_ = creditLine(info);
        showMetaRow_ = !info.isEmpty();
    }

    // The meta row changes this component's desired height, so the parent has
    // to re-carve. Without this the row would only appear after some
    // unrelated resize.
    if (showMetaRow_ != wasShowingMetaRow) {
        if (auto* parent = getParentComponent())
            parent->resized();
    }
    resized();
    repaint();
}

bool FaustUI::tryLoad(const juce::String& name, const juce::String& source) {
    DBG("[FaustUI] tryLoad name='" << name << "' src.len=" << source.length());
    if (plugin_ == nullptr) {
        DBG("[FaustUI] tryLoad: plugin_ is NULL — bailing");
        return false;
    }
    juce::String err;
    if (!plugin_->loadDspSource(name, source, err)) {
        DBG("[FaustUI] tryLoad: loadDspSource FAILED: " << err);
        errorLabel_.setText(err, juce::dontSendNotification);
        return false;
    }
    DBG("[FaustUI] tryLoad: loadDspSource OK, pool active=" << plugin_->getPool().activeCount());
    errorLabel_.setText({}, juce::dontSendNotification);
    refreshNameLabel();

    // Surface any pool diagnostics (overflow / duplicate idx) in the
    // header's error label so silent failures don't slip through.
    const auto& diagnostics = plugin_->getLastRebindDiagnostics();
    if (!diagnostics.empty())
        errorLabel_.setText(diagnostics.front(), juce::dontSendNotification);

    // Push the now-active pool layout into TrackManager.DeviceInfo so
    // the slot rebuild reads fresh ParameterInfo from FaustProcessor.
    // populateParameters runs once at processor registration; we have
    // to nudge it here because Faust's parameter set changes at
    // runtime. Then notify so the chain UI rebuilds against the new
    // DeviceInfo.
    auto& tm = TrackManager::getInstance();
    auto* dev = tm.getDeviceInChainByPath(devicePath_);
    DBG("[FaustUI] tryLoad: device-by-path lookup " << (dev ? "ok" : "NULL")
                                                    << " trackId=" << devicePath_.trackId);
    if (dev) {
        if (auto* engine = tm.getAudioEngine()) {
            if (auto* bridge = engine->getAudioBridge()) {
                DBG("[FaustUI] tryLoad: calling refreshDeviceParameters for path deviceId="
                    << (int)devicePath_.getDeviceId());
                bridge->getPluginManager().refreshDeviceParameters(devicePath_);
                bridge->getPluginManager().capturePluginState(devicePath_);
            } else {
                DBG("[FaustUI] tryLoad: AudioBridge is NULL");
            }
        } else {
            DBG("[FaustUI] tryLoad: AudioEngine is NULL");
        }
    }

    // notifyTrackDevicesChanged tears down the DeviceSlotComponent that
    // owns this FaustUI — calling it inline destroys `this` mid-method
    // and the rest of tryLoad runs on freed memory. Defer to the next
    // message-thread tick so the modal-callback frame can unwind first.
    // Lambda captures trackId by value, so it doesn't touch `this`.
    if (devicePath_.trackId != INVALID_TRACK_ID) {
        const auto trackId = devicePath_.trackId;
        DBG("[FaustUI] tryLoad: queuing notifyTrackDevicesChanged trackId=" << trackId);
        juce::MessageManager::callAsync(
            [trackId]() { TrackManager::getInstance().notifyTrackDevicesChanged(trackId); });
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
            if (!file.existsAsFile() || plugin_ == nullptr)
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
            if (!plugin_->loadDspSource(editedName, src, err))
                return false;
            errorLabel_.setText({}, juce::dontSendNotification);
            refreshNameLabel();
            auto& tm = TrackManager::getInstance();
            if (auto* dev = tm.getDeviceInChainByPath(devicePath_)) {
                if (auto* engine = tm.getAudioEngine()) {
                    if (auto* bridge = engine->getAudioBridge()) {
                        bridge->getPluginManager().refreshDeviceParameters(devicePath_);
                        bridge->getPluginManager().capturePluginState(devicePath_);
                    }
                }
            }
            // Same deferred-notify rule as tryLoad — the rebuild
            // destroys `this` synchronously.
            if (devicePath_.trackId != INVALID_TRACK_ID) {
                const auto trackId = devicePath_.trackId;
                juce::MessageManager::callAsync([trackId]() {
                    TrackManager::getInstance().notifyTrackDevicesChanged(trackId);
                });
            }
            return true;
        });
}

void FaustUI::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();

    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(bounds);

    if (logo_) {
        logo_->drawWithin(g, logoBounds_,
                          juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, 0.7f);
    }

    // Single vertical rules between the three bands. Full height, so the
    // strip reads as columns rather than a boxed name with loose icons.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawVerticalLine(logoRuleX_, static_cast<float>(bounds.getY()),
                       static_cast<float>(bounds.getBottom()));
    g.drawVerticalLine(actionRuleX_, static_cast<float>(bounds.getY()),
                       static_cast<float>(bounds.getBottom()));

    // Second identity row: the credit line under the patch name it describes.
    if (showMetaRow_ && metaText_.isNotEmpty() && !metaBounds_.isEmpty()) {
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(9.0f));
        g.drawText(metaText_, metaBounds_, juce::Justification::centredLeft, true);
    }

    // Re-set the colour: the meta row above leaves the text colour behind.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(bounds.getBottom() - 1, static_cast<float>(bounds.getX()),
                         static_cast<float>(bounds.getRight()));
}

void FaustUI::resized() {
    constexpr int kBandPadding = 8;
    constexpr int kLogoBandWidth = 84;
    constexpr int kIconSize = 16;
    constexpr int kIconGap = 6;
    constexpr int kActionBandWidth = 2 * kBandPadding + 3 * kIconSize + 2 * kIconGap;
    constexpr int kNameRowHeight = 16;

    auto bounds = getLocalBounds();

    // Band 1: logo. Vertically centred over the whole strip, so it stays put
    // when the meta row appears.
    auto logoBand = bounds.removeFromLeft(kLogoBandWidth);
    logoBounds_ = logoBand.reduced(kBandPadding, 8).toFloat();

    logoRuleX_ = bounds.getX();
    bounds.removeFromLeft(1);

    // Band 3: Save / Load / Edit, left to right, on a fixed cell grid so the
    // icons line up with the rule instead of floating on button metrics.
    auto actionBand = bounds.removeFromRight(kActionBandWidth);
    actionRuleX_ = actionBand.getX();
    bounds.removeFromRight(1);

    auto iconRow = actionBand.reduced(kBandPadding, 0);
    auto placeIcon = [&iconRow](magda::SvgButton& button) {
        button.setBounds(
            iconRow.removeFromLeft(kIconSize).withSizeKeepingCentre(kIconSize, kIconSize));
        iconRow.removeFromLeft(kIconGap);
    };
    placeIcon(*saveButton_);
    placeIcon(*loadButton_);
    placeIcon(*editButton_);

    // Band 2: patch name over its metadata. The two rows are centred as one
    // block, so a patch without metadata centres its name instead of leaving
    // a gap where the second row would have been.
    auto identityBand = bounds.reduced(kBandPadding, 4);

    if (errorLabel_.getText().isNotEmpty()) {
        const int errWidth = juce::jmin(identityBand.getWidth() / 2, 200);
        errorLabel_.setBounds(identityBand.removeFromRight(errWidth));
        identityBand.removeFromRight(4);
    }

    const int blockHeight = kNameRowHeight + (showMetaRow_ ? kMetaRowHeight : 0);
    auto textBlock = identityBand.withSizeKeepingCentre(identityBand.getWidth(), blockHeight);
    nameLabel_.setBounds(textBlock.removeFromTop(kNameRowHeight));
    metaBounds_ = showMetaRow_ ? textBlock : juce::Rectangle<int>{};
}

void FaustUI::lookAndFeelChanged() {
    if (logo_) {
        logo_->replaceColour(juce::Colour(0xFFD9D9D9), DarkTheme::getSecondaryTextColour());
        DarkTheme::applyToSvgIcon(*logo_);
    }
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
}

}  // namespace magda::daw::ui
