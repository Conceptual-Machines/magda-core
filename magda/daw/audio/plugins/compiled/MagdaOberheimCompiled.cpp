#include "CompiledFaustPluginBase.hpp"

// clang-format off
#include <faust/dsp/dsp.h>
#include <faust/gui/UI.h>
#include <faust/gui/meta.h>
#include "magda_filter_oberheim.generated.cpp"
// clang-format on

namespace magda::daw::audio::compiled {

const char* MagdaOberheimCompiledPlugin::xmlTypeName = "magda_oberheim";

MagdaOberheimCompiledPlugin::MagdaOberheimCompiledPlugin(const te::PluginCreationInfo& info)
    : CompiledFaustPluginBase(info, std::make_unique<MagdaOberheimDsp>(), "Oberheim", xmlTypeName) {
}

}  // namespace magda::daw::audio::compiled
