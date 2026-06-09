#pragma once

#include <string>

namespace magda {

/** Normalize a user-entered base URL for an OpenAI-compatible HTTP server.

    Local servers (LM Studio, Ollama, GPUStack, llama.cpp server, ...) are
    configured by base URL, and users enter it in many shapes:
    `http://localhost:11434`, `http://localhost:11434/`, `.../v1`, `.../v1/`.

    This collapses all of them to a single canonical form ending in exactly one
    `/v1` and no trailing slash, so that the OpenAI-Chat client (which appends
    `/chat/completions`) and model discovery (which appends `/models`) both
    produce the right path with no duplicated or missing `/v1` segment.

    Whitespace is trimmed. An empty/whitespace-only input returns "" so callers
    can fall back to their own default. */
inline std::string normalizeOpenAIBaseUrl(std::string url) {
    // Trim leading/trailing ASCII whitespace.
    const auto isWs = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    std::size_t begin = 0;
    while (begin < url.size() && isWs(url[begin]))
        ++begin;
    std::size_t end = url.size();
    while (end > begin && isWs(url[end - 1]))
        --end;
    url = url.substr(begin, end - begin);

    if (url.empty())
        return url;

    // Strip trailing slashes.
    while (!url.empty() && url.back() == '/')
        url.pop_back();

    if (url.empty())
        return url;

    // Ensure exactly one trailing "/v1". Match case-insensitively so a typed
    // "/V1" is recognized rather than duplicated.
    const auto endsWithV1 = [](const std::string& s) {
        if (s.size() < 3)
            return false;
        const std::string tail = s.substr(s.size() - 3);
        return (tail[0] == '/' && (tail[1] == 'v' || tail[1] == 'V') && tail[2] == '1');
    };

    if (!endsWithV1(url))
        url += "/v1";

    return url;
}

}  // namespace magda
