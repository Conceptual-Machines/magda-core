#include "TrackInspector.hpp"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "../../../audio/AudioBridge.hpp"
#include "../../../audio/MidiBridge.hpp"
#include "../../../components/common/MasterSpeakerButton.hpp"
#include "../../../components/mixer/LevelMeterScale.hpp"
#include "../../../engine/AudioEngine.hpp"
#include "../../components/common/ColourSwatch.hpp"
#include "../../components/mixer/RoutingSyncHelper.hpp"
#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/DialogLookAndFeel.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/SmallButtonLookAndFeel.hpp"
#include "core/AutomationManager.hpp"
#include "core/ClipManager.hpp"
#include "core/Config.hpp"
#include "core/StringTable.hpp"
#include "core/TechnicalText.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda::daw::ui {
namespace {
void useLocalizedLabelPainter(juce::Label& label) {
    label.setLookAndFeel(&DialogLookAndFeel::getInstance());
}

void clearLocalizedLabelPainter(juce::Label& label) {
    label.setLookAndFeel(nullptr);
}

// Inspector-tuned metrics for the shared track_controls layout. The control
// sizes match the arrange track headers; only the row height differs.
magda::track_controls::Metrics inspectorControlMetrics() {
    magda::track_controls::Metrics m;
    m.rowH = 22;
    m.buttonW = 26;
    m.buttonH = 18;
    m.cellW = 26;
    m.gap = 4;
    m.rowGap = 4;
    m.iconW = 16;
    m.ddGap = 4;
    return m;
}
}  // namespace

TrackInspector::TrackInspector() {
    // Track name
    trackNameLabel_.setText(tr("inspector.name"), juce::dontSendNotification);
    trackNameLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    trackNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(trackNameLabel_);

    trackNameValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
    trackNameValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    trackNameValue_.setColour(juce::Label::backgroundColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE));
    trackNameValue_.setEditable(true);
    trackNameValue_.onTextChange = [this]() {
        // The master track cannot be renamed; its name is fixed.
        if (selectedTrackId_ != magda::INVALID_TRACK_ID &&
            selectedTrackId_ != magda::MASTER_TRACK_ID) {
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackNameCommand>(selectedTrackId_,
                                                             trackNameValue_.getText()));
        }
    };
    addAndMakeVisible(trackNameValue_);

    // Colour swatch
    colourSwatch_ = std::make_unique<magda::ColourSwatch>();
    auto* swatch = static_cast<magda::ColourSwatch*>(colourSwatch_.get());
    swatch->onColourClicked = [this, swatch]() {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID && selectedTrackIds_.empty())
            return;

        auto menu = juce::PopupMenu();
        menu.addItem(1, "None");
        menu.addSeparator();

        // Helper to create a colour chip icon for menu items
        auto makeChip = [](juce::Colour colour) {
            juce::Image chip(juce::Image::ARGB, 14, 14, true);
            juce::Graphics cg(chip);
            cg.setColour(colour);
            cg.fillRoundedRectangle(0.0f, 0.0f, 14.0f, 14.0f, 2.0f);
            auto drawable = std::make_unique<juce::DrawableImage>();
            drawable->setImage(chip);
            return drawable;
        };

        // Default colours (always available)
        for (size_t i = 0; i < magda::Config::defaultColourPalette.size(); ++i) {
            auto colour = juce::Colour(magda::Config::defaultColourPalette[i].colour);
            menu.addItem(static_cast<int>(i + 2), magda::Config::defaultColourPalette[i].name, true,
                         false, makeChip(colour));
        }

        // Custom colours from Config (user-defined)
        const auto customPalette = magda::Config::getInstance().getTrackColourPalette();
        const int customOffset = static_cast<int>(magda::Config::defaultColourPalette.size()) + 2;
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
            const int customOff = static_cast<int>(magda::Config::defaultColourPalette.size()) + 2;
            auto trackIds = selectedTrackIds_.empty()
                                ? std::unordered_set<magda::TrackId>{selectedTrackId_}
                                : selectedTrackIds_;
            if (result == 1) {
                // "None"
                swatch->clearColour();
                for (auto tid : trackIds) {
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetTrackColourCommand>(tid,
                                                                       juce::Colour(0xFF444444)));
                }
            } else if (result >= 2 && result < customOff) {
                // Default colour
                auto colour = juce::Colour(magda::Config::getDefaultColour(result - 2));
                swatch->setColour(colour);
                for (auto tid : trackIds) {
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetTrackColourCommand>(tid, colour));
                }
            } else {
                // Custom colour
                auto idx = static_cast<size_t>(result - customOff);
                if (idx < customPalette.size()) {
                    auto colour = juce::Colour(customPalette[idx].colour);
                    swatch->setColour(colour);
                    for (auto tid : trackIds) {
                        magda::UndoManager::getInstance().executeCommand(
                            std::make_unique<magda::SetTrackColourCommand>(tid, colour));
                    }
                }
            }
        });
    };
    addAndMakeVisible(*colourSwatch_);

    // MAGDA glyph shown in the master track's (empty) colour-swatch slot.
    masterGlyph_ =
        std::make_unique<juce::DrawableButton>("masterGlyph", juce::DrawableButton::ImageFitted);
    {
        auto glyph = juce::Drawable::createFromImageData(BinaryData::BoldMGlyph_svg,
                                                         BinaryData::BoldMGlyph_svgSize);
        if (glyph)
            glyph->replaceColour(juce::Colour(0xFF0A0A0A), DarkTheme::getSecondaryTextColour());
        masterGlyph_->setImages(glyph.get());
    }
    masterGlyph_->setEdgeIndent(0);
    masterGlyph_->setInterceptsMouseClicks(false, false);
    masterGlyph_->setColour(juce::DrawableButton::backgroundColourId,
                            juce::Colours::transparentBlack);
    addChildComponent(*masterGlyph_);

    // Mute button (arrange track-header style)
    muteButton_ = std::make_unique<SvgButton>(
        "mute", BinaryData::master_on_svg, BinaryData::master_on_svgSize,
        BinaryData::master_off_svg, BinaryData::master_off_svgSize);
    configureMasterSpeakerButton(*muteButton_);
    muteButton_->setInactiveIconOpacity(0.58f);
    muteButton_->onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetMasterMuteCommand>(muteButton_->getToggleState()));
            else
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetTrackMuteCommand>(selectedTrackId_,
                                                                 muteButton_->getToggleState()));
        }
    };
    addAndMakeVisible(*muteButton_);

    // Speaker icon button (used for master mute instead of "M" text)
    speakerButton_ = magda::makeMasterSpeakerButton();
    speakerButton_->onClick = [this]() {
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetMasterMuteCommand>(speakerButton_->getToggleState()));
    };
    addChildComponent(*speakerButton_);  // Hidden by default

    // Chord audition: the same 3-state control (Silent / Audible / Solo) as the
    // chord track header, folding mute / solo / monitor into one chord glyph.
    chordSpeakerButton_ = std::make_unique<ChordAuditionControl>();
    chordSpeakerButton_->getTrackId = [this]() { return selectedTrackId_; };
    addChildComponent(*chordSpeakerButton_);  // Hidden by default

    // Solo button (arrange track-header style)
    soloButton_ =
        std::make_unique<SvgButton>("solo", BinaryData::solo_svg, BinaryData::solo_svgSize);
    soloButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    soloButton_->setNormalBackgroundColor(DarkTheme::getColour(DarkTheme::SURFACE));
    soloButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    soloButton_->setStateColourReplacement(juce::Colour(0xFFB3B3B3), DarkTheme::ICON_NEUTRAL,
                                           DarkTheme::ICON_ON_ACCENT);
    soloButton_->setIconPadding(5.0f);  // match the arrange track-header solo glyph
    soloButton_->setInactiveIconOpacity(0.58f);
    soloButton_->setClickingTogglesState(true);
    soloButton_->onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackSoloCommand>(selectedTrackId_,
                                                             soloButton_->getToggleState()));
        }
    };
    addAndMakeVisible(*soloButton_);

    // Record button (arrange track-header style)
    recordButton_ = std::make_unique<SvgButton>("record", BinaryData::track_record_svg,
                                                BinaryData::track_record_svgSize);
    recordButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    recordButton_->setNormalBackgroundColor(DarkTheme::getColour(DarkTheme::SURFACE));
    recordButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::STATUS_ERROR));
    recordButton_->setStateColourReplacement(juce::Colour(0xFFB3B3B3), DarkTheme::ICON_NEUTRAL,
                                             DarkTheme::ICON_ON_ACCENT);
    recordButton_->setIconPadding(5.0f);  // match the arrange track-header record glyph
    recordButton_->setInactiveIconOpacity(0.58f);
    recordButton_->setClickingTogglesState(true);
    recordButton_->onClick = [this]() {
        DBG("TrackInspector::recordButton clicked - trackId="
            << selectedTrackId_ << " toggleState=" << (int)recordButton_->getToggleState());
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().setTrackRecordArmed(selectedTrackId_,
                                                                   recordButton_->getToggleState());
        }
    };
    addAndMakeVisible(*recordButton_);

    // Track enable/disable: drives the chain bypass — the same signal-flow
    // power as the track chain header's power button. Chip-style bordered
    // switch in the name row, matching the clip inspector's toggle.
    enableButton_ = std::make_unique<SvgButton>(
        "enable", BinaryData::toggle_off_svg, BinaryData::toggle_off_svgSize,
        BinaryData::toggle_on_svg, BinaryData::toggle_on_svgSize);
    enableButton_->setNormalBackgroundColor(DarkTheme::getColour(DarkTheme::SURFACE));
    enableButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    enableButton_->setStateColourReplacement(juce::Colour(0xFFB3B3B3), DarkTheme::ICON_NEUTRAL,
                                             DarkTheme::ICON_NEUTRAL);
    enableButton_->setStateColourReplacement(juce::Colour(0xFF1E1E1E), DarkTheme::ICON_ON_ACCENT,
                                             DarkTheme::ICON_ON_ACCENT);
    enableButton_->setBorderThickness(1.0f);
    enableButton_->setIconPadding(2.0f);
    enableButton_->setTooltip(tr("tracks.enable.tooltip"));
    enableButton_->setClickingTogglesState(true);
    enableButton_->onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().setChainEnabled(selectedTrackId_,
                                                               enableButton_->getToggleState());
        }
    };
    addAndMakeVisible(*enableButton_);

    // Input-monitor: 3-state control (Off / In / Auto). It reads/writes the mode
    // itself; the lambdas resolve single- vs multi-track selection dynamically so
    // they don't need rewiring when the selection changes.
    monitorButton_.getTrackId = [this]() {
        if (isMultiTrackMode_ && !selectedTrackIds_.empty())
            return *selectedTrackIds_.begin();
        return selectedTrackId_;
    };
    monitorButton_.getTargets = [this]() {
        if (isMultiTrackMode_)
            return std::vector<magda::TrackId>(selectedTrackIds_.begin(), selectedTrackIds_.end());
        return std::vector<magda::TrackId>{selectedTrackId_};
    };
    monitorButton_.setInactiveIconOpacity(0.58f);
    addAndMakeVisible(monitorButton_);

    // Automation indicator — mirrors the arrange track-header automation button.
    // Lights purple when the track has automation, and clicking it collapses /
    // expands the "Automated" section below.
    automationIndicator_ = std::make_unique<SvgButton>("Automation", BinaryData::automation_svg,
                                                       BinaryData::automation_svgSize);
    automationIndicator_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    automationIndicator_->setNormalBackgroundColor(DarkTheme::getColour(DarkTheme::SURFACE));
    automationIndicator_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    automationIndicator_->setIconPadding(2.5f);
    automationIndicator_->onClick = [this]() {
        automatedSectionExpanded_ = !automatedSectionExpanded_;
        updateAutomatedParametersSummary();
        resized();
        repaint();
    };
    addChildComponent(*automationIndicator_);

    // Gain label (TCP style - draggable dB display)
    gainLabel_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Decibels);
    gainLabel_->setRange(-60.0, 6.0, 0.0);  // -60 to +6 dB, default 0 dB
    gainLabel_->setFillProportionMapper(magda::level_meter_scale::dbFillProportion);
    gainLabel_->onValueChange = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            double db = gainLabel_->getValue();
            float gain = (db <= -60.0f) ? 0.0f : std::pow(10.0f, static_cast<float>(db) / 20.0f);
            if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetMasterVolumeCommand>(gain));
            else
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetTrackVolumeCommand>(selectedTrackId_, gain));
        }
    };
    addAndMakeVisible(*gainLabel_);

    // Pan label (TCP style - draggable L/C/R display)
    panLabel_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Pan);
    panLabel_->setRange(-1.0, 1.0, 0.0);  // -1 (L) to +1 (R), default center
    panLabel_->onValueChange = [this]() {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;
        float pan = static_cast<float>(panLabel_->getValue());
        if (panLabel_->isDragging()) {
            // Apply directly during drag (no undo command per pixel)
            if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                magda::TrackManager::getInstance().setMasterPan(pan);
            else
                magda::TrackManager::getInstance().setTrackPan(selectedTrackId_, pan);
        } else {
            // Non-drag changes (keyboard edit, double-click reset)
            if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetMasterPanCommand>(pan));
            else
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetTrackPanCommand>(selectedTrackId_, pan));
        }
    };
    panLabel_->onDragEnd = [this](double startValue) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;
        float oldPan = static_cast<float>(startValue);
        float newPan = static_cast<float>(panLabel_->getValue());
        if (selectedTrackId_ == magda::MASTER_TRACK_ID)
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetMasterPanCommand>(oldPan, newPan));
        else
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackPanCommand>(selectedTrackId_, oldPan, newPan));
    };
    addAndMakeVisible(*panLabel_);

    automatedSectionLabel_.setText(tr("inspector.automated"), juce::dontSendNotification);
    automatedSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    automatedSectionLabel_.setColour(juce::Label::textColourId,
                                     DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(automatedSectionLabel_);

    automatedParamsLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    automatedParamsLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    automatedParamsLabel_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(automatedParamsLabel_);

    // Routing section
    routingSectionLabel_.setText(tr("inspector.routing"), juce::dontSendNotification);
    routingSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    routingSectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(routingSectionLabel_);

    // Input type selector (hidden, kept for internal state)
    inputTypeSelector_ = std::make_unique<magda::InputTypeSelector>();

    // Audio input selector
    audioInputSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::AudioIn);
    audioInputSelector_->setSelectedId(1);
    audioInputSelector_->setEnabled(false);  // Disabled by default (MIDI input active)
    addAndMakeVisible(*audioInputSelector_);

    // MIDI input selector
    inputSelector_ = std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::MidiIn);
    addAndMakeVisible(*inputSelector_);

    // Audio output selector
    outputSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::AudioOut);
    addAndMakeVisible(*outputSelector_);

    // MIDI output selector
    midiOutputSelector_ =
        std::make_unique<magda::RoutingSelector>(magda::RoutingSelector::Type::MidiOut);
    midiOutputSelector_->setSelectedId(1);   // "None"
    midiOutputSelector_->setEnabled(false);  // Disabled by default
    addAndMakeVisible(*midiOutputSelector_);

    // Column header labels for routing selectors. "Audio" and "MIDI" are kept
    // as fixed technical tokens so the paired headers render at the same base
    // (Latin) size — a translated "Audio" would scale with the localized font
    // and tower over the fixed "MIDI" next to it.
    audioColumnLabel_.setText(magda::technicalText(magda::TechnicalTextToken::Audio),
                              juce::dontSendNotification);
    audioColumnLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    audioColumnLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    audioColumnLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(audioColumnLabel_);

    midiColumnLabel_.setText(magda::technicalText(magda::TechnicalTextToken::Midi),
                             juce::dontSendNotification);
    midiColumnLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    midiColumnLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    midiColumnLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(midiColumnLabel_);

    // I/O routing icons (non-interactive visual indicators)
    auto inputDrawable =
        std::make_unique<juce::DrawableButton>("inputIcon", juce::DrawableButton::ImageFitted);
    if (auto svg =
            juce::Drawable::createFromImageData(BinaryData::Input_svg, BinaryData::Input_svgSize)) {
        DarkTheme::applyToSvgIcon(*svg);
        inputDrawable->setImages(svg.get());
    }
    inputDrawable->setInterceptsMouseClicks(false, false);
    inputIcon_ = std::move(inputDrawable);
    addAndMakeVisible(*inputIcon_);

    auto outputDrawable =
        std::make_unique<juce::DrawableButton>("outputIcon", juce::DrawableButton::ImageFitted);
    if (auto svg = juce::Drawable::createFromImageData(BinaryData::Output_svg,
                                                       BinaryData::Output_svgSize)) {
        DarkTheme::applyToSvgIcon(*svg);
        outputDrawable->setImages(svg.get());
    }
    outputDrawable->setInterceptsMouseClicks(false, false);
    outputIcon_ = std::move(outputDrawable);
    addAndMakeVisible(*outputIcon_);

    // Send/Receive section
    sendReceiveSectionLabel_.setText(tr("inspector.sends"), juce::dontSendNotification);
    sendReceiveSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    sendReceiveSectionLabel_.setColour(juce::Label::textColourId,
                                       DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(sendReceiveSectionLabel_);

    addSendButton_ =
        std::make_unique<SvgButton>("AddSend", BinaryData::add_svg, BinaryData::add_svgSize);
    addSendButton_->setTooltip(tr("inspector.add_send"));
    addSendButton_->setIconPadding(4.0f);
    addSendButton_->setOriginalColor(DarkTheme::getSecondaryTextColour());
    addSendButton_->onClick = [this]() { showAddSendMenu(); };
    addAndMakeVisible(*addSendButton_);

    noSendsLabel_.setText(tr("inspector.no_sends"), juce::dontSendNotification);
    noSendsLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    noSendsLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(noSendsLabel_);

    receivesLabel_.setText(tr("inspector.no_receives"), juce::dontSendNotification);
    receivesLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    receivesLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(receivesLabel_);

    // Clips section
    clipsSectionLabel_.setText(tr("inspector.clips"), juce::dontSendNotification);
    clipsSectionLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    clipsSectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(clipsSectionLabel_);

    clipCountLabel_.setText(tr("inspector.clip_count.other").replace("{0}", "0"),
                            juce::dontSendNotification);
    clipCountLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    clipCountLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addAndMakeVisible(clipCountLabel_);

    // Latency display
    latencyLabel_.setText(tr("inspector.latency"), juce::dontSendNotification);
    latencyLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    latencyLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(latencyLabel_);

    latencyValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
    latencyValue_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addAndMakeVisible(latencyValue_);

    for (auto* label :
         {&trackNameLabel_, &trackNameValue_, &routingSectionLabel_, &audioColumnLabel_,
          &midiColumnLabel_, &sendReceiveSectionLabel_, &noSendsLabel_, &receivesLabel_,
          &clipsSectionLabel_, &clipCountLabel_, &automatedSectionLabel_, &automatedParamsLabel_,
          &latencyLabel_, &latencyValue_}) {
        useLocalizedLabelPainter(*label);
    }
}

void TrackInspector::midiDeviceListChanged() {
    juce::MessageManager::callAsync([this]() { populateMidiInputOptions(); });
}

TrackInspector::~TrackInspector() {
    for (auto* label :
         {&trackNameLabel_, &trackNameValue_, &routingSectionLabel_, &audioColumnLabel_,
          &midiColumnLabel_, &sendReceiveSectionLabel_, &noSendsLabel_, &receivesLabel_,
          &clipsSectionLabel_, &clipCountLabel_, &automatedSectionLabel_, &automatedParamsLabel_,
          &latencyLabel_, &latencyValue_}) {
        clearLocalizedLabelPainter(*label);
    }
    for (auto& label : sendDestLabels_)
        clearLocalizedLabelPainter(*label);

    if (audioEngine_) {
        if (auto* mb = audioEngine_->getMidiBridge())
            mb->removeMidiDeviceListListener(this);
    }
    stopTimer();
    magda::AutomationManager::getInstance().removeListener(this);
    magda::TrackManager::getInstance().removeListener(this);
}

void TrackInspector::timerCallback() {
    if (!audioEngine_)
        return;

    auto* midiBridge = audioEngine_->getMidiBridge();
    if (midiBridge) {
        size_t inputCount = midiBridge->getAvailableMidiInputs().size();
        size_t outputCount = midiBridge->getAvailableMidiOutputs().size();

        if (inputCount != lastMidiInputCount_ || outputCount != lastMidiOutputCount_) {
            lastMidiInputCount_ = inputCount;
            lastMidiOutputCount_ = outputCount;
            populateMidiInputOptions();
            populateMidiOutputOptions();
            if (selectedTrackId_ != magda::INVALID_TRACK_ID)
                updateRoutingSelectorsFromTrack();
        }
    }
}

void TrackInspector::onActivated() {
    magda::TrackManager::getInstance().addListener(this);
    magda::AutomationManager::getInstance().addListener(this);
    populateRoutingSelectors();
    updateFromSelectedTrack();
    // Poll for MIDI device changes every 2 seconds (matching TrackHeadersPanel)
    startTimerHz(1);
}

void TrackInspector::onDeactivated() {
    stopTimer();
    magda::AutomationManager::getInstance().removeListener(this);
    magda::TrackManager::getInstance().removeListener(this);
}

void TrackInspector::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

    // Draw section separators
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    auto area = getLocalBounds().reduced(8);
    for (int y : sectionSeparatorYs_) {
        g.drawHorizontalLine(y, static_cast<float>(area.getX()),
                             static_cast<float>(area.getRight()));
    }
}

void TrackInspector::resized() {
    auto bounds = getLocalBounds().reduced(8);
    sectionSeparatorYs_.clear();
    const int separatorPadding = 6;

    // Track properties layout (TCP style)
    trackNameLabel_.setBounds(bounds.removeFromTop(16));
    auto nameRow = bounds.removeFromTop(24);
    if (selectedTrackId_ == magda::MASTER_TRACK_ID) {
        // Right-align the glyph flush to the row edge in a 22px cell, matching
        // the volume row's speaker mute button below it so the two line up.
        masterGlyph_->setBounds(nameRow.removeFromRight(22).withSizeKeepingCentre(22, 22));
        nameRow.removeFromRight(4);
    } else {
        // Colour spine on the left doubles as the colour swatch, matching the
        // clip inspector's name row.
        colourSwatch_->setBounds(nameRow.removeFromLeft(6));
        nameRow.removeFromLeft(6);
        // Enable/disable switch on the right, same position as the clip
        // inspector's toggle.
        if (enableButton_->isVisible()) {
            enableButton_->setBounds(nameRow.removeFromRight(28));
            nameRow.removeFromRight(6);
        }
    }
    trackNameValue_.setBounds(nameRow);
    bounds.removeFromTop(separatorPadding);
    sectionSeparatorYs_.push_back(bounds.getY());
    bounds.removeFromTop(separatorPadding);

    const auto& p = policy_;
    const auto m = inspectorControlMetrics();

    // Mix rows — which controls exist comes from the shared policy; the
    // shared layout reflows to one or two rows depending on the panel width,
    // so nothing is hidden when the panel gets narrow.
    if (p.gain) {
        track_controls::MixControls mix;
        mix.gain = gainLabel_.get();
        switch (p.muteStyle) {
            case magda::TrackControlsPolicy::MuteStyle::MasterSpeaker:
                mix.pan = speakerButton_.get();  // speaker mute rides the pan cell
                break;
            case magda::TrackControlsPolicy::MuteStyle::ChordAudition:
                // Full row height, like the pan label and master speaker, so
                // the chip doesn't sit squat next to the gain field.
                mix.pan = chordSpeakerButton_.get();
                break;
            case magda::TrackControlsPolicy::MuteStyle::Standard:
                if (p.pan)
                    mix.pan = panLabel_.get();
                break;
        }
        if (p.mute && p.muteStyle == magda::TrackControlsPolicy::MuteStyle::Standard)
            mix.buttons.push_back(muteButton_.get());
        if (p.solo)
            mix.buttons.push_back(soloButton_.get());
        if (p.record)
            mix.buttons.push_back(recordButton_.get());
        if (p.monitor)
            mix.buttons.push_back(&monitorButton_);
        if (p.automation)
            mix.trailing = automationIndicator_.get();

        track_controls::layoutMixControls(bounds, mix, m);
        bounds.removeFromTop(separatorPadding);
        sectionSeparatorYs_.push_back(bounds.getY());
        bounds.removeFromTop(separatorPadding);
    }

    if (automatedSectionLabel_.isVisible()) {
        automatedSectionLabel_.setBounds(bounds.removeFromTop(16));
        bounds.removeFromTop(2);
        automatedParamsLabel_.setBounds(bounds.removeFromTop(automatedParamsLabel_.getHeight()));
        bounds.removeFromTop(separatorPadding);
        sectionSeparatorYs_.push_back(bounds.getY());
        bounds.removeFromTop(separatorPadding);
    }

    // Routing section — column labels above, then the input/output rows. Row
    // composition comes from the policy; the shared routing-row layout splits
    // the width between whichever dropdowns exist.
    if (p.anyRouting()) {
        const int selectorHeight = 18;
        const int columnHeaderHeight = 14;
        const int numCols = ((p.audioIn || p.audioOut) ? 1 : 0) + ((p.midiIn || p.midiOut) ? 1 : 0);
        const int ddW = track_controls::routingDropdownWidth(bounds.getWidth(), numCols, m);

        if (audioColumnLabel_.isVisible() || midiColumnLabel_.isVisible()) {
            auto headerRow = bounds.removeFromTop(columnHeaderHeight);
            if (audioColumnLabel_.isVisible()) {
                audioColumnLabel_.setBounds(headerRow.removeFromLeft(ddW));
                headerRow.removeFromLeft(m.ddGap);
            }
            if (midiColumnLabel_.isVisible())
                midiColumnLabel_.setBounds(headerRow.removeFromLeft(ddW));
            bounds.removeFromTop(2);
        }

        if (p.anyInput()) {
            auto inputRow = bounds.removeFromTop(selectorHeight);
            track_controls::layoutRoutingRow(
                inputRow, p.audioIn ? audioInputSelector_.get() : nullptr,
                p.midiIn ? inputSelector_.get() : nullptr, inputIcon_.get(), m);
            bounds.removeFromTop(4);
        }
        if (p.anyOutput()) {
            auto outputRow = bounds.removeFromTop(selectorHeight);
            track_controls::layoutRoutingRow(
                outputRow, p.audioOut ? outputSelector_.get() : nullptr,
                p.midiOut ? midiOutputSelector_.get() : nullptr, outputIcon_.get(), m);
        }
        bounds.removeFromTop(separatorPadding);
        sectionSeparatorYs_.push_back(bounds.getY());
        bounds.removeFromTop(separatorPadding);
    }

    // Send/Receive section — only lay out if visible
    if (sendReceiveSectionLabel_.isVisible()) {
        auto sendHeaderRow = bounds.removeFromTop(22);
        sendReceiveSectionLabel_.setBounds(
            sendHeaderRow.removeFromLeft(100).withSizeKeepingCentre(100, 16));
        addSendButton_->setBounds(sendHeaderRow.removeFromRight(22).withSizeKeepingCentre(22, 22));
        bounds.removeFromTop(4);

        if (sendDestLabels_.empty()) {
            noSendsLabel_.setBounds(bounds.removeFromTop(16));
            noSendsLabel_.setVisible(true);
        } else {
            noSendsLabel_.setVisible(false);
            for (size_t i = 0; i < sendDestLabels_.size(); ++i) {
                auto sendRow = bounds.removeFromTop(18);
                sendDestLabels_[i]->setBounds(sendRow.removeFromLeft(60));
                sendRow.removeFromLeft(4);
                sendLevelLabels_[i]->setBounds(sendRow.removeFromLeft(50));
                sendRow.removeFromLeft(4);
                sendDeleteButtons_[i]->setBounds(sendRow.removeFromLeft(18));
                bounds.removeFromTop(2);
            }
        }

        receivesLabel_.setBounds(bounds.removeFromTop(16));
        bounds.removeFromTop(separatorPadding);
        sectionSeparatorYs_.push_back(bounds.getY());
        bounds.removeFromTop(separatorPadding);
    }

    // Clips section — only lay out if visible
    if (clipsSectionLabel_.isVisible()) {
        clipsSectionLabel_.setBounds(bounds.removeFromTop(16));
        bounds.removeFromTop(4);
        clipCountLabel_.setBounds(bounds.removeFromTop(20));
    }

    // Latency — only lay out if visible
    if (latencyLabel_.isVisible()) {
        bounds.removeFromTop(separatorPadding);
        sectionSeparatorYs_.push_back(bounds.getY());
        bounds.removeFromTop(separatorPadding);
        latencyLabel_.setBounds(bounds.removeFromTop(16));
        bounds.removeFromTop(4);
        latencyValue_.setBounds(bounds.removeFromTop(20));
    }
}

void TrackInspector::setSelectedTrack(magda::TrackId trackId) {
    bool wasMulti = isMultiTrackMode_;
    isMultiTrackMode_ = false;
    selectedTrackIds_.clear();
    selectedTrackId_ = trackId;

    // Bind automation targets so the inspector gain/pan mirror the track
    // header's purple/grey state automatically via the observer. Skip the
    // master track (no automation lanes for master volume/pan).
    if (trackId != magda::INVALID_TRACK_ID && trackId != magda::MASTER_TRACK_ID) {
        magda::AutomationTarget volTarget;
        volTarget.kind = magda::ControlTarget::Kind::TrackVolume;
        volTarget.devicePath = magda::ChainNodePath::trackLevel(trackId);
        gainLabel_->setAutomationTarget(volTarget);
        magda::AutomationTarget panTarget;
        panTarget.kind = magda::ControlTarget::Kind::TrackPan;
        panTarget.devicePath = magda::ChainNodePath::trackLevel(trackId);
        panLabel_->setAutomationTarget(panTarget);
    } else {
        gainLabel_->clearAutomationTarget();
        panLabel_->clearAutomationTarget();
    }

    // Restore single-track callbacks if switching from multi-track mode
    if (wasMulti) {
        muteButton_->onClick = [this]() {
            if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
                if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetMasterMuteCommand>(
                            muteButton_->getToggleState()));
                else
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetTrackMuteCommand>(
                            selectedTrackId_, muteButton_->getToggleState()));
            }
        };
        soloButton_->onClick = [this]() {
            if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetTrackSoloCommand>(selectedTrackId_,
                                                                 soloButton_->getToggleState()));
            }
        };
        enableButton_->onClick = [this]() {
            if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
                magda::TrackManager::getInstance().setChainEnabled(selectedTrackId_,
                                                                   enableButton_->getToggleState());
            }
        };
        trackNameValue_.setEditable(true);

        gainLabel_->clearTextOverride();
        gainLabel_->onValueChange = [this]() {
            if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
                double db = gainLabel_->getValue();
                float gain =
                    (db <= -60.0f) ? 0.0f : std::pow(10.0f, static_cast<float>(db) / 20.0f);
                if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetMasterVolumeCommand>(gain));
                else
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetTrackVolumeCommand>(selectedTrackId_, gain));
            }
        };

        panLabel_->clearTextOverride();
        panLabel_->onValueChange = [this]() {
            if (selectedTrackId_ == magda::INVALID_TRACK_ID)
                return;
            float pan = static_cast<float>(panLabel_->getValue());
            if (panLabel_->isDragging()) {
                if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                    magda::TrackManager::getInstance().setMasterPan(pan);
                else
                    magda::TrackManager::getInstance().setTrackPan(selectedTrackId_, pan);
            } else {
                if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetMasterPanCommand>(pan));
                else
                    magda::UndoManager::getInstance().executeCommand(
                        std::make_unique<magda::SetTrackPanCommand>(selectedTrackId_, pan));
            }
        };
        panLabel_->onDragEnd = [this](double startValue) {
            if (selectedTrackId_ == magda::INVALID_TRACK_ID)
                return;
            float oldPan = static_cast<float>(startValue);
            float newPan = static_cast<float>(panLabel_->getValue());
            if (selectedTrackId_ == magda::MASTER_TRACK_ID)
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetMasterPanCommand>(oldPan, newPan));
            else
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetTrackPanCommand>(selectedTrackId_, oldPan, newPan));
        };

        recordButton_->onClick = [this]() {
            if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
                magda::TrackManager::getInstance().setTrackRecordArmed(
                    selectedTrackId_, recordButton_->getToggleState());
            }
        };
        // monitorButton_ handles its own clicks via the control (getTrackId /
        // getTargets wired in the constructor); no per-mode rewiring needed.
    }

    updateFromSelectedTrack();
}

void TrackInspector::setSelectedTracks(const std::unordered_set<magda::TrackId>& trackIds) {
    isMultiTrackMode_ = true;
    selectedTrackIds_ = trackIds;
    selectedTrackId_ = magda::INVALID_TRACK_ID;
    // Multi-track selection has no single target to mirror, so clear any
    // binding left from single-track mode.
    gainLabel_->clearAutomationTarget();
    panLabel_->clearAutomationTarget();
    updateFromMultiTrackSelection();
}

// ============================================================================
// TrackManagerListener Interface
// ============================================================================

void TrackInspector::tracksChanged() {
    updateFromSelectedTrack();
}

void TrackInspector::trackPropertyChanged(int trackId) {
    if (isMultiTrackMode_) {
        // Don't refresh during an active drag — it would reset text overrides and base values
        if (gainLabel_->isDragging() || panLabel_->isDragging())
            return;
        if (selectedTrackIds_.count(static_cast<magda::TrackId>(trackId)) > 0) {
            updateFromMultiTrackSelection();
        }
        return;
    }
    if (static_cast<magda::TrackId>(trackId) == selectedTrackId_) {
        if (gainLabel_->isDragging() || panLabel_->isDragging())
            return;
        updateFromSelectedTrack();
    }
}

void TrackInspector::trackDevicesChanged(magda::TrackId trackId) {
    if (trackId == selectedTrackId_) {
        rebuildSendsUI();

        // Resync the enable switch — setChainEnabled notifies through
        // trackDevicesChanged, so the chain power button and this switch
        // stay mirrored whichever one was pressed.
        enableButton_->setToggleState(
            magda::TrackManager::getInstance().isChainEnabled(selectedTrackId_),
            juce::dontSendNotification);

        // Adding/removing/editing an External Instrument insert changes the
        // read-only mirror on the track MIDI-out / audio-in, so re-apply it.
        if (!isMultiTrackMode_)
            showTrackControls(true);

        // Refresh latency (devices added/removed/loaded)
        if (latencyLabel_.isVisible()) {
            double latency =
                magda::TrackManager::getInstance().getTrackLatencySeconds(selectedTrackId_);
            auto latencyMs = latency * 1000.0;
            latencyValue_.setText(
                (latency > 0.0 ? juce::String(latencyMs, 1) : juce::String("0")) +
                    magda::technicalTextSuffix(magda::TechnicalTextToken::Milliseconds),
                juce::dontSendNotification);
            latencyValue_.repaint();
        }
    }
}

void TrackInspector::devicePropertyChanged(const magda::ChainNodePath& devicePath) {
    // An External Instrument's send/return picker changed: re-apply the
    // read-only mirror on this track's MIDI-out / audio-in.
    if (!isMultiTrackMode_ && devicePath.trackId == selectedTrackId_)
        showTrackControls(true);
}

void TrackInspector::trackSelectionChanged(magda::TrackId trackId) {
    // Not used - selection is managed externally
    (void)trackId;
}

void TrackInspector::masterChannelChanged() {
    if (selectedTrackId_ == magda::MASTER_TRACK_ID) {
        if (gainLabel_->isDragging())
            return;
        updateFromSelectedTrack();
    }
}

void TrackInspector::deviceParameterChanged(const magda::ChainNodePath& devicePath, int paramIndex,
                                            float newValue) {
    // Not relevant for track inspector
    (void)devicePath;
    (void)paramIndex;
    (void)newValue;
}

void TrackInspector::automationLanesChanged() {
    updateAutomatedParametersSummary();
    resized();
    repaint();
}

void TrackInspector::automationLanePropertyChanged(magda::AutomationLaneId laneId) {
    juce::ignoreUnused(laneId);
    updateAutomatedParametersSummary();
    resized();
    repaint();
}

// ============================================================================
// Private Methods
// ============================================================================

void TrackInspector::updateAutomatedParametersSummary() {
    if (isMultiTrackMode_ || selectedTrackId_ == magda::INVALID_TRACK_ID) {
        if (automationIndicator_)
            automationIndicator_->setActive(false);
        automatedSectionLabel_.setVisible(false);
        automatedParamsLabel_.setVisible(false);
        return;
    }

    auto& automationManager = magda::AutomationManager::getInstance();
    auto laneIds = automationManager.getLanesForTrack(selectedTrackId_);
    if (selectedTrackId_ == magda::MASTER_TRACK_ID) {
        for (auto laneId : automationManager.getEditScopedLanes())
            laneIds.push_back(laneId);
    }

    juce::StringArray names;
    for (auto laneId : laneIds) {
        if (const auto* lane = automationManager.getLane(laneId))
            names.addIfNotAlreadyThere(lane->getDisplayName());
    }

    // Automation button lights purple whenever the track has automation.
    const bool hasAutomation = !names.isEmpty();
    if (automationIndicator_)
        automationIndicator_->setActive(hasAutomation);

    // The "Automated" section is shown only when there's automation and the
    // button hasn't collapsed it.
    if (!hasAutomation || !automatedSectionExpanded_) {
        automatedSectionLabel_.setVisible(false);
        automatedParamsLabel_.setVisible(false);
        return;
    }

    constexpr int maxShown = 4;
    juce::String text;
    const int shown = std::min(maxShown, names.size());
    for (int i = 0; i < shown; ++i) {
        if (i > 0)
            text << "\n";
        text << "- " << names[i];
    }
    if (names.size() > maxShown)
        text << "\n+ " << juce::String(names.size() - maxShown) << " more";

    automatedParamsLabel_.setText(text, juce::dontSendNotification);
    automatedParamsLabel_.setSize(1, (shown + (names.size() > maxShown ? 1 : 0)) * 14);
    automatedSectionLabel_.setVisible(true);
    automatedParamsLabel_.setVisible(true);
}

void TrackInspector::updateFromSelectedTrack() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID) {
        showTrackControls(false);
        updateAutomatedParametersSummary();
        return;
    }

    // Master track — show basic controls from MasterChannelState
    if (selectedTrackId_ == magda::MASTER_TRACK_ID) {
        const auto& master = magda::TrackManager::getInstance().getMasterChannel();
        trackNameValue_.setText(magda::technicalText(magda::TechnicalTextToken::Master),
                                juce::dontSendNotification);
        trackNameValue_.setEditable(false);  // master cannot be renamed
        syncMasterSpeakerButton(*speakerButton_, master.muted);
        soloButton_->setToggleState(false, juce::dontSendNotification);
        recordButton_->setToggleState(false, juce::dontSendNotification);

        float gainDb = (master.volume <= 0.0f) ? -60.0f : 20.0f * std::log10(master.volume);
        gainLabel_->setValue(gainDb, juce::dontSendNotification);

        clipCountLabel_.setText(tr("inspector.clip_count.other").replace("{0}", "0"),
                                juce::dontSendNotification);

        showTrackControls(true);
        updateAutomatedParametersSummary();
        resized();
        repaint();
        return;
    }

    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (track) {
        // Update colour swatch
        auto* swatch = static_cast<magda::ColourSwatch*>(colourSwatch_.get());
        if (track->colour == juce::Colour(0xFF444444))
            swatch->clearColour();
        else
            swatch->setColour(track->colour);

        trackNameValue_.setText(track->name, juce::dontSendNotification);
        trackNameValue_.setEditable(true);  // re-enable after a master selection
        muteButton_->setToggleState(track->muted, juce::dontSendNotification);
        enableButton_->setToggleState(
            magda::TrackManager::getInstance().isChainEnabled(selectedTrackId_),
            juce::dontSendNotification);
        chordSpeakerButton_->refresh();
        soloButton_->setToggleState(track->soloed, juce::dontSendNotification);
        recordButton_->setToggleState(track->recordArmed, juce::dontSendNotification);

        // Update monitor button
        monitorButton_.refresh();

        // Convert linear gain to dB for display
        float gainDb = (track->volume <= 0.0f) ? -60.0f : 20.0f * std::log10(track->volume);
        gainLabel_->setValue(gainDb, juce::dontSendNotification);
        panLabel_->setValue(track->pan, juce::dontSendNotification);

        // Update clip count
        auto clips = magda::ClipManager::getInstance().getClipsOnTrack(selectedTrackId_);
        int clipCount = static_cast<int>(clips.size());
        juce::String clipText =
            tr(clipCount == 1 ? "inspector.clip_count.one" : "inspector.clip_count.other")
                .replace("{0}", juce::String(clipCount));
        clipCountLabel_.setText(clipText, juce::dontSendNotification);

        // Update track latency
        double latency =
            magda::TrackManager::getInstance().getTrackLatencySeconds(selectedTrackId_);
        const juce::String msSuffix =
            magda::technicalTextSuffix(magda::TechnicalTextToken::Milliseconds);
        if (latency > 0.0) {
            auto latencyMs = latency * 1000.0;
            latencyValue_.setText(juce::String(latencyMs, 1) + msSuffix,
                                  juce::dontSendNotification);
        } else {
            latencyValue_.setText("0" + msSuffix, juce::dontSendNotification);
        }

        // Update routing selectors to match track state
        updateRoutingSelectorsFromTrack();

        // Update send level values in-place (don't rebuild — that destroys mid-drag labels)
        const auto& sends = track->sends;
        if (sends.size() == sendLevelLabels_.size()) {
            for (size_t i = 0; i < sends.size(); ++i) {
                float levelDb =
                    (sends[i].level <= 0.0f) ? -60.0f : 20.0f * std::log10(sends[i].level);
                sendLevelLabels_[i]->setValue(levelDb, juce::dontSendNotification);
            }
        } else {
            rebuildSendsUI();
        }

        showTrackControls(true);
        updateAutomatedParametersSummary();
    } else {
        showTrackControls(false);
        updateAutomatedParametersSummary();
    }

    resized();
    repaint();
}

void TrackInspector::updateFromMultiTrackSelection() {
    if (selectedTrackIds_.empty()) {
        showTrackControls(false);
        resized();
        repaint();
        return;
    }

    auto& tm = magda::TrackManager::getInstance();

    // Header: "N tracks selected"
    int count = static_cast<int>(selectedTrackIds_.size());
    trackNameLabel_.setText(tr("inspector.selection"), juce::dontSendNotification);
    trackNameValue_.setText(tr("inspector.tracks_selected").replace("{0}", juce::String(count)),
                            juce::dontSendNotification);
    trackNameValue_.setEditable(false);

    // Check button states: "on" only if ALL selected tracks share that state
    bool allMuted = true;
    bool allSoloed = true;
    bool allRecordArmed = true;
    bool allEnabled = true;
    for (auto tid : selectedTrackIds_) {
        const auto* track = tm.getTrack(tid);
        if (!track)
            continue;
        if (!track->muted)
            allMuted = false;
        if (!track->soloed)
            allSoloed = false;
        if (!track->recordArmed)
            allRecordArmed = false;
        if (!tm.isChainEnabled(tid))
            allEnabled = false;
    }

    muteButton_->setToggleState(allMuted, juce::dontSendNotification);
    soloButton_->setToggleState(allSoloed, juce::dontSendNotification);
    recordButton_->setToggleState(allRecordArmed, juce::dontSendNotification);
    enableButton_->setToggleState(allEnabled, juce::dontSendNotification);
    monitorButton_.refresh();

    // Rewire button callbacks for multi-track mode
    muteButton_->onClick = [this]() {
        bool newState = muteButton_->getToggleState();
        for (auto tid : selectedTrackIds_) {
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackMuteCommand>(tid, newState));
        }
    };
    soloButton_->onClick = [this]() {
        bool newState = soloButton_->getToggleState();
        for (auto tid : selectedTrackIds_) {
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackSoloCommand>(tid, newState));
        }
    };
    recordButton_->onClick = [this]() {
        bool newState = recordButton_->getToggleState();
        for (auto tid : selectedTrackIds_) {
            magda::TrackManager::getInstance().setTrackRecordArmed(tid, newState);
        }
    };
    enableButton_->onClick = [this]() {
        bool newState = enableButton_->getToggleState();
        for (auto tid : selectedTrackIds_) {
            magda::TrackManager::getInstance().setChainEnabled(tid, newState);
        }
    };
    // monitorButton_ cycles/menus itself; getTargets returns the multi-track
    // selection, so a change applies to all selected tracks at once.

    // Volume/Pan: check if all values are the same or mixed
    float firstVolDb = 0.0f;
    float firstPan = 0.0f;
    bool volumeMixed = false;
    bool panMixed = false;
    bool first = true;
    for (auto tid : selectedTrackIds_) {
        const auto* track = tm.getTrack(tid);
        if (!track)
            continue;
        float volDb = (track->volume <= 0.0f) ? -60.0f : 20.0f * std::log10(track->volume);
        if (first) {
            firstVolDb = volDb;
            firstPan = track->pan;
            first = false;
        } else {
            if (std::abs(volDb - firstVolDb) > 0.01f)
                volumeMixed = true;
            if (std::abs(track->pan - firstPan) > 0.01f)
                panMixed = true;
        }
    }

    if (volumeMixed) {
        gainLabel_->setTextOverride("mixed");
    } else {
        gainLabel_->clearTextOverride();
        gainLabel_->setValue(firstVolDb, juce::dontSendNotification);
    }

    if (panMixed) {
        panLabel_->setTextOverride("mixed");
    } else {
        panLabel_->clearTextOverride();
        panLabel_->setValue(firstPan, juce::dontSendNotification);
    }

    // Wire up volume/pan for relative multi-track adjustment
    // Capture base values when drag starts, then apply delta to all tracks
    gainLabel_->onValueChange = [this]() {
        // On first call of a new drag, capture base values
        if (multiTrackBaseVolumes_.empty()) {
            auto& tmInner = magda::TrackManager::getInstance();
            for (auto tid : selectedTrackIds_) {
                const auto* track = tmInner.getTrack(tid);
                if (track)
                    multiTrackBaseVolumes_[tid] = track->volume;
            }
            multiTrackDragStartDb_ = gainLabel_->getValue();
        }
        double delta = gainLabel_->getValue() - multiTrackDragStartDb_;
        for (auto& [tid, baseVol] : multiTrackBaseVolumes_) {
            float baseDb = (baseVol <= 0.0f) ? -60.0f : 20.0f * std::log10(baseVol);
            float newDb = juce::jlimit(-60.0f, 6.0f, static_cast<float>(baseDb + delta));
            float newGain = (newDb <= -60.0f) ? 0.0f : std::pow(10.0f, newDb / 20.0f);
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetTrackVolumeCommand>(tid, newGain));
        }
        gainLabel_->clearTextOverride();
        // Clear base values when drag ends so next drag re-captures
        if (!gainLabel_->isDragging())
            multiTrackBaseVolumes_.clear();
    };

    panLabel_->onValueChange = [this]() {
        if (multiTrackBasePans_.empty()) {
            auto& tmInner = magda::TrackManager::getInstance();
            for (auto tid : selectedTrackIds_) {
                const auto* track = tmInner.getTrack(tid);
                if (track)
                    multiTrackBasePans_[tid] = track->pan;
            }
            multiTrackDragStartPan_ = panLabel_->getValue();
        }
        double delta = panLabel_->getValue() - multiTrackDragStartPan_;
        for (auto& [tid, basePan] : multiTrackBasePans_) {
            float newPan = juce::jlimit(-1.0f, 1.0f, static_cast<float>(basePan + delta));
            magda::TrackManager::getInstance().setTrackPan(tid, newPan);
        }
        panLabel_->clearTextOverride();
        if (!panLabel_->isDragging())
            multiTrackBasePans_.clear();
    };
    panLabel_->onDragEnd = [this](double /*startValue*/) {
        // Create undo commands for all tracks using pre-drag base values
        for (auto& [tid, basePan] : multiTrackBasePans_) {
            auto* track = magda::TrackManager::getInstance().getTrack(tid);
            if (track)
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetTrackPanCommand>(tid, basePan, track->pan));
        }
        multiTrackBasePans_.clear();
    };

    // Show the shared multi-selection controls; hide routing/sends/clips.
    showTrackControls(true);

    resized();
    repaint();
}

void TrackInspector::showTrackControls(bool show) {
    // Which controls exist for the selection comes from the shared
    // TrackControlsPolicy — the same rules drive the arrange track headers,
    // so the two views can't drift apart.
    policy_ = magda::TrackControlsPolicy::hidden();
    if (show) {
        if (isMultiTrackMode_) {
            policy_ = magda::TrackControlsPolicy::forMultiSelection();
        } else if (selectedTrackId_ == magda::MASTER_TRACK_ID) {
            policy_ = magda::TrackControlsPolicy::forType(magda::TrackType::Master);
        } else if (const auto* track =
                       magda::TrackManager::getInstance().getTrack(selectedTrackId_)) {
            policy_ = magda::TrackControlsPolicy::forTrack(*track);
        }
    }
    const auto& p = policy_;
    using MuteStyle = magda::TrackControlsPolicy::MuteStyle;
    const bool isMaster = p.muteStyle == MuteStyle::MasterSpeaker;
    isChordTrack_ = p.muteStyle == MuteStyle::ChordAudition;

    trackNameLabel_.setVisible(show);
    trackNameValue_.setVisible(show);
    colourSwatch_->setVisible(show && !isMaster);
    masterGlyph_->setVisible(show && isMaster);

    muteButton_->setVisible(p.mute && p.muteStyle == MuteStyle::Standard);
    enableButton_->setVisible(p.mute && p.muteStyle == MuteStyle::Standard);
    speakerButton_->setVisible(p.mute && isMaster);
    chordSpeakerButton_->setVisible(p.mute && isChordTrack_);
    soloButton_->setVisible(p.solo);
    recordButton_->setVisible(p.record);
    monitorButton_.setVisible(p.monitor);
    gainLabel_->setVisible(p.gain);
    panLabel_->setVisible(p.pan);
    // The automation indicator's active state is driven by
    // updateAutomatedParametersSummary; here it only loses its slot when the
    // type has no automation button at all.
    if (!p.automation)
        automationIndicator_->setVisible(false);

    routingSectionLabel_.setVisible(false);
    audioInputSelector_->setVisible(p.audioIn);
    audioColumnLabel_.setVisible(p.audioIn);
    inputSelector_->setVisible(p.midiIn);
    midiColumnLabel_.setVisible(p.midiIn || p.midiOut);
    inputIcon_->setVisible(p.anyInput());
    outputSelector_->setVisible(p.audioOut);
    midiOutputSelector_->setVisible(p.midiOut);
    outputIcon_->setVisible(p.anyOutput());

    // An External Instrument insert owns the track's MIDI send + audio return.
    // The track-level MIDI-out and audio-in stay visible but go read-only and
    // mirror the device's selection (the synth audio returns via the insert,
    // not the record path), so the routing is only editable on the device.
    auto extRouting =
        (show && !isMaster)
            ? magda::TrackManager::getInstance().getExternalInstrumentRouting(selectedTrackId_)
            : magda::TrackManager::ExternalInstrumentRouting{};
    midiOutputSelector_->setReadOnly(extRouting.present, extRouting.midiOut);
    audioInputSelector_->setReadOnly(extRouting.present, extRouting.audioReturn);

    sendReceiveSectionLabel_.setVisible(p.sends);
    addSendButton_->setVisible(p.sends);
    noSendsLabel_.setVisible(p.sends);
    receivesLabel_.setVisible(p.sends);
    for (auto& l : sendDestLabels_)
        l->setVisible(p.sends);
    for (auto& l : sendLevelLabels_)
        l->setVisible(p.sends);
    for (auto& b : sendDeleteButtons_)
        b->setVisible(p.sends);

    // Clips / latency are inspector-only extras, not per-type track controls.
    clipsSectionLabel_.setVisible(show && !isMaster && !isMultiTrackMode_);
    clipCountLabel_.setVisible(show && !isMaster && !isMultiTrackMode_);
    latencyLabel_.setVisible(show && !isMultiTrackMode_);
    latencyValue_.setVisible(show && !isMultiTrackMode_);
    if (!show || isMultiTrackMode_) {
        automatedSectionLabel_.setVisible(false);
        automatedParamsLabel_.setVisible(false);
    }
}

void TrackInspector::rebuildSendsUI() {
    // Remove existing send UI components
    for (auto& l : sendDestLabels_) {
        clearLocalizedLabelPainter(*l);
        removeChildComponent(l.get());
    }
    for (auto& l : sendLevelLabels_)
        removeChildComponent(l.get());
    for (auto& b : sendDeleteButtons_)
        removeChildComponent(b.get());
    sendDestLabels_.clear();
    sendLevelLabels_.clear();
    sendDeleteButtons_.clear();

    if (selectedTrackId_ == magda::INVALID_TRACK_ID)
        return;

    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (!track)
        return;

    // Aux tracks don't have sends
    if (track->type == magda::TrackType::Aux)
        return;

    for (const auto& send : track->sends) {
        // Destination name label
        auto destLabel = std::make_unique<juce::Label>();
        const auto* destTrack = magda::TrackManager::getInstance().getTrack(send.destTrackId);
        destLabel->setText(destTrack ? destTrack->name : "?", juce::dontSendNotification);
        destLabel->setFont(FontManager::getInstance().getUIFont(10.0f));
        destLabel->setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        useLocalizedLabelPainter(*destLabel);
        addAndMakeVisible(*destLabel);
        sendDestLabels_.push_back(std::move(destLabel));

        // Send level label (draggable dB)
        auto levelLabel = std::make_unique<magda::DraggableValueLabel>(
            magda::DraggableValueLabel::Format::Decibels);
        levelLabel->setRange(-60.0, 6.0, 0.0);
        float levelDb = (send.level <= 0.0f) ? -60.0f : 20.0f * std::log10(send.level);
        levelLabel->setValue(levelDb, juce::dontSendNotification);

        int busIndex = send.busIndex;
        magda::TrackId srcId = selectedTrackId_;
        auto* levelLabelPtr = levelLabel.get();
        levelLabel->onValueChange = [srcId, busIndex, levelLabelPtr]() {
            double db = levelLabelPtr->getValue();
            float gain = (db <= -60.0) ? 0.0f : std::pow(10.0f, static_cast<float>(db) / 20.0f);
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetSendLevelCommand>(srcId, busIndex, gain));
        };

        // Bind automation visual state + right-click menu so the send label
        // tracks purple/grey like the volume fader and can add/show its lane.
        magda::AutomationTarget sendTarget;
        sendTarget.kind = magda::ControlTarget::Kind::SendLevel;
        sendTarget.devicePath = magda::ChainNodePath::trackLevel(srcId);
        sendTarget.sendBusIndex = busIndex;
        levelLabel->setAutomationTarget(sendTarget);

        levelLabel->onRightClick = [srcId, busIndex]() {
            auto& autoMgr = magda::AutomationManager::getInstance();
            magda::AutomationTarget target;
            target.kind = magda::ControlTarget::Kind::SendLevel;
            target.devicePath = magda::ChainNodePath::trackLevel(srcId);
            target.sendBusIndex = busIndex;

            auto laneId = autoMgr.getLaneForTarget(target);
            const bool hasLane = laneId != magda::INVALID_AUTOMATION_LANE_ID;

            juce::PopupMenu menu;
            menu.addItem(1, hasLane ? "Show Automation Lane" : "Add Automation Lane");
            if (hasLane)
                menu.addItem(2, "Delete Automation Lane");
            menu.showMenuAsync(juce::PopupMenu::Options(), [target](int result) {
                auto& mgr = magda::AutomationManager::getInstance();
                if (result == 1) {
                    auto id = mgr.getOrCreateLane(target, magda::AutomationLaneType::Absolute);
                    mgr.setLaneVisible(id, true);
                } else if (result == 2) {
                    auto id = mgr.getLaneForTarget(target);
                    if (id != magda::INVALID_AUTOMATION_LANE_ID)
                        mgr.deleteLane(id);
                }
            });
        };
        addAndMakeVisible(*levelLabel);
        sendLevelLabels_.push_back(std::move(levelLabel));

        // Delete button
        auto deleteBtn = std::make_unique<juce::TextButton>("x");
        deleteBtn->setConnectedEdges(juce::Button::ConnectedOnLeft |
                                     juce::Button::ConnectedOnRight | juce::Button::ConnectedOnTop |
                                     juce::Button::ConnectedOnBottom);
        deleteBtn->setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        deleteBtn->setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        deleteBtn->onClick = [srcId, busIndex]() {
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::RemoveSendCommand>(srcId, busIndex));
        };
        addAndMakeVisible(*deleteBtn);
        sendDeleteButtons_.push_back(std::move(deleteBtn));
    }

    resized();
    repaint();
}

void TrackInspector::showAddSendMenu() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID)
        return;

    const auto* currentTrack = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (!currentTrack)
        return;

    juce::PopupMenu menu;
    auto& trackManager = magda::TrackManager::getInstance();
    const auto& allTracks = trackManager.getTracks();

    // Collect descendants to prevent routing cycles
    std::vector<magda::TrackId> descendants;
    if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
        descendants = trackManager.getAllDescendants(selectedTrackId_);
    }

    int itemId = 1;
    std::vector<magda::TrackId> destTrackIds;

    auto addTracksOfType = [&](magda::TrackType type) {
        bool addedSeparator = false;
        for (const auto& track : allTracks) {
            if (track.type != type)
                continue;
            if (track.id == selectedTrackId_)
                continue;
            if (track.type == magda::TrackType::Master)
                continue;
            if (std::find(descendants.begin(), descendants.end(), track.id) != descendants.end())
                continue;

            // Filter out tracks that already have a send from this track
            bool alreadyHasSend = false;
            for (const auto& send : currentTrack->sends) {
                if (send.destTrackId == track.id) {
                    alreadyHasSend = true;
                    break;
                }
            }
            if (alreadyHasSend)
                continue;

            if (!addedSeparator && itemId > 1) {
                menu.addSeparator();
                addedSeparator = true;
            }
            if (!addedSeparator) {
                addedSeparator = true;
            }

            menu.addItem(itemId, track.name);
            destTrackIds.push_back(track.id);
            ++itemId;
        }
    };

    addTracksOfType(magda::TrackType::Aux);
    addTracksOfType(magda::TrackType::Group);
    addTracksOfType(magda::TrackType::Audio);

    if (menu.getNumItems() == 0) {
        menu.addItem(-1, "(No available tracks)", false);
    }

    // Capture selectedTrackId_ by value to avoid stale reference if selection
    // changes while the async menu is open
    TrackId sourceTrackId = selectedTrackId_;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addSendButton_.get()),
                       [sourceTrackId, destTrackIds](int result) {
                           if (result > 0 && result <= static_cast<int>(destTrackIds.size())) {
                               magda::UndoManager::getInstance().executeCommand(
                                   std::make_unique<magda::AddSendCommand>(
                                       sourceTrackId, destTrackIds[result - 1]));
                           }
                       });
}

void TrackInspector::populateRoutingSelectors() {
    if (!audioEngine_)
        return;

    // Register for device list changes (QWERTY keyboard toggle, etc.)
    if (auto* mb = audioEngine_->getMidiBridge())
        mb->addMidiDeviceListListener(this);

    // Populate all routing selectors
    populateAudioInputOptions();
    populateMidiInputOptions();
    populateAudioOutputOptions();
    populateMidiOutputOptions();

    auto* midiBridge = audioEngine_->getMidiBridge();

    // Audio input selector callbacks (mutually exclusive with MIDI input)
    audioInputSelector_->onEnabledChanged = [this](bool enabled) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (enabled) {
            // Disable MIDI input (mutually exclusive)
            inputSelector_->setEnabled(false);
            magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "");
            // Preserve existing track input if already set, otherwise default
            auto* trackInfo = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
            if (trackInfo && trackInfo->audioInputDevice.startsWith("track:"))
                magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_,
                                                                      trackInfo->audioInputDevice);
            else
                magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "default");
        } else {
            magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "");
        }
    };

    audioInputSelector_->onSelectionChanged = [this](int selectedId) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        DBG("TrackInspector::audioInput onSelectionChanged - selectedId=" << selectedId);

        if (selectedId == 1) {
            magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "");
        } else if (selectedId >= 200) {
            // Track-as-input (resampling)
            auto it = inputTrackMapping_.find(selectedId);
            if (it != inputTrackMapping_.end()) {
                magda::TrackManager::getInstance().setTrackAudioInput(
                    selectedTrackId_, "track:" + juce::String(it->second));
            }
        } else if (selectedId >= 10) {
            // Map to specific TE wave device name
            auto it = inputChannelMapping_.find(selectedId);
            if (it != inputChannelMapping_.end()) {
                // Copy the string — the map can be repopulated during setTrackAudioInput
                // (via notifyTrackPropertyChanged → updateRoutingSelectorsFromTrack)
                juce::String deviceName = it->second;
                DBG("  -> mapped to device: '" << deviceName << "'");
                magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, deviceName);
            } else {
                DBG("  -> no mapping found, using default");
                magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "default");
            }
        }
    };

    // MIDI input selector callbacks (mutually exclusive with audio input)
    inputSelector_->onEnabledChanged = [this, midiBridge](bool enabled) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (enabled) {
            // Disable audio input (mutually exclusive)
            audioInputSelector_->setEnabled(false);
            magda::TrackManager::getInstance().setTrackAudioInput(selectedTrackId_, "");
            int selectedId = inputSelector_->getSelectedId();
            if (selectedId == 1) {
                magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
            } else if (selectedId >= 200) {
                // Preserve existing track input instead of forcing "all"
                auto it = midiInputTrackMapping_.find(selectedId);
                if (it != midiInputTrackMapping_.end()) {
                    magda::TrackManager::getInstance().setTrackMidiInput(
                        selectedTrackId_, "track:" + juce::String(it->second));
                } else {
                    magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
                }
            } else if (selectedId >= 10 && midiBridge) {
                auto midiInputs = midiBridge->getAvailableMidiInputs();
                int deviceIndex = selectedId - 10;
                if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size())) {
                    magda::TrackManager::getInstance().setTrackMidiInput(
                        selectedTrackId_, midiInputs[deviceIndex].id);
                } else {
                    magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
                }
            } else {
                magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
            }
        } else {
            magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "");
        }
    };

    inputSelector_->onSelectionChanged = [this, midiBridge](int selectedId) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (selectedId == 2) {
            magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "");
        } else if (selectedId == 1) {
            magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_, "all");
        } else if (selectedId >= 200) {
            // Track-as-input (internal MIDI routing)
            auto it = midiInputTrackMapping_.find(selectedId);
            if (it != midiInputTrackMapping_.end()) {
                magda::TrackManager::getInstance().setTrackMidiInput(
                    selectedTrackId_, "track:" + juce::String(it->second));
            }
        } else if (selectedId >= 10 && midiBridge) {
            auto midiInputs = midiBridge->getAvailableMidiInputs();
            int deviceIndex = selectedId - 10;
            if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size())) {
                magda::TrackManager::getInstance().setTrackMidiInput(selectedTrackId_,
                                                                     midiInputs[deviceIndex].id);
            }
        }
    };

    // Output selector callbacks
    outputSelector_->onEnabledChanged = [this](bool enabled) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (enabled) {
            magda::TrackManager::getInstance().setTrackAudioOutput(selectedTrackId_, "master");
        } else {
            magda::TrackManager::getInstance().setTrackAudioOutput(selectedTrackId_, "");
        }
    };

    outputSelector_->onSelectionChanged = [this](int selectedId) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (selectedId == 1) {
            // Master
            magda::TrackManager::getInstance().setTrackAudioOutput(selectedTrackId_, "master");
        } else if (selectedId == 2) {
            // None
            magda::TrackManager::getInstance().setTrackAudioOutput(selectedTrackId_, "");
        } else if (selectedId >= 200) {
            // Track destination (Group, Aux, Audio, Instrument)
            auto it = outputTrackMapping_.find(selectedId);
            if (it != outputTrackMapping_.end()) {
                magda::TrackManager::getInstance().setTrackAudioOutput(
                    selectedTrackId_, "track:" + juce::String(it->second));
            }
        } else if (selectedId >= 10) {
            // Hardware output
            magda::TrackManager::getInstance().setTrackAudioOutput(selectedTrackId_, "master");
        }
    };

    // MIDI output selector callbacks
    midiOutputSelector_->onEnabledChanged = [this](bool enabled) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (!enabled) {
            magda::TrackManager::getInstance().setTrackMidiOutput(selectedTrackId_, "");
        }
        // When enabling, don't set anything yet — user picks a device from dropdown
    };

    midiOutputSelector_->onSelectionChanged = [this, midiBridge](int selectedId) {
        if (selectedTrackId_ == magda::INVALID_TRACK_ID)
            return;

        if (selectedId == 1) {
            magda::TrackManager::getInstance().setTrackMidiOutput(selectedTrackId_, "");
        } else if (selectedId >= 200) {
            // "MIDI To track" — internal routing, mirror of the dest's MIDI input
            auto it = midiOutputTrackMapping_.find(selectedId);
            if (it != midiOutputTrackMapping_.end()) {
                magda::TrackManager::getInstance().routeMidiOutputToTrack(selectedTrackId_,
                                                                          it->second);
            }
        } else if (selectedId >= 10 && midiBridge) {
            auto midiOutputs = midiBridge->getAvailableMidiOutputs();
            int deviceIndex = selectedId - 10;
            if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiOutputs.size())) {
                magda::TrackManager::getInstance().setTrackMidiOutput(selectedTrackId_,
                                                                      midiOutputs[deviceIndex].id);
            }
        }
    };
}

void TrackInspector::populateAudioInputOptions() {
    if (!audioInputSelector_ || !audioEngine_)
        return;
    auto* deviceManager = audioEngine_->getDeviceManager();
    if (!deviceManager)
        return;
    juce::BigInteger enabledInputChannels;
    std::map<int, juce::String> teInputDeviceNames;
    if (auto* bridge = audioEngine_->getAudioBridge()) {
        enabledInputChannels = bridge->getEnabledInputChannels();
        teInputDeviceNames = bridge->getInputDeviceNamesByChannel();
    }
    magda::RoutingSyncHelper::populateAudioInputOptions(
        audioInputSelector_.get(), deviceManager->getCurrentAudioDevice(), selectedTrackId_,
        &inputTrackMapping_, enabledInputChannels, &inputChannelMapping_, teInputDeviceNames);
}

void TrackInspector::populateAudioOutputOptions() {
    if (!outputSelector_ || !audioEngine_)
        return;
    auto* deviceManager = audioEngine_->getDeviceManager();
    if (!deviceManager)
        return;
    juce::BigInteger enabledOutputChannels;
    if (auto* bridge = audioEngine_->getAudioBridge())
        enabledOutputChannels = bridge->getEnabledOutputChannels();
    magda::RoutingSyncHelper::populateAudioOutputOptions(
        outputSelector_.get(), selectedTrackId_, deviceManager->getCurrentAudioDevice(),
        outputTrackMapping_, enabledOutputChannels);
}

void TrackInspector::populateMidiInputOptions() {
    if (!inputSelector_ || !audioEngine_)
        return;
    magda::RoutingSyncHelper::populateMidiInputOptions(inputSelector_.get(),
                                                       audioEngine_->getMidiBridge(),
                                                       selectedTrackId_, &midiInputTrackMapping_);
}

void TrackInspector::populateMidiOutputOptions() {
    if (!midiOutputSelector_ || !audioEngine_)
        return;
    magda::RoutingSyncHelper::populateMidiOutputOptions(midiOutputSelector_.get(),
                                                        audioEngine_->getMidiBridge(),
                                                        midiOutputTrackMapping_, selectedTrackId_);
}

void TrackInspector::updateRoutingSelectorsFromTrack() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID || !audioEngine_)
        return;

    const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
    if (!track)
        return;

    // Always re-populate audio input options so track-as-input entries are current
    populateAudioInputOptions();

    auto* deviceManager = audioEngine_->getDeviceManager();
    auto* device = deviceManager ? deviceManager->getCurrentAudioDevice() : nullptr;
    juce::BigInteger enabledIn, enabledOut;
    std::map<int, juce::String> teInputDeviceNames;
    if (auto* bridge = audioEngine_->getAudioBridge()) {
        enabledIn = bridge->getEnabledInputChannels();
        enabledOut = bridge->getEnabledOutputChannels();
        teInputDeviceNames = bridge->getInputDeviceNamesByChannel();
    }
    magda::RoutingSyncHelper::syncSelectorsFromTrack(
        *track, audioInputSelector_.get(), inputSelector_.get(), outputSelector_.get(),
        midiOutputSelector_.get(), audioEngine_->getMidiBridge(), device, selectedTrackId_,
        outputTrackMapping_, midiOutputTrackMapping_, &inputTrackMapping_, enabledIn, enabledOut,
        &inputChannelMapping_, teInputDeviceNames, &midiInputTrackMapping_);
}

}  // namespace magda::daw::ui
