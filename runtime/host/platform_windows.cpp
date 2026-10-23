#include "platform.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <io.h>

#include <array>
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
