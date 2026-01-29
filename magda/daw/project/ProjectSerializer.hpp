#pragma once

#include <juce_core/juce_core.h>

#include "../core/AutomationInfo.hpp"
#include "../core/ClipInfo.hpp"
#include "../core/TrackInfo.hpp"
#include "ProjectInfo.hpp"

namespace magda {

/**
 * @brief Main serialization class for Magda projects
 *
 * Handles serialization/deserialization of complete project state to/from JSON format.
 * Files are compressed with gzip for efficient storage while remaining debuggable.
 */
class ProjectSerializer {
  public:
    // ========================================================================
    // High-level save/load (handles gzip compression)
    // ========================================================================

    /**
     * @brief Save entire project to .mgd file
     * @param file Target file path
     * @param info Project metadata
     * @return true on success, false on error
     */
    static bool saveToFile(const juce::File& file, const ProjectInfo& info);

    /**
     * @brief Load entire project from .mgd file
     * @param file Source file path
     * @param outInfo Output project metadata
     * @return true on success, false on error
     */
    static bool loadFromFile(const juce::File& file, ProjectInfo& outInfo);

    // ========================================================================
    // Project-level serialization
    // ========================================================================

    /**
     * @brief Serialize entire project to JSON
     * @param info Project metadata
     * @return JSON var containing complete project state
     */
    static juce::var serializeProject(const ProjectInfo& info);

    /**
     * @brief Deserialize JSON to project
     * @param json JSON var containing project state
     * @param outInfo Output project metadata
     * @return true on success, false on error
     */
    static bool deserializeProject(const juce::var& json, ProjectInfo& outInfo);

    // ========================================================================
    // Component-level serialization
    // ========================================================================

    /**
     * @brief Serialize all tracks to JSON array
     */
    static juce::var serializeTracks();

    /**
     * @brief Serialize all clips to JSON array
     */
    static juce::var serializeClips();

    /**
     * @brief Serialize all automation lanes to JSON array
     */
    static juce::var serializeAutomation();

    // ========================================================================
    // Component-level deserialization
    // ========================================================================

    /**
     * @brief Deserialize tracks from JSON array
     * @param json JSON array containing track data
     * @return true on success, false on error
     */
    static bool deserializeTracks(const juce::var& json);

    /**
     * @brief Deserialize clips from JSON array
     * @param json JSON array containing clip data
     * @return true on success, false on error
     */
    static bool deserializeClips(const juce::var& json);

    /**
     * @brief Deserialize automation lanes from JSON array
     * @param json JSON array containing automation data
     * @return true on success, false on error
     */
    static bool deserializeAutomation(const juce::var& json);

    /**
     * @brief Get last error message
     */
    static const juce::String& getLastError() {
        return lastError_;
    }

  private:
    // ========================================================================
    // Track serialization helpers
    // ========================================================================

    static juce::var serializeTrackInfo(const TrackInfo& track);
    static bool deserializeTrackInfo(const juce::var& json, TrackInfo& outTrack);

    static juce::var serializeChainElement(const ChainElement& element);
    static bool deserializeChainElement(const juce::var& json, ChainElement& outElement);

    static juce::var serializeDeviceInfo(const DeviceInfo& device);
    static bool deserializeDeviceInfo(const juce::var& json, DeviceInfo& outDevice);

    static juce::var serializeRackInfo(const RackInfo& rack);
    static bool deserializeRackInfo(const juce::var& json, RackInfo& outRack);

    static juce::var serializeChainInfo(const ChainInfo& chain);
    static bool deserializeChainInfo(const juce::var& json, ChainInfo& outChain);

    // ========================================================================
    // Clip serialization helpers
    // ========================================================================

    static juce::var serializeClipInfo(const ClipInfo& clip);
    static bool deserializeClipInfo(const juce::var& json, ClipInfo& outClip);

    static juce::var serializeAudioSource(const AudioSource& source);
    static bool deserializeAudioSource(const juce::var& json, AudioSource& outSource);

    static juce::var serializeMidiNote(const MidiNote& note);
    static bool deserializeMidiNote(const juce::var& json, MidiNote& outNote);

    // ========================================================================
    // Automation serialization helpers
    // ========================================================================

    static juce::var serializeAutomationLaneInfo(const AutomationLaneInfo& lane);
    static bool deserializeAutomationLaneInfo(const juce::var& json, AutomationLaneInfo& outLane);

    static juce::var serializeAutomationClipInfo(const AutomationClipInfo& clip);
    static bool deserializeAutomationClipInfo(const juce::var& json, AutomationClipInfo& outClip);

    static juce::var serializeAutomationPoint(const AutomationPoint& point);
    static bool deserializeAutomationPoint(const juce::var& json, AutomationPoint& outPoint);

    static juce::var serializeAutomationTarget(const AutomationTarget& target);
    static bool deserializeAutomationTarget(const juce::var& json, AutomationTarget& outTarget);

    static juce::var serializeBezierHandle(const BezierHandle& handle);
    static bool deserializeBezierHandle(const juce::var& json, BezierHandle& outHandle);

    static juce::var serializeChainNodePath(const ChainNodePath& path);
    static bool deserializeChainNodePath(const juce::var& json, ChainNodePath& outPath);

    // ========================================================================
    // Macro and Mod serialization helpers
    // ========================================================================

    static juce::var serializeMacroInfo(const MacroInfo& macro);
    static bool deserializeMacroInfo(const juce::var& json, MacroInfo& outMacro);

    static juce::var serializeModInfo(const ModInfo& mod);
    static bool deserializeModInfo(const juce::var& json, ModInfo& outMod);

    static juce::var serializeParameterInfo(const ParameterInfo& param);
    static bool deserializeParameterInfo(const juce::var& json, ParameterInfo& outParam);

    // ========================================================================
    // Utility functions
    // ========================================================================

    /**
     * @brief Convert a colour to hex string for JSON
     */
    static juce::String colourToString(const juce::Colour& colour);

    /**
     * @brief Convert hex string from JSON to colour
     */
    static juce::Colour stringToColour(const juce::String& str);

    /**
     * @brief Make file path relative to project directory
     */
    static juce::String makeRelativePath(const juce::File& projectFile,
                                         const juce::File& targetFile);

    /**
     * @brief Resolve relative path from project directory
     */
    static juce::File resolveRelativePath(const juce::File& projectFile,
                                          const juce::String& relativePath);

    static inline thread_local juce::String lastError_;
};

}  // namespace magda
