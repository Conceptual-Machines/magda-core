# MAGDA documentation

Everything here is prose about the project. The manual users read is `manual/`, the code
comments are in the code, and this is the layer in between: how something is built, why it is
built that way, and how to work on it.

Six places, and the rule for which is which:

| Directory | What belongs in it |
| --- | --- |
| [`architecture/`](architecture) | how a subsystem works and why it is shaped that way |
| [`specs/`](specs) | a feature or a piece of work described before it exists |
| [`development/`](development) | how to work on the codebase |
| [`ci/`](ci) | the repository itself: builds, runners, protections, templates |
| [`product/`](product) | what MAGDA is for, and how it is shown |
| [`archive/`](archive) | finished. Kept because it explains how something got this way |

`issues/` and `smoke-tests/` are working notes attached to particular issues and manual test
passes, and stay as they are.

A doc that is still true belongs in one of the first five. A doc that was true when it was
written and describes work that has since finished belongs in `archive/`, which is a statement
about the document rather than about its quality.

---

## architecture

| Doc | What it covers |
| --- | --- |
| [native-engine.md](architecture/native-engine.md) | the engine replacing Tracktion Engine: plan, executor, clips, threads |
| [faust-integration.md](architecture/faust-integration.md) | how Faust DSP is compiled and hosted |
| [faust-param-pool.md](architecture/faust-param-pool.md) | the 64-slot parameter pool and its routing rules |
| [tracktion-parameter-writes.md](architecture/tracktion-parameter-writes.md) | why host parameter writes go through `setParameterFromHost` |
| [remote-api-contract.md](architecture/remote-api-contract.md) | the remote API's surface |
| [remote-api-transport.md](architecture/remote-api-transport.md) | the WebSocket transport behind it |
| [dual-mode-system.md](architecture/dual-mode-system.md) | arrangement and session, and what switches between them |
| [chord-suggestion-pipeline.md](architecture/chord-suggestion-pipeline.md) | how a chord suggestion is arrived at |
| [beat-migration-remaining.md](architecture/beat-migration-remaining.md) | beats as the authoritative domain, and what still holds seconds |
| [component-management-guide.md](architecture/component-management-guide.md) | UI component ownership and lifetime |
| [expandable-central-panel.md](architecture/expandable-central-panel.md) | the central panel's expansion model |

## specs

| Doc | What it covers |
| --- | --- |
| [track-model-architecture.md](specs/track-model-architecture.md) | the track model |
| [mixing-engine-plan.md](specs/mixing-engine-plan.md) | the mixing work, as planned |
| [sequencer-stabilization-plan.md](specs/sequencer-stabilization-plan.md) | collapsing the duplicated sequencer state paths |
| [curve-modulator.md](specs/curve-modulator.md) | the curve modulator |
| [customizable-footer-tabs.md](specs/customizable-footer-tabs.md) | footer tab customisation |

## development

| Doc | What it covers |
| --- | --- |
| [testing-guide.md](development/testing-guide.md) | how the tests are organised and how to add one |
| [code-style.md](development/code-style.md) | the house style |
| [glossary.md](development/glossary.md) | what a word means here, when it means something particular |

## ci

| Doc | What it covers |
| --- | --- |
| [ci-setup.md](ci/ci-setup.md) | what the pipeline runs |
| [automated-workflows.md](ci/automated-workflows.md) | the periodic and analysis workflows |
| [branch-protection.md](ci/branch-protection.md) | branch protection and the security model around it |
| [github-settings.md](ci/github-settings.md) | repository settings, written down so they can be restored |
| [github-issue-templates.md](ci/github-issue-templates.md) | the issue templates and what each is for |
| [codeql-optimization.md](ci/codeql-optimization.md) | keeping the CodeQL run affordable |
| [windows-runner-setup.md](ci/windows-runner-setup.md) | standing up the self-hosted Windows runner |

## product

| Doc | What it covers |
| --- | --- |
| [vision.md](product/vision.md) | what MAGDA is and what it is for |
| [manifesto.md](product/manifesto.md) | the shorter statement of the same |
| [video-shotlist.md](product/video-shotlist.md) | the showcase shot list |
| [video-scripts/](product/video-scripts) | scripts for those videos |

## archive

Finished work, kept for the trail it leaves: two workflow setup summaries, a February bug
analysis, the local LLM proof of concept, the Faust free-tier POC notes, an earlier manifesto
draft, and the January test plan that `development/testing-guide.md` replaced.
