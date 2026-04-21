#pragma once
// =============================================================================
// FanController.hpp
// -----------------------------------------------------------------------------
// The bridge between the raw machine profiles and the TUI's main loop.
//
// Every iteration of the TUI loop needs to do two things:
//   1. Fetch the latest fan state from the EC (speeds, mode) → FanState
//   2. React to a command the user typed                     → process_command()
//
// This module provides both.  It is TUI-specific because it knows about the
// command language ("set cpu 80", "auto", "q") that only makes sense in an
// interactive terminal context.  A daemon would have its own controller with
// a different interface (e.g. a config file or D-Bus messages).
//
// Used by: TUI only
// =============================================================================

#include "MachineProfile.hpp"
#include "EmbeddedController.hpp"

#include <string>
#include <vector>

// =============================================================================
// FanEntry
// -----------------------------------------------------------------------------
// Describes a single fan as the TUI needs to display it:
//   label    — what to print in the "Live Fan Speeds" section, e.g. "CPU"
//   rpm      — current speed in RPM (0 = stalled / unknown)
//   min_rpm  — lower bound of the display range (e.g. 900 for the Orion)
//   max_rpm  — upper bound of the display range (e.g. 4800 for Orion CPU fan)
// =============================================================================
struct FanEntry {
    std::string label;
    uint16_t    rpm;
    uint16_t    min_rpm;
    uint16_t    max_rpm;
};

// =============================================================================
// FanState
// -----------------------------------------------------------------------------
// A snapshot of the fan subsystem captured once per TUI frame.
// Everything the TUI needs to redraw in the fan section lives here.
// =============================================================================
struct FanState {
    std::string          mode_str;  // "AUTO" or "MANUAL" — shown in the header
    bool                 is_manual; // true → colour mode label red; false → green
    std::vector<FanEntry> fans;     // One entry per fan on this machine
};

// poll_fan_state(ec, machine)
// -----------------------------------------------------------------------------
// Reads all relevant EC registers for the given machine and returns a fully
// populated FanState.  Call this once per TUI frame before redrawing.
//
// Parameters:
//   ec      — the EC driver instance
//   machine — which machine profile to use for register addresses
//
// Returns: a FanState with mode and per-fan RPM data filled in
FanState poll_fan_state(EmbeddedControllerLinux &ec, Machine machine);

// process_command(ec, machine, input, stop_flag)
// -----------------------------------------------------------------------------
// Parses and executes a single command string typed by the user.
//
// Supported commands by machine:
//
//   Both machines:
//     q / quit / exit  — sets *stop_flag = 1 to exit the TUI loop
//
//   Orion:
//     auto             — switch EC back to automatic thermal control
//     manual           — switch EC to manual mode (required before "set")
//     set <fan> <pct>  — set a named fan to a percentage in speed
//                        fan names: cpu, front, back
//
//   G5 KF5:
//     auto             — write ~30% PWM to approximate firmware idle behaviour
//     set <pct>        — set both fans to a percentage in speed via the shared PWM register
//
// Parameters:
//   ec        — the EC driver instance
//   machine   — which machine profile to use
//   input     — the raw command string as typed by the user
//   stop_flag — pointer to the TUI's stop flag; set to 1 on quit commands
void process_command(EmbeddedControllerLinux &ec,
                     Machine machine,
                     const std::string &input,
                     volatile int *stop_flag);

// command_hint(machine)
// -----------------------------------------------------------------------------
// Returns a short string listing the available commands for the given machine.
// Displayed in the TUI footer so the user always knows what to type.
//
// Parameters:
//   machine — the active machine
//
// Returns: a static C string, e.g. "auto, manual, set <cpu|front|back> <%>, q"
const char *command_hint(Machine machine);
