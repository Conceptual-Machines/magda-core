#include "FaustResources.hpp"

#include <BinaryData.h>

namespace magda::daw::audio {

namespace {

juce::String readBinaryDspAsString(const char* resourceName) {
    int size = 0;
    if (auto* data = BinaryData::getNamedResource(resourceName, size); data && size > 0)
        return juce::String::fromUTF8(data, size);
    return {};
}

}  // namespace

juce::File getFaustLibrariesPath() {
    auto exe = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
#if JUCE_MAC
    // currentApplicationFile is the .app bundle on macOS. Libraries live in
    // Contents/Resources/faustlibraries (matches CMake's POST_BUILD copy).
    return exe.getChildFile("Contents/Resources/faustlibraries");
#else
    // On Windows/Linux, libraries sit next to the executable.
    return exe.getParentDirectory().getChildFile("faustlibraries");
#endif
}

std::vector<StarterDsp> getBundledStarterDsps() {
    // Resource names follow juce_add_binary_data's slugifier: dots → underscores.
    static const struct {
        const char* displayName;
        const char* filename;
        const char* resourceName;
        FaustCustomViewKind viewKind;
    } kStarters[] = {
        {"Drive", "magda_drive.dsp", "magda_drive_dsp", FaustCustomViewKind::MagdaDrive},
        {"Tremolo", "magda_tremolo.dsp", "magda_tremolo_dsp", FaustCustomViewKind::None},
        {"Delay", "magda_delay.dsp", "magda_delay_dsp", FaustCustomViewKind::None},
        {"Granular Delay", "magda_granular_delay.dsp", "magda_granular_delay_dsp",
         FaustCustomViewKind::None},
        {"SVF", "magda_filter_svf.dsp", "magda_filter_svf_dsp", FaustCustomViewKind::None},
        {"Ladder", "magda_filter_ladder.dsp", "magda_filter_ladder_dsp", FaustCustomViewKind::None},
        {"Korg 35", "magda_filter_korg35.dsp", "magda_filter_korg35_dsp",
         FaustCustomViewKind::None},
        {"Oberheim", "magda_filter_oberheim.dsp", "magda_filter_oberheim_dsp",
         FaustCustomViewKind::None},
        {"Sallen-Key", "magda_filter_sk.dsp", "magda_filter_sk_dsp", FaustCustomViewKind::None},
    };

    std::vector<StarterDsp> out;
    for (const auto& s : kStarters) {
        auto src = readBinaryDspAsString(s.resourceName);
        if (src.isNotEmpty())
            out.push_back({juce::String(s.displayName), juce::String(s.filename), src, s.viewKind});
    }
    return out;
}

}  // namespace magda::daw::audio
