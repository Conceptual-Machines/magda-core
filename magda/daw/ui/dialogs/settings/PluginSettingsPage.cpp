#include "PluginSettingsPage.hpp"

#include <algorithm>

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/SettingsLookAndFeel.hpp"
#include "SettingsSections.hpp"
#include "core/Config.hpp"
#include "core/StringTable.hpp"
#include "core/TechnicalText.hpp"
#include "engine/PluginScanCoordinator.hpp"
#include "engine/PluginService.hpp"

namespace magda {

// =============================================================================
// DirectoryListModel
// =============================================================================

int PluginSettingsPage::DirectoryListModel::getNumRows() {
    return paths ? static_cast<int>(paths->size()) : 0;
}

void PluginSettingsPage::DirectoryListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                                              int width, int height,
                                                              bool rowIsSelected) {
    if (!paths || rowNumber < 0 || rowNumber >= static_cast<int>(paths->size()))
        return;

    if (rowIsSelected) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY).withAlpha(0.3f));
        g.fillRect(0, 0, width, height);
    }

    g.setColour(ActiveTheme::getTextColour());
    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    g.drawText(juce::String((*paths)[static_cast<size_t>(rowNumber)]), 4, 0, width - 8, height,
               juce::Justification::centredLeft);
}

// =============================================================================
// ExcludedTableModel
// =============================================================================

int PluginSettingsPage::ExcludedTableModel::getNumRows() {
    return entries ? static_cast<int>(entries->size()) : 0;
}

void PluginSettingsPage::ExcludedTableModel::paintRowBackground(juce::Graphics& g,
                                                                int /*rowNumber*/, int width,
                                                                int height, bool rowIsSelected) {
    if (rowIsSelected) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY).withAlpha(0.3f));
    } else {
        g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE));
    }
    g.fillRect(0, 0, width, height);
}

void PluginSettingsPage::ExcludedTableModel::paintCell(juce::Graphics& g, int rowNumber,
                                                       int columnId, int width, int height,
                                                       bool /*rowIsSelected*/) {
    if (!entries || rowNumber < 0 || rowNumber >= static_cast<int>(entries->size()))
        return;

    const auto& entry = (*entries)[static_cast<size_t>(rowNumber)];

    g.setColour(ActiveTheme::getTextColour());
    g.setFont(FontManager::getInstance().getUIFont(11.0f));

    juce::String text;
    switch (columnId) {
        case 1: {
            text = pluginDisplayName(entry.path);
            break;
        }
        case 2:
            text = entry.reason;
            break;
        case 3:
            text = entry.timestamp;
            break;
        default:  // dismissed, or a column with no text
            break;
    }

    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

juce::Component* PluginSettingsPage::ExcludedTableModel::refreshComponentForCell(int, int, bool,
                                                                                 juce::Component*) {
    return nullptr;
}

// =============================================================================
// PluginSettingsPage
// =============================================================================

PluginSettingsPage::PluginSettingsPage() : scanProgressBar_(scanProgress_) {
    // Load current data
    customPaths_ = Config::getInstance().getCustomPluginPaths();

    excludedPlugins_ = PluginService::getInstance().excludedPlugins();
    systemPaths_ = PluginService::getInstance().systemSearchPaths();

    // Wire up models
    systemDirListModel_.paths = &systemPaths_;
    dirListModel_.paths = &customPaths_;
    excludedTableModel_.entries = &excludedPlugins_;

    // System directories section (read-only)
    setupSectionHeader(systemDirsHeader_, tr("plugin_settings.section.system_directories"));

    systemDirsList_.setModel(&systemDirListModel_);
    systemDirsList_.setColour(juce::ListBox::backgroundColourId,
                              ActiveTheme::getColour(ActiveTheme::SURFACE));
    systemDirsList_.setColour(juce::ListBox::outlineColourId, ActiveTheme::getBorderColour());
    systemDirsList_.setOutlineThickness(1);
    systemDirsList_.setRowHeight(22);
    addAndMakeVisible(systemDirsList_);

    // Custom directories section
    setupSectionHeader(directoriesHeader_, tr("plugin_settings.section.custom_directories"));

    directoriesList_.setModel(&dirListModel_);
    directoriesList_.setColour(juce::ListBox::backgroundColourId,
                               ActiveTheme::getColour(ActiveTheme::SURFACE));
    directoriesList_.setColour(juce::ListBox::outlineColourId, ActiveTheme::getBorderColour());
    directoriesList_.setOutlineThickness(1);
    directoriesList_.setRowHeight(22);
    addAndMakeVisible(directoriesList_);

    addDirButton_.setButtonText(trEllipsis("plugin_settings.button.add"));
    addDirButton_.onClick = [this]() {
        fileChooser_ =
            std::make_unique<juce::FileChooser>(tr("plugin_settings.dialog.select_directory"));
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc) {
                auto result = fc.getResult();
                if (result.exists()) {
                    customPaths_.push_back(result.getFullPathName().toStdString());
                    directoriesList_.updateContent();
                    directoriesList_.repaint();
                }
            });
    };
    addAndMakeVisible(addDirButton_);

    removeDirButton_.setButtonText(tr("plugin_settings.button.remove"));
    removeDirButton_.onClick = [this]() {
        int selected = directoriesList_.getSelectedRow();
        if (selected >= 0 && selected < static_cast<int>(customPaths_.size())) {
            customPaths_.erase(customPaths_.begin() + selected);
            directoriesList_.updateContent();
            directoriesList_.repaint();
        }
    };
    addAndMakeVisible(removeDirButton_);

    // Scan section
    scanButton_.setButtonText(tr("plugin_settings.button.scan"));
    scanButton_.onClick = [this]() {
        // Apply settings first so custom paths are used during scan
        applySettings();

        setScanningUIEnabled(false);
        scanProgress_ = 0.0;
        scanStatusLabel_.setText(trEllipsis("plugin_settings.status.starting"),
                                 juce::dontSendNotification);
        scanProgressBar_.setVisible(true);
        scanStatusLabel_.setVisible(true);

        auto safeThis = juce::Component::SafePointer<PluginSettingsPage>(this);

        PluginService::getInstance().startScan(
            [safeThis](float progress, const juce::String& pluginName) {
                juce::MessageManager::callAsync([safeThis, progress, pluginName]() {
                    if (safeThis == nullptr)
                        return;
                    safeThis->scanProgress_ = static_cast<double>(progress);
                    safeThis->scanStatusLabel_.setText(tr("plugin_settings.status.scanning") + " " +
                                                           pluginDisplayName(pluginName),
                                                       juce::dontSendNotification);
                });
            });

        PluginService::getInstance().setScanCompletionCallback(
            [safeThis](bool success, int numPlugins, const juce::StringArray& failedPlugins) {
                juce::MessageManager::callAsync([safeThis, success, numPlugins, failedPlugins]() {
                    if (safeThis == nullptr)
                        return;
                    safeThis->setScanningUIEnabled(true);
                    safeThis->scanProgress_ = -1.0;
                    safeThis->scanProgressBar_.setVisible(false);
                    if (!success) {
                        juce::String message = tr("plugin_settings.status.failed");
                        if (numPlugins > 0)
                            message += " (" + juce::String(numPlugins) + " " +
                                       tr("plugin_settings.status.found_before_error") + ")";
                        if (failedPlugins.size() > 0)
                            message += ", " + juce::String(failedPlugins.size()) + " " +
                                       tr("plugin_settings.status.plugins_failed");
                        safeThis->scanStatusLabel_.setText(message, juce::dontSendNotification);
                    } else {
                        safeThis->scanStatusLabel_.setText(
                            tr("plugin_settings.status.found") + " " + juce::String(numPlugins) +
                                " " + tr("plugin_settings.status.plugins") +
                                (failedPlugins.size() > 0
                                     ? ", " + juce::String(failedPlugins.size()) + " " +
                                           tr("plugin_settings.status.failed_short")
                                     : ""),
                            juce::dontSendNotification);
                    }

                    safeThis->updatePluginCountLabel();

                    // Refresh excluded plugins list
                    safeThis->excludedPlugins_ = PluginService::getInstance().excludedPlugins();
                    safeThis->markCommitted();
                    safeThis->excludedTable_.updateContent();
                    safeThis->excludedTable_.repaint();
                });
            });
    };
    addAndMakeVisible(scanButton_);

    scanNewButton_.setButtonText(tr("plugin_settings.button.scan_new"));
    scanNewButton_.onClick = [this]() {
        applySettings();

        setScanningUIEnabled(false);
        scanProgress_ = -1.0;  // detectNewPlugins doesn't report fractional progress
        scanStatusLabel_.setText(trEllipsis("plugin_settings.status.checking_new"),
                                 juce::dontSendNotification);
        scanProgressBar_.setVisible(true);
        scanStatusLabel_.setVisible(true);

        auto safeThis = juce::Component::SafePointer<PluginSettingsPage>(this);

        PluginService::getInstance().detectNewPlugins(
            [safeThis](PluginScanPhase phase, const juce::String& currentPlugin) {
                juce::MessageManager::callAsync([safeThis, phase, currentPlugin]() {
                    if (safeThis == nullptr)
                        return;
                    using Phase = PluginScanPhase;
                    switch (phase) {
                        case Phase::Discovering:
                            safeThis->scanStatusLabel_.setText(
                                trEllipsis("plugin_settings.status.checking_new"),
                                juce::dontSendNotification);
                            break;
                        case Phase::UpToDate:
                            // Final message for the no-new-plugins path; the
                            // completion callback runs right after but keeps
                            // this text rather than overwriting it.
                            safeThis->scanStatusLabel_.setText(
                                tr("plugin_settings.status.up_to_date"),
                                juce::dontSendNotification);
                            break;
                        case Phase::Scanning:
                            safeThis->scanStatusLabel_.setText(
                                tr("plugin_settings.status.scanning") + " " +
                                    pluginDisplayName(currentPlugin),
                                juce::dontSendNotification);
                            break;
                    }
                });
            },
            [safeThis](bool success, int addedCount, int totalCount,
                       const juce::StringArray& failedPlugins) {
                juce::MessageManager::callAsync([safeThis, success, addedCount, totalCount,
                                                 failedPlugins]() {
                    if (safeThis == nullptr)
                        return;
                    safeThis->setScanningUIEnabled(true);
                    safeThis->scanProgress_ = -1.0;
                    safeThis->scanProgressBar_.setVisible(false);

                    if (!success) {
                        juce::String message = tr("plugin_settings.status.failed");
                        if (failedPlugins.size() > 0)
                            message += ", " + juce::String(failedPlugins.size()) + " " +
                                       tr("plugin_settings.status.plugins_failed");
                        safeThis->scanStatusLabel_.setText(message, juce::dontSendNotification);
                    } else if (addedCount > 0) {
                        safeThis->scanStatusLabel_.setText(
                            tr("plugin_settings.status.added") + " " + juce::String(addedCount) +
                                " " + tr("plugin_settings.status.new_plugins") +
                                (failedPlugins.size() > 0
                                     ? ", " + juce::String(failedPlugins.size()) + " " +
                                           tr("plugin_settings.status.failed_short")
                                     : ""),
                            juce::dontSendNotification);
                    }
                    // addedCount == 0 → leave the "Plugins up to date"
                    // message that the status callback already set.

                    juce::ignoreUnused(totalCount);
                    safeThis->updatePluginCountLabel();

                    safeThis->excludedPlugins_ = PluginService::getInstance().excludedPlugins();
                    safeThis->markCommitted();
                    safeThis->excludedTable_.updateContent();
                    safeThis->excludedTable_.repaint();
                });
            });
    };
    addAndMakeVisible(scanNewButton_);

    viewReportButton_.setButtonText(tr("plugin_settings.button.view_report"));
    viewReportButton_.onClick = [this]() {
        auto reportFile = PluginService::getInstance().scanReportFile();
        if (reportFile.existsAsFile())
            reportFile.startAsProcess();
    };
    addAndMakeVisible(viewReportButton_);

    scanOnStartupToggle_.setButtonText(tr("plugin_settings.toggle.scan_on_startup"));
    scanOnStartupToggle_.setToggleState(Config::getInstance().getScanPluginsOnStartup(),
                                        juce::dontSendNotification);
    scanOnStartupToggle_.setColour(juce::ToggleButton::textColourId,
                                   ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    addAndMakeVisible(scanOnStartupToggle_);

    formatPreferenceLabel_.setText(tr("plugin_settings.label.external_format_preference"),
                                   juce::dontSendNotification);
    formatPreferenceLabel_.setColour(juce::Label::textColourId,
                                     ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    formatPreferenceLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    addAndMakeVisible(formatPreferenceLabel_);

    // Ids are PluginFormat values so the mapping needs no lookup table. AU is
    // macOS-only; VST3 and LV2 exist everywhere MAGDA runs.
    formatPreferenceSelector_.addItem(
        tr("plugin_settings.option.prefer_vst3")
            .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Vst3)),
        static_cast<int>(PluginFormat::VST3) + 1);
#if JUCE_MAC
    formatPreferenceSelector_.addItem(
        tr("plugin_settings.option.prefer_au")
            .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Au)),
        static_cast<int>(PluginFormat::AU) + 1);
#endif
    formatPreferenceSelector_.addItem(
        tr("plugin_settings.option.prefer_lv2")
            .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Lv2)),
        static_cast<int>(PluginFormat::LV2) + 1);
    formatPreferenceSelector_.setColour(juce::ComboBox::backgroundColourId,
                                        ActiveTheme::getColour(ActiveTheme::SURFACE));
    formatPreferenceSelector_.setColour(juce::ComboBox::textColourId, ActiveTheme::getTextColour());
    formatPreferenceSelector_.setColour(juce::ComboBox::outlineColourId,
                                        ActiveTheme::getBorderColour());
    auto currentFormatPreference =
        PluginPreferences::getInstance().externalPluginFormatPreference();
#if !JUCE_MAC
    // A config carried over from macOS can name AU, which has no item here.
    // Show VST3 rather than leaving the box blank.
    if (currentFormatPreference == PluginFormat::AU)
        currentFormatPreference = PluginFormat::VST3;
#endif
    formatPreferenceSelector_.setSelectedId(static_cast<int>(currentFormatPreference) + 1,
                                            juce::dontSendNotification);
    addAndMakeVisible(formatPreferenceSelector_);

    scanProgressBar_.setPercentageDisplay(true);
    scanProgressBar_.setVisible(false);
    addAndMakeVisible(scanProgressBar_);

    scanStatusLabel_.setColour(juce::Label::textColourId,
                               ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    scanStatusLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    scanStatusLabel_.setVisible(false);
    addAndMakeVisible(scanStatusLabel_);

    pluginCountLabel_.setColour(juce::Label::textColourId,
                                ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    pluginCountLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    updatePluginCountLabel();
    addAndMakeVisible(pluginCountLabel_);

    // Excluded plugins section
    setupSectionHeader(excludedHeader_, tr("plugin_settings.section.excluded"));

    excludedTable_.setModel(&excludedTableModel_);
    excludedTable_.setColour(juce::ListBox::backgroundColourId,
                             ActiveTheme::getColour(ActiveTheme::SURFACE));
    excludedTable_.setColour(juce::ListBox::outlineColourId, ActiveTheme::getBorderColour());
    excludedTable_.setOutlineThickness(1);
    excludedTable_.getHeader().addColumn(tr("plugin_settings.column.plugin"), 1, 250, 100, 400);
    excludedTable_.getHeader().addColumn(tr("plugin_settings.column.reason"), 2, 80, 60, 150);
    excludedTable_.getHeader().addColumn(tr("plugin_settings.column.date"), 3, 150, 80, 250);
    excludedTable_.getHeader().setColour(juce::TableHeaderComponent::backgroundColourId,
                                         ActiveTheme::getColour(ActiveTheme::SURFACE));
    excludedTable_.getHeader().setColour(juce::TableHeaderComponent::textColourId,
                                         ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    excludedTable_.setMultipleSelectionEnabled(true);
    addAndMakeVisible(excludedTable_);

    removeSelectedButton_.setButtonText(tr("plugin_settings.button.remove_selected"));
    removeSelectedButton_.onClick = [this]() {
        auto selectedRows = excludedTable_.getSelectedRows();
        std::vector<int> indices;
        for (int i = 0; i < selectedRows.size(); ++i) {
            int idx = selectedRows[i];
            if (idx >= 0 && idx < static_cast<int>(excludedPlugins_.size()))
                indices.push_back(idx);
        }
        std::ranges::sort(indices, std::ranges::greater{});
        for (int idx : indices) {
            excludedPlugins_.erase(excludedPlugins_.begin() + idx);
        }
        excludedTable_.updateContent();
        excludedTable_.repaint();
    };
    addAndMakeVisible(removeSelectedButton_);

    resetAllButton_.setButtonText(tr("plugin_settings.button.reset_all"));
    resetAllButton_.onClick = [this]() {
        excludedPlugins_.clear();
        excludedTable_.updateContent();
        excludedTable_.repaint();
    };
    addAndMakeVisible(resetAllButton_);

    setSize(550, kPreferredHeight);
}

PluginSettingsPage::~PluginSettingsPage() {
    systemDirsList_.setModel(nullptr);
    directoriesList_.setModel(nullptr);
    excludedTable_.setModel(nullptr);
}

void PluginSettingsPage::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getColour(ActiveTheme::PANEL_BACKGROUND));
}

void PluginSettingsPage::resized() {
    auto bounds = getLocalBounds().reduced(16);
    const int headerHeight = 24;
    const int buttonHeight = 28;
    const int buttonWidth = 90;
    const int spacing = 8;

    // System directories section
    systemDirsHeader_.setBounds(bounds.removeFromTop(headerHeight));
    bounds.removeFromTop(4);

    int systemDirsHeight = std::max(44, static_cast<int>(systemPaths_.size()) * 22 + 2);
    systemDirsList_.setBounds(bounds.removeFromTop(systemDirsHeight));

    bounds.removeFromTop(spacing);

    // Custom directories section
    directoriesHeader_.setBounds(bounds.removeFromTop(headerHeight));
    bounds.removeFromTop(4);

    auto dirArea = bounds.removeFromTop(88);
    auto dirButtons = dirArea.removeFromRight(buttonWidth + 4);
    directoriesList_.setBounds(dirArea);
    addDirButton_.setBounds(dirButtons.removeFromTop(buttonHeight));
    dirButtons.removeFromTop(4);
    removeDirButton_.setBounds(dirButtons.removeFromTop(buttonHeight));

    bounds.removeFromTop(spacing * 2);

    // Scan section
    auto scanRow = bounds.removeFromTop(buttonHeight);
    scanButton_.setBounds(scanRow.removeFromLeft(140));
    scanRow.removeFromLeft(spacing);
    scanNewButton_.setBounds(scanRow.removeFromLeft(130));
    scanRow.removeFromLeft(spacing);
    viewReportButton_.setBounds(scanRow.removeFromLeft(130));
    scanRow.removeFromLeft(spacing);
    scanProgressBar_.setBounds(scanRow);

    bounds.removeFromTop(2);
    scanStatusLabel_.setBounds(bounds.removeFromTop(18));
    pluginCountLabel_.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(2);
    scanOnStartupToggle_.setBounds(bounds.removeFromTop(22));
    bounds.removeFromTop(4);
    auto formatPreferenceRow = bounds.removeFromTop(buttonHeight);
    formatPreferenceLabel_.setBounds(formatPreferenceRow.removeFromLeft(210));
    formatPreferenceSelector_.setBounds(formatPreferenceRow.removeFromLeft(160));

    bounds.removeFromTop(spacing);

    // Excluded plugins section
    excludedHeader_.setBounds(bounds.removeFromTop(headerHeight));
    bounds.removeFromTop(4);

    // Excluded buttons row
    auto excludedButtonRow = bounds.removeFromBottom(buttonHeight);
    bounds.removeFromBottom(4);

    // Excluded table takes remaining space
    excludedTable_.setBounds(bounds);

    // Excluded buttons - right aligned
    {
        auto btnArea = excludedButtonRow;
        resetAllButton_.setBounds(btnArea.removeFromRight(buttonWidth));
        btnArea.removeFromRight(4);
        removeSelectedButton_.setBounds(btnArea.removeFromRight(120));
    }
}

void PluginSettingsPage::setScanningUIEnabled(bool enabled) {
    addDirButton_.setEnabled(enabled);
    removeDirButton_.setEnabled(enabled);
    scanButton_.setEnabled(enabled);
    scanNewButton_.setEnabled(enabled);
    viewReportButton_.setEnabled(enabled);
    removeSelectedButton_.setEnabled(enabled);
    resetAllButton_.setEnabled(enabled);
    scanOnStartupToggle_.setEnabled(enabled);
    formatPreferenceSelector_.setEnabled(enabled);
}

void PluginSettingsPage::load(const Config& config) {
    customPaths_ = config.getCustomPluginPaths();
    scanOnStartupToggle_.setToggleState(config.getScanPluginsOnStartup(),
                                        juce::dontSendNotification);
    directoriesList_.updateContent();
    directoriesList_.repaint();
}

void PluginSettingsPage::writeTo(Config& config) const {
    config.setCustomPluginPaths(customPaths_);
    config.setScanPluginsOnStartup(scanOnStartupToggle_.getToggleState());
}

void PluginSettingsPage::markCommitted() {
    committedFormat_ = formatPreferenceSelector_.getSelectedId();
    committedExclusions_.clear();
    for (const auto& entry : excludedPlugins_)
        committedExclusions_.add(entry.path);
}

int PluginSettingsPage::countChangesOutsideConfig() const {
    juce::StringArray exclusions;
    for (const auto& entry : excludedPlugins_)
        exclusions.add(entry.path);
    return (formatPreferenceSelector_.getSelectedId() != committedFormat_ ? 1 : 0) +
           (exclusions != committedExclusions_ ? 1 : 0);
}

void PluginSettingsPage::commitOutsideConfig() {
    if (const int selected = formatPreferenceSelector_.getSelectedId(); selected > 0)
        PluginPreferences::getInstance().setExternalPluginFormatPreference(
            static_cast<PluginFormat>(selected - 1));
    PluginService::getInstance().setExcludedPlugins(excludedPlugins_);
    markCommitted();
}

void PluginSettingsPage::applySettings() {
    auto& config = Config::getInstance();
    writeTo(config);
    config.save();
    commitOutsideConfig();
    if (onAppliedForScan)
        onAppliedForScan();
}

bool PluginSettingsPage::isScanRunning() const {
    return PluginService::getInstance().isScanRunning();
}

void PluginSettingsPage::updatePluginCountLabel() {
    int count = Config::getInstance().getTotalPluginCount();
    int excluded = static_cast<int>(excludedPlugins_.size());

    if (count > 0) {
        juce::String text =
            juce::String(count) + " " + tr("plugin_settings.status.plugins_available");
        if (excluded > 0)
            text += ", " + juce::String(excluded) + " " + tr("plugin_settings.status.excluded");
        pluginCountLabel_.setText(text, juce::dontSendNotification);
    } else {
        pluginCountLabel_.setText(tr("plugin_settings.status.none_scanned"),
                                  juce::dontSendNotification);
    }
}

void PluginSettingsPage::setupSectionHeader(juce::Label& header, const juce::String& text) {
    header.setText(text, juce::dontSendNotification);
    daw::ui::SettingsLookAndFeel::markSectionHeader(header);
    addAndMakeVisible(header);
}

namespace {

/** @brief Plugin folders, scanning and exclusions; Apply commits them. */
class PluginsSection : public SettingsSection {
  public:
    PluginsSection() : SettingsSection(SettingsSectionId::Plugins) {
        page_.markCommitted();
        // A scan applies the folders first, so they are no longer pending.
        page_.onAppliedForScan = [this] { captureBaseline(); };
    }

    juce::String getTitle() const override {
        return tr("settings.section.plugins");
    }
    std::vector<Tab> getTabs() override {
        return {{getTitle(), &page_, [](int) { return PluginSettingsPage::kPreferredHeight; }}};
    }
    void load(Config& config) override {
        page_.load(config);
    }
    void writeTo(Config& config) override {
        page_.writeTo(config);
    }
    void commit(const Config&) override {
        page_.commitOutsideConfig();
    }
    int countChanges() override {
        return SettingsSection::countChanges() + page_.countChangesOutsideConfig();
    }

  private:
    PluginSettingsPage page_;
};

}  // namespace

std::unique_ptr<SettingsSection> createPluginsSection() {
    return std::make_unique<PluginsSection>();
}

}  // namespace magda
