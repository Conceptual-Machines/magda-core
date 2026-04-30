#include "ControllersDialog.hpp"

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

void styleListBox(juce::ListBox& lb) {
    lb.setColour(juce::ListBox::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    lb.setColour(juce::ListBox::outlineColourId, DarkTheme::getBorderColour());
    lb.setOutlineThickness(1);
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
        listModel_.isConnected = [this](const Controller& c) { return isControllerConnected(c); };
        listModel_.isEnabled = [](const Controller& c) {
            return BindingRegistry::getInstance().hasAnyBindingForController(c.id);
        };
        listModel_.onRowClicked = [this](int row, const juce::MouseEvent& e) {
            onRowClicked(row, e);
        };

        list_ = std::make_unique<juce::ListBox>("profiles", &listModel_);
        styleListBox(*list_);
        list_->setRowHeight(46);
        addAndMakeVisible(*list_);

        refreshLiveInputs();
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
        refreshLiveInputs();
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

    void refreshLiveInputs() {
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
    g.drawText(line1, textX, pad, width - textX - pad, lineH, juce::Justification::centredLeft,
               true);

    juce::String status;
    if (!enabled)
        status = tr("controllers.disabled");
    else if (connected)
        status = tr("controllers.connected");
    else
        status = tr("controllers.not_connected");

    juce::String portText = c.inputPortName.isNotEmpty() ? c.inputPortName : c.inputPort;
    juce::String line2 = portText + juce::String::fromUTF8("  \xc2\xb7  ") + status;

    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
    g.setFont(FontManager::getInstance().getUIFont(10.0f));
    g.drawText(line2, textX, pad + lineH, width - textX - pad, lineH,
               juce::Justification::centredLeft, true);
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
    refreshLiveInputs();
    if (liveInputs_.isEmpty()) {
        juce::AlertWindow::showMessageBox(juce::AlertWindow::WarningIcon,
                                          tr("controllers.add_profile"),
                                          tr("controllers.no_midi_inputs"));
        return;
    }
    juce::PopupMenu menu;
    for (int i = 0; i < liveInputs_.size(); ++i)
        menu.addItem(i + 1, liveInputs_[i].name);
    auto devicesCopy = liveInputs_;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addButton_),
                       [this, profile, devicesCopy](int result) {
                           if (result <= 0)
                               return;
                           int idx = result - 1;
                           if (idx < 0 || idx >= devicesCopy.size())
                               return;
                           onPortPicked(profile, devicesCopy[idx]);
                       });
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
    onRowToggled(row);
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
        listModel_.onRowClicked = [this](int row, const juce::MouseEvent& e) {
            onRowClicked(row, e);
        };

        list_ = std::make_unique<juce::ListBox>("scripts", &listModel_);
        styleListBox(*list_);
        list_->setRowHeight(28);
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
        const int openW = 170;
        const int importW = 140;
        const int reloadW = 100;

        // Left-to-right: Open Folder | Import | Reload. Same shape as Profiles.
        auto buttonRow = bounds.removeFromTop(rowH);
        openScriptsFolderButton_.setBounds(buttonRow.removeFromLeft(openW));
        buttonRow.removeFromLeft(btnGap);
        importButton_.setBounds(buttonRow.removeFromLeft(importW));
        buttonRow.removeFromLeft(btnGap);
        reloadLuaButton_.setBounds(buttonRow.removeFromLeft(reloadW));
        bounds.removeFromTop(8);

        list_->setBounds(bounds);
    }

  private:
    struct ScriptListModel : public juce::ListBoxModel {
        std::vector<juce::File>* scripts = nullptr;
        std::function<juce::String()> activeScriptName;
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
        scripting_app::loadLuaScript(scripts_[static_cast<size_t>(row)]);
        rebuildScripts();
    }

    void onRowMenu(int row) {
        if (row < 0 || row >= static_cast<int>(scripts_.size()))
            return;
        const auto file = scripts_[static_cast<size_t>(row)];
        const bool isActive = file.getFileName() == scripting_app::activeLuaScriptName();

        juce::PopupMenu menu;
        menu.addItem(1, tr("controllers.scripts.reveal"));
        if (isActive)
            menu.addItem(2, tr("controllers.scripts.unload"));

        juce::Component::SafePointer<LuaScriptsPage> self(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [self, file](int result) {
            if (result == 1) {
                if (file.existsAsFile())
                    file.revealToUser();
            } else if (result == 2) {
                scripting_app::unloadLuaScript();
                if (auto* page = self.getComponent())
                    page->rebuildScripts();
            }
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

    if (isActive) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
        g.fillEllipse(static_cast<float>(dotX), static_cast<float>(dotY),
                      static_cast<float>(dotSize), static_cast<float>(dotSize));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
        g.drawEllipse(static_cast<float>(dotX), static_cast<float>(dotY),
                      static_cast<float>(dotSize), static_cast<float>(dotSize), 1.0f);
    }

    juce::String line = name;
    if (isActive) {
        // Build the middle dot via charToString rather than a UTF-8 byte
        // literal — same mojibake we hit in the chat panel (96ca226f).
        line += "  " + juce::String::charToString(0x00B7) + "  " + tr("controllers.scripts.active");
    }

    g.setColour(isActive ? DarkTheme::getTextColour()
                         : DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(isActive ? FontManager::getInstance().getUIFontBold(12.0f)
                       : FontManager::getInstance().getUIFont(12.0f));
    g.drawText(line, textX, 0, width - textX - pad, height, juce::Justification::centredLeft, true);
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

    setSize(560, 480);
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
