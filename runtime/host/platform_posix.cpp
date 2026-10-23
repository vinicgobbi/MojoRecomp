#include "platform.h"

#include <dlfcn.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <system_error>
#include <utility>

namespace mojorecomp::host {
namespace {

// Linux limits thread names to 15 bytes plus the terminator.
constexpr size_t kMaxThreadNameLength = 15;

// Nice value applied for ThreadPriority::BelowNormal. Linux nice values are
// per thread; this is a scheduling hint, not a measured match for Windows'
// THREAD_PRIORITY_BELOW_NORMAL.
constexpr int kBelowNormalNice = 5;

} // namespace

std::filesystem::path ExecutablePath()
{
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : path;
}

uint32_t CurrentThreadId()
{
    // syscall() keeps this working on glibc releases older than gettid().
    return static_cast<uint32_t>(syscall(SYS_gettid));
}

void SetCurrentThreadName(std::string_view name)
{
    std::string truncated(name.substr(0, kMaxThreadNameLength));
    pthread_setname_np(pthread_self(), truncated.c_str());
}

void SetCurrentThreadPriority(ThreadPriority priority)
{
    const int nice = priority == ThreadPriority::BelowNormal ? kBelowNormalNice : 0;
    // With PRIO_PROCESS, a thread id addresses that single thread on Linux.
    setpriority(PRIO_PROCESS, static_cast<id_t>(CurrentThreadId()), nice);
}

int SeekFile64(std::FILE* file, int64_t offset, int origin)
{
    return fseeko(file, static_cast<off_t>(offset), origin);
}

int64_t TellFile64(std::FILE* file)
{
    return static_cast<int64_t>(ftello(file));
}

bool ResizeFile(std::FILE* file, int64_t size)
{
    return ftruncate(fileno(file), static_cast<off_t>(size)) == 0;
}

bool SyncFileToStorage(std::FILE* file)
{
    return fsync(fileno(file)) == 0;
}

std::string SharedLibraryFileName(std::string_view baseName)
{
    return "lib" + std::string(baseName) + ".so";
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
    handle_ = dlopen(pathOrName.c_str(), RTLD_NOW | RTLD_LOCAL);
    return handle_ != nullptr;
}

void DynamicLibrary::Close()
{
    if (handle_)
        dlclose(std::exchange(handle_, nullptr));
}

void* DynamicLibrary::Symbol(const char* name) const
{
    return handle_ ? dlsym(handle_, name) : nullptr;
}

} // namespace mojorecomp::host
