#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <vector>

namespace magda {

/**
 * @brief Utility functions for plugin state blob compression and encoding.
 *
 * Plugin state is an opaque binary blob (e.g., from getStateInformation()).
 * For project-file storage we:
 *   1. Compress with gzip   (raw bytes → compressed bytes)
 *   2. Encode as base64     (compressed bytes → JSON-safe string)
 *
 * On load we reverse the process.
 */
namespace PluginStateUtils {

// =========================================================================
// Compression
// =========================================================================

/**
 * @brief Compress raw plugin state data with gzip.
 * @param raw  Uncompressed plugin state bytes
 * @return     Gzip-compressed bytes (empty on failure)
 */
inline std::vector<uint8_t> compress(const std::vector<uint8_t>& raw) {
    if (raw.empty())
        return {};

    juce::MemoryOutputStream compressed;
    {
        juce::GZIPCompressorOutputStream gzip(compressed, 9);
        gzip.write(raw.data(), raw.size());
        gzip.flush();
    }

    auto* data = static_cast<const uint8_t*>(compressed.getData());
    return {data, data + compressed.getDataSize()};
}

/**
 * @brief Decompress gzip-compressed plugin state data.
 * @param compressed  Gzip-compressed bytes
 * @return            Uncompressed bytes (empty on failure)
 */
inline std::vector<uint8_t> decompress(const std::vector<uint8_t>& compressed) {
    if (compressed.empty())
        return {};

    juce::MemoryInputStream memStream(compressed.data(), compressed.size(), false);
    juce::GZIPDecompressorInputStream gzip(memStream);

    juce::MemoryOutputStream decompressed;
    decompressed.writeFromInputStream(gzip, -1);

    auto* data = static_cast<const uint8_t*>(decompressed.getData());
    return {data, data + decompressed.getDataSize()};
}

// =========================================================================
// Base64 encoding / decoding
// =========================================================================

/**
 * @brief Encode binary data as a base64 string (JSON-safe).
 * @param data  Raw binary bytes
 * @return      Base64-encoded string (empty if input is empty)
 */
inline juce::String toBase64(const std::vector<uint8_t>& data) {
    if (data.empty())
        return {};

    juce::MemoryBlock block(data.data(), data.size());
    return block.toBase64Encoding();
}

/**
 * @brief Decode a base64 string back to binary data.
 * @param base64  Base64-encoded string
 * @return        Decoded binary bytes (empty on failure or empty input)
 */
inline std::vector<uint8_t> fromBase64(const juce::String& base64) {
    if (base64.isEmpty())
        return {};

    juce::MemoryBlock block;
    if (!block.fromBase64Encoding(base64))
        return {};

    auto* data = static_cast<const uint8_t*>(block.getData());
    return {data, data + block.getSize()};
}

// =========================================================================
// Combined helpers  (compress + encode  /  decode + decompress)
// =========================================================================

/**
 * @brief Compress raw state and encode as base64 for JSON storage.
 * @param raw  Uncompressed plugin state
 * @return     Base64-encoded compressed string (empty if input is empty)
 */
inline juce::String compressAndEncode(const std::vector<uint8_t>& raw) {
    return toBase64(compress(raw));
}

/**
 * @brief Decode base64 and decompress to recover raw plugin state.
 * @param encoded  Base64-encoded compressed string
 * @return         Uncompressed plugin state bytes (empty on failure)
 */
inline std::vector<uint8_t> decodeAndDecompress(const juce::String& encoded) {
    return decompress(fromBase64(encoded));
}

}  // namespace PluginStateUtils
}  // namespace magda
