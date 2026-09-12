# Testing and contributing

Event Horizon is currently seeking musical testing more than new features.

When reporting a problem, please include:

- Operating system and processor architecture.
- Host and host version, or whether the Standalone build was used.
- Plug-in format: VST3 or Audio Unit.
- Sample rate and audio-buffer size.
- The shortest sequence that reproduces the problem.
- Whether it also happens after pressing **CLEAR** or in a new project.

Short audio/video examples are welcome when they make the behaviour easier to understand. Do not upload private recordings or third-party copyrighted material you are not allowed to share.

For code contributions, keep the engine’s governing laws in `DESIGN-NOTES.md` intact. New behaviour must remain probabilistic, bounded, causally connected to engine state, and musically subordinate to the ordinary journey.
