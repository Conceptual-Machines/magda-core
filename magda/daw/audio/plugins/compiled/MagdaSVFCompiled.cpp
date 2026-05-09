// Compiled-from-Faust SVF filter — proof of concept for #1214.
//
// This translation unit pulls in:
//   - the Faust runtime base (`dsp`, `Meta`, `UI` interfaces)
//   - the Faust-generated SVF DSP class (`MagdaSVFDsp`, produced at
//     build time from `magda_filter_svf.dsp` via the
//     `magda_compile_faust_dsp()` CMake helper)
//
// The compiled class is then host-wrapped by `CompiledFaustHost` so the
// rest of MAGDA can drive audio processing and harvest controls without
// touching any libfaust runtime APIs (no JIT, no interpreter — this is
// pure native C++ from here on out).
//
// Device-level te::Plugin registration is deliberately deferred to a
// follow-up; this file's job is to prove the build pipeline works end
// to end.

#include "CompiledFaustHost.hpp"
#include "magda_filter_svf.generated.cpp"

namespace magda::daw::audio::compiled {

// Anchor symbol — keeps the translation unit live so the linker doesn't
// elide it. Will be replaced by a proper plugin factory call in a
// follow-up.
const CompiledFaustHost<MagdaSVFDsp>* compiled_svf_anchor() {
    static CompiledFaustHost<MagdaSVFDsp> instance;
    return &instance;
}

}  // namespace magda::daw::audio::compiled
