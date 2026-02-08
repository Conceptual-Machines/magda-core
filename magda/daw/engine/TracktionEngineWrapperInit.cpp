#include "TracktionEngineWrapper.hpp"

#include <iostream>

#include "../audio/AudioBridge.hpp"
#include "../audio/MidiBridge.hpp"
#include "../audio/SessionClipScheduler.hpp"
#include "../core/Config.hpp"
#include "MagdaEngineBehaviour.hpp"
#include "MagdaUIBehaviour.hpp"
#include "PluginWindowManager.hpp"

namespace magda {

TracktionEngineWrapper::TracktionEngineWrapper() = default;

TracktionEngineWrapper::~TracktionEngineWrapper() {
    shutdown();
}

void TracktionEngineWrapper::initializePluginFormats() {
    // Register ToneGeneratorPlugin (not registered by default)
    engine_->getPluginManager().createBuiltInType<tracktion::ToneGeneratorPlugin>();

    // Enable out-of-process scanning to prevent plugin crashes from crashing the app
    auto& pluginManager = engine_->getPluginManager();
    pluginManager.setUsesSeparateProcessForScanning(true);
    std::cout << "Enabled out-of-process plugin scanning" << std::endl;

    // Load saved plugin list from persistent storage
    loadPluginList();

    // Log registered plugin formats
    auto& formatManager = pluginManager.pluginFormatManager;
    std::cout << "Plugin formats registered by Tracktion Engine: " << formatManager.getNumFormats()
              << std::endl;
    for (int i = 0; i < formatManager.getNumFormats(); ++i) {
        auto* format = formatManager.getFormat(i);
        if (format) {
            std::cout << "  Format " << i << ": " << format->getName() << std::endl;
        }
    }
}

void TracktionEngineWrapper::initializeDeviceManager() {
    auto& dm = engine_->getDeviceManager();
    auto& juceDeviceManager = dm.deviceManager;

    // Log available audio device types
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

    // Get user preferences
    auto& config = magda::Config::getInstance();
    int preferredInputs = config.getPreferredInputChannels();
    int preferredOutputs = config.getPreferredOutputChannels();

    // Initialize DeviceManager with preferred channel counts
    int inputChannels = (preferredInputs > 0) ? preferredInputs : 1;
    int outputChannels = (preferredOutputs > 0) ? preferredOutputs : 2;
    dm.initialise(inputChannels, outputChannels);
    DBG("DeviceManager initialized with " << inputChannels << " input / " << outputChannels
                                          << " output channels");
}

void TracktionEngineWrapper::configureAudioDevices() {
    auto& config = magda::Config::getInstance();
    std::string preferredInputDevice = config.getPreferredInputDevice();
    std::string preferredOutputDevice = config.getPreferredOutputDevice();
    int preferredInputs = config.getPreferredInputChannels();
    int preferredOutputs = config.getPreferredOutputChannels();

    // Only configure if user specified preferences
    if (preferredInputDevice.empty() && preferredOutputDevice.empty()) {
        return;
    }

    auto& dm = engine_->getDeviceManager();
    auto& juceDeviceManager = dm.deviceManager;
    auto& deviceTypes = juceDeviceManager.getAvailableDeviceTypes();

    if (deviceTypes.isEmpty()) {
        return;
    }

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
    if (!preferredOutputDevice.empty() && outputDevices.contains(preferredOutputDevice)) {
        setup.outputDeviceName = preferredOutputDevice;
        DBG("Found preferred output device: " << preferredOutputDevice);
    }

    // Enable channels based on preference
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

    // Apply the device setup
    auto result = juceDeviceManager.setAudioDeviceSetup(setup, true);
    if (result.isEmpty()) {
        DBG("Successfully selected preferred devices - Input: "
            << setup.inputDeviceName << " (" << preferredInputs
            << " ch), Output: " << setup.outputDeviceName << " (" << preferredOutputs << " ch)");
    } else {
        DBG("Failed to select preferred devices: " << result);
    }

    // Log currently selected device
    if (auto* currentDevice = juceDeviceManager.getCurrentAudioDevice()) {
        DBG("Current audio device: " + currentDevice->getName());
        DBG("  Type: " + currentDevice->getTypeName());
        DBG("  Sample rate: " + juce::String(currentDevice->getCurrentSampleRate()));
        DBG("  Buffer size: " + juce::String(currentDevice->getCurrentBufferSizeSamples()));
        DBG("  Input channels: " + juce::String(currentDevice->getInputChannelNames().size()));
        DBG("  Output channels: " + juce::String(currentDevice->getOutputChannelNames().size()));
    } else {
        DBG("WARNING: No audio device selected!");
    }
}

void TracktionEngineWrapper::setupMidiDevices() {
    auto& dm = engine_->getDeviceManager();
    auto& juceDeviceManager = dm.deviceManager;

    // Enable MIDI devices at JUCE level
    auto midiInputs = juce::MidiInput::getAvailableDevices();
    DBG("JUCE MIDI inputs available: " << midiInputs.size());
    for (const auto& midiInput : midiInputs) {
        if (!juceDeviceManager.isMidiInputDeviceEnabled(midiInput.identifier)) {
            juceDeviceManager.setMidiInputDeviceEnabled(midiInput.identifier, true);
            DBG("Enabled JUCE MIDI input: " << midiInput.name);
        }
    }

    // Listen for device manager changes
    dm.addChangeListener(this);

    // Trigger rescan for Tracktion Engine to pick up MIDI devices
    dm.rescanMidiDeviceList();
    DBG("MIDI device rescan triggered (async, listener registered)");

    // Enable Tracktion Engine MIDI input devices
    for (auto& midiInput : dm.getMidiInDevices()) {
        if (midiInput && !midiInput->isEnabled()) {
            midiInput->setEnabled(true);
            DBG("Enabled TE MIDI input device: " << midiInput->getName());
        }
    }
}

void TracktionEngineWrapper::createEditAndBridges() {
    // Create a temporary Edit (project)
    auto editFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("magda_temp.tracktionedit");

    // Delete any existing temp file to ensure clean state
    if (editFile.existsAsFile()) {
        editFile.deleteFile();
    }

    currentEdit_ = tracktion::createEmptyEdit(*engine_, editFile);

    if (!currentEdit_) {
        std::cout << "Tracktion Engine initialized (no Edit created)" << std::endl;
        return;
    }

    // Set default tempo
    auto& tempoSeq = currentEdit_->tempoSequence;
    if (tempoSeq.getNumTempos() > 0) {
        auto tempo = tempoSeq.getTempo(0);
        if (tempo) {
            tempo->setBpm(120.0);
        }
    }

    // Ensure playback context is created for MIDI routing
    currentEdit_->getTransport().ensureContextAllocated();
    if (auto* ctx = currentEdit_->getCurrentPlaybackContext()) {
        DBG("Playback context allocated for live MIDI monitoring");
        DBG("  Total inputs in context: " << ctx->getAllInputs().size());
    } else {
        DBG("WARNING: ensureContextAllocated() called but context is still null!");
    }

    // Create AudioBridge for TrackManager synchronization
    audioBridge_ = std::make_unique<AudioBridge>(*engine_, *currentEdit_);
    audioBridge_->syncAll();

    // Create SessionClipScheduler and PluginWindowManager only when NOT in headless CI
    // Both extend juce::Timer which creates GUI infrastructure and leaks in tests
    // Check: Skip if DISPLAY env var not set (Linux headless) or if explicitly disabled
    auto osType = juce::SystemStats::getOperatingSystemType();
    bool isMacOS = (osType & juce::SystemStats::MacOSX) != 0;
    bool isWindows = (osType & juce::SystemStats::Windows) != 0;
    bool isHeadless = (std::getenv("DISPLAY") == nullptr && !isMacOS && !isWindows);

    if (!isHeadless) {
        sessionScheduler_ = std::make_unique<SessionClipScheduler>(*audioBridge_, *currentEdit_);
        pluginWindowManager_ = std::make_unique<PluginWindowManager>(*engine_, *currentEdit_);
        audioBridge_->setPluginWindowManager(pluginWindowManager_.get());
    }

    // Configure AudioBridge
    audioBridge_->setEngineWrapper(this);
    audioBridge_->enableAllMidiInputDevices();

    // Create MidiBridge for MIDI device management
    midiBridge_ = std::make_unique<MidiBridge>(*engine_);
    midiBridge_->setAudioBridge(audioBridge_.get());
    midiBridge_->setRecordingQueue(&recordingNoteQueue_, &transportPositionForMidi_);

    // Register as transport listener for recording callbacks
    currentEdit_->getTransport().addListener(this);

    std::cout << "Tracktion Engine initialized with Edit, AudioBridge, and MidiBridge" << std::endl;
}

bool TracktionEngineWrapper::initialize() {
    try {
        // Initialize Tracktion Engine with custom UIBehaviour for plugin windows
        auto uiBehaviour = std::make_unique<MagdaUIBehaviour>();
        auto engineBehaviour = std::make_unique<MagdaEngineBehaviour>();
        engine_ = std::make_unique<tracktion::Engine>("MAGDA", std::move(uiBehaviour),
                                                      std::move(engineBehaviour));

        // Initialize plugin formats and load plugin list
        initializePluginFormats();

        // Initialize device manager with preferred settings
        initializeDeviceManager();

        // Configure audio devices if user has preferences
        configureAudioDevices();

        // Setup MIDI devices
        setupMidiDevices();

        // Create Edit and bridges
        createEditAndBridges();

        // Ensure devicesLoading_ is cleared so transport isn't blocked
        // The async changeListenerCallback may not fire if no MIDI devices are present
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

    // Remove transport listener before destroying edit
    if (currentEdit_) {
        currentEdit_->getTransport().removeListener(this);
    }

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

}  // namespace magda
