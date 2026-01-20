#include "TrackChainContent.hpp"

#include <cmath>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/MixerMetrics.hpp"

namespace magica::daw::ui {

// dB conversion helpers
namespace {
constexpr float MIN_DB = -60.0f;
constexpr float MAX_DB = 6.0f;
constexpr float UNITY_DB = 0.0f;

float gainToDb(float gain) {
    if (gain <= 0.0f)
        return MIN_DB;
    return 20.0f * std::log10(gain);
}

float dbToGain(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}

float dbToFaderPos(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    if (db >= MAX_DB)
        return 1.0f;

    if (db < UNITY_DB) {
        return 0.75f * (db - MIN_DB) / (UNITY_DB - MIN_DB);
    } else {
        return 0.75f + 0.25f * (db - UNITY_DB) / (MAX_DB - UNITY_DB);
    }
}

float faderPosToDb(float pos) {
    if (pos <= 0.0f)
        return MIN_DB;
    if (pos >= 1.0f)
        return MAX_DB;

    if (pos < 0.75f) {
        return MIN_DB + (pos / 0.75f) * (UNITY_DB - MIN_DB);
    } else {
        return UNITY_DB + ((pos - 0.75f) / 0.25f) * (MAX_DB - UNITY_DB);
    }
}
}  // namespace

TrackChainContent::TrackChainContent() {
    setName("Track Chain");

    // No selection label
    noSelectionLabel_.setText("Select a track to view its signal chain",
                              juce::dontSendNotification);
    noSelectionLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    noSelectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    noSelectionLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(noSelectionLabel_);

    // Track name at right strip
    trackNameLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    trackNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    trackNameLabel_.setJustificationType(juce::Justification::centredLeft);
    addChildComponent(trackNameLabel_);

    // Mute button
    muteButton_.setButtonText("M");
    muteButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    muteButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::STATUS_WARNING));
    muteButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    muteButton_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    muteButton_.setClickingTogglesState(true);
    muteButton_.onClick = [this]() {
        if (selectedTrackId_ != magica::INVALID_TRACK_ID) {
            magica::TrackManager::getInstance().setTrackMuted(selectedTrackId_,
                                                              muteButton_.getToggleState());
        }
    };
    addChildComponent(muteButton_);

    // Solo button
    soloButton_.setButtonText("S");
    soloButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    soloButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    soloButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    soloButton_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    soloButton_.setClickingTogglesState(true);
    soloButton_.onClick = [this]() {
        if (selectedTrackId_ != magica::INVALID_TRACK_ID) {
            magica::TrackManager::getInstance().setTrackSoloed(selectedTrackId_,
                                                               soloButton_.getToggleState());
        }
    };
    addChildComponent(soloButton_);

    // Gain slider - using dB scale with unity at 0.75 position
    gainSlider_.setSliderStyle(juce::Slider::LinearVertical);
    gainSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    gainSlider_.setRange(0.0, 1.0, 0.001);
    gainSlider_.setValue(0.75);  // Unity gain (0 dB)
    gainSlider_.setSliderSnapsToMousePosition(false);
    gainSlider_.setColour(juce::Slider::trackColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    gainSlider_.setColour(juce::Slider::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    gainSlider_.setColour(juce::Slider::thumbColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    gainSlider_.setLookAndFeel(&mixerLookAndFeel_);
    gainSlider_.onValueChange = [this]() {
        if (selectedTrackId_ != magica::INVALID_TRACK_ID) {
            float faderPos = static_cast<float>(gainSlider_.getValue());
            float db = faderPosToDb(faderPos);
            float gain = dbToGain(db);
            magica::TrackManager::getInstance().setTrackVolume(selectedTrackId_, gain);
            // Update gain label
            juce::String dbText;
            if (db <= MIN_DB) {
                dbText = "-inf";
            } else {
                dbText = juce::String(db, 1) + " dB";
            }
            gainValueLabel_.setText(dbText, juce::dontSendNotification);
        }
    };
    addChildComponent(gainSlider_);

    // Gain value label
    gainValueLabel_.setText("0.0 dB", juce::dontSendNotification);
    gainValueLabel_.setJustificationType(juce::Justification::centred);
    gainValueLabel_.setColour(juce::Label::textColourId,
                              DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    gainValueLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    addChildComponent(gainValueLabel_);

    // Pan slider (rotary knob)
    panSlider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setColour(juce::Slider::rotarySliderFillColourId,
                         DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    panSlider_.setColour(juce::Slider::rotarySliderOutlineColourId,
                         DarkTheme::getColour(DarkTheme::SURFACE));
    panSlider_.setLookAndFeel(&mixerLookAndFeel_);
    panSlider_.onValueChange = [this]() {
        if (selectedTrackId_ != magica::INVALID_TRACK_ID) {
            magica::TrackManager::getInstance().setTrackPan(
                selectedTrackId_, static_cast<float>(panSlider_.getValue()));
            // Update pan label
            float pan = static_cast<float>(panSlider_.getValue());
            juce::String panText;
            if (std::abs(pan) < 0.01f) {
                panText = "C";
            } else if (pan < 0) {
                panText = juce::String(static_cast<int>(std::abs(pan) * 100)) + "L";
            } else {
                panText = juce::String(static_cast<int>(pan * 100)) + "R";
            }
            panValueLabel_.setText(panText, juce::dontSendNotification);
        }
    };
    addChildComponent(panSlider_);

    // Pan value label
    panValueLabel_.setText("C", juce::dontSendNotification);
    panValueLabel_.setJustificationType(juce::Justification::centred);
    panValueLabel_.setColour(juce::Label::textColourId,
                             DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    panValueLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    addChildComponent(panValueLabel_);

    // Register as listener
    magica::TrackManager::getInstance().addListener(this);

    // Check if there's already a selected track
    selectedTrackId_ = magica::TrackManager::getInstance().getSelectedTrack();
    updateFromSelectedTrack();
}

TrackChainContent::~TrackChainContent() {
    magica::TrackManager::getInstance().removeListener(this);
    // Clear look and feel before destruction
    gainSlider_.setLookAndFeel(nullptr);
    panSlider_.setLookAndFeel(nullptr);
}

void TrackChainContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    if (selectedTrackId_ != magica::INVALID_TRACK_ID) {
        // Draw the chain mockup area (horizontal layout)
        auto bounds = getLocalBounds();
        auto stripWidth = 100;
        auto chainArea = bounds.withTrimmedRight(stripWidth);

        paintChainMockup(g, chainArea);

        // Draw separator line before strip
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawLine(static_cast<float>(chainArea.getRight()), 0.0f,
                   static_cast<float>(chainArea.getRight()), static_cast<float>(getHeight()), 1.0f);
    }
}

void TrackChainContent::paintChainMockup(juce::Graphics& g, juce::Rectangle<int> area) {
    // Draw mockup FX chain slots horizontally
    auto slotArea = area.reduced(8);
    int slotWidth = 120;
    int slotSpacing = 8;
    int arrowWidth = 20;

    // Draw empty FX slots horizontally
    juce::StringArray slotLabels = {"Input", "Insert 1", "Insert 2", "Insert 3", "Send"};
    for (size_t i = 0; i < slotLabels.size(); ++i) {
        if (slotArea.getWidth() < slotWidth)
            break;

        auto slot = slotArea.removeFromLeft(slotWidth);

        // Slot background
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(slot.toFloat(), 4.0f);

        // Slot border
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(slot.toFloat(), 4.0f, 1.0f);

        // Slot label at top
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText(slotLabels[static_cast<int>(i)], slot.reduced(6).removeFromTop(16),
                   juce::Justification::centredLeft);

        // "(empty)" hint centered
        g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.5f));
        g.setFont(FontManager::getInstance().getUIFont(9.0f));
        g.drawText("(empty)", slot, juce::Justification::centred);

        // Draw arrow between slots (except after last)
        if (i < slotLabels.size() - 1) {
            auto arrowArea = slotArea.removeFromLeft(arrowWidth);
            g.setColour(DarkTheme::getSecondaryTextColour());

            // Draw arrow: →
            int arrowY = arrowArea.getCentreY();
            int arrowX = arrowArea.getCentreX();
            g.drawLine(static_cast<float>(arrowX - 6), static_cast<float>(arrowY),
                       static_cast<float>(arrowX + 6), static_cast<float>(arrowY), 1.5f);
            // Arrow head
            g.drawLine(static_cast<float>(arrowX + 2), static_cast<float>(arrowY - 4),
                       static_cast<float>(arrowX + 6), static_cast<float>(arrowY), 1.5f);
            g.drawLine(static_cast<float>(arrowX + 2), static_cast<float>(arrowY + 4),
                       static_cast<float>(arrowX + 6), static_cast<float>(arrowY), 1.5f);

            slotArea.removeFromLeft(slotSpacing);
        }
    }
}

void TrackChainContent::resized() {
    auto bounds = getLocalBounds();
    const auto& metrics = magica::MixerMetrics::getInstance();

    if (selectedTrackId_ == magica::INVALID_TRACK_ID) {
        noSelectionLabel_.setBounds(bounds);
    } else {
        // Track info strip at right border
        auto stripWidth = 100;
        auto strip = bounds.removeFromRight(stripWidth).reduced(4);

        // Track name at top
        trackNameLabel_.setBounds(strip.removeFromTop(20));
        strip.removeFromTop(4);

        // Pan knob
        auto panArea = strip.removeFromTop(metrics.knobSize);
        panSlider_.setBounds(panArea.withSizeKeepingCentre(metrics.knobSize, metrics.knobSize));

        // Pan value label
        panValueLabel_.setBounds(strip.removeFromTop(14));
        strip.removeFromTop(4);

        // M/S buttons
        auto buttonRow = strip.removeFromTop(24);
        muteButton_.setBounds(buttonRow.removeFromLeft(36));
        buttonRow.removeFromLeft(4);
        soloButton_.setBounds(buttonRow.removeFromLeft(36));
        strip.removeFromTop(4);

        // Gain value label
        gainValueLabel_.setBounds(strip.removeFromTop(12));

        // Gain slider (vertical) - takes remaining space
        gainSlider_.setBounds(strip);
    }
}

void TrackChainContent::onActivated() {
    selectedTrackId_ = magica::TrackManager::getInstance().getSelectedTrack();
    updateFromSelectedTrack();
}

void TrackChainContent::onDeactivated() {
    // Nothing to do
}

void TrackChainContent::tracksChanged() {
    if (selectedTrackId_ != magica::INVALID_TRACK_ID) {
        const auto* track = magica::TrackManager::getInstance().getTrack(selectedTrackId_);
        if (!track) {
            selectedTrackId_ = magica::INVALID_TRACK_ID;
            updateFromSelectedTrack();
        }
    }
}

void TrackChainContent::trackPropertyChanged(int trackId) {
    if (static_cast<magica::TrackId>(trackId) == selectedTrackId_) {
        updateFromSelectedTrack();
    }
}

void TrackChainContent::trackSelectionChanged(magica::TrackId trackId) {
    selectedTrackId_ = trackId;
    updateFromSelectedTrack();
}

void TrackChainContent::updateFromSelectedTrack() {
    if (selectedTrackId_ == magica::INVALID_TRACK_ID) {
        showTrackStrip(false);
        noSelectionLabel_.setVisible(true);
    } else {
        const auto* track = magica::TrackManager::getInstance().getTrack(selectedTrackId_);
        if (track) {
            trackNameLabel_.setText(track->name, juce::dontSendNotification);
            muteButton_.setToggleState(track->muted, juce::dontSendNotification);
            soloButton_.setToggleState(track->soloed, juce::dontSendNotification);

            // Convert linear gain to fader position
            float db = gainToDb(track->volume);
            float faderPos = dbToFaderPos(db);
            gainSlider_.setValue(faderPos, juce::dontSendNotification);

            // Update gain label
            juce::String dbText;
            if (db <= MIN_DB) {
                dbText = "-inf";
            } else {
                dbText = juce::String(db, 1) + " dB";
            }
            gainValueLabel_.setText(dbText, juce::dontSendNotification);

            panSlider_.setValue(track->pan, juce::dontSendNotification);

            // Update pan label
            float pan = track->pan;
            juce::String panText;
            if (std::abs(pan) < 0.01f) {
                panText = "C";
            } else if (pan < 0) {
                panText = juce::String(static_cast<int>(std::abs(pan) * 100)) + "L";
            } else {
                panText = juce::String(static_cast<int>(pan * 100)) + "R";
            }
            panValueLabel_.setText(panText, juce::dontSendNotification);

            showTrackStrip(true);
            noSelectionLabel_.setVisible(false);
        } else {
            showTrackStrip(false);
            noSelectionLabel_.setVisible(true);
        }
    }

    resized();
    repaint();
}

void TrackChainContent::showTrackStrip(bool show) {
    trackNameLabel_.setVisible(show);
    muteButton_.setVisible(show);
    soloButton_.setVisible(show);
    gainSlider_.setVisible(show);
    gainValueLabel_.setVisible(show);
    panSlider_.setVisible(show);
    panValueLabel_.setVisible(show);
}

}  // namespace magica::daw::ui
