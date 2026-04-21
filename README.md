# ThermaCore

Open-source thermal control for systems utilizing Embedded Controller (EC) fan management on Linux.

ThermaCore is a Linux-native utility designed to interface with the EC chip to monitor and control cooling systems. It provides a low-level driver interface for hardware that manages thermal sensors and fan speeds through standard x86 I/O ports. While many laptops and desktops rely on proprietary software for thermal management, ThermaCore offers a direct, hardware-level solution for Linux environments.

## Supported Hardware

* **Acer Predator Orion 3000 (PO3-640)**
* **Gigabyte G5 KF5**

### Note on Hardware Support

The project aims to support a wide range of computers using EC-based fan control. Support for additional models depends on community involvement. If you would like a specific model supported, please open an issue with a request. Development requires Embedded Controller memory dumps from the included debugger tool to reverse-engineer hardware registers.

## Building

This project uses a containerized build process to ensure a clean environment without installing development dependencies on the host system.

### 1. Build the compiler image

Create the Debian-based builder image:

```bash
podman build -t thermacore-build .
```

### 2. Compile the project

Run the compiler inside the container to generate the binaries using CMake:

```bash
podman run --rm -v $(pwd):/workdir thermacore-build \
    sh -c "cmake -B build -S . && cmake --build build"
```

The output binary for the terminal interface is located at `build/TUI/thermacore-tui`.

## Development Environment

A dedicated development container is available with a full toolchain, including GDB and clangd for IDE support.

### Build the development image

```bash
podman build -f Containerfile.dev -t thermacore-dev .
```

### Start an interactive shell

The privileged flag is required because the utility calls ioperm to access hardware I/O ports.

```bash
podman run --rm -it \
    --privileged \
    -v $(pwd):/workdir \
    thermacore-dev bash
```

## Usage

Accessing hardware registers requires root privileges.

### Running the TUI

Start the terminal interface by specifying your machine type with the `--machine` or `-m` flag. If no flag is provided, the program defaults to the Orion profile.

```bash
# General usage
sudo ./build/TUI/thermacore-tui --machine <model>

# For Acer Predator Orion 3000
sudo ./build/TUI/thermacore-tui --machine orion

# For Gigabyte G5 KF5
sudo ./build/TUI/thermacore-tui -m g5kf5
```

Available machine identifiers:
* **Acer**: `orion`, `po3-640`
* **Gigabyte**: `g5kf5`, `g5-kf5`, `G5KF5`

### Controls

Once the TUI is active, use the following commands:
* **auto**: Returns fans to default firmware logic.
* **manual**: Enables manual override mode.
* **set <cpu|front|back> <0-100>**: Sets a specific fan to a static percentage.
* **q**: Exits the program.

### Dumping EC Memory

To assist in adding hardware support, use the debugger tool to generate a memory table:

1. Start the debugger: `sudo ./build/Debugger/thermacore-debugger`
2. Use the `dump` command at the prompt.
3. Provide the resulting hex table in a GitHub issue.

## Project Structure

* **Global**: Shared static library containing the core driver and machine profiles.
* **TUI**: Terminal-based user interface.
* **Daemon**: Background service for persistent control (in development).
* **GUI**: Graphical user interface (planned).
* **Debugger**: Utility for hardware register discovery.
