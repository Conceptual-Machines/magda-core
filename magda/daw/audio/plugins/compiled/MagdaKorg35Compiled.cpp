#include "CompiledFaustPluginBase.hpp"

// clang-format off
#include <faust/dsp/dsp.h>
#include <faust/gui/UI.h>
#include <faust/gui/meta.h>
#include "magda_filter_korg35.generated.cpp"
// clang-format on

namespace magda::daw::audio::compiled {

const char* MagdaKorg35CompiledPlugin::xmlTypeName = "magda_korg35";

MagdaKorg35CompiledPlugin::MagdaKorg35CompiledPlugin(const te::PluginCreationInfo& info)
    : CompiledFaustPluginBase(info, std::make_unique<MagdaKorg35Dsp>(), "Korg 35", xmlTypeName) {}

}  // namespace magda::daw::audio::compiled
