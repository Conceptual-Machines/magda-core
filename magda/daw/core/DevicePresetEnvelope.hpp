#pragma once

#include <juce_core/juce_core.h>

#include <optional>

#include "DeviceInfo.hpp"

/**
 * @file DevicePresetEnvelope.hpp
 * @brief A device preset as the SDK's portable `magda.preset` document (#2939).
 *
 * Parameters and device state go in the portable members; everything else a DeviceInfo carries
 * goes under `host.magda`, so a preset loads to the same device from either format.
 */

namespace magda::device_preset {

/// What a save carries over from the preset file it replaces.
struct Metadata {
    juce::String id;
    juce::String name;
    juce::String author;
    juce::StringArray tags;
    juce::String created;
};

/**
 * @brief The envelope text for @p device, to be written at @p presetFile.
 *
 * Nullopt when the envelope cannot hold the device exactly (state it cannot normalise, a
 * parameter in the wrong domain, a sample on another volume); the caller writes the legacy payload.
 */
std::optional<juce::String> writeEnvelope(const DeviceInfo& device, const juce::File& presetFile,
                                          const Metadata& metadata);

/// The id, name, author, tags and creation time of an envelope file; nullopt for any other file.
std::optional<Metadata> readMetadata(const juce::File& presetFile);

enum class ReadStatus {
    Loaded,
    /// Not a `magda.preset` document: read it as a legacy preset.
    NotAnEnvelope,
    /// Written by a newer build. Nothing was read.
    FromNewerVersion,
    Invalid,
};

struct ReadResult {
    ReadStatus status = ReadStatus::NotAnEnvelope;
    juce::String error;

    /// Unknown parameters, links and assets that were left out or not found.
    juce::StringArray warnings;
};

/**
 * @brief Read an envelope into @p device and complete it as a legacy preset load does.
 *
 * Parameters are mapped from stable ids to slots through the device's descriptors. An unknown id
 * is reported and ignored; a parameter the preset omits keeps its default.
 */
ReadResult readEnvelope(const juce::String& text, const juce::File& presetFile, DeviceInfo& device);

/// @p text with its asset paths made relative to @p to instead of @p from.
std::optional<juce::String> rebaseAssets(const juce::String& text, const juce::File& from,
                                         const juce::File& to);

}  // namespace magda::device_preset
