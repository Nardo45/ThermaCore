#pragma once
// =============================================================================
// HwmonSensor.hpp
// -----------------------------------------------------------------------------
// Linux Hardware Monitor (hwmon) interface.
//
// The Linux kernel exposes hardware sensor data through the virtual filesystem
// at /sys/class/hwmon/.  Each hardware monitoring chip on the system (CPU
// thermal sensor, NVMe drive, Wi-Fi chip, etc.) gets its own directory there,
// e.g. /sys/class/hwmon/hwmon0/.
//
// Inside each directory:
//   name          — a text file containing the chip's driver name (e.g. "coretemp")
//   temp1_input   — current temperature in millidegrees Celsius (divide by 1000)
//   temp2_input   — a second sensor on the same chip, if present
//   ... etc.
//
// This module scans all hwmon directories at startup, builds a list of named
// Sensor entries, and provides a function to read the current temperature of
// any sensor from that list.
//
// Used by: TUI, GUI, Daemon (anything that needs to show or react to temperatures)
// =============================================================================

#include <string>
#include <vector>

// =============================================================================
// Sensor
// -----------------------------------------------------------------------------
// Represents a single temperature sensor discovered in /sys/class/hwmon.
// =============================================================================
struct Sensor {
    std::string display_name; // Human-readable label shown in the UI, e.g. "CPU Core 1"
    std::string path;         // Full path to the sysfs input file, e.g. "/sys/class/hwmon/hwmon0/temp1_input"
};

// scan_hwmon(sensors)
// -----------------------------------------------------------------------------
// Walks /sys/class/hwmon/, finds every temp*_input file, and appends a Sensor
// entry for each one into `sensors`.  Should be called once at startup before
// the main loop begins.
//
// The function maps known driver names to friendly labels:
//   "coretemp" → "CPU Core N"
//   "nvme"     → "NVMe SSD N"
//   "iwlwifi_1"→ "Wi-Fi Module"
//   "acpitz"   → "Motherboard"
//   anything else → raw chip name + filename (fallback)
//
// Parameters:
//   sensors — the vector to populate; entries are appended (not replaced)
void scan_hwmon(std::vector<Sensor> &sensors);

// read_temp(path)
// -----------------------------------------------------------------------------
// Reads the current temperature from a sysfs sensor file.
// The kernel stores temperatures as integer millidegrees (e.g. 45000 = 45.0 °C).
// This function divides by 1000 to return a human-readable double in °C.
//
// Parameters:
//   path — full path to a temp*_input file (as stored in Sensor::path)
//
// Returns:
//   Temperature in degrees Celsius, or 0.0 if the file could not be read.
double read_temp(const std::string &path);
