#pragma once

// Host OS primitives that shared runtime code needs. Each host provides one
// implementation file (platform_windows.cpp or platform_posix.cpp), selected by
// CMake, so callers never branch on the operating system themselves.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace mojorecomp::host {

// Full path of the running executable, or an empty path if the host cannot
// report it.
std::filesystem::path ExecutablePath();

// Identifier of the calling OS thread, for logs and per-thread bookkeeping.
uint32_t CurrentThreadId();

// Names the calling thread for debuggers and profilers. Hosts with a short
// name limit truncate it; the name is advisory only.
void SetCurrentThreadName(std::string_view name);

enum class ThreadPriority
{
    Normal,
    BelowNormal,
};

// Scheduling hint for background work on the calling thread. Failure is
// ignored because the hint never affects correctness.
void SetCurrentThreadPriority(ThreadPriority priority);

// 64-bit stdio positioning; same contract as fseek/ftell.
int SeekFile64(std::FILE* file, int64_t offset, int origin);
int64_t TellFile64(std::FILE* file);

// Sets the length of the file behind a stdio stream, extending with zeros.
bool ResizeFile(std::FILE* file, int64_t size);

// Flushes a stdio stream's data through to stable storage. The stream must
// already have been flushed with std::fflush.
bool SyncFileToStorage(std::FILE* file);

// File name of a shared library for this host, for example "dxcompiler" ->
// "dxcompiler.dll" on Windows and "libdxcompiler.so" on Linux.
std::string SharedLibraryFileName(std::string_view baseName);

// Recursive lock backing guest critical sections. Windows uses a native
// CRITICAL_SECTION with a spin count (MOJORECOMP_CS_NATIVE=0 selects
// std::recursive_mutex instead); other hosts use std::recursive_mutex.
// Satisfies the standard Lockable requirements.
class RecursiveLock
{
public:
    RecursiveLock();
    ~RecursiveLock();

    RecursiveLock(const RecursiveLock&) = delete;
    RecursiveLock& operator=(const RecursiveLock&) = delete;

    void lock();
    void unlock();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Owns a dynamically loaded shared library. Move-only; unloads on destruction.
class DynamicLibrary
{
public:
    DynamicLibrary() = default;
    ~DynamicLibrary();

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

    // Loads a library by path or by name through the host's search rules.
    // Returns false and leaves this object empty on failure.
    bool Open(const std::filesystem::path& pathOrName);
    void Close();

    bool IsOpen() const noexcept { return handle_ != nullptr; }
    void* Symbol(const char* name) const;

private:
    void* handle_ = nullptr;
};

} // namespace mojorecomp::host
