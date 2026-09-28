// Guards a bug class that is invisible until someone looks at the screen.
//
// `juce::String(const char*)` decodes through `CharPointer_ASCII`, one byte to
// one character. A source literal is UTF-8, so an em dash's three bytes become
// three Latin-1 characters and "Listening — MCP" renders as "Listening â€" MCP".
// JUCE's own constructor documents this and asserts on it in debug, but the
// assertion fires far from the literal and nothing fails in a headless build,
// so these accumulate: this test was written after finding fourteen of them,
// including a session-clip-editor close button whose entire label was mojibake.
//
// The fix at each site is `juce::String::fromUTF8(...)`, or plain ASCII where
// the text is a log line and the typography was never load-bearing.
//
// Scanning source from a test is unusual, and is the point: the defect exists in
// the source text, produces no compiler diagnostic, and cannot be observed from
// inside the program. MAGDA_REPO_ROOT is already defined for this target.

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>
#include <vector>

namespace {

/// A literal the loader would misread, with enough context to find it.
struct Finding {
    juce::String file;
    int line = 0;
    juce::String text;
};

/**
 * @brief Every non-ASCII ordinary string literal that is not already decoded.
 *
 * Three things are deliberately *not* findings:
 *
 * - **Comments.** This codebase writes prose in them and uses em dashes
 *   throughout; they never reach a `juce::String`.
 * - **Raw strings** (`R"(...)"`). They are handed to a consumer verbatim — the
 *   agent system prompts are `const char*` that callers wrap in `fromUTF8`
 *   themselves — so the bytes survive.
 * - **Anything already wrapped.** Adjacent literals concatenate before any
 *   conversion happens, so one `fromUTF8(` covers the whole run and only the
 *   first literal in it is preceded by the call.
 * - **Anything marked `utf8-ok`.** A lexical scan cannot see the type a literal
 *   is assigned to, and `std::string` keeps the bytes exactly as written — so
 *   two of these are correct as they stand. The marker is deliberately a
 *   comment a human has to type: it records that someone checked the
 *   destination, and it greps.
 */
std::vector<Finding> scan(const juce::File& file) {
    std::vector<Finding> findings;
    const auto source = file.loadFileAsString();
    const auto* text = source.toRawUTF8();
    const auto length = static_cast<int>(std::strlen(text));

    const auto lineAt = [&](int offset) {
        int line = 1;
        for (int i = 0; i < offset && i < length; ++i)
            if (text[i] == '\n')
                ++line;
        return line;
    };

    // The marker may sit on the run's own line or the line above it, so a long
    // wrapped statement does not have to carry it awkwardly inline.
    const auto isMarkedOk = [&](int offset) {
        auto lineStart = offset;
        while (lineStart > 0 && text[lineStart - 1] != '\n')
            --lineStart;
        auto previousStart = lineStart > 0 ? lineStart - 1 : 0;
        while (previousStart > 0 && text[previousStart - 1] != '\n')
            --previousStart;
        auto lineEnd = offset;
        while (lineEnd < length && text[lineEnd] != '\n')
            ++lineEnd;
        const std::string window(text + previousStart,
                                 static_cast<size_t>(lineEnd - previousStart));
        return window.find("utf8-ok") != std::string::npos;
    };

    // Byte comparison, not juce::String: that constructor's size argument counts
    // *characters*, so truncating a prefix at a byte offset lands in the wrong
    // place as soon as anything earlier in the file is multi-byte — which, in a
    // file that has non-ASCII literals, it always is.
    const auto endsWithWrapper = [&](int offset) {
        auto i = offset;
        while (i > 0 && (text[i - 1] == ' ' || text[i - 1] == '\t' || text[i - 1] == '\n' ||
                         text[i - 1] == '\r'))
            --i;
        const auto endsWith = [&](const char* suffix) {
            const auto suffixLength = static_cast<int>(std::strlen(suffix));
            return i >= suffixLength && std::memcmp(text + i - suffixLength, suffix,
                                                    static_cast<size_t>(suffixLength)) == 0;
        };
        return endsWith("fromUTF8(") || endsWith("CharPointer_UTF8(");
    };

    for (int i = 0; i < length;) {
        if (text[i] == '/' && i + 1 < length && text[i + 1] == '/') {
            while (i < length && text[i] != '\n')
                ++i;
            continue;
        }
        if (text[i] == '/' && i + 1 < length && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < length && !(text[i] == '*' && text[i + 1] == '/'))
                ++i;
            i = juce::jmin(length, i + 2);
            continue;
        }
        // A raw string opens with R"delim( and closes with )delim".
        if (text[i] == 'R' && i + 1 < length && text[i + 1] == '"') {
            const auto delimiterStart = i + 2;
            int j = delimiterStart;
            while (j < length && text[j] != '(')
                ++j;
            if (j < length) {
                const std::string closing =
                    ")" +
                    std::string(text + delimiterStart, static_cast<size_t>(j - delimiterStart)) +
                    "\"";
                const auto rest = std::string(text + j, static_cast<size_t>(length - j));
                const auto at = rest.find(closing);
                i = at == std::string::npos ? length : j + static_cast<int>(at + closing.size());
                continue;
            }
        }
        if (text[i] == '"') {
            const auto runStart = i;
            auto runHasNonAscii = false;
            while (i < length && text[i] == '"') {
                auto j = i + 1;
                auto escaped = false;
                while (j < length) {
                    if (escaped)
                        escaped = false;
                    else if (text[j] == '\\')
                        escaped = true;
                    else if (text[j] == '"' || text[j] == '\n')
                        break;
                    ++j;
                }
                for (auto b = i + 1; b < j; ++b)
                    if (static_cast<unsigned char>(text[b]) > 127)
                        runHasNonAscii = true;
                i = j + 1;

                // Only whitespace between two literals means they concatenate,
                // so the run continues.
                auto k = i;
                while (k < length && juce::CharacterFunctions::isWhitespace(text[k]))
                    ++k;
                if (k < length && text[k] == '"')
                    i = k;
                else
                    break;
            }
            if (runHasNonAscii && !endsWithWrapper(runStart) && !isMarkedOk(runStart)) {
                const std::string excerpt(
                    text + runStart, static_cast<size_t>(juce::jmin(i, runStart + 60) - runStart));
                findings.push_back({file.getFileName(), lineAt(runStart),
                                    juce::String::fromUTF8(excerpt.c_str())});
            }
            continue;
        }
        ++i;
    }
    return findings;
}

}  // namespace

TEST_CASE("No source literal relies on juce::String decoding UTF-8 as ASCII",
          "[encoding][source]") {
    const juce::File root(MAGDA_REPO_ROOT);
    const auto sources =
        root.getChildFile("magda").findChildFiles(juce::File::findFiles, true, "*.cpp;*.hpp");
    REQUIRE_FALSE(sources.isEmpty());

    std::vector<Finding> findings;
    for (const auto& file : sources)
        for (auto& finding : scan(file))
            findings.push_back(std::move(finding));

    if (!findings.empty()) {
        juce::String report("Non-ASCII string literals that juce::String would misdecode:\n");
        for (const auto& finding : findings)
            report << "  " << finding.file << ":" << finding.line << "  " << finding.text << "\n";
        report << "\nWrap them in juce::String::fromUTF8(...), or use ASCII if the text is a "
                  "log line.";
        UNSCOPED_INFO(report.toStdString());
    }
    REQUIRE(findings.empty());
}

TEST_CASE("The scanner recognises what it is supposed to skip", "[encoding][source]") {
    // Without these the guard above is worth nothing: a scanner that flags
    // comments would have been deleted on its first false positive, and one that
    // flags raw strings would have had every agent system prompt rewritten for
    // no reason.
    juce::TemporaryFile scratch(".cpp");
    const auto write = [&scratch](const char* contents) {
        scratch.getFile().replaceWithText(juce::String::fromUTF8(contents));
        return scan(scratch.getFile());
    };

    REQUIRE(write("// a comment — with an em dash\n").empty());
    REQUIRE(write("/* a block comment — with one */\n").empty());
    REQUIRE(write("const char* p = R\"X(raw — text)X\";\n").empty());
    REQUIRE(write("auto s = juce::String::fromUTF8(\"already — wrapped\");\n").empty());
    REQUIRE(write("auto s = juce::String(juce::CharPointer_UTF8(\"wrapped — too\"));\n").empty());
    REQUIRE(write("auto s = \"plain ascii\";\n").empty());

    // The run rule: one wrapper covers every literal it concatenates with.
    REQUIRE(write("auto s = juce::String::fromUTF8(\"first \"\n\"second — dash\");\n").empty());

    // And the thing it must actually catch, including inside a run whose first
    // literal is innocent.
    REQUIRE(write("auto s = juce::String(\"bare — dash\");\n").size() == 1);
    REQUIRE(write("label.setText(\"plain \"\n\"then — dash\");\n").size() == 1);
}
