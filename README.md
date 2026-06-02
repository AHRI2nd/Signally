# MicTrans

A Windows audio routing application built on **WASAPI Shared Low Latency**, with a
node-based graph editor for wiring an unlimited number of input and output devices,
VST3 plugin hosting, and ASIO support.

Its key feature is **audio isolation**: processed audio can be routed to apps like
Discord while staying invisible to screen recorders and capture tools (OBS, Game Bar).

## Features

- **Graph-based routing** — connect any number of input/output devices and processing
  nodes by dragging connection lines between pins.
- **Low-latency I/O** — WASAPI Exclusive for microphone input, WASAPI Shared Low Latency
  for output, with a dedicated real-time mix thread (MMCSS "Pro Audio").
- **Audio isolation** — a custom capture-only WDM virtual microphone driver exposes
  processed audio as a microphone with no render endpoint, so it cannot be picked up by
  WASAPI loopback capture.
- **VST3 hosting** — insert VST3 plugins anywhere in the processing chain.
- **ASIO support** — direct device I/O via the Steinberg ASIO SDK.
- **Sessions** — save and load the entire graph as a `.mictrans` file.
- **Monitoring** — live CPU load and buffer-underrun counters.

## Architecture

```
Microphone (WASAPI Exclusive)
  -> AudioEngine (mix thread runs the graph)
  -> VST3 / Mixer / Splitter nodes
  -> Virtual Mic Output -> shared memory -> WDM driver
  -> Discord (sees it as a microphone)

OBS system-audio loopback -> no render endpoint -> nothing captured
```

## Build

Windows 10/11, Visual Studio 2022, CMake 3.22+. See [BUILD.md](BUILD.md) for full
instructions, including the WDK driver build and signing steps.

```bat
git submodule update --init --recursive
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The VST3 SDK is pulled in as a git submodule; the ASIO SDK must be placed manually —
see [third_party/README.md](third_party/README.md).

## License

- Application: depends on your JUCE license (AGPLv3 or commercial).
- VST3 SDK: MIT.
- ASIO SDK: Steinberg license (not redistributable).
