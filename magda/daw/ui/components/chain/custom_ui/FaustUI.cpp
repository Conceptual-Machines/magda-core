#include "custom_ui/FaustUI.hpp"

#include <BinaryData.h>
#include <tracktion_engine/tracktion_engine.h>

#include "audio/AudioBridge.hpp"
#include "audio/FaustResources.hpp"
#include "audio/plugin_manager/PluginManager.hpp"
#include "audio/plugins/FaustPlugin.hpp"
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

    nameLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    nameLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(nameLabel_);

    errorLabel_.setFont(FontManager::getInstance().getMonoFont(9.0f));
    errorLabel_.setColour(juce::Label::textColourId, juce::Colours::red);
    errorLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(errorLabel_);

    loadButton_ = std::make_unique<magda::SvgButton>("Load DSP", BinaryData::folderopen_svg,
                                                     BinaryData::folderopen_svgSize);
    loadButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    loadButton_->setIconPadding(2.0f);
    loadButton_->onClick = [this] { showLoadMenu(); };
    addAndMakeVisible(*loadButton_);

    saveButton_ = std::make_unique<magda::SvgButton>("Save DSP", BinaryData::save_svg,
                                                     BinaryData::save_svgSize);
    saveButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    saveButton_->setIconPadding(4.0f);  // floppy glyph is denser; pad more to match Load/Edit
    saveButton_->onClick = [this] { saveDspToFile(); };
    addAndMakeVisible(*saveButton_);

    editButton_ = std::make_unique<magda::SvgButton>("Edit DSP", BinaryData::script_svg,
                                                     BinaryData::script_svgSize);
    editButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    editButton_->setIconPadding(2.0f);
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

void FaustUI::setPlugin(magda::daw::audio::FaustPlugin* plugin) {
    plugin_ = plugin;
    DBG("[FaustUI] setPlugin: " << (plugin ? "ok" : "NULL"));
    refreshNameLabel();
}

void FaustUI::refreshNameLabel() {
    if (plugin_ == nullptr) {
        nameLabel_.setText({}, juce::dontSendNotification);
        return;
    }
    nameLabel_.setText(plugin_->state.getProperty("dspName", juce::String()).toString(),
                       juce::dontSendNotification);
}

bool FaustUI::tryLoad(const juce::String& name, const juce::String& source,
                      magda::daw::audio::FaustCustomViewKind viewKind) {
    DBG("[FaustUI] tryLoad name='" << name << "' src.len=" << source.length()
                                   << " viewKind=" << static_cast<int>(viewKind));
    if (plugin_ == nullptr) {
        DBG("[FaustUI] tryLoad: plugin_ is NULL — bailing");
        return false;
    }
    juce::String err;
    if (!plugin_->loadDspSource(name, source, err, viewKind)) {
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
    auto starters = magda::daw::audio::getBundledStarterDsps();
    int id = 1;
    for (const auto& s : starters)
        menu.addItem(id++, s.name);

    // User-saved effects from the FaustEffects library (written by the save
    // button). Grouped in a submenu so the bundled starters stay tidy.
    auto savedFiles = userEffectsDir().findChildFiles(juce::File::findFiles, false, "*.dsp");
    savedFiles.sort();
    const int savedBaseId = id;
    if (!savedFiles.isEmpty()) {
        juce::PopupMenu savedMenu;
        for (const auto& f : savedFiles)
            savedMenu.addItem(id++, f.getFileNameWithoutExtension());
        menu.addSeparator();
        menu.addSubMenu("My Effects", savedMenu);
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
                                   tryLoad(s.name, s.source, s.viewKind);
                               }
                               return;
                           }
                           const int idx = result - savedBaseId;
                           if (idx >= 0 && idx < savedFiles.size()) {
                               const auto file = savedFiles[idx];
                               if (file.existsAsFile())
                                   tryLoad(file.getFileNameWithoutExtension(),
                                           file.loadFileAsString(),
                                           magda::daw::audio::FaustCustomViewKind::None);
                           }
                       });
}

void FaustUI::loadFromFile() {
    fileChooser_ = std::make_unique<juce::FileChooser>("Choose a .dsp file",
                                                       FaustUI::userEffectsDir(), "*.dsp");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (!file.existsAsFile() || plugin_ == nullptr)
                return;
            tryLoad(file.getFileNameWithoutExtension(), file.loadFileAsString(),
                    magda::daw::audio::FaustCustomViewKind::None);
        });
}

juce::File FaustUI::userEffectsDir() {
    auto dir = magda::paths::dataDir().getChildFile("FaustEffects");
    dir.createDirectory();
    return dir;
}

void FaustUI::saveDspToFile() {
    if (plugin_ == nullptr)
        return;
    const auto source = plugin_->state.getProperty("dspSource", juce::String()).toString();
    if (source.isEmpty())
        return;
    const auto name = plugin_->state.getProperty("dspName", juce::String("effect")).toString();

    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Save Faust DSP", userEffectsDir().getChildFile(name + ".dsp"), "*.dsp");
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
    const auto title = juce::String::fromUTF8("Faust DSP \xe2\x80\x94 ") +
                       plugin_->state.getProperty("dspName", juce::String()).toString();
    const auto source = plugin_->state.getProperty("dspSource", juce::String()).toString();
    editorWindow_ = std::make_unique<FaustCodeEditorWindow>(
        title, source, [this](const juce::String& src, juce::String& err) -> bool {
            if (plugin_ == nullptr)
                return false;
            const auto editedName =
                plugin_->state.getProperty("dspName", juce::String("Custom")).toString();
            // Preserve the existing custom-view kind across in-place
            // edits — a user tweaking the bundled MagdaDrive source
            // shouldn't lose its bespoke view because the code editor
            // re-saved the same DSP.
            const auto preservedKind = plugin_->getCustomViewKind();
            if (!plugin_->loadDspSource(editedName, src, err, preservedKind))
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
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds());

    if (logo_) {
        logo_->drawWithin(g, logoBounds_,
                          juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, 0.7f);
    }

    if (!nameBorderBounds_.isEmpty()) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(nameBorderBounds_, 3.0f, 1.0f);
    }

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    const auto bounds = getLocalBounds();
    g.drawHorizontalLine(bounds.getBottom() - 1, static_cast<float>(bounds.getX()),
                         static_cast<float>(bounds.getRight()));
}

void FaustUI::resized() {
    auto area = getLocalBounds().reduced(6, 4);

    // Logo on the left.
    logoBounds_ = area.removeFromLeft(72).toFloat();
    area.removeFromLeft(6);

    // Save / Load / Edit icons on the right (ordered left-to-right Save,
    // Load, Edit so removeFromRight in reverse order). Save sits next to the
    // Load folder.
    constexpr int iconSize = 22;
    editButton_->setBounds(
        area.removeFromRight(iconSize).withSizeKeepingCentre(iconSize, iconSize));
    area.removeFromRight(4);
    loadButton_->setBounds(
        area.removeFromRight(iconSize).withSizeKeepingCentre(iconSize, iconSize));
    area.removeFromRight(4);
    saveButton_->setBounds(
        area.removeFromRight(iconSize).withSizeKeepingCentre(iconSize, iconSize));
    area.removeFromRight(6);

    // Optional error / diagnostic text right of the name; takes a
    // chunk of width when present.
    if (errorLabel_.getText().isNotEmpty()) {
        const int errWidth = juce::jmin(area.getWidth() / 2, 200);
        errorLabel_.setBounds(area.removeFromRight(errWidth));
        area.removeFromRight(4);
    }

    // Name box fills the middle.
    nameBorderBounds_ = area.toFloat().reduced(0.0f, 1.0f);
    nameLabel_.setBounds(area.reduced(8, 2));
}

}  // namespace magda::daw::ui
