#include "MidiActivityLed.hpp"

#include "../../../audio/TrackMeters.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kFrameRateHz = 30;
constexpr int kHoldFrames = 4;  // ~130ms at full brightness
constexpr float kDecayPerFrame = 0.7f;
constexpr float kLedDiameter = 7.0f;
}  // namespace

bool showsLiveMidiActivity(const magda::TrackInfo& track, bool playing) {
    if (!track.receivesLiveMidiInput())
        return false;
    switch (track.inputMonitor) {
        case magda::InputMonitorMode::In:
            return true;
        case magda::InputMonitorMode::Auto:
            return !playing;
        case magda::InputMonitorMode::Off:
            return false;
    }
    return false;
}

MidiActivityLed::MidiActivityLed() {
    setInterceptsMouseClicks(false, false);
}

MidiActivityLed::~MidiActivityLed() {
    stopTimer();
}

void MidiActivityLed::setTrack(magda::TrackId trackId) {
    trackId_ = trackId;
    lastCounter_ = 0;
    if (auto* engine = magda::TrackManager::getInstance().getAudioEngine())
        lastCounter_ = engine->meters().midiActivity.getActivityCounter(trackId);
    level_ = 0.0f;
    holdFrames_ = 0;
    repaint();
}

void MidiActivityLed::visibilityChanged() {
    if (isVisible())
        startTimerHz(kFrameRateHz);
    else
        stopTimer();
}

void MidiActivityLed::timerCallback() {
    auto* engine = magda::TrackManager::getInstance().getAudioEngine();
    if (engine == nullptr || trackId_ == magda::INVALID_TRACK_ID)
        return;

    const auto counter = engine->meters().midiActivity.getActivityCounter(trackId_);
    if (counter != lastCounter_) {
        lastCounter_ = counter;
        const auto* track = magda::TrackManager::getInstance().getTrack(trackId_);
        if (track != nullptr && showsLiveMidiActivity(*track, engine->isPlaying())) {
            level_ = 1.0f;
            holdFrames_ = kHoldFrames;
            repaint();
        }
    }

    if (holdFrames_ > 0) {
        --holdFrames_;
    } else if (level_ > 0.0f) {
        level_ *= kDecayPerFrame;
        if (level_ < 0.01f)
            level_ = 0.0f;
        repaint();
    }
}

void MidiActivityLed::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    auto labelArea = bounds.removeFromBottom(9.0f);
    const auto centre = bounds.getCentre().withY(bounds.getBottom() - kLedDiameter / 2.0f - 2.0f);
    const auto led = juce::Rectangle<float>(kLedDiameter, kLedDiameter).withCentre(centre);

    const auto green = ActiveTheme::getColour(ActiveTheme::DEVICE_GREEN);
    if (level_ > 0.0f) {
        g.setColour(green.withAlpha(0.35f * level_));
        g.fillEllipse(led.expanded(3.0f));
    }
    const auto idle = ActiveTheme::getColour(ActiveTheme::DEVICE_LINE2);
    g.setColour(idle.interpolatedWith(green, level_));
    g.fillEllipse(led);

    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
    g.setFont(FontManager::getInstance().getMonoFont(7.5f).boldened());
    g.drawText("MIDI", labelArea, juce::Justification::centredTop, false);
}

}  // namespace magda::daw::ui
