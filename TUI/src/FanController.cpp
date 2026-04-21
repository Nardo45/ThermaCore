// =============================================================================
// FanController.cpp
// -----------------------------------------------------------------------------
// Implements poll_fan_state(), process_command(), and command_hint().
// See FanController.hpp for the full explanation of each function.
// =============================================================================

#include "FanController.hpp"

#include <cstdlib>  // strdup, free
#include <cstring>  // strtok
#include <string>
#include <vector>

// =============================================================================
// Internal helper: tokenise
// -----------------------------------------------------------------------------
// Splits a command string on whitespace and returns the tokens as a vector.
// This is used by process_command() to break "set cpu 80" into ["set","cpu","80"].
// =============================================================================
static std::vector<std::string> tokenise(const std::string &input) {
    std::vector<std::string> toks;
    char *cstr = strdup(input.c_str());

    // "token" represents the current chunk of text between spaces/tabs
    char *token = strtok(cstr, " \t");

    while (token != nullptr) {
        toks.emplace_back(token); // Add the found word to the list

        // Find the next chunk
        token = strtok(nullptr, " \t");
    }

    free(cstr);
    return toks;
}

// =============================================================================
// poll_fan_state
// =============================================================================

FanState poll_fan_state(EmbeddedControllerLinux &ec, Machine machine) {
    FanState state;

    switch (machine) {

        // ── Acer Predator Orion 3000 PO3-640 ───────────────────────────────────
        case Machine::ORION: {
            // The Orion has a real hardware mode register, so we can ask the EC
            // directly whether it is currently in AUTO or MANUAL mode.
            state.is_manual = Orion::is_manual(ec);
            state.mode_str  = state.is_manual ? "MANUAL" : "AUTO";

            state.fans.push_back({ "CPU",   Orion::read_cpu_rpm(ec),   Orion::MIN_RPM, Orion::CPU_MAX_RPM  });
            state.fans.push_back({ "Front", Orion::read_front_rpm(ec), Orion::MIN_RPM, Orion::CASE_MAX_RPM });
            state.fans.push_back({ "Back",  Orion::read_back_rpm(ec),  Orion::MIN_RPM, Orion::CASE_MAX_RPM });
            break;
        }

        // ── Gigabyte G5 KF5 ────────────────────────────────────────────────────
        case Machine::G5KF5: {
            // The G5 KF5 has no mode register.  We infer the mode from the current
            // PWM value: the EC's own idle curve tends to sit around 25–30%, so we
            // treat anything above 35% as a sign the user has manually set the fans.
            // This is a heuristic — it will show MANUAL right after boot if the EC
            // happens to ramp above 35% due to thermals.
            int cur_pct     = G5KF5::read_current_percent(ec);
            state.is_manual = (cur_pct > 35);
            state.mode_str  = state.is_manual ? "MANUAL" : "AUTO";

            // Display range 0–5000 is approximate; G5 KF5 fans don't have a
            // documented hard max RPM.  Adjust once real measurements are known.
            state.fans.push_back({ "Fan 1", G5KF5::read_fan1_rpm(ec), 0, 5000 });
            state.fans.push_back({ "Fan 2", G5KF5::read_fan2_rpm(ec), 0, 5000 });
            break;
        }
    }

    return state;
}

// =============================================================================
// process_command
// =============================================================================

void process_command(EmbeddedControllerLinux &ec,
                     Machine machine,
                     const std::string &input,
                     volatile int *stop_flag)
{
    auto toks = tokenise(input);
    if (toks.empty()) return;

    const std::string &cmd = toks[0];

    // ── Universal commands (work on every machine) ───────────────────────────
    if (cmd == "quit" || cmd == "q" || cmd == "exit") {
        if (stop_flag) *stop_flag = 1;
        return;
    }

    // ── Machine-specific commands ────────────────────────────────────────────
    switch (machine) {

        case Machine::ORION: {
            if (cmd == "auto") {
                Orion::set_mode_auto(ec);
            }
            else if (cmd == "manual") {
                Orion::set_mode_manual(ec);
            }
            else if (cmd == "set" && toks.size() >= 3) {
                int pct = 0;
                try { pct = std::stoi(toks[2]); } catch (...) { return; }
                Orion::set_fan_percent(ec, toks[1], pct);
            }
            break;
        }

        case Machine::G5KF5: {
            if (cmd == "auto") {
                // No mode register to flip.
                G5KF5::set_fans_percent(ec, 30);
            }
            else if (cmd == "manual") {
                // Nothing to do. The G5 KF5 is always "manual" in the sense that
                // whatever PWM we write sticks immediately.  The command is accepted
                // silently so users coming from the Orion aren't confused.
            }
            else if (cmd == "set" && toks.size() >= 2) {
                int pct = 0;
                try { pct = std::stoi(toks[1]); } catch (...) { return; }
                G5KF5::set_fans_percent(ec, pct);
            }
            break;
        }
    }
}

// =============================================================================
// command_hint
// =============================================================================

const char *command_hint(Machine machine) {
    switch (machine) {
        case Machine::ORION:
            return "auto, manual, set <cpu|front|back> <%%>, q";
        case Machine::G5KF5:
            return "auto, set <%%>, q";
    }
    return "q";
}
