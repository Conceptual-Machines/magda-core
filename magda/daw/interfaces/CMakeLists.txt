# Header-only abstract interfaces shared between layers.

target_sources(magda_daw PRIVATE
    clip_interface.hpp
    mixer_interface.hpp
    prompt_interface.hpp
    track_interface.hpp
    transport_interface.hpp
)
