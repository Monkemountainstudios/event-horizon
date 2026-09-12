# Event Horizon

Event Horizon is an experimental generative audio instrument built with JUCE. Feed it notes, phrases, speech, pads, drums, or complete gestures and it turns the performer’s recent past into an orbiting musical ecology.

Material begins as recognisable call-and-response, accelerates and breaks apart, then accumulates as a long-lived plasma of interwoven grains. Eventually it crosses the horizon and emits delayed, outward-moving Hawking radiation through a separate shimmer field.

This is a public pre-release intended for adventurous testers. Expect unusual behaviour; please report repeatable crashes, audio dropouts, broken project recall, and host compatibility problems through GitHub Issues.

## What makes it Event Horizon

- Manual four-second recording and audio-file insertion up to six seconds.
- Probabilistic continuous LIVE capture with phrase-sensitive endings and hunger-shaped 2–6 second limits.
- Six sounding generations plus two waiting slots, all sharing fixed real-time voice pools.
- Recognisable outer repetitions that progressively accelerate, fragment, lose matter, and become granular.
- A long, energy-normalised plasma dwell that retains the pitch, formants, and identity of its sources.
- Separate gravity reverb and delayed octave/fifth Hawking shimmer.
- Audio-linked orbital panning, collisions, plasma activity, and centre-to-edge radiation lightning.
- Rare Ghost memories, Retrograde phrases, parabolic flybys, slingshots, and catastrophic collisions.
- Catastrophic impacts can destroy one body and divide another into red normal-speed and half-speed descendants.

The engine is intentionally bounded: source storage, fragments, radiation voices, and collisions cannot grow without limit during a long performance.

## Controls

| Control | Purpose |
| --- | --- |
| **GRAVITY** | Changes the overall inward pull and journey rate. |
| **CHURN** | Sets the scale of the long residency at the event horizon. |
| **MAGNITUDE** | A single macro for appetite, dispersion, field, and radiation intensity. |
| **MIX** | Equal-power dry/wet balance for insert use. |

The four buttons provide **RECORD**, **MANUAL INSERT**, **CLEAR**, and **LIVE CAPTURE**. Six amber lamps show sounding generations; two red lamps show occupied waiting capacity.

## Downloads

Unsigned public-test builds are attached to each GitHub pre-release:

- **Windows x64:** Standalone application and VST3.
- **macOS Universal:** Standalone application, VST3, and Audio Unit for Intel and Apple Silicon.

### Windows installation

Copy `Event Horizon.vst3` to:

```text
C:\Program Files\Common Files\VST3
```

The standalone `Event Horizon.exe` may be placed anywhere. Because these test builds are not commercially code-signed, Windows may identify the publisher as unknown.

### macOS installation

Copy the formats you want to use:

```text
Event Horizon.vst3      → ~/Library/Audio/Plug-Ins/VST3/
Event Horizon.component → ~/Library/Audio/Plug-Ins/Components/
```

The standalone `.app` may be placed in `/Applications`. Public-test builds are ad-hoc signed but not Apple-notarized, so macOS may require the user to approve first launch in the normal system interface.

## Building from source

Requirements:

- CMake 3.22 or newer
- A C++20 compiler
- Git access during first configuration

The project fetches JUCE 9.0.1 automatically when no local checkout is provided:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
```

To use an existing JUCE checkout:

```sh
cmake -S . -B build -DEVENT_HORIZON_JUCE_PATH=/path/to/JUCE
```

Windows builds produce Standalone and VST3. macOS builds additionally produce Audio Unit. Tagging a commit with a version beginning in `v` runs the public Windows/macOS build workflow and creates a GitHub pre-release.

## Credits

- **Original concept, instrument design, creative direction, sound design, performance, and testing:** Monke Mountain Studios.
- **Programming, DSP systems, interface implementation, and technical development:** Codex by OpenAI, working interactively with the creator.
- **Concept development, interpretation, documentation, and creative think tank:** ASTRA and Codex.

Event Horizon was developed as a sustained human–AI collaboration. Monke Mountain Studios takes responsibility for the instrument’s ideas, intent, aesthetic decisions, and public release.

## Licence

Event Horizon is free software licensed under the **GNU Affero General Public License v3.0 or later** (`AGPL-3.0-or-later`). JUCE itself is not vendored here and remains separately dual-licensed under AGPLv3 or the commercial JUCE licence. This public project uses JUCE through its AGPL route.

See `LICENSE` for the complete terms and `DESIGN-NOTES.md` for the instrument’s governing laws and development history.
