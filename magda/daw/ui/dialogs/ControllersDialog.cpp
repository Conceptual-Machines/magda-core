#include "ControllersDialog.hpp"

#include <algorithm>
#include <map>

#include "../themes/DarkTheme.hpp"
#include "../themes/DialogLookAndFeel.hpp"
#include "../themes/FontManager.hpp"
#include "core/Config.hpp"
#include "core/StringTable.hpp"
#include "core/controllers/BindingRegistry.hpp"
#include "core/controllers/ControllerProfileRegistry.hpp"
#include "core/controllers/ControllerRegistry.hpp"
#include "scripting_app.hpp"

namespace magda {

namespace {

constexpr int kPollIntervalMs = 2000;
constexpr int kProfilePortOutWidth = 170;
constexpr int kScriptPortOutWidth = 170;
constexpr int kScriptPortInWidth = 170;

void styleListBox(juce::ListBox& lb) {
    lb.setColour(juce::ListBox::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    lb.setColour(juce::ListBox::outlineColourId, DarkTheme::getBorderColour());
    lb.setOutlineThickness(1);
}

juce::String displayNameForDevice(const juce::Array<juce::MidiDeviceInfo>& devices,
                                  const juce::String& id) {
    if (id.isEmpty())
        return {};
    for (const auto& dev : devices)
        if (dev.identifier == id || dev.name == id)
            return dev.name;
    return id;
}

void addMidiDeviceMenuItems(juce::PopupMenu& menu, const juce::Array<juce::MidiDeviceInfo>& devices,
                            const juce::String& currentId) {
    menu.addItem(1, tr("controllers.port.none"), true, currentId.isEmpty());
    menu.addSeparator();
    for (int i = 0; i < devices.size(); ++i) {
        const auto& dev = devices[i];
        menu.addItem(i + 2, dev.name, true, dev.identifier == currentId || dev.name == currentId);
    }
}

}  // namespace

// =============================================================================
// ControllerProfilesPage
// =============================================================================

class ControllerProfilesPage : public juce::Component,
                               private ControllerRegistryListener,
                               private juce::Timer {
  public:
    ControllerProfilesPage() {
        openFolderButton_.setButtonText(tr("controllers.open_profiles_folder"));
        openFolderButton_.onClick = [this]() { onOpenFolderClicked(); };
        addAndMakeVisible(openFolderButton_);

        uploadButton_.setButtonText(tr("controllers.upload_profile"));
        uploadButton_.onClick = [this]() { onUploadClicked(); };
        addAndMakeVisible(uploadButton_);

        addButton_.setButtonText(tr("controllers.add_profile"));
        addButton_.onClick = [this]() { onAddClicked(); };
        addAndMakeVisible(addButton_);

        listModel_.controllers = &controllers_;
        listModel_.liveInputs = &liveInputs_;
        listModel_.isConnected = [this](const Controller& c) { return isControllerConnected(c); };
        listModel_.isEnabled = [](const Controller& c) {
            return BindingRegistry::getInstance().hasAnyBindingForController(c.id);
        };
        listModel_.onRowClicked = [this](int row, const juce::MouseEvent& e) {
            onRowClicked(row, e);
        };

        list_ = std::make_unique<juce::ListBox>("profiles", &listModel_);
        styleListBox(*list_);
        list_->setRowHeight(42);
        addAndMakeVisible(*list_);

        refreshLiveDevices();
        if (ControllerRegistry::getInstance().rematchInputPorts(liveInputs_))
            persist();
        rebuildList();

        ControllerRegistry::getInstance().addListener(this);
        startTimer(kPollIntervalMs);
    }

    ~ControllerProfilesPage() override {
        stopTimer();
        ControllerRegistry::getInstance().removeListener(this);
        if (list_)
            list_->setModel(nullptr);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16);
        const int rowH = 28;
        const int btnGap = 6;
        const int openW = 170;
        const int uploadW = 140;
        const int addW = 120;

        // Left-to-right: Open Folder | Upload | + Add. Same shape as Scripts.
        auto buttonRow = bounds.removeFromTop(rowH);
        openFolderButton_.setBounds(buttonRow.removeFromLeft(openW));
        buttonRow.removeFromLeft(btnGap);
        uploadButton_.setBounds(buttonRow.removeFromLeft(uploadW));
        buttonRow.removeFromLeft(btnGap);
        addButton_.setBounds(buttonRow.removeFromLeft(addW));
        bounds.removeFromTop(8);

        list_->setBounds(bounds);
    }

  private:
    struct ControllerListModel : public juce::ListBoxModel {
        std::vector<Controller>* controllers = nullptr;
        juce::Array<juce::MidiDeviceInfo>* liveInputs = nullptr;
        std::function<bool(const Controller&)> isConnected;
        std::function<bool(const Controller&)> isEnabled;
        std::function<void(int, const juce::MouseEvent&)> onRowClicked;

        int getNumRows() override {
            return controllers ? static_cast<int>(controllers->size()) : 0;
        }
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                              bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override {
            if (onRowClicked)
                onRowClicked(row, e);
        }
    };

    void controllerRegistryChanged() override {
        rebuildList();
    }

    void timerCallback() override {
        auto previous = liveInputs_;
        refreshLiveDevices();
        bool changed = previous.size() != liveInputs_.size();
        if (!changed) {
            for (int i = 0; i < liveInputs_.size(); ++i) {
                if (previous[i].identifier != liveInputs_[i].identifier ||
                    previous[i].name != liveInputs_[i].name) {
                    changed = true;
                    break;
                }
            }
        }
        if (!changed)
            return;
        if (ControllerRegistry::getInstance().rematchInputPorts(liveInputs_))
            persist();
        rebuildList();
    }

    void refreshLiveDevices() {
        liveInputs_ = juce::MidiInput::getAvailableDevices();
    }

    void rebuildList() {
        controllers_ = ControllerRegistry::getInstance().all();
        if (list_)
            list_->updateContent();
        repaint();
    }

    static void persist() {
        auto& cfg = Config::getInstance();
        cfg.setControllers(ControllerRegistry::getInstance().saveToConfig());
        cfg.setGlobalBindings(BindingRegistry::getInstance().saveGlobal());
        cfg.save();
    }

    bool isControllerConnected(const Controller& c) const {
        for (const auto& dev : liveInputs_)
            if (dev.identifier == c.inputPort)
                return true;
        return false;
    }

    void onAddClicked();
    void onUploadClicked();
    void onOpenFolderClicked();
    void importProfileFile(const juce::File& file, const juce::String& title);
    void onProfilePicked(const ControllerProfile& profile);
    void onPortPicked(const ControllerProfile& profile, const juce::MidiDeviceInfo& dev);

    void onRowClicked(int row, const juce::MouseEvent& e);
    void onRowToggled(int row);
    void onRowPortRequested(int row);
    void onRowRemoveRequested(int row);

    std::vector<Controller> controllers_;
    juce::Array<juce::MidiDeviceInfo> liveInputs_;

    juce::TextButton openFolderButton_;
    juce::TextButton uploadButton_;
    juce::TextButton addButton_;
    ControllerListModel listModel_;
    std::unique_ptr<juce::ListBox> list_;
    std::unique_ptr<juce::FileChooser> uploadChooser_;
};

void ControllerProfilesPage::ControllerListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                                                   int width, int height,
                                                                   bool rowIsSelected) {
    if (!controllers || rowNumber < 0 || rowNumber >= static_cast<int>(controllers->size()))
        return;

    const auto& c = (*controllers)[static_cast<size_t>(rowNumber)];
    const bool connected = isConnected ? isConnected(c) : false;
    const bool enabled = isEnabled ? isEnabled(c) : true;
    const bool active = enabled && connected;

    if (rowIsSelected) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.20f));
        g.fillRect(0, 0, width, height);
    }

    const int pad = 6;
    const int dotSize = 8;
    const int dotX = pad;
    const int textX = dotX + dotSize + 8;
    const int portX = width - kProfilePortOutWidth - pad;
    const int nameW = juce::jmax(40, portX - textX - 8);
    const int lineH = (height - 2 * pad) / 2;

    const int dotY = (height - dotSize) / 2;
    g.setColour(active ? DarkTheme::getColour(DarkTheme::ACCENT_GREEN)
                       : DarkTheme::getColour(DarkTheme::TEXT_DIM));
    g.fillEllipse(static_cast<float>(dotX), static_cast<float>(dotY), static_cast<float>(dotSize),
                  static_cast<float>(dotSize));

    juce::String line1 = c.vendor.isEmpty() ? c.name : c.vendor + "  \xc2\xb7  " + c.name;
    g.setColour(active ? DarkTheme::getTextColour()
                       : DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(FontManager::getInstance().getUIFontBold(12.0f));
    g.drawText(line1, textX, pad, nameW, lineH, juce::Justification::centredLeft, true);

    juce::String status;
    if (!enabled)
        status = tr("controllers.disabled");
    else if (connected)
        status = tr("controllers.connected");
    else
        status = tr("controllers.not_connected");

    juce::String line2 = status;
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
    g.setFont(FontManager::getInstance().getUIFont(10.0f));
    g.drawText(line2, textX, pad + lineH, nameW, lineH, juce::Justification::centredLeft, true);

    auto portText =
        liveInputs != nullptr ? displayNameForDevice(*liveInputs, c.inputPort) : c.inputPort;
    if (portText.isEmpty())
        portText = tr("controllers.port.none");

    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    g.drawText(tr("controllers.port.port_out"), portX, 4, kProfilePortOutWidth, 12,
               juce::Justification::centredLeft, true);

    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(FontManager::getInstance().getUIFont(11.0f));
    g.drawText(portText, portX, 18, kProfilePortOutWidth, 18, juce::Justification::centredLeft,
               true);
}

// -- Profiles page handlers ---------------------------------------------------

void ControllerProfilesPage::onOpenFolderClicked() {
    auto dir = ControllerProfileRegistry::userControllersDirectory();
    if (!dir.isDirectory())
        dir.createDirectory();
    dir.revealToUser();
    // Re-scan when the user comes back — they may have dropped/removed files.
    ControllerProfileRegistry::getInstance().load();
    rebuildList();
}

void ControllerProfilesPage::onUploadClicked() {
    auto title = tr("controllers.upload_profile");
    uploadChooser_ = std::make_unique<juce::FileChooser>(
        title, juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.json");
    juce::Component::SafePointer<ControllerProfilesPage> safeThis(this);
    uploadChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                    juce::FileBrowserComponent::canSelectFiles,
                                [safeThis, title](const juce::FileChooser& fc) {
                                    auto file = fc.getResult();
                                    if (file == juce::File{})
                                        return;
                                    if (safeThis == nullptr)
                                        return;
                                    safeThis->importProfileFile(file, title);
                                });
}

void ControllerProfilesPage::importProfileFile(const juce::File& file, const juce::String& title) {
    auto fail = [&](const juce::String& reason) {
        juce::AlertWindow::showMessageBox(juce::AlertWindow::WarningIcon, title, reason);
    };

    auto parsed = juce::JSON::parse(file.loadFileAsString());
    if (parsed.isVoid())
        return fail(tr("controllers.upload_invalid_json"));

    auto profileOpt = decodeControllerProfile(parsed);
    if (!profileOpt.has_value())
        return fail(tr("controllers.upload_invalid_profile"));

    auto issues = validateControllerProfile(*profileOpt);
    if (!issues.empty()) {
        juce::String body = tr("controllers.upload_validation_failed");
        for (const auto& issue : issues)
            body += "\n  • " + tr(issue.key).replace("{0}", issue.arg);
        return fail(body);
    }

    auto& reg = ControllerProfileRegistry::getInstance();
    auto userDir = ControllerProfileRegistry::userControllersDirectory();
    if (!userDir.isDirectory()) {
        auto createResult = userDir.createDirectory();
        if (createResult.failed())
            return fail(tr("controllers.upload_create_dir_failed")
                            .replace("{0}", createResult.getErrorMessage()));
    }

    auto destFile =
        userDir.getChildFile(ControllerProfileRegistry::filenameForProfileId(profileOpt->id));

    if (destFile.existsAsFile()) {
        bool ok = juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::QuestionIcon, title,
            tr("controllers.upload_overwrite").replace("{0}", profileOpt->id), tr("dialogs.ok"),
            tr("dialogs.cancel"));
        if (!ok)
            return;
    }

    if (!file.copyFileTo(destFile))
        return fail(tr("controllers.upload_copy_failed"));

    reg.load();
    rebuildList();

    juce::AlertWindow::showMessageBox(
        juce::AlertWindow::InfoIcon, title,
        tr("controllers.upload_success").replace("{0}", profileOpt->id));
}

void ControllerProfilesPage::onAddClicked() {
    auto& profileReg = ControllerProfileRegistry::getInstance();
    profileReg.load();
    auto profiles = profileReg.all();
    if (profiles.empty()) {
        juce::AlertWindow::showMessageBox(juce::AlertWindow::InfoIcon,
                                          tr("controllers.add_profile"),
                                          tr("controllers.no_profiles"));
        return;
    }

    juce::PopupMenu menu;
    std::map<juce::String, int> nameCounts;
    for (const auto& p : profiles) {
        juce::String key = p.vendor + "\x1f" + p.name;
        nameCounts[key]++;
    }
    for (size_t i = 0; i < profiles.size(); ++i) {
        const auto& p = profiles[i];
        juce::String label = p.vendor.isEmpty() ? p.name : p.vendor + "  \xc2\xb7  " + p.name;
        juce::String key = p.vendor + "\x1f" + p.name;
        if (nameCounts[key] > 1)
            label += "  (" + p.id + ")";
        menu.addItem(static_cast<int>(i) + 1, label);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addButton_),
                       [this, profiles](int result) {
                           if (result <= 0)
                               return;
                           size_t idx = static_cast<size_t>(result - 1);
                           if (idx >= profiles.size())
                               return;
                           onProfilePicked(profiles[idx]);
                       });
}

void ControllerProfilesPage::onProfilePicked(const ControllerProfile& profile) {
    auto mat = materialiseControllerFromProfile(profile, {}, {});
    ControllerRegistry::getInstance().add(mat.controller);
    for (const auto& b : mat.bindings)
        BindingRegistry::getInstance().add(BindingScope::Global, b);

    persist();
    rebuildList();
}

void ControllerProfilesPage::onPortPicked(const ControllerProfile& profile,
                                          const juce::MidiDeviceInfo& dev) {
    auto& controllerReg = ControllerRegistry::getInstance();
    auto& bindingReg = BindingRegistry::getInstance();

    for (const auto& existing : controllerReg.all()) {
        if (existing.inputPort == dev.identifier) {
            bindingReg.removeAllForController(BindingScope::Global, existing.id);
            bindingReg.removeAllForController(BindingScope::Project, existing.id);
        }
    }

    auto mat = materialiseControllerFromProfile(profile, dev.identifier, {}, dev.name);
    controllerReg.add(mat.controller);
    for (const auto& b : mat.bindings)
        bindingReg.add(BindingScope::Global, b);

    persist();
    rebuildList();
}

void ControllerProfilesPage::onRowClicked(int row, const juce::MouseEvent& e) {
    if (row < 0 || row >= static_cast<int>(controllers_.size()))
        return;
    if (e.mods.isPopupMenu() || e.mods.isRightButtonDown() || e.mods.isCtrlDown()) {
        onRowRemoveRequested(row);
        return;
    }
    const int portX = list_ ? list_->getWidth() - kProfilePortOutWidth - 6 : 0;
    if (e.x >= portX) {
        onRowPortRequested(row);
        return;
    }
    onRowToggled(row);
}

void ControllerProfilesPage::onRowPortRequested(int row) {
    if (row < 0 || row >= static_cast<int>(controllers_.size()))
        return;
    refreshLiveDevices();
    const auto current = controllers_[static_cast<size_t>(row)];

    juce::PopupMenu menu;
    addMidiDeviceMenuItems(menu, liveInputs_, current.inputPort);
    menu.showMenuAsync(juce::PopupMenu::Options(), [this, row](int result) {
        if (result <= 0)
            return;
        auto controllers = controllers_;
        if (row < 0 || row >= static_cast<int>(controllers.size()))
            return;

        auto selected = controllers[static_cast<size_t>(row)];
        if (result == 1) {
            selected.inputPort = {};
            selected.inputPortName = {};
        } else {
            const int idx = result - 2;
            if (idx < 0 || idx >= liveInputs_.size())
                return;
            selected.inputPort = liveInputs_[idx].identifier;
            selected.inputPortName = liveInputs_[idx].name;

            for (auto& other : controllers) {
                if (other.id != selected.id && other.inputPort == selected.inputPort) {
                    other.inputPort = {};
                    other.inputPortName = {};
                    ControllerRegistry::getInstance().update(other);
                }
            }
        }

        ControllerRegistry::getInstance().update(selected);
        persist();
        rebuildList();
    });
}

void ControllerProfilesPage::onRowToggled(int row) {
    if (row < 0 || row >= static_cast<int>(controllers_.size()))
        return;
    const auto& c = controllers_[static_cast<size_t>(row)];
    auto& controllerReg = ControllerRegistry::getInstance();
    auto& bindingReg = BindingRegistry::getInstance();

    if (bindingReg.hasAnyBindingForController(c.id)) {
        bindingReg.removeAllForController(BindingScope::Global, c.id);
        bindingReg.removeAllForController(BindingScope::Project, c.id);
    } else {
        for (const auto& other : controllerReg.all()) {
            if (other.id == c.id || other.inputPort != c.inputPort)
                continue;
            bindingReg.removeAllForController(BindingScope::Global, other.id);
            bindingReg.removeAllForController(BindingScope::Project, other.id);
        }
        auto profileOpt = ControllerProfileRegistry::getInstance().findById(c.profileId);
        if (!profileOpt.has_value())
            return;
        auto mat = materialiseControllerFromProfile(*profileOpt, c.inputPort, c.outputPort,
                                                    c.inputPortName);
        for (auto& b : mat.bindings) {
            b.source.controllerId = c.id;
            bindingReg.add(BindingScope::Global, b);
        }
    }
    persist();
    rebuildList();
}

void ControllerProfilesPage::onRowRemoveRequested(int row) {
    if (row < 0 || row >= static_cast<int>(controllers_.size()))
        return;
    const auto& c = controllers_[static_cast<size_t>(row)];

    juce::PopupMenu menu;
    const bool haveProfile = c.profileId.isNotEmpty();
    if (haveProfile)
        menu.addItem(2, tr("controllers.show_in_finder"));
    menu.addItem(1, tr("controllers.remove"));

    const auto id = c.id;
    const auto name = c.name;
    const auto profileId = c.profileId;

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, id, name, profileId](int result) {
        if (result == 2) {
            auto file =
                ControllerProfileRegistry::getInstance().findSourceFileForProfileId(profileId);
            if (file.existsAsFile())
                file.revealToUser();
            return;
        }
        if (result != 1)
            return;

        juce::String title = tr("controllers.remove_confirm_title");
        juce::String msg = tr("controllers.remove_confirm_message");
        msg = msg.replace("{0}", name);

        juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::QuestionIcon, title, msg, tr("dialogs.ok"), tr("dialogs.cancel"),
            nullptr, juce::ModalCallbackFunction::create([this, id](int result2) {
                if (result2 != 1)
                    return;
                BindingRegistry::getInstance().removeAllForController(BindingScope::Global, id);
                ControllerRegistry::getInstance().remove(id);
                persist();
                rebuildList();
            }));
    });
}

// =============================================================================
// LuaScriptsPage
// =============================================================================

class LuaScriptsPage : public juce::Component {
  public:
    LuaScriptsPage() {
        addScriptButton_.setButtonText(tr("controllers.scripts.add"));
        addScriptButton_.onClick = [this]() { onAddScriptClicked(); };
        addAndMakeVisible(addScriptButton_);

        openScriptsFolderButton_.setButtonText(tr("controllers.scripts.open_folder"));
        openScriptsFolderButton_.onClick = [this]() { onOpenScriptsFolderClicked(); };
        addAndMakeVisible(openScriptsFolderButton_);

        importButton_.setButtonText(tr("controllers.scripts.import"));
        importButton_.onClick = [this]() { onImportClicked(); };
        addAndMakeVisible(importButton_);

        reloadLuaButton_.setButtonText(tr("controllers.scripts.reload"));
        reloadLuaButton_.onClick = [this]() { onReloadLuaClicked(); };
        addAndMakeVisible(reloadLuaButton_);

        listModel_.scripts = &scripts_;
        listModel_.activeScriptName = []() { return scripting_app::activeLuaScriptName(); };
        listModel_.portsForScript = [](const juce::String& name) {
            return scripting_app::luaScriptPorts(name);
        };
        listModel_.liveInputs = &liveInputs_;
        listModel_.liveOutputs = &liveOutputs_;
        listModel_.onRowClicked = [this](int row, const juce::MouseEvent& e) {
            onRowClicked(row, e);
        };

        list_ = std::make_unique<juce::ListBox>("scripts", &listModel_);
        styleListBox(*list_);
        list_->setRowHeight(42);
        addAndMakeVisible(*list_);

        rebuildScripts();
    }

    ~LuaScriptsPage() override {
        if (list_)
            list_->setModel(nullptr);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16);
        const int rowH = 28;
        const int btnGap = 6;
        const int rowGap = 4;

        auto layoutRow = [&](juce::Button& left, juce::Button& right) {
            auto row = bounds.removeFromTop(rowH);
            const int half = (row.getWidth() - btnGap) / 2;
            left.setBounds(row.removeFromLeft(half));
            row.removeFromLeft(btnGap);
            right.setBounds(row.removeFromLeft(half));
        };

        layoutRow(addScriptButton_, importButton_);
        bounds.removeFromTop(rowGap);
        layoutRow(openScriptsFolderButton_, reloadLuaButton_);
        bounds.removeFromTop(8);

        list_->setBounds(bounds);
    }

  private:
    struct ScriptListModel : public juce::ListBoxModel {
        std::vector<juce::File>* scripts = nullptr;
        std::function<juce::String()> activeScriptName;
        std::function<scripting_app::LuaScriptPorts(const juce::String&)> portsForScript;
        juce::Array<juce::MidiDeviceInfo>* liveInputs = nullptr;
        juce::Array<juce::MidiDeviceInfo>* liveOutputs = nullptr;
        std::function<void(int, const juce::MouseEvent&)> onRowClicked;

        int getNumRows() override {
            return scripts ? static_cast<int>(scripts->size()) : 0;
        }
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                              bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override {
            if (onRowClicked)
                onRowClicked(row, e);
        }
    };

    void rebuildScripts() {
        liveInputs_ = juce::MidiInput::getAvailableDevices();
        liveOutputs_ = juce::MidiOutput::getAvailableDevices();
        scripts_ = scripting_app::enumerateLuaScripts();
        if (list_)
            list_->updateContent();
        repaint();
    }

    void onRowClicked(int row, const juce::MouseEvent& e) {
        if (row < 0 || row >= static_cast<int>(scripts_.size()))
            return;
        if (e.mods.isPopupMenu() || e.mods.isRightButtonDown() || e.mods.isCtrlDown()) {
            onRowMenu(row);
            return;
        }
        const int portInX = list_ ? list_->getWidth() - kScriptPortInWidth - 6 : 0;
        const int portOutX = portInX - kScriptPortOutWidth - 8;
        if (e.x >= portInX) {
            onRowOutputRequested(row);
            return;
        }
        if (e.x >= portOutX) {
            onRowDawInputRequested(row);
            return;
        }
        scripting_app::loadLuaScript(scripts_[static_cast<size_t>(row)]);
        rebuildScripts();
    }

    void onRowOutputRequested(int row) {
        if (row < 0 || row >= static_cast<int>(scripts_.size()))
            return;
        rebuildScripts();
        const auto scriptName = scripts_[static_cast<size_t>(row)].getFileName();
        auto ports = scripting_app::luaScriptPorts(scriptName);

        juce::PopupMenu menu;
        addMidiDeviceMenuItems(menu, liveOutputs_, ports.midiOutputPort);
        menu.showMenuAsync(juce::PopupMenu::Options(), [this, scriptName](int result) {
            if (result <= 0)
                return;
            auto ports = scripting_app::luaScriptPorts(scriptName);
            if (result == 1) {
                ports.midiOutputPort = {};
            } else {
                const int idx = result - 2;
                if (idx < 0 || idx >= liveOutputs_.size())
                    return;
                ports.midiOutputPort = liveOutputs_[idx].identifier;
            }
            scripting_app::setLuaScriptPorts(scriptName, ports);
            rebuildScripts();
        });
    }

    void onRowDawInputRequested(int row) {
        if (row < 0 || row >= static_cast<int>(scripts_.size()))
            return;
        rebuildScripts();
        const auto scriptName = scripts_[static_cast<size_t>(row)].getFileName();
        auto ports = scripting_app::luaScriptPorts(scriptName);

        juce::PopupMenu menu;
        addMidiDeviceMenuItems(menu, liveInputs_, ports.dawInputPort);
        menu.showMenuAsync(juce::PopupMenu::Options(), [this, scriptName](int result) {
            if (result <= 0)
                return;
            auto ports = scripting_app::luaScriptPorts(scriptName);
            if (result == 1) {
                ports.dawInputPort = {};
            } else {
                const int idx = result - 2;
                if (idx < 0 || idx >= liveInputs_.size())
                    return;
                ports.dawInputPort = liveInputs_[idx].identifier;
            }
            scripting_app::setLuaScriptPorts(scriptName, ports);
            rebuildScripts();
        });
    }

    void onRowMenu(int row) {
        if (row < 0 || row >= static_cast<int>(scripts_.size()))
            return;
        const auto file = scripts_[static_cast<size_t>(row)];
        const bool isActive = file.getFileName() == scripting_app::activeLuaScriptName();
        const bool isFactory = scripting_app::isFactoryLuaScript(file);

        juce::PopupMenu menu;
        menu.addItem(1, tr("controllers.scripts.reveal"));
        if (isActive)
            menu.addItem(2, tr("controllers.scripts.unload"));
        if (isFactory)
            menu.addItem(3, tr("controllers.scripts.disable"));

        juce::Component::SafePointer<LuaScriptsPage> self(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [self, file](int result) {
            if (result == 1) {
                if (file.existsAsFile())
                    file.revealToUser();
            } else if (result == 2) {
                scripting_app::unloadLuaScript();
                if (auto* page = self.getComponent())
                    page->rebuildScripts();
            } else if (result == 3) {
                auto& cfg = magda::Config::getInstance();
                auto enabled = cfg.getEnabledFactoryLuaScripts();
                const auto name = file.getFileName().toStdString();
                enabled.erase(std::remove(enabled.begin(), enabled.end(), name), enabled.end());
                cfg.setEnabledFactoryLuaScripts(std::move(enabled));
                cfg.save();
                if (file.getFileName() == scripting_app::activeLuaScriptName())
                    scripting_app::unloadLuaScript();
                if (auto* page = self.getComponent())
                    page->rebuildScripts();
            }
        });
    }

    void onAddScriptClicked() {
        auto available = scripting_app::enumerateAvailableFactoryLuaScripts();
        if (available.empty()) {
            juce::AlertWindow::showMessageBox(juce::AlertWindow::InfoIcon,
                                              tr("controllers.scripts.add"),
                                              tr("controllers.scripts.add_no_options"));
            return;
        }
        juce::PopupMenu menu;
        for (size_t i = 0; i < available.size(); ++i)
            menu.addItem(static_cast<int>(i + 1), available[i].getFileName());

        juce::Component::SafePointer<LuaScriptsPage> self(this);
        const auto availableCopy = available;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addScriptButton_),
                           [self, availableCopy](int result) {
                               if (result <= 0)
                                   return;
                               const size_t idx = static_cast<size_t>(result - 1);
                               if (idx >= availableCopy.size())
                                   return;
                               auto& cfg = magda::Config::getInstance();
                               auto enabled = cfg.getEnabledFactoryLuaScripts();
                               const auto name = availableCopy[idx].getFileName().toStdString();
                               if (std::find(enabled.begin(), enabled.end(), name) ==
                                   enabled.end()) {
                                   enabled.push_back(name);
                                   cfg.setEnabledFactoryLuaScripts(std::move(enabled));
                                   cfg.save();
                               }
                               if (auto* page = self.getComponent())
                                   page->rebuildScripts();
                           });
    }

    void onReloadLuaClicked() {
        if (!scripting_app::reloadActiveLuaScript() && scripting_app::hasAnyLuaScripts()) {
            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                                             .withTitle(tr("controllers.tab.scripts"))
                                             .withMessage(tr("controllers.scripts.reload_failed"))
                                             .withButton(tr("dialogs.ok")),
                                         nullptr);
        }
        rebuildScripts();
    }

    void onOpenScriptsFolderClicked() {
        scripting_app::revealLuaScriptsFolder();
        rebuildScripts();
    }

    void onImportClicked() {
        auto title = tr("controllers.scripts.import");
        importChooser_ = std::make_unique<juce::FileChooser>(
            title, juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.lua");
        juce::Component::SafePointer<LuaScriptsPage> self(this);
        importChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                        juce::FileBrowserComponent::canSelectFiles,
                                    [self, title](const juce::FileChooser& fc) {
                                        auto file = fc.getResult();
                                        if (file == juce::File{})
                                            return;
                                        if (auto* page = self.getComponent())
                                            page->importScriptFile(file, title);
                                    });
    }

    void importScriptFile(const juce::File& file, const juce::String& title) {
        auto fail = [&](const juce::String& reason) {
            juce::AlertWindow::showMessageBox(juce::AlertWindow::WarningIcon, title, reason);
        };
        if (!file.existsAsFile() || !file.hasFileExtension("lua"))
            return fail(tr("controllers.scripts.import_invalid"));

        auto scriptsDir = scripting_app::luaScriptsFolder();
        if (!scriptsDir.isDirectory())
            return fail(tr("controllers.scripts.import_copy_failed"));

        auto destFile = scriptsDir.getChildFile(file.getFileName());
        if (destFile.existsAsFile()) {
            bool ok = juce::AlertWindow::showOkCancelBox(
                juce::AlertWindow::QuestionIcon, title,
                tr("controllers.scripts.import_overwrite").replace("{0}", file.getFileName()),
                tr("dialogs.ok"), tr("dialogs.cancel"));
            if (!ok)
                return;
        }
        if (!file.copyFileTo(destFile))
            return fail(tr("controllers.scripts.import_copy_failed"));

        rebuildScripts();
        juce::AlertWindow::showMessageBox(
            juce::AlertWindow::InfoIcon, title,
            tr("controllers.scripts.import_success").replace("{0}", file.getFileName()));
    }

    std::vector<juce::File> scripts_;
    juce::Array<juce::MidiDeviceInfo> liveInputs_;
    juce::Array<juce::MidiDeviceInfo> liveOutputs_;

    juce::TextButton addScriptButton_;
    juce::TextButton openScriptsFolderButton_;
    juce::TextButton importButton_;
    juce::TextButton reloadLuaButton_;
    ScriptListModel listModel_;
    std::unique_ptr<juce::ListBox> list_;
    std::unique_ptr<juce::FileChooser> importChooser_;
};

void LuaScriptsPage::ScriptListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width,
                                                       int height, bool rowIsSelected) {
    if (!scripts || rowNumber < 0 || rowNumber >= static_cast<int>(scripts->size()))
        return;

    const auto& file = (*scripts)[static_cast<size_t>(rowNumber)];
    const juce::String name = file.getFileName();
    const juce::String active = activeScriptName ? activeScriptName() : juce::String{};
    const bool isActive = name == active && active.isNotEmpty();

    if (rowIsSelected) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.20f));
        g.fillRect(0, 0, width, height);
    }

    const int pad = 6;
    const int dotSize = 8;
    const int dotX = pad;
    const int textX = dotX + dotSize + 8;
    const int dotY = (height - dotSize) / 2;
    const int portInX = width - kScriptPortInWidth - pad;
    const int portOutX = portInX - kScriptPortOutWidth - 8;
    const int nameW = juce::jmax(40, portOutX - textX - 8);

    if (isActive) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
        g.fillEllipse(static_cast<float>(dotX), static_cast<float>(dotY),
                      static_cast<float>(dotSize), static_cast<float>(dotSize));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
        g.drawEllipse(static_cast<float>(dotX), static_cast<float>(dotY),
                      static_cast<float>(dotSize), static_cast<float>(dotSize), 1.0f);
    }

    g.setColour(isActive ? DarkTheme::getTextColour()
                         : DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(isActive ? FontManager::getInstance().getUIFontBold(12.0f)
                       : FontManager::getInstance().getUIFont(12.0f));
    g.drawText(name, textX, 5, nameW, 18, juce::Justification::centredLeft, true);

    if (isActive) {
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText(tr("controllers.scripts.active"), textX, 22, nameW, 14,
                   juce::Justification::centredLeft, true);
    }

    auto ports = portsForScript ? portsForScript(name) : scripting_app::LuaScriptPorts{};
    auto portInText = liveOutputs != nullptr
                          ? displayNameForDevice(*liveOutputs, ports.midiOutputPort)
                          : ports.midiOutputPort;
    auto portOutText = liveInputs != nullptr ? displayNameForDevice(*liveInputs, ports.dawInputPort)
                                             : ports.dawInputPort;
    if (portInText.isEmpty())
        portInText = tr("controllers.port.none");
    if (portOutText.isEmpty())
        portOutText = tr("controllers.port.none");

    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    g.drawText(tr("controllers.port.port_out"), portOutX, 4, kScriptPortOutWidth, 12,
               juce::Justification::centredLeft, true);
    g.drawText(tr("controllers.port.port_in"), portInX, 4, kScriptPortInWidth, 12,
               juce::Justification::centredLeft, true);

    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(FontManager::getInstance().getUIFont(11.0f));
    g.drawText(portOutText, portOutX, 18, kScriptPortOutWidth, 18, juce::Justification::centredLeft,
               true);
    g.drawText(portInText, portInX, 18, kScriptPortInWidth, 18, juce::Justification::centredLeft,
               true);
}

// =============================================================================
// ControllersDialog
// =============================================================================

ControllersDialog::ControllersDialog() {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());

    profilesPage_ = std::make_unique<ControllerProfilesPage>();
    scriptsPage_ = std::make_unique<LuaScriptsPage>();

    auto tabBg = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    tabbedComponent_.addTab(tr("controllers.tab.profiles"), tabBg, profilesPage_.get(), false);
    tabbedComponent_.addTab(tr("controllers.tab.scripts"), tabBg, scriptsPage_.get(), false);
    addAndMakeVisible(tabbedComponent_);

    setSize(560, 512);
}

ControllersDialog::~ControllersDialog() {
    setLookAndFeel(nullptr);
}

void ControllersDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void ControllersDialog::resized() {
    tabbedComponent_.setBounds(getLocalBounds().reduced(8));
}

// =============================================================================
// showDialog
// =============================================================================

namespace {
class SelfClosingDialogWindow : public juce::DialogWindow {
  public:
    SelfClosingDialogWindow(const juce::String& title, juce::Colour bg)
        : juce::DialogWindow(title, bg, false, true) {}

    void closeButtonPressed() override {
        juce::MessageManager::callAsync([self = this]() { delete self; });
    }
};
}  // namespace

void ControllersDialog::showDialog(juce::Component* /*parent*/) {
    auto* dialog = new ControllersDialog();
    auto bg = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);

    auto* window = new SelfClosingDialogWindow(tr("controllers.title"), bg);
    window->setContentOwned(dialog, true);
    window->setUsingNativeTitleBar(true);
    window->setResizable(true, false);
    window->setAlwaysOnTop(true);
    window->centreWithSize(dialog->getWidth(), dialog->getHeight());
    window->setVisible(true);
}

}  // namespace magda
