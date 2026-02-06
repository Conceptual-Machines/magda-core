# MAGDA - Project Vision & Feature Requirements

## Project Overview
MAGDA - **Multi-Agent Digital Audio** - a DAW that combines traditional audio production with AI-driven multi-agent collaboration. Beyond the multi-agent framework we've discussed, here are the core DAW features and design principles.

## Core DAW Features

### 1. Hybrid Track System
**Concept**: No distinction between audio and MIDI tracks (like Reaper/Bitwig hybrid tracks)

**Requirements**:
- Single track type that can contain both audio and MIDI clips
- Seamless workflow between MIDI and audio content
- Plugin chains work identically for both content types
- Track routing and processing unified

**Benefits**:
- Simplified workflow
- More flexible arrangements
- Easier collaboration between different content types

### 2. Advanced Bounce-in-Place System
**Concept**: Freeze MIDI to audio while retaining full editing capability

**Core Features**:
- **Bounce with Plugin Chain Retention**: When bouncing MIDI to audio, keep the original plugin chain active
- **Layered Processing**: Audio result can still be processed through the same effects chain
- **Reversible Operations**: Always able to return to MIDI source

**Advanced Features**:
- **Super Undo History**: Complete history of all bounce operations on a track
- **Track State Timeline**: Visual representation of track evolution (MIDI → Audio → Re-MIDI → etc.)
- **Version Management**: Multiple versions of track states accessible
- **Non-destructive Workflow**: Original MIDI always preserved

**Implementation Notes**:
- Track history stored as state snapshots
- Each bounce operation creates new history node
- Plugin chain states also versioned
- Background processing for large bounce operations

### 3. User Interface Design

#### Layout Philosophy
**Inspiration**: Ableton Live / Bitwig Studio hybrid approach

**Main Layout**:
- **Top**: Track headers and mixer controls
- **Middle**: Main timeline/arrangement view
- **Bottom**: Context-sensitive detail panel

#### Bottom Panel System
**Dynamic Content Based on Selection**:
- **Track Content View**: When track selected, show clips, automation, etc.
- **Plugin Chain View**: When plugin selected, show full effects chain with parameters
- **DSP Script View**: When DSP processor selected, show code editor and parameters
- **Agent Prompt Space**: Dedicated tab for multi-agent interaction and conversation
- **Seamless Switching**: Quick toggle between content, plugin, script, and prompt views

#### Track Containers
**Concept**: Hierarchical track organization like Ableton/Bitwig groups

**Features**:
- **Group Tracks**: Container tracks that hold other tracks
- **Nested Groups**: Groups within groups for complex arrangements
- **Group Processing**: Plugin chains on group tracks affect all contained tracks
- **Collapsible UI**: Expand/collapse groups for organization
- **Group Automation**: Automate group-level parameters

### 4. Dual Mode System: Live vs Arrangement

#### Conceptual Difference
Not just different views, but different **CPU optimization strategies**:

#### **Arrangement Mode** (Primary Development Target)
**Characteristics**:
- **Linear Timeline**: Traditional DAW arrangement view
- **CPU Optimization**: Larger audio buffers for efficiency
- **Workflow**: Composition, arrangement, mixing, mastering
- **Processing**: Optimized for larger projects, complex routing

**Technical Implementation**:
- Buffer sizes: 512-1024 samples
- Background processing for non-critical operations
- Disk streaming optimized for large projects
- Plugin latency compensation active

#### **Live Mode** (Future Implementation)
**Characteristics**:
- **Session/Clip View**: Grid-based clip launching (like Ableton Live)
- **CPU Strategy**: Maximum responsiveness at expense of efficiency
- **Workflow**: Live performance, jamming, loop-based composition
- **Processing**: Low-latency, immediate response

**Technical Implementation**:
- Buffer sizes: 64-128 samples
- Maximum polling frequency
- Aggressive CPU usage for minimal latency
- Real-time processing prioritized
- Simplified routing for performance

### 5. Integrated DSP Language & Scripting

#### DSP Language Integration
**Concept**: Built-in DSP scripting language for custom audio processing

**Core Features**:
- **High-Level DSP Language**: Similar to JUCE DSP, Faust, or Gen~ (Max/MSP)
- **Real-time Compilation**: Scripts compile to optimized native code
- **Visual Editor**: Node-based visual programming option alongside text
- **Hot-swapping**: Modify DSP code during playback without interruption

**Supported Operations**:
- Audio effects processing (filters, delays, reverbs, distortion)
- MIDI processing and transformation
- Control voltage generation and modulation
- Custom synthesis algorithms
- Advanced routing and mixing

#### DSP Agent System
**Concept**: Dedicated AI agent for DSP script creation and optimization

**Agent Capabilities**:
- **Natural Language to DSP**: "Create a warm analog filter" → Generated filter code
- **Code Analysis**: Automatically detect performance issues and suggest optimizations
- **Effect Suggestions**: Recommend DSP processing based on audio content analysis
- **Learning System**: Learns from user preferences and coding patterns
- **Template Generation**: Creates boilerplate code for common DSP patterns

**Integration Points**:
- **Chat Interface**: Natural language DSP requests
- **Code Completion**: Intelligent autocomplete for DSP functions
- **Real-time Optimization**: Automatic code improvements during development
- **Effect Morphing**: Gradually transform one effect into another via code interpolation

### 6. Intelligent Sample Browser & Organization

#### Concept
**Philosophy**: ML-powered sample browser that understands audio content and organizes samples by sonic similarity

**Core Features**:
- **Audio Content Analysis**: Deep learning models analyze spectral content, rhythm, key, mood
- **Similarity Clustering**: Automatically group samples by sonic characteristics
- **Smart Tagging**: AI-generated tags based on audio analysis (genre, energy, timbre, etc.)
- **Contextual Search**: "Find drums like this but with more reverb" or "Similar basslines in different keys"

#### Background Organization Agent
**Concept**: Dedicated agent continuously organizing and analyzing sample library

**Agent Capabilities**:
- **Automatic Scanning**: Monitors sample folders and analyzes new content
- **Similarity Mapping**: Creates multidimensional similarity maps for browsing
- **Duplicate Detection**: Finds near-identical samples across different libraries
- **Quality Assessment**: Flags low-quality samples (clipping, noise, etc.)
- **Missing Tag Generation**: Fills in metadata gaps using audio analysis

**Organization Features**:
- **Dynamic Collections**: Auto-generated playlists based on similarity clusters
- **Cross-Library Search**: Search across multiple sample libraries simultaneously
- **Version Tracking**: Track different versions/remixes of the same sample
- **Usage Analytics**: Learn from user preferences and selection patterns

#### Advanced Browse Modes
**Similarity Browse**:
- **2D Similarity Map**: Visual representation of sample relationships
- **Radial Browser**: Navigate outward from a seed sample to find similar content
- **Morphing Search**: Smoothly transition between different sonic characteristics
- **AI Recommendations**: "Samples that work well with your current project"

**Content-Aware Features**:
- **Key/Scale Detection**: Automatic musical key analysis and matching
- **Tempo Sync**: BPM detection and tempo-matched browsing
- **Harmonic Compatibility**: Find samples that work harmonically together
- **Energy Level Matching**: Match samples by perceived energy/intensity

#### Integration Points
**Multi-Agent Collaboration**:
- **Composition Agent**: Suggests samples during composition based on context
- **Mixing Agent**: Recommends samples that fit current mix spectrum
- **DSP Agent**: Automatically applies processing to match sample characteristics

**Project Integration**:
- **Context-Aware Suggestions**: Different recommendations based on current track content
- **Auto-Replacement**: Swap samples with similar alternatives for variation
- **Sample Chain Building**: Build percussion kits from compatible one-shots

### 7. Multi-Agent Prompt Space & Interaction Design

#### Concept
**Philosophy**: Natural language interface should feel integrated and context-aware, not like a separate chat application

#### Primary Prompt Space Locations

**1. Bottom Panel Integration (Primary)**
- **Dedicated Agent Tab**: Full-featured prompt space in the bottom panel system
- **Context Awareness**: Automatically understands current selection (track, plugin, sample, etc.)
- **Multi-Agent Display**: Shows which agents are active and their specialties
- **Conversation History**: Persistent chat history with threading by topic/agent
- **Smart Suggestions**: Context-aware prompt suggestions based on current workflow

**2. Quick Prompt Overlay (Secondary)**
- **Keyboard Shortcut Access**: Instant popup prompt (like VS Code command palette)
- **Minimal Interface**: Single line input with smart autocomplete
- **Quick Actions**: Fast commands without leaving current view
- **Agent Routing**: Smart routing to appropriate agent based on context

**3. Contextual Mini-Prompts (Tertiary)**
- **Track-Level Prompts**: Small prompt inputs directly on track headers
- **Plugin Prompts**: Embedded prompts in plugin interfaces
- **Sample Browser Prompts**: Integrated search and generation in sample browser
- **Timeline Prompts**: Right-click → "Ask Agent" anywhere on timeline

#### Prompt Space Features

**Context Intelligence**:
- **Current Selection Awareness**: "Add reverb to this track" automatically knows which track
- **Project State Understanding**: Agents know tempo, key, current mix state, etc.
- **Workflow Phase Detection**: Different suggestions for composition vs mixing vs mastering
- **Multi-Selection Support**: "Process these 3 tracks the same way"

**Multi-Agent Coordination**:
- **Agent Picker**: Choose which agent to talk to, or let system auto-route
- **Agent Handoffs**: Smooth transitions between agents ("Let me get the DSP agent for this")
- **Collaborative Responses**: Multiple agents working together on complex requests
- **Agent Status Indicators**: Show which agents are currently active/thinking

**Conversation Management**:
- **Threading**: Organize conversations by topic (mixing, composition, technical, etc.)
- **Bookmarking**: Save important agent responses for later reference
- **Undo Integration**: "Undo what the agent just did" as natural command
- **Progress Tracking**: Visual feedback for long-running agent operations

#### UX Integration Principles

**Non-Intrusive Design**:
- **Collapsible/Expandable**: Can minimize when not needed
- **Floating Options**: Drag prompt space to different screen areas
- **Transparency Modes**: Semi-transparent overlay options
- **Smart Auto-Hide**: Disappears during playback unless explicitly pinned

**Natural Language Design**:
- **Conversational Tone**: Agents respond like helpful collaborators, not robots
- **Technical Flexibility**: Can handle both "make it sound warmer" and "add 3dB at 2kHz"
- **Learning Adaptation**: Interface learns user's preferred communication style
- **Voice Input Support**: Full voice command system for hands-free operation (see below)

#### Voice Command System

**Concept**: Hands-free DAW control through natural speech - essential for musicians whose hands are on instruments

**Core Voice Features**:

**1. Always-On Listening Mode**
- **Wake Word**: Configurable activation phrase ("Hey MAGDA", "OK MAGDA", custom)
- **Push-to-Talk**: Hardware button or foot pedal activation
- **Continuous Mode**: Always listening during recording sessions
- **Privacy Controls**: Local processing option, no cloud required

**2. Command Categories**

**Transport Control**:
```
"Play" / "Stop" / "Record"
"Go to bar 32"
"Loop the chorus"
"Set tempo to 128"
"Punch in at the bridge"
```

**Track Operations**:
```
"Mute the drums"
"Solo guitar track"
"Add a new audio track"
"Duplicate this track"
"Arm track 3 for recording"
```

**Mixing Commands**:
```
"Turn up the bass by 3 dB"
"Pan the vocals left"
"Add reverb to the snare"
"Bypass the compressor on track 2"
"Set the master volume to -6"
```

**Agent Interaction**:
```
"Hey MAGDA, make this warmer"
"Suggest some drum patterns"
"Clean up this vocal take"
"Find me a similar bassline"
"What's clashing in this mix?"
```

**Navigation**:
```
"Zoom in on the verse"
"Show me the mixer"
"Open the piano roll"
"Go to marker 'chorus'"
```

**3. Context-Aware Processing**
- **Track Context**: "Add compression" applies to selected track
- **Time Context**: "Delete this" removes content at playhead
- **Selection Context**: "Move it up an octave" affects selected notes
- **Workflow Context**: Different command interpretation during mixing vs recording

**4. Voice Feedback Options**
- **Audio Confirmation**: Brief spoken confirmations ("Done", "Track muted")
- **Visual Only**: Silent operation with on-screen feedback
- **Detailed Mode**: Full spoken status updates
- **Configurable Verbosity**: User controls feedback level

**Technical Implementation**:

**Speech Recognition Pipeline**:
```
Audio Input → VAD (Voice Activity Detection)
                    ↓
            Wake Word Detection
                    ↓
            Speech-to-Text (Local/Cloud)
                    ↓
            Intent Classification
                    ↓
            Command Execution / Agent Routing
                    ↓
            Feedback Generation
```

**Processing Options**:
- **Local Processing** (Privacy-focused):
  - Whisper.cpp for speech-to-text
  - Local intent classification model
  - Zero latency for simple commands

- **Cloud Processing** (Accuracy-focused):
  - OpenAI Whisper API
  - Higher accuracy for complex phrases
  - Fallback when local fails

- **Hybrid Mode** (Recommended):
  - Local for transport/simple commands
  - Cloud for complex agent interactions
  - Automatic routing based on command complexity

**Latency Targets**:
- **Transport Commands**: <100ms (critical for performance)
- **Track Operations**: <200ms
- **Agent Queries**: <500ms initial response
- **Complex Processing**: Background with progress feedback

**Hardware Integration**:
- **Foot Pedals**: Map pedals to push-to-talk or common commands
- **MIDI Controllers**: Trigger voice mode via MIDI
- **Stream Deck**: Voice command buttons with visual feedback
- **Headset Mics**: Optimized for close-mic voice capture

**Voice Training & Customization**:
- **User Voice Profile**: Adapts to individual speech patterns
- **Custom Commands**: Define personal command aliases
- **Accent Support**: Multiple language/accent models
- **Noise Adaptation**: Learns to filter studio environment noise

**Recording Session Integration**:
- **Non-Interfering**: Voice commands filtered from recordings
- **Talkback Integration**: Seamless switch between commands and talkback
- **Cue System**: Voice-triggered cue points and markers
- **Take Management**: "That's a keeper" / "Delete that take"

**Safety & Control**:
- **Confirmation Required**: Destructive operations need verbal confirmation
- **Undo by Voice**: "Undo that" / "Bring it back"
- **Emergency Stop**: "Stop everything" halts all operations
- **Command History**: Review and repeat recent voice commands

**Accessibility Benefits**:
- **Hands-Free Production**: Essential for musicians with mobility limitations
- **Eyes-Free Operation**: Work without looking at screen
- **Reduced RSI**: Less mouse/keyboard repetitive strain
- **Workflow Speed**: Faster than navigating menus

**Use Cases**:
- **Live Recording**: Control transport while playing instrument
- **Mixing Sessions**: Adjust levels while listening critically
- **Arrangement**: Navigate and edit while away from computer
- **Collaboration**: Voice commands during studio sessions
- **Accessibility**: Full DAW control for users with limited mobility

**Visual Integration**:
- **Color Coding**: Different agents have subtle color themes
- **Status Animations**: Gentle animations show agent thinking/working
- **Result Highlighting**: Visual feedback showing what agents changed
- **Suggestion Overlays**: Transparent suggestions appear over relevant UI elements

### 8. Modular Architecture & Engine Migration Strategy

#### Concept
**Philosophy**: Design with abstraction layers to enable gradual migration from high-level frameworks to bare-metal performance

#### Migration Path Strategy
**Phase 1: Rapid Development (Current)**
- **Tracktion Engine**: Full DAW functionality, fast prototyping
- **JUCE UI**: Cross-platform GUI framework
- **Benefits**: Quick feature development, stable foundation, extensive documentation

**Phase 2: Performance Optimization**
- **Custom Audio Engine**: Replace Tracktion with optimized custom engine
- **JUCE UI Retained**: Keep familiar UI framework initially
- **Benefits**: Better performance, custom optimizations, reduced dependencies

**Phase 3: Maximum Performance**
- **Bare Metal Audio**: Custom SIMD-optimized audio processing
- **Native UI**: Platform-specific UI for maximum responsiveness
- **Benefits**: Minimal latency, maximum CPU efficiency, platform-optimized

#### Abstraction Layer Design

**Audio Engine Interface**
```cpp
// Abstract base class that any engine can implement
class AudioEngineInterface {
public:
    virtual ~AudioEngineInterface() = default;

    // Core engine operations
    virtual bool initialize(const AudioConfig& config) = 0;
    virtual void processAudio(AudioBuffer& buffer) = 0;
    virtual void shutdown() = 0;

    // Project management
    virtual std::unique_ptr<ProjectInterface> createProject() = 0;
    virtual bool loadProject(const std::string& path) = 0;
    virtual bool saveProject(const std::string& path) = 0;

    // Real-time operations
    virtual void play() = 0;
    virtual void stop() = 0;
    virtual void record() = 0;
    virtual double getCurrentPosition() const = 0;
};
```

**Engine Implementations**:
- `TracktionAudioEngine` (Phase 1)
- `MAGDAAudioEngine` (Phase 2)
- `BareMetalAudioEngine` (Phase 3)

**UI Framework Interface**
```cpp
// Abstract UI system for cross-framework compatibility
class UIFrameworkInterface {
public:
    virtual ~UIFrameworkInterface() = default;

    // Window management
    virtual std::unique_ptr<WindowInterface> createMainWindow() = 0;
    virtual std::unique_ptr<ComponentInterface> createComponent(ComponentType type) = 0;

    // Event handling
    virtual void processEvents() = 0;
    virtual void registerCallback(EventType type, std::function<void()> callback) = 0;

    // Rendering
    virtual void render() = 0;
    virtual void invalidateRegion(const Rectangle& region) = 0;
};
```

**UI Implementations**:
- `JUCEUIFramework` (Phase 1 & 2)
- `NativeUIFramework` (Phase 3 - platform-specific)
- `ImGuiUIFramework` (Alternative for Phase 2/3)

#### Abstraction Benefits

**Gradual Migration**:
- **No Big Bang Rewrites**: Replace components incrementally
- **Risk Mitigation**: Always have a working system during transitions
- **Performance Testing**: Compare implementations side-by-side
- **Rollback Capability**: Can revert to previous implementation if needed

**Development Flexibility**:
- **Parallel Development**: Different teams can work on different engine implementations
- **A/B Testing**: Compare performance and features of different backends
- **Platform Optimization**: Different engines for different platforms (mobile vs desktop)
- **Feature Parity**: Ensure all engines support the same feature set

**Performance Strategy**:
- **Bottleneck Identification**: Profile which components need optimization most
- **Targeted Optimization**: Replace only performance-critical components first
- **Benchmark Continuity**: Consistent performance testing across implementations
- **User Transparency**: Users experience smooth transitions between backends

#### Implementation Architecture

**Core Abstraction Layers**:
```
MAGDA Application Layer
├── Audio Engine Interface
│   ├── TracktionAudioEngine
│   ├── MAGDAAudioEngine (future)
│   └── BareMetalAudioEngine (future)
├── UI Framework Interface
│   ├── JUCEUIFramework
│   ├── NativeUIFramework (future)
│   └── ImGuiUIFramework (alternative)
├── Platform Abstraction Layer
│   ├── Audio Device Interface
│   ├── File System Interface
│   └── Threading Interface
└── Multi-Agent Framework (Separate Process/Thread Pool)
    ├── Agent Orchestrator
    ├── DSP Agent
    ├── Sample Organization Agent
    ├── Composition Agent
    ├── Mixing Agent
    └── Utility Agent
```

**Plugin Architecture**:
- **Effect Interface**: Abstract base for all audio processing
- **Instrument Interface**: Abstract base for all synthesizers/samplers
- **UI Component Interface**: Abstract base for plugin UIs
- **Parameter Interface**: Abstract automation and control system
- **Multi-Format Support**: VST2, VST3, AU, LV2, CLAP with unified interface

#### Migration Strategies Per Component

**Audio Processing Priority**:
1. **Real-time audio thread** (highest priority for custom implementation)
2. **Sample-accurate automation**
3. **Plugin hosting and processing**
4. **File I/O and streaming**
5. **MIDI processing**

**UI Migration Priority**:
1. **Main timeline and tracks** (most performance-critical)
2. **Real-time meters and visualization**
3. **Plugin UIs and parameter controls**
4. **File browsers and sample management**
5. **Settings and configuration dialogs**

**Compatibility Considerations**:
- **Project File Format**: Engine-agnostic JSON-based format with binary optimization
- **Plugin Compatibility**: Maintain VST/AU/LV2/CLAP support across all engines
- **User Preferences**: Settings work consistently across implementations
- **Script API**: Scripting interface remains stable across backend changes

#### Project Serialization Strategy

**Hybrid Serialization Approach**: JSON + Binary optimization for performance and version control

**Primary Format: JSON**
```json
{
  "magda_project": {
    "version": "1.0.0",
    "metadata": {
      "name": "My Song",
      "created": "2024-01-15T10:30:00Z",
      "tempo": 120.0,
      "time_signature": [4, 4],
      "sample_rate": 48000,
      "key": "C major"
    },
    "tracks": [
      {
        "id": "track_001",
        "name": "Drums",
        "type": "hybrid",
        "clips": [
          {
            "id": "clip_001",
            "type": "audio",
            "start_time": 0.0,
            "length": 4.0,
            "audio_ref": "audio/drums_take1.wav",
            "audio_hash": "sha256:abc123..."
          }
        ],
        "plugin_chain": [
          {
            "id": "plugin_001",
            "type": "vst3",
            "name": "ReaEQ",
            "parameters": {...},
            "enabled": true
          }
        ],
        "automation": {
          "volume": {
            "points": [[0.0, 0.8], [4.0, 1.0]],
            "curve_type": "linear"
          }
        }
      }
    ],
    "routing": {...},
    "version_control": {
      "current_version": "v1.2.0",
      "parent_versions": ["v1.1.0"],
      "branch": "main"
    }
  }
}
```

**Why JSON as Primary Format?**

**Advantages**:
- **Version Control Friendly**: Text-based, easy to diff and merge
- **Human Readable**: Can debug and understand project structure
- **Cross-Platform**: Universal support across all platforms
- **Agent Integration**: Multi-agent system can easily parse and modify
- **Extensible**: Easy to add new fields without breaking compatibility
- **Tooling**: Excellent ecosystem for validation, transformation, querying

**Performance Optimizations**:

**1. Binary Audio References**
```json
{
  "audio_ref": "audio/drums_take1.wav",
  "audio_hash": "sha256:abc123...",
  "waveform_cache": "cache/drums_take1.waveform.bin",
  "analysis_cache": "cache/drums_take1.analysis.json"
}
```

**2. Compressed Plugin State**
```json
{
  "plugin_state": {
    "format": "base64_compressed",
    "data": "eJyNkl1vgjAURp9L...",
    "original_size": 2048,
    "compressed_size": 512
  }
}
```

**3. Lazy Loading Structure**
```json
{
  "large_data_ref": "data/automation_chunk_001.bin",
  "preview": {
    "point_count": 1500,
    "time_range": [0.0, 240.0],
    "value_range": [0.0, 1.0]
  }
}
```

**Alternative Formats Considered**:

**XML (Tracktion Native)**
```xml
<EDIT>
  <MASTERVOLUME>
    <PLUGIN type="volume" id="1001"/>
  </MASTERVOLUME>
</EDIT>
```
- ✅ Native Tracktion support
- ❌ Verbose, larger files
- ❌ More complex parsing

**Binary Formats (MessagePack, Protocol Buffers)**
```
Advantages: Fast, compact
Disadvantages: Not version-control friendly, not human readable
```

**YAML**
```yaml
magda_project:
  version: "1.0.0"
  tracks:
    - name: "Drums"
      clips: [...]
```
- ✅ Very readable
- ❌ Parsing complexity
- ❌ Performance concerns

**Hybrid Implementation Strategy**:

**File Structure**:
```
MyProject.magda/
├── project.json              # Main project data
├── audio/                    # Audio file references
│   ├── source/              # Original audio files
│   └── processed/           # Bounced/processed audio
├── cache/                   # Performance caches
│   ├── waveforms/          # Visual waveform data
│   ├── analysis/           # Audio analysis results
│   └── thumbnails/         # Visual previews
├── data/                    # Large binary data chunks
│   ├── automation/         # Dense automation data
│   └── plugin_states/      # Binary plugin states
└── .magda/                # Version control data
    ├── versions.db
    └── objects/
```

**Loading Strategy**:
1. **Parse JSON**: Fast initial load of project structure
2. **Lazy Load**: Load heavy data (automation, analysis) on demand
3. **Cache**: Keep frequently accessed data in memory
4. **Background**: Load remaining data in background threads

**Performance Benchmarks Target**:
- **Small Project** (<10 tracks): <500ms load time
- **Medium Project** (50 tracks): <2s load time
- **Large Project** (200+ tracks): <5s load time
- **Version Switch**: <2s for any project size

**Version Control Integration**:
- **JSON Diffs**: Clean, readable diffs for project changes
- **Audio Handling**: Content-addressed storage prevents duplication
- **Selective Loading**: Only load changed components when switching versions
- **Conflict Resolution**: Text-based merging with semantic understanding

#### DAWproject Interoperability & Track Import

**Concept**: Seamless project and track importing via the open DAWproject standard

**DAWproject Overview**:
- **Open Standard**: Bitwig-initiated, MIT-licensed format for DAW interoperability
- **Current Support**: Bitwig Studio, Studio One, Cubase, Cubasis, VST Live
- **Format**: ZIP container with XML structure (project.xml, metadata.xml)
- **Content**: Audio/MIDI timelines, automation, plugin states, track structure

**Import Capabilities**:

**1. Full Project Import**
```
File → Import → DAWproject File (.dawproject)
```
- Import complete arrangements from other DAWs
- Preserve track structure, routing, and organization
- Maintain plugin states (where compatible)
- Convert automation curves and envelopes
- Import embedded audio and MIDI data

**2. Selective Track Import**
```
Track → Import Track from DAWproject...
```
- Browse and select specific tracks from external projects
- Import individual tracks into current project
- Maintain track's plugin chain and automation
- Automatic tempo/key matching options
- Preview tracks before importing

**3. Agent-Assisted Import**
- **Composition Agent**: Suggests which tracks to import based on current project context
- **Mixing Agent**: Analyzes imported tracks for mix compatibility
- **Orchestrator**: Coordinates import process and resolves conflicts

**Technical Implementation**:

**DAWproject Parser**:
```cpp
class DAWprojectImporter {
public:
    // Full project import
    MAGDAProject importProject(const std::string& dawprojectPath);

    // Selective track import
    std::vector<Track> importTracks(const std::string& dawprojectPath,
                                   const std::vector<std::string>& trackIds);

    // Preview capabilities
    ProjectMetadata getProjectInfo(const std::string& dawprojectPath);
    std::vector<TrackInfo> getTrackList(const std::string& dawprojectPath);

private:
    XMLParser parser_;
    AudioFormatManager audioManager_;
    PluginStateConverter pluginConverter_;
};
```

**Conversion Strategy**:

**1. Track Mapping**
```
DAWproject Track → MAGDA Hybrid Track
├── Audio clips → Audio clips (direct)
├── MIDI clips → MIDI clips (direct)
├── Plugin chain → Converted plugin chain
├── Automation → MAGDA automation format
└── Routing → MAGDA routing system
```

**2. Plugin Compatibility**
```
Priority Order:
1. Direct plugin match (same VST/AU)
2. Generic plugin mapping (EQ → MAGDA EQ)
3. DSP Agent recreation ("Create similar reverb")
4. Bypass with notification
```

**3. Time/Musical Mapping**
```
DAWproject Timeline → MAGDA Timeline
├── Tempo changes → Tempo automation
├── Time signatures → Time signature events
├── Musical timing → Beat-based positioning
└── Audio timing → Sample-accurate positioning
```

**Export Capabilities**:

**1. Native to DAWproject Export**
```cpp
class DAWprojectExporter {
public:
    // Export full project
    bool exportProject(const MAGDAProject& project,
                      const std::string& outputPath);

    // Export selected tracks
    bool exportTracks(const std::vector<Track>& tracks,
                     const std::string& outputPath);

    // Export options
    struct ExportOptions {
        bool includePluginStates = true;
        bool includeAutomation = true;
        bool bounceToAudio = false;
        AudioFormat audioFormat = AudioFormat::WAV_48kHz_24bit;
    };
};
```

**2. Agent-Enhanced Export**
- **Version Control Agent**: Suggests which project version to export
- **Mixing Agent**: Offers to bounce tracks to audio for better compatibility
- **DSP Agent**: Converts custom DSP scripts to compatible effects

**User Experience**:

**Import Workflow**:
1. **File Browser**: Specialized DAWproject browser with project previews
2. **Track Selection**: Visual track selector with audio previews
3. **Compatibility Check**: Real-time analysis of plugin/feature compatibility
4. **Import Options**: Tempo matching, key transposition, track placement
5. **Agent Suggestions**: Context-aware recommendations during import

**Use Cases**:

**1. Multi-DAW Workflow**
- Start composition in Bitwig, import to MAGDA for mixing
- Move between DAWs for different production stages
- Collaborate with users of different DAWs

**2. Track Library Building**
- Import individual tracks from old projects
- Build libraries of processed tracks and stems
- Create template collections from various sources

**3. Remix & Collaboration**
- Import tracks for remixing
- Share project components with other producers
- Version control with external collaborators

**Benefits for MAGDA**:
- **Ecosystem Integration**: Works with existing DAW ecosystem
- **Migration Path**: Easy migration from other DAWs to MAGDA
- **Collaboration**: Enhanced collaboration across different platforms
- **Track Reuse**: Extensive track and stem library capabilities
- **Learning**: Analyze projects from other DAWs for educational purposes

#### CLAP Plugin Format Support & Implementation Strategy

**CLAP Overview**:
- **Modern Standard**: Open-source plugin format designed for modern DAWs
- **Performance**: Lower latency, better threading, more efficient than VST
- **Future-Proof**: Designed with modern audio processing in mind
- **Growing Ecosystem**: Increasing adoption by major plugin developers

**Current Status & Challenge**:
- **Tracktion Engine**: Limited CLAP support (JUCE-based, older plugin architecture)
- **JUCE**: Basic CLAP hosting support available but not fully integrated
- **Migration Path**: Need custom CLAP implementation for full support

**Implementation Strategy**:

**Phase 1: JUCE-Based CLAP Support (Current)**
```cpp
// Leverage JUCE's existing CLAP support
class JUCEPluginManager {
private:
    juce::AudioPluginFormatManager formatManager_;
    std::unique_ptr<juce::CLAPPluginFormat> clapFormat_;

public:
    bool initializeCLAPSupport() {
        clapFormat_ = std::make_unique<juce::CLAPPluginFormat>();
        formatManager_.addFormat(clapFormat_.get());
        return true;
    }

    std::vector<juce::PluginDescription> scanCLAPPlugins() {
        return clapFormat_->findAllTypes();
    }
};
```

**Phase 2: Custom CLAP Host Implementation**
```cpp
// Direct CLAP hosting for maximum performance
class CLAPHost {
private:
    clap_host_t host_;
    std::vector<std::unique_ptr<CLAPPlugin>> plugins_;

public:
    // CLAP host callbacks
    static const clap_host_t* getCLAPHost() {
        static clap_host_t host = {
            .clap_version = CLAP_VERSION,
            .host_data = nullptr,
            .name = "MAGDA",
            .vendor = "MAGDA",
            .url = "https://magda-daw.com",
            .version = MAGICA_VERSION,
            .get_extension = getExtension,
            .request_restart = requestRestart,
            .request_process = requestProcess,
            .request_callback = requestCallback
        };
        return &host;
    }

    // Plugin management
    bool loadCLAPPlugin(const std::string& path);
    void unloadCLAPPlugin(const std::string& pluginId);
    std::vector<CLAPParameter> getPluginParameters(const std::string& pluginId);
};
```

**Phase 3: Unified Plugin Interface**
```cpp
// Abstract plugin interface supporting all formats
class PluginInterface {
public:
    virtual ~PluginInterface() = default;

    // Common plugin operations
    virtual bool initialize(const AudioConfig& config) = 0;
    virtual void processAudio(AudioBuffer& buffer) = 0;
    virtual void shutdown() = 0;

    // Parameter management
    virtual std::vector<Parameter> getParameters() const = 0;
    virtual void setParameter(const std::string& id, double value) = 0;
    virtual double getParameter(const std::string& id) const = 0;

    // State management
    virtual void saveState(std::vector<uint8_t>& data) = 0;
    virtual void loadState(const std::vector<uint8_t>& data) = 0;

    // Plugin info
    virtual std::string getName() const = 0;
    virtual std::string getVendor() const = 0;
    virtual PluginType getType() const = 0;
};

// Format-specific implementations
class CLAPPlugin : public PluginInterface {
private:
    const clap_plugin_t* plugin_;
    clap_plugin_params_t* params_;
    clap_plugin_state_t* state_;

public:
    // CLAP-specific implementation
    bool initialize(const AudioConfig& config) override;
    void processAudio(AudioBuffer& buffer) override;
    // ... other implementations
};

class VST3Plugin : public PluginInterface {
    // VST3-specific implementation
};

class AUPlugin : public PluginInterface {
    // AU-specific implementation
};
```

**Plugin Discovery & Management**:

**1. Multi-Format Scanner**
```cpp
class PluginScanner {
public:
    struct PluginInfo {
        std::string name;
        std::string vendor;
        std::string path;
        PluginFormat format; // CLAP, VST3, AU, LV2
        PluginType type;     // Effect, Instrument, etc.
        std::string version;
        bool is64bit;
        bool isValid;
    };

    std::vector<PluginInfo> scanAllFormats() {
        std::vector<PluginInfo> allPlugins;

        // Scan CLAP plugins
        auto clapPlugins = scanCLAPPlugins();
        allPlugins.insert(allPlugins.end(), clapPlugins.begin(), clapPlugins.end());

        // Scan VST3 plugins
        auto vst3Plugins = scanVST3Plugins();
        allPlugins.insert(allPlugins.end(), vst3Plugins.begin(), vst3Plugins.end());

        // Scan AU plugins (macOS only)
        #ifdef JUCE_MAC
        auto auPlugins = scanAUPlugins();
        allPlugins.insert(allPlugins.end(), auPlugins.begin(), auPlugins.end());
        #endif

        return allPlugins;
    }

private:
    std::vector<PluginInfo> scanCLAPPlugins();
    std::vector<PluginInfo> scanVST3Plugins();
    std::vector<PluginInfo> scanAUPlugins();
};
```

**2. Plugin Compatibility Matrix**
```cpp
struct PluginCompatibility {
    std::string pluginId;
    std::vector<PluginFormat> supportedFormats;
    std::map<PluginFormat, std::string> formatPaths;
    bool hasCLAP;
    bool hasVST3;
    bool hasAU;
    bool hasLV2;

    // Performance metrics
    double clapLatency;
    double vst3Latency;
    double auLatency;

    // Feature support
    bool supportsNoteExpression;
    bool supportsPolyphonicModulation;
    bool supportsTempoSync;
};
```

**CLAP-Specific Features**:

**1. Note Expression Support**
```cpp
// CLAP's advanced note expression system
class CLAPNoteExpression {
public:
    enum class ExpressionType {
        Volume,
        Pan,
        Tuning,
        Vibrato,
        Expression,
        Brightness,
        Pressure
    };

    struct NoteExpression {
        int32_t noteId;
        int16_t portIndex;
        int16_t channel;
        int16_t key;
        ExpressionType type;
        double value;
    };

    void processNoteExpressions(const std::vector<NoteExpression>& expressions);
};
```

**2. Polyphonic Modulation**
```cpp
// CLAP's polyphonic modulation system
class CLAPPolyphonicModulation {
public:
    struct ModulationTarget {
        int32_t noteId;
        int16_t portIndex;
        int16_t channel;
        int16_t key;
        int32_t paramId;
        double value;
    };

    void processPolyphonicModulation(const std::vector<ModulationTarget>& targets);
};
```

**3. Transport Sync**
```cpp
// CLAP's transport synchronization
class CLAPTransport {
public:
    struct TransportInfo {
        double tempo;
        double tempoIncrement;
        int32_t barStart;
        double barNumber;
        double loopStart;
        double loopEnd;
        bool isPlaying;
        bool isRecording;
        bool isLoopActive;
    };

    void updateTransport(const TransportInfo& info);
};
```

**Performance Benefits**:

**1. Lower Latency**
- **CLAP**: Direct parameter access, no parameter smoothing overhead
- **VST3**: Parameter smoothing can add latency
- **AU**: Additional Core Audio overhead

**2. Better Threading**
- **CLAP**: Native multi-threading support
- **VST3**: Limited threading model
- **AU**: Single-threaded processing

**3. Memory Efficiency**
- **CLAP**: Smaller memory footprint
- **VST3**: Larger object model
- **AU**: Core Audio framework overhead

**Migration Strategy**:

**Phase 1: JUCE CLAP Support (Immediate)**
- Use JUCE's existing CLAP hosting capabilities
- Limited but functional CLAP support
- Compatible with current Tracktion Engine setup

**Phase 2: Custom CLAP Host (Phase 2)**
- Implement direct CLAP hosting
- Full CLAP feature support
- Better performance than JUCE wrapper

**Phase 3: Unified Plugin System (Phase 3)**
- Single interface for all plugin formats
- Automatic format selection based on availability
- Seamless migration between formats

**User Experience**:

**1. Plugin Browser**
- **Format Filtering**: Filter by CLAP, VST3, AU, LV2
- **Performance Indicators**: Show latency and CPU usage per format
- **Compatibility Warnings**: Alert users to format-specific issues
- **Format Preferences**: Allow users to prioritize certain formats

**2. Plugin Loading**
- **Smart Loading**: Automatically choose best available format
- **Fallback System**: If CLAP fails, try VST3, then AU
- **Performance Monitoring**: Track plugin performance across formats
- **Automatic Updates**: Update plugin format when better version available

**3. Project Compatibility**
- **Format Preservation**: Maintain plugin format in projects
- **Cross-Platform**: Handle format differences between platforms
- **Version Control**: Track plugin format changes in project history

**Benefits for MAGDA**:
- **Future-Proof**: CLAP is the modern standard for audio plugins
- **Performance**: Lower latency and better threading than VST3
- **Ecosystem**: Growing CLAP plugin ecosystem
- **Innovation**: Access to CLAP-specific features like note expressions
- **Competitive Advantage**: Early CLAP adoption positions MAGDA as forward-thinking

#### Multi-Agent Threading & Process Architecture

**Concept**: Complete isolation of AI agents from real-time audio processing

**Critical Requirements**:
- **Zero Audio Interference**: Agent processing must never affect audio thread performance
- **Real-time Priority Protection**: Audio thread gets highest priority, agents run at lower priority
- **Resource Isolation**: Agents can use intensive CPU/GPU without impacting audio
- **Scalable Processing**: Agent framework can utilize multiple cores/machines

**Threading Architecture**:
```
Real-time Audio Thread (Highest Priority)
├── Audio processing only
├── No memory allocation
├── No disk I/O
├── No network operations
└── Lock-free communication with other threads

UI Thread (High Priority)
├── User interface rendering
├── User input handling
├── Quick UI updates
└── Message passing to agent threads

Agent Framework Process/Thread Pool (Normal Priority)
├── Multi-Agent Orchestrator
├── DSP Agent (CPU intensive)
├── Sample Organization Agent (I/O intensive)
├── Composition Agent (LLM inference)
├── Mixing Agent (analysis & DSP)
└── Utility Agent (file operations)

Background Services (Low Priority)
├── Sample library scanning
├── ML model training
├── Project backup/sync
└── System maintenance
```

**Inter-Process Communication**:
- **Lock-free Queues**: Audio thread → Agent framework (commands)
- **Callback System**: Agent framework → Audio thread (results)
- **Shared Memory**: Large data transfer (audio buffers, sample data)
- **Message Passing**: UI ↔ Agents for user interactions

**Process Architecture Options**:

**Option A: Separate Process (Recommended)**
```
MAGDA Process:
├── Real-time Audio Thread
├── UI Thread
└── Audio Engine Components

MAGDA Agent Process:
├── Agent Orchestrator
├── Individual Agent Threads
├── ML Model Inference
└── External API Communications
```

**Benefits**:
- **Complete Isolation**: Agent crashes don't affect audio
- **Resource Control**: Can limit agent memory/CPU usage
- **Scalability**: Can run agents on different machines
- **Security**: Agent process can have restricted permissions

**Option B: Thread Pool (Alternative)**
```
Single MAGDA Process:
├── Real-time Audio Thread (isolated)
├── UI Thread
├── Agent Thread Pool
│   ├── Agent Worker 1
│   ├── Agent Worker 2
│   └── Agent Worker N
└── Background Thread Pool
```

**Benefits**:
- **Lower Latency**: No IPC overhead
- **Simpler Deployment**: Single executable
- **Easier Debugging**: All in one process

**Communication Protocols**:

**Audio → Agents (Commands)**:
```cpp
struct AgentCommand {
    CommandType type;
    Timestamp audio_time;
    TargetTrack track_id;
    Parameters params;
    CallbackID callback_id;
};
```

**Agents → Audio (Results)**:
```cpp
struct AgentResult {
    CallbackID callback_id;
    ResultType type;
    AudioData audio_data;     // For DSP results
    ProjectChanges changes;   // For project modifications
    ErrorInfo error;          // If operation failed
};
```

**Performance Guarantees**:
- **Audio Thread**: Never blocked by agent operations
- **Latency Bounds**: Agent responses within 100ms for UI updates
- **Resource Limits**: Agents limited to specific CPU/memory quotas
- **Graceful Degradation**: Audio continues even if agents crash/hang

**Agent Lifecycle Management**:
- **Lazy Loading**: Agents start on first use
- **Auto-restart**: Crashed agents automatically restart
- **Health Monitoring**: System monitors agent responsiveness
- **Resource Cleanup**: Automatic cleanup of stuck operations

### 9. Project Version Control & Creative Branching

#### Concept
**Philosophy**: Musicians need version control designed for creative workflows, not code - track creative evolution and enable fearless experimentation

#### Core Version Control Features

**Project Snapshots**:
- **Named Versions**: "Verse 1 Draft", "Final Mix v3", "Alternative Chorus Idea"
- **Automatic Snapshots**: Auto-save at significant points (before major changes, daily, before mixdown)
- **Semantic Versioning**: Major.Minor.Patch format adapted for music (Album.Song.Take)
- **Rich Metadata**: Timestamp, description, audio preview, visual thumbnail

**Creative Branching**:
- **Arrangement Branches**: Try different song structures without losing original
- **Mix Branches**: Experiment with different mixing approaches
- **Instrumentation Branches**: Test different instrument combinations
- **Tempo/Key Branches**: Explore different musical variations

**Asset Management**:
- **Smart Audio Handling**: Only store diffs for audio files, not full copies
- **Reference System**: Link to shared sample libraries instead of duplicating
- **Compression**: Lossless compression for project data, configurable for audio
- **Deduplication**: Automatic detection and sharing of identical audio segments

#### Integration with Track History System

**Unified Versioning**:
```
Project Level:
├── Major Version (Complete song iterations)
│   ├── Track Level History (bounce operations, plugin changes)
│   ├── Arrangement Changes (structure modifications)
│   └── Mix Snapshots (parameter automation states)

Version Tree Example:
Main Project
├── v1.0.0 - "Initial Demo"
├── v1.1.0 - "Added Drums"
├── v1.2.0 - "Vocal Recording"
├── v2.0.0 - "Complete Restructure"
│   ├── v2.1.0 - "Alternative Chorus" (branch)
│   └── v2.2.0 - "Extended Outro" (branch)
└── v3.0.0 - "Final Mix" (merged from v2.1.0)
```

**Cross-Level Integration**:
- **Track History → Project Versions**: Major track changes trigger version prompts
- **Project Versions → Track History**: Reverting versions restores all track states
- **Selective Reversion**: "Keep current drums but revert to old bassline"

#### Advanced Version Control Features

**Visual Comparison Tools**:
- **Waveform Diff**: Visual comparison of audio regions between versions
- **Arrangement Timeline**: Side-by-side comparison of project structures
- **Parameter Diff**: Show automation and plugin parameter changes
- **Spectrum Analysis**: Frequency content comparison between mix versions

**Intelligent Merging**:
- **Track-Level Merging**: Combine changes from different branches at track level
- **Automation Merging**: Blend automation curves from different versions
- **Arrangement Splicing**: Take verses from one version, chorus from another
- **Smart Conflict Resolution**: AI-assisted resolution of conflicting changes

**Collaboration Features**:
- **Branch Permissions**: Control who can modify which branches
- **Change Attribution**: Track which collaborator made which changes
- **Review System**: Approve/reject changes before merging
- **Comment System**: Attach notes and feedback to specific versions

#### Version Control Agent Integration

**Version Control Agent Capabilities**:
- **Smart Versioning**: Automatically suggest when to create versions
- **Branch Recommendations**: "This seems like a good time to branch for experimentation"
- **Merge Assistance**: Help resolve conflicts when combining versions
- **Change Analysis**: "This version has 23% more energy in the chorus"
- **Collaboration Coordination**: Manage multi-user workflows

**Integration with Other Agents**:
- **Composition Agent**: "Let's try this melody in a new branch"
- **Mixing Agent**: "Save current mix as branch before trying these changes"
- **DSP Agent**: "Version this before applying experimental processing"

#### Technical Implementation

**Storage Strategy**:
```
Project Repository Structure:
├── .magda/
│   ├── versions.db          # Version metadata and tree
│   ├── objects/             # Deduplicated project components
│   │   ├── audio/          # Audio file chunks and diffs
│   │   ├── automation/     # Parameter automation data
│   │   └── structure/      # Project structure snapshots
│   ├── refs/               # Branch and tag references
│   └── hooks/              # Custom version control triggers
├── current.project         # Working copy
└── assets/                 # Shared audio resources
```

**Efficient Storage**:
- **Delta Compression**: Only store differences between versions
- **Block-Level Deduplication**: Shared audio chunks across versions
- **Lazy Loading**: Load version data on-demand
- **Background Compression**: Optimize storage during idle time

**Performance Considerations**:
- **Fast Switching**: Version changes in <2 seconds
- **Background Operations**: Version creation doesn't block audio
- **Incremental Sync**: Only sync changed components
- **Local Cache**: Frequently accessed versions cached locally

#### User Experience Design

**Seamless Integration**:
- **Version Timeline**: Visual representation of project evolution
- **Quick Branch**: One-click branching for experimentation
- **Version Player**: Preview any version without switching
- **Smart Suggestions**: Context-aware versioning recommendations

**Workflow Integration**:
- **Save vs Version**: Clear distinction between saves and version creation
- **Undo vs Revert**: Undo for recent changes, revert for version switching
- **Branch Visualization**: Clear visual tree of project evolution
- **Progress Tracking**: See how project has evolved over time

### 10. Open Sequencer API (Reaper-style)

#### Concept
**Philosophy**: Completely open and programmable sequencer like Reaper's scripting system

**Core API Features**:
- **Full Project Access**: Read/write access to all project elements
- **Real-time Modification**: Change project during playback
- **Multi-language Support**: Python, JavaScript, Lua scripting
- **Custom Actions**: User-defined functions accessible via shortcuts
- **Automation API**: Programmatic control of all parameters

#### API Scope
**Project Level**:
```
- Project properties (tempo, time signature, sample rate)
- Track management (create, delete, reorder)
- Routing configuration
- Master section control
```

**Track Level**:
```
- Clip manipulation (create, move, resize, split)
- Plugin management (insert, remove, configure)
- Automation data access
- Track state management
```

**Real-time Control**:
```
- Transport control (play, stop, record, seek)
- Parameter automation
- MIDI generation and manipulation
- Audio routing changes
```

#### Integration with Multi-Agent System
**Agent-Script Bridge**:
- **Script Generation**: Agents can generate and execute API scripts
- **Workflow Automation**: Common tasks automated via agent-generated scripts
- **Custom Tools**: Users can create agent-assisted custom tools
- **Macro Recording**: Record user actions and convert to reusable scripts

**Example Use Cases**:
- Batch processing operations
- Custom mixing workflows
- Algorithmic composition tools
- Project analysis and statistics
- Custom export/import formats

### 11. MAGICA DSL (Domain-Specific Language)

#### Concept
**Philosophy**: A unified query/command language for both AI agents and power users.

The DSL uses a **Context-Free Grammar (CFG)** approach where:
- **LLM agents** generate DSL scripts (constrained by grammar, no hallucination)
- **Power users** type DSL commands directly for quick automation
- **Runtime** executes scripts with full context access (the LLM doesn't need to see all clips/tracks)

This separates "what to do" (DSL script) from "how to do it" (runtime with full DAW state).

#### Why CFG-Based DSL?

**Traditional LLM approach (context-heavy)**:
```
User: "Find clips shorter than 1 second"
→ LLM receives: [clip1: 0.5s, clip2: 2.3s, clip3: 0.8s, ...] (huge context)
→ LLM reasons about each clip
→ LLM outputs: specific clip IDs
```

**MAGICA DSL approach (context-free)**:
```
User: "Find clips shorter than 1 second"
→ LLM generates DSL: clips().filter(duration_lt=1.0).select()
→ Runtime executes with full Tracktion Edit access
→ Runtime returns: [clip1, clip3]
```

**Benefits**:
- LLM doesn't need DAW state context - just generates the query/script
- Grammar constrains output - no hallucinated commands
- Deterministic execution - same script always produces same result
- Scalable - works whether you have 10 clips or 10,000
- Learnable - users can read AI-generated scripts and learn the DSL

#### DSL Syntax Examples

**Query Operations**:
```
# Find and manipulate clips
clips().filter(track="Piano", duration_lt=1.0).delete()
clips().filter(muted=true).unmute()
clips().in_selection().quantize(grid=0.25)

# Track operations
tracks().filter(has_midi=true).arm()
tracks().filter(name_contains="Vocal").solo()

# Automation
automation().on(track="Bass", param="volume").smooth(window=0.1)
automation().on(track="Synth", param="filter_cutoff").randomize(amount=0.2)
```

**MIDI Operations**:
```
midi().in_clip("Piano_01").transpose(semitones=12)
midi().in_selection().quantize(grid=0.125, strength=0.8)
midi().filter(velocity_lt=20).delete()
midi().filter(note=60).set(velocity=100)
```

**Batch Operations**:
```
# Process all clips on drum tracks
tracks().filter(name_contains="Drum").clips().normalize()

# Find overlapping clips
clips().filter(overlapping=true).list()

# Export stems
tracks().filter(type="audio").each(t => t.export(path="/stems/{name}.wav"))
```

#### Dual-Use Interface

| Use Case | Input | Example |
|----------|-------|---------|
| **AI Agent** | Natural language → LLM → DSL | "Delete short clips" → `clips().filter(duration_lt=0.1).delete()` |
| **Power User** | Direct DSL typing | User types `clips().filter(duration_lt=0.1).delete()` |
| **Macro Recording** | Actions → DSL script | Record actions, export as reusable script |
| **Script Files** | Load .magda scripts | `magda run cleanup.magda` |

#### User Interface

**Command Palette** (Cmd+Shift+P):
- Quick one-liner execution
- Autocomplete with grammar awareness
- Recent command history

**Console Panel**:
- Full REPL with multi-line support
- Syntax highlighting
- Output display with clickable results
- History navigation

**Script Editor**:
- Multi-line script editing
- Syntax highlighting and validation
- Run selection or full script
- Save/load .magda script files

#### C++ Implementation

**Core Components**:

```cpp
// AST Types
struct Value {
    enum Kind { Number, String, Identifier, Bool, Function };
    Kind kind;
    double num;
    std::string str;
    bool boolean;
};

struct Arg { std::string name; Value value; };
struct Call { std::string name; std::vector<Arg> args; };
struct CallChain { std::vector<Call> calls; };

// Engine with method registration (no reflection in C++)
class DSLEngine {
    std::unordered_map<std::string, MethodHandler> methods;
public:
    void registerMethod(const std::string& name, MethodHandler handler);
    Result execute(const std::string& dslCode);
    std::string exportCFG();  // Export grammar for LLM tools
};

// DSL Implementation with Tracktion access
class MAGDADSL {
    tracktion::Edit& edit;  // Direct access to DAW state
public:
    void registerAll(DSLEngine& engine);

    // Query methods
    Result clips(const Args& args);
    Result tracks(const Args& args);
    Result midi(const Args& args);

    // Filter/transform methods
    Result filter(const Args& args);
    Result select(const Args& args);

    // Action methods
    Result delete_(const Args& args);
    Result quantize(const Args& args);
    Result transpose(const Args& args);
};
```

**Parser Options**:
- Hand-written recursive descent (simple, no dependencies)
- PEGTL (header-only PEG parser for C++)
- lexy (modern C++ parser combinator)

#### CFG Export for LLM Tools

The engine exports grammar in formats compatible with LLM structured generation:

```cpp
// Export for OpenAI CFG-constrained generation
std::string cfg = engine.exportCFG();

// Returns Lark-format grammar:
// start: call_chain
// call_chain: call ("." call)*
// call: IDENTIFIER "(" arguments? ")"
// arguments: argument ("," argument)*
// ...
```

This allows LLMs to generate only valid DSL code, eliminating syntax errors and hallucinated commands.

#### Integration with Voice Commands

Voice commands can generate DSL:
```
Voice: "Select all clips shorter than half a beat"
→ STT: "select all clips shorter than half a beat"
→ LLM: clips().filter(duration_lt=0.5).select()
→ Runtime: Executes with full context
→ UI: Updates selection
```

## Development Phases

### Phase 1: Foundation (Current)
- ✅ Multi-agent communication framework (MCP) with process isolation
- ✅ Tracktion Engine integration with abstraction layer
- 🔄 Basic hybrid track system
- 🔄 Arrangement mode UI foundation
- 🔄 Audio/UI abstraction interfaces design
- 🔄 Real-time audio thread isolation

### Phase 2: Core DAW Features
- Advanced bounce-in-place system
- Track history/super undo
- Plugin chain management
- Basic track containers
- Audio engine abstraction layer completion

### Phase 3: Scripting & DSP Integration
- DSP language implementation
- DSP agent development
- Basic sequencer API
- Script editor integration
- Sample organization agent and ML models
- Project version control system

### Phase 4: UI Polish & Advanced Features
- Bottom panel system with integrated prompt space
- Track container UI
- Advanced arrangement view
- History visualization
- Visual DSP editor
- Intelligent sample browser UI
- Multi-agent conversation interface

### Phase 5: Live Mode & Performance
- Session view implementation
- CPU mode switching
- Live performance features
- Real-time optimization

### Phase 6: Advanced API & Extensibility
- Complete open API implementation
- Third-party plugin development kit
- Agent marketplace/sharing system
- Advanced workflow automation

### Phase 7: Performance Migration (Future)
- Custom audio engine implementation
- Performance-critical component replacement
- Bare-metal optimizations
- Native UI implementation (optional)

## Technical Architecture Considerations

### Track History System
```
Track State = {
    content: [clips...],
    plugins: [effect_chain...],
    dsp_scripts: [custom_processors...],
    bounce_history: [
        {timestamp, operation, source_state, result_state},
        ...
    ],
    current_version: version_id
}
```

### DSP Language Architecture
```
DSP Pipeline:
Source Code → Parser → AST → Optimizer → Code Generator → Native Code
                                    ↓
                           Real-time Hot-swap System
```

### Mode-Specific Optimization
```
Engine Modes:
- ARRANGEMENT: {
    buffer_size: 1024,
    latency_compensation: true,
    bg_processing: true,
    dsp_optimization: "throughput"
  }
- LIVE: {
    buffer_size: 64,
    latency_compensation: false,
    bg_processing: false,
    dsp_optimization: "latency"
  }
```

### Track Container Hierarchy
```
Project
├── Master Track
├── Group Track A
│   ├── Audio Track 1 (with DSP script)
│   ├── Audio Track 2
│   └── Sub-Group B
│       ├── MIDI Track 1
│       └── MIDI Track 2 (with custom plugin)
└── Audio Track 3
```

### Open API Structure
```
MAGDAAPI
├── Project
│   ├── Tracks
│   ├── Transport
│   └── Routing
├── DSP
│   ├── ScriptManager
│   ├── CodeCompiler
│   └── EffectChain
├── SampleLibrary
│   ├── ContentAnalysis
│   ├── SimilarityEngine
│   └── SmartBrowser
└── Agents
    ├── DSPAgent
         ├── MixingAgent
     ├── CompositionAgent
     ├── SampleOrganizationAgent
     └── VersionControlAgent
```

## Multi-Agent System Integration

### Core Agents
1. **Orchestrator Agent**: Coordinates between all other agents
2. **DSP Agent**: Generates and optimizes DSP code
3. **Mixing Agent**: Handles mixing decisions and automation
4. **Composition Agent**: Assists with creative composition tasks
5. **Sample Organization Agent**: Continuously analyzes and organizes sample libraries
6. **Version Control Agent**: Manages project versioning and creative branching
7. **Voice Agent**: Processes voice commands and routes to appropriate agents
8. **Utility Agent**: Handles file operations, project management

### Agent Capabilities
- **Cross-Domain Knowledge**: Each agent can work with tracks, DSP, and API
- **Script Generation**: All agents can generate API scripts for their domains
- **Collaborative Workflow**: Agents can request help from other specialized agents
- **Learning System**: Agents learn from user interactions and preferences

## Future GitHub Issues Structure

This document should generate issues like:

**Core Features**:
- `[CORE] Implement hybrid track system`
- `[FEATURE] Advanced bounce-in-place with history`
- `[UI] Bottom panel context system`
- `[FEATURE] Track container implementation`

**Architecture & Migration**:
- `[ARCHITECTURE] Design audio engine abstraction interface`
- `[ARCHITECTURE] Implement UI framework abstraction layer`
- `[ARCHITECTURE] Multi-agent process isolation and communication`
- `[ARCHITECTURE] JSON-based project serialization with binary optimization`
- `[PERFORMANCE] Real-time audio thread protection`
- `[PERFORMANCE] Custom audio engine development`
- `[MIGRATION] Gradual component replacement strategy`

**DSP & Scripting**:
- `[DSP] Implement DSP language parser and compiler`
- `[AGENT] Create DSP code generation agent`
- `[API] Basic sequencer API implementation`
- `[UI] DSP script editor with syntax highlighting`

**Sample Management & ML**:
- `[ML] Implement audio content analysis models`
- `[AGENT] Create sample organization agent`
- `[FEATURE] Similarity-based sample clustering`
- `[UI] Intelligent sample browser with 2D similarity map`

**Version Control & Collaboration**:
- `[FEATURE] Project version control system design`
- `[AGENT] Version control agent with smart suggestions`
- `[UI] Visual version comparison and branching interface`
- `[STORAGE] Efficient delta compression for audio projects`

**Performance & Optimization**:
- `[OPTIMIZATION] Live vs Arrangement mode CPU strategies`
- `[DSP] Hot-swap system for real-time code updates`
- `[PERFORMANCE] Real-time DSP compilation optimization`

**UI & UX**:
- `[UI] Track history visualization`
- `[UI] Visual DSP programming interface`
- `[UX] Agent-assisted workflow design`
- `[UI] Multi-agent prompt space design and implementation`
- `[UX] Context-aware agent interaction patterns`

**Integration**:
- `[INTEGRATION] Agent-API bridge system`
- `[FEATURE] Multi-agent collaborative scripting`
- `[API] Third-party plugin development framework`
- `[INTEGRATION] Sample browser integration with composition workflow`
- `[INTEGRATION] Context-aware prompt routing and agent coordination`

**Voice Commands**:
- `[VOICE] Implement voice command recognition pipeline`
- `[VOICE] Wake word detection system`
- `[VOICE] Local speech-to-text integration (Whisper.cpp)`
- `[VOICE] Intent classification for DAW commands`
- `[VOICE] Voice feedback and confirmation system`
- `[AGENT] Voice agent for command routing`
- `[HARDWARE] Foot pedal and MIDI controller integration for voice activation`

## Success Metrics

### Technical Performance
- **Workflow Efficiency**: Faster MIDI-to-audio workflow than traditional DAWs
- **Creative Flexibility**: Non-destructive editing with complete history
- **Performance**: Arrangement mode handles large projects, Live mode achieves <5ms latency
- **DSP Performance**: Custom DSP scripts perform within 5% of hand-optimized C++ code
- **Migration Transparency**: Engine transitions invisible to users, zero downtime
- **Performance Scaling**: 2x performance improvement with each major engine iteration
- **Audio Thread Isolation**: Zero audio dropouts regardless of agent processing load
- **Agent Responsiveness**: Agent operations complete within 100ms for UI interactions

### User Experience
- **AI Integration**: Multi-agent system enhances rather than replaces human creativity
- **Learning Curve**: New users productive within 2 hours using agent assistance
- **Scripting Adoption**: 30% of users create custom scripts within first month
- **Agent Effectiveness**: 80% of agent-generated code accepted by users
- **Sample Discovery**: Users find relevant samples 3x faster than traditional browsing
- **Organization Efficiency**: Sample libraries automatically organized without user intervention
- **Prompt Efficiency**: Common tasks completed 5x faster via natural language than manual UI
- **Context Accuracy**: Agents correctly understand user intent 90% of the time without clarification
- **Version Control Adoption**: 70% of users regularly use branching for creative experimentation
- **Collaboration Efficiency**: Multi-user projects 3x more efficient than traditional file sharing
- **Voice Command Accuracy**: 95%+ recognition rate for common transport/track commands
- **Voice Latency**: Transport commands execute in <100ms from end of utterance
- **Hands-Free Sessions**: Users complete entire recording sessions without touching mouse/keyboard

### Ecosystem Growth
- **Third-party Development**: Active plugin/script marketplace
- **Community Engagement**: User-shared agents and workflows
- **Educational Impact**: Used in audio programming education

---

## Philosophy & Vision

**A DAW that thinks like a musician** - MAGDA combines the flexibility of open-source audio tools with the intelligence of AI assistance. The goal is not to replace human creativity but to amplify it through:

- **Intelligent Assistance**: AI agents that understand musical context
- **Unlimited Extensibility**: Every aspect programmable and customizable
- **Non-destructive Workflow**: Complete creative freedom with full undo history
- **Performance Flexibility**: Optimized for both studio work and live performance
- **Community-Driven**: Open platform for sharing tools and workflows

This vision represents the next evolution of digital audio workstations - where artificial intelligence serves human creativity, and every limitation can be overcome through intelligent automation and extensible programming.
