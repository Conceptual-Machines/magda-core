#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

/**
 * @file AraIdentityPlugin.cpp
 * @brief An ARA effect that plays its regions' source audio back unchanged (#1579).
 *
 * The host-side ARA tests load it as a real plugin: whatever it renders came through the host's
 * audio access controller, so a render equal to the file proves the document and the reads.
 */

namespace {

/** @brief Each region's source, read through the host and laid at its playback position. */
class IdentityRenderer final : public juce::ARAPlaybackRenderer {
  public:
    using juce::ARAPlaybackRenderer::ARAPlaybackRenderer;

    void prepareToPlay(double rate, int maxBlock, int channels,
                       juce::AudioProcessor::ProcessingPrecision, AlwaysNonRealtime) override {
        sampleRate_ = rate;
        scratch_.setSize(channels, maxBlock);
        for (auto* region : getPlaybackRegions()) {
            auto* source = region->getAudioModification()->getAudioSource();
            if (!readers_.contains(source))
                readers_.emplace(source, std::make_unique<juce::ARAAudioSourceReader>(source));
        }
    }

    void releaseResources() override {
        readers_.clear();
    }

    bool processBlock(juce::AudioBuffer<float>& buffer, juce::AudioProcessor::Realtime,
                      const juce::AudioPlayHead::PositionInfo& position) noexcept override {
        buffer.clear();
        if (!position.getIsPlaying())
            return true;

        const auto block = juce::Range<juce::int64>::withStartAndLength(
            position.getTimeInSamples().orFallback(0), buffer.getNumSamples());

        for (auto* region : getPlaybackRegions()) {
            const auto playback = region->getSampleRange(
                sampleRate_, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);
            const auto range = block.getIntersectionWith(playback);
            if (range.isEmpty())
                continue;

            auto* source = region->getAudioModification()->getAudioSource();
            const auto offset = region->getStartInAudioModificationSamples() - playback.getStart();
            const int count = static_cast<int>(range.getLength());
            const int at = static_cast<int>(range.getStart() - block.getStart());

            if (!readers_.at(source)->read(&scratch_, 0, count, range.getStart() + offset, true,
                                           true))
                return false;
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.addFrom(c, at, scratch_, c, 0, count);
        }
        return true;
    }

  private:
    double sampleRate_ = 44100.0;
    juce::AudioBuffer<float> scratch_;
    std::map<juce::ARAAudioSource*, std::unique_ptr<juce::ARAAudioSourceReader>> readers_;
};

/** @brief Nothing to archive: the render is a function of the source alone. */
class IdentityDocument final : public juce::ARADocumentControllerSpecialisation {
  public:
    using juce::ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;

  protected:
    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override {
        return new IdentityRenderer(getDocumentController());
    }

    bool doRestoreObjectsFromStream(juce::ARAInputStream&,
                                    const juce::ARARestoreObjectsFilter*) noexcept override {
        return true;
    }

    bool doStoreObjectsToStream(juce::ARAOutputStream&,
                                const juce::ARAStoreObjectsFilter*) noexcept override {
        return true;
    }
};

/** @brief A stereo effect whose only processing is its ARA renderer. */
class IdentityProcessor final : public juce::AudioProcessor,
                                private juce::AudioProcessorARAExtension {
  public:
    IdentityProcessor()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    void prepareToPlay(double rate, int block) override {
        prepareToPlayForARA(rate, block, getMainBusNumOutputChannels(), getProcessingPrecision());
    }

    void releaseResources() override {
        releaseResourcesForARA();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        if (!processBlockForARA(buffer, isRealtime(), getPlayHead()))
            buffer.clear();
    }

    using AudioProcessor::processBlock;

    juce::AudioProcessorARAExtension* getARAClientExtensions() override {
        return this;
    }

    const juce::String getName() const override {
        return "MAGDA ARA Identity";
    }
    bool acceptsMidi() const override {
        return false;
    }
    bool producesMidi() const override {
        return false;
    }
    double getTailLengthSeconds() const override {
        return 0.0;
    }
    juce::AudioProcessorEditor* createEditor() override {
        return nullptr;
    }
    bool hasEditor() const override {
        return false;
    }
    int getNumPrograms() override {
        return 1;
    }
    int getCurrentProgram() override {
        return 0;
    }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {
        return {};
    }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

}  // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new IdentityProcessor();
}

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory() {
    return juce::ARADocumentControllerSpecialisation::createARAFactory<IdentityDocument>();
}
