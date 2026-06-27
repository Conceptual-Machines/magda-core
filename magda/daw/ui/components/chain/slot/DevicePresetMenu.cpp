#include "slot/DevicePresetMenu.hpp"

#include "audio/AudioBridge.hpp"
#include "core/PluginPresetScanner.hpp"
#include "core/PresetManager.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kSaveOverwrite = 1;
constexpr int kSaveAs = 2;
constexpr int kRevealInFinder = 3;
constexpr int kPresetIdBase = 1000;
constexpr int kProgramIdBase = 5000;
constexpr int kSavePluginPresetAs = 1;
constexpr int kRevealPluginUserDir = 2;
constexpr int kRescanPluginPresets = 3;

void showPresetErrorAsync(const juce::String& title, const juce::String& message) {
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle(title)
                                     .withMessage(message)
                                     .withButton("OK"),
                                 nullptr);
}

magda::AudioBridge* getAudioBridge() {
    auto* engine = magda::TrackManager::getInstance().getAudioEngine();
    return engine != nullptr ? engine->getAudioBridge() : nullptr;
}

void buildPresetSubmenu(juce::PopupMenu& menu, const juce::File& dir, const juce::String& prefix,
                        int idBase, const juce::String& currentLoaded,
                        juce::StringArray& outIndex) {
    if (!dir.isDirectory())
        return;

    auto subdirs = dir.findChildFiles(juce::File::findDirectories, false);
    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.mps");
    subdirs.sort();
    files.sort();

    for (const auto& sub : subdirs) {
        juce::PopupMenu submenu;
        buildPresetSubmenu(submenu, sub, prefix + sub.getFileName() + "/", idBase, currentLoaded,
                           outIndex);
        menu.addSubMenu(sub.getFileName(), submenu);
    }

    for (const auto& file : files) {
        const auto displayName = file.getFileNameWithoutExtension();
        const auto relPath = prefix + displayName;
        outIndex.add(relPath);
        menu.addItem(idBase + outIndex.size() - 1, displayName, true, relPath == currentLoaded);
    }
}

void buildPluginPresetSubmenu(juce::PopupMenu& menu,
                              const std::vector<magda::PluginPresetScanner::Entry>& entries,
                              int idBase, juce::Array<juce::File>& outFiles,
                              const juce::File& currentLoaded) {
    for (const auto& entry : entries) {
        if (entry.isFolder) {
            juce::PopupMenu submenu;
            buildPluginPresetSubmenu(submenu, entry.children, idBase, outFiles, currentLoaded);
            if (submenu.getNumItems() > 0)
                menu.addSubMenu(entry.name, submenu);
        } else {
            outFiles.add(entry.file);
            menu.addItem(idBase + outFiles.size() - 1, entry.name, true,
                         currentLoaded == entry.file);
        }
    }
}

juce::String cleanCategory(juce::String category) {
    while (category.startsWithChar('/'))
        category = category.substring(1);
    while (category.endsWithChar('/'))
        category = category.dropLastCharacters(1);
    return category;
}

class PluginPresetsButtonLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& /*bgColour*/, bool isHighlighted,
                              bool isDown) override {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto bg = DarkTheme::getColour(DarkTheme::SURFACE);
        if (isDown)
            bg = bg.darker(0.2f);
        else if (isHighlighted)
            bg = bg.brighter(0.1f);
        g.setColour(bg);
        g.fillRoundedRectangle(bounds, 3.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool /*highlighted*/,
                        bool /*down*/) override {
        auto bounds = button.getLocalBounds().reduced(6, 0);
        constexpr float chevronW = 10.0f;
        auto chevronArea = bounds.removeFromRight((int)chevronW).toFloat();

        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.setColour(
            DarkTheme::getTextColour().withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
        g.drawText(button.getButtonText(), bounds.toFloat(), juce::Justification::centredLeft,
                   /*useEllipses*/ true);

        const float cx = chevronArea.getCentreX();
        const float cy = chevronArea.getCentreY() + 1.0f;
        constexpr float halfSize = 2.5f;
        juce::Path chevron;
        chevron.startNewSubPath(cx - halfSize, cy - 1.0f);
        chevron.lineTo(cx, cy + 1.5f);
        chevron.lineTo(cx + halfSize, cy - 1.0f);
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.strokePath(chevron, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }
};

}  // namespace

void showMagdaPresetMenu(juce::Component* targetComponent, const juce::String& pluginFolder,
                         const juce::String& currentPresetName, MagdaPresetMenuActions actions) {
    auto& presetManager = magda::PresetManager::getInstance();

    juce::PopupMenu menu;
    menu.addSectionHeader("MAGDA Presets");

    juce::StringArray indexedPresetPaths;
    buildPresetSubmenu(menu, presetManager.getDevicePluginDirectory(pluginFolder), "",
                       kPresetIdBase, currentPresetName, indexedPresetPaths);

    if (indexedPresetPaths.isEmpty())
        menu.addItem(kPresetIdBase, "(no presets yet)", false);

    menu.addSeparator();
    if (currentPresetName.isNotEmpty())
        menu.addItem(kSaveOverwrite, "Save \"" + currentPresetName + "\"");
    menu.addItem(kSaveAs, "Save as MAGDA Preset...");
    menu.addItem(kRevealInFinder, "Reveal in Finder");

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(targetComponent),
        [pluginFolder, indexedPresetPaths, actions = std::move(actions)](int chosen) {
            if (chosen == 0)
                return;

            if (chosen == kSaveAs) {
                if (actions.saveAs)
                    actions.saveAs();
                return;
            }

            if (chosen == kSaveOverwrite) {
                if (actions.saveCurrent)
                    actions.saveCurrent();
                return;
            }

            if (chosen == kRevealInFinder) {
                auto& presetManager = magda::PresetManager::getInstance();
                auto dir = presetManager.getDevicePluginDirectory(pluginFolder);
                if (!dir.exists())
                    dir = presetManager.getDevicesDirectory();
                dir.revealToUser();
                return;
            }

            if (chosen < kPresetIdBase)
                return;

            const int idx = chosen - kPresetIdBase;
            if (idx >= 0 && idx < indexedPresetPaths.size() && actions.loadPreset)
                actions.loadPreset(indexedPresetPaths[idx]);
        });
}

std::optional<magda::DeviceInfo> snapshotDeviceForPreset(const magda::DeviceInfo& fallbackDevice,
                                                         const magda::ChainNodePath& nodePath) {
    auto& trackManager = magda::TrackManager::getInstance();
    if (auto* bridge = getAudioBridge())
        bridge->getPluginManager().capturePluginState(nodePath);

    if (auto* live = trackManager.getDeviceInChainByPath(nodePath))
        return *live;

    return fallbackDevice;
}

void showSaveMagdaPresetDialog(const magda::DeviceInfo& device,
                               const juce::String& currentPresetName,
                               PresetSnapshotProvider snapshotProvider,
                               std::function<void(const juce::String& presetName)> onSaved) {
    auto* alert = new juce::AlertWindow(
        "Save MAGDA Preset", "Enter a name and optional category for this device preset:",
        juce::MessageBoxIconType::NoIcon);

    auto defaultPath = currentPresetName;
    if (defaultPath.isEmpty())
        defaultPath = magda::PresetManager::getInstance().getSuggestedPresetName(device.id);
    if (defaultPath.isEmpty())
        defaultPath = device.name;

    juce::String defaultCategory;
    auto defaultName = defaultPath;
    const auto slash = defaultPath.lastIndexOfChar('/');
    if (slash > 0) {
        defaultCategory = defaultPath.substring(0, slash);
        defaultName = defaultPath.substring(slash + 1);
    }

    alert->addTextEditor("category", defaultCategory, "Category (optional):");
    alert->addTextEditor("name", defaultName, "Name:");
    alert->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    alert->enterModalState(
        true, juce::ModalCallbackFunction::create([alert, pluginFolder = device.name,
                                                   snapshotProvider = std::move(snapshotProvider),
                                                   onSaved = std::move(onSaved)](int result) {
            if (result != 1) {
                delete alert;
                return;
            }

            const auto name = alert->getTextEditorContents("name").trim();
            const auto category = cleanCategory(alert->getTextEditorContents("category").trim());
            delete alert;

            if (name.isEmpty())
                return;

            const auto fullName = category.isEmpty() ? name : (category + "/" + name);
            auto doSave = [fullName, snapshotProvider, onSaved]() {
                if (!snapshotProvider)
                    return;

                auto snapshot = snapshotProvider();
                if (!snapshot.has_value())
                    return;

                auto& presetManager = magda::PresetManager::getInstance();
                if (!presetManager.saveDevicePreset(*snapshot, fullName)) {
                    showPresetErrorAsync("Save Preset Failed", presetManager.getLastError());
                    return;
                }

                if (onSaved)
                    onSaved(fullName);
            };

            if (magda::PresetManager::getInstance()
                    .getDevicePresets(pluginFolder)
                    .contains(fullName)) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::QuestionIcon)
                        .withTitle("Overwrite Preset?")
                        .withMessage("\"" + fullName + "\" already exists. Overwrite?")
                        .withButton("Overwrite")
                        .withButton("Cancel"),
                    [doSave](int choice) {
                        if (choice == 1)
                            doSave();
                    });
            } else {
                doSave();
            }
        }));
}

void saveCurrentMagdaPreset(const juce::String& currentPresetName,
                            PresetSnapshotProvider snapshotProvider) {
    if (currentPresetName.isEmpty() || !snapshotProvider)
        return;

    auto snapshot = snapshotProvider();
    if (!snapshot.has_value())
        return;

    auto& presetManager = magda::PresetManager::getInstance();
    if (!presetManager.saveDevicePreset(*snapshot, currentPresetName))
        showPresetErrorAsync("Save Preset Failed", presetManager.getLastError());
}

void loadMagdaPreset(
    const juce::String& pluginFolder, const magda::ChainNodePath& nodePath,
    const juce::String& presetRelativePath,
    std::function<void(const magda::DeviceInfo& liveDevice, const juce::String& presetName)>
        onLoaded) {
    magda::DeviceInfo preset;
    auto& presetManager = magda::PresetManager::getInstance();
    if (!presetManager.loadDevicePreset(pluginFolder, presetRelativePath, preset)) {
        showPresetErrorAsync("Load Preset Failed", presetManager.getLastError());
        return;
    }

    auto& trackManager = magda::TrackManager::getInstance();
    if (!trackManager.applyDevicePreset(nodePath, preset)) {
        showPresetErrorAsync("Load Preset Failed",
                             "Preset is for a different plugin (\"" + preset.pluginId + "\").");
        return;
    }

    if (auto* live = trackManager.getDeviceInChainByPath(nodePath)) {
        if (onLoaded)
            onLoaded(*live, presetRelativePath);
    }
}

struct MagdaDevicePresetPresenter::State {
    juce::String currentPresetName;
};

MagdaDevicePresetPresenter::MagdaDevicePresetPresenter() : state_(std::make_shared<State>()) {}

void MagdaDevicePresetPresenter::clearCurrentPreset() {
    state_->currentPresetName.clear();
}

void MagdaDevicePresetPresenter::showMenu(
    juce::Component* targetComponent, const magda::DeviceInfo& device,
    const magda::ChainNodePath& devicePath,
    std::function<void(const magda::DeviceInfo& liveDevice)> onLoaded) {
    auto state = state_;
    const auto snapshotProvider = [device, devicePath]() -> std::optional<magda::DeviceInfo> {
        return snapshotDeviceForPreset(device, devicePath);
    };

    MagdaPresetMenuActions actions;
    actions.saveAs = [state, device, snapshotProvider]() {
        showSaveMagdaPresetDialog(
            device, state->currentPresetName, snapshotProvider,
            [state](const juce::String& presetName) { state->currentPresetName = presetName; });
    };
    actions.saveCurrent = [state, snapshotProvider]() {
        saveCurrentMagdaPreset(state->currentPresetName, snapshotProvider);
    };
    actions.loadPreset = [state, device, devicePath,
                          onLoaded = std::move(onLoaded)](const juce::String& presetRelativePath) {
        loadMagdaPreset(
            device.name, devicePath, presetRelativePath,
            [state, onLoaded](const magda::DeviceInfo& liveDevice, const juce::String& presetName) {
                state->currentPresetName = presetName;
                if (onLoaded)
                    onLoaded(liveDevice);
            });
    };

    showMagdaPresetMenu(targetComponent, device.name, state->currentPresetName, std::move(actions));
}

bool hasPluginPresetsAvailable(const magda::DeviceInfo& device, bool isInternalDevice) {
    if (isInternalDevice || device.loadState != magda::DeviceLoadState::Loaded)
        return false;

    return !magda::PluginPresetScanner::getInstance().getPresets(device).empty();
}

struct PluginDevicePresetPresenter::State {
    juce::File currentPresetFile;
    juce::String presetName;
};

PluginDevicePresetPresenter::PluginDevicePresetPresenter() : state_(std::make_shared<State>()) {}

void PluginDevicePresetPresenter::clearCurrentPreset() {
    state_->currentPresetFile = juce::File();
    state_->presetName.clear();
}

juce::String PluginDevicePresetPresenter::getCurrentPresetLabel() const {
    return state_->presetName.isNotEmpty() ? state_->presetName : juce::String("Presets");
}

void PluginDevicePresetPresenter::showMenu(juce::Component* targetComponent,
                                           const magda::DeviceInfo& device,
                                           const magda::ChainNodePath& devicePath,
                                           bool isInternalDevice,
                                           std::function<void()> onSelectionChanged) {
    auto state = state_;
    PluginPresetMenuActions actions;
    actions.saveAs = [this, device, devicePath, onSelectionChanged]() {
        showSaveDialog(device, devicePath, onSelectionChanged);
    };
    actions.loadFile = [this, devicePath, onSelectionChanged](const juce::File& file) {
        loadFile(devicePath, file, onSelectionChanged);
    };
    actions.selectionChanged = [state, onSelectionChanged](const juce::File& currentFile,
                                                           const juce::String& displayName) {
        state->currentPresetFile = currentFile;
        state->presetName = displayName;
        if (onSelectionChanged)
            onSelectionChanged();
    };

    showPluginPresetMenu(targetComponent, device, devicePath, isInternalDevice,
                         state->currentPresetFile, std::move(actions));
}

void PluginDevicePresetPresenter::loadFile(const magda::ChainNodePath& devicePath,
                                           const juce::File& file,
                                           std::function<void()> onSelectionChanged) {
    auto state = state_;
    loadPluginPresetFile(devicePath, file,
                         [state, onSelectionChanged](const juce::File& currentFile,
                                                     const juce::String& displayName) {
                             state->currentPresetFile = currentFile;
                             state->presetName = displayName;
                             if (onSelectionChanged)
                                 onSelectionChanged();
                         });
}

void PluginDevicePresetPresenter::showSaveDialog(const magda::DeviceInfo& device,
                                                 const magda::ChainNodePath& devicePath,
                                                 std::function<void()> onSelectionChanged) {
    auto state = state_;
    showSavePluginPresetDialog(device, devicePath, state->presetName,
                               [state, onSelectionChanged](const juce::File& currentFile,
                                                           const juce::String& displayName) {
                                   state->currentPresetFile = currentFile;
                                   state->presetName = displayName;
                                   if (onSelectionChanged)
                                       onSelectionChanged();
                               });
}

juce::LookAndFeel& getPluginPresetsButtonLookAndFeel() {
    static PluginPresetsButtonLookAndFeel instance;
    return instance;
}

void showPluginPresetMenu(juce::Component* targetComponent, const magda::DeviceInfo& device,
                          const magda::ChainNodePath& devicePath, bool isInternalDevice,
                          const juce::File& currentPluginPresetFile,
                          PluginPresetMenuActions actions) {
    auto* bridge = getAudioBridge();
    if (bridge == nullptr || isInternalDevice)
        return;

    auto& scanner = magda::PluginPresetScanner::getInstance();
    const bool diskPresetsSupported = scanner.getPresetExtension(device).isNotEmpty();

    juce::PopupMenu menu;
    juce::Array<juce::File> indexedPresetFiles;

    if (diskPresetsSupported) {
        const auto& tree = scanner.getPresets(device);
        if (!tree.empty()) {
            buildPluginPresetSubmenu(menu, tree.roots, kPresetIdBase, indexedPresetFiles,
                                     currentPluginPresetFile);
        } else {
            menu.addItem(kPresetIdBase, "(no presets installed)", false);
        }
    }

    const int numPrograms = bridge->getPluginNumPrograms(devicePath);
    if (numPrograms > 1) {
        juce::PopupMenu programsSubmenu;
        const int currentProgram = bridge->getPluginCurrentProgram(devicePath);
        for (int i = 0; i < numPrograms; ++i) {
            auto name = bridge->getPluginProgramName(devicePath, i);
            if (name.isEmpty())
                name = "Program " + juce::String(i + 1);
            programsSubmenu.addItem(kProgramIdBase + i, name, true, i == currentProgram);
        }

        if (menu.getNumItems() > 0)
            menu.addSeparator();
        menu.addSubMenu("Built-in Programs", programsSubmenu);
    }

    if (diskPresetsSupported) {
        menu.addSeparator();
        menu.addItem(kSavePluginPresetAs, "Save Preset As...");
        const auto userDir = scanner.getUserPresetDirectory(device);
        menu.addItem(kRevealPluginUserDir, "Reveal User Preset Folder",
                     !userDir.getFullPathName().isEmpty());
        menu.addItem(kRescanPluginPresets, "Rescan");
    } else if (numPrograms <= 1) {
        return;
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(targetComponent),
        [device, devicePath, indexedPresetFiles, actions = std::move(actions)](int chosen) {
            if (chosen == 0)
                return;

            if (chosen == kSavePluginPresetAs) {
                if (actions.saveAs)
                    actions.saveAs();
                return;
            }

            if (chosen == kRevealPluginUserDir) {
                auto dir = magda::PluginPresetScanner::getInstance().getUserPresetDirectory(device);
                if (!dir.exists())
                    dir.createDirectory();
                if (dir.exists())
                    dir.revealToUser();
                return;
            }

            if (chosen == kRescanPluginPresets) {
                magda::PluginPresetScanner::getInstance().rescan(device);
                return;
            }

            if (chosen >= kProgramIdBase) {
                const int programIndex = chosen - kProgramIdBase;
                if (auto* bridge = getAudioBridge()) {
                    if (bridge->setPluginCurrentProgram(devicePath, programIndex)) {
                        auto name = bridge->getPluginProgramName(devicePath, programIndex);
                        if (name.isEmpty())
                            name = "Program " + juce::String(programIndex + 1);
                        if (actions.selectionChanged)
                            actions.selectionChanged({}, name);
                    }
                }
                return;
            }

            if (chosen >= kPresetIdBase) {
                const int idx = chosen - kPresetIdBase;
                if (idx >= 0 && idx < indexedPresetFiles.size() && actions.loadFile)
                    actions.loadFile(indexedPresetFiles[idx]);
            }
        });
}

void loadPluginPresetFile(
    const magda::ChainNodePath& devicePath, const juce::File& file,
    std::function<void(const juce::File& currentFile, const juce::String& displayName)> onLoaded) {
    auto* bridge = getAudioBridge();
    if (bridge == nullptr)
        return;

    if (!bridge->loadPluginPresetFile(devicePath, file)) {
        showPresetErrorAsync("Load Preset Failed",
                             "Could not load \"" + file.getFileName() + "\".");
        return;
    }

    if (onLoaded)
        onLoaded(file, file.getFileNameWithoutExtension());
}

void showSavePluginPresetDialog(
    const magda::DeviceInfo& device, const magda::ChainNodePath& devicePath,
    const juce::String& currentPluginPresetName,
    std::function<void(const juce::File& currentFile, const juce::String& displayName)> onSaved) {
    auto& scanner = magda::PluginPresetScanner::getInstance();
    const auto extension = scanner.getPresetExtension(device);
    if (extension.isEmpty())
        return;

    const auto userDir = scanner.getUserPresetDirectory(device);
    if (userDir.getFullPathName().isEmpty()) {
        showPresetErrorAsync("Save Preset Failed",
                             "No writable preset directory for this plugin format.");
        return;
    }

    auto* alert = new juce::AlertWindow(
        "Save Plugin Preset", "Enter a name for this " + extension.substring(1) + " preset:",
        juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor(
        "name", currentPluginPresetName.isNotEmpty() ? currentPluginPresetName : device.name,
        "Name:");
    alert->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    alert->enterModalState(
        true, juce::ModalCallbackFunction::create([alert, device, devicePath, userDir, extension,
                                                   onSaved = std::move(onSaved)](int result) {
            if (result != 1) {
                delete alert;
                return;
            }

            const auto rawName = alert->getTextEditorContents("name").trim();
            delete alert;
            if (rawName.isEmpty())
                return;

            const auto safeName = juce::File::createLegalFileName(rawName);
            if (safeName.isEmpty())
                return;

            const auto target = userDir.getChildFile(safeName + extension);
            auto doSave = [device, devicePath, target, onSaved]() {
                auto* bridge = getAudioBridge();
                if (bridge == nullptr)
                    return;

                if (!bridge->savePluginPresetFile(devicePath, target)) {
                    showPresetErrorAsync("Save Preset Failed",
                                         "Could not write \"" + target.getFileName() + "\".");
                    return;
                }

                magda::PluginPresetScanner::getInstance().rescan(device);
                if (onSaved)
                    onSaved(target, target.getFileNameWithoutExtension());
            };

            if (target.existsAsFile()) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::QuestionIcon)
                        .withTitle("Overwrite Preset?")
                        .withMessage("\"" + target.getFileName() + "\" already exists. Overwrite?")
                        .withButton("Overwrite")
                        .withButton("Cancel"),
                    [doSave](int choice) {
                        if (choice == 1)
                            doSave();
                    });
            } else {
                doSave();
            }
        }));
}

}  // namespace magda::daw::ui
