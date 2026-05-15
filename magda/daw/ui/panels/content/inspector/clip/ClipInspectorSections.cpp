#include <cmath>

#include "../../../../../audio/AudioThumbnailManager.hpp"
#include "../../../../components/common/ColourSwatch.hpp"
#include "../../../../state/TimelineController.hpp"
#include "../../../../themes/DarkTheme.hpp"
#include "../../../../themes/FontManager.hpp"
#include "../../../../themes/InspectorComboBoxLookAndFeel.hpp"
#include "../../../../themes/SmallButtonLookAndFeel.hpp"
#include "../../../../utils/TimelineUtils.hpp"
#include "../ClipInspector.hpp"
#include "BinaryData.h"
#include "audio/AudioBridge.hpp"
#include "core/ClipBatchEdit.hpp"
#include "core/ClipCommands.hpp"
#include "core/ClipDisplayInfo.hpp"
#include "core/ClipOperations.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/Config.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/TempoUtils.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "engine/AudioEngine.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "project/ProjectManager.hpp"

namespace magda::daw::ui {
namespace {

double getAudioFileDurationForInspector(const magda::ClipInfo& clip) {
    if (!clip.isAudio() || clip.audio().source.filePath.isEmpty())
        return 0.0;

    if (auto* thumbnail = magda::AudioThumbnailManager::getInstance().getThumbnail(
            clip.audio().source.filePath)) {
        return thumbnail->getTotalLength();
    }

    return clip.audio().source.durationSeconds;
}

double displayBeatsToAudioSourceSeconds(const magda::ClipInfo& clip, double displayBeats,
                                        double projectBpm) {
    const double bpm = projectBpm > 0.0 ? projectBpm : 120.0;
    const auto info =
        magda::ClipDisplayInfo::from(clip, bpm, getAudioFileDurationForInspector(clip));
    const double displaySeconds = magda::TimelineUtils::beatsToSeconds(displayBeats, bpm);
    return info.timelineToSource(displaySeconds);
}

double timelineStartSeconds(const magda::ClipInfo& clip, double bpm) {
    return clip.getTimelineStart(bpm);
}

double timelineLengthSeconds(const magda::ClipInfo& clip, double bpm) {
    return clip.getTimelineLength(bpm);
}

}  // namespace

// ========================================================================
// GroovePickerPopup — two-column category browser
// ========================================================================

class ClipInspector::GroovePickerPopup : public juce::Component {
  public:
    GroovePickerPopup(ClipInspector& owner) : owner_(owner) {
        addAndMakeVisible(categoryList_);
        addAndMakeVisible(templateList_);

        categoryList_.setModel(&categoryModel_);
        categoryList_.setRowHeight(22);
        categoryList_.setColour(juce::ListBox::backgroundColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
        categoryList_.setOutlineThickness(0);

        templateList_.setModel(&templateModel_);
        templateList_.setRowHeight(22);
        templateList_.setColour(juce::ListBox::backgroundColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
        templateList_.setOutlineThickness(0);

        // Click on category → update right column
        categoryModel_.onSelectionChanged = [this](int row) {
            if (row >= 0 && row < static_cast<int>(categories_.size())) {
                selectedCategory_ = row;
                templateModel_.setItems(categories_[static_cast<size_t>(row)].templates);
                templateList_.updateContent();
                templateList_.deselectAllRows();
                templateList_.repaint();
            }
        };

        // Click on template → select and close
        templateModel_.onItemClicked = [this](int row) {
            auto& items = templateModel_.getItems();
            if (row >= 0 && row < static_cast<int>(items.size())) {
                owner_.onGrooveTemplateSelected(items[static_cast<size_t>(row)]);
                // Dismiss the callout box we're hosted in
                if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
                    callout->dismiss();
            }
        };
    }

    struct Category {
        juce::String name;
        juce::StringArray templates;
    };

    void populate(const std::vector<Category>& cats, const juce::String& currentTemplate) {
        categories_ = cats;
        categoryModel_.setItems(categories_);
        categoryList_.updateContent();

        // Select the category containing the current template
        selectedCategory_ = 0;
        for (size_t i = 0; i < categories_.size(); ++i) {
            if (categories_[i].templates.contains(currentTemplate)) {
                selectedCategory_ = static_cast<int>(i);
                break;
            }
        }
        categoryList_.selectRow(selectedCategory_);
        if (selectedCategory_ < static_cast<int>(categories_.size())) {
            templateModel_.setItems(categories_[static_cast<size_t>(selectedCategory_)].templates);
            templateList_.updateContent();

            // Highlight current template
            int idx = templateModel_.getItems().indexOf(currentTemplate);
            if (idx >= 0)
                templateList_.selectRow(idx);
        }
    }

    void resized() override {
        auto b = getLocalBounds().reduced(1);
        int catWidth = b.getWidth() * 2 / 5;
        categoryList_.setBounds(b.removeFromLeft(catWidth));
        templateList_.setBounds(b);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE));
        g.setColour(DarkTheme::getBorderColour());
        g.drawRect(getLocalBounds());
        // Separator between columns
        int catWidth = getWidth() * 2 / 5;
        g.drawVerticalLine(catWidth, 1.0f, static_cast<float>(getHeight() - 1));
    }

  private:
    // Left column model: category names
    class CategoryListModel : public juce::ListBoxModel {
      public:
        std::function<void(int)> onSelectionChanged;

        void setItems(const std::vector<Category>& cats) {
            categories_ = &cats;
        }

        int getNumRows() override {
            return categories_ ? static_cast<int>(categories_->size()) : 0;
        }

        void paintListBoxItem(int row, juce::Graphics& g, int width, int height,
                              bool rowIsSelected) override {
            if (!categories_ || row < 0 || row >= static_cast<int>(categories_->size()))
                return;
            if (rowIsSelected) {
                g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.25f));
                g.fillRect(0, 0, width, height);
            }
            g.setColour(rowIsSelected ? DarkTheme::getTextColour()
                                      : DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(11.0f));
            g.drawText((*categories_)[static_cast<size_t>(row)].name, 8, 0, width - 8, height,
                       juce::Justification::centredLeft);
        }

        void selectedRowsChanged(int lastRowSelected) override {
            if (onSelectionChanged)
                onSelectionChanged(lastRowSelected);
        }

      private:
        const std::vector<Category>* categories_ = nullptr;
    };

    // Right column model: template names
    class TemplateListModel : public juce::ListBoxModel {
      public:
        std::function<void(int)> onItemClicked;

        void setItems(const juce::StringArray& items) {
            items_ = items;
        }

        const juce::StringArray& getItems() const {
            return items_;
        }

        int getNumRows() override {
            return items_.size();
        }

        void paintListBoxItem(int row, juce::Graphics& g, int width, int height,
                              bool rowIsSelected) override {
            if (row < 0 || row >= items_.size())
                return;
            if (rowIsSelected) {
                g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.25f));
                g.fillRect(0, 0, width, height);
            }
            g.setColour(DarkTheme::getTextColour());
            g.setFont(FontManager::getInstance().getUIFont(11.0f));
            g.drawText(items_[row], 8, 0, width - 8, height, juce::Justification::centredLeft);
        }

        void listBoxItemClicked(int row, const juce::MouseEvent&) override {
            if (onItemClicked)
                onItemClicked(row);
        }

      private:
        juce::StringArray items_;
    };

    ClipInspector& owner_;
    std::vector<Category> categories_;
    int selectedCategory_ = 0;

    CategoryListModel categoryModel_;
    TemplateListModel templateModel_;
    juce::ListBox categoryList_{"Categories"};
    juce::ListBox templateList_{"Templates"};
};

// ========================================================================
// Clip properties section
// ========================================================================

void ClipInspector::initClipPropertiesSection() {
    // Clip name (used as header - no "Name" label needed)
    clipNameLabel_.setVisible(false);  // Not used anymore

    clipNameValue_.setFont(FontManager::getInstance().getUIFont(14.0f));  // Larger for header
    clipNameValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    clipNameValue_.setColour(juce::Label::backgroundColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    clipNameValue_.setEditable(true);
    clipNameValue_.onTextChange = [this]() {
        if (primaryClipId() != magda::INVALID_CLIP_ID) {
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetClipNameCommand>(primaryClipId(),
                                                            clipNameValue_.getText()));
        }
    };
    addChildComponent(clipNameValue_);

    // Colour swatch
    colourSwatch_ = std::make_unique<magda::ColourSwatch>();
    auto* swatch = static_cast<magda::ColourSwatch*>(colourSwatch_.get());
    swatch->onColourClicked = [this, swatch]() {
        auto pid = primaryClipId();
        if (pid == magda::INVALID_CLIP_ID)
            return;

        auto menu = juce::PopupMenu();
        menu.addItem(1, "None");
        menu.addSeparator();

        auto makeChip = [](juce::Colour colour) {
            juce::Image chip(juce::Image::ARGB, 14, 14, true);
            juce::Graphics cg(chip);
            cg.setColour(colour);
            cg.fillRoundedRectangle(0.0f, 0.0f, 14.0f, 14.0f, 2.0f);
            auto drawable = std::make_unique<juce::DrawableImage>();
            drawable->setImage(chip);
            return drawable;
        };

        // Inherit from track option
        menu.addItem(2, "Inherit from Track");
        menu.addSeparator();

        // Default colours
        for (size_t i = 0; i < magda::Config::defaultColourPalette.size(); ++i) {
            auto colour = juce::Colour(magda::Config::defaultColourPalette[i].colour);
            menu.addItem(static_cast<int>(i + 3), magda::Config::defaultColourPalette[i].name, true,
                         false, makeChip(colour));
        }

        // Custom colours from Config
        const auto customPalette = magda::Config::getInstance().getTrackColourPalette();
        const int customOffset = static_cast<int>(magda::Config::defaultColourPalette.size()) + 3;
        if (!customPalette.empty()) {
            menu.addSeparator();
            for (size_t i = 0; i < customPalette.size(); ++i) {
                auto colour = juce::Colour(customPalette[i].colour);
                menu.addItem(customOffset + static_cast<int>(i),
                             juce::String(customPalette[i].name), true, false, makeChip(colour));
            }
        }

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(swatch), [this, swatch,
                                                                                    customPalette](
                                                                                       int result) {
            if (result == 0)
                return;
            auto pid = primaryClipId();
            if (pid == magda::INVALID_CLIP_ID)
                return;
            const int customOff = static_cast<int>(magda::Config::defaultColourPalette.size()) + 3;

            if (result == 1) {
                // "None"
                swatch->clearColour();
                magda::ClipBatchEdit batch("Set Clip Colour", selectedClipIds_.size());
                for (auto cid : selectedClipIds_) {
                    batch.execute(std::make_unique<magda::SetClipColourCommand>(
                        cid, juce::Colour(0xFF444444)));
                }
            } else if (result == 2) {
                // Inherit from track
                magda::ClipBatchEdit batch("Set Clip Colour", selectedClipIds_.size());
                for (auto cid : selectedClipIds_) {
                    const auto* clip = magda::ClipManager::getInstance().getClip(cid);
                    if (clip) {
                        const auto* track =
                            magda::TrackManager::getInstance().getTrack(clip->trackId);
                        if (track) {
                            if (cid == pid)
                                swatch->setColour(track->colour);
                            batch.execute(
                                std::make_unique<magda::SetClipColourCommand>(cid, track->colour));
                        }
                    }
                }
            } else if (result >= 3 && result < customOff) {
                auto colour = juce::Colour(magda::Config::getDefaultColour(result - 3));
                swatch->setColour(colour);
                magda::ClipBatchEdit batch("Set Clip Colour", selectedClipIds_.size());
                for (auto cid : selectedClipIds_) {
                    batch.execute(std::make_unique<magda::SetClipColourCommand>(cid, colour));
                }
            } else {
                auto idx = static_cast<size_t>(result - customOff);
                if (idx < customPalette.size()) {
                    auto colour = juce::Colour(customPalette[idx].colour);
                    swatch->setColour(colour);
                    magda::ClipBatchEdit batch("Set Clip Colour", selectedClipIds_.size());
                    for (auto cid : selectedClipIds_) {
                        batch.execute(std::make_unique<magda::SetClipColourCommand>(cid, colour));
                    }
                }
            }
        });
    };
    addChildComponent(*colourSwatch_);

    // Clip file path (read-only, inside viewport)
    clipFilePathLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    clipFilePathLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipFilePathLabel_.setJustificationType(juce::Justification::centredLeft);
    clipPropsContainer_.addChildComponent(clipFilePathLabel_);

    // Clip type icon (audio_clip for audio, midi_clip for MIDI)
    clipTypeIcon_ = std::make_unique<magda::SvgButton>("Type", BinaryData::audio_clip_svg,
                                                       BinaryData::audio_clip_svgSize);
    clipTypeIcon_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    clipTypeIcon_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    clipTypeIcon_->setIconPadding(1.0f);
    clipTypeIcon_->setInterceptsMouseClicks(false, false);
    clipTypeIcon_->setTooltip("Audio clip");
    addChildComponent(*clipTypeIcon_);

    // Clip view icon (Session or Arrangement)
    clipViewIcon_ = std::make_unique<magda::SvgButton>("View", BinaryData::Arrangement_svg,
                                                       BinaryData::Arrangement_svgSize);
    clipViewIcon_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    clipViewIcon_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    clipViewIcon_->setIconPadding(1.0f);
    clipViewIcon_->setInterceptsMouseClicks(false, false);
    clipViewIcon_->setTooltip("Arrangement clip");
    addChildComponent(*clipViewIcon_);

    // Source BPM (editable — shown at bottom with WARP/BEAT buttons)
    clipBpmValue_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipBpmValue_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipBpmValue_.setColour(juce::Label::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    clipBpmValue_.setJustificationType(juce::Justification::centred);
    clipBpmValue_.setEditable(true);
    clipBpmValue_.onTextChange = [this]() {
        auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip || !clip->isAudio())
            return;

        // Parse BPM from text. Older builds stored the unit in the text, so strip it defensively.
        juce::String text = clipBpmValue_.getText().trimCharactersAtEnd(" BPMbpm");
        double newBPM = text.getDoubleValue();
        if (newBPM < 20.0 || newBPM > 999.0)
            return;

        double bpm = timelineController_ ? timelineController_->getState().tempo.bpm : 120.0;

        // BPM and Beats are two editable views of the same fixed-duration source
        // interpretation. Editing the BPM must keep totalBeats coherent against
        // the same file duration so the inspector doesn't display the previous
        // (often project-BPM-derived) beat count under the new tempo.
        //
        // The user setting the BPM is asserting "this file is N BPM" — that is
        // the authoritative musical interpretation. totalBeats = fileDuration ×
        // newBPM / 60, regardless of autoTempo (autoTempo controls playback
        // stretching, not the interpretation metadata).
        double durationSeconds = clip->audio().source.durationSeconds;
        double thumbDuration = 0.0;
        if (auto* thumb = magda::AudioThumbnailManager::getInstance().getThumbnail(
                clip->audio().source.filePath)) {
            thumbDuration = thumb->getTotalLength();
            if (thumbDuration > 0.0)
                durationSeconds = thumbDuration;
        }
        if (durationSeconds <= 0.0)
            durationSeconds = clip->getSourceLength();

        if (clip->autoTempo) {
            magda::ClipManager::AudioClipBeatsUpdate u;
            u.interpretationBpm = newBPM;
            if (thumbDuration > 0.0 && clip->audio().source.durationSeconds <= 0.0)
                u.sourceDurationSeconds = thumbDuration;
            if (durationSeconds > 0.0) {
                u.interpretationTotalBeats = durationSeconds * newBPM / 60.0;
                u.lockInterpretationTotalBeats = true;
            }
            magda::ClipManager::getInstance().applyAudioClipBeats(primaryClipId(), u, bpm);
        } else {
            // Non-autoTempo audio: source interpretation is stored metadata,
            // not playback-affecting, but the inspector reads it for display
            // and tooling (autoTempo toggle, future stretch correctness, etc.)
            // so the BPM-and-totalBeats pair must stay coherent here too.
            clip->audio().interpretation.bpm = newBPM;
            if (thumbDuration > 0.0 && clip->audio().source.durationSeconds <= 0.0)
                clip->audio().source.durationSeconds = thumbDuration;
            if (durationSeconds > 0.0) {
                clip->audio().interpretation.totalBeats = durationSeconds * newBPM / 60.0;
                clip->audio().interpretation.totalBeatsLocked = true;
            }
            magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(primaryClipId());
        }

        clipBpmValue_.setText(juce::String(newBPM, 1), juce::dontSendNotification);
        updateFromSelectedClip();
    };
    clipPropsContainer_.addChildComponent(clipBpmValue_);

    clipBpmUnitLabel_.setText("BPM", juce::dontSendNotification);
    clipBpmUnitLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipBpmUnitLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipBpmUnitLabel_.setJustificationType(juce::Justification::centredLeft);
    clipPropsContainer_.addChildComponent(clipBpmUnitLabel_);

    // Source interpretation total beats (shown next to source BPM when auto-tempo is enabled)
    clipBeatsLengthValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    clipBeatsLengthValue_->setRange(0.25, 4096.0, 4.0);
    clipBeatsLengthValue_->setSuffix("");
    clipBeatsLengthValue_->setDecimalPlaces(2);
    clipBeatsLengthValue_->setSnapToInteger(true);
    clipBeatsLengthValue_->setDrawBackground(false);
    clipBeatsLengthValue_->setDrawBorder(true);
    clipBeatsLengthValue_->setShowFillIndicator(false);
    clipBeatsLengthValue_->onValueChange = [this]() {
        if (primaryClipId() != magda::INVALID_CLIP_ID) {
            auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
            if (clip && clip->autoTempo) {
                double newSourceBeats = clipBeatsLengthValue_->getValue();
                double projectBpm =
                    timelineController_ ? timelineController_->getState().tempo.bpm : 120.0;

                double durationSeconds = clip->audio().source.durationSeconds;
                if (durationSeconds <= 0.0) {
                    if (auto* thumb = magda::AudioThumbnailManager::getInstance().getThumbnail(
                            clip->audio().source.filePath)) {
                        durationSeconds = thumb->getTotalLength();
                    }
                }

                magda::ClipManager::AudioClipBeatsUpdate u;
                u.interpretationTotalBeats = newSourceBeats;
                u.lockInterpretationTotalBeats = true;
                if (durationSeconds > 0.0)
                    u.interpretationBpm = newSourceBeats * 60.0 / durationSeconds;
                if (durationSeconds > 0.0 && clip->audio().source.durationSeconds <= 0.0)
                    u.sourceDurationSeconds = durationSeconds;

                magda::ClipManager::getInstance().applyAudioClipBeats(primaryClipId(), u,
                                                                      projectBpm);
            }
        }
    };
    clipPropsContainer_.addChildComponent(*clipBeatsLengthValue_);

    clipBeatsUnitLabel_.setText("Beats", juce::dontSendNotification);
    clipBeatsUnitLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipBeatsUnitLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipBeatsUnitLabel_.setJustificationType(juce::Justification::centredLeft);
    clipPropsContainer_.addChildComponent(clipBeatsUnitLabel_);

    // Position icon (static, non-interactive)
    clipPositionIcon_ = std::make_unique<magda::SvgButton>("Position", BinaryData::position_svg,
                                                           BinaryData::position_svgSize);
    clipPositionIcon_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    clipPositionIcon_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    clipPositionIcon_->setIconPadding(1.0f);
    clipPositionIcon_->setInterceptsMouseClicks(false, false);
    clipPropsContainer_.addChildComponent(*clipPositionIcon_);

    // Row labels for position grid
    playbackColumnLabel_.setText("position", juce::dontSendNotification);
    playbackColumnLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    playbackColumnLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(playbackColumnLabel_);

    loopColumnLabel_.setText("loop", juce::dontSendNotification);
    loopColumnLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    loopColumnLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(loopColumnLabel_);

    // Clip start
    clipStartLabel_.setText("start", juce::dontSendNotification);
    clipStartLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipStartLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipStartLabel_);

    clipStartValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipStartValue_->setRange(0.0, 10000.0, 0.0);
    clipStartValue_->setDoubleClickResetsValue(false);
    clipStartValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        double bpm = 120.0;
        if (timelineController_) {
            bpm = timelineController_->getState().tempo.bpm;
        }
        double currentValue = clipStartValue_->getValue();
        double deltaBeats = currentValue - multiStartDragStart_;
        double deltaSeconds = magda::TimelineUtils::beatsToSeconds(deltaBeats, bpm);
        magda::ClipBatchEdit batch("Move Clips", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->view != magda::ClipView::Session) {
                double newStart = juce::jmax(0.0, timelineStartSeconds(*c, bpm) + deltaSeconds);
                batch.execute(std::make_unique<magda::MoveClipCommand>(
                    cid, magda::BeatPosition{newStart * bpm / 60.0}, bpm));
            }
        }
        multiStartDragStart_ = currentValue;
    };
    clipPropsContainer_.addChildComponent(*clipStartValue_);

    // Clip end
    clipEndLabel_.setText("end", juce::dontSendNotification);
    clipEndLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipEndLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipEndLabel_);

    clipEndValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipEndValue_->setRange(0.0, 10000.0, 4.0);
    clipEndValue_->setDoubleClickResetsValue(false);
    clipEndValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        double bpm = 120.0;
        if (timelineController_) {
            bpm = timelineController_->getState().tempo.bpm;
        }
        double currentValue = clipEndValue_->getValue();
        double deltaBeats = currentValue - multiEndDragStart_;
        double deltaSeconds = magda::TimelineUtils::beatsToSeconds(deltaBeats, bpm);
        magda::ClipBatchEdit batch("Resize Clips", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->view != magda::ClipView::Session) {
                double newLength = juce::jmax(0.0, timelineLengthSeconds(*c, bpm) + deltaSeconds);
                batch.execute(std::make_unique<magda::ResizeClipCommand>(
                    cid, magda::BeatDuration{newLength * bpm / 60.0}, false, bpm));
            }
        }
        multiEndDragStart_ = currentValue;
    };
    clipPropsContainer_.addChildComponent(*clipEndValue_);

    // Clip length (shown in position row, 3rd column)
    clipLengthLabel_.setText("len", juce::dontSendNotification);
    clipLengthLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipLengthLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipLengthLabel_);

    clipLengthValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipLengthValue_->setRange(0.25, 10000.0, 4.0);
    clipLengthValue_->setBarsBeatsIsPosition(false);
    clipLengthValue_->setDoubleClickResetsValue(false);
    clipLengthValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        double bpm = 120.0;
        if (timelineController_) {
            bpm = timelineController_->getState().tempo.bpm;
        }
        const double currentValue = clipLengthValue_->getValue();
        const double deltaBeats = currentValue - multiLengthDragStart_;
        const double deltaSeconds = magda::TimelineUtils::beatsToSeconds(deltaBeats, bpm);
        magda::ClipBatchEdit batch("Resize Clips", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->view != magda::ClipView::Session) {
                const double newLength = juce::jmax(magda::ClipOperations::MIN_CLIP_LENGTH,
                                                    timelineLengthSeconds(*c, bpm) + deltaSeconds);
                batch.execute(std::make_unique<magda::ResizeClipCommand>(
                    cid, magda::BeatDuration{newLength * bpm / 60.0}, false, bpm));
            }
        }
        multiLengthDragStart_ = currentValue;
    };
    clipPropsContainer_.addChildComponent(*clipLengthValue_);

    // Loop toggle (dual icon: clip_loop_off / clip_loop_on)
    clipLoopToggle_ = std::make_unique<magda::SvgButton>(
        "Loop", BinaryData::clip_loop_off_svg, BinaryData::clip_loop_off_svgSize,
        BinaryData::clip_loop_on_svg, BinaryData::clip_loop_on_svgSize);
    clipLoopToggle_->setClickingTogglesState(false);
    clipLoopToggle_->onClick = [this]() {
        if (selectedClipIds_.empty())
            return;
        auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;

        // Beat mode owns looping. Keep the button visually active, but don't let a click
        // make the model appear to toggle out from under auto-tempo playback.
        if (clip->autoTempo)
            return;

        bool newState = !clipLoopToggle_->isActive();
        clipLoopToggle_->setActive(newState);
        double bpm = 120.0;
        if (timelineController_) {
            bpm = timelineController_->getState().tempo.bpm;
        }
        magda::ClipBatchEdit batch("Set Clip Loop", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Loop", [newState, bpm](auto& manager, magda::ClipId id) {
                    manager.setClipLoopEnabled(id, newState, bpm);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(*clipLoopToggle_);

    // Audio clip properties collapse toggle
    audioPropsCollapseToggle_.setButtonText(juce::String::charToString(
        audioPropsCollapsed_ ? (juce::juce_wchar)0x25B6 : (juce::juce_wchar)0x25BC));
    audioPropsCollapseToggle_.setColour(juce::TextButton::buttonColourId,
                                        juce::Colours::transparentBlack);
    audioPropsCollapseToggle_.setColour(juce::TextButton::buttonOnColourId,
                                        juce::Colours::transparentBlack);
    audioPropsCollapseToggle_.setColour(juce::TextButton::textColourOffId,
                                        DarkTheme::getSecondaryTextColour());
    audioPropsCollapseToggle_.setColour(juce::TextButton::textColourOnId,
                                        DarkTheme::getSecondaryTextColour());
    audioPropsCollapseToggle_.setConnectedEdges(
        juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
        juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    audioPropsCollapseToggle_.onClick = [this]() {
        audioPropsCollapsed_ = !audioPropsCollapsed_;
        audioPropsCollapseToggle_.setButtonText(juce::String::charToString(
            audioPropsCollapsed_ ? (juce::juce_wchar)0x25B6 : (juce::juce_wchar)0x25BC));
        updateFromSelectedClip();
    };
    clipPropsContainer_.addChildComponent(audioPropsCollapseToggle_);

    audioPropsLabel_.setText("Audio Properties", juce::dontSendNotification);
    audioPropsLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    audioPropsLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    audioPropsLabel_.setJustificationType(juce::Justification::centredLeft);
    clipPropsContainer_.addChildComponent(audioPropsLabel_);

    // Warp toggle (pin icon)
    clipWarpToggle_.setButtonText("WARP");
    clipWarpToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    clipWarpToggle_.setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE));
    clipWarpToggle_.setColour(juce::TextButton::buttonOnColourId,
                              DarkTheme::getAccentColour().withAlpha(0.3f));
    clipWarpToggle_.setColour(juce::TextButton::textColourOffId,
                              DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    clipWarpToggle_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    clipWarpToggle_.setClickingTogglesState(false);
    clipWarpToggle_.onClick = [this]() {
        if (selectedClipIds_.empty())
            return;
        bool newState = !clipWarpToggle_.getToggleState();
        clipWarpToggle_.setToggleState(newState, juce::dontSendNotification);
        magda::ClipBatchEdit batch("Set Clip Warp", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Warp", [newState](auto& manager, magda::ClipId id) {
                    manager.setClipWarpEnabled(id, newState);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(clipWarpToggle_);

    // Auto-tempo (beat mode) toggle
    clipAutoTempoToggle_.setButtonText("BEAT");
    clipAutoTempoToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    clipAutoTempoToggle_.setColour(juce::TextButton::buttonColourId,
                                   DarkTheme::getColour(DarkTheme::SURFACE));
    clipAutoTempoToggle_.setColour(juce::TextButton::buttonOnColourId,
                                   DarkTheme::getAccentColour().withAlpha(0.3f));
    clipAutoTempoToggle_.setColour(juce::TextButton::textColourOffId,
                                   DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    clipAutoTempoToggle_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    clipAutoTempoToggle_.setClickingTogglesState(false);
    clipAutoTempoToggle_.setTooltip(
        "Lock clip to musical time (bars/beats) instead of absolute time.\n"
        "Clip length changes with tempo to maintain fixed beat length.");

    auto getSelectedAudioClipIds = [this]() {
        std::vector<magda::ClipId> clipIds;
        for (auto cid : selectedClipIds_) {
            const auto* clip = magda::ClipManager::getInstance().getClip(cid);
            if (clip && clip->isAudio())
                clipIds.push_back(cid);
        }
        return clipIds;
    };

    auto seedSourceInterpretation = [](magda::ClipId cid, auto& clip, double bpm) {
        // When enabling, seed source interpretation BPM/source interpretation total beats from
        // detected BPM (AudioThumbnailManager) since the clip model may have stale metadata from
        // TE's default loopInfo. Cached value is applied immediately (pre-setAutoTempo, before the
        // canonical path is open); cache miss kicks off background detection and the callback
        // funnels through applyAudioClipBeats once autoTempo is on.
        const bool sourceInterpretationBpmLooksDefaulted =
            clip.audio().interpretation.bpm <= 0.0 ||
            (magda::isValidBpm(bpm) && std::abs(clip.audio().interpretation.bpm - bpm) < 0.1);
        if (!sourceInterpretationBpmLooksDefaulted)
            return;

        // Issue #1157: only seed from AudioThumbnailManager when the file
        // didn't carry tempo metadata. setSourceMetadata (from TE's
        // loopInfo) is authoritative when present; TempoDetect can be
        // wrong by ~1.3x on syncopated loops.
        auto& thumbs = magda::AudioThumbnailManager::getInstance();
        double cached = thumbs.getCachedBPM(clip.audio().source.filePath);
        if (cached > 0.0) {
            clip.audio().interpretation.bpm = cached;
            if (auto* thumb = thumbs.getThumbnail(clip.audio().source.filePath)) {
                double fileDuration = thumb->getTotalLength();
                if (fileDuration > 0.0) {
                    if (clip.audio().source.durationSeconds <= 0.0)
                        clip.audio().source.durationSeconds = fileDuration;
                    clip.audio().interpretation.totalBeats = fileDuration * cached / 60.0;
                }
            }
        } else {
            thumbs.requestBPMDetection(clip.audio().source.filePath, [cid](double detectedBPM) {
                if (detectedBPM <= 0.0)
                    return;
                auto& mgr = magda::ClipManager::getInstance();
                auto* c = mgr.getClip(cid);
                if (!c)
                    return;
                // Issue #1157: file metadata wins over audio analysis.
                // TempoDetect can be wrong by ~1.3x on syncopated loops.
                double live = magda::ProjectManager::getInstance().getCurrentProjectInfo().tempo;
                bool existingLooksDefaulted = c->audio().interpretation.bpm > 0.0 &&
                                              magda::isValidBpm(live) &&
                                              std::abs(c->audio().interpretation.bpm - live) < 0.1;
                if (c->audio().interpretation.bpm > 0.0 && !existingLooksDefaulted)
                    return;
                magda::ClipManager::AudioClipBeatsUpdate u;
                u.interpretationBpm = detectedBPM;
                if (auto* thumb = magda::AudioThumbnailManager::getInstance().getThumbnail(
                        c->audio().source.filePath)) {
                    double fileDuration = thumb->getTotalLength();
                    if (fileDuration > 0.0) {
                        u.sourceDurationSeconds = fileDuration;
                        u.interpretationTotalBeats = fileDuration * detectedBPM / 60.0;
                    }
                }
                mgr.applyAudioClipBeats(cid, u, live);
            });
        }
    };

    // Helper lambda: apply auto-tempo state change and sync
    auto applyAutoTempo =
        [this, seedSourceInterpretation](bool enable, const std::vector<magda::ClipId>& clipIds) {
            double bpm = 120.0;
            if (timelineController_) {
                bpm = timelineController_->getState().tempo.bpm;
            }

            magda::ClipBatchEdit batch("Set Clip Beat Mode", clipIds.size());
            for (auto cid : clipIds) {
                auto* clip = magda::ClipManager::getInstance().getClip(cid);
                if (!clip || !clip->isAudio())
                    continue;
                batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                    cid, "Set Clip Beat Mode",
                    [enable, bpm, seedSourceInterpretation](auto& manager, magda::ClipId id) {
                        auto* targetClip = manager.getClip(id);
                        if (!targetClip || !targetClip->isAudio())
                            return;
                        if (enable)
                            seedSourceInterpretation(id, *targetClip, bpm);
                        manager.setAutoTempo(id, enable, bpm);
                    }));
            }
            updateFromSelectedClip();
        };

    clipAutoTempoToggle_.onClick = [this, getSelectedAudioClipIds, applyAutoTempo]() {
        if (primaryClipId() == magda::INVALID_CLIP_ID)
            return;
        auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip || !clip->isAudio())
            return;

        auto targetClipIds = getSelectedAudioClipIds();
        if (targetClipIds.empty())
            return;

        bool newState = !clip->autoTempo;

        int clipsNeedingStretchReset = 0;
        for (auto cid : targetClipIds) {
            const auto* selectedClip = magda::ClipManager::getInstance().getClip(cid);
            if (selectedClip && std::abs(selectedClip->speedRatio - 1.0) > 0.001)
                ++clipsNeedingStretchReset;
        }

        if (newState && clipsNeedingStretchReset > 0) {
            // Show async warning — avoid re-entrancy from synchronous modal loop
            juce::String message = "Auto-tempo mode requires speed ratio 1.0.\n";
            if (clipsNeedingStretchReset == 1 && targetClipIds.size() == 1) {
                message << "Current stretch (" << juce::String(clip->speedRatio, 2)
                        << "x) will be reset.\n\nContinue?";
            } else {
                message << juce::String(clipsNeedingStretchReset)
                        << (clipsNeedingStretchReset == 1
                                ? " selected clip will have stretch reset.\n\nContinue?"
                                : " selected clips will have stretch reset.\n\nContinue?");
            }
            juce::NativeMessageBox::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle("Reset Time Stretch")
                    .withMessage(message)
                    .withButton("OK")
                    .withButton("Cancel"),
                [applyAutoTempo, targetClipIds](int result) {
                    if (result == 0)
                        applyAutoTempo(true, targetClipIds);
                });
            return;
        }

        applyAutoTempo(newState, targetClipIds);
    };
    clipPropsContainer_.addChildComponent(clipAutoTempoToggle_);

    clipStretchValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    clipStretchValue_->setRange(0.25, 4.0, 1.0);
    clipStretchValue_->setDecimalPlaces(3);
    clipStretchValue_->setSuffix("x");
    clipStretchValue_->setDrawBackground(false);
    clipStretchValue_->setDrawBorder(true);
    clipStretchValue_->setShowFillIndicator(false);
    clipStretchValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        double currentValue = clipStretchValue_->getValue();
        double delta = currentValue - multiSpeedRatioDragStart_;
        magda::ClipBatchEdit batch("Set Clip Speed", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isAudio()) {
                double newVal = juce::jlimit(0.25, 4.0, c->speedRatio + delta);
                batch.execute(std::make_unique<magda::SetClipSpeedRatioCommand>(cid, newVal));
            }
        }
        multiSpeedRatioDragStart_ = currentValue;
        computeClipRange();
        refreshClipRangeDisplay();
    };
    clipPropsContainer_.addChildComponent(*clipStretchValue_);

    // Stretch mode selector (algorithm)
    stretchModeCombo_.setColour(juce::ComboBox::backgroundColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
    stretchModeCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    stretchModeCombo_.setColour(juce::ComboBox::outlineColourId,
                                DarkTheme::getColour(DarkTheme::BORDER));
    // Mode values match TimeStretcher::Mode enum (combo ID = mode + 1)
    stretchModeCombo_.addItem("Off", 1);            // disabled = 0
    stretchModeCombo_.addItem("SoundTouch", 4);     // soundtouchNormal = 3
    stretchModeCombo_.addItem("SoundTouch HQ", 5);  // soundtouchBetter = 4
    stretchModeCombo_.setSelectedId(1, juce::dontSendNotification);
    stretchModeCombo_.onChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        int mode = stretchModeCombo_.getSelectedId() - 1;
        magda::ClipBatchEdit batch("Set Clip Stretch Mode", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipStretchModeCommand>(cid, mode));
        }
    };
    clipPropsContainer_.addChildComponent(stretchModeCombo_);

    // Apply themed LookAndFeel to all inspector combo boxes
    stretchModeCombo_.setLookAndFeel(&InspectorComboBoxLookAndFeel::getInstance());
    autoPitchModeCombo_.setLookAndFeel(&InspectorComboBoxLookAndFeel::getInstance());
    launchModeCombo_.setLookAndFeel(&InspectorComboBoxLookAndFeel::getInstance());
    launchQuantizeCombo_.setLookAndFeel(&InspectorComboBoxLookAndFeel::getInstance());
    followActionCombo_.setLookAndFeel(&InspectorComboBoxLookAndFeel::getInstance());

    // Loop start
    clipLoopStartLabel_.setText("start", juce::dontSendNotification);
    clipLoopStartLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipLoopStartLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipLoopStartLabel_);

    clipLoopStartValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipLoopStartValue_->setRange(0.0, 10000.0, 0.0);
    clipLoopStartValue_->setDoubleClickResetsValue(true);
    clipLoopStartValue_->onValueChange = [this]() {
        if (primaryClipId() == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;

        double bpm = 120.0;
        if (timelineController_) {
            bpm = timelineController_->getState().tempo.bpm;
        }
        double currentPhase = clip->getSourceOffset() - clip->getSourceLoopStart();
        double newLoopStartBeats = clipLoopStartValue_->getValue();
        double newLoopStartSeconds =
            clip->isAudio() ? displayBeatsToAudioSourceSeconds(*clip, newLoopStartBeats, bpm)
                            : magda::TimelineUtils::beatsToSeconds(newLoopStartBeats, bpm);
        newLoopStartSeconds = std::max(0.0, newLoopStartSeconds);
        double newOffset = newLoopStartSeconds + currentPhase;
        // Atomic: change loopStart, then place offset to preserve phase. Undo
        // collapses both in a single step.
        magda::UndoManager::getInstance().beginCompoundOperation("Set Clip Loop Start");
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipLoopStartCommand>(primaryClipId(), newLoopStartSeconds,
                                                             bpm));
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipOffsetCommand>(primaryClipId(), newOffset));
        magda::UndoManager::getInstance().endCompoundOperation();
    };
    clipPropsContainer_.addChildComponent(*clipLoopStartValue_);

    // Loop end (derived: loopStart + loopLength)
    clipLoopEndLabel_.setText("end", juce::dontSendNotification);
    clipLoopEndLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipLoopEndLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipLoopEndLabel_);

    clipLoopEndValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipLoopEndValue_->setRange(0.25, 10000.0, 4.0);
    clipLoopEndValue_->setDoubleClickResetsValue(false);
    clipLoopEndValue_->onValueChange = [this]() {
        if (primaryClipId() == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;

        double bpm = 120.0;
        if (timelineController_) {
            bpm = timelineController_->getState().tempo.bpm;
        }

        // Compute new loop length from loop end - loop start.
        double newLoopEndBeats = clipLoopEndValue_->getValue();

        double newLoopLengthSeconds;
        if (clip->isAudio()) {
            const double newLoopEndSeconds =
                displayBeatsToAudioSourceSeconds(*clip, newLoopEndBeats, bpm);
            newLoopLengthSeconds = juce::jmax(0.0, newLoopEndSeconds - clip->getSourceLoopStart());
        } else {
            double loopStartBeats = magda::TimelineUtils::secondsToBeats(clip->loopStart, bpm);
            double newLoopLengthBeats = newLoopEndBeats - loopStartBeats;
            if (newLoopLengthBeats < 0.25)
                newLoopLengthBeats = 0.25;
            newLoopLengthSeconds = magda::TimelineUtils::beatsToSeconds(newLoopLengthBeats, bpm);
        }
        newLoopLengthSeconds =
            juce::jmax(magda::ClipOperations::MIN_SOURCE_LENGTH, newLoopLengthSeconds);

        bool shouldResizeClip = false;
        double resizeLengthSeconds = 0.0;

        if (clip->view == magda::ClipView::Session) {
            double clipEndSeconds = timelineLengthSeconds(*clip, bpm);
            const double sourceLoopStart = clip->getSourceLoopStart();
            const double sourceLoopLength = clip->getSourceLoopLength();
            double currentSourceEnd = sourceLoopStart + sourceLoopLength;
            bool sourceEndMatchedClipEnd = std::abs(currentSourceEnd - clipEndSeconds) < 0.001;
            double newSourceEnd = sourceLoopStart + newLoopLengthSeconds;

            if (sourceEndMatchedClipEnd && newSourceEnd > clipEndSeconds) {
                shouldResizeClip = true;
                resizeLengthSeconds = newSourceEnd;
            } else {
                if (newSourceEnd > clipEndSeconds) {
                    newLoopLengthSeconds = clipEndSeconds - sourceLoopStart;
                }
            }
        }

        magda::ClipBatchEdit batch("Set Clip Loop End", shouldResizeClip ? 2u : 1u);
        if (shouldResizeClip) {
            batch.execute(std::make_unique<magda::ResizeClipCommand>(
                primaryClipId(), magda::BeatDuration{resizeLengthSeconds * bpm / 60.0}, false,
                bpm));
        }

        batch.execute(std::make_unique<magda::SetClipLoopLengthCommand>(primaryClipId(),
                                                                        newLoopLengthSeconds, bpm));
    };
    clipPropsContainer_.addChildComponent(*clipLoopEndValue_);

    // Source read offset / loop phase (same model field, mode-specific label)
    clipLoopPhaseLabel_.setText("phase", juce::dontSendNotification);
    clipLoopPhaseLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipLoopPhaseLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipLoopPhaseLabel_);

    clipLoopPhaseValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipLoopPhaseValue_->setRange(0.0, 10000.0, 0.0);
    clipLoopPhaseValue_->setBarsBeatsIsPosition(false);
    clipLoopPhaseValue_->setDoubleClickResetsValue(true);
    clipLoopPhaseValue_->onValueChange = [this]() {
        if (primaryClipId() == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;

        double newPhaseOrOffsetBeats = std::max(0.0, clipLoopPhaseValue_->getValue());
        const bool loopOn =
            clip->view == magda::ClipView::Session || clip->loopEnabled || clip->autoTempo;
        if (clip->isMidi()) {
            // MIDI phase/offset lives in midiOffset (beats)
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetClipOffsetCommand>(primaryClipId(),
                                                              newPhaseOrOffsetBeats));
        } else {
            double bpm = 120.0;
            if (timelineController_) {
                bpm = timelineController_->getState().tempo.bpm;
            }
            double newSeconds = displayBeatsToAudioSourceSeconds(*clip, newPhaseOrOffsetBeats, bpm);
            if (loopOn) {
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetClipLoopPhaseCommand>(primaryClipId(), newSeconds));
            } else {
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetClipOffsetCommand>(primaryClipId(), newSeconds));
            }
        }
    };
    clipPropsContainer_.addChildComponent(*clipLoopPhaseValue_);
}

// ========================================================================
// Session clip launch properties
// ========================================================================

void ClipInspector::initSessionLaunchSection() {
    launchModeLabel_.setText("Launch Mode", juce::dontSendNotification);
    launchModeLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    launchModeLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(launchModeLabel_);

    launchModeCombo_.addItem("Trigger", 1);
    launchModeCombo_.addItem("Toggle", 2);
    launchModeCombo_.setColour(juce::ComboBox::backgroundColourId,
                               DarkTheme::getColour(DarkTheme::SURFACE));
    launchModeCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    launchModeCombo_.setColour(juce::ComboBox::outlineColourId,
                               DarkTheme::getColour(DarkTheme::SEPARATOR));
    launchModeCombo_.onChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        auto mode = static_cast<magda::LaunchMode>(launchModeCombo_.getSelectedId() - 1);
        magda::ClipBatchEdit batch("Set Clip Launch Mode", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Launch Mode",
                [mode](auto& manager, magda::ClipId id) { manager.setClipLaunchMode(id, mode); }));
        }
    };
    clipPropsContainer_.addChildComponent(launchModeCombo_);

    launchQuantizeLabel_.setText("Launch Quantize", juce::dontSendNotification);
    launchQuantizeLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    launchQuantizeLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(launchQuantizeLabel_);

    launchQuantizeCombo_.addItem("None", 1);
    launchQuantizeCombo_.addItem("8 Bars", 2);
    launchQuantizeCombo_.addItem("4 Bars", 3);
    launchQuantizeCombo_.addItem("2 Bars", 4);
    launchQuantizeCombo_.addItem("1 Bar", 5);
    launchQuantizeCombo_.addItem("1/2", 6);
    launchQuantizeCombo_.addItem("1/4", 7);
    launchQuantizeCombo_.addItem("1/8", 8);
    launchQuantizeCombo_.addItem("1/16", 9);
    launchQuantizeCombo_.setColour(juce::ComboBox::backgroundColourId,
                                   DarkTheme::getColour(DarkTheme::SURFACE));
    launchQuantizeCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    launchQuantizeCombo_.setColour(juce::ComboBox::outlineColourId,
                                   DarkTheme::getColour(DarkTheme::SEPARATOR));
    launchQuantizeCombo_.onChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        auto quantize =
            static_cast<magda::LaunchQuantize>(launchQuantizeCombo_.getSelectedId() - 1);
        magda::ClipBatchEdit batch("Set Clip Launch Quantize", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Launch Quantize", [quantize](auto& manager, magda::ClipId id) {
                    manager.setClipLaunchQuantize(id, quantize);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(launchQuantizeCombo_);

    followActionLabel_.setText("Follow Action", juce::dontSendNotification);
    followActionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    followActionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(followActionLabel_);

    followActionCombo_.addItem("None", 1);
    followActionCombo_.addItem("Play Next", 2);
    followActionCombo_.addItem("Play Previous", 3);
    followActionCombo_.addItem("Play Random", 4);
    followActionCombo_.addItem("Stop", 5);
    followActionCombo_.addItem("Play Again", 6);
    followActionCombo_.setColour(juce::ComboBox::backgroundColourId,
                                 DarkTheme::getColour(DarkTheme::SURFACE));
    followActionCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    followActionCombo_.setColour(juce::ComboBox::outlineColourId,
                                 DarkTheme::getColour(DarkTheme::SEPARATOR));
    followActionCombo_.onChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        auto action = static_cast<magda::FollowAction>(followActionCombo_.getSelectedId() - 1);
        magda::ClipBatchEdit batch("Set Clip Follow Action", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Follow Action", [action](auto& manager, magda::ClipId id) {
                    manager.setClipFollowAction(id, action);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(followActionCombo_);

    followActionDelayLabel_.setText("Follow Delay (beats)", juce::dontSendNotification);
    followActionDelayLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    followActionDelayLabel_.setColour(juce::Label::textColourId,
                                      DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(followActionDelayLabel_);

    followActionDelaySlider_.setRange(0.0, 64.0, 0.25);
    followActionDelaySlider_.setOrientation(TextSlider::Orientation::Horizontal);
    followActionDelaySlider_.setDefaultValue(0.0);
    followActionDelaySlider_.onValueChanged = [this](double delayBeats) {
        if (selectedClipIds_.empty())
            return;
        magda::ClipBatchEdit batch("Set Clip Follow Delay", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Follow Delay", [delayBeats](auto& manager, magda::ClipId id) {
                    manager.setClipFollowActionDelayBeats(id, delayBeats);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(followActionDelaySlider_);

    followActionLoopCountLabel_.setText("Follow Loops", juce::dontSendNotification);
    followActionLoopCountLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    followActionLoopCountLabel_.setColour(juce::Label::textColourId,
                                          DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(followActionLoopCountLabel_);

    followActionLoopCountSlider_.setRange(1.0, 64.0, 1.0);
    followActionLoopCountSlider_.setOrientation(TextSlider::Orientation::Horizontal);
    followActionLoopCountSlider_.setDefaultValue(1.0);
    followActionLoopCountSlider_.setValueFormatter(
        [](double value) { return juce::String(static_cast<int>(std::round(value))); });
    followActionLoopCountSlider_.setValueParser([](const juce::String& text) {
        return static_cast<double>(static_cast<int>(std::round(text.getDoubleValue())));
    });
    followActionLoopCountSlider_.onValueChanged = [this](double value) {
        if (selectedClipIds_.empty())
            return;
        const int loopCount = static_cast<int>(std::round(value));
        magda::ClipBatchEdit batch("Set Clip Follow Loops", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Follow Loops", [loopCount](auto& manager, magda::ClipId id) {
                    manager.setClipFollowActionLoopCount(id, loopCount);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(followActionLoopCountSlider_);
}

// ========================================================================
// Clip properties viewport (scrollable container)
// ========================================================================

void ClipInspector::initViewport() {
    clipPropsViewport_.setViewedComponent(&clipPropsContainer_, false);
    clipPropsViewport_.setScrollBarsShown(true, false);
    addChildComponent(clipPropsViewport_);
}

// ========================================================================
// Pitch section
// ========================================================================

void ClipInspector::initPitchSection() {
    pitchSectionLabel_.setText("Pitch", juce::dontSendNotification);
    pitchSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    pitchSectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(pitchSectionLabel_);

    autoPitchToggle_.setButtonText("AUTO-PITCH");
    autoPitchToggle_.setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::SURFACE));
    autoPitchToggle_.setColour(juce::TextButton::buttonOnColourId,
                               DarkTheme::getAccentColour().withAlpha(0.3f));
    autoPitchToggle_.setColour(juce::TextButton::textColourOffId,
                               DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    autoPitchToggle_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    autoPitchToggle_.onClick = [this]() {
        if (selectedClipIds_.empty())
            return;
        auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;
        bool newState = !clip->autoPitch;
        magda::ClipBatchEdit batch("Set Clip Auto Pitch", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Auto Pitch", [newState](auto& manager, magda::ClipId id) {
                    manager.setAutoPitch(id, newState);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(autoPitchToggle_);

    analogPitchToggle_.setButtonText("ANALOG");
    analogPitchToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    analogPitchToggle_.setColour(juce::TextButton::buttonColourId,
                                 DarkTheme::getColour(DarkTheme::SURFACE));
    analogPitchToggle_.setColour(juce::TextButton::buttonOnColourId,
                                 DarkTheme::getAccentColour().withAlpha(0.3f));
    analogPitchToggle_.setColour(juce::TextButton::textColourOffId,
                                 DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    analogPitchToggle_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    analogPitchToggle_.setTooltip(
        "Analog pitch shift: resample instead of time-stretch.\n"
        "Changes playback speed to change pitch (tape/vinyl/sampler behavior).");
    analogPitchToggle_.onClick = [this]() {
        if (selectedClipIds_.empty())
            return;
        auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;
        bool newState = !clip->analogPitch;
        magda::ClipBatchEdit batch("Set Clip Analog Pitch", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Analog Pitch", [newState](auto& manager, magda::ClipId id) {
                    manager.setAnalogPitch(id, newState);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(analogPitchToggle_);

    autoPitchModeCombo_.setColour(juce::ComboBox::backgroundColourId,
                                  DarkTheme::getColour(DarkTheme::SURFACE));
    autoPitchModeCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    autoPitchModeCombo_.setColour(juce::ComboBox::outlineColourId,
                                  DarkTheme::getColour(DarkTheme::BORDER));
    autoPitchModeCombo_.addItem("Pitch Track", 1);
    autoPitchModeCombo_.addItem("Chord Mono", 2);
    autoPitchModeCombo_.addItem("Chord Poly", 3);
    autoPitchModeCombo_.setSelectedId(1, juce::dontSendNotification);
    autoPitchModeCombo_.onChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        int mode = autoPitchModeCombo_.getSelectedId() - 1;
        magda::ClipBatchEdit batch("Set Clip Auto Pitch Mode", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Auto Pitch Mode",
                [mode](auto& manager, magda::ClipId id) { manager.setAutoPitchMode(id, mode); }));
        }
    };
    clipPropsContainer_.addChildComponent(autoPitchModeCombo_);

    // MIDI transpose buttons (destructive: shift all notes up/down)
    auto transposeAction = [this](int direction, bool shift) {
        if (selectedClipIds_.empty())
            return;
        int semitones = direction * (shift ? 12 : 1);
        magda::ClipBatchEdit batch("Transpose MIDI Clips", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isMidi()) {
                batch.execute(std::make_unique<magda::TransposeMidiClipCommand>(cid, semitones));
            }
        }
    };

    midiTransposeLabel_.setText("Transpose", juce::dontSendNotification);
    midiTransposeLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    midiTransposeLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    midiTransposeLabel_.setJustificationType(juce::Justification::centredLeft);
    clipPropsContainer_.addChildComponent(midiTransposeLabel_);

    midiTransposeDownBtn_.setButtonText("-");
    midiTransposeDownBtn_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    midiTransposeDownBtn_.setColour(juce::TextButton::buttonColourId,
                                    DarkTheme::getColour(DarkTheme::SURFACE));
    midiTransposeDownBtn_.setColour(juce::TextButton::textColourOffId,
                                    DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    midiTransposeDownBtn_.setTooltip("Transpose down (Shift = octave)");
    midiTransposeDownBtn_.onClick = [transposeAction]() {
        transposeAction(-1, juce::ModifierKeys::currentModifiers.isShiftDown());
    };
    clipPropsContainer_.addChildComponent(midiTransposeDownBtn_);

    midiTransposeUpBtn_.setButtonText("+");
    midiTransposeUpBtn_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    midiTransposeUpBtn_.setColour(juce::TextButton::buttonColourId,
                                  DarkTheme::getColour(DarkTheme::SURFACE));
    midiTransposeUpBtn_.setColour(juce::TextButton::textColourOffId,
                                  DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    midiTransposeUpBtn_.setTooltip("Transpose up (Shift = octave)");
    midiTransposeUpBtn_.onClick = [transposeAction]() {
        transposeAction(1, juce::ModifierKeys::currentModifiers.isShiftDown());
    };
    clipPropsContainer_.addChildComponent(midiTransposeUpBtn_);

    pitchChangeValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    pitchChangeValue_->setRange(-48.0, 48.0, 0.0);
    pitchChangeValue_->setSuffix(" st");
    pitchChangeValue_->setDecimalPlaces(2);
    pitchChangeValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        double currentValue = pitchChangeValue_->getValue();
        double delta = currentValue - multiPitchChangeDragStart_;
        magda::ClipBatchEdit batch("Set Clip Pitch", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isAudio()) {
                float newVal =
                    juce::jlimit(-48.0f, 48.0f, c->pitchChange + static_cast<float>(delta));
                batch.execute(std::make_unique<magda::SetClipPitchCommand>(cid, newVal));
            }
        }
        multiPitchChangeDragStart_ = currentValue;
        computeClipRange();
        refreshClipRangeDisplay();
    };
    clipPropsContainer_.addChildComponent(*pitchChangeValue_);
}

// ========================================================================
// Groove/Shuffle/Swing section (MIDI clips only)
// ========================================================================

void ClipInspector::initGrooveSection() {
    grooveSectionLabel_.setText("Groove", juce::dontSendNotification);
    grooveSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    grooveSectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(grooveSectionLabel_);

    // Groove template picker button
    grooveTemplateButton_.setButtonText("None");
    grooveTemplateButton_.setColour(juce::TextButton::buttonColourId,
                                    DarkTheme::getColour(DarkTheme::SURFACE));
    grooveTemplateButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    grooveTemplateButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    grooveTemplateButton_.onClick = [this]() { showGroovePicker(); };
    clipPropsContainer_.addChildComponent(grooveTemplateButton_);

    // Groove strength slider
    grooveStrengthLabel_.setText("Strength", juce::dontSendNotification);
    grooveStrengthLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    grooveStrengthLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    grooveStrengthLabel_.setJustificationType(juce::Justification::centredLeft);
    clipPropsContainer_.addChildComponent(grooveStrengthLabel_);

    grooveStrengthValue_ =
        std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Percentage);
    grooveStrengthValue_->setRange(0.0, 1.0, 0.0);
    grooveStrengthValue_->setDecimalPlaces(0);
    grooveStrengthValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        float newStrength = static_cast<float>(grooveStrengthValue_->getValue());
        magda::ClipBatchEdit batch("Set Clip Groove Strength", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isMidi()) {
                batch.execute(
                    std::make_unique<magda::SetClipGrooveStrengthCommand>(cid, newStrength));
            }
        }
    };
    clipPropsContainer_.addChildComponent(*grooveStrengthValue_);
}

void ClipInspector::showGroovePicker() {
    using Category = GroovePickerPopup::Category;

    // Build category list from TE's GrooveTemplateManager
    std::vector<Category> categories;
    categories.push_back({"None", {"None"}});

    juce::String currentTemplate;
    if (!selectedClipIds_.empty()) {
        auto* c = magda::ClipManager::getInstance().getClip(*selectedClipIds_.begin());
        if (c)
            currentTemplate = c->grooveTemplate;
    }

    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    auto* teWrapper = dynamic_cast<magda::TracktionEngineWrapper*>(audioEngine);
    if (teWrapper && teWrapper->getEngine()) {
        auto& gtm = teWrapper->getEngine()->getGrooveTemplateManager();
        auto names = gtm.getTemplateNames();

        struct GroupDef {
            juce::String heading;
            juce::String prefix;
        };
        const GroupDef groupDefs[] = {
            {"Swing", "Swing"},          {"Swing", "Basic"},          {"Push Swing", "PushSwing"},
            {"Pull Swing", "PullSwing"}, {"Push Snare", "PushSnare"}, {"Pull Snare", "PullSnare"},
            {"Timing", "Slow"},          {"Timing", "Fast"},          {"Random", "random"},
            {"Random", "Random"},
        };

        std::map<juce::String, juce::StringArray> grouped;
        juce::StringArray uncategorized;

        for (const auto& n : names) {
            bool found = false;
            for (const auto& g : groupDefs) {
                if (n.startsWithIgnoreCase(g.prefix)) {
                    grouped[g.heading].add(n);
                    found = true;
                    break;
                }
            }
            if (!found)
                uncategorized.add(n);
        }

        const juce::String order[] = {"Swing",      "Push Swing", "Pull Swing", "Push Snare",
                                      "Pull Snare", "Timing",     "Random"};
        for (const auto& cat : order) {
            auto it = grouped.find(cat);
            if (it != grouped.end() && !it->second.isEmpty())
                categories.push_back({cat, it->second});
        }

        if (!uncategorized.isEmpty())
            categories.push_back({"Custom", uncategorized});
    }

    // Create popup and hand off to CallOutBox
    auto popup = std::make_unique<GroovePickerPopup>(*this);
    popup->populate(categories, currentTemplate.isEmpty() ? "None" : currentTemplate);
    popup->setSize(320, 220);

    auto buttonBounds = grooveTemplateButton_.getScreenBounds();
    juce::CallOutBox::launchAsynchronously(std::move(popup), buttonBounds, nullptr);
}

void ClipInspector::onGrooveTemplateSelected(const juce::String& templateName) {
    juce::String name = (templateName == "None") ? juce::String() : templateName;

    magda::ClipBatchEdit batch("Set Clip Groove Template", selectedClipIds_.size());
    for (auto cid : selectedClipIds_) {
        const auto* c = magda::ClipManager::getInstance().getClip(cid);
        if (c && c->isMidi()) {
            batch.execute(std::make_unique<magda::SetClipGrooveTemplateCommand>(cid, name));
        }
    }

    // Popup dismisses itself via findParentComponentOfClass<CallOutBox>()
}

// ========================================================================
// Per-Clip Mix section
// ========================================================================

void ClipInspector::initMixSection() {
    clipMixSectionLabel_.setText("Mix", juce::dontSendNotification);
    clipMixSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipMixSectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(clipMixSectionLabel_);

    clipVolumeValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Decibels);
    clipVolumeValue_->setRange(-100.0, 0.0, 0.0);
    clipVolumeValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        double currentValue = clipVolumeValue_->getValue();
        double delta = currentValue - multiVolumeDragStart_;
        magda::ClipBatchEdit batch("Set Clip Volume", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isAudio()) {
                float newVal = juce::jlimit(-100.0f, 0.0f, c->volumeDB + static_cast<float>(delta));
                batch.execute(std::make_unique<magda::SetClipVolumeDBCommand>(cid, newVal));
            }
        }
        multiVolumeDragStart_ = currentValue;
        computeClipRange();
        refreshClipRangeDisplay();
    };
    clipPropsContainer_.addChildComponent(*clipVolumeValue_);

    clipPanValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Pan);
    clipPanValue_->setRange(-1.0, 1.0, 0.0);
    clipPanValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        double currentValue = clipPanValue_->getValue();
        double delta = currentValue - multiPanDragStart_;
        magda::ClipBatchEdit batch("Set Clip Pan", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isAudio()) {
                float newVal = juce::jlimit(-1.0f, 1.0f, c->pan + static_cast<float>(delta));
                batch.execute(std::make_unique<magda::SetClipPanCommand>(cid, newVal));
            }
        }
        multiPanDragStart_ = currentValue;
        computeClipRange();
        refreshClipRangeDisplay();
    };
    clipPropsContainer_.addChildComponent(*clipPanValue_);

    clipGainValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    clipGainValue_->setRange(0.0, 24.0, 0.0);
    clipGainValue_->setSuffix(" dB");
    clipGainValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        double currentValue = clipGainValue_->getValue();
        double delta = currentValue - multiGainDragStart_;
        magda::ClipBatchEdit batch("Set Clip Gain", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isAudio()) {
                float newVal = juce::jlimit(0.0f, 24.0f, c->gainDB + static_cast<float>(delta));
                batch.execute(std::make_unique<magda::SetClipGainDBCommand>(cid, newVal));
            }
        }
        multiGainDragStart_ = currentValue;
        computeClipRange();
        refreshClipRangeDisplay();
    };
    clipPropsContainer_.addChildComponent(*clipGainValue_);
}

// ========================================================================
// Playback / Beat Detection section
// ========================================================================

void ClipInspector::initPlaybackSection() {
    beatDetectionSectionLabel_.setText("Playback", juce::dontSendNotification);
    beatDetectionSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    beatDetectionSectionLabel_.setColour(juce::Label::textColourId,
                                         DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(beatDetectionSectionLabel_);

    reverseToggle_.setButtonText("REVERSE");
    reverseToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    reverseToggle_.setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    reverseToggle_.setColour(juce::TextButton::buttonOnColourId,
                             DarkTheme::getAccentColour().withAlpha(0.3f));
    reverseToggle_.setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    reverseToggle_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    reverseToggle_.onClick = [this]() {
        if (selectedClipIds_.empty())
            return;
        auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;
        bool newState = !clip->isReversed;
        magda::ClipBatchEdit batch("Set Clip Reverse", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipReversedCommand>(cid, newState));
        }
    };
    clipPropsContainer_.addChildComponent(reverseToggle_);

    autoDetectBeatsToggle_.setButtonText("AUTO-DETECT");
    autoDetectBeatsToggle_.setColour(juce::TextButton::buttonColourId,
                                     DarkTheme::getColour(DarkTheme::SURFACE));
    autoDetectBeatsToggle_.setColour(juce::TextButton::buttonOnColourId,
                                     DarkTheme::getAccentColour().withAlpha(0.3f));
    autoDetectBeatsToggle_.setColour(juce::TextButton::textColourOffId,
                                     DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    autoDetectBeatsToggle_.setColour(juce::TextButton::textColourOnId,
                                     DarkTheme::getAccentColour());
    autoDetectBeatsToggle_.onClick = [this]() {
        if (selectedClipIds_.empty())
            return;
        auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;
        bool newState = !clip->autoDetectBeats;
        magda::ClipBatchEdit batch("Set Clip Auto Detect Beats", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Auto Detect Beats", [newState](auto& manager, magda::ClipId id) {
                    manager.setAutoDetectBeats(id, newState);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(autoDetectBeatsToggle_);

    beatSensitivityValue_ =
        std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Percentage);
    beatSensitivityValue_->setRange(0.0, 1.0, 0.5);
    beatSensitivityValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        float sensitivity = static_cast<float>(beatSensitivityValue_->getValue());
        magda::ClipBatchEdit batch("Set Clip Beat Sensitivity", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isAudio()) {
                batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                    cid, "Set Clip Beat Sensitivity",
                    [sensitivity](auto& manager, magda::ClipId id) {
                        manager.setBeatSensitivity(id, sensitivity);
                    }));
            }
        }
    };
    clipPropsContainer_.addChildComponent(*beatSensitivityValue_);

    // Transient sensitivity
    transientSectionLabel_.setText("Transient Detection", juce::dontSendNotification);
    transientSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    transientSectionLabel_.setColour(juce::Label::textColourId,
                                     DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(transientSectionLabel_);

    transientSensitivityLabel_.setText("Sensitivity", juce::dontSendNotification);
    transientSensitivityLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    transientSensitivityLabel_.setColour(juce::Label::textColourId,
                                         DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(transientSensitivityLabel_);

    transientSensitivityValue_ =
        std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Percentage);
    transientSensitivityValue_->setRange(0.0, 1.0, 0.01);
    transientSensitivityValue_->setValue(0.5, juce::dontSendNotification);
    transientSensitivityValue_->setDoubleClickResetsValue(true);
    transientSensitivityValue_->onValueChange = [this]() {
        if (primaryClipId() == magda::INVALID_CLIP_ID)
            return;
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (!audioEngine)
            return;
        auto* bridge = audioEngine->getAudioBridge();
        if (bridge) {
            bridge->setTransientSensitivity(
                primaryClipId(), static_cast<float>(transientSensitivityValue_->getValue()));
            // Notify listeners so WaveformEditorContent restarts transient polling
            magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(primaryClipId());
        }
    };
    clipPropsContainer_.addChildComponent(*transientSensitivityValue_);
}

// ========================================================================
// Fades section
// ========================================================================

void ClipInspector::initFadesSection() {
    fadesSection_ = std::make_unique<ClipFadesSection>();
    clipPropsContainer_.addChildComponent(*fadesSection_);
}

// ========================================================================
// Channels section
// ========================================================================

void ClipInspector::initChannelsSection() {
    channelsSectionLabel_.setText("Channels", juce::dontSendNotification);
    channelsSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    channelsSectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    clipPropsContainer_.addChildComponent(channelsSectionLabel_);

    leftChannelToggle_.setButtonText("L");
    leftChannelToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    leftChannelToggle_.setColour(juce::TextButton::buttonColourId,
                                 DarkTheme::getColour(DarkTheme::SURFACE));
    leftChannelToggle_.setColour(juce::TextButton::buttonOnColourId,
                                 DarkTheme::getAccentColour().withAlpha(0.3f));
    leftChannelToggle_.setColour(juce::TextButton::textColourOffId,
                                 DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    leftChannelToggle_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    leftChannelToggle_.onClick = [this]() {
        if (selectedClipIds_.empty())
            return;
        auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;
        bool newState = !clip->leftChannelActive;
        magda::ClipBatchEdit batch("Set Clip Left Channel", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Left Channel", [newState](auto& manager, magda::ClipId id) {
                    manager.setLeftChannelActive(id, newState);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(leftChannelToggle_);

    rightChannelToggle_.setButtonText("R");
    rightChannelToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    rightChannelToggle_.setColour(juce::TextButton::buttonColourId,
                                  DarkTheme::getColour(DarkTheme::SURFACE));
    rightChannelToggle_.setColour(juce::TextButton::buttonOnColourId,
                                  DarkTheme::getAccentColour().withAlpha(0.3f));
    rightChannelToggle_.setColour(juce::TextButton::textColourOffId,
                                  DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    rightChannelToggle_.setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    rightChannelToggle_.onClick = [this]() {
        if (selectedClipIds_.empty())
            return;
        auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;
        bool newState = !clip->rightChannelActive;
        magda::ClipBatchEdit batch("Set Clip Right Channel", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Right Channel", [newState](auto& manager, magda::ClipId id) {
                    manager.setRightChannelActive(id, newState);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(rightChannelToggle_);
}

}  // namespace magda::daw::ui
