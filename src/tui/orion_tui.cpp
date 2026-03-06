// orion_tui.cpp
// TUI Fan Controller for Acer Predator Orion 3000 PO3-640
// Must be run as root to access I/O ports.

#define _POSIX_C_SOURCE 200809L
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/io.h>
#include <chrono>
#include <thread>
#include <vector>
#include <map>
#include <csignal>
#include <cmath>
#include <fstream>
#include <string>
#include <filesystem>
#include <ncurses.h>
#include <locale.h>

using namespace std;

// EC Constants
static const uint8_t EC_OBF = 0x01;
static const uint8_t EC_IBF = 0x02;
static const uint8_t EC_DATA = 0x62;
static const uint8_t EC_SC = 0x66;
static const uint8_t RD_EC = 0x80;
static const uint8_t WR_EC = 0x81;

volatile sig_atomic_t g_stop_flag = 0;
static void sigint_handler(int) { g_stop_flag = 1; }

// Core EC Logic
struct EmbeddedControllerLinux {
    uint8_t scPort = EC_SC;
    uint8_t dataPort = EC_DATA;
    uint8_t endianness = 1; // BIG_ENDIAN match
    unsigned retry = 5;
    unsigned timeout = 100;
    bool initialized = false;

    EmbeddedControllerLinux() {
        if (ioperm(scPort, 1, 1) != 0 || ioperm(dataPort, 1, 1) != 0) {
            initialized = false;
        } else {
            initialized = true;
        }
    }

    ~EmbeddedControllerLinux() {
        ioperm(scPort, 1, 0);
        ioperm(dataPort, 1, 0);
    }

    static void sleep_us(unsigned int usec){
        std::this_thread::sleep_for(std::chrono::microseconds(usec));
    }

    bool status(uint8_t flag) {
        bool wait_for_obf = (flag == EC_OBF);
        for (unsigned i = 0; i < timeout; ++i) {
            uint8_t result = inb(scPort);
            if (wait_for_obf) {
                if ((result & EC_OBF) != 0) return true;
            } else {
                if ((result & EC_IBF) == 0) return true;
            }
            sleep_us(50);
        }
        return false;
    }

    bool operation(uint8_t mode, uint8_t reg, uint8_t *value) {
        bool isRead = (mode == 0);
        uint8_t opType = isRead ? RD_EC : WR_EC;

        for (unsigned i = 0; i < retry; ++i) {
            if (!status(EC_IBF)) continue;
            outb(opType, scPort);
            if (!status(EC_IBF)) continue;
            outb(reg, dataPort);
            if (!status(EC_IBF)) continue;
            if (isRead) {
                if (!status(EC_OBF)) continue;
                *value = inb(dataPort);
                return true;
            } else {
                outb(*value, dataPort);
                return true;
            }
        }
        return false;
    }

    uint8_t readByte(uint8_t reg) {
        uint8_t val = 0;
        operation(0, reg, &val);
        return val;
    }

    bool writeByte(uint8_t reg, uint8_t v) {
        uint8_t tmp = v;
        return operation(1, reg, &tmp);
    }

    uint16_t readWord(uint8_t reg) {
        uint8_t b1 = 0, b2 = 0;
        uint16_t res = 0;
        if (operation(0, reg, &b1) && operation(0, reg + 1, &b2)) {
            if (endianness == 1) std::swap(b1, b2);
            res = (uint16_t)b1 | ((uint16_t)b2 << 8);
        }
        return res;
    }

    bool writeWord(uint8_t reg, uint16_t v) {
        uint8_t b1 = v & 0xFF;
        uint8_t b2 = (v >> 8) & 0xFF;
        if (endianness == 1) std::swap(b1, b2);
        return (operation(1, reg, &b1) && operation(1, reg + 1, &b2));
    }
};

// Fan mapping logic
static uint16_t percent_to_rpm(int percent, uint16_t minv, uint16_t maxv) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    double diff = (double)(maxv - minv);
    int offset = (int)round(diff * (percent / 100.0));
    return (uint16_t)(minv + offset);
}

// Hardware Monitor (hwmon) Structs & Logic
struct Sensor {
    string display_name;
    string path;
};
vector<Sensor> system_sensors;

void scan_hwmon() {
    int cpu_count = 1;
    int nvme_count = 1;

    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/hwmon")) {
        string dir = entry.path().string();
        ifstream name_file(dir + "/name");
        string chip_name;

        if (name_file >> chip_name) {
            for (const auto& file : std::filesystem::directory_iterator(dir)) {
                string filename = file.path().filename().string();
                if (filename.find("temp") == 0 && filename.find("_input") != string::npos) {
                    Sensor s;
                    s.path = file.path().string();

                    // Map vague names to human-readable ones
                    if (chip_name == "coretemp") s.display_name = "CPU Core " + to_string(cpu_count++);
                    else if (chip_name == "nvme") s.display_name = "NVMe SSD " + to_string(nvme_count++);
                    else if (chip_name == "iwlwifi_1") s.display_name = "Wi-Fi Module";
                    else if (chip_name == "acpitz") s.display_name = "Motherboard";
                    else s.display_name = chip_name + " " + filename; // Fallback

                    system_sensors.push_back(s);
                }
            }
        }
    }
}

double read_temp(const string& path) {
    ifstream tempFile(path);
    if (tempFile.is_open()) {
        double millidegrees;
        tempFile >> millidegrees;
        return millidegrees / 1000.0;
    }
    return 0.0;
}

// Global Variables for Fan Control
map<string, uint8_t> fan_write_reg = { {"cpu", 0xF0}, {"front", 0xF2}, {"back", 0xF6} };
map<string, uint16_t> fan_max_rpm  = { {"cpu", 4700}, {"front", 3500}, {"back", 3500} };
const uint16_t MIN_RPM = 900;

// Process User Input string
void process_command(EmbeddedControllerLinux &ec, const string& input) {
    vector<string> toks;
    char *cstr = strdup(input.c_str());
    char *p = strtok(cstr, " \t");
    while (p) { toks.emplace_back(p); p = strtok(NULL, " \t"); }
    free(cstr);

    if (toks.empty()) return;
    string cmd = toks[0];

    if (cmd == "quit" || cmd == "q" || cmd == "exit") {
        g_stop_flag = 1;
    }
    else if (cmd == "auto") {
        ec.writeByte(0x1F, 0x00);
    }
    else if (cmd == "manual") {
        ec.writeByte(0x1F, 0x01);
    }
    else if (cmd == "set" && toks.size() >= 3) {
        string target_fan = toks[1];
        int pct = 0;
        try { pct = stoi(toks[2]); } catch (...) { return; }

        if (fan_write_reg.count(target_fan)) {
            ec.writeByte(0x1F, 0x01); // Force manual mode on set
            uint8_t reg = fan_write_reg[target_fan];
            uint16_t max_rpm = fan_max_rpm[target_fan];
            uint16_t target_rpm = percent_to_rpm(pct, MIN_RPM, max_rpm);
            ec.writeWord(reg, target_rpm);
        }
    }
}

int main() {
    signal(SIGINT, sigint_handler);

    // Initializations
    EmbeddedControllerLinux ec;
    if (!ec.initialized) {
        cerr << "ERROR: Failed to access I/O ports. Did you run as root using sudo?\n";
        return 1;
    }

    scan_hwmon();
    setlocale(LC_ALL, ""); // Ensure UTF-8 box drawing characters work

    // Ncurses Setup
    initscr();
    start_color();
    use_default_colors(); // Keep terminal background transparent
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE); // Non-blocking getch
    curs_set(1); // Show cursor for the input box

    // Color Pairs
    init_pair(1, COLOR_CYAN, -1);   // Borders and headers
    init_pair(2, COLOR_GREEN, -1);  // Auto / Good status
    init_pair(3, COLOR_RED, -1);    // Manual / Alerts
    init_pair(4, COLOR_YELLOW, -1); // Temperatures
    init_pair(5, COLOR_WHITE, -1);  // General text

    string input_buffer = "";

    // Main Loop
    while (!g_stop_flag) {
        erase(); // Clear screen for fresh paint

        // --- Fetch Live Data ---
        uint8_t modeReg = ec.readByte(0x1F);
        bool isManual = (modeReg == 0x01);
        string modeStr = isManual ? "MANUAL" : "AUTO";
        int modeColor = isManual ? COLOR_PAIR(3) : COLOR_PAIR(2);

        uint16_t cpu_rpm = ec.readWord(0x14);
        uint16_t front_rpm = ec.readWord(0x16);
        uint16_t back_rpm = ec.readWord(0x1A);

        // --- Draw UI ---
        attron(COLOR_PAIR(1));
        mvprintw(0, 0, "╭────────────────────────────────────────────────────────────╮");
        mvprintw(1, 0, "│                                                            │");
        mvprintw(2, 0, "├────────────────────────────────────────────────────────────┤");
        mvprintw(3, 0, "│"); mvprintw(3, 61, "│");
        mvprintw(4, 0, "├────────────────────────────────────────────────────────────┤");
        attroff(COLOR_PAIR(1));

        attron(COLOR_PAIR(5) | A_BOLD);
        mvprintw(1, 14, "Acer Predator Orion 3000 PO3-640");
        attroff(COLOR_PAIR(5) | A_BOLD);

        attron(COLOR_PAIR(1));
        mvprintw(3, 3, "Control Mode: ");
        attroff(COLOR_PAIR(1));
        attron(modeColor | A_BOLD);
        mvprintw(3, 17, "%s", modeStr.c_str());
        attroff(modeColor | A_BOLD);

        // Draw Live Temps Section
        int row = 5;
        attron(COLOR_PAIR(1));
        mvprintw(row++, 0, "│  [ System Temperatures ]                                   │");
        attroff(COLOR_PAIR(1));

        attron(COLOR_PAIR(4));
        for (size_t i = 0; i < system_sensors.size(); i++) {
            int col = (i % 2 == 0) ? 4 : 34; // Two columns
            mvprintw(row, col, "%-14s : %4.1f °C", system_sensors[i].display_name.c_str(), read_temp(system_sensors[i].path));
            if (i % 2 != 0) row++;
        }
        if (system_sensors.size() % 2 != 0) row++;
        attroff(COLOR_PAIR(4));

        attron(COLOR_PAIR(1));
        mvprintw(row++, 0, "├────────────────────────────────────────────────────────────┤");
        mvprintw(row++, 0, "│  [ Live Fan Speeds ]                                       │");
        attroff(COLOR_PAIR(1));

        // Draw Fan Data
        attron(COLOR_PAIR(5));
        mvprintw(row++, 0, "│  - CPU:   %4d RPM   [ Range:  900 - 4700 RPM ]            │", cpu_rpm);
        mvprintw(row++, 0, "│  - Front: %4d RPM   [ Range:  900 - 3500 RPM ]            │", front_rpm);
        mvprintw(row++, 0, "│  - Back:  %4d RPM   [ Range:  900 - 3500 RPM ]            │", back_rpm);
        attroff(COLOR_PAIR(5));

        attron(COLOR_PAIR(1));
        mvprintw(row++, 0, "├────────────────────────────────────────────────────────────┤");
        mvprintw(row++, 0, "│  Commands: auto, manual, set <cpu|front|back> <%%>, q       │");
        mvprintw(row,   0, "│  > ");
        mvprintw(row+1, 0, "╰────────────────────────────────────────────────────────────╯");
        attroff(COLOR_PAIR(1));

        // Draw Input Buffer
        attron(COLOR_PAIR(2) | A_BOLD);
        mvprintw(row, 5, "%s", input_buffer.c_str());
        attroff(COLOR_PAIR(2) | A_BOLD);

        // Move cursor to end of user input text
        move(row, 5 + input_buffer.length());
        refresh();

        // --- Input Handling ---
        int ch = getch();
        if (ch != ERR) {
            if (ch == '\n' || ch == '\r') {
                process_command(ec, input_buffer);
                input_buffer.clear();
            }
            else if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
                if (!input_buffer.empty()) input_buffer.pop_back();
            }
            else if (isprint(ch)) {
                input_buffer += (char)ch;
            }
        }

        // Slight sleep to prevent 100% CPU usage on the infinite loop
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Cleanup and Exit
    endwin();
    cout << "Fan control exited. Have a great day!\n";
    return 0;
}
