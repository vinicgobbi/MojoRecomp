#include "platform.h"

#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <mutex>
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

bool SetPageAccess(void* address, size_t bytes, PageAccess access) noexcept
{
    const int protection =
        access == PageAccess::ReadOnly ? PROT_READ : PROT_READ | PROT_WRITE;
    return mprotect(address, bytes, protection) == 0;
}

namespace {

std::atomic<WriteFaultHandler> g_writeFaultHandler{nullptr};
struct sigaction g_previousSegvAction{};
bool g_segvHandlerInstalled = false;

bool IsWriteFault(void* context) noexcept
{
#if defined(__x86_64__)
    // Bit 1 of the x86 page-fault error code is set for writes.
    const auto* ucontext = static_cast<const ucontext_t*>(context);
    return (ucontext->uc_mcontext.gregs[REG_ERR] & 0x2) != 0;
#else
#error "Write fault detection is implemented for x86-64 Linux only"
#endif
}

void ForwardToPreviousHandler(int signal, siginfo_t* info, void* context) noexcept
{
    const struct sigaction& previous = g_previousSegvAction;
    if ((previous.sa_flags & SA_SIGINFO) && previous.sa_sigaction)
    {
        previous.sa_sigaction(signal, info, context);
        return;
    }
    if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN)
    {
        previous.sa_handler(signal);
        return;
    }
    // Restore the default action; returning re-executes the faulting
    // instruction, which then terminates the process as an unhandled fault.
    struct sigaction defaultAction{};
    defaultAction.sa_handler = SIG_DFL;
    sigemptyset(&defaultAction.sa_mask);
    sigaction(signal, &defaultAction, nullptr);
}

void SegvHandler(int signal, siginfo_t* info, void* context)
{
    const WriteFaultHandler handler = g_writeFaultHandler.load(std::memory_order_acquire);
    if (handler && info && info->si_code == SEGV_ACCERR && IsWriteFault(context) &&
        handler(reinterpret_cast<uintptr_t>(info->si_addr)))
        return;
    ForwardToPreviousHandler(signal, info, context);
}

} // namespace

bool InstallWriteFaultHandler(WriteFaultHandler handler)
{
    if (!handler)
        return false;
    if (g_segvHandlerInstalled)
        return g_writeFaultHandler.load(std::memory_order_acquire) == handler;

    g_writeFaultHandler.store(handler, std::memory_order_release);
    struct sigaction action{};
    action.sa_sigaction = SegvHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGSEGV, &action, &g_previousSegvAction) != 0)
    {
        g_writeFaultHandler.store(nullptr, std::memory_order_release);
        return false;
    }
    g_segvHandlerInstalled = true;
    return true;
}

void RemoveWriteFaultHandler()
{
    if (g_segvHandlerInstalled)
    {
        sigaction(SIGSEGV, &g_previousSegvAction, nullptr);
        g_segvHandlerInstalled = false;
    }
    g_writeFaultHandler.store(nullptr, std::memory_order_release);
}

struct RecursiveLock::Impl
{
    std::recursive_mutex mutex;
};

RecursiveLock::RecursiveLock() : impl_(std::make_unique<Impl>())
{
}

RecursiveLock::~RecursiveLock() = default;

void RecursiveLock::lock()
{
    impl_->mutex.lock();
}

void RecursiveLock::unlock()
{
    impl_->mutex.unlock();
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
