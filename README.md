# ThermaCore

Open-source thermal control for Acer Predator systems on Linux

ThermaCore is a Linux-native utility for monitoring and controlling fans on Acer Predator Orion desktops via the Embedded Controller.

## Supported Hardware

* **Acer Predator Orion 3000 (PO3-640)**

### Note on Hardware Support

Support for more models can be added in the future, but it is heavily dependent on community involvement. I cannot add support for hardware I do not physically own.

If you would like your specific PC to be supported, please open an issue with a request. To assist in development, you will need to provide **Embedded Controller (EC) memory dumps** using the included debugger tool. This helps reverse-engineer how PredatorSense interacts with your specific hardware registers.

## Building

This project uses a containerized build process to avoid installing development dependencies on your host system.

### 1. Build the compiler image
Create the Arch-based builder image:
```bash
podman build -t thermacore-builder .

```

### 2. Compile the TUI

Run the compiler inside the container to build the main interface:

```bash
podman run --rm -v ".:/workdir:Z" thermacore-builder g++ src/tui/orion_tui.cpp -o thermacore -O2 -lncurses -std=c++17

```

### 3. Compile the Debugger (for EC Dumps)

Run the compiler without the ncurses requirement to build the testing utility:

```bash
podman run --rm -v ".:/workdir:Z" thermacore-builder g++ src/debugger/fanctl_embedded.cpp -o thermacore-debugger -O2

```

## Usage

Both utilities require raw I/O port access to communicate with the hardware and must be run with root privileges.

### Running the TUI

```bash
sudo ./thermacore

```

### Dumping EC Memory for Hardware Support

To help add support for a new model, run the debugger and use the `dump` command:

1. Start the debugger: `sudo ./thermacore-debugger`
2. At the `>` prompt, type: `dump`
3. Copy the output (the 00-FF hex table) into your GitHub issue.

## Cleanup

**Remove the builder image:**

```bash
podman rmi thermacore-builder

```

**Remove the binaries:**

```bash
rm thermacore thermacore-debugger

```

## Roadmap

These features are planned for future updates with no specific due dates:

* **Background Daemon**: For persistent fan control without keeping a terminal open.
* **Systemd Integration**: Auto-start fan profiles on boot.
* **Custom Fan Curves**: Set RPM targets based on temperature thresholds.
* **Graphical User Interface (GUI)**: A dedicated desktop application.
* **RGB Control**: Reverse-engineering the EC registers for internal LED management.
