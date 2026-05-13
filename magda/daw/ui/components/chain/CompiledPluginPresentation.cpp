#include "CompiledPluginPresentation.hpp"

#include <algorithm>

#include "audio/plugins/compiled/MagdaChorusCompiledPlugin.hpp"

namespace magda::daw::ui {

// Chorus has no inline curve view yet (Phase 1 ships without one). Its
// presentation spec lives here rather than in a curve-view .cpp; when a
// CompiledChorusCurveView is added, move this next to it.
const CompiledPresentationSpec& getMagdaChorusPresentation() {
    static const LegacyUiKind kSuppress[] = {LegacyUiKind::Chorus};
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaChorusCompiledPlugin::xmlTypeName,
        .layoutCellCount = 8,
        .layoutCellsPerRow = 8,
        .createPanel = nullptr,
        .suppressLegacyUis = kSuppress,
    };
    return kSpec;
}

// Per-device presentation specs live next to each curve view (or the
// wrapper if there's no curve view). Add a new compiled plugin by
// writing a getMagdaXxxPresentation() accessor and wiring it in
// kAllPresentations below.
const CompiledPresentationSpec& getMagdaFilterPresentation();
const CompiledPresentationSpec& getMagdaSaturatorPresentation();
const CompiledPresentationSpec& getMagdaDelayPresentation();
const CompiledPresentationSpec& getMagdaGrainDelayPresentation();
const CompiledPresentationSpec& getMagdaGritPresentation();
const CompiledPresentationSpec& getMagdaMultibandPresentation();
const CompiledPresentationSpec& getMagdaPhaserPresentation();
const CompiledPresentationSpec& getMagdaCompressorPresentation();
const CompiledPresentationSpec& getMagdaModPresentation();

namespace {

const CompiledPresentationSpec* const kAllPresentations[] = {
    &getMagdaFilterPresentation(), &getMagdaSaturatorPresentation(),
    &getMagdaDelayPresentation(),  &getMagdaGrainDelayPresentation(),
    &getMagdaGritPresentation(),   &getMagdaMultibandPresentation(),
    &getMagdaPhaserPresentation(), &getMagdaCompressorPresentation(),
    &getMagdaModPresentation(),    &getMagdaChorusPresentation(),
};

}  // namespace

std::span<const CompiledPresentationSpec* const> getAllCompiledPresentations() {
    return {kAllPresentations, std::size(kAllPresentations)};
}

const CompiledPresentationSpec* findCompiledPresentation(const juce::String& pluginId) {
    for (const auto* spec : kAllPresentations) {
        if (pluginId.equalsIgnoreCase(spec->pluginId))
            return spec;
    }
    return nullptr;
}

bool shouldSuppressLegacyUi(const juce::String& pluginId, LegacyUiKind kind) {
    const auto* spec = findCompiledPresentation(pluginId);
    if (spec == nullptr)
        return false;
    for (LegacyUiKind k : spec->suppressLegacyUis)
        if (k == kind)
            return true;
    return false;
}

}  // namespace magda::daw::ui
