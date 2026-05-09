#include "CompiledFaustPluginBase.hpp"

// clang-format off
#include <faust/dsp/dsp.h>
#include <faust/gui/UI.h>
#include <faust/gui/meta.h>
#include "magda_filter_svf.generated.cpp"
// clang-format on

namespace magda::daw::audio::compiled {

const char* MagdaSVFCompiledPlugin::xmlTypeName = "magda_svf";

MagdaSVFCompiledPlugin::MagdaSVFCompiledPlugin(const te::PluginCreationInfo& info)
    : CompiledFaustPluginBase(info, std::make_unique<MagdaSVFDsp>(), "Filter - SVF", xmlTypeName) {}

}  // namespace magda::daw::audio::compiled
