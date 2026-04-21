#pragma once
// =============================================================================
// EmbeddedController.hpp
// -----------------------------------------------------------------------------
// Low-level driver for the ACPI Embedded Controller (EC) accessed through the
// standard x86 I/O port pair:
//
//   0x62  — EC data port  (read/write data here)
//   0x66  — EC status/command port  (send commands here, poll status here)
//
// The EC is a small microcontroller on the motherboard that manages things
// the main CPU doesn't handle directly: fan speeds, thermal sensors, power
// buttons, LEDs, etc.  On Linux we reach it by calling ioperm() to get
// permission to use those I/O ports, then using inb()/outb() to talk to it.
//
// This file declares EmbeddedControllerLinux, the single class that wraps
// all of that low-level I/O.  It is kept intentionally hardware-agnostic —
// it knows nothing about fans or temperatures; it only knows how to read and
// write bytes/words at EC register addresses.  Machine-specific knowledge
// lives in MachineProfile.hpp.
//
// Used by: TUI, GUI, Daemon, Debugger
// =============================================================================

#include <cstdint>  // uint8_t, uint16_t, etc.

// -----------------------------------------------------------------------------
// EC port and protocol constants
// These come from the ACPI specification for the Embedded Controller interface.
// -----------------------------------------------------------------------------

// Status register flags (read from the command/status port 0x66):
static constexpr uint8_t EC_OBF  = 0x01; // Output Buffer Full — data is ready to be read from 0x62
static constexpr uint8_t EC_IBF  = 0x02; // Input Buffer Full  — EC is still processing; wait before writing

// Port addresses:
static constexpr uint8_t EC_DATA = 0x62; // Data port: read results / write values here
static constexpr uint8_t EC_SC   = 0x66; // Status/Command port: send commands here, read status here

// Command bytes sent to EC_SC to start an operation:
static constexpr uint8_t RD_EC   = 0x80; // Tell the EC we want to READ  a register
static constexpr uint8_t WR_EC   = 0x81; // Tell the EC we want to WRITE a register

// =============================================================================
// EmbeddedControllerLinux
// -----------------------------------------------------------------------------
// Wraps ioperm() + inb()/outb() into safe, retrying read/write helpers.
//
// Construction requests I/O port access from the kernel (requires root).
// Destruction releases that access. If ioperm() fails (not root, or ports
// already locked), initialized will be false — callers must check this before
// proceeding.
// =============================================================================
struct EmbeddedControllerLinux {

    // ── Configuration ─────────────────────────────────────────────────────────
    uint8_t  scPort     = EC_SC;   // Status/command port address (almost always 0x66)
    uint8_t  dataPort   = EC_DATA; // Data port address           (almost always 0x62)
    uint8_t  endianness = 1;       // 1 = big-endian byte order for 16-bit read/writes
    unsigned retry      = 5;       // How many times to retry a failed operation
    unsigned timeout    = 100;     // How many 50 µs polls to wait for IBF/OBF

    // Set to true by the constructor if ioperm() succeeded.
    // Always check this before calling any read/write function.
    bool initialized = false;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    // Constructor: calls ioperm() to request access to the EC ports.
    // Requires root privileges. Sets initialized = false on failure.
    EmbeddedControllerLinux();

    // Destructor: releases ioperm() on both ports.
    ~EmbeddedControllerLinux();

    // ── Public read / write API ───────────────────────────────────────────────

    // readByte(reg)
    //   Reads a single 8-bit value from EC register 'reg'.
    //   Returns the byte value, or 0 on failure.
    uint8_t  readByte(uint8_t reg);

    // writeByte(Reg, value)
    //   Writes a single 8-bit value to EC register 'reg'.
    //   Returns true on success, false if the EC never became ready.
    bool     writeByte(uint8_t reg, uint8_t value);

    // readWord(reg)
    //   Reads two consecutive 8-bit registers (reg, reg+1) and combines them
    //   into a 16-bit value. Byte order is determined by 'endianness'.
    //   Returns the word value, or 0 on failure.
    uint16_t readWord(uint8_t reg);

    // writeWord(reg, value)
    //   Splits a 16-bit value into two bytes respecting 'endianness' and writes
    //   them to registers reg and reg+1.
    //   Returns true only if both byte writes succeed.
    bool     writeWord(uint8_t reg, uint16_t value);

private:
    // sleep_us(usec)
    //   Sleeps for 'usec' microseconds. Used between IBF/OBF poll attempts
    //   so we don't burn 100% CPU waiting on the EC.
    static void sleep_us(unsigned usec);

    // status(flag)
    //   Polls the EC status port until the requested flag condition is met:
    //     EC_OBF — waits until the output buffer IS     full (data ready to read)
    //     EC_IBF — waits until the input  buffer IS NOT full (EC ready to accept)
    //   Polls up to 'timeout' times with 50 µs between each attempt.
    //   Returns true if the condition was met, false if we timed out.
    bool status(uint8_t flag);

    // operation(mode, reg, value)
    //   The single internal function that implements both reads and writes.
    //   mode == 0 → read:  sends RD_EC, sends reg, reads result into *value
    //   mode == 1 → write: sends WR_EC, sends reg, writes *value
    // Retries up to 'retry' times if the EC goes unresponsive mid-transaction.
    // Returns true on success.
    bool operation(uint8_t mode, uint8_t reg, uint8_t *value);
};
