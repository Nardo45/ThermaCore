// =============================================================================
// HwmonSensor.cpp
// -----------------------------------------------------------------------------
// Implementation of hwmon scanning and temperature reading.
// See HwmonSensor.hpp for the full explanation.
// =============================================================================

#include "HwmonSensor.hpp"

#include <filesystem> // std::filesystem::directory_iterator
#include <fstream>    // std::ifstream
#include <string>
#include <vector>

void scan_hwmon(std::vector<Sensor> &sensors) {
    // These counters let us give distinct names to chips of the same type,
    // e.g. "CPU Core 1", "CPU Core 2" if coretemp reports multiple sensors.
    int cpu_count  = 1;
    int nvme_count = 1;

    // Iterate every hwmon device directory under /sys/class/hwmon/
    for (const auto &entry : std::filesystem::directory_iterator("/sys/class/hwmon")) {
        std::string dir = entry.path().string();

        // The "name" file inside each hwmon directory contains the driver name,
        // e.g. "coretemp", "nvme", "acpitz".  Skip this device if we can't read it.
        std::ifstream name_file(dir + "/name");
        std::string   chip_name;
        if (!(name_file >> chip_name)) continue;

        // Now look at every file inside this hwmon directory
        for (const auto &file : std::filesystem::directory_iterator(dir)) {
            std::string filename = file.path().filename().string();

            // We only care about temperature input files: temp1_input, temp2_input, etc.
            // They always start with "temp" and end with "_input".
            bool is_temp_input = (filename.find("temp") == 0) &&
            (filename.find("_input") != std::string::npos);
            if (!is_temp_input) continue;

            Sensor s;
            s.path = file.path().string();

            // Map known driver names to friendly display labels.
            // Add new entries here when adding support for new hardware.
            if      (chip_name == "coretemp")  s.display_name = "CPU Core "  + std::to_string(cpu_count++);
            else if (chip_name == "nvme")       s.display_name = "NVMe SSD " + std::to_string(nvme_count++);
            else if (chip_name == "iwlwifi_1")  s.display_name = "Wi-Fi Module";
            else if (chip_name == "acpitz")     s.display_name = "Motherboard";
            else                                s.display_name = chip_name + " " + filename; // Fallback

            sensors.push_back(s);
        }
    }
}

double read_temp(const std::string &path) {
    std::ifstream f(path);
    if (!f.is_open()) return 0.0;

    // The kernel gives us millidegrees, e.g. "45000" means 45.0 °C
    double millidegrees = 0.0;
    f >> millidegrees;
    return millidegrees / 1000.0;
}
