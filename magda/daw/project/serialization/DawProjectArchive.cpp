#include "DawProjectArchive.hpp"

#include <cstring>

#include "DawProjectValidator.hpp"
#include "DawProjectXmlAdapter.hpp"

namespace magda {
namespace {

std::unique_ptr<juce::MemoryInputStream> streamForXml(const juce::String& xml) {
    const auto* data = xml.toRawUTF8();
    return std::make_unique<juce::MemoryInputStream>(data, std::strlen(data), true);
}

bool readZipEntry(juce::ZipFile& zip, const juce::String& entryName, juce::String& outText,
                  juce::String& error) {
    const auto index = zip.getIndexOfFileName(entryName, false);
    if (index < 0) {
        error = "DAWproject archive is missing " + entryName;
        return false;
    }

    std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(index));
    if (!stream) {
        error = "Could not open DAWproject archive entry " + entryName;
        return false;
    }

    outText = stream->readEntireStreamAsString();
    return true;
}

}  // namespace

juce::String DawProjectArchive::toMetadataXml(const ProjectDocument& document) {
    juce::XmlElement metadata("MetaData");

    if (document.info.name.isNotEmpty()) {
        auto* title = metadata.createNewChildElement("Title");
        title->addTextElement(document.info.name);
    }

    return metadata.toString();
}

bool DawProjectArchive::writeToFile(const juce::File& file, const ProjectDocument& document,
                                    juce::String& error) {
    error.clear();

    auto parentDir = file.getParentDirectory();
    if (!parentDir.createDirectory()) {
        error = "Failed to create DAWproject output directory: " + parentDir.getFullPathName();
        return false;
    }

    const auto projectXml = DawProjectXmlAdapter::toProjectXml(document);
    const auto metadataXml = toMetadataXml(document);

    if (!DawProjectValidator::validateProjectXml(projectXml, error))
        return false;

    if (!DawProjectValidator::validateMetadataXml(metadataXml, error))
        return false;

    juce::TemporaryFile tempFile(file);

    {
        juce::FileOutputStream output(tempFile.getFile());
        if (!output.openedOk()) {
            error = "Failed to open temporary DAWproject archive for writing: " +
                    tempFile.getFile().getFullPathName();
            return false;
        }

        juce::ZipFile::Builder builder;
        builder.addEntry(streamForXml(projectXml).release(), 9, "project.xml",
                         juce::Time::getCurrentTime());
        builder.addEntry(streamForXml(metadataXml).release(), 9, "metadata.xml",
                         juce::Time::getCurrentTime());

        // Embed referenced audio so the project is self-contained and portable.
        // Stored (compression 0) since audio barely compresses and we want fast
        // writes. The XML already references these by their archive-relative path.
        for (const auto& audio : DawProjectXmlAdapter::collectEmbeddedAudio(document))
            builder.addFile(juce::File(audio.sourcePath), 0, audio.archivePath);

        // Embed device state files (a VST3 .vstpreset, or the opaque TE blob for
        // other devices) as the files their <State> references point at.
        for (const auto& state : DawProjectXmlAdapter::collectDeviceStates(document))
            builder.addEntry(new juce::MemoryInputStream(state.bytes, true), 9, state.archivePath,
                             juce::Time::getCurrentTime());

        double progress = 0.0;
        if (!builder.writeToStream(output, &progress)) {
            error = "Failed to write DAWproject archive";
            return false;
        }

        output.flush();
    }

    if (!tempFile.overwriteTargetFileWithTemporary()) {
        error = "Failed to replace target DAWproject archive";
        return false;
    }

    return true;
}

bool DawProjectArchive::readFromFile(const juce::File& file, ProjectDocument& outDocument,
                                     juce::String& error, const juce::File& audioExtractionDir) {
    error.clear();

    if (!file.existsAsFile()) {
        error = "DAWproject archive not found: " + file.getFullPathName();
        return false;
    }

    juce::ZipFile zip(file);

    juce::String projectXml;
    if (!readZipEntry(zip, "project.xml", projectXml, error))
        return false;

    if (!DawProjectValidator::validateProjectXml(projectXml, error))
        return false;

    juce::String metadataXml;
    if (readZipEntry(zip, "metadata.xml", metadataXml, error)) {
        if (!DawProjectValidator::validateMetadataXml(metadataXml, error))
            return false;
    } else {
        error.clear();
    }

    if (!DawProjectXmlAdapter::fromProjectXml(projectXml, outDocument, error))
        return false;

    // Extract embedded audio to disk and repoint clips at the extracted files,
    // so audio resolves after import. Files referenced by a relative archive path
    // are embedded; absolute paths are external references and left untouched.
    juce::File mediaDir = audioExtractionDir != juce::File()
                              ? audioExtractionDir
                              : juce::File::getSpecialLocation(juce::File::tempDirectory)
                                    .getChildFile("magda_dawproject_import")
                                    .getChildFile(file.getFileNameWithoutExtension());
    for (auto& clip : outDocument.clips) {
        if (!clip.isAudio())
            continue;
        const auto entryName = clip.audio().source.filePath;
        if (entryName.isEmpty())
            continue;
        const int entryIndex = zip.getIndexOfFileName(entryName, false);
        if (entryIndex < 0)
            continue;  // external/absolute reference, nothing to extract

        mediaDir.createDirectory();
        if (zip.uncompressEntry(entryIndex, mediaDir, true).wasOk())
            clip.audio().source.filePath = mediaDir.getChildFile(entryName).getFullPathName();
    }

    // Pull embedded device state back into the model. fromProjectXml left the
    // archive path in pluginState; resolve it to the stored bytes. A .vstpreset
    // entry becomes vst3Preset (base64, applied via setPreset on load); anything
    // else is the opaque TE blob, restored verbatim as a string.
    for (auto& track : outDocument.tracks) {
        for (auto& element : track.chain.fxChainElements) {
            if (!isDevice(element))
                continue;
            auto& device = getDevice(element);
            const auto statePath = device.pluginState;
            if (statePath.isEmpty())
                continue;
            const int stateIndex = zip.getIndexOfFileName(statePath, false);
            if (stateIndex < 0)
                continue;  // not an embedded path; leave untouched

            std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(stateIndex));
            if (!stream)
                continue;

            if (statePath.endsWithIgnoreCase(".vstpreset")) {
                juce::MemoryBlock bytes;
                stream->readIntoMemoryBlock(bytes);
                device.vst3Preset = juce::Base64::toBase64(bytes.getData(), bytes.getSize());
                device.pluginState = {};  // clear the path placeholder
            } else {
                device.pluginState = stream->readEntireStreamAsString();
            }
        }
    }

    if (metadataXml.isNotEmpty()) {
        if (auto metadata = juce::parseXML(metadataXml)) {
            if (auto* title = metadata->getChildByName("Title")) {
                const auto name = title->getAllSubText().trim();
                if (name.isNotEmpty())
                    outDocument.info.name = name;
            }
        }
    }

    return true;
}

}  // namespace magda
