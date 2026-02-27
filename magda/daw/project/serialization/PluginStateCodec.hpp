#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace magda {

/**
 * @brief Codec for compressing/decompressing plugin state blobs.
 *
 * Encodes a juce::ValueTree (the te::Plugin::state snapshot) into a
 * compact base64 string suitable for embedding in a JSON project file.
 *
 * Pipeline:
 *   ValueTree → XML string → gzip compress → base64 encode → juce::String
 *
 * The resulting string is stored in DeviceInfo::pluginStateData and written
 * as a JSON string field ("pluginStateData") by ProjectSerializer.
 */
namespace PluginStateCodec {

/**
 * @brief Encode a ValueTree into a compressed base64 string.
 *
 * @param tree The plugin state ValueTree to encode.
 * @return Base64-encoded gzip-compressed XML, or empty string on failure.
 */
inline juce::String encode(const juce::ValueTree& tree) {
    if (!tree.isValid())
        return {};

    // 1. ValueTree → XML string (UTF-8)
    auto xml = tree.createXml();
    if (!xml)
        return {};

    auto xmlString = xml->toString();
    if (xmlString.isEmpty())
        return {};

    auto xmlUtf8 = xmlString.toUTF8();
    auto xmlBytes = static_cast<size_t>(xmlUtf8.sizeInBytes() - 1);  // exclude null terminator

    // 2. Gzip compress
    juce::MemoryOutputStream compressedStream;
    {
        juce::GZIPCompressorOutputStream gzip(compressedStream, 9);
        if (!gzip.write(xmlUtf8.getAddress(), xmlBytes))
            return {};
        gzip.flush();
    }

    // 3. Base64 encode
    return juce::Base64::toBase64(compressedStream.getData(), compressedStream.getDataSize());
}

/**
 * @brief Decode a compressed base64 string back to a ValueTree.
 *
 * @param encoded The base64 string previously produced by encode().
 * @return The decoded ValueTree, or an invalid ValueTree on failure.
 */
inline juce::ValueTree decode(const juce::String& encoded) {
    if (encoded.isEmpty())
        return {};

    // 1. Base64 decode
    juce::MemoryOutputStream decodedStream;
    if (!juce::Base64::convertFromBase64(decodedStream, encoded))
        return {};

    // 2. Gzip decompress
    juce::MemoryInputStream compressedInput(decodedStream.getData(), decodedStream.getDataSize(),
                                            false);
    juce::GZIPDecompressorInputStream gzip(compressedInput);
    auto xmlString = gzip.readEntireStreamAsString();
    if (xmlString.isEmpty())
        return {};

    // 3. XML → ValueTree
    auto xml = juce::parseXML(xmlString);
    if (!xml)
        return {};

    return juce::ValueTree::fromXml(*xml);
}

}  // namespace PluginStateCodec

}  // namespace magda
