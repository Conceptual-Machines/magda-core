#include "MixerView.hpp"

#include <cmath>
#include <set>
#include <unordered_map>

#include "../../audio/AudioBridge.hpp"
#include "../../audio/DrumGridPlugin.hpp"
#include "../../audio/MeteringBuffer.hpp"
#include "../../audio/MidiBridge.hpp"
#include "../../core/RackInfo.hpp"
#include "../../engine/AudioEngine.hpp"
#include "../../engine/TracktionEngineWrapper.hpp"
#include "../../profiling/PerformanceProfiler.hpp"
#include "../components/mixer/RoutingSyncHelper.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackPropertyCommands.hpp"
#include "core/UndoManager.hpp"
#include "core/ViewModeController.hpp"

namespace magda {

// dB conversion helpers
namespace {
constexpr float MIN_DB = -60.0f;
constexpr float MAX_DB = 6.0f;

// Convert linear gain (0-1) to dB
float gainToDb(float gain) {
    if (gain <= 0.0f)
        return MIN_DB;
    return 20.0f * std::log10(gain);
}

// Convert dB to linear gain
float dbToGain(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}

// Exponent for power curve scaling - lower values spread out the bottom labels more
constexpr float METER_CURVE_EXPONENT = 2.0f;

// Convert dB to normalized meter position (0-1) with power curve
// Used consistently for meters, labels, and faders across the app
float dbToMeterPos(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    if (db >= MAX_DB)
        return 1.0f;

    float normalized = (db - MIN_DB) / (MAX_DB - MIN_DB);
    return std::pow(normalized, METER_CURVE_EXPONENT);
}

// Convert meter position back to dB (inverse of dbToMeterPos)
float meterPosToDb(float pos) {
    if (pos <= 0.0f)
        return MIN_DB;
    if (pos >= 1.0f)
        return MAX_DB;

    float normalized = std::pow(pos, 1.0f / METER_CURVE_EXPONENT);
    return MIN_DB + normalized * (MAX_DB - MIN_DB);
}

}  // namespace

// Stereo level meter component (L/R bars)
class MixerView::ChannelStrip::LevelMeter : public juce::Component {
  public:
    LevelMeter() = default;

    void setLevel(float newLevel) {
        // Set both channels to the same level (for mono compatibility)
        setLevels(newLevel, newLevel);
    }

    void setLevels(float left, float right) {
        // Allow up to 2.0 gain (+6 dB)
        leftLevel_ = juce::jlimit(0.0f, 2.0f, left);
        rightLevel_ = juce::jlimit(0.0f, 2.0f, right);
        repaint();
    }

    float getLevel() const {
        return std::max(leftLevel_, rightLevel_);
    }

    void paint(juce::Graphics& g) override {
        auto effectiveBounds = getLocalBounds().toFloat();

        // Split into L/R with 1px gap
        const float gap = 1.0f;
        float barWidth = (effectiveBounds.getWidth() - gap) / 2.0f;

        auto leftBounds = effectiveBounds.withWidth(barWidth);
        auto rightBounds =
            effectiveBounds.withWidth(barWidth).withX(effectiveBounds.getX() + barWidth + gap);

        drawMeterBar(g, leftBounds, leftLevel_);
        drawMeterBar(g, rightBounds, rightLevel_);
    }

  private:
    float leftLevel_ = 0.0f;
    float rightLevel_ = 0.0f;

    void drawMeterBar(juce::Graphics& g, juce::Rectangle<float> bounds, float level) {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(bounds, 1.0f);

        // Use power curve to match fader and tick positions
        float displayLevel = dbToMeterPos(gainToDb(level));
        float meterHeight = bounds.getHeight() * displayLevel;
        auto fillBounds = bounds;
        fillBounds = fillBounds.removeFromBottom(meterHeight);

        g.setColour(getMeterColour(level));
        g.fillRoundedRectangle(fillBounds, 1.0f);
    }

    static juce::Colour getMeterColour(float level) {
        float dbLevel = gainToDb(level);
        juce::Colour green(0xFF55AA55);
        juce::Colour yellow(0xFFAAAA55);
        juce::Colour red(0xFFAA5555);

        if (dbLevel < -12.0f) {
            return green;
        } else if (dbLevel < 0.0f) {
            float t = (dbLevel + 12.0f) / 12.0f;
            return green.interpolatedWith(yellow, t);
        } else if (dbLevel < 3.0f) {
            float t = dbLevel / 3.0f;
            return yellow.interpolatedWith(red, t);
        } else {
            return red;
        }
    }
};

// Send area resize handle (horizontal, between sends viewport and fader)
class MixerView::ChannelStrip::SendResizeHandle : public juce::Component {
  public:
    SendResizeHandle() {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    void paint(juce::Graphics& g) override {
        // Single subtle line, highlights on hover
        g.setColour(isHovering_ ? DarkTheme::getColour(DarkTheme::ACCENT_BLUE)
                                : DarkTheme::getColour(DarkTheme::SEPARATOR));
        int y = getHeight() / 2;
        g.fillRect(4, y, getWidth() - 8, 2);
    }

    void mouseEnter(const juce::MouseEvent& /*event*/) override {
        isHovering_ = true;
        repaint();
    }

    void mouseExit(const juce::MouseEvent& /*event*/) override {
        isHovering_ = false;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override {
        isDragging_ = true;
        dragStartY_ = event.getScreenY();
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (!isDragging_ || !onResize)
            return;
        int deltaY = event.getScreenY() - dragStartY_;
        onResize(deltaY);
        dragStartY_ = event.getScreenY();
    }

    void mouseUp(const juce::MouseEvent& /*event*/) override {
        isDragging_ = false;
        isHovering_ = false;
        if (onResizeEnd)
            onResizeEnd();
        repaint();
    }

    std::function<void(int deltaY)> onResize;
    std::function<void()> onResizeEnd;

  private:
    bool isHovering_ = false;
    bool isDragging_ = false;
    int dragStartY_ = 0;
};

// dB scale component — draws tick marks and dB labels, resizes with fader area
class MixerView::ChannelStrip::DbScale : public juce::Component {
  public:
    DbScale() {
        setInterceptsMouseClicks(false, false);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        if (bounds.isEmpty())
            return;

        const auto& metrics = MixerMetrics::getInstance();

        const float dbValues[] = {6.0f,   3.0f,   0.0f,   -3.0f,  -6.0f,
                                  -12.0f, -18.0f, -24.0f, -36.0f, -48.0f};

        // The component has extra padding at top/bottom for label overflow.
        // The fader-aligned area starts at paddingTop and has faderHeight.
        float paddingTop = metrics.labelTextHeight / 2.0f + 1.0f;
        float paddingBottom = metrics.labelTextHeight / 2.0f;
        float top = paddingTop;
        float height = static_cast<float>(bounds.getHeight()) - paddingTop - paddingBottom;
        float totalWidth = static_cast<float>(bounds.getWidth());

        float tickW = metrics.tickWidth();
        float labelWidth = metrics.labelTextWidth;
        float centre = totalWidth / 2.0f;

        g.setFont(FontManager::getInstance().getUIFont(metrics.labelFontSize));

        float minSpacing = metrics.labelTextHeight + 2.0f;
        float lastDrawnY = -1000.0f;

        for (float db : dbValues) {
            float faderPos = dbToMeterPos(db);
            float y = top + height * (1.0f - faderPos);

            if (std::abs(y - lastDrawnY) < minSpacing)
                continue;
            lastDrawnY = y;

            float tickHeight = metrics.tickHeight();
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.fillRect(0.0f, y - tickHeight / 2.0f, tickW - 1.0f, tickHeight);
            g.fillRect(totalWidth - tickW + 1.0f, y - tickHeight / 2.0f, tickW - 1.0f, tickHeight);

            juce::String labelText;
            int dbInt = static_cast<int>(db);
            if (db <= MIN_DB) {
                labelText = juce::String::charToString(0x221E);
            } else {
                labelText = juce::String(std::abs(dbInt));
            }

            float textHeight = metrics.labelTextHeight;
            float textX = centre - labelWidth / 2.0f;
            float textY = y - textHeight / 2.0f;

            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            g.drawText(labelText, static_cast<int>(textX), static_cast<int>(textY),
                       static_cast<int>(labelWidth), static_cast<int>(textHeight),
                       juce::Justification::centred, false);
        }
    }
};

// Channel strip implementation
MixerView::ChannelStrip::ChannelStrip(const TrackInfo& track, AudioEngine* audioEngine,
                                      bool isMaster)
    : trackId_(track.id),
      trackType_(track.type),
      isMaster_(isMaster),
      isChildTrack_(track.hasParent()),
      trackColour_(track.colour),
      trackName_(track.name),
      audioEngine_(audioEngine) {
    setOpaque(true);
    setupControls();
    updateFromTrack(track);
}

MixerView::ChannelStrip::~ChannelStrip() = default;

void MixerView::ChannelStrip::updateFromTrack(const TrackInfo& track) {
    bool wasChild = isChildTrack_;
    isChildTrack_ = track.hasParent();
    trackColour_ = track.colour;
    trackName_ = track.name;
    if (isChildTrack_ != wasChild)
        repaint();

    if (trackLabel) {
        trackLabel->setText(isMaster_ ? "Master" : track.name, juce::dontSendNotification);
    }
    if (volumeSlider && !volumeSlider->isBeingDragged()) {
        float db = gainToDb(track.volume);
        float faderPos = dbToMeterPos(db);
        volumeSlider->setValue(faderPos, juce::dontSendNotification);
    }
    if (panSlider && !panSlider->isBeingDragged()) {
        panSlider->setValue(track.pan, juce::dontSendNotification);
    }
    if (muteButton) {
        muteButton->setToggleState(track.muted, juce::dontSendNotification);
    }
    if (soloButton) {
        soloButton->setToggleState(track.soloed, juce::dontSendNotification);
    }
    if (recordButton) {
        recordButton->setToggleState(track.recordArmed, juce::dontSendNotification);
    }
    if (monitorButton) {
        switch (track.inputMonitor) {
            case InputMonitorMode::Off:
                monitorButton->setButtonText("-");
                break;
            case InputMonitorMode::In:
                monitorButton->setButtonText("I");
                break;
            case InputMonitorMode::Auto:
                monitorButton->setButtonText("A");
                break;
        }
        monitorButton->setToggleState(track.inputMonitor != InputMonitorMode::Off,
                                      juce::dontSendNotification);
    }

    // Sync send slots
    if (!isMaster_) {
        bool sendsCountChanged = sendSlots_.size() != track.sends.size();
        if (sendsCountChanged) {
            rebuildSendSlots(track.sends);
        } else {
            // Update existing slots in-place
            for (size_t i = 0; i < sendSlots_.size(); ++i) {
                auto& slot = sendSlots_[i];
                const auto& send = track.sends[i];
                if (slot->levelSlider && !slot->levelSlider->isBeingDragged())
                    slot->levelSlider->setValue(send.level, juce::dontSendNotification);
                // Update dest name
                if (slot->nameLabel && send.destTrackId != INVALID_TRACK_ID) {
                    if (auto* destTrack = TrackManager::getInstance().getTrack(send.destTrackId))
                        slot->nameLabel->setText(destTrack->name, juce::dontSendNotification);
                }
            }
        }

        // Sync routing selectors from current track state
        if (audioEngine_ && audioInSelector && audioOutSelector && midiInSelector &&
            midiOutSelector) {
            auto* deviceManager = audioEngine_->getDeviceManager();
            auto* device = deviceManager ? deviceManager->getCurrentAudioDevice() : nullptr;
            auto* midiBridge = audioEngine_->getMidiBridge();
            juce::BigInteger enabledIn, enabledOut;
            if (auto* bridge = audioEngine_->getAudioBridge()) {
                enabledIn = bridge->getEnabledInputChannels();
                enabledOut = bridge->getEnabledOutputChannels();
            }
            RoutingSyncHelper::syncSelectorsFromTrack(
                track, audioInSelector.get(), midiInSelector.get(), audioOutSelector.get(),
                midiOutSelector.get(), midiBridge, device, trackId_, outputTrackMapping_,
                midiOutputTrackMapping_, &inputTrackMapping_, enabledIn, enabledOut);
        }
    }

    repaint();
}

void MixerView::ChannelStrip::setupControls() {
    // Track label
    trackLabel = std::make_unique<juce::Label>();
    trackLabel->setText(isMaster_ ? "Master" : trackName_, juce::dontSendNotification);
    trackLabel->setJustificationType(juce::Justification::centred);
    trackLabel->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    trackLabel->setColour(juce::Label::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    trackLabel->setFont(FontManager::getInstance().getUIFont(10.0f));
    addAndMakeVisible(*trackLabel);

    // Pan slider (horizontal TextSlider)
    panSlider = std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Pan);
    panSlider->setOrientation(daw::ui::TextSlider::Orientation::Horizontal);
    panSlider->setRange(-1.0, 1.0, 0.01);
    panSlider->setValue(0.0, juce::dontSendNotification);
    panSlider->setFont(FontManager::getInstance().getUIFont(10.0f));
    panSlider->onValueChanged = [this](double val) {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetTrackPanCommand>(trackId_, static_cast<float>(val)));
    };
    addAndMakeVisible(*panSlider);

    // Level meter
    levelMeter = std::make_unique<LevelMeter>();
    addAndMakeVisible(*levelMeter);

    // dB scale (ticks + labels between fader and meter)
    dbScale_ = std::make_unique<DbScale>();
    addAndMakeVisible(*dbScale_);

    // Peak label
    peakLabel = std::make_unique<juce::Label>();
    peakLabel->setText("-inf", juce::dontSendNotification);
    peakLabel->setJustificationType(juce::Justification::centred);
    peakLabel->setColour(juce::Label::textColourId,
                         DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    peakLabel->setFont(FontManager::getInstance().getUIFont(9.0f));
    addAndMakeVisible(*peakLabel);

    // Volume slider (vertical TextSlider, 0-1 range with power curve mapping)
    volumeSlider = std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decibels);
    volumeSlider->setOrientation(daw::ui::TextSlider::Orientation::Vertical);
    volumeSlider->setRange(0.0, 1.0, 0.001);
    volumeSlider->setValue(dbToMeterPos(0.0f), juce::dontSendNotification);  // 0 dB
    volumeSlider->setFont(FontManager::getInstance().getUIFont(9.0f));
    // Display dB text via custom formatter
    volumeSlider->setValueFormatter([](double pos) -> juce::String {
        float db = meterPosToDb(static_cast<float>(pos));
        if (db <= MIN_DB)
            return "-inf";
        return juce::String(db, 1);
    });
    // Parse typed dB input
    volumeSlider->setValueParser([](const juce::String& text) -> double {
        auto t = text.trim();
        if (t.endsWithIgnoreCase("db"))
            t = t.dropLastCharacters(2).trim();
        if (t.equalsIgnoreCase("-inf") || t.equalsIgnoreCase("inf"))
            return 0.0;
        float db = t.getFloatValue();
        return static_cast<double>(dbToMeterPos(db));
    });
    volumeSlider->onValueChanged = [this](double pos) {
        float db = meterPosToDb(static_cast<float>(pos));
        float gain = dbToGain(db);
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetTrackVolumeCommand>(trackId_, gain));
    };
    addAndMakeVisible(*volumeSlider);

    // Mute button (square corners, compact)
    muteButton = std::make_unique<juce::TextButton>("M");
    muteButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                                  juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    muteButton->setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    muteButton->setColour(juce::TextButton::buttonOnColourId,
                          juce::Colour(0xFFAA8855));  // Orange when active
    muteButton->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    muteButton->setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    muteButton->setClickingTogglesState(true);
    muteButton->onClick = [this]() {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetTrackMuteCommand>(trackId_, muteButton->getToggleState()));
    };
    addAndMakeVisible(*muteButton);

    // Solo button (square corners, compact)
    soloButton = std::make_unique<juce::TextButton>("S");
    soloButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                                  juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    soloButton->setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    soloButton->setColour(juce::TextButton::buttonOnColourId,
                          juce::Colour(0xFFAAAA55));  // Yellow when active
    soloButton->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    soloButton->setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    soloButton->setClickingTogglesState(true);
    soloButton->onClick = [this]() {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetTrackSoloCommand>(trackId_, soloButton->getToggleState()));
    };
    addAndMakeVisible(*soloButton);

    // Record arm button (not on master)
    if (!isMaster_) {
        recordButton = std::make_unique<juce::TextButton>("R");
        recordButton->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        recordButton->setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        recordButton->setColour(juce::TextButton::buttonOnColourId,
                                DarkTheme::getColour(DarkTheme::STATUS_ERROR));  // Red when armed
        recordButton->setColour(juce::TextButton::textColourOffId,
                                DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        recordButton->setColour(juce::TextButton::textColourOnId,
                                DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        recordButton->setClickingTogglesState(true);
        recordButton->onClick = [this]() {
            TrackManager::getInstance().setTrackRecordArmed(trackId_,
                                                            recordButton->getToggleState());
        };
        addAndMakeVisible(*recordButton);

        // Monitor button (3-state: Off → In → Auto → Off)
        monitorButton = std::make_unique<juce::TextButton>("-");
        monitorButton->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        monitorButton->setColour(juce::TextButton::buttonColourId,
                                 DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        monitorButton->setColour(juce::TextButton::buttonOnColourId,
                                 DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
        monitorButton->setColour(juce::TextButton::textColourOffId,
                                 DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        monitorButton->setColour(juce::TextButton::textColourOnId,
                                 DarkTheme::getColour(DarkTheme::BACKGROUND));
        monitorButton->setTooltip("Input monitoring (Off/In/Auto)");
        monitorButton->onClick = [this]() {
            auto* track = TrackManager::getInstance().getTrack(trackId_);
            if (!track)
                return;
            InputMonitorMode nextMode;
            switch (track->inputMonitor) {
                case InputMonitorMode::Off:
                    nextMode = InputMonitorMode::In;
                    break;
                case InputMonitorMode::In:
                    nextMode = InputMonitorMode::Auto;
                    break;
                case InputMonitorMode::Auto:
                    nextMode = InputMonitorMode::Off;
                    break;
            }
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackInputMonitorCommand>(trackId_, nextMode));
        };
        addAndMakeVisible(*monitorButton);

        // Send viewport (scrollable container for send slots)
        sendContainer_ = std::make_unique<juce::Component>();
        sendViewport_ = std::make_unique<juce::Viewport>();
        sendViewport_->setViewedComponent(sendContainer_.get(), false);
        sendViewport_->setScrollBarsShown(false, false, true, false);  // hidden but scrollable
        addAndMakeVisible(*sendViewport_);

        // Send area resize handle (thin horizontal bar below sends viewport)
        sendResizeHandle_ = std::make_unique<SendResizeHandle>();
        sendResizeHandle_->onResize = [this](int deltaY) {
            auto& metrics = MixerMetrics::getInstance();
            int newHeight =
                juce::jlimit(MixerMetrics::minSendAreaHeight, MixerMetrics::maxSendAreaHeight,
                             metrics.sendAreaHeight + deltaY);
            if (metrics.sendAreaHeight != newHeight) {
                metrics.sendAreaHeight = newHeight;
                if (onSendAreaResized)
                    onSendAreaResized();
            }
        };
        addAndMakeVisible(*sendResizeHandle_);

        // Audio/MIDI routing selectors (toggle + dropdown, not on master)
        audioInSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::AudioIn);
        addAndMakeVisible(*audioInSelector);

        audioOutSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::AudioOut);
        addAndMakeVisible(*audioOutSelector);

        midiInSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::MidiIn);
        addAndMakeVisible(*midiInSelector);

        midiOutSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::MidiOut);
        addAndMakeVisible(*midiOutSelector);

        // Populate routing options from real data and wire callbacks
        if (audioEngine_) {
            auto* deviceManager = audioEngine_->getDeviceManager();
            auto* device = deviceManager ? deviceManager->getCurrentAudioDevice() : nullptr;
            auto* midiBridge = audioEngine_->getMidiBridge();

            juce::BigInteger enabledInputChannels, enabledOutputChannels;
            if (auto* bridge = audioEngine_->getAudioBridge()) {
                enabledInputChannels = bridge->getEnabledInputChannels();
                enabledOutputChannels = bridge->getEnabledOutputChannels();
            }

            RoutingSyncHelper::populateAudioInputOptions(audioInSelector.get(), device, trackId_,
                                                         &inputTrackMapping_, enabledInputChannels);
            RoutingSyncHelper::populateAudioOutputOptions(audioOutSelector.get(), trackId_, device,
                                                          outputTrackMapping_,
                                                          enabledOutputChannels);
            RoutingSyncHelper::populateMidiInputOptions(midiInSelector.get(), midiBridge);
            RoutingSyncHelper::populateMidiOutputOptions(midiOutSelector.get(), midiBridge,
                                                         midiOutputTrackMapping_);
        }

        setupRoutingCallbacks();
    }
}

void MixerView::ChannelStrip::setupRoutingCallbacks() {
    if (!audioInSelector || !audioOutSelector || !midiInSelector || !midiOutSelector)
        return;

    auto* midiBridge = audioEngine_ ? audioEngine_->getMidiBridge() : nullptr;

    // Audio input selector callbacks (mutually exclusive with MIDI input)
    audioInSelector->onEnabledChanged = [this](bool enabled) {
        if (enabled) {
            midiInSelector->setEnabled(false);
            TrackManager::getInstance().setTrackMidiInput(trackId_, "");
            auto* trackInfo = TrackManager::getInstance().getTrack(trackId_);
            if (trackInfo && trackInfo->audioInputDevice.startsWith("track:"))
                TrackManager::getInstance().setTrackAudioInput(trackId_,
                                                               trackInfo->audioInputDevice);
            else
                TrackManager::getInstance().setTrackAudioInput(trackId_, "default");
        } else {
            TrackManager::getInstance().setTrackAudioInput(trackId_, "");
        }
    };

    audioInSelector->onSelectionChanged = [this](int selectedId) {
        if (selectedId == 1) {
            TrackManager::getInstance().setTrackAudioInput(trackId_, "");
        } else if (selectedId >= 200) {
            auto it = inputTrackMapping_.find(selectedId);
            if (it != inputTrackMapping_.end()) {
                TrackManager::getInstance().setTrackAudioInput(trackId_,
                                                               "track:" + juce::String(it->second));
            }
        } else if (selectedId >= 10) {
            TrackManager::getInstance().setTrackAudioInput(trackId_, "default");
        }
    };

    // MIDI input selector callbacks (mutually exclusive with audio input)
    midiInSelector->onEnabledChanged = [this, midiBridge](bool enabled) {
        if (enabled) {
            audioInSelector->setEnabled(false);
            TrackManager::getInstance().setTrackAudioInput(trackId_, "");
            int selectedId = midiInSelector->getSelectedId();
            if (selectedId == 1) {
                TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
            } else if (selectedId >= 10 && midiBridge) {
                auto midiInputs = midiBridge->getAvailableMidiInputs();
                int deviceIndex = selectedId - 10;
                if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size())) {
                    TrackManager::getInstance().setTrackMidiInput(trackId_,
                                                                  midiInputs[deviceIndex].id);
                } else {
                    TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
                }
            } else {
                TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
            }
        } else {
            TrackManager::getInstance().setTrackMidiInput(trackId_, "");
        }
    };

    midiInSelector->onSelectionChanged = [this, midiBridge](int selectedId) {
        if (selectedId == 2) {
            TrackManager::getInstance().setTrackMidiInput(trackId_, "");
        } else if (selectedId == 1) {
            TrackManager::getInstance().setTrackMidiInput(trackId_, "all");
        } else if (selectedId >= 10 && midiBridge) {
            auto midiInputs = midiBridge->getAvailableMidiInputs();
            int deviceIndex = selectedId - 10;
            if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size())) {
                TrackManager::getInstance().setTrackMidiInput(trackId_, midiInputs[deviceIndex].id);
            }
        }
    };

    // Output selector callbacks
    audioOutSelector->onEnabledChanged = [this](bool enabled) {
        if (enabled) {
            TrackManager::getInstance().setTrackAudioOutput(trackId_, "master");
        } else {
            TrackManager::getInstance().setTrackAudioOutput(trackId_, "");
        }
    };

    audioOutSelector->onSelectionChanged = [this](int selectedId) {
        if (selectedId == 1) {
            TrackManager::getInstance().setTrackAudioOutput(trackId_, "master");
        } else if (selectedId == 2) {
            TrackManager::getInstance().setTrackAudioOutput(trackId_, "");
        } else if (selectedId >= 200) {
            auto it = outputTrackMapping_.find(selectedId);
            if (it != outputTrackMapping_.end()) {
                TrackManager::getInstance().setTrackAudioOutput(
                    trackId_, "track:" + juce::String(it->second));
            }
        } else if (selectedId >= 10) {
            TrackManager::getInstance().setTrackAudioOutput(trackId_, "master");
        }
    };

    // MIDI output selector callbacks
    midiOutSelector->onEnabledChanged = [this](bool enabled) {
        if (!enabled) {
            TrackManager::getInstance().setTrackMidiOutput(trackId_, "");
        }
    };

    midiOutSelector->onSelectionChanged = [this, midiBridge](int selectedId) {
        if (selectedId == 1) {
            TrackManager::getInstance().setTrackMidiOutput(trackId_, "");
        } else if (selectedId >= 200) {
            auto it = midiOutputTrackMapping_.find(selectedId);
            if (it != midiOutputTrackMapping_.end()) {
                TrackManager::getInstance().setTrackMidiOutput(trackId_,
                                                               "track:" + juce::String(it->second));
            }
        } else if (selectedId >= 10 && midiBridge) {
            auto midiOutputs = midiBridge->getAvailableMidiOutputs();
            int deviceIndex = selectedId - 10;
            if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiOutputs.size())) {
                TrackManager::getInstance().setTrackMidiOutput(trackId_,
                                                               midiOutputs[deviceIndex].id);
            }
        }
    };
}

void MixerView::ChannelStrip::rebuildSendSlots(const std::vector<SendInfo>& sends) {
    // Remove old slots from send container
    for (auto& slot : sendSlots_) {
        if (sendContainer_) {
            sendContainer_->removeChildComponent(slot->nameLabel.get());
            sendContainer_->removeChildComponent(slot->levelSlider.get());
            sendContainer_->removeChildComponent(slot->removeButton.get());
        }
    }
    sendSlots_.clear();

    for (const auto& send : sends) {
        auto slot = std::make_unique<SendSlot>();
        slot->busIndex = send.busIndex;

        // Destination name label
        slot->nameLabel = std::make_unique<juce::Label>();
        juce::String destName = "Bus " + juce::String(send.busIndex);
        if (send.destTrackId != INVALID_TRACK_ID) {
            if (auto* destTrack = TrackManager::getInstance().getTrack(send.destTrackId))
                destName = destTrack->name;
        }
        slot->nameLabel->setText(destName, juce::dontSendNotification);
        slot->nameLabel->setFont(FontManager::getInstance().getUIFont(9.0f));
        slot->nameLabel->setColour(juce::Label::textColourId,
                                   DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        slot->nameLabel->setJustificationType(juce::Justification::centredLeft);
        sendContainer_->addAndMakeVisible(*slot->nameLabel);

        // Level slider (horizontal, 0-1)
        slot->levelSlider =
            std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decimal);
        slot->levelSlider->setOrientation(daw::ui::TextSlider::Orientation::Horizontal);
        slot->levelSlider->setRange(0.0, 1.0, 0.01);
        slot->levelSlider->setValue(send.level, juce::dontSendNotification);
        slot->levelSlider->setFont(FontManager::getInstance().getUIFont(9.0f));
        int busIdx = send.busIndex;
        slot->levelSlider->onValueChanged = [this, busIdx](double val) {
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetSendLevelCommand>(trackId_, busIdx, static_cast<float>(val)));
        };
        sendContainer_->addAndMakeVisible(*slot->levelSlider);

        // Remove button
        slot->removeButton = std::make_unique<juce::TextButton>("x");
        slot->removeButton->setConnectedEdges(
            juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
            juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        slot->removeButton->setColour(juce::TextButton::buttonColourId,
                                      DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        slot->removeButton->setColour(juce::TextButton::textColourOffId,
                                      DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        slot->removeButton->onClick = [this, busIdx]() {
            TrackManager::getInstance().removeSend(trackId_, busIdx);
        };
        sendContainer_->addAndMakeVisible(*slot->removeButton);

        sendSlots_.push_back(std::move(slot));
    }

    resized();
}

void MixerView::ChannelStrip::paint(juce::Graphics& g) {
    auto fullBounds = getLocalBounds();
    bool hasGroupChildren = !groupChildren_.empty();
    bool isNestedInGroup = dynamic_cast<ChannelStrip*>(getParentComponent()) != nullptr;

    // The group's own controls column (leftmost channelWidth when group has children)
    auto ownBounds = hasGroupChildren
                         ? fullBounds.withWidth(MixerMetrics::getInstance().channelWidth)
                         : fullBounds;

    // Background
    if (selected) {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    }
    g.fillRect(ownBounds);

    // Separator on right side of own column
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
    g.fillRect(ownBounds.getRight() - 1, 0, 1, ownBounds.getHeight());

    // Channel color indicator at top — skip for children nested in group (envelope provides this)
    // When selected, use accent blue as top border instead of track colour
    if (!isNestedInGroup) {
        if (selected) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        } else if (!isMaster_) {
            g.setColour(trackColour_);
        } else {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        }
        g.fillRect(0, 0, ownBounds.getWidth() - 1, 4);
    }

    // Draw fader region border (top and bottom lines)
    if (!faderRegion_.isEmpty()) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.fillRect(faderRegion_.getX(), faderRegion_.getY(), faderRegion_.getWidth(), 1);
        g.fillRect(faderRegion_.getX(), faderRegion_.getBottom() - 1, faderRegion_.getWidth(), 1);
    }

    // dB ticks and labels are drawn by the DbScale component

    // Group envelope: header banner + border around the entire group area
    if (hasGroupChildren) {
        const int groupHeaderHeight = 4 + 4 + 24 + MixerMetrics::getInstance().controlSpacing;

        // Fill the header banner area across full width
        g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        g.fillRect(0, 0, fullBounds.getWidth(), groupHeaderHeight);

        // Colour bar across entire top
        g.setColour(trackColour_);
        g.fillRect(2, 2, fullBounds.getWidth() - 4, 4);

        // Horizontal separator below header (drawn in paint, before children)
        g.setColour(trackColour_.withAlpha(0.4f));
        g.fillRect(0, groupHeaderHeight, fullBounds.getWidth(), 1);
    }
}

void MixerView::ChannelStrip::paintOverChildren(juce::Graphics& g) {
    // Group envelope border — drawn over children so it's not obscured
    if (!groupChildren_.empty()) {
        auto fullBounds = getLocalBounds();
        g.setColour(trackColour_.withAlpha(0.6f));
        g.drawRect(fullBounds, 2);
    }

    // Skip overlay for child tracks nested inside a group envelope
    if (!isChildTrack_)
        return;

    // Check if this strip is a child component of a group strip
    if (auto* parentStrip = dynamic_cast<ChannelStrip*>(getParentComponent())) {
        if (!parentStrip->groupChildren_.empty())
            return;  // Nested inside group — no overlay needed
    }

    // Semi-transparent overlay to dim child tracks (fallback for non-group children)
    g.setColour(juce::Colour(0x30000000));
    g.fillRect(getLocalBounds());

    // Left-edge bracket bar showing group membership
    g.setColour(trackColour_.withAlpha(0.8f));
    g.fillRect(0, 0, 3, getHeight());
}

void MixerView::ChannelStrip::resized() {
    const auto& metrics = MixerMetrics::getInstance();
    bool hasGroupChildren = !groupChildren_.empty();

    // Group envelope header height: colour bar + padding + label + spacing
    const int groupHeaderHeight = 4 + 4 + 24 + metrics.controlSpacing;

    // If this is a group strip with children, lay out the shared header banner
    // across the full width, then position children below it
    if (hasGroupChildren) {
        int channelWidth = metrics.channelWidth;
        int childTop = groupHeaderHeight;
        int childHeight = getHeight() - childTop;

        const int borderWidth = 2;
        for (size_t i = 0; i < groupChildren_.size(); ++i) {
            bool isLast = (i == groupChildren_.size() - 1);
            int w = isLast ? channelWidth - borderWidth : channelWidth;
            groupChildren_[i]->setBounds(static_cast<int>(i + 1) * channelWidth, childTop, w,
                                         childHeight);
        }
    }

    // For a group with children: own controls in the leftmost column, below the shared header
    // For everything else: full width, full height
    int ownWidth = hasGroupChildren ? metrics.channelWidth : getWidth();
    int ownTop = hasGroupChildren ? groupHeaderHeight : 0;
    int ownHeight = getHeight() - ownTop;

    auto bounds =
        juce::Rectangle<int>(0, ownTop, ownWidth, ownHeight).reduced(metrics.channelPadding);

    if (hasGroupChildren) {
        // Group: label is part of the shared header banner (already positioned above)
        // Position label in the header area spanning full width
        auto headerBounds = juce::Rectangle<int>(0, 0, getWidth(), groupHeaderHeight)
                                .reduced(metrics.channelPadding);
        headerBounds.removeFromTop(6);  // colour bar space
        auto titleRow = headerBounds.removeFromTop(24);
        if (expandToggle_) {
            expandToggle_->setBounds(titleRow.removeFromLeft(20).withSizeKeepingCentre(18, 18));
            titleRow.removeFromLeft(2);
        }
        trackLabel->setBounds(titleRow);
    } else {
        // Non-group: colour bar space + label at top of own bounds
        bounds.removeFromTop(6);
        auto titleRow = bounds.removeFromTop(24);
        if (expandToggle_) {
            expandToggle_->setBounds(titleRow.removeFromLeft(20).withSizeKeepingCentre(18, 18));
            titleRow.removeFromLeft(2);
        }
        trackLabel->setBounds(titleRow);
    }
    bounds.removeFromTop(metrics.controlSpacing);

    // Sends area (scrollable viewport) — between track label and fader
    if (!isMaster_ && sendViewport_) {
        const int sendSlotHeight = 18;
        const int sendAreaHeight = metrics.sendAreaHeight;

        // Layout send slots inside the container
        int containerWidth = bounds.getWidth();
        int totalContentHeight = 0;
        for (auto& slot : sendSlots_) {
            auto row = juce::Rectangle<int>(0, totalContentHeight, containerWidth, sendSlotHeight);
            slot->nameLabel->setBounds(row.removeFromLeft(row.getWidth() * 40 / 100));
            auto removeArea = row.removeFromRight(16);
            slot->removeButton->setBounds(removeArea);
            slot->levelSlider->setBounds(row);
            totalContentHeight += sendSlotHeight + 1;  // 1px gap
        }

        sendContainer_->setBounds(0, 0, containerWidth, totalContentHeight);
        bounds.removeFromTop(2);  // Gap between track header and sends/handle
        sendViewport_->setBounds(bounds.removeFromTop(sendAreaHeight));
        sendViewport_->setVisible(sendAreaHeight > 0);

        // Resize handle overlapping the bottom of the sends viewport
        if (sendResizeHandle_) {
            int handleH = 8;
            int handleOverlap = 6;
            sendResizeHandle_->setBounds(bounds.getX(), bounds.getY() - handleH - handleOverlap,
                                         bounds.getWidth(), handleH);
            sendResizeHandle_->setAlwaysOnTop(true);
        }
    }

    // M/S/R/Mon buttons at bottom — multi-out children only show M/S
    bool isMultiOut = trackType_ == TrackType::MultiOut;
    auto buttonArea = bounds.removeFromBottom(metrics.buttonSize);

    if (isMultiOut || isMaster_ || !recordButton) {
        // M/S only
        int buttonWidth = (buttonArea.getWidth() - 2) / 2;
        muteButton->setBounds(buttonArea.removeFromLeft(buttonWidth));
        buttonArea.removeFromLeft(2);
        soloButton->setBounds(buttonArea);
        if (recordButton)
            recordButton->setVisible(false);
        if (monitorButton)
            monitorButton->setVisible(false);
    } else {
        int numButtons = monitorButton ? 4 : 3;
        int buttonWidth = (buttonArea.getWidth() - (numButtons - 1) * 2) / numButtons;
        muteButton->setBounds(buttonArea.removeFromLeft(buttonWidth));
        buttonArea.removeFromLeft(2);
        soloButton->setBounds(buttonArea.removeFromLeft(buttonWidth));
        buttonArea.removeFromLeft(2);
        recordButton->setVisible(true);
        if (!monitorButton) {
            recordButton->setBounds(buttonArea);
        } else {
            monitorButton->setVisible(true);
            recordButton->setBounds(buttonArea.removeFromLeft(buttonWidth));
            buttonArea.removeFromLeft(2);
            monitorButton->setBounds(buttonArea);
        }
    }

    // Routing selectors above M/S/R
    // Multi-out children: show audio out only (no input, no MIDI)
    if (audioInSelector && audioOutSelector && midiInSelector && midiOutSelector) {
        if (metrics.showRouting) {
            bool showInputs = !isMultiOut;
            bool showMidi = !isMultiOut;

            audioOutSelector->setVisible(true);
            audioInSelector->setVisible(showInputs);
            midiInSelector->setVisible(showMidi);
            midiOutSelector->setVisible(showMidi);

            bounds.removeFromBottom(2);  // Small gap

            if (showMidi) {
                // MIDI row (Mi/Mo)
                auto midiRow = bounds.removeFromBottom(16);
                int halfWidth = (midiRow.getWidth() - 2) / 2;
                midiInSelector->setBounds(midiRow.removeFromLeft(halfWidth));
                midiRow.removeFromLeft(2);
                midiOutSelector->setBounds(midiRow);

                bounds.removeFromBottom(2);  // Small gap
            }

            // Audio row (Ai/Ao or just Ao for multi-out)
            auto audioRow = bounds.removeFromBottom(16);
            if (showInputs) {
                int halfWidth = (audioRow.getWidth() - 2) / 2;
                audioInSelector->setBounds(audioRow.removeFromLeft(halfWidth));
                audioRow.removeFromLeft(2);
                audioOutSelector->setBounds(audioRow);
            } else {
                audioOutSelector->setBounds(audioRow);
            }
        } else {
            audioInSelector->setVisible(false);
            audioOutSelector->setVisible(false);
            midiInSelector->setVisible(false);
            midiOutSelector->setVisible(false);
        }
    }

    // Pan slider — now below fader region, above routing
    bounds.removeFromBottom(2);
    panSlider->setBounds(bounds.removeFromBottom(20));
    bounds.removeFromBottom(2);

    // Small gap before fader region
    bounds.removeFromTop(2);

    // Layout: [fader] [gap] [leftTicks] [labels] [rightTicks] [gap] [meter]
    // Fader and meter scale proportionally with channel width
    int availWidth = bounds.getWidth();
    int faderWidth = juce::jlimit(20, 60, availWidth * 40 / 100);
    int meterWidthVal = faderWidth;  // Same width as fader
    int gap = metrics.tickToFaderGap;

    // Store the entire fader region for border drawing
    faderRegion_ = bounds;

    // Position peak label right above the fader region top border
    const int labelHeight = 12;
    auto valueLabelArea =
        juce::Rectangle<int>(faderRegion_.getX(), faderRegion_.getY() - labelHeight,
                             faderRegion_.getWidth(), labelHeight);
    peakLabel->setBounds(valueLabelArea);

    // Add vertical padding inside the border
    bounds.removeFromTop(6);
    bounds.removeFromBottom(3);

    auto layoutArea = bounds;

    // Volume TextSlider on left
    faderArea_ = layoutArea.removeFromLeft(faderWidth);
    volumeSlider->setBounds(faderArea_);

    // Meter on right
    meterArea_ = layoutArea.removeFromRight(meterWidthVal);
    levelMeter->setBounds(meterArea_);

    // dB scale component — extends above/below fader area for label overflow
    if (dbScale_) {
        int labelPad = static_cast<int>(metrics.labelTextHeight / 2.0f + 1.0f);
        int scaleLeft = faderArea_.getRight() + gap;
        int scaleRight = meterArea_.getX() - metrics.tickToMeterGap;
        dbScale_->setBounds(scaleLeft, layoutArea.getY() - labelPad, scaleRight - scaleLeft,
                            layoutArea.getHeight() + labelPad * 2);
    }
}

void MixerView::ChannelStrip::setMeterLevel(float level) {
    setMeterLevels(level, level);
}

void MixerView::ChannelStrip::setMeterLevels(float leftLevel, float rightLevel) {
    meterLevel = std::max(leftLevel, rightLevel);
    if (levelMeter) {
        levelMeter->setLevels(leftLevel, rightLevel);
    }

    // Update peak value
    float maxLevel = std::max(leftLevel, rightLevel);
    if (maxLevel > peakValue_) {
        peakValue_ = maxLevel;
        if (peakLabel) {
            float db = gainToDb(peakValue_);
            juce::String peakText;
            if (db <= MIN_DB) {
                peakText = "-inf";
            } else {
                peakText = juce::String(db, 1);
            }
            peakLabel->setText(peakText, juce::dontSendNotification);
        }
    }
}

void MixerView::ChannelStrip::setSelected(bool shouldBeSelected) {
    if (selected != shouldBeSelected) {
        selected = shouldBeSelected;
        repaint();
    }
}

void MixerView::ChannelStrip::mouseDown(const juce::MouseEvent& event) {
    if (event.mods.isPopupMenu()) {
        juce::PopupMenu menu;

        // Add Send submenu (not for master)
        if (!isMaster_) {
            juce::PopupMenu sendSubMenu;
            const auto& tracks = TrackManager::getInstance().getTracks();
            std::set<TrackId> existingSendDests;
            if (auto* thisTrack = TrackManager::getInstance().getTrack(trackId_)) {
                for (const auto& send : thisTrack->sends)
                    existingSendDests.insert(send.destTrackId);
            }
            for (const auto& t : tracks) {
                if (t.id != trackId_ && t.type != TrackType::Master &&
                    existingSendDests.find(t.id) == existingSendDests.end()) {
                    sendSubMenu.addItem(t.id, t.name);
                }
            }
            if (sendSubMenu.getNumItems() == 0) {
                sendSubMenu.addItem(-1, "(No tracks available)", false);
            }
            menu.addSubMenu("Add Send", sendSubMenu);
            menu.addSeparator();
        }

        // Show/hide I/O routing toggle
        auto& metrics = MixerMetrics::getInstance();
        const int toggleRoutingId = -100;
        menu.addItem(toggleRoutingId, "Show I/O Routing", true, metrics.showRouting);

        menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
            if (result == -100) {
                auto& m = MixerMetrics::getInstance();
                m.showRouting = !m.showRouting;
                if (onSendAreaResized)
                    onSendAreaResized();  // Triggers relayout of all strips
            } else if (result > 0) {
                TrackManager::getInstance().addSend(trackId_, static_cast<TrackId>(result));
            }
        });
    } else if (onClicked) {
        onClicked(trackId_, isMaster_);
    }
}

//==============================================================================
// DrumSubChannelStrip - LevelMeter (same as ChannelStrip::LevelMeter)
//==============================================================================
class MixerView::DrumSubChannelStrip::LevelMeter : public juce::Component {
  public:
    LevelMeter() = default;

    void setLevels(float left, float right) {
        leftLevel_ = juce::jlimit(0.0f, 2.0f, left);
        rightLevel_ = juce::jlimit(0.0f, 2.0f, right);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto effectiveBounds = getLocalBounds().toFloat();
        const float gap = 1.0f;
        float barWidth = (effectiveBounds.getWidth() - gap) / 2.0f;
        auto leftBounds = effectiveBounds.withWidth(barWidth);
        auto rightBounds =
            effectiveBounds.withWidth(barWidth).withX(effectiveBounds.getX() + barWidth + gap);
        drawMeterBar(g, leftBounds, leftLevel_);
        drawMeterBar(g, rightBounds, rightLevel_);
    }

  private:
    float leftLevel_ = 0.0f;
    float rightLevel_ = 0.0f;

    void drawMeterBar(juce::Graphics& g, juce::Rectangle<float> bounds, float level) {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(bounds, 1.0f);
        float displayLevel = dbToMeterPos(gainToDb(level));
        float meterHeight = bounds.getHeight() * displayLevel;
        auto fillBounds = bounds;
        fillBounds = fillBounds.removeFromBottom(meterHeight);
        g.setColour(getMeterColour(level));
        g.fillRoundedRectangle(fillBounds, 1.0f);
    }

    static juce::Colour getMeterColour(float level) {
        float dbLevel = gainToDb(level);
        juce::Colour green(0xFF55AA55);
        juce::Colour yellow(0xFFAAAA55);
        juce::Colour red(0xFFAA5555);
        if (dbLevel < -12.0f)
            return green;
        else if (dbLevel < 0.0f)
            return green.interpolatedWith(yellow, (dbLevel + 12.0f) / 12.0f);
        else if (dbLevel < 3.0f)
            return yellow.interpolatedWith(red, dbLevel / 3.0f);
        else
            return red;
    }
};

//==============================================================================
// DrumSubChannelStrip implementation
//==============================================================================
MixerView::DrumSubChannelStrip::DrumSubChannelStrip(daw::audio::DrumGridPlugin* dg, int chainIndex,
                                                    const juce::String& name,
                                                    juce::Colour parentColour)
    : drumGrid_(dg), chainIndex_(chainIndex), parentColour_(parentColour), chainName_(name) {
    setupControls();
    updateFromChain();
}

MixerView::DrumSubChannelStrip::~DrumSubChannelStrip() = default;

void MixerView::DrumSubChannelStrip::setupControls() {
    // Track label
    trackLabel = std::make_unique<juce::Label>();
    trackLabel->setText(chainName_, juce::dontSendNotification);
    trackLabel->setJustificationType(juce::Justification::centred);
    trackLabel->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    trackLabel->setColour(juce::Label::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    trackLabel->setFont(FontManager::getInstance().getUIFont(10.0f));
    addAndMakeVisible(*trackLabel);

    // Pan slider (horizontal TextSlider)
    panSlider = std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Pan);
    panSlider->setOrientation(daw::ui::TextSlider::Orientation::Horizontal);
    panSlider->setRange(-1.0, 1.0, 0.01);
    panSlider->setValue(0.0, juce::dontSendNotification);
    panSlider->setFont(FontManager::getInstance().getUIFont(10.0f));
    panSlider->onValueChanged = [this](double val) {
        if (auto* chain = drumGrid_->getChainByIndexMutable(chainIndex_))
            chain->pan = static_cast<float>(val);
    };
    addAndMakeVisible(*panSlider);

    // Level meter
    levelMeter = std::make_unique<LevelMeter>();
    addAndMakeVisible(*levelMeter);

    // Peak label
    peakLabel = std::make_unique<juce::Label>();
    peakLabel->setText("-inf", juce::dontSendNotification);
    peakLabel->setJustificationType(juce::Justification::centred);
    peakLabel->setColour(juce::Label::textColourId,
                         DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    peakLabel->setFont(FontManager::getInstance().getUIFont(9.0f));
    addAndMakeVisible(*peakLabel);

    // Volume slider (vertical TextSlider, 0-1 range with power curve mapping)
    volumeSlider = std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decibels);
    volumeSlider->setOrientation(daw::ui::TextSlider::Orientation::Vertical);
    volumeSlider->setRange(0.0, 1.0, 0.001);
    volumeSlider->setValue(dbToMeterPos(0.0f), juce::dontSendNotification);
    volumeSlider->setFont(FontManager::getInstance().getUIFont(9.0f));
    volumeSlider->setValueFormatter([](double pos) -> juce::String {
        float db = meterPosToDb(static_cast<float>(pos));
        if (db <= MIN_DB)
            return "-inf";
        return juce::String(db, 1);
    });
    volumeSlider->setValueParser([](const juce::String& text) -> double {
        auto t = text.trim();
        if (t.endsWithIgnoreCase("db"))
            t = t.dropLastCharacters(2).trim();
        if (t.equalsIgnoreCase("-inf") || t.equalsIgnoreCase("inf"))
            return 0.0;
        float db = t.getFloatValue();
        return static_cast<double>(dbToMeterPos(db));
    });
    volumeSlider->onValueChanged = [this](double pos) {
        float db = meterPosToDb(static_cast<float>(pos));
        if (auto* chain = drumGrid_->getChainByIndexMutable(chainIndex_))
            chain->level = db;
    };
    addAndMakeVisible(*volumeSlider);

    // Mute button
    muteButton = std::make_unique<juce::TextButton>("M");
    muteButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                                  juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    muteButton->setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    muteButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFAA8855));
    muteButton->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    muteButton->setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    muteButton->setClickingTogglesState(true);
    muteButton->onClick = [this]() {
        if (auto* chain = drumGrid_->getChainByIndexMutable(chainIndex_))
            chain->mute = muteButton->getToggleState();
    };
    addAndMakeVisible(*muteButton);

    // Solo button
    soloButton = std::make_unique<juce::TextButton>("S");
    soloButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                                  juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    soloButton->setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    soloButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFAAAA55));
    soloButton->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    soloButton->setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    soloButton->setClickingTogglesState(true);
    soloButton->onClick = [this]() {
        if (auto* chain = drumGrid_->getChainByIndexMutable(chainIndex_))
            chain->solo = soloButton->getToggleState();
    };
    addAndMakeVisible(*soloButton);
}

void MixerView::DrumSubChannelStrip::updateFromChain() {
    const auto* chain = drumGrid_->getChainByIndex(chainIndex_);
    if (!chain)
        return;

    if (trackLabel)
        trackLabel->setText(chain->name.isNotEmpty() ? chain->name : chainName_,
                            juce::dontSendNotification);
    if (volumeSlider && !volumeSlider->isBeingDragged()) {
        float db = chain->level.get();
        float faderPos = dbToMeterPos(db);
        volumeSlider->setValue(faderPos, juce::dontSendNotification);
    }
    if (panSlider && !panSlider->isBeingDragged())
        panSlider->setValue(chain->pan.get(), juce::dontSendNotification);
    if (muteButton)
        muteButton->setToggleState(chain->mute.get(), juce::dontSendNotification);
    if (soloButton)
        soloButton->setToggleState(chain->solo.get(), juce::dontSendNotification);
}

void MixerView::DrumSubChannelStrip::setMeterLevels(float l, float r) {
    if (levelMeter)
        levelMeter->setLevels(l, r);

    float maxLevel = std::max(l, r);
    if (maxLevel > peakValue_) {
        peakValue_ = maxLevel;
        if (peakLabel) {
            float db = gainToDb(peakValue_);
            juce::String peakText;
            if (db <= MIN_DB)
                peakText = "-inf";
            else
                peakText = juce::String(db, 1);
            peakLabel->setText(peakText, juce::dontSendNotification);
        }
    }
}

void MixerView::DrumSubChannelStrip::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Slightly dimmer background for sub-channels
    g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND).darker(0.15f));
    g.fillRect(bounds);

    // Border on right side (separator)
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
    g.fillRect(bounds.getRight() - 1, 0, 1, bounds.getHeight());

    // Indented color bar at top matching parent track colour
    g.setColour(parentColour_.withAlpha(0.6f));
    g.fillRect(4, 0, getWidth() - 5, 3);

    // Draw fader region border
    if (!faderRegion_.isEmpty()) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.fillRect(faderRegion_.getX(), faderRegion_.getY(), faderRegion_.getWidth(), 1);
        g.fillRect(faderRegion_.getX(), faderRegion_.getBottom() - 1, faderRegion_.getWidth(), 1);
    }
}

void MixerView::DrumSubChannelStrip::resized() {
    const auto& metrics = MixerMetrics::getInstance();
    auto bounds = getLocalBounds().reduced(metrics.channelPadding);

    // Color indicator space
    bounds.removeFromTop(5);

    // Track label at top
    trackLabel->setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(metrics.controlSpacing);

    // M/S buttons at bottom (no record button for sub-channels)
    auto buttonArea = bounds.removeFromBottom(metrics.buttonSize);
    int buttonWidth = (buttonArea.getWidth() - 2) / 2;
    muteButton->setBounds(buttonArea.removeFromLeft(buttonWidth));
    buttonArea.removeFromLeft(2);
    soloButton->setBounds(buttonArea.removeFromLeft(buttonWidth));

    bounds.removeFromBottom(metrics.controlSpacing);

    // Pan slider above buttons (horizontal TextSlider)
    panSlider->setBounds(bounds.removeFromBottom(20));
    bounds.removeFromBottom(metrics.controlSpacing);

    // Fader region
    int faderHeight = static_cast<int>(bounds.getHeight() * metrics.faderHeightRatio / 100.0f);
    int extraSpace = bounds.getHeight() - faderHeight;
    bounds.removeFromTop(extraSpace / 2);
    bounds.setHeight(faderHeight);

    int availWidth = bounds.getWidth();
    int faderWidth = juce::jlimit(20, 60, availWidth * 40 / 100);
    int meterWidthVal = faderWidth;  // Same width as fader

    faderRegion_ = bounds;

    // Peak label above fader region
    const int labelHeight = 12;
    auto valueLabelArea =
        juce::Rectangle<int>(faderRegion_.getX(), faderRegion_.getY() - labelHeight,
                             faderRegion_.getWidth(), labelHeight);
    peakLabel->setBounds(valueLabelArea);

    const int borderPadding = 6;
    bounds.removeFromTop(borderPadding);
    bounds.removeFromBottom(borderPadding);

    auto layoutArea = bounds;

    // Volume TextSlider on left
    faderArea_ = layoutArea.removeFromLeft(faderWidth);
    volumeSlider->setBounds(faderArea_);

    // Meter on right
    meterArea_ = layoutArea.removeFromRight(meterWidthVal);
    levelMeter->setBounds(meterArea_);
}

void MixerView::DrumSubChannelStrip::mouseDown(const juce::MouseEvent& /*event*/) {
    if (onClicked)
        onClicked();
}

//==============================================================================
// Helper: find DrumGridPlugin for a track via TE plugin list
//==============================================================================
namespace {
daw::audio::DrumGridPlugin* findDrumGridForTrack(const TrackInfo& track, AudioEngine* audioEngine) {
    if (!audioEngine)
        return nullptr;
    auto* bridge = audioEngine->getAudioBridge();
    if (!bridge)
        return nullptr;

    auto* teTrack = bridge->getAudioTrack(track.id);
    if (!teTrack)
        return nullptr;

    for (auto* plugin : teTrack->pluginList) {
        // Direct match (not rack-wrapped)
        if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(plugin))
            return dg;

        // Check inside rack instances (DrumGridPlugin is rack-wrapped)
        if (auto* rackInstance = dynamic_cast<te::RackInstance*>(plugin)) {
            if (rackInstance->type != nullptr) {
                for (auto* innerPlugin : rackInstance->type->getPlugins()) {
                    if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(innerPlugin))
                        return dg;
                }
            }
        }
    }
    return nullptr;
}
}  // namespace

// MixerView implementation
MixerView::MixerView(AudioEngine* audioEngine) : audioEngine_(audioEngine) {
    // Get current view mode
    currentViewMode_ = ViewModeController::getInstance().getViewMode();

    // Create channel container
    channelContainer = std::make_unique<juce::Component>();
    channelContainer->setPaintingIsUnclipped(true);

    // Create viewport for scrollable channels
    channelViewport = std::make_unique<juce::Viewport>();
    channelViewport->setViewedComponent(channelContainer.get(), false);
    channelViewport->setScrollBarsShown(false, true);  // Horizontal scroll only
    addAndMakeVisible(*channelViewport);

    // Create aux container (fixed, between channels and master)
    auxContainer = std::make_unique<juce::Component>();
    addAndMakeVisible(*auxContainer);

    // Create master strip (uses shared MasterChannelStrip component)
    masterStrip = std::make_unique<MasterChannelStrip>(MasterChannelStrip::Orientation::Vertical);
    masterStrip->onSendAreaResized = [this]() { relayoutAllStrips(); };
    addAndMakeVisible(*masterStrip);

    // Create channel resize handle (thin, overlaps right edge of last channel strip)
    channelResizeHandle_ = std::make_unique<ChannelResizeHandle>();
    channelResizeHandle_->onResize = [this](int deltaX) {
        auto& metrics = MixerMetrics::getInstance();
        int newWidth =
            juce::jlimit(minChannelWidth_, maxChannelWidth_, metrics.channelWidth + deltaX);
        if (metrics.channelWidth != newWidth) {
            isResizeDragging_ = true;
            metrics.channelWidth = newWidth;
            // Coalesce: just store the new width, apply on next vblank
            if (!pendingResizeUpdate_) {
                pendingResizeUpdate_ = true;
                juce::Component::SafePointer<MixerView> safeThis(this);
                juce::MessageManager::callAsync([safeThis]() {
                    if (auto* self = safeThis.getComponent()) {
                        if (self->pendingResizeUpdate_) {
                            self->pendingResizeUpdate_ = false;
                            self->updateStripWidths();
                        }
                    }
                });
            }
        }
    };
    channelResizeHandle_->onResizeEnd = [this]() {
        isResizeDragging_ = false;
        pendingResizeUpdate_ = false;
        updateStripWidths();  // Ensure final width is applied
    };
    channelContainer->addAndMakeVisible(*channelResizeHandle_);

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Register as ViewModeController listener
    ViewModeController::getInstance().addListener(this);

    // Build channel strips from TrackManager
    rebuildChannelStrips();

    // Debug panel disabled - remove F12 toggle
    // debugPanel_ = std::make_unique<MixerDebugPanel>();
    // debugPanel_->setVisible(false);
    // debugPanel_->onMetricsChanged = [this]() { rebuildChannelStrips(); };
    // addAndMakeVisible(*debugPanel_);

    // Start timer for meter animation (30fps)
    startTimer(33);
}

MixerView::~MixerView() {
    stopTimer();
    TrackManager::getInstance().removeListener(this);
    ViewModeController::getInstance().removeListener(this);

    // Explicitly clear all UI components before automatic member destruction
    // This ensures components release their LookAndFeel references before
    // mixerLookAndFeel_ is destroyed (member destruction happens in reverse order)
    for (auto& strip : channelStrips)
        strip->groupChildren_.clear();
    drumSubStrips_.clear();
    orderedStrips_.clear();
    channelStrips.clear();
    auxChannelStrips.clear();
    masterStrip.reset();
    debugPanel_.reset();
    auxContainer.reset();
    channelContainer.reset();
    channelViewport.reset();
    channelResizeHandle_.reset();
}

void MixerView::rebuildChannelStrips() {
    // Clear group children references before destroying strips
    for (auto& strip : channelStrips)
        strip->groupChildren_.clear();

    // Clear existing strips
    drumSubStrips_.clear();
    orderedStrips_.clear();
    channelStrips.clear();

    const auto& tracks = TrackManager::getInstance().getTracks();

    for (const auto& track : tracks) {
        // Only show tracks visible in the current view mode
        if (!track.isVisibleIn(currentViewMode_)) {
            continue;
        }

        if (track.type == TrackType::Aux)
            continue;  // Aux strips handled separately

        // Skip children of collapsed group tracks
        if (track.hasParent()) {
            if (auto* parent = TrackManager::getInstance().getTrack(track.parentId)) {
                if (parent->isGroup() && parent->isCollapsedIn(currentViewMode_))
                    continue;
            }
        }

        // Skip collapsed multi-out children
        if (track.type == TrackType::MultiOut && track.multiOutLink) {
            if (auto* parent =
                    TrackManager::getInstance().getTrack(track.multiOutLink->sourceTrackId)) {
                bool skipTrack = false;
                for (const auto& elem : parent->chainElements) {
                    if (isDevice(elem)) {
                        const auto& dev = getDevice(elem);
                        if (dev.id == track.multiOutLink->sourceDeviceId &&
                            dev.multiOut.isMultiOut && dev.multiOut.mixerChildrenCollapsed) {
                            skipTrack = true;
                            break;
                        }
                    }
                }
                if (skipTrack)
                    continue;
            }
        }

        auto strip = std::make_unique<ChannelStrip>(track, audioEngine_, false);
        strip->onClicked = [this](int trackId, bool isMaster) {
            // Find the index of this track in the visible strips
            for (size_t i = 0; i < channelStrips.size(); ++i) {
                if (channelStrips[i]->getTrackId() == trackId) {
                    selectChannel(static_cast<int>(i), isMaster);
                    break;
                }
            }
        };

        // Wire up send area resize callback (coalesced relayout of all strips)
        strip->onSendAreaResized = [this]() { relayoutAllStrips(); };

        // Check if this track has a DrumGridPlugin
        auto* drumGrid = findDrumGridForTrack(track, audioEngine_);
        if (drumGrid) {
            strip->drumGrid_ = drumGrid;

            // Create expand toggle button
            strip->expandToggle_ = std::make_unique<juce::TextButton>(
                drumGrid->isMixerExpanded() ? juce::String::charToString(0x25BC)    // ▼
                                            : juce::String::charToString(0x25B6));  // ▶
            strip->expandToggle_->setConnectedEdges(
                juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
            strip->expandToggle_->setColour(juce::TextButton::buttonColourId,
                                            DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
            strip->expandToggle_->setColour(juce::TextButton::textColourOffId,
                                            DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            strip->expandToggle_->onClick = [this, drumGrid]() {
                drumGrid->setMixerExpanded(!drumGrid->isMixerExpanded());
                rebuildChannelStrips();
            };
            strip->addAndMakeVisible(*strip->expandToggle_);
        }

        // Add expand/collapse toggle for group tracks with children
        if (!drumGrid && track.isGroup() && track.hasChildren()) {
            bool isCollapsed = track.isCollapsedIn(currentViewMode_);
            TrackId trackId = track.id;
            strip->expandToggle_ = std::make_unique<juce::TextButton>(
                isCollapsed ? juce::String::charToString(0x25B6)    // ▶
                            : juce::String::charToString(0x25BC));  // ▼
            strip->expandToggle_->setConnectedEdges(
                juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
            strip->expandToggle_->setColour(juce::TextButton::buttonColourId,
                                            DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
            strip->expandToggle_->setColour(juce::TextButton::textColourOffId,
                                            DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            strip->expandToggle_->onClick = [this, trackId]() {
                auto* t = TrackManager::getInstance().getTrack(trackId);
                if (t) {
                    bool collapsed = t->isCollapsedIn(currentViewMode_);
                    t->viewSettings.setCollapsed(currentViewMode_, !collapsed);
                }
                rebuildChannelStrips();
            };
            strip->addAndMakeVisible(*strip->expandToggle_);
        }

        // Check if this track has active multi-out children (and no DrumGrid toggle already)
        if (!drumGrid && !track.isGroup()) {
            bool hasActiveMultiOut = false;
            bool isCollapsed = false;
            TrackId trackId = track.id;
            DeviceId multiOutDeviceId = INVALID_DEVICE_ID;
            for (const auto& elem : track.chainElements) {
                if (isDevice(elem)) {
                    const auto& dev = getDevice(elem);
                    if (dev.multiOut.isMultiOut) {
                        for (const auto& pair : dev.multiOut.outputPairs) {
                            if (pair.active) {
                                hasActiveMultiOut = true;
                                break;
                            }
                        }
                        if (hasActiveMultiOut) {
                            multiOutDeviceId = dev.id;
                            isCollapsed = dev.multiOut.mixerChildrenCollapsed;
                            break;
                        }
                    }
                }
            }

            if (hasActiveMultiOut) {
                strip->expandToggle_ = std::make_unique<juce::TextButton>(
                    isCollapsed ? juce::String::charToString(0x25B6)    // ▶
                                : juce::String::charToString(0x25BC));  // ▼
                strip->expandToggle_->setConnectedEdges(
                    juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                    juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
                strip->expandToggle_->setColour(juce::TextButton::buttonColourId,
                                                DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
                strip->expandToggle_->setColour(juce::TextButton::textColourOffId,
                                                DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                strip->expandToggle_->onClick = [this, trackId, multiOutDeviceId]() {
                    auto* dev = TrackManager::getInstance().getDevice(trackId, multiOutDeviceId);
                    if (dev && dev->multiOut.isMultiOut) {
                        dev->multiOut.mixerChildrenCollapsed =
                            !dev->multiOut.mixerChildrenCollapsed;
                    }
                    rebuildChannelStrips();
                };
                strip->addAndMakeVisible(*strip->expandToggle_);
            }
        }

        channelStrips.push_back(std::move(strip));

        // If DrumGrid is expanded, create sub-strips for non-empty chains
        if (drumGrid && drumGrid->isMixerExpanded()) {
            for (const auto& chain : drumGrid->getChains()) {
                if (chain->plugins.empty())
                    continue;

                juce::String name = chain->name.isNotEmpty()
                                        ? chain->name
                                        : juce::String("Pad ") + juce::String(chain->index);

                auto subStrip = std::make_unique<DrumSubChannelStrip>(drumGrid, chain->index, name,
                                                                      track.colour);

                drumSubStrips_.push_back(std::move(subStrip));
            }
        }
    }

    // Second pass: build orderedStrips_ and wire up parent-child hierarchy.
    // Children of groups, multi-out parents, and DrumGrid parents all get
    // nested inside their parent strip's groupChildren_ for envelope rendering.
    // Use addChildComponent (not addAndMakeVisible) to avoid intermediate layouts.

    std::unordered_map<int, ChannelStrip*> stripByTrackId;
    for (auto& strip : channelStrips)
        stripByTrackId[strip->getTrackId()] = strip.get();

    // Map drum sub-strips to their owning track
    std::unordered_map<int, std::vector<size_t>> drumSubsByTrack;
    for (size_t i = 0; i < drumSubStrips_.size(); ++i) {
        for (auto& cs : channelStrips) {
            if (cs->drumGrid_ == drumSubStrips_[i]->getDrumGrid()) {
                drumSubsByTrack[cs->getTrackId()].push_back(i);
                break;
            }
        }
    }

    for (auto& strip : channelStrips) {
        int trackId = strip->getTrackId();
        const auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        if (!trackInfo)
            continue;

        // --- Nest inside group parent ---
        if (trackInfo->hasParent()) {
            if (auto* parentTrack = TrackManager::getInstance().getTrack(trackInfo->parentId)) {
                if (parentTrack->isGroup()) {
                    auto it = stripByTrackId.find(trackInfo->parentId);
                    if (it != stripByTrackId.end()) {
                        it->second->addChildComponent(*strip);
                        it->second->groupChildren_.push_back(strip.get());
                        continue;
                    }
                }
            }
        }

        // --- Nest multi-out children inside their source parent ---
        if (trackInfo->type == TrackType::MultiOut && trackInfo->multiOutLink) {
            auto it = stripByTrackId.find(trackInfo->multiOutLink->sourceTrackId);
            if (it != stripByTrackId.end()) {
                it->second->addChildComponent(*strip);
                it->second->groupChildren_.push_back(strip.get());
                continue;
            }
        }

        // --- Top-level strip ---
        channelContainer->addChildComponent(*strip);
        orderedStrips_.push_back(strip.get());

        // Nest drum sub-strips inside this strip's envelope
        auto dsIt = drumSubsByTrack.find(trackId);
        if (dsIt != drumSubsByTrack.end()) {
            for (size_t idx : dsIt->second) {
                strip->addChildComponent(*drumSubStrips_[idx]);
                strip->groupChildren_.push_back(drumSubStrips_[idx].get());
            }
        }
    }

    // Now make everything visible.
    // Group parent strips must not be opaque — they need to paint the envelope
    // border around/behind their children.
    for (auto* strip : orderedStrips_)
        strip->setVisible(true);
    for (auto& strip : channelStrips) {
        if (!strip->groupChildren_.empty())
            strip->setOpaque(false);
        for (auto* child : strip->groupChildren_)
            child->setVisible(true);
    }

    // Build aux channel strips separately
    auxChannelStrips.clear();
    for (const auto& track : tracks) {
        if (track.type != TrackType::Aux || !track.isVisibleIn(currentViewMode_))
            continue;
        auto strip = std::make_unique<ChannelStrip>(track, audioEngine_, false);
        strip->onClicked = [this](int trackId, bool isMaster) {
            for (size_t i = 0; i < channelStrips.size(); ++i) {
                if (channelStrips[i]->getTrackId() == trackId) {
                    selectChannel(static_cast<int>(i), isMaster);
                    return;
                }
            }
            // Check aux strips — use negative index offset for identification
            for (size_t i = 0; i < auxChannelStrips.size(); ++i) {
                if (auxChannelStrips[i]->getTrackId() == trackId) {
                    // Select via TrackManager directly
                    SelectionManager::getInstance().selectTrack(trackId);
                    return;
                }
            }
        };
        auxContainer->addAndMakeVisible(*strip);
        auxChannelStrips.push_back(std::move(strip));
    }

    // Update master strip visibility
    const auto& master = TrackManager::getInstance().getMasterChannel();
    bool masterVisible = master.isVisibleIn(currentViewMode_);
    masterStrip->setVisible(masterVisible);

    // Sync selection with TrackManager's current selection
    trackSelectionChanged(TrackManager::getInstance().getSelectedTrack());

    resized();
}

void MixerView::tracksChanged() {
    // Rebuild all channel strips when tracks are added/removed/reordered
    rebuildChannelStrips();
}

void MixerView::trackPropertyChanged(int trackId) {
    // Update the specific channel strip - find it by track ID since indices may differ
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (!track)
        return;

    for (auto& strip : channelStrips) {
        if (strip->getTrackId() == trackId) {
            strip->updateFromTrack(*track);
            return;
        }
    }

    // Check aux strips
    for (auto& strip : auxChannelStrips) {
        if (strip->getTrackId() == trackId) {
            strip->updateFromTrack(*track);
            return;
        }
    }
}

void MixerView::trackDevicesChanged(TrackId trackId) {
    // Sends are notified via trackDevicesChanged — update the strip
    trackPropertyChanged(trackId);
}

void MixerView::viewModeChanged(ViewMode mode, const AudioEngineProfile& /*profile*/) {
    currentViewMode_ = mode;
    rebuildChannelStrips();
}

void MixerView::masterChannelChanged() {
    // Update master strip visibility
    const auto& master = TrackManager::getInstance().getMasterChannel();
    bool masterVisible = master.isVisibleIn(currentViewMode_);
    masterStrip->setVisible(masterVisible);
    resized();
}

void MixerView::paint(juce::Graphics& g) {
    MAGDA_MONITOR_SCOPE("UIFrame");
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

    // Left border (visible when side panel is collapsed)
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
    g.fillRect(0, 0, 1, getHeight());
}

void MixerView::resized() {
    const auto& metrics = MixerMetrics::getInstance();
    auto bounds = getLocalBounds();

    // Master strip on the right (only if visible)
    if (masterStrip->isVisible()) {
        masterStrip->setBounds(bounds.removeFromRight(metrics.channelWidth));
    }

    // Aux channel strips between regular channels and master
    int numAux = static_cast<int>(auxChannelStrips.size());
    int auxWidth = numAux * metrics.channelWidth;
    if (auxWidth > 0) {
        auto auxArea = bounds.removeFromRight(auxWidth);
        auxContainer->setBounds(auxArea);
        for (int i = 0; i < numAux; ++i) {
            auxChannelStrips[i]->setBounds(i * metrics.channelWidth, 0, metrics.channelWidth,
                                           auxArea.getHeight());
        }
    } else {
        auxContainer->setBounds(0, 0, 0, 0);
    }

    // 1px left border padding (visible when side panel is collapsed)
    bounds.removeFromLeft(1);

    // Channel viewport takes remaining space
    channelViewport->setBounds(bounds);

    // If orderedStrips_ hasn't been populated yet, fall back to channelStrips
    if (orderedStrips_.empty() && !channelStrips.empty()) {
        for (auto& s : channelStrips)
            orderedStrips_.push_back(s.get());
    }

    // Size the channel container — group strips may be wider than channelWidth
    int containerHeight = bounds.getHeight();
    int containerWidth = 0;
    for (auto* strip : orderedStrips_) {
        int stripWidth = metrics.channelWidth;
        if (auto* cs = dynamic_cast<ChannelStrip*>(strip)) {
            if (!cs->groupChildren_.empty())
                stripWidth =
                    (1 + static_cast<int>(cs->groupChildren_.size())) * metrics.channelWidth;
        }
        containerWidth += stripWidth;
    }
    channelContainer->setSize(containerWidth, containerHeight);

    // Position all strips with cumulative x (group strips span multiple columns)
    int xPos = 0;
    for (auto* strip : orderedStrips_) {
        int stripWidth = metrics.channelWidth;
        if (auto* cs = dynamic_cast<ChannelStrip*>(strip)) {
            if (!cs->groupChildren_.empty())
                stripWidth =
                    (1 + static_cast<int>(cs->groupChildren_.size())) * metrics.channelWidth;
        }
        strip->setBounds(xPos, 0, stripWidth, containerHeight);
        xPos += stripWidth;
    }

    // Resize handle centered on right border of last channel strip
    if (!orderedStrips_.empty() && channelResizeHandle_) {
        const int handleWidth = 8;
        int handleX = containerWidth - handleWidth / 2;
        channelResizeHandle_->setBounds(handleX, 0, handleWidth, containerHeight);
        channelResizeHandle_->toFront(false);
    }
}

void MixerView::updateStripWidths() {
    const auto& metrics = MixerMetrics::getInstance();
    int containerHeight = channelContainer->getHeight();

    // Compute total container width with variable-width group strips
    int containerWidth = 0;
    for (auto* strip : orderedStrips_) {
        int stripWidth = metrics.channelWidth;
        if (auto* cs = dynamic_cast<ChannelStrip*>(strip)) {
            if (!cs->groupChildren_.empty())
                stripWidth =
                    (1 + static_cast<int>(cs->groupChildren_.size())) * metrics.channelWidth;
        }
        containerWidth += stripWidth;
    }
    channelContainer->setSize(containerWidth, containerHeight);

    // Position strips with cumulative x
    int xPos = 0;
    for (auto* strip : orderedStrips_) {
        int stripWidth = metrics.channelWidth;
        if (auto* cs = dynamic_cast<ChannelStrip*>(strip)) {
            if (!cs->groupChildren_.empty())
                stripWidth =
                    (1 + static_cast<int>(cs->groupChildren_.size())) * metrics.channelWidth;
        }
        strip->setBounds(xPos, 0, stripWidth, containerHeight);
        xPos += stripWidth;
    }

    // Update resize handle position
    if (!orderedStrips_.empty() && channelResizeHandle_) {
        const int handleWidth = 8;
        int handleX = containerWidth - handleWidth / 2;
        channelResizeHandle_->setBounds(handleX, 0, handleWidth, containerHeight);
        channelResizeHandle_->toFront(false);
    }

    // Update aux strips
    int numAux = static_cast<int>(auxChannelStrips.size());
    int auxWidth = numAux * metrics.channelWidth;
    if (auxWidth > 0) {
        for (int i = 0; i < numAux; ++i) {
            auxChannelStrips[i]->setBounds(i * metrics.channelWidth, 0, metrics.channelWidth,
                                           auxContainer->getHeight());
        }
    }
}

void MixerView::relayoutAllStrips() {
    if (!pendingSendResizeUpdate_) {
        pendingSendResizeUpdate_ = true;
        juce::Component::SafePointer<MixerView> safeThis(this);
        juce::MessageManager::callAsync([safeThis]() {
            if (auto* self = safeThis.getComponent()) {
                if (self->pendingSendResizeUpdate_) {
                    self->pendingSendResizeUpdate_ = false;
                    for (auto& strip : self->channelStrips) {
                        strip->resized();
                        strip->repaint();
                    }
                    for (auto& strip : self->auxChannelStrips) {
                        strip->resized();
                        strip->repaint();
                    }
                    if (self->masterStrip) {
                        self->masterStrip->resized();
                        self->masterStrip->repaint();
                    }
                }
            }
        });
    }
}

void MixerView::timerCallback() {
    // Skip meter updates during resize drag to avoid repaints
    if (isResizeDragging_)
        return;

    // Read metering data from AudioBridge
    if (!audioEngine_)
        return;

    auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(audioEngine_);
    if (!teWrapper)
        return;

    auto* bridge = teWrapper->getAudioBridge();
    if (!bridge)
        return;

    auto& meteringBuffer = bridge->getMeteringBuffer();

    // Update channel strip meters
    for (auto& strip : channelStrips) {
        int trackId = strip->getTrackId();
        MeterData data;
        if (meteringBuffer.popLevels(trackId, data)) {
            strip->setMeterLevels(data.peakL, data.peakR);
        }
    }

    // Update aux channel strip meters
    for (auto& strip : auxChannelStrips) {
        int trackId = strip->getTrackId();
        MeterData data;
        if (meteringBuffer.popLevels(trackId, data)) {
            strip->setMeterLevels(data.peakL, data.peakR);
        }
    }

    // Update drum sub-channel strip meters and sync controls (two-way sync)
    for (auto& strip : drumSubStrips_) {
        auto [peakL, peakR] = strip->getDrumGrid()->consumeChainPeak(strip->getChainIndex());
        strip->setMeterLevels(peakL, peakR);
        strip->updateFromChain();
    }

    // Update master strip meters
    if (masterStrip) {
        float masterPeakL = bridge->getMasterPeakL();
        float masterPeakR = bridge->getMasterPeakR();
        masterStrip->setPeakLevels(masterPeakL, masterPeakR);
    }
}

bool MixerView::keyPressed(const juce::KeyPress& /*key*/) {
    // Debug panel disabled
    return false;
}

bool MixerView::isInChannelResizeZone(const juce::Point<int>& /*pos*/) const {
    // Not used anymore - resize handle component handles this
    return false;
}

void MixerView::mouseMove(const juce::MouseEvent& /*event*/) {
    // Resize handled by ChannelResizeHandle
}

void MixerView::mouseDown(const juce::MouseEvent& /*event*/) {
    // Resize handled by ChannelResizeHandle
}

void MixerView::mouseDrag(const juce::MouseEvent& /*event*/) {
    // Resize handled by ChannelResizeHandle
}

void MixerView::mouseUp(const juce::MouseEvent& /*event*/) {
    // Resize handled by ChannelResizeHandle
}

// ChannelResizeHandle implementation
MixerView::ChannelResizeHandle::ChannelResizeHandle() = default;

void MixerView::ChannelResizeHandle::paint(juce::Graphics& /*g*/) {
    // Invisible — cursor change on hover is the only affordance
}

void MixerView::ChannelResizeHandle::mouseEnter(const juce::MouseEvent& /*event*/) {
    isHovering_ = true;
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    repaint();
}

void MixerView::ChannelResizeHandle::mouseExit(const juce::MouseEvent& /*event*/) {
    isHovering_ = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void MixerView::ChannelResizeHandle::mouseDown(const juce::MouseEvent& event) {
    isDragging_ = true;
    hasConfirmedHorizontalDrag_ = false;
    dragStartX_ = event.getScreenX();
}

void MixerView::ChannelResizeHandle::mouseDrag(const juce::MouseEvent& event) {
    if (!isDragging_ || !onResize)
        return;

    if (!hasConfirmedHorizontalDrag_) {
        int dx = std::abs(event.getDistanceFromDragStartX());
        int dy = std::abs(event.getDistanceFromDragStartY());
        if (dx < 4 && dy < 4)
            return;
        if (dy > dx) {
            isDragging_ = false;
            return;
        }
        hasConfirmedHorizontalDrag_ = true;
    }

    int deltaX = event.getScreenX() - dragStartX_;
    onResize(deltaX);
    dragStartX_ = event.getScreenX();
}

void MixerView::ChannelResizeHandle::mouseUp(const juce::MouseEvent& /*event*/) {
    isDragging_ = false;
    if (onResizeEnd)
        onResizeEnd();
    repaint();
}

void MixerView::selectChannel(int index, bool isMaster) {
    // Deselect all channel strips (including aux)
    for (auto& strip : channelStrips) {
        strip->setSelected(false);
    }
    for (auto& strip : auxChannelStrips) {
        strip->setSelected(false);
    }

    // Select the clicked channel
    if (isMaster) {
        selectedChannelIndex = -1;
        selectedIsMaster = true;
        SelectionManager::getInstance().selectTrack(MASTER_TRACK_ID);
    } else {
        if (index >= 0 && index < static_cast<int>(channelStrips.size())) {
            channelStrips[index]->setSelected(true);
            // Notify SelectionManager of selection (which syncs with TrackManager)
            SelectionManager::getInstance().selectTrack(channelStrips[index]->getTrackId());
        }
        selectedChannelIndex = index;
        selectedIsMaster = false;
    }

    // Notify listener
    if (onChannelSelected) {
        onChannelSelected(selectedChannelIndex, selectedIsMaster);
    }
}

void MixerView::trackSelectionChanged(TrackId trackId) {
    // Sync our visual selection with TrackManager's selection
    // Deselect all first
    for (auto& strip : channelStrips) {
        strip->setSelected(false);
    }
    for (auto& strip : auxChannelStrips) {
        strip->setSelected(false);
    }
    selectedIsMaster = false;
    selectedChannelIndex = -1;

    if (trackId == INVALID_TRACK_ID) {
        return;
    }

    // Handle master track selection
    if (trackId == MASTER_TRACK_ID) {
        selectedIsMaster = true;
        selectedChannelIndex = -1;
        if (onChannelSelected) {
            onChannelSelected(selectedChannelIndex, selectedIsMaster);
        }
        return;
    }

    // Find and select the matching channel strip
    for (size_t i = 0; i < channelStrips.size(); ++i) {
        if (channelStrips[i]->getTrackId() == trackId) {
            channelStrips[i]->setSelected(true);
            selectedChannelIndex = static_cast<int>(i);
            return;
        }
    }

    // Check aux strips
    for (size_t i = 0; i < auxChannelStrips.size(); ++i) {
        if (auxChannelStrips[i]->getTrackId() == trackId) {
            auxChannelStrips[i]->setSelected(true);
            return;
        }
    }
}

}  // namespace magda
