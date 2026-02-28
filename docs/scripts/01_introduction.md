# MAGDA — Video Script #1: Introduction

> **Format**: Voiceover + screen recordings, polished & professional, accessible to a mixed audience
> **Target length**: 5–7 minutes
> **Structure**: Personal intro → Motivation → What is MAGDA → UI walkthrough

---

## PART 1: WHO I AM (~1 min)

**[Voiceover — screen shows MAGDA logo or title card, then transitions to slow pan of the UI]**

Hi, I'm Luca. I'm a software engineer based in the UK, and I'm building MAGDA — a new open-source DAW with AI built into its foundation.

I want to start by telling you a little about how I got here, because this project didn't come out of nowhere. It came out of about twenty years of circling the same idea from different angles.

I got into programming through music. I was about nineteen, making electronic music with Cubase, Reaktor, some of the first virtual synths — and the more time I spent inside those tools, the more I wanted to understand how they actually worked.

I've been mostly working as a Python developer throughout my career. But that original connection — music and code, the feeling that these two things belong together — never went away.

---

## PART 2: WHY I BUILT MAGDA (~1.5 min)

**[Voiceover — screen recordings of early prototypes, code editor, maybe the Musical AIdeas plugin UI]**

I'd always found C++ intimidating. But over the past couple of years, AI-assisted coding tools have made it much more realistic for someone like me to work in it. The other thing that made this possible is Tracktion Engine — it's an open-source audio engine that handles a huge amount of the low-level DAW complexity. Without it, I honestly doubt I could have gotten this far.

But the idea itself came from a simple question: what if the kind of AI assistance I was using in my code editor existed inside a DAW?

I started with a JUCE plugin called Musical AIdeas — it helped write chord progressions and melodies using AI. Interesting concept, but the results were inconsistent, the latency was over a minute, and it didn't really capture what I was going for.

Then I tried a Reaper extension, more focused on the automation side — creating tracks, editing, functional music generation. Things like "write a progression in E minor." That worked much better. But Reaper's extension ecosystem is limited, and I kept hitting walls.

So I thought — why not build the whole thing? A DAW where AI isn't bolted on as a plugin, but part of the architecture from day one.

That was less than three months ago. And here we are.

**[Beat. Transition to MAGDA UI on screen.]**

---

## PART 3: WHAT IS MAGDA (~1 min)

**[Screen recording — MAGDA open, arrangement view visible]**

MAGDA stands for Multi-Agent Digital Audio. "Multi-Agent" because it hosts AI agents that help you work inside the DAW — but also because the software itself has been heavily built with the help of AI agents. It's agents all the way down, in a sense.

The name actually came about in a pretty mundane way — early on I had two folders, "agents" and "daw", and at some point I just merged them: agda, magda. It stuck.

It's a digital audio workstation — like Ableton, Logic, or Bitwig — but built from the ground up with AI-driven automation at its core.

I want to be very clear about something: this is not an AI music generator. I'm not building the next Suno or Udio. The core principle is that AI serves as an automation tool, not a creativity replacement. Creativity stays human. The AI is here to remove friction — to handle the tedious parts so you can focus on the music.

**[Brief text overlay or graphic: "Intelligence as automation, not replacement"]**

The project is fully open source under GPL v3. The core will always be free. Down the road, I plan to offer a pro tier with additional instruments, effects, and services — but the DAW itself stays open.

---

## PART 4: THE UI WALKTHROUGH (~3 min)

**[Full screen recording of MAGDA, mouse movements deliberate and clear]**

Let me walk you through the interface. If you've used Ableton or Bitwig, a lot of this will feel familiar — but there are some key differences.

MAGDA has three main views: Arrangement, Session, and Mix. You switch between them from the footer bar at the bottom of the screen. The transport bar at the top and the footer bar at the bottom are always visible regardless of which view you're in — they're the constant frame around everything else.

Let's start with Arrangement, since that's where you'll spend most of your time.

### Transport Bar

**[Mouse highlights the top transport panel]**

At the top, you have the transport bar. Play, stop, record — the essentials. You also have go-to-start and go-to-end buttons, punch in/out controls, a time selection panel, and panels for the playhead and edit cursor positions. There's also a Resume button, which takes you back to the arrangement view when you're in session mode — more on that in a moment.

### Footer Bar

**[Mouse highlights the footer]**

And at the very bottom, the footer bar. This is where you switch between the three main views — Arrangement, Session, and Mix.

Now let's look at each view.

### Arrangement View

**[Arrangement view visible]**

#### The Timeline

**[Mouse moves to the centre of the screen]**

The main area is your timeline — this is where you compose and arrange your tracks linearly, the traditional DAW workflow.

On the left, you have the **track headers**. Each track has its name, volume, pan, and mute/solo controls. Tracks in MAGDA are hybrid — every track can host both audio and MIDI clips, so there's no need to choose between track types.

The centre area is where your clips live. Audio clips, MIDI clips — you can move them, resize them, cut, copy, paste. Standard arrangement operations.

Above the tracks, you have the **timeline ruler** with your playhead, loop regions, and time markers.

#### Right Panel

**[Mouse moves to the right panel, clicks through tabs]**

On the right side, you have the **Plugin Browser** — find and load VST and AU plugins from your collection. And the **Sample Browser** — browse and preview your audio samples and files.

#### Creating Tracks

**[Show each method as it's mentioned]**

There are a few ways to create new tracks. You can use the menu — straightforward. But you can also drag and drop a plugin from the plugin browser into the arrangement, and it'll create a new track with that synth loaded. Same with samples — drag an audio file from the sample browser into the arrangement and it creates a track with that sample ready to go.

#### Left Panel

**[Mouse moves to the left panel, clicks through tabs]**

On the left side, you have two tabs.

The **Inspector** gives you detailed information about whatever is currently selected — whether that's a track or a clip. It adapts to context, so you always have the relevant properties at hand.

Then there's the **AI Chat** panel. This is the agent interface. You type natural language commands and the AI translates them into actions. "Create a new track called Drums." "Add a reverb to the vocal track." "Write a bass line in D minor." It generates code in MAGDA's internal DSL — a domain-specific language — and executes it directly. No copy-pasting, no leaving the app.

#### Bottom Panel

**[Mouse moves to the bottom panel, clicks through different states]**

The bottom panel is context-sensitive. It changes based on what you have selected.

**Select a track** — and it shows the **track effects chain**. You can load plugins, reorder them, and see the full signal path.

**[Click on a MIDI clip]**

**Select a MIDI clip** — and you get the **piano roll**. Full note editing, velocity, and this is where pitchbend and MIDI CC lanes will live as well. You can also switch to the **drum grid** view for rhythmic programming — a step sequencer style editor.

### Session View

**[Click Session in the footer bar, session view appears]**

The session view is MAGDA's live performance mode. If you've used Ableton Live, this will look familiar — tracks are stacked horizontally with vertical clip slots that you can trigger independently. This is still a work in progress, but the foundation is there for loop-based composition and live performance workflows.

Remember that Resume button in the transport bar? That's how you jump back to the arrangement view from here.

---

### Mix View

**[Click Mix in the footer bar, mixer appears]**

And then there's the mixer — a full channel strip view with faders, pan, mute, solo. Sends are on the roadmap for the first release, which will open up parallel processing, reverb buses, and proper mix routing.

---

### The Menu Bar

**[Mouse moves to the top menu, opens Settings]**

Finally, the top menu gives you access to the main application functions. One worth highlighting is **Settings**, where you'll find your audio and MIDI device configuration, plugin folder management, and general preferences.

---

## PART 5: CLOSING (~30 sec)

**[Voiceover — MAGDA UI visible, maybe pull back to a wider view]**

That's MAGDA in its current state. We're close to version 0.1.0 — the first public release — with most of the core features already in place: recording, editing, mixing, export, project save and load, and the AI agent system. Beyond that, the roadmap goes through five milestones, all the way to Faust DSP integration, a full session view, and an extensible scripting API.

It's a DAW built in the open, with AI that assists without replacing, and an architecture designed to evolve.

If you're interested, the code is on GitHub, and I'll be sharing more of this journey as we go. If you're a developer, a musician, or just someone who thinks creative tools should be more open — I'd love to hear from you.

Thanks for watching.

**[End card with GitHub link, subscribe prompt]**

---

## PRODUCTION NOTES

- **B-roll ideas**: Early prototypes (Musical AIdeas plugin, Reaper extension), code scrolling in IDE, the manifesto text, GitHub repo
- **Text overlays**: Key phrases from the manifesto ("Software should adapt to the user", "Intelligence as automation, not replacement", "A system, not a product")
- **Music**: Subtle, tasteful background — ideally something made in MAGDA if possible, or a simple ambient bed
- **Pacing**: Let the UI walkthrough breathe. Don't rush through panels. Each context switch in the bottom panel is a mini "aha moment" — give it space.
- **Estimated timing**: Part 1 (~60s) + Part 2 (~90s) + Part 3 (~60s) + Part 4 (~180s) + Part 5 (~30s) = ~7 minutes
