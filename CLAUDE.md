# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

Requires Qt 6 with the `SerialPort` module and CMake ≥ 3.16.

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./sk3m111_viewer
```

On Fedora/RHEL: `sudo dnf install qt6-qtdeclarative-devel qt6-qtbase-devel`
On Ubuntu/Debian: `sudo apt install qt6-declarative-dev qt6-base-dev`

No Qt serial module — serial I/O uses POSIX `termios` directly (Linux only).

## Architecture

**Backend (C++):**
- `serialhandler.h / .cpp` — `SerialHandler : QObject` exposed to QML via `rootContext()->setContextProperty("serialHandler", ...)`. Opens the port with `open(2)`, configures it via `termios` at 115200 8N1 raw mode, and watches the fd with `QSocketNotifier` for event-loop integration. Buffers incoming bytes in `m_buffer` and emits `receivedDataChanged()` per complete newline-terminated line. Port enumeration scans `/dev/ttyUSB*`, `ttyACM*`, `ttyAMA*`, `ttyS*` (filtering `ttyS*` without a `/sys/class/tty/.../device` entry).
- `main.cpp` — creates `QGuiApplication`, instantiates `SerialHandler`, loads `qrc:/main.qml`.

**Frontend (QML):**
- `main.qml` — single `ApplicationWindow` with a port-selection `ComboBox` (populated from `serialHandler.availablePorts`), a connect/disconnect `Button`, a full-width `Rectangle` whose `color` drives the green/red indicator, and a status `Label`.

**Data flow:** `SerialHandler::onReadyRead()` → `m_buffer` accumulation → line extraction → `toDouble()` check sets `m_isNumber` → `receivedDataChanged()` signal → QML binding updates rectangle color and text.

**Color logic:**
- Number received → `#2e7d32` (green), white text
- `"OFF"` (non-numeric) received → `#c62828` (red), white text
- Not connected / waiting → `#616161` (grey), white text

**Resources:** `resources.qrc` embeds `main.qml` at `qrc:/main.qml`. Adding new QML files requires updating this file.

## Qt 5 compatibility

Replace `find_package(Qt6 ...)` with `find_package(Qt5 ...)` and update `target_link_libraries` prefixes accordingly. QML imports (`2.15` versions) are already compatible.
