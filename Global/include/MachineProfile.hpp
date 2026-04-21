#pragma once
// =============================================================================
// MachineProfile.hpp
// -----------------------------------------------------------------------------
// All machine-specific knowledge: register addresses, fan limits, and the
// higher-level read/write operations built on top of the raw EC driver.
//
// "Machine profile" means: given a particular laptop or desktop model, what
// EC registers control the fans, what values do those registers expect, and
// what are the physical limits of the hardware?
//
// Each supported machine gets its own namespace inside this file so that the
// constants and helper functions for the Orion and the G5 KF5 never collide,
// and so it's obvious at a glance where a piece of logic belongs.
//
// Adding a new machine:
//   1. Add it to the Machine enum below.
//   2. Create a new namespace (e.g. namespace MyMachine { ... }) with the
//      register constants and read/write helpers.
//   3. Update FanController.cpp to handle the new enum value.
//
// Used by: TUI, GUI, Daemon, Debugger — anything that needs to control fans.
// =============================================================================

#include "EmbeddedController.hpp"
#include <cstdint>
#include <string>

// =============================================================================
// Machine enum
// -----------------------------------------------------------------------------
// Identifies which physical machine ThermaCore is running on.
// The TUI selects this at startup via the --machine flag or auto-detection.
// =============================================================================
enum class Machine {
    ORION,  // Acer Predator Orion 3000 PO3-640
    G5KF5,  // Gigabyte G5 KF5
};

// =============================================================================
// Orion namespace: Acer Predator Orion 3000 PO3-640
// -----------------------------------------------------------------------------
// Fan control on the Orion works through RPM targets:
//   - A mode register (0x1F) switches the EC between AUTO and MANUAL control.
//   - In MANUAL mode, you write a 16-bit target RPM to the per-fan set register.
//   - The EC then drives that fan to match the requested RPM.
//   - Fan speeds are read back (live RPM) from separate read registers.
// =============================================================================
namespace Orion {

    // ── Mode register ──────────────────────────────────────────────────────────
    // Writing to this register tells the EC whether it should manage fans on its
    // own (AUTO) or let us set them directly (MANUAL).
    static constexpr uint8_t REG_MODE  = 0x1F;
    static constexpr uint8_t MODE_AUTO = 0x00; // EC controls fans automatically
    static constexpr uint8_t MODE_MAN  = 0x01; // We control fans manually

    // ── Live RPM read registers (16-bit words, big-endian) ─────────────────────
    // Reading these tells us how fast each fan is actually spinning right now.
    static constexpr uint8_t REG_CPU_RPM   = 0x14;
    static constexpr uint8_t REG_FRONT_RPM = 0x16;
    static constexpr uint8_t REG_BACK_RPM  = 0x1A;

    // ── RPM set registers (16-bit words) ───────────────────────────────────────
    // Writing a target RPM to these (in MANUAL mode) commands the EC to spin
    // the corresponding fan to that speed.
    static constexpr uint8_t REG_CPU_SET   = 0xF0;
    static constexpr uint8_t REG_FRONT_SET = 0xF2;
    static constexpr uint8_t REG_BACK_SET  = 0xF6;

    // ── Fan RPM limits ─────────────────────────────────────────────────────────
    // The physical fans cannot spin below MIN_RPM or above their respective max.
    // Requesting a value outside this range would either stall the fan or be
    // clamped by the EC firmware.
    static constexpr uint16_t MIN_RPM      = 900;
    static constexpr uint16_t CPU_MAX_RPM  = 4800;
    static constexpr uint16_t CASE_MAX_RPM = 3500; // Used by both front and back fans

    // percent_to_rpm(percent, minv, maxv)
    // -------------------------------------------------------------------------
    // Converts a user-supplied percentage (0–100) into an RPM value scaled
    // linearly between minv and maxv.
    //   0%   → minv  (fans keep spinning at minimum; we never command 0 RPM)
    //   100% → maxv
    //
    // Parameters:
    //   percent — desired speed as a percentage [0, 100]; clamped if out of range
    //   minv    — minimum RPM for this fan
    //   maxv    — maximum RPM for this fan
    //
    // Returns: target RPM as a uint16_t
    uint16_t percent_to_rpm(int percent, uint16_t minv, uint16_t maxv);

    // set_mode_auto(ec)
    // -------------------------------------------------------------------------
    // Writes MODE_AUTO to REG_MODE, returning fan control to the EC.
    void set_mode_auto(EmbeddedControllerLinux &ec);

    // set_mode_manual(ec)
    // -------------------------------------------------------------------------
    // Writes MODE_MAN to REG_MODE, allowing us to set fan speeds directly.
    void set_mode_manual(EmbeddedControllerLinux &ec);

    // is_manual(ec)
    // -------------------------------------------------------------------------
    // Reads REG_MODE and returns true if the EC is currently in manual mode.
    bool is_manual(EmbeddedControllerLinux &ec);

    // set_fan_percent(ec, fan_name, percent)
    // -------------------------------------------------------------------------
    // Sets a single fan to the requested percentage of its speed range.
    // Automatically switches to MANUAL mode first.
    //
    // Parameters:
    //   ec       — the EC driver instance
    //   fan_name — "cpu", "front", or "back"
    //   percent  — target speed percentage [0, 100]
    //
    // Returns: true on success, false if fan_name was not recognised
    bool set_fan_percent(EmbeddedControllerLinux &ec,
                         const std::string &fan_name, int percent);

    // read_cpu_rpm / read_front_rpm / read_back_rpm
    // -------------------------------------------------------------------------
    // Read the live RPM of each fan from the EC.
    // Returns the current RPM as reported by the EC firmware.
    uint16_t read_cpu_rpm  (EmbeddedControllerLinux &ec);
    uint16_t read_front_rpm(EmbeddedControllerLinux &ec);
    uint16_t read_back_rpm (EmbeddedControllerLinux &ec);

} // namespace Orion

// =============================================================================
// G5KF5 namespace: Gigabyte G5 KF5
// -----------------------------------------------------------------------------
// Fan control on the G5 KF5 is PWM-based and simpler than the Orion:
//   - A single byte register (0xE7) sets the duty cycle for BOTH fans at once.
//     0 = 0%, 255 = 100%.  There is no per-fan register.
//   - There is NO hardware auto/manual mode register.  Writing to 0xE7 takes
//     effect immediately.
//   - Fan speeds are read as raw tachometer counts from 0xD0 (fan1) and
//     0xD2 (fan2). These are NOT already in RPM, see tach_to_rpm() below.
// =============================================================================
namespace G5KF5 {

    // ── Register addresses ─────────────────────────────────────────────────────
    static constexpr uint8_t REG_PWM   = 0xE7; // PWM duty cycle for both fans (0-255)
    static constexpr uint8_t REG_TACH1 = 0xD0; // Raw tachometer count for fan 1
    static constexpr uint8_t REG_TACH2 = 0xD2; // Raw tachometer count for fan 2

    // ── Tach-to-RPM conversion ─────────────────────────────────────────────────
    // The EC counts how many tachometer pulses it sees over a fixed time window
    // rather than reporting RPM directly. Fewer pulses in the window = slower
    // fan = higher count value, so the relationship is INVERSELY proportional:
    //
    //   RPM = TACH_K / tach_count
    //
    // TACH_K is derived from the EC's internal timer:
    //   Timer freq ~500 kHz, 2 pulses per revolution, 16x prescaler:
    //   (500000 / (2 * 16)) * 60 ≈ 937500
    //
    // NOTE: This constant is an educated estimate. To calibrate it accurately,
    // read tach_count at a known RPM (e.g. spin fans up fully, read a hwmon
    // tach if available) and solve: TACH_K = known_RPM * tach_count.
    static constexpr uint32_t TACH_K    = 937500;

    // Any tach count below this value is treated as a stalled or absent fan
    // (the math would give an absurdly large RPM otherwise).
    static constexpr uint8_t  TACH_MIN  = 5;

    // tach_to_rpm(tach_count)
    // -------------------------------------------------------------------------
    // Converts a raw 8-bit tachometer count read from REG_TACH1 or REG_TACH2
    // into a human-readable RPM value.
    //
    // Parameters:
    //   tach_count — raw byte read from the EC tachometer register
    //
    // Returns: RPM as uint16_t, or 0 if the count is below TACH_MIN
    uint16_t tach_to_rpm(uint8_t tach_count);

    // percent_to_pwm(percent)
    // -------------------------------------------------------------------------
    // Converts a user percentage (0–100) to the 0–255 PWM byte the EC expects.
    //   0%   → 0x00  (fans at minimum / off)
    //   100% → 0xFF  (fans at full speed)
    //
    // Parameters:
    //   percent — desired speed percentage [0, 100]; clamped if out of range
    //
    // Returns: PWM byte value [0, 255]
    uint8_t percent_to_pwm(int percent);

    // set_fans_percent(ec, percent)
    // -------------------------------------------------------------------------
    // Writes the PWM register to set both fans to the requested percentage.
    // Takes effect immediately with no mode switch required.
    //
    // Parameters:
    //   ec      — the EC driver instance
    //   percent — target speed percentage [0, 100]
    //
    // Returns: true on success
    bool set_fans_percent(EmbeddedControllerLinux &ec, int percent);

    // read_fan1_rpm / read_fan2_rpm
    // -------------------------------------------------------------------------
    // Read the live tachometer register for each fan and convert to RPM.
    // Returns 0 if the fan appears stalled or the register is unresponsive.
    uint16_t read_fan1_rpm(EmbeddedControllerLinux &ec);
    uint16_t read_fan2_rpm(EmbeddedControllerLinux &ec);

    // read_current_percent(ec)
    // -------------------------------------------------------------------------
    // Reads the current PWM register value and converts it back to an
    // approximate percentage.  Used to infer whether the user has manually
    // set the fans or the EC is still at its idle level.
    //
    // Returns: current PWM as a percentage [0, 100]
    int read_current_percent(EmbeddedControllerLinux &ec);

} // namespace G5KF5
