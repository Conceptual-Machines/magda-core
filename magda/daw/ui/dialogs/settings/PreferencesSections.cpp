// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_extra/juce_gui_extra.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <vector>

#include "../../../audio/io/AudioIOControl.hpp"
#include "../../../media_db/MediaDatabase.hpp"
#include "../../../media_db/MediaDbContext.hpp"
#include "../../../media_db/MediaDbQuery.hpp"
#include "../../../media_db/PresetDbIndexer.hpp"
#include "../../../project/ProjectManager.hpp"
#include "../../components/common/PillSegments.hpp"
#include "../../components/common/TextSlider.hpp"
#include "../../themes/ActiveTheme.hpp"
#include "../../themes/DialogLookAndFeel.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/SettingsLookAndFeel.hpp"
#include "../../themes/UserTheme.hpp"
#include "../../windows/MainWindow.hpp"
#include "../ProjectFormatChoices.hpp"
#include "SettingsLink.hpp"
#include "SettingsSections.hpp"
#include "SettingsToolbar.hpp"
#include "core/AppPaths.hpp"
#include "core/ClipManager.hpp"
#include "core/Config.hpp"
#include "core/GestureRouter.hpp"
#include "core/StringTable.hpp"
#include "core/TechnicalText.hpp"
#include "core/UIScale.hpp"
#include "engine/AudioEngine.hpp"
#include "ui/components/common/MagdaAlertWindow.hpp"

// ---------------------------------------------------------------------------
// Setup helpers — internal linkage, shared by all page classes
// ---------------------------------------------------------------------------
namespace {

void setupTextSlider(juce::Component& owner, magda::daw::ui::TextSlider& slider, juce::Label& label,
                     const juce::String& labelText, double min, double max, double interval,
                     int decimals = 0, const juce::String& suffix = {}) {
    label.setText(labelText, juce::dontSendNotification);
    label.setFont(magda::FontManager::getInstance().getUIFont(12.0f));
    label.setColour(juce::Label::textColourId,
                    magda::ActiveTheme::getColour(magda::ActiveTheme::TEXT_PRIMARY));
    label.setJustificationType(juce::Justification::centredLeft);
    owner.addAndMakeVisible(label);

    slider.setRange(min, max, interval);
    slider.setOrientation(magda::daw::ui::TextSlider::Orientation::Horizontal);
    slider.setShowFillIndicator(false);
    slider.setValueFormatter(
        [decimals, suffix](double value) { return juce::String(value, decimals) + suffix; });
    slider.setValueParser([suffix](const juce::String& text) {
        auto trimmed = text.trim();
        if (suffix.isNotEmpty() && trimmed.endsWithIgnoreCase(suffix))
            trimmed = trimmed.dropLastCharacters(suffix.length()).trim();
        return trimmed.getDoubleValue();
    });
    owner.addAndMakeVisible(slider);
}

void setupTextSlider(juce::Component& owner, magda::daw::ui::TextSlider& slider, juce::Label& label,
                     const juce::String& labelText, double min, double max, double interval,
                     magda::TechnicalTextToken suffix, int decimals = 0,
                     bool leadingSpace = false) {
    setupTextSlider(owner, slider, label, labelText, min, max, interval, decimals,
                    magda::technicalTextSuffix(suffix, leadingSpace));
}

void setupToggle(juce::Component& owner, juce::ToggleButton& toggle, const juce::String& text) {
    toggle.setButtonText(text);
    toggle.setColour(juce::ToggleButton::textColourId,
                     magda::ActiveTheme::getColour(magda::ActiveTheme::TEXT_PRIMARY));
    toggle.setColour(juce::ToggleButton::tickColourId,
                     magda::ActiveTheme::getColour(magda::ActiveTheme::ACCENT_PRIMARY));
    toggle.setColour(juce::ToggleButton::tickDisabledColourId,
                     magda::ActiveTheme::getColour(magda::ActiveTheme::TEXT_DIM));
    owner.addAndMakeVisible(toggle);
}

void setupSectionHeader(juce::Component& owner, juce::Label& header, const juce::String& text) {
    header.setText(text, juce::dontSendNotification);
    magda::daw::ui::SettingsLookAndFeel::markSectionHeader(header);
    owner.addAndMakeVisible(header);
}

void styleCombo(juce::ComboBox& combo) {
    combo.setColour(juce::ComboBox::backgroundColourId,
                    magda::ActiveTheme::getColour(magda::ActiveTheme::SURFACE));
    combo.setColour(juce::ComboBox::textColourId,
                    magda::ActiveTheme::getColour(magda::ActiveTheme::TEXT_PRIMARY));
    combo.setColour(juce::ComboBox::outlineColourId,
                    magda::ActiveTheme::getColour(magda::ActiveTheme::BORDER));
}

void setupComboLabel(juce::Component& owner, juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(magda::FontManager::getInstance().getUIFont(12.0f));
    label.setColour(juce::Label::textColourId,
                    magda::ActiveTheme::getColour(magda::ActiveTheme::TEXT_PRIMARY));
    label.setJustificationType(juce::Justification::centredLeft);
    owner.addAndMakeVisible(label);
}

void layoutTextSliderRow(juce::Rectangle<int>& bounds, juce::Label& label,
                         magda::daw::ui::TextSlider& slider, int rowH, int sliderH) {
    constexpr int sliderW = 96;
    constexpr int gap = 12;
    auto row = bounds.removeFromTop(rowH);
    auto sliderArea = row.removeFromRight(sliderW);
    row.removeFromRight(gap);
    label.setBounds(row);
    slider.setBounds(sliderArea.reduced(0, (rowH - sliderH) / 2));
}

void layoutComboRow(juce::Rectangle<int>& bounds, juce::Label& label, juce::ComboBox& combo,
                    int rowH) {
    const int comboW = juce::jlimit(160, 240, bounds.getWidth() / 2);
    constexpr int gap = 12;
    auto row = bounds.removeFromTop(rowH);
    auto comboArea = row.removeFromRight(comboW);
    row.removeFromRight(gap);
    label.setBounds(row);
    combo.setBounds(comboArea.reduced(0, 4));
}

juce::String trOr(const juce::String& key, const juce::String& fallback) {
    const auto translated = magda::tr(key);
    return translated == key ? fallback : translated;
}

void setupPathRowLabel(juce::Component& owner, juce::Label& label, const juce::String& text,
                       float size = 12.0f) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(magda::FontManager::getInstance().getUIFont(size));
    label.setColour(juce::Label::textColourId,
                    magda::ActiveTheme::getColour(magda::ActiveTheme::TEXT_PRIMARY));
    label.setJustificationType(juce::Justification::centredLeft);
    owner.addAndMakeVisible(label);
}

void setupPathTextEditor(juce::Component& owner, juce::TextEditor& editor) {
    editor.setReadOnly(true);
    editor.setFont(magda::FontManager::getInstance().getUIFont(12.0f));
    editor.setColour(juce::TextEditor::backgroundColourId,
                     magda::ActiveTheme::getColour(magda::ActiveTheme::SURFACE));
    editor.setColour(juce::TextEditor::textColourId,
                     magda::ActiveTheme::getColour(magda::ActiveTheme::TEXT_PRIMARY));
    editor.setColour(juce::TextEditor::outlineColourId,
                     magda::ActiveTheme::getColour(magda::ActiveTheme::BORDER));
    owner.addAndMakeVisible(editor);
}

}  // namespace

// ---------------------------------------------------------------------------
// Tab page components
// ---------------------------------------------------------------------------
namespace magda {

// ---- General tab: Zoom, Timeline, UI behaviour -----------------------------

class GeneralPage : public juce::Component {
  public:
    GeneralPage() {
        setupSectionHeader(*this, zoomHeader, tr("preferences.section.zoom"));
        setupTextSlider(*this, zoomInSensitivitySlider, zoomInLabel,
                        tr("preferences.slider.zoom_in_sensitivity"), 5.0, 100.0, 1.0);
        setupTextSlider(*this, zoomOutSensitivitySlider, zoomOutLabel,
                        tr("preferences.slider.zoom_out_sensitivity"), 5.0, 100.0, 1.0);
        setupTextSlider(*this, zoomShiftSensitivitySlider, zoomShiftLabel,
                        tr("preferences.slider.zoom_shift_sensitivity")
                            .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Shift)),
                        1.0, 50.0, 0.5, 1);

        setupSectionHeader(*this, timelineHeader, tr("preferences.section.timeline"));
        // How much of the timeline is on screen, not how long it is. The default
        // length a new project gets sits with the other new-project defaults on
        // the Defaults tab.
        setupTextSlider(*this, viewDurationSlider, viewDurationLabel,
                        tr("preferences.slider.default_view"), 4.0, 128.0, 1.0,
                        magda::TechnicalTextToken::Bars, 0, true);

        setupSectionHeader(*this, transportHeader, tr("preferences.section.transport"));
        setupToggle(*this, stopUpdatesPlayheadToggle,
                    tr("preferences.toggle.stop_updates_playhead"));
        setupToggle(*this, followPlayheadToggle, tr("preferences.toggle.follow_playhead"));

        setupSectionHeader(*this, autoSaveHeader, tr("preferences.section.autosave"));
        setupToggle(*this, autoSaveToggle, tr("preferences.toggle.enable_autosave"));
        setupTextSlider(*this, autoSaveIntervalSlider, autoSaveIntervalLabel,
                        tr("preferences.slider.interval"), 10.0, 300.0, 10.0,
                        magda::TechnicalTextToken::ShortSeconds, 0, true);

        setupSectionHeader(*this, engineHeader, tr("preferences.section.engine"));
        setupToggle(*this, renderAheadToggle, tr("preferences.toggle.render_ahead"));
        setupTextSlider(*this, renderAheadTimeSlider, renderAheadTimeLabel,
                        tr("preferences.slider.render_ahead_time"), 5.0, 200.0, 5.0,
                        magda::TechnicalTextToken::Milliseconds, 0, true);
        setupToggle(*this, captureToggle, tr("preferences.toggle.capture"));
        setupTextSlider(*this, captureSecondsSlider, captureSecondsLabel,
                        tr("preferences.slider.capture_buffer"), 10.0,
                        static_cast<double>(Config::kMaxCaptureSeconds), 10.0,
                        magda::TechnicalTextToken::Seconds, 0, true);
        // Its length and what that costs: stereo floats at 48 kHz, about 23 MB a minute,
        // in memory up to a minute and on disk past it (#3052).
        captureSecondsSlider.setValueFormatter([](double seconds) {
            const auto whole = static_cast<int>(seconds);
            const auto megabytes = seconds * 48000.0 * 2.0 * 4.0 / 1.0e6;
            const auto clock = (whole >= 3600 ? juce::String(whole / 3600) + ":" +
                                                    juce::String(whole / 60 % 60).paddedLeft('0', 2)
                                              : juce::String(whole / 60)) +
                               ":" + juce::String(whole % 60).paddedLeft('0', 2);
            const auto size = megabytes >= 1000.0
                                  ? juce::String(megabytes / 1000.0, 1) + " GB"
                                  : juce::String(juce::roundToInt(megabytes)) + " MB";
            return clock + " (" + size + (whole > 60 ? " on disk)" : ")");
        });
        captureSecondsSlider.setValueParser([](const juce::String& text) {
            auto seconds = 0.0;
            for (const auto& part : juce::StringArray::fromTokens(
                     text.upToFirstOccurrenceOf(" ", false, false), ":", {}))
                seconds = seconds * 60.0 + part.getDoubleValue();
            return seconds;
        });
        setupTextSlider(*this, captureBarsSlider, captureBarsLabel,
                        tr("preferences.slider.capture_length"), 1.0, 64.0, 1.0,
                        magda::TechnicalTextToken::Bars, 0, true);

        setupSectionHeader(*this, layoutHeader, tr("preferences.section.layout"));
        setupToggle(*this, headersOnRightToggle, tr("preferences.toggle.headers_on_right"));
        setupToggle(*this, autoHideScrollbarsToggle,
                    tr("preferences.toggle.autohide_arrangement_scrollbars"));
        // Range matches TrackHeadersPanel's MIN / MAX_TRACK_HEIGHT.
        setupTextSlider(*this, compactTrackHeightSlider, compactTrackHeightLabel,
                        tr("preferences.slider.compact_track_height"), 32.0, 220.0, 1.0);
        setupTextSlider(*this, mediumTrackHeightSlider, mediumTrackHeightLabel,
                        tr("preferences.slider.medium_track_height"), 32.0, 220.0, 1.0);
        setupTextSlider(*this, largeTrackHeightSlider, largeTrackHeightLabel,
                        tr("preferences.slider.large_track_height"), 32.0, 220.0, 1.0);

        setupSectionHeader(*this, behaviorHeader, tr("preferences.section.behavior"));
        setupToggle(*this, confirmTrackDeleteToggle, tr("preferences.toggle.confirm_track_delete"));
        setupToggle(*this, autoMonitorToggle, tr("preferences.toggle.auto_monitor"));
        setupToggle(*this, duplicateLoopGrowsToggle, tr("preferences.toggle.duplicate_loop_grows"));
        setupToggle(*this, showTooltipsToggle, tr("preferences.toggle.show_tooltips"));
        setupToggle(*this, showInfoLineToggle, tr("preferences.toggle.show_info_line"));
        setupToggle(*this, openPluginWindowOnDropToggle,
                    tr("preferences.toggle.open_plugin_window_on_drop"));
        setupToggle(*this, chordPreviewDefaultToggle,
                    tr("preferences.toggle.chord_preview_default"));
        setupToggle(*this, autoCrossfadeDefaultToggle,
                    tr("preferences.toggle.auto_crossfade_default"));
        setupToggle(*this, assumeProjectTempoToggle, tr("preferences.toggle.assume_project_tempo"));
        setupToggle(*this, clipOverlapPlaysBothToggle,
                    tr("preferences.toggle.clip_overlap_plays_both"));
        setupToggle(*this, postFxPostFaderDefaultToggle,
                    tr("preferences.toggle.post_fx_post_fader_default"));

        setupSectionHeader(*this, languageHeader, tr("preferences.language.header"));
        setupComboLabel(*this, languageLabel, tr("preferences.language.label"));
        styleCombo(languageCombo);
        addAndMakeVisible(languageCombo);

        restartHint.setText(tr("preferences.language.restart_required"),
                            juce::dontSendNotification);
        restartHint.setFont(FontManager::getInstance().getUIFont(11.0f));
        restartHint.setColour(juce::Label::textColourId,
                              ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        restartHint.setJustificationType(juce::Justification::centredLeft);
        restartHint.setVisible(false);
        addAndMakeVisible(restartHint);

        languageCombo.onChange = [this] {
            int idx = languageCombo.getSelectedId() - 1;
            if (idx >= 0 && idx < static_cast<int>(availableLanguages_.size())) {
                restartHint.setVisible(availableLanguages_[idx] != initialLanguage_);
                if (!localizedFontScaleExplicit_) {
                    localizedFontScaleSlider.setValue(
                        Config::defaultLocalizedUIFontScaleForLanguage(availableLanguages_[idx]) *
                            100.0,
                        juce::dontSendNotification);
                }
            }
        };

        // Localized Font Size lives with Language: its default is language-driven
        // (see languageCombo.onChange). UI Scale, UI Font, and Font Size moved to
        // the Appearance tab.
        setupTextSlider(*this, localizedFontScaleSlider, localizedFontScaleLabel,
                        tr("preferences.localized_font_scale.label"), 100.0, 300.0, 5.0,
                        magda::TechnicalTextToken::Percent);
        localizedFontScaleSlider.onValueChanged = [this](double) {
            localizedFontScaleExplicit_ = true;
        };
    }

    static int getPreferredHeight(int width) {
        const int height =
            shouldUseSingleColumnLayout(width)
                ? getSingleColumnPreferredHeight()
                : juce::jmax(getLeftColumnPreferredHeight(), getRightColumnPreferredHeight());

        return juce::jmax(height, width < 520 ? 760 : 0);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16);
        const int rowH = 32;
        const int sliderH = 24;
        const int headerH = 28;
        const int secGap = 12;

        if (!shouldUseSingleColumnLayout(getWidth())) {
            layoutTwoColumns(bounds, rowH, sliderH, headerH, secGap);
            return;
        }

        layoutSingleColumn(bounds, rowH, sliderH, headerH, secGap);
    }

    void loadSettings(Config& config) {
        zoomInSensitivitySlider.setValue(config.getZoomInSensitivity(), juce::dontSendNotification);
        zoomOutSensitivitySlider.setValue(config.getZoomOutSensitivity(),
                                          juce::dontSendNotification);
        zoomShiftSensitivitySlider.setValue(config.getZoomInSensitivityShift(),
                                            juce::dontSendNotification);
        viewDurationSlider.setValue(config.getDefaultZoomViewBars(), juce::dontSendNotification);
        followPlayheadToggle.setToggleState(config.getFollowPlayhead(), juce::dontSendNotification);
        stopUpdatesPlayheadToggle.setToggleState(config.getStopUpdatesPlayhead(),
                                                 juce::dontSendNotification);
        autoSaveToggle.setToggleState(config.getAutoSaveEnabled(), juce::dontSendNotification);
        autoSaveIntervalSlider.setValue(config.getAutoSaveIntervalSeconds(),
                                        juce::dontSendNotification);
        renderAheadToggle.setToggleState(config.getRenderAheadEnabled(),
                                         juce::dontSendNotification);
        renderAheadTimeSlider.setValue(config.getRenderAheadMs(), juce::dontSendNotification);
        captureToggle.setToggleState(config.getCaptureEnabled(), juce::dontSendNotification);
        captureSecondsSlider.setValue(config.getCaptureSeconds(), juce::dontSendNotification);
        captureBarsSlider.setValue(config.getCaptureBars(), juce::dontSendNotification);
        headersOnRightToggle.setToggleState(config.getScrollbarOnLeft(),
                                            juce::dontSendNotification);
        compactTrackHeightSlider.setValue(config.getCompactTrackHeight(),
                                          juce::dontSendNotification);
        mediumTrackHeightSlider.setValue(config.getMediumTrackHeight(), juce::dontSendNotification);
        largeTrackHeightSlider.setValue(config.getLargeTrackHeight(), juce::dontSendNotification);
        autoHideScrollbarsToggle.setToggleState(config.getMainViewScrollbarsAutoHide(),
                                                juce::dontSendNotification);
        confirmTrackDeleteToggle.setToggleState(config.getConfirmTrackDelete(),
                                                juce::dontSendNotification);
        autoMonitorToggle.setToggleState(config.getAutoMonitorSelectedTrack(),
                                         juce::dontSendNotification);
        duplicateLoopGrowsToggle.setToggleState(config.getDuplicateLoopGrows(),
                                                juce::dontSendNotification);
        showTooltipsToggle.setToggleState(config.getShowTooltips(), juce::dontSendNotification);
        showInfoLineToggle.setToggleState(config.getShowInfoLine(), juce::dontSendNotification);
        openPluginWindowOnDropToggle.setToggleState(config.getOpenPluginWindowOnDrop(),
                                                    juce::dontSendNotification);
        chordPreviewDefaultToggle.setToggleState(config.getChordPreviewOnByDefault(),
                                                 juce::dontSendNotification);
        clipOverlapPlaysBothToggle.setToggleState(config.getClipOverlapPlaysBoth(),
                                                  juce::dontSendNotification);
        assumeProjectTempoToggle.setToggleState(config.getAssumeProjectTempo(),
                                                juce::dontSendNotification);
        autoCrossfadeDefaultToggle.setToggleState(config.getAutoCrossfadeByDefault(),
                                                  juce::dontSendNotification);
        postFxPostFaderDefaultToggle.setToggleState(config.getPostFxPostFaderByDefault(),
                                                    juce::dontSendNotification);

        languageCombo.clear(juce::dontSendNotification);
        availableLanguages_.clear();

        auto langDir = magda::StringTable::findLangDirectory();
        if (langDir.isDirectory()) {
            for (const auto& f : langDir.findChildFiles(juce::File::findFiles, false, "*.json"))
                availableLanguages_.push_back(f.getFileNameWithoutExtension());
        }

        if (availableLanguages_.empty())
            availableLanguages_.emplace_back("en");

        auto currentLang = juce::String(config.getLanguage());
        initialLanguage_ = currentLang;
        int selectedId = 1;
        for (int i = 0; i < static_cast<int>(availableLanguages_.size()); ++i) {
            languageCombo.addItem(availableLanguages_[i], i + 1);
            if (availableLanguages_[i] == currentLang)
                selectedId = i + 1;
        }
        languageCombo.setSelectedId(selectedId, juce::dontSendNotification);
        restartHint.setVisible(false);

        localizedFontScaleExplicit_ = config.hasExplicitLocalizedUIFontScale();
        localizedFontScaleSlider.setValue(config.getLocalizedUIFontScale() * 100.0,
                                          juce::dontSendNotification);
    }

    void applySettings(Config& config) {
        config.setZoomInSensitivity(zoomInSensitivitySlider.getValue());
        config.setZoomOutSensitivity(zoomOutSensitivitySlider.getValue());
        config.setZoomInSensitivityShift(zoomShiftSensitivitySlider.getValue());
        config.setZoomOutSensitivityShift(zoomShiftSensitivitySlider.getValue());
        config.setDefaultZoomViewBars(static_cast<int>(viewDurationSlider.getValue()));
        config.setStopUpdatesPlayhead(stopUpdatesPlayheadToggle.getToggleState());
        config.setFollowPlayhead(followPlayheadToggle.getToggleState());
        config.setAutoSaveEnabled(autoSaveToggle.getToggleState());
        config.setAutoSaveIntervalSeconds(static_cast<int>(autoSaveIntervalSlider.getValue()));
        config.setRenderAheadEnabled(renderAheadToggle.getToggleState());
        config.setRenderAheadMs(static_cast<int>(renderAheadTimeSlider.getValue()));
        config.setCaptureEnabled(captureToggle.getToggleState());
        config.setCaptureSeconds(static_cast<int>(captureSecondsSlider.getValue()));
        config.setCaptureBars(static_cast<int>(captureBarsSlider.getValue()));
        config.setScrollbarOnLeft(headersOnRightToggle.getToggleState());
        config.setMainViewScrollbarsAutoHide(autoHideScrollbarsToggle.getToggleState());
        config.setCompactTrackHeight(static_cast<int>(compactTrackHeightSlider.getValue()));
        config.setMediumTrackHeight(static_cast<int>(mediumTrackHeightSlider.getValue()));
        config.setLargeTrackHeight(static_cast<int>(largeTrackHeightSlider.getValue()));
        config.setConfirmTrackDelete(confirmTrackDeleteToggle.getToggleState());
        config.setAutoMonitorSelectedTrack(autoMonitorToggle.getToggleState());
        config.setDuplicateLoopGrows(duplicateLoopGrowsToggle.getToggleState());
        config.setShowTooltips(showTooltipsToggle.getToggleState());
        config.setShowInfoLine(showInfoLineToggle.getToggleState());
        config.setOpenPluginWindowOnDrop(openPluginWindowOnDropToggle.getToggleState());
        config.setChordPreviewOnByDefault(chordPreviewDefaultToggle.getToggleState());
        config.setAutoCrossfadeByDefault(autoCrossfadeDefaultToggle.getToggleState());
        config.setAssumeProjectTempo(assumeProjectTempoToggle.getToggleState());

        // Playing through an overlap is a per-clip switch (#2003), so this is
        // the value new clips start with — the same shape as AUTO-XFADE's
        // default above. Existing clips keep whatever they were set to.
        config.setClipOverlapPlaysBoth(clipOverlapPlaysBothToggle.getToggleState());

        // Same shape again: the side of the fader a new track's post-FX stage
        // starts on (#2094). Existing tracks keep the side they were set to.
        config.setPostFxPostFaderByDefault(postFxPostFaderDefaultToggle.getToggleState());

        int selIdx = languageCombo.getSelectedId() - 1;
        if (selIdx >= 0 && selIdx < static_cast<int>(availableLanguages_.size())) {
            auto newLang = availableLanguages_[selIdx];
            config.setLanguage(newLang.toStdString());
        }

        config.setLocalizedUIFontScale(localizedFontScaleSlider.getValue() / 100.0);
    }

  private:
    static constexpr int kTwoColumnMinWidth =
        1100;  // one column in a default-sized Settings window (#3162)

    static bool shouldUseSingleColumnLayout(int width) {
        return width < kTwoColumnMinWidth;
    }

    static int getSingleColumnPreferredHeight() {
        constexpr int padding = 16;
        constexpr int rowH = 32;
        constexpr int headerH = 28;
        constexpr int secGap = 12;

        // The Behaviour block is 11 toggles with a 4px gap between them.
        return padding + headerH + 4 + (rowH * 3) + 8 + secGap + headerH + 4 + (rowH * 2) + 4 +
               secGap + headerH + 4 + rowH + secGap + headerH + 4 + rowH + 4 + rowH + secGap +
               headerH + 4 + rowH + 4 + rowH + secGap + headerH + 4 + rowH + secGap + headerH + 4 +
               (rowH * 11) + 40 + secGap + headerH + 4 + rowH + 18 + 4 + rowH + padding +
               3 * (4 + rowH)     // Layout's track-height rows
               + 3 * (4 + rowH);  // Engine's capture rows
    }

    static int getLeftColumnPreferredHeight() {
        constexpr int padding = 16;
        constexpr int rowH = 32;
        constexpr int headerH = 28;
        constexpr int secGap = 12;

        return padding + headerH + 4 + (rowH * 3) + 8         // Zoom
               + secGap + headerH + 4 + rowH                  // Timeline
               + secGap + headerH + 4 + rowH + 4 + rowH       // Transport
               + secGap + headerH + 4 + rowH + 4 + rowH       // Auto-Save
               + secGap + headerH + 4 + rowH + 4 + rowH       // Engine
               + 3 * (4 + rowH)                               // Engine: capture
               + secGap + headerH + 4 + rowH + 18 + 4 + rowH  // Language + Localized Font Size
               + padding;
    }

    static int getRightColumnPreferredHeight() {
        constexpr int padding = 16;
        constexpr int rowH = 32;
        constexpr int headerH = 28;
        constexpr int secGap = 12;

        return padding + headerH + 4 + rowH + 4 + rowH    // Layout
               + 3 * (4 + rowH)                           // Layout: track-height presets
               + secGap + headerH + 4 + (rowH * 11) + 40  // Behaviour: 11 toggles, 4px apart
               + padding;
    }

    void layoutSingleColumn(juce::Rectangle<int> bounds, int rowH, int sliderH, int headerH,
                            int secGap) {
        // Zoom
        zoomHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, zoomInLabel, zoomInSensitivitySlider, rowH, sliderH);
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, zoomOutLabel, zoomOutSensitivitySlider, rowH, sliderH);
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, zoomShiftLabel, zoomShiftSensitivitySlider, rowH, sliderH);
        bounds.removeFromTop(secGap);

        // Timeline
        timelineHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, viewDurationLabel, viewDurationSlider, rowH, sliderH);
        bounds.removeFromTop(secGap);

        // Transport
        transportHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        stopUpdatesPlayheadToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        followPlayheadToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(secGap);

        // Auto-Save
        autoSaveHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        autoSaveToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, autoSaveIntervalLabel, autoSaveIntervalSlider, rowH, sliderH);
        bounds.removeFromTop(secGap);

        // Engine
        engineHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        renderAheadToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, renderAheadTimeLabel, renderAheadTimeSlider, rowH, sliderH);
        bounds.removeFromTop(4);
        captureToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, captureSecondsLabel, captureSecondsSlider, rowH, sliderH);
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, captureBarsLabel, captureBarsSlider, rowH, sliderH);
        bounds.removeFromTop(secGap);

        // Layout
        layoutHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        headersOnRightToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        autoHideScrollbarsToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, compactTrackHeightLabel, compactTrackHeightSlider, rowH,
                            sliderH);
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, mediumTrackHeightLabel, mediumTrackHeightSlider, rowH, sliderH);
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, largeTrackHeightLabel, largeTrackHeightSlider, rowH, sliderH);
        bounds.removeFromTop(secGap);

        // Behaviour
        behaviorHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        confirmTrackDeleteToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        autoMonitorToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        duplicateLoopGrowsToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        showTooltipsToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        showInfoLineToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        openPluginWindowOnDropToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        chordPreviewDefaultToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        autoCrossfadeDefaultToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        assumeProjectTempoToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        clipOverlapPlaysBothToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        postFxPostFaderDefaultToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(secGap);

        // Language
        languageHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        layoutComboRow(bounds, languageLabel, languageCombo, rowH);
        restartHint.setBounds(bounds.removeFromTop(18));
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, localizedFontScaleLabel, localizedFontScaleSlider, rowH,
                            sliderH);
    }

    void layoutTwoColumns(juce::Rectangle<int> bounds, int rowH, int sliderH, int headerH,
                          int secGap) {
        constexpr int colGap = 28;

        const int colW = (bounds.getWidth() - colGap) / 2;
        auto left = bounds.removeFromLeft(colW);
        bounds.removeFromLeft(colGap);
        auto right = bounds;

        // Zoom
        zoomHeader.setBounds(left.removeFromTop(headerH));
        left.removeFromTop(4);
        layoutTextSliderRow(left, zoomInLabel, zoomInSensitivitySlider, rowH, sliderH);
        left.removeFromTop(4);
        layoutTextSliderRow(left, zoomOutLabel, zoomOutSensitivitySlider, rowH, sliderH);
        left.removeFromTop(4);
        layoutTextSliderRow(left, zoomShiftLabel, zoomShiftSensitivitySlider, rowH, sliderH);
        left.removeFromTop(secGap);

        // Timeline
        timelineHeader.setBounds(left.removeFromTop(headerH));
        left.removeFromTop(4);
        layoutTextSliderRow(left, viewDurationLabel, viewDurationSlider, rowH, sliderH);
        left.removeFromTop(secGap);

        // Transport
        transportHeader.setBounds(left.removeFromTop(headerH));
        left.removeFromTop(4);
        stopUpdatesPlayheadToggle.setBounds(left.removeFromTop(rowH).reduced(0, 4));
        left.removeFromTop(4);
        followPlayheadToggle.setBounds(left.removeFromTop(rowH).reduced(0, 4));
        left.removeFromTop(secGap);

        // Auto-Save
        autoSaveHeader.setBounds(left.removeFromTop(headerH));
        left.removeFromTop(4);
        autoSaveToggle.setBounds(left.removeFromTop(rowH).reduced(0, 4));
        left.removeFromTop(4);
        layoutTextSliderRow(left, autoSaveIntervalLabel, autoSaveIntervalSlider, rowH, sliderH);
        left.removeFromTop(secGap);

        // Engine
        engineHeader.setBounds(left.removeFromTop(headerH));
        left.removeFromTop(4);
        renderAheadToggle.setBounds(left.removeFromTop(rowH).reduced(0, 4));
        left.removeFromTop(4);
        layoutTextSliderRow(left, renderAheadTimeLabel, renderAheadTimeSlider, rowH, sliderH);
        left.removeFromTop(4);
        captureToggle.setBounds(left.removeFromTop(rowH).reduced(0, 4));
        left.removeFromTop(4);
        layoutTextSliderRow(left, captureSecondsLabel, captureSecondsSlider, rowH, sliderH);
        left.removeFromTop(4);
        layoutTextSliderRow(left, captureBarsLabel, captureBarsSlider, rowH, sliderH);
        left.removeFromTop(secGap);

        // Language
        languageHeader.setBounds(left.removeFromTop(headerH));
        left.removeFromTop(4);
        layoutComboRow(left, languageLabel, languageCombo, rowH);
        restartHint.setBounds(left.removeFromTop(18));
        left.removeFromTop(4);
        layoutTextSliderRow(left, localizedFontScaleLabel, localizedFontScaleSlider, rowH, sliderH);

        // Layout
        layoutHeader.setBounds(right.removeFromTop(headerH));
        right.removeFromTop(4);
        headersOnRightToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        autoHideScrollbarsToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        layoutTextSliderRow(right, compactTrackHeightLabel, compactTrackHeightSlider, rowH,
                            sliderH);
        right.removeFromTop(4);
        layoutTextSliderRow(right, mediumTrackHeightLabel, mediumTrackHeightSlider, rowH, sliderH);
        right.removeFromTop(4);
        layoutTextSliderRow(right, largeTrackHeightLabel, largeTrackHeightSlider, rowH, sliderH);
        right.removeFromTop(secGap);

        // Behaviour
        behaviorHeader.setBounds(right.removeFromTop(headerH));
        right.removeFromTop(4);
        confirmTrackDeleteToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        autoMonitorToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        duplicateLoopGrowsToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        showTooltipsToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        showInfoLineToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        openPluginWindowOnDropToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        chordPreviewDefaultToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        autoCrossfadeDefaultToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        assumeProjectTempoToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        clipOverlapPlaysBothToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        postFxPostFaderDefaultToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
    }

    juce::Label zoomHeader, timelineHeader, transportHeader, autoSaveHeader;
    magda::daw::ui::TextSlider zoomInSensitivitySlider, zoomOutSensitivitySlider,
        zoomShiftSensitivitySlider;
    juce::Label zoomInLabel, zoomOutLabel, zoomShiftLabel;
    magda::daw::ui::TextSlider viewDurationSlider;
    juce::Label viewDurationLabel;
    juce::ToggleButton stopUpdatesPlayheadToggle;
    juce::ToggleButton followPlayheadToggle;
    juce::ToggleButton autoSaveToggle;
    magda::daw::ui::TextSlider autoSaveIntervalSlider;
    juce::Label autoSaveIntervalLabel;
    juce::Label layoutHeader, behaviorHeader, languageHeader;
    juce::Label engineHeader, renderAheadTimeLabel, captureSecondsLabel, captureBarsLabel;
    juce::ToggleButton renderAheadToggle;
    juce::ToggleButton captureToggle;
    magda::daw::ui::TextSlider renderAheadTimeSlider, captureSecondsSlider, captureBarsSlider;
    juce::ToggleButton headersOnRightToggle;
    juce::ToggleButton autoHideScrollbarsToggle;
    juce::Label compactTrackHeightLabel, mediumTrackHeightLabel, largeTrackHeightLabel;
    magda::daw::ui::TextSlider compactTrackHeightSlider, mediumTrackHeightSlider,
        largeTrackHeightSlider;
    juce::ToggleButton confirmTrackDeleteToggle, autoMonitorToggle;
    juce::ToggleButton duplicateLoopGrowsToggle;
    juce::ToggleButton showTooltipsToggle;
    juce::ToggleButton showInfoLineToggle;
    juce::ToggleButton openPluginWindowOnDropToggle;
    juce::ToggleButton chordPreviewDefaultToggle;
    juce::ToggleButton autoCrossfadeDefaultToggle;
    juce::ToggleButton assumeProjectTempoToggle;
    juce::ToggleButton clipOverlapPlaysBothToggle;
    juce::ToggleButton postFxPostFaderDefaultToggle;
    juce::Label languageLabel;
    juce::ComboBox languageCombo;
    juce::Label restartHint;
    std::vector<juce::String> availableLanguages_;
    juce::String initialLanguage_;
    juce::Label localizedFontScaleLabel;
    magda::daw::ui::TextSlider localizedFontScaleSlider;
    bool localizedFontScaleExplicit_ = false;
};

// ---- Appearance tab: Application theme and track colours ------------------

class AppearancePage : public juce::Component {
  public:
    static constexpr int MAX_PALETTE_SIZE = 16;

    AppearancePage() {
        setupSectionHeader(*this, themeHeader, trOr("preferences.section.theme", "Theme"));
        setupComboLabel(*this, themeLabel, trOr("preferences.theme.label", "Application theme"));
        styleCombo(themeCombo);
        rebuildThemeCombo();
        addAndMakeVisible(themeCombo);

        // Get Template: export the current base palette as a complete, valid
        // JSON file the user can edit. Load: pick an edited JSON and adopt it.
        loadThemeButton.setButtonText(trOr("preferences.button.load_theme", "Load Theme"));
        loadThemeButton.onClick = [this]() { loadThemeFromFile(); };
        addAndMakeVisible(loadThemeButton);

        getTemplateButton.setButtonText(
            trOr("preferences.button.get_template", "Download Theme Template"));
        getTemplateButton.onClick = [this]() { saveThemeTemplate(); };
        addAndMakeVisible(getTemplateButton);

        openThemesFolderButton.setButtonText(
            trOr("preferences.button.open_themes_folder", "Open Themes Folder"));
        openThemesFolderButton.onClick = [this]() {
            auto dir = paths::themesDir();
            dir.createDirectory();
            dir.revealToUser();
        };
        addAndMakeVisible(openThemesFolderButton);

        // Scale and text: UI scale (DPI), font family, font size and spacing.
        setupSectionHeader(*this, scaleHeader, tr("preferences.section.scale"));
        setupComboLabel(*this, scaleLabel, tr("preferences.scale.label"));
        styleCombo(scaleCombo);
        scaleCombo.addItem(tr("preferences.scale.auto"), 1);
        scaleCombo.addItem("100%", 2);
        scaleCombo.addItem("125%", 3);
        scaleCombo.addItem("150%", 4);
        scaleCombo.addItem("175%", 5);
        scaleCombo.addItem("200%", 6);
        addAndMakeVisible(scaleCombo);

        setupComboLabel(*this, fontFamilyLabel, trOr("preferences.font_family.label", "UI Font"));
        styleCombo(fontFamilyCombo);
        fontFamilyCombo.addItem(trOr("preferences.font_family.default", "Inter (default)"), 1);
        fontFamilies_ = juce::Font::findAllTypefaceNames();
        fontFamilies_.sort(true);
        for (int i = 0; i < fontFamilies_.size(); ++i)
            fontFamilyCombo.addItem(fontFamilies_[i], kFontFamilyIdBase + i);
        addAndMakeVisible(fontFamilyCombo);

        setupTextSlider(*this, fontScaleSlider, fontScaleLabel, tr("preferences.font_scale.label"),
                        80.0, 150.0, 5.0, magda::TechnicalTextToken::Percent);
        setupTextSlider(*this, densitySlider, densityLabel,
                        trOr("preferences.density.label", "Spacing"), 60.0, 140.0, 5.0,
                        magda::TechnicalTextToken::Percent);

        // How controls across the app draw.
        setupSectionHeader(*this, controlsHeader, trOr("preferences.section.controls", "Controls"));
        setupComboLabel(*this, trackColourStyleLabel,
                        trOr("preferences.track_colour_style.label", "Track colour"));
        trackColourStyle.setOptions({trOr("preferences.track_colour_style.spine", "Spine"),
                                     trOr("preferences.track_colour_style.full", "Full bar")});
        addAndMakeVisible(trackColourStyle);

        setupComboLabel(*this, muteSoloStyleLabel,
                        trOr("preferences.mute_solo_style.label", "Mute / solo"));
        muteSoloStyle.setOptions({trOr("preferences.mute_solo_style.icons", "Icons"),
                                  trOr("preferences.mute_solo_style.letters", "Letters (M / S)")});
        addAndMakeVisible(muteSoloStyle);

        // Track palette: a swatch grid; the selected swatch is edited below it.
        setupSectionHeader(*this, coloursHeader, tr("preferences.section.track_colour_palette"));

        setupPaletteEditor(hexEditor_);
        hexEditor_.setInputRestrictions(6, "0123456789ABCDEFabcdef");
        hexEditor_.onTextChange = [this] {
            if (auto* entry = selectedEntry()) {
                entry->colour =
                    static_cast<uint32_t>(hexEditor_.getText().getHexValue64() | 0xFF000000ULL);
                repaint();
            }
        };
        setupPaletteEditor(nameEditor_);
        nameEditor_.onTextChange = [this] {
            if (auto* entry = selectedEntry())
                entry->name = nameEditor_.getText().toStdString();
        };

        removeColourButton_.setButtonText(trOr("preferences.button.remove", "Remove"));
        magda::daw::ui::SettingsLookAndFeel::markLinkButton(removeColourButton_);
        removeColourButton_.onClick = [this] { removeSelectedColour(); };
        addAndMakeVisible(removeColourButton_);

        applyPaletteToProjectToggle.setButtonText(
            tr("preferences.toggle.apply_palette_to_project"));
        applyPaletteToProjectToggle.setColour(juce::ToggleButton::textColourId,
                                              ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        addAndMakeVisible(applyPaletteToProjectToggle);

        setupSectionHeader(*this, clipColourHeader, tr("preferences.section.clip_colours"));
        setupComboLabel(*this, clipColourModeLabel, tr("preferences.label.new_clip_colour"));
        styleCombo(clipColourModeCombo);
        clipColourModeCombo.addItem(tr("preferences.option.inherit_from_track"), 1);
        clipColourModeCombo.addItem(tr("preferences.option.cycle_palette"), 2);
        addAndMakeVisible(clipColourModeCombo);
    }

    int getPreferredHeight(int width) const {
        const int inner = width - 2 * kPadding;
        if (singleColumn(width))
            return kPadding + leftColumnHeight() + kSectionGap + rightColumnHeight(inner) +
                   kPadding;
        return kPadding + juce::jmax(leftColumnHeight(), rightColumnHeight(columnWidth(inner))) +
               kPadding;
    }

    void lookAndFeelChanged() override {
        const auto primary = ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY);
        for (auto* label :
             {&themeLabel, &scaleLabel, &fontFamilyLabel, &fontScaleLabel, &densityLabel,
              &trackColourStyleLabel, &muteSoloStyleLabel, &clipColourModeLabel})
            label->setColour(juce::Label::textColourId, primary);
        for (auto* label :
             {&themeHeader, &scaleHeader, &controlsHeader, &coloursHeader, &clipColourHeader})
            magda::daw::ui::SettingsLookAndFeel::markSectionHeader(*label);
        for (auto* combo : {&themeCombo, &scaleCombo, &fontFamilyCombo, &clipColourModeCombo})
            styleCombo(*combo);
        for (auto* editor : {&hexEditor_, &nameEditor_})
            setupPaletteEditor(*editor);
        applyPaletteToProjectToggle.setColour(juce::ToggleButton::textColourId, primary);
        repaint();
    }

    void resized() override {
        rowLines_.clear();
        auto bounds = getLocalBounds().reduced(kPadding);
        if (singleColumn(getWidth())) {
            layoutLeftColumn(bounds);
            bounds.removeFromTop(kSectionGap);
            layoutRightColumn(bounds);
            return;
        }
        const int colW = columnWidth(bounds.getWidth());
        auto left = bounds.removeFromLeft(colW);
        bounds.removeFromLeft(kColumnGap);
        layoutLeftColumn(left);
        layoutRightColumn(bounds);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(ActiveTheme::getBorderColour().withMultipliedAlpha(0.6f));
        for (const auto& line : rowLines_)
            g.fillRect(line);

        for (size_t i = 0; i < swatchBounds_.size(); ++i) {
            const auto area = swatchBounds_[i].toFloat();
            if (i < palette_.size()) {
                g.setColour(juce::Colour(palette_[i].colour));
                g.fillRoundedRectangle(area.reduced(2.0f), 4.0f);
                if (static_cast<int>(i) == selected_) {
                    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
                    g.drawRoundedRectangle(area.reduced(0.75f), 5.0f, 1.5f);
                }
            } else {
                // The add tile.
                juce::Path outline;
                outline.addRoundedRectangle(area.reduced(2.5f), 4.0f);
                juce::Path dashed;
                const float dashes[] = {3.0f, 3.0f};
                juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
                g.setColour(ActiveTheme::getBorderColour());
                g.fillPath(dashed);
                g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
                g.setFont(FontManager::getInstance().getUIFont(16.0f));
                g.drawText("+", area, juce::Justification::centred);
            }
        }

        if (selectedEntry() != nullptr && !selectedSwatchBounds_.isEmpty()) {
            g.setColour(juce::Colour(selectedEntry()->colour));
            g.fillRoundedRectangle(selectedSwatchBounds_.toFloat(), 4.0f);
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        for (size_t i = 0; i < swatchBounds_.size(); ++i) {
            if (!swatchBounds_[i].contains(e.getPosition()))
                continue;
            if (i < palette_.size())
                selectColour(static_cast<int>(i));
            else
                addColour();
            return;
        }
    }

    void loadSettings(Config& config) {
        rebuildThemeCombo();  // pick up any theme files added since construction
        themeCombo.setSelectedId(themeIdForValue(config.getTheme()), juce::dontSendNotification);

        scaleCombo.setSelectedId(scaleIdForValue(config.getUIScale()), juce::dontSendNotification);
        fontFamilyCombo.setSelectedId(fontFamilyIdForValue(config.getUIFontFamily()),
                                      juce::dontSendNotification);
        fontScaleSlider.setValue(config.getUIFontScale() * 100.0, juce::dontSendNotification);
        densitySlider.setValue(config.getUIDensityScale() * 100.0, juce::dontSendNotification);
        trackColourStyle.setSelectedIndex(config.getTrackColourStyle() == "full" ? 1 : 0);
        muteSoloStyle.setSelectedIndex(config.getMuteSoloStyle() == "letters" ? 1 : 0);

        palette_ = config.getTrackColourPalette();
        selectColour(palette_.empty() ? -1 : 0);
        applyPaletteToProjectToggle.setToggleState(false, juce::dontSendNotification);
        applyPaletteToProjectToggle.setEnabled(ProjectManager::getInstance().hasOpenProject());

        clipColourModeCombo.setSelectedId(config.getClipColourMode() + 1,
                                          juce::dontSendNotification);
    }

    void applySettings(Config& config) {
        config.setTheme(themeValueForId(themeCombo.getSelectedId()));
        config.setUIDensityScale(densitySlider.getValue() / 100.0);
        config.setTrackColourStyle(trackColourStyle.getSelectedIndex() == 1 ? "full" : "spine");
        config.setMuteSoloStyle(muteSoloStyle.getSelectedIndex() == 1 ? "letters" : "icons");

        config.setUIScale(juce::jmax(0.0, scaleValueForId(scaleCombo.getSelectedId())));
        config.setUIFontFamily(fontFamilyValueForId(fontFamilyCombo.getSelectedId()));
        config.setUIFontScale(fontScaleSlider.getValue() / 100.0);

        auto palette = palette_;
        for (size_t i = 0; i < palette.size(); ++i)
            if (palette[i].name.empty())
                palette[i].name = tr("preferences.colours.default_name_prefix").toStdString() +
                                  " " + std::to_string(i + 1);
        config.setTrackColourPalette(palette);
        config.setClipColourMode(clipColourModeCombo.getSelectedId() - 1);
    }

    bool shouldApplyPaletteToCurrentProject() const {
        return applyPaletteToProjectToggle.getToggleState();
    }

    void clearApplyPaletteToCurrentProject() {
        applyPaletteToProjectToggle.setToggleState(false, juce::dontSendNotification);
    }

  private:
    static constexpr int kPadding = 16;
    static constexpr int kHeaderH = 30;
    static constexpr int kRowH = 36;
    static constexpr int kButtonRowH = 40;
    static constexpr int kSliderH = 24;
    static constexpr int kSectionGap = 22;
    static constexpr int kColumnGap = 32;
    static constexpr int kSwatchColumns = 8;
    static constexpr int kSwatchGap = 6;
    static constexpr int kTwoColumnMinWidth = 700;

    static bool singleColumn(int width) {
        return width < kTwoColumnMinWidth;
    }
    static int columnWidth(int innerWidth) {
        return (innerWidth - kColumnGap) / 2;
    }
    static int swatchSize(int columnW) {
        return (columnW - (kSwatchColumns - 1) * kSwatchGap) / kSwatchColumns;
    }

    static int leftColumnHeight() {
        return kHeaderH + kRowH + kButtonRowH         // theme
               + kSectionGap + kHeaderH + 4 * kRowH;  // scale and text
    }

    int rightColumnHeight(int columnW) const {
        const int tiles = static_cast<int>(palette_.size()) +
                          (static_cast<int>(palette_.size()) < MAX_PALETTE_SIZE ? 1 : 0);
        const int swatchRows = (tiles + kSwatchColumns - 1) / kSwatchColumns;
        const int grid = swatchRows * (swatchSize(columnW) + kSwatchGap);
        return kHeaderH + 4 * kRowH                                 // controls
               + kSectionGap + kHeaderH + 8 + grid + kRowH + kRowH  // palette, editor, toggle
               + kSectionGap + kHeaderH + kRowH;                    // clip colours
    }

    juce::Rectangle<int> takeRow(juce::Rectangle<int>& b, int height) {
        auto row = b.removeFromTop(height);
        rowLines_.push_back(row.withTop(row.getBottom() - 1));
        return row;
    }

    void layoutLeftColumn(juce::Rectangle<int>& b) {
        themeHeader.setBounds(b.removeFromTop(kHeaderH));
        layoutCombo(takeRow(b, kRowH), themeLabel, themeCombo);
        {
            auto row = takeRow(b, kButtonRowH).reduced(0, 4);
            for (auto* button : {&loadThemeButton, &getTemplateButton, &openThemesFolderButton}) {
                const int w =
                    juce::GlyphArrangement::getStringWidthInt(
                        FontManager::getInstance().getUIFont(13.5f), button->getButtonText()) +
                    32;
                button->setBounds(row.removeFromLeft(w));
                row.removeFromLeft(8);
            }
        }

        b.removeFromTop(kSectionGap);
        scaleHeader.setBounds(b.removeFromTop(kHeaderH));
        layoutCombo(takeRow(b, kRowH), scaleLabel, scaleCombo);
        layoutCombo(takeRow(b, kRowH), fontFamilyLabel, fontFamilyCombo);
        layoutSlider(takeRow(b, kRowH), fontScaleLabel, fontScaleSlider);
        layoutSlider(takeRow(b, kRowH), densityLabel, densitySlider);
    }

    void layoutRightColumn(juce::Rectangle<int>& b) {
        controlsHeader.setBounds(b.removeFromTop(kHeaderH));
        layoutSegments(takeRow(b, kRowH), trackColourStyleLabel, trackColourStyle);
        layoutSegments(takeRow(b, kRowH), muteSoloStyleLabel, muteSoloStyle);

        b.removeFromTop(kSectionGap);
        coloursHeader.setBounds(b.removeFromTop(kHeaderH));
        b.removeFromTop(8);
        layoutSwatches(b);
        {
            auto row = takeRow(b, kRowH).reduced(0, 4);
            const bool editing = selectedEntry() != nullptr;
            hexEditor_.setVisible(editing);
            nameEditor_.setVisible(editing);
            removeColourButton_.setVisible(editing);
            selectedSwatchBounds_ =
                editing ? row.removeFromLeft(row.getHeight()) : juce::Rectangle<int>();
            row.removeFromLeft(8);
            hexEditor_.setBounds(row.removeFromLeft(80));
            row.removeFromLeft(8);
            removeColourButton_.setBounds(row.removeFromRight(64));
            row.removeFromRight(8);
            nameEditor_.setBounds(row);
        }
        applyPaletteToProjectToggle.setBounds(takeRow(b, kRowH).reduced(0, 6));

        b.removeFromTop(kSectionGap);
        clipColourHeader.setBounds(b.removeFromTop(kHeaderH));
        layoutCombo(takeRow(b, kRowH), clipColourModeLabel, clipColourModeCombo);
    }

    void layoutSwatches(juce::Rectangle<int>& b) {
        swatchBounds_.clear();
        const int size = swatchSize(b.getWidth());
        const size_t tiles = palette_.size() + (palette_.size() < MAX_PALETTE_SIZE ? 1 : 0);
        for (size_t i = 0; i < tiles; ++i) {
            const int col = static_cast<int>(i) % kSwatchColumns;
            const int rowIndex = static_cast<int>(i) / kSwatchColumns;
            swatchBounds_.emplace_back(b.getX() + col * (size + kSwatchGap),
                                       b.getY() + rowIndex * (size + kSwatchGap), size, size);
        }
        const int rows = static_cast<int>((tiles + kSwatchColumns - 1) / kSwatchColumns);
        b.removeFromTop(rows * (size + kSwatchGap));
    }

    static void layoutCombo(juce::Rectangle<int> row, juce::Label& label, juce::ComboBox& combo) {
        const int comboW = juce::jlimit(140, 220, row.getWidth() / 2);
        combo.setBounds(row.removeFromRight(comboW).reduced(0, 5));
        label.setBounds(row);
    }

    static void layoutSlider(juce::Rectangle<int> row, juce::Label& label,
                             magda::daw::ui::TextSlider& slider) {
        slider.setBounds(row.removeFromRight(88).withSizeKeepingCentre(88, kSliderH));
        label.setBounds(row);
    }

    static void layoutSegments(juce::Rectangle<int> row, juce::Label& label,
                               magda::daw::ui::PillSegments& segments) {
        const int w = juce::jmin(segments.getIdealWidth(), row.getWidth() / 2 + 40);
        segments.setBounds(row.removeFromRight(w).reduced(0, 6));
        label.setBounds(row);
    }

    void setupPaletteEditor(juce::TextEditor& editor) {
        editor.setFont(FontManager::getInstance().getUIFont(13.0f));
        editor.setColour(juce::TextEditor::backgroundColourId,
                         ActiveTheme::getColour(ActiveTheme::SURFACE));
        editor.setColour(juce::TextEditor::textColourId,
                         ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        editor.setColour(juce::TextEditor::outlineColourId, ActiveTheme::getBorderColour());
        editor.setIndents(8, 6);
        addChildComponent(editor);
    }

    Config::TrackColourEntry* selectedEntry() {
        return juce::isPositiveAndBelow(selected_, static_cast<int>(palette_.size()))
                   ? &palette_[static_cast<size_t>(selected_)]
                   : nullptr;
    }
    const Config::TrackColourEntry* selectedEntry() const {
        return const_cast<AppearancePage*>(this)->selectedEntry();
    }

    void selectColour(int index) {
        selected_ = index;
        if (const auto* entry = selectedEntry()) {
            hexEditor_.setText(
                juce::String::toHexString(static_cast<int>(entry->colour & 0x00FFFFFF))
                    .paddedLeft('0', 6)
                    .toUpperCase(),
                juce::dontSendNotification);
            nameEditor_.setText(juce::String(entry->name), juce::dontSendNotification);
        }
        resized();
        repaint();
    }

    void addColour() {
        if (static_cast<int>(palette_.size()) >= MAX_PALETTE_SIZE)
            return;
        palette_.push_back({0xFF808080, tr("preferences.colours.new_default_name").toStdString()});
        selectColour(static_cast<int>(palette_.size()) - 1);
        notifyHeightChanged();
    }

    void removeSelectedColour() {
        if (selectedEntry() == nullptr)
            return;
        palette_.erase(palette_.begin() + selected_);
        selectColour(
            palette_.empty() ? -1 : juce::jmin(selected_, static_cast<int>(palette_.size()) - 1));
        notifyHeightChanged();
    }

    // The grid can gain or lose a row, which changes the page's height.
    void notifyHeightChanged() {
        setSize(getWidth(), juce::jmax(getHeight(), getPreferredHeight(getWidth())));
    }

    struct BuiltInThemeOption {
        int comboId;
        const char* themeId;
        const char* label;
    };

    inline static constexpr std::array<BuiltInThemeOption, 3> builtInThemeOptions_{{
        {1, ThemeManager::kDarkThemeId, "Dark"},
        {2, ThemeManager::kLightThemeId, "Light"},
        {3, ThemeManager::kHighContrastThemeId, "High Contrast"},
    }};

    // Factory (embedded) theme ids start here, user theme ids above them;
    // each range indexes into its list (id - base).
    static constexpr int kFactoryThemeIdBase = 50;
    static constexpr int kUserThemeIdBase = 100;

    // Repopulates the combo: built-ins first, then the embedded factory
    // themes, then discovered user themes. A user theme with a factory id
    // shadows the factory entry (matching applyThemeById resolution).
    // Called on construction, each time the dialog loads, and after New Theme.
    void rebuildThemeCombo() {
        themeCombo.clear(juce::dontSendNotification);
        for (const auto& option : builtInThemeOptions_)
            themeCombo.addItem(option.label, option.comboId);

        userThemes_ = scanUserThemes();

        factoryThemes_.clear();
        for (const auto& factory : factoryThemes()) {
            bool shadowed = false;
            for (const auto& user : userThemes_)
                shadowed = shadowed || user.id == factory.id;
            if (!shadowed)
                factoryThemes_.push_back(factory);
        }
        if (!factoryThemes_.empty()) {
            themeCombo.addSeparator();
            for (size_t i = 0; i < factoryThemes_.size(); ++i)
                themeCombo.addItem(juce::String(factoryThemes_[i].name),
                                   kFactoryThemeIdBase + static_cast<int>(i));
        }

        if (!userThemes_.empty()) {
            themeCombo.addSeparator();
            for (size_t i = 0; i < userThemes_.size(); ++i)
                themeCombo.addItem(juce::String(userThemes_[i].name),
                                   kUserThemeIdBase + static_cast<int>(i));
        }
    }

    int themeIdForValue(const std::string& theme) const {
        for (const auto& option : builtInThemeOptions_)
            if (theme == option.themeId)
                return option.comboId;
        for (size_t i = 0; i < userThemes_.size(); ++i)
            if (theme == userThemes_[i].id)
                return kUserThemeIdBase + static_cast<int>(i);
        for (size_t i = 0; i < factoryThemes_.size(); ++i)
            if (theme == factoryThemes_[i].id)
                return kFactoryThemeIdBase + static_cast<int>(i);
        return 1;  // persisted theme no longer on disk -> fall back to Dark
    }

    std::string themeValueForId(int id) const {
        if (id >= kUserThemeIdBase) {
            const auto index = static_cast<size_t>(id - kUserThemeIdBase);
            if (index < userThemes_.size())
                return userThemes_[index].id;
        }
        if (id >= kFactoryThemeIdBase) {
            const auto index = static_cast<size_t>(id - kFactoryThemeIdBase);
            if (index < factoryThemes_.size())
                return factoryThemes_[index].id;
        }
        for (const auto& option : builtInThemeOptions_)
            if (id == option.comboId)
                return option.themeId;
        return ThemeManager::kDarkThemeId;
    }

    // Display Scale id schemes. Font family: id 1 is "Inter (default)"; ids from
    // kFontFamilyIdBase index into fontFamilies_.
    static constexpr int kFontFamilyIdBase = 100;

    int fontFamilyIdForValue(const std::string& family) const {
        if (family.empty())
            return 1;
        const int idx = fontFamilies_.indexOf(juce::String(family));
        return idx >= 0 ? kFontFamilyIdBase + idx : 1;
    }

    std::string fontFamilyValueForId(int id) const {
        const int idx = id - kFontFamilyIdBase;
        if (idx >= 0 && idx < fontFamilies_.size())
            return fontFamilies_[idx].toStdString();
        return {};  // "Inter (default)"
    }

    static int scaleIdForValue(double v) {
        if (v <= 0.0)
            return 1;
        if (std::abs(v - 1.0) < 0.01)
            return 2;
        if (std::abs(v - 1.25) < 0.01)
            return 3;
        if (std::abs(v - 1.5) < 0.01)
            return 4;
        if (std::abs(v - 1.75) < 0.01)
            return 5;
        if (std::abs(v - 2.0) < 0.01)
            return 6;
        return 1;
    }

    static double scaleValueForId(int id) {
        switch (id) {
            case 2:
                return 1.0;
            case 3:
                return 1.25;
            case 4:
                return 1.5;
            case 5:
                return 1.75;
            case 6:
                return 2.0;
            default:
                return 0.0;
        }
    }

    // Exports the current selection's base table (dark/light) as a complete,
    // editable JSON template to a location the user picks.
    void saveThemeTemplate() {
        const std::string selectedId = themeValueForId(themeCombo.getSelectedId());
        const std::string baseId = (selectedId == ThemeManager::kLightThemeId)
                                       ? ThemeManager::kLightThemeId
                                       : ThemeManager::kDarkThemeId;

        auto dir = paths::themesDir();
        dir.createDirectory();
        const juce::String suggested =
            (baseId == ThemeManager::kLightThemeId) ? "Light Theme.json" : "Dark Theme.json";

        fileChooser_ = std::make_unique<juce::FileChooser>(
            tr("preferences.dialog.save_theme_template"), dir.getChildFile(suggested), "*.json");
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                juce::FileBrowserComponent::warnAboutOverwriting,
            [this, baseId](const juce::FileChooser& fc) {
                auto dest = fc.getResult();
                if (dest == juce::File())
                    return;
                if (!dest.hasFileExtension("json"))
                    dest = dest.withFileExtension("json");
                writeThemeTemplate(dest, baseId, dest.getFileNameWithoutExtension().toStdString());
            });
    }

    // Picks a theme JSON, copies it into the Themes folder (so it persists and
    // hot-reloads via the id = filename-stem resolution), then selects it. It
    // is applied when the dialog is accepted, like every other setting here.
    void loadThemeFromFile() {
        auto dir = paths::themesDir();
        dir.createDirectory();

        fileChooser_ =
            std::make_unique<juce::FileChooser>(tr("preferences.dialog.load_theme"), dir, "*.json");
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                const auto file = fc.getResult();
                if (!file.existsAsFile() || !loadThemeFile(file).has_value())
                    return;  // not a usable theme file

                auto themesDir = paths::themesDir();
                themesDir.createDirectory();
                juce::File dest = file;
                if (file.getParentDirectory() != themesDir) {
                    dest = themesDir.getChildFile(file.getFileName());
                    file.copyFileTo(dest);
                }

                rebuildThemeCombo();
                themeCombo.setSelectedId(
                    themeIdForValue(dest.getFileNameWithoutExtension().toStdString()),
                    juce::dontSendNotification);
            });
    }

    juce::Label themeHeader;
    juce::Label themeLabel;
    juce::ComboBox themeCombo;
    juce::TextButton loadThemeButton;
    juce::TextButton getTemplateButton;
    juce::TextButton openThemesFolderButton;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    std::vector<ThemeFileEntry> userThemes_;
    std::vector<FactoryThemeEntry> factoryThemes_;

    juce::Label scaleHeader;
    juce::Label scaleLabel;
    juce::ComboBox scaleCombo;
    juce::Label fontFamilyLabel;
    juce::ComboBox fontFamilyCombo;
    juce::StringArray fontFamilies_;  // system families, combo-order (id = base + index)
    juce::Label fontScaleLabel;
    magda::daw::ui::TextSlider fontScaleSlider;
    juce::Label densityLabel;
    magda::daw::ui::TextSlider densitySlider;

    juce::Label controlsHeader;
    juce::Label trackColourStyleLabel;
    magda::daw::ui::PillSegments trackColourStyle;
    juce::Label muteSoloStyleLabel;
    magda::daw::ui::PillSegments muteSoloStyle;

    juce::Label coloursHeader;
    std::vector<Config::TrackColourEntry> palette_;
    int selected_ = -1;
    std::vector<juce::Rectangle<int>> swatchBounds_;
    juce::Rectangle<int> selectedSwatchBounds_;
    juce::TextEditor hexEditor_;
    juce::TextEditor nameEditor_;
    juce::TextButton removeColourButton_;
    juce::ToggleButton applyPaletteToProjectToggle;

    juce::Label clipColourHeader;
    juce::Label clipColourModeLabel;
    juce::ComboBox clipColourModeCombo;

    std::vector<juce::Rectangle<int>> rowLines_;
};

// ---- Defaults tab (what a new project starts as) --------------------------
//
// Nothing on this tab touches the project that is open. A project's own length,
// sample rate, bit depths and credits live in its ProjectInfo and are edited in
// File > Project Settings; these are only the values a newly created project is
// seeded with. That is why the tab is called Defaults rather than Project - a
// Project tab in Preferences reads like it edits the one you have open, which is
// what the Project Settings dialog is for.
//
// Two sections: the format values every project needs, and the credits that
// describe the person making it. Not every metadata field appears - see
// ProjectMetadataField::seededFromDefaults for which ones do and why.

class DefaultsPage : public juce::Component {
  public:
    explicit DefaultsPage(AudioEngine* audioEngine) : audioEngine_(audioEngine) {
        setupSectionHeader(*this, formatHeader, tr("preferences.section.new_project_defaults"));
        setupSectionHeader(*this, creditsHeader, tr("preferences.section.credit_defaults"));

        // Labelled from the Project Settings keys rather than a second set of
        // preferences.* ones: these are the same thirteen field names, and two
        // copies of "Original artist" that have to stay identical is a worse
        // problem than one key serving two dialogs.
        for (const auto& field : kProjectMetadataFields) {
            if (!field.seededFromDefaults)
                continue;

            auto row = std::make_unique<CreditRow>();
            row->field = &field;
            setupComboLabel(row->label, tr("project_settings.metadata." + juce::String(field.key)));

            row->label.setFont(FontManager::getInstance().getUIFont(12.0f));
            row->label.setColour(juce::Label::textColourId,
                                 ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
            row->label.setBorderSize({});
            if (field.key == std::string("genre"))
                row->editor.setTextToShowWhenEmpty(tr("preferences.credits.genre_hint"),
                                                   ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
            else if (field.key == std::string("website"))
                row->editor.setTextToShowWhenEmpty("https://",
                                                   ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
            row->editor.setIndents(8, 6);
            row->editor.setFont(FontManager::getInstance().getUIFont(13.0f));
            row->editor.setColour(juce::TextEditor::backgroundColourId,
                                  ActiveTheme::getColour(ActiveTheme::SURFACE));
            row->editor.setColour(juce::TextEditor::textColourId,
                                  ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
            row->editor.setColour(juce::TextEditor::outlineColourId,
                                  ActiveTheme::getColour(ActiveTheme::BORDER));
            addAndMakeVisible(row->editor);
            creditRows_.push_back(std::move(row));
        }

        setupTextSlider(*this, lengthSlider, lengthLabel,
                        tr("preferences.slider.default_total_length"), 16.0, 4096.0, 16.0,
                        magda::TechnicalTextToken::Bars, 0, true);

        setupComboLabel(sampleRateLabel, tr("preferences.label.default_sample_rate"));
        setupComboLabel(renderBitLabel, tr("preferences.label.default_render_bit_depth"));
        setupComboLabel(bounceBitLabel, tr("preferences.label.default_bounce_bit_depth"));

        magda::daw::ui::fillSampleRateCombo(sampleRateCombo);
        magda::daw::ui::fillBitDepthCombo(renderBitCombo);
        magda::daw::ui::fillBitDepthCombo(bounceBitCombo);
        for (auto* combo : {&sampleRateCombo, &renderBitCombo, &bounceBitCombo}) {
            styleCombo(*combo);
            combo->onChange = [this] { updateNotes(); };
            addAndMakeVisible(*combo);
        }

        for (auto* note : {&sampleRateNote, &bounceBitNote, &creditsNote}) {
            note->setFont(FontManager::getInstance().getUIFont(11.5f));
            note->setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
            note->setBorderSize({});
            addAndMakeVisible(*note);
        }
        creditsNote.setText(tr("preferences.credits.note"), juce::dontSendNotification);
    }

    int getPreferredHeight(int) const {
        const int pairs = (static_cast<int>(creditRows_.size()) + 1) / 2;
        return 16 + 30 + 4 * kRowH + 22 + 30 + 30 + pairs * kCreditH + 16;
    }

    void resized() override {
        rowLines_.clear();
        auto bounds = getLocalBounds().reduced(0, 16);
        // Label over an optional note on the left, the control on the right.
        const auto row = [&](juce::Label& label, juce::Component& control, juce::Label* note,
                             int controlW) {
            auto r = bounds.removeFromTop(kRowH);
            rowLines_.push_back(r.withTop(r.getBottom() - 1));
            control.setBounds(r.removeFromRight(controlW).withSizeKeepingCentre(controlW, 26));
            const bool hasNote = note != nullptr && note->getText().isNotEmpty();
            if (note != nullptr)
                note->setVisible(hasNote);
            label.setBounds(hasNote ? r.removeFromTop(kRowH / 2 + 2).withTrimmedTop(4) : r);
            if (hasNote)
                note->setBounds(r.removeFromTop(16));
        };

        formatHeader.setBounds(bounds.removeFromTop(30));
        row(lengthLabel, lengthSlider, nullptr, 96);
        row(sampleRateLabel, sampleRateCombo, &sampleRateNote, 160);
        row(renderBitLabel, renderBitCombo, nullptr, 160);
        row(bounceBitLabel, bounceBitCombo, &bounceBitNote, 160);

        bounds.removeFromTop(22);
        creditsHeader.setBounds(bounds.removeFromTop(30));
        creditsNote.setBounds(bounds.removeFromTop(30).withTrimmedBottom(8));

        // Two columns, label over field.
        constexpr int gap = 16;
        const int colW = (bounds.getWidth() - gap) / 2;
        for (size_t i = 0; i < creditRows_.size(); i += 2) {
            auto pair = bounds.removeFromTop(kCreditH);
            for (size_t j = i; j < juce::jmin(i + 2, creditRows_.size()); ++j) {
                auto cell = (j == i) ? pair.removeFromLeft(colW) : pair.withTrimmedLeft(gap);
                creditRows_[j]->label.setBounds(cell.removeFromTop(20));
                creditRows_[j]->editor.setBounds(cell.removeFromTop(30));
            }
        }
    }

    void paint(juce::Graphics& g) override {
        g.setColour(ActiveTheme::getBorderColour().withMultipliedAlpha(0.6f));
        for (const auto& line : rowLines_)
            g.fillRect(line);
    }

    void loadSettings(Config& config) {
        lengthSlider.setValue(config.getDefaultTimelineLengthBars(), juce::dontSendNotification);
        sampleRateCombo.setSelectedId(
            magda::daw::ui::sampleRateItemId(config.getRenderSampleRate()),
            juce::dontSendNotification);
        renderBitCombo.setSelectedId(magda::daw::ui::bitDepthItemId(config.getRenderBitDepth()),
                                     juce::dontSendNotification);
        bounceBitCombo.setSelectedId(magda::daw::ui::bitDepthItemId(config.getBounceBitDepth()),
                                     juce::dontSendNotification);
        updateNotes();

        const auto& credits = config.getProjectMetadataDefaults();
        for (auto& row : creditRows_) {
            const auto entry = credits.find(row->field->key);
            row->editor.setText(entry == credits.end() ? juce::String()
                                                       : juce::String(entry->second),
                                juce::dontSendNotification);
        }
    }

    void applySettings(Config& config) {
        config.setDefaultTimelineLengthBars(static_cast<int>(lengthSlider.getValue()));
        config.setRenderSampleRate(
            magda::daw::ui::sampleRateForItemId(sampleRateCombo.getSelectedId()));
        config.setRenderBitDepth(magda::daw::ui::bitDepthForItemId(renderBitCombo.getSelectedId()));
        config.setBounceBitDepth(magda::daw::ui::bitDepthForItemId(bounceBitCombo.getSelectedId()));

        std::map<std::string, std::string> credits;
        for (const auto& row : creditRows_) {
            const auto value = row->editor.getText().trim();
            if (value.isNotEmpty())
                credits[row->field->key] = value.toStdString();
        }
        config.setProjectMetadataDefaults(std::move(credits));
    }

  private:
    void setupComboLabel(juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(FontManager::getInstance().getUIFont(12.0f));
        label.setColour(juce::Label::textColourId,
                        ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        addAndMakeVisible(label);
    }

    struct CreditRow {
        const ProjectMetadataField* field = nullptr;
        juce::Label label;
        juce::TextEditor editor;
    };

    // The device mismatch in amber, and why float is offered for bounces.
    void updateNotes() {
        juce::String rateNote;
        if (auto* io = audioEngine_ != nullptr ? audioEngine_->getAudioIO() : nullptr) {
            const double device = io->status().sampleRate;
            const double chosen =
                magda::daw::ui::sampleRateForItemId(sampleRateCombo.getSelectedId());
            if (device > 0.0 && std::abs(device - chosen) > 0.5)
                rateNote = tr("preferences.note.device_rate")
                               .replace("{0}", juce::String(static_cast<int>(device)) + " Hz");
        }
        sampleRateNote.setText(rateNote, juce::dontSendNotification);
        sampleRateNote.setColour(juce::Label::textColourId,
                                 ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION));
        bounceBitNote.setText(bounceBitCombo.getSelectedId() == 3
                                  ? tr("preferences.note.float_bounce")
                                  : juce::String(),
                              juce::dontSendNotification);
        resized();
    }

    static constexpr int kRowH = 44;
    static constexpr int kCreditH = 58;

    AudioEngine* audioEngine_ = nullptr;
    juce::Label formatHeader, creditsHeader;
    juce::Label lengthLabel, sampleRateLabel, renderBitLabel, bounceBitLabel;
    juce::Label sampleRateNote, bounceBitNote, creditsNote;
    magda::daw::ui::TextSlider lengthSlider;
    juce::ComboBox sampleRateCombo, renderBitCombo, bounceBitCombo;
    std::vector<std::unique_ptr<CreditRow>> creditRows_;
    std::vector<juce::Rectangle<int>> rowLines_;
};

// ---- Rendering tab --------------------------------------------------------

// The tokens a file-name pattern can hold; the expansion lives in ClipCommands and
// MainWindowExport.
const juce::StringArray kPatternTokens{"clip-name", "track-name", "project-name", "date-time"};

/**
 * @brief A file-name pattern as chips and literal text (#3162).
 *
 * Tokens are whole chips, so a typo cannot break one: typing adds literal text,
 * Backspace removes the last character or chip, and clicking a chip removes it.
 */
class PatternField : public juce::Component {
  public:
    std::function<void()> onChange;
    std::function<void()> onFocus;

    PatternField() {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::IBeamCursor);
    }

    void setPattern(const juce::String& pattern) {
        segments_.clear();
        auto rest = pattern;
        while (rest.isNotEmpty()) {
            const int open = rest.indexOfChar('<');
            const int close = open >= 0 ? rest.indexOfChar(open, '>') : -1;
            const auto name = close > open ? rest.substring(open + 1, close) : juce::String();
            if (open < 0 || close < 0 || !kPatternTokens.contains(name)) {
                appendLiteral(rest);
                break;
            }
            appendLiteral(rest.substring(0, open));
            segments_.push_back({true, name});
            rest = rest.substring(close + 1);
        }
        repaint();
    }

    juce::String getPattern() const {
        juce::String out;
        for (const auto& segment : segments_)
            out << (segment.token ? "<" + segment.text + ">" : segment.text);
        return out;
    }

    void insertToken(const juce::String& name) {
        segments_.push_back({true, name});
        changed();
    }

    void paint(juce::Graphics& g) override {
        const auto box = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(ActiveTheme::getBackgroundColour());
        g.fillRoundedRectangle(box, 5.0f);
        g.setColour(hasKeyboardFocus(false) ? ActiveTheme::getAccentColour()
                                            : ActiveTheme::getBorderColour());
        g.drawRoundedRectangle(box, 5.0f, hasKeyboardFocus(false) ? 1.5f : 1.0f);

        const auto mono = FontManager::getInstance().getMonoFont(12.0f);
        g.setFont(mono);
        chips_.clear();
        float x = 8.0f;
        const float h = static_cast<float>(getHeight());
        for (std::size_t i = 0; i < segments_.size(); ++i) {
            const auto& segment = segments_[i];
            const float w =
                static_cast<float>(juce::GlyphArrangement::getStringWidthInt(mono, segment.text));
            if (segment.token) {
                const auto chip = juce::Rectangle<float>(x, 0.0f, w + 16.0f, h)
                                      .withSizeKeepingCentre(w + 16.0f, h - 12.0f);
                g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE));
                g.fillRoundedRectangle(chip, 4.0f);
                g.setColour(ActiveTheme::getBorderColour().brighter(0.2f));
                g.drawRoundedRectangle(chip.reduced(0.5f), 4.0f, 1.0f);
                g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
                g.drawText(segment.text, chip, juce::Justification::centred, false);
                chips_.push_back({i, chip});
                x += chip.getWidth() + 3.0f;
            } else {
                g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
                g.drawText(segment.text, juce::Rectangle<float>(x, 0.0f, w + 2.0f, h),
                           juce::Justification::centredLeft, false);
                x += w + 3.0f;
            }
        }
        if (hasKeyboardFocus(false)) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
            g.fillRect(x, h * 0.3f, 1.0f, h * 0.4f);
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        for (const auto& [index, chip] : chips_) {
            if (chip.contains(e.position)) {
                segments_.erase(segments_.begin() + static_cast<std::ptrdiff_t>(index));
                changed();
                return;
            }
        }
        grabKeyboardFocus();
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::backspaceKey) {
            if (segments_.empty())
                return true;
            auto& last = segments_.back();
            if (last.token || last.text.length() <= 1)
                segments_.pop_back();
            else
                last.text = last.text.dropLastCharacters(1);
            changed();
            return true;
        }
        const auto c = key.getTextCharacter();
        // The characters the expansion replaces, and the token brackets.
        if (c >= ' ' && !juce::String("<>/\\:").containsChar(c) &&
            !key.getModifiers().isCommandDown()) {
            appendLiteral(juce::String::charToString(c));
            changed();
            return true;
        }
        return false;
    }

    void focusGained(FocusChangeType) override {
        if (onFocus)
            onFocus();
        repaint();
    }
    void focusLost(FocusChangeType) override {
        repaint();
    }

  private:
    struct Segment {
        bool token;
        juce::String text;
    };

    void appendLiteral(const juce::String& text) {
        if (text.isEmpty())
            return;
        if (!segments_.empty() && !segments_.back().token)
            segments_.back().text << text;
        else
            segments_.push_back({false, text});
    }

    void changed() {
        repaint();
        if (onChange)
            onChange();
    }

    std::vector<Segment> segments_;
    std::vector<std::pair<std::size_t, juce::Rectangle<float>>> chips_;
};

/** @brief A dashed "+ token" chip that inserts its token into the focused pattern. */
class InsertChip : public juce::Component {
  public:
    std::function<void()> onClick;

    explicit InsertChip(juce::String token) : token_(std::move(token)) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    int getIdealWidth() const {
        return juce::GlyphArrangement::getStringWidthInt(
                   FontManager::getInstance().getMonoFont(12.0f), "+ " + token_) +
               18;
    }

    void paint(juce::Graphics& g) override {
        juce::Path outline;
        outline.addRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f);
        juce::Path dashed;
        const float dashes[] = {3.0f, 3.0f};
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(isMouseOver() ? ActiveTheme::getAccentColour()
                                  : ActiveTheme::getBorderColour());
        g.fillPath(dashed);
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        g.setFont(FontManager::getInstance().getMonoFont(12.0f));
        g.drawText("+ " + token_, getLocalBounds(), juce::Justification::centred, false);
    }

    void mouseEnter(const juce::MouseEvent&) override {
        repaint();
    }
    void mouseExit(const juce::MouseEvent&) override {
        repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (onClick && getLocalBounds().contains(e.getPosition()))
            onClick();
    }

  private:
    juce::String token_;
};

/// A pattern expanded as the export and bounce code expand it, for the preview line.
juce::String previewPattern(juce::String pattern, const juce::String& clip,
                            const juce::String& track) {
    auto project = ProjectManager::getInstance().getProjectName();
    if (project.isEmpty())
        project = "untitled";
    project = project.replaceCharacters(" /\\:", "____");
    const auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    pattern = pattern.replace("<clip-name>", clip.isEmpty() ? project : clip);
    pattern = pattern.replace("<track-name>", track);
    pattern = pattern.replace("<project-name>", project);
    pattern = pattern.replace("<date-time>", timestamp);
    return pattern.replaceCharacters("/\\:", "___");
}

class RenderingPage : public juce::Component {
  public:
    RenderingPage() {
        setupSectionHeader(*this, renderHeader, tr("preferences.section.output"));

        setupComboLabel(renderFolderLabel, tr("preferences.label.export_folder"));
        renderFolderNote.setText(tr("preferences.note.export_folder"), juce::dontSendNotification);
        renderFolderNote.setFont(FontManager::getInstance().getUIFont(11.5f));
        renderFolderNote.setColour(juce::Label::textColourId,
                                   ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        renderFolderNote.setBorderSize({});
        addAndMakeVisible(renderFolderNote);

        folderChoice.setOptions(
            {tr("preferences.option.documents_folder"), tr("preferences.option.custom_folder")});
        folderChoice.onChange = [this](int index) {
            if (index == 0) {
                renderFolderPath_.clear();
                showFolder();
            } else {
                chooseFolder();
            }
        };
        addAndMakeVisible(folderChoice);

        renderFolderValue.setFont(FontManager::getInstance().getMonoFont(12.0f));
        renderFolderValue.setColour(juce::Label::textColourId,
                                    ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        addAndMakeVisible(renderFolderValue);

        renderFolderBrowseButton.setButtonText(trEllipsis("preferences.button.choose"));
        renderFolderBrowseButton.onClick = [this] { chooseFolder(); };
        addAndMakeVisible(renderFolderBrowseButton);

        setupSectionHeader(*this, namingHeader, tr("preferences.section.file_naming"));
        setupComboLabel(patternLabel, tr("preferences.label.export_pattern"));
        setupComboLabel(bouncePatternLabel, tr("preferences.label.bounce_pattern"));
        for (auto* field : {&patternField, &bouncePatternField}) {
            field->onChange = [this] { updatePreviews(); };
            field->onFocus = [this, field] { focusedField_ = field; };
            addAndMakeVisible(*field);
        }
        for (auto* preview : {&patternPreview, &bouncePreview}) {
            preview->setFont(FontManager::getInstance().getMonoFont(12.0f));
            preview->setColour(juce::Label::textColourId,
                               ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
            addAndMakeVisible(*preview);
        }

        insertLabel.setText(tr("preferences.label.insert"), juce::dontSendNotification);
        insertLabel.setFont(FontManager::getInstance().getUIFont(12.5f));
        insertLabel.setColour(juce::Label::textColourId,
                              ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        addAndMakeVisible(insertLabel);
        for (const auto& token : kPatternTokens) {
            auto chip = std::make_unique<InsertChip>(token);
            chip->onClick = [this, token] { focusedField_->insertToken(token); };
            addAndMakeVisible(*chip);
            insertChips_.push_back(std::move(chip));
        }
    }

    int getPreferredHeight(int) const {
        return 16 + 30 + 52 + 40 + 22 + 30 + 2 * 72 + 44 + 16;
    }

    void resized() override {
        rowLines_.clear();
        auto bounds = getLocalBounds().reduced(0, 16);
        const auto takeRow = [this](juce::Rectangle<int>& b, int h) {
            auto row = b.removeFromTop(h);
            rowLines_.push_back(row.withTop(row.getBottom() - 1));
            return row;
        };

        renderHeader.setBounds(bounds.removeFromTop(30));
        {
            auto row = takeRow(bounds, 52);
            folderChoice.setBounds(row.removeFromRight(folderChoice.getIdealWidth())
                                       .withSizeKeepingCentre(folderChoice.getIdealWidth(), 28));
            renderFolderLabel.setBounds(row.removeFromTop(26).withTrimmedTop(6));
            renderFolderNote.setBounds(row.removeFromTop(18));
        }
        {
            auto row = takeRow(bounds, 40).reduced(0, 5);
            renderFolderBrowseButton.setBounds(row.removeFromRight(100));
            row.removeFromRight(8);
            renderFolderValue.setBounds(row);
        }

        bounds.removeFromTop(22);
        namingHeader.setBounds(bounds.removeFromTop(30));
        for (auto [label, field, preview] :
             {std::tuple{&patternLabel, &patternField, &patternPreview},
              std::tuple{&bouncePatternLabel, &bouncePatternField, &bouncePreview}}) {
            auto row = takeRow(bounds, 72);
            auto top = row.removeFromTop(48).withTrimmedTop(8);
            label->setBounds(top.removeFromLeft(140));
            field->setBounds(top);
            preview->setBounds(row.withTrimmedLeft(140).withTrimmedBottom(4));
        }
        auto row = bounds.removeFromTop(44).reduced(0, 9);
        insertLabel.setBounds(row.removeFromLeft(56));
        for (auto& chip : insertChips_) {
            chip->setBounds(row.removeFromLeft(chip->getIdealWidth()));
            row.removeFromLeft(8);
        }
    }

    void paint(juce::Graphics& g) override {
        g.setColour(ActiveTheme::getBorderColour().withMultipliedAlpha(0.6f));
        for (const auto& line : rowLines_)
            g.fillRect(line);
    }

    void loadSettings(Config& config) {
        renderFolderPath_ = config.getRenderFolder();
        showFolder();
        patternField.setPattern(juce::String(config.getRenderFilePattern()));
        bouncePatternField.setPattern(juce::String(config.getBounceFilePattern()));
        updatePreviews();
    }

    void applySettings(Config& config) {
        config.setRenderFolder(renderFolderPath_);

        auto pattern = patternField.getPattern().toStdString();
        if (pattern.empty())
            pattern = "<project-name>_<date-time>";
        config.setRenderFilePattern(pattern);

        auto bouncePattern = bouncePatternField.getPattern().toStdString();
        if (bouncePattern.empty())
            bouncePattern = "<clip-name>_<date-time>";
        config.setBounceFilePattern(bouncePattern);
    }

  private:
    void setupComboLabel(juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(FontManager::getInstance().getUIFont(13.0f));
        label.setColour(juce::Label::textColourId,
                        ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        label.setJustificationType(juce::Justification::centredLeft);
        label.setBorderSize({});
        addAndMakeVisible(label);
    }

    // Export's dialog opens in Documents unless a folder is set (MainWindowExport).
    void showFolder() {
        const bool custom = !renderFolderPath_.empty();
        folderChoice.setSelectedIndex(custom ? 1 : 0);
        renderFolderValue.setText(
            custom ? juce::String(renderFolderPath_)
                   : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                         .getFullPathName(),
            juce::dontSendNotification);
        renderFolderValue.setAlpha(custom ? 1.0f : 0.5f);
    }

    void chooseFolder() {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            tr("preferences.dialog.select_render_output_folder"));
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc) {
                if (const auto result = fc.getResult(); result.exists())
                    renderFolderPath_ = result.getFullPathName().toStdString();
                showFolder();
            });
    }

    void updatePreviews() {
        const auto arrow = juce::String(juce::CharPointer_UTF8("\xe2\x86\x92 "));
        patternPreview.setText(arrow + previewPattern(patternField.getPattern(), {}, "master"),
                               juce::dontSendNotification);
        bouncePreview.setText(arrow +
                                  previewPattern(bouncePatternField.getPattern(), "clip", "track"),
                              juce::dontSendNotification);
    }

    juce::Label renderHeader, namingHeader;
    juce::Label renderFolderLabel, renderFolderNote;
    magda::daw::ui::PillSegments folderChoice;
    juce::Label renderFolderValue;
    juce::TextButton renderFolderBrowseButton;
    std::string renderFolderPath_;

    juce::Label patternLabel, bouncePatternLabel, patternPreview, bouncePreview, insertLabel;
    PatternField patternField, bouncePatternField;
    PatternField* focusedField_ = &patternField;
    std::vector<std::unique_ptr<InsertChip>> insertChips_;
    std::vector<juce::Rectangle<int>> rowLines_;

    std::unique_ptr<juce::FileChooser> fileChooser_;
};

// ---- Paths tab (configurable user-data locations) -------------------------
//
// Two pickers: Data Folder (logs, config, scripts, plugin caches) and
// Presets Folder (Chains, Racks, Devices). Render Folder lives on the
// Rendering tab — kept there to avoid duplication.
//
// UX: when the user picks a new folder via Browse, immediately surface a
// migration prompt (Copy / Don't copy / Cancel). The user's answer is
// remembered as `pendingCopyData_` / `pendingCopyPresets_`. Cancel reverts
// the pick entirely. On Apply, the Paths section persists the new path(s)
// to Config, runs any pending copy, and (for the data folder) quits so the
// app restarts at the new path.

class PathsPage : public juce::Component {
  public:
    PathsPage() {
        // --- Data Folder ---
        setupSectionHeader(*this, dataHeader_, tr("preferences.paths.section.data"));

        dataLabel_.setText(tr("preferences.paths.label.folder"), juce::dontSendNotification);
        dataLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        dataLabel_.setColour(juce::Label::textColourId,
                             ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        dataLabel_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(dataLabel_);

        dataValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
        dataValue_.setColour(juce::Label::textColourId,
                             ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        dataValue_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(dataValue_);

        dataBrowse_.setButtonText(trEllipsis("preferences.button.browse"));
        dataBrowse_.onClick = [this]() {
            pickFolder(Kind::Data, tr("preferences.paths.dialog.choose_data"));
        };
        addAndMakeVisible(dataBrowse_);

        dataReveal_.setButtonText(tr(
#if JUCE_MAC
            "preferences.button.reveal_finder"
#elif JUCE_WINDOWS
            "preferences.button.reveal_explorer"
#else
            "preferences.button.reveal_files"
#endif
            ));
        dataReveal_.onClick = [this]() {
            revealConfigured(dataPath_, magda::paths::alwaysOSDefault());
        };
        addAndMakeVisible(dataReveal_);

        dataReset_.setButtonText(tr("preferences.button.reset"));
        dataReset_.onClick = [this]() {
            dataPath_.clear();
            updateDisplay();
        };
        addAndMakeVisible(dataReset_);

        dataNote_.setFont(FontManager::getInstance().getUIFont(11.0f));
        dataNote_.setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        dataNote_.setJustificationType(juce::Justification::centredLeft);
        dataNote_.setText(tr("preferences.paths.note.data"), juce::dontSendNotification);
        addAndMakeVisible(dataNote_);

        // --- Presets Folder ---
        setupSectionHeader(*this, presetsHeader_, tr("preferences.paths.section.presets"));

        presetsLabel_.setText(tr("preferences.paths.label.folder"), juce::dontSendNotification);
        presetsLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        presetsLabel_.setColour(juce::Label::textColourId,
                                ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        presetsLabel_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(presetsLabel_);

        presetsValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
        presetsValue_.setColour(juce::Label::textColourId,
                                ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        presetsValue_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(presetsValue_);

        presetsBrowse_.setButtonText(trEllipsis("preferences.button.browse"));
        presetsBrowse_.onClick = [this]() {
            pickFolder(Kind::Presets, tr("preferences.paths.dialog.choose_presets"));
        };
        addAndMakeVisible(presetsBrowse_);

        presetsReveal_.setButtonText(tr(
#if JUCE_MAC
            "preferences.button.reveal_finder"
#elif JUCE_WINDOWS
            "preferences.button.reveal_explorer"
#else
            "preferences.button.reveal_files"
#endif
            ));
        presetsReveal_.onClick = [this]() { revealConfigured(presetsPath_, presetsDefaultDir()); };
        addAndMakeVisible(presetsReveal_);

        presetsReset_.setButtonText(tr("preferences.button.reset"));
        presetsReset_.onClick = [this]() {
            presetsPath_.clear();
            updateDisplay();
        };
        addAndMakeVisible(presetsReset_);

        presetsNote_.setFont(FontManager::getInstance().getUIFont(11.0f));
        presetsNote_.setColour(juce::Label::textColourId,
                               ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        presetsNote_.setJustificationType(juce::Justification::centredLeft);
        presetsNote_.setText(tr("preferences.paths.note.presets"), juce::dontSendNotification);
        addAndMakeVisible(presetsNote_);

        // --- Hint about Render Folder ---
        renderHint_.setFont(FontManager::getInstance().getUIFont(11.0f));
        renderHint_.setColour(juce::Label::textColourId,
                              ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        renderHint_.setJustificationType(juce::Justification::centredLeft);
        renderHint_.setText(tr("preferences.paths.note.render_hint"), juce::dontSendNotification);
        addAndMakeVisible(renderHint_);

        // --- Media Library ---
        setupSectionHeader(*this, editorHeader_, "External Audio Editor");
        setupPathRowLabel(*this, editorLocationCaption_, "Editor");
        setupPathTextEditor(*this, editorLocationField_);

        editorBrowse_.setButtonText("Browse...");
        editorBrowse_.onClick = [this]() { browseForExternalEditor(); };
        addAndMakeVisible(editorBrowse_);

        editorReset_.setButtonText("Reset");
        editorReset_.onClick = [this]() {
            editorPath_.clear();
            refreshMediaLibrarySettings();
        };
        addAndMakeVisible(editorReset_);

        setupSectionHeader(*this, dbHeader_, "Media Database");
        setupPathRowLabel(*this, dbLocationCaption_, "Database location");
        setupPathTextEditor(*this, dbLocationField_);

        dbBrowse_.setButtonText("Browse...");
        dbBrowse_.onClick = [this]() { browseForDbLocation(); };
        addAndMakeVisible(dbBrowse_);

        dbReset_.setButtonText("Reset");
        dbReset_.onClick = [this]() {
            dbDir_.clear();
            refreshMediaLibrarySettings();
        };
        addAndMakeVisible(dbReset_);

        dbStats_.setFont(magda::FontManager::getInstance().getUIFont(11.0f));
        dbStats_.setColour(juce::Label::textColourId,
                           magda::ActiveTheme::getColour(magda::ActiveTheme::TEXT_SECONDARY));
        dbStats_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(dbStats_);

        wipeButton_.setButtonText("Wipe database...");
        wipeButton_.onClick = [this]() { confirmAndWipe(); };
        addAndMakeVisible(wipeButton_);

        indexPresetsButton_.setButtonText("Index presets");
        indexPresetsButton_.onClick = [this]() { runPresetIndex(); };
        addAndMakeVisible(indexPresetsButton_);

        // The capture buffer's history past its first minute (#3052): about 1.4 GB an hour.
        setupSectionHeader(*this, captureHeader_, "Capture History");
        setupPathRowLabel(*this, captureLocationCaption_, "Folder");
        setupPathTextEditor(*this, captureLocationField_);

        captureBrowse_.setButtonText("Browse...");
        captureBrowse_.onClick = [this]() { browseForCaptureLocation(); };
        addAndMakeVisible(captureBrowse_);

        captureReset_.setButtonText("Reset");
        captureReset_.onClick = [this]() {
            captureDir_.clear();
            refreshCaptureFolder();
        };
        addAndMakeVisible(captureReset_);
        refreshCaptureFolder();
        restyle();
    }

    static int getPreferredHeight(int) {
        return 16 + 3 * 30 + 2 * 22 + 4 * kRowH + kLinkRowH + 2 * kRowH + kRowH + 16;
    }

    void resized() override {
        rows_.clear();
        auto bounds = getLocalBounds().reduced(0, 16);
        const auto buttonW = [](juce::Button& button) {
            return juce::GlyphArrangement::getStringWidthInt(
                       FontManager::getInstance().getUIFont(13.5f), button.getButtonText()) +
                   28;
        };
        // One shape per row: name and note over the path, actions on the right.
        const auto row = [&](Row::Kind kind, int height,
                             std::initializer_list<juce::Button*> buttons) {
            auto r = bounds.removeFromTop(height);
            rows_.push_back({kind, r});
            auto actions = r.reduced(0, (height - 30) / 2);
            for (auto it = std::rbegin(buttons); it != std::rend(buttons); ++it) {
                auto* button = *it;
                if (!button->isVisible())
                    continue;
                button->setBounds(actions.removeFromRight(buttonW(*button)));
                actions.removeFromRight(6);
            }
        };

        dataHeader_.setBounds(bounds.removeFromTop(30));
        row(Row::Kind::Data, kRowH, {&dataReset_, &dataReveal_, &dataBrowse_});
        row(Row::Kind::Presets, kRowH, {&presetsReset_, &presetsReveal_, &presetsBrowse_});
        row(Row::Kind::Capture, kRowH, {&captureReset_, &captureReveal_, &captureBrowse_});
        row(Row::Kind::Render, kLinkRowH, {&renderLink_});

        bounds.removeFromTop(22);
        dbHeader_.setBounds(bounds.removeFromTop(30));
        row(Row::Kind::Database, kRowH, {&dbReset_, &dbReveal_, &dbBrowse_});
        row(Row::Kind::Index, kRowH, {&indexPresetsButton_, &wipeButton_});

        bounds.removeFromTop(22);
        editorHeader_.setBounds(bounds.removeFromTop(30));
        row(Row::Kind::Editor, kRowH, {&editorReset_, &editorBrowse_});
    }

    void paint(juce::Graphics& g) override {
        const auto home =
            juce::File::getSpecialLocation(juce::File::userHomeDirectory).getFullPathName();
        const auto shorten = [&home](const juce::String& path) {
            return path.startsWith(home) ? "~" + path.substring(home.length()) : path;
        };
        for (const auto& r : rows_) {
            g.setColour(ActiveTheme::getBorderColour().withMultipliedAlpha(0.6f));
            g.fillRect(r.bounds.withTop(r.bounds.getBottom() - 1));

            juce::String title, note, path, state;
            bool restart = false;
            switch (r.kind) {
                case Row::Kind::Data:
                    title = tr("preferences.paths.row.data");
                    note = tr("preferences.paths.desc.data");
                    path = dataPath_.empty() ? magda::paths::alwaysOSDefault().getFullPathName()
                                             : juce::String(dataPath_);
                    state = stateText(!dataPath_.empty());
                    restart = dataDirChanged();
                    break;
                case Row::Kind::Presets:
                    title = tr("preferences.paths.row.presets");
                    note = tr("preferences.paths.desc.presets");
                    path = presetsPath_.empty() ? presetsDefaultDir().getFullPathName()
                                                : juce::String(presetsPath_);
                    state = stateText(!presetsPath_.empty());
                    break;
                case Row::Kind::Capture:
                    title = tr("preferences.paths.row.capture");
                    note = tr("preferences.paths.desc.capture");
                    path = captureDir_.empty() ? tr("preferences.paths.system_temp")
                                               : juce::String(captureDir_);
                    state = stateText(!captureDir_.empty());
                    break;
                case Row::Kind::Render:
                    title = tr("preferences.paths.row.render");
                    break;
                case Row::Kind::Database:
                    title = tr("preferences.paths.row.location");
                    path = dbLocationField_.getText();
                    state = stateText(!dbDir_.empty());
                    break;
                case Row::Kind::Index:
                    title =
                        tr("preferences.paths.files_indexed").replace("{0}", juce::String(files_));
                    note = tr("preferences.paths.embeddings")
                               .replace("{0}", juce::String(embeddings_));
                    break;
                case Row::Kind::Editor:
                    title = tr("preferences.paths.row.editor");
                    path = editorPath_.empty() ? tr("preferences.paths.not_set")
                                               : juce::String(editorPath_);
                    break;
            }

            auto text = r.bounds.withTrimmedRight(300);
            const bool twoLines = path.isNotEmpty() || r.kind == Row::Kind::Index;
            auto top =
                twoLines ? text.removeFromTop(r.bounds.getHeight() / 2).withTrimmedTop(4) : text;
            const auto titleFont = FontManager::getInstance().getUIFont(13.0f);
            g.setFont(titleFont);
            g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
            const int titleW = juce::GlyphArrangement::getStringWidthInt(titleFont, title) + 10;
            g.drawText(title, top.removeFromLeft(titleW),
                       twoLines ? juce::Justification::bottomLeft
                                : juce::Justification::centredLeft,
                       true);
            const auto small = FontManager::getInstance().getUIFont(11.5f);
            if (r.kind != Row::Kind::Index && note.isNotEmpty()) {
                g.setFont(small);
                g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
                const int noteW = juce::GlyphArrangement::getStringWidthInt(small, note) + 10;
                g.drawText(note, top.removeFromLeft(noteW), juce::Justification::bottomLeft, true);
            }
            if (restart) {
                const auto badgeText = tr("preferences.paths.restart_to_apply");
                const int w = juce::GlyphArrangement::getStringWidthInt(small, badgeText) + 14;
                const auto badge = top.removeFromLeft(w)
                                       .withTrimmedBottom(1)
                                       .withHeight(18)
                                       .withY(top.getBottom() - 19)
                                       .toFloat();
                const auto amber = ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION);
                g.setColour(amber.withAlpha(0.12f));
                g.fillRoundedRectangle(badge, 3.0f);
                g.setColour(amber);
                g.drawRoundedRectangle(badge.reduced(0.5f), 3.0f, 1.0f);
                g.setFont(small);
                g.drawText(badgeText, badge, juce::Justification::centred, false);
            }
            if (!twoLines)
                continue;
            if (r.kind == Row::Kind::Index) {
                g.setFont(small);
                g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
                g.drawText(note, text.withTrimmedBottom(4), juce::Justification::topLeft, true);
                continue;
            }
            const auto mono = FontManager::getInstance().getMonoFont(12.0f);
            g.setFont(mono);
            g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
            const auto shown = shorten(path);
            const int pathW = juce::jmin(
                text.getWidth() - 70, juce::GlyphArrangement::getStringWidthInt(mono, shown) + 10);
            auto line = text.withTrimmedBottom(4);
            g.drawText(shown, line.removeFromLeft(pathW), juce::Justification::topLeft, true);
            if (state.isNotEmpty()) {
                g.setFont(small);
                g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
                g.drawText(state, line, juce::Justification::topLeft, true);
            }
        }
    }

    enum class Kind { Data, Presets };

    void loadSettings(Config& config) {
        // The originals are what is in use, so a restored default shows as a change.
        originalDataPath_ = Config::getInstance().getDataDir();
        originalPresetsPath_ = Config::getInstance().getPresetsDir();
        dataPath_ = config.getDataDir();
        presetsPath_ = config.getPresetsDir();
        pendingCopyData_ = false;
        pendingCopyPresets_ = false;
        editorPath_ = config.getExternalAudioEditorPath();
        dbDir_ = config.getMediaDbDir();
        captureDir_ = config.getCaptureDir();
        updateDisplay();
        refreshMediaLibrarySettings();
        refreshCaptureFolder();
    }

    // The data and presets folders are committed by the section, after the
    // migration prompts the user answered at Browse time.
    void applySettings(Config& config) {
        config.setExternalAudioEditorPath(editorPath_);
        config.setMediaDbDir(dbDir_);
        config.setCaptureDir(captureDir_);
    }

    void markPathsCommitted() {
        originalDataPath_ = dataPath_;
        originalPresetsPath_ = presetsPath_;
        pendingCopyData_ = false;
        pendingCopyPresets_ = false;
        refreshMediaLibrarySettings();
    }

    bool dataDirChanged() const {
        return dataPath_ != originalDataPath_;
    }
    bool presetsDirChanged() const {
        return presetsPath_ != originalPresetsPath_;
    }
    bool shouldCopyData() const {
        return pendingCopyData_;
    }
    bool shouldCopyPresets() const {
        return pendingCopyPresets_;
    }
    std::string getOriginalDataPath() const {
        return originalDataPath_;
    }
    std::string getNewDataPath() const {
        return dataPath_;
    }
    std::string getOriginalPresetsPath() const {
        return originalPresetsPath_;
    }
    std::string getNewPresetsPath() const {
        return presetsPath_;
    }

  private:
    static juce::File presetsDefaultDir() {
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("MAGDA")
            .getChildFile("Presets");
    }

    // Reveal the configured folder if it exists on disk, falling back to the
    // platform default. If neither exists yet, create the default first so
    // revealToUser has something to point at.
    static void revealConfigured(const std::string& configured, const juce::File& defaultDir) {
        juce::File target = configured.empty() ? defaultDir : juce::File(juce::String(configured));
        if (!target.isDirectory())
            target = defaultDir;
        if (!target.isDirectory())
            target.createDirectory();
        target.revealToUser();
    }

    void pickFolder(Kind kind, const juce::String& title) {
        const juce::File defaultDir =
            (kind == Kind::Data) ? magda::paths::alwaysOSDefault() : presetsDefaultDir();
        const std::string& target = (kind == Kind::Data) ? dataPath_ : presetsPath_;

        // Only pass `initial` to FileChooser when it actually resolves to a
        // real directory. Passing a non-existent path can make NSOpenPanel
        // on macOS silently drop the dialog.
        juce::File initial = target.empty() ? juce::File() : juce::File(juce::String(target));
        if (!initial.isDirectory())
            initial = defaultDir;
        if (!initial.isDirectory())
            initial = juce::File();

        fileChooser_ = initial == juce::File()
                           ? std::make_unique<juce::FileChooser>(title)
                           : std::make_unique<juce::FileChooser>(title, initial);

        juce::Component::SafePointer<PathsPage> self(this);
        fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectDirectories,
                                  [self, kind](const juce::FileChooser& fc) {
                                      auto result = fc.getResult();
                                      if (auto* page = self.getComponent()) {
                                          if (result.exists() && result.isDirectory())
                                              page->onFolderPicked(kind, result);
                                      }
                                  });
    }

    void onFolderPicked(Kind kind, const juce::File& picked) {
        const std::string newPathStr = picked.getFullPathName().toStdString();
        std::string& target = (kind == Kind::Data) ? dataPath_ : presetsPath_;
        const std::string& original =
            (kind == Kind::Data) ? originalDataPath_ : originalPresetsPath_;

        // No-op pick: same as the currently-stored path. Nothing to ask.
        if (newPathStr == target)
            return;

        // If the user has effectively re-selected the original path, just
        // wipe the in-flight change without prompting.
        if (newPathStr == original) {
            target = newPathStr;
            (kind == Kind::Data ? pendingCopyData_ : pendingCopyPresets_) = false;
            updateDisplay();
            return;
        }

        // Resolve from/to for the prompt body. Empty `original` means the
        // user is on the platform default.
        const juce::File defaultDir =
            (kind == Kind::Data) ? magda::paths::alwaysOSDefault() : presetsDefaultDir();
        juce::String oldPath =
            original.empty() ? defaultDir.getFullPathName() : juce::String(original);
        const juce::String& newPath = picked.getFullPathName();

        const juce::String titleKey = (kind == Kind::Data)
                                          ? "preferences.paths.migration.data_title"
                                          : "preferences.paths.migration.presets_title";
        const juce::String messageKey = (kind == Kind::Data)
                                            ? "preferences.paths.migration.data_message"
                                            : "preferences.paths.migration.presets_message";

        juce::Component::SafePointer<PathsPage> self(this);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::QuestionIcon)
                .withTitle(tr(titleKey).replace(
                    "{0}", magda::technicalText(magda::TechnicalTextToken::Magda)))
                .withMessage(
                    tr(messageKey)
                        .replace("{0}", oldPath)
                        .replace("{1}", newPath)
                        .replace("{2}", magda::technicalText(magda::TechnicalTextToken::Magda)))
                .withButton(tr("preferences.paths.migration.button.copy"))
                .withButton(tr("preferences.paths.migration.button.dont_copy"))
                .withButton(tr("preferences.paths.migration.button.cancel")),
            [self, kind, newPathStr](int result) {
                auto* page = self.getComponent();
                if (page == nullptr)
                    return;
                // JUCE 3-button mapping: 1=first ("Copy"), 2=second
                // ("Don't copy"), 0=third/escape ("Cancel").
                if (result == 0)
                    return;  // Cancel: leave configured path untouched.
                page->setPendingPath(kind, newPathStr, /*copyFiles=*/result == 1);
            });
    }

    void setPendingPath(Kind kind, const std::string& newPath, bool copyFiles) {
        if (kind == Kind::Data) {
            dataPath_ = newPath;
            pendingCopyData_ = copyFiles;
        } else {
            presetsPath_ = newPath;
            pendingCopyPresets_ = copyFiles;
        }
        updateDisplay();
    }

    void updateDisplay() {
        showStates();
        if (dataPath_.empty()) {
            dataValue_.setText("default: " + magda::paths::alwaysOSDefault().getFullPathName(),
                               juce::dontSendNotification);
        } else {
            dataValue_.setText(juce::String(dataPath_), juce::dontSendNotification);
        }

        if (presetsPath_.empty()) {
            auto def = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                           .getChildFile("MAGDA")
                           .getChildFile("Presets");
            presetsValue_.setText("default: " + def.getFullPathName(), juce::dontSendNotification);
        } else {
            presetsValue_.setText(juce::String(presetsPath_), juce::dontSendNotification);
        }
    }

    void refreshMediaLibrarySettings() {
        auto& ctx = magda::media::MediaDbContext::getInstance();

        const auto editorPath = juce::String(editorPath_);
        editorLocationField_.setText(editorPath.isEmpty() ? juce::String("Not configured")
                                                          : editorPath,
                                     juce::dontSendNotification);

        dbLocationField_.setText(dbDir_.empty() ? juce::String(ctx.dbPath().parent_path().string())
                                                : juce::String(dbDir_),
                                 juce::dontSendNotification);

        int files = 0;
        int embeddings = 0;
        if (ctx.ensureInitialized()) {
            magda::media::MediaDbQuery probe(ctx.db(), nullptr, nullptr);
            files = probe.totalFiles();
            embeddings = probe.totalEmbeddings();
        }
        files_ = files;
        embeddings_ = embeddings;
        showStates();
        wipeButton_.setEnabled(files > 0);
    }

    void refreshCaptureFolder() {
        showStates();
        const juce::String folder(captureDir_);
        captureLocationField_.setText(folder.isEmpty() ? juce::String("System temp folder")
                                                       : folder,
                                      juce::dontSendNotification);
    }

    void browseForCaptureLocation() {
        const juce::File current(juce::File::isAbsolutePath(captureDir_) ? juce::String(captureDir_)
                                                                         : juce::String());
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Choose a folder for the capture history",
            current.isDirectory() ? current
                                  : juce::File::getSpecialLocation(juce::File::userHomeDirectory));

        juce::Component::SafePointer<PathsPage> self(this);
        fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectDirectories,
                                  [self](const juce::FileChooser& fc) {
                                      const auto picked = fc.getResult();
                                      auto* page = self.getComponent();
                                      if (!picked.isDirectory() || page == nullptr)
                                          return;
                                      page->captureDir_ = picked.getFullPathName().toStdString();
                                      page->refreshCaptureFolder();
                                  });
    }

    void browseForDbLocation() {
        const auto currentDir = juce::File(juce::String(
            magda::media::MediaDbContext::getInstance().dbPath().parent_path().string()));
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Choose a folder for the media database",
            currentDir.exists() ? currentDir
                                : juce::File::getSpecialLocation(juce::File::userHomeDirectory));

        juce::Component::SafePointer<PathsPage> self(this);
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [self](const juce::FileChooser& fc) {
                const auto picked = fc.getResult();
                if (auto* page = self.getComponent(); page != nullptr && picked.isDirectory()) {
                    page->dbDir_ = picked.getFullPathName().toStdString();
                    page->refreshMediaLibrarySettings();
                }
            });
    }

    void browseForExternalEditor() {
        const auto configured = juce::String(editorPath_);
        juce::File initial = configured.isNotEmpty()
                                 ? juce::File(configured)
                                 : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
        if (!initial.exists()) {
            initial = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
        }

        fileChooser_ =
            std::make_unique<juce::FileChooser>("Choose an external audio editor", initial);
        auto chooserFlags =
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
#if JUCE_MAC
        chooserFlags |= juce::FileBrowserComponent::canSelectDirectories;
#endif

        juce::Component::SafePointer<PathsPage> self(this);
        fileChooser_->launchAsync(chooserFlags, [self](const juce::FileChooser& fc) {
            const auto picked = fc.getResult();
            if (!picked.exists()) {
                return;
            }
#if JUCE_MAC
            if (!picked.existsAsFile() && !picked.isBundle()) {
                return;
            }
#else
            if (!picked.existsAsFile()) {
                return;
            }
#endif
            if (auto* page = self.getComponent()) {
                page->editorPath_ = picked.getFullPathName().toStdString();
                page->refreshMediaLibrarySettings();
            }
        });
    }

    void runPresetIndex() {
        auto& ctx = magda::media::MediaDbContext::getInstance();
        if (!ctx.ensureInitialized()) {
            return;
        }
        const auto presetsRoot =
            std::filesystem::path(magda::paths::presetsDir().getFullPathName().toStdString());

        indexPresetsButton_.setEnabled(false);
        indexPresetsButton_.setButtonText("Indexing presets...");

        const juce::Component::SafePointer<PathsPage> self(this);
        juce::Thread::launch([self, presetsRoot]() {
            auto& ctx = magda::media::MediaDbContext::getInstance();
            magda::media::PresetDbIndexer indexer(ctx.db());
            const auto stats = indexer.indexAll(presetsRoot);
            ctx.bumpMediaRevision();
            juce::MessageManager::callAsync([self, stats]() {
                if (auto* page = self.getComponent()) {
                    page->indexPresetsButton_.setEnabled(true);
                    page->indexPresetsButton_.setButtonText("Index presets");
                    page->refreshMediaLibrarySettings();
                    juce::Logger::writeToLog(juce::String("[indexPresets] inserted=") +
                                             juce::String(stats.inserted) +
                                             " updated=" + juce::String(stats.updated) +
                                             " skipped=" + juce::String(stats.skipped) +
                                             " failed=" + juce::String(stats.failed));
                }
            });
        });
    }

    void confirmAndWipe() {
        const juce::Component::SafePointer<PathsPage> self(this);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions{}
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Wipe media database")
                .withMessage("This deletes every indexed file, embedding, and tag from the media "
                             "database.\nYour audio files on disk are untouched.\n\nContinue?")
                .withButton("Wipe")
                .withButton("Cancel"),
            [self](int result) {
                if (auto* page = self.getComponent(); page != nullptr && result == 1) {
                    magda::media::MediaDbContext::getInstance().wipeAll();
                    page->refreshMediaLibrarySettings();
                }
            });
    }

    juce::Label dataHeader_, dataLabel_, dataValue_, dataNote_;
    juce::TextButton dataBrowse_, dataReveal_, dataReset_;
    std::string dataPath_;
    std::string originalDataPath_;
    bool pendingCopyData_ = false;

    juce::Label presetsHeader_, presetsLabel_, presetsValue_, presetsNote_;
    juce::TextButton presetsBrowse_, presetsReveal_, presetsReset_;
    std::string presetsPath_;
    std::string originalPresetsPath_;
    bool pendingCopyPresets_ = false;

    juce::Label renderHint_;

    std::string editorPath_;
    std::string dbDir_;
    std::string captureDir_;

    juce::Label editorHeader_;
    juce::Label editorLocationCaption_;
    juce::TextEditor editorLocationField_;
    juce::TextButton editorBrowse_;
    juce::TextButton editorReset_;

    juce::Label dbHeader_;
    juce::Label dbLocationCaption_;
    juce::TextEditor dbLocationField_;
    juce::TextButton dbBrowse_;
    juce::TextButton dbReset_;
    juce::Label dbStats_;
    juce::TextButton wipeButton_;
    juce::TextButton indexPresetsButton_;

    juce::Label captureHeader_;
    juce::Label captureLocationCaption_;
    juce::TextEditor captureLocationField_;
    juce::TextButton captureBrowse_;
    juce::TextButton captureReset_;

    std::unique_ptr<juce::FileChooser> fileChooser_;

    struct Row {
        enum class Kind { Data, Presets, Capture, Render, Database, Index, Editor };
        Kind kind;
        juce::Rectangle<int> bounds;
    };
    static constexpr int kRowH = 54;
    static constexpr int kLinkRowH = 40;
    std::vector<Row> rows_;
    juce::TextButton captureReveal_, dbReveal_, renderLink_;
    int files_ = 0;
    int embeddings_ = 0;

    static juce::String stateText(bool custom) {
        return tr(custom ? "preferences.paths.custom" : "preferences.paths.default");
    }

    // The old one-line captions and notes; the rows now paint their own text.
    void restyle() {
        for (juce::Component* c : std::initializer_list<juce::Component*>{
                 &dataLabel_, &dataValue_, &dataNote_, &presetsHeader_, &presetsLabel_,
                 &presetsValue_, &presetsNote_, &renderHint_, &editorLocationCaption_,
                 &editorLocationField_, &dbLocationCaption_, &dbLocationField_, &dbStats_,
                 &captureHeader_, &captureLocationCaption_, &captureLocationField_})
            c->setVisible(false);

        dataHeader_.setText(tr("preferences.paths.section.folders"), juce::dontSendNotification);
        dbHeader_.setText(tr("preferences.paths.section.media_db"), juce::dontSendNotification);
        editorHeader_.setText(tr("preferences.paths.section.apps"), juce::dontSendNotification);

        for (auto* reveal : {&dataReveal_, &presetsReveal_, &captureReveal_, &dbReveal_})
            reveal->setButtonText(tr("preferences.button.reveal"));
        for (auto* change : {&dataBrowse_, &presetsBrowse_, &captureBrowse_, &dbBrowse_})
            change->setButtonText(trEllipsis("preferences.button.change"));
        for (auto* reset : {&dataReset_, &presetsReset_, &captureReset_, &dbReset_}) {
            reset->setButtonText(tr("preferences.button.use_default"));
            magda::daw::ui::SettingsLookAndFeel::markLinkButton(*reset);
        }
        editorBrowse_.setButtonText(trEllipsis("preferences.button.choose_app"));
        editorReset_.setButtonText(tr("preferences.button.remove"));
        magda::daw::ui::SettingsLookAndFeel::markLinkButton(editorReset_);
        wipeButton_.setButtonText(trEllipsis("preferences.button.wipe_database"));
        MagdaAlertWindow::setRole(wipeButton_, MagdaAlertWindow::ButtonRole::Destructive);
        indexPresetsButton_.setButtonText(tr("preferences.button.index_presets"));

        captureReveal_.onClick = [this] {
            revealConfigured(captureDir_,
                             juce::File::getSpecialLocation(juce::File::tempDirectory));
        };
        addAndMakeVisible(captureReveal_);
        dbReveal_.onClick = [this] { juce::File(dbLocationField_.getText()).revealToUser(); };
        addAndMakeVisible(dbReveal_);

        renderLink_.setButtonText(tr("preferences.paths.set_in_rendering"));
        magda::daw::ui::SettingsLookAndFeel::markLinkButton(renderLink_);
        renderLink_.onClick = [] { openSettings(SettingsSectionId::Rendering); };
        addAndMakeVisible(renderLink_);
    }

    // Use default only where there is something to reset; refresh the painted rows.
    void showStates() {
        dataReset_.setVisible(!dataPath_.empty());
        presetsReset_.setVisible(!presetsPath_.empty());
        captureReset_.setVisible(!captureDir_.empty());
        dbReset_.setVisible(!dbDir_.empty());
        editorReset_.setVisible(!editorPath_.empty());
        resized();
        repaint();
    }
};

// ---- Shortcuts tab ---------------------------------------------------------

class ShortcutsPage {
  public:
    explicit ShortcutsPage(juce::ApplicationCommandManager* commandManager)
        : keyboardPage_(std::make_unique<KeyboardShortcutsPage>(commandManager)),
          gesturesPage_(std::make_unique<GestureBindingsPage>()) {}

    juce::Component& getKeyboardPage() {
        return *keyboardPage_;
    }
    juce::Component& getGesturesPage() {
        return *gesturesPage_;
    }
    juce::Component& getKeyboardToolbar() {
        return keyboardPage_->getToolbar();
    }
    juce::Component& getGesturesToolbar() {
        return gesturesPage_->getToolbar();
    }

    void commit() {
        keyboardPage_->commit();
        GestureRouter::getInstance().loadFromConfig();
    }

    void loadSettings(Config& config) {
        keyboardPage_->loadSettings(config);
        gesturesPage_->loadSettings(config);
    }

    void applySettings(Config& config) {
        keyboardPage_->applySettings(config);
        gesturesPage_->applySettings(config);
    }

  private:
    /**
     * @brief Every bindable command by category, its keys drawn as caps (#3162).
     *
     * Click a cap to re-record it, "Not set" to add one, right-click a cap to clear
     * it. A key two commands share is shown on both in amber.
     */
    class KeyList : public juce::Component, private juce::ChangeListener {
      public:
        KeyList(juce::ApplicationCommandManager& commands, juce::KeyPressMappingSet& mappings)
            : commands_(commands), mappings_(mappings) {
            setWantsKeyboardFocus(true);
            mappings_.addChangeListener(this);
            rebuild();
        }

        ~KeyList() override {
            mappings_.removeChangeListener(this);
        }

        void setFilter(const juce::String& text, const juce::KeyPress& key) {
            filterText_ = text;
            filterKey_ = key;
            rebuild();
        }

        int getContentHeight() const {
            return static_cast<int>(rows_.size()) * kRowH;
        }

        void paint(juce::Graphics& g) override {
            const auto font = FontManager::getInstance().getUIFont(13.0f);
            const auto capFont = FontManager::getInstance().getMonoFont(12.0f);
            caps_.clear();
            int y = 0;
            for (const auto& row : rows_) {
                auto r = juce::Rectangle<int>(0, y, getWidth(), kRowH);
                y += kRowH;
                g.setColour(ActiveTheme::getBorderColour().withMultipliedAlpha(0.5f));
                g.fillRect(r.removeFromBottom(1));

                if (row.command == 0) {
                    const bool open = !collapsed_.contains(row.category);
                    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
                    g.setFont(FontManager::getInstance().getUIFont(10.0f));
                    g.drawText(open ? juce::String(juce::CharPointer_UTF8("\xe2\x96\xbe"))
                                    : juce::String(juce::CharPointer_UTF8("\xe2\x96\xb8")),
                               r.removeFromLeft(18), juce::Justification::centred);
                    g.setFont(FontManager::getInstance().getUIFontMedium(13.0f));
                    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
                    const int w =
                        juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), row.category);
                    g.drawText(row.category, r.removeFromLeft(w + 8),
                               juce::Justification::centredLeft);
                    g.setFont(FontManager::getInstance().getUIFont(11.0f));
                    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
                    g.drawText(juce::String(row.count), r, juce::Justification::centredLeft);
                    continue;
                }

                g.setFont(font);
                g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
                g.drawText(commands_.getNameOfCommand(row.command), r.withTrimmedLeft(18),
                           juce::Justification::centredLeft, true);

                auto right = r.reduced(0, 6);
                if (recording_ == row.command) {
                    cancelArea_ = right.removeFromRight(20);
                    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
                    g.drawText(juce::String(juce::CharPointer_UTF8("\xc3\x97")), cancelArea_,
                               juce::Justification::centred);
                    right.removeFromRight(6);
                    const auto prompt = tr("preferences.shortcuts.recording");
                    const int w = juce::GlyphArrangement::getStringWidthInt(capFont, prompt) + 20;
                    const auto box = right.removeFromRight(w).toFloat();
                    g.setColour(ActiveTheme::getAccentColour().withAlpha(0.12f));
                    g.fillRoundedRectangle(box, 4.0f);
                    g.setColour(ActiveTheme::getAccentColour());
                    g.drawRoundedRectangle(box.reduced(0.5f), 4.0f, 1.0f);
                    g.setFont(capFont);
                    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
                    g.drawText(prompt, box, juce::Justification::centred);
                    continue;
                }

                const auto keys = mappings_.getKeyPressesAssignedToCommand(row.command);
                if (keys.isEmpty()) {
                    const auto text = tr("preferences.shortcuts.not_set");
                    const int w = juce::GlyphArrangement::getStringWidthInt(font, text) + 8;
                    const auto area = right.removeFromRight(w);
                    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
                    g.setFont(FontManager::getInstance().getUIFont(12.0f));
                    g.drawText(text, area, juce::Justification::centredRight);
                    caps_.push_back({row.command, -1, area});
                    continue;
                }
                juce::String conflict;
                for (int k = keys.size(); --k >= 0;) {
                    const auto text = keys[k].getTextDescriptionWithIcons();
                    const int w = juce::GlyphArrangement::getStringWidthInt(capFont, text) + 16;
                    const auto cap = right.removeFromRight(w);
                    right.removeFromRight(6);
                    const auto other = otherCommandFor(keys[k], row.command);
                    if (other != 0)
                        conflict = commands_.getNameOfCommand(other);
                    const auto colour = other != 0
                                            ? ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION)
                                            : ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY);
                    g.setColour(ActiveTheme::getBackgroundColour());
                    g.fillRoundedRectangle(cap.toFloat(), 4.0f);
                    g.setColour(other != 0 ? colour
                                           : ActiveTheme::getBorderColour().brighter(0.2f));
                    g.drawRoundedRectangle(cap.toFloat().reduced(0.5f), 4.0f, 1.0f);
                    g.setColour(colour);
                    g.setFont(capFont);
                    g.drawText(text, cap, juce::Justification::centred);
                    caps_.push_back({row.command, k, cap});
                }
                if (conflict.isNotEmpty()) {
                    g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_ATTENTION));
                    g.setFont(FontManager::getInstance().getUIFont(12.0f));
                    g.drawText(tr("preferences.shortcuts.also_used_by").replace("{0}", conflict),
                               right.removeFromRight(260), juce::Justification::centredRight, true);
                }
            }
        }

        void mouseUp(const juce::MouseEvent& e) override {
            grabKeyboardFocus();
            if (recording_ != 0 && cancelArea_.contains(e.getPosition())) {
                stopRecording();
                return;
            }
            for (const auto& cap : caps_) {
                if (!cap.bounds.contains(e.getPosition()))
                    continue;
                if (e.mods.isPopupMenu() && cap.index >= 0) {
                    mappings_.removeKeyPress(cap.command, cap.index);
                    return;
                }
                recording_ = cap.command;
                replacing_ = cap.index;
                repaint();
                return;
            }
            const int index = e.getPosition().y / kRowH;
            if (juce::isPositiveAndBelow(index, static_cast<int>(rows_.size())) &&
                rows_[static_cast<size_t>(index)].command == 0) {
                const auto& category = rows_[static_cast<size_t>(index)].category;
                if (collapsed_.contains(category))
                    collapsed_.removeString(category);
                else
                    collapsed_.add(category);
                rebuild();
            }
        }

        bool keyPressed(const juce::KeyPress& key) override {
            if (recording_ == 0)
                return false;
            if (key == juce::KeyPress::escapeKey) {
                stopRecording();
                return true;
            }
            if (replacing_ >= 0)
                mappings_.removeKeyPress(recording_, replacing_);
            mappings_.addKeyPress(recording_, key);
            stopRecording();
            return true;
        }

      private:
        static constexpr int kRowH = 32;

        struct Row {
            juce::CommandID command;  // 0 for a category header
            juce::String category;
            int count = 0;
        };
        struct Cap {
            juce::CommandID command;
            int index;  // -1 for "Not set"
            juce::Rectangle<int> bounds;
        };

        void changeListenerCallback(juce::ChangeBroadcaster*) override {
            rebuild();
        }

        void stopRecording() {
            recording_ = 0;
            replacing_ = -1;
            repaint();
        }

        juce::CommandID otherCommandFor(const juce::KeyPress& key, juce::CommandID self) const {
            for (const auto& category : commands_.getCommandCategories())
                for (const auto id : commands_.getCommandsInCategory(category))
                    if (id != self && mappings_.getKeyPressesAssignedToCommand(id).contains(key))
                        return id;
            return 0;
        }

        bool matches(juce::CommandID id) const {
            if (filterKey_.isValid())
                return mappings_.getKeyPressesAssignedToCommand(id).contains(filterKey_);
            return filterText_.isEmpty() ||
                   commands_.getNameOfCommand(id).containsIgnoreCase(filterText_);
        }

        void rebuild() {
            rows_.clear();
            for (const auto& category : commands_.getCommandCategories()) {
                std::vector<juce::CommandID> ids;
                for (const auto id : commands_.getCommandsInCategory(category)) {
                    const auto* info = commands_.getCommandForID(id);
                    if (info != nullptr &&
                        (info->flags & juce::ApplicationCommandInfo::hiddenFromKeyEditor) == 0 &&
                        matches(id))
                        ids.push_back(id);
                }
                if (ids.empty())
                    continue;
                rows_.push_back({0, category, static_cast<int>(ids.size())});
                if (collapsed_.contains(category) && filterText_.isEmpty() && !filterKey_.isValid())
                    continue;
                for (const auto id : ids)
                    rows_.push_back({id, category});
            }
            setSize(getWidth(), getContentHeight());
            repaint();
        }

        juce::ApplicationCommandManager& commands_;
        juce::KeyPressMappingSet& mappings_;
        std::vector<Row> rows_;
        std::vector<Cap> caps_;
        juce::StringArray collapsed_;
        juce::String filterText_;
        juce::KeyPress filterKey_;
        juce::CommandID recording_ = 0;
        int replacing_ = -1;
        juce::Rectangle<int> cancelArea_;
    };

    /// Typing filters by name; a shortcut pressed here filters by key instead.
    class KeyFilter : public juce::TextEditor {
      public:
        std::function<void(const juce::KeyPress&)> onKey;

        bool keyPressed(const juce::KeyPress& key) override {
            const auto mods = key.getModifiers();
            if ((mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown()) &&
                key != juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0)) {
                setText(key.getTextDescriptionWithIcons(), juce::dontSendNotification);
                if (onKey)
                    onKey(key);
                return true;
            }
            return juce::TextEditor::keyPressed(key);
        }
    };

    class KeyboardShortcutsPage : public juce::Component {
      public:
        explicit KeyboardShortcutsPage(juce::ApplicationCommandManager* commandManager) {
            if (commandManager != nullptr) {
                // Edits go to a copy, so they wait for Apply like every other setting.
                live_ = commandManager->getKeyMappings();
                mappings_ = std::make_unique<juce::KeyPressMappingSet>(*live_);
                list_ = std::make_unique<KeyList>(*commandManager, *mappings_);
                viewport_.setViewedComponent(list_.get(), false);
                viewport_.setScrollBarsShown(true, false);
                addAndMakeVisible(viewport_);

                filter_.setTextToShowWhenEmpty(tr("preferences.shortcuts.filter"),
                                               ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
                filter_.setFont(FontManager::getInstance().getUIFont(12.5f));
                filter_.setIndents(8, 6);
                filter_.onTextChange = [this] { list_->setFilter(filter_.getText(), {}); };
                filter_.onKey = [this](const juce::KeyPress& key) { list_->setFilter({}, key); };
                toolbar_.addAndMakeVisible(filter_);

                resetButton_.setButtonText(tr("preferences.shortcuts.reset_all"));
                resetButton_.onClick = [this] { mappings_->resetToDefaultMappings(); };
                toolbar_.addAndMakeVisible(resetButton_);
                toolbar_.onResize = [this] {
                    auto bar = toolbar_.getLocalBounds();
                    resetButton_.setBounds(
                        bar.removeFromRight(juce::GlyphArrangement::getStringWidthInt(
                                                FontManager::getInstance().getUIFont(13.5f),
                                                resetButton_.getButtonText()) +
                                            28));
                    bar.removeFromRight(8);
                    filter_.setBounds(bar.withTrimmedLeft(juce::jmax(0, bar.getWidth() - 420)));
                };
            } else {
                missingLabel_.setText("Keyboard shortcuts are available from the main window.",
                                      juce::dontSendNotification);
                missingLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
                missingLabel_.setColour(juce::Label::textColourId,
                                        ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
                missingLabel_.setJustificationType(juce::Justification::centredLeft);
                addAndMakeVisible(missingLabel_);
            }
        }

        juce::Component& getToolbar() {
            return toolbar_;
        }

        void resized() override {
            auto bounds = getLocalBounds().reduced(0, 8);
            if (list_ == nullptr) {
                missingLabel_.setBounds(bounds.removeFromTop(28));
                return;
            }
            viewport_.setBounds(bounds);
            list_->setSize(viewport_.getMaximumVisibleWidth(), list_->getContentHeight());
        }

        void loadSettings(Config& config) {
            if (mappings_ == nullptr)
                return;
            mappings_->resetToDefaultMappings();
            if (auto xml = juce::parseXML(config.getKeyboardBindings().toString()))
                mappings_->restoreFromXml(*xml);
        }

        void applySettings(Config& config) {
            if (mappings_ == nullptr)
                return;
            // The same diff-from-defaults form KeyMappingStore saves.
            if (auto xml = mappings_->createXml(true))
                config.setKeyboardBindings(xml->toString());
            else
                config.setKeyboardBindings(juce::var());
        }

        void commit() {
            if (mappings_ != nullptr)
                if (auto xml = mappings_->createXml(true))
                    live_->restoreFromXml(*xml);
        }

      private:
        /// A holder that lays out its children when the window resizes it.
        class Toolbar : public juce::Component {
          public:
            std::function<void()> onResize;
            void resized() override {
                if (onResize)
                    onResize();
            }
        };

        juce::Label missingLabel_;
        juce::KeyPressMappingSet* live_ = nullptr;
        std::unique_ptr<juce::KeyPressMappingSet> mappings_;
        std::unique_ptr<KeyList> list_;
        juce::Viewport viewport_;
        Toolbar toolbar_;
        KeyFilter filter_;
        juce::TextButton resetButton_;
    };

    class GestureBindingsPage : public juce::Component {
      public:
        GestureBindingsPage() {
            resetButton_.setButtonText(tr("preferences.shortcuts.reset_all"));
            resetButton_.onClick = [this] {
                capturingRow_ = nullptr;
                loadFromRouter(*GestureRouter::createDetached());
            };
            toolbar_.addButton(resetButton_);

            viewport_.setViewedComponent(&content_, false);
            viewport_.setScrollBarsShown(true, false);
            viewport_.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
            content_.setViewportIgnoreDragFlag(true);
            addAndMakeVisible(viewport_);
            setWantsKeyboardFocus(true);
            addMouseListener(this, true);

            for (auto context : contexts()) {
                for (auto axis : {GestureAxis::Vertical, GestureAxis::Horizontal}) {
                    for (auto mods : modifiers())
                        addGestureRow(context,
                                      {GestureInputKind::Wheel, GestureArea::Main, axis, mods});
                }

                if (context == GestureContext::EqCurveEditor) {
                    for (auto axis : {GestureAxis::Vertical, GestureAxis::Horizontal}) {
                        for (auto mods : modifiers()) {
                            addGestureRow(context, {GestureInputKind::SmoothWheel,
                                                    GestureArea::Main, axis, mods});
                        }
                    }
                }

                if (hasRulerWheelBindings(context)) {
                    for (auto axis : {GestureAxis::Vertical, GestureAxis::Horizontal}) {
                        for (auto mods : modifiers()) {
                            addGestureRow(
                                context, {GestureInputKind::Wheel, GestureArea::Ruler, axis, mods});
                        }
                    }
                }

                for (const auto& row : dragRows()) {
                    if (row.context == context)
                        addGestureRow(row.context, row.input);
                }
            }
        }

        juce::Component& getToolbar() {
            return toolbar_;
        }

        void resized() override {
            auto bounds = getLocalBounds().reduced(0, 8);
            columnHeader_ = bounds.removeFromTop(28);
            viewport_.setBounds(bounds);
            layoutRows(viewport_.getMaximumVisibleWidth());
        }

        void paint(juce::Graphics& g) override {
            const auto columns =
                columnsFor(columnHeader_.withTrimmedRight(viewport_.getScrollBarThickness()));
            g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
            g.setFont(
                FontManager::getInstance().getUIFontMedium(11.0f).withExtraKerningFactor(0.08f));
            const char* titles[] = {"preferences.gestures.action", "preferences.gestures.input",
                                    "preferences.gestures.sensitivity",
                                    "preferences.gestures.invert"};
            for (size_t i = 0; i < 4; ++i)
                g.drawText(tr(titles[i]).toUpperCase(), columns[i],
                           i == 2 ? juce::Justification::centredRight
                                  : juce::Justification::centredLeft,
                           false);
            g.setColour(ActiveTheme::getBorderColour());
            g.fillRect(columnHeader_.withTop(columnHeader_.getBottom() - 1));
        }

        void mouseMove(const juce::MouseEvent& e) override {
            const auto y = e.getEventRelativeTo(&content_).getPosition().y;
            GestureRow* hovered = nullptr;
            for (auto& row : rows_)
                if (row->contextLabel.isVisible() && row->contextLabel.getY() <= y &&
                    y < row->contextLabel.getBottom())
                    hovered = row.get();
            if (hovered != hoveredRow_) {
                hoveredRow_ = hovered;
                updateLearnButtons();
            }
        }

        void mouseExit(const juce::MouseEvent& e) override {
            if (!getLocalBounds().contains(e.getEventRelativeTo(this).getPosition())) {
                hoveredRow_ = nullptr;
                updateLearnButtons();
            }
        }

        void mouseWheelMove(const juce::MouseEvent& e,
                            const juce::MouseWheelDetails& wheel) override {
            // This runs as a mouse listener for every child (addMouseListener
            // (this, true)) so Learn can capture a wheel over any row widget —
            // including the sensitivity slider, which eats the wheel before it
            // reaches the list viewport. JUCE dispatches listener callbacks
            // AFTER the target's own handling, so the inner list viewport has
            // already captured-or-scrolled by now. Never fall through to the
            // base handler: it would bubble this same event to the outer
            // preferences viewport and scroll the whole page on top of that.
            handleCaptureWheel(e, wheel);
        }

        bool handleCaptureWheel(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
            if (capturingRow_ == nullptr)
                return false;

            const GestureAxis axis = std::abs(wheel.deltaX) > std::abs(wheel.deltaY)
                                         ? GestureAxis::Horizontal
                                         : GestureAxis::Vertical;
            const auto kind =
                wheel.isSmooth ? GestureInputKind::SmoothWheel : GestureInputKind::Wheel;
            setRowInput(*capturingRow_,
                        {kind, GestureArea::Main, axis, gestureModifierMaskFrom(e.mods)});
            setCapturingRow(nullptr);
            return true;
        }

        void mouseDown(const juce::MouseEvent& e) override {
            if (capturingRow_ == nullptr)
                return;

            captureDragStart_ = e.getPosition();
        }

        void mouseDrag(const juce::MouseEvent& e) override {
            if (capturingRow_ == nullptr)
                return;

            const auto delta = e.getPosition() - captureDragStart_;
            if (std::max(std::abs(delta.x), std::abs(delta.y)) <= 3)
                return;

            const auto area = learnedDragAreaFor(*capturingRow_);
            setRowInput(*capturingRow_, {GestureInputKind::Drag, area, GestureAxis::Vertical,
                                         gestureModifierMaskFrom(e.mods)});
            setCapturingRow(nullptr);
        }

        bool keyPressed(const juce::KeyPress& key) override {
            if (capturingRow_ != nullptr && key == juce::KeyPress::escapeKey) {
                setCapturingRow(nullptr);
                return true;
            }

            return false;
        }

        void loadSettings(Config& config) {
            loadFromRouter(*routerFor(config));
        }

        void applySettings(Config& config) {
            const auto edited = routerFor(config);
            auto& router = *edited;

            for (const auto& row : rows_) {
                if (!row->contextLabel.isVisible())
                    continue;

                if (sameInput(row->currentInput, row->input))
                    continue;

                if (row->defaultBinding.action == GestureActionType::None) {
                    router.clearBinding(row->context, row->input);
                } else {
                    router.setBinding(row->context, row->input,
                                      {GestureActionType::None, row->defaultBinding.sensitivity,
                                       row->defaultBinding.invert});
                }
            }

            for (const auto& row : rows_) {
                if (!row->contextLabel.isVisible())
                    continue;

                router.setBinding(row->context, row->currentInput,
                                  {row->action, static_cast<float>(row->sensitivity.getValue()),
                                   row->invert.getToggleState()});
            }

            config.setGestureBindings(router.toVar());
        }

        static std::unique_ptr<GestureRouter> routerFor(const Config& config) {
            auto router = GestureRouter::createDetached();
            router->loadFromVar(config.getGestureBindings());
            return router;
        }

      private:
        class CaptureViewport : public juce::Viewport {
          public:
            explicit CaptureViewport(GestureBindingsPage& owner) : owner_(owner) {}

            void mouseWheelMove(const juce::MouseEvent& e,
                                const juce::MouseWheelDetails& wheel) override {
                if (owner_.handleCaptureWheel(e, wheel))
                    return;
                juce::Viewport::mouseWheelMove(e, wheel);
            }

          private:
            GestureBindingsPage& owner_;
        };

        struct GestureRow {
            GestureContext context = GestureContext::Unknown;
            GestureInput input;
            GestureInput currentInput;
            GestureActionType action = GestureActionType::None;
            GestureBinding defaultBinding;
            juce::Label contextLabel;
            juce::Label axisLabel;
            juce::Label inputLabel;
            juce::TextButton learnButton;
            magda::daw::ui::TextSlider sensitivity;
            juce::ToggleButton invert;
        };

        struct DragRowSpec {
            GestureContext context;
            GestureInput input;
        };

        static std::array<GestureContext, 9> contexts() {
            return {GestureContext::Arrangement,  GestureContext::PianoRoll,
                    GestureContext::DrumGrid,     GestureContext::Waveform,
                    GestureContext::CurveEditor,  GestureContext::Chain,
                    GestureContext::Sampler,      GestureContext::StepSequencer,
                    GestureContext::EqCurveEditor};
        }

        static std::array<uint8_t, 8> modifiers() {
            return {GestureMod_None,
                    GestureMod_Shift,
                    GestureMod_Command,
                    GestureMod_Alt,
                    static_cast<uint8_t>(GestureMod_Shift | GestureMod_Command),
                    static_cast<uint8_t>(GestureMod_Shift | GestureMod_Alt),
                    static_cast<uint8_t>(GestureMod_Command | GestureMod_Alt),
                    static_cast<uint8_t>(GestureMod_Shift | GestureMod_Command | GestureMod_Alt)};
        }

        static std::array<DragRowSpec, 12> dragRows() {
            return {
                DragRowSpec{GestureContext::Arrangement,
                            {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical,
                             GestureMod_None}},
                DragRowSpec{GestureContext::Arrangement,
                            {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical,
                             GestureMod_Shift}},
                DragRowSpec{GestureContext::Arrangement,
                            {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical,
                             GestureMod_Alt}},
                DragRowSpec{GestureContext::Arrangement,
                            {GestureInputKind::Drag, GestureArea::Body, GestureAxis::Vertical,
                             GestureMod_Alt}},
                DragRowSpec{GestureContext::PianoRoll,
                            {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical,
                             GestureMod_None}},
                DragRowSpec{GestureContext::PianoRoll,
                            {GestureInputKind::Drag, GestureArea::ZoomStrip, GestureAxis::Vertical,
                             GestureMod_None}},
                DragRowSpec{GestureContext::PianoRoll,
                            {GestureInputKind::Drag, GestureArea::Keyboard, GestureAxis::Horizontal,
                             GestureMod_None}},
                DragRowSpec{GestureContext::DrumGrid,
                            {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical,
                             GestureMod_None}},
                DragRowSpec{GestureContext::DrumGrid,
                            {GestureInputKind::Drag, GestureArea::ZoomStrip, GestureAxis::Vertical,
                             GestureMod_None}},
                DragRowSpec{GestureContext::Waveform,
                            {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical,
                             GestureMod_None}},
                DragRowSpec{GestureContext::Waveform,
                            {GestureInputKind::Drag, GestureArea::Header, GestureAxis::Vertical,
                             GestureMod_None}},
                DragRowSpec{GestureContext::Waveform,
                            {GestureInputKind::Drag, GestureArea::Body, GestureAxis::Vertical,
                             GestureMod_None}},
            };
        }

        static bool hasRulerWheelBindings(GestureContext context) {
            return context == GestureContext::PianoRoll || context == GestureContext::DrumGrid ||
                   context == GestureContext::CurveEditor || context == GestureContext::Waveform;
        }

        static juce::String contextName(GestureContext context) {
            switch (context) {
                case GestureContext::Arrangement:
                    return "Arrangement";
                case GestureContext::PianoRoll:
                    return "Piano roll";
                case GestureContext::DrumGrid:
                    return "Drum grid";
                case GestureContext::Waveform:
                    return "Waveform";
                case GestureContext::CurveEditor:
                    return "Curve editor";
                case GestureContext::ValueLabel:
                    return "Value label";
                case GestureContext::Chain:
                    return "Chain";
                case GestureContext::Sampler:
                    return "Sampler";
                case GestureContext::StepSequencer:
                    return "Step sequencer";
                case GestureContext::EqCurveEditor:
                    return "EQ curve editor";
                case GestureContext::Unknown:
                    return "Unknown";
                default:
                    return "Unknown";
            }
        }

        static bool sameInput(const GestureInput& a, const GestureInput& b) {
            return a.kind == b.kind && a.area == b.area && a.axis == b.axis &&
                   a.modifiers == b.modifiers;
        }

        static std::vector<GestureInput> candidateInputsForMovedBinding(const GestureRow& row) {
            std::vector<GestureInput> candidates;
            auto add = [&candidates](GestureInput input) { candidates.push_back(input); };

            for (auto kind : {GestureInputKind::Wheel, GestureInputKind::SmoothWheel}) {
                for (auto area : {GestureArea::Main, GestureArea::Ruler}) {
                    for (auto axis : {GestureAxis::Vertical, GestureAxis::Horizontal}) {
                        for (auto mods : modifiers())
                            add({kind, area, axis, mods});
                    }
                }
            }

            for (auto area : {GestureArea::Ruler, GestureArea::Body, GestureArea::Header,
                              GestureArea::Keyboard, GestureArea::ZoomStrip}) {
                for (auto mods : modifiers())
                    add({GestureInputKind::Drag, area, GestureAxis::Vertical, mods});
            }

            candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                            [&row](const GestureInput& input) {
                                                return sameInput(input, row.input);
                                            }),
                             candidates.end());
            return candidates;
        }

        static bool isOverrideBinding(const GestureRouter& router, GestureContext context,
                                      const GestureInput& input, const GestureBinding& binding) {
            const auto* defaultBinding = router.findDefaultBinding(context, input);
            return defaultBinding == nullptr || *defaultBinding != binding;
        }

        static juce::String areaName(GestureContext context, GestureArea area) {
            juce::String suffix;
            switch (area) {
                case GestureArea::Main:
                    suffix = "main";
                    break;
                case GestureArea::Ruler:
                    suffix = "ruler";
                    break;
                case GestureArea::Body:
                    suffix = "body";
                    break;
                case GestureArea::Header:
                    suffix = "header";
                    break;
                case GestureArea::Keyboard:
                    suffix = "keyboard";
                    break;
                case GestureArea::ZoomStrip:
                    suffix = "zoom strip";
                    break;
            }
            return contextName(context) + " " + suffix;
        }

        static juce::String inputName(const GestureInput& input) {
            juce::String base;
            if (input.kind == GestureInputKind::Wheel)
                base = input.axis == GestureAxis::Vertical ? "wheel" : "horizontal wheel";
            else if (input.kind == GestureInputKind::SmoothWheel)
                base = input.axis == GestureAxis::Vertical ? "smooth wheel"
                                                           : "horizontal smooth wheel";
            else
                base = "drag";

            if (input.modifiers == GestureMod_None)
                return base;
            return modifierName(input.modifiers) + "+" + base;
        }

        static juce::String learnButtonText() {
            return "Learn";
        }

        static juce::String capturePromptText(const GestureInput& input) {
            juce::ignoreUnused(input);
            return "Learning...";
        }

        // The input as a chip: modifier glyphs, then the wheel or drag.
        static juce::String inputLabelName(const GestureInput& input) {
            juce::String base;
            if (input.kind == GestureInputKind::Drag)
                base = "drag";
            else if (input.kind == GestureInputKind::SmoothWheel)
                base = input.axis == GestureAxis::Vertical ? "smooth wheel" : "smooth tilt";
            else
                base = input.axis == GestureAxis::Vertical ? "wheel" : "tilt wheel";
            juce::String mods;
#if JUCE_MAC
            if ((input.modifiers & GestureMod_Shift) != 0)
                mods << juce::String(juce::CharPointer_UTF8("\xe2\x87\xa7"));
            if ((input.modifiers & GestureMod_Alt) != 0)
                mods << juce::String(juce::CharPointer_UTF8("\xe2\x8c\xa5"));
            if ((input.modifiers & GestureMod_Command) != 0)
                mods << juce::String(juce::CharPointer_UTF8("\xe2\x8c\x98"));
#else
            if (input.modifiers != GestureMod_None)
                mods = modifierName(input.modifiers) + "+";
#endif
            return mods.isEmpty() ? base : mods + " " + base;
        }

        static juce::String modifierName(uint8_t modifiers) {
            if (modifiers == GestureMod_None)
                return "None";

            juce::StringArray parts;
            if ((modifiers & GestureMod_Shift) != 0)
                parts.add("Shift");
#if JUCE_MAC
            if ((modifiers & GestureMod_Command) != 0)
                parts.add("Cmd");
#else
            if ((modifiers & GestureMod_Command) != 0)
                parts.add("Ctrl");
#endif
            if ((modifiers & GestureMod_Alt) != 0)
                parts.add("Alt");
            return parts.joinIntoString("+");
        }

        static juce::String actionName(GestureActionType action) {
            auto name = gestureName(action);
            if (name.endsWith(" gesture"))
                name = name.dropLastCharacters(8);
            return name;
        }

        static juce::String gestureName(GestureActionType action) {
            switch (action) {
                case GestureActionType::ScrollHorizontal:
                    return "Horizontal scroll gesture";
                case GestureActionType::ScrollVertical:
                    return "Vertical scroll gesture";
                case GestureActionType::ZoomHorizontal:
                    return "Horizontal zoom gesture";
                case GestureActionType::ZoomVertical:
                    return "Vertical zoom gesture";
                case GestureActionType::Pan:
                    return "Pan gesture";
                case GestureActionType::DuplicateOnDrag:
                    return "Duplicate clips on drag";
                case GestureActionType::DuplicateAsGhostOnDrag:
                    return "Ghost-copy clips on drag";
                case GestureActionType::AdjustValue:
                    return "Adjust value gesture";
                case GestureActionType::AdjustValueFine:
                    return "Fine value adjustment";
                case GestureActionType::AdjustSecondaryValue:
                    return "Adjust secondary value";
                case GestureActionType::AdjustSecondaryValueFine:
                    return "Fine secondary adjustment";
                case GestureActionType::None:
                    return "Unassigned gesture";
            }
            return "Unassigned gesture";
        }

        // Discrete actions (copy-on-drag) have no analog magnitude, so the
        // sensitivity and invert columns are meaningless for them.
        static bool actionHasMagnitude(GestureActionType action) {
            return action != GestureActionType::None &&
                   action != GestureActionType::DuplicateOnDrag &&
                   action != GestureActionType::DuplicateAsGhostOnDrag;
        }

        static GestureArea defaultDragAreaFor(GestureContext context, GestureActionType action) {
            switch (action) {
                case GestureActionType::ZoomHorizontal:
                    return context == GestureContext::Waveform ? GestureArea::Body
                                                               : GestureArea::Ruler;
                case GestureActionType::ZoomVertical:
                    return GestureArea::ZoomStrip;
                default:
                    return GestureArea::Main;
            }
        }

        static GestureBinding tunedBindingForInput(GestureContext context, GestureInput input,
                                                   GestureActionType action,
                                                   const GestureBinding& fallback) {
            auto& router = GestureRouter::getInstance();
            if (const auto* exact = router.findDefaultBinding(context, input);
                exact != nullptr && exact->action == action) {
                return *exact;
            }

            if (input.kind == GestureInputKind::Drag) {
                auto alternateAxis = input;
                alternateAxis.axis = input.axis == GestureAxis::Vertical ? GestureAxis::Horizontal
                                                                         : GestureAxis::Vertical;
                if (const auto* alternate = router.findDefaultBinding(context, alternateAxis);
                    alternate != nullptr && alternate->action == action) {
                    return *alternate;
                }

                auto unmodifiedAlternate = alternateAxis;
                unmodifiedAlternate.modifiers = GestureMod_None;
                if (const auto* unmodified =
                        router.findDefaultBinding(context, unmodifiedAlternate);
                    unmodified != nullptr && unmodified->action == action) {
                    return *unmodified;
                }
            }

            return fallback;
        }

        static void setupBodyLabel(juce::Label& label, const juce::String& text) {
            label.setText(text, juce::dontSendNotification);
            label.setFont(FontManager::getInstance().getUIFont(12.0f));
            label.setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
            label.setJustificationType(juce::Justification::centredLeft);
        }

        void setupHeaderLabel(juce::Label& label, const juce::String& text) {
            label.setText(text, juce::dontSendNotification);
            label.setFont(FontManager::getInstance().getUIFontBold(11.0f));
            label.setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
            label.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(label);
        }

        void addGestureRow(GestureContext context, GestureInput input) {
            auto row = std::make_unique<GestureRow>();
            row->context = context;
            row->input = input;
            row->currentInput = input;
            auto* rowPtr = row.get();

            if (const auto* binding =
                    GestureRouter::getInstance().findDefaultBinding(context, input))
                row->defaultBinding = *binding;

            row->action = row->defaultBinding.action;
            setupBodyLabel(row->contextLabel, actionName(row->action));
            setupBodyLabel(row->axisLabel, areaName(context, input.area));
            setupBodyLabel(row->inputLabel, inputLabelName(row->currentInput));

            row->learnButton.setButtonText(learnButtonText());
            row->learnButton.onClick = [this, rowPtr] { setCapturingRow(rowPtr); };

            row->sensitivity.setRange(0.0, 1200.0, 0.01);
            row->sensitivity.setOrientation(magda::daw::ui::TextSlider::Orientation::Horizontal);
            row->sensitivity.setShowFillIndicator(false);
            row->sensitivity.setValueFormatter(
                [](double value) { return juce::String(value, value < 10.0 ? 2 : 0); });

            row->invert.setButtonText({});
            magda::daw::ui::SettingsLookAndFeel::markCheckBox(row->invert);
            row->inputLabel.setFont(FontManager::getInstance().getMonoFont(12.0f));
            row->inputLabel.setJustificationType(juce::Justification::centred);
            row->inputLabel.setColour(juce::Label::outlineColourId,
                                      ActiveTheme::getBorderColour().brighter(0.2f));
            row->contextLabel.setFont(FontManager::getInstance().getUIFont(13.0f));
            addToGroup(*row);

            content_.addAndMakeVisible(row->contextLabel);
            content_.addAndMakeVisible(row->inputLabel);
            content_.addAndMakeVisible(row->learnButton);
            content_.addAndMakeVisible(row->sensitivity);
            content_.addAndMakeVisible(row->invert);
            rows_.push_back(std::move(row));
        }

        void loadFromRouter(const GestureRouter& router) {
            for (auto& row : rows_) {
                GestureBinding binding;
                GestureInput displayedInput = row->input;
                if (const auto* current = router.findBinding(row->context, row->input)) {
                    binding = *current;
                }

                if (binding.action == GestureActionType::None &&
                    row->defaultBinding.action != GestureActionType::None) {
                    for (const auto& candidate : candidateInputsForMovedBinding(*row)) {
                        const auto* moved = router.findBinding(row->context, candidate);
                        if (moved == nullptr || moved->action != row->defaultBinding.action)
                            continue;
                        if (!isOverrideBinding(router, row->context, candidate, *moved))
                            continue;

                        binding = *moved;
                        displayedInput = candidate;
                        break;
                    }
                }

                if (displayedInput.kind == GestureInputKind::Drag &&
                    (binding.action == GestureActionType::ZoomHorizontal ||
                     binding.action == GestureActionType::ZoomVertical) &&
                    binding.sensitivity < 2.0f) {
                    binding =
                        tunedBindingForInput(row->context, displayedInput, binding.action, binding);
                }

                row->action = binding.action;
                row->currentInput = displayedInput;
                row->axisLabel.setText(areaName(row->context, row->currentInput.area),
                                       juce::dontSendNotification);
                row->inputLabel.setText(inputLabelName(row->currentInput),
                                        juce::dontSendNotification);
                row->sensitivity.setValue(binding.sensitivity, juce::dontSendNotification);
                row->invert.setToggleState(binding.invert, juce::dontSendNotification);

                const bool visible = binding.action != GestureActionType::None;
                const bool hasMagnitude = actionHasMagnitude(binding.action);
                row->contextLabel.setText(actionName(binding.action), juce::dontSendNotification);
                row->contextLabel.setVisible(visible);
                row->axisLabel.setVisible(visible);
                row->inputLabel.setVisible(visible);
                row->learnButton.setVisible(visible);
                row->sensitivity.setVisible(visible && hasMagnitude);
                row->invert.setVisible(visible && hasMagnitude);
            }

            setCapturingRow(nullptr);
            layoutRows(viewport_.getMaximumVisibleWidth());
        }

        static void setRowInput(GestureRow& row, GestureInput input) {
            const auto tuned =
                tunedBindingForInput(row.context, input, row.action,
                                     {row.action, static_cast<float>(row.sensitivity.getValue()),
                                      row.invert.getToggleState()});
            row.currentInput = input;
            row.axisLabel.setText(areaName(row.context, input.area), juce::dontSendNotification);
            row.inputLabel.setText(inputLabelName(input), juce::dontSendNotification);
            row.sensitivity.setValue(tuned.sensitivity, juce::dontSendNotification);
            row.invert.setToggleState(tuned.invert, juce::dontSendNotification);
            row.learnButton.setButtonText(learnButtonText());
        }

        static GestureArea learnedDragAreaFor(const GestureRow& row) {
            if (row.currentInput.area != GestureArea::Main)
                return row.currentInput.area;
            if (row.input.area != GestureArea::Main)
                return row.input.area;
            return defaultDragAreaFor(row.context, row.action);
        }

        void setCapturingRow(GestureRow* row) {
            if (capturingRow_ != nullptr)
                capturingRow_->learnButton.setButtonText(learnButtonText());

            capturingRow_ = row;

            if (capturingRow_ != nullptr) {
                capturingRow_->learnButton.setButtonText(
                    capturePromptText(capturingRow_->currentInput));
                captureDragStart_ = {};
                grabKeyboardFocus();
            }
            updateLearnButtons();
        }

        // Learn shows on the row under the pointer, and stays while it listens.
        void updateLearnButtons() {
            for (auto& row : rows_)
                row->learnButton.setVisible(
                    row->contextLabel.isVisible() &&
                    (row.get() == hoveredRow_ || row.get() == capturingRow_));
        }

        /// Action, input, sensitivity and invert across @p row.
        static std::array<juce::Rectangle<int>, 4> columnsFor(juce::Rectangle<int> row) {
            auto r = row.withTrimmedLeft(20);
            const auto invert = r.removeFromRight(56);
            r.removeFromRight(16);
            const auto sensitivity = r.removeFromRight(80);
            r.removeFromRight(24);
            const auto input = r.removeFromRight(juce::jlimit(170, 240, r.getWidth() / 3));
            return {r, input, sensitivity, invert.withTrimmedLeft(12)};
        }

        /// A header that folds its group of gesture rows.
        class GroupHeader : public juce::Component {
          public:
            std::function<void()> onToggle;
            juce::String title;
            bool collapsed = false;

            void paint(juce::Graphics& g) override {
                auto r = getLocalBounds();
                g.setColour(ActiveTheme::getBorderColour().withMultipliedAlpha(0.6f));
                g.fillRect(r.removeFromBottom(1));
                g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
                g.setFont(FontManager::getInstance().getUIFont(10.0f));
                g.drawText(collapsed ? juce::String(juce::CharPointer_UTF8("\xe2\x96\xb8"))
                                     : juce::String(juce::CharPointer_UTF8("\xe2\x96\xbe")),
                           r.removeFromLeft(18), juce::Justification::centred);
                g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
                g.setFont(FontManager::getInstance().getUIFontMedium(13.0f));
                g.drawText(title, r, juce::Justification::centredLeft, true);
            }
            void mouseUp(const juce::MouseEvent&) override {
                collapsed = !collapsed;
                repaint();
                if (onToggle)
                    onToggle();
            }
        };

        struct Group {
            juce::String title;
            std::vector<GestureRow*> rows;
            std::unique_ptr<GroupHeader> header;
        };

        static juce::String groupTitle(GestureContext context, GestureArea area) {
            switch (area) {
                case GestureArea::Main:
                    return contextName(context);
                case GestureArea::Ruler:
                    return contextName(context) + " \xc2\xb7 ruler";
                case GestureArea::Body:
                    return contextName(context) + " \xc2\xb7 body";
                case GestureArea::Header:
                    return contextName(context) + " \xc2\xb7 header";
                case GestureArea::Keyboard:
                    return contextName(context) + " \xc2\xb7 keyboard";
                case GestureArea::ZoomStrip:
                    return contextName(context) + " \xc2\xb7 zoom strip";
            }
            return contextName(context);
        }

        void addToGroup(GestureRow& row) {
            const auto title = groupTitle(row.context, row.input.area);
            for (auto& group : groups_) {
                if (group.title == title) {
                    group.rows.push_back(&row);
                    return;
                }
            }
            Group group;
            group.title = title;
            group.rows.push_back(&row);
            group.header = std::make_unique<GroupHeader>();
            group.header->title = title;
            group.header->onToggle = [this] { layoutRows(viewport_.getMaximumVisibleWidth()); };
            content_.addAndMakeVisible(*group.header);
            groups_.push_back(std::move(group));
        }

        void layoutRows(int width) {
            constexpr int rowH = 32;
            int y = 0;
            for (auto& group : groups_) {
                const bool any = std::ranges::any_of(group.rows, [](const GestureRow* row) {
                    return row->contextLabel.isVisible();
                });
                group.header->setVisible(any);
                if (!any)
                    continue;
                group.header->setBounds(0, y, width, rowH);
                y += rowH;
                for (auto* row : group.rows) {
                    if (!row->contextLabel.isVisible())
                        continue;
                    const bool shown = !group.header->collapsed;
                    for (juce::Component* c : std::initializer_list<juce::Component*>{
                             &row->contextLabel, &row->inputLabel, &row->sensitivity, &row->invert})
                        c->setAlpha(shown ? 1.0f : 0.0f);
                    if (!shown) {
                        for (juce::Component* c : std::initializer_list<juce::Component*>{
                                 &row->contextLabel, &row->inputLabel, &row->learnButton,
                                 &row->sensitivity, &row->invert})
                            c->setBounds({});
                        continue;
                    }
                    const auto columns = columnsFor({0, y, width, rowH});
                    row->contextLabel.setBounds(columns[0]);
                    const int chipW = juce::GlyphArrangement::getStringWidthInt(
                                          row->inputLabel.getFont(), row->inputLabel.getText()) +
                                      18;
                    row->inputLabel.setBounds(
                        columns[1]
                            .withWidth(juce::jmin(chipW, columns[1].getWidth()))
                            .reduced(0, 6));
                    row->learnButton.setBounds(
                        columns[1].withTrimmedLeft(chipW + 8).withWidth(76).reduced(0, 4));
                    row->sensitivity.setBounds(columns[2].reduced(0, 5));
                    row->invert.setBounds(columns[3].withWidth(24));
                    y += rowH;
                }
            }
            content_.setSize(width, y + 8);
        }

        SettingsToolbar toolbar_;
        juce::TextButton resetButton_;
        juce::Rectangle<int> columnHeader_;
        std::vector<Group> groups_;
        GestureRow* hoveredRow_ = nullptr;
        CaptureViewport viewport_{*this};
        juce::Component content_;
        std::vector<std::unique_ptr<GestureRow>> rows_;
        GestureRow* capturingRow_ = nullptr;
        juce::Point<int> captureDragStart_;
    };

    std::unique_ptr<KeyboardShortcutsPage> keyboardPage_;
    std::unique_ptr<GestureBindingsPage> gesturesPage_;
};

// ---------------------------------------------------------------------------
// Settings sections (#3162)
// ---------------------------------------------------------------------------

namespace {

void showMigrationFailureAsync(const juce::File& from, const juce::File& to) {
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle(tr("preferences.paths.migration.fail_title"))
                                     .withMessage(tr("preferences.paths.migration.fail_message")
                                                      .replace("{0}", from.getFullPathName())
                                                      .replace("{1}", to.getFullPathName()))
                                     .withButton(tr("dialogs.ok")),
                                 nullptr);
}

// Copy `from` into `to`. No-op if either is missing or the paths are equal.
// The destination is created if needed so copyDirectoryTo has a target.
bool copyFolderIfNeeded(const juce::File& from, const juce::File& to) {
    if (!from.isDirectory() || from.getFullPathName() == to.getFullPathName())
        return true;
    to.createDirectory();
    return from.copyDirectoryTo(to);
}

void scaleExplicitFonts(juce::Component& component, double ratio) {
    if (std::abs(ratio - 1.0) < 0.001)
        return;

    // A subtree that re-resolves its own fonts has already been handed the new
    // scale by the look-and-feel broadcast; multiplying its cached fonts by the
    // ratio on top of that would apply the scale twice.
    if (magda::resolvesOwnFonts(component))
        return;

    const auto scaleFont = [ratio](const juce::Font& font) {
        return font.withHeight(font.getHeight() * static_cast<float>(ratio));
    };

    if (auto* label = dynamic_cast<juce::Label*>(&component))
        label->setFont(scaleFont(label->getFont()));
    else if (auto* editor = dynamic_cast<juce::TextEditor*>(&component))
        editor->setFont(scaleFont(editor->getFont()));

    for (int i = 0; i < component.getNumChildComponents(); ++i) {
        if (auto* child = component.getChildComponent(i))
            scaleExplicitFonts(*child, ratio);
    }
}

void refreshFontScaleForOpenWindows(double oldScale, double newScale) {
    if (oldScale <= 0.0)
        oldScale = 1.0;

    const double ratio = newScale / oldScale;
    for (int i = juce::TopLevelWindow::getNumTopLevelWindows(); --i >= 0;) {
        if (auto* window = juce::TopLevelWindow::getTopLevelWindow(i)) {
            scaleExplicitFonts(*window, ratio);
            window->resized();
            window->repaint();
        }
    }
}

MainWindow* findMainWindow() {
    for (int i = juce::TopLevelWindow::getNumTopLevelWindows(); --i >= 0;)
        if (auto* mw = dynamic_cast<MainWindow*>(juce::TopLevelWindow::getTopLevelWindow(i)))
            return mw;
    return nullptr;
}

/** @brief A Preferences page shown as a section with one scrolling tab. */
template <typename Page> class PageSection : public SettingsSection {
  public:
    template <typename... PageArgs>
    PageSection(SettingsSectionId id, juce::String title, PageArgs&&... pageArgs)
        : SettingsSection(id),
          page_(std::forward<PageArgs>(pageArgs)...),
          title_(std::move(title)) {}

    juce::String getTitle() const override {
        return title_;
    }
    std::vector<Tab> getTabs() override {
        return {{title_, &page_, [this](int width) { return page_.getPreferredHeight(width); }}};
    }
    void load(Config& config) override {
        page_.loadSettings(config);
    }
    void writeTo(Config& config) override {
        page_.applySettings(config);
    }

  protected:
    Page page_;

  private:
    juce::String title_;
};

class GeneralSection : public PageSection<GeneralPage> {
  public:
    GeneralSection() : PageSection(SettingsSectionId::General, tr("preferences.tab.general")) {}

    void commit(const Config& before) override {
        auto& config = Config::getInstance();
        if (before.getLanguage() != config.getLanguage())
            StringTable::getInstance().loadLanguage(juce::String(config.getLanguage()));
        ProjectManager::getInstance().setAutoSaveEnabled(config.getAutoSaveEnabled(),
                                                         config.getAutoSaveIntervalSeconds());
        if (auto* mw = findMainWindow()) {
            mw->applyPanelVisibilityFromConfig();
            mw->applyLayoutFromConfig();
        }
    }
};

class AppearanceSection : public PageSection<AppearancePage> {
  public:
    AppearanceSection()
        : PageSection(SettingsSectionId::Appearance,
                      trOr("preferences.tab.appearance", "Appearance")) {}

    int countChanges() override {
        return PageSection::countChanges() + (page_.shouldApplyPaletteToCurrentProject() ? 1 : 0);
    }

    void commit(const Config& before) override {
        auto& config = Config::getInstance();
        if (before.getUIScale() != config.getUIScale()) {
            if (config.getUIScale() > 0.0)
                applyUIScale(config.getUIScale());
            else
                applyUIScale(dpiOnlyAutoScale(), /*persist=*/false);
        }
        refreshFontScaleForOpenWindows(before.getUIFontScale(), config.getUIFontScale());
        if (page_.shouldApplyPaletteToCurrentProject()) {
            ProjectManager::getInstance().applyConfigPaletteToCurrentProject();
            page_.clearApplyPaletteToCurrentProject();
        }
    }
};

class PathsSection : public PageSection<PathsPage> {
  public:
    PathsSection() : PageSection(SettingsSectionId::Paths, tr("preferences.tab.paths")) {}

    int countChanges() override {
        return PageSection::countChanges() + (page_.dataDirChanged() ? 1 : 0) +
               (page_.presetsDirChanged() ? 1 : 0);
    }

    void commit(const Config& before) override {
        auto& config = Config::getInstance();
        if (before.getMediaDbDir() != config.getMediaDbDir())
            magda::media::MediaDbContext::getInstance().resetForReopen();

        const bool dataChanged = page_.dataDirChanged();
        const bool presetsChanged = page_.presetsDirChanged();
        const bool copyData = page_.shouldCopyData();
        const bool copyPresets = page_.shouldCopyPresets();
        const juce::File dataFrom(juce::String(page_.getOriginalDataPath()).isEmpty()
                                      ? magda::paths::alwaysOSDefault().getFullPathName()
                                      : juce::String(page_.getOriginalDataPath()));
        const juce::File dataTo(juce::String(page_.getNewDataPath()));
        const juce::File presetsFrom(
            juce::String(page_.getOriginalPresetsPath()).isEmpty()
                ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                      .getChildFile("MAGDA")
                      .getChildFile("Presets")
                      .getFullPathName()
                : juce::String(page_.getOriginalPresetsPath()));
        const juce::File presetsTo(juce::String(page_.getNewPresetsPath()));
        const auto newDataPath = page_.getNewDataPath();
        page_.markPathsCommitted();

        // Presets path: hot-apply. The copy / don't-copy answer came at Browse time.
        if (presetsChanged) {
            config.setPresetsDir(page_.getNewPresetsPath());
            config.save();
            magda::paths::resolve();
            if (copyPresets && !copyFolderIfNeeded(presetsFrom, presetsTo))
                showMigrationFailureAsync(presetsFrom, presetsTo);
        }

        // Data path: needs a restart so the logger, plugin scanner and the rest
        // re-open at the new location. Persist Config, copy if requested, quit.
        if (dataChanged) {
            auto& projectManager = ProjectManager::getInstance();
            if (projectManager.isDirty() && !projectManager.showUnsavedChangesDialog())
                return;

            if (copyData && !copyFolderIfNeeded(dataFrom, dataTo)) {
                showMigrationFailureAsync(dataFrom, dataTo);
                return;  // Don't quit if the copy failed — let the user investigate.
            }

            projectManager.prepareForCleanShutdown(copyData ? dataTo : juce::File());
            config.setDataDir(newDataPath);
            config.save();
            magda::paths::resolve();
            juce::JUCEApplication::quit();
        }
    }
};

class ShortcutsSection : public SettingsSection {
  public:
    explicit ShortcutsSection(juce::ApplicationCommandManager* commandManager)
        : SettingsSection(SettingsSectionId::Shortcuts), page_(commandManager) {}

    juce::String getTitle() const override {
        return tr("preferences.tab.shortcuts");
    }
    std::vector<Tab> getTabs() override {
        return {{"Keyboard", &page_.getKeyboardPage(), {}, &page_.getKeyboardToolbar()},
                {"Gestures", &page_.getGesturesPage(), {}, &page_.getGesturesToolbar()}};
    }
    void load(Config& config) override {
        page_.loadSettings(config);
    }
    void writeTo(Config& config) override {
        page_.applySettings(config);
    }
    void commit(const Config&) override {
        page_.commit();
    }

  private:
    ShortcutsPage page_;
};

}  // namespace

std::vector<std::unique_ptr<SettingsSection>> createPreferencesSections(
    juce::ApplicationCommandManager* commandManager, AudioEngine* audioEngine) {
    std::vector<std::unique_ptr<SettingsSection>> sections;
    sections.push_back(std::make_unique<GeneralSection>());
    sections.push_back(std::make_unique<AppearanceSection>());
    sections.push_back(std::make_unique<PageSection<RenderingPage>>(
        SettingsSectionId::Rendering, tr("preferences.tab.rendering")));
    sections.push_back(std::make_unique<PathsSection>());
    sections.push_back(std::make_unique<PageSection<DefaultsPage>>(
        SettingsSectionId::Defaults, tr("preferences.tab.defaults"), audioEngine));
    sections.push_back(std::make_unique<ShortcutsSection>(commandManager));
    return sections;
}

}  // namespace magda
