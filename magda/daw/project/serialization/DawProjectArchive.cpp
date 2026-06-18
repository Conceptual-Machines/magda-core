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
                                     juce::String& error) {
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
