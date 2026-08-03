#include "FaustResources.hpp"

#include <algorithm>
#include <regex>
#include <string>

#include "core/AppPaths.hpp"

namespace magda::daw::audio {

namespace {

// Pulls a global `declare <key> "<value>";` out of .dsp source. Faust allows
// arbitrary global metadata, so the file itself carries everything the loader
// needs and no filename has to be repeated in C++.
juce::String readDeclare(const juce::String& source, juce::StringRef key) {
    const std::regex re("declare\\s+" + std::string(key.text) + "\\s+\"([^\"]*)\"");
    const std::string subject = source.toStdString();
    std::smatch m;
    if (std::regex_search(subject, m, re) && m.size() > 1)
        return juce::String(m[1].str()).trim();
    return {};
}

}  // namespace

juce::String readCustomViewName(const juce::String& source) {
    return readDeclare(source, "magda_view");
}

FaustPatchInfo readPatchInfo(const juce::String& source) {
    FaustPatchInfo info;
    info.author = readDeclare(source, "author");
    info.version = readDeclare(source, "version");
    info.license = readDeclare(source, "license");
    info.description = readDeclare(source, "description");
    return info;
}

juce::File getFaustLibrariesPath() {
#if JUCE_MAC
    // currentApplicationFile is the .app bundle on macOS. Libraries live in
    // Contents/Resources/faustlibraries (matches CMake's POST_BUILD copy).
    return juce::File::getSpecialLocation(juce::File::currentApplicationFile)
        .getChildFile("Contents/Resources/faustlibraries");
#else
    // On Windows/Linux, libraries sit next to the executable. Go through
    // paths::executableDir() rather than currentApplicationFile so this
    // resolves inside the mount when running from a Linux AppImage — libfaust
    // takes this as its -I path, so getting it wrong fails every
    // import("stdfaust.lib").
    return magda::paths::executableDir().getChildFile("faustlibraries");
#endif
}

juce::File getFaustRuntimeDspPath() {
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::currentApplicationFile)
        .getChildFile("Contents/Resources/faust_dsp");
#else
    // As above: currentApplicationFile points at the outer .AppImage launcher
    // under an AppImage, which is why the factory starter DSPs showed up when
    // running an extracted binary but not from the .AppImage itself (#1999).
    return magda::paths::executableDir().getChildFile("faust_dsp");
#endif
}

juce::File getFaustRuntimeDspPath(FaustPatchKind kind) {
    return getFaustRuntimeDspPath().getChildFile(kind == FaustPatchKind::Instrument ? "instruments"
                                                                                    : "effects");
}

std::vector<StarterDsp> getBundledStarterDsps(FaustPatchKind kind) {
    const auto root = getFaustRuntimeDspPath(kind);
    if (!root.isDirectory())
        return {};

    std::vector<StarterDsp> out;
    for (const auto& entry :
         juce::RangedDirectoryIterator(root, true, "*.dsp", juce::File::findFiles)) {
        const auto file = entry.getFile();
        const auto source = file.loadFileAsString();
        if (source.isEmpty())
            continue;

        StarterDsp dsp;
        // `declare name` is the DSP's own title; fall back to the file name so
        // a .dsp without one still shows up rather than being silently skipped.
        dsp.name = readDeclare(source, "name");
        if (dsp.name.isEmpty())
            dsp.name = file.getFileNameWithoutExtension().replaceCharacter('_', ' ');
        dsp.filename = file.getFileName();
        dsp.source = source;
        // The subfolder the file sits in, e.g. <root>/texture -> "texture".
        // A file at the root has no category of its own; comparing against the
        // root itself keeps that independent of what the staged folder is
        // called on disk.
        const auto parent = file.getParentDirectory();
        dsp.category = parent == root ? juce::String() : parent.getFileName();
        out.push_back(std::move(dsp));
    }

    std::sort(out.begin(), out.end(), [](const StarterDsp& a, const StarterDsp& b) {
        if (a.category != b.category)
            return a.category < b.category;
        return a.name < b.name;
    });
    return out;
}

}  // namespace magda::daw::audio
