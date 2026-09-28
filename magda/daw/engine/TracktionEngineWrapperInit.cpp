#include <cstdlib>

#include "../api/magda_api_live.hpp"
#include "../audio/AudioBridge.hpp"
#include "../audio/DeviceParameterDisplayTextProvider.hpp"
#include "../audio/MidiBridge.hpp"
#include "../audio/TrackMeters.hpp"
#include "../audio/controllers/ControllerRouter.hpp"
#include "../audio/insert_capture/InsertRenderCaptureService.hpp"
#include "../audio/session/SessionClipScheduler.hpp"
#include "../audio/session/SessionRecorder.hpp"
#include "../core/AppPaths.hpp"
#include "../core/Config.hpp"
#include "../core/controllers/MidiLearnCoordinator.hpp"
#include "AppServices.hpp"
#include "AudioEngineChoice.hpp"
#if MAGDA_HAS_NATIVE_ENGINE
    #include "MagdaAudioEngine.hpp"
    #include "host/ClickSounds.hpp"
#endif
#include "MagdaEngineBehaviour.hpp"
#include "MagdaUIBehaviour.hpp"
#include "PluginService.hpp"
#include "PluginWindowManager.hpp"
#include "TempoLaneSync.hpp"
#include "TracktionEngineWrapper.hpp"
#include "TracktionTempoMap.hpp"
#include "WaveDeviceChannels.hpp"

namespace magda {

TracktionEngineWrapper::TracktionEngineWrapper()
    : tempoMap_(std::make_unique<TracktionTempoMap>([this] { return getEdit(); })) {}

TracktionEngineWrapper::~TracktionEngineWrapper() {
    shutdown();
}

std::unique_ptr<AudioEngine> createDefaultAudioEngine(AudioEngineOptions options) {
    // The setting is read before either engine exists to read it: the config is
    // otherwise loaded inside initialize(), which runs after this call has
    // already chosen (#2559). Loading it here rather than moving that call
    // leaves every other caller of initialize() alone, and load() only reads
    // the file into fields.
    Config::getInstance().load();

    const auto choice = chosenAudioEngine();

    // Out loud, once, wherever an engine is built. Which one a session ran on
    // is the first question any report about it raises, and the answer should
    // not need a debugger -- or a guess about which construction site the app
    // took.
    juce::Logger::writeToLog(juce::String("[engine] rendering through ") + nameOf(choice) +
                             " (#2551)");

#if MAGDA_HAS_NATIVE_ENGINE
    if (choice == AudioEngineChoice::Magda)
        return std::make_unique<MagdaAudioEngine>(options);
#else
    juce::ignoreUnused(choice);
#endif

    auto engine = std::make_unique<TracktionEngineWrapper>();
    engine->setForceHeadless(options.headless);
    return engine;
}

bool TracktionEngineWrapper::isHeadlessRuntime() const {
    return app_services::isHeadless(forceHeadless_);
}

// Tracktion's click plays sample files; hand it native's two synthesised clicks so the
// metronome sounds the same on both engines (#2802).
void TracktionEngineWrapper::useNativeClickSounds() {
#if MAGDA_HAS_NATIVE_ENGINE
    constexpr double kSampleRate = 96000.0;
    const auto folder = paths::dataDir().getChildFile("Click");
    if (!folder.createDirectory())
        return;

    auto& storage = engine_->getPropertyStorage();
    for (const bool accent : {true, false}) {
        const auto file = folder.getChildFile(accent ? "accent.wav" : "beat.wav");
        const auto sound = daw::engine_host::renderClickSound(accent, kSampleRate);

        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        if (stream == nullptr)
            continue;
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                                                      .withSampleRate(kSampleRate)
                                                      .withNumChannels(1)
                                                      .withBitsPerSample(24));
        if (writer == nullptr ||
            !writer->writeFromAudioSampleBuffer(sound, 0, sound.getNumSamples()))
            continue;
        writer.reset();

        storage.setProperty(accent ? tracktion::SettingID::clickTrackSampleBig
                                   : tracktion::SettingID::clickTrackSampleSmall,
                            file.getFullPathName());
    }
#endif
}

void TracktionEngineWrapper::initializePluginFormats() {
    // Register ToneGeneratorPlugin (not registered by default)
    engine_->getPluginManager().createBuiltInType<tracktion::ToneGeneratorPlugin>();

    // Out-of-process, so a plugin that crashes on scan does not take the app with it.
    auto& pluginManager = engine_->getPluginManager();
    pluginManager.setUsesSeparateProcessForScanning(true);

    // The list Tracktion's own hosting reads is the one the service answers off, until
    // the fork goes (#2557).
    auto& plugins = PluginService::getInstance();
    plugins.useEngineList(pluginManager.pluginFormatManager, pluginManager.knownPluginList);
    plugins.setInternalParameterScanner(
        [this](const juce::String& pluginId) { return scanInternalParametersInEdit(pluginId); });
    plugins.openList(!isHeadlessRuntime() && Config::getInstance().getScanPluginsOnStartup());

    auto& formatManager = pluginManager.pluginFormatManager;
    DBG("Plugin formats registered by Tracktion Engine: " << formatManager.getNumFormats());
    for (int i = 0; i < formatManager.getNumFormats(); ++i) {
        auto* format = formatManager.getFormat(i);
        if (format) {
            DBG("  Format " << i << ": " << format->getName());
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

    // Validate saved audio device setup before TE reads it — if the saved state
    // has a missing input or output device name, CoreAudio will hang trying to
    // open a half-configured aggregate device.
    {
        auto& storage = engine_->getPropertyStorage();
        auto audioXml = storage.getXmlProperty(tracktion::SettingID::audio_device_setup);
        if (audioXml != nullptr) {
            auto* deviceSetup = audioXml->getChildByName("DEVICESETUP");
            if (deviceSetup != nullptr) {
                auto savedInput = deviceSetup->getStringAttribute("audioInputDeviceName");
                auto savedOutput = deviceSetup->getStringAttribute("audioOutputDeviceName");
                // If either device name is empty while the other is set, the saved
                // state is incomplete and will cause CoreAudio to hang on init.
                if ((savedInput.isNotEmpty() && savedOutput.isEmpty()) ||
                    (savedInput.isEmpty() && savedOutput.isNotEmpty())) {
                    storage.removeProperty(tracktion::SettingID::audio_device_setup);
                }
            }
        }
    }

    // Request all available channels — Tracktion creates WaveInputDevices
    // for all hardware channels and expects them all in the audio callback.
    // JUCE clamps to actual hardware count.
    static constexpr int kMaxRequestedChannels = 256;
    int inputChannels = kMaxRequestedChannels;
    int outputChannels = kMaxRequestedChannels;
    dm.initialise(inputChannels, outputChannels);
    DBG("DeviceManager initialized with " << inputChannels << " input / " << outputChannels
                                          << " output channels");

    if (juceDeviceManager.getCurrentAudioDevice() == nullptr)
        DBG("WARNING: No audio device opened after initialise - user can configure in Audio "
            "Settings");
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
    const auto& deviceTypes = juceDeviceManager.getAvailableDeviceTypes();

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

    // Apply the device setup — let JUCE pick default channels for the new device,
    // then we'll enable all channels after the device has opened.
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = true;

    auto result = juceDeviceManager.setAudioDeviceSetup(setup, true);
    // Flush pending async updates so the new device is fully active
    juce::MessageManager::getInstance()->runDispatchLoopUntil(0);
    if (result.isEmpty()) {
        DBG("Successfully selected preferred devices - Input: "
            << setup.inputDeviceName << " (" << preferredInputs
            << " ch), Output: " << setup.outputDeviceName << " (" << preferredOutputs << " ch)");
    } else {
        DBG("Failed to select preferred devices: " << result);
    }

    // Enable ALL hardware channels on the NEW device — Tracktion expects every
    // hardware channel in the audio callback. We must read channel count from
    // the new device, not the old one.
    // Note: setAudioDeviceSetup triggers TE's changeListenerCallback which calls
    // saveSettings() + rescanWaveDeviceList() automatically. The flush processes
    // the async rescan so wave devices are rebuilt before we configure them.
    if (auto* device = juceDeviceManager.getCurrentAudioDevice()) {
        auto newSetup = juceDeviceManager.getAudioDeviceSetup();
        newSetup.inputChannels.clear();
        newSetup.inputChannels.setRange(0, device->getInputChannelNames().size(), true);
        newSetup.outputChannels.clear();
        newSetup.outputChannels.setRange(0, device->getOutputChannelNames().size(), true);
        newSetup.useDefaultInputChannels = false;
        newSetup.useDefaultOutputChannels = false;
        juceDeviceManager.setAudioDeviceSetup(newSetup, true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(0);
    }

    // Apply saved channel preferences at the TE wave device level
    if (preferredInputs > 0) {
        const auto withinPreferredInputs = [preferredInputs](int index) {
            return index < preferredInputs;
        };
        enableDevicesForChannels(dm.getWaveInputDevices(), withinPreferredInputs);
        DBG("Applied preferred input channel count: " << preferredInputs);
    }
    if (preferredOutputs > 0) {
        const auto withinPreferredOutputs = [preferredOutputs](int index) {
            return index < preferredOutputs;
        };
        enableDevicesForChannels(dm.getWaveOutputDevices(), withinPreferredOutputs);
        DBG("Applied preferred output channel count: " << preferredOutputs);
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

InsertRenderCapture* TracktionEngineWrapper::getInsertRenderCapture() {
    return insertRenderCapture_.get();
}

bool TracktionEngineWrapper::initialiseServices() {
    // Initialize Tracktion Engine with custom UIBehaviour for plugin windows
    juce::Logger::writeToLog("[Init] Creating Tracktion Engine...");
    engine_ = std::make_unique<tracktion::Engine>(
        std::make_unique<tracktion::PropertyStorage>("MAGDA"), std::make_unique<MagdaUIBehaviour>(),
        std::make_unique<MagdaEngineBehaviour>());
    audioIO_ = std::make_unique<TracktionAudioIO>(engine_->getDeviceManager());

    // Config before the devices, whose preferred settings it holds.
    app_services::bringUp();

    useNativeClickSounds();

    // Initialize plugin formats and load plugin list
    juce::Logger::writeToLog("[Init] initializePluginFormats()...");
    initializePluginFormats();
    juce::Logger::writeToLog("[Init] initializePluginFormats() done");

    if (!isHeadlessRuntime()) {
        // Initialize device manager with preferred settings
        juce::Logger::writeToLog("[Init] initializeDeviceManager()...");
        initializeDeviceManager();
        juce::Logger::writeToLog("[Init] initializeDeviceManager() done");

        // Configure audio devices if user has preferences
        juce::Logger::writeToLog("[Init] configureAudioDevices()...");
        configureAudioDevices();
        juce::Logger::writeToLog("[Init] configureAudioDevices() done");

        // Setup MIDI devices
        juce::Logger::writeToLog("[Init] setupMidiDevices()...");
        setupMidiDevices();
        juce::Logger::writeToLog("[Init] setupMidiDevices() done");
    } else {
        juce::Logger::writeToLog("[Init] Headless mode: skipping audio/MIDI device startup");
    }

    // MIDI is the app's service; this engine lends it the virtual devices Tracktion
    // holds, which the system's MIDI list never carries (#2759). Enabled ones only: the
    // routing selectors relist on midiDeviceListChanged, so the filter is effective.
    auto& midiBridge = MidiBridge::getInstance();
    midiBridge.useEngine(this, [this] {
        std::vector<MidiDeviceInfo> devices;
        for (const auto& device : engine_->getDeviceManager().getMidiInDevices()) {
            if (dynamic_cast<te::VirtualMidiInputDevice*>(device.get()) == nullptr ||
                !device->isEnabled())
                continue;
            devices.emplace_back(device->getDeviceID(), device->getName(), /*enabled=*/true);
        }
        return devices;
    });
    midiBridge.setMeters(&meters_);

    lendEngineServices();

    return engine_ != nullptr;
}

void TracktionEngineWrapper::lendEngineServices() {
    // What this engine answers for, off the services that own each concern (#2757).
    GrooveLibrary::getInstance().setStore(
        [this] { return readGrooveTemplates(); },
        [this](const GrooveTemplateData& groove) { return upsertGrooveTemplate(groove); });
    SamplerMedia::getInstance().setProvider([this] { return getSamplerMediaReferences(); });
    setTempoSequenceRippleBuilder(
        [this](TempoSequenceRippleMode mode, BeatPosition start, BeatPosition end) {
            return buildTempoSequenceRipple(mode, start, end);
        });
    setDeviceParameterFormatter(
        [this](const ChainNodePath& devicePath, int paramIndex, float normalised) {
            return formatDeviceParameter(devicePath, paramIndex, normalised);
        });
}

bool TracktionEngineWrapper::initialisePlayback() {
    juce::Logger::writeToLog("[Init] initialisePlayback()...");

    // Create a temporary Edit (project)
    auto editFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("magda_temp.tracktionedit");

    // Delete any existing temp file to ensure clean state
    if (editFile.existsAsFile()) {
        editFile.deleteFile();
    }

    currentEdit_ = tracktion::createEmptyEdit(*engine_, editFile);

    if (!currentEdit_) {
        DBG("Tracktion Engine initialized (no Edit created)");
        return false;
    }

    // Set default tempo
    auto& tempoSeq = currentEdit_->tempoSequence;
    if (tempoSeq.getNumTempos() > 0) {
        auto* tempo = tempoSeq.getTempo(0);
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
    audioBridge_ = std::make_unique<AudioBridge>(*engine_, *currentEdit_, meters_, deviceMeters_);
    audioBridge_->syncAll();
    MidiBridge::getInstance().onActiveInputsChanged = [this] {
        audioBridge_->refreshActiveMidiInputs();
    };

#ifndef MAGDA_NO_AUTO_TEMPO_LANE_SYNC
    // Keep the edit-scoped Tempo automation lane and tempoSequence in sync.
    // Disabled in test builds (the shared engine would attach to the
    // AutomationManager singleton and perturb other tests).
    tempoLaneSync_ = std::make_unique<TempoLaneSync>(*currentEdit_);
#endif

    // Create SessionClipScheduler and PluginWindowManager only when NOT in headless CI
    // Both extend juce::Timer which creates GUI infrastructure and leaks in tests
    // Check: Skip if DISPLAY env var not set (Linux headless) or if explicitly disabled
    if (!isHeadlessRuntime()) {
        sessionScheduler_ = std::make_unique<SessionClipScheduler>(
            *audioBridge_, *currentEdit_, audioBridge_->getSessionAudioMonitor());
        // Install the session monitor plugin up-front so the audio-thread
        // pulse (transport position, beat indicator) keeps ticking even
        // before any session clip has been launched.
        audioBridge_->ensureSessionMonitorPlugin();
        sessionRecorder_ = std::make_unique<SessionRecorder>(*currentEdit_);
        sessionRecorder_->setRecordingPreviews(&recordingPreviews_);
        sessionRecorder_->setPlayStateQuery([this](ClipId clipId) {
            return sessionScheduler_ ? sessionScheduler_->getClipPlayState(clipId)
                                     : SessionClipPlayState::Stopped;
        });
        sessionRecorder_->setLaunchTimeQuery([this](TrackId trackId) {
            return audioBridge_ ? audioBridge_->getLastLaunchTimeForTrack(trackId) : 0.0;
        });
        pluginWindowManager_ = std::make_unique<PluginWindowManager>(*engine_, *currentEdit_);
        audioBridge_->setPluginWindowManager(pluginWindowManager_.get());
        // The export capture pass needs the live transport + hardware I/O;
        // pointless (and Timer-based) in the headless runtime.
        insertRenderCapture_ = std::make_unique<InsertRenderCaptureService>(*currentEdit_);
    }

    // Configure AudioBridge
    audioBridge_->enableAllMidiInputDevices();

    MidiBridge::getInstance().setAudioBridge(audioBridge_.get());
    MidiBridge::getInstance().setRecordingQueue(&recordingNoteQueue_, &transportPositionForMidi_);

    // Track-routed MIDI ("track:N" inputs) bypasses MidiBridge entirely, so the
    // recording preview for those tracks is fed by MidiInputRouter via TE input
    // consumers into its own single-producer queue.
    audioBridge_->setTrackMidiRecordingQueue(&trackMidiRecordingNoteQueue_,
                                             &transportPositionForMidi_);

    // Register as transport listener for recording callbacks
    currentEdit_->getTransport().addListener(this);

    // Programmatic facade onto DAW state — shared with AI Chat panel and
    // app-level Lua controller wiring.
    auto live = std::make_unique<MagdaApiLive>();
    live->setMidiBridge(&MidiBridge::getInstance());
    live->setProjectTempoWriter([this](double bpm) { setTempo(bpm); });
    live->setProjectTimeSignatureWriter(
        [this](int numerator, int denominator) { setTimeSignature(numerator, denominator); });
    live->setProjectLoopRangeWriter([this](double start, double end) {
        setLoopRegionBeats({{start}, {end}});
    });
    live->setProjectTempoMap([this] { return tempoMap(); });
    live->setEditAccessor([this]() -> tracktion::Edit* { return currentEdit_.get(); });
    magdaApi_ = std::move(live);

    juce::Logger::writeToLog("[Init] initialisePlayback() done");
    return currentEdit_ != nullptr;
}

bool TracktionEngineWrapper::initialize() {
    try {
        const bool ready = initialiseServices() && initialisePlayback();

        juce::Logger::writeToLog("[Init] initialize() complete, edit=" +
                                 juce::String(currentEdit_ != nullptr ? "OK" : "NULL"));
        return ready;

    } catch (const std::exception& e) {
        juce::Logger::writeToLog("ERROR: Failed to initialize: " + juce::String(e.what()));
        return false;
    }
}

void TracktionEngineWrapper::shutdown() {
    DBG("TracktionEngineWrapper::shutdown - starting...");

    // Stop the service reaching the AudioBridge before that bridge is torn down. This is
    // also safe for a wrapper that was never selected as TrackManager's renderer.
    GrooveLibrary::getInstance().forgetStore();
    SamplerMedia::getInstance().forgetProvider();
    forgetTempoSequenceRippleBuilder();
    forgetDeviceParameterFormatter();

    PluginService::getInstance().forgetStateProvider(*this);

    // The service answers off the list this engine owns, so it lets go -- and joins its
    // discovery thread -- before any of it is torn down (#2756).
    PluginService::getInstance().forgetEngineList();

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
    audioIO_.reset();

    // CRITICAL: Close all plugin windows FIRST (before plugins are destroyed)
    // This prevents malloc errors from windows trying to access destroyed plugins
    if (pluginWindowManager_) {
        DBG("Closing all plugin windows...");
        pluginWindowManager_->closeAllWindows();
        // Detach the bridge's raw pointer BEFORE freeing the manager so a queued
        // open-window callback (e.g. open-on-drop) sees null instead of a
        // dangling PluginWindowManager*.
        if (audioBridge_)
            audioBridge_->setPluginWindowManager(nullptr);
        pluginWindowManager_.reset();
    }

    // Cancel any export capture pass and drop the service before the Edit
    // goes away (it references it).
    insertRenderCapture_.reset();

    // Detach the Tempo lane sync first (it listens to tempoSequence + the
    // AutomationManager singleton, and holds an Edit reference).
    tempoLaneSync_.reset();

    // Destroy session scheduler before AudioBridge (it references both)
    if (sessionScheduler_) {
        sessionScheduler_.reset();
    }

    // Hand the MIDI service back BEFORE destroying the AudioBridge it was lent, and only
    // when this wrapper is the one attached: a wrapper that never came up, or one another
    // engine has since attached over, would otherwise unwind the live engine's MIDI.
    //
    // Clearing the pointer is not enough on its own. A MIDI callback that already loaded
    // it goes on holding it, so the AudioBridge has to outlive the drain rather than the
    // store: forgetEngine() stops the inputs and does not return until every callback in
    // flight has left. That also unregisters the CoreMIDI callbacks, which must happen
    // while the MIDI devices still exist -- they are closed further down.
    auto& midiBridge = MidiBridge::getInstance();
    const bool ownsMidi = midiBridge.isAttachedTo(this);
    if (ownsMidi) {
        // Cancel any active MIDI Learn session before shutting down the router.
        MidiLearnCoordinator::getInstance().cancelLearn();
        // Shut down ControllerRouter before stopping MIDI inputs so it can unsubscribe
        // cleanly.
        ControllerRouter::getInstance().shutdown();
        DBG("Stopping MIDI inputs...");
        midiBridge.forgetEngine(this);
    }

    // Destroy AudioBridge first (it references Edit and Engine)
    if (audioBridge_) {
        audioBridge_.reset();
    }

    // CRITICAL: Stop transport and release playback context BEFORE destroying Edit
    // This ensures audio/MIDI devices are properly released
    if (currentEdit_) {
        DBG("Stopping transport and releasing playback context...");
        auto& transport = currentEdit_->getTransport();

        // Stop playback if running
        if (transport.isPlaying()) {
            transport.stop(false, false);
        }

        // Release the playback context - this frees audio/MIDI device resources
        transport.freePlaybackContext();

        DBG("Destroying Edit...");
        currentEdit_.reset();
    }

    // MagdaApi is a thin facade over singletons — safe to reset anytime,
    // but match teardown order with construction.
    magdaApi_.reset();

    // Close audio/MIDI devices before destroying engine
    if (engine_) {
        DBG("Closing audio devices...");
        auto& dm = engine_->getDeviceManager();
        dm.closeDevices();

        DBG("Destroying Tracktion Engine...");
        engine_.reset();
    }

    DBG("Tracktion Engine shutdown complete");
}

}  // namespace magda
