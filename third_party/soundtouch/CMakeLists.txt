# SoundTouch, the stretch engine behind the pinned mode values kSoundTouchNormal and
# kSoundTouchBetter (magda/daw/core/TimeStretchModes.hpp). Vendored for the native
# engine (#1882), which may not reach into the Tracktion fork for it.
#
# Its own target, from unmodified sources, on purpose: SoundTouch is LGPL-2.1 and MAGDA
# is GPL-3.0, which is compatible, but keeping it as one replaceable archive is what
# leaves the relink option open. See SOURCES.md.
#
# x86 optimisations: cpu_detect_x86.cpp, mmx_optimized.cpp and sse_optimized.cpp compile
# to nothing on non-x86 (they guard on SOUNDTOUCH_ALLOW_X86_OPTIMIZATIONS and the
# architecture), so they are listed unconditionally and the arm64 build simply gets empty
# objects.

add_library(soundtouch STATIC
    source/SoundTouch/AAFilter.cpp
    source/SoundTouch/BPMDetect.cpp
    source/SoundTouch/FIFOSampleBuffer.cpp
    source/SoundTouch/FIRFilter.cpp
    source/SoundTouch/InterpolateCubic.cpp
    source/SoundTouch/InterpolateLinear.cpp
    source/SoundTouch/InterpolateShannon.cpp
    source/SoundTouch/PeakFinder.cpp
    source/SoundTouch/RateTransposer.cpp
    source/SoundTouch/SoundTouch.cpp
    source/SoundTouch/TDStretch.cpp
    source/SoundTouch/cpu_detect_x86.cpp
    source/SoundTouch/mmx_optimized.cpp
    source/SoundTouch/sse_optimized.cpp
)
add_library(magda::soundtouch ALIAS soundtouch)

# SYSTEM so that a warning in a header MAGDA did not write is not a warning MAGDA has to
# answer for.
target_include_directories(soundtouch SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# SoundTouch.cpp defines one extern "C" symbol at global scope, a version printer autoconf
# calls to prove the library links. The Tracktion fork compiles its own copy of these
# sources into its unity build, so while both exist that symbol is defined twice and the
# final link refuses it. Renaming ours through the preprocessor keeps the vendored source
# unmodified and costs nothing; it can go when the fork does.
target_compile_definitions(soundtouch PRIVATE soundtouch_ac_test=magda_soundtouch_ac_test)

# Vendored third-party C++; suppress upstream warnings rather than chasing them, the way
# third_party/sqlite does.
if(MSVC)
    target_compile_options(soundtouch PRIVATE /w)
else()
    target_compile_options(soundtouch PRIVATE -w)
endif()

set_target_properties(soundtouch PROPERTIES POSITION_INDEPENDENT_CODE ON)
