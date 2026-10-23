#pragma once

// Split between the POSIX crash reporter (host side) and the guest details it
// includes. crash_report_guest.cpp implements the guest side for the runtime;
// tests link their own definitions so the reporter needs no guest image.
// Everything here runs inside the crash signal handler.

#include <cstddef>
#include <cstdint>

namespace mojorecomp::host::crash_report {

// Host side, valid only while a report is being written.
// Appends one formatted line to the crash log and to stderr.
void Write(const char* format, ...) noexcept __attribute__((format(printf, 1, 2)));
// Copies memory that may be unmapped or corrupt; returns false instead of
// faulting.
bool SafeRead(void* destination, const void* source, size_t bytes) noexcept;

// Guest side.
uint64_t GuestFrameCount() noexcept;
bool ExtendedInfoEnabled() noexcept;
// Writes the guest lines of the report: last indirect call and the crashing
// thread's PPC context.
void WriteGuestState(bool extended) noexcept;

} // namespace mojorecomp::host::crash_report
