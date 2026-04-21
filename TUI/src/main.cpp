// =============================================================================
// main.cpp  —  ThermaCore TUI
// -----------------------------------------------------------------------------
// Entry point for the terminal user interface.
//
// This file is intentionally thin.  Its only jobs are:
//   1. Parse the --machine command-line flag
//   2. Initialise the EC driver and hwmon sensor list
//   3. Set up ncurses
//   4. Run the main draw/input loop
//   5. Clean up on exit
//
// All hardware knowledge lives in Global/  (EmbeddedController, MachineProfile,
// HwmonSensor).  All fan-command logic lives in TUI/src/FanController.cpp.
// This file only knows about drawing and keyboard input.
//
// Must be run as root:  sudo ./thermacore-tui [--machine <name>]
//
// Supported --machine values:
//   orion   / po3-640    — Acer Predator Orion 3000 PO3-640  (default)
//   g5kf5   / g5-kf5    — Gigabyte G5 KF5
// =============================================================================

#define _POSIX_C_SOURCE 200809L

#include "EmbeddedController.hpp"
#include "HwmonSensor.hpp"
#include "MachineProfile.hpp"
#include "FanController.hpp"

#include <csignal>   // signal(), SIGINT
#include <cstdlib>   // setenv
#include <cstring>   // strlen
#include <iostream>  // cerr, cout
#include <locale.h>  // setlocale
#include <ncurses.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

using namespace std;

// =============================================================================
// Signal handling
// -----------------------------------------------------------------------------
// Catching SIGINT (Ctrl-C) lets us exit gracefully through the normal loop
// exit path, which ensures endwin() is always called and the terminal is
// restored to a sane state.
// =============================================================================
volatile sig_atomic_t g_stop_flag = 0;
static void sigint_handler(int) { g_stop_flag = 1; }

// =============================================================================
// parse_machine(argc, argv)
// -----------------------------------------------------------------------------
// Looks for a --machine / -m flag in the command-line arguments and returns
// the corresponding Machine enum value.  Exits with an error message if an
// unrecognised name is given.
//
// Defaults to Machine::ORION if no flag is present (backward compatibility).
// =============================================================================
static Machine parse_machine(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if ((arg == "--machine" || arg == "-m") && i + 1 < argc) {
            string m = argv[++i];
            if (m == "orion"  || m == "po3-640") return Machine::ORION;
            if (m == "g5kf5"  || m == "g5-kf5"  || m == "G5KF5") return Machine::G5KF5;
            cerr << "Unknown machine '" << m << "'. Use: orion, g5kf5\n";
            exit(1);
        }
    }
    return Machine::ORION; // Default
}

// =============================================================================
// setup_ncurses()
// -----------------------------------------------------------------------------
// Initialises ncurses and configures it for our TUI:
//   - Raw character input (no line buffering)
//   - No automatic echo (we draw the input buffer ourselves)
//   - Non-blocking getch() so the loop stays responsive
//   - UTF-8 box-drawing characters via setlocale() + NCURSES_NO_UTF8_ACS
//   - A fixed palette of 5 colour pairs used throughout the UI
//
// UTF-8 note: we must link against ncursesw (the wide-character variant of
// ncurses) and set setlocale(LC_ALL, "") BEFORE initscr().  On Ubuntu install
// libncursesw5-dev and link with -lncursesw.  The NCURSES_NO_UTF8_ACS env
// variable prevents ncursesw from remapping UTF-8 box characters to legacy
// terminal codes, which produces garbage on any modern UTF-8 terminal.
// =============================================================================
static void setup_ncurses() {
    // setlocale MUST come before initscr().  It tells the C runtime and
    // ncursesw to interpret all strings as UTF-8.  Without this the
    // multi-byte box-drawing characters (╭ ─ │ ╰ etc.) print as raw bytes.
    setlocale(LC_ALL, "");

    // On some distros (Ubuntu in particular) ncursesw needs this environment
    // hint to stop it mapping UTF-8 box-drawing characters onto the legacy
    // ACS (Alternate Character Set), which produces garbage on modern terminals.
    setenv("NCURSES_NO_UTF8_ACS", "1", 1);

    initscr();
    start_color();
    use_default_colors();  // -1 as background = transparent (uses terminal's bg colour)
    cbreak();              // Pass keypresses to us immediately, don't wait for Enter
    noecho();              // Don't print characters as the user types, we do it manually
    keypad(stdscr, TRUE);  // Enable special keys (backspace, arrows, etc.)
    nodelay(stdscr, TRUE); // getch() returns ERR immediately if no key is pressed
    curs_set(1);           // Show the cursor so users can see where they're typing

    // Colour pair index reference (used throughout the draw functions):
    init_pair(1, COLOR_CYAN,   -1); // Borders, section headers
    init_pair(2, COLOR_GREEN,  -1); // AUTO mode label, input text
    init_pair(3, COLOR_RED,    -1); // MANUAL mode label
    init_pair(4, COLOR_YELLOW, -1); // Temperature values
    init_pair(5, COLOR_WHITE,  -1); // General text, fan RPM values
}

// =============================================================================
// draw_frame(machine, fs, sensors, input_buffer)
// -----------------------------------------------------------------------------
// Erases the screen and redraws the entire TUI from scratch.
// Called every loop iteration (~20 fps).
//
// Layout (rows grow downward):
//   0     ╭─ top border ─╮
//   1     │  Machine name  │
//   2     ├─ divider ──────┤
//   3     │  Control Mode  │
//   4     ├─ divider ──────┤
//   5     │ [Temperatures] │
//   5+N   ├─ divider ──────┤
//   6+N   │ [Fan Speeds]   │
//   ...   │  fan rows      │
//   last  ├─ divider ──────┤
//   last  │  Commands:...  │
//   last  │  > input       │
//   last  ╰─ bottom border ╯
//
// Parameters:
//   machine      — active machine (selects fan rows and command hint)
//   fs           — fan state polled this frame
//   sensors      — hwmon sensor list built at startup
//   input_buffer — the string the user has typed so far
// =============================================================================
static void draw_frame(Machine machine,
                       const FanState &fs,
                       const vector<Sensor> &sensors,
                       const string &input_buffer)
{
    erase(); // Clear the screen; refresh() later will push the new content

    int modeColor = fs.is_manual ? COLOR_PAIR(3) : COLOR_PAIR(2);

    // ── Static chrome: top border, title row, mode row ──────────────────────
    attron(COLOR_PAIR(1));
    mvprintw(0, 0, "╭────────────────────────────────────────────────────────────╮");
    mvprintw(1, 0, "│                                                            │");
    mvprintw(2, 0, "├────────────────────────────────────────────────────────────┤");
    mvprintw(3, 0, "│"); mvprintw(3, 61, "│");
    mvprintw(4, 0, "├────────────────────────────────────────────────────────────┤");
    attroff(COLOR_PAIR(1));

    // Machine name centred in the title row
    {
        const char *name = (machine == Machine::ORION)
        ? "Acer Predator Orion 3000 PO3-640"
        : "Gigabyte G5 KF5";
        int col = (62 - static_cast<int>(strlen(name))) / 2;
        attron(COLOR_PAIR(5) | A_BOLD);
        mvprintw(1, col, "%s", name);
        attroff(COLOR_PAIR(5) | A_BOLD);
    }

    // Control mode: label in cyan, value in green (AUTO) or red (MANUAL)
    attron(COLOR_PAIR(1));
    mvprintw(3, 3, "Control Mode: ");
    attroff(COLOR_PAIR(1));
    attron(modeColor | A_BOLD);
    mvprintw(3, 17, "%s", fs.mode_str.c_str());
    attroff(modeColor | A_BOLD);

    // ── Temperature section ──────────────────────────────────────────────────
    int row = 5;
    attron(COLOR_PAIR(1));
    mvprintw(row++, 0, "│  [ System Temperatures ]                                   │");
    attroff(COLOR_PAIR(1));

    // Print sensors two per row using column offsets
    attron(COLOR_PAIR(4));
    for (size_t i = 0; i < sensors.size(); i++) {
        int col = (i % 2 == 0) ? 4 : 34;
        mvprintw(row, col, "%-14s : %4.1f °C",
                 sensors[i].display_name.c_str(),
                 read_temp(sensors[i].path));
        if (i % 2 != 0) row++; // Advance row after filling both columns
    }
    if (sensors.size() % 2 != 0) row++; // Odd number of sensors: advance the last row
    attroff(COLOR_PAIR(4));

    // ── Fan speed section ────────────────────────────────────────────────────
    attron(COLOR_PAIR(1));
    mvprintw(row++, 0, "├────────────────────────────────────────────────────────────┤");
    mvprintw(row++, 0, "│  [ Live Fan Speeds ]                                       │");
    attroff(COLOR_PAIR(1));

    attron(COLOR_PAIR(5));
    for (const auto &fan : fs.fans) {
        mvprintw(row++, 0,
                 "│  - %-6s %4d RPM   [ Range: %4d - %4d RPM ]            │",
                 (fan.label + ":").c_str(),
                 fan.rpm,
                 fan.min_rpm,
                 fan.max_rpm);
    }
    attroff(COLOR_PAIR(5));

    // ── Command bar and input line ───────────────────────────────────────────
    attron(COLOR_PAIR(1));
    mvprintw(row++, 0, "├────────────────────────────────────────────────────────────┤");
    mvprintw(row++, 0, "│  Commands: %-48s│", command_hint(machine));
    mvprintw(row,   0, "│  > ");
    mvprintw(row+1, 0, "╰────────────────────────────────────────────────────────────╯");
    attroff(COLOR_PAIR(1));

    // Input buffer in green so it's visually distinct from static text
    attron(COLOR_PAIR(2) | A_BOLD);
    mvprintw(row, 5, "%s", input_buffer.c_str());
    attroff(COLOR_PAIR(2) | A_BOLD);

    // Place the hardware cursor at the end of what the user has typed
    move(row, 5 + static_cast<int>(input_buffer.length()));
    refresh();
}

// =============================================================================
// main
// =============================================================================

int main(int argc, char *argv[]) {
    signal(SIGINT, sigint_handler);

    // ── Startup ──────────────────────────────────────────────────────────────
    Machine machine = parse_machine(argc, argv);

    EmbeddedControllerLinux ec;
    if (!ec.initialized) {
        cerr << "ERROR: Failed to access I/O ports. Did you run as root using sudo?\n";
        return 1;
    }

    // Scan hwmon once at startup; the list doesn't change while we're running
    vector<Sensor> sensors;
    scan_hwmon(sensors);

    setup_ncurses();

    string input_buffer;

    // ── Main loop ─────────────────────────────────────────────────────────────
    // Each iteration: poll EC → redraw screen → handle any keypress → sleep
    while (!g_stop_flag) {

        FanState fs = poll_fan_state(ec, machine);
        draw_frame(machine, fs, sensors, input_buffer);

        // Handle keyboard input.  getch() returns ERR immediately if no key
        // is waiting (because we set nodelay above).
        int ch = getch();
        if (ch != ERR) {
            if (ch == '\n' || ch == '\r') {
                // User pressed Enter: execute the buffered command then clear it
                process_command(ec, machine, input_buffer,
                                const_cast<volatile int*>(&g_stop_flag));
                input_buffer.clear();
            }
            else if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
                if (!input_buffer.empty()) input_buffer.pop_back();
            }
            else if (isprint(ch)) {
                input_buffer += static_cast<char>(ch);
            }
        }

        // Sleep 50 ms between frames (~20 fps).  Without this the loop would
        // consume 100% of a CPU core doing nothing useful.
        this_thread::sleep_for(chrono::milliseconds(50));
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    // endwin() restores the terminal to its original state (echo, cursor, etc.)
    // This must always be called before exiting, even on error paths.
    endwin();
    cout << "Fan control exited. Have a great day!\n";
    return 0;
}
