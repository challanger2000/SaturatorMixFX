# SaturatorMixFX

Experimental saturation processor and Mix FX research project for Studio One / Fender Studio.

## Goal

Build a compact hardware-style saturation processor with three selectable analog characters and use it as a clean research base for Studio One Mix FX integration.

### Planned controls

- Drive
- Character: Triode / Pentode / Iron
- Mix
- Output

### GUI direction

Photorealistic 3D hardware front panel inspired by vintage studio equipment: dark metal, dark wood, three visible vacuum tubes, large central drive control and a mechanical character selector.

## Development strategy

1. Build and validate a conventional VST3 audio effect first.
2. Keep DSP, GUI and host-integration code separated.
3. Add Mix FX-specific integration only after the required host interfaces / factory metadata have been verified.
4. Keep Windows builds reproducible through GitHub Actions.

## Status

Initial project scaffold.

## Toolchain

- C++17
- CMake
- Steinberg VST3 SDK
- Windows x64 / Visual Studio 2022

## License

No license has been selected yet.
