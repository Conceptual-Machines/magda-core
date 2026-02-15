#include "TrackHeadersPanel.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

#include "../../../audio/AudioBridge.hpp"
#include "../../../audio/MidiBridge.hpp"
#include "../../../core/SelectionManager.hpp"
#include "../../../core/TrackCommands.hpp"
#include "../../../core/UndoManager.hpp"
#include "../../../engine/TracktionEngineWrapper.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
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
    // -12 dB → ~38%, 0 dB → ~75%, +6 dB → 100%
    return std::pow(normalized, 3.0f);
}

// Simple stereo level meter component for track headers
class TrackMeter : public juce::Component {
  public:
    TrackMeter() = default;

    void setLevels(float left, float right) {
        // Allow up to 2.0 gain (+6 dB) for proper headroom display
        levelL_ = juce::jlimit(0.0f, 2.0f, left);
        levelR_ = juce::jlimit(0.0f, 2.0f, right);
        repaint();
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
        drawMeterBar(g, leftBar, levelL_);
        drawMeterBar(g, rightBar, levelR_);
    }

  private:
    float levelL_ = 0.0f;
    float levelR_ = 0.0f;

    void drawMeterBar(juce::Graphics& g, juce::Rectangle<float> bounds, float level) {
        if (level <= 0.0f)
            return;

        // Convert gain to dB, then to meter position (linear dB scaling)
        float db = gainToDb(level);
        float meterPos = dbToMeterPos(db);
        float fillHeight = bounds.getHeight() * meterPos;
        auto fillBounds = bounds.removeFromBottom(fillHeight);

        // Color gradient based on dB: green -> yellow -> red
        // Green: < -12 dB, Yellow: -12 to 0 dB, Red: 0 to +12 dB
        juce::Colour color;
        if (db < -12.0f) {
            color = juce::Colour(0xFF55AA55);  // Green (safe zone)
        } else if (db < 0.0f) {
            // Transition from green to yellow
            float t = (db + 12.0f) / 12.0f;
            color = juce::Colour(0xFF55AA55).interpolatedWith(juce::Colour(0xFFAAAA55), t);
        } else if (db < 6.0f) {
            // Transition from yellow/orange to red
            float t = db / 6.0f;
            color = juce::Colour(0xFFAAAA55).interpolatedWith(juce::Colour(0xFFAA5555), t);
        } else {
            color = juce::Colour(0xFFAA5555);  // Red (clipping zone above +6 dB)
        }

        g.setColour(color);
        g.fillRoundedRectangle(fillBounds, 1.0f);
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
}  // namespace

TrackHeadersPanel::TrackHeader::TrackHeader(const juce::String& trackName) : name(trackName) {
    // Create UI components
    nameLabel = std::make_unique<juce::Label>("trackName", trackName);
    nameLabel->setEditable(true);
    nameLabel->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    nameLabel->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    nameLabel->setFont(FontManager::getInstance().getUIFont(12.0f));

    muteButton = std::make_unique<juce::TextButton>("M");
    muteButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                                  juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
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
    soloButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                                  juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
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
    recordButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                                    juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
    recordButton->setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    recordButton->setColour(juce::TextButton::buttonOnColourId,
                            DarkTheme::getColour(DarkTheme::STATUS_ERROR));  // Red when armed
    recordButton->setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    recordButton->setColour(juce::TextButton::textColourOnId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    recordButton->setClickingTogglesState(true);

    // Automation button (bezier curve icon)
    automationButton = std::make_unique<SvgButton>("Automation", BinaryData::bezier_svg,
                                                   BinaryData::bezier_svgSize);
    automationButton->setTooltip("Show automation lanes");
    automationButton->setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
    automationButton->setColour(juce::TextButton::buttonOnColourId,
                                DarkTheme::getColour(DarkTheme::ACCENT_BLUE));

    // Volume label (shows dB, draggable)
    volumeLabel = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Decibels);
    volumeLabel->setRange(MIN_DB, MAX_DB, 0.0);  // -60 to +6 dB, default 0 dB
    volumeLabel->setValue(gainToDb(volume), juce::dontSendNotification);

    // Pan label (shows L/C/R, draggable)
    panLabel = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Pan);
    panLabel->setRange(-1.0, 1.0, 0.0);  // -1 (L) to +1 (R), default center
    panLabel->setValue(pan, juce::dontSendNotification);

    // Collapse button for groups (triangle indicator)
    collapseButton = std::make_unique<juce::TextButton>();
    collapseButton->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    collapseButton->setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    collapseButton->setColour(juce::TextButton::textColourOffId,
                              DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));

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
}

TrackHeadersPanel::TrackHeadersPanel(AudioEngine* audioEngine) : audioEngine_(audioEngine) {
    std::cout << "TrackHeadersPanel created with audioEngine=" << (audioEngine ? "valid" : "NULL")
              << std::endl;
    setSize(TRACK_HEADER_WIDTH, 400);

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Register as ViewModeController listener
    ViewModeController::getInstance().addListener(this);
    currentViewMode_ = ViewModeController::getInstance().getViewMode();

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

        // Check for new MIDI note-on (counter comparison)
        auto counter = bridge->getMidiActivityCounter(header->trackId);
        if (counter != header->lastMidiCounter) {
            header->lastMidiCounter = counter;
            header->midiActivity = 1.0f;
            header->midiHoldFrames = 4;  // Hold bright for ~130ms at 30fps
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

void TrackHeadersPanel::populateAudioInputOptions(RoutingSelector* selector) {
    if (!selector || !audioEngine_)
        return;
    auto* deviceManager = audioEngine_->getDeviceManager();
    if (!deviceManager)
        return;
    RoutingSyncHelper::populateAudioInputOptions(selector, deviceManager->getCurrentAudioDevice());
}

void TrackHeadersPanel::populateAudioOutputOptions(RoutingSelector* selector,
                                                   TrackId currentTrackId) {
    if (!selector || !audioEngine_)
        return;
    auto* deviceManager = audioEngine_->getDeviceManager();
    if (!deviceManager)
        return;
    RoutingSyncHelper::populateAudioOutputOptions(
        selector, currentTrackId, deviceManager->getCurrentAudioDevice(), outputTrackMapping_);
}

void TrackHeadersPanel::populateMidiInputOptions(RoutingSelector* selector) {
    if (!selector || !audioEngine_)
        return;
    RoutingSyncHelper::populateMidiInputOptions(selector, audioEngine_->getMidiBridge());
}

void TrackHeadersPanel::populateMidiOutputOptions(RoutingSelector* selector) {
    if (!selector || !audioEngine_)
        return;
    RoutingSyncHelper::populateMidiOutputOptions(selector, audioEngine_->getMidiBridge());
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
            TrackManager::getInstance().setTrackAudioInput(trackId, "default");
        } else {
            TrackManager::getInstance().setTrackAudioInput(trackId, "");
        }
    };

    header.audioInputSelector->onSelectionChanged = [this, trackId](int selectedId) {
        if (selectedId == 1) {
            TrackManager::getInstance().setTrackAudioInput(trackId, "");
        } else if (selectedId >= 10) {
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

    header.inputSelector->onSelectionChanged = [this, trackId, midiBridge](int selectedId) {
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
    header.outputSelector->onEnabledChanged = [this, trackId](bool enabled) {
        if (enabled) {
            TrackManager::getInstance().setTrackAudioOutput(trackId, "master");
        } else {
            TrackManager::getInstance().setTrackAudioOutput(trackId, "");
        }
    };

    // Capture outputTrackMapping_ by value so each header has its own snapshot
    // (the shared member is rebuilt per-header in populateAudioOutputOptions)
    header.outputSelector->onSelectionChanged = [this, trackId,
                                                 mapping = outputTrackMapping_](int selectedId) {
        if (selectedId == 1) {
            // Master
            TrackManager::getInstance().setTrackAudioOutput(trackId, "master");
        } else if (selectedId == 2) {
            // None
            TrackManager::getInstance().setTrackAudioOutput(trackId, "");
        } else if (selectedId >= 200 && selectedId < 400) {
            // Group or Aux track destination
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
    header.midiOutputSelector->onEnabledChanged = [this, trackId, midiBridge](bool enabled) {
        if (!enabled) {
            TrackManager::getInstance().setTrackMidiOutput(trackId, "");
        }
        // When enabling, don't set anything yet — user picks a device from dropdown
    };

    header.midiOutputSelector->onSelectionChanged = [this, trackId, midiBridge](int selectedId) {
        if (selectedId == 1) {
            // None
            TrackManager::getInstance().setTrackMidiOutput(trackId, "");
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
    selectedTrackIndex = -1;

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
        header->isCollapsed = track->isCollapsedIn(currentViewMode_);
        header->muted = track->muted;
        header->solo = track->soloed;
        header->volume = track->volume;
        header->pan = track->pan;

        // Use height from view settings
        header->height = track->viewSettings.getHeight(currentViewMode_);

        // Set up callbacks with track ID (not index)
        setupTrackHeaderWithId(*header, trackId);

        // Add components
        addAndMakeVisible(*header->nameLabel);
        addAndMakeVisible(*header->muteButton);
        addAndMakeVisible(*header->soloButton);
        addAndMakeVisible(*header->recordButton);
        addAndMakeVisible(*header->automationButton);
        addAndMakeVisible(*header->volumeLabel);
        addAndMakeVisible(*header->panLabel);
        addAndMakeVisible(*header->audioInputSelector);
        addAndMakeVisible(*header->inputSelector);
        addAndMakeVisible(*header->outputSelector);
        addAndMakeVisible(*header->midiOutputSelector);
        for (auto& sendLabel : header->sendLabels) {
            addChildComponent(*sendLabel);  // Hidden by default; shown when track has sends
        }
        addAndMakeVisible(*header->meterComponent);
        addAndMakeVisible(*header->midiIndicator);

        // Add collapse button for groups and tracks with multi-out children
        if (header->isGroup || track->hasChildren()) {
            header->collapseButton->setButtonText(header->isCollapsed ? "▶" : "▼");
            header->collapseButton->onClick = [this, trackId]() { handleCollapseToggle(trackId); };
            addAndMakeVisible(*header->collapseButton);
        }

        // Update UI state
        header->muteButton->setToggleState(track->muted, juce::dontSendNotification);
        header->soloButton->setToggleState(track->soloed, juce::dontSendNotification);
        header->recordButton->setToggleState(track->recordArmed, juce::dontSendNotification);
        header->volumeLabel->setValue(gainToDb(track->volume), juce::dontSendNotification);
        header->panLabel->setValue(track->pan, juce::dontSendNotification);

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
        header.volume = track->volume;
        header.pan = track->pan;

        // Note: Don't update height here - height should only change via:
        // 1. tracksChanged() (initial load)
        // 2. setTrackHeight() (user resize)
        // Updating height on every property change would reset user's resize

        header.nameLabel->setText(track->name, juce::dontSendNotification);
        header.muteButton->setToggleState(track->muted, juce::dontSendNotification);
        header.soloButton->setToggleState(track->soloed, juce::dontSendNotification);
        header.volumeLabel->setValue(gainToDb(track->volume), juce::dontSendNotification);
        header.panLabel->setValue(track->pan, juce::dontSendNotification);

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
    RoutingSyncHelper::syncSelectorsFromTrack(
        *track, header.audioInputSelector.get(), header.inputSelector.get(),
        header.outputSelector.get(), header.midiOutputSelector.get(), audioEngine_->getMidiBridge(),
        device, header.trackId, outputTrackMapping_);
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
                             static_cast<int>(i) == selectedTrackIndex);

            // Draw resize handle
            auto resizeArea = getResizeHandleArea(static_cast<int>(i));
            paintResizeHandle(g, resizeArea);
        }

        // Draw automation lane headers for this track
        paintAutomationLaneHeaders(g, static_cast<int>(i));
    }

    // Draw drag-and-drop feedback on top
    paintDragFeedback(g);
}

void TrackHeadersPanel::resized() {
    updateTrackHeaderLayout();
}

void TrackHeadersPanel::selectTrack(int index) {
    if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
        selectedTrackIndex = index;

        // Notify SelectionManager of selection change (which syncs with TrackManager)
        TrackId trackId = trackHeaders[index]->trackId;
        SelectionManager::getInstance().selectTrack(trackId);

        if (onTrackSelected) {
            onTrackSelected(index);
        }

        repaint();
    }
}

int TrackHeadersPanel::getNumTracks() const {
    return static_cast<int>(trackHeaders.size());
}

void TrackHeadersPanel::setTrackHeight(int trackIndex, int height) {
    if (trackIndex >= 0 && trackIndex < trackHeaders.size()) {
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
    if (trackIndex >= 0 && trackIndex < trackHeaders.size()) {
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
        if (trackIndex < trackHeaders.size()) {
            auto& header = *trackHeaders[trackIndex];
            header.name = header.nameLabel->getText();

            if (onTrackNameChanged) {
                onTrackNameChanged(trackIndex, header.name);
            }
        }
    };

    // Mute button callback
    header.muteButton->onClick = [this, trackIndex]() {
        if (trackIndex < trackHeaders.size()) {
            auto& header = *trackHeaders[trackIndex];
            header.muted = header.muteButton->getToggleState();

            if (onTrackMutedChanged) {
                onTrackMutedChanged(trackIndex, header.muted);
            }
        }
    };

    // Solo button callback
    header.soloButton->onClick = [this, trackIndex]() {
        if (trackIndex < trackHeaders.size()) {
            auto& header = *trackHeaders[trackIndex];
            header.solo = header.soloButton->getToggleState();

            if (onTrackSoloChanged) {
                onTrackSoloChanged(trackIndex, header.solo);
            }
        }
    };

    // Volume label callback
    header.volumeLabel->onValueChange = [this, trackIndex]() {
        if (trackIndex < trackHeaders.size()) {
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
        if (trackIndex < trackHeaders.size()) {
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
            TrackManager::getInstance().setTrackName(trackId, header.name);
        }
    };

    // Mute button callback - updates TrackManager
    header.muteButton->onClick = [this, trackId]() {
        int index = TrackManager::getInstance().getTrackIndex(trackId);
        if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[index];
            header.muted = header.muteButton->getToggleState();
            TrackManager::getInstance().setTrackMuted(trackId, header.muted);
        }
    };

    // Solo button callback - updates TrackManager
    header.soloButton->onClick = [this, trackId]() {
        int index = TrackManager::getInstance().getTrackIndex(trackId);
        if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[index];
            header.solo = header.soloButton->getToggleState();
            TrackManager::getInstance().setTrackSoloed(trackId, header.solo);
        }
    };

    // Volume label callback - updates TrackManager
    header.volumeLabel->onValueChange = [this, trackId]() {
        int index = TrackManager::getInstance().getTrackIndex(trackId);
        if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[index];
            // Convert dB to linear gain
            header.volume = dbToGain(static_cast<float>(header.volumeLabel->getValue()));
            TrackManager::getInstance().setTrackVolume(trackId, header.volume);
        }
    };

    // Pan label callback - updates TrackManager
    header.panLabel->onValueChange = [this, trackId]() {
        int index = TrackManager::getInstance().getTrackIndex(trackId);
        if (index >= 0 && index < static_cast<int>(trackHeaders.size())) {
            auto& header = *trackHeaders[index];
            header.pan = static_cast<float>(header.panLabel->getValue());
            TrackManager::getInstance().setTrackPan(trackId, header.pan);
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

    // Automation button callback - shows automation lane menu
    header.automationButton->onClick = [this, trackId, &header]() {
        showAutomationMenu(trackId, header.automationButton.get());
    };

    // Create send labels from actual track sends
    rebuildSendLabels(header, trackId);

    // Populate all routing selectors

    populateAudioInputOptions(header.audioInputSelector.get());
    populateMidiInputOptions(header.inputSelector.get());
    populateAudioOutputOptions(header.outputSelector.get(), trackId);
    populateMidiOutputOptions(header.midiOutputSelector.get());

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
        label->onValueChange = [this, trackId, busIndex, &header]() {
            // Find the label index by matching bus index
            const auto* track = TrackManager::getInstance().getTrack(trackId);
            if (!track)
                return;
            for (size_t i = 0; i < header.sendLabels.size() && i < track->sends.size(); ++i) {
                if (track->sends[i].busIndex == busIndex) {
                    float newLevel = dbToGain(static_cast<float>(header.sendLabels[i]->getValue()));
                    TrackManager::getInstance().setSendLevel(trackId, busIndex, newLevel);
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

    // Draw indent guide lines for nested tracks
    if (header.depth > 0) {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
        for (int d = 0; d < header.depth; ++d) {
            int x = d * INDENT_WIDTH + INDENT_WIDTH / 2;
            g.drawLine(x, area.getY(), x, area.getBottom(), 1.0f);
        }
    }

    // Background - groups have slightly different color
    auto bgArea = area.withTrimmedLeft(indent);
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

    // Group indicator color strip on the left
    if (header.isGroup) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.7f));
        g.fillRect(bgArea.getX(), bgArea.getY(), 3, bgArea.getHeight());
    }
}

void TrackHeadersPanel::paintResizeHandle(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.fillRect(area);

    // Draw resize grip
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    int centerY = area.getCentreY();
    for (int i = 0; i < 3; ++i) {
        int x = area.getX() + 5 + i * 3;
        g.drawLine(x, centerY - 1, x, centerY + 1, 1.0f);
    }
}

juce::Rectangle<int> TrackHeadersPanel::getTrackHeaderArea(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= trackHeaders.size()) {
        return {};
    }

    int yPosition = getTrackYPosition(trackIndex);
    int height = static_cast<int>(trackHeaders[trackIndex]->height * verticalZoom);

    return juce::Rectangle<int>(0, yPosition, getWidth(), height - RESIZE_HANDLE_HEIGHT);
}

juce::Rectangle<int> TrackHeadersPanel::getResizeHandleArea(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= trackHeaders.size()) {
        return {};
    }

    int yPosition = getTrackYPosition(trackIndex);
    int height = static_cast<int>(trackHeaders[trackIndex]->height * verticalZoom);

    return juce::Rectangle<int>(0, yPosition + height - RESIZE_HANDLE_HEIGHT, getWidth(),
                                RESIZE_HANDLE_HEIGHT);
}

bool TrackHeadersPanel::isResizeHandleArea(const juce::Point<int>& point, int& trackIndex) const {
    for (int i = 0; i < trackHeaders.size(); ++i) {
        if (getResizeHandleArea(i).contains(point)) {
            trackIndex = i;
            return true;
        }
    }
    return false;
}

void TrackHeadersPanel::updateTrackHeaderLayout() {
    for (size_t i = 0; i < trackHeaders.size(); ++i) {
        auto& header = *trackHeaders[i];
        auto headerArea = getTrackHeaderArea(static_cast<int>(i));

        if (!headerArea.isEmpty()) {
            // Dynamic layout based on track height
            // Large (>80px): name, M R fader pan, S input, meters
            // Medium (60-80px): name + M R S, fader pan, meters
            // Small (<60px): name + M S R only, meters

            const int meterWidth = 20;
            const int midiIndicatorWidth = 12;
            const int meterPadding = 4;
            const int trackHeight = headerArea.getHeight();

            // Extract meters area on the right (full height)
            auto workArea = headerArea.reduced(4);
            auto meterArea = workArea.removeFromRight(meterWidth);
            workArea.removeFromRight(2);

            // MIDI indicator to the left of audio meters
            auto midiArea = workArea.removeFromRight(midiIndicatorWidth);
            workArea.removeFromRight(meterPadding);

            // Audio meter spans full track height
            header.meterComponent->setBounds(meterArea);
            header.meterComponent->setVisible(true);

            // MIDI indicator spans full track height
            header.midiIndicator->setBounds(midiArea);
            header.midiIndicator->setVisible(header.inputSelector &&
                                             header.inputSelector->isEnabled());
            header.midiIndicator->toFront(false);  // Ensure it's on top

            // Apply indentation based on depth for TCP area
            int indent = header.depth * INDENT_WIDTH;
            auto tcpArea = workArea.withTrimmedLeft(indent);

            // Constants
            const int nameRowHeight = 18;
            const int rowHeight = 16;
            const int smallButtonSize = 16;
            const int spacing = 2;

            // Top row: collapse button (if group) + name label
            auto topRow = tcpArea.removeFromTop(nameRowHeight);

            if (header.isGroup) {
                header.collapseButton->setBounds(topRow.removeFromLeft(COLLAPSE_BUTTON_SIZE));
                topRow.removeFromLeft(2);
                header.collapseButton->setVisible(true);
            } else {
                header.collapseButton->setVisible(false);
            }

            header.nameLabel->setBounds(topRow);
            tcpArea.removeFromTop(3);

            // Helper to hide all routing selectors and sends
            auto hideAllRouting = [&]() {
                header.audioInputSelector->setVisible(false);
                header.inputSelector->setVisible(false);
                header.outputSelector->setVisible(false);
                header.midiOutputSelector->setVisible(false);
                for (auto& sendLabel : header.sendLabels) {
                    sendLabel->setVisible(false);
                }
            };

            // Label widths for draggable values
            const int volumeLabelWidth = 42;  // "-60.0" or "+6.0"
            const int panLabelWidth = 28;     // "L100" or "R100" or "C"
            const int sendLabelWidth = 28;    // "-inf" or "-12"

            if (trackHeight >= 100) {
                // LARGE LAYOUT - fixed 4-row grid, sends use remaining space
                // Order: M S R A, Volume/Pan, Input routing, Output routing
                // Sends appear between Vol/Pan and I/O if present
                const int toggleWidth = 40;
                const int dropdownWidth = 55;
                const int buttonGap = 2;
                const int contentRowHeight = rowHeight - 2;
                const bool hasSends = !header.sendLabels.empty();

                // Always 4 base rows for stable spacing
                constexpr int numRows = 4;

                // Calculate even spacing between rows
                int totalContentHeight = numRows * contentRowHeight;
                if (hasSends)
                    totalContentHeight += contentRowHeight;
                int availableSpace = tcpArea.getHeight() - totalContentHeight;
                int rowGap = numRows > 1
                                 ? std::max(2, availableSpace / (numRows - 1 + (hasSends ? 1 : 0)))
                                 : 2;

                // M S R A buttons row
                auto buttonsRow = tcpArea.removeFromTop(contentRowHeight);
                header.muteButton->setBounds(buttonsRow.removeFromLeft(smallButtonSize));
                buttonsRow.removeFromLeft(buttonGap);
                header.soloButton->setBounds(buttonsRow.removeFromLeft(smallButtonSize));
                buttonsRow.removeFromLeft(buttonGap);
                if (!header.isMultiOut) {
                    header.recordButton->setBounds(buttonsRow.removeFromLeft(smallButtonSize));
                    header.recordButton->setVisible(true);
                    buttonsRow.removeFromLeft(buttonGap);
                } else {
                    header.recordButton->setVisible(false);
                }
                header.automationButton->setBounds(buttonsRow.removeFromLeft(smallButtonSize));
                header.automationButton->setVisible(true);

                tcpArea.removeFromTop(rowGap);

                // Volume / Pan row
                auto mixRow = tcpArea.removeFromTop(contentRowHeight);

                header.volumeLabel->setBounds(mixRow.removeFromLeft(volumeLabelWidth));
                header.volumeLabel->setVisible(true);
                mixRow.removeFromLeft(spacing);

                header.panLabel->setBounds(mixRow.removeFromLeft(panLabelWidth));
                header.panLabel->setVisible(true);

                // Sends row (only if track has sends — squeezed in without affecting base rows)
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

                // Input row: [Audio In] [MIDI In] — hidden for multi-out child tracks
                auto inputRow = tcpArea.removeFromTop(contentRowHeight);
                if (!header.isMultiOut) {
                    header.audioInputSelector->setBounds(inputRow.removeFromLeft(dropdownWidth));
                    header.audioInputSelector->setVisible(true);
                    inputRow.removeFromLeft(spacing);
                    header.inputSelector->setBounds(inputRow.removeFromLeft(dropdownWidth));
                    header.inputSelector->setVisible(true);
                } else {
                    header.audioInputSelector->setVisible(false);
                    header.inputSelector->setVisible(false);
                }
                tcpArea.removeFromTop(rowGap);

                // Output row: [Audio Out] [MIDI Out]
                auto outputRow = tcpArea.removeFromTop(contentRowHeight);
                header.outputSelector->setBounds(outputRow.removeFromLeft(dropdownWidth));
                header.outputSelector->setVisible(true);
                outputRow.removeFromLeft(spacing);
                header.midiOutputSelector->setBounds(outputRow.removeFromLeft(dropdownWidth));
                header.midiOutputSelector->setVisible(true);

            } else if (trackHeight >= 55) {
                // MEDIUM LAYOUT: Buttons + volume/pan only
                // Row 1: M S R A [volume] [pan]
                auto row1 = tcpArea.removeFromTop(rowHeight);
                header.muteButton->setBounds(row1.removeFromLeft(smallButtonSize));
                row1.removeFromLeft(spacing);
                header.soloButton->setBounds(row1.removeFromLeft(smallButtonSize));
                row1.removeFromLeft(spacing);
                if (!header.isMultiOut) {
                    header.recordButton->setBounds(row1.removeFromLeft(smallButtonSize));
                    header.recordButton->setVisible(true);
                    row1.removeFromLeft(spacing);
                } else {
                    header.recordButton->setVisible(false);
                }
                header.automationButton->setBounds(row1.removeFromLeft(smallButtonSize));
                header.automationButton->setVisible(true);
                row1.removeFromLeft(spacing + 2);

                header.volumeLabel->setBounds(row1.removeFromLeft(volumeLabelWidth));
                header.volumeLabel->setVisible(true);
                row1.removeFromLeft(spacing);

                header.panLabel->setBounds(row1.removeFromLeft(panLabelWidth));
                header.panLabel->setVisible(true);

                hideAllRouting();

            } else {
                // SMALL LAYOUT: Buttons + volume/pan on same row
                // Row 1: M S R A [volume] [pan]
                auto row1 = tcpArea.removeFromTop(rowHeight);
                header.muteButton->setBounds(row1.removeFromLeft(smallButtonSize));
                row1.removeFromLeft(spacing);
                header.soloButton->setBounds(row1.removeFromLeft(smallButtonSize));
                row1.removeFromLeft(spacing);
                if (!header.isMultiOut) {
                    header.recordButton->setBounds(row1.removeFromLeft(smallButtonSize));
                    header.recordButton->setVisible(true);
                    row1.removeFromLeft(spacing);
                } else {
                    header.recordButton->setVisible(false);
                }
                header.automationButton->setBounds(row1.removeFromLeft(smallButtonSize));
                header.automationButton->setVisible(true);
                row1.removeFromLeft(spacing + 2);

                header.volumeLabel->setBounds(row1.removeFromLeft(volumeLabelWidth));
                header.volumeLabel->setVisible(true);
                row1.removeFromLeft(spacing);

                header.panLabel->setBounds(row1.removeFromLeft(panLabelWidth));
                header.panLabel->setVisible(true);

                hideAllRouting();
            }
        }
    }
}

void TrackHeadersPanel::mouseDown(const juce::MouseEvent& event) {
    // Handle vertical track height resizing and track selection
    int trackIndex;
    if (isResizeHandleArea(event.getPosition(), trackIndex)) {
        // Start resizing
        isResizing = true;
        resizingTrackIndex = trackIndex;
        resizeStartY = event.y;
        resizeStartHeight = trackHeaders[trackIndex]->height;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    } else {
        // Find which track was clicked
        for (int i = 0; i < static_cast<int>(trackHeaders.size()); ++i) {
            if (getTrackHeaderArea(i).contains(event.getPosition())) {
                selectTrack(i);

                // Right-click shows context menu
                if (event.mods.isPopupMenu()) {
                    showContextMenu(i, event.getPosition());
                } else {
                    // Record potential drag start
                    draggedTrackIndex_ = i;
                    dragStartX_ = event.x;
                    dragStartY_ = event.y;
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
    }
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
    if (trackIndex >= 0 && trackIndex < trackHeaders.size()) {
        trackHeaders[trackIndex]->name = name;
        trackHeaders[trackIndex]->nameLabel->setText(name, juce::dontSendNotification);
    }
}

void TrackHeadersPanel::setTrackMuted(int trackIndex, bool muted) {
    if (trackIndex >= 0 && trackIndex < trackHeaders.size()) {
        trackHeaders[trackIndex]->muted = muted;
        trackHeaders[trackIndex]->muteButton->setToggleState(muted, juce::dontSendNotification);
    }
}

void TrackHeadersPanel::setTrackSolo(int trackIndex, bool solo) {
    if (trackIndex >= 0 && trackIndex < trackHeaders.size()) {
        trackHeaders[trackIndex]->solo = solo;
        trackHeaders[trackIndex]->soloButton->setToggleState(solo, juce::dontSendNotification);
    }
}

void TrackHeadersPanel::setTrackVolume(int trackIndex, float volume) {
    if (trackIndex >= 0 && trackIndex < trackHeaders.size()) {
        trackHeaders[trackIndex]->volume = volume;
        // Convert linear gain to dB
        trackHeaders[trackIndex]->volumeLabel->setValue(gainToDb(volume),
                                                        juce::dontSendNotification);
    }
}

void TrackHeadersPanel::setTrackPan(int trackIndex, float pan) {
    if (trackIndex >= 0 && trackIndex < trackHeaders.size()) {
        trackHeaders[trackIndex]->pan = pan;
        trackHeaders[trackIndex]->panLabel->setValue(pan, juce::dontSendNotification);
    }
}

void TrackHeadersPanel::handleCollapseToggle(TrackId trackId) {
    auto& trackManager = TrackManager::getInstance();
    const auto* track = trackManager.getTrack(trackId);
    if (track && (track->isGroup() || track->hasChildren())) {
        bool currentlyCollapsed = track->isCollapsedIn(currentViewMode_);
        trackManager.setTrackCollapsed(trackId, currentViewMode_, !currentlyCollapsed);
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

    // MultiOut tracks have limited context menu (can't delete/move independently)
    if (track->type == TrackType::MultiOut) {
        menu.addItem(0, "Output track (managed by parent instrument)", false, false);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(
                               juce::Rectangle<int>(position.x, position.y, 1, 1))),
                           nullptr);
        return;
    }

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

    // Add Send submenu (list available aux tracks) — not for aux tracks themselves
    if (track->type != TrackType::Aux) {
        juce::PopupMenu sendMenu;
        const auto& allTracks = trackManager.getTracks();
        int sendItemId = 500;
        bool hasAuxOptions = false;

        for (const auto& t : allTracks) {
            if (t.type != TrackType::Aux)
                continue;
            // Skip aux tracks that already have a send from this track
            bool alreadyConnected = false;
            for (const auto& s : track->sends) {
                if (s.destTrackId == t.id) {
                    alreadyConnected = true;
                    break;
                }
            }
            if (!alreadyConnected) {
                if (t.id >= 100) {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon, "Limit Reached",
                        "Cannot create more than 100 aux tracks.");
                    break;
                }
                sendMenu.addItem(sendItemId + t.id, t.name);
                hasAuxOptions = true;
            }
        }

        if (hasAuxOptions) {
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

    menu.addSeparator();

    // Duplicate track
    menu.addItem(4, "Duplicate Track");
    menu.addItem(5, "Duplicate Track Without Content");

    // Delete track
    menu.addItem(3, "Delete Track");

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

        // Skip self
        if (i == draggedTrackIndex_)
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
    TrackId draggedTrackId = trackHeaders[draggedTrackIndex_]->trackId;
    const auto* draggedTrack = trackManager.getTrack(draggedTrackId);
    if (!draggedTrack)
        return;

    if (dropTargetType_ == DropTargetType::BetweenTracks && dropTargetIndex_ >= 0) {
        // Determine the target parent based on drop position
        TrackId targetParentId = INVALID_TRACK_ID;

        if (dropTargetIndex_ < static_cast<int>(visibleTrackIds_.size())) {
            // Dropping before an existing track - adopt that track's parent
            TrackId targetTrackId = visibleTrackIds_[dropTargetIndex_];
            const auto* targetTrack = trackManager.getTrack(targetTrackId);
            if (targetTrack) {
                targetParentId = targetTrack->parentId;
            }
        } else if (!visibleTrackIds_.empty()) {
            // Dropping at the end - adopt the last track's parent
            TrackId lastTrackId = visibleTrackIds_.back();
            const auto* lastTrack = trackManager.getTrack(lastTrackId);
            if (lastTrack) {
                targetParentId = lastTrack->parentId;
            }
        }

        // Calculate the target position in TrackManager order
        int targetIndex;
        if (dropTargetIndex_ >= static_cast<int>(visibleTrackIds_.size())) {
            // Drop at the end
            targetIndex = trackManager.getNumTracks();
        } else {
            // Get the track at drop target position
            TrackId targetTrackId = visibleTrackIds_[dropTargetIndex_];
            targetIndex = trackManager.getTrackIndex(targetTrackId);
        }

        // Adjust if dragging from above
        int currentIndex = trackManager.getTrackIndex(draggedTrackId);
        if (currentIndex < targetIndex) {
            targetIndex--;
        }

        // Only change group membership if moving to a different parent
        if (draggedTrack->parentId != targetParentId) {
            // Remove from current group
            trackManager.removeTrackFromGroup(draggedTrackId);

            // Add to new group if target has a parent
            if (targetParentId != INVALID_TRACK_ID) {
                trackManager.addTrackToGroup(draggedTrackId, targetParentId);
            }
        }

        // Move to new position
        trackManager.moveTrack(draggedTrackId, targetIndex);
    } else if (dropTargetType_ == DropTargetType::OntoGroup && dropTargetIndex_ >= 0) {
        TrackId groupId = trackHeaders[dropTargetIndex_]->trackId;
        trackManager.addTrackToGroup(draggedTrackId, groupId);
    }

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

    // Draw semi-transparent overlay on dragged track
    auto draggedArea = getTrackHeaderArea(draggedTrackIndex_);
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
    g.fillRect(draggedArea);

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

}  // namespace magda
