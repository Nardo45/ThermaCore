// =============================================================================
// EmbeddedController.cpp
// -----------------------------------------------------------------------------
// Implementation of EmbeddedControllerLinux.
// See EmbeddedController.hpp for the full explanation of what this does and why.
// =============================================================================

#include "EmbeddedController.hpp"

#include <sys/io.h>    // ioperm(), inb(), outb()
#include <algorithm>   // std::swap
#include <chrono>      // std::chrono::microseconds
#include <thread>      // std::this_thread::sleep_for

// Lifecycle

EmbeddedControllerLinux::EmbeddedControllerLinux() {
    // ioperm(port, num_ports, enable)
    // Asks the kernel to allow this process to access the given I/O ports.
    // We need both the data port (0x62) and the status/command port (0x66).
    // This call will fail if the process is not running as root.
    if (ioperm(scPort, 1, 1) != 0 || ioperm(dataPort, 1, 1) != 0) {
        initialized = false;
    } else {
        initialized = true;
    }
}

EmbeddedControllerLinux::~EmbeddedControllerLinux() {
    // Release our port permissions when the object is destroyed.
    // The third argument 0 means "revoke access".
    ioperm(scPort,   1, 0);
    ioperm(dataPort, 1, 0);
}

// Private helpers

void EmbeddedControllerLinux::sleep_us(unsigned usec) {
    std::this_thread::sleep_for(std::chrono::microseconds(usec));
}

bool EmbeddedControllerLinux::status(uint8_t flag) {
    // We're waiting for one of two things:
    //   EC_OBF — output buffer full:  EC has put data in the data port for us to read
    //   EC_IBF — input  buffer full:  EC is still busy; we must wait before writing
    bool wait_for_obf = (flag == EC_OBF);

    for (unsigned i = 0; i < timeout; ++i) {
        uint8_t s = inb(scPort); // Read the status register

        if (wait_for_obf) {
            if (s & EC_OBF) return true; // Data is ready
        } else {
            if (!(s & EC_IBF)) return true; // EC is ready to accept input
        }

        sleep_us(50); // Wait 50 µs before trying again
    }

    return false; // Timed out
}

bool EmbeddedControllerLinux::operation(uint8_t mode, uint8_t reg, uint8_t *value) {
    bool isRead = (mode == 0);
    uint8_t cmd = isRead ? RD_EC : WR_EC;

    // The EC transaction protocol (from the ACPI spec) is:
    //   1. Wait until the EC is ready to accept input (IBF clear)
    //   2. Send the command byte (read or write) to the command port
    //   3. Wait until EC is ready again
    //   4. Send the register address to the data port
    //   5a. READ:  Wait for the EC to put its answer in the data port (OBF set), then read it
    //   5b. WRITE: Wait for the EC to be ready, then write the value to the data port

    for (unsigned i = 0; i < retry; ++i) {
        if (!status(EC_IBF)) continue;    // 1: EC must be ready
	outb(cmd, scPort);                // 2: Send read or write command
	if (!status(EC_IBF)) continue;    // 3: Wait for EC to process command
	outb(reg, dataPort);              // 4: Send the register address
	if (!status(EC_IBF)) continue;    // Wait again before the data phase

        if (isRead) {
            if (!status(EC_OBF)) continue;    // 5a: Wait for data to be ready
	    *value = inb(dataPort);           // Read the result byte
	    return true;
        } else {
            outb(*value, dataPort);           // 5b: Write our value
	    return true;
        }
    }

    return false; // All retries exhausted
}

// Public API

uint8_t EmbeddedControllerLinux::readByte(uint8_t reg) {
    uint8_t val = 0;
    operation(0, reg, &val); // mode 0 = read
    return val;
}

bool EmbeddedControllerLinux::writeByte(uint8_t reg, uint8_t value) {
    uint8_t tmp = value;
    return operation(1, reg, &tmp); // mode 1 = write
}

uint16_t EmbeddedControllerLinux::readWord(uint8_t reg) {
    uint8_t  b1 = 0, b2 = 0; // byte 1 & byte 2
    uint16_t res = 0; // result

    // Read two consecutive registers: reg holds the first byte, reg+1 the second.
    if (operation(0, reg, &b1) && operation(0, reg + 1, &b2)) {
        // The EC stores 16-bit values in big-endian order (high byte first),
        // but x86 is little-endian, so we swap the bytes before assembling
        // the final value.
        if (endianness == 1) std::swap(b1, b2);
        res = static_cast<uint16_t>(b1) | (static_cast<uint16_t>(b2) << 8);
    }

    return res;
}

bool EmbeddedControllerLinux::writeWord(uint8_t reg, uint16_t value) {
    uint8_t b1 = value & 0xFF;          // Low byte
    uint8_t b2 = (value >> 8) & 0xFF;   // High byte

    // Swap for big-endian storage before writing
    if (endianness == 1) std::swap(b1, b2);

    return operation(1, reg, &b1) && operation(1, reg + 1, &b2);
}
