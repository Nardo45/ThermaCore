// =============================================================================
// MachineProfile.cpp
// -----------------------------------------------------------------------------
// Implementation of per-machine fan control logic.
// See MachineProfile.hpp for the full explanation of each function.
// =============================================================================

#include "MachineProfile.hpp"

#include <cmath>   // std::round
#include <string>

// =============================================================================
// Orion::  —  Acer Predator Orion 3000 PO3-640
// =============================================================================

namespace Orion {

    uint16_t percent_to_rpm(int percent, uint16_t minv, uint16_t maxv) {
        // Clamp to valid range first
        if (percent < 0)   percent = 0;
        if (percent > 100) percent = 100;

        // Scale linearly: offset = (max - min) * (pct / 100)
        double diff   = static_cast<double>(maxv - minv);
        int    offset = static_cast<int>(std::round(diff * (percent / 100.0)));
        return static_cast<uint16_t>(minv + offset);
    }

    void set_mode_auto(EmbeddedControllerLinux &ec) {
        ec.writeByte(REG_MODE, MODE_AUTO);
    }

    void set_mode_manual(EmbeddedControllerLinux &ec) {
        ec.writeByte(REG_MODE, MODE_MAN);
    }

    bool is_manual(EmbeddedControllerLinux &ec) {
        return ec.readByte(REG_MODE) == MODE_MAN;
    }

    bool set_fan_percent(EmbeddedControllerLinux &ec,
                         const std::string &fan_name, int percent)
    {
        // Look up the fan's write register and max RPM by name.
        // Adding a new fan: add another entry to this table.
        struct FanDef { const char *name; uint8_t reg; uint16_t max_rpm; };
        static const FanDef fans[] = {
            { "cpu",   REG_CPU_SET,   CPU_MAX_RPM  },
            { "front", REG_FRONT_SET, CASE_MAX_RPM },
            { "back",  REG_BACK_SET,  CASE_MAX_RPM },
        };

        for (const auto &f : fans) {
            if (fan_name == f.name) {
                set_mode_manual(ec); // Must be in manual mode before writing RPM
                uint16_t target = percent_to_rpm(percent, MIN_RPM, f.max_rpm);
                return ec.writeWord(f.reg, target);
            }
        }
        return false; // Unrecognised fan name
    }

    uint16_t read_cpu_rpm  (EmbeddedControllerLinux &ec) { return ec.readWord(REG_CPU_RPM);   }
    uint16_t read_front_rpm(EmbeddedControllerLinux &ec) { return ec.readWord(REG_FRONT_RPM); }
    uint16_t read_back_rpm (EmbeddedControllerLinux &ec) { return ec.readWord(REG_BACK_RPM);  }

} // namespae Orion

// =============================================================================
// G5KF5::  —  Gigabyte G5 KF5
// =============================================================================

namespace G5KF5 {

    uint16_t tach_to_rpm(uint8_t tach_count) {
        // A count below TACH_MIN means the fan is stopped or the register isn't
        // populated.  Dividing TACH_K by a tiny number would give a wildly wrong
        // RPM so we return 0 instead.
        if (tach_count < TACH_MIN) return 0;
        return static_cast<uint16_t>(TACH_K / tach_count);
    }

    uint8_t percent_to_pwm(int percent) {
        if (percent < 0)   percent = 0;
        if (percent > 100) percent = 100;
        // Integer multiply then divide preserves precision without floating point.
        return static_cast<uint8_t>(static_cast<unsigned>(percent) * 255 / 100);
    }

    bool set_fans_percent(EmbeddedControllerLinux &ec, int percent) {
        return ec.writeByte(REG_PWM, percent_to_pwm(percent));
    }

    uint16_t read_fan1_rpm(EmbeddedControllerLinux &ec) {
        return tach_to_rpm(ec.readByte(REG_TACH1));
    }

    uint16_t read_fan2_rpm(EmbeddedControllerLinux &ec) {
        return tach_to_rpm(ec.readByte(REG_TACH2));
    }

    int read_current_percent(EmbeddedControllerLinux &ec) {
        uint8_t pwm = ec.readByte(REG_PWM);
        return static_cast<int>(static_cast<unsigned>(pwm) * 100 / 255);
    }

} // namespace G5KF5
