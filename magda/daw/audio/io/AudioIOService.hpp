#pragma once

/**
 * @file AudioIOService.hpp
 * @brief The one owner of hardware audio I/O (#2746).
 */

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>
#include <optional>
#include <vector>

#include "../../core/Config.hpp"
#include "AudioIOControl.hpp"

namespace magda {

/**
 * @brief Backend, interface and the channels open on it, opened only as asked (#2746).
 *
 * No mask is derived from the channels an interface advertises: a virtual endpoint offering
 * 128 stays a stereo stream unless 128 were chosen (#2528).
 */
class AudioIOService : public AudioIOControl, private juce::ChangeListener {
  public:
    /** @brief What is open, as the interface reports it. Empty when nothing is. */
    struct ActiveConfiguration {
        juce::String backend;
        juce::String inputInterface;
        juce::String outputInterface;
        double sampleRate = 0.0;
        int bufferSize = 0;
        juce::BigInteger inputChannels;
        juce::BigInteger outputChannels;
        /// Every channel the open interface has, by its own names (#2737).
        juce::StringArray inputChannelNames;
        juce::StringArray outputChannelNames;
        int inputLatencySamples = 0;
    };

    /** @brief On the platform's backends, migrating from MAGDA's Tracktion Settings.xml. */
    AudioIOService();

    /** @brief On @p backends instead, with @p tracktionSettings as what a first run migrates. */
    AudioIOService(std::vector<std::unique_ptr<juce::AudioIODeviceType>> backends,
                   juce::File tracktionSettings);

    ~AudioIOService() override;

    AudioIOService(const AudioIOService&) = delete;
    AudioIOService& operator=(const AudioIOService&) = delete;

    /**
     * @brief Open what was saved, else what Tracktion saved, else stereo out and no input.
     *
     * A failure is logged and leaves nothing open, so the app still launches.
     */
    void open();

    /** @brief Open @p settings and save them as the user's choice; returns the open error. */
    juce::String apply(const AudioIOSettings& settings) override;

    /** @brief What was saved, or what is open when nothing was. */
    AudioIOSettings chosen() const override;

    juce::StringArray channelNames(const juce::String& backend, const juce::String& interfaceName,
                                   bool inputs) override {
        return getChannelNames(backend, interfaceName, inputs);
    }

    juce::StringArray getBackendNames();
    juce::StringArray getInterfaceNames(const juce::String& backend, bool inputs);

    juce::StringArray backendNames() override {
        return getBackendNames();
    }
    juce::StringArray interfaceNames(const juce::String& backend, bool inputs) override {
        return getInterfaceNames(backend, inputs);
    }
    bool isSingleInterfaceBackend(const juce::String& backend) override;
    std::vector<double> availableSampleRates() const override;
    std::vector<int> availableBufferSizes() const override;

    void addCallback(juce::AudioIODeviceCallback* callback) override {
        manager_.addAudioCallback(callback);
    }
    void removeCallback(juce::AudioIODeviceCallback* callback) override {
        manager_.removeAudioCallback(callback);
    }
    Status status() const override;

    /** @brief What @p interfaceName calls its channels, whether or not it is open. */
    juce::StringArray getChannelNames(const juce::String& backend,
                                      const juce::String& interfaceName, bool inputs);

    ActiveConfiguration getActiveConfiguration() const;

    /** @brief What is open, in the form apply() takes. */
    AudioIOSettings openSettings() const;

    bool isOpen() const override;
    Direction inputs() const override;
    Direction outputs() const override;

    /** @brief Where an audio callback attaches. Configuration goes through open() and apply(). */
    juce::AudioDeviceManager& getDeviceManager() {
        return manager_;
    }

  private:
    juce::AudioIODeviceType* backendNamed(const juce::String& name);
    juce::AudioIODeviceType* defaultBackend();

    /** @brief @p wanted cut to the interfaces present and the channels they have. */
    AudioIOSettings fit(const std::optional<AudioIOSettings>& wanted);

    juce::String openFitted(const AudioIOSettings& fitted);
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    juce::AudioDeviceManager manager_;
    juce::File tracktionSettings_;
};

}  // namespace magda
