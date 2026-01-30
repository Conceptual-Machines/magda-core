#include "TracktionEngineWrapper.hpp"

#include <iostream>

#include "../audio/AudioBridge.hpp"
#include "../audio/MidiBridge.hpp"
#include "../audio/SessionClipScheduler.hpp"
#include "../core/Config.hpp"
#include "../core/DeviceInfo.hpp"
#include "../core/TrackManager.hpp"
#include "MagdaUIBehaviour.hpp"
#include "PluginScanCoordinator.hpp"
#include "PluginWindowManager.hpp"

namespace magda {

TracktionEngineWrapper::TracktionEngineWrapper() = default;

TracktionEngineWrapper::~TracktionEngineWrapper() {
    shutdown();
}

bool TracktionEngineWrapper::initialize() {
    try {
        // Initialize Tracktion Engine with custom UIBehaviour for plugin windows
        auto uiBehaviour = std::make_unique<MagdaUIBehaviour>();
        engine_ = std::make_unique<tracktion::Engine>("MAGDA", std::move(uiBehaviour), nullptr);

        // Register ToneGeneratorPlugin (not registered by default)
        engine_->getPluginManager().createBuiltInType<tracktion::ToneGeneratorPlugin>();

        // Register external plugin formats (VST3, AU)
        auto& pluginManager = engine_->getPluginManager();
        auto& formatManager = pluginManager.pluginFormatManager;

        // Enable out-of-process scanning to prevent plugin crashes from crashing the app
        // This launches the same executable with a special command line to scan each plugin
        pluginManager.setUsesSeparateProcessForScanning(true);
        std::cout << "Enabled out-of-process plugin scanning" << std::endl;

        // Load saved plugin list from persistent storage
        loadPluginList();

        // Note: Tracktion Engine automatically registers plugin formats (VST3, AU, etc.)
        // via PluginManager::initialise() -> addDefaultFormatsToManager()
        // No need to add them manually here
        std::cout << "Plugin formats registered by Tracktion Engine: "
                  << formatManager.getNumFormats() << std::endl;
        for (int i = 0; i < formatManager.getNumFormats(); ++i) {
            auto* format = formatManager.getFormat(i);
            if (format) {
                std::cout << "  Format " << i << ": " << format->getName() << std::endl;
            }
        }

        // Initialize the DeviceManager FIRST - this creates MIDI device wrappers
        // Parameters: default number of input channels, default number of output channels
        auto& dm = engine_->getDeviceManager();

        // Get JUCE's AudioDeviceManager to access device list
        auto& juceDeviceManager = dm.deviceManager;

        // Log available audio device types (CoreAudio on macOS)
        DBG("Available audio device types:");
        for (auto* type : juceDeviceManager.getAvailableDeviceTypes()) {
            DBG("  - " << type->getTypeName());

            // Log devices for each type
            type->scanForDevices();
            auto inputNames = type->getDeviceNames(true);    // inputs
            auto outputNames = type->getDeviceNames(false);  // outputs

            DBG("    Input devices:");
            for (const auto& name : inputNames) {
                DBG("      - " << name);
            }
            DBG("    Output devices:");
            for (const auto& name : outputNames) {
                DBG("      - " << name);
            }
        }

        // Get user's preferred audio device settings from Config
        auto& config = magda::Config::getInstance();
        std::string preferredInputDevice = config.getPreferredInputDevice();
        std::string preferredOutputDevice = config.getPreferredOutputDevice();
        int preferredInputs = config.getPreferredInputChannels();
        int preferredOutputs = config.getPreferredOutputChannels();

        // Initialize DeviceManager with preferred channel counts
        // If preference is 0, use system defaults (0, 2)
        int inputChannels = (preferredInputs > 0) ? preferredInputs : 0;
        int outputChannels = (preferredOutputs > 0) ? preferredOutputs : 2;
        dm.initialise(inputChannels, outputChannels);
        DBG("DeviceManager initialized with " << inputChannels << " input / " << outputChannels
                                              << " output channels");

        // Try to select preferred audio devices if specified
        if (!preferredInputDevice.empty() || !preferredOutputDevice.empty()) {
            auto& deviceTypes = juceDeviceManager.getAvailableDeviceTypes();
            if (!deviceTypes.isEmpty()) {
                auto* deviceType = deviceTypes[0];  // Use first available type (CoreAudio on macOS)
                deviceType->scanForDevices();

                auto outputDevices = deviceType->getDeviceNames(false);  // outputs
                auto inputDevices = deviceType->getDeviceNames(true);    // inputs

                juce::AudioDeviceManager::AudioDeviceSetup setup;
                juceDeviceManager.getAudioDeviceSetup(setup);

                // Set input device if specified
                if (!preferredInputDevice.empty() && inputDevices.contains(preferredInputDevice)) {
                    setup.inputDeviceName = preferredInputDevice;
                    DBG("Found preferred input device: " << preferredInputDevice);
                }

                // Set output device if specified
                if (!preferredOutputDevice.empty() &&
                    outputDevices.contains(preferredOutputDevice)) {
                    setup.outputDeviceName = preferredOutputDevice;
                    DBG("Found preferred output device: " << preferredOutputDevice);
                }

                // Enable channels based on preference
                // Only override channel configuration if user specified a preference (> 0)
                // Otherwise, keep the existing channel setup (device defaults)
                if (preferredInputs > 0) {
                    setup.inputChannels.clear();
                    for (int i = 0; i < preferredInputs; ++i) {
                        setup.inputChannels.setBit(i, true);
                    }
                }

                if (preferredOutputs > 0) {
                    setup.outputChannels.clear();
                    for (int i = 0; i < preferredOutputs; ++i) {
                        setup.outputChannels.setBit(i, true);
                    }
                }

                // Try to set the devices
                auto result = juceDeviceManager.setAudioDeviceSetup(setup, true);
                if (result.isEmpty()) {
                    DBG("Successfully selected preferred devices - Input: "
                        << setup.inputDeviceName << " (" << preferredInputs << " ch), Output: "
                        << setup.outputDeviceName << " (" << preferredOutputs << " ch)");
                } else {
                    DBG("Failed to select preferred devices: " << result);
                }
            }
        }

        // Log currently selected device
        if (auto* currentDevice = juceDeviceManager.getCurrentAudioDevice()) {
            DBG("Current audio device: " + currentDevice->getName());
            DBG("  Type: " + currentDevice->getTypeName());
            DBG("  Sample rate: " + juce::String(currentDevice->getCurrentSampleRate()));
            DBG("  Buffer size: " + juce::String(currentDevice->getCurrentBufferSizeSamples()));
            DBG("  Input channels: " + juce::String(currentDevice->getInputChannelNames().size()));
            DBG("  Output channels: " +
                juce::String(currentDevice->getOutputChannelNames().size()));
        } else {
            DBG("WARNING: No audio device selected!");
        }

        // Enable MIDI devices at JUCE level - this must be done so TE picks them up
        auto midiInputs = juce::MidiInput::getAvailableDevices();
        DBG("JUCE MIDI inputs available: " << midiInputs.size());
        for (const auto& midiInput : midiInputs) {
            if (!juceDeviceManager.isMidiInputDeviceEnabled(midiInput.identifier)) {
                juceDeviceManager.setMidiInputDeviceEnabled(midiInput.identifier, true);
                DBG("Enabled JUCE MIDI input: " << midiInput.name);
            }
        }

        // Listen for device manager changes BEFORE triggering rescan
        // This ensures we catch the notification when MIDI devices are created
        dm.addChangeListener(this);

        // Trigger a rescan so TE picks up the newly enabled MIDI devices
        // Note: The rescan is asynchronous (uses a timer). MIDI devices will be
        // created later and we'll be notified via changeListenerCallback.
        dm.rescanMidiDeviceList();
        DBG("MIDI device rescan triggered (async, listener registered)");
        for (auto& midiInput : dm.getMidiInDevices()) {
            if (midiInput && !midiInput->isEnabled()) {
                midiInput->setEnabled(true);
                DBG("Enabled TE MIDI input device: " << midiInput->getName());
            }
        }

        // Note: Audio inputs are now enabled by default (changed from previous behavior)
        // Users can configure them via Audio Settings dialog

        // Create a temporary Edit (project) so transport methods work
        auto editFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("magda_temp.tracktionedit");

        // Delete any existing temp file to ensure clean state (no stale input device config)
        if (editFile.existsAsFile()) {
            editFile.deleteFile();
        }

        currentEdit_ = tracktion::createEmptyEdit(*engine_, editFile);

        if (currentEdit_) {
            // Set default tempo at position 0
            auto& tempoSeq = currentEdit_->tempoSequence;
            if (tempoSeq.getNumTempos() > 0) {
                auto tempo = tempoSeq.getTempo(0);
                if (tempo) {
                    tempo->setBpm(120.0);
                }
            }

            // Note: Master track automatically routes to default audio device (first enabled stereo
            // pair) This is configured by Tracktion Engine's default behavior

            // Ensure the playback context is created and graph is allocated
            // This is needed for MIDI routing to work even before pressing play
            currentEdit_->getTransport().ensureContextAllocated();
            if (auto* ctx = currentEdit_->getCurrentPlaybackContext()) {
                DBG("Playback context allocated for live MIDI monitoring");
                DBG("  Total inputs in context: " << ctx->getAllInputs().size());
            } else {
                DBG("WARNING: ensureContextAllocated() called but context is still null!");
            }

            // Create AudioBridge for TrackManager-to-Tracktion synchronization
            audioBridge_ = std::make_unique<AudioBridge>(*engine_, *currentEdit_);
            audioBridge_->syncAll();

            // Create SessionClipScheduler for session view clip playback
            sessionScheduler_ =
                std::make_unique<SessionClipScheduler>(*audioBridge_, *currentEdit_);

            // Create PluginWindowManager for safe window lifecycle
            // Must be created AFTER AudioBridge, destroyed BEFORE AudioBridge
            pluginWindowManager_ = std::make_unique<PluginWindowManager>(*engine_, *currentEdit_);

            // Tell AudioBridge about the window manager and engine wrapper for delegation
            audioBridge_->setPluginWindowManager(pluginWindowManager_.get());
            audioBridge_->setEngineWrapper(this);

            // Enable all MIDI input devices (redundant now but keeps the API consistent)
            audioBridge_->enableAllMidiInputDevices();

            // Create MidiBridge for MIDI device management
            midiBridge_ = std::make_unique<MidiBridge>(*engine_);

            // Connect MidiBridge to AudioBridge for MIDI activity monitoring
            midiBridge_->setAudioBridge(audioBridge_.get());

            // Note: Change listener was already registered earlier (before MIDI rescan)

            std::cout << "Tracktion Engine initialized with Edit, AudioBridge, and MidiBridge"
                      << std::endl;
        } else {
            std::cout << "Tracktion Engine initialized (no Edit created)" << std::endl;
        }

        // Ensure devicesLoading_ is cleared so transport isn't blocked.
        // The async changeListenerCallback may not fire if no MIDI devices are present.
        if (devicesLoading_) {
            devicesLoading_ = false;
        }

        return currentEdit_ != nullptr;

    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to initialize Tracktion Engine: " << e.what() << std::endl;
        return false;
    }
}

void TracktionEngineWrapper::shutdown() {
    std::cout << "TracktionEngineWrapper::shutdown - starting..." << std::endl;

    // Release test tone plugin first (before Edit is destroyed)
    testTonePlugin_.reset();

    // Remove device manager listener
    if (engine_) {
        engine_->getDeviceManager().removeChangeListener(this);
    }

    // CRITICAL: Close all plugin windows FIRST (before plugins are destroyed)
    // This prevents malloc errors from windows trying to access destroyed plugins
    if (pluginWindowManager_) {
        std::cout << "Closing all plugin windows..." << std::endl;
        pluginWindowManager_->closeAllWindows();
        pluginWindowManager_.reset();
    }

    // Destroy session scheduler before AudioBridge (it references both)
    if (sessionScheduler_) {
        sessionScheduler_.reset();
    }

    // Destroy bridges (they reference Edit and/or Engine)
    if (audioBridge_) {
        audioBridge_.reset();
    }
    if (midiBridge_) {
        midiBridge_.reset();
    }

    // CRITICAL: Stop transport and release playback context BEFORE destroying Edit
    // This ensures audio/MIDI devices are properly released
    if (currentEdit_) {
        std::cout << "Stopping transport and releasing playback context..." << std::endl;
        auto& transport = currentEdit_->getTransport();

        // Stop playback if running
        if (transport.isPlaying()) {
            transport.stop(false, false);
        }

        // Release the playback context - this frees audio/MIDI device resources
        transport.freePlaybackContext();

        std::cout << "Destroying Edit..." << std::endl;
        currentEdit_.reset();
    }

    // Close audio/MIDI devices before destroying engine
    if (engine_) {
        std::cout << "Closing audio devices..." << std::endl;
        auto& dm = engine_->getDeviceManager();
        dm.closeDevices();

        std::cout << "Destroying Tracktion Engine..." << std::endl;
        engine_.reset();
    }

    std::cout << "Tracktion Engine shutdown complete" << std::endl;
}

// =============================================================================
// PDC Query Methods
// =============================================================================

double TracktionEngineWrapper::getPluginLatencySeconds(const std::string& effect_id) const {
    // TODO: Implement when we have effect tracking
    // For now, iterate all tracks and their plugins to find by ID
    juce::ignoreUnused(effect_id);
    return 0.0;
}

double TracktionEngineWrapper::getGlobalLatencySeconds() const {
    if (!currentEdit_) {
        return 0.0;
    }

    // Get the playback context which contains the audio graph
    auto* context = currentEdit_->getCurrentPlaybackContext();
    if (!context) {
        return 0.0;
    }

    // Tracktion Engine calculates PDC automatically and stores max latency
    // The easiest approach is to iterate all tracks and find max plugin latency
    double maxLatency = 0.0;

    for (auto* track : currentEdit_->getTrackList()) {
        if (auto* audioTrack = dynamic_cast<tracktion::AudioTrack*>(track)) {
            // Check all plugins on this track
            for (auto* plugin : audioTrack->pluginList) {
                // Use base Plugin class method (works for all plugin types)
                maxLatency = std::max(maxLatency, plugin->getLatencySeconds());
            }
        }
    }

    // Add device latency
    maxLatency += engine_->getDeviceManager().getOutputLatencySeconds();

    return maxLatency;
}

// =============================================================================
// Change Listener
// =============================================================================

void TracktionEngineWrapper::changeListenerCallback(juce::ChangeBroadcaster* source) {
    // DeviceManager changed - this happens during MIDI device scanning
    if (engine_ && source == &engine_->getDeviceManager()) {
        auto& dm = engine_->getDeviceManager();

        // Check if MIDI devices have appeared
        auto midiDevices = dm.getMidiInDevices();
        bool hasMidiDevices = !midiDevices.empty();

        DBG("Device change callback: " << midiDevices.size() << " MIDI input devices");

        // Enable any new MIDI input devices that have appeared
        for (auto& midiIn : midiDevices) {
            if (midiIn && !midiIn->isEnabled()) {
                midiIn->setEnabled(true);
                DBG("Device change: Enabled MIDI input: " << midiIn->getName());
            }
        }

        // Only reallocate playback context when devices are added
        // (avoids unnecessary reallocation on device removal or property changes)
        if (currentEdit_) {
            if (auto* ctx = currentEdit_->getCurrentPlaybackContext()) {
                int inputsBefore = static_cast<int>(ctx->getAllInputs().size());

                // Count current available devices to detect additions
                int totalDevices = static_cast<int>(dm.getMidiInDevices().size()) +
                                   static_cast<int>(dm.getWaveInputDevices().size()) +
                                   static_cast<int>(dm.getWaveOutputDevices().size());

                if (totalDevices > lastKnownDeviceCount_) {
                    ctx->reallocate();
                    int inputsAfter = static_cast<int>(ctx->getAllInputs().size());
                    DBG("Device change: Reallocated playback context (inputs: "
                        << inputsBefore << " -> " << inputsAfter << ")");
                }
                lastKnownDeviceCount_ = totalDevices;

                // Notify AudioBridge that MIDI devices are now available
                // so it can apply any pending MIDI routes
                if (hasMidiDevices && audioBridge_) {
                    audioBridge_->onMidiDevicesAvailable();
                }
            }
        }

        // Build a description of currently enabled devices
        juce::StringArray deviceNames;

        // Get MIDI input devices (returns shared_ptr)
        for (const auto& midiIn : dm.getMidiInDevices()) {
            if (midiIn && midiIn->isEnabled()) {
                deviceNames.add("MIDI: " + midiIn->getName());
            }
        }

        // Get audio output device (returns raw pointers)
        for (auto* waveOut : dm.getWaveOutputDevices()) {
            if (waveOut && waveOut->isEnabled()) {
                deviceNames.add("Audio: " + waveOut->getName());
            }
        }

        juce::String message;
        if (devicesLoading_) {
            message = "Scanning devices...";
            if (deviceNames.size() > 0) {
                message = "Found: " + deviceNames.joinIntoString(", ");
            }
        } else {
            message = "Devices ready";
        }

        // If we were playing, stop and remember we need to resume
        if (isPlaying() && devicesLoading_) {
            wasPlayingBeforeDeviceChange_ = true;
            stop();
            std::cout << "Stopped playback during device initialization" << std::endl;
        }

        // Mark devices as no longer loading after first change notification
        if (devicesLoading_) {
            devicesLoading_ = false;
            std::cout << "Device initialization complete: " << message << std::endl;

            if (onDevicesLoadingChanged) {
                onDevicesLoadingChanged(false, message);
            }
        }
    }
}

CommandResponse TracktionEngineWrapper::processCommand(const Command& command) {
    const auto& type = command.getType();

    try {
        if (type == "play") {
            play();
            return CommandResponse(CommandResponse::Status::Success, "Playback started");
        } else if (type == "stop") {
            stop();
            return CommandResponse(CommandResponse::Status::Success, "Playback stopped");
        } else if (type == "createTrack") {
            // Simple parameter parsing - in a real implementation you'd parse JSON
            auto trackId = createMidiTrack("New Track");

            juce::DynamicObject::Ptr obj = new juce::DynamicObject();
            obj->setProperty("trackId", juce::String(trackId));
            juce::var responseData(obj.get());

            auto response = CommandResponse(CommandResponse::Status::Success, "Track created");
            response.setData(responseData);
            return response;
        } else {
            return CommandResponse(CommandResponse::Status::Error, "Unknown command");
        }
    } catch (const std::exception& e) {
        return CommandResponse(CommandResponse::Status::Error,
                               "Command execution failed: " + std::string(e.what()));
    }
}

// TransportInterface implementation
void TracktionEngineWrapper::play() {
    // Block playback while devices are loading to prevent audio glitches
    if (devicesLoading_) {
        std::cout << "Playback blocked - devices still loading" << std::endl;
        return;
    }

    if (currentEdit_) {
        auto& transport = currentEdit_->getTransport();

        // Detect stale audio device (e.g. CoreAudio daemon stuck after sleep)
        // Check multiple times to avoid false positives from momentary zero readings
        auto& jdm = engine_->getDeviceManager().deviceManager;
        auto* device = jdm.getCurrentAudioDevice();
        if (device && device->isPlaying() && jdm.getCpuUsage() == 0.0) {
            int zeroCount = 1;
            for (int i = 0; i < 2; ++i) {
                juce::Thread::sleep(50);
                if (jdm.getCpuUsage() == 0.0)
                    ++zeroCount;
            }
            if (zeroCount >= 3) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon, "Audio Device Not Responding",
                    "The audio device '" + device->getName() +
                        "' is not processing audio.\n\n"
                        "Try disconnecting and reconnecting your audio interface, "
                        "or restarting the audio driver.",
                    "OK");
            }
        }

        transport.play(false);
        std::cout << "Playback started" << std::endl;
    }
}

void TracktionEngineWrapper::stop() {
    if (currentEdit_) {
        currentEdit_->getTransport().stop(false, false);
        std::cout << "Playback stopped" << std::endl;
    }
}

void TracktionEngineWrapper::pause() {
    stop();  // Tracktion doesn't distinguish between stop and pause
}

void TracktionEngineWrapper::record() {
    // Block recording while devices are loading
    if (devicesLoading_) {
        std::cout << "Recording blocked - devices still loading" << std::endl;
        return;
    }

    if (currentEdit_) {
        currentEdit_->getTransport().record(false);
        std::cout << "Recording started" << std::endl;
    }
}

void TracktionEngineWrapper::locate(double position_seconds) {
    if (currentEdit_) {
        currentEdit_->getTransport().setPosition(
            tracktion::TimePosition::fromSeconds(position_seconds));
    }
}

void TracktionEngineWrapper::locateMusical(int bar, int beat, int tick) {
    // Convert musical position to time
    if (currentEdit_) {
        auto& tempoSequence = currentEdit_->tempoSequence;
        auto beatPosition =
            tracktion::BeatPosition::fromBeats(bar * 4.0 + beat - 1.0 + tick / 1000.0);
        auto timePosition = tempoSequence.beatsToTime(beatPosition);
        currentEdit_->getTransport().setPosition(timePosition);
    }
}

double TracktionEngineWrapper::getCurrentPosition() const {
    if (currentEdit_) {
        return currentEdit_->getTransport().position.get().inSeconds();
    }
    return 0.0;
}

void TracktionEngineWrapper::getCurrentMusicalPosition(int& bar, int& beat, int& tick) const {
    if (currentEdit_) {
        auto position = tracktion::TimePosition::fromSeconds(getCurrentPosition());
        auto& tempoSequence = currentEdit_->tempoSequence;
        auto beatPosition = tempoSequence.timeToBeats(position);
        auto beats = beatPosition.inBeats();

        bar = static_cast<int>(beats / 4.0) + 1;
        beat = static_cast<int>(beats) % 4 + 1;
        tick = static_cast<int>((beats - static_cast<int>(beats)) * 1000);
    }
}

bool TracktionEngineWrapper::isPlaying() const {
    if (currentEdit_) {
        return currentEdit_->getTransport().isPlaying();
    }
    return false;
}

bool TracktionEngineWrapper::isRecording() const {
    if (currentEdit_) {
        return currentEdit_->getTransport().isRecording();
    }
    return false;
}

double TracktionEngineWrapper::getSessionPlayheadPosition() const {
    if (sessionScheduler_)
        return sessionScheduler_->getSessionPlayheadPosition();
    return -1.0;
}

void TracktionEngineWrapper::setTempo(double bpm) {
    if (currentEdit_) {
        auto& tempoSeq = currentEdit_->tempoSequence;
        if (tempoSeq.getNumTempos() > 0) {
            auto tempo = tempoSeq.getTempo(0);
            if (tempo) {
                tempo->setBpm(bpm);
                std::cout << "Set tempo: " << bpm << " BPM" << std::endl;
            }
        }
    }
}

double TracktionEngineWrapper::getTempo() const {
    if (currentEdit_) {
        auto timePos = tracktion::TimePosition::fromSeconds(0.0);
        return currentEdit_->tempoSequence.getTempoAt(timePos).getBpm();
    }
    return 120.0;
}

void TracktionEngineWrapper::setTimeSignature(int numerator, int denominator) {
    if (currentEdit_) {
        // Time signature handling in Tracktion Engine - simplified for now
        std::cout << "Set time signature: " << numerator << "/" << denominator << std::endl;
    }
}

void TracktionEngineWrapper::getTimeSignature(int& numerator, int& denominator) const {
    if (currentEdit_) {
        // Time signature handling in Tracktion Engine - simplified for now
        numerator = 4;
        denominator = 4;
    } else {
        numerator = 4;
        denominator = 4;
    }
}

void TracktionEngineWrapper::setLooping(bool enabled) {
    if (currentEdit_) {
        currentEdit_->getTransport().looping = enabled;
    }
}

void TracktionEngineWrapper::setLoopRegion(double start_seconds, double end_seconds) {
    if (currentEdit_) {
        auto startPos = tracktion::TimePosition::fromSeconds(start_seconds);
        auto endPos = tracktion::TimePosition::fromSeconds(end_seconds);
        currentEdit_->getTransport().setLoopRange(tracktion::TimeRange(startPos, endPos));
    }
}

bool TracktionEngineWrapper::isLooping() const {
    if (currentEdit_) {
        return currentEdit_->getTransport().looping;
    }
    return false;
}

bool TracktionEngineWrapper::justStarted() const {
    return justStarted_;
}

bool TracktionEngineWrapper::justLooped() const {
    return justLooped_;
}

void TracktionEngineWrapper::updateTriggerState() {
    // Reset flags at start of each frame
    justStarted_ = false;
    justLooped_ = false;

    bool currentlyPlaying = isPlaying();
    double currentPosition = getCurrentPosition();

    // Detect play start (was not playing, now playing)
    if (currentlyPlaying && !wasPlaying_) {
        justStarted_ = true;
    }

    // Detect loop (position jumped backward while playing and looping)
    if (currentlyPlaying && isLooping() && currentPosition < lastPosition_) {
        // Position went backward - likely a loop
        // Add a threshold to avoid false positives from small position jitter
        if (lastPosition_ - currentPosition > 0.1) {  // More than 100ms backward
            justLooped_ = true;
        }
    }

    // Update state for next frame
    wasPlaying_ = currentlyPlaying;
    lastPosition_ = currentPosition;

    // Update AudioBridge with transport state for trigger sync
    if (audioBridge_) {
        audioBridge_->updateTransportState(currentlyPlaying, justStarted_, justLooped_);
    }
}

// Metronome/click track methods
void TracktionEngineWrapper::setMetronomeEnabled(bool enabled) {
    if (currentEdit_) {
        currentEdit_->clickTrackEnabled = enabled;
        std::cout << "Metronome " << (enabled ? "enabled" : "disabled") << std::endl;
    }
}

bool TracktionEngineWrapper::isMetronomeEnabled() const {
    if (currentEdit_) {
        return currentEdit_->clickTrackEnabled;
    }
    return false;
}

juce::AudioDeviceManager* TracktionEngineWrapper::getDeviceManager() {
    if (engine_) {
        return &engine_->getDeviceManager().deviceManager;
    }
    return nullptr;
}

// ===== AudioEngineListener Implementation =====
// These methods are called by TimelineController when UI state changes

void TracktionEngineWrapper::onTransportPlay(double position) {
    // If a session clip is selected, trigger it via the clip launcher
    auto& cm = ClipManager::getInstance();
    ClipId selectedClip = cm.getSelectedClip();
    if (selectedClip != INVALID_CLIP_ID) {
        const auto* clip = cm.getClip(selectedClip);
        if (clip && clip->view == ClipView::Session && !clip->isPlaying && !clip->isQueued) {
            cm.triggerClip(selectedClip);
            return;  // SessionClipScheduler will start transport
        }
    }

    locate(position);
    play();
}

void TracktionEngineWrapper::onTransportStop(double returnPosition) {
    // Stop any playing session clips
    if (sessionScheduler_) {
        sessionScheduler_->deactivateAllSessionClips();
    }

    stop();
    locate(returnPosition);
}

void TracktionEngineWrapper::onTransportPause() {
    pause();
}

void TracktionEngineWrapper::onTransportRecord(double position) {
    locate(position);
    record();
}

void TracktionEngineWrapper::onEditPositionChanged(double position) {
    // Only seek if not currently playing
    if (!isPlaying()) {
        locate(position);
    }
}

void TracktionEngineWrapper::onTempoChanged(double bpm) {
    setTempo(bpm);
}

void TracktionEngineWrapper::onTimeSignatureChanged(int numerator, int denominator) {
    setTimeSignature(numerator, denominator);
}

void TracktionEngineWrapper::onLoopRegionChanged(double startTime, double endTime, bool enabled) {
    setLoopRegion(startTime, endTime);
    setLooping(enabled);
}

void TracktionEngineWrapper::onLoopEnabledChanged(bool enabled) {
    setLooping(enabled);
}

// TrackInterface implementation
std::string TracktionEngineWrapper::createAudioTrack(const std::string& name) {
    if (!currentEdit_)
        return "";

    auto trackId = generateTrackId();
    auto tracks = tracktion::getAudioTracks(*currentEdit_);
    auto insertPoint = tracktion::TrackInsertPoint(nullptr, nullptr);
    auto track = currentEdit_->insertNewAudioTrack(insertPoint, nullptr);
    if (track) {
        track->setName(name);
        trackMap_[trackId] = track;
        std::cout << "Created audio track: " << name << " (ID: " << trackId << ")" << std::endl;
    }
    return trackId;
}

std::string TracktionEngineWrapper::createMidiTrack(const std::string& name) {
    if (!currentEdit_)
        return "";

    auto trackId = generateTrackId();
    auto insertPoint = tracktion::TrackInsertPoint(nullptr, nullptr);
    auto track = currentEdit_->insertNewAudioTrack(insertPoint, nullptr);
    if (track) {
        track->setName(name);
        trackMap_[trackId] = track;
        std::cout << "Created MIDI track: " << name << " (ID: " << trackId << ")" << std::endl;
    }
    return trackId;
}

void TracktionEngineWrapper::deleteTrack(const std::string& track_id) {
    auto it = trackMap_.find(track_id);
    if (it != trackMap_.end() && currentEdit_) {
        currentEdit_->deleteTrack(it->second.get());
        trackMap_.erase(it);
        std::cout << "Deleted track ID: " << track_id << std::endl;
    }
}

void TracktionEngineWrapper::setTrackName(const std::string& track_id, const std::string& name) {
    auto track = findTrackById(track_id);
    if (track) {
        track->setName(name);
    }
}

std::string TracktionEngineWrapper::getTrackName(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    return track ? track->getName().toStdString() : "";
}

void TracktionEngineWrapper::setTrackMuted(const std::string& track_id, bool muted) {
    auto track = findTrackById(track_id);
    if (track) {
        track->setMute(muted);
    }
}

bool TracktionEngineWrapper::isTrackMuted(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    return track ? track->isMuted(false) : false;
}

void TracktionEngineWrapper::setTrackSolo(const std::string& track_id, bool solo) {
    auto track = findTrackById(track_id);
    if (track) {
        track->setSolo(solo);
    }
}

bool TracktionEngineWrapper::isTrackSolo(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    return track ? track->isSolo(false) : false;
}

void TracktionEngineWrapper::setTrackArmed(const std::string& track_id, bool armed) {
    auto track = findTrackById(track_id);
    if (track) {
        if (auto audioTrack = dynamic_cast<tracktion::AudioTrack*>(track)) {
            // Simplified - in real implementation would set input recording
            std::cout << "Set track armed (stub): " << track_id << " = " << armed << std::endl;
        }
    }
}

bool TracktionEngineWrapper::isTrackArmed(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    if (track) {
        if (auto audioTrack = dynamic_cast<tracktion::AudioTrack*>(track)) {
            // Simplified - in real implementation would check input recording
            return false;
        }
    }
    return false;
}

void TracktionEngineWrapper::setTrackColor(const std::string& track_id, int r, int g, int b) {
    auto track = findTrackById(track_id);
    if (track) {
        track->setColour(juce::Colour::fromRGB(r, g, b));
    }
}

std::vector<std::string> TracktionEngineWrapper::getAllTrackIds() const {
    std::vector<std::string> ids;
    for (const auto& pair : trackMap_) {
        ids.push_back(pair.first);
    }
    return ids;
}

bool TracktionEngineWrapper::trackExists(const std::string& track_id) const {
    return trackMap_.find(track_id) != trackMap_.end();
}

void TracktionEngineWrapper::previewNoteOnTrack(const std::string& track_id, int noteNumber,
                                                int velocity, bool isNoteOn) {
    DBG("TracktionEngineWrapper::previewNoteOnTrack - Track="
        << track_id << ", Note=" << noteNumber << ", Velocity=" << velocity
        << ", On=" << (isNoteOn ? "YES" : "NO"));

    if (!audioBridge_) {
        DBG("TracktionEngineWrapper: WARNING - No AudioBridge!");
        return;
    }

    // Convert string track ID to integer (MAGDA TrackId) with validation
    int magdaTrackId = 0;
    try {
        magdaTrackId = std::stoi(track_id);
    } catch (const std::exception& e) {
        DBG("TracktionEngineWrapper: WARNING - Invalid track ID '"
            << track_id << "' passed to previewNoteOnTrack: " << e.what());
        return;
    }
    DBG("TracktionEngineWrapper: Looking up MAGDA track ID: " << magdaTrackId);

    // Use AudioBridge to get the Tracktion AudioTrack
    auto* audioTrack = audioBridge_->getAudioTrack(magdaTrackId);
    if (!audioTrack) {
        DBG("TracktionEngineWrapper: WARNING - Track not found in AudioBridge!");
        return;
    }

    DBG("TracktionEngineWrapper: Track found, injecting MIDI");

    // Ensure MIDI input device is in monitoring mode (always audible)
    auto& midiInput = audioTrack->getMidiInputDevice();
    auto currentMode = midiInput.getMonitorMode();
    DBG("TracktionEngineWrapper: Current monitor mode: " << (int)currentMode);

    if (currentMode != tracktion::InputDevice::MonitorMode::on) {
        DBG("TracktionEngineWrapper: Enabling monitor mode");
        midiInput.setMonitorMode(tracktion::InputDevice::MonitorMode::on);
    }

    // Create MIDI message
    juce::MidiMessage message =
        isNoteOn ? juce::MidiMessage::noteOn(1, noteNumber, (juce::uint8)velocity)
                 : juce::MidiMessage::noteOff(1, noteNumber, (juce::uint8)velocity);

    DBG("TracktionEngineWrapper: MIDI message created - " << message.getDescription());

    // Inject MIDI through DeviceManager (simulates physical MIDI keyboard input)
    // This ensures the message goes through the normal MIDI routing graph
    DBG("TracktionEngineWrapper: Injecting MIDI through DeviceManager");
    engine_->getDeviceManager().injectMIDIMessageToDefaultDevice(message);
    DBG("TracktionEngineWrapper: MIDI message injected successfully");
}

// ClipInterface implementation

// Helper: Convert beats to seconds using current tempo
static double beatsToSeconds(double beats, double tempo) {
    return beats * (60.0 / tempo);
}

// Helper: Convert seconds to beats using current tempo
static double secondsToBeats(double seconds, double tempo) {
    return seconds / (60.0 / tempo);
}

std::string TracktionEngineWrapper::addMidiClip(const std::string& track_id, double start_time,
                                                double length, const std::vector<MidiNote>& notes) {
    auto track = findTrackById(track_id);
    if (!track || !currentEdit_) {
        std::cerr << "addMidiClip: Track not found or no edit: " << track_id << std::endl;
        return "";
    }

    // Cast to AudioTrack (needed for insertMIDIClip)
    auto* audioTrack = dynamic_cast<tracktion::AudioTrack*>(track);
    if (!audioTrack) {
        std::cerr << "addMidiClip: Track is not an AudioTrack: " << track_id << std::endl;
        return "";
    }

    // Create MIDI clip at specified position
    namespace te = tracktion;
    auto timeRange = te::TimeRange(te::TimePosition::fromSeconds(start_time),
                                   te::TimePosition::fromSeconds(start_time + length));

    auto midiClipPtr = audioTrack->insertMIDIClip(timeRange, nullptr);
    if (!midiClipPtr) {
        std::cerr << "addMidiClip: Failed to create MIDI clip" << std::endl;
        return "";
    }

    // Get current tempo for beat-to-seconds conversion
    double tempo = getTempo();

    // Add notes to the clip's sequence
    auto& sequence = midiClipPtr->getSequence();
    for (const auto& note : notes) {
        // Convert beat-relative position to absolute beats
        // Note: Tracktion Engine uses beats, not seconds for MIDI notes
        te::BeatPosition startBeat = te::BeatPosition::fromBeats(note.startBeat);
        te::BeatDuration lengthBeats = te::BeatDuration::fromBeats(note.lengthBeats);

        DBG("Adding MIDI note: number=" << note.noteNumber << " start=" << note.startBeat
                                        << " beats, length=" << note.lengthBeats
                                        << " beats, velocity=" << note.velocity);

        sequence.addNote(note.noteNumber, startBeat, lengthBeats, note.velocity,
                         0,         // colour index
                         nullptr);  // undo manager
    }

    // Generate ID and store in clipMap_
    auto clipId = generateClipId();
    clipMap_[clipId] = midiClipPtr;

    std::cout << "Created MIDI clip: " << clipId << " on track " << track_id << " with "
              << notes.size() << " notes" << std::endl;
    return clipId;
}

std::string TracktionEngineWrapper::addAudioClip(const std::string& track_id, double start_time,
                                                 const std::string& audio_file_path) {
    // 1. Find Tracktion AudioTrack
    auto* track = findTrackById(track_id);
    if (!track || !currentEdit_) {
        DBG("TracktionEngineWrapper: Track not found or no edit: " << track_id);
        return "";
    }

    auto* audioTrack = dynamic_cast<tracktion::AudioTrack*>(track);
    if (!audioTrack) {
        DBG("TracktionEngineWrapper: Track is not an AudioTrack");
        return "";
    }

    // 2. Validate audio file exists
    juce::File audioFile(audio_file_path);
    if (!audioFile.existsAsFile()) {
        DBG("TracktionEngineWrapper: Audio file not found: " << audio_file_path);
        return "";
    }

    // 3. Get audio file metadata to determine length
    namespace te = tracktion;
    te::AudioFile teAudioFile(audioTrack->edit.engine, audioFile);
    if (!teAudioFile.isValid()) {
        DBG("TracktionEngineWrapper: Invalid audio file: " << audio_file_path);
        return "";
    }

    double fileLengthSeconds = teAudioFile.getLength();

    // 4. Create time range for clip position
    auto timeRange = te::TimeRange(te::TimePosition::fromSeconds(start_time),
                                   te::TimePosition::fromSeconds(start_time + fileLengthSeconds));

    // 5. Insert WaveAudioClip onto track
    auto clipPtr =
        insertWaveClip(*audioTrack,
                       audioFile.getFileNameWithoutExtension(),  // clip name
                       audioFile, te::ClipPosition{timeRange}, te::DeleteExistingClips::no);

    if (!clipPtr) {
        DBG("TracktionEngineWrapper: Failed to create WaveAudioClip");
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, "Audio Clip Error",
            "Failed to create audio clip from:\n" + juce::String(audio_file_path));
        return "";
    }

    // 6. Store in clip map with generated ID
    auto clipId = generateClipId();
    clipMap_[clipId] = clipPtr.get();  // Store raw pointer

    DBG("TracktionEngineWrapper: Created audio clip '" << clipId << "' from " << audio_file_path);
    return clipId;
}

void TracktionEngineWrapper::deleteClip(const std::string& clip_id) {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "deleteClip: Clip not found: " << clip_id << std::endl;
        return;
    }

    // Remove from track
    clip->removeFromParent();

    // Remove from clipMap_
    clipMap_.erase(clip_id);

    std::cout << "Deleted clip: " << clip_id << std::endl;
}

void TracktionEngineWrapper::moveClip(const std::string& clip_id, double new_start_time) {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "moveClip: Clip not found: " << clip_id << std::endl;
        return;
    }

    // Get current position and create new position with updated start time
    auto currentPos = clip->getPosition();
    auto currentLength = currentPos.getLength().inSeconds();

    namespace te = tracktion;
    auto newTimeRange =
        te::TimeRange(te::TimePosition::fromSeconds(new_start_time),
                      te::TimePosition::fromSeconds(new_start_time + currentLength));

    clip->setStart(newTimeRange.getStart(), false, false);

    std::cout << "Moved clip: " << clip_id << " to " << new_start_time << std::endl;
}

void TracktionEngineWrapper::resizeClip(const std::string& clip_id, double new_length) {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "resizeClip: Clip not found: " << clip_id << std::endl;
        return;
    }

    // Get current position and create new position with updated length
    auto currentPos = clip->getPosition();
    auto currentStart = currentPos.getStart();

    namespace te = tracktion;
    auto newEnd = te::TimePosition::fromSeconds(currentStart.inSeconds() + new_length);

    clip->setEnd(newEnd, false);

    std::cout << "Resized clip: " << clip_id << " to " << new_length << std::endl;
}

double TracktionEngineWrapper::getClipStartTime(const std::string& clip_id) const {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "getClipStartTime: Clip not found: " << clip_id << std::endl;
        return 0.0;
    }

    return clip->getPosition().getStart().inSeconds();
}

double TracktionEngineWrapper::getClipLength(const std::string& clip_id) const {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "getClipLength: Clip not found: " << clip_id << std::endl;
        return 1.0;
    }

    return clip->getPosition().getLength().inSeconds();
}

void TracktionEngineWrapper::addNoteToMidiClip(const std::string& clip_id, const MidiNote& note) {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "addNoteToMidiClip: Clip not found: " << clip_id << std::endl;
        return;
    }

    // Cast to MidiClip
    namespace te = tracktion;
    auto* midiClip = dynamic_cast<te::MidiClip*>(clip);
    if (!midiClip) {
        std::cerr << "addNoteToMidiClip: Clip is not a MIDI clip: " << clip_id << std::endl;
        return;
    }

    // Convert beat-relative position to absolute beats
    namespace te = tracktion;
    te::BeatPosition startBeat = te::BeatPosition::fromBeats(note.startBeat);
    te::BeatDuration lengthBeats = te::BeatDuration::fromBeats(note.lengthBeats);

    // Add note to sequence
    auto& sequence = midiClip->getSequence();
    sequence.addNote(note.noteNumber, startBeat, lengthBeats, note.velocity,
                     0,         // colour index
                     nullptr);  // undo manager

    std::cout << "Added note " << note.noteNumber << " to MIDI clip: " << clip_id << std::endl;
}

void TracktionEngineWrapper::removeNotesFromMidiClip(const std::string& clip_id, double start_time,
                                                     double end_time) {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "removeNotesFromMidiClip: Clip not found: " << clip_id << std::endl;
        return;
    }

    // Cast to MidiClip
    namespace te = tracktion;
    auto* midiClip = dynamic_cast<te::MidiClip*>(clip);
    if (!midiClip) {
        std::cerr << "removeNotesFromMidiClip: Clip is not a MIDI clip: " << clip_id << std::endl;
        return;
    }

    // Remove notes in the specified time range
    // Note: start_time and end_time are in beats (relative to clip)
    auto& sequence = midiClip->getSequence();
    juce::Array<te::MidiNote*> notesToRemove;

    for (auto* note : sequence.getNotes()) {
        double noteStart = note->getStartBeat().inBeats();
        if (noteStart >= start_time && noteStart < end_time) {
            notesToRemove.add(note);
        }
    }

    for (auto* note : notesToRemove) {
        sequence.removeNote(*note, nullptr);  // nullptr = no undo
    }

    std::cout << "Removed " << notesToRemove.size() << " notes from MIDI clip: " << clip_id
              << std::endl;
}

std::vector<MidiNote> TracktionEngineWrapper::getMidiClipNotes(const std::string& clip_id) const {
    auto* clip = findClipById(clip_id);
    if (!clip) {
        std::cerr << "getMidiClipNotes: Clip not found: " << clip_id << std::endl;
        return {};
    }

    // Cast to MidiClip
    namespace te = tracktion;
    auto* midiClip = dynamic_cast<te::MidiClip*>(clip);
    if (!midiClip) {
        std::cerr << "getMidiClipNotes: Clip is not a MIDI clip: " << clip_id << std::endl;
        return {};
    }

    // Read notes from sequence and convert to our format
    std::vector<MidiNote> notes;
    auto& sequence = midiClip->getSequence();

    for (auto* note : sequence.getNotes()) {
        MidiNote midiNote;
        midiNote.noteNumber = note->getNoteNumber();
        midiNote.velocity = note->getVelocity();

        // Note: Tracktion Engine stores MIDI notes in beats (relative to clip)
        midiNote.startBeat = note->getStartBeat().inBeats();
        midiNote.lengthBeats = note->getLengthBeats().inBeats();

        notes.push_back(midiNote);
    }

    return notes;
}

std::vector<std::string> TracktionEngineWrapper::getTrackClips(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    if (!track) {
        std::cerr << "getTrackClips: Track not found: " << track_id << std::endl;
        return {};
    }

    // Cast to AudioTrack (needed for getClips)
    auto* audioTrack = dynamic_cast<tracktion::AudioTrack*>(track);
    if (!audioTrack) {
        std::cerr << "getTrackClips: Track is not an AudioTrack: " << track_id << std::endl;
        return {};
    }

    // Iterate clips and find their IDs in our clipMap_
    std::vector<std::string> clipIds;
    for (auto* clip : audioTrack->getClips()) {
        // Search clipMap_ for this clip pointer
        for (const auto& [id, clipPtr] : clipMap_) {
            if (clipPtr.get() == clip) {
                clipIds.push_back(id);
                break;
            }
        }
    }

    return clipIds;
}

bool TracktionEngineWrapper::clipExists(const std::string& clip_id) const {
    return clipMap_.find(clip_id) != clipMap_.end();
}

// MixerInterface implementation - uses VolumeAndPanPlugin
void TracktionEngineWrapper::setTrackVolume(const std::string& track_id, double volume) {
    auto track = findTrackById(track_id);
    if (track) {
        // Find VolumeAndPanPlugin on track
        if (auto volPan =
                track->pluginList.findFirstPluginOfType<tracktion::VolumeAndPanPlugin>()) {
            // Convert linear gain to dB for the plugin
            float db =
                volume > 0.0 ? static_cast<float>(juce::Decibels::gainToDecibels(volume)) : -100.0f;
            volPan->setVolumeDb(db);
        } else {
            // No VolumeAndPanPlugin found - this shouldn't happen as Tracktion auto-creates one
            std::cerr << "Warning: No VolumeAndPanPlugin on track " << track_id << std::endl;
        }
    }
}

double TracktionEngineWrapper::getTrackVolume(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    if (track) {
        if (auto volPan =
                track->pluginList.findFirstPluginOfType<tracktion::VolumeAndPanPlugin>()) {
            float db = volPan->getVolumeDb();
            return juce::Decibels::decibelsToGain(db);
        }
    }
    return 1.0;
}

void TracktionEngineWrapper::setTrackPan(const std::string& track_id, double pan) {
    auto track = findTrackById(track_id);
    if (track) {
        if (auto volPan =
                track->pluginList.findFirstPluginOfType<tracktion::VolumeAndPanPlugin>()) {
            // Pan is -1.0 (left) to 1.0 (right)
            volPan->setPan(static_cast<float>(pan));
        }
    }
}

double TracktionEngineWrapper::getTrackPan(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    if (track) {
        if (auto volPan =
                track->pluginList.findFirstPluginOfType<tracktion::VolumeAndPanPlugin>()) {
            return volPan->getPan();
        }
    }
    return 0.0;
}

void TracktionEngineWrapper::setMasterVolume(double volume) {
    if (currentEdit_) {
        currentEdit_->getMasterVolumePlugin()->setVolumeDb(juce::Decibels::gainToDecibels(volume));
    }
}

double TracktionEngineWrapper::getMasterVolume() const {
    if (currentEdit_) {
        return juce::Decibels::decibelsToGain(currentEdit_->getMasterVolumePlugin()->getVolumeDb());
    }
    return 1.0;
}

std::string TracktionEngineWrapper::addEffect(const std::string& track_id,
                                              const std::string& effect_name) {
    // TODO: Implement effect addition
    auto effectId = generateEffectId();
    std::cout << "Added effect (stub): " << effect_name << " to track " << track_id << std::endl;
    return effectId;
}

void TracktionEngineWrapper::removeEffect(const std::string& effect_id) {
    // TODO: Implement effect removal
    std::cout << "Removed effect (stub): " << effect_id << std::endl;
}

void TracktionEngineWrapper::setEffectParameter(const std::string& effect_id,
                                                const std::string& parameter_name, double value) {
    // TODO: Implement effect parameter setting
    std::cout << "Set effect parameter (stub): " << effect_id << "." << parameter_name << " = "
              << value << std::endl;
}

double TracktionEngineWrapper::getEffectParameter(const std::string& effect_id,
                                                  const std::string& parameter_name) const {
    // TODO: Implement effect parameter retrieval
    return 0.0;
}

void TracktionEngineWrapper::setEffectEnabled(const std::string& effect_id, bool enabled) {
    // TODO: Implement effect enable/disable
    std::cout << "Set effect enabled (stub): " << effect_id << " = " << enabled << std::endl;
}

bool TracktionEngineWrapper::isEffectEnabled(const std::string& effect_id) const {
    // TODO: Implement effect enabled check
    return true;
}

std::vector<std::string> TracktionEngineWrapper::getAvailableEffects() const {
    // TODO: Implement available effects retrieval
    return {"Reverb", "Delay", "EQ", "Compressor"};
}

std::vector<std::string> TracktionEngineWrapper::getTrackEffects(
    const std::string& track_id) const {
    // TODO: Implement track effects retrieval
    return {};
}

// Helper methods
tracktion::Track* TracktionEngineWrapper::findTrackById(const std::string& track_id) const {
    auto it = trackMap_.find(track_id);
    return (it != trackMap_.end()) ? it->second.get() : nullptr;
}

tracktion::Clip* TracktionEngineWrapper::findClipById(const std::string& clip_id) const {
    auto it = clipMap_.find(clip_id);
    return (it != clipMap_.end()) ? static_cast<tracktion::Clip*>(it->second.get()) : nullptr;
}

std::string TracktionEngineWrapper::generateTrackId() {
    return "track_" + std::to_string(nextTrackId_++);
}

std::string TracktionEngineWrapper::generateClipId() {
    return "clip_" + std::to_string(nextClipId_++);
}

std::string TracktionEngineWrapper::generateEffectId() {
    return "effect_" + std::to_string(nextEffectId_++);
}

// =============================================================================
// Plugin Scanning - Uses out-of-process scanner to prevent crashes
// =============================================================================

void TracktionEngineWrapper::startPluginScan(
    std::function<void(float, const juce::String&)> progressCallback) {
    if (!engine_ || isScanning_) {
        return;
    }

    isScanning_ = true;
    scanProgressCallback_ = progressCallback;

    auto& pluginManager = engine_->getPluginManager();
    auto& knownPlugins = pluginManager.knownPluginList;
    auto& formatManager = pluginManager.pluginFormatManager;

    // List available formats
    juce::StringArray formatNames;
    for (int i = 0; i < formatManager.getNumFormats(); ++i) {
        auto* format = formatManager.getFormat(i);
        if (format) {
            formatNames.add(format->getName());
        }
    }
    std::cout << "Starting plugin scan with OUT-OF-PROCESS scanner" << std::endl;
    std::cout << "Available formats: " << formatNames.joinIntoString(", ") << std::endl;

    // Create coordinator if needed
    if (!pluginScanCoordinator_) {
        pluginScanCoordinator_ = std::make_unique<PluginScanCoordinator>();
    }

    // Start scanning using the out-of-process coordinator
    pluginScanCoordinator_->startScan(
        formatManager,
        // Progress callback
        [this, progressCallback](float progress, const juce::String& currentPlugin) {
            if (progressCallback) {
                progressCallback(progress, currentPlugin);
            }
        },
        // Completion callback
        [this, &knownPlugins](bool success, const juce::Array<juce::PluginDescription>& plugins,
                              const juce::StringArray& failedPlugins) {
            // Add found plugins to KnownPluginList
            for (const auto& desc : plugins) {
                knownPlugins.addType(desc);
            }

            int numPlugins = knownPlugins.getNumTypes();
            std::cout << "Plugin scan complete. Found " << numPlugins << " plugins." << std::endl;

            if (failedPlugins.size() > 0) {
                std::cout << "Failed/crashed plugins (" << failedPlugins.size()
                          << "):" << std::endl;
                for (const auto& failed : failedPlugins) {
                    std::cout << "  - " << failed << std::endl;
                }
            }

            // Save the updated plugin list to persistent storage
            savePluginList();

            isScanning_ = false;

            if (onPluginScanComplete) {
                onPluginScanComplete(success, numPlugins, failedPlugins);
            }
        });
}

void TracktionEngineWrapper::abortPluginScan() {
    if (pluginScanCoordinator_) {
        pluginScanCoordinator_->abortScan();
    }
    isScanning_ = false;
}

void TracktionEngineWrapper::clearPluginScanCrashFiles() {
    // Clear the blacklist in the coordinator
    if (pluginScanCoordinator_) {
        pluginScanCoordinator_->clearBlacklist();
    } else {
        // Create a temporary coordinator just to clear the blacklist
        PluginScanCoordinator tempCoordinator;
        tempCoordinator.clearBlacklist();
    }
    std::cout << "Plugin blacklist cleared. Previously problematic plugins will be scanned again."
              << std::endl;
}

juce::KnownPluginList& TracktionEngineWrapper::getKnownPluginList() {
    return engine_->getPluginManager().knownPluginList;
}

const juce::KnownPluginList& TracktionEngineWrapper::getKnownPluginList() const {
    return engine_->getPluginManager().knownPluginList;
}

juce::File TracktionEngineWrapper::getPluginListFile() const {
    // Store plugin list in app data directory
    // macOS: ~/Library/Application Support/MAGDA/
    // Windows: %APPDATA%/MAGDA/
    // Linux: ~/.config/MAGDA/
    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("MAGDA");

    // Create directory if it doesn't exist
    if (!appDataDir.exists()) {
        appDataDir.createDirectory();
    }

    return appDataDir.getChildFile("PluginList.xml");
}

void TracktionEngineWrapper::savePluginList() {
    if (!engine_) {
        std::cerr << "Cannot save plugin list: engine not initialized" << std::endl;
        return;
    }

    auto& knownPlugins = engine_->getPluginManager().knownPluginList;
    auto pluginListFile = getPluginListFile();

    // Create XML representation of the plugin list
    if (auto xml = knownPlugins.createXml()) {
        if (xml->writeTo(pluginListFile)) {
            std::cout << "Saved plugin list (" << knownPlugins.getNumTypes()
                      << " plugins) to: " << pluginListFile.getFullPathName() << std::endl;
        } else {
            std::cerr << "Failed to write plugin list to: " << pluginListFile.getFullPathName()
                      << std::endl;
        }
    }
}

void TracktionEngineWrapper::loadPluginList() {
    if (!engine_) {
        std::cerr << "Cannot load plugin list: engine not initialized" << std::endl;
        return;
    }

    auto& knownPlugins = engine_->getPluginManager().knownPluginList;
    auto pluginListFile = getPluginListFile();

    if (pluginListFile.existsAsFile()) {
        if (auto xml = juce::XmlDocument::parse(pluginListFile)) {
            knownPlugins.recreateFromXml(*xml);
            std::cout << "Loaded plugin list (" << knownPlugins.getNumTypes()
                      << " plugins) from: " << pluginListFile.getFullPathName() << std::endl;
        } else {
            std::cerr << "Failed to parse plugin list from: " << pluginListFile.getFullPathName()
                      << std::endl;
            knownPlugins.clear();
        }
    } else {
        std::cout << "No saved plugin list found at: " << pluginListFile.getFullPathName()
                  << std::endl;
        std::cout << "Plugins will need to be scanned manually via the Plugin Browser" << std::endl;
        knownPlugins.clear();
    }
}

void TracktionEngineWrapper::clearPluginList() {
    if (!engine_) {
        std::cerr << "Cannot clear plugin list: engine not initialized" << std::endl;
        return;
    }

    // Clear in-memory list
    auto& knownPlugins = engine_->getPluginManager().knownPluginList;
    knownPlugins.clear();

    // Delete the saved file
    auto pluginListFile = getPluginListFile();
    if (pluginListFile.existsAsFile()) {
        pluginListFile.deleteFile();
        std::cout << "Deleted plugin list file: " << pluginListFile.getFullPathName() << std::endl;
    }

    std::cout << "Plugin list cleared. Use 'Scan' to rediscover plugins." << std::endl;
}

}  // namespace magda
