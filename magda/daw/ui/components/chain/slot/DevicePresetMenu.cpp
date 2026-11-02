// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "slot/DevicePresetMenu.hpp"

#include <optional>

#include "core/PluginPresetScanner.hpp"
#include "core/PresetManager.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "engine/PluginService.hpp"
#include "ui/components/common/MagdaAlertWindow.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kRevealInFinder = 3;
constexpr int kPresetIdBase = 1000;
constexpr int kProgramIdBase = 5000;
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

magda::AudioEngine* getAudioEngine() {
    return magda::TrackManager::getInstance().getAudioEngine();
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
                              const juce::String& currentLoaded) {
    for (const auto& entry : entries) {
        if (entry.isFolder) {
            juce::PopupMenu submenu;
            buildPluginPresetSubmenu(submenu, entry.children, idBase, outFiles, currentLoaded);
            if (submenu.getNumItems() > 0)
                menu.addSubMenu(entry.name, submenu);
        } else {
            outFiles.add(entry.file);
            menu.addItem(idBase + outFiles.size() - 1, entry.name, true,
                         currentLoaded == entry.file.getFileNameWithoutExtension());
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

void rememberPresetName(const magda::ChainNodePath& devicePath, const juce::String& presetName) {
    if (auto* live = magda::TrackManager::getInstance().getDeviceInChainByPath(devicePath))
        live->presetName = presetName;
}

std::optional<magda::DeviceInfo> snapshotDeviceForPreset(const magda::DeviceInfo& fallbackDevice,
                                                         const magda::ChainNodePath& nodePath) {
    auto& trackManager = magda::TrackManager::getInstance();
    magda::PluginService::getInstance().capturePluginStateAt(nodePath);

    if (auto* live = trackManager.getDeviceInChainByPath(nodePath))
        return *live;

    return fallbackDevice;
}

void loadMagdaPreset(const juce::String& pluginFolder, const magda::ChainNodePath& nodePath,
                     const juce::String& presetRelativePath,
                     const std::function<void(const magda::DeviceInfo& liveDevice)>& onLoaded) {
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
            onLoaded(*live);
    }
}

void loadPluginPresetFile(const magda::ChainNodePath& devicePath, const juce::File& file,
                          const std::function<void()>& onLoaded) {
    auto* engine = getAudioEngine();
    if (engine == nullptr)
        return;

    if (!engine->loadPluginPresetFile(devicePath, file)) {
        showPresetErrorAsync("Load Preset Failed",
                             "Could not load \"" + file.getFileName() + "\".");
        return;
    }

    rememberPresetName(devicePath, file.getFileNameWithoutExtension());
    if (onLoaded)
        onLoaded();
}

}  // namespace

void showMagdaPresetMenu(juce::Component* targetComponent, const magda::DeviceInfo& device,
                         const magda::ChainNodePath& devicePath,
                         std::function<void(const magda::DeviceInfo& liveDevice)> onLoaded,
                         std::function<void()> onDismissed) {
    auto& presetManager = magda::PresetManager::getInstance();

    juce::PopupMenu menu;
    menu.addSectionHeader("MAGDA Presets");

    juce::StringArray indexedPresetPaths;
    buildPresetSubmenu(menu, presetManager.getDevicePluginDirectory(device.name), "", kPresetIdBase,
                       device.presetName, indexedPresetPaths);

    if (indexedPresetPaths.isEmpty())
        menu.addItem(kPresetIdBase, "(no presets yet)", false);

    menu.addSeparator();
    menu.addItem(kRevealInFinder, "Reveal in Finder");

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(targetComponent),
        [pluginFolder = device.name, devicePath, indexedPresetPaths, onLoaded = std::move(onLoaded),
         onDismissed = std::move(onDismissed)](int chosen) {
            if (onDismissed)
                onDismissed();
            if (chosen == 0)
                return;

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
            if (idx >= 0 && idx < indexedPresetPaths.size())
                loadMagdaPreset(pluginFolder, devicePath, indexedPresetPaths[idx], onLoaded);
        });
}

void showSaveMagdaPresetDialog(const magda::DeviceInfo& device,
                               const magda::ChainNodePath& devicePath,
                               std::function<void()> onSaved) {
    auto* alert = new magda::MagdaAlertWindow(
        "Save MAGDA Preset", "Enter a name and optional category for this device preset:",
        juce::MessageBoxIconType::NoIcon);

    auto defaultPath = device.presetName;
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
        true, juce::ModalCallbackFunction::create([alert, device, devicePath,
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
            auto doSave = [fullName, device, devicePath, onSaved]() {
                auto snapshot = snapshotDeviceForPreset(device, devicePath);
                if (!snapshot.has_value())
                    return;

                auto& presetManager = magda::PresetManager::getInstance();
                if (!presetManager.saveDevicePreset(*snapshot, fullName)) {
                    showPresetErrorAsync("Save Preset Failed", presetManager.getLastError());
                    return;
                }

                rememberPresetName(devicePath, fullName);
                if (onSaved)
                    onSaved();
            };

            if (magda::PresetManager::getInstance()
                    .getDevicePresets(device.name)
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

bool hasPluginPresetsAvailable(const magda::DeviceInfo& device, bool isInternalDevice) {
    if (isInternalDevice || device.loadState != magda::DeviceLoadState::Loaded)
        return false;

    return !magda::PluginPresetScanner::getInstance().getPresets(device).empty();
}

void showPluginPresetMenu(juce::Component* targetComponent, const magda::DeviceInfo& device,
                          const magda::ChainNodePath& devicePath, bool isInternalDevice,
                          std::function<void()> onSelectionChanged,
                          std::function<void()> onDismissed) {
    auto* engine = getAudioEngine();
    if (engine == nullptr || isInternalDevice) {
        if (onDismissed)
            onDismissed();
        return;
    }

    auto& scanner = magda::PluginPresetScanner::getInstance();
    const bool diskPresetsSupported = scanner.getPresetExtension(device).isNotEmpty();

    juce::PopupMenu menu;
    juce::Array<juce::File> indexedPresetFiles;

    if (diskPresetsSupported) {
        const auto& tree = scanner.getPresets(device);
        if (!tree.empty()) {
            buildPluginPresetSubmenu(menu, tree.roots, kPresetIdBase, indexedPresetFiles,
                                     device.presetName);
        } else {
            menu.addItem(kPresetIdBase, "(no presets installed)", false);
        }
    }

    const auto programs = engine->getPluginPrograms(devicePath);
    const int numPrograms = programs ? programs->names.size() : 0;
    if (numPrograms > 1) {
        juce::PopupMenu programsSubmenu;
        const int currentProgram = programs->current;
        for (int i = 0; i < numPrograms; ++i) {
            auto name = programs->names[i];
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
        const auto userDir = scanner.getUserPresetDirectory(device);
        menu.addItem(kRevealPluginUserDir, "Reveal User Preset Folder",
                     !userDir.getFullPathName().isEmpty());
        menu.addItem(kRescanPluginPresets, "Rescan");
    } else if (numPrograms <= 1) {
        menu.addItem(kPresetIdBase, "(no presets or programs available)", false);
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(targetComponent),
        [device, devicePath, indexedPresetFiles, onSelectionChanged = std::move(onSelectionChanged),
         onDismissed = std::move(onDismissed)](int chosen) {
            if (onDismissed)
                onDismissed();
            if (chosen == 0)
                return;

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
                if (auto* engine = getAudioEngine()) {
                    if (engine->setPluginCurrentProgram(devicePath, programIndex)) {
                        const auto selected = engine->getPluginPrograms(devicePath);
                        auto name = selected && programIndex < selected->names.size()
                                        ? selected->names[programIndex]
                                        : juce::String{};
                        if (name.isEmpty())
                            name = "Program " + juce::String(programIndex + 1);
                        rememberPresetName(devicePath, name);
                        if (onSelectionChanged)
                            onSelectionChanged();
                    }
                }
                return;
            }

            if (chosen >= kPresetIdBase) {
                const int idx = chosen - kPresetIdBase;
                if (idx >= 0 && idx < indexedPresetFiles.size())
                    loadPluginPresetFile(devicePath, indexedPresetFiles[idx], onSelectionChanged);
            }
        });
}

void showSavePluginPresetDialog(const magda::DeviceInfo& device,
                                const magda::ChainNodePath& devicePath,
                                std::function<void()> onSaved) {
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

    auto* alert = new magda::MagdaAlertWindow(
        "Save Plugin Preset", "Enter a name for this " + extension.substring(1) + " preset:",
        juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor("name", device.presetName.isNotEmpty() ? device.presetName : device.name,
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
                auto* engine = getAudioEngine();
                if (engine == nullptr)
                    return;

                if (!engine->savePluginPresetFile(devicePath, target)) {
                    showPresetErrorAsync("Save Preset Failed",
                                         "Could not write \"" + target.getFileName() + "\".");
                    return;
                }

                magda::PluginPresetScanner::getInstance().rescan(device);
                rememberPresetName(devicePath, target.getFileNameWithoutExtension());
                if (onSaved)
                    onSaved();
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
