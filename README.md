# Workbench Screens

Workbench Screens is a modular C++ application intended to run on a Raspberry Pi that drives one or more LCD panels on an electronics workbench. It is designed to aggregate measurements and diagnostics from diverse instruments—power supplies, logic analyzers, oscilloscopes, serial devices, and Teensy-hosted peripherals—and present them through a flexible UI built with [FTXUI](https://github.com/ArthurSonzogni/ftxui).

This repository currently provides the core architecture, plugin interfaces, UI scaffolding, and a Teensy communication stub required to grow the system into a fully featured workbench console.

---

## Highlights

- **Modular data sources**: Each feature is packaged as a plugin implementing the `core::Module` interface.
- **Central data registry**: Modules publish structured data frames that can be observed by other components and UI widgets.
- **FTXUI dashboard**: A window-based terminal interface capable of hosting multiple visualizations with add/close/clone semantics.
- **Teensy link abstraction**: A placeholder protocol handler for exchanging measurements and GPIO commands with a Teensy 4.1 over USB.
- **Demo module**: A mock voltage monitor illustrating how to register data sources and expose UI panels.

---

## Repository Layout

```
.
├── CMakeLists.txt           # Build configuration (C++17, single executable)
├── src/
│   ├── App.{h,cpp}          # Top-level application bootstrap
│   ├── main.cpp             # Entry point registering demo module
│   ├── core/                # Data models, module interfaces, plugin manager
│   ├── ui/                  # Dashboard + window descriptors (FTXUI integration)
│   ├── hardware/            # Teensy link and protocol placeholders
│   └── modules/             # Example modules (currently DemoModule)
└── .gitignore
```

---

## Getting Started

### Prerequisites

- C++17-capable compiler (MSVC 19.28+, GCC 9+, Clang 10+).
- CMake 3.15 or newer.
- (Optional) [FTXUI](https://github.com/ArthurSonzogni/ftxui). The code uses `__has_include` to detect headers; without FTXUI the project still builds, but UI components are no-ops. # TODO: make FTXUI mandatory once UI is fleshed out.
- (Future) USB serial access to a Teensy 4.1 and connected sensors.

### Build Instructions

```powershell
cmake -S . -B build
cmake --build build
```

On Windows the default generator is Visual Studio 2022. Pass `-G "Ninja"` or similar if you prefer another toolchain. On Linux/macOS add `-D CMAKE_BUILD_TYPE=Release` as appropriate.

### Running

```powershell
./build/Debug/WorkbenchScreens.exe     # Windows (debug configuration)
# or
./build/WorkbenchScreens               # Unix-likes if using Make/Ninja
```

Without FTXUI available, the executable will initialize modules and immediately shut down; when FTXUI is linked it opens a fullscreen dashboard window containing the demo panel.

---

## System Architecture

### Application Bootstrap (`src/App.*`)

`App` wires together the global subsystems:

- Instantiates a `core::DataRegistry` for module data exchange.
- Owns a `hardware::TeensyLink` instance for hardware communication.
- Constructs a `core::PluginManager` that manages module lifecycles.
- Hosts a `ui::Dashboard` that renders window instances using FTXUI.
- Collects window specifications from modules and opens any flagged `openByDefault`.

### Core Layer (`src/core/`)

- **`Types.h`** – Defines canonical data payload shapes (`NumericSample`, `WaveformSample`, `SerialSample`, `LogicSample`, `GpioState`) and the `DataFrame` container delivered through the registry.
- **`DataRegistry`** – Maintains source metadata, latest frames, and observer callbacks. Modules publish via `update`, consumers subscribe with `addObserver`.
- **`Module.h`** – Contract for plugins. Each module declares sources, default windows, and responds to lifecycle hooks (`initialize`, `shutdown`, `tick`).
- **`ModuleContext`** – Bundles access to shared facilities (`DataRegistry`, `TeensyLink`). Passed to modules and UI window factories.
- **`PluginManager`** – Tracks module instances, registers their sources with the registry, dispatches lifecycle events, and supports runtime addition/removal.

### UI Layer (`src/ui/`)

- **`WindowSpec`** – Describes an FTXUI component factory (title, clone/close flags, default-open preference) bound to a `WindowContext`.
- **`Dashboard`** – Manages available window specs, active window instances, and builds the composite FTXUI renderer. Provides utilities for adding, cloning, and closing windows that modules may invoke later.

The UI is intentionally minimal: header controls are placeholders and window-level buttons are rendered as labels until interactive widgets are added. This keeps the focus on the data flow while leaving space for future interaction design.

### Hardware Layer (`src/hardware/`)

- **`TeensyLink`** – Thread-safe wrapper that will own the USB serial connection. Currently queues incoming byte buffers and dispatches decoded messages to the `DataRegistry`.
- **`TeensyProtocol`** – Enumerates the protocol message types (handshake, measurement updates, logic frames, serial data, GPIO commands, heartbeats). Encoding/decoding functions return placeholder stubs to be implemented once the on-device firmware protocol is finalized.

### Modules (`src/modules/`)

- **`DemoModule`** – A reference module that:
  - Declares a numeric voltage data source (`demo.metrics`).
  - Publishes periodic mock voltage samples to the registry.
  - Registers a default-open window that reads the latest published value and renders it via FTXUI.
  - Demonstrates use of the module `tick` hook to schedule updates.

---

## Developing Modules

1. Derive from `core::Module`.
2. Implement `id()` and `displayName()` (must be unique and user-friendly).
3. In `declareSources()`, return `core::SourceMetadata` entries for each logical data stream you plan to publish.
4. Use `initialize()` to register observers, start hardware resources, or seed initial data via `ModuleContext::dataRegistry`.
5. Implement `tick()` if you need timed polling (call `PluginManager::tickModules` from the main loop to drive it).
6. Populate `createDefaultWindows()` with `ui::WindowSpec` objects that create FTXUI components. The supplied `WindowContext` grants access to the shared `ModuleContext`.
7. Call `shutdown()` to release resources or unregister observers if necessary.

Register modules with the application by calling `app.registerModule(std::make_unique<MyModule>());` before invoking `app.run()`.

---

## Data Registry & Observer Model

- **Sources** must be registered before publishing frames; the plugin manager automates this by calling `declareSources()` during initialization.
- **Updates** involve filling out a `core::DataFrame` with channel IDs and payload variants (`NumericSample`, `WaveformSample`, etc.), then calling `DataRegistry::update()`.
- **Observers** subscribe per-source and receive the full `DataFrame`. Tokens returned by `addObserver` can be used with `removeObserver` to clean up.
- **Thread Safety** is handled internally via `std::shared_mutex` allowing concurrent reads and serialized writes.

This design enables both UI widgets and background analytics modules to tap into the same data streams without tight coupling to producers.

---

## Teensy Communication Protocol (Draft)

The protocol is still a stub; the headers enumerate anticipated message types to aid firmware development.

| Message Type             | Direction        | Notes                                                                   |
|--------------------------|------------------|-------------------------------------------------------------------------|
| `HandshakeRequest/Response` | Teensy ↔ Pi      | Establish protocol version, firmware ID, compatibility.                 |
| `MeasurementUpdate`      | Teensy → Pi      | Batched numeric channel readings (volts, amps, watts, etc.).            |
| `LogicFrame`             | Teensy → Pi      | Packed digital logic samples plus sample rate metadata.                 |
| `SerialData`             | Teensy → Pi      | Raw serial bytes forwarded from attached instruments or adapters.       |
| `SetGpioState`           | Pi → Teensy      | Command to drive a GPIO pin high/low (for relays, toggles, etc.).       |
| `QueryGpioState` / `GpioStateResponse` | Pi ↔ Teensy | Poll or receive current GPIO levels.                                   |
| `Heartbeat` / `Ack` / `Nack` | Bidirectional | Keep-alive and reliability semantics (timeouts, retries, diagnostics).   |

Implementations of `Encode`/`Decode` should define a lightweight framing scheme (length prefix, checksum, etc.) once the payload formats are finalized. `TeensyLink::handleMessage` already demonstrates how measurement and GPIO payloads map into registry frames.

---

## Roadmap & Suggestions

- **Complete protocol serialization** and integrate with a USB serial backend (Boost.Asio, libserialport, or native Win32 APIs).
- **Implement real-time UI interactivity**: button handlers for window controls, module selection menus, and layout management (tabs, grids).
- **Add background scheduler** to call `PluginManager::tickModules` at a fixed cadence without blocking the UI.
- **Expand module catalog**: serial console, oscilloscope viewer, logic analyzer timelines, power supply controllers.
- **Persist configuration**: remember open windows, module settings, and hardware port selections across runs.
- **Testing & CI**: add unit tests for the data registry, protocol parsing, and module lifecycles.
- **Packaging for Raspberry Pi**: set up a cross-compilation toolchain and systemd service definition for kiosk deployment.

---

## Contributing

1. Fork the repository and create a feature branch.
2. Keep code in `src/` organized by responsibility (core/UI/hardware/modules).
3. Document new modules, data types, and protocol updates in this README.
4. Run `cmake --build build` before submitting PRs to ensure the project compiles.

Issues and PRs describing additional measurement devices, UI widgets, or protocol refinements are very welcome.

---

## License

Project licensing has not yet been specified. Add a `LICENSE` file (MIT, Apache-2.0, etc.) once a decision is made.
