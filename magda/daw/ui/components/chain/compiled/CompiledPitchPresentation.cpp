#include "audio/plugins/compiled/MagdaPitchCompiledPlugin.hpp"
#include "compiled/CompiledPluginPresentation.hpp"

namespace magda::daw::ui {

// Pitch has no curve view - there's no useful signal to plot (it's a pitch
// shifter, the output is just the input transposed). The 6-cell param grid
// fills the slot body, same width and visual weight as Dimension /
// Compressor / Filter / Mod (all 6 cells per row).
const CompiledPresentationSpec& getMagdaPitchPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaPitchCompiledPlugin::xmlTypeName,
        .layoutCellCount = magda::daw::audio::compiled::MagdaPitchCompiledPlugin::kHostSlotCount,
        .layoutCellsPerRow = 6,
        .createPanel = nullptr,
    };
    return kSpec;
}

}  // namespace magda::daw::ui
