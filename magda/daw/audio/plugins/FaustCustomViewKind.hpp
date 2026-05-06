#pragma once

namespace magda::daw::audio {

/**
 * @brief Identifier for a built-in Faust DSP that has a bespoke
 *        custom view registered against it.
 *
 * Lives in the audio layer so `FaustPlugin` can store the kind on
 * `loadDspSource`, and so the UI layer's `FaustCustomUIRegistry` can
 * key on it without a back-dependency.
 *
 * `None` is the default for user-loaded .dsp files (file picker /
 * code editor); they get the standard grid-only layout. Add a value
 * here whenever a new built-in DSP wants its own view, then teach
 * `FaustResources::getBundledStarterDsps()` and the matching view
 * factory about it.
 */
enum class FaustCustomViewKind {
    None,
    MagdaDrive,
};

}  // namespace magda::daw::audio
