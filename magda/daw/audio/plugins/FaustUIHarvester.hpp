#pragma once

#include <map>
#include <vector>

#include "FaustParamPool.hpp"
#include "faust/gui/UI.h"

namespace magda::daw::audio {

/**
 * Walks a Faust UI tree and emits the controls consumed by FaustParamPool.
 *
 * Both the effect and instrument paths use this implementation so label
 * annotations, declare() metadata, widget kinds, and group labels have one
 * interpretation. Polyphonic Faust DSPs add structural proxy/voice boxes
 * around the author's UI; PolyphonicVoices mode removes those proxy controls
 * and reports the outermost author group instead.
 */
class FaustUIHarvester final : public ::UI {
  public:
    enum class Layout {
        Standard,
        PolyphonicVoices,
    };

    explicit FaustUIHarvester(Layout layout = Layout::Standard) : layout_(layout) {}

    const std::vector<HarvestedControl>& controls() const {
        return controls_;
    }

    /// Bargraphs, in declaration order. One entry per occurrence: a
    /// polyphonic DSP reports the same author bargraph once per voice, and
    /// merging those is the instrument's job, not the harvester's.
    const std::vector<HarvestedOutput>& outputs() const {
        return outputs_;
    }

    void openTabBox(const char* label) override;
    void openHorizontalBox(const char* label) override;
    void openVerticalBox(const char* label) override;
    void closeBox() override;

    void addButton(const char* label, FAUSTFLOAT* zone) override;
    void addCheckButton(const char* label, FAUSTFLOAT* zone) override;
    void addVerticalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                           FAUSTFLOAT max, FAUSTFLOAT step) override;
    void addHorizontalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                             FAUSTFLOAT max, FAUSTFLOAT step) override;
    void addNumEntry(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                     FAUSTFLOAT max, FAUSTFLOAT step) override;

    void addHorizontalBargraph(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT min,
                               FAUSTFLOAT max) override;
    void addVerticalBargraph(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT min,
                             FAUSTFLOAT max) override;
    void addSoundfile(const char*, const char*, Soundfile**) override {}

    void declare(FAUSTFLOAT* zone, const char* key, const char* value) override;

  private:
    void pushGroup(const char* label);
    ControlMetadata mergedMetadataFor(FAUSTFLOAT* zone);
    void emitControl(FaustParamSlot::Kind kind, const char* rawLabel, FAUSTFLOAT* zone,
                     FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step);
    void emitOutput(bool vertical, const char* rawLabel, FAUSTFLOAT* zone, FAUSTFLOAT min,
                    FAUSTFLOAT max);
    bool isInsidePolyProxyGroup() const;
    juce::String controlGroup() const;

    Layout layout_;
    std::vector<HarvestedControl> controls_;
    std::vector<HarvestedOutput> outputs_;
    std::vector<ControlMetadata> groupMetadataStack_;
    std::vector<juce::String> groupLabelStack_;
    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

}  // namespace magda::daw::audio
