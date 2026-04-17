# Project Commitments

*Last updated: 2026-04-15*

This document describes the commitments I make as the maintainer of magda-core to users and contributors. These are promises about how the project is run, beyond what the license itself requires.

## Licensing

magda-core is licensed under GPL-3.0.

I commit to the following:

- Any version of magda-core released under GPL-3.0 will remain available under GPL-3.0 in perpetuity. No future licensing decision can retroactively remove the GPL-licensed version from circulation.
- The main branch and tagged releases of this repository will continue to be published under GPL-3.0 for the foreseeable future.
- If the project is ever sold, transferred, or undergoes a major governance change, the GPL-licensed codebase as of that date will remain available for community forking, and I will support that transition in good faith.

If magda-core ever adopts a dual-licensing or commercial offering in addition to the GPL track (for example, to fund development or offer commercial support), this will be announced openly with reasoning, and the GPL track will continue in parallel.

## Contributor License Agreement

magda-core currently uses a CLA for external contributions. I want to be transparent about why it exists and what it does.

The CLA grants me, as maintainer, the right to relicense contributed code under alternative licenses. In practice, this exists to preserve the option of offering a commercial license alongside the GPL version in the future, and to resolve potential license-compatibility issues with upstream dependencies.

What the CLA does **not** mean:

- It does not remove any contributor's rights to their own work. Contributors retain copyright and can use their contributions however they like elsewhere.
- It does not allow me to retroactively un-GPL any released version of magda-core (see the Licensing section above).
- It does not give me ownership of contributors' work — only a broad license to use it.

I am open to revisiting the CLA structure (including moving to a DCO-based model) based on contributor feedback. If you are considering contributing and the CLA is a blocker, please open a discussion.

## Engineering Practices

magda-core is built with deliberate attention to software engineering discipline. Regardless of whether code is written by hand or with AI assistance, the same standards apply:

- **Design patterns.** The codebase follows established design patterns appropriate to a DAW architecture — clear separation of concerns between audio engine, UI, plugin hosting, and project persistence.
- **Modularity.** Components are built to be testable, replaceable, and independently reasoned about. I resist shortcuts that create hidden coupling.
- **Change discipline.** Changes go through pull requests with a clear description of intent. AI-assisted contributions are held to the same standard as any other code.
- **Testing.** Critical paths (audio rendering, project save/load, plugin state) are covered by automated tests. Bug fixes come with regression tests where practical.
- **Readability.** Code is written to be read. Naming, structure, and comments are treated as part of the product, not an afterthought.

These are not claims of perfection — bugs will still happen and architecture will still evolve. They are commitments about how I approach the work.

## AI-Assisted Development

magda-core is developed with AI-assisted tooling as part of the workflow. I believe in being transparent about this rather than hiding it, since it is increasingly common across the industry.

My practices:

- All code, whether human-written or AI-assisted, is reviewed and integrated by me. I am the author of record for the project and take responsibility for what ships.
- AI tools are used as assistance for implementation, refactoring, test generation, and debugging — not as unreviewed code generators.
- I review AI-assisted output for structural originality before integration. I do not accept large verbatim suggestions that appear to reproduce identifiable third-party code.
- The project does not knowingly incorporate code from licenses incompatible with GPL-3.0.

If any user identifies code in magda-core that appears to reproduce copyrighted material from another source, please open an issue with the label `license-concern` and I will investigate and remediate promptly.

The copyright position of magda-core is standard for AI-assisted projects: the creative direction, architecture, integration, and authorship decisions are human-made, and the project as a whole is a copyrightable human-authored work. This aligns with current guidance from the U.S. Copyright Office and similar bodies.

## Maintainer Commitments

- **Bug triage.** I aim to triage new issues within a reasonable window and prioritize reproducible bugs on supported platforms. Issues that cannot be reproduced or that lack sufficient information may be labeled `needs-info` and closed if they remain inactive.
- **Breaking changes.** Breaking changes are documented in the changelog with migration notes.
- **Telemetry.** magda-core does not include telemetry, analytics, or phone-home behavior. Your project files, settings, and usage stay on your machine.
- **Data locality.** All user data (projects, settings, plugin state) is stored locally. The project has no cloud dependency.
- **Deprecation.** If I ever need to stop maintaining magda-core, I will post a clear deprecation notice well in advance and, where possible, help transition to a community fork.

## Scope of Support

magda-core targets the following platforms as primary support tier:

- Windows 10 / 11
- macOS (recent versions)
- Major Linux distributions with PipeWire

Bugs on supported platforms are prioritized. Bugs on other configurations (older distros, exotic audio stacks, specific hardware combinations) are accepted and investigated as time permits, but may not be fixed on the same timeline.

## Reporting Concerns

If you have a concern about any commitment in this document, or believe the project is not living up to it, please open an issue or start a discussion. I would rather hear about a concern directly than have it go unaddressed.

---

*These commitments are written in good faith. They will evolve as the project grows, but any change will be visible in the commit history of this file.*
