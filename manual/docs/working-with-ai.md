# AI in MAGDA: Philosophy and Expectations

MAGDA's automation features are not exclusively about music generation.

In fact, pure musical content generation is almost a side effect of a much broader goal: helping users automate repetitive tasks and interact with the DAW more naturally.

Examples include:

- Creating tracks.
- Selecting clips.
- Manipulating arrangements.
- Building chord progressions from explicit instructions.
- Generating scales, melodies or rhythms.
- Assisting with workflow and experimentation.

## Different Levels of Automation

Not all requests are content generation.

### Functional requests

These describe exactly what you want.

Examples:

- Create a new MIDI track.
- Create a drum track and insert a step sequencer.
- Generate the progression Cmin, Amaj, Bdim.
- Create a 16-bar drum pattern using the selected kit.

In these cases, the AI is primarily acting as an assistant or automation layer.

### Creative requests

These leave part of the decision-making to the model.

Examples:

- Generate a jazzy chord progression.
- Create an uplifting melody in C major.
- Suggest a bassline that complements the selected chords.

In these cases, the AI contributes musical ideas rather than simply executing instructions.

## What Should I Expect?

The quality of AI-generated results depends heavily on:

- The selected model.
- Whether the model is local or cloud-based.
- The quality of the prompt.
- The capabilities of your hardware.

A small local model running on your machine will generally produce lower-quality results than large commercial models today, though the gap is narrowing as local inference improves (see [Local Models vs Cloud Models](#local-models-vs-cloud-models) below).

The trade-off is privacy, cost, and control.

## AI Is a Starting Point, Not a Replacement

The purpose of content generation in MAGDA is to provide a foundation for further work.

The generated material is intended to be:

- Edited.
- Rearranged.
- Reinterpreted.
- Combined with your own ideas.

This is particularly useful for:

- Exploring chord progressions you may not have considered.
- Generating variations.
- Discovering happy accidents.
- Using step sequencers as intelligent random generators.

Think of the AI as a collaborator, not as a finished-product generator.

## Prompting Tips

Instead of:

"Write a melody in C."

Try:

"Generate an uplifting 8-bar melody in C major suitable for liquid drum and bass. Include rhythmic variation and a memorable motif."

The more context you provide, the better the results are likely to be.

Good prompts often include:

- Genre.
- Mood.
- Musical references.
- Desired complexity.
- Instrumentation.
- Structure.

## Local Models vs Cloud Models

MAGDA supports both cloud AI providers and local AI models. They sit at different points on a quality, privacy and cost trade-off rather than one simply being better than the other.

**Cloud providers** generally offer the highest-quality results today, but require an account and API credentials, send your prompts to a third party, and bill per use.

**Local models** run entirely on your machine. There are no API keys, nothing leaves your computer, and there is no per-request cost. MAGDA ships a small model fine-tuned specifically for its DSL (`magda-v0.3.0`), so it knows MAGDA's commands out of the box, and you can also point MAGDA at any GGUF model or a local OpenAI-compatible server such as LM Studio, Ollama, GPUStack or a `llama.cpp` server. See [AI Settings](interface/ai-settings.md) for configuration.

Local results vary with the model you choose and the hardware you run it on. Smaller quantised models are fast and light but less capable; larger models close the gap with cloud at the cost of more memory and slower responses.

### Hardware

Local inference is most responsive with GPU acceleration: Metal on Apple Silicon, or CUDA on supported builds. It also runs CPU-only, just more slowly. As a rough guide:

- The bundled fine-tuned model is 4-bit quantised and built to run on typical consumer hardware. A modern multi-core CPU with a few gigabytes of free RAM can run it; an Apple Silicon Mac or a dedicated GPU makes it noticeably faster.
- Larger general-purpose models need correspondingly more RAM (when running on CPU) or VRAM (when offloaded to a GPU).
- If responses are slow or a model fails to load, choose a smaller or more heavily quantised model, lower the **GPU Layers**, or reduce the **Context** size on the [AI Settings](interface/ai-settings.md) Local tab.

### Local inference is improving

Local inference in MAGDA is an area of active development. The fine-tuned model is updated over time, and the distance between local and cloud quality continues to narrow as both the models and the surrounding tooling improve. Choosing local today is a deliberate trade for privacy, cost and control, not a dead end.

## What You Should Not Expect

Do not expect the AI to create a smash hit for you.

That is not the goal of MAGDA's automation features and it is unlikely to ever be.

The most successful workflows are usually those where the user provides direction, evaluates the results, and iterates.

The AI can generate ideas.

The creative decisions remain yours.
