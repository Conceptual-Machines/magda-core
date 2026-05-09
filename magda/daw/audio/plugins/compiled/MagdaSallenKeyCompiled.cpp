#include "CompiledFaustPluginBase.hpp"

// clang-format off
#include <faust/dsp/dsp.h>
#include <faust/gui/UI.h>
#include <faust/gui/meta.h>
#include "magda_filter_sk.generated.cpp"
// clang-format on

namespace magda::daw::audio::compiled {

const char* MagdaSallenKeyCompiledPlugin::xmlTypeName = "magda_sallen_key";

MagdaSallenKeyCompiledPlugin::MagdaSallenKeyCompiledPlugin(const te::PluginCreationInfo& info)
    : CompiledFaustPluginBase(info, std::make_unique<MagdaSallenKeyDsp>(), "Filter - Sallen-Key",
                              xmlTypeName) {}

}  // namespace magda::daw::audio::compiled
