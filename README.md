# ThermaCore

Open-source thermal control for Acer Predator systems on Linux

ThermaCore is a Linux-native TUI for monitoring and controlling fans on Acer Predator Orion desktops via the Embedded Controller.

## Supported Hardware

* **Acer Predator Orion 3000 (PO3-640)**

### Note on Hardware Support

Support for more models can be added in the future, but it is heavily dependent on community involvement. I cannot add support for hardware I do not physically own.

If you would like your specific PC to be supported, you may open an issue with a request. However, be prepared to assist in the development process. This involves:

* Providing **Embedded Controller (EC) memory dumps**.
* Helping **reverse-engineer** how PredatorSense interacts with your specific hardware (monitoring which registers change when you toggle settings).
* Testing experimental builds on your machine to verify functionality.

## Building

This project uses a containerized build process to avoid installing development dependencies on your host system.

### 1. Build the compiler image

Create the Arch-based builder image:

```bash
podman build -t thermacore-builder .

```

### 2. Compile the binary

Run the compiler inside the container. This mounts your current directory to `/workdir` and outputs the `thermacore` binary directly to your host:

```bash
podman run --rm -v ".:/workdir:Z" thermacore-builder g++ src/orion_tui.cpp -o thermacore -O2 -lncurses -std=c++17

```

## Usage

Since the program requires raw I/O port access to communicate with the hardware, it must be run with root privileges:

```bash
sudo ./thermacore

```

## Cleanup

**Remove the builder image:**

```bash
podman rmi thermacore-builder

```

**Remove the binary:**

```bash
rm thermacore

```

## Roadmap

These features are planned for future updates with no specific due dates:

* **Background Daemon**: For persistent fan control without keeping a terminal open.
* **Systemd Integration**: Auto-start fan profiles on boot.
* **Custom Fan Curves**: Set RPM targets based on temperature thresholds.
* **Graphical User Interface (GUI)**: A dedicated desktop application.
* **RGB Control**: Reverse-engineering the EC registers for internal LED management.
