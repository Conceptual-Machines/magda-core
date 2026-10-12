# libmp3lame, the MP3 encoder behind MP3 export (#2955, #3032). JUCE has none of its own:
# LAMEEncoderAudioFormat runs a `lame` executable the user would have to install.
#
# LAME ships autotools and hand-written makefiles but no CMake, so FetchContent only
# populates lame_SOURCE_DIR and the library is built here, from config.h beside this
# file instead of a configure step. Encoder only: mpglib (the decoder) is left out.
#
# Its own static target, like third_party/soundtouch, so the archive stays replaceable
# under the LGPL. See SOURCES.md.

set(LAME_DIR "${lame_SOURCE_DIR}/libmp3lame")

add_library(mp3lame STATIC
    "${LAME_DIR}/VbrTag.c"
    "${LAME_DIR}/bitstream.c"
    "${LAME_DIR}/encoder.c"
    "${LAME_DIR}/fft.c"
    "${LAME_DIR}/gain_analysis.c"
    "${LAME_DIR}/id3tag.c"
    "${LAME_DIR}/lame.c"
    "${LAME_DIR}/mpglib_interface.c"
    "${LAME_DIR}/newmdct.c"
    "${LAME_DIR}/presets.c"
    "${LAME_DIR}/psymodel.c"
    "${LAME_DIR}/quantize.c"
    "${LAME_DIR}/quantize_pvt.c"
    "${LAME_DIR}/reservoir.c"
    "${LAME_DIR}/set_get.c"
    "${LAME_DIR}/tables.c"
    "${LAME_DIR}/takehiro.c"
    "${LAME_DIR}/util.c"
    "${LAME_DIR}/vbrquantize.c"
    "${LAME_DIR}/version.c"
)
add_library(magda::mp3lame ALIAS mp3lame)

# lame.h is the public header; the rest, config.h included, are the library's own.
target_include_directories(mp3lame SYSTEM PUBLIC "${lame_SOURCE_DIR}/include")
target_include_directories(mp3lame PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${LAME_DIR}"
)
target_compile_definitions(mp3lame PRIVATE HAVE_CONFIG_H)

if(MSVC)
    target_compile_definitions(mp3lame PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(mp3lame PRIVATE /w)
else()
    target_compile_options(mp3lame PRIVATE -w)
endif()

if(UNIX)
    target_link_libraries(mp3lame PRIVATE m)
endif()

set_target_properties(mp3lame PROPERTIES POSITION_INDEPENDENT_CODE ON)
