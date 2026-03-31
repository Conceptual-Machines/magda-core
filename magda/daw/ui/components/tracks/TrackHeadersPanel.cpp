#include "TrackHeadersPanel.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

#include "../../../audio/AudioBridge.hpp"
#include "../../../audio/MidiBridge.hpp"
#include "../../../core/Config.hpp"
#include "../../../core/DeviceInfo.hpp"
#include "../../../core/SelectionManager.hpp"
#include "../../../core/TrackCommands.hpp"
#include "../../../core/TrackPropertyCommands.hpp"
#include "../../../core/UndoManager.hpp"
#include "../../../engine/TracktionEngineWrapper.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/SmallButtonLookAndFeel.hpp"
#include "../automation/AutomationLaneComponent.hpp"
#include "../mixer/RoutingSyncHelper.hpp"
#include "BinaryData.h"

namespace magda {

// dB conversion helpers for volume
namespace {
constexpr float MIN_DB = -60.0f;
constexpr float MAX_DB = 6.0f;  // Allow +6 dB headroom

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

// Meter-specific scaling: simple logarithmic curve
// Single power curve compresses bottom, leaves room at top for headroom
float dbToMeterPos(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    if (db >= MAX_DB)
        return 1.0f;

    // Normalize to 0-1 range
    float normalized = (db - MIN_DB) / (MAX_DB - MIN_DB);

    // Apply power curve: y = x^3
    return std::pow(normalized, 3.0f);
}

// Simple stereo level meter component for track headers
// Uses smooth ballistics (fast attack, exponential decay) and peak hold.
class TrackMeter : public juce::Component, private juce::Timer {
  public:
    TrackMeter() = default;
    ~TrackMeter() override {
        stopTimer();
    }

    void setLevels(float left, float right) {
        targetL_ = juce::jlimit(0.0f, 2.0f, left);
        targetR_ = juce::jlimit(0.0f, 2.0f, right);

        // Update peak hold
        float leftDb = gainToDb(targetL_);
        float rightDb = gainToDb(targetR_);
        if (leftDb > peakLeftDb_) {
            peakLeftDb_ = leftDb;
            peakLeftHold_ = PEAK_HOLD_MS;
        }
        if (rightDb > peakRightDb_) {
            peakRightDb_ = rightDb;
            peakRightHold_ = PEAK_HOLD_MS;
        }

        if (!isTimerRunning())
            startTimerHz(60);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        const float gap = 2.0f;

        // Split into L/R bars with gap
        float barWidth = (bounds.getWidth() - gap) / 2.0f;
        auto leftBar = bounds.withWidth(barWidth);
        auto rightBar = bounds.withWidth(barWidth).withX(bounds.getX() + barWidth + gap);

        // Background for each bar (darker background)
        g.setColour(juce::Colour(0xFF1A1A1A));
        g.fillRoundedRectangle(leftBar, 2.0f);
        g.fillRoundedRectangle(rightBar, 2.0f);

        // Draw border so meters are always visible
        g.setColour(juce::Colour(0xFF404040));
        g.drawRoundedRectangle(leftBar, 2.0f, 1.0f);
        g.drawRoundedRectangle(rightBar, 2.0f, 1.0f);

        // Draw level fills
        drawMeterBar(g, leftBar, displayL_, peakLeftDb_);
        drawMeterBar(g, rightBar, displayR_, peakRightDb_);

        // 0dB tick mark (aligned with header separator)
        if (nameRowY_ >= 0) {
            float localY = static_cast<float>(nameRowY_ - getY());
            if (localY > 0 && localY < bounds.getBottom()) {
                g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
                g.drawHorizontalLine(static_cast<int>(localY), bounds.getX(), bounds.getRight());
            }
        }
    }

    void setNameRowY(int y) {
        nameRowY_ = y;
    }

  private:
    float targetL_ = 0.0f, targetR_ = 0.0f;
    float displayL_ = 0.0f, displayR_ = 0.0f;
    float peakLeftDb_ = -60.0f, peakRightDb_ = -60.0f;
    float peakLeftHold_ = 0.0f, peakRightHold_ = 0.0f;
    int nameRowY_ = -1;

    static constexpr float ATTACK_COEFF = 0.9f;
    static constexpr float RELEASE_COEFF = 0.05f;
    static constexpr float PEAK_HOLD_MS = 1500.0f;
    static constexpr float PEAK_DECAY_DB_PER_FRAME = 0.8f;

    void timerCallback() override {
        bool changed = false;
        changed |= updateLevel(displayL_, targetL_);
        changed |= updateLevel(displayR_, targetR_);
        changed |= updatePeak(peakLeftDb_, peakLeftHold_, gainToDb(targetL_));
        changed |= updatePeak(peakRightDb_, peakRightHold_, gainToDb(targetR_));
        if (changed)
            repaint();
        else if (displayL_ < 0.001f && displayR_ < 0.001f && peakLeftDb_ <= -60.0f &&
                 peakRightDb_ <= -60.0f)
            stopTimer();
    }

    static bool updateLevel(float& display, float target) {
        float prev = display;
        display += (target - display) * (target > display ? ATTACK_COEFF : RELEASE_COEFF);
        if (display < 0.001f)
            display = 0.0f;
        return std::abs(display - prev) > 0.0001f;
    }

    static bool updatePeak(float& peakDb, float& holdTime, float currentDb) {
        float prev = peakDb;
        if (currentDb > peakDb) {
            peakDb = currentDb;
            holdTime = PEAK_HOLD_MS;
        } else if (holdTime > 0.0f) {
            holdTime -= 1000.0f / 60.0f;
        } else {
            peakDb -= PEAK_DECAY_DB_PER_FRAME;
            if (peakDb < -60.0f)
                peakDb = -60.0f;
        }
        return std::abs(peakDb - prev) > 0.01f;
    }

    void drawMeterBar(juce::Graphics& g, juce::Rectangle<float> bounds, float level, float peakDb) {
        if (level > 0.0f) {
            float meterPos = dbToMeterPos(gainToDb(level));
            float meterHeight = bounds.getHeight() * meterPos;
            auto fillBounds = bounds;
            auto fullBounds = bounds;
            fillBounds = fillBounds.removeFromBottom(meterHeight);

            const juce::Colour green(0xFF55AA55);
            const juce::Colour yellow(0xFFAAAA55);
            const juce::Colour red(0xFFAA5555);

            float yellowPos = dbToMeterPos(-12.0f);
            float redPos = dbToMeterPos(0.0f);
            constexpr float fade = 0.03f;

            juce::ColourGradient grad(green, 0.0f, fullBounds.getBottom(), red, 0.0f,
                                      fullBounds.getY(), false);
            grad.addColour(std::max(0.0, (double)yellowPos - fade), green);
            grad.addColour(std::min(1.0, (double)yellowPos + fade), yellow);
            grad.addColour(std::max(0.0, (double)redPos - fade), yellow);
            grad.addColour(std::min(1.0, (double)redPos + fade), red);

            g.setGradientFill(grad);
            g.fillRect(fillBounds);
        }

        // Peak hold indicator
        float peakPos = dbToMeterPos(peakDb);
        if (peakPos > 0.01f) {
            float peakY = bounds.getBottom() - bounds.getHeight() * peakPos;
            auto peakColour = peakDb >= 0.0f     ? juce::Colour(0xFFAA5555)
                              : peakDb >= -12.0f ? juce::Colour(0xFFAAAA55)
                                                 : juce::Colour(0xFF55AA55);
            g.setColour(peakColour.withAlpha(0.9f));
            g.fillRect(bounds.getX(), peakY, bounds.getWidth(), 1.5f);
        }
    }
};

// MIDI activity indicator - small blinking dot
class MidiActivityIndicator : public juce::Component {
  public:
    MidiActivityIndicator() = default;

    void setActivity(float level) {
        activity_ = juce::jlimit(0.0f, 1.0f, level);
        repaint();
    }

    void trigger() {
        activity_ = 1.0f;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();

        // Calculate dot size and position (centered horizontally, at top)
        float dotSize = std::min(bounds.getWidth(), 8.0f);
        float dotX = bounds.getCentreX() - dotSize / 2.0f;
        float dotY = bounds.getY() + 2.0f;  // Small padding from top
        auto dotBounds = juce::Rectangle<float>(dotX, dotY, dotSize, dotSize);

        // Inactive state: visible cyan dot (dimmed)
        g.setColour(juce::Colour(0xFF00AACC).withAlpha(0.4f));
        g.fillEllipse(dotBounds);

        // Active state: bright cyan glow
        if (activity_ > 0.01f) {
            auto activeColor = juce::Colour(0xFF00FFFF).withAlpha(activity_);
            g.setColour(activeColor);
            g.fillEllipse(dotBounds);
        }
    }

  private:
    float activity_ = 0.0f;
};

// Session mode indicator button — shows resume icon, orange when track is in session mode
class SessionModeButton : public juce::Component {
  public:
    SessionModeButton() {
        resumeOffDrawable_ =
            juce::Drawable::createFromImageData(BinaryData::resume_svg, BinaryData::resume_svgSize);
        resumeOnDrawable_ = juce::Drawable::createFromImageData(BinaryData::resume_on_svg,
                                                                BinaryData::resume_on_svgSize);
    }

    void setSessionMode(bool inSession) {
        if (inSession_ != inSession) {
            inSession_ = inSession;
            repaint();
        }
    }

    bool isInSessionMode() const {
        return inSession_;
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        float iconSize = std::min(bounds.getWidth(), std::min(bounds.getHeight(), 12.0f));
        auto iconArea = bounds.withSizeKeepingCentre(iconSize, iconSize);

        auto& drawable = inSession_ ? resumeOnDrawable_ : resumeOffDrawable_;
        if (drawable) {
            drawable->drawWithin(g, iconArea, juce::RectanglePlacement::centred,
                                 inSession_ ? 1.0f : 0.25f);
        }
    }

    std::function<void()> onClick;

    void mouseDown(const juce::MouseEvent&) override {
        if (onClick)
            onClick();
    }

  private:
    bool inSession_ = false;
    std::unique_ptr<juce::Drawable> resumeOffDrawable_;
    std::unique_ptr<juce::Drawable> resumeOnDrawable_;
};
}  // namespace

TrackHeadersPanel::TrackHeader::TrackHeader(const juce::String& trackName) : name(trackName) {
    // Create UI components
    nameLabel = std::make_unique<juce::Label>("trackName", trackName);
    nameLabel->setEditable(true);
    nameLabel->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    nameLabel->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    nameLabel->setFont(FontManager::getInstance().getUIFont(12.0f));

    muteButton = std::make_unique<juce::TextButton>("M");
    muteButton->setLookAndFeel(&magda::daw::ui::SmallButtonLookAndFeel::getInstance());
    muteButton->setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    muteButton->setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::STATUS_WARNING));
    muteButton->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    muteButton->setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    muteButton->setClickingTogglesState(true);

    soloButton = std::make_unique<juce::TextButton>("S");
    soloButton->setLookAndFeel(&magda::daw::ui::SmallButtonLookAndFeel::getInstance());
    soloButton->setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    soloButton->setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    soloButton->setColour(juce::TextButton::textColourOffId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    soloButton->setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    soloButton->setClickingTogglesState(true);

    recordButton = std::make_unique<juce::TextButton>("R");
    recordButton->setLookAndFeel(&magda::daw::ui::SmallButtonLookAndFeel::getInstance());
    recordButton->setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    recordButton->setColour(juce::TextButton::buttonOnColourId,
                            DarkTheme::getColour(DarkTheme::STATUS_ERROR));  // Red when armed
    recordButton->setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    recordButton->setColour(juce::TextButton::textColourOnId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    recordButton->setClickingTogglesState(true);

    // Monitor button (3-state: Off → In → Auto → Off)
    monitorButton = std::make_unique<juce::TextButton>("-");
    monitorButton->setLookAndFeel(&magda::daw::ui::SmallButtonLookAndFeel::getInstance());
    monitorButton->setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    monitorButton->setColour(juce::TextButton::buttonOnColourId,
                             DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
    monitorButton->setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    monitorButton->setColour(juce::TextButton::textColourOnId,
                             DarkTheme::getColour(DarkTheme::BACKGROUND));
    monitorButton->setTooltip("Input monitoring (Off/In/Auto)");

    // Automation button (bezier curve icon)
    automationButton = std::make_unique<SvgButton>("Automation", BinaryData::bezier_svg,
                                                   BinaryData::bezier_svgSize);
    automationButton->setTooltip("Automation (coming soon)");
    automationButton->setEnabled(false);
    automationButton->setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
    automationButton->setColour(juce::TextButton::buttonOnColourId,
                                DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    automationButton->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    automationButton->setNormalBackgroundColor(DarkTheme::getColour(DarkTheme::SURFACE));

    // Volume label (shows dB, draggable)
    volumeLabel = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Decibels);
    volumeLabel->setRange(MIN_DB, MAX_DB, 0.0);  // -60 to +6 dB, default 0 dB
    volumeLabel->setValue(gainToDb(volume), juce::dontSendNotification);

    // Pan label (shows L/C/R, draggable)
    panLabel = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Pan);
    panLabel->setRange(-1.0, 1.0, 0.0);  // -1 (L) to +1 (R), default center
    panLabel->setValue(pan, juce::dontSendNotification);

    // Collapse button for groups (chevron indicator)
    collapseButton =
        std::make_unique<juce::DrawableButton>("Collapse", juce::DrawableButton::ImageFitted);
    collapseButton->setColour(juce::DrawableButton::backgroundColourId,
                              juce::Colours::transparentBlack);
    collapseButton->setColour(juce::DrawableButton::backgroundOnColourId,
                              juce::Colours::transparentBlack);
    collapseButton->setEdgeIndent(1);

    // Input type selector (hidden, kept for internal state)
    inputTypeSelector = std::make_unique<InputTypeSelector>();

    // Audio input selector
    audioInputSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::AudioIn);
    audioInputSelector->setSelectedId(1);
    audioInputSelector->setEnabled(false);  // Disabled by default (MIDI input active)

    // MIDI input selector
    inputSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::MidiIn);
    inputSelector->setSelectedId(1);
    inputSelector->setEnabled(midiInEnabled);

    // Output selector (audio output, always master)
    outputSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::AudioOut);
    outputSelector->setSelectedId(1);
    outputSelector->setEnabled(audioOutEnabled);

    // MIDI output selector
    midiOutputSelector = std::make_unique<RoutingSelector>(RoutingSelector::Type::MidiOut);
    midiOutputSelector->setSelectedId(1);   // "None"
    midiOutputSelector->setEnabled(false);  // Disabled by default

    // Send labels — created dynamically in setupTrackHeaderWithId based on actual sends

    // Meter component (stereo level display)
    meterComponent = std::make_unique<TrackMeter>();
    // Levels will be set by timerCallback reading from AudioBridge

    // MIDI activity indicator
    midiIndicator = std::make_unique<MidiActivityIndicator>();
    midiIndicator->setAlwaysOnTop(true);  // Ensure always visible on top

    // Session mode button (back to arrangement)
    sessionModeButton = std::make_unique<SessionModeButton>();

    // Column header labels for routing selectors
    audioColumnLabel = std::make_unique<juce::Label>("audioCol", "Audio");
    audioColumnLabel->setFont(FontManager::getInstance().getUIFont(8.0f));
    audioColumnLabel->setColour(juce::Label::textColourId,
                                DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    audioColumnLabel->setJustificationType(juce::Justification::centred);

    midiColumnLabel = std::make_unique<juce::Label>("midiCol", "MIDI");
    midiColumnLabel->setFont(FontManager::getInstance().getUIFont(8.0f));
    midiColumnLabel->setColour(juce::Label::textColourId,
                               DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    midiColumnLabel->setJustificationType(juce::Justification::centred);

    // I/O routing icons (non-interactive visual indicators)
    auto inputDrawable =
        std::make_unique<juce::DrawableButton>("inputIcon", juce::DrawableButton::ImageFitted);
    if (auto svg =
            juce::Drawable::createFromImageData(BinaryData::Input_svg, BinaryData::Input_svgSize)) {
        inputDrawable->setImages(svg.get());
    }
    inputDrawable->setInterceptsMouseClicks(false, false);
    inputIcon = std::move(inputDrawable);

    auto outputDrawable =
        std::make_unique<juce::DrawableButton>("outputIcon", juce::DrawableButton::ImageFitted);
    if (auto svg = juce::Drawable::createFromImageData(BinaryData::Output_svg,
                                                       BinaryData::Output_svgSize)) {
        outputDrawable->setImages(svg.get());
    }
    outputDrawable->setInterceptsMouseClicks(false, false);
    outputIcon = std::move(outputDrawable);
}

TrackHeadersPanel::TrackHeadersPanel(AudioEngine* audioEngine) : audioEngine_(audioEngine) {
    DBG("TrackHeadersPanel created with audioEngine=" << (audioEngine ? "valid" : "NULL"));
    setSize(TRACK_HEADER_WIDTH, 400);
    setWantsKeyboardFocus(true);
    addMouseListener(this, true);  // Receive child clicks for track selection

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Register as ViewModeController listener
    ViewModeController::getInstance().addListener(this);
    currentViewMode_ = ViewModeController::getInstance().getViewMode();

    // Register as SelectionManager listener
    SelectionManager::getInstance().addListener(this);

    // Register as AutomationManager listener
    AutomationManager::getInstance().addListener(this);

    // Set up MIDI activity monitoring
    // MIDI activity is handled via the lock-free MidiActivityMonitor
    // (triggerMidiActivity → getMidiActivityCounter in timerCallback).
    // No onNoteEvent callback needed here.

    // Build tracks from TrackManager
    tracksChanged();

    // Start timer for metering updates (30 FPS)
    startTimerHz(30);

    // Refresh MIDI selectors immediately (Tracktion Engine loads devices async)
    refreshInputSelectors();
}

TrackHeadersPanel::~TrackHeadersPanel() {
    stopTimer();
    TrackManager::getInstance().removeListener(this);
    SelectionManager::getInstance().removeListener(this);
    ViewModeController::getInstance().removeListener(this);
    AutomationManager::getInstance().removeListener(this);
}

void TrackHeadersPanel::timerCallback() {
    // Get metering data from AudioBridge (30 FPS timer)
    if (!audioEngine_)
        return;

    auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(audioEngine_);
    if (!teWrapper)
        return;

    auto* bridge = teWrapper->getAudioBridge();
    if (!bridge)
        return;

    auto& meteringBuffer = bridge->getMeteringBuffer();

    // Decay rate for MIDI activity (fade out over time)
    const float midiDecayRate = 0.7f;  // Per frame decay (~100ms to near-zero at 30fps)

    // Check for MIDI device changes every 60 frames (2 seconds at 30 FPS)
    static int midiDeviceCheckCounter = 0;
    if (++midiDeviceCheckCounter >= 60) {
        midiDeviceCheckCounter = 0;

        // Check if MIDI device count has changed
        auto* midiBridge = audioEngine_->getMidiBridge();
        if (midiBridge) {
            auto midiInputs = midiBridge->getAvailableMidiInputs();
            static size_t lastMidiDeviceCount = 0;
            if (midiInputs.size() != lastMidiDeviceCount) {
                lastMidiDeviceCount = midiInputs.size();
                refreshInputSelectors();
            }
        }
    }

    // Update meters and MIDI activity for all visible tracks
    for (auto& header : trackHeaders) {
        // Update audio meters
        MeterData data;
        if (meteringBuffer.popLevels(header->trackId, data)) {
            if (header->meterComponent) {
                static_cast<TrackMeter*>(header->meterComponent.get())
                    ->setLevels(data.peakL, data.peakR);
            }
        }

        // Check for new MIDI note-on (counter comparison), gated by monitor mode
        auto counter = bridge->getMidiActivityCounter(header->trackId);
        if (counter != header->lastMidiCounter) {
            header->lastMidiCounter = counter;

            // Only show activity when monitoring is active AND the track
            // is actually receiving MIDI (selected or record-armed)
            bool showActivity = false;
            if (auto* trackInfo = TrackManager::getInstance().getTrack(header->trackId)) {
                bool receivingMidi =
                    trackInfo->recordArmed ||
                    SelectionManager::getInstance().getSelectedTrack() == header->trackId;
                if (receivingMidi) {
                    switch (trackInfo->inputMonitor) {
                        case InputMonitorMode::In:
                            showActivity = true;
                            break;
                        case InputMonitorMode::Auto:
                            showActivity = !bridge->isTransportPlaying();
                            break;
                        case InputMonitorMode::Off:
                            showActivity = false;
                            break;
                    }
                }
            }
            if (showActivity) {
                header->midiActivity = 1.0f;
                header->midiHoldFrames = 4;  // Hold bright for ~130ms at 30fps
            }
        }

        // Decay only after hold expires
        if (header->midiHoldFrames > 0) {
            header->midiHoldFrames--;
        } else if (header->midiActivity > 0.01f) {
            header->midiActivity *= midiDecayRate;
        }

        // Update indicator
        if (header->midiIndicator) {
            static_cast<MidiActivityIndicator*>(header->midiIndicator.get())
                ->setActivity(header->midiActivity);
        }
    }
}

void TrackHeadersPanel::viewModeChanged(ViewMode mode, const AudioEngineProfile& /*profile*/) {
    currentViewMode_ = mode;
    tracksChanged();  // Rebuild with new visibility settings
}

void TrackHeadersPanel::populateAudioInputOptions(RoutingSelector* selector, TrackId trackId) {
    if (!selector || !audioEngine_)
        return;
    auto* deviceManager = audioEngine_->getDeviceManager();
    if (!deviceManager)
        return;
    // If no trackId provided, find it from the existing trackHeaders
    if (trackId == INVALID_TRACK_ID) {
        for (const auto& h : trackHeaders) {
            if (h->audioInputSelector.get() == selector) {
                trackId = h->trackId;
                break;
            }
        }
    }
    juce::BigInteger enabledInputChannels;
    std::map<int, juce::String> teInputDeviceNames;
    if (auto* bridge = audioEngine_->getAudioBridge()) {
        enabledInputChannels = bridge->getEnabledInputChannels();
        teInputDeviceNames = bridge->getInputDeviceNamesByChannel();
    }
    RoutingSyncHelper::populateAudioInputOptions(selector, deviceManager->getCurrentAudioDevice(),
                                                 trackId, &inputTrackMapping_, enabledInputChannels,
                                                 &inputChannelMapping_, teInputDeviceNames);
}

void TrackHeadersPanel::populateAudioOutputOptions(RoutingSelector* selector,
                                                   TrackId currentTrackId) {
    if (!selector || !audioEngine_)
        return;
    auto* deviceManager = audioEngine_->getDeviceManager();
    if (!deviceManager)
        return;
    juce::BigInteger enabledOutputChannels;
    if (auto* bridge = audioEngine_->getAudioBridge())
        enabledOutputChannels = bridge->getEnabledOutputChannels();
    RoutingSyncHelper::populateAudioOutputOptions(selector, currentTrackId,
                                                  deviceManager->getCurrentAudioDevice(),
                                                  outputTrackMapping_, enabledOutputChannels);
}

void TrackHeadersPanel::populateMidiInputOptions(RoutingSelector* selector) {
    if (!selector || !audioEngine_)
        return;
    RoutingSyncHelper::populateMidiInputOptions(selector, audioEngine_->getMidiBridge());
}

void TrackHeadersPanel::populateMidiOutputOptions(RoutingSelector* selector, TrackId trackId) {
    if (!selector || !audioEngine_)
        return;
    juce::ignoreUnused(trackId);
    RoutingSyncHelper::populateMidiOutputOptions(selector, audioEngine_->getMidiBridge(),
                                                 midiOutputTrackMapping_);
}

void TrackHeadersPanel::refreshInputSelectors() {
    for (auto& header : trackHeaders) {
        if (header->audioInputSelector)
            populateAudioInputOptions(header->audioInputSelector.get());
        if (header->inputSelector)
            populateMidiInputOptions(header->inputSelector.get());
    }

    repaint();
}

void TrackHeadersPanel::setupRoutingCallbacks(TrackHeader& header, TrackId trackId) {
    if (!audioEngine_)
        return;

    auto* midiBridge = audioEngine_->getMidiBridge();

    // Audio input selector callbacks (mutually exclusive with MIDI input)
    header.audioInputSelector->onEnabledChanged = [this, trackId](bool enabled) {
        if (enabled) {
            // Disable MIDI input (mutually exclusive) — find header by trackId
            for (auto& h : trackHeaders) {
                if (h->trackId == trackId) {
                    h->inputSelector->setEnabled(false);
                    break;
                }
            }
            TrackManager::getInstance().setTrackMidiInput(trackId, "");
            // Preserve existing track input if already set, otherwise default
            auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
            if (trackInfo && trackInfo->audioInputDevice.startsWith("track:"))
                TrackManager::getInstance().setTrackAudioInput(trackId,
                                                               trackInfo->audioInputDevice);
            else
                TrackManager::getInstance().setTrackAudioInput(trackId, "default");
        } else {
            TrackManager::getInstance().setTrackAudioInput(trackId, "");
        }
    };

    header.audioInputSelector->onSelectionChanged = [this, trackId](int selectedId) {
        if (selectedId == 1) {
            TrackManager::getInstance().setTrackAudioInput(trackId, "");
        } else if (selectedId >= 200) {
            // Track-as-input (resampling)
            auto it = inputTrackMapping_.find(selectedId);
            if (it != inputTrackMapping_.end()) {
                TrackManager::getInstance().setTrackAudioInput(trackId,
                                                               "track:" + juce::String(it->second));
            }
        } else if (selectedId >= 10) {
            // Map to specific TE wave device name
            auto it = inputChannelMapping_.find(selectedId);
            if (it != inputChannelMapping_.end())
                TrackManager::getInstance().setTrackAudioInput(trackId, it->second);
            else
                TrackManager::getInstance().setTrackAudioInput(trackId, "default");
        }
    };

    // MIDI input selector callbacks (mutually exclusive with audio input)
    header.inputSelector->onEnabledChanged = [this, trackId, midiBridge](bool enabled) {
        if (enabled) {
            // Disable audio input (mutually exclusive) — find header by trackId
            int selectedId = 1;
            for (auto& h : trackHeaders) {
                if (h->trackId == trackId) {
                    h->audioInputSelector->setEnabled(false);
                    selectedId = h->inputSelector->getSelectedId();
                    break;
                }
            }
            TrackManager::getInstance().setTrackAudioInput(trackId, "");
            if (selectedId == 1) {
                TrackManager::getInstance().setTrackMidiInput(trackId, "all");
            } else if (selectedId >= 10 && midiBridge) {
                auto midiInputs = midiBridge->getAvailableMidiInputs();
                int deviceIndex = selectedId - 10;
                if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size())) {
                    TrackManager::getInstance().setTrackMidiInput(trackId,
                                                                  midiInputs[deviceIndex].id);
                } else {
                    TrackManager::getInstance().setTrackMidiInput(trackId, "all");
                }
            } else {
                TrackManager::getInstance().setTrackMidiInput(trackId, "all");
            }
        } else {
            TrackManager::getInstance().setTrackMidiInput(trackId, "");
        }
    };

    header.inputSelector->onSelectionChanged = [trackId, midiBridge](int selectedId) {
        if (selectedId == 2) {
            TrackManager::getInstance().setTrackMidiInput(trackId, "");
        } else if (selectedId == 1) {
            TrackManager::getInstance().setTrackMidiInput(trackId, "all");
        } else if (selectedId >= 10 && midiBridge) {
            auto midiInputs = midiBridge->getAvailableMidiInputs();
            int deviceIndex = selectedId - 10;
            if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiInputs.size())) {
                TrackManager::getInstance().setTrackMidiInput(trackId, midiInputs[deviceIndex].id);
            }
        }
    };

    // Output selector callbacks
    header.outputSelector->onEnabledChanged = [trackId](bool enabled) {
        if (enabled) {
            TrackManager::getInstance().setTrackAudioOutput(trackId, "master");
        } else {
            TrackManager::getInstance().setTrackAudioOutput(trackId, "");
        }
    };

    // Capture outputTrackMapping_ by value so each header has its own snapshot
    // (the shared member is rebuilt per-header in populateAudioOutputOptions)
    header.outputSelector->onSelectionChanged = [trackId,
                                                 mapping = outputTrackMapping_](int selectedId) {
        if (selectedId == 1) {
            // Master
            TrackManager::getInstance().setTrackAudioOutput(trackId, "master");
        } else if (selectedId == 2) {
            // None
            TrackManager::getInstance().setTrackAudioOutput(trackId, "");
        } else if (selectedId >= 200) {
            // Track destination (Group, Aux, Audio, Instrument)
            auto it = mapping.find(selectedId);
            if (it != mapping.end()) {
                TrackManager::getInstance().setTrackAudioOutput(
                    trackId, "track:" + juce::String(it->second));
            }
        } else if (selectedId >= 10) {
            // Hardware output device (existing behavior — use device ID)
            // For now, hardware channels route via device ID
            TrackManager::getInstance().setTrackAudioOutput(trackId, "master");
        }
    };

    // MIDI output selector callbacks
    header.midiOutputSelector->onEnabledChanged = [trackId](bool enabled) {
        if (!enabled) {
            TrackManager::getInstance().setTrackMidiOutput(trackId, "");
        }
        // When enabling, don't set anything yet — user picks a device from dropdown
    };

    header.midiOutputSelector->onSelectionChanged = [this, trackId, midiBridge](int selectedId) {
        if (selectedId == 1) {
            // None
            TrackManager::getInstance().setTrackMidiOutput(trackId, "");
        } else if (selectedId >= 200) {
            // Track destination
            auto it = midiOutputTrackMapping_.find(selectedId);
            if (it != midiOutputTrackMapping_.end()) {
                TrackManager::getInstance().setTrackMidiOutput(trackId,
                                                               "track:" + juce::String(it->second));
            }
        } else if (selectedId >= 10 && midiBridge) {
            auto midiOutputs = midiBridge->getAvailableMidiOutputs();
            int deviceIndex = selectedId - 10;
            if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiOutputs.size())) {
                TrackManager::getInstance().setTrackMidiOutput(trackId,
                                                               midiOutputs[deviceIndex].id);
            }
        }
    };
}

void TrackHeadersPanel::tracksChanged() {
    // Clear existing track headers
    for (auto& header : trackHeaders) {
        removeChildComponent(header->nameLabel.get());
        removeChildComponent(header->muteButton.get());
        removeChildComponent(header->soloButton.get());
        removeChildComponent(header->volumeLabel.get());
        removeChildComponent(header->panLabel.get());
        removeChildComponent(header->collapseButton.get());
        removeChildComponent(header->audioInputSelector.get());
        removeChildComponent(header->inputSelector.get());
        removeChildComponent(header->outputSelector.get());
        removeChildComponent(header->midiOutputSelector.get());
    }
    trackHeaders.clear();
    visibleTrackIds_.clear();
    selectedTrackIndices_.clear();

    // Build visible tracks list (respecting hierarchy)
    auto& trackManager = TrackManager::getInstance();
    auto topLevelTracks = trackManager.getVisibleTopLevelTracks(currentViewMode_);

    // Helper lambda to add track and its visible children recursively
    std::function<void(TrackId, int)> addTrackRecursive = [&](TrackId trackId, int depth) {
        const auto* track = trackManager.getTrack(trackId);
        if (!track || !track->isVisibleIn(currentViewMode_))
            return;

        if (track->type == TrackType::Aux)
            return;  // Aux tracks rendered in separate section

        visibleTrackIds_.push_back(trackId);

        auto header = std::make_unique<TrackHeader>(track->name);
        header->trackId = trackId;
        header->depth = depth;
        header->isGroup = track->isGroup() || track->hasChildren();
        header->isMultiOut = (track->type == TrackType::MultiOut);
        header->isMaster = (track->type == TrackType::Master);
        header->isCollapsed = track->isCollapsedIn(currentViewMode_);
        header->muted = track->muted;
        header->solo = track->soloed;
        header->frozen = track->frozen;
        header->volume = track->volume;
        header->pan = track->pan;

        // Set track colour on swatch
        header->trackColour = track->colour;

        // Inherit global I/O routing visibility
        header->showIORouting = showIORouting_;

        // Use height from view settings
        header->height = track->viewSettings.getHeight(currentViewMode_);

        // Set up callbacks with track ID (not index)
        setupTrackHeaderWithId(*header, trackId);

        // Add components
        addAndMakeVisible(*header->nameLabel);
        addAndMakeVisible(*header->muteButton);
        addAndMakeVisible(*header->soloButton);
        addAndMakeVisible(*header->recordButton);
        addAndMakeVisible(*header->monitorButton);
        addAndMakeVisible(*header->automationButton);
        addAndMakeVisible(*header->volumeLabel);
        addAndMakeVisible(*header->panLabel);
        addAndMakeVisible(*header->audioInputSelector);
        addAndMakeVisible(*header->inputSelector);
        addAndMakeVisible(*header->outputSelector);
        addAndMakeVisible(*header->midiOutputSelector);
        addAndMakeVisible(*header->audioColumnLabel);
        addAndMakeVisible(*header->midiColumnLabel);
        addAndMakeVisible(*header->inputIcon);
        addAndMakeVisible(*header->outputIcon);
        for (auto& sendLabel : header->sendLabels) {
            addChildComponent(*sendLabel);  // Hidden by default; shown when track has sends
        }
        addAndMakeVisible(*header->meterComponent);
        addAndMakeVisible(*header->midiIndicator);
        addAndMakeVisible(*header->sessionModeButton);

        // Wire session mode button
        auto* smBtn = static_cast<SessionModeButton*>(header->sessionModeButton.get());
        smBtn->setSessionMode(track->playbackMode == TrackPlaybackMode::Session);
        smBtn->onClick = [trackId]() {
            TrackManager::getInstance().setTrackPlaybackMode(trackId,
                                                             TrackPlaybackMode::Arrangement);
        };

        // Add collapse button for groups and tracks with multi-out children
        if (header->isGroup || track->hasChildren()) {
            updateCollapseButtonIcon(*header);
            header->collapseButton->onClick = [this, trackId]() { handleCollapseToggle(trackId); };
            addAndMakeVisible(*header->collapseButton);
        }

        // Update UI state
        header->muteButton->setToggleState(track->muted, juce::dontSendNotification);
        header->soloButton->setToggleState(track->soloed, juce::dontSendNotification);
        header->recordButton->setToggleState(track->recordArmed, juce::dontSendNotification);
        header->volumeLabel->setValue(gainToDb(track->volume), juce::dontSendNotification);
        header->panLabel->setValue(track->pan, juce::dontSendNotification);

        // Sync monitor button state
        switch (track->inputMonitor) {
            case InputMonitorMode::Off:
                header->monitorButton->setButtonText("-");
                break;
            case InputMonitorMode::In:
                header->monitorButton->setButtonText("I");
                break;
            case InputMonitorMode::Auto:
                header->monitorButton->setButtonText("A");
                break;
        }
        header->monitorButton->setToggleState(track->inputMonitor != InputMonitorMode::Off,
                                              juce::dontSendNotification);

        trackHeaders.push_back(std::move(header));

        // Add children if group/instrument is not collapsed
        if (track->hasChildren() && !track->isCollapsedIn(currentViewMode_)) {
            for (auto childId : track->childIds) {
                addTrackRecursive(childId, depth + 1);
            }
        }
    };

    // Add all visible top-level tracks (and their children)
    for (auto trackId : topLevelTracks) {
        addTrackRecursive(trackId, 0);
    }

    // Sync routing selectors to match each track's saved input/output state
    for (size_t i = 0; i < trackHeaders.size(); ++i) {
        const auto* trk = trackManager.getTrack(visibleTrackIds_[i]);
        if (trk)
            updateRoutingSelectorFromTrack(*trackHeaders[i], trk);
    }

    // Sync automation lane visibility from AutomationManager
    syncAutomationLaneVisibility();

    updateTrackHeaderLayout();
    repaint();
}

void TrackHeadersPanel::trackPropertyChanged(int trackId) {
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (!track)
        return;

    // Find the index in our visible tracks list
    int index = -1;
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        if (visibleTrackIds_[i] == trackId) {
            index = static_cast<int>(i);
            break;
        }
    }

    if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
        auto& header = *trackHeaders[index];
        header.name = track->name;
        header.muted = track->muted;
        header.solo = track->soloed;
        header.frozen = track->frozen;
        header.volume = track->volume;
        header.pan = track->pan;

        // Note: Don't update height here - height should only change via:
        // 1. tracksChanged() (initial load)
        // 2. setTrackHeight() (user resize)
        // Updating height on every property change would reset user's resize

        header.nameLabel->setText(track->name, juce::dontSendNotification);
        header.muteButton->setToggleState(track->muted, juce::dontSendNotification);
        header.soloButton->setToggleState(track->soloed, juce::dontSendNotification);
        header.recordButton->setToggleState(track->recordArmed, juce::dontSendNotification);
        header.volumeLabel->setValue(gainToDb(track->volume), juce::dontSendNotification);
        header.panLabel->setValue(track->pan, juce::dontSendNotification);

        // Update monitor button
        if (header.monitorButton) {
            switch (track->inputMonitor) {
                case InputMonitorMode::Off:
                    header.monitorButton->setButtonText("-");
                    break;
                case InputMonitorMode::In:
                    header.monitorButton->setButtonText("I");
                    break;
                case InputMonitorMode::Auto:
                    header.monitorButton->setButtonText("A");
                    break;
            }
            header.monitorButton->setToggleState(track->inputMonitor != InputMonitorMode::Off,
                                                 juce::dontSendNotification);
        }

        // Update track colour
        header.trackColour = track->colour;

        // Update session mode button
        if (header.sessionModeButton) {
            static_cast<SessionModeButton*>(header.sessionModeButton.get())
                ->setSessionMode(track->playbackMode == TrackPlaybackMode::Session);
        }

        // Update routing selectors to match track state
        updateRoutingSelectorFromTrack(header, track);

        // MultiOut children: show where audio actually goes (parent's output destination)
        if (header.isMultiOut && track->hasParent()) {
            juce::String outputName = "Master";
            if (auto* parent = TrackManager::getInstance().getTrack(track->parentId)) {
                if (parent->hasParent()) {
                    if (auto* group = TrackManager::getInstance().getTrack(parent->parentId)) {
                        if (group->isGroup())
                            outputName = group->name;
                    }
                }
            }
            header.outputSelector->setOptions({{1, outputName}});
            header.outputSelector->setSelectedId(1);
            header.outputSelector->setEnabled(false);
        }

        // Update send labels from track data
        if (track->sends.size() == header.sendLabels.size()) {
            // Same count — update levels in-place (avoids destroying labels mid-drag)
            for (size_t i = 0; i < header.sendLabels.size(); ++i) {
                float levelDb = gainToDb(track->sends[i].level);
                header.sendLabels[i]->setValue(levelDb, juce::dontSendNotification);
            }
        } else {
            // Send count changed — full rebuild
            rebuildSendLabels(header, trackId);
        }

        updateTrackHeaderLayout();
        repaint();
    }
}

void TrackHeadersPanel::trackDevicesChanged(TrackId trackId) {
    // Sends were added or removed — rebuild send labels for this track
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        if (visibleTrackIds_[i] == trackId && i < trackHeaders.size()) {
            rebuildSendLabels(*trackHeaders[i], trackId);
            updateTrackHeaderLayout();
            repaint();
            break;
        }
    }
}

void TrackHeadersPanel::updateRoutingSelectorFromTrack(TrackHeader& header,
                                                       const TrackInfo* track) {
    if (!track || !audioEngine_)
        return;
    auto* deviceManager = audioEngine_->getDeviceManager();
    auto* device = deviceManager ? deviceManager->getCurrentAudioDevice() : nullptr;
    juce::BigInteger enabledIn, enabledOut;
    std::map<int, juce::String> teInputDeviceNames;
    if (auto* bridge = audioEngine_->getAudioBridge()) {
        enabledIn = bridge->getEnabledInputChannels();
        enabledOut = bridge->getEnabledOutputChannels();
        teInputDeviceNames = bridge->getInputDeviceNamesByChannel();
    }
    RoutingSyncHelper::syncSelectorsFromTrack(
        *track, header.audioInputSelector.get(), header.inputSelector.get(),
        header.outputSelector.get(), header.midiOutputSelector.get(), audioEngine_->getMidiBridge(),
        device, header.trackId, outputTrackMapping_, midiOutputTrackMapping_, &inputTrackMapping_,
        enabledIn, enabledOut, &inputChannelMapping_, teInputDeviceNames);
}

void TrackHeadersPanel::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));

    // Draw border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);

    // Draw track headers and automation lane headers
    for (size_t i = 0; i < trackHeaders.size(); ++i) {
        auto headerArea = getTrackHeaderArea(static_cast<int>(i));
        if (headerArea.intersects(getLocalBounds())) {
            paintTrackHeader(g, *trackHeaders[i], headerArea,
                             selectedTrackIndices_.count(static_cast<int>(i)) > 0);

            // Draw resize handle
            auto resizeArea = getResizeHandleArea(static_cast<int>(i));
            paintResizeHandle(g, resizeArea);
        }

        // Draw automation lane headers for this track
        paintAutomationLaneHeaders(g, static_cast<int>(i));
    }

    // Draw drag-and-drop feedback on top
    paintDragFeedback(g);

    // Draw plugin drop highlight
    if (pluginDropTrackIndex_ >= 0 &&
        pluginDropTrackIndex_ < static_cast<int>(trackHeaders.size())) {
        auto area = getTrackHeaderArea(pluginDropTrackIndex_);
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.fillRect(area);
    }
}

void TrackHeadersPanel::resized() {
    updateTrackHeaderLayout();
}

void TrackHeadersPanel::selectTrack(int index) {
    if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
        selectedTrackIndices_.clear();
        selectedTrackIndices_.insert(index);

        // Notify SelectionManager of selection change (which syncs with TrackManager)
        TrackId trackId = trackHeaders[index]->trackId;
        SelectionManager::getInstance().selectTrack(trackId);

        if (onTrackSelected) {
            onTrackSelected(index);
        }

        grabKeyboardFocus();
        repaint();
    }
}

void TrackHeadersPanel::trackSelectionChanged(TrackId trackId) {
    selectedTrackIndices_.clear();
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        if (visibleTrackIds_[i] == trackId) {
            selectedTrackIndices_.insert(static_cast<int>(i));
            break;
        }
    }
    repaint();
}

void TrackHeadersPanel::selectionTypeChanged(SelectionType /*newType*/) {
    // Handled by trackSelectionChanged / multiTrackSelectionChanged
}

void TrackHeadersPanel::multiTrackSelectionChanged(const std::unordered_set<TrackId>& trackIds) {
    selectedTrackIndices_.clear();
    for (size_t i = 0; i < visibleTrackIds_.size(); ++i) {
        if (trackIds.count(visibleTrackIds_[i]) > 0) {
            selectedTrackIndices_.insert(static_cast<int>(i));
        }
    }
    repaint();
}

int TrackHeadersPanel::getNumTracks() const {
    return static_cast<int>(trackHeaders.size());
}

void TrackHeadersPanel::setTrackHeight(int trackIndex, int height) {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(trackHeaders.size())) {
        height = juce::jlimit(MIN_TRACK_HEIGHT, MAX_TRACK_HEIGHT, height);
        trackHeaders[trackIndex]->height = height;

        // Persist to TrackManager so height survives tracksChanged() rebuilds
        TrackId trackId = trackHeaders[trackIndex]->trackId;
        TrackManager::getInstance().setTrackHeight(trackId, currentViewMode_, height);

        updateTrackHeaderLayout();
        repaint();

        if (onTrackHeightChanged) {
            onTrackHeightChanged(trackIndex, height);
        }
    }
}

int TrackHeadersPanel::getTrackHeight(int trackIndex) const {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(trackHeaders.size())) {
        return trackHeaders[trackIndex]->height;
    }
    return DEFAULT_TRACK_HEIGHT;
}

int TrackHeadersPanel::getTotalTracksHeight() const {
    int totalHeight = 0;
    for (size_t i = 0; i < trackHeaders.size(); ++i) {
        totalHeight += getTrackTotalHeight(static_cast<int>(i));
    }
    return totalHeight;
}

int TrackHeadersPanel::getTrackYPosition(int trackIndex) const {
    int yPosition = 0;
    for (int i = 0; i < trackIndex && i < static_cast<int>(trackHeaders.size()); ++i) {
        yPosition += getTrackTotalHeight(i);
    }
    return yPosition;
}

int TrackHeadersPanel::getTrackTotalHeight(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(trackHeaders.size())) {
        return 0;
    }

    // Base track height
    int totalHeight = static_cast<int>(trackHeaders[trackIndex]->height * verticalZoom);

    // Add visible automation lanes
    if (trackIndex < static_cast<int>(visibleTrackIds_.size())) {
        TrackId trackId = visibleTrackIds_[trackIndex];
        totalHeight += getVisibleAutomationLanesHeight(trackId);
    }

    return totalHeight;
}

int TrackHeadersPanel::getVisibleAutomationLanesHeight(TrackId trackId) const {
    int totalHeight = 0;

    auto it = visibleAutomationLanes_.find(trackId);
    if (it != visibleAutomationLanes_.end()) {
        auto& manager = AutomationManager::getInstance();
        for (auto laneId : it->second) {
            const auto* lane = manager.getLane(laneId);
            if (lane && lane->visible) {
                // Apply vertical zoom to automation lane height (header + content + resize handle)
                int laneHeight = lane->expanded ? (AutomationLaneComponent::HEADER_HEIGHT +
                                                   static_cast<int>(lane->height * verticalZoom) +
                                                   AutomationLaneComponent::RESIZE_HANDLE_HEIGHT)
                                                : AutomationLaneComponent::HEADER_HEIGHT;
                totalHeight += laneHeight;
            }
        }
    }

    return totalHeight;
}

void TrackHeadersPanel::syncAutomationLaneVisibility() {
    visibleAutomationLanes_.clear();

    auto& manager = AutomationManager::getInstance();

    for (auto trackId : visibleTrackIds_) {
        auto laneIds = manager.getLanesForTrack(trackId);
        for (auto laneId : laneIds) {
            const auto* lane = manager.getLane(laneId);
            if (lane && lane->visible) {
                visibleAutomationLanes_[trackId].push_back(laneId);
            }
        }
    }
}

void TrackHeadersPanel::automationLanesChanged() {
    syncAutomationLaneVisibility();
    updateTrackHeaderLayout();
    repaint();
}

void TrackHeadersPanel::automationLanePropertyChanged(AutomationLaneId /*laneId*/) {
    syncAutomationLaneVisibility();
    updateTrackHeaderLayout();
    repaint();
}

void TrackHeadersPanel::setVerticalZoom(double zoom) {
    verticalZoom = juce::jlimit(0.5, 3.0, zoom);
    updateTrackHeaderLayout();
    repaint();
}

void TrackHeadersPanel::setupTrackHeader(TrackHeader& header, int trackIndex) {
    // Name label callback
    header.nameLabel->onTextChange = [this, trackIndex]() {
        if (trackIndex < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[trackIndex];
            header.name = header.nameLabel->getText();

            if (onTrackNameChanged) {
                onTrackNameChanged(trackIndex, header.name);
            }
        }
    };

    // Mute button callback
    header.muteButton->onClick = [this, trackIndex]() {
        if (trackIndex < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[trackIndex];
            header.muted = header.muteButton->getToggleState();

            if (onTrackMutedChanged) {
                onTrackMutedChanged(trackIndex, header.muted);
            }
        }
    };

    // Solo button callback
    header.soloButton->onClick = [this, trackIndex]() {
        if (trackIndex < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[trackIndex];
            header.solo = header.soloButton->getToggleState();

            if (onTrackSoloChanged) {
                onTrackSoloChanged(trackIndex, header.solo);
            }
        }
    };

    // Volume label callback
    header.volumeLabel->onValueChange = [this, trackIndex]() {
        if (trackIndex < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[trackIndex];
            // Convert dB to linear gain
            header.volume = dbToGain(static_cast<float>(header.volumeLabel->getValue()));

            if (onTrackVolumeChanged) {
                onTrackVolumeChanged(trackIndex, header.volume);
            }
        }
    };

    // Pan label callback
    header.panLabel->onValueChange = [this, trackIndex]() {
        if (trackIndex < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[trackIndex];
            header.pan = static_cast<float>(header.panLabel->getValue());

            if (onTrackPanChanged) {
                onTrackPanChanged(trackIndex, header.pan);
            }
        }
    };

    // Populate input options and output options
    populateMidiInputOptions(header.inputSelector.get());
    populateAudioOutputOptions(header.outputSelector.get());
}

void TrackHeadersPanel::setupTrackHeaderWithId(TrackHeader& header, int trackId) {
    // Name label callback - updates TrackManager
    header.nameLabel->onTextChange = [this, trackId]() {
        int index = TrackManager::getInstance().getTrackIndex(trackId);
        if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[index];
            header.name = header.nameLabel->getText();
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackNameCommand>(trackId, header.name));
        }
    };

    // Mute button callback - updates TrackManager
    header.muteButton->onClick = [this, trackId]() {
        int index = TrackManager::getInstance().getTrackIndex(trackId);
        if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[index];
            header.muted = header.muteButton->getToggleState();
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackMuteCommand>(trackId, header.muted));
        }
    };

    // Solo button callback - updates TrackManager
    header.soloButton->onClick = [this, trackId]() {
        int index = TrackManager::getInstance().getTrackIndex(trackId);
        if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[index];
            header.solo = header.soloButton->getToggleState();
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackSoloCommand>(trackId, header.solo));
        }
    };

    // Volume label callback - updates TrackManager
    header.volumeLabel->onValueChange = [this, trackId]() {
        int index = TrackManager::getInstance().getTrackIndex(trackId);
        if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[index];
            // Convert dB to linear gain
            header.volume = dbToGain(static_cast<float>(header.volumeLabel->getValue()));
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackVolumeCommand>(trackId, header.volume));
        }
    };

    // Pan label callback - updates TrackManager
    header.panLabel->onValueChange = [this, trackId]() {
        int index = TrackManager::getInstance().getTrackIndex(trackId);
        if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[index];
            header.pan = static_cast<float>(header.panLabel->getValue());
            UndoManager::getInstance().executeCommand(
                std::make_unique<SetTrackPanCommand>(trackId, header.pan));
        }
    };

    // Record arm button callback - updates TrackManager
    header.recordButton->onClick = [this, trackId]() {
        int index = TrackManager::getInstance().getTrackIndex(trackId);
        if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
            bool armed = trackHeaders[index]->recordButton->getToggleState();
            TrackManager::getInstance().setTrackRecordArmed(trackId, armed);
        }
    };

    // Monitor button callback - cycles Off → In → Auto → Off
    header.monitorButton->onClick = [trackId]() {
        auto* track = TrackManager::getInstance().getTrack(trackId);
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
            std::make_unique<SetTrackInputMonitorCommand>(trackId, nextMode));
    };

    // Automation button - no-op for now (planned for v0.2.0)
    header.automationButton->onClick = nullptr;

    // Create send labels from actual track sends
    rebuildSendLabels(header, trackId);

    // Populate all routing selectors

    populateAudioInputOptions(header.audioInputSelector.get(), trackId);
    populateMidiInputOptions(header.inputSelector.get());
    populateAudioOutputOptions(header.outputSelector.get(), trackId);
    populateMidiOutputOptions(header.midiOutputSelector.get(), trackId);

    // Set up routing callbacks (audio/MIDI input with mutual exclusion, outputs)
    setupRoutingCallbacks(header, trackId);
}

void TrackHeadersPanel::rebuildSendLabels(TrackHeader& header, TrackId trackId) {
    // Remove existing send labels from parent
    for (auto& label : header.sendLabels) {
        removeChildComponent(label.get());
    }
    header.sendLabels.clear();

    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (!track || track->type == TrackType::Aux)
        return;

    for (const auto& send : track->sends) {
        auto label = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Decibels);
        label->setRange(MIN_DB, MAX_DB, 0.0);
        float levelDb = gainToDb(send.level);
        label->setValue(levelDb, juce::dontSendNotification);

        int busIndex = send.busIndex;
        label->onValueChange = [trackId, busIndex, &header]() {
            // Find the label index by matching bus index
            const auto* track = TrackManager::getInstance().getTrack(trackId);
            if (!track)
                return;
            for (size_t i = 0; i < header.sendLabels.size() && i < track->sends.size(); ++i) {
                if (track->sends[i].busIndex == busIndex) {
                    float newLevel = dbToGain(static_cast<float>(header.sendLabels[i]->getValue()));
                    UndoManager::getInstance().executeCommand(
                        std::make_unique<SetSendLevelCommand>(trackId, busIndex, newLevel));
                    break;
                }
            }
        };

        // Right-click to remove send
        label->onRightClick = [trackId, busIndex]() {
            juce::PopupMenu menu;
            menu.addItem(1, "Remove Send");
            menu.showMenuAsync(juce::PopupMenu::Options(), [trackId, busIndex](int result) {
                if (result == 1) {
                    TrackManager::getInstance().removeSend(trackId, busIndex);
                }
            });
        };

        addChildComponent(*label);  // Hidden by default, layout will show if space allows
        header.sendLabels.push_back(std::move(label));
    }
}

void TrackHeadersPanel::paintTrackHeader(juce::Graphics& g, const TrackHeader& header,
                                         juce::Rectangle<int> area, bool isSelected) {
    // Calculate indent
    int indent = header.depth * INDENT_WIDTH;
    SideColumn outer(!headersOnRight_);  // outer edge: right normally, left when swapped

    // Draw indent guide lines for nested tracks on outer side
    if (header.depth > 0) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
        for (int d = 0; d < header.depth; ++d) {
            int x = headersOnRight_ ? area.getRight() - d * INDENT_WIDTH - INDENT_WIDTH / 2
                                    : area.getX() + d * INDENT_WIDTH + INDENT_WIDTH / 2;
            g.drawLine(x, area.getY(), x, area.getBottom(), 1.0f);
        }
    }

    // Background - groups have slightly different color
    auto bgArea = outer.trimmed(area, indent);
    if (header.isGroup) {
        g.setColour(isSelected ? DarkTheme::getColour(DarkTheme::TRACK_SELECTED)
                               : DarkTheme::getColour(DarkTheme::SURFACE).brighter(0.05f));
    } else {
        g.setColour(isSelected ? DarkTheme::getColour(DarkTheme::TRACK_SELECTED)
                               : DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND));
    }
    g.fillRect(bgArea);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(bgArea, 1);

    // Group indicator color strip on outer edge
    if (header.isGroup) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.7f));
        int stripX = headersOnRight_ ? bgArea.getRight() - 3 : bgArea.getX();
        g.fillRect(stripX, bgArea.getY(), 3, bgArea.getHeight());
    }

    // Track colour tinted name row — stretches to 0dB mark
    float zeroDbFrac = 1.0f - dbToMeterPos(0.0f);
    int nameRowHeight = juce::jmax(22, static_cast<int>(bgArea.getHeight() * zeroDbFrac));
    if (!header.isMaster && header.trackColour != juce::Colour(0xFF444444)) {
        auto nameRowArea = bgArea.withHeight(nameRowHeight);
        g.setColour(header.trackColour.withAlpha(0.5f));
        g.fillRect(nameRowArea);
    }

    // Separator line at 0dB boundary
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
    g.drawHorizontalLine(bgArea.getY() + nameRowHeight, static_cast<float>(bgArea.getX()),
                         static_cast<float>(bgArea.getRight()));

    // Frozen overlay — dim the track header
    if (header.frozen) {
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillRect(bgArea);
        // Cyan strip on outer edge to indicate frozen state
        g.setColour(juce::Colour(0xFF66CCDD).withAlpha(0.8f));
        int stripX = headersOnRight_ ? bgArea.getRight() - 3 : bgArea.getX();
        g.fillRect(stripX, bgArea.getY(), 3, bgArea.getHeight());
    }
}

void TrackHeadersPanel::paintResizeHandle(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.fillRect(area);
}

juce::Rectangle<int> TrackHeadersPanel::getTrackHeaderArea(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(trackHeaders.size())) {
        return {};
    }

    int yPosition = getTrackYPosition(trackIndex);
    int height = static_cast<int>(trackHeaders[trackIndex]->height * verticalZoom);

    return juce::Rectangle<int>(0, yPosition, getWidth(), height - RESIZE_HANDLE_HEIGHT);
}

juce::Rectangle<int> TrackHeadersPanel::getResizeHandleArea(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(trackHeaders.size())) {
        return {};
    }

    int yPosition = getTrackYPosition(trackIndex);
    int height = static_cast<int>(trackHeaders[trackIndex]->height * verticalZoom);

    return juce::Rectangle<int>(0, yPosition + height - RESIZE_HANDLE_HEIGHT, getWidth(),
                                RESIZE_HANDLE_HEIGHT);
}

bool TrackHeadersPanel::isResizeHandleArea(const juce::Point<int>& point, int& trackIndex) const {
    for (int i = 0; i < static_cast<int>(trackHeaders.size()); ++i) {
        if (getResizeHandleArea(i).contains(point)) {
            trackIndex = i;
            return true;
        }
    }
    return false;
}

void TrackHeadersPanel::layoutMeterColumn(TrackHeader& header, juce::Rectangle<int>& workArea,
                                          const SideColumn& outer) {
    const int meterWidth = 20;
    const int midiIndicatorWidth = 12;
    const int meterPadding = 4;

    auto meterArea = outer.removeFrom(workArea, meterWidth);
    outer.removeSpacing(workArea, 2);

    // MIDI indicator adjacent to audio meters (toward center)
    auto midiArea = outer.removeFrom(workArea, midiIndicatorWidth);
    outer.removeSpacing(workArea, meterPadding);

    // Audio meter spans full track height
    header.meterComponent->setBounds(meterArea);
    header.meterComponent->setVisible(true);
    static_cast<TrackMeter*>(header.meterComponent.get())->setNameRowY(header.nameRowBottomY);

    // MIDI indicator in top portion, session mode button in bottom portion
    const int sessionBtnSize = 14;
    auto midiTopArea = midiArea;
    auto sessionBtnArea = midiArea;

    if (midiArea.getHeight() > sessionBtnSize + 4) {
        sessionBtnArea = midiArea.removeFromBottom(sessionBtnSize + 2);
        midiTopArea = midiArea;
    }

    header.midiIndicator->setBounds(midiTopArea);
    header.midiIndicator->setVisible(header.inputSelector && header.inputSelector->isEnabled());
    header.midiIndicator->toFront(false);

    header.sessionModeButton->setBounds(sessionBtnArea);
    header.sessionModeButton->setVisible(true);
}

void TrackHeadersPanel::layoutVolPanAndButtons(TrackHeader& header, juce::Rectangle<int>& area,
                                               const SideColumn& inner, int gapOverride) {
    const int gap = 2;
    const int rh = 16;  // rowHeight
    const int areaWidth = area.getWidth();

    // Master track: volume + mute only (no solo, pan, record, monitor)
    if (header.isMaster) {
        auto row = area.removeFromTop(rh);
        const int rowWidth = std::min(areaWidth, areaWidth >= 260 ? areaWidth : 120);
        auto content = inner.removeFrom(row, rowWidth);
        const int btnW = 20;
        header.volumeLabel->setBounds(content.removeFromLeft(content.getWidth() - btnW - gap));
        header.volumeLabel->setVisible(true);
        content.removeFromLeft(gap);
        header.muteButton->setBounds(content);
        header.soloButton->setVisible(false);
        header.panLabel->setVisible(false);
        header.recordButton->setVisible(false);
        header.monitorButton->setVisible(false);
        header.automationButton->setVisible(false);
        return;
    }

    const int numBtns = header.isMultiOut ? 3 : 5;

    if (areaWidth >= 260) {
        // Single row: Vol Pan | M S R Mon A — fills full width
        auto row = area.removeFromTop(rh);
        const int rowWidth = areaWidth;
        const int mixW = rowWidth * 60 / 100;
        const int btnsW = rowWidth - mixW - gap;
        const int mixGap = 2;
        const int volW = (mixW - mixGap) * 80 / 100;
        // Anchor content block to inner side, fill left-to-right
        auto content = inner.removeFrom(row, rowWidth);
        header.volumeLabel->setBounds(content.removeFromLeft(volW));
        header.volumeLabel->setVisible(true);
        content.removeFromLeft(mixGap);
        header.panLabel->setBounds(content.removeFromLeft(mixW - volW - mixGap));
        header.panLabel->setVisible(true);
        content.removeFromLeft(gap);
        const int btnGapTotal = (numBtns - 1) * gap;
        const int btnW = (btnsW - btnGapTotal) / numBtns;
        header.muteButton->setBounds(content.removeFromLeft(btnW));
        content.removeFromLeft(gap);
        header.soloButton->setBounds(content.removeFromLeft(btnW));
        content.removeFromLeft(gap);
        if (!header.isMultiOut) {
            header.recordButton->setBounds(content.removeFromLeft(btnW));
            header.recordButton->setVisible(true);
            content.removeFromLeft(gap);
            header.monitorButton->setBounds(content.removeFromLeft(btnW));
            header.monitorButton->setVisible(true);
            content.removeFromLeft(gap);
        } else {
            header.recordButton->setVisible(false);
            header.monitorButton->setVisible(false);
        }
        header.automationButton->setBounds(
            content.removeFromLeft(btnsW - (numBtns - 1) * (btnW + gap)));
        header.automationButton->setVisible(true);
    } else {
        // Two rows: Vol Pan, then M S R Mon A
        const int rowWidth = std::min(areaWidth, 120);
        const int rowPadding =
            gapOverride >= 0 ? gapOverride : std::max(2, (area.getHeight() - 2 * rh) / 3);

        auto volPanRow = area.removeFromTop(rh);
        auto vpContent = inner.removeFrom(volPanRow, rowWidth);
        const int mixGap = 4;
        const int volW = (rowWidth - mixGap) * 80 / 100;
        header.volumeLabel->setBounds(vpContent.removeFromLeft(volW));
        header.volumeLabel->setVisible(true);
        vpContent.removeFromLeft(mixGap);
        header.panLabel->setBounds(vpContent.removeFromLeft(rowWidth - volW - mixGap));
        header.panLabel->setVisible(true);
        area.removeFromTop(rowPadding);

        auto btnRow = area.removeFromTop(rh);
        auto btnContent = inner.removeFrom(btnRow, rowWidth);
        const int btnW = (rowWidth - (numBtns - 1) * gap) / numBtns;
        header.muteButton->setBounds(btnContent.removeFromLeft(btnW));
        btnContent.removeFromLeft(gap);
        header.soloButton->setBounds(btnContent.removeFromLeft(btnW));
        btnContent.removeFromLeft(gap);
        if (!header.isMultiOut) {
            header.recordButton->setBounds(btnContent.removeFromLeft(btnW));
            header.recordButton->setVisible(true);
            btnContent.removeFromLeft(gap);
            header.monitorButton->setBounds(btnContent.removeFromLeft(btnW));
            header.monitorButton->setVisible(true);
            btnContent.removeFromLeft(gap);
        } else {
            header.recordButton->setVisible(false);
            header.monitorButton->setVisible(false);
        }
        header.automationButton->setBounds(
            btnContent.removeFromLeft(rowWidth - (numBtns - 1) * (btnW + gap) - gap));
        header.automationButton->setVisible(true);
    }
}

void TrackHeadersPanel::layoutControlArea(TrackHeader& header, juce::Rectangle<int>& tcpArea,
                                          const SideColumn& inner, int trackHeight) {
    const int spacing = 2;

    // Master track: skip name row space (painted "Master" label), then volume + mute
    if (header.isMaster) {
        header.nameLabel->setVisible(false);
        header.collapseButton->setVisible(false);
        header.audioInputSelector->setVisible(false);
        header.inputSelector->setVisible(false);
        header.outputSelector->setVisible(false);
        header.midiOutputSelector->setVisible(false);
        header.audioColumnLabel->setVisible(false);
        header.midiColumnLabel->setVisible(false);
        header.inputIcon->setVisible(false);
        header.outputIcon->setVisible(false);
        for (auto& sendLabel : header.sendLabels)
            sendLabel->setVisible(false);
        layoutVolPanAndButtons(header, tcpArea, inner);
        return;
    }

    // Name row is laid out by the parent (in the coloured top area)

    // Helper to hide all routing selectors and sends
    auto hideAllRouting = [&]() {
        header.audioInputSelector->setVisible(false);
        header.inputSelector->setVisible(false);
        header.outputSelector->setVisible(false);
        header.midiOutputSelector->setVisible(false);
        header.audioColumnLabel->setVisible(false);
        header.midiColumnLabel->setVisible(false);
        header.inputIcon->setVisible(false);
        header.outputIcon->setVisible(false);
        for (auto& sendLabel : header.sendLabels) {
            sendLabel->setVisible(false);
        }
    };

    if (trackHeight >= 100) {
        // LARGE LAYOUT
        const int contentRowHeight = 18;
        const bool hasSends = !header.sendLabels.empty();

        const int sendLabelWidth = 28;
        const bool wideEnough = tcpArea.getWidth() >= 260;
        const int ioRows = header.showIORouting ? 2 : 0;
        const int numRows = (wideEnough ? 1 : 2) + ioRows;

        int totalContentHeight = numRows * contentRowHeight;
        if (hasSends)
            totalContentHeight += contentRowHeight;
        int availableSpace = tcpArea.getHeight() - totalContentHeight;
        int divider = numRows - 1 + (hasSends ? 1 : 0);
        int rowGap = divider > 0 ? std::clamp(availableSpace / divider, 2, 8) : 2;

        layoutVolPanAndButtons(header, tcpArea, inner, rowGap);

        // Sends row
        if (hasSends) {
            tcpArea.removeFromTop(rowGap);
            auto sendRow = tcpArea.removeFromTop(contentRowHeight);

            for (auto& sendLabel : header.sendLabels) {
                if (sendRow.getWidth() >= sendLabelWidth) {
                    sendLabel->setBounds(sendRow.removeFromLeft(sendLabelWidth));
                    sendLabel->setVisible(true);
                    sendRow.removeFromLeft(spacing);
                } else {
                    sendLabel->setVisible(false);
                }
            }
        } else {
            for (auto& sendLabel : header.sendLabels) {
                sendLabel->setVisible(false);
            }
        }

        tcpArea.removeFromTop(rowGap);

        header.audioColumnLabel->setVisible(false);
        header.midiColumnLabel->setVisible(false);

        if (header.showIORouting) {
            const int iconSize = 16;
            const int ddGap = spacing;
            const int ioWidth = std::min(tcpArea.getWidth(), 120);
            const int ddWidth = (ioWidth - ddGap - ddGap - iconSize) / 2;

            auto inputRow = tcpArea.removeFromTop(contentRowHeight);
            auto inputContent = inner.removeFrom(inputRow, ioWidth);
            if (!header.isMultiOut) {
                header.audioInputSelector->setBounds(inputContent.removeFromLeft(ddWidth));
                header.audioInputSelector->setVisible(true);
                inputContent.removeFromLeft(ddGap);
                header.inputSelector->setBounds(inputContent.removeFromLeft(ddWidth));
                header.inputSelector->setVisible(true);
                inputContent.removeFromLeft(ddGap);
                header.inputIcon->setBounds(inputContent.removeFromLeft(iconSize));
                header.inputIcon->setVisible(true);
            } else {
                header.audioInputSelector->setVisible(false);
                header.inputSelector->setVisible(false);
                header.inputIcon->setVisible(false);
            }
            tcpArea.removeFromTop(rowGap);

            auto outputRow = tcpArea.removeFromTop(contentRowHeight);
            auto outputContent = inner.removeFrom(outputRow, ioWidth);
            header.outputSelector->setBounds(outputContent.removeFromLeft(ddWidth));
            header.outputSelector->setVisible(true);
            outputContent.removeFromLeft(ddGap);
            if (!header.isMultiOut) {
                header.midiOutputSelector->setBounds(outputContent.removeFromLeft(ddWidth));
                header.midiOutputSelector->setVisible(true);
            } else {
                header.midiOutputSelector->setVisible(false);
            }
            outputContent.removeFromLeft(ddGap);
            header.outputIcon->setBounds(outputContent.removeFromLeft(iconSize));
            header.outputIcon->setVisible(true);
        } else {
            hideAllRouting();
        }

    } else if (trackHeight >= 60) {
        // MEDIUM LAYOUT
        layoutVolPanAndButtons(header, tcpArea, inner);
        hideAllRouting();

    } else {
        // SMALL LAYOUT
        layoutVolPanAndButtons(header, tcpArea, inner);
        hideAllRouting();
    }
}

void TrackHeadersPanel::updateTrackHeaderLayout() {
    headersOnRight_ = Config::getInstance().getScrollbarOnLeft();
    SideColumn outer(headersOnRight_);   // meters: right normally, left when swapped
    SideColumn inner(!headersOnRight_);  // controls+indent: left normally, right when swapped

    for (size_t i = 0; i < trackHeaders.size(); ++i) {
        auto& header = *trackHeaders[i];
        auto headerArea = getTrackHeaderArea(static_cast<int>(i));

        if (!headerArea.isEmpty()) {
            const int trackHeight = headerArea.getHeight();

            // Split at 0dB: top = name/header area, bottom = controls
            float zeroDbFrac = 1.0f - dbToMeterPos(0.0f);
            int nameRowHeight = juce::jmax(22, static_cast<int>(trackHeight * zeroDbFrac));
            header.nameRowBottomY = headerArea.getY() + nameRowHeight;

            auto workArea = headerArea.reduced(4);
            layoutMeterColumn(header, workArea, outer);

            // Apply indentation on nesting side based on depth
            int indent = header.depth * INDENT_WIDTH;
            auto tcpArea = inner.trimmed(workArea, indent);

            // Name label in the top (coloured) area
            auto topArea = tcpArea.removeFromTop(nameRowHeight - 4);  // -4 for reduced() padding
            {
                const int nameRowH = 18;
                auto nameRow = topArea.removeFromTop(nameRowH);
                if (header.isGroup) {
                    auto btnArea = nameRow.removeFromLeft(COLLAPSE_BUTTON_SIZE);
                    int btnY = btnArea.getCentreY() - COLLAPSE_BUTTON_SIZE / 2;
                    header.collapseButton->setBounds(btnArea.getX(), btnY, COLLAPSE_BUTTON_SIZE,
                                                     COLLAPSE_BUTTON_SIZE);
                    nameRow.removeFromLeft(2);
                    header.collapseButton->setVisible(true);
                } else {
                    header.collapseButton->setVisible(false);
                }
                auto nameArea = nameRow.withTrimmedRight(nameRow.getWidth() / 4);
                header.nameLabel->setBounds(nameArea);
                header.nameLabel->setVisible(true);
                header.nameLabel->setJustificationType(headersOnRight_
                                                           ? juce::Justification::centredRight
                                                           : juce::Justification::centredLeft);
            }

            // Controls in the bottom area (below 0dB line)
            tcpArea.removeFromTop(3);
            layoutControlArea(header, tcpArea, inner, trackHeight);
        }
    }
}

void TrackHeadersPanel::mouseDown(const juce::MouseEvent& event) {
    // Convert to panel coordinates (handles clicks forwarded from children)
    auto localEvent = event.getEventRelativeTo(this);
    auto pos = localEvent.getPosition();

    // Handle vertical track height resizing and track selection
    int trackIndex;
    if (isResizeHandleArea(pos, trackIndex)) {
        // Start resizing
        isResizing = true;
        resizingTrackIndex = trackIndex;
        resizeStartY = localEvent.y;
        resizeStartHeight = trackHeaders[trackIndex]->height;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    } else {
        // Find which track was clicked
        for (int i = 0; i < static_cast<int>(trackHeaders.size()); ++i) {
            if (getTrackHeaderArea(i).contains(pos)) {
                TrackId trackId = trackHeaders[i]->trackId;

                if (event.mods.isCommandDown() && !event.mods.isPopupMenu()) {
                    // Cmd+click: toggle track in multi-selection
                    SelectionManager::getInstance().toggleTrackSelection(trackId);
                    grabKeyboardFocus();
                } else if (event.mods.isShiftDown() && !event.mods.isPopupMenu()) {
                    // Shift+click: range select from anchor to clicked track
                    auto anchorTrack = SelectionManager::getInstance().getAnchorTrack();
                    int anchorIndex = -1;
                    for (size_t j = 0; j < visibleTrackIds_.size(); ++j) {
                        if (visibleTrackIds_[j] == anchorTrack) {
                            anchorIndex = static_cast<int>(j);
                            break;
                        }
                    }
                    if (anchorIndex >= 0) {
                        int lo = std::min(anchorIndex, i);
                        int hi = std::max(anchorIndex, i);
                        std::unordered_set<TrackId> rangeIds;
                        for (int k = lo; k <= hi; ++k) {
                            rangeIds.insert(trackHeaders[k]->trackId);
                        }
                        SelectionManager::getInstance().selectTracks(rangeIds);
                    } else {
                        selectTrack(i);
                    }
                    grabKeyboardFocus();
                } else {
                    // Plain click on a track that's already in multi-selection:
                    // defer single-selection to mouseUp so drag can keep multi-selection
                    if (selectedTrackIndices_.size() > 1 && selectedTrackIndices_.count(i) > 0) {
                        deferredSingleSelectIndex_ = i;
                    } else {
                        // Plain click: single selection
                        selectTrack(i);
                    }
                }

                // Right-click shows context menu (only for direct clicks, not child forwards)
                if (event.mods.isPopupMenu() && event.originalComponent == this) {
                    showContextMenu(i, pos);
                } else if (event.originalComponent == this && !event.mods.isCommandDown() &&
                           !event.mods.isShiftDown()) {
                    // Record potential drag start (plain clicks or clicks on multi-selected track)
                    draggedTrackIndex_ = i;
                    dragStartX_ = localEvent.x;
                    dragStartY_ = localEvent.y;
                }
                break;
            }
        }
    }
}

void TrackHeadersPanel::mouseDrag(const juce::MouseEvent& event) {
    // Handle vertical track height resizing
    if (isResizing && resizingTrackIndex >= 0) {
        int deltaY = event.y - resizeStartY;
        int newHeight =
            juce::jlimit(MIN_TRACK_HEIGHT, MAX_TRACK_HEIGHT, resizeStartHeight + deltaY);
        setTrackHeight(resizingTrackIndex, newHeight);
        return;
    }

    // Handle drag-to-reorder
    if (draggedTrackIndex_ >= 0) {
        int deltaX = std::abs(event.x - dragStartX_);
        int deltaY = std::abs(event.y - dragStartY_);

        // Check if we've exceeded the drag threshold
        if (!isDraggingToReorder_ && (deltaX > DRAG_THRESHOLD || deltaY > DRAG_THRESHOLD)) {
            isDraggingToReorder_ = true;
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        }

        if (isDraggingToReorder_) {
            currentDragY_ = event.y;
            calculateDropTarget(event.x, event.y);

            // Update cursor based on drop target type
            if (dropTargetType_ == DropTargetType::OntoGroup) {
                setMouseCursor(juce::MouseCursor::CopyingCursor);
            } else {
                setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            }

            repaint();
        }
    }
}

void TrackHeadersPanel::mouseWheelMove(const juce::MouseEvent& event,
                                       const juce::MouseWheelDetails& wheel) {
    if (scrollTarget_) {
        // Match JUCE Viewport's scroll formula: deltaY * 14.0f * singleStepSize (default 16)
        auto pos = scrollTarget_->getViewPosition();
        float distance = wheel.deltaY * 14.0f * 16.0f;
        int step = juce::roundToInt(distance < 0.0f ? juce::jmin(distance, -1.0f)
                                                    : juce::jmax(distance, 1.0f));
        scrollTarget_->setViewPosition(pos.x, pos.y - step);
    } else {
        juce::Component::mouseWheelMove(event, wheel);
    }
}

void TrackHeadersPanel::mouseUp(const juce::MouseEvent& /*event*/) {
    // Handle vertical track height resizing cleanup
    if (isResizing) {
        isResizing = false;
        resizingTrackIndex = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    // Handle drag-to-reorder completion
    if (isDraggingToReorder_) {
        executeDrop();
        deferredSingleSelectIndex_ = -1;  // Don't reduce to single after drag
    } else if (deferredSingleSelectIndex_ >= 0) {
        // No drag happened — reduce multi-selection to single click target
        selectTrack(deferredSingleSelectIndex_);
    }
    deferredSingleSelectIndex_ = -1;
    resetDragState();
}

void TrackHeadersPanel::mouseMove(const juce::MouseEvent& event) {
    // Handle vertical track height resizing
    int trackIndex;
    if (isResizeHandleArea(event.getPosition(), trackIndex)) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

// Setter methods
void TrackHeadersPanel::setTrackName(int trackIndex, const juce::String& name) {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(trackHeaders.size())) {
        trackHeaders[trackIndex]->name = name;
        trackHeaders[trackIndex]->nameLabel->setText(name, juce::dontSendNotification);
    }
}

void TrackHeadersPanel::setTrackMuted(int trackIndex, bool muted) {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(trackHeaders.size())) {
        trackHeaders[trackIndex]->muted = muted;
        trackHeaders[trackIndex]->muteButton->setToggleState(muted, juce::dontSendNotification);
    }
}

void TrackHeadersPanel::setTrackSolo(int trackIndex, bool solo) {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(trackHeaders.size())) {
        trackHeaders[trackIndex]->solo = solo;
        trackHeaders[trackIndex]->soloButton->setToggleState(solo, juce::dontSendNotification);
    }
}

void TrackHeadersPanel::setTrackVolume(int trackIndex, float volume) {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(trackHeaders.size())) {
        trackHeaders[trackIndex]->volume = volume;
        // Convert linear gain to dB
        trackHeaders[trackIndex]->volumeLabel->setValue(gainToDb(volume),
                                                        juce::dontSendNotification);
    }
}

void TrackHeadersPanel::setTrackPan(int trackIndex, float pan) {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(trackHeaders.size())) {
        trackHeaders[trackIndex]->pan = pan;
        trackHeaders[trackIndex]->panLabel->setValue(pan, juce::dontSendNotification);
    }
}

void TrackHeadersPanel::updateCollapseButtonIcon(TrackHeader& header) {
    auto colour = DarkTheme::getColour(DarkTheme::TEXT_PRIMARY);
    if (header.isCollapsed) {
        auto icon = juce::Drawable::createFromImageData(BinaryData::chevron_right_svg,
                                                        BinaryData::chevron_right_svgSize);
        icon->replaceColour(juce::Colour(0xFFB3B3B3), colour);
        header.collapseButton->setImages(icon.get());
    } else {
        auto icon = juce::Drawable::createFromImageData(BinaryData::chevron_down_svg,
                                                        BinaryData::chevron_down_svgSize);
        icon->replaceColour(juce::Colour(0xFFB3B3B3), colour);
        header.collapseButton->setImages(icon.get());
    }
}

void TrackHeadersPanel::handleCollapseToggle(TrackId trackId) {
    auto& trackManager = TrackManager::getInstance();
    const auto* track = trackManager.getTrack(trackId);
    if (track && (track->isGroup() || track->hasChildren())) {
        bool currentlyCollapsed = track->isCollapsedIn(currentViewMode_);
        trackManager.setTrackCollapsed(trackId, !currentlyCollapsed);
    }
}

void TrackHeadersPanel::showContextMenu(int trackIndex, juce::Point<int> position) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(trackHeaders.size()))
        return;

    auto& header = *trackHeaders[trackIndex];
    auto& trackManager = TrackManager::getInstance();
    const auto* track = trackManager.getTrack(header.trackId);
    if (!track)
        return;

    juce::PopupMenu menu;

    // Track type info
    menu.addSectionHeader(track->name);
    menu.addSeparator();

    // Group operations
    if (track->isGroup()) {
        // Collapse/expand
        menu.addItem(1, track->isCollapsedIn(currentViewMode_) ? "Expand Group" : "Collapse Group");
        menu.addSeparator();
    }

    // Move to group submenu
    juce::PopupMenu moveToGroupMenu;
    const auto& allTracks = trackManager.getTracks();
    bool hasGroups = false;

    for (const auto& t : allTracks) {
        if (t.isGroup() && t.id != header.trackId) {
            // Don't allow moving a group into its own descendants
            if (track->isGroup()) {
                auto descendants = trackManager.getAllDescendants(header.trackId);
                if (std::find(descendants.begin(), descendants.end(), t.id) != descendants.end())
                    continue;
            }
            moveToGroupMenu.addItem(100 + t.id, t.name);
            hasGroups = true;
        }
    }

    if (hasGroups) {
        menu.addSubMenu("Move to Group", moveToGroupMenu);
    }

    // Remove from group (if track has a parent)
    if (!track->isTopLevel()) {
        menu.addItem(2, "Remove from Group");
    }

    menu.addSeparator();

    // Add Send submenu (list all available tracks as send destinations)
    {
        juce::PopupMenu sendMenu;
        const auto& allTracks = trackManager.getTracks();
        int sendItemId = 500;
        bool hasOptions = false;

        // Collect descendants to prevent routing cycles
        std::vector<TrackId> descendants;
        if (track->id != INVALID_TRACK_ID) {
            descendants = trackManager.getAllDescendants(track->id);
        }

        auto addSendTargets = [&](TrackType type) {
            bool addedSeparator = false;
            for (const auto& t : allTracks) {
                if (t.type != type || t.id == track->id || t.type == TrackType::Master)
                    continue;
                if (std::find(descendants.begin(), descendants.end(), t.id) != descendants.end())
                    continue;

                bool alreadyConnected = false;
                for (const auto& s : track->sends) {
                    if (s.destTrackId == t.id) {
                        alreadyConnected = true;
                        break;
                    }
                }
                if (alreadyConnected)
                    continue;

                if (!addedSeparator && hasOptions) {
                    sendMenu.addSeparator();
                    addedSeparator = true;
                }
                sendMenu.addItem(sendItemId + t.id, t.name);
                hasOptions = true;
            }
        };

        addSendTargets(TrackType::Aux);
        addSendTargets(TrackType::Group);
        addSendTargets(TrackType::Audio);

        if (hasOptions) {
            menu.addSubMenu("Add Send", sendMenu);
        }

        // Remove Send submenu (list current sends)
        if (!track->sends.empty()) {
            juce::PopupMenu removeSendMenu;
            for (const auto& s : track->sends) {
                const auto* destTrack = trackManager.getTrack(s.destTrackId);
                juce::String destName = destTrack ? destTrack->name : "Unknown";
                removeSendMenu.addItem(600 + s.busIndex, destName);
            }
            menu.addSubMenu("Remove Send", removeSendMenu);
        }
    }

    // Freeze/Unfreeze (for regular tracks only)
    if (track->type == TrackType::Audio) {
        menu.addSeparator();
        menu.addItem(7, track->frozen ? "Unfreeze Track" : "Freeze Track");
    }

    menu.addSeparator();

    // Duplicate track
    menu.addItem(4, "Duplicate Track");
    menu.addItem(5, "Duplicate Track Without Content");

    // Delete track
    menu.addItem(3, "Delete Track");

    menu.addSeparator();

    // Show/Hide I/O routing
    menu.addItem(6, header.showIORouting ? "Hide I/O Routing" : "Show I/O Routing");

    // Show menu and handle result
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                           localAreaToGlobal(juce::Rectangle<int>(position.x, position.y, 1, 1))),
                       [this, trackId = header.trackId, trackIndex](int result) {
                           if (result == 1) {
                               // Toggle collapse
                               handleCollapseToggle(trackId);
                           } else if (result == 2) {
                               // Remove from group
                               TrackManager::getInstance().removeTrackFromGroup(trackId);
                           } else if (result == 3) {
                               // Delete track (through undo system)
                               auto cmd = std::make_unique<DeleteTrackCommand>(trackId);
                               UndoManager::getInstance().executeCommand(std::move(cmd));
                           } else if (result == 4) {
                               // Duplicate track with content
                               auto cmd = std::make_unique<DuplicateTrackCommand>(trackId, true);
                               UndoManager::getInstance().executeCommand(std::move(cmd));
                           } else if (result == 5) {
                               // Duplicate track without content
                               auto cmd = std::make_unique<DuplicateTrackCommand>(trackId, false);
                               UndoManager::getInstance().executeCommand(std::move(cmd));
                           } else if (result == 6) {
                               // Toggle I/O routing visibility (per-track)
                               if (trackIndex >= 0 &&
                                   trackIndex < static_cast<int>(trackHeaders.size())) {
                                   trackHeaders[trackIndex]->showIORouting =
                                       !trackHeaders[trackIndex]->showIORouting;
                                   resized();
                               }
                           } else if (result == 7) {
                               // Toggle freeze
                               auto* t = TrackManager::getInstance().getTrack(trackId);
                               if (t) {
                                   TrackManager::getInstance().setTrackFrozen(trackId, !t->frozen);
                               }
                           } else if (result >= 600) {
                               // Remove send (busIndex = result - 600)
                               int busIndex = result - 600;
                               TrackManager::getInstance().removeSend(trackId, busIndex);
                           } else if (result >= 500) {
                               // Add send (aux trackId = result - 500)
                               // Note: checked after >= 600 to avoid collision when trackId >= 100
                               TrackId auxId = result - 500;
                               TrackManager::getInstance().addSend(trackId, auxId);
                           } else if (result >= 100) {
                               // Move to group
                               TrackId groupId = result - 100;
                               TrackManager::getInstance().addTrackToGroup(trackId, groupId);
                           }
                       });
}

void TrackHeadersPanel::toggleRouting(int trackIndex, RoutingType type) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(trackHeaders.size()))
        return;

    auto& header = *trackHeaders[trackIndex];

    switch (type) {
        case RoutingType::AudioIn:
            header.audioInEnabled = !header.audioInEnabled;
            header.audioInputSelector->setEnabled(header.audioInEnabled);
            break;
        case RoutingType::AudioOut:
            header.audioOutEnabled = !header.audioOutEnabled;
            header.outputSelector->setEnabled(header.audioOutEnabled);
            break;
        case RoutingType::MidiIn:
            header.midiInEnabled = !header.midiInEnabled;
            header.inputSelector->setEnabled(header.midiInEnabled);
            break;
        case RoutingType::MidiOut:
            header.midiOutEnabled = !header.midiOutEnabled;
            header.midiOutputSelector->setEnabled(header.midiOutEnabled);
            break;
    }

    // Recalculate layout to show/hide routing rows
    updateTrackHeaderLayout();
    repaint();
}

void TrackHeadersPanel::calculateDropTarget(int /*mouseX*/, int mouseY) {
    dropTargetType_ = DropTargetType::None;
    dropTargetIndex_ = -1;

    if (draggedTrackIndex_ < 0 || trackHeaders.empty())
        return;

    // Iterate through track headers to find drop position
    for (int i = 0; i < static_cast<int>(trackHeaders.size()); ++i) {
        auto headerArea = getTrackHeaderArea(i);
        if (headerArea.isEmpty())
            continue;

        int headerTop = headerArea.getY();
        int headerBottom = headerArea.getBottom();
        int headerHeight = headerArea.getHeight();
        int quarterHeight = headerHeight / 4;

        // Skip selected tracks (all tracks being dragged together)
        bool isBeingDragged = (selectedTrackIndices_.size() > 1 &&
                               selectedTrackIndices_.count(draggedTrackIndex_) > 0)
                                  ? selectedTrackIndices_.count(i) > 0
                                  : (i == draggedTrackIndex_);
        if (isBeingDragged)
            continue;

        // Check if mouse is in this track's vertical range
        if (mouseY >= headerTop && mouseY <= headerBottom) {
            // Top quarter = insert before this track
            if (mouseY < headerTop + quarterHeight) {
                dropTargetType_ = DropTargetType::BetweenTracks;
                dropTargetIndex_ = i;
                return;
            }
            // Bottom quarter = insert after this track
            else if (mouseY > headerBottom - quarterHeight) {
                dropTargetType_ = DropTargetType::BetweenTracks;
                dropTargetIndex_ = i + 1;
                return;
            }
            // Middle half = drop onto group (if it is a group)
            else if (trackHeaders[i]->isGroup && canDropIntoGroup(draggedTrackIndex_, i)) {
                dropTargetType_ = DropTargetType::OntoGroup;
                dropTargetIndex_ = i;
                return;
            }
        }
    }

    // Check if mouse is below all tracks
    int totalHeight = getTotalTracksHeight();
    if (mouseY > totalHeight && !trackHeaders.empty()) {
        dropTargetType_ = DropTargetType::BetweenTracks;
        dropTargetIndex_ = static_cast<int>(trackHeaders.size());
    }
}

bool TrackHeadersPanel::canDropIntoGroup(int draggedIndex, int targetGroupIndex) const {
    if (draggedIndex < 0 || targetGroupIndex < 0)
        return false;
    if (draggedIndex >= static_cast<int>(trackHeaders.size()) ||
        targetGroupIndex >= static_cast<int>(trackHeaders.size()))
        return false;

    // Can't drop onto self
    if (draggedIndex == targetGroupIndex)
        return false;

    // Target must be a group
    if (!trackHeaders[targetGroupIndex]->isGroup)
        return false;

    // If dragging a group, can't drop into its own descendants
    const auto& draggedHeader = *trackHeaders[draggedIndex];
    if (draggedHeader.isGroup) {
        auto& trackManager = TrackManager::getInstance();
        auto descendants = trackManager.getAllDescendants(draggedHeader.trackId);
        TrackId targetId = trackHeaders[targetGroupIndex]->trackId;
        if (std::find(descendants.begin(), descendants.end(), targetId) != descendants.end()) {
            return false;
        }
    }

    return true;
}

void TrackHeadersPanel::executeDrop() {
    if (draggedTrackIndex_ < 0 || dropTargetType_ == DropTargetType::None)
        return;

    auto& trackManager = TrackManager::getInstance();

    // Collect tracks to move: if dragged track is part of multi-selection, move all selected
    // tracks in display order; otherwise move just the single dragged track
    bool isMultiDrag =
        selectedTrackIndices_.size() > 1 && selectedTrackIndices_.count(draggedTrackIndex_) > 0;

    std::vector<TrackId> tracksToMove;
    if (isMultiDrag) {
        // Collect selected track IDs in display order (ascending index)
        std::vector<int> sortedIndices(selectedTrackIndices_.begin(), selectedTrackIndices_.end());
        std::sort(sortedIndices.begin(), sortedIndices.end());
        for (int idx : sortedIndices) {
            if (idx >= 0 && idx < static_cast<int>(trackHeaders.size()))
                tracksToMove.push_back(trackHeaders[idx]->trackId);
        }
    } else {
        TrackId draggedTrackId = trackHeaders[draggedTrackIndex_]->trackId;
        tracksToMove.push_back(draggedTrackId);
    }

    if (tracksToMove.empty())
        return;

    // Verify all tracks exist
    for (auto tid : tracksToMove) {
        if (!trackManager.getTrack(tid))
            return;
    }

    if (isMultiDrag)
        UndoManager::getInstance().beginCompoundOperation("Move Tracks");

    if (dropTargetType_ == DropTargetType::BetweenTracks && dropTargetIndex_ >= 0) {
        // Determine the target parent based on drop position
        TrackId targetParentId = INVALID_TRACK_ID;

        if (dropTargetIndex_ < static_cast<int>(visibleTrackIds_.size())) {
            TrackId targetTrackId = visibleTrackIds_[dropTargetIndex_];
            const auto* targetTrack = trackManager.getTrack(targetTrackId);
            if (targetTrack) {
                targetParentId = targetTrack->parentId;
            }
        } else if (!visibleTrackIds_.empty()) {
            TrackId lastTrackId = visibleTrackIds_.back();
            const auto* lastTrack = trackManager.getTrack(lastTrackId);
            if (lastTrack) {
                targetParentId = lastTrack->parentId;
            }
        }

        // Calculate the initial target position in TrackManager order
        int baseTargetIndex;
        if (dropTargetIndex_ >= static_cast<int>(visibleTrackIds_.size())) {
            baseTargetIndex = trackManager.getNumTracks();
        } else {
            TrackId targetTrackId = visibleTrackIds_[dropTargetIndex_];
            baseTargetIndex = trackManager.getTrackIndex(targetTrackId);
        }

        // Move each track in display order, adjusting target index as we go
        int insertAt = baseTargetIndex;
        for (auto trackId : tracksToMove) {
            const auto* track = trackManager.getTrack(trackId);
            if (!track)
                continue;

            // Update group membership if needed
            if (track->parentId != targetParentId) {
                trackManager.removeTrackFromGroup(trackId);
                if (targetParentId != INVALID_TRACK_ID) {
                    trackManager.addTrackToGroup(trackId, targetParentId);
                }
            }

            // Adjust insertion index: if track is currently above insertAt, removing it
            // shifts insertAt down by one
            int currentIndex = trackManager.getTrackIndex(trackId);
            int adjustedTarget = insertAt;
            if (currentIndex < adjustedTarget) {
                adjustedTarget--;
            }

            trackManager.moveTrack(trackId, adjustedTarget);

            // Next track goes after this one
            insertAt = trackManager.getTrackIndex(trackId) + 1;
        }
    } else if (dropTargetType_ == DropTargetType::OntoGroup && dropTargetIndex_ >= 0) {
        TrackId groupId = trackHeaders[dropTargetIndex_]->trackId;
        for (auto trackId : tracksToMove) {
            trackManager.addTrackToGroup(trackId, groupId);
        }
    }

    if (isMultiDrag)
        UndoManager::getInstance().endCompoundOperation();

    // TrackManager will notify listeners which triggers tracksChanged()
}

void TrackHeadersPanel::resetDragState() {
    isDraggingToReorder_ = false;
    draggedTrackIndex_ = -1;
    dragStartX_ = 0;
    dragStartY_ = 0;
    currentDragY_ = 0;
    dropTargetType_ = DropTargetType::None;
    dropTargetIndex_ = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void TrackHeadersPanel::paintDragFeedback(juce::Graphics& g) {
    if (!isDraggingToReorder_ || draggedTrackIndex_ < 0)
        return;

    // Draw semi-transparent overlay on all dragged tracks
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
    bool isMultiDrag =
        selectedTrackIndices_.size() > 1 && selectedTrackIndices_.count(draggedTrackIndex_) > 0;
    if (isMultiDrag) {
        for (int idx : selectedTrackIndices_) {
            g.fillRect(getTrackHeaderArea(idx));
        }
    } else {
        g.fillRect(getTrackHeaderArea(draggedTrackIndex_));
    }

    // Draw appropriate drop indicator
    if (dropTargetType_ == DropTargetType::BetweenTracks) {
        paintDropIndicatorLine(g);
    } else if (dropTargetType_ == DropTargetType::OntoGroup) {
        paintDropTargetGroupHighlight(g);
    }
}

void TrackHeadersPanel::paintDropIndicatorLine(juce::Graphics& g) {
    if (dropTargetIndex_ < 0)
        return;

    int indicatorY;
    if (dropTargetIndex_ >= static_cast<int>(trackHeaders.size())) {
        // At the end
        indicatorY = getTotalTracksHeight();
    } else {
        indicatorY = getTrackYPosition(dropTargetIndex_);
    }

    // Draw cyan line with arrow indicators
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));

    // Main line
    g.fillRect(0, indicatorY - 2, getWidth(), 4);

    // Arrow on left side
    juce::Path leftArrow;
    leftArrow.addTriangle(0, indicatorY - 6, 12, indicatorY, 0, indicatorY + 6);
    g.fillPath(leftArrow);

    // Arrow on right side
    juce::Path rightArrow;
    rightArrow.addTriangle(getWidth(), indicatorY - 6, getWidth() - 12, indicatorY, getWidth(),
                           indicatorY + 6);
    g.fillPath(rightArrow);
}

void TrackHeadersPanel::paintDropTargetGroupHighlight(juce::Graphics& g) {
    if (dropTargetIndex_ < 0 || dropTargetIndex_ >= static_cast<int>(trackHeaders.size()))
        return;

    auto targetArea = getTrackHeaderArea(dropTargetIndex_);

    // Draw orange border around the group
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    g.drawRect(targetArea, 3);

    // Draw subtle fill
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.15f));
    g.fillRect(targetArea);
}

void TrackHeadersPanel::showAutomationMenu(TrackId trackId, juce::Component* relativeTo) {
    auto& automationManager = AutomationManager::getInstance();

    juce::PopupMenu menu;
    menu.addSectionHeader("Show Automation Lane");
    menu.addSeparator();

    // Get existing lanes for this track
    auto existingLanes = automationManager.getLanesForTrack(trackId);

    // Add existing lanes first (with toggle indicator)
    if (!existingLanes.empty()) {
        for (auto laneId : existingLanes) {
            const auto* lane = automationManager.getLane(laneId);
            if (lane) {
                juce::String name = lane->target.getDisplayName();
                bool isVisible = lane->visible;
                menu.addItem(1000 + laneId, name, true, isVisible);
            }
        }
        menu.addSeparator();
    }

    // "Add New Lane" submenu with common targets
    juce::PopupMenu addNewMenu;

    // Track volume
    AutomationTarget volumeTarget;
    volumeTarget.type = AutomationTargetType::TrackVolume;
    volumeTarget.trackId = trackId;
    addNewMenu.addItem(1, "Track Volume");

    // Track pan
    AutomationTarget panTarget;
    panTarget.type = AutomationTargetType::TrackPan;
    panTarget.trackId = trackId;
    addNewMenu.addItem(2, "Track Pan");

    menu.addSubMenu("Add New Lane...", addNewMenu);

    // Show menu
    auto options = juce::PopupMenu::Options();
    if (relativeTo) {
        options = options.withTargetComponent(relativeTo);
    }

    menu.showMenuAsync(options, [this, trackId](int result) {
        if (result == 0)
            return;

        auto& automationManager = AutomationManager::getInstance();

        if (result >= 1000) {
            // Toggle existing lane visibility
            AutomationLaneId laneId = result - 1000;
            const auto* lane = automationManager.getLane(laneId);
            if (lane) {
                bool newVisible = !lane->visible;
                // Defer visibility change to avoid destroying listeners during notification loop
                juce::MessageManager::callAsync([laneId, newVisible]() {
                    AutomationManager::getInstance().setLaneVisible(laneId, newVisible);
                });
            }
        } else if (result == 1) {
            // Create track volume automation lane
            AutomationTarget target;
            target.type = AutomationTargetType::TrackVolume;
            target.trackId = trackId;
            auto laneId = automationManager.getOrCreateLane(target, AutomationLaneType::Absolute);
            automationManager.setLaneVisible(laneId, true);
            if (onShowAutomationLane) {
                onShowAutomationLane(trackId, laneId);
            }
        } else if (result == 2) {
            // Create track pan automation lane
            AutomationTarget target;
            target.type = AutomationTargetType::TrackPan;
            target.trackId = trackId;
            auto laneId = automationManager.getOrCreateLane(target, AutomationLaneType::Absolute);
            automationManager.setLaneVisible(laneId, true);
            if (onShowAutomationLane) {
                onShowAutomationLane(trackId, laneId);
            }
        }
    });
}

// ============================================================================
// Automation Lane Header Painting
// ============================================================================

void TrackHeadersPanel::paintAutomationLaneHeaders(juce::Graphics& g, int trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(visibleTrackIds_.size())) {
        return;
    }

    TrackId trackId = visibleTrackIds_[trackIndex];
    auto it = visibleAutomationLanes_.find(trackId);
    if (it == visibleAutomationLanes_.end() || it->second.empty()) {
        return;
    }

    auto& manager = AutomationManager::getInstance();

    // Calculate Y position: after track header
    int y = getTrackYPosition(trackIndex) +
            static_cast<int>(trackHeaders[trackIndex]->height * verticalZoom);

    for (auto laneId : it->second) {
        const auto* lane = manager.getLane(laneId);
        if (!lane || !lane->visible) {
            continue;
        }

        // Calculate lane height (same as in getVisibleAutomationLanesHeight)
        int laneHeight = lane->expanded ? (AutomationLaneComponent::HEADER_HEIGHT +
                                           static_cast<int>(lane->height * verticalZoom) +
                                           AutomationLaneComponent::RESIZE_HANDLE_HEIGHT)
                                        : AutomationLaneComponent::HEADER_HEIGHT;

        // Header area for this automation lane
        auto headerArea =
            juce::Rectangle<int>(0, y, getWidth(), AutomationLaneComponent::HEADER_HEIGHT);

        // Header background
        g.setColour(juce::Colour(0xFF252525));
        g.fillRect(headerArea);

        // Header border
        g.setColour(juce::Colour(0xFF333333));
        g.drawHorizontalLine(headerArea.getBottom() - 1, static_cast<float>(headerArea.getX()),
                             static_cast<float>(headerArea.getRight()));

        // Parameter name
        g.setColour(juce::Colour(0xFFCCCCCC));
        g.setFont(11.0f);
        auto nameArea = headerArea.reduced(4, 2);
        g.drawText(lane->getDisplayName(), nameArea, juce::Justification::centredLeft);

        y += laneHeight;
    }
}

// =============================================================================
// Keyboard Handling
// =============================================================================

bool TrackHeadersPanel::keyPressed(const juce::KeyPress& key) {
    // Forward all keys up the parent chain so shortcuts (delete, Cmd+D, etc.)
    // reach MainComponent's command handler.
    auto* parent = getParentComponent();
    while (parent != nullptr) {
        if (parent->keyPressed(key))
            return true;
        parent = parent->getParentComponent();
    }
    return false;
}

// =============================================================================
// Plugin Drag-and-Drop Implementation (DragAndDropTarget)
// =============================================================================

bool TrackHeadersPanel::isInterestedInDragSource(const SourceDetails& details) {
    if (auto* obj = details.description.getDynamicObject()) {
        return obj->getProperty("type").toString() == "plugin";
    }
    return false;
}

void TrackHeadersPanel::itemDragEnter(const SourceDetails& details) {
    pluginDropTrackIndex_ = -1;
    for (int i = 0; i < static_cast<int>(trackHeaders.size()); ++i) {
        if (getTrackHeaderArea(i).contains(details.localPosition)) {
            pluginDropTrackIndex_ = i;
            break;
        }
    }
    repaint();
}

void TrackHeadersPanel::itemDragMove(const SourceDetails& details) {
    int prev = pluginDropTrackIndex_;
    pluginDropTrackIndex_ = -1;
    for (int i = 0; i < static_cast<int>(trackHeaders.size()); ++i) {
        if (getTrackHeaderArea(i).contains(details.localPosition)) {
            pluginDropTrackIndex_ = i;
            break;
        }
    }
    if (pluginDropTrackIndex_ != prev)
        repaint();
}

void TrackHeadersPanel::itemDragExit(const SourceDetails& /*details*/) {
    pluginDropTrackIndex_ = -1;
    repaint();
}

void TrackHeadersPanel::itemDropped(const SourceDetails& details) {
    auto* obj = details.description.getDynamicObject();
    if (!obj) {
        pluginDropTrackIndex_ = -1;
        repaint();
        return;
    }

    // Determine which track header was dropped on
    int targetIndex = -1;
    for (int i = 0; i < static_cast<int>(trackHeaders.size()); ++i) {
        if (getTrackHeaderArea(i).contains(details.localPosition)) {
            targetIndex = i;
            break;
        }
    }

    auto device = TrackManager::deviceInfoFromPluginObject(*obj);

    if (targetIndex >= 0 && targetIndex < static_cast<int>(visibleTrackIds_.size())) {
        // Dropped on existing track header → add plugin to that track
        TrackId trackId = visibleTrackIds_[targetIndex];
        auto cmd = std::make_unique<AddDeviceToTrackCommand>(trackId, device);
        UndoManager::getInstance().executeCommand(std::move(cmd));
        TrackManager::getInstance().setSelectedTrack(trackId);

        DBG("Dropped plugin on track header: " << juce::String(device.name) << " → track "
                                               << trackId);
    } else {
        // Dropped on empty area → create new track with plugin
        TrackType trackType = TrackType::Audio;
        juce::String pluginName = obj->getProperty("name").toString();
        auto cmd = std::make_unique<CreateTrackWithDeviceCommand>(pluginName, trackType, device);
        UndoManager::getInstance().executeCommand(std::move(cmd));
    }

    pluginDropTrackIndex_ = -1;
    repaint();
}

bool TrackHeadersPanel::isIORoutingVisible() const {
    if (trackHeaders.empty())
        return showIORouting_;
    for (auto& h : trackHeaders) {
        if (h->showIORouting)
            return true;
    }
    return false;
}

void TrackHeadersPanel::toggleIORouting() {
    // If any track has I/O visible, hide all; otherwise show all
    bool anyVisible = false;
    for (auto& h : trackHeaders) {
        if (h->showIORouting) {
            anyVisible = true;
            break;
        }
    }
    for (auto& h : trackHeaders) {
        h->showIORouting = !anyVisible;
    }
    showIORouting_ = !anyVisible;
    resized();
}

}  // namespace magda
