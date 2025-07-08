# Magica DAW - Terminology Glossary

This comprehensive glossary defines all terminology used throughout the Magica DAW project to ensure consistent communication during development.

---

## Core DAW Concepts

### **DAW (Digital Audio Workstation)**
- The complete software application for music production
- Magica is a DAW built with JUCE framework

### **Project**
- A complete music production session
- Contains tracks, clips, settings, and arrangement

### **Session**
- Active working state of a project
- Includes playback state, selections, and temporary settings

---

## UI Components & Layout

### **MainWindow**
- Top-level application window
- Contains all panels and views
- Manages window state and menu bar

### **MainView**
- Primary content area of the application
- Contains track headers, timeline, and track content
- Handles resize interactions between panels

### **Track Headers Panel**
- Left panel showing track names and controls
- Contains track selection, mute/solo buttons
- Resizable via drag handle

### **Track Content Area**
- Main central area showing track lanes
- Contains audio/MIDI clips and automation
- Synchronized with timeline for horizontal scrolling

### **Timeline Component**
- Top horizontal component showing time reference
- **NOT the same as track content area**
- Contains three zones: Sections Area, Zoom Zone, Playhead Zone

---

## Timeline Component Zones

### **Sections Area**
- Top 40% of timeline component
- Contains colored section blocks (verse, chorus, etc.)
- Editable when unlocked, part of zoom zone when locked

### **Zoom Zone**
- Middle area between sections area and playhead zone
- Dedicated space for vertical drag zoom gestures
- Empty space for interaction purposes

### **Playhead Zone**
- Bottom 60 pixels of timeline component
- Contains time markers (numbers) and ticks
- Click to set playhead position

---

## Timeline Elements

### **Sections**
- Colored rectangular blocks representing song structure
- Have start time, end time, name, and color
- Can be selected, moved, and resized

### **Ticks**
- Short vertical lines showing time divisions
- 13 pixels tall, drawn at bottom of playhead zone
- Visual time reference markers

### **Time Markers**
- Text labels showing time (e.g., "0:00", "1:30")
- Positioned above ticks in playhead zone
- Readable time reference

### **Playhead**
- Vertical blue line showing current playback position
- Extends through entire timeline component height
- Set by clicking in playhead zone

---

## Panel Components

### **LeftPanel**
- Contains track headers and controls
- Resizable panel on left side of main view

### **RightPanel**
- Contains additional tools and controls
- Expandable panel on right side

### **BottomPanel**
- Contains transport controls and status information
- Fixed height panel at bottom of main view

### **TopPanel / HeaderComponent**
- Contains menu, title, and global controls
- Fixed height panel at top of main view

---

## Audio/MIDI Concepts

### **Track**
- Individual channel for audio or MIDI content
- Has volume, pan, mute/solo controls
- Contains clips and automation

### **Clip**
- Individual piece of audio or MIDI content
- Has start time, duration, and content
- Can be moved, resized, and edited

### **Automation**
- Time-based parameter changes
- Drawn as curves/lines over time
- Controls volume, pan, effects, etc.

---

## Interface Components

### **Clip Interface**
- Handles clip-related operations
- Part of DAW mode interface system

### **Mixer Interface**
- Handles mixing operations
- Volume, pan, effects routing

### **Transport Interface**
- Handles playback control
- Play, stop, record, loop functions

### **DAW Mode Interface**
- Main interface for DAW functionality
- Coordinates all DAW-related operations

---

## Interaction States

### **Arrangement Locked/Unlocked**
- **Locked**: Sections cannot be edited, sections area becomes zoom zone
- **Unlocked**: Sections can be selected, moved, and resized

### **Zoom Mode**
- **Active**: User dragging vertically, cursor shows resize arrows
- **Inactive**: Normal mouse interactions

### **Playback State**
- **Playing**: Audio/MIDI playback active
- **Stopped**: No playback
- **Recording**: Capturing audio/MIDI input

---

## Visual Components

### **SvgButton**
- Custom button component using SVG icons
- Provides consistent iconography throughout UI

### **Mode Switcher**
- Component for switching between different DAW modes
- Handles mode state and visual feedback

### **Themes**
- **DarkTheme**: Primary dark color scheme
- **FontManager**: Manages font loading and selection
- **Colors**: Consistent color palette definition

---

## Engine Components

### **Tracktion Engine Wrapper**
- Wraps Tracktion Engine for DAW functionality
- Handles audio processing and routing

### **Command System**
- Handles user actions and undo/redo
- Command pattern implementation

### **Audio Engine**
- Core audio processing system
- Handles real-time audio I/O

---

## Agent System

### **Agent Interface**
- Interface for AI/automation agents
- Handles agent communication and coordination

### **Agent Manager**
- Manages multiple agents
- Coordinates agent interactions

### **LLM Integration**
- Large Language Model integration
- Provides AI-assisted music production

---

## File Structure Terms

### **Assets**
- Static resources (fonts, icons, images)
- Embedded in binary for distribution

### **Themes Directory**
- Contains theme-related code
- Font management and color schemes

### **Components Directory**
- UI component implementations
- Reusable interface elements

### **Interfaces Directory**
- Abstract interfaces for DAW functionality
- Separation of concerns architecture

---

## Build System

### **CMake**
- Build system configuration
- Handles dependencies and compilation

### **JUCE Framework**
- C++ application framework
- Provides GUI, audio, and utility classes

### **Third Party**
- External libraries and dependencies
- Tracktion Engine, audio libraries

---

## Common Abbreviations

- **UI**: User Interface
- **DAW**: Digital Audio Workstation
- **MIDI**: Musical Instrument Digital Interface
- **VST**: Virtual Studio Technology
- **LLM**: Large Language Model
- **AI**: Artificial Intelligence
- **I/O**: Input/Output
- **BPM**: Beats Per Minute

---

## Spatial Terminology

### **Horizontal**
- Time-based dimension
- Left-to-right represents time progression
- Scroll horizontally to navigate through time

### **Vertical**
- Track-based dimension
- Top-to-bottom represents different tracks
- Scroll vertically to see more tracks

### **Zoom**
- **Horizontal Zoom**: Change time scale (zoom in/out on timeline)
- **Vertical Zoom**: Change track height (make tracks taller/shorter)

---

## Development Terms

### **Zone**
- Interaction area with specific behavior
- Mutually exclusive functionality

### **Area**
- Visual region of the interface
- May contain multiple zones

### **Panel**
- Distinct UI section with specific purpose
- Can be resizable, collapsible, or fixed

### **Component**
- JUCE UI element class
- Self-contained piece of interface

---

## Common Confusions to Avoid

1. **"Timeline"** → Specify **"Timeline Component"** (UI element) vs **"Time axis"** (concept)
2. **"Arrangement"** → Use **"Sections"** (timeline blocks) vs **"Track Content"** (main area)
3. **"Tracks"** → In Timeline Component = sections, in Track Content = actual audio/MIDI tracks
4. **"Panel"** vs **"Component"** → Panel = larger UI section, Component = individual UI element
5. **"Engine"** → Specify **"Audio Engine"** (processing) vs **"Tracktion Engine"** (framework)
6. **"Interface"** → Specify **"UI Interface"** (visual) vs **"Code Interface"** (abstract class)

---

## Code Organization

### **Namespaces**
- `magica`: Main project namespace
- Component-specific namespaces for organization

### **File Extensions**
- `.cpp`: C++ implementation files
- `.hpp`: C++ header files
- `.h`: C header files (legacy/third-party)
- `.md`: Markdown documentation files

---

## Version Control

### **Branches**
- Feature branches for development
- Main branch for stable releases

### **Commits**
- Atomic changes with descriptive messages
- Follow conventional commit format

---

*This glossary should be updated as the project evolves and new terminology is introduced.* 