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
#include "ui/components/common/MagdaAlertWindow.hpp"

namespace magda {

// =============================================================================
// PluginSettingsPage
// =============================================================================

PluginSettingsPage::PluginSettingsPage() : scanProgressBar_(scanProgress_) {
    // Load current data
    customPaths_ = Config::getInstance().getCustomPluginPaths();

    excludedPlugins_ = PluginService::getInstance().excludedPlugins();
    systemPaths_ = PluginService::getInstance().systemSearchPaths();

    toolbar_.onResize = [this] { layoutToolbar(); };
    setupSectionHeader(foldersHeader_, tr("plugin_settings.section.folders"));
    setupSectionHeader(scanningHeader_, tr("plugin_settings.section.scanning"));

    addDirButton_.setButtonText(trEllipsis("plugin_settings.button.add_folder"));
    addDirButton_.onClick = [this]() {
        fileChooser_ =
            std::make_unique<juce::FileChooser>(tr("plugin_settings.dialog.select_directory"));
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc) {
                auto result = fc.getResult();
                if (result.exists()) {
                    customPaths_.push_back(result.getFullPathName().toStdString());
                    rebuildFolderRows();
                }
            });
    };
    addAndMakeVisible(addDirButton_);

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
                    safeThis->rebuildExclusionRows();
                });
            });
    };
    toolbar_.addAndMakeVisible(scanButton_);

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
                    safeThis->rebuildExclusionRows();
                });
            });
    };
    MagdaAlertWindow::setRole(scanNewButton_, MagdaAlertWindow::ButtonRole::Primary);
    toolbar_.addAndMakeVisible(scanNewButton_);

    viewReportButton_.setButtonText(tr("plugin_settings.button.view_report"));
    viewReportButton_.onClick = [this]() {
        auto reportFile = PluginService::getInstance().scanReportFile();
        if (reportFile.existsAsFile())
            reportFile.startAsProcess();
    };
    toolbar_.addAndMakeVisible(viewReportButton_);

    stopScanButton_.setButtonText(tr("plugin_settings.button.stop_scan"));
    stopScanButton_.onClick = [this] {
        // An abort does not report back through the completion callback.
        PluginService::getInstance().abortScan();
        setScanningUIEnabled(true);
        updatePluginCountLabel();
    };
    toolbar_.addChildComponent(stopScanButton_);

    scanOnStartupToggle_.setButtonText(tr("plugin_settings.toggle.scan_on_startup"));
    scanOnStartupToggle_.setToggleState(Config::getInstance().getScanPluginsOnStartup(),
                                        juce::dontSendNotification);
    scanOnStartupToggle_.setColour(juce::ToggleButton::textColourId,
                                   ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
    addAndMakeVisible(scanOnStartupToggle_);

    formatPreferenceLabel_.setText(
        tr("plugin_settings.label.external_format_preference").trimCharactersAtEnd(":"),
        juce::dontSendNotification);
    formatPreferenceLabel_.setFont(FontManager::getInstance().getUIFont(13.0f));
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

    scanProgressBar_.setPercentageDisplay(false);
    toolbar_.addChildComponent(scanProgressBar_);

    scanStatusLabel_.setColour(juce::Label::textColourId,
                               ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    scanStatusLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    toolbar_.addAndMakeVisible(scanStatusLabel_);

    pluginCountLabel_.setFont(FontManager::getInstance().getUIFontMedium(12.5f));
    updatePluginCountLabel();
    toolbar_.addAndMakeVisible(pluginCountLabel_);

    // Excluded plugins section
    setupSectionHeader(excludedHeader_, tr("plugin_settings.section.excluded"));

    resetAllButton_.setButtonText(tr("plugin_settings.button.reset_all"));
    daw::ui::SettingsLookAndFeel::markLinkButton(resetAllButton_);
    resetAllButton_.onClick = [this]() {
        excludedPlugins_.clear();
        rebuildExclusionRows();
    };
    addAndMakeVisible(resetAllButton_);

    excludedEmpty_.setText(tr("plugin_settings.excluded_none"), juce::dontSendNotification);
    excludedEmpty_.setFont(FontManager::getInstance().getUIFont(12.0f));
    excludedEmpty_.setColour(juce::Label::textColourId,
                             ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    excludedEmpty_.setBorderSize({});
    addChildComponent(excludedEmpty_);

    rebuildFolderRows();
    rebuildExclusionRows();
    setSize(550, getPreferredHeight(550));
}

PluginSettingsPage::~PluginSettingsPage() = default;

void PluginSettingsPage::paint(juce::Graphics& g) {
    g.setColour(ActiveTheme::getBorderColour().withMultipliedAlpha(0.6f));
    for (const auto& line : rowLines_)
        g.fillRect(line);

    // Folder paths, with the system ones tagged.
    const auto mono = FontManager::getInstance().getMonoFont(12.0f);
    for (std::size_t i = 0; i < folderRows_.size(); ++i) {
        auto row = folderRows_[i];
        const bool system = i < systemPaths_.size();
        const auto path = system ? systemPaths_[i] : customPaths_[i - systemPaths_.size()];
        if (system) {
            const auto chip = row.removeFromRight(58).withSizeKeepingCentre(52, 18).toFloat();
            g.setColour(ActiveTheme::getBorderColour());
            g.drawRoundedRectangle(chip, 3.0f, 1.0f);
            g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
            g.setFont(FontManager::getInstance().getUIFont(11.0f));
            g.drawText(tr("plugin_settings.label.system"), chip, juce::Justification::centred);
        } else {
            row.removeFromRight(70);
        }
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        g.setFont(mono);
        g.drawText(juce::String(path), row, juce::Justification::centredLeft, true);
    }

    // Excluded plugins: name and where it lives, why, and when.
    if (!excludedPlugins_.empty()) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        g.setFont(FontManager::getInstance().getUIFontMedium(11.0f).withExtraKerningFactor(0.08f));
        g.drawText(tr("plugin_settings.column.plugin").toUpperCase(), excludedColumns_[0],
                   juce::Justification::centredLeft);
        g.drawText(tr("plugin_settings.column.reason").toUpperCase(), excludedColumns_[1],
                   juce::Justification::centredLeft);
        g.drawText(tr("plugin_settings.column.date").toUpperCase(), excludedColumns_[2],
                   juce::Justification::centredLeft);
    }
    for (std::size_t i = 0; i < exclusionRows_.size() && i < excludedPlugins_.size(); ++i) {
        const auto& entry = excludedPlugins_[i];
        const auto row = exclusionRows_[i];
        const auto nameArea =
            row.withX(excludedColumns_[0].getX()).withWidth(excludedColumns_[0].getWidth());
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        g.setFont(FontManager::getInstance().getUIFont(13.0f));
        g.drawText(pluginDisplayName(entry.path), nameArea.withHeight(22).translated(0, 3),
                   juce::Justification::centredLeft, true);
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.drawText(entry.path, nameArea.withTrimmedTop(24).withHeight(16),
                   juce::Justification::centredLeft, true);

        const auto reason =
            juce::Rectangle<int>(excludedColumns_[1].getX(), row.getY(),
                                 excludedColumns_[1].getWidth() - 12, row.getHeight())
                .withSizeKeepingCentre(excludedColumns_[1].getWidth() - 12, 20)
                .toFloat();
        g.setColour(ActiveTheme::getAccentColour().withAlpha(0.12f));
        g.fillRoundedRectangle(reason, 3.0f);
        g.setColour(ActiveTheme::getAccentColour().withAlpha(0.8f));
        g.drawRoundedRectangle(reason.reduced(0.5f), 3.0f, 1.0f);
        g.setFont(FontManager::getInstance().getMonoFont(11.0f));
        g.drawText(entry.reason, reason.reduced(6.0f, 0.0f), juce::Justification::centredLeft,
                   true);

        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        g.drawText(entry.timestamp.upToFirstOccurrenceOf("T", false, false),
                   excludedColumns_[2].withY(row.getY()).withHeight(row.getHeight()),
                   juce::Justification::centredLeft, true);
    }
}

void PluginSettingsPage::resized() {
    layOut(getWidth(), true);
}

int PluginSettingsPage::getPreferredHeight(int width) {
    return layOut(width, false);
}

int PluginSettingsPage::layOut(int width, bool apply) {
    constexpr int pad = 16;
    constexpr int rowH = 34;
    constexpr int headerH = 30;
    constexpr int sectionGap = 22;
    const auto place = [apply](juce::Component& c, juce::Rectangle<int> r) {
        if (apply)
            c.setBounds(r);
    };
    if (apply) {
        rowLines_.clear();
        folderRows_.clear();
        exclusionRows_.clear();
    }
    const auto takeRow = [this, apply](juce::Rectangle<int>& b, int h) {
        auto row = b.removeFromTop(h);
        if (apply)
            rowLines_.push_back(row.withTop(row.getBottom() - 1));
        return row;
    };

    auto b = juce::Rectangle<int>(0, 0, width, 100000).reduced(0, pad);

    place(foldersHeader_, b.removeFromTop(headerH));
    const auto folderCount = systemPaths_.size() + customPaths_.size();
    for (std::size_t i = 0; i < folderCount; ++i) {
        auto row = takeRow(b, rowH);
        if (apply)
            folderRows_.push_back(row);
        if (i >= systemPaths_.size()) {
            const auto index = i - systemPaths_.size();
            if (index < folderRemoveButtons_.size())
                place(*folderRemoveButtons_[index], row.removeFromRight(64).reduced(0, 6));
        }
    }
    {
        auto row = b.removeFromTop(44).reduced(0, 6);
        const int w =
            juce::GlyphArrangement::getStringWidthInt(FontManager::getInstance().getUIFont(13.5f),
                                                      addDirButton_.getButtonText()) +
            32;
        place(addDirButton_, row.removeFromLeft(w));
    }

    b.removeFromTop(sectionGap);
    place(scanningHeader_, b.removeFromTop(headerH));
    place(scanOnStartupToggle_, takeRow(b, rowH).reduced(0, 6));
    {
        auto row = takeRow(b, rowH);
        place(formatPreferenceSelector_,
              row.removeFromRight(juce::jlimit(150, 230, row.getWidth() / 2)).reduced(0, 4));
        place(formatPreferenceLabel_, row);
    }

    b.removeFromTop(sectionGap);
    {
        auto header = b.removeFromTop(headerH);
        place(excludedHeader_, header);
        if (apply)
            excludedHeader_.setText(
                tr("plugin_settings.section.excluded") +
                    (excludedPlugins_.empty()
                         ? juce::String()
                         : " \xc2\xb7 " + juce::String(excludedPlugins_.size())),
                juce::dontSendNotification);
        place(resetAllButton_, header.removeFromRight(90).withTrimmedBottom(4));
    }
    if (excludedPlugins_.empty()) {
        place(excludedEmpty_, b.removeFromTop(40).withTrimmedTop(10));
    } else {
        auto columns = takeRow(b, 28);
        auto actions = columns.removeFromRight(70);
        juce::ignoreUnused(actions);
        if (apply) {
            excludedColumns_[2] = columns.removeFromRight(90);
            excludedColumns_[1] = columns.removeFromRight(juce::jmin(190, columns.getWidth() / 3));
            excludedColumns_[0] = columns;
        }
        for (std::size_t i = 0; i < excludedPlugins_.size(); ++i) {
            auto row = takeRow(b, 44);
            if (apply)
                exclusionRows_.push_back(row);
            if (i < exclusionRemoveButtons_.size())
                place(*exclusionRemoveButtons_[i], row.removeFromRight(64).reduced(0, 10));
        }
    }
    return b.getY() + pad;
}

void PluginSettingsPage::layoutToolbar() {
    auto bar = toolbar_.getLocalBounds();
    const auto buttonWidth = [](juce::Button& button) {
        return juce::GlyphArrangement::getStringWidthInt(
                   FontManager::getInstance().getUIFont(13.5f), button.getButtonText()) +
               32;
    };
    if (stopScanButton_.isVisible()) {
        stopScanButton_.setBounds(bar.removeFromRight(buttonWidth(stopScanButton_)));
        bar.removeFromRight(12);
        scanProgressBar_.setBounds(bar.removeFromLeft(120).withSizeKeepingCentre(120, 6));
        bar.removeFromLeft(12);
        scanStatusLabel_.setBounds(bar);
        return;
    }
    for (auto* button : {&scanNewButton_, &scanButton_, &viewReportButton_}) {
        button->setBounds(bar.removeFromRight(buttonWidth(*button)));
        bar.removeFromRight(6);
    }
    const int countW = juce::GlyphArrangement::getStringWidthInt(pluginCountLabel_.getFont(),
                                                                 pluginCountLabel_.getText()) +
                       12;
    pluginCountLabel_.setBounds(bar.removeFromLeft(countW));
    scanStatusLabel_.setBounds(bar);
}

void PluginSettingsPage::rebuildFolderRows() {
    folderRemoveButtons_.clear();
    for (std::size_t i = 0; i < customPaths_.size(); ++i) {
        auto button = std::make_unique<juce::TextButton>(tr("plugin_settings.button.remove"));
        daw::ui::SettingsLookAndFeel::markLinkButton(*button);
        button->onClick = [this, i] {
            if (i < customPaths_.size()) {
                customPaths_.erase(customPaths_.begin() + static_cast<std::ptrdiff_t>(i));
                juce::MessageManager::callAsync(
                    [safe = juce::Component::SafePointer<PluginSettingsPage>(this)] {
                        if (safe != nullptr)
                            safe->rebuildFolderRows();
                    });
            }
        };
        addAndMakeVisible(*button);
        folderRemoveButtons_.push_back(std::move(button));
    }
    relayout();
}

void PluginSettingsPage::rebuildExclusionRows() {
    exclusionRemoveButtons_.clear();
    for (std::size_t i = 0; i < excludedPlugins_.size(); ++i) {
        auto button = std::make_unique<juce::TextButton>(tr("plugin_settings.button.remove"));
        daw::ui::SettingsLookAndFeel::markLinkButton(*button);
        button->onClick = [this, i] {
            if (i < excludedPlugins_.size()) {
                excludedPlugins_.erase(excludedPlugins_.begin() + static_cast<std::ptrdiff_t>(i));
                juce::MessageManager::callAsync(
                    [safe = juce::Component::SafePointer<PluginSettingsPage>(this)] {
                        if (safe != nullptr)
                            safe->rebuildExclusionRows();
                    });
            }
        };
        addAndMakeVisible(*button);
        exclusionRemoveButtons_.push_back(std::move(button));
    }
    excludedEmpty_.setVisible(excludedPlugins_.empty());
    resetAllButton_.setVisible(!excludedPlugins_.empty());
    relayout();
}

// Rows come and go, so the page's height follows them.
void PluginSettingsPage::relayout() {
    if (getWidth() > 0)
        setSize(getWidth(), juce::jmax(getParentHeight(), getPreferredHeight(getWidth())));
    resized();
    repaint();
}

void PluginSettingsPage::setScanningUIEnabled(bool enabled) {
    addDirButton_.setEnabled(enabled);
    for (auto& button : folderRemoveButtons_)
        button->setEnabled(enabled);
    for (auto& button : exclusionRemoveButtons_)
        button->setEnabled(enabled);
    resetAllButton_.setEnabled(enabled);
    scanOnStartupToggle_.setEnabled(enabled);
    formatPreferenceSelector_.setEnabled(enabled);

    // While scanning the toolbar shows progress and Stop; otherwise the count and the actions.
    for (auto* button : {&scanButton_, &scanNewButton_, &viewReportButton_})
        button->setVisible(enabled);
    pluginCountLabel_.setVisible(enabled);
    stopScanButton_.setVisible(!enabled);
    scanProgressBar_.setVisible(!enabled);
    layoutToolbar();
}

void PluginSettingsPage::load(const Config& config) {
    customPaths_ = config.getCustomPluginPaths();
    scanOnStartupToggle_.setToggleState(config.getScanPluginsOnStartup(),
                                        juce::dontSendNotification);
    rebuildFolderRows();
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
    layoutToolbar();
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
        return {{getTitle(), &page_, [this](int width) { return page_.getPreferredHeight(width); },
                 &page_.getToolbar()}};
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
