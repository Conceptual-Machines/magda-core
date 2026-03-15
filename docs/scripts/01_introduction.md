# MAGDA - Video Script #1: Introduction

> **Format**: Voiceover + screen recordings, polished & professional, accessible to a mixed audience
> **Target length**: 5–7 minutes
> **Structure**: On-camera intro → UI walkthrough → What is MAGDA / backstory → Closing

---

## PART 1: INTRO (~5 sec)

**[On camera (Mac camera)]**

Hi, I'm Luca, and I'm building MAGDA, an open-source DAW with integrated AI.

**[Cut to MAGDA UI on screen]**

MAGDA stands for Multi-Agent Digital Audio. It has all the features you'd expect from a DAW: audio and MIDI recording, sequencing, mixing, plugin hosting. But it also lets you use natural language to interact with the software and automate your workflow, similar to modern AI-powered IDEs. Let me walk you through the interface.

### Transport Bar

**[Mouse highlights the top transport panel]**

At the top, you have the transport bar. On the left, your main transport controls: home, previous, next, play, stop, record, loop, and back to arrangement. Right next to those, you have punch in/out toggles with editable punch start and end positions.

Moving along, there are display panels for time selection, loop region, playhead and edit cursor positions, all editable. Then you have tempo, time signature, metronome, snap, auto/manual grid quantize with a division control, and a CPU usage meter on the far right.

There's also a Resume button, which takes you back to the arrangement view when you're in session mode. More on that in a moment.

### Footer Bar

**[Mouse highlights the footer]**

At the very bottom, the footer bar. MAGDA has three main views: Arrangement, Session, and Mix. This is where you switch between them. The transport bar and footer are always visible regardless of which view you're in.

**[Quickly click through each view to show them, then back to Arrangement]**

Let me quickly show you all three. Arrangement is your linear timeline. Session is a clip launcher for live performance. And Mix is your mixer. We'll come back to Session and Mix in a moment. Let's start with Arrangement, since that's where you'll spend most of your time.

### Side Panels

**[Mouse moves to the left panel, briefly clicks through tabs]**

On the left side, you have the **Inspector**, which gives you details about whatever is currently selected. And the **AI Chat** panel, which is the agent interface. We'll come back to that one in detail.

**[Mouse moves to the right panel, briefly clicks through tabs]**

On the right, the **Plugin Browser** for loading VST and AU plugins, and the **Sample Browser** for previewing audio files.

All three panels, left, right, and bottom, are resizable and collapsible, so you can open up as much screen space as you need for the arrangement.

### The Arrangement

**[Mouse moves to the centre of the screen]**

The main area is your timeline. On the left, **track headers** with name, volume, pan, and mute/solo controls. Tracks in MAGDA are hybrid, so every track can host both audio and MIDI clips, so there's no need to choose between track types.

The centre area is where your clips live. You can move them, resize, cut, copy, paste. Above the tracks, the **timeline ruler** with your playhead, loop regions, and time markers.

#### Creating Tracks

**[Show each method as it's mentioned]**

There are a few ways to create new tracks. You can use the menu. But you can also drag a plugin from the plugin browser into the arrangement, and it'll create a new track with that synth loaded. Same with samples. Drag an audio file in and it creates a track with that sample ready to go.

#### The Bottom Panel

**[Mouse moves to the bottom panel, clicks through different states]**

The bottom panel is context-sensitive. It changes based on what you have selected.

**Select a track** and it shows the **track effects chain**. You can load plugins, reorder them, and see the full signal path.

#### Racks

**[Show a rack in the chain, expand it to show chains inside]**

You can also group devices into racks. A rack contains parallel chains, and each chain is its own signal path with its own volume, pan, mute, and solo. So if you want to do something like parallel compression, you set up two chains in a rack, one dry, one compressed, and they sum together at the output.

Racks can be nested too. A chain inside a rack can contain another rack. And each rack gets its own modulators and macros, separate from the track level.

#### Modulation

**[Show the mods and macros panels in the track chain area]**

Devices and racks have their own modulation system. You get 16 modulators and 16 macro knobs, organized in pages of eight.

**[Click on a modulator, show the editor panel]**

The modulators include a classic LFO with the standard waveforms, and a bezier curve editor for drawing custom shapes.

**[Show link mode - click the link button on a modulator, then click a plugin parameter]**

Routing is straightforward. You enter link mode on a modulator or macro, click the parameter you want to target, and adjust the amount. One modulator can drive multiple parameters, and one parameter can receive from multiple sources.
**[Show the visual indicators on the parameter slots]**

When modulation is active, you can see it directly on the parameter knobs. Animated indicators show the modulation range in real time.

**[Show the macros panel]**

Then there are the macro knobs. These let you map a single knob to multiple parameters across your effects chain. So one macro can control, say, filter cutoff on one plugin and reverb mix on another at the same time. They link to parameters the same way modulators do.

**[Click on a MIDI clip]**

**Select a MIDI clip** and you get the **piano roll**. Full note editing, velocity, pitchbend, and MIDI CC lanes. You can also switch to the **drum grid** view for rhythmic programming, a step sequencer style editor.

### Session View

**[Click Session in the footer bar, session view appears]**

The session view is MAGDA's live performance mode. Tracks are stacked horizontally with vertical clip slots that you can trigger independently. It's mostly complete. The main thing still missing is clip follow actions.

Remember that Resume button in the transport bar? That restores any track that was playing a session clip back to the arrangement.

### Mix View

**[Click Mix in the footer bar, mixer appears]**

And then there's the mixer. A full channel strip view with faders, pan, mute, solo, sends, and I/O routing.

---

### Deeper Look: The AI Chat Panel

**[Back to Arrangement, open the AI Chat tab in the left panel]**

Now let's come back to the AI chat. This is the agent interface. You type natural language commands and the AI translates them into actions. You can start simple - "create a track." Add a bit more detail - "create a track called Bass." Go further - "create a track called Bass with Serum 2." Or go all the way - "create a track called Bass with Serum 2 and add a funky bassline." It generates code in MAGDA's internal DSL, a domain-specific language, and executes it directly.

### The Menu Bar

**[Mouse moves to the top menu, opens Settings]**

Finally, the top menu gives you access to the main application functions. One worth highlighting is **Settings**, where you'll find your audio and MIDI device configuration, plugin folder management, and general preferences.

---

## PART 3: BACKSTORY & PHILOSOPHY (~1.5 min)

**[Voiceover - MAGDA UI visible or title card]**

I've been into electronic music production on and off for a long time. I started making beats in the late 90s, and I actually approached programming in the first place because of music production. So this project is somehow coming full circle.

Most of my career as a developer has been in Python and other higher-level languages like JavaScript. Recently, thanks to AI-assisted coding, I started venturing into C++, which I've always found intimidating, and honestly still do, but it's what enabled me to take on a project like this. On top of that, an open-source project like Tracktion Engine, which handles a huge amount of the low-level DAW complexity, made this solo project possible.

The name MAGDA stands for Multi-Agent Digital Audio. "Multi-Agent" because it hosts AI agents that help you work inside the DAW, but also because the software itself has been heavily built with the help of AI agents. It's agents all the way down, in a sense. The name actually came about in a pretty mundane way. Early on I had two folders, "agents" and "daw", and at some point I just merged them: agda, magda. It stuck.

I want to be clear about one thing: I'm not here to sell AI hype. AI in MAGDA is an automation and assistance tool, not a creativity replacement. It handles the tedious parts so you can focus on the music. The creative decisions stay with you.

**[Brief text overlay or graphic: "Intelligence as automation, not replacement"]**

The project is fully open source under GPL v3. The core will always be free. Down the road, I plan to offer a pro tier with additional instruments, effects, and services. But the DAW itself stays open.

---

## PART 4: CLOSING (~30 sec)

**[Voiceover - MAGDA UI visible]**

We're close to version 0.1.0, the first public release, with most of the core features already in place. Beyond that, the roadmap goes through five milestones, all the way to Faust DSP integration, a full session view, and an extensible scripting API.

If you're interested, the code is on GitHub, and I'll be sharing more of this journey as we go. If you're a developer, a musician, or just someone who thinks creative tools should be more open - I'd love to hear from you.

Thanks for watching.

**[End card with GitHub link, subscribe prompt]**

---

## PRODUCTION NOTES

- **B-roll ideas**: Code scrolling in IDE, the manifesto text, GitHub repo
- **Text overlays**: Key phrases from the manifesto ("Software should adapt to the user", "Intelligence as automation, not replacement", "A system, not a product")
- **Music**: Subtle, tasteful background - ideally something made in MAGDA if possible, or a simple ambient bed
- **Pacing**: Let the UI walkthrough breathe. Don't rush through panels. Each context switch in the bottom panel is a mini "aha moment" - give it space.
- **Estimated timing**: Part 1 (~5s) + Part 2 (~3min) + Part 3 (~90s) + Part 4 (~30s) = ~5 minutes
