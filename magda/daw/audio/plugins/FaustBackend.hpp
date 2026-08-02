#pragma once

// The single place that knows which libfaust factory flavour this build uses.
//
// libfaust exposes one factory type per execution backend, but they share the
// same shape: the factory subclasses dsp_factory, createDSPInstance() hands
// back a plain dsp*, and the delete function is refcount-aware. So the runtime
// Faust path only has to vary factory creation and teardown -- everything
// downstream (param harvesting, compute, hot swap) is backend-agnostic.
//
// Selected by MAGDA_FAUST_BACKEND at configure time; see
// magda/daw/audio/DevicesDependencies.cmake. Exactly one backend is compiled
// into libfaust, so this must agree with the CMake selection.

#include <string>

#if MAGDA_FAUST_BACKEND_WASM
    #include "faust/dsp/wasm-dsp.h"
#else
    #include "faust/dsp/interpreter-dsp.h"
#endif

namespace magda::faust {

// Both backends' factories derive from dsp_factory by plain single
// inheritance, and the base already declares createDSPInstance(), getSHAKey()
// and getJSON() -- everything the runtime Faust path needs. Trafficking in the
// base keeps the plugin headers free of any backend selection, so no consumer
// can be compiled against a different choice than libfaust was built with.
using Factory = ::dsp_factory;

/** Compiles DSP source into a factory, or returns null and fills errorOut. */
inline Factory* createFactoryFromString(const std::string& name, const std::string& source,
                                        int argc, const char* argv[], std::string& errorOut) {
#if MAGDA_FAUST_BACKEND_WASM
    // The trailing flag asks for internal memory, i.e. the module owns its own
    // linear memory rather than importing it from the host.
    return createWasmDSPFactoryFromString(name, source, argc, argv, errorOut, true);
#else
    return createInterpreterDSPFactoryFromString(name, source, argc, argv, errorOut);
#endif
}

/** Drops a reference to a factory. Instances must be deleted first. */
inline void deleteFactory(Factory* factory) {
    if (factory == nullptr)
        return;

#if MAGDA_FAUST_BACKEND_WASM
    deleteWasmDSPFactory(static_cast<wasm_dsp_factory*>(factory));
#else
    deleteInterpreterDSPFactory(static_cast<interpreter_dsp_factory*>(factory));
#endif
}

}  // namespace magda::faust
