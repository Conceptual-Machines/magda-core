#include "ControllersDialog.hpp"

#include "../themes/DarkTheme.hpp"
#include "../themes/DialogLookAndFeel.hpp"
#include "../themes/FontManager.hpp"
#include "core/Config.hpp"
#include "core/StringTable.hpp"
#include "core/controllers/BindingRegistry.hpp"
#include "core/controllers/ControllerProfileRegistry.hpp"
#include "core/controllers/ControllerRegistry.hpp"

namespace magda {

// =============================================================================
// RemoveRowButton — small button embedded in each enabled-controller row
// =============================================================================

class RemoveRowButton : public juce::Component {
  public:
    std::function<void()> onClick;

    RemoveRowButton() {
        button_.setButtonText(tr("controllers.remove"));
        button_.onClick = [this]() {
            if (onClick)
                onClick();
        };
        addAndMakeVisible(button_);
    }

    void resized() override {
        button_.setBounds(getLocalBounds());
    }

    void paint(juce::Graphics&) override {}

    juce::TextButton button_;
};

// =============================================================================
// ProfileListModel
// =============================================================================

void ControllersDialog::ProfileListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                                           int width, int height,
                                                           bool rowIsSelected) {
    if (!profiles || rowNumber < 0 || rowNumber >= static_cast<int>(profiles->size()))
        return;

    const auto& profile = (*profiles)[static_cast<size_t>(rowNumber)];

    if (rowIsSelected) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.35f));
        g.fillRect(0, 0, width, height);
    }

    const int pad = 6;
    const int lineH = (height - 2 * pad) / 2;

    // Line 1: "Vendor  .  Name" — bold
    juce::String line1 =
        profile.vendor.isEmpty() ? profile.name : profile.vendor + "  \xc2\xb7  " + profile.name;
    g.setColour(DarkTheme::getTextColour());
    g.setFont(FontManager::getInstance().getUIFontBold(12.0f));
    g.drawText(line1, pad, pad, width - 2 * pad, lineH, juce::Justification::centredLeft, true);

    // Line 2: profile id — muted
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
    g.setFont(FontManager::getInstance().getUIFont(10.0f));
    g.drawText(profile.id, pad, pad + lineH, width - 2 * pad, lineH,
               juce::Justification::centredLeft, true);
}

// =============================================================================
// ControllerListModel
// =============================================================================

void ControllersDialog::ControllerListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                                              int width, int height,
                                                              bool rowIsSelected) {
    if (!controllers || rowNumber < 0 || rowNumber >= static_cast<int>(controllers->size()))
        return;

    const auto& c = (*controllers)[static_cast<size_t>(rowNumber)];

    if (rowIsSelected) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.35f));
        g.fillRect(0, 0, width, height);
    }

    const int pad = 6;
    const int btnW = 70;
    const int textW = width - 2 * pad - btnW - 8;
    const int lineH = (height - 2 * pad) / 2;

    // Line 1: controller name — bold
    g.setColour(DarkTheme::getTextColour());
    g.setFont(FontManager::getInstance().getUIFontBold(12.0f));
    g.drawText(c.name, pad, pad, textW, lineH, juce::Justification::centredLeft, true);

    // Line 2: port display name — muted
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(FontManager::getInstance().getUIFont(10.0f));
    juce::String portLine = tr("controllers.port_label") + " " + c.inputPort;
    g.drawText(portLine, pad, pad + lineH, textW, lineH, juce::Justification::centredLeft, true);
}

juce::Component* ControllersDialog::ControllerListModel::refreshComponentForRow(
    int rowNumber, bool /*isRowSelected*/, juce::Component* existingComponent) {
    if (!controllers || rowNumber < 0 || rowNumber >= static_cast<int>(controllers->size())) {
        delete existingComponent;
        return nullptr;
    }

    auto* btn = dynamic_cast<RemoveRowButton*>(existingComponent);
    if (!btn) {
        delete existingComponent;
        btn = new RemoveRowButton();
    }

    const int capturedRow = rowNumber;
    btn->onClick = [this, capturedRow]() {
        if (onRemoveClicked)
            onRemoveClicked(capturedRow);
    };

    return btn;
}

// =============================================================================
// ControllersDialog
// =============================================================================

ControllersDialog::ControllersDialog() {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());

    // Available profiles section
    setupSectionLabel(availableLabel_, tr("controllers.available_profiles"));

    profileListModel_.profiles = &availableProfiles_;
    profileListModel_.onRowSelected = [this](int row) {
        selectedProfileIndex_ = row;
        refreshPortComboBox();
        enableButton_.setEnabled(selectedProfileIndex_ >= 0);
    };

    profileList_ = std::make_unique<juce::ListBox>("profiles", &profileListModel_);
    profileList_->setColour(juce::ListBox::backgroundColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    profileList_->setColour(juce::ListBox::outlineColourId, DarkTheme::getBorderColour());
    profileList_->setOutlineThickness(1);
    profileList_->setRowHeight(46);
    addAndMakeVisible(*profileList_);

    // Port selection row
    portLabel_.setText(tr("controllers.midi_input_port"), juce::dontSendNotification);
    portLabel_.setColour(juce::Label::textColourId,
                         DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    portLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    addAndMakeVisible(portLabel_);

    portComboBox_.setColour(juce::ComboBox::backgroundColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    portComboBox_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    portComboBox_.setColour(juce::ComboBox::outlineColourId, DarkTheme::getBorderColour());
    addAndMakeVisible(portComboBox_);

    enableButton_.setButtonText(tr("controllers.enable"));
    enableButton_.setEnabled(false);
    enableButton_.onClick = [this]() { onEnableClicked(); };
    addAndMakeVisible(enableButton_);

    // Enabled controllers section
    setupSectionLabel(enabledLabel_, tr("controllers.my_controllers"));

    controllerListModel_.controllers = &enabledControllers_;
    controllerListModel_.onRemoveClicked = [this](int row) { onRemoveClicked(row); };

    controllerList_ = std::make_unique<juce::ListBox>("controllers", &controllerListModel_);
    controllerList_->setColour(juce::ListBox::backgroundColourId,
                               DarkTheme::getColour(DarkTheme::SURFACE));
    controllerList_->setColour(juce::ListBox::outlineColourId, DarkTheme::getBorderColour());
    controllerList_->setOutlineThickness(1);
    controllerList_->setRowHeight(46);
    addAndMakeVisible(*controllerList_);

    // Close button
    closeButton_.setButtonText(tr("controllers.close"));
    closeButton_.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            juce::MessageManager::callAsync([dw]() { delete dw; });
    };
    addAndMakeVisible(closeButton_);

    // Load data
    rebuildProfileList();
    rebuildControllerList();

    // Populate MIDI inputs
    liveInputs_ = juce::MidiInput::getAvailableDevices();
    portComboBox_.addItem(tr("controllers.select_port"), 1);
    portComboBox_.setSelectedId(1);

    for (int i = 0; i < liveInputs_.size(); ++i)
        portComboBox_.addItem(liveInputs_[i].name, i + 2);

    // Register for registry updates
    ControllerRegistry::getInstance().addListener(this);

    setSize(560, 500);
}

ControllersDialog::~ControllersDialog() {
    ControllerRegistry::getInstance().removeListener(this);
    setLookAndFeel(nullptr);
    profileList_->setModel(nullptr);
    controllerList_->setModel(nullptr);
}

void ControllersDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));

    // Horizontal divider between the two sections
    auto bounds = getLocalBounds().reduced(16);
    // Approximate divider position: below the enable button area
    // We'll draw it at fixed Y for simplicity; layout matches resized()
    const int dividerY = 16 + 24 + 6 + 184 + 8 + 20 + 8 + 28 + 12;
    g.setColour(DarkTheme::getBorderColour());
    g.drawHorizontalLine(dividerY, static_cast<float>(bounds.getX()),
                         static_cast<float>(bounds.getRight()));
}

void ControllersDialog::resized() {
    auto bounds = getLocalBounds().reduced(16);
    const int labelH = 24;
    const int spacing = 8;
    const int buttonH = 28;
    const int buttonW = 90;
    const int portRowH = 28;

    // Available profiles section
    availableLabel_.setBounds(bounds.removeFromTop(labelH));
    bounds.removeFromTop(6);

    profileList_->setBounds(bounds.removeFromTop(184));
    bounds.removeFromTop(spacing);

    // Port row
    auto portRow = bounds.removeFromTop(portRowH);
    portLabel_.setBounds(portRow.removeFromLeft(120));
    portRow.removeFromLeft(6);
    portComboBox_.setBounds(portRow);
    bounds.removeFromTop(spacing);

    // Enable button
    enableButton_.setBounds(bounds.removeFromTop(buttonH).removeFromLeft(buttonW));
    bounds.removeFromTop(12);

    // Divider space is handled in paint(); just skip a little
    bounds.removeFromTop(4);

    // Enabled controllers section
    enabledLabel_.setBounds(bounds.removeFromTop(labelH));
    bounds.removeFromTop(6);

    // Reserve close button at bottom
    auto bottomRow = bounds.removeFromBottom(buttonH);
    bounds.removeFromBottom(spacing);

    controllerList_->setBounds(bounds);

    // Close button — right-aligned
    closeButton_.setBounds(bottomRow.removeFromRight(buttonW));
}

// -----------------------------------------------------------------------------
// Data helpers
// -----------------------------------------------------------------------------

void ControllersDialog::rebuildProfileList() {
    availableProfiles_ = ControllerProfileRegistry::getInstance().all();
    if (profileList_)
        profileList_->updateContent();
}

void ControllersDialog::rebuildControllerList() {
    enabledControllers_ = ControllerRegistry::getInstance().all();
    if (controllerList_)
        controllerList_->updateContent();
}

void ControllersDialog::refreshPortComboBox() {
    if (selectedProfileIndex_ < 0 ||
        selectedProfileIndex_ >= static_cast<int>(availableProfiles_.size())) {
        portComboBox_.setSelectedId(1);  // placeholder
        return;
    }

    const auto& profile = availableProfiles_[static_cast<size_t>(selectedProfileIndex_)];

    if (profile.portMatchPattern.isEmpty()) {
        portComboBox_.setSelectedId(1);
        return;
    }

    // Substring match against live device names
    for (int i = 0; i < liveInputs_.size(); ++i) {
        if (liveInputs_[i].name.containsIgnoreCase(profile.portMatchPattern)) {
            portComboBox_.setSelectedId(i + 2);
            return;
        }
    }

    portComboBox_.setSelectedId(1);
}

// -----------------------------------------------------------------------------
// Enable flow
// -----------------------------------------------------------------------------

void ControllersDialog::onEnableClicked() {
    if (selectedProfileIndex_ < 0 ||
        selectedProfileIndex_ >= static_cast<int>(availableProfiles_.size()))
        return;

    const auto& profile = availableProfiles_[static_cast<size_t>(selectedProfileIndex_)];

    // Get selected port identifier
    const int cbId = portComboBox_.getSelectedId();
    juce::String portId;
    juce::String portName;
    if (cbId >= 2 && cbId - 2 < liveInputs_.size()) {
        portId = liveInputs_[cbId - 2].identifier;
        portName = liveInputs_[cbId - 2].name;
    }

    if (portId.isEmpty() && portName.isEmpty()) {
        // Nothing selected; use placeholder text so the user knows what's stored
        portId = portComboBox_.getText();
    }

    // Materialise
    auto mat = materialiseControllerFromProfile(profile, portId.isEmpty() ? portName : portId, {});

    // Add to registries
    ControllerRegistry::getInstance().add(mat.controller);
    for (const auto& b : mat.bindings)
        BindingRegistry::getInstance().add(BindingScope::Global, b);

    Config::getInstance().save();
    rebuildControllerList();
}

// -----------------------------------------------------------------------------
// Remove flow
// -----------------------------------------------------------------------------

void ControllersDialog::onRemoveClicked(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= static_cast<int>(enabledControllers_.size()))
        return;

    const auto& c = enabledControllers_[static_cast<size_t>(rowIndex)];

    const juce::String title = tr("controllers.remove_confirm_title");
    juce::String msg = tr("controllers.remove_confirm_message");
    msg = msg.replace("{0}", c.name);

    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::QuestionIcon, title, msg, tr("dialogs.ok"), tr("dialogs.cancel"),
        nullptr, juce::ModalCallbackFunction::create([this, id = c.id](int result) {
            if (result != 1)
                return;

            // Remove all global bindings owned by this controller
            auto bindings = BindingRegistry::getInstance().bindings(BindingScope::Global);
            for (const auto& b : bindings) {
                if (b.source.controllerId == id)
                    BindingRegistry::getInstance().remove(BindingScope::Global, b.id);
            }

            ControllerRegistry::getInstance().remove(id);
            Config::getInstance().save();
            rebuildControllerList();
        }));
}

// -----------------------------------------------------------------------------
// ControllerRegistryListener
// -----------------------------------------------------------------------------

void ControllersDialog::controllerRegistryChanged() {
    rebuildControllerList();
}

// -----------------------------------------------------------------------------
// Misc helpers
// -----------------------------------------------------------------------------

juce::String ControllersDialog::selectedPortIdentifier() const {
    const int cbId = portComboBox_.getSelectedId();
    if (cbId >= 2 && cbId - 2 < liveInputs_.size())
        return liveInputs_[cbId - 2].identifier;
    return {};
}

juce::String ControllersDialog::portDisplayName(const juce::String& identifier) const {
    for (const auto& dev : liveInputs_)
        if (dev.identifier == identifier)
            return dev.name;
    return identifier;
}

void ControllersDialog::setupSectionLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    label.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label);
}

// =============================================================================
// showDialog
// =============================================================================

void ControllersDialog::showDialog(juce::Component* /*parent*/) {
    auto* dialog = new ControllersDialog();
    auto bg = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);

    auto* window = new juce::DialogWindow(tr("controllers.title"), bg, false, true);
    window->setContentOwned(dialog, true);
    window->setUsingNativeTitleBar(true);
    window->setResizable(true, false);
    window->setAlwaysOnTop(true);
    window->centreWithSize(dialog->getWidth(), dialog->getHeight());
    window->setVisible(true);
}

}  // namespace magda
