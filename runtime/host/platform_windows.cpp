#include "platform.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <io.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <utility>

namespace mojorecomp::host {

std::filesystem::path ExecutablePath()
{
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size())
        return {};
    return std::filesystem::path(buffer.data(), buffer.data() + length);
}

uint32_t CurrentThreadId()
{
    return static_cast<uint32_t>(GetCurrentThreadId());
}

void SetCurrentThreadName(std::string_view name)
{
    // Thread names are ASCII literals, so a byte-wise widening is exact.
    std::wstring wide(name.begin(), name.end());
    SetThreadDescription(GetCurrentThread(), wide.c_str());
}

void SetCurrentThreadPriority(ThreadPriority priority)
{
    const int value = priority == ThreadPriority::BelowNormal
        ? THREAD_PRIORITY_BELOW_NORMAL
        : THREAD_PRIORITY_NORMAL;
    SetThreadPriority(GetCurrentThread(), value);
}

int SeekFile64(std::FILE* file, int64_t offset, int origin)
{
    return _fseeki64(file, offset, origin);
}

int64_t TellFile64(std::FILE* file)
{
    return _ftelli64(file);
}

bool ResizeFile(std::FILE* file, int64_t size)
{
    return _chsize_s(_fileno(file), size) == 0;
}

bool SyncFileToStorage(std::FILE* file)
{
    return _commit(_fileno(file)) == 0;
}

bool SetPageAccess(void* address, size_t bytes, PageAccess access) noexcept
{
    const DWORD protection =
        access == PageAccess::ReadOnly ? PAGE_READONLY : PAGE_READWRITE;
    DWORD previous = 0;
    return VirtualProtect(address, bytes, protection, &previous) != FALSE;
}

namespace {

std::atomic<WriteFaultHandler> g_writeFaultHandler{nullptr};
PVOID g_vectoredHandler = nullptr;

LONG CALLBACK VectoredWriteFaultHandler(EXCEPTION_POINTERS* exceptionInfo)
{
    const EXCEPTION_RECORD* record = exceptionInfo ? exceptionInfo->ExceptionRecord : nullptr;
    // ExceptionInformation[0] == 1 marks a write access.
    if (!record || record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        record->NumberParameters < 2 || record->ExceptionInformation[0] != 1)
        return EXCEPTION_CONTINUE_SEARCH;

    const WriteFaultHandler handler = g_writeFaultHandler.load(std::memory_order_acquire);
    if (handler && handler(static_cast<uintptr_t>(record->ExceptionInformation[1])))
        return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

bool InstallWriteFaultHandler(WriteFaultHandler handler)
{
    if (!handler)
        return false;
    if (g_vectoredHandler)
        return g_writeFaultHandler.load(std::memory_order_acquire) == handler;
    g_writeFaultHandler.store(handler, std::memory_order_release);
    g_vectoredHandler = AddVectoredExceptionHandler(1, VectoredWriteFaultHandler);
    if (!g_vectoredHandler)
        g_writeFaultHandler.store(nullptr, std::memory_order_release);
    return g_vectoredHandler != nullptr;
}

void RemoveWriteFaultHandler()
{
    if (g_vectoredHandler)
    {
        RemoveVectoredExceptionHandler(g_vectoredHandler);
        g_vectoredHandler = nullptr;
    }
    g_writeFaultHandler.store(nullptr, std::memory_order_release);
}

namespace {

bool NativeCriticalSectionEnabled()
{
    static const bool enabled = [] {
        const char* value = std::getenv("MOJORECOMP_CS_NATIVE");
        return !value || !*value || std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

} // namespace

struct RecursiveLock::Impl
{
    CRITICAL_SECTION native{};
    std::unique_ptr<std::recursive_mutex> fallback;
    bool useNative = false;
};

RecursiveLock::RecursiveLock() : impl_(std::make_unique<Impl>())
{
    impl_->useNative = NativeCriticalSectionEnabled();
    if (impl_->useNative)
        InitializeCriticalSectionEx(&impl_->native, 4000, 0);
    else
        impl_->fallback = std::make_unique<std::recursive_mutex>();
}

RecursiveLock::~RecursiveLock()
{
    if (impl_->useNative)
        DeleteCriticalSection(&impl_->native);
}

void RecursiveLock::lock()
{
    if (impl_->useNative)
        EnterCriticalSection(&impl_->native);
    else
        impl_->fallback->lock();
}

void RecursiveLock::unlock()
{
    if (impl_->useNative)
        LeaveCriticalSection(&impl_->native);
    else
        impl_->fallback->unlock();
}

std::string SharedLibraryFileName(std::string_view baseName)
{
    return std::string(baseName) + ".dll";
}

DynamicLibrary::~DynamicLibrary()
{
    Close();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr))
{
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
{
    if (this != &other)
    {
        Close();
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

bool DynamicLibrary::Open(const std::filesystem::path& pathOrName)
{
    Close();
    handle_ = LoadLibraryW(pathOrName.c_str());
    return handle_ != nullptr;
}

void DynamicLibrary::Close()
{
    if (handle_)
        FreeLibrary(static_cast<HMODULE>(std::exchange(handle_, nullptr)));
}

void* DynamicLibrary::Symbol(const char* name) const
{
    if (!handle_)
        return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name));
}

} // namespace mojorecomp::host
