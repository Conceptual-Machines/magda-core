#include "CompiledFaustPluginBase.hpp"

// clang-format off
#include <faust/dsp/dsp.h>
#include <faust/gui/UI.h>
#include <faust/gui/meta.h>
#include "magda_filter_ladder.generated.cpp"
// clang-format on

namespace magda::daw::audio::compiled {

const char* MagdaLadderCompiledPlugin::xmlTypeName = "magda_ladder";

MagdaLadderCompiledPlugin::MagdaLadderCompiledPlugin(const te::PluginCreationInfo& info)
    : CompiledFaustPluginBase(info, std::make_unique<MagdaLadderDsp>(), "Ladder", xmlTypeName) {}

}  // namespace magda::daw::audio::compiled
