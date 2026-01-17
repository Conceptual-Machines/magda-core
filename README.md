# 🔮 Magica — Multi-Agent Generative Interface for Creative Audio

**Magica** (**M**ulti-**A**gent **G**enerative **I**nterface for **C**reative **A**udio) is an experimental AI-driven Digital Audio Workstation built from the ground up for multi-agent collaboration. It combines a modern DAW engine with a comprehensive API, enabling intelligent agents to compose, arrange, and manipulate music in real-time.

## 🎵 What Makes Magica Special

- **AI-First Design**: Built specifically for AI agent collaboration
- **Universal API**: Agents can be written in any language (Python, Go, JavaScript, etc.)
- **Natural Language Interface**: Talk to your DAW in plain English
- **Real-time Collaboration**: Multiple agents working together simultaneously
- **Modern Architecture**: Clean separation between DAW engine, API, and UI

## 🏗️ Architecture

```
🤖 AI Agents (Python/Go/JS)
       ↓ MCP
🔮 Magica System
   ├── 🎵 DAW Domain (C++)
   │   ├── Audio Engine (+ aideas-core)
   │   ├── UI Components
   │   └── DAW Core Logic
   └── 🤖 MCP Domain (C++)
       ├── Communication Server
       ├── Protocol Buffers
       └── Agent Management
```

## 🚀 Quick Start

### 1. Start Magica DAW
```bash
# Build and run the DAW
make build
./build/magica_daw_app

# Output:
# ✓ Audio engine initialized
# 🚀 Magica DAW communication server ready
# 🤖 Ready for AI agents to connect!
# 🔮 Magica DAW is ready!
```

### 2. Connect AI Agents

**Utility Agent (Go) - High-performance MIDI processing:**
```bash
go run mcp/agents/utility_agent.go --daw localhost:50051 --action listen

# Output:
# 🤖 Starting Utility Agent...
# ✓ Registered as agent: util_7f8a9b2c
# 🔗 Connected to Magica DAW at localhost:50051
# 👂 Listening for events... (Press Ctrl+C to exit)
```

**Orchestrator Agent (Python) - Natural language interface:**
```bash
python mcp/agents/orchestrator.py --daw localhost:50051

# Output:
# 🔗 Connecting to Magica DAW at localhost:50051
# ✅ Registered as orchestrator: orch_001
# 🎹 Interactive mode started. Type 'quit' to exit.
#
# 🔮 Magica>
```

### 3. Use Natural Language Commands

```bash
🔮 Magica> Clean up the piano recording

# 📊 Session context: 3 tracks, 120 BPM
# 🤖 Found 2 connected agents:
#    • UtilityAgent (utility): deduplicate_notes, remove_short_notes, quantize_notes, cleanup_recording
#    • OrchestratorAgent (orchestrator): natural_language_processing, agent_coordination
# 🎯 Intent: Clean up MIDI recording (complexity: simple)
# 🎯 Executing workflow with 1 steps
# 📋 Step 1: cleanup_recording via utility agent
# ✅ Step 1 sent to UtilityAgent

# Meanwhile, in Utility Agent terminal:
# 🧹 Deduplicating notes in clip: clip_piano_take_1
#    📝 Original notes: 1247
#    🗑️  Removed duplicates: 89
#    ✅ Deduplication complete: 1158 notes remaining
# ✂️  Removing notes shorter than 0.050 beats
#    🗑️  Removed short notes: 23
#    ✅ Short note removal complete: 1135 notes remaining
# 📐 Quantizing clip to 0.250 beat grid
#    ✅ Quantization complete
# 🎉 Recording cleanup complete for clip: clip_piano_take_1
```

## 🎛️ Agent Communication

Magica exposes a comprehensive API covering all DAW functionality:

### **Transport Control**
```python
# Play/Stop/Record
await client.Play(PlayRequest())
await client.Stop(StopRequest())
await client.SetTempo(SetTempoRequest(tempo=140.0))
```

### **Track Management**
```python
# Create tracks
track_resp = await client.CreateTrack(CreateTrackRequest(
    name="Piano",
    type=TrackType.MIDI
))

# Mute/Solo/Arm
await client.MuteTrack(MuteTrackRequest(
    track_id=track_resp.track_id,
    muted=True
))
```

### **MIDI Operations**
```python
# Create MIDI clip with notes
notes = [
    MidiNote(pitch=60, velocity=100, start_time=0.0, duration=0.5),
    MidiNote(pitch=64, velocity=100, start_time=0.5, duration=0.5),
    MidiNote(pitch=67, velocity=100, start_time=1.0, duration=0.5),
]

clip_resp = await client.CreateMidiClip(CreateMidiClipRequest(
    track_id=track_id,
    start_time=0.0,
    length=4.0,
    initial_notes=notes
))
```

### **Agent Management**
```python
# Register agent
register_resp = await client.RegisterAgent(RegisterAgentRequest(
    name="MyComposerAgent",
    type="composer",
    capabilities=["melody_generation", "chord_progressions"]
))

# Send messages between agents
await client.SendMessageToAgent(SendMessageRequest(
    target_agent_id="other_agent_id",
    message=json.dumps({"task": "harmonize", "clip_id": "clip_123"})
))
```

## 🤖 Agent Examples

### **Utility Agent (Go)**
High-performance MIDI processing for:
- Note deduplication
- Short note removal
- Quantization
- Velocity normalization
- Recording cleanup

```bash
# Direct clip processing
cd mcp/agents
go run utility_agent.go --daw localhost:50051 --action cleanup --clip clip_123

# Event-driven processing
go run utility_agent.go --daw localhost:50051 --action listen
```

### **Orchestrator Agent (Python)**
Natural language interface with LLM integration:
- Intent analysis
- Workflow coordination
- Agent routing
- Session management

```bash
# Interactive mode
cd mcp/agents
python orchestrator.py --daw localhost:50051 --openai-key sk-...

# Single command
python orchestrator.py --daw localhost:50051 --request "Add a jazz melody"
```

### **Melody Agent (Python)**
AI composition capabilities:
- Melody generation
- Chord progressions
- Harmonization
- Style transfer

### **Percussion Agent (Python)**
Rhythm generation:
- Drum patterns
- Groove templates
- Fill generation
- Style adaptation

## 🎯 Real-World Usage Scenarios

### **Scenario 1: Post-Recording Cleanup**
```
User: "I just recorded this piano part but it has duplicates and short notes"

Workflow:
1. Orchestrator (mcp/agents/orchestrator.py) analyzes intent
2. Routes to Utility Agent via MCP server
3. Utility Agent (mcp/agents/utility_agent.go) processes 1000+ notes in <20ms
4. DAW engine updates with clean MIDI
5. User can immediately continue editing in the DAW UI
```

### **Scenario 2: Creative Composition**
```
User: "Add a walking bass line that follows the chord progression"

Workflow:
1. Orchestrator analyzes current session
2. Detects chord progression from existing tracks
3. Routes to Bass Agent with musical context
4. Bass Agent generates walking bass line
5. New MIDI track created with bass line
6. User can adjust and refine
```

### **Scenario 3: Full Arrangement**
```
User: "Turn this 4-bar loop into a full song with drums and strings"

Workflow:
1. Orchestrator creates complex workflow
2. Structure Agent analyzes loop and creates song form
3. Percussion Agent generates drum patterns for each section
4. String Agent creates orchestral arrangement
5. All parts coordinated and timed correctly
6. Full arrangement ready for mixing
```

## 🔧 Building from Source

### **Prerequisites**
- C++20 compiler (GCC 10+, Clang 12+)
- CMake 3.20+
- JUCE framework
- Protocol Buffers

### **Quick Setup**
```bash
# Clone repository with submodules
git clone --recursive https://github.com/Conceptual-Machines/MAGICA.git
cd magica

# Run automated setup (handles submodules, dependencies, pre-commit)
make setup

# Build and run
make debug
make run
```

### **Manual Build Steps**
```bash
# Clone repository (if not done with --recursive)
git clone https://github.com/yourusername/magica.git
cd magica

# Initialize submodules (required for Tracktion Engine + JUCE)
git submodule update --init --recursive

# Build entire system
make debug

# Run the DAW application
make run

# Run tests
make test

# Check code quality
make quality
```

### **Development Quick Start**
```bash
# Set up development environment (if not done with make setup)
pip install pre-commit
pre-commit install

# Development workflow
make format         # Format code
make quality        # Run all quality checks
make test           # Run tests
make debug          # Build project
```

### **Dependencies**
```cmake
# Core Dependencies (as Git submodules):
# - Tracktion Engine (audio engine) - magica-minimal branch
# - JUCE 8.0.10 (GUI framework)

# Automatically fetched via CMake FetchContent:
# - nlohmann/json v3.11.3
# - Catch2 v3.4.0 (testing)
```

## 🔧 Development & Code Quality

Magica uses automated code quality tools to ensure consistent, maintainable, and high-quality C++ code throughout the project.

### **Code Quality Tools**

Our development workflow includes:
- **clang-format**: Automatic code formatting with LLVM style
- **clang-tidy**: Static analysis and linting (with graceful fallback)
- **pre-commit hooks**: Automated quality checks before commits
- **CI/CD integration**: Quality checks on all pull requests

### **Available Make Targets**

```bash
# 🔧 Build & Development
make build          # Build entire system
make debug          # Debug build with symbols
make test           # Run all tests
make clean          # Clean build artifacts

# 🎨 Code Quality
make format         # Format all code with clang-format
make check-format   # Check formatting without changes
make lint           # Run static analysis (sample of files)
make lint-all       # Run comprehensive static analysis
make quality        # Run all quality checks
make fix            # Fix common issues and format code

# 📊 Other
make help           # Show all available targets
```

### **Code Style Guidelines**

We follow consistent C++ conventions:
- **Line Length**: 100 characters maximum
- **Indentation**: 4 spaces (no tabs)
- **Naming**: CamelCase for classes, camelBack for functions/variables
- **Pointers**: Left-aligned (`int* ptr`, `int& ref`)
- **Braces**: Attached style (`{` on same line)

### **Pre-commit Hooks**

Set up automatic quality checks:
```bash
# Install pre-commit (if not already installed)
pip install pre-commit

# Install hooks
pre-commit install

# Run hooks on all files
pre-commit run --all-files
```

### **Development Workflow**

1. **Make changes** to C++ code
2. **Format automatically** with `make format`
3. **Check quality** with `make quality`
4. **Run tests** with `make test`
5. **Commit changes** (pre-commit hooks run automatically)
6. **Push to GitHub** (CI runs full quality checks)

### **CI/CD Integration**

Every pull request automatically runs:
- ✅ Code formatting checks
- ✅ Static analysis with clang-tidy
- ✅ Build verification across platforms
- ✅ Complete test suite execution
- ✅ Documentation generation

### **Code Quality Notes**

- **clang-tidy compatibility**: Some versions may have issues; the system gracefully falls back to formatting-only mode
- **Comprehensive coverage**: Quality checks cover all C++ files in `daw/`, `agents/`, and `tests/`
- **Developer-friendly**: Tools are configured to be helpful, not obstructive
- **Documentation**: Full style guide available in [`docs/code_style.md`](docs/code_style.md)

## 🎼 Why Magica?

**Traditional DAWs** were designed for human workflows. **Magica** is designed for **human-AI collaboration**:

- **Natural Language Interface**: Talk to your DAW like a collaborator
- **Real-time AI Assistance**: Agents continuously help with composition and arrangement
- **Unlimited Extensibility**: Create custom agents for any musical task
- **Language Agnostic**: Write agents in Python for AI, Go for performance, JavaScript for web integration
- **Modern Architecture**: Built with today's AI and cloud technologies

## 🤝 Contributing

Magica is an experimental project exploring the future of AI-driven music production. Contributions welcome!

### **Getting Started**

1. **Fork and clone** the repository:
   ```bash
   git clone --recursive https://github.com/Conceptual-Machines/MAGICA.git
   cd magica
   ```

2. **Run automated setup**:
   ```bash
   make setup
   ```
   This will:
   - ✅ Initialize git submodules (Tracktion Engine + JUCE)
   - ✅ Check for required dependencies (CMake, C/C++ compilers)
   - ✅ Install pre-commit hooks automatically
   - ✅ Create build directories

3. **Build and test**:
   ```bash
   make debug      # Build project
   make test       # Run tests
   make quality    # Check code quality
   ```

4. **Follow our development workflow**:
   - Make your changes
   - Run `make quality` to check code quality
   - Run `make test` to ensure tests pass
   - Commit (pre-commit hooks run automatically)
   - Submit a pull request

### **Areas for Contribution**
- **DAW Domain (`daw/`)**:
  - aideas-core integration for advanced audio processing
  - Modern UI components and JUCE interface improvements
  - Real-time audio engine enhancements
- **MCP Domain (`mcp/`)**:
  - New agent types (mastering, sound design, music theory)
  - Language bindings for more programming languages
  - Agent coordination and workflow improvements
- **Cross-Domain**:
  - Documentation and tutorials
  - Performance optimization
  - Testing and CI/CD
  - Code quality improvements

### **Code Quality Standards**

All contributions must pass our automated quality checks:
- ✅ Code formatting (clang-format)
- ✅ Static analysis (clang-tidy)
- ✅ Build verification
- ✅ Test suite execution
- ✅ Documentation updates

See [`docs/code_style.md`](docs/code_style.md) for detailed guidelines.

## 🔧 Troubleshooting

### **Git Submodules Issues**

If you encounter build errors related to missing files, the issue is likely uninitialized submodules:

```bash
# Error: tracktion_engine headers not found
# Error: JUCE modules missing
# Error: No such file or directory: 'third_party/tracktion_engine/...'
```

**Solution:**
```bash
# Re-run setup to fix submodules
make setup

# Or manually initialize submodules
git submodule update --init --recursive

# If submodules are corrupted, reset them
git submodule deinit --all
git submodule update --init --recursive
```

### **Compiler Issues**

**Missing C/C++ compilers:**
```bash
# macOS - Install Xcode Command Line Tools
xcode-select --install

# Ubuntu/Debian
sudo apt-get install build-essential

# Fedora/RHEL
sudo dnf install gcc gcc-c++ make
```

**clang-tidy compatibility issues:**
- The system gracefully falls back to formatting-only mode
- See [`docs/code_style.md`](docs/code_style.md) for alternative installation methods

### **Build Issues**

```bash
# Clean build if encountering CMake cache issues
make clean
make setup
make debug

# Force regenerate build files
rm -rf cmake-build-debug
make debug
```

## 📄 License

MIT License - see [LICENSE](LICENSE) file for details.

---

**Magica** - Where AI and music creation meet. 🎵🤖

---

## 🏗️ Project Structure

Magica follows a **product-based** architecture with two main domains:

```
magica/
├── 🎵 daw/                    # DAW Product Domain
│   ├── Audio engine + aideas-core integration
│   ├── User interface components
│   ├── DAW core logic and interfaces
│   └── Main application (magica_daw_app)
│
└── 🤖 mcp/                    # Multi-Agent Communication Domain
    ├── MCP server implementation
    ├── Protocol buffer definitions
    ├── Example agents (orchestrator.py, utility_agent.go)
    └── Agent management
```

**Key Benefits:**
- **Clear Boundaries**: Each domain has specific responsibilities
- **Independent Development**: DAW and MCP can evolve separately
- **Easy Integration**: aideas-core scope is clearly defined
- **Scalable**: Easy to add new domains (plugins, cloud, etc.)

## 🧠 Key Components

### 🎵 DAW Domain (`daw/`)
The complete digital audio workstation:
- **Audio Engine**: Real-time audio processing (+ future aideas-core integration)
- **UI Framework**: Modern interface built with JUCE
- **Core Interfaces**: Track, clip, mixer, transport operations
- **Command System**: Unified command pattern for all DAW operations

### 🤖 MCP Domain (`mcp/`)
The multi-agent communication system:
- **Communication Server**: High-performance agent communication
- **Protocol Buffers**: Strongly-typed API definitions
- **Agent Management**: Registration, routing, and coordination
- **Example Agents**: Orchestrator (Python), Utility (Go)

---

## 🧪 Status

Magica is in early research and prototyping. It is **not yet ready for production use**, but contributors and feedback are welcome as we design the core protocols and data model.

See [VISION.md](VISION.md) for the detailed architecture specification and roadmap.

---

## 📦 Example Command

```json
{
  "command": "addMidiClip",
  "trackId": "track_1",
  "start": 4.0,
  "length": 2.0,
  "notes": [
    { "note": 60, "velocity": 100, "start": 0.0, "duration": 0.5 }
  ]
}
