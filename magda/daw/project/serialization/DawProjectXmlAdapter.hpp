#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "ProjectDocument.hpp"

namespace magda {

class DawProjectXmlAdapter {
  public:
    // An on-disk audio source and the relative path it gets stored under inside
    // the .dawproject archive. DAWproject references embedded media by a relative
    // path (external="false"); the archive writer copies these files in.
    struct EmbeddedAudioFile {
        juce::String sourcePath;   // absolute on-disk path of the sample
        juce::String archivePath;  // relative path inside the .dawproject zip
    };

    // Raw bytes of a device's state file and the relative path it gets stored
    // under inside the archive. For VST3 this is the .vstpreset (Steinberg preset,
    // loadable by other hosts); for other devices it's MAGDA's opaque TE plugin-
    // state blob. The <State> fileReference points at this path.
    struct EmbeddedDeviceState {
        juce::MemoryBlock bytes;   // exact file contents to write into the archive
        juce::String archivePath;  // relative path inside the .dawproject zip
    };

    static juce::String toProjectXml(const ProjectDocument& document);
    static bool fromProjectXml(const juce::String& xml, ProjectDocument& outDocument,
                               juce::String& error);

    // Unique, on-disk audio sources referenced by the document's audio clips,
    // each assigned a collision-free archive-relative path. toProjectXml() and
    // the archive writer share this so the XML refs and the stored files agree.
    static std::vector<EmbeddedAudioFile> collectEmbeddedAudio(const ProjectDocument& document);

    // Plugin state chunks for the VST3/AU devices the exporter writes, each
    // assigned an archive-relative path. toProjectXml() and the archive writer
    // share this so the <State> refs and the stored chunks agree.
    static std::vector<EmbeddedDeviceState> collectDeviceStates(const ProjectDocument& document);
};

}  // namespace magda
