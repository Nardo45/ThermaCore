// fanctl_embedded.cpp
// Linux-native Embedded Controller tool modeled after ECLibrary/ec.cpp
// Compile:
//   g++ fanctl_embedded.cpp -o fanctl_emb -O2
// Run (on host as root):
//   sudo ./fanctl_emb
//
// Commands in program:
//   monitor                   - read 3 fan RPM regs once
//   loop <ms>                 - monitor every ms (ctrl-c to stop)
//   set <cpu|front|back> <percent>  - set fan percent (30..100 -> RPM target written as 16-bit BE)
//   pwm <reg_hex> <value>     - write raw 0..255 to register (hex reg)
//   writew <reg_hex> <val>    - write 16-bit word (uses BIG_ENDIAN)
//   readw <reg_hex>           - read 16-bit word and print
//   dump                      - dump 0x00..0xFF
//   hold <ms> all <pct>       - continuously write <pct> (percentage -> RPM target) to all fans every <ms> ms
//   hold <ms> cpu front back  - continuously write cpu/front/back percents (use - to skip) every <ms> ms
//   quit
//
// WARNING: EC registers control low-level hardware. Use caution.

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

using namespace std;

static const uint8_t EC_OBF = 0x01;  // Output Buffer Full
static const uint8_t EC_IBF = 0x02;  // Input Buffer Full
static const uint8_t EC_DATA = 0x62; // Data Port
static const uint8_t EC_SC = 0x66;   // Status/Command Port
static const uint8_t RD_EC = 0x80;   // Read Embedded Controller
static const uint8_t WR_EC = 0x81;   // Write Embedded Controller

volatile sig_atomic_t g_stop_flag = 0;
static void sigint_handler(int) { g_stop_flag = 1; }

struct EmbeddedControllerLinux {
    uint8_t scPort = EC_SC;
    uint8_t dataPort = EC_DATA;
    uint8_t endianness = 1; // 1 => BIG_ENDIAN (match ECLibrary's usage)
    unsigned retry = 5;
    unsigned timeout = 100; // status poll loops
    bool initialized = false;

    EmbeddedControllerLinux() {
        if (ioperm(scPort, 1, 1) != 0 || ioperm(dataPort, 1, 1) != 0) {
            perror("ioperm");
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

    void wait_input_empty() {
        for (unsigned guard = 0; guard < 50000; ++guard) {
            if ((inb(scPort) & EC_IBF) == 0) break;
            sleep_us(100);
        }
    }

    bool operation(uint8_t mode, uint8_t reg, uint8_t *value) {
        bool isRead = (mode == 0);
        uint8_t opType = isRead ? RD_EC : WR_EC;

        for (unsigned i = 0; i < retry; ++i) {
            // wait IBF clear
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

    uint16_t readWord(uint8_t reg) {
        uint8_t b1 = 0, b2 = 0;
        uint16_t res = 0;
        if (operation(0, reg, &b1) && operation(0, reg + 1, &b2)) {
            if (endianness == 1) { // BIG_ENDIAN: swap to match ECLibrary
                std::swap(b1, b2);
            }
            res = (uint16_t)b1 | ((uint16_t)b2 << 8);
        }
        return res;
    }

    bool writeByte(uint8_t reg, uint8_t v) {
        uint8_t tmp = v;
        return operation(1, reg, &tmp);
    }

    bool writeWord(uint8_t reg, uint16_t v) {
        uint8_t b1 = v & 0xFF;
        uint8_t b2 = (v >> 8) & 0xFF;
        if (endianness == 1) std::swap(b1, b2);
        return (operation(1, reg, &b1) && operation(1, reg + 1, &b2));
    }

    void dumpAll() {
        cout << "EC dump 00-FF:\n";
        for (int row = 0; row < 16; ++row) {
            cout << hex << setw(2) << setfill('0') << (row*16) << ": ";
            for (int col = 0; col < 16; ++col) {
                uint8_t r = readByte(row*16 + col);
                cout << setw(2) << (int)r << " ";
            }
            cout << "\n";
        }
        cout << dec << setfill(' ');
    }
};

// helpers
static int percent_to_pwm(int pct, int minPct = 30) {
    if (pct < minPct) pct = minPct;
    if (pct > 100) pct = 100;
    int pwm = (pct * 255) / 100;
    if (pwm < 0) pwm = 0;
    if (pwm > 255) pwm = 255;
    return pwm;
}

/* Map percent to RPM target using plugin logic:
 * offset = round((max - min) * percent / 100)
 * target = min + offset
 */
static uint16_t percent_to_rpm(int percent, uint16_t minv, uint16_t maxv) {
    if (percent < 30) percent = 30;
    if (percent > 100) percent = 100;
    double diff = (double)(maxv - minv);
    int offset = (int)round(diff * (percent / 100.0));
    int target = (int)minv + offset;
    if (target < 0) target = 0;
    if (target > 0xFFFF) target = 0xFFFF;
    return (uint16_t)target;
}

static void print_rpms(EmbeddedControllerLinux &ec, const vector<uint8_t> &speed_regs) {
    cout << hex << showbase;
    for (size_t i = 0; i < speed_regs.size(); ++i) {
        uint16_t w = ec.readWord(speed_regs[i]);
        cout << dec << nouppercase << (int)w << " (0x" << hex << setw(4) << setfill('0') << w << dec << ")";
        if (i+1 < speed_regs.size()) cout << "\t";
    }
    cout << "\n";
}

int main() {
    signal(SIGINT, sigint_handler);

    EmbeddedControllerLinux ec;
    if (!ec.initialized) {
        cerr << "ERROR: ioperm failed. Run as root on host." << endl;
        return 1;
    }

    cout << "EmbeddedControllerLinux ready. Type 'help' for commands.\n";

    // Data aligned: indices 0=cpu,1=front,2=back
    map<string,uint8_t> fan_write_reg = {
        {"cpu", 0xF0},
        {"front", 0xF2},
        {"back", 0xF6}
    };
    vector<uint8_t> speed_regs = {0x14, 0x16, 0x1A}; // read RPM registers per plugin
    vector<string> fan_keys = {"cpu","front","back"};

    // min/max RPM ranges from the plugin ECLibrary/AcerPlugin values
    vector<uint16_t> min_rpm = {900, 900, 900};
    vector<uint16_t> max_rpm = {4700, 3500, 3500};

    string line;
    while (true) {
        cout << "> " << flush;
        if (!getline(cin, line)) break;
        if (line.empty()) continue;

        // split
        vector<string> toks;
        {
            char *cstr = strdup(line.c_str());
            char *p = strtok(cstr, " \t");
            while (p) { toks.emplace_back(p); p = strtok(NULL, " \t"); }
            free(cstr);
        }
        if (toks.empty()) continue;

        string cmd = toks[0];

        if (cmd == "quit" || cmd == "q" || cmd == "exit") {
            break;
        } else if (cmd == "help") {
            cout << "Commands:\n"
            << "  monitor                   - read 3 fan RPM regs once\n"
            << "  loop <ms>                 - monitor every ms (ctrl-c to stop)\n"
            << "  set <cpu|front|back> <percent>  - set fan percent (30..100 -> RPM target written as 16-bit BE)\n"
            << "  pwm <reg_hex> <value>     - write raw 0..255 to register (hex reg)\n"
            << "  writew <reg_hex> <val>    - write 16-bit word (uses BIG_ENDIAN)\n"
            << "  readw <reg_hex>           - read 16-bit word and print\n"
            << "  dump                      - dump 0x00..0xFF\n"
            << "  hold <ms> all <pct>       - continuously write <pct> (percent->RPM) to all fans every <ms> ms\n"
            << "  hold <ms> cpu front back  - continuously write cpu/front/back percents (use - to skip) every <ms> ms\n"
            << "  quit\n";
            continue;
        } else if (cmd == "monitor") {
            // print RPMs nicely
            for (size_t i = 0; i < speed_regs.size(); ++i) {
                uint16_t w = ec.readWord(speed_regs[i]);
                cout << hex << showbase << (int)speed_regs[i] << dec << nouppercase
                << " -> " << w << " (0x" << hex << setw(4) << setfill('0') << w << dec << ")\t";
            }
            cout << "\n";
            continue;
        } else if (cmd == "loop") {
            int ms = 1000;
            if (toks.size() >= 2) ms = stoi(toks[1]);
            cout << "Press Ctrl-C to stop loop.\n";
            g_stop_flag = 0;
            while (!g_stop_flag) {
                for (size_t i = 0; i < speed_regs.size(); ++i) {
                    uint16_t w = ec.readWord(speed_regs[i]);
                    cout << (i? "\t":"") << dec << (int)w << " ";
                }
                cout << "\r" << flush;
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            }
            cout << "\nLoop stopped.\n";
            g_stop_flag = 0;
            continue;
        } else if (cmd == "set") {
            if (toks.size() < 3) { cerr << "usage: set cpu|front|back <percent>\n"; continue; }
            string who = toks[1];
            int pct = stoi(toks[2]);
            auto it = fan_write_reg.find(who);
            if (it == fan_write_reg.end()) { cerr << "unknown fan\n"; continue; }
            uint8_t reg = it->second;
            // find index for min/max
            int idx = -1;
            for (size_t i = 0; i < fan_keys.size(); ++i) if (fan_keys[i] == who) { idx = (int)i; break; }
            if (idx < 0) { cerr << "internal error\n"; continue; }
            uint16_t target = percent_to_rpm(pct, min_rpm[idx], max_rpm[idx]);
            bool ok = ec.writeWord(reg, target);
            uint16_t rb = ec.readWord(reg);
            cout << (ok? "WROTE ":"FAILED ") << "word reg 0x" << hex << (int)reg << dec << " = " << target
            << " (readback: " << rb << ", 0x" << hex << setw(4) << setfill('0') << rb << dec << ")\n";
            cout << "RPMs: ";
            print_rpms(ec, speed_regs);
            continue;
        } else if (cmd == "pwm") {
            if (toks.size() < 3) { cerr << "usage: pwm <reg_hex> <value>\n"; continue; }
            uint8_t reg = (uint8_t)strtoul(toks[1].c_str(), NULL, 16);
            int val = stoi(toks[2]);
            if (val < 0 || val > 255) { cerr << "value range 0..255\n"; continue; }
            bool ok = ec.writeByte(reg, (uint8_t)val);
            uint8_t rb = ec.readByte(reg);
            cout << (ok? "WROTE ":"FAILED ") << "reg 0x" << hex << (int)reg << dec << " = " << val << " (readback 0x" << hex << (int)rb << dec << ")\n";
            cout << "RPMs: ";
            print_rpms(ec, speed_regs);
            continue;
        } else if (cmd == "dump") {
            ec.dumpAll();
            continue;
        } else if (cmd == "writew") {
            if (toks.size() < 3) { cerr << "usage: writew <reg_hex> <value>\n"; continue; }
            uint8_t reg = (uint8_t)strtoul(toks[1].c_str(), NULL, 16);
            uint16_t v = (uint16_t)strtoul(toks[2].c_str(), NULL, 0);
            bool ok = ec.writeWord(reg, v);
            cout << (ok? "WROTE ":"FAILED ") << "word reg 0x" << hex << (int)reg << dec << " = " << v << "\n";
            continue;
        } else if (cmd == "readw") {
            if (toks.size() < 2) { cerr << "usage: readw <reg_hex>\n"; continue; }
            uint8_t reg = (uint8_t)strtoul(toks[1].c_str(), NULL, 16);
            uint16_t v = ec.readWord(reg);
            cout << "reg 0x" << hex << (int)reg << dec << " -> " << v << " (0x" << hex << setw(4) << setfill('0') << v << dec << ")\n";
            continue;
        } else if (cmd == "hold") {
            // two forms:
            // hold <ms> all <pct>
            // hold <ms> <cpu_pct|-> <front_pct|-> <back_pct|->   (- = skip)
            if (toks.size() < 3) { cerr << "usage: hold <ms> all <pct>   OR   hold <ms> cpu front back (use - to skip)\n"; continue; }
            int ms = stoi(toks[1]);
            vector<int> targets = {-1,-1,-1}; // cpu, front, back
            if (toks[2] == "all") {
                if (toks.size() < 4) { cerr << "usage: hold <ms> all <pct>\n"; continue; }
                int p = stoi(toks[3]);
                targets = {p,p,p};
            } else {
                for (int i = 0; i < 3; ++i) {
                    if ((int)toks.size() > 2+i) {
                        if (toks[2+i] == "-") targets[i] = -1;
                        else targets[i] = stoi(toks[2+i]);
                    }
                }
            }

            // prepare RPM target values (or -1 to skip)
            vector<int> rpm_targets(3, -1);
            for (int i = 0; i < 3; ++i) {
                if (targets[i] >= 0) rpm_targets[i] = percent_to_rpm(targets[i], min_rpm[i], max_rpm[i]);
            }

            cout << "Starting hold loop (Ctrl-C to stop). Interval " << ms << " ms.\n";
            g_stop_flag = 0;
            while (!g_stop_flag) {
                // apply writes (word writes)
                for (int i = 0; i < 3; ++i) {
                    if (rpm_targets[i] >= 0) {
                        uint8_t reg = fan_write_reg[fan_keys[i]];
                        ec.writeWord(reg, (uint16_t)rpm_targets[i]);
                    }
                }
                // small delay to let EC settle a bit before readback
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                // readback target registers and RPMs
                cout << "[HOLD] Target readback: ";
                for (int i = 0; i < 3; ++i) {
                    uint8_t reg = fan_write_reg[fan_keys[i]];
                    uint16_t rb = ec.readWord(reg);
                    cout << fan_keys[i] << "=0x" << hex << setw(4) << setfill('0') << rb << dec;
                    if (i<2) cout << " ";
                }
                cout << "   RPMs: ";
                // show RPMs
                for (size_t i = 0; i < speed_regs.size(); ++i) {
                    uint16_t w = ec.readWord(speed_regs[i]);
                    cout << (i? "\t":"") << dec << (int)w;
                }
                cout << "\n";
                // sleep requested interval (but break early if signalled)
                int slept = 0;
                while (!g_stop_flag && slept < ms) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    slept += 50;
                }
            }
            cout << "\nHold loop stopped by user.\n";
            g_stop_flag = 0;
            continue;
        } else {
            cerr << "Unknown command. type 'help'\n";
            continue;
        }
    }

    cout << "Exiting.\n";
    return 0;
}
