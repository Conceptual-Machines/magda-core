#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/Vst3Preset.hpp"

using namespace magda;

namespace {

// Build a minimal .vstpreset blob: 'VST3' + int32 version + 32-char classID + tail.
juce::MemoryBlock makePreset(const juce::String& classId, const char* magic = "VST3") {
    juce::MemoryOutputStream out;
    out.write(magic, 4);
    out.writeInt(1);  // version
    out.write(classId.toRawUTF8(), static_cast<size_t>(classId.getNumBytesAsUTF8()));
    out.writeInt64(0);  // chunk-list offset
    return out.getMemoryBlock();
}

}  // namespace

TEST_CASE("vst3::classIdFromPreset extracts the class id from a .vstpreset header",
          "[audio][vst3][preset]") {
    const juce::String classId = "ABCDEF019182FAEB786C6E4178414432";  // 32 hex chars
    REQUIRE(vst3::classIdFromPreset(makePreset(classId)) == classId);
}

TEST_CASE("vst3::classIdFromPreset rejects malformed blobs", "[audio][vst3][preset]") {
    // Wrong magic.
    REQUIRE(
        vst3::classIdFromPreset(makePreset("ABCDEF019182FAEB786C6E4178414432", "XXXX")).isEmpty());

    // Too small to hold the header.
    juce::MemoryBlock tiny("VST3", 4);
    REQUIRE(vst3::classIdFromPreset(tiny).isEmpty());

    // Non-hex class id.
    REQUIRE(vst3::classIdFromPreset(makePreset("not-a-valid-hex-classid-000000000")).isEmpty());

    // Empty blob.
    REQUIRE(vst3::classIdFromPreset({}).isEmpty());
}
