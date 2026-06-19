#pragma once

#include <juce_core/juce_core.h>

#include <cstring>

namespace magda::vst3 {

// A Steinberg ".vstpreset" blob begins with a fixed header:
//   offset 0  : 'VST3'           (4-byte ASCII magic)
//   offset 4  : version          (int32)
//   offset 8  : classID          (32 ASCII hex chars - the VST3 component FUID)
//   offset 40 : chunk-list offset (int64)
// The 32-char classID is exactly the identifier other hosts (e.g. Bitwig) use as
// the DAWproject <Vst3Plugin deviceID="...">. JUCE's PluginDescription only keeps
// a 32-bit hash of it, so the preset header is the one place the full id survives.
inline constexpr int kVst3PresetClassIdOffset = 8;
inline constexpr int kVst3PresetClassIdLength = 32;

// Extract the 32-char VST3 class id from a .vstpreset blob (as produced by
// ExtensionsVisitor::VST3Client::getPreset()). Returns an empty string if
// the blob is too small, lacks the 'VST3' magic, or the id isn't valid hex.
inline juce::String classIdFromPreset(const juce::MemoryBlock& preset) {
    if (preset.getSize() < static_cast<size_t>(kVst3PresetClassIdOffset + kVst3PresetClassIdLength))
        return {};

    const auto* bytes = static_cast<const char*>(preset.getData());
    if (std::memcmp(bytes, "VST3", 4) != 0)
        return {};

    juce::String id(bytes + kVst3PresetClassIdOffset,
                    static_cast<size_t>(kVst3PresetClassIdLength));
    return id.containsOnly("0123456789ABCDEFabcdef") ? id : juce::String();
}

}  // namespace magda::vst3
