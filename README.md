# SaturatorMixFX

Experimental saturation processor and Mix FX research project for Studio One / Fender Studio.

## Goal

Build a compact hardware-style saturation processor with three selectable analog characters and use it as a clean research base for Studio One Mix FX integration.

## Current controls

- On / Off
- Drive
- Character: Triode / Pentode / Iron
- Mix
- Output

## Planned architecture

The project is intentionally prepared for two related components:

1. **Saturator Channel** - conventional VST3 channel effect used for normal insert processing and later, if the verified Mix FX architecture requires it, per-channel participation/control.
2. **Saturator MixFX** - the dedicated Studio One / Fender Studio Mix FX component once the required host interfaces and registration details are verified.

DSP should be shared between both components so the saturation models only exist once in the codebase.

No proprietary Mix FX interface is guessed or hard-coded before it has been verified through legitimate interoperability research.

## GUI direction

Photorealistic 3D hardware front panel inspired by vintage studio equipment: dark metal, dark wood, three visible vacuum tubes, large central Drive control, mechanical character selector and a physical On/Off switch. The selected/active tube can later be represented by illumination states.

## Local development

The primary development workflow is local on Windows. GitHub is used for source control and backup.

Run:

`BUILD_SaturatorMixFX.cmd`

The script checks the toolchain, locates the VST3 SDK, configures CMake, builds x64 Release and keeps the console open on success or failure.

GitHub Actions remains available as a manual fallback/verification build only; commits to `main` do not automatically start paid Windows runners.

## Development strategy

1. Build and validate the conventional VST3 audio effect first.
2. Keep DSP, GUI and host-integration code separated.
3. Add Mix FX-specific integration only after the required host interfaces / factory metadata have been verified.
4. Use local Windows builds for the normal edit-build-test loop in Studio One.
5. Use the manual GitHub Actions workflow only for independent verification when useful.

## Status

- Initial VST3 scaffold complete.
- Windows x64 GitHub verification build passed.
- Triode / Pentode / Iron saturation curves implemented.
- Automatable internal On/Off bypass implemented.
- Local one-click build script added.
- Mix FX host integration: research/verification stage.

## Toolchain

- C++17
- CMake
- Steinberg VST3 SDK
- Windows x64 / Visual Studio 2022

## License

No license has been selected yet.
